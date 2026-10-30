"""Expression frames from edited copies of a portrait (numpy + OpenCV).

Image models redraw the whole picture when asked to close her eyes, so an
edit is never pixel-aligned with the original, often not the same size, and
its skin tone drifts a little. For each edit in <set>/src/ this
 1. finds the face in it: the scale and offset that best match the base face,
    ignoring the eyes, brows and mouth (multi-scale template matching),
 2. matches its colors to the base place by place around those features,
 3. pastes onto the base cut-out the regions the edit is about (eyes, brows,
    mouth) whole, plus what changed just around them, fading into the skin.
Writes <set>/<edit>.png. The face's window and regions, and what each edit
changes, are in the set's rig.json ("face", "frames"; tools/rig/rig.py). An
edit marked "sameEyes" (only the irises move) has each eye fitted again on its
own, by its lids and lashes, so it neither grows nor jumps.

usage: python tools/rig/frames.py art/heroine        every frame of the set
       python tools/rig/frames.py art/heroine eyes_half mouth_open
tools/rig/convert.py runs it with the other steps.
"""
import os
import sys

import cv2
import numpy as np

import rig

CHANGED = (10, 34)  # color difference: below the first the base's, above the second the edit's
FEATHER = 2         # px of soft edge on the regions and the changed pixels
UPSCALED = 1.5      # an edit blown up more than this gets sharpened
SHARPEN = 0.8
TINT_SPREAD = 9     # px over which the edit's tint against the base is smoothed


