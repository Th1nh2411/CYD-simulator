# Capri: sources and rights

Capri is an unofficial fan character (Project Capricorn, *Cyberpunk: Edgerunners*). This firmware's
Capri is not approved or endorsed by CD PROJEKT RED, the anime's rights holders or its voice actors.
Non-commercial use only: both projects she comes from are under the PolyForm Noncommercial License
1.0.0 (https://polyformproject.org/licenses/noncommercial/1.0.0), and their notices go with any copy.

## Pictures (this folder) — mochiiwuff/capri-desktop

`intro-*.webp` (her figure: the intro close-up cels, fan art), `capri-face.png`, `face-*.png` and
`cutin-*.png` (frames of the show) are copied unchanged from https://github.com/mochiiwuff/capri-desktop
(`assets/capri/`). That project says they belong to their respective rights holders and are **not**
covered by its code licence; a rights holder may ask for them to be removed. Do not sell or redistribute builds
that contain them commercially.

    Required Notice: Copyright (c) 2026 mochiiwuff (https://github.com/mochiiwuff/capri-desktop)

Her event lines (lead lines by card tag) and the panel and sound design follow its
`src/renderer/js/lines.js`, `capri.css` and `voice.js`.

## Voice — lachydotmcg/capri-desktop

`voice/hello-*.wav` (her greeting clips), her persona (`tools/capri/capri_brain.py`) and her own lines
(`src/app/ui/capri/capri_lines.cpp`, translated to Korean here) come from
https://github.com/lachydotmcg/capri-desktop (`assets/voice/`, `app/Brain.cs`, `app/Capri.cs`). Its
speech box (colors, "> sys:" / "CAPRI:" rows) is the panel's model.

    Required Notice: Copyright 2026 Lachy (https://github.com/lachydotmcg)
    Required Notice: Capri's art and animations are by her artist, who prefers to stay private. Anyone who uses any of them must credit them as "Capri's artist".
    Required Notice: Anyone who uses Capri's code or any of its other assets must credit the Capri project (https://github.com/lachydotmcg/capri-desktop). Parts made by others keep their own licences, listed in its README.

Her spoken voice (`tools/capri/capri_brain.py`) is that project's own synthetic voice: its voice pack
`capri-voice-1` (Kokoro-82M, Apache-2.0, run with sherpa-onnx, Apache-2.0), speaker 53 "Her own",
downloaded at run time to `~/.capri_brain/voice-pack`, not kept in this repo.

That project's voice policy: only use voices you have the rights to; no cloning or impersonating a
living person's voice without consent.

## Generated from these

`tools/capri/convert.py` makes `src/app/ui/capri/capri_art.*` and `capri_voice.*`; do not edit those.
