"""A 2D character's art set: the folder art/<name>/ with its pictures and rig.json,
which holds everything the tools need to know about her:

  "name", "namespace"     names of the generated tables (k<name>Poses ...)
  "source"                the painted character; cutout.py cuts it out into
                          base_cut.png. Optional "cutout": {"bg": "#rrggbb",
                          "bottom": true} as its command line takes them
  "face"                  for frames.py, in base px: "window" [x0, y0, x1, y1]
                          where the face is matched in an edit, and "shapes":
                          the ellipses [cx, cy, rx, ry] of "eyes", "brows", "mouth"
  "frames"                the expressions, in the firmware's order: name ->
                          {"edit": edited copy src/<edit>.png, "regions": what
                          it changes ("eyes brows"), "sameEyes": true when its
                          eyes keep the base's shape and only the irises move}
  "portrait"              the rig portrait.py paints the poses with: rows used,
                          size, the head's pivot, neck and hair rows, tilt steps
                          (see there), and "out", the tables' path without extension
  "backdrop"              the room behind her for scene.py: "svg" in the set,
                          "out", "name", "size" and the colors of each time of day

Paths in rig.json are the set's own, except "out" (from the repo root).
"""
import glob
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def sets():
    """Every set of this repo: the folders art/*/ with a rig.json."""
    return sorted(os.path.dirname(p) for p in glob.glob(os.path.join(ROOT, "art", "*", "rig.json")))


def load(folder):
    """The set's rig.json, plus "dir" (absolute) and "rel" (from the repo root) of the folder."""
    folder = os.path.abspath(folder)
    with open(os.path.join(folder, "rig.json"), encoding="utf-8") as f:
        rig = json.load(f)
    rig["dir"] = folder
    rig["rel"] = os.path.relpath(folder, ROOT).replace(os.sep, "/")
    return rig


WIDTH = 140  # rig.json lines: a value stays on one line while it fits


def dump(rig):
    """rig.json text (without "dir" and "rel"): a value on one line if it fits, else
    one entry per line, so an edit shows as a small diff."""
    def text(v, indent, lead=0):
        flat = json.dumps(v, ensure_ascii=False)
        if indent and (indent + lead + len(flat) <= WIDTH or not isinstance(v, (dict, list)) or not v):
            return flat
        pad = " " * (indent + 2)
        if isinstance(v, dict):
            items = [f"{pad}{json.dumps(k)}: {text(x, indent + 2, len(json.dumps(k)) + 2)}" for k, x in v.items()]
            return "{\n" + ",\n".join(items) + "\n" + " " * indent + "}"
        return "[\n" + ",\n".join(pad + text(x, indent + 2) for x in v) + "\n" + " " * indent + "]"
    return text({k: v for k, v in rig.items() if k not in ("dir", "rel")}, 0) + "\n"


def save(folder, rig):
    """Writes the set's rig.json; leaves the file (and its time, which the steps go by) alone if unchanged."""
    path = os.path.join(folder, "rig.json")
    new = dump(rig)
    if os.path.exists(path):
        with open(path, encoding="utf-8", newline="") as f:
            if f.read() == new:
                return False
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(new)
    return True
