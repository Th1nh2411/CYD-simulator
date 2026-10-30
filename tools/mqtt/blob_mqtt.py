"""MQTT test kit for the device, standard library only (MQTT 3.1.1, QoS 0/1).

  python tools/mqtt/blob_mqtt.py broker                    small broker on :1883 that prints all traffic
  ... broker --tls-port 8883 --tls-cert server.pem --tls-key server.key [--client-ca ca.pem]
                                                           also TLS 1.2 on :8883; with --client-ca every
                                                           client needs a certificate of that CA (mutual TLS;
                                                           tools/mqtt/make_certs.py makes a test set)
  python tools/mqtt/blob_mqtt.py notify "Title|Body"       notification card
  python tools/mqtt/blob_mqtt.py prompt "Title|Body|Yes|No" prompt; waits for the answer and prints it
  python tools/mqtt/blob_mqtt.py time                      lends the device this PC's clock
  python tools/mqtt/blob_mqtt.py watch                     prints everything the device publishes

Client commands talk to --host (default 127.0.0.1) and find the device by its
retained blob/<id>/status, or use --topic blob/<id>. Topics and payloads are in
src/platform/esp32/network.h. The broker is for tests on a LAN: it accepts any
user and keeps nothing on disk; use Mosquitto or similar for real use.
"""

import argparse
import asyncio
import contextlib
import itertools
import json
import random
import socket
import ssl
import struct
import sys
import time

CONNECT, CONNACK, PUBLISH, PUBACK, SUBSCRIBE, SUBACK, UNSUBSCRIBE, UNSUBACK, PINGREQ, PINGRESP, DISCONNECT = (
    1, 2, 3, 4, 8, 9, 10, 11, 12, 13, 14)


def encode_length(n):
    out = bytearray()
    while True:
        byte, n = n % 128, n // 128
        out.append(byte | (0x80 if n else 0))
        if not n:
            return bytes(out)


def packet(ptype, flags, body=b""):
    return bytes([ptype << 4 | flags]) + encode_length(len(body)) + body


def mqtt_str(s):
    b = s.encode() if isinstance(s, str) else s
    return struct.pack(">H", len(b)) + b


def take_str(body, i):
    n = struct.unpack_from(">H", body, i)[0]
    return body[i + 2:i + 2 + n], i + 2 + n


def publish_packet(topic, payload, qos, retain, pid=0):
    body = mqtt_str(topic) + (struct.pack(">H", pid) if qos else b"") + payload
    return packet(PUBLISH, qos << 1 | int(retain), body)


def parse_publish(flags, body):
    qos, retain = (flags >> 1) & 3, bool(flags & 1)
    topic, i = take_str(body, 0)
    pid = None
    if qos:
        pid = struct.unpack_from(">H", body, i)[0]
        i += 2
    return topic.decode(errors="replace"), body[i:], qos, retain, pid


def matches(filt, topic):
    f, t = filt.split("/"), topic.split("/")
    for i, part in enumerate(f):
        if part == "#":
            return True
        if i >= len(t) or (part != "+" and part != t[i]):
            return False
    return len(f) == len(t)


def show(payload):
    return payload.decode("utf-8", errors="replace")


# ---- Broker ----

class Broker:
    def __init__(self):
        self.sessions = {}  # client id -> Session
        self.retained = {}  # topic -> payload

    def route(self, topic, payload, qos):
        for s in list(self.sessions.values()):
            granted = max((q for f, q in s.subs.items() if matches(f, topic)), default=None)
            if granted is not None:
                s.send_publish(topic, payload, min(qos, granted), False)

    def accept_publish(self, who, topic, payload, qos, retain):
        flags = " retained" if retain else ""
        print(f"{time.strftime('%H:%M:%S')} {who} -> {topic}{flags} {show(payload)}", flush=True)
        if retain:
            if payload:
                self.retained[topic] = payload
            else:
                self.retained.pop(topic, None)
        self.route(topic, payload, qos)


