"""Pixel sprite sheets (PNG) -> palette-indexed tables for the firmware.

A sprite set is a folder art/<name>/ holding sprites.json and its PNG sheets:
  "header": "src/app/ui/monster/monster_art.h"      the generated header
  "namespace": "ui::monster::art"
  "palettes": {"Fire": {"o": "#391808 outline", ...}}
      slots in index order from 1 (0 is transparent): a letter, the color the
      sheets draw it in, what it is. The firmware picks the real colors (they
      follow the settings), so a slot's color here only tells the sheets apart.
  "sheets": [{"png": "tiles.png", "palette": "Tile", "frame": [16, 16],
              "frames": [["TileGrass", "TileGrass2"]], "about": "..."}]
      sheets every character shares: a grid of frames, rows top to bottom
      (null leaves a cell empty); "about" says what they are, for the studio
  "characters": {"type": "Species", "sheets": [...], "roles": {...}, "list": [...]}
      optional: characters that all have the same sheets. "sheets" is their
      layout, "{id}" in a png name standing for the character, frame names
      being roles ("Down0"); "roles" says what each role is. Each "list" entry
      has an "id", a "palette" and whatever the firmware wants to know of it
      (defaults, labels ...). Its frames are named <Id><role> ("FireDown0").
Pixels less than half opaque are transparent; any other takes the slot of the
nearest palette color, and one farther than TOLERANCE is an error, reported
with its place. A sheet may be drawn larger by a whole factor (each art pixel a
k x k block, as an upscaled export is): the center of each block is read.

The header holds one gfx::PixelArt per frame, written out as text in the slots'
letters, so a change of art reads as a picture in the diff. With characters, it
also has enum Role, each character's frames by role and kCharacters: {id,
frames, the rest of its entry as JSON} of the "type" the including file declares.

usage: python tools/sprites/sprites.py art/monster    convert one set
       python tools/sprites/sprites.py --all          every art/*/sprites.json
The build regenerates stale headers (tools/sprites/pio_hook.py, tools/web/sim.py).
The output is checked in.
"""
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TOLERANCE = 48  # RGB distance from the nearest palette color past which a pixel is an error
MAX_ERRORS = 10
CHARACTER_ID = re.compile(r"^[a-z][a-z0-9_]{0,23}$")


class SpriteError(Exception):
    pass


def sets():
    return sorted(os.path.dirname(p) for p in glob.glob(os.path.join(ROOT, "art", "*", "sprites.json")))


def manifest(folder):
    path = os.path.join(folder, "sprites.json")
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError) as e:
        raise SpriteError(f"{path}: {e}")


WIDTH = 140  # sprites.json lines: a value stays on one line while it fits


def dump(m):
    """sprites.json text: a value on one line if it fits, else one entry per line."""
    def text(v, indent, lead=0):
        flat = json.dumps(v, ensure_ascii=False)
        if indent and (indent + lead + len(flat) <= WIDTH or not isinstance(v, (dict, list)) or not v):
            return flat
        pad = " " * (indent + 2)
        if isinstance(v, dict):
            items = [f"{pad}{json.dumps(k, ensure_ascii=False)}: {text(x, indent + 2, len(json.dumps(k)) + 2)}"
                     for k, x in v.items()]
            return "{\n" + ",\n".join(items) + "\n" + " " * indent + "}"
        return "[\n" + ",\n".join(pad + text(x, indent + 2) for x in v) + "\n" + " " * indent + "]"
    return text(m, 0) + "\n"


