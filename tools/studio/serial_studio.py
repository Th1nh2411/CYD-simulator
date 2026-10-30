"""Blob Studio over USB: the device's settings page on this PC, no Wi-Fi.

Serves the same page the firmware serves over Wi-Fi (read from
src/platform/esp32/web_page.h, so there is a single copy) and relays its
/api/* calls to the board over the serial port using the firmware's '@'
line protocol (see handleMachine in src/platform/esp32/main.cpp).

usage: python tools/studio/serial_studio.py [PORT] [--http 8765] [--no-browser]
       then open http://localhost:8765

The serial port is held while this runs: stop it (Ctrl+C) before uploading firmware.
"""
import argparse
import json
import os
import queue
import re
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PAGE_SRC = os.path.join(ROOT, "src", "platform", "esp32", "web_page.h")


def load_page():
    with open(PAGE_SRC, encoding="utf-8") as f:
        match = re.search(r'R"HTML\((.*)\)HTML"', f.read(), re.S)
    if not match:
        raise SystemExit(f"settings page not found in {PAGE_SRC}")
    return match.group(1).replace("<h1>Blob Studio</h1>", "<h1>Blob Studio · USB</h1>")


class Device:
    """Serial link: one '@' request at a time, replies picked out of the stats stream."""

    def __init__(self, port):
        s = serial.Serial()
        s.port, s.baudrate, s.timeout = port, 115200, 0.1
        s.dtr = False  # keep IO0/EN released: opening the port must not reset the board
        s.rts = False
        s.open()
        self.serial = s
        self.replies = queue.Queue()
        self.lock = threading.Lock()
        self.stats = ""
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        buf = b""
        while True:
            buf += self.serial.read(4096)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").rstrip("\r")
                if line.startswith("@"):
                    self.replies.put(line)
                elif line.startswith("fps "):
                    self.stats = line
                elif line.strip():
                    print("device:", line)  # prompt answers, errors, boot messages

    def call(self, command, timeout=3.0):
        with self.lock:
            while not self.replies.empty():
                self.replies.get_nowait()
            self.serial.write((command + "\n").encode("utf-8"))
            try:
                return self.replies.get(timeout=timeout)
            except queue.Empty:
                return None


def make_handler(device, page):
    class Handler(BaseHTTPRequestHandler):
        def _send(self, code, body, content_type="application/json"):
            data = body.encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _relay(self, command):
            reply = device.call(command)
            if reply is None:
                return self._send(504, '{"error":"no reply from device"}')
            if reply.startswith(("@config ", "@meta ", "@net ", "@display ")):
                return self._send(200, reply.split(" ", 1)[1])
            if reply == "@ok":
                return self._send(200, '{"ok":true}')
            return self._send(400, '{"ok":false}')

        def do_GET(self):
            if self.path == "/":
                return self._send(200, page, "text/html; charset=utf-8")
            if self.path == "/api/meta":
                return self._relay("@meta")
            if self.path == "/api/config":
                return self._relay("@config")
            if self.path == "/api/net":
                return self._relay("@net")
            if self.path == "/api/display":
                return self._relay("@display")
            if self.path == "/api/stats":
                return self._send(200, json.dumps({"stats": device.stats}))
            self._send(404, "{}")

        def do_POST(self):
            length = int(self.headers.get("Content-Length") or 0)
            try:
                body = json.loads(self.rfile.read(length) or b"{}")
            except ValueError:
                return self._send(400, '{"error":"json"}')
            compact = json.dumps(body, separators=(",", ":"), ensure_ascii=False)
            commands = {
                "/api/config": lambda: "@merge " + compact,
                "/api/save": lambda: "@save",
                "/api/reset": lambda: "@reset",
                "/api/preset": lambda: "@preset " + str(body.get("name", "")),
                "/api/preview": lambda: "@preview " + str(body.get("expr", "")),
                "/api/card": lambda: "@card " + compact,
                "/api/time": lambda: "@time " + compact,
                "/api/net": lambda: "@net " + compact,
                "/api/display": lambda: "@display " + compact,
            }
            if self.path not in commands:
                return self._send(404, "{}")
            self._relay(commands[self.path]())

        def log_message(self, *args):
            pass

    return Handler


def main():
    parser = argparse.ArgumentParser(description="Blob Studio over USB serial")
    parser.add_argument("port", nargs="?", default="COM3")
    parser.add_argument("--http", type=int, default=8765)
    parser.add_argument("--no-browser", action="store_true")
    args = parser.parse_args()

    device = Device(args.port)
    meta = device.call("@meta")
    if meta is None:
        raise SystemExit(f"no reply on {args.port}: is the board running firmware with the @ protocol?")

    server = ThreadingHTTPServer(("127.0.0.1", args.http), make_handler(device, load_page()))
    url = f"http://localhost:{args.http}"
    print(f"Blob Studio (USB, {args.port}) -> {url}   Ctrl+C to stop")
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
