# Felucca licensing

Felucca is free software. Its **code** is licensed under the GNU General Public License,
version 3 only (`GPL-3.0-only`, full text in `LICENSE`). Its **assets** are not part of
that licence: the icon atlas `assets/icons.png`, the panel image `docs/panel.jpg` and the drum sounds made by
`tools/gen_waves.py` (the Hügelton Sample Pack) are Copyright (C) 2026 Hügelton Instruments,
all rights reserved. Their licence terms will be published later.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## What is code (GPL-3.0-only)

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`

You may use, study, change and share it under the GPL. If you distribute Felucca, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Felucca
inside.

## Additional permission (GPL-3.0 section 7)

As an additional permission under GPL-3.0 section 7, you may combine Felucca, or a work
based on it, with the Felucca Assets (above), and convey the combination.
This is allowed even though the Felucca Assets are not licensed under the GPL, provided
that:

- you follow the GPL for every part that is not a Felucca Asset; and
- you follow the terms published for the assets.

The Felucca Assets are data (wavetables, icons, sample data). They are not program
code. A firmware image built from the GPL sources with replacement assets, or with no
assets, is entirely governed by the GPL.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Instrument samples (Versilian Studios VSCO-2 CE, VCSL) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| Terminus font 8x16 (ter-u16n) | SIL OFL 1.1 | `assets/fonts/ter-u16n.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| UnifrakturCook Bold (j. 'mach' wust, Peter Wiegel): the HortatoR logo, rendered by `tools/gen_logo.py` | SIL OFL 1.1 | `assets/fonts/UnifrakturCook-Bold.ttf`, `assets/fonts/UnifrakturCook-LICENSE.txt` |
| Fukiai icon font (Hügelton Instruments), web editor only | MIT | `web/fukiai.ttf`, `web/FUKIAI-LICENSE.txt`, `LICENSES/MIT-Fukiai.txt` |
| Grids by Emilie Gillet / Mutable Instruments (<https://github.com/pichenettes/eurorack>): the GRIDS pattern engine is a C port of `grids/pattern_generator.cc` with its pattern tables (and avrlib's random generator) | GPL-3.0-or-later | `firmware/src/grids.c`, `firmware/src/grids_tables.h` |
| Streams by Emilie Gillet / Mutable Instruments (<https://github.com/pichenettes/eurorack>): the COMP sidechain compressor is a C port of `streams/compressor.cc` with its tables | MIT | `firmware/src/comp.c`, `firmware/src/comp_tables.h`; `LICENSES/MIT-Mutable-Instruments.txt` |
| Plaits and stmlib by Emilie Gillet / Mutable Instruments (<https://github.com/pichenettes/eurorack>): integer ports of its drum algorithms | MIT | `firmware/src/dm_kick.c`, `dm_snare.c`, `dm_metal.c`, `dm_dsp.c`; `LICENSES/MIT-Mutable-Instruments.txt` |
| jl-uboot-tool by Andrey Grigoryev (kagaimiq) (<https://github.com/kagaimiq/jl-uboot-tool>): the boot-mode cipher and command framing in the Mac recovery tool. Its `wl82loader.bin` is not in this tree; users fetch it (docs/RECOVERY.md) | MIT | `tools/rescue/fm1_uboot.py`; `LICENSES/MIT-jl-uboot-tool.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>; `LICENSES/Apache-2.0.txt` |

The licence texts in `LICENSES/` travel with every published package (`web/make_site.py`).

## Contributions

Contributions are welcome under GPL-3.0-only. By submitting one, you agree that it may be
combined with the Felucca Assets under the section 7 permission above.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. Felucca is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

## Radio

Felucca never enables the Bluetooth / Wi-Fi radio of the hardware.
