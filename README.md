# Hortator

[![License: GPL-3.0-only](https://img.shields.io/badge/license-GPL--3.0--only-blue.svg)](LICENSE)

**Drum-machine firmware for the M-VAVE FM-1.** Eight tracks of drums, a step sequencer, Grids, a pumping
compressor, LFOs, resonators and live effects, all on the FM-1's own keys, knobs and screen. By DEADACTIVE, built
on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita.

On a Roman galley, the *hortator* beat the drum that kept the rowers in time. Felucca is a Nile sailing boat; this
is its drummer.

- **Play it in your browser:** <https://deadactive.github.io/hortator/>. The page runs the real firmware, compiled
  to WebAssembly, on a 3D FM-1 you can play with the mouse, a touch screen or the computer keyboard.
- **Install it on your FM-1:** <https://deadactive.github.io/hortator/webapp/installer/>, in Chrome or Edge, over
  the FM-1's USB cable.

**Beta.** Hortator is untested on most hardware and may be unstable. You install it at your own risk: the author
accepts no responsibility if your FM-1 is damaged or misbehaves. M-VAVE's own updater takes you back to the official
firmware ([below](#back-to-the-official-firmware)).

![FM-1 controls](docs/panel.jpg)

## What it does

- **Eight drum tracks.** Each plays a drum model: 808 and 909 kicks, snares and claps, hats, toms, cowbell, cymbal,
  rim, clave, conga, ports of Plaits' drum algorithms, and samples. Every model has TUNE, DECAY, TONE and a fourth
  knob of its own.
- **Step sequencer.** Up to 64 steps per track on the 16 white keys, each track with its own length, division and
  swing. Accents, PROB and RATCH per step, fills that play every other bar, live recording, mutes that wait for the
  next bar.
- **MOTION.** Knob turns recorded per step while a track plays, and played back with the pattern.
- **SONG and names.** Chain up to 16 rows of projects with repeats; name projects on the device.
- **Grids.** Mutable Instruments' pattern generator: a map of drum patterns, chaos, or Euclidean rhythms. Any track
  can follow one of its three channels.
- **Compressor.** The Streams compressor keyed by any track, for ducking and pumping; GHOST keeps the pump with the
  key track muted.
- **Movement.** Two LFOs per track, ten waves with a morph between them, synced to the tempo.
- **Effects.** DIST, the SLICER and RESON (string, pipe or chord resonator) per track; delay, ROOM or SPRING reverb
  and chorus sends; a master limiter; SPEAKER EQ with BASS+ for the small speaker.
- **PERFORM.** Hold FX: the black keys play live master effects (repeat, reverse, tape stop, filters, freeze,
  octave), the white keys mute tracks while held, the knobs become FILTER / CRUSH / THROW / DEPTH.
- **Connections.** MIDI in over USB and the TRS jack, MIDI clock in (USB or TRS), and USB audio: the FM-1 records
  into a DAW over its USB cable.
- **Projects.** Four slots, A to D, saved in the FM-1's flash.

### On the panel

| Button | Opens |
| --- | --- |
| **HOME** | the main screen and the compressor (PRESETS turns the pages); hold it for the menu |
| **EDIT** | the selected track's sound: its model and knobs |
| **SEQ** | the steps on the white keys, then PATTERN, MOTION and SONG |
| **FX** | FX, SLICER, RESON, delay, reverb and chorus; hold it for PERFORM |
| **ARP** | Grids |
| **LFO** | the track's two LFOs |
| **REC** | TRACKS (levels, lengths, pans, mutes) and recording |
| **GLO** | global and system settings |
| **SAVE** | projects (save, load, name) and tools |

ALGORITHM picks the track anywhere; PRESETS turns the pages of a section; KNOB 1 to 4 edit what the screen shows.
SELECT sets the tempo.

## Install

1. Open the [installer](https://deadactive.github.io/hortator/webapp/installer/) in Chrome or Edge on a computer.
2. Connect the FM-1 straight to the computer with a USB data cable. No probe or Transporter is needed.
3. Press Install and keep the cable in until it says Done. The FM-1 restarts with Hortator.

Releases are also on [GitHub](https://github.com/DeadActive/hortator/releases) as `hortator-<version>.fwsc`, with
what changed in each ([CHANGELOG.md](CHANGELOG.md)). `tools/fm1_install.py` installs a package from a terminal.

### Back to the official firmware

Use M-VAVE's updater, M-UPGRADE, from <https://www.m-vave.com/download> (Downloads → PC Software → M-UPGRADE), with
the official firmware (PC Firmware → FM-1 V15). If an install fails and the FM-1 no longer starts, recovering it
needs a [Transporter](https://github.com/kurogedelic/FM-1-transporter).

## Build

- The firmware and its package: [BUILDING.md](BUILDING.md) (`./build.sh`; `DRUM_PACKAGE=1 ./build.sh` for an
  installable package and the site).
- The web simulator and the landing page: [docs/SIMULATOR.md](docs/SIMULATOR.md) (`tools/build_sim.sh`; build it
  before the package, so the site carries both).
- Tests: `tests/run_tests.sh` and `tests/run_drum_tests.sh` (firmware), `tools/build_sim.sh` (simulator and site).
- Versions and publishing: [docs/VERSIONING.md](docs/VERSIONING.md), `tools/publish.sh`.

## Layout

| Path | What |
| --- | --- |
| `firmware/` | firmware sources: `src/` app, `hal/` hardware layer, `loader/` update loader |
| `tools/` | build scripts, generators, package maker, installer, publishing |
| `assets/` | icon atlas, font, CC0 samples |
| `web/` | web installer and editor; `web/sim/` the landing page and simulator |
| `tests/` | tests that run on the build machine; `tests/sim_*` the simulator's |
| `docs/` | device install notes, versioning, the simulator, design specs and plans |

## Credits

- Hortator by DEADACTIVE.
- [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita ([@kurogedelic](https://github.com/kurogedelic)),
  [Hügelton Instruments](https://hugelton.com): the firmware Hortator is built on, its hardware layer, update
  loader, installer, UI and engines.
- [Grids](https://github.com/pichenettes/eurorack/tree/master/grids) by Emilie Gillet, Mutable Instruments: the GRIDS
  pattern engine is a C port (GPL-3.0-or-later).
- [Streams](https://github.com/pichenettes/eurorack/tree/master/streams) by Emilie Gillet, Mutable Instruments: the
  compressor is a C port (MIT).
- [Plaits](https://github.com/pichenettes/eurorack/tree/master/plaits) by Emilie Gillet, Mutable Instruments: integer
  ports of its drum algorithms (MIT).
- Font: [Terminus](https://terminus-font.sourceforge.net/) by Dimitar Toshkov Zhekov,
  [SIL OFL 1.1](assets/fonts/Terminus-LICENSE.txt).
- Samples: [Versilian Studios](https://versilian-studios.com/) VSCO-2 Community Edition and VCSL, CC0 1.0
  ([attribution](assets/samples-cc0/ATTRIBUTION.txt)); the Hügelton Sample Pack (© Hügelton Instruments, not CC0).
- Web editor icons: Fukiai by Hügelton Instruments, [MIT](web/FUKIAI-LICENSE.txt).
- Package format and boot files: [JieLi AC79 SDK](https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK) (Apache-2.0, not
  included).

## Licence

Code: [GPL-3.0-only](LICENSE). Assets and third-party material: [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. Hortator is independent firmware that runs on FM-1
hardware; it is not affiliated with, endorsed by or supported by them. It never enables the hardware's Bluetooth /
Wi-Fi radio.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; drum machine fork: 2026 DEADACTIVE.
