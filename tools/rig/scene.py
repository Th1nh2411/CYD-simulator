"""Backdrop SVG -> pre-rendered palette images (one per time of day) for the firmware.

The backdrop is drawn on the PC, anti-aliased (4x supersampled), once for each
time of day with that hour's colors, quantized to 256 colors and run-length
encoded row by row (gfx::RleImage): the device only copies runs of one color.

SVG: every visible shape (path, circle, ellipse, rect, polygon) is drawn in
document order, group transforms applied.
  <svg viewBox="0 0 320 240" data-colors="wall:#f3e8dc sky:#6cb4f0 ...">   base colors
  part attributes:
    data-c="wall" | "wall #000000 8" | "#rrggbb"   color: a base, a base mixed toward a color
                                                    by t/32, or fixed (default: the fill)
    data-c2="..."                                    second color: vertical gradient, top to bottom
    opacity / fill-opacity                           opacity
    data-alpha="Daylight" | "Night"                  opacity scaled by the hour's value
    data-clip="partId"                               drawn only inside another part

The backdrop is the "backdrop" of an art set's rig.json (tools/rig/rig.py):
"svg" (in the set), "out" (from the repo root, without extension), "name",
"size" [w, h], and "periods": for each time of day its "name", the base
"colors" and the opacity of daytime ("daylight": clouds) and night ("night":
stars, moon) things.

usage: python tools/rig/scene.py art/heroine     draw one set's backdrop
       python tools/rig/scene.py --all           every set with a backdrop (art/*/rig.json)
The output is checked in; the simulator (tools/web/sim.py) and tools/rig/convert.py
regenerate it when the SVG or rig.json changes.
"""
import argparse
import math
import os
import re
import sys
import xml.etree.ElementTree as ET

import cv2
import numpy as np
from PIL import Image

import rig

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SVG = "{http://www.w3.org/2000/svg}"
TOL = 0.1           # curve flattening tolerance, px
SUPER = 4           # supersampling for anti-aliasing
KAPPA = 0.5522847498

class SceneError(Exception):
    pass


# ---- geometry ----

def mat_mul(a, b):
    return (a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1],
            a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5])


IDENT = (1, 0, 0, 1, 0, 0)
NUMBER = r"[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?"


def parse_transform(s):
    m = IDENT
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", s or ""):
        v = [float(x) for x in re.findall(NUMBER, args)]
        if name == "translate":
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0)
        elif name == "scale":
            t = (v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0)
        elif name == "rotate":
            a = math.radians(v[0])
            t = (math.cos(a), math.sin(a), -math.sin(a), math.cos(a), 0, 0)
            if len(v) == 3:
                t = mat_mul(mat_mul((1, 0, 0, 1, v[1], v[2]), t), (1, 0, 0, 1, -v[1], -v[2]))
        elif name == "matrix":
            t = tuple(v)
        else:
            raise SceneError(f"unsupported transform {name}")
        m = mat_mul(m, t)
    return m


def apply(m, p):
    return (m[0] * p[0] + m[2] * p[1] + m[4], m[1] * p[0] + m[3] * p[1] + m[5])


def cubic(p0, c1, c2, p1):
    """Points along a cubic, enough for the tolerance, excluding its end."""
    dd = max(math.hypot(p0[0] - 2 * c1[0] + c2[0], p0[1] - 2 * c1[1] + c2[1]),
             math.hypot(c1[0] - 2 * c2[0] + p1[0], c1[1] - 2 * c2[1] + p1[1]))
    n = max(1, math.ceil(math.sqrt(6 * dd / (8 * TOL))))
    out = []
    for j in range(n):
        t = j / n
        u = 1 - t
        a, b, c, d = u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t
        out.append((a * p0[0] + b * c1[0] + c * c2[0] + d * p1[0], a * p0[1] + b * c1[1] + c * c2[1] + d * p1[1]))
    return out