def read(path):
    img = cv2.imread(path, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise SystemExit(f"cannot read {path}")
    return img


def regions_mask(face, shape, regions, grow=0):
    m = np.zeros(shape[:2], np.uint8)
    for name in regions.split():
        for cx, cy, rx, ry in face["shapes"][name]:
            cv2.ellipse(m, (cx, cy), (rx + grow, ry + grow), 0, 0, 360, 255, -1)
    return m


def locate(face, base_rgb, edit):
    """(s, ox, oy) with base px = edit px * s + (ox, oy), matching the face but not its features."""
    x0, y0, x1, y1 = face["window"]
    templ = base_rgb[y0:y1, x0:x1]
    keep = 255 - regions_mask(face, base_rgb.shape, "eyes brows mouth", grow=6)[y0:y1, x0:x1]

    def best_at(scales, factor):
        t = cv2.resize(templ, None, fx=factor, fy=factor, interpolation=cv2.INTER_AREA)
        k = cv2.resize(keep, (t.shape[1], t.shape[0]), interpolation=cv2.INTER_NEAREST)
        best = None
        for s in scales:
            e = cv2.resize(edit, None, fx=s * factor, fy=s * factor, interpolation=cv2.INTER_LINEAR)
            if e.shape[0] < t.shape[0] or e.shape[1] < t.shape[1]:
                continue
            r = cv2.matchTemplate(e, t, cv2.TM_SQDIFF, mask=k)
            v, _, loc, _ = cv2.minMaxLoc(r)
            v /= max(1, int(np.count_nonzero(k))) * 3
            if best is None or v < best[0]:
                best = (v, s, loc[0] / factor, loc[1] / factor)
        return best

    _, s0, _, _ = best_at(np.arange(1.0, 3.01, 0.04), 0.25)
    err, s, lx, ly = best_at(np.arange(s0 - 0.05, s0 + 0.051, 0.005), 1.0)
    return err, s, x0 - lx, y0 - ly


def fit_eyes(face, base_rgb, warped):
    """Each eye of the (face-aligned) edit scaled and moved a little to sit on the
    base's eye, matching the lids and lashes around it, not the iris inside."""
    out = warped.copy()
    h, w = base_rgb.shape[:2]
    b = base_rgb.astype(np.float32)
    for cx, cy, rx, ry in face["shapes"]["eyes"]:
        x0, y0, x1, y1 = cx - rx - 14, cy - ry - 14, cx + rx + 14, cy + ry + 14
        ring = np.zeros((h, w), np.uint8)
        cv2.ellipse(ring, (cx, cy), (rx + 8, ry + 8), 0, 0, 360, 255, -1)
        cv2.ellipse(ring, (cx, cy), (int(rx * 0.6), int(ry * 0.7)), 0, 0, 360, 0, -1)
        keep = ring[y0:y1, x0:x1] > 0
        best = None
        for k in np.arange(0.9, 1.101, 0.01):
            for dy in range(-8, 9):
                for dx in range(-8, 9):
                    # base px p takes the edit at c + (p - c) * k + (dx, dy)
                    m = np.float32([[k, 0, cx - k * cx + dx], [0, k, cy - k * cy + dy]])
                    cand = cv2.warpAffine(warped, m, (w, h), flags=cv2.INTER_LINEAR | cv2.WARP_INVERSE_MAP)
                    err = np.abs(cand[y0:y1, x0:x1] - b[y0:y1, x0:x1]).sum(axis=2)[keep].mean()
                    if best is None or err < best[0]:
                        best = (err, m)
        moved = cv2.warpAffine(warped, best[1], (w, h), flags=cv2.INTER_CUBIC | cv2.WARP_INVERSE_MAP)
        region = np.zeros((h, w), np.float32)
        cv2.ellipse(region, (cx, cy), (rx + 30, ry + 30), 0, 0, 360, 1, -1)
        region = cv2.GaussianBlur(region, (0, 0), 6)[..., None]
        out = out * (1 - region) + moved * region
    return out


def frame(r, spec):
    """One frame of a set (rig.load), spec its entry in "frames"."""
    face, art, name, regions = r["face"], r["dir"], spec["edit"], spec["regions"]
    base = read(os.path.join(art, "base_cut.png"))  # BGRA
    base_rgb = base[:, :, :3]
    edit = read(os.path.join(art, "src", name + ".png"))[:, :, :3]
    err, s, ox, oy = locate(face, base_rgb, edit)
    h, w = base.shape[:2]
    warped = cv2.warpAffine(edit, np.float32([[s, 0, ox], [0, s, oy]]), (w, h), flags=cv2.INTER_CUBIC,
                            borderMode=cv2.BORDER_REPLICATE).astype(np.float32)
    if spec.get("sameEyes"):
        warped = fit_eyes(face, base_rgb, warped)
    area = regions_mask(face, base.shape, regions)
    b = base_rgb.astype(np.float32)
    # Colors, place by place: where both show the same skin, measure the edit's
    # tint against the base, spread it smoothly and take it out of the edit, so
    # its skin and blush match the base's right up to the eyes and lips
    near = regions_mask(face, base.shape, regions, grow=40) > 0
    same = (np.abs(warped - b).max(axis=2) < 40) & near & (b.mean(axis=2) > 120)
    weight = cv2.GaussianBlur(same.astype(np.float32), (0, 0), TINT_SPREAD)
    for c in range(3):
        offset = cv2.GaussianBlur((b[..., c] - warped[..., c]) * same, (0, 0), TINT_SPREAD)
        warped[..., c] += np.where(weight > 0.02, offset / np.maximum(weight, 1e-3), 0)
    if s > UPSCALED:
        # Blown up from a small picture: sharpen back toward the base's crisp lines
        warped += SHARPEN * (warped - cv2.GaussianBlur(warped, (0, 0), 0.5 * s))
    warped = np.clip(warped, 0, 255)
    # The regions themselves come wholly from the edit: half-taken features
    # leave ghosts of the base's eyes. Around them only what really changed
    # (lash tips reaching out), fading into the skin, whose colors now match
    core = cv2.GaussianBlur(area.astype(np.float32) / 255, (0, 0), FEATHER)
    diff = np.abs(warped - b).max(axis=2)
    changed = np.clip((diff - CHANGED[0]) / (CHANGED[1] - CHANGED[0]), 0, 1)
    changed = cv2.GaussianBlur(cv2.dilate(changed, np.ones((3, 3), np.uint8)), (0, 0), FEATHER)
    reach = regions_mask(face, base.shape, regions, grow=12).astype(np.float32) / 255
    m = np.maximum(core, changed * cv2.GaussianBlur(reach, (0, 0), 4))[..., None]
    out = base.copy()
    out[:, :, :3] = np.clip(b * (1 - m) + warped * m, 0, 255).astype(np.uint8)
    cv2.imwrite(os.path.join(art, name + ".png"), out)
    print(f"{name}: face at scale {s:.3f}, offset ({ox:.0f}, {oy:.0f}), mismatch {err:.0f}, "
          f"{int((m[..., 0] > 0.5).sum())} px taken")


def main():
    if len(sys.argv) < 2:
        sys.exit("usage: python tools/rig/frames.py <set folder> [edit ...]")
    r = rig.load(sys.argv[1])
    specs = list(r["frames"].values())
    if sys.argv[2:]:
        unknown = set(sys.argv[2:]) - {f["edit"] for f in specs}
        if unknown:
            sys.exit(f"not in {r['rel']}/rig.json: {', '.join(sorted(unknown))}")
        specs = [f for f in specs if f["edit"] in sys.argv[2:]]
    for spec in specs:
        frame(r, spec)


if __name__ == "__main__":
    main()
