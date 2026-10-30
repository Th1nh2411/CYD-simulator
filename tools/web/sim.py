"""Screen simulator: the firmware's portable app compiled to WebAssembly.

Builds src/app + the web board/platform with Emscripten (one source tree with
the firmware) and serves the simulated screen next to the device's settings
page. Saving a source file rebuilds and reloads the page; compile errors show
on the page. Only what is drawn matches the device, not timing or load.

usage: python tools/web/sim.py              build, serve http://localhost:8766, watch src/
       python tools/web/sim.py --build      build once (exit code = result)
       python tools/web/sim.py --aibuddy HB-0006   also be that AI Buddy device: this server holds its
                                            MQTT link to AI Buddy's broker (the browser cannot) and
                                            relays the frames to the page, which runs the firmware's
                                            protocol (app/net/aibuddy); log in with the backoffice's OTP
options: --port 8766  --no-browser  --emsdk PATH (default: $EMSDK, or an emsdk
         folder next to the repo or any parent of it)

Output goes to build/web/ (index.html, studio.html, sim.js, sim.wasm).
"""
import argparse
import concurrent.futures
import hashlib
import glob
import hashlib
import json
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools", "rig"))
sys.path.insert(0, os.path.join(ROOT, "tools", "sprites"))
sys.path.insert(0, os.path.join(ROOT, "tools", "mqtt"))
import scene  # noqa: E402  (backdrop SVG -> pre-rendered images)
import sprites  # noqa: E402  (pixel sprite sheets -> tables)
from blob_mqtt import Client  # noqa: E402  (MQTT 3.1.1, standard library only)

OUT = os.path.join(ROOT, "build", "web")
OBJ = os.path.join(OUT, "obj")

# Same split as platformio.ini's src_filter, with the web board and platform
SOURCES = [
    "src/app/**/*.cpp",
    "src/platform/common/*.cpp",
    "src/platform/web/*.cpp",
    "src/drivers/web/*.cpp",
    "src/boards/web/*.cpp",
]
SHELL_PAGE = "src/platform/web/index.html"
SETTINGS_PAGE = "src/platform/esp32/web_page.h"

CXXFLAGS = ["-std=gnu++17", "-O2"]
LDFLAGS = [
    "-O2",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sENVIRONMENT=web",
    "-sEXPORTED_RUNTIME_METHODS=ccall,UTF8ToString",
    "-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$UTF8ToString,$stringToNewUTF8",  # used by EM_JS code
]

# Injected into the settings page: its /api/* calls go to the simulated device
# in the parent page (simApi in index.html) instead of over HTTP
API_SHIM = """<script>
{const deviceFetch = window.fetch.bind(window);
window.fetch = (url, opt = {}) => String(url).startsWith('/api/')
  ? parent.simApi(opt.method || 'GET', String(url), opt.body)
  : deviceFetch(url, opt);}
</script>"""


class BuildError(Exception):
    pass


def find_emxx(emsdk):
    candidates = [emsdk, os.environ.get("EMSDK")]
    d = ROOT
    while True:
        candidates.append(os.path.join(d, "emsdk"))
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    for c in filter(None, candidates):
        emxx = os.path.join(c, "upstream", "emscripten", "em++.py")
        if os.path.isfile(emxx):
            return emxx
    raise BuildError("emsdk not found: install it next to the repo (C:\\Workspace\\emsdk) or pass --emsdk")


def find_arduinojson():
    pattern = os.path.join(ROOT, ".pio", "libdeps", "*", "ArduinoJson", "src")
    found = glob.glob(pattern)
    if not found:
        # Same library version as the firmware: let PlatformIO fetch lib_deps
        print("ArduinoJson not in .pio/libdeps yet, installing with PlatformIO...")
        subprocess.run([sys.executable, "-m", "platformio", "pkg", "install", "-e", "cyd2usb", "-d", ROOT],
                       check=False)
        found = glob.glob(pattern)
    if not found:
        raise BuildError("ArduinoJson not found; run: python -m platformio pkg install -e cyd2usb")
    return found[0]


def read_deps(dep_file):
    """Paths listed in a make-style .d file written by -MMD."""
    with open(dep_file, encoding="utf-8") as f:
        text = f.read().replace("\\\n", " ")
    deps = text.split(": ", 1)[1] if ": " in text else ""
    return [p.replace("\\ ", " ") for p in re.findall(r"(?:\\ |\S)+", deps)]


def stale(obj, dep_file):
    if not (os.path.exists(obj) and os.path.exists(dep_file)):
        return True
    built = os.path.getmtime(obj)
    for p in read_deps(dep_file):
        path = p if os.path.isabs(p) else os.path.join(ROOT, p)
        if not os.path.exists(path) or os.path.getmtime(path) > built:
            return True
    return False