def path_polygons(d):
    """Closed polygons of SVG path data (M L H V C S Q T Z, absolute and relative)."""
    tokens = re.findall(r"[MmLlHhVvCcSsQqTtZzAa]|" + NUMBER, d)
    polys, pts = [], []
    cur = start = (0.0, 0.0)
    last_ctrl, cmd, i = None, None, 0

    def num():
        nonlocal i
        i += 1
        return float(tokens[i - 1])

    def close():
        nonlocal pts, cur
        if len(pts) >= 3:
            polys.append(pts)
        pts, cur = [], start

    while i < len(tokens):
        t = tokens[i]
        if re.match(r"[A-Za-z]", t):
            cmd = t
            i += 1
            if cmd in "Zz":
                close()
                last_ctrl = None
                continue
        elif cmd is None:
            raise SceneError("path data must start with a command")
        rel, c = cmd.islower(), cmd.upper()
        ox, oy = cur if rel else (0.0, 0.0)
        if c == "M":
            if pts:
                close()
            cur = start = (ox + num(), oy + num())
            pts = [cur]
            cmd, last_ctrl = ("l" if rel else "L"), None
        elif c in "LHV":
            if c == "L":
                p = (ox + num(), oy + num())
            elif c == "H":
                p = ((ox if rel else 0) + num(), cur[1])
            else:
                p = (cur[0], (oy if rel else 0) + num())
            pts.append(p)
            cur, last_ctrl = p, None
        elif c in "CS":
            c1 = (ox + num(), oy + num()) if c == "C" else (
                (2 * cur[0] - last_ctrl[0], 2 * cur[1] - last_ctrl[1]) if last_ctrl else cur)
            c2 = (ox + num(), oy + num())
            p = (ox + num(), oy + num())
            pts += cubic(cur, c1, c2, p)[1:] + [p]
            cur, last_ctrl = p, c2
        elif c in "QT":
            q = (ox + num(), oy + num()) if c == "Q" else (
                (2 * cur[0] - last_ctrl[0], 2 * cur[1] - last_ctrl[1]) if last_ctrl else cur)
            p = (ox + num(), oy + num())
            c1 = (cur[0] + 2 / 3 * (q[0] - cur[0]), cur[1] + 2 / 3 * (q[1] - cur[1]))
            c2 = (p[0] + 2 / 3 * (q[0] - p[0]), p[1] + 2 / 3 * (q[1] - p[1]))
            pts += cubic(cur, c1, c2, p)[1:] + [p]
            cur, last_ctrl = p, q
        else:
            raise SceneError(f"path command {cmd} not supported (no arcs: use curves)")
    close()
    return polys


def ellipse_polygon(cx, cy, rx, ry):
    n = max(12, math.ceil(math.pi * (rx + ry) / 2))
    return [(cx + rx * math.cos(2 * math.pi * j / n), cy + ry * math.sin(2 * math.pi * j / n)) for j in range(n)]


def rect_polygon(x, y, w, h, rx, ry):
    rx, ry = min(rx or ry, w / 2), min(ry or rx, h / 2)
    if rx <= 0:
        return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    pts = []
    for cx, cy, a0 in ((x + w - rx, y + ry, -90), (x + w - rx, y + h - ry, 0), (x + rx, y + h - ry, 90),
                       (x + rx, y + ry, 180)):
        for j in range(7):
            a = math.radians(a0 + 15 * j)
            pts.append((cx + rx * math.cos(a), cy + ry * math.sin(a)))
    return pts


def shape_polygons(el):
    tag = el.tag.replace(SVG, "")
    f = lambda k: float(el.get(k, 0))
    if tag == "path":
        return path_polygons(el.get("d", ""))
    if tag == "circle":
        return [ellipse_polygon(f("cx"), f("cy"), f("r"), f("r"))]
    if tag == "ellipse":
        return [ellipse_polygon(f("cx"), f("cy"), f("rx"), f("ry"))]
    if tag == "rect":
        return [rect_polygon(f("x"), f("y"), f("width"), f("height"), f("rx"), f("ry"))]
    if tag == "polygon":
        v = [float(x) for x in re.findall(NUMBER, el.get("points", ""))]
        return [list(zip(v[0::2], v[1::2]))]
    return None


# ---- colors ----

def hex_rgb(s):
    if not re.fullmatch(r"#[0-9a-fA-F]{6}", s.strip()):
        raise SceneError(f"color must be #rrggbb: {s!r}")
    return np.array([int(s.strip()[i:i + 2], 16) for i in (1, 3, 5)], np.float32)


def attr(el, name):
    style = dict(kv.split(":", 1) for kv in (el.get("style") or "").replace(" ", "").split(";") if ":" in kv)
    return style.get(name, el.get(name))


# ---- the scene ----

