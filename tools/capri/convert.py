"""Capri's pictures (art/capri/) -> constexpr tables for the firmware.

Her figure is her intro close-up (intro-*.webp from mochiiwuff/capri-desktop):
her holding the screen by its edges, drawn as cels that differ only where her
mouth, her eyes or her right arm change (grab = at rest). The device swaps the
cels (talking, happy, waving, a swing of the arm) and moves the picture whole:
it bobs, its mane sways row by row and glitches into slices and afterimages.
Each cel is scaled to a few widths (the device picks the widest that fits) and
stored as a gfx::SpanImage; all share one palette of 255 colors with their
opacity (anti-aliased edges), and rows that are the same in several cels are
stored once. For each cel the rows where it differs from grab are listed, so
that a swap redraws only those.

The portraits in her message panel (face-*.png, capri-face.png as "grin") and
the comic cut-ins that flash beside her (cutin-*.png) are opaque: quantized to
256 colors each and run-length encoded (gfx::RleImage). Her greeting clips
(voice/*.wav) become 16 kHz samples for the synth (capri_voice.*).

usage: python3 tools/capri/convert.py        (Pillow + numpy; the output is checked in)
"""
import os
import wave

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ART = os.path.join(ROOT, "art", "capri")
OUT = os.path.join(ROOT, "src", "app", "ui", "capri", "capri_art")
NS = "ui::capri"

CELS = ["grab", "happy", "mouth1", "mouth2", "mouth3", "smear", "wave1", "wave2", "wave3"]  # intro-<name>.webp
WIDTHS = (480, 320, 240)  # figure sizes, widest first
NECK = 0.40               # her mane sways above this part of the picture's height
# Where each kind of cel changes her, in source px (capri-desktop's tools/build-intro.py CHANGES): only
# that is taken from it, the rest from grab, so the lossy WebP noise elsewhere does not swap with it
CHANGES = {"mouth": (690, 360, 1020, 650), "wave": (930, 120, 1672, 941), "smear": (930, 120, 1672, 941),
           "happy": (600, 180, 1060, 470)}
FACE = 64                 # panel portrait, px square
FACES = [("Calm", "face-calm"), ("Aside", "face-aside"), ("Smug", "face-smug"), ("Wide", "face-wide"),
         ("Look", "face-look"), ("Laugh", "face-laugh"), ("Many", "face-many"), ("Grin", "capri-face")]
CUTIN = (192, 128)
CUTINS = [("Grin", "cutin-grin"), ("Laugh", "cutin-laugh"), ("Eyes", "cutin-eyes"), ("Many", "cutin-many"),
          ("Look", "cutin-look")]
CLIPS = ["hello-morning", "hello-late", "hello-miss"]
RATE = 16000  # audio::kSampleRate
LEVELS = np.array([10, 18, 26, 32])  # opacity of the four alpha steps, of 32
BUDGET = {3: 191, 2: 24, 1: 24, 0: 15}  # palette entries per step (+ entry 0, transparent)


def wrap(items, width=110):
    lines, line = [], "   "
    for it in items:
        if len(line) + len(it) + 2 > width:
            lines.append(line)
            line = "   "
        line += " " + it + ","
    lines.append(line)
    return "\n".join(lines) + "\n"


def resize(img, size):
    # Premultiplied so that transparent pixels do not bleed their color into the edges
    a = np.asarray(img.convert("RGBA")).astype(np.float32) / 255
    pm = np.dstack([a[..., :3] * a[..., 3:], a[..., 3:]])
    chans = [np.asarray(Image.fromarray(pm[..., k]).resize(size, Image.LANCZOS)) for k in range(4)]
    out = np.clip(np.dstack(chans), 0, 1)
    alpha = out[..., 3:]
    rgb = np.where(alpha > 1e-3, out[..., :3] / np.maximum(alpha, 1e-3), 0)
    return np.clip(np.dstack([rgb, alpha]) * 255 + 0.5, 0, 255).astype(np.uint8)


