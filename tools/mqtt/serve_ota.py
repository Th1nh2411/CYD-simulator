"""HTTPS server for trying the over-the-air update (standard library only; test keys from make_certs.py).

  python tools/mqtt/serve_ota.py --bin .pio/build/jc3248w535/firmware.bin [--port 8443]
         [--cert tools/mqtt/certs/server.pem --key tools/mqtt/certs/server.key] [--client-ca tools/mqtt/certs/ca.pem]
         [--cut BYTES] [--garbage] [--for SECONDS]

Serves the image at https://<this PC>:8443/firmware.bin and prints every request. The device is told with
`ota https://<this PC>:8443/firmware.bin` (serial) or POST /api/ota. For the cases that must fail:
  --client-ca   the device must show a certificate of that CA (mutual TLS, as the MQTT link does)
  --cut BYTES   the connection ends after that many bytes, a download that breaks off
  --garbage     the right size but not a firmware image
"""
import argparse
import http.server
import os
import ssl
import threading
import time


def main():
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "certs")
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--bin", required=True, help="the firmware image to serve (firmware.bin)")
    p.add_argument("--port", type=int, default=8443)
    p.add_argument("--cert", default=os.path.join(here, "server.pem"))
    p.add_argument("--key", default=os.path.join(here, "server.key"))
    p.add_argument("--client-ca", help="require a client certificate of this CA")
    p.add_argument("--cut", type=int, help="close the connection after this many bytes")
    p.add_argument("--garbage", action="store_true", help="serve bytes that are not an image")
    p.add_argument("--for", dest="seconds", type=int, default=0, help="stop by itself after this many seconds")
    args = p.parse_args()
    if args.seconds:
        threading.Timer(args.seconds, lambda: os._exit(0)).start()

    with open(args.bin, "rb") as f:
        image = f.read()
    if args.garbage:
        image = bytes((i * 31 + 7) & 0xFF for i in range(len(image)))

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            who = self.client_address[0]
            stamp = time.strftime("%H:%M:%S")
            if self.path != "/firmware.bin":
                print(f"{stamp} {who} GET {self.path} -> 404", flush=True)
                self.send_error(404)
                return
            print(f"{stamp} {who} GET {self.path} -> {len(image)} bytes" + (f", cut at {args.cut}" if args.cut else ""),
                  flush=True)
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(image)))
            self.end_headers()
            try:
                if args.cut:
                    self.wfile.write(image[:args.cut])
                    self.wfile.flush()
                    self.connection.shutdown(2)
                else:
                    self.wfile.write(image)
                    print(f"{time.strftime('%H:%M:%S')} {who} sent it all", flush=True)
            except (ConnectionError, ssl.SSLError, OSError) as e:
                print(f"{time.strftime('%H:%M:%S')} {who} the transfer broke: {e}", flush=True)

        def log_message(self, *a):
            pass

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.minimum_version = ssl.TLSVersion.TLSv1_2
    ctx.load_cert_chain(args.cert, args.key)
    if args.client_ca:
        ctx.load_verify_locations(args.client_ca)
        ctx.verify_mode = ssl.CERT_REQUIRED

    class Server(http.server.ThreadingHTTPServer):
        def get_request(self):
            sock, addr = self.socket.accept()
            # sendall() takes this as the limit for the whole transfer, not per packet
            sock.settimeout(300)
            return sock, addr

        def finish_request(self, request, client_address):
            try:  # the handshake here, so one that fails is a line, not a traceback
                request = ctx.wrap_socket(request, server_side=True)
            except (ssl.SSLError, OSError) as e:
                print(f"{time.strftime('%H:%M:%S')} {client_address[0]} TLS refused: {e}", flush=True)
                return
            super().finish_request(request, client_address)

    print(f"serving {args.bin} ({len(image)} bytes) on https://0.0.0.0:{args.port}/firmware.bin"
          + (", client certificate required" if args.client_ca else ""), flush=True)
    Server(("0.0.0.0", args.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
