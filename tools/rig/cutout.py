"""Cut a character out of a plain background: a chroma color (green), another
flat color (white, ...) or a gray checkerboard painted into the image (image
generators often draw one instead of real transparency).

 - chroma: a strongly colored background (#00ff00) is removed wherever it
   shows, also between hair strands. Alpha comes from how much of the key
   color a pixel holds, which is then taken back out of it (no green fringe).
   Specks are dropped.
For the others only background connected to the image's top, left and right
edges is removed, so white or gray parts inside the outline (eye whites, a
white blouse, a gray top) survive, and only the character's own connected
shape is kept:
 - flat: edge pixels get partial alpha from their distance to the background
   color, and its tint is taken back out of them
 - checker: the background is any neutral gray in the checker's range, and
   gray pockets enclosed by the outline that show both checker shades; the
   outline is pulled in by a pixel so no gray is left on the edge, and the
   shrink to device size smooths it again

usage: python tools/rig/cutout.py art/heroine/base.png [--out cut.png] [--bg #00ff00] [--bottom]
       (--bottom: flat and checker backgrounds touching the bottom edge go too)
       writes <name>_cut.png (RGBA) and <name>_check.png: the cut-out over a
       checkerboard, a dark and a light color, full size and at device size.
"""
import argparse
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

LOW, HIGH = 28, 90  # flat: color distance below LOW is background, above HIGH solid
KEY_SAT = 100       # chroma: a background whose channels spread this much is a key color
KEY_DEAD = 16       # chroma: key color excess up to this is the character's own
SPECK = 40          # chroma: smaller separate bits are noise, px
GRAY_SAT = 22       # checker: HSV saturation at most this is a gray
CHECK_TOL = 14      # checker: luminance within this of a level is that level
DEVICE_H = 240      # device screen height, px: the sprite will be about this tall


def border(img):
    w, h = img.size
    px = img.load()
    ring = [px[x, 0] for x in range(w)] + [px[x, h - 1] for x in range(w)]
    return ring + [px[0, y] for y in range(h)] + [px[w - 1, y] for y in range(h)]


