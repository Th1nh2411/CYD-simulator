"""Rig studio: the characters' art sets in the browser.

A 2D set (art/<name>/rig.json, tools/rig/rig.py) is made in steps: the painted
character, its cutout, the head rig and the face regions dragged on the
picture itself, one edited copy per expression the firmware uses (SLOTS), and
the conversion (tools/rig/convert.py) with its log. The preview paints a head
tilt at device size from the rig as it is on screen (unsaved) in about a
second, with how much each part follows the head.

A sprite set (art/<name>/sprites.json, tools/sprites/sprites.py) shows the
sheets its characters share, then each character with its own sheets (what
every frame is) and its entry (label, defaults ...); characters can be added
from a copy of another and removed. A sheet uploaded in place of one, or an
edited entry, is converted at once and put back if it does not fit.

usage: python tools/rig/studio.py [--port 8770] [--host 127.0.0.1] [--no-browser]
"""
import argparse
import base64
import io
import json
import mimetypes
import os
import re
import subprocess
import sys
import threading
import webbrowser
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

import cv2
import numpy as np
from PIL import Image

import portrait
import rig
import scene

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "sprites"))
import sprites  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
PAGE = os.path.join(HERE, "studio.html")
SET_NAME = re.compile(r"^[a-z][a-z0-9_]{0,31}$")    # folder art/<name>/
EDIT_NAME = re.compile(r"^[a-z][a-z0-9_]{0,31}$")   # src/<edit>.png
FRAME_NAME = re.compile(r"^[A-Z][A-Za-z0-9]{0,31}$")  # firmware enum k<Name>Frame<frame>
REGIONS = {"eyes", "brows", "mouth"}
IMAGES = {".png", ".jpg", ".jpeg", ".webp"}
SERVED = IMAGES | {".svg"}

# The expressions the 2D character in the firmware shows (kHeroineFrame*), in its
# frame order: name, edited copy, regions it changes, eyes keep their shape, and
# for the page: what it is, what it looks like, what to ask an image model
SLOTS = [
    ("EyesHalf", "eyes_half", "eyes", False, "눈 반쯤 감기", "눈꺼풀이 반쯤 내려온 눈. 깜빡일 때와 졸릴 때",
     "눈만 반쯤 감게"),
    ("EyesClosed", "eyes_closed", "eyes", False, "눈 감기", "눈을 완전히 감은 얼굴. 깜빡일 때와 잘 때", "눈만 감게"),
    ("EyesSmile", "eyes_smile", "eyes brows", False, "눈웃음", "초승달처럼 휘어 웃는 눈. 기쁠 때", "눈웃음 짓게"),
    ("LookLeft", "look_left", "eyes", True, "왼쪽 보기", "눈 모양은 그대로, 눈동자만 그림의 왼쪽으로. 곁눈질",
     "눈동자만 그림의 왼쪽을 보게"),
    ("LookRight", "look_right", "eyes", True, "오른쪽 보기", "눈 모양은 그대로, 눈동자만 그림의 오른쪽으로",
     "눈동자만 그림의 오른쪽을 보게"),
    ("Surprised", "face_surprised", "eyes brows mouth", False, "놀람", "눈을 크게 뜨고 눈썹이 올라가고 입을 조금 벌림",
     "놀란 표정으로"),
    ("Sad", "face_sad", "eyes brows mouth", False, "슬픔", "눈썹이 처지고 입꼬리가 내려감", "슬픈 표정으로"),
    ("Annoyed", "face_annoyed", "eyes brows mouth", False, "뾰로통", "눈을 가늘게 뜨고 눈썹을 찌푸림",
     "뾰로통한 표정으로"),
    ("MouthHalf", "mouth_half", "mouth", False, "입 조금 벌림", "말하는 중간 입 모양", "입만 조금 벌리게"),
    ("MouthOpen", "mouth_open", "mouth", False, "입 벌림", "말할 때와 웃을 때 크게 벌린 입", "입만 벌려 말하는 것처럼"),
    ("MouthSmile", "mouth_smile", "mouth", False, "입 웃음", "입꼬리가 올라간 미소", "입만 미소 짓게"),
    ("MouthO", "mouth_o", "mouth", False, "입 오", "동그랗게 모은 입", "입만 동그랗게 '오' 모양으로"),
    ("MouthPout", "mouth_pout", "mouth", False, "입 삐죽", "삐죽 내민 입", "입만 삐죽 내밀게"),
]