class Session:
    def __init__(self, broker, reader, writer):
        self.broker, self.reader, self.writer = broker, reader, writer
        self.subs, self.id, self.will, self.clean_exit = {}, None, None, False
        self.pids = itertools.cycle(range(1, 65536))

    def send_publish(self, topic, payload, qos, retain):
        self.writer.write(publish_packet(topic, payload, qos, retain, next(self.pids) if qos else 0))

    async def read_packet(self, timeout):
        head = await asyncio.wait_for(self.reader.readexactly(1), timeout)
        length, mult = 0, 1
        while True:
            b = (await self.reader.readexactly(1))[0]
            length += (b & 0x7F) * mult
            mult *= 128
            if not b & 0x80:
                break
        return head[0] >> 4, head[0] & 0x0F, await self.reader.readexactly(length)

    async def run(self):
        peer = self.writer.get_extra_info("peername")
        try:
            ptype, _, body = await self.read_packet(10)
            if ptype != CONNECT:
                return
            keepalive = self.on_connect(body)
            print(f"{time.strftime('%H:%M:%S')} {self.id} connected from {peer[0]}{self.tls_note()}", flush=True)
            timeout = keepalive * 1.5 if keepalive else None
            while True:
                ptype, flags, body = await self.read_packet(timeout)
                if ptype == PUBLISH:
                    topic, payload, qos, retain, pid = parse_publish(flags, body)
                    if qos == 2:
                        return  # not supported: drop the client
                    if qos == 1:
                        self.writer.write(packet(PUBACK, 0, struct.pack(">H", pid)))
                    self.broker.accept_publish(self.id, topic, payload, qos, retain)
                elif ptype == SUBSCRIBE:
                    self.on_subscribe(body)
                elif ptype == UNSUBSCRIBE:
                    pid, i = struct.unpack_from(">H", body)[0], 2
                    while i < len(body):
                        f, i = take_str(body, i)
                        self.subs.pop(f.decode(), None)
                    self.writer.write(packet(UNSUBACK, 0, struct.pack(">H", pid)))
                elif ptype == PINGREQ:
                    self.writer.write(packet(PINGRESP, 0))
                elif ptype == DISCONNECT:
                    self.clean_exit = True
                    return
                await self.writer.drain()
        except (asyncio.IncompleteReadError, asyncio.TimeoutError, ConnectionError, struct.error):
            pass
        finally:
            self.close()

    def tls_note(self):
        """What the TLS handshake told about the client, for the connect line."""
        if self.writer.get_extra_info("ssl_object") is None:
            return ""
        cert = self.writer.get_extra_info("peercert")
        if not cert:
            return " (TLS, no client certificate)"
        names = [v for rdn in cert.get("subject", ()) for k, v in rdn if k == "commonName"]
        return f" (TLS, client certificate {names[0] if names else 'without a name'})"

    def on_connect(self, body):
        _, i = take_str(body, 0)  # protocol name
        flags, keepalive = body[i + 1], struct.unpack_from(">H", body, i + 2)[0]
        client_id, i = take_str(body, i + 4)
        self.id = client_id.decode(errors="replace") or f"anon-{random.randrange(1 << 16):04x}"
        if flags & 0x04:
            will_topic, i = take_str(body, i)
            will_msg, i = take_str(body, i)
            self.will = (will_topic.decode(), will_msg, (flags >> 3) & 3, bool(flags & 0x20))
        old = self.broker.sessions.get(self.id)
        if old:
            old.writer.close()  # same client id: the newer connection wins
        self.broker.sessions[self.id] = self
        self.writer.write(packet(CONNACK, 0, b"\x00\x00"))
        return keepalive

    def on_subscribe(self, body):
        pid, i, granted, new = struct.unpack_from(">H", body)[0], 2, bytearray(), []
        while i < len(body):
            f, i = take_str(body, i)
            qos = min(body[i], 1)
            i += 1
            self.subs[f.decode()] = qos
            granted.append(qos)
            new.append((f.decode(), qos))
            print(f"{time.strftime('%H:%M:%S')} {self.id} subscribed {f.decode()}", flush=True)
        self.writer.write(packet(SUBACK, 0, struct.pack(">H", pid) + bytes(granted)))
        for topic, payload in self.broker.retained.items():
            qos = max((q for f, q in new if matches(f, topic)), default=None)
            if qos is not None:
                self.send_publish(topic, payload, qos, True)

    def close(self):
        if self.broker.sessions.get(self.id) is self:
            del self.broker.sessions[self.id]
            print(f"{time.strftime('%H:%M:%S')} {self.id} {'left' if self.clean_exit else 'dropped'}", flush=True)
            if self.will and not self.clean_exit:
                topic, msg, qos, retain = self.will
                self.broker.accept_publish(f"{self.id} (last will)", topic, msg, min(qos, 1), retain)
        self.writer.close()


def tls_context(cert, key, client_ca):
    """Server side of TLS 1.2 (what the device speaks); with client_ca every client must show a
    certificate that CA signed (mutual TLS)."""
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(cert, key)
    if client_ca:
        ctx.load_verify_locations(client_ca)
        ctx.verify_mode = ssl.CERT_REQUIRED
    return ctx


