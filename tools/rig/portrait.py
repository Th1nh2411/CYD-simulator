"""Character portrait (painted art) -> poses painted beforehand, for the firmware.

Takes the cut-out base picture (tools/rig/cutout.py) and frames of it with one
part changed (eyes closed, mouth open ...; tools/rig/frames.py) and paints her
in a few head tilts by bending the full-size pictures through a mesh: the head
turns around the neck, the hair down to the shoulders with it, the long hair
below hangs still. Each pose is warped supersampled and shrunk to the device's
size; for each tilt the frames keep only the pixels where they differ from
that pose. Everything is quantized to one palette of 255 colors with their
opacity (k-means; index 0 = transparent) and written as gfx::SpanImage tables;
rows that are the same in every tilt are stored once. The firmware only picks
a tilt and moves whole rows (gfx/span_image), so it never resamples her.

Its settings are the "portrait" of the set's rig.json (tools/rig/rig.py):
  "crop": [top, bottom]   source rows from the top of the hair to below the chest
  "height"                device px for those rows
  "shown"                 of which on screen at rest
  "below"                 rows kept below the screen, for her to rise into
  "tilt"                  head tilts: "levels" each way and "step" radians apart;
                          "pivot" [x, y] at the neck's base, rows over which the
                          head hands over to the body ("neck") and the hair stops
                          following it ("hair"); the warp mesh's "grid" step (device px)
  "chest"                 first row of her chest: above it she rises whole, below it stretches
  "out"                   the generated .h/.cpp, from the repo root, without extension

usage: python tools/rig/portrait.py art/heroine     paint one set's poses
       python tools/rig/portrait.py --all           every set (art/*/rig.json)
tools/rig/convert.py runs it with the other steps. The output is checked in.
"""
import argparse
import os
import sys

import cv2
import numpy as np
from PIL import Image

import rig

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

ALPHA_MIN = 8      # texels less opaque than this (of 255) are transparent
SETTLE = (4, 14)  # frame pixels differing less than the first from the pose become the pose's,
                  # more than the second stay the frame's, smoothly in between
SUPER = 4         # supersampling of the warp
DITHER = 10       # peak-to-peak noise (of 255) added before picking palette colors: soft
                  # gradients (skin) dither between colors instead of banding into contour lines