class Bad(Exception):
    """A request the studio refuses; the message is shown on the page."""


def folder_of(name):
    if not name or not SET_NAME.match(name):
        raise Bad(f"세트 이름은 영문 소문자·숫자·_ 입니다: {name!r}")
    return os.path.join(rig.ROOT, "art", name)


def ints(v, n, what):
    if not (isinstance(v, list) and len(v) == n and all(isinstance(x, int) for x in v)):
        raise Bad(f"{what}: 정수 {n}개여야 합니다")
    return v


def check(r):
    """Refuses a rig the tools could not use, with what is wrong. Numbers meant as
    decimals stay decimals (the page's JSON writes 1.0 as 1)."""
    try:
        r["portrait"]["tilt"]["step"] = float(r["portrait"]["tilt"]["step"])
        for period in r.get("backdrop", {}).get("periods", []):
            period["daylight"], period["night"] = float(period["daylight"]), float(period["night"])
        if not FRAME_NAME.match(r["name"]):
            raise Bad("캐릭터 이름은 영문 대문자로 시작합니다 (예: Heroine)")
        face = r["face"]
        ints(face["window"], 4, "얼굴 창")
        for k in REGIONS:
            for e in face["shapes"][k]:
                ints(e, 4, f"{k} 타원")
        edits = set()
        for name, f in r["frames"].items():
            if not FRAME_NAME.match(name):
                raise Bad(f"표정 이름 {name!r}: 영문 대문자로 시작, 영문·숫자만")
            if not EDIT_NAME.match(f["edit"]) or f["edit"] in edits:
                raise Bad(f"표정 {name}: 편집본 이름 {f['edit']!r}이 맞지 않거나 겹칩니다")
            edits.add(f["edit"])
            words = f["regions"].split()
            if not words or not set(words) <= REGIONS:
                raise Bad(f"표정 {name}: 바뀌는 영역은 eyes, brows, mouth 중에서")
            for w in words:
                if not face["shapes"][w]:
                    raise Bad(f"표정 {name}: {w} 타원이 없습니다")
        p = r["portrait"]
        c0, c1 = ints(p["crop"], 2, "자를 행")
        if not 0 <= c0 < c1:
            raise Bad("자를 행: 위가 아래보다 위여야 합니다")
        for k in ("height", "shown", "below", "chest"):
            if not isinstance(p[k], int) or p[k] < 0:
                raise Bad(f"{k}: 0 이상의 정수")
        if p["shown"] + p["below"] > p["height"]:
            raise Bad("화면 행 + 아래 여유가 높이보다 큽니다")
        t = p["tilt"]
        if not isinstance(t["levels"], int) or t["levels"] < 0 or not isinstance(t["grid"], int) or t["grid"] < 2:
            raise Bad("기울임 단계 수와 메시 간격은 정수")
        if not isinstance(t["step"], (int, float)) or t["step"] <= 0:
            raise Bad("기울임 간격(rad)은 0보다 큰 수")
        for k in ("pivot", "neck", "hair"):
            ints(t[k], 2, k)
        if not p["out"].startswith("src/"):
            raise Bad("출력 경로는 src/ 아래")
    except (KeyError, TypeError, AttributeError) as e:
        raise Bad(f"rig.json 형식이 맞지 않습니다: {e}")


def template(name):
    """A new set: the picture's geometry is filled in by the page once the source is in."""
    camel = "".join(w.capitalize() for w in name.split("_"))
    return {"name": camel, "namespace": f"ui::{name}", "source": "",
            "face": {"window": [0, 0, 0, 0], "shapes": {"eyes": [], "brows": [], "mouth": []}},
            "frames": {s[0]: dict({"edit": s[1], "regions": s[2]}, **({"sameEyes": True} if s[3] else {}))
                       for s in SLOTS},
            "portrait": {"crop": [0, 0], "height": 285, "shown": 240, "below": 12,
                         "tilt": {"levels": 3, "step": 0.02, "pivot": [0, 0], "neck": [0, 0], "hair": [0, 0],
                                  "grid": 10},
                         "chest": 0, "out": f"src/app/ui/{name}/{name}_portrait"}}