def detect(img):
    """('chroma' | 'flat', median color) or ('checker', (dark, light)) gray levels."""
    ring = border(img)
    median = tuple(sorted(c[i] for c in ring)[len(ring) // 2] for i in range(3))
    if max(median) - min(median) >= KEY_SAT:
        return "chroma", median
    gray = [c for c in ring if max(c) - min(c) <= 10]
    values = sorted(sum(c) // 3 for c in gray)
    if len(gray) > 0.9 * len(ring) and values[len(values) * 9 // 10] - values[len(values) // 10] > 40:
        mid = (values[len(values) // 10] + values[len(values) * 9 // 10]) // 2
        dark, light = [v for v in values if v < mid], [v for v in values if v >= mid]
        return "checker", (dark[len(dark) // 2], light[len(light) // 2])
    return "flat", median


def regions(size, inside):
    """Connected areas (4-neighbour) of the pixels where inside(x, y) holds, as pixel lists."""
    w, h = size
    seen = bytearray(w * h)
    for sy in range(h):
        for sx in range(w):
            if seen[sy * w + sx] or not inside(sx, sy):
                continue
            region, todo = [], [(sx, sy)]
            seen[sy * w + sx] = 1
            while todo:
                x, y = todo.pop()
                region.append((x, y))
                for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                    if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] and inside(nx, ny):
                        seen[ny * w + nx] = 1
                        todo.append((nx, ny))
            yield region


# ---- chroma key ----

def cut_chroma(img, key):
    k = key.index(max(key))
    others = [i for i in range(3) if i != k]
    full = key[k] - max(key[i] for i in others)  # the key's own excess
    w, h = img.size
    src = img.load()
    rgba = Image.new("RGBA", img.size)
    dst = rgba.load()
    for y in range(h):
        for x in range(w):
            c = src[x, y]
            excess = c[k] - max(c[i] for i in others)
            if excess <= KEY_DEAD:
                # Solid, but its line art may carry a hint of the key: no channel above the rest
                u = list(c)
                u[k] = min(u[k], max(u[i] for i in others))
                dst[x, y] = (u[0], u[1], u[2], 255)
                continue
            a = 1 - min(1.0, (excess - KEY_DEAD) / (full - KEY_DEAD))
            if a < 0.04:
                continue
            # Unmix the key (observed = a * color + (1 - a) * key), then no channel above the rest
            u = [min(255, max(0, round((c[i] - (1 - a) * key[i]) / a))) for i in range(3)]
            u[k] = min(u[k], max(u[i] for i in others))
            dst[x, y] = (u[0], u[1], u[2], round(a * 255))
    alpha = rgba.getchannel("A").load()
    for region in regions(img.size, lambda x, y: alpha[x, y] > 0):
        if len(region) < SPECK:
            for x, y in region:
                dst[x, y] = (0, 0, 0, 0)
    return rgba


# ---- flat color and checkerboard: background reachable from the edges ----

def distance(img, bg):
    """Per-pixel max channel difference to the background color (L image)."""
    diff = ImageChops.difference(img, Image.new("RGB", img.size, bg))
    r, g, b = diff.split()
    return ImageChops.lighter(ImageChops.lighter(r, g), b)


def outside(like, bottom):
    """Background-like pixels reachable from the edges: 255 there, 0 elsewhere.
    Not from the bottom edge unless asked: the bust is cropped there, and a top
    of the background's color running off it would go with the background."""
    w, h = like.size
    mask = like.copy()
    seeds = [(x, 0) for x in range(0, w, 8)] + [(0, y) for y in range(0, h, 8)] + [(w - 1, y) for y in range(0, h, 8)]
    if bottom:
        seeds += [(x, h - 1) for x in range(0, w, 8)]
    for s in seeds:
        if mask.getpixel(s) == 255:
            ImageDraw.floodfill(mask, s, 128)
    return mask.point(lambda v: 255 if v == 128 else 0)


def character(out):
    """The connected shape around the image's center line (drops background specks)."""
    w, h = out.size
    fg = out.point(lambda v: 0 if v else 255)
    for y in range(h // 4, h):  # first character pixel down the middle, a quarter in
        if fg.getpixel((w // 2, y)):
            ImageDraw.floodfill(fg, (w // 2, y), 128)
            break
    return fg.point(lambda v: 255 if v == 128 else 0)


def cut_flat(img, bg, bottom):
    dist = distance(img, bg)
    out = outside(dist.point(lambda d: 255 if d < LOW else 0), bottom)
    keep = character(out)
    near = out.filter(ImageFilter.MaxFilter(5))  # edge band
    w, h = img.size
    src, dp, kp, np_ = img.load(), dist.load(), keep.load(), near.load()
    rgba = Image.new("RGBA", img.size)
    dst = rgba.load()
    for y in range(h):
        for x in range(w):
            if not kp[x, y]:
                continue  # stays transparent
            r, g, b = src[x, y]
            if not np_[x, y]:
                dst[x, y] = (r, g, b, 255)
                continue
            a = min(1.0, max(0.0, (dp[x, y] - LOW) / (HIGH - LOW)))
            if a <= 0:
                continue
            # Unmix the background: observed = a * color + (1 - a) * bg
            c = [min(255, max(0, round(bg[i] + (v - bg[i]) / a))) for i, v in enumerate((r, g, b))]
            dst[x, y] = (c[0], c[1], c[2], round(a * 255))
    return rgba


def checker_like(img, lo, hi):
    """255 where a pixel could be the checkerboard: a gray inside its value range."""
    _, s, v = img.convert("HSV").split()
    neutral = s.point(lambda x: 255 if x <= GRAY_SAT else 0)
    in_range = v.point(lambda x: 255 if lo <= x <= hi else 0)
    return ImageChops.multiply(neutral, in_range)


def pockets(img, like, out, dark, light):
    """Background enclosed by the outline (between strands, under an arm): gray
    areas that hold both checker levels. A plain gray inside the character (a
    gray top) holds one shade and stays."""
    lum = img.convert("L").load()
    lp, op = like.load(), out.load()
    found = Image.new("L", img.size, 0)
    fp = found.load()
    for region in regions(img.size, lambda x, y: lp[x, y] and not op[x, y]):
        if len(region) < 30:
            continue
        darks = sum(1 for x, y in region if abs(lum[x, y] - dark) <= CHECK_TOL)
        lights = sum(1 for x, y in region if abs(lum[x, y] - light) <= CHECK_TOL)
        if min(darks, lights) >= 0.15 * len(region):
            for x, y in region:
                fp[x, y] = 255
    return found


def cut_checker(img, dark, light, bottom):
    like = checker_like(img, dark - 2 * CHECK_TOL, light + 2 * CHECK_TOL)
    out = outside(like, bottom)
    keep = character(ImageChops.lighter(out, pockets(img, like, out, dark, light)))
    rgba = img.convert("RGBA")
    rgba.putalpha(keep.filter(ImageFilter.MinFilter(3)))
    return rgba


# ---- review sheet ----

def checkerboard(size, cell=16):
    img = Image.new("RGB", size, (200, 200, 200))
    d = ImageDraw.Draw(img)
    for y in range(0, size[1], cell):
        for x in range((y // cell) % 2 * cell, size[0], 2 * cell):
            d.rectangle([x, y, x + cell - 1, y + cell - 1], fill=(150, 150, 150))
    return img


def shrink(rgba, size):
    """Resize with premultiplied alpha, so no background bleeds into the edges."""
    return rgba.convert("RGBa").resize(size, Image.LANCZOS).convert("RGBA")


def check_sheet(rgba):
    """The cut-out over checker / dark / light, then the same at device size."""
    w, h = rgba.size
    backs = [checkerboard((w, h)), Image.new("RGB", (w, h), (24, 26, 48)), Image.new("RGB", (w, h), (250, 236, 220))]
    small_w = round(w * DEVICE_H / h)
    small_rgba = shrink(rgba, (small_w, DEVICE_H))
    big = Image.new("RGB", (w * 3, h))
    small = Image.new("RGB", (small_w * 3, DEVICE_H))
    for i, back in enumerate(backs):
        b = back.copy()
        b.paste(rgba, (0, 0), rgba)
        big.paste(b, (i * w, 0))
        s = back.resize((small_w, DEVICE_H))
        s.paste(small_rgba, (0, 0), small_rgba)
        small.paste(s, (i * small_w, 0))
    zoom = small.resize((small.width * 3, DEVICE_H * 3), Image.NEAREST)  # device pixels, 3x
    sheet = Image.new("RGB", (max(big.width, zoom.width), big.height + DEVICE_H + zoom.height + 16), (255, 255, 255))
    sheet.paste(big, (0, 0))
    sheet.paste(small, (0, big.height + 8))
    sheet.paste(zoom, (0, big.height + DEVICE_H + 16))
    return sheet


def cut(image, out=None, bg=None, bottom=False):
    """Writes <out or image name>_cut.png and _check.png; bg "#rrggbb" or detected."""
    img = Image.open(image).convert("RGB")
    if bg:
        color = tuple(int(bg[i:i + 2], 16) for i in (1, 3, 5))
        mode, info = ("chroma" if max(color) - min(color) >= KEY_SAT else "flat"), color
    else:
        mode, info = detect(img)
    if mode == "chroma":
        rgba = cut_chroma(img, info)
    elif mode == "checker":
        rgba = cut_checker(img, *info, bottom)
    else:
        rgba = cut_flat(img, info, bottom)
    base = os.path.splitext(out or image)[0].removesuffix("_cut")
    rgba.save(base + "_cut.png")
    check_sheet(rgba).save(base + "_check.png")
    what = f"checkerboard, grays {info[0]} and {info[1]}" if mode == "checker" else f"{mode} #%02x%02x%02x" % info
    print(f"{image}: {img.size[0]}x{img.size[1]}, background {what}, character box {rgba.getbbox()}")
    print(f"wrote {base}_cut.png and {base}_check.png")


def main():
    ap = argparse.ArgumentParser(description="cut a character out of a plain background")
    ap.add_argument("image")
    ap.add_argument("--out", help="RGBA output (default <name>_cut.png)")
    ap.add_argument("--bg", help="background color #rrggbb (default: detected)")
    ap.add_argument("--bottom", action="store_true", help="also remove background touching the bottom edge")
    args = ap.parse_args()
    cut(args.image, args.out, args.bg, args.bottom)


if __name__ == "__main__":
    sys.exit(main())
