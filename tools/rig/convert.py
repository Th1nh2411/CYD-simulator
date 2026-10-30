"""A 2D character's art set -> the firmware's tables, in one go.

The set is a folder art/<name>/ (tools/rig/rig.py): the painted character, its
edited copies (src/<edit>.png: eyes closed, mouth open ...), the room's SVG and
rig.json, which holds the rig and everything else the steps need. Runs the
steps whose inputs changed since their output was made, in order:
 1. cutout.py    the painted character -> base_cut.png (background removed)
 2. frames.py    each edit -> <edit>.png, aligned onto the base
 3. portrait.py  base and frames painted in the head tilts -> portrait "out" .h/.cpp
 4. scene.py     the room -> backdrop "out" .h/.cpp
A step's inputs are its files, its own script and its part of rig.json: the
settings each step last ran with are kept in <set>/.convert.json, so moving the
pivot repaints the poses (20 s) without cutting out and aligning again (minutes).
The firmware draws the tables with a Character of its own (src/app/ui/heroine/
for this one).

usage: python tools/rig/convert.py art/heroine            the steps that are stale
       python tools/rig/convert.py art/heroine --force    every step
"""
import argparse
import json
import os
import sys
import time

import cutout
import frames
import portrait
import rig
import scene


def stale(output, inputs):
    """Whether output is missing or older than any input (which must exist)."""
    for i in inputs:
        if not os.path.exists(i):
            raise SystemExit(f"convert: missing {os.path.relpath(i, rig.ROOT)}")
    return not os.path.exists(output) or max(map(os.path.getmtime, inputs)) > os.path.getmtime(output)


STAMP = ".convert.json"


def settings(r):
    """The part of rig.json each step depends on, by step."""
    def key(v):
        return json.dumps(v, sort_keys=True)
    s = {"cutout": key([r["source"], r.get("cutout")])}
    for f in r["frames"].values():
        s["frame " + f["edit"]] = key([r["face"], f])
    s["portrait"] = key([r["name"], r["namespace"], r["portrait"], r["frames"]])
    if r.get("backdrop"):
        s["backdrop"] = key([r["namespace"], r["backdrop"]])
    return s


def convert(folder, force):
    r = rig.load(folder)
    d = r["dir"]
    stamp_path = os.path.join(d, STAMP)
    try:
        with open(stamp_path, encoding="utf-8") as f:
            ran = json.load(f)
    except (OSError, ValueError):
        ran = {}
    now = settings(r)
    done = []

    def due(step, output, inputs):
        return force or ran.get(step) != now[step] or stale(output, inputs)

    def did(step, name):
        ran[step] = now[step]
        with open(stamp_path, "w", encoding="utf-8") as f:
            json.dump(ran, f, indent=1)
        done.append(name)

    base = os.path.join(d, "base_cut.png")
    source = os.path.join(d, r["source"])
    if due("cutout", base, [source, cutout.__file__]):
        options = r.get("cutout", {})
        cutout.cut(source, base, options.get("bg"), options.get("bottom", False))
        did("cutout", "cutout")

    edits = [os.path.join(d, f["edit"] + ".png") for f in r["frames"].values()]
    for spec, out in zip(r["frames"].values(), edits):
        step = "frame " + spec["edit"]
        if due(step, out, [os.path.join(d, "src", spec["edit"] + ".png"), base, frames.__file__]):
            frames.frame(r, spec)
            did(step, spec["edit"])

    p = portrait.project(r)
    if due("portrait", os.path.join(rig.ROOT, p["out"] + ".cpp"), [base, *edits, portrait.__file__]):
        portrait.convert(p)
        did("portrait", "portrait")

    b = scene.project(r)
    if b and due("backdrop", os.path.join(rig.ROOT, b["out"] + ".cpp"), [os.path.join(rig.ROOT, b["svg"]), scene.__file__]):
        try:
            scene.convert(b)
        except scene.SceneError as e:
            raise SystemExit(f"scene: {e}")
        did("backdrop", "backdrop")
    return done


def main():
    ap = argparse.ArgumentParser(description="2D character art set -> firmware tables")
    ap.add_argument("set", help="art set folder, e.g. art/heroine")
    ap.add_argument("--force", action="store_true", help="run every step, stale or not")
    args = ap.parse_args()
    t0 = time.time()
    done = convert(args.set, args.force)
    print(f"{args.set}: " + (", ".join(done) if done else "up to date") + f" ({time.time() - t0:.0f} s)")


if __name__ == "__main__":
    sys.exit(main())