def file_info(path):
    if not os.path.exists(path):
        return None
    info = {"mtime": int(os.path.getmtime(path) * 1000)}
    if os.path.splitext(path)[1].lower() in IMAGES:
        with Image.open(path) as im:
            info["size"] = list(im.size)
    return info


def state(name):
    folder = folder_of(name)
    r = rig.load(folder)
    used = {f["edit"] for f in r["frames"].values()}
    src = os.path.join(folder, "src")
    unused = sorted(os.path.splitext(n)[0] for n in (os.listdir(src) if os.path.isdir(src) else [])
                    if n.endswith(".png") and os.path.splitext(n)[0] not in used)
    files = {
        "source": file_info(os.path.join(folder, r["source"])) if r["source"] else None,
        "cut": file_info(os.path.join(folder, "base_cut.png")),
        "check": file_info(os.path.join(folder, "base_check.png")),
        "edits": {e: {"src": file_info(os.path.join(src, e + ".png")), "out": file_info(os.path.join(folder, e + ".png"))}
                  for e in sorted(used)},
        "unused": unused,
        "svg": file_info(os.path.join(folder, r["backdrop"]["svg"])) if r.get("backdrop") else None,
    }
    return {"name": name, "rig": {k: v for k, v in r.items() if k not in ("dir", "rel")}, "files": files}


_bases = {}  # (path, mtime, crop) -> premultiplied picture rows, for quick previews


def preview(name, r, level):
    folder = folder_of(name)
    check(r)
    r = dict(r, dir=folder, rel=os.path.relpath(folder, rig.ROOT).replace(os.sep, "/"))
    p = portrait.project(r)
    path = os.path.join(rig.ROOT, p["base"])
    if not os.path.exists(path):
        raise Bad("누끼(base_cut.png)가 아직 없습니다: 먼저 '누끼 만들기'를 하세요")
    key = (path, os.path.getmtime(path), tuple(p["crop"]))
    if key not in _bases:
        _bases.clear()
        _bases[key] = portrait.load(path, p["crop"])
    base = _bases[key]
    scale = p["height"] / base.shape[0]
    size = (round(base.shape[1] * scale), p["height"])
    t = p["tilt"]
    warp = portrait.Warp(base, size, scale, t)
    level = max(-t["levels"], min(t["levels"], int(level)))
    pose = warp.render(base, warp.maps(level * t["step"]))[:p["shown"] + p["below"]]
    h, w = warp.hair.shape
    ys, xs = np.mgrid[0:h, 0:w]
    follow = portrait.head_weight(warp.hair, xs, ys, t)
    shown = cv2.resize(base[..., 3], size, interpolation=cv2.INTER_AREA) / 255  # only where she is
    tint = np.zeros((h, w, 4), np.uint8)
    tint[..., 0], tint[..., 1], tint[..., 2] = 255, 60, 60
    tint[..., 3] = np.clip(follow * shown * 170, 0, 255).astype(np.uint8)
    return {"pose": png_url(pose), "follow": png_url(tint), "level": level, "scale": scale,
            "width": size[0], "height": size[1]}


def room(name, r):
    """The backdrop drawn for each time of day, from the colors as they are on the page."""
    folder = folder_of(name)
    check(r)
    b = scene.project(dict(r, dir=folder, rel=os.path.relpath(folder, rig.ROOT).replace(os.sep, "/")))
    if not b:
        raise Bad("이 세트에는 방 배경이 없습니다")
    try:
        drawing = scene.Scene(os.path.join(rig.ROOT, b["svg"]))
        out = []
        for _, colors, daylight, night in b["periods"]:
            bases = dict(drawing.bases)
            bases.update({k: scene.hex_rgb(v) for k, v in colors.items()})
            rgb = np.clip(np.rint(drawing.render(b["size"], bases, {"Daylight": daylight, "Night": night})), 0, 255)
            alpha = np.full(rgb.shape[:2] + (1,), 255, np.uint8)
            out.append(png_url(np.concatenate([rgb.astype(np.uint8), alpha], axis=2)))
    except (scene.SceneError, OSError) as e:
        raise Bad(f"방: {e}")
    return {"periods": out}