def save(folder, m):
    """Writes the set's sprites.json (tools/rig/studio.py edits sets through this)."""
    with open(os.path.join(folder, "sprites.json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(dump(m))


def expand(m):
    """(sheets, characters, roles): every sheet, each character's own made from the
    shared layout first; a character as (id, frame name prefix, its entry)."""
    block = m.get("characters")
    if not block:
        return list(m["sheets"]), [], []
    roles = [f for s in block["sheets"] for row in s["frames"] for f in row if f]
    sheets, chars = [], []
    for c in block["list"]:
        cid = c.get("id", "")
        if not CHARACTER_ID.match(cid):
            raise SpriteError(f"character id {cid!r}: lowercase letters, digits and _")
        if any(cid == other for other, _, _ in chars):
            raise SpriteError(f"character id {cid!r} twice")
        prefix = "".join(w.capitalize() for w in cid.split("_"))
        for s in block["sheets"]:
            sheets.append({"png": s["png"].replace("{id}", cid), "palette": c["palette"], "frame": s["frame"],
                           "frames": [[prefix + f if f else None for f in row] for row in s["frames"]]})
        chars.append((cid, prefix, c))
    return sheets + list(m["sheets"]), chars, roles


def wrapped(head, items, tail, width=110):
    """head item, item, ... tail over as many lines as it takes."""
    lines, line = [head], "   "
    for i, it in enumerate(items):
        it += "," if i < len(items) - 1 else ""
        if len(line) + len(it) + 1 > width:
            lines.append(line)
            line = "   "
        line += " " + it
    lines.append(line + tail)
    return lines


def palette(name, slots):
    keys = []
    for letter, spec in slots.items():
        if len(letter) != 1 or letter in '."\\':
            raise SpriteError(f"palette {name}: slot {letter!r} must be one letter, not . \" or \\")
        color, _, what = spec.partition(" ")
        if len(color) != 7 or color[0] != "#":
            raise SpriteError(f"palette {name}: slot {letter}: {spec!r} does not start with #rrggbb")
        keys.append((letter, tuple(int(color[i:i + 2], 16) for i in (1, 3, 5)), what.strip()))
    return keys


def frames(folder, sheet, keys):
    """The sheet's frames as (name, text of slot letters, row by row)."""
    try:
        from PIL import Image
    except ImportError:
        raise SpriteError("converting sprite sheets needs Pillow (pip install pillow)")
    fw, fh = sheet["frame"]
    grid = sheet["frames"]
    cols = max(len(row) for row in grid)
    try:
        img = Image.open(os.path.join(folder, sheet["png"])).convert("RGBA")
    except OSError as e:
        raise SpriteError(str(e))
    k = img.width // (cols * fw)
    if k < 1 or img.size != (k * cols * fw, k * len(grid) * fh):
        raise SpriteError(f"{sheet['png']}: {img.width}x{img.height} px is not {len(grid)} rows of {cols} "
                          f"frames of {fw}x{fh} px (or that times a whole number)")
    px = img.load()
    out, errors = [], []
    for gy, row in enumerate(grid):
        for gx, name in enumerate(row):
            if name is None:
                continue
            text = []
            for y in range(fh):
                for x in range(fw):
                    r, g, b, a = px[(gx * fw + x) * k + k // 2, (gy * fh + y) * k + k // 2]
                    if a < 128:
                        text.append(".")
                        continue
                    d, letter = min(((r - c[0]) ** 2 + (g - c[1]) ** 2 + (b - c[2]) ** 2, l) for l, c, _ in keys)
                    if d > TOLERANCE ** 2:
                        errors.append(f"{sheet['png']} {name} ({x}, {y}): #{r:02x}{g:02x}{b:02x} is far from "
                                      f"every color of palette {sheet['palette']}")
                    text.append(letter)
            out.append((name, "".join(text)))
    if errors:
        more = [f"... and {len(errors) - MAX_ERRORS} more"] if len(errors) > MAX_ERRORS else []
        raise SpriteError("\n".join(errors[:MAX_ERRORS] + more))
    return out


def convert(folder):
    """Writes the set's header (only if it changed); returns its path and frame count."""
    m = manifest(folder)
    rel = os.path.relpath(folder, ROOT).replace(os.sep, "/")
    palettes = {name: palette(name, slots) for name, slots in m["palettes"].items()}
    lines = ["#pragma once", "",
             f"// Generated by tools/sprites/sprites.py from {rel}/sprites.json and its PNG sheets.",
             "// Do not edit: change the sheets, the build regenerates this.", "",
             '#include "app/gfx/pixel_art.h"', "", f"namespace {m['namespace']} {{", ""]
    for name, keys in palettes.items():
        comment = f"// {name}:"
        for i, (l, _, what) in enumerate(keys):
            item = f" {l} {what}" + ("," if i < len(keys) - 1 else "")
            if len(comment) + len(item) > 100:
                lines.append(comment)
                comment = "//  "
            comment += item
        lines += [comment, f'constexpr char k{name}Legend[] = ".{"".join(l for l, _, _ in keys)}";']
    sheets, chars, roles = expand(m)
    seen = set()
    for sheet in sheets:
        if sheet["palette"] not in palettes:
            raise SpriteError(f"{sheet['png']}: no palette {sheet['palette']}")
        fw, fh = sheet["frame"]
        for name, text in frames(folder, sheet, palettes[sheet["palette"]]):
            if not name.isidentifier() or name in seen:
                raise SpriteError(f"{sheet['png']}: frame name {name!r} is not a new identifier")
            seen.add(name)
            lines += ["", f"constexpr gfx::PixelArt<{fw}, {fh}> k{name}(k{sheet['palette']}Legend,"]
            lines += [f'    "{text[y * fw:(y + 1) * fw]}"' for y in range(fh)]
            lines[-1] += ");"
    if chars:
        lines += ["", '// Characters (sprites.json "characters"): their frames by role, and the rest of their entry',
                  *wrapped("enum Role : uint8_t {", [f"kRole{r}" for r in roles] + ["kRoleCount"], " };")]
        for _, prefix, _ in chars:
            lines += wrapped(f"constexpr gfx::IndexedImage k{prefix}Frames[kRoleCount] = {{",
                             [f"k{prefix}{r}.image()" for r in roles], "};")
        lines.append(f"constexpr {m['characters']['type']} kCharacters[] = {{")
        for cid, prefix, c in chars:
            entry = json.dumps({k: v for k, v in c.items() if k != "id"}, ensure_ascii=False, separators=(",", ":"))
            if ")json" in entry:
                raise SpriteError(f"character {cid}: ')json' cannot appear in its entry")
            lines.append(f'    {{"{cid}", k{prefix}Frames, R"json({entry})json"}},')
        lines += ["};", f"constexpr int kCharacterCount = {len(chars)};"]
    lines += ["", f"}}  // namespace {m['namespace']}", ""]
    text = "\n".join(lines)
    out = os.path.join(ROOT, m["header"])
    old = None
    if os.path.exists(out):
        with open(out, encoding="utf-8", newline="") as f:
            old = f.read()
    if text != old:
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    else:
        os.utime(out)  # up to date with the sheets now
    return m["header"], len(seen)


def build_stale():
    """Regenerates the headers of sets whose manifest or sheets (or this script) changed; returns them."""
    done = []
    for folder in sets():
        m = manifest(folder)
        target = os.path.join(ROOT, m["header"])
        sources = [os.path.join(folder, "sprites.json"), os.path.abspath(__file__)]
        sources += [os.path.join(folder, s["png"]) for s in expand(m)[0]]
        if os.path.exists(target) and all(os.path.exists(s) for s in sources) and \
                max(map(os.path.getmtime, sources)) <= os.path.getmtime(target):
            continue
        done.append(convert(folder)[0])
    return done


def main():
    ap = argparse.ArgumentParser(description="pixel sprite sheets (PNG) -> palette-indexed tables")
    ap.add_argument("set", nargs="*", help="sprite set folder, e.g. art/monster")
    ap.add_argument("--all", action="store_true", help="every art/*/sprites.json")
    args = ap.parse_args()
    folders = sets() if args.all else [os.path.abspath(s) for s in args.set]
    if not folders:
        ap.error("name a sprite set folder or use --all")
    try:
        for folder in folders:
            header, count = convert(folder)
            print(f"{header}: {count} frames")
    except SpriteError as e:
        print(f"sprites: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