def steps_of(rgba):
    alpha = (rgba[..., 3].astype(np.int32) * 32 + 127) // 255  # the device's 0 .. 32
    return alpha > 0, np.minimum(3, (alpha * 4) // 33)


def palette(cels):
    """One palette for every cel: per opacity step, median cut over the shown pixels of all cels."""
    pal = [(0, 0, 0, 0)]
    for s in (3, 2, 1, 0):
        px = []
        for rgba in cels:
            shown, steps = steps_of(rgba)
            px.append(rgba[shown & (steps == s)][:, :3])
        px = np.concatenate(px)
        if not len(px):
            continue
        n = min(BUDGET[s], len(np.unique(px, axis=0)))
        q = Image.fromarray(px.reshape(-1, 1, 3)).quantize(n, method=Image.MEDIANCUT, dither=Image.NONE)
        pal += [(int(r), int(g), int(b), int(LEVELS[s])) for r, g, b in
                np.array(q.getpalette()[:3 * n]).reshape(-1, 3)]
    return pal + [(0, 0, 0, 0)] * (256 - len(pal))


def texels(rgba, pal):
    """Each shown pixel as the nearest palette color of its opacity step."""
    ref = np.array(pal, np.int32)
    shown, steps = steps_of(rgba)
    out = np.zeros(steps.shape, np.uint8)
    for s in range(4):
        m = shown & (steps == s)
        cand = np.nonzero(ref[:, 3] == LEVELS[s])[0]
        if not m.any() or not len(cand):
            continue
        px = rgba[m][:, :3].astype(np.int32)
        best = np.zeros(len(px), np.int64)
        for i in range(0, len(px), 8192):  # in chunks: pixels x candidates distances
            d = ((px[i:i + 8192, None, :] - ref[None, cand, :3]) ** 2).sum(2)
            best[i:i + 8192] = cand[d.argmin(1)]
        out[m] = best
    return out


def opaque(name, size):
    img = Image.open(os.path.join(ART, name + ".png")).convert("RGB").resize(size, Image.LANCZOS)
    q = img.quantize(256, method=Image.MEDIANCUT, dither=Image.NONE)
    colors = np.array(q.getpalette()[:768] + [0] * 768)[:768].reshape(-1, 3)
    return np.asarray(q), colors


def span_rows(texels, data, at):
    rows = []
    for t in texels:
        spans, x, w = [], 0, len(t)
        while x < w:
            gap = 0
            while x < w and t[x] == 0 and gap < 255:
                x, gap = x + 1, gap + 1
            n = 0
            while x + n < w and t[x + n] != 0 and n < 255:
                n += 1
            spans.append(bytes([gap, n]) + bytes(t[x:x + n].tolist()))
            x += n
        while spans and spans[-1][1] == 0:
            spans.pop()
        b = bytes([len(spans)]) + b"".join(spans)
        if b not in at:
            at[b] = len(data)
            data += b
        rows.append(at[b])
    return rows


def rle(texels):
    rows, runs = [], []
    for t in texels:
        rows.append(len(runs) // 2)
        x = 0
        while x < len(t):
            n = 1
            while x + n < len(t) and t[x + n] == t[x] and n < 255:
                n += 1
            runs += [n, int(t[x])]
            x += n
    return rows, runs


def color565(r, g, b):
    return f"gfx::rgb({r}, {g}, {b})"


def main():
    h = ["#pragma once\n\n// Generated by tools/capri/convert.py from art/capri/. Do not edit.\n\n"
         "#include <cstdint>\n\n#include \"app/gfx/rle_image.h\"\n#include \"app/gfx/span_image.h\"\n\n"
         f"namespace {NS} {{\n\n"]
    c = ["// Generated by tools/capri/convert.py. Do not edit.\n#include \"app/ui/capri/capri_art.h\"\n\n"
         f"namespace {NS} {{\n\nnamespace {{\n\n"]
    total = 0

    # Figure: every cel at each width, one palette, rows shared wherever they are the same
    src = [Image.open(os.path.join(ART, f"intro-{name}.webp")).convert("RGBA") for name in CELS]
    for i, name in enumerate(CELS[1:], 1):
        box = next(b for kind, b in CHANGES.items() if name.startswith(kind))
        settled = src[0].copy()
        settled.paste(src[i].crop(box), box[:2])
        src[i] = settled
    sizes = [(w, round(src[0].height * w / src[0].width)) for w in WIDTHS]
    scaled = [[resize(im, size) for im in src] for size in sizes]
    pal = palette(scaled[0])
    data, at, inits, changed = bytearray(), {}, [], []
    for si, (size, cels) in enumerate(zip(sizes, scaled)):
        tex = [texels(rgba, pal) for rgba in cels]
        row_inits, ranges = [], []
        for ci, t in enumerate(tex):
            rows = span_rows(t, data, at)
            c.append(f"constexpr uint32_t kFigure{si}{CELS[ci].title()}Rows[] = {{\n" + wrap(str(v) for v in rows) +
                     "};\n")
            total += 4 * len(rows)
            row_inits.append(f"{{{size[0]}, {size[1]}, kFigure{si}{CELS[ci].title()}Rows, kFigureSpans}}")
            diff = np.nonzero((t != tex[0]).any(1))[0]
            ranges.append((int(diff.min()), int(diff.max()) + 1) if len(diff) else (0, 0))
        inits.append(row_inits)
        changed.append(ranges)
        print(f"capri: figure {size[0]}x{size[1]}, cels change rows {ranges}", flush=True)
    c.insert(3, "constexpr uint8_t kFigureSpans[] = {\n" + wrap(str(v) for v in data) + "};\n")
    total += len(data)

    # Faces and cut-ins
    def rle_set(tag, items, size):
        nonlocal total
        out = []
        for name, file in items:
            texels_, colors = opaque(file, size)
            rows, runs = rle(texels_)
            c.append(f"constexpr uint16_t k{tag}{name}Rows[] = {{\n" + wrap(str(v) for v in rows) + "};\n")
            c.append(f"constexpr uint8_t k{tag}{name}Runs[] = {{\n" + wrap(str(v) for v in runs) + "};\n")
            c.append(f"constexpr gfx::Color k{tag}{name}Palette[256] = {{\n" +
                     wrap(color565(*map(int, rgb)) for rgb in colors) + "};\n")
            out.append(f"{{{size[0]}, {size[1]}, k{tag}{name}Rows, k{tag}{name}Runs, k{tag}{name}Palette}}")
            total += 2 * len(rows) + len(runs) + 512
        return out

    face_inits = rle_set("Face", FACES, (FACE, FACE))
    cutin_inits = rle_set("Cutin", CUTINS, CUTIN)
    c.append("\n}  // namespace\n\n")

    h.append(f"// Her figure: {len(CELS)} cels of her intro close-up at {len(WIDTHS)} sizes, widest first; all\n"
             "// share one palette. Rows above kFigureNeck[size] are her head and mane.\n")
    h.append(f"constexpr int kFigureSizes = {len(WIDTHS)};\n")
    h.append("enum Cel : uint8_t { " + "".join(f"kCel{n.title()}, " for n in CELS) + "kCelCount };\n")
    h.append("constexpr int kFigureNeck[kFigureSizes] = {" + ", ".join(str(round(NECK * hh)) for _, hh in sizes) +
             "};\n")
    h.append("extern const uint8_t kFigurePaletteRgb[256 * 3];  // r, g, b\n")
    h.append("extern const uint8_t kFigurePaletteOpacity[256];  // 0 .. 32, entry 0 transparent\n")
    h.append("extern const gfx::SpanImage kFigure[kFigureSizes][kCelCount];\n")
    h.append("// Rows [first, end) where each cel differs from kCelGrab ({0, 0}: the same)\n")
    h.append("struct CelRows {\n  int16_t first, end;\n};\n")
    h.append("extern const CelRows kCelChanged[kFigureSizes][kCelCount];\n\n")
    h.append("// Her face in the message panel, and the close-ups that flash beside her\n")
    h.append("enum Face : uint8_t { " + "".join(f"kFace{n}, " for n, _ in FACES) + "kFaceCount };\n")
    h.append("enum Cutin : uint8_t { " + "".join(f"kCutin{n}, " for n, _ in CUTINS) + "kCutinCount };\n")
    h.append(f"constexpr int kFaceSize = {FACE}, kCutinW = {CUTIN[0]}, kCutinH = {CUTIN[1]};\n")
    h.append("extern const gfx::RleImage kFaces[kFaceCount];\n")
    h.append("extern const gfx::RleImage kCutins[kCutinCount];\n")
    h.append(f"\n}}  // namespace {NS}\n")

    c.append("constexpr uint8_t kFigurePaletteRgb[256 * 3] = {\n" +
             wrap(str(v) for e in pal for v in e[:3]) + "};\n")
    c.append("constexpr uint8_t kFigurePaletteOpacity[256] = {\n" + wrap(str(e[3]) for e in pal) + "};\n")
    c.append("constexpr gfx::SpanImage kFigure[kFigureSizes][kCelCount] = {\n" + "".join(
        "    {" + ", ".join(row) + "},\n" for row in inits) + "};\n")
    c.append("constexpr CelRows kCelChanged[kFigureSizes][kCelCount] = {\n" + "".join(
        "    {" + ", ".join(f"{{{a}, {b}}}" for a, b in r) + "},\n" for r in changed) + "};\n")
    c.append("constexpr gfx::RleImage kFaces[kFaceCount] = {\n" + "".join(f"    {x},\n" for x in face_inits) + "};\n")
    c.append("constexpr gfx::RleImage kCutins[kCutinCount] = {\n" + "".join(f"    {x},\n" for x in cutin_inits) +
             "};\n")
    c.append(f"\n}}  // namespace {NS}\n")

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT + ".h", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(h))
    with open(OUT + ".cpp", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(c))
    print(f"capri: {len(CELS)} cels x {sizes}, {len(FACES)} faces {FACE}px, {len(CUTINS)} cut-ins {CUTIN}, "
          f"~{(total + 1023) // 1024} KB flash")
    clips()


def clips():
    """Her greeting clips (voice/hello-*.wav) at the synth's rate, 16-bit (audio::Clip)."""
    h = ["#pragma once\n\n// Generated by tools/capri/convert.py from art/capri/voice/. Do not edit.\n\n"
         "#include \"app/audio/synth.h\"\n\nnamespace ui::capri {\n\n"
         "enum VoiceClip : uint8_t { " + "".join(f"kClip{''.join(p.title() for p in n.split('-'))}, " for n in CLIPS)
         + "kClipCount };\n"
         "extern const audio::Clip kClips[kClipCount];\n\n}  // namespace ui::capri\n"]
    c = ["// Generated by tools/capri/convert.py. Do not edit.\n#include \"app/ui/capri/capri_voice.h\"\n\n"
         "namespace ui::capri {\n\nnamespace {\n\n"]
    total = 0
    for name in CLIPS:
        with wave.open(os.path.join(ART, "voice", name + ".wav")) as w:
            assert w.getsampwidth() == 2 and w.getnchannels() == 1
            rate = w.getframerate()
            pcm = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float64)
        # Box-filtered down to the synth's rate (from 24 kHz: no content left above 8 kHz to fold back)
        k = max(1, round(rate / RATE))
        if k > 1:
            pcm = np.convolve(pcm, np.ones(k) / k, mode="same")
        t = np.arange(0, len(pcm) / rate, 1 / RATE)
        out = np.clip(np.interp(t, np.arange(len(pcm)) / rate, pcm), -32768, 32767).astype(np.int16)
        tag = "".join(p.title() for p in name.split("-"))
        c.append(f"constexpr int16_t k{tag}[] = {{\n" + wrap(str(v) for v in out) + "};\n")
        total += 2 * len(out)
    c.append("\n}  // namespace\n\n")
    c.append("constexpr audio::Clip kClips[kClipCount] = {\n" + "".join(
        f"    {{k{''.join(p.title() for p in n.split('-'))}, sizeof(k{''.join(p.title() for p in n.split('-'))}) / 2}},\n"
        for n in CLIPS) + "};\n\n}  // namespace ui::capri\n")
    out = os.path.join(ROOT, "src", "app", "ui", "capri", "capri_voice")
    with open(out + ".h", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(h))
    with open(out + ".cpp", "w", encoding="utf-8", newline="\n") as f:
        f.write("".join(c))
    print(f"capri: {len(CLIPS)} voice clips at {RATE} Hz, ~{(total + 1023) // 1024} KB flash")


if __name__ == "__main__":
    main()