def settings_page():
    with open(os.path.join(ROOT, SETTINGS_PAGE), encoding="utf-8") as f:
        match = re.search(r'R"HTML\((.*)\)HTML"', f.read(), re.S)
    if not match:
        raise BuildError(f"settings page not found in {SETTINGS_PAGE}")
    page = match.group(1)
    for old, new in [('<meta charset="utf-8">', '<meta charset="utf-8">' + API_SHIM),
                     ("<h1>Blob Studio</h1>", "<h1>Blob Studio · Sim</h1>")]:
        if old not in page:
            raise BuildError(f"{SETTINGS_PAGE}: expected {old!r}")
        page = page.replace(old, new, 1)
    return page


def run(cmd):
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, (r.stdout + r.stderr).strip()


def build(emsdk=None):
    """Incremental build into OUT; raises BuildError with the compiler output."""
    t0 = time.time()
    try:  # an edited backdrop (SVG) or sprite sheet (PNG) shows on save like code does
        for out in scene.build_stale():
            print(f"backdrop regenerated: {out}")
        for out in sprites.build_stale():
            print(f"sprites regenerated: {out}")
    except scene.SceneError as e:
        raise BuildError(f"scene: {e}")
    except sprites.SpriteError as e:
        raise BuildError(f"sprites: {e}")
    emxx = [sys.executable, find_emxx(emsdk)]
    flags = CXXFLAGS + ["-Isrc", "-I" + os.path.relpath(find_arduinojson(), ROOT)]
    os.makedirs(OBJ, exist_ok=True)

    # A flag change invalidates every object
    stamp = os.path.join(OBJ, "flags.json")
    signature = json.dumps([flags, LDFLAGS])
    previous = None
    if os.path.exists(stamp):
        with open(stamp, encoding="utf-8") as f:
            previous = f.read()
    if previous != signature:
        for f in glob.glob(os.path.join(OBJ, "*")):
            os.remove(f)
        with open(stamp, "w", encoding="utf-8") as f:
            f.write(signature)

    sources = sorted({os.path.relpath(p, ROOT).replace("\\", "/")
                      for pattern in SOURCES for p in glob.glob(os.path.join(ROOT, pattern), recursive=True)})
    objs, jobs = [], []
    for src in sources:
        obj = os.path.join(OBJ, src.replace("/", "__") + ".o")
        objs.append(obj)
        if stale(obj, obj[:-2] + ".d"):
            jobs.append(emxx + flags + ["-MMD", "-MF", obj[:-2] + ".d", "-c", src, "-o", obj])

    errors = []
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for code, out in pool.map(run, jobs):
            if out:
                print(out)
            if code:
                errors.append(out)
    if errors:
        raise BuildError("\n\n".join(errors))

    wasm = os.path.join(OUT, "sim.wasm")
    if jobs or not os.path.exists(wasm):
        code, out = run(emxx + objs + LDFLAGS + ["-o", os.path.join(OUT, "sim.js")])
        if out:
            print(out)
        if code:
            raise BuildError(out)

    shutil.copyfile(os.path.join(ROOT, SHELL_PAGE), os.path.join(OUT, "index.html"))
    with open(os.path.join(OUT, "studio.html"), "w", encoding="utf-8") as f:
        f.write(settings_page())
    print(f"built {len(jobs)}/{len(sources)} files in {time.time() - t0:.1f}s")


def output_version():
    h = hashlib.md5()
    for name in ("index.html", "studio.html", "sim.js", "sim.wasm"):
        path = os.path.join(OUT, name)
        if os.path.exists(path):
            with open(path, "rb") as f:
                h.update(f.read())
    return h.hexdigest()


class State:
    lock = threading.Lock()
    version = ""
    error = ""


def rebuild(emsdk):
    try:
        build(emsdk)
        error = ""
    except BuildError as e:
        error = str(e)
        print(error)
    with State.lock:
        State.version, State.error = output_version(), error


def snapshot():
    files = {}
    for top in ("src", "art"):  # code, and the art the build converts
        for dirpath, _, names in os.walk(os.path.join(ROOT, top)):
            for n in names:
                p = os.path.join(dirpath, n)
                files[p] = os.path.getmtime(p)
    return files


def watch(emsdk):
    last = snapshot()
    while True:
        time.sleep(0.4)
        now = snapshot()
        if now != last:
            time.sleep(0.2)  # editors may write several files at once
            last = snapshot()
            print("change detected, rebuilding...")
            rebuild(emsdk)