async def serve(bind, port, tls_port=0, tls=None):
    broker = Broker()
    handler = lambda r, w: Session(broker, r, w).run()
    server = await asyncio.start_server(handler, bind, port)
    servers = [server]
    if tls_port:
        # A failed handshake (no or wrong certificate) is one line here, not a traceback
        def on_error(loop, context):
            exc = context.get("exception")
            if isinstance(exc, (ssl.SSLError, ConnectionError)):
                print(f"{time.strftime('%H:%M:%S')} TLS connection refused: {exc}", flush=True)
            else:
                loop.default_exception_handler(context)
        asyncio.get_running_loop().set_exception_handler(on_error)
        servers.append(await asyncio.start_server(handler, bind, tls_port, ssl=tls_context(*tls),
                                                  ssl_handshake_timeout=10))
    addrs = sorted({a[4][0] for a in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET)})
    print(f"broker on port {port}; this PC: {', '.join(addrs)} (set one as the device's broker address)", flush=True)
    if tls_port:
        print(f"TLS on port {tls_port}, " + ("clients need a certificate of " + tls[2] if tls[2] else "no client certificate needed"),
              flush=True)
    async with contextlib.AsyncExitStack() as stack:
        for s in servers:
            await stack.enter_async_context(s)
        await asyncio.gather(*(s.serve_forever() for s in servers))


# ---- Client ----

class Client:
    def __init__(self, host, port, keepalive=30, user=None, password=None, client_id=None, will=None):
        """will: (topic, payload) the broker publishes at QoS 1 when this connection drops."""
        self.sock = socket.create_connection((host, port), timeout=10)
        self.keepalive, self.pids, self.inbox = keepalive, itertools.cycle(range(1, 65536)), []
        cid = client_id or f"blob-cli-{random.randrange(1 << 24):06x}"
        # Clean session, plus the will (QoS 1, not retained) and login flags when wanted
        flags = 0x02 | (0x0C if will else 0) | (0x80 if user else 0) | (0x40 if user and password else 0)
        body = mqtt_str("MQTT") + bytes([4, flags]) + struct.pack(">H", keepalive) + mqtt_str(cid)
        if will:
            body += mqtt_str(will[0]) + mqtt_str(will[1])
        if user:
            body += mqtt_str(user) + (mqtt_str(password) if password else b"")
        self.sock.sendall(packet(CONNECT, 0, body))
        ptype, _, body = self.read(10)
        if ptype != CONNACK or body[1] != 0:
            code = body[1] if len(body) > 1 else "?"
            why = {4: ": wrong or missing user name / password (--user, --pass)", 5: ": not authorized"}.get(code, "")
            raise SystemExit(f"broker refused the connection (code {code}{why})")
        self.last_send = time.monotonic()

    def send(self, data):
        self.sock.sendall(data)
        self.last_send = time.monotonic()

    def read_exact(self, n):
        data = b""
        while len(data) < n:
            chunk = self.sock.recv(n - len(data))
            if not chunk:
                raise SystemExit("broker closed the connection")
            data += chunk
        return data

    def read(self, timeout):
        """Next packet, or None when nothing arrives within timeout seconds."""
        self.sock.settimeout(max(timeout, 0.01))
        try:
            head = self.read_exact(1)
        except socket.timeout:
            return None
        self.sock.settimeout(10)
        length, mult = 0, 1
        while True:
            b = self.read_exact(1)[0]
            length += (b & 0x7F) * mult
            mult *= 128
            if not b & 0x80:
                break
        return head[0] >> 4, head[0] & 0x0F, self.read_exact(length)

    def wait_for(self, want, timeout=10):
        """Reads until a packet of type want; publications that arrive meanwhile are kept."""
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            p = self.read(end - time.monotonic())
            if p is None:
                break
            if p[0] == want:
                return p
            self.keep(p)
        raise SystemExit("no answer from the broker")

    def keep(self, p):
        if p[0] == PUBLISH:
            topic, payload, qos, retain, pid = parse_publish(p[1], p[2])
            if qos == 1:
                self.send(packet(PUBACK, 0, struct.pack(">H", pid)))
            self.inbox.append((topic, payload, retain))

    def subscribe(self, topic):
        self.send(packet(SUBSCRIBE, 2, struct.pack(">H", next(self.pids)) + mqtt_str(topic) + b"\x01"))
        self.wait_for(SUBACK)

    def publish(self, topic, payload, qos=1):
        pid = next(self.pids)
        self.send(publish_packet(topic, payload, qos, False, pid))
        if qos:
            self.wait_for(PUBACK)

    def messages(self, timeout):
        """Yields (topic, payload, retained) until timeout seconds pass (None: forever)."""
        end = None if timeout is None else time.monotonic() + timeout
        while True:
            while self.inbox:
                yield self.inbox.pop(0)
            left = None if end is None else end - time.monotonic()
            if left is not None and left <= 0:
                return
            ping_in = self.keepalive / 2 - (time.monotonic() - self.last_send)
            if ping_in <= 0:
                self.send(packet(PINGREQ, 0))
                continue
            p = self.read(min(ping_in, left) if left is not None else ping_in)
            if p:
                self.keep(p)