class Scene:
    """The shapes of a backdrop SVG, in drawing order."""

    def __init__(self, path):
        self.path = path
        root = ET.parse(path).getroot()
        vb = [float(v) for v in root.get("viewBox", "0 0 320 240").split()]
        self.width, self.height = vb[2], vb[3]
        self.bases = {}
        for item in (root.get("data-colors") or "").split():
            k, v = item.split(":")
            self.bases[k] = hex_rgb(v)
        self.parts = []
        self.walk(root, IDENT)

    def walk(self, el, m):
        for child in el:
            if attr(child, "display") == "none":
                continue
            cm = mat_mul(m, parse_transform(child.get("transform")))
            if child.tag == SVG + "g":
                self.walk(child, cm)
                continue
            polys = shape_polygons(child)
            if polys is None or (attr(child, "fill") == "none" and not child.get("data-c")):
                continue
            fill = attr(child, "fill")
            self.parts.append({
                "id": child.get("id"),
                "polys": [[apply(cm, p) for p in poly] for poly in polys],
                "color": child.get("data-c") or fill or "#000000",
                "color2": child.get("data-c2"),
                "opacity": float(attr(child, "opacity") or 1) * float(attr(child, "fill-opacity") or 1),
                "alpha": child.get("data-alpha"),
                "clip": child.get("data-clip"),
            })

    def color(self, spec, bases):
        """A color spec for these base colors: base, base mixed toward #rrggbb by t/32, or #rrggbb."""
        words = spec.split()
        if words[0].startswith("#"):
            return hex_rgb(words[0])
        if words[0] not in bases:
            raise SceneError(f"{os.path.basename(self.path)}: unknown base color {words[0]}")
        c = bases[words[0]]
        if len(words) >= 3:
            c = c + (hex_rgb(words[1]) - c) * (int(words[2]) / 32)
        return c

    def render(self, size, bases, params):
        """RGB image of the scene at size (w, h) px, anti-aliased."""
        w, h = size
        sx, sy = w * SUPER / self.width, h * SUPER / self.height
        canvas = np.zeros((h * SUPER, w * SUPER, 3), np.float32)
        coverage = {}
        for part in self.parts:
            mask = np.zeros(canvas.shape[:2], np.uint8)
            polys = [np.round(np.array(poly) * (sx, sy) * 16).astype(np.int32) for poly in part["polys"]]
            cv2.fillPoly(mask, polys, 255, lineType=cv2.LINE_8, shift=4)
            cov = mask.astype(np.float32) / 255
            if part["clip"]:
                if part["clip"] not in coverage:
                    raise SceneError(f"{os.path.basename(self.path)}: clip part {part['clip']} must come first")
                cov *= coverage[part["clip"]]
            if part["id"]:
                coverage[part["id"]] = cov
            a = part["opacity"] * (params[part["alpha"]] if part["alpha"] else 1)
            if a <= 0 or not mask.any():
                continue
            top = self.color(part["color"], bases)
            if part["color2"]:
                ys = np.nonzero(mask.any(axis=1))[0]
                t = np.clip((np.arange(canvas.shape[0]) - ys[0]) / max(1, ys[-1] - ys[0]), 0, 1)[:, None, None]
                color = top + (self.color(part["color2"], bases) - top) * t
            else:
                color = top
            k = (cov * a)[..., None]
            canvas = canvas * (1 - k) + color * k
        return cv2.resize(canvas, size, interpolation=cv2.INTER_AREA)


# ---- output ----

def wrap(items, width=110):
    lines, line = [], "   "
    for it in items:
        if len(line) + len(it) + 2 > width:
            lines.append(line)
            line = "   "
        line += " " + it + ","
    lines.append(line)
    return "\n".join(lines) + "\n"


def rle(img):
    """Palette (256 RGB) and per-row runs (count 1..255, index) of an RGB image."""
    q = Image.fromarray(np.clip(img + 0.5, 0, 255).astype(np.uint8)).quantize(256, method=Image.Quantize.FASTOCTREE)
    idx = np.asarray(q)
    rows, data = [], []
    for row in idx:
        rows.append(len(data))
        x = 0
        while x < len(row):
            n = 1
            while x + n < len(row) and n < 255 and row[x + n] == row[x]:
                n += 1
            data += [n, int(row[x])]
            x += n
    if len(data) > 65535:
        raise SceneError("a backdrop's runs exceed 64 KB; simplify it")
    return q.getpalette()[:768], rows, data


def project(r):
    """scene.py's settings of a set (rig.load), paths from the repo root; None without a backdrop."""
    b = r.get("backdrop")
    if b is None:
        return None
    periods = [(t["name"], t["colors"], t["daylight"], t["night"]) for t in b["periods"]]
    return dict(b, svg=f"{r['rel']}/{b['svg']}", namespace=r["namespace"], periods=periods,
                rig=os.path.join(r["dir"], "rig.json"))


