# PlatformIO pre-build script: regenerates the sprite tables whose PNG sheets
# changed (tools/sprites/sprites.py), so an edited sheet shows in the next build.
import os
import sys

Import("env")  # noqa: F821  (SCons)

sys.path.insert(0, os.path.join(env["PROJECT_DIR"], "tools", "sprites"))  # noqa: F821
import sprites  # noqa: E402

try:
    for out in sprites.build_stale():
        print(f"sprites regenerated: {out}")
except sprites.SpriteError as e:
    sys.stderr.write(f"sprites: {e}\n")
    env.Exit(1)  # noqa: F821