def smoothstep(e0, e1, v):
    t = np.clip((v - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def load(path, crop):
    """Source picture rows in crop, premultiplied RGBA float."""
    img = np.asarray(Image.open(os.path.join(ROOT, path)).convert("RGBA")).astype(np.float32)[crop[0]:crop[1]]
    a = img[..., 3:4] / 255
    return np.concatenate([img[..., :3] * a, img[..., 3:4]], axis=2)


def head_weight(hair, xs, ys, cfg):
    """How much points (device px) follow the head: wholly above the neck rows,
    and the hair (mask) down to the hair rows."""
    h, w = hair.shape
    hx = hair[np.minimum(ys, h - 1), np.minimum(xs, w - 1)]
    n0, n1 = cfg["neck"]
    h0, h1 = cfg["hair"]
    return np.maximum(1 - smoothstep(n0, n1, ys), hx * (1 - smoothstep(h0, h1, ys)))


class Warp:
    """Grid mesh over the picture (device px), each vertex weighted by how much
    it follows the head; renders the source bent to a head tilt."""

    def __init__(self, base, size, scale, cfg):
        w, h = size
        self.size, self.scale, self.cfg = size, scale, cfg
        small = cv2.resize(base, (w, h), interpolation=cv2.INTER_AREA)
        alpha = small[..., 3] / 255
        rgb = small[..., :3] / np.maximum(alpha[..., None], 1e-3)
        hair = ((rgb.mean(axis=2) < 95) & (alpha > 0.5)).astype(np.float32)
        self.hair = np.clip(cv2.GaussianBlur(hair, (0, 0), 3) * 1.6, 0, 1)
        g = cfg["grid"]
        xs, ys = np.meshgrid(np.arange(0, w + g, g), np.arange(0, h + g, g))
        head = head_weight(self.hair, xs, ys, cfg)
        self.rest = np.stack([xs, ys], axis=2).reshape(-1, 2).astype(np.float32)
        self.head = head.reshape(-1).astype(np.float32)
        cols = xs.shape[1]
        tris = []
        for r in range(xs.shape[0] - 1):
            for c in range(cols - 1):
                a, b, d, e = r * cols + c, r * cols + c + 1, (r + 1) * cols + c + 1, (r + 1) * cols + c
                tris += [(a, b, d), (a, d, e)]
        self.tris = np.array(tris, np.int32)

    def posed(self, roll):
        px, py = self.cfg["pivot"]
        a = roll * self.head
        s, c = np.sin(a), np.cos(a)
        dx, dy = self.rest[:, 0] - px, self.rest[:, 1] - py
        return np.stack([px + dx * c - dy * s, py + dx * s + dy * c], axis=1).astype(np.float32)

    def maps(self, roll):
        """Source px to sample for each supersampled px of the tilted picture."""
        w, h = self.size
        pos = self.posed(roll)
        ids = np.full((h * SUPER, w * SUPER), -1, np.int32)
        inverse = np.zeros((len(self.tris), 2, 3), np.float32)
        for i, t in enumerate(self.tris):
            cv2.fillConvexPoly(ids, np.round(pos[t] * SUPER * 16).astype(np.int32), i, cv2.LINE_8, 4)
            inverse[i] = cv2.getAffineTransform(pos[t], self.rest[t])
        yy, xx = np.mgrid[0:h * SUPER, 0:w * SUPER].astype(np.float32)
        X, Y = (xx + 0.5) / SUPER, (yy + 0.5) / SUPER
        m = inverse[np.maximum(ids, 0)]
        mx = (m[..., 0, 0] * X + m[..., 0, 1] * Y + m[..., 0, 2]) / self.scale - 0.5
        my = (m[..., 1, 0] * X + m[..., 1, 1] * Y + m[..., 1, 2]) / self.scale - 0.5
        mx[ids < 0] = -10
        return mx, my

    def render(self, src, maps):
        out = cv2.remap(src, maps[0], maps[1], cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT, borderValue=0)
        out = cv2.resize(out, self.size, interpolation=cv2.INTER_AREA)
        a = out[..., 3:4]
        rgb = np.where(a > 0, out[..., :3] / np.maximum(a / 255, 1e-6), 0)
        return np.clip(np.rint(np.concatenate([rgb, a], axis=2)), 0, 255).astype(np.uint8)


def settle(pose, frame):
    """Where the frame differs from the pose, faint differences taken out so that
    only the changed features remain and they meet the pose without a step (a
    panel shows even a few levels' step). Returns the settled frame and the
    mask of pixels it changes."""
    b = pose.astype(np.float32)
    f = frame.astype(np.float32)
    diff = np.abs(f - b).max(axis=2)
    w = np.clip((diff - SETTLE[0]) / (SETTLE[1] - SETTLE[0]), 0, 1)[..., None]
    out = np.rint(b + (f - b) * w).astype(np.uint8)
    return out, np.any(out != pose, axis=2)


BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.float32)


def noise(shape, origin):
    """4x4 ordered dither thresholds, -0.5 .. 0.5, fixed to picture px (a frame
    placed at origin dithers exactly like the pose under it). Ordered, not
    noise: a noise's grain shows as stripes on the panel."""
    ys, xs = np.mgrid[0:shape[0], 0:shape[1]]
    return (BAYER4[(ys + origin[1]) % 4, (xs + origin[0]) % 4] + 0.5) / 16 - 0.5


def quantize(images, masks, origins):
    """One palette for all images: 255 RGBA colors by k-means over their visible
    pixels inside the masks (opacity weighs double, so soft edges keep their
    steps), entry 0 transparent, then each pixel's nearest color after a little
    position-fixed noise. Returns the palette (256 x RGBA) and each image's
    texels."""
    weight = np.float32([1, 1, 1, 2])
    picked = [img[m & (img[..., 3] >= ALPHA_MIN)].astype(np.float32) * weight for img, m in zip(images, masks)]
    visible = np.concatenate(picked)
    rng = np.random.default_rng(1)
    sample = visible[rng.choice(len(visible), min(len(visible), 80000), replace=False)]
    criteria = (cv2.TERM_CRITERIA_EPS + cv2.TERM_CRITERIA_MAX_ITER, 30, 0.2)
    _, _, centers = cv2.kmeans(sample, 255, None, criteria, 2, cv2.KMEANS_PP_CENTERS)

    def nearest(px):
        out = np.empty(len(px), np.uint8)
        for i in range(0, len(px), 8192):
            d = ((px[i:i + 8192, None, :] - centers[None, :, :]) ** 2).sum(axis=2)
            out[i:i + 8192] = d.argmin(axis=1) + 1
        return out

    texels = []
    for img, m, origin in zip(images, masks, origins):
        t = np.zeros(img.shape[:2], np.uint8)
        m = m & (img[..., 3] >= ALPHA_MIN)
        jitter = noise(img.shape[:2], origin)[m][:, None] * np.float32([DITHER, DITHER, DITHER, 0])
        t[m] = nearest((img[m].astype(np.float32) + jitter) * weight)
        texels.append(t)
    palette = np.zeros((256, 4), np.float32)
    palette[1:] = centers / weight
    return np.clip(np.rint(palette), 0, 255).astype(int), texels


class Spans:
    """SpanImage rows in one shared byte table, identical rows stored once."""

    def __init__(self):
        self.data, self.at = bytearray(), {}

    def row(self, t):
        spans, x, w = [], 0, len(t)
        while x < w:
            gap = 0
            while x < w and t[x] == 0 and gap < 255:
                x, gap = x + 1, gap + 1
            n = 0
            while x + n < w and t[x + n] != 0 and n < 255:
                n += 1
            spans.append(bytes([gap, n]) + bytes(t[x:x + n]))
            x += n
        while spans and spans[-1][1] == 0:  # trailing gaps need no span
            spans.pop()
        if len(spans) > 255:
            raise SystemExit("portrait: too many spans in a row")
        b = bytes([len(spans)]) + b"".join(spans)
        if b not in self.at:
            self.at[b] = len(self.data)
            self.data += b
        return self.at[b]

    def image(self, t):
        return [self.row(r) for r in t]


def wrap(items, width=110):
    lines, line = [], "   "
    for it in items:
        if len(line) + len(it) + 2 > width:
            lines.append(line)
            line = "   "
        line += " " + it + ","
    lines.append(line)
    return "\n".join(lines) + "\n"


def project(r):
    """portrait.py's settings of a set (rig.load), paths from the repo root."""
    p = dict(r["portrait"], name=r["name"], namespace=r["namespace"])
    p["base"] = f"{r['rel']}/base_cut.png"
    p["frames"] = {name: f"{r['rel']}/{f['edit']}.png" for name, f in r["frames"].items()}
    return p


def convert(p):
    N, cfg = p["name"], p["tilt"]
    rows = p["shown"] + p["below"]
    base = load(p["base"], p["crop"])
    scale = p["height"] / base.shape[0]
    size = (round(base.shape[1] * scale), p["height"])
    warp = Warp(base, size, scale, cfg)
    frames = {name: load(path, p["crop"]) for name, path in p["frames"].items()}
    levels = range(-cfg["levels"], cfg["levels"] + 1)

    images, masks, origins, poses, patches = [], [], [], [], {}
    for lv in levels:
        maps = warp.maps(lv * cfg["step"])
        pose = warp.render(base, maps)[:rows]
        poses.append(len(images))
        images.append(pose)
        masks.append(np.ones(pose.shape[:2], bool))
        origins.append((0, 0))
        for name, src in frames.items():
            out, changed = settle(pose, warp.render(src, maps)[:rows])
            ys, xs = np.nonzero(changed)
            if not len(xs):
                raise SystemExit(f"portrait: frame {name} does not differ from the base")
            if ys.max() >= p["chest"]:
                raise SystemExit(f"portrait: frame {name} reaches below the chest row")
            box = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
            patches[name, lv] = (box, len(images))
            images.append(out[box[1]:box[3], box[0]:box[2]])
            masks.append(changed[box[1]:box[3], box[0]:box[2]])
            origins.append(box[:2])
        print(f"{N}: tilt {lv:+d} painted", flush=True)
    palette, texels = quantize(images, masks, origins)

    spans = Spans()
    pose_rows = [spans.image(texels[i]) for i in poses]
    shared = sum(all(pr[r] == pose_rows[0][r] for pr in pose_rows) for r in range(rows))
    patch_rows = {k: (box, spans.image(texels[i])) for k, (box, i) in patches.items()}
    drawn = np.zeros((rows, size[0]), bool)
    for i in poses:
        drawn |= texels[i] != 0
    ys, xs = np.nonzero(drawn)
    bounds = (int(xs.min()), int(ys.min()), int(xs.max()) + 1 - int(xs.min()), int(ys.max()) + 1 - int(ys.min()))

    count = len(levels)
    h = [f"#pragma once\n\n// Generated by tools/rig/portrait.py from {p['base']}"
         + "".join(f", {v}" for v in p["frames"].values()) + ". Do not edit.\n\n"
         '#include <cstdint>\n\n#include "app/gfx/span_image.h"\n\n'
         f"namespace {p['namespace']} {{\n\n"]
    h.append(f"// Her picture, {size[0]}x{rows} px, painted in {count} head tilts. At rest its top\n"
             f"// {p['shown']} rows are on screen; the rest is for rising into.\n")
    h.append(f"constexpr int k{N}Width = {size[0]}, k{N}Height = {rows}, k{N}ShownHeight = {p['shown']};\n")
    h.append(f"// Tilt levels -k{N}TiltMax .. k{N}TiltMax, k{N}TiltStep radians apart (> 0 toward her left shoulder)\n")
    h.append(f"constexpr int k{N}TiltMax = {cfg['levels']}, k{N}TiltCount = {count};\n")
    h.append(f"constexpr float k{N}TiltStep = {cfg['step']}f;\n")
    h.append(f"// First row of her chest: above it she rises whole, below it the rows stretch\n")
    h.append(f"constexpr int k{N}ChestRow = {p['chest']};\n")
    h.append(f"// Where any pose draws, picture px\n")
    h.append(f"constexpr int k{N}BoundsX = {bounds[0]}, k{N}BoundsY = {bounds[1]}, k{N}BoundsW = {bounds[2]}, "
             f"k{N}BoundsH = {bounds[3]};\n")
    h.append(f"enum {N}Frame : uint8_t {{ " + "".join(f"k{N}Frame{n}, " for n in frames) + f"k{N}FrameCount }};\n")
    h.append(f"extern const uint8_t k{N}PaletteRgb[256 * 3];  // r, g, b: the firmware adjusts them for the panel\n")
    h.append(f"extern const uint8_t k{N}PaletteOpacity[256];  // 0 .. 32, entry 0 transparent\n")
    h.append(f"extern const gfx::SpanImage k{N}Poses[k{N}TiltCount];  // by tilt level + k{N}TiltMax\n")
    h.append("// A frame's changes for one tilt, placed in the picture; drawn over the pose\n")
    h.append("struct PortraitPatch {\n  int16_t x, y;\n  gfx::SpanImage image;\n};\n")
    h.append(f"extern const PortraitPatch k{N}Frames[k{N}FrameCount][k{N}TiltCount];\n")
    h.append(f"\n}}  // namespace {p['namespace']}\n")

    rel = os.path.relpath(os.path.join(ROOT, p["out"]), os.path.join(ROOT, "src")).replace(os.sep, "/")
    c = [f'// Generated by tools/rig/portrait.py. Do not edit.\n#include "{rel}.h"\n\nnamespace {p["namespace"]} {{\n\n']
    c.append("namespace {\n\n")
    c.append(f"constexpr uint8_t kSpans[] = {{\n" + wrap(str(v) for v in spans.data) + "};\n")
    tables = []

    def span_image(tag, w, offsets):
        tables.append(len(offsets))
        c.append(f"constexpr uint32_t k{tag}Rows[] = {{\n" + wrap(str(v) for v in offsets) + "};\n")
        return f"{{{w}, {len(offsets)}, k{tag}Rows, kSpans}}"

    pose_inits = [span_image(f"Pose{i}", size[0], r) for i, r in enumerate(pose_rows)]
    frame_inits = {}
    for (name, lv), (box, r) in patch_rows.items():
        init = span_image(f"{name}{lv + cfg['levels']}", box[2] - box[0], r)
        frame_inits[name, lv] = f"{{{box[0]}, {box[1]}, {init}}}"
    c.append("\n}  // namespace\n\n")
    c.append(f"constexpr uint8_t k{N}PaletteRgb[256 * 3] = {{\n" + wrap(str(v) for v in palette[:, :3].ravel()) + "};\n")
    c.append(f"constexpr uint8_t k{N}PaletteOpacity[256] = {{\n" + wrap(
        str((v * 32 + 127) // 255) for v in palette[:, 3]) + "};\n")
    c.append(f"constexpr gfx::SpanImage k{N}Poses[k{N}TiltCount] = {{\n" + "".join(
        f"    {init},\n" for init in pose_inits) + "};\n")
    c.append(f"constexpr PortraitPatch k{N}Frames[k{N}FrameCount][k{N}TiltCount] = {{\n" + "".join(
        "    {" + ", ".join(frame_inits[name, lv] for lv in levels) + "},\n" for name in frames) + "};\n")
    c.append(f"\n}}  // namespace {p['namespace']}\n")
    out = os.path.join(ROOT, p["out"])
    os.makedirs(os.path.dirname(out), exist_ok=True)  # a new set's character folder
    with open(out + ".h", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(h))
    with open(out + ".cpp", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(c))
    total = len(spans.data) + 4 * sum(tables)
    print(f"{N}: {count} tilts of {size[0]}x{rows}, {shared} rows shared by all, {len(frames)} frames each, "
          f"bounds {bounds}, ~{(total + 1023) // 1024} KB flash")


def main():
    ap = argparse.ArgumentParser(description="portrait art -> pre-rendered pose tables")
    ap.add_argument("set", nargs="*", help="art set folder, e.g. art/heroine")
    ap.add_argument("--all", action="store_true", help="every set of this repo (art/*/rig.json)")
    args = ap.parse_args()
    folders = rig.sets() if args.all else args.set
    if not folders:
        ap.error("name an art set folder or use --all")
    for folder in folders:
        convert(project(rig.load(folder)))


if __name__ == "__main__":
    sys.exit(main())