class Bridge:
    """The simulated device's MQTT link to AI Buddy, as the firmware's network.cpp makes it: client id
    and user = number, password = secret key, d/<number>/dn in, /up out, /st status and last will.
    Frames go to the page as server-sent events; the page posts what the device sends."""

    def __init__(self, device, secret, host, port, wifi):
        self.device, self.secret, self.host, self.port, self.wifi = device, secret, host, port, wifi
        self.connected, self.lock, self.listeners, self.out = False, threading.Lock(), [], queue.Queue()

    def info(self):
        return {"device": self.device, "wifiHash": hashlib.sha256(self.wifi.encode()).hexdigest(),
                "connected": self.connected}

    def listen(self):
        q = queue.Queue()
        with self.lock:
            self.listeners.append(q)
        if self.connected:
            q.put({"type": "connected"})
        return q

    def forget(self, q):
        with self.lock:
            self.listeners.remove(q)

    def tell(self, event):
        with self.lock:
            for q in self.listeners:
                q.put(event)

    def run(self):
        d = self.device
        while True:
            try:
                c = Client(self.host, self.port, user=d, password=self.secret, client_id=d,
                           will=(f"d/{d}/st", b'{"isConnected":false}'))
                c.subscribe(f"d/{d}/dn")
                c.publish(f"d/{d}/st", b'{"isConnected":true}')
                self.connected = True
                print(f"aibuddy: connected to {self.host}:{self.port} as {d}")
                self.tell({"type": "connected"})
                while True:  # one thread owns the socket: reads, then sends what the page posted
                    for _topic, payload, _retained in c.messages(0.1):
                        self.tell({"type": "frame", "json": payload.decode("utf-8", "replace")})
                    while not self.out.empty():
                        c.publish(f"d/{d}/up", self.out.get())
            except (OSError, SystemExit) as e:
                if self.connected:
                    self.tell({"type": "disconnected"})
                self.connected = False
                print(f"aibuddy: link down ({e}), retrying in 3 s")
                time.sleep(3)


BRIDGE = None


class Handler(SimpleHTTPRequestHandler):
    # Explicit types: Windows' registry can map .js to text/plain
    extensions_map = {**SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript", ".html": "text/html; charset=utf-8"}

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=OUT, **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_POST(self):
        if self.path != "/__aibuddy/up" or not BRIDGE:
            return self.send_error(404)
        BRIDGE.out.put(self.rfile.read(int(self.headers.get("Content-Length") or 0)))
        self.send_response(204)
        self.end_headers()

    def events(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        q = BRIDGE.listen()
        try:
            while True:
                try:
                    event = q.get(timeout=15)
                except queue.Empty:
                    event = None
                self.wfile.write(f"data: {json.dumps(event)}\n\n".encode() if event else b": keepalive\n\n")
                self.wfile.flush()
        except OSError:
            pass  # the page went away (reload, closed)
        finally:
            BRIDGE.forget(q)

    def do_GET(self):
        path = self.path.split("?")[0]
        if path == "/__aibuddy/events" and BRIDGE:
            return self.events()
        if path == "/__aibuddy":
            body = json.dumps(BRIDGE.info() if BRIDGE else None).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            return self.wfile.write(body)
        if path != "/__build":
            return super().do_GET()
        with State.lock:
            body = json.dumps({"version": State.version, "error": State.error}).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def main():
    parser = argparse.ArgumentParser(description="CYD screen simulator (WebAssembly)")
    parser.add_argument("--build", action="store_true", help="build once and exit")
    parser.add_argument("--port", type=int, default=8766)
    parser.add_argument("--host", default="127.0.0.1", help="0.0.0.0 to open it to other devices on the LAN")
    parser.add_argument("--no-browser", action="store_true")
    parser.add_argument("--emsdk")
    parser.add_argument("--aibuddy", metavar="NUMBER", help="be this AI Buddy device, e.g. HB-0006")
    parser.add_argument("--aibuddy-secret", help="its secret key (default: the dev rule <number>_secret_dev)")
    parser.add_argument("--mqtt-host", default="127.0.0.1", help="AI Buddy's broker (RabbitMQ MQTT)")
    parser.add_argument("--mqtt-port", type=int, default=1883)
    parser.add_argument("--wifi", default="HANBIRO-5G", help="the Wi-Fi name the device says it is on (hashed)")
    args = parser.parse_args()

    if args.build:
        try:
            build(args.emsdk)
        except BuildError as e:
            print(e)
            sys.exit(1)
        return

    global BRIDGE
    if args.aibuddy:
        BRIDGE = Bridge(args.aibuddy, args.aibuddy_secret or f"{args.aibuddy}_secret_dev", args.mqtt_host,
                        args.mqtt_port, args.wifi)
        threading.Thread(target=BRIDGE.run, daemon=True).start()
    rebuild(args.emsdk)
    threading.Thread(target=watch, args=(args.emsdk,), daemon=True).start()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    url = f"http://localhost:{args.port}"
    print(f"simulator -> {url}   (rebuilds on save, Ctrl+C to stop)")
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