def find_device(client):
    """The device's topic from the retained status messages of devices that are online."""
    client.subscribe("blob/+/status")
    online = {}
    for topic, payload, _ in client.messages(1.5):
        try:
            status = json.loads(payload)
        except ValueError:
            continue
        base = topic.rsplit("/", 1)[0]
        if status.get("online"):
            online[base] = status
        else:
            online.pop(base, None)
    if len(online) != 1:
        found = ", ".join(f"{t} ({s.get('name')})" for t, s in online.items()) or "none"
        raise SystemExit(f"devices online: {found}. Pick one with --topic blob/<id>")
    base, status = next(iter(online.items()))
    print(f"device {status.get('name')} ({status.get('board')}, {status.get('ip')}) at {base}")
    return base


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("command", choices=["broker", "notify", "prompt", "time", "watch"])
    parser.add_argument("text", nargs="?", default="", help="Title|Body[|Option1|Option2|Option3]")
    parser.add_argument("--host", default="127.0.0.1", help="broker address (client commands)")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--bind", default="0.0.0.0", help="broker: address to listen on (127.0.0.1 = this PC only)")
    parser.add_argument("--tls-port", type=int, default=0, help="broker: also listen with TLS on this port (8883)")
    parser.add_argument("--tls-cert", help="broker: server certificate (PEM) for --tls-port")
    parser.add_argument("--tls-key", help="broker: its private key (PEM)")
    parser.add_argument("--client-ca", help="broker: CA (PEM) whose clients are accepted; makes TLS mutual")
    parser.add_argument("--topic", help="device topic, e.g. blob/3294 (default: the one device online)")
    parser.add_argument("--sticky", action="store_true", help="card stays until tapped")
    parser.add_argument("--wait", type=float, default=90, help="prompt: seconds to wait for the answer, 0 = forever")
    args = parser.parse_args()

    if args.command == "broker":
        tls = None
        if args.tls_port:
            if not (args.tls_cert and args.tls_key):
                raise SystemExit("--tls-port needs --tls-cert and --tls-key")
            tls = (args.tls_cert, args.tls_key, args.client_ca)
        try:
            asyncio.run(serve(args.bind, args.port, args.tls_port, tls))
        except KeyboardInterrupt:
            pass
        return

    client = Client(args.host, args.port)
    base = args.topic or find_device(client)
    parts = args.text.split("|")
    if args.command == "watch":
        client.subscribe(base + "/#")
        for topic, payload, retained in client.messages(None):
            print(f"{time.strftime('%H:%M:%S')} {topic}{' (retained)' if retained else ''} {show(payload)}",
                  flush=True)
    elif args.command == "time":
        offset = -(time.altzone if time.localtime().tm_isdst else time.timezone) // 60
        client.publish(base + "/time", json.dumps({"epoch": int(time.time()), "offset": offset}).encode())
        print("sent the time")
    elif args.command == "notify":
        card = {"kind": "notification", "title": parts[0], "body": "|".join(parts[1:]), "sticky": args.sticky}
        client.publish(base + "/card", json.dumps(card, ensure_ascii=False).encode())
        print("sent")
    else:
        if len(parts) < 3:
            raise SystemExit('prompt needs "Title|Body|Option1|Option2"')
        options = parts[2:5]
        ref = random.randrange(1, 1 << 31)
        client.subscribe(base + "/outcome")  # first, so the answer cannot slip past
        card = {"kind": "prompt", "title": parts[0], "body": parts[1], "options": options, "ref": ref,
                "sticky": args.sticky}
        client.publish(base + "/card", json.dumps(card, ensure_ascii=False).encode())
        print(f"sent prompt ref {ref}, waiting for the answer...", flush=True)
        for _, payload, _ in client.messages(args.wait or None):
            try:
                outcome = json.loads(payload)
            except ValueError:
                continue
            if outcome.get("ref") != ref:
                continue
            option = outcome.get("option", -1)
            if option < 0:
                print("expired without an answer")
                sys.exit(2)
            print(f"answered: {option} {options[option] if option < len(options) else '?'}")
            sys.exit(0)
        print("no answer in time")
        sys.exit(3)


if __name__ == "__main__":
    main()
