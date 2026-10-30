# CLAUDE.md

Firmware for a desk companion: a 2D character (fluid slime, a pixel monster
with three starters, a visual-novel heroine, or Capri with her PC-side AI brain;
switchable at run time) that lives on a small display and
shows notifications / approval prompts received over Wi-Fi, in Korean too.
Current target: the ESP32-S3 boards (JC3248W535 first); the CYD2USB (ESP32-2432S028R, no PSRAM) is
not used any more (user, 2026-10-29), its env still builds. **Read `docs/HANDOFF.md` first** —
state, config schema, test procedure, pitfalls and the next task.
`docs/ARCHITECTURE.md` describes the whole system: requirements, layers,
threads and data flow, every module's design, measured performance, design
decisions and how to extend it.

The user writes in Korean; answer in Korean. You cannot see the device. The
web simulator draws the same frames from the same sources: check drawing and
layout there (browser pane, launch config `sim`, screenshots). Feel, real panel
colors, touch and performance still need the device: say what you verified
where (simulator, build, serial logs, numbers) and ask the user for the rest.

## Commands (PowerShell 5.1 on Windows)

```powershell
python -m platformio run -e cyd2usb                 # build
python -m platformio run -e cyd2usb -t upload       # build + flash (COM3, 460800)
python tools/serial/bootlog.py COM3                 # reset + capture boot log / first stats
python tools/serial/serial_cmd.py COM3 20 "wait:5" "p Deploy?|Release?|Approve|Deny"   # send serial cmds without resetting
python tools/web/sim.py                             # screen simulator: http://localhost:8766, rebuilds + reloads on save
python tools/web/sim.py --build                     # simulator build only (Emscripten, C:\Workspace\emsdk)
python -m platformio run -e replay -t upload        # deterministic replay: screen hashes to compare builds (HANDOFF 5)
$env:PLATFORMIO_BUILD_FLAGS="-DREPLAY_ACTIVE=1"     # (before the line above) replay the monster's script instead of the slime's
$env:PLATFORMIO_BUILD_FLAGS="-DHEAP_PROBE=1"        # (before a build) print the internal heap after each boot step
python -m platformio run -e jc3248w535_tls          # S3 board on ESP-IDF + Arduino (TLS buffers in PSRAM): also muma_tls, s3lcd13_tls.
                                                    # ~5 min; for releases and RAM experiments, check features on the plain env (docs/ARCHITECTURE 11.2a)
$env:PLATFORMIO_BUILD_FLAGS="-DWEB_PROBE=1"         # (before a build) the board tries its own port 80 every 10 s, result on serial and <topic>/debug
python tools/mqtt/battery_web.py                    # battery page for the JC: http://localhost:8772 (broker on this PC)
python tools/mqtt/serve_ota.py --bin .pio/build/jc3248w535/firmware.bin   # test HTTPS server, then `ota https://<PC>:8443/firmware.bin` on serial
                                                    # (certificates: python tools/mqtt/make_certs.py --host <PC ip>; docs/ARCHITECTURE 11.2a)
python tools/sprites/sprites.py --all               # pixel sprite sheets art/*/ -> tables (every build does this itself)
python tools/rig/convert.py art/heroine             # 2D art set (pictures + rig.json) -> tables, run by hand
python tools/rig/studio.py                          # rig studio: upload pictures, rig on them, preview, convert (:8770)
python tools/capri/convert.py                       # Capri's pictures and voice clips art/capri/ -> tables, run by hand
python tools/capri/capri_brain.py serve --http 8773 # Capri's AI brain (MQTT + simulator port); hook / print-hooks for Claude Code
```

- Git may not be on PATH in old shells: use `"C:\Program Files\Git\cmd\git.exe"`.
- Upload speed must stay 460800; 921600 corrupts data on this board's CH340.
- Serial prints stats every 2 s: fps, draw vs SPI time, per-core CPU, heap.
  Serial `bench` = draw-primitive cost + memory; `stress` = full-screen redraw for 6 s.

## Rules

- `src/app/` is portable C++17: no Arduino / ESP-IDF / FreeRTOS headers
  (ArduinoJson is the one allowed library). Check with a grep before committing.
- Board/SoC differences go behind `src/hal/` interfaces; a new board is a new
  `src/boards/<name>/board.cpp` plus an env in `platformio.ini`, not edits elsewhere.
- Tables read by constructors of other translation units' globals must be
  `constexpr` (static-init order crash happened with the eye-set table).
- Xtensa GCC 8.4 can hit an internal compiler error on heavily inlined float
  code: route through one `__attribute__((noinline))` function (see `primDist`).
- The simulator compiles with clang; it does not replace the device build
  (Xtensa GCC) before committing.
- Performance budget: UI core (core 1) ≤ 50% CPU, ≥ 100 KB free internal heap
  with Wi-Fi and plain MQTT up, so room is left for the features still to come.
  TLS (and OTA) are budgeted on top, separately; no figure is set yet (ARCHITECTURE 11.2a).
  Measure (HUD / serial stats, `HEAP_PROBE`) instead of estimating.
- Character slime is frozen at git tag `slime` (`releases/slime/`). Changes to
  shared code must not change slime's behavior; compare against the tag with
  the replay env (identical screen hashes for all 2049 frames).
- Characters are registered only in `src/app/ui/characters.h`; the view and the
  platform know them through `ui::Character`. Pixel art and fonts are constexpr tables.
- Character art lives in `art/<name>/` (PNG sprite sheets + `sprites.json`, or a 2D set
  with `rig.json`); the tables in `src/` are generated from it, never edited by hand.
- Other Claude sessions share this repo and the board on COM3: tell them before
  uploading or holding the serial port.
- Match surrounding style: brief comments explaining why, no dead code.