def png_url(rgba):
    buf = io.BytesIO()
    Image.fromarray(rgba, "RGBA").save(buf, "PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


class Job:
    """One step at a time (they are heavy): its title, output lines and exit code."""
    lock = threading.Lock()
    proc, title, lines, code = None, "", [], None

    @classmethod
    def start(cls, title, argv):
        with cls.lock:
            if cls.proc and cls.proc.poll() is None:
                raise Bad(f"'{cls.title}'이(가) 아직 도는 중입니다")
            cls.title, cls.lines, cls.code = title, [], None
            env = dict(os.environ, PYTHONIOENCODING="utf-8")
            cls.proc = subprocess.Popen([sys.executable, "-u", *argv], cwd=rig.ROOT, env=env, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        threading.Thread(target=cls.pump, args=(cls.proc,), daemon=True).start()

    @classmethod
    def pump(cls, proc):
        for line in proc.stdout:
            cls.lines.append(line.rstrip())
        cls.code = proc.wait()

    @classmethod
    def view(cls):
        running = cls.proc is not None and cls.proc.poll() is None
        return {"title": cls.title, "running": running, "lines": cls.lines[-400:],
                "code": None if running else cls.code}


def run(name, what):
    folder = folder_of(name)
    r = rig.load(folder)
    rel = os.path.relpath(folder, rig.ROOT)
    if what == "cutout":
        if not r["source"]:
            raise Bad("원본 그림을 먼저 올리세요")
        argv = [os.path.join(HERE, "cutout.py"), os.path.join(rel, r["source"]),
                "--out", os.path.join(rel, "base_cut.png")]
        options = r.get("cutout", {})
        if options.get("bg"):
            argv += ["--bg", options["bg"]]
        if options.get("bottom"):
            argv.append("--bottom")
        Job.start("누끼", argv)
    elif what in ("convert", "force"):
        Job.start("변환" + (" (전부)" if what == "force" else ""),
                  [os.path.join(HERE, "convert.py"), rel] + (["--force"] if what == "force" else []))
    else:
        raise Bad(f"모르는 작업: {what}")


def kind_of(folder):
    if os.path.exists(os.path.join(folder, "rig.json")):
        return "rig"
    if os.path.exists(os.path.join(folder, "sprites.json")):
        return "sprites"
    return None


def all_sets():
    names = sorted(n for n in os.listdir(os.path.join(rig.ROOT, "art")) if SET_NAME.match(n))
    return [{"name": n, "kind": kind_of(folder_of(n))} for n in names if kind_of(folder_of(n))]


def sprite_state(name):
    folder = folder_of(name)
    try:
        m = sprites.manifest(folder)
        sheets, chars, roles = sprites.expand(m)
    except sprites.SpriteError as e:
        raise Bad(str(e))
    own = {cid: [s for s in sheets[i * len(m["characters"]["sheets"]):(i + 1) * len(m["characters"]["sheets"])]]
           for i, (cid, _, _) in enumerate(chars)} if chars else {}
    return {"name": name, "manifest": m, "shared": m["sheets"], "own": own, "roles": roles,
            "files": {s["png"]: file_info(os.path.join(folder, s["png"])) for s in sheets}}


def apply_manifest(name, m, added=(), removed=()):
    """Writes sprites.json and converts the set; on a failure puts the old one (and files) back."""
    folder = folder_of(name)
    path = os.path.join(folder, "sprites.json")
    with open(path, "rb") as f:
        old = f.read()
    kept = {}
    for png in removed:
        with open(os.path.join(folder, png), "rb") as f:
            kept[png] = f.read()
        os.remove(os.path.join(folder, png))
    sprites.save(folder, m)
    try:
        sprites.convert(folder)
    except sprites.SpriteError as e:
        with open(path, "wb") as f:
            f.write(old)
        for png in added:
            os.remove(os.path.join(folder, png))
        for png, data in kept.items():
            with open(os.path.join(folder, png), "wb") as f:
                f.write(data)
        raise Bad(f"되돌렸습니다. {e}")


MOVES = ("flame", "leaf", "bubble")
COLOR = re.compile(r"^#[0-9a-fA-F]{6}$")


def edit_character(name, cid, entry):
    """New label, preset, move, accent names and defaults of one character."""
    m = sprites.manifest(folder_of(name))
    chars = m["characters"]["list"]
    c = next((c for c in chars if c["id"] == cid), None)
    if c is None:
        raise Bad(f"없는 캐릭터: {cid}")
    preset = str(entry.get("preset", c["preset"]))
    if not EDIT_NAME.match(preset) or any(o["preset"] == preset for o in chars if o is not c):
        raise Bad(f"프리셋 이름 {preset!r}: 영문 소문자로, 다른 캐릭터와 겹치지 않게")
    if entry.get("move", c["move"]) not in MOVES:
        raise Bad("기술 모양은 flame, leaf, bubble 중 하나")
    defaults = dict(c["defaults"], **entry.get("defaults", {}))
    for k, v in defaults.items():
        if k != "name" and not COLOR.match(str(v)):
            raise Bad(f"{k}: #rrggbb 색이어야 합니다")
    names = entry.get("accentNames", c["accentNames"])
    if not (isinstance(names, list) and len(names) == 2 and all(isinstance(n, str) and n for n in names)):
        raise Bad("강조색 이름은 두 개")
    c.update(label=str(entry.get("label", c["label"])), preset=preset, move=entry.get("move", c["move"]),
             accentNames=names, defaults=defaults)
    apply_manifest(name, m)


def add_character(name, cid, label, nickname, template, blank):
    """A new character: the palette and info of another, and either a copy of its
    sheets or empty (transparent) ones of the same layout to draw on."""
    folder = folder_of(name)
    m = sprites.manifest(folder)
    block, chars = m["characters"], m["characters"]["list"]
    if not sprites.CHARACTER_ID.match(cid or "") or any(c["id"] == cid for c in chars):
        raise Bad(f"캐릭터 id {cid!r}: 영문 소문자·숫자·_, 다른 캐릭터와 겹치지 않게")
    src = next((c for c in chars if c["id"] == template), None)
    if src is None:
        raise Bad(f"없는 캐릭터: {template}")
    pal = "".join(w.capitalize() for w in cid.split("_"))
    if pal in m["palettes"]:
        raise Bad(f"팔레트 {pal}가 이미 있습니다")
    m["palettes"][pal] = dict(m["palettes"][src["palette"]])
    preset = cid if all(c["preset"] != cid for c in chars) else cid + "_starter"
    entry = json.loads(json.dumps(src))
    entry.update(id=cid, palette=pal, label=label or cid, preset=preset)
    entry["defaults"]["name"] = nickname or label or cid
    chars.append(entry)
    added = []
    for s in block["sheets"]:
        a, b = s["png"].replace("{id}", template), s["png"].replace("{id}", cid)
        if blank:
            (fw, fh), grid = s["frame"], s["frames"]
            Image.new("RGBA", (fw * max(len(row) for row in grid), fh * len(grid))).save(os.path.join(folder, b))
        else:
            with open(os.path.join(folder, a), "rb") as f:
                data = f.read()
            with open(os.path.join(folder, b), "wb") as f:
                f.write(data)
        added.append(b)
    apply_manifest(name, m, added=added)


def remove_character(name, cid):
    folder = folder_of(name)
    m = sprites.manifest(folder)
    chars = m["characters"]["list"]
    c = next((c for c in chars if c["id"] == cid), None)
    if c is None:
        raise Bad(f"없는 캐릭터: {cid}")
    if len(chars) == 1:
        raise Bad("캐릭터가 하나는 있어야 합니다")
    chars.remove(c)
    if all(o["palette"] != c["palette"] for o in chars) and all(s["palette"] != c["palette"] for s in m["sheets"]):
        del m["palettes"][c["palette"]]
    apply_manifest(name, m, removed=[s["png"].replace("{id}", cid) for s in m["characters"]["sheets"]])


def thumb(name, path, width, face):
    """A small PNG of a set's picture; face: only around the face window of the set's rig."""
    folder = folder_of(name)
    full = os.path.normpath(os.path.join(folder, path))
    if not full.startswith(folder + os.sep) or not os.path.isfile(full):
        raise Bad("없는 그림")
    im = Image.open(full).convert("RGBA")
    if face:
        x0, y0, x1, y1 = rig.load(folder)["face"]["window"]
        if x1 > x0 and y1 > y0:
            pad = (x1 - x0) // 8
            im = im.crop((x0 - pad, y0 - pad, x1 + pad, y1 + pad))
    width = max(16, min(512, width))
    im = im.resize((width, max(1, round(im.height * width / im.width))), Image.LANCZOS)
    buf = io.BytesIO()
    im.save(buf, "PNG")
    return buf.getvalue()


def upload_sheet(name, sheet, data):
    """Puts a sprite sheet in place and converts the set; puts the old one back if it does not fit."""
    folder = folder_of(name)
    m = sprites.manifest(folder)
    if sheet not in {s["png"] for s in sprites.expand(m)[0]}:
        raise Bad(f"이 세트에 없는 시트: {sheet}")
    path = os.path.join(folder, sheet)
    with open(path, "rb") as f:
        old = f.read()
    with open(path, "wb") as f:
        f.write(data)
    try:
        header, count = sprites.convert(folder)
    except sprites.SpriteError as e:
        with open(path, "wb") as f:
            f.write(old)
        raise Bad(f"시트는 그대로 두었습니다. {e}")
    return f"{sheet}를 바꾸고 {header}를 다시 만들었습니다 (프레임 {count}장). 다음 빌드에 들어갑니다."


def upload(name, kind, filename, data, edit=None):
    folder = folder_of(name)
    ext = os.path.splitext(filename or "")[1].lower()
    if ext not in IMAGES:
        raise Bad("그림은 png, jpg, webp만")
    try:
        im = Image.open(io.BytesIO(data))
        im.load()
    except OSError:
        raise Bad("그림 파일을 읽을 수 없습니다")
    if kind == "source":
        target = "base_source" + ext
        with open(os.path.join(folder, target), "wb") as f:
            f.write(data)
        r = rig.load(folder)
        if r["source"] != target:
            r["source"] = target
            rig.save(folder, r)
    elif kind == "edit":
        if not edit or not EDIT_NAME.match(edit):
            raise Bad(f"편집본 이름은 영문 소문자·숫자·_ 입니다: {edit!r}")
        os.makedirs(os.path.join(folder, "src"), exist_ok=True)
        im.convert("RGBA" if "A" in im.getbands() else "RGB").save(os.path.join(folder, "src", edit + ".png"))
    else:
        raise Bad(f"모르는 종류: {kind}")


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def reply(self, code, body, ctype="application/json; charset=utf-8"):
        if not isinstance(body, bytes):
            body = json.dumps(body, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def handle_api(self, method):
        url = urlparse(self.path)
        q = {k: v[0] for k, v in parse_qs(url.query).items()}
        body = self.rfile.read(int(self.headers.get("Content-Length") or 0)) if method == "POST" else b""
        try:
            if method == "GET" and url.path in ("/", "/index.html"):
                with open(PAGE, "rb") as f:
                    return self.reply(200, f.read(), "text/html; charset=utf-8")
            if method == "GET" and url.path == "/api/sets":
                example = "heroine"  # its pictures show what each step wants
                folder = folder_of(example)
                source = rig.load(folder)["source"] if kind_of(folder) == "rig" else ""
                return self.reply(200, {"sets": all_sets(), "example": example, "exampleSource": source})
            if method == "GET" and url.path == "/api/slots":
                keys = ("name", "edit", "regions", "sameEyes", "label", "look", "ask")
                return self.reply(200, {"slots": [dict(zip(keys, s)) for s in SLOTS]})
            if method == "GET" and url.path == "/api/sprites":
                return self.reply(200, sprite_state(q.get("name")))
            if method == "GET" and url.path == "/thumb":
                data = thumb(q.get("set"), q.get("path", ""), int(q.get("w", 160)), q.get("face") == "1")
                return self.reply(200, data, "image/png")
            if method == "GET" and url.path == "/api/set":
                return self.reply(200, state(q.get("name")))
            if method == "GET" and url.path == "/api/job":
                return self.reply(200, Job.view())
            if method == "GET" and url.path == "/file":
                folder = folder_of(q.get("set"))
                path = os.path.normpath(os.path.join(folder, q.get("path", "")))
                if not path.startswith(folder + os.sep) or os.path.splitext(path)[1].lower() not in SERVED \
                        or not os.path.isfile(path):
                    return self.reply(404, {"error": "없는 파일"})
                with open(path, "rb") as f:
                    return self.reply(200, f.read(), mimetypes.guess_type(path)[0] or "application/octet-stream")
            if method == "POST" and url.path == "/api/create":
                folder = folder_of(q.get("name"))
                if os.path.exists(os.path.join(folder, "rig.json")):
                    raise Bad("이미 있는 세트입니다")
                os.makedirs(folder, exist_ok=True)
                rig.save(folder, template(q["name"]))
                return self.reply(200, state(q["name"]))
            if method == "POST" and url.path == "/api/save":
                r = json.loads(body)
                check(r)
                changed = rig.save(folder_of(q.get("set")), r)
                return self.reply(200, {"changed": changed, **state(q["set"])})
            if method == "POST" and url.path == "/api/sprites/character":
                edit_character(q.get("set"), q.get("id"), json.loads(body))
                return self.reply(200, sprite_state(q["set"]))
            if method == "POST" and url.path == "/api/sprites/add":
                req = json.loads(body)
                add_character(q.get("set"), req.get("id"), req.get("label"), req.get("name"), req.get("from"),
                              bool(req.get("blank")))
                return self.reply(200, sprite_state(q["set"]))
            if method == "POST" and url.path == "/api/sprites/remove":
                remove_character(q.get("set"), q.get("id"))
                return self.reply(200, sprite_state(q["set"]))
            if method == "POST" and url.path == "/api/upload" and q.get("kind") == "sheet":
                message = upload_sheet(q.get("set"), q.get("sheet"), body)
                return self.reply(200, {"message": message, **sprite_state(q["set"])})
            if method == "POST" and url.path == "/api/upload":
                upload(q.get("set"), q.get("kind"), q.get("filename"), body, q.get("edit"))
                return self.reply(200, state(q["set"]))
            if method == "POST" and url.path == "/api/preview":
                req = json.loads(body)
                return self.reply(200, preview(q.get("set"), req["rig"], req.get("level", 0)))
            if method == "POST" and url.path == "/api/room":
                return self.reply(200, room(q.get("set"), json.loads(body)))
            if method == "POST" and url.path == "/api/run":
                run(q.get("set"), q.get("what"))
                return self.reply(200, Job.view())
            return self.reply(404, {"error": "없는 주소"})
        except Bad as e:
            return self.reply(400, {"error": str(e)})
        except (ValueError, KeyError) as e:
            return self.reply(400, {"error": f"요청이 맞지 않습니다: {e}"})

    def do_GET(self):
        self.handle_api("GET")

    def do_POST(self):
        self.handle_api("POST")


def main():
    ap = argparse.ArgumentParser(description="2D character rig studio (browser)")
    ap.add_argument("--port", type=int, default=8770)
    ap.add_argument("--host", default="127.0.0.1", help="0.0.0.0 to open it to the network")
    ap.add_argument("--no-browser", action="store_true")
    args = ap.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    url = f"http://localhost:{args.port}"
    print(f"rig studio: {url}", flush=True)
    if not args.no_browser:
        webbrowser.open(url)
    server.serve_forever()


if __name__ == "__main__":
    sys.exit(main())