def projects():
    return [p for p in (project(rig.load(folder)) for folder in rig.sets()) if p]


def convert(p):
    scene = Scene(os.path.join(ROOT, p["svg"]))
    N, ns = p["name"], p["namespace"]
    periods = p["periods"]
    h = [f"#pragma once\n\n// Generated by tools/rig/scene.py from {p['svg']}. Do not edit.\n\n"
         '#include <cstdint>\n\n#include "app/gfx/rle_image.h"\n\n'
         f"namespace {ns} {{\n\n"
         f"// The backdrop drawn for each time of day, {p['size'][0]}x{p['size'][1]} px\n"
         f"enum {N}Time : uint8_t {{ " + "".join(f"k{N}{name}, " for name, *_ in periods) + f"k{N}TimeCount }};\n"
         f"extern const gfx::RleImage k{N}[k{N}TimeCount];\n"
         f"extern const gfx::Color k{N}Wall[k{N}TimeCount];  // its main color\n"
         f"\n}}  // namespace {ns}\n"]
    rel = os.path.relpath(os.path.join(ROOT, p["out"]), os.path.join(ROOT, "src")).replace(os.sep, "/")
    c = [f'// Generated by tools/rig/scene.py. Do not edit.\n#include "{rel}.h"\n\nnamespace {ns} {{\n\nnamespace {{\n\n']
    inits, walls, total = [], [], 0
    for name, colors, daylight, night in periods:
        bases = dict(scene.bases)
        bases.update({k: hex_rgb(v) for k, v in colors.items()})
        img = scene.render(p["size"], bases, {"Daylight": daylight, "Night": night})
        pal, rows, data = rle(img)
        total += len(data) + 2 * len(rows) + 512
        c.append(f"constexpr gfx::Color k{N}{name}Palette[256] = {{\n" + wrap(
            f"gfx::rgb({pal[3 * i]}, {pal[3 * i + 1]}, {pal[3 * i + 2]})" for i in range(256)) + "};\n")
        c.append(f"constexpr uint16_t k{N}{name}Rows[] = {{\n" + wrap(str(v) for v in rows) + "};\n")
        c.append(f"constexpr uint8_t k{N}{name}Runs[] = {{\n" + wrap(str(v) for v in data) + "};\n")
        inits.append(f"{{{p['size'][0]}, {p['size'][1]}, k{N}{name}Rows, k{N}{name}Runs, k{N}{name}Palette}}")
        wall = bases.get("wall", img[img.shape[0] // 2, 0])
        walls.append("gfx::rgb({}, {}, {})".format(*(int(v + 0.5) for v in wall)))
    c.append("\n}  // namespace\n\n")
    c.append(f"constexpr gfx::RleImage k{N}[k{N}TimeCount] = {{\n" + "".join(f"    {i},\n" for i in inits) + "};\n")
    c.append(f"constexpr gfx::Color k{N}Wall[k{N}TimeCount] = {{" + ", ".join(walls) + "};\n")
    c.append(f"\n}}  // namespace {ns}\n")
    out = os.path.join(ROOT, p["out"])
    with open(out + ".h", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(h))
    with open(out + ".cpp", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(c))
    print(f"{N}: {len(scene.parts)} shapes, {len(periods)} times of day, ~{total / 1024:.0f} KB flash")


def build_stale():
    """Regenerates backdrops whose SVG or rig.json (or this script) changed; returns their outputs."""
    done = []
    for p in projects():
        target = os.path.join(ROOT, p["out"] + ".cpp")
        newest = max(os.path.getmtime(f) for f in (os.path.join(ROOT, p["svg"]), p["rig"], os.path.abspath(__file__)))
        if os.path.exists(target) and newest <= os.path.getmtime(target):
            continue
        try:
            convert(p)
        except ET.ParseError as e:
            raise SceneError(f"{p['svg']}: {e}")
        done.append(p["out"])
    return done


def main():
    ap = argparse.ArgumentParser(description="backdrop SVG -> pre-rendered palette images")
    ap.add_argument("set", nargs="*", help="art set folder, e.g. art/heroine")
    ap.add_argument("--all", action="store_true", help="every set of this repo with a backdrop")
    args = ap.parse_args()
    if not args.all and not args.set:
        ap.error("name an art set folder or use --all")
    try:
        for p in projects() if args.all else [project(rig.load(folder)) for folder in args.set]:
            if p is None:
                raise SceneError("the set's rig.json has no backdrop")
            convert(p)
    except (SceneError, ET.ParseError) as e:
        print(f"scene: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
