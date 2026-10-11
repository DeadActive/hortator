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
  next bar (the MUTE screen: hold GLO).
- **Notes and ACID.** Every step holds a note. Drum tracks play their steps transposed (C4 plays as tuned); ACID, a
  303-style bass voice (TUNE DECAY CUTOF RESO ENVMD ACCNT WAVE GATE: saw or square into a resonant 24 dB filter, with
  accent, slide and tie), plays them as a bass line on up to two tracks ("ACID: 2 TRACKS MAX"). SCL turns the keys
  into a keyboard for the selected track (KEYS): play it, record it live, or enter a line step by step. Live record:
  a REC tap, on any screen, arms the selected track (and starts the transport when stopped); a key writes the nearest step, a key
  held into later steps writes TIEs, a legato overlap writes a SLIDE (TIE and SLIDE on ACID tracks only). An ACID
  track's STEP page is a piano roll; a step the white keys turn on takes the last KEYS note on an ACID track, C4 on a
  drum. KEYS notes send no MIDI out, and an ACID note (or a tied chain) longer than 5.5 s
  is cut (a safety limit). A loaded project keeps ACID on its first two ACID tracks; the others come back as their
  power-on kit drum.
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
  octave), the white keys mute tracks while held, the knobs become FILTER / CRUSH / THROW / DEPTH, which stay on
  until you turn them back.
- **Metronome.** GLOBAL 3/3 METRO: a click on every quarter note while playing, the bar's first higher.
- **Key backlight.** MENU BACKLIGHT ON: the keys glow dimly while unlit, the steps 1, 5, 9 and 13 a little brighter.
- **Connections.** MIDI in over USB and the TRS jack, MIDI clock in (USB or TRS), and USB audio: the FM-1 records
  into a DAW over its USB cable. The Bluetooth version adds Bluetooth MIDI ([below](#bluetooth-midi)): a BLE-MIDI
  keyboard or clock, or the M-VAVE SMC-Mixer as a mixer for the eight tracks.
- **Projects.** Four slots, A to D, saved in the FM-1's flash; the last one saved loads at power-on.

### On the panel

| Button | Opens |
| --- | --- |
| **HOME** | two modes: **TRACKS** (levels, lengths, pans; the selected track's steps on the keys) and **HOME** (the scope, the selected track's first four knobs, the keys play the tracks; SELECT on to the compressor's pages). A HOME tap on HOME switches between them; from any other screen it opens the one last shown. Power-on: TRACKS. Hold HOME for the menu |
| **EDIT** | the selected track's sound: its model and knobs |
| **SEQ** | the steps on the white keys, then PATTERN, MOTION and SONG |
| **FX** | FX, SLICER, RESON, delay, reverb and chorus; hold it for PERFORM |
| **ARP** | Grids |
| **LFO** | the track's two LFOs |
| **REC** | arms / disarms the selected track for recording, on any screen (arming while stopped starts play). On STEP with KEYS on and stopped: step record. Held on a SEQ page: CLEAR TRACK; on SONG: DELETE ROW / CLEAR SONG |
| **GLO** | a tap: global and system settings; held: the MUTE screen ([mutes](#mutes)) |
| **SAVE** | projects (save, load, name) and tools |
| **SCL** (labelled SEL in the web simulator) | KEYS: the keys play the selected track; OCT- / OCT+ set the octave (-2 to +2); on STEP, stopped, REC starts step record |

With KEYS on, OCT- / OCT+ are the octave: LAYER (from EDIT) and the LFO pages' OCT buttons need KEYS off; the STEP
bank stays on KNOB 1; TRACKS keeps OCT as its bank. TRACKS, COMP, NAME, the menu, PERFORM (FX held) and the MUTE
screen (GLO held) keep their keys.
Step record starts in KEYS mode, on the STEP page, with the transport stopped, by a REC tap: the cursor starts at the
bank's first step and the top bar shows `STEP REC nn`. KNOB 1 to 4 are STEP, NOTE, TIME (NOTE / TIE / REST) and FLAG
(- / ACC / SLD / A+S); EDIT writes a rest. A REC tap, PLAY or leaving the page ends it.

ALGORITHM picks the track anywhere. SELECT turns the pages of a section and moves the menu's cursor; on HOME's
TRACKS mode it sets the tempo instead (`CLK USB` / `CLK TRS` when following a clock; GLOBAL 1/3 KNOB 1 sets it on any
page). KNOB 1 to 4 edit what the screen shows. PRESETS does nothing.

### Mutes

Hold GLO: the **MUTE screen**, up while GLO is held (a quick GLO tap opens GLOBAL instead; a key pressed while GLO
is down opens the screen at once). The eight tracks are boxes, **filled = heard, outline = muted**, each with its
number and model, and under it the key that toggles it (F3 to F4, the white keys; their LEDs lit for the heard
tracks). The top C# sets `MUTE: NOW`, the top D# `MUTE: NEXT BAR` (the same setting as GLOBAL 3/3 MUTE): with NEXT
BAR, a mute made while playing blinks, then lands on the next downbeat. While the screen is up the keys play
nothing and the knobs and the other buttons rest, PLAY aside. Let go of GLO: back to the page you were on.

### PERFORM

Hold FX for the PERFORM screen. The black keys are buffer effects while held (REPEAT 1/8 1/16 1/32, REVERSE, TAPE
STOP, LPF and HPF sweeps, FREEZE, OCT UP / DN), the white keys silence their tracks while held, and KNOB 1 to 4 are
FILTER (left LPF, right HPF), CRUSH, THROW and DEPTH. **The knob effects stay on** after FX is let go (or the PERFORM
page closes) and keep working on every page; the top bar shows an amber `FX` in place of the tempo icon while any
is on. To switch one off, turn it back to 0 on the PERFORM screen; TOOLS `INIT*` switches them all off. The
buffer effects stop when their key is let go.

### Projects

SAVE > PROJECT: four slots, A to D. **The last project saved loads at power-on** (`LOADED` in the top bar, stopped;
PROJECT's SLOT points at it). If it can't be loaded (the slot is empty or unreadable), the FM-1 asks
`LOAD FAILED: SLOT B` / `START NEW PROJECT?`: OCT+ (YES) forgets it, OCT- (NO) tries it again at the next power-on;
both carry on with the power-on kit. A safe start (SEQ held at power-on) loads nothing.

Projects saved by this build can't be read by 0.16.0 or older; older projects load (every track with PRIO HIGH).

### Metronome

GLOBAL 3/3 **METRO**: OFF, LOW, MID, HIGH. While playing, on the FM-1's own clock or following one, a click on every
quarter note, higher on the bar's first. It comes after the PERFORM effects (they leave it alone) and follows the
MASTER volume, on the speaker, the headphones and USB audio. A device setting: saved, not part of projects.

### Key backlight

MENU (hold HOME) **BACKLIGHT**: OFF (as before) or ON. With ON, every key LED that is not lit glows at about 5 %, so
the keys can be found in the dark; where the white keys are the selected track's steps (STEP and the other SEQ pages,
HOME's TRACKS mode), the unlit steps 1, 5, 9 and 13 of the bank shown glow at about 25 %, marking the beats. Lit keys
(steps that are on, the playhead, held keys, the MUTE screen's heard tracks, PERFORM's effects) stay fully lit; the
buttons' LEDs are unchanged. KNOB 1 or OCT+ on the row switches it. A device setting: saved when the menu closes,
not part of projects; OFF until it is set. A safe start keeps every LED dark, as before.

### CPU and PRIO

When an audio block takes more than 95 % of its time, the FM-1 stops one sounding voice to catch up, and the top bar
shows a red CPU icon for 2 s. A track with **PRIO LOW** loses its voices before a PRIO HIGH one, the oldest first;
a held ACID note goes last. The same order picks the voice to stop when too many play at once. PRIO is on the track's last EDIT (SOUND) page, after CHOKE: HIGH by default, saved in the project.

## Bluetooth MIDI

A second version of each release, **Hortator + Bluetooth MIDI** (experimental): the installer offers it next to the
plain one, and the release has it as `hortator-X.Y.Z-bluetooth.fwsc`. Everything above works the same in it.
Bluetooth is **off at every power-on**; it runs only after you turn it on, until the FM-1 is switched off.

### The Bluetooth page

MENU (hold HOME), SELECT to **BLUETOOTH**, OCT+. SELECT moves the cursor, KNOB 1 changes the row, OCT+ acts on it,
OCT- goes back to the menu.

| Row | What it does |
| --- | --- |
| **BLUETOOTH** | ON starts Bluetooth and reconnects to the remembered device; OFF disconnects and stops reconnecting. KNOB 1 right / left, or OCT+. Not saved: off at every power-on. |
| **MODE** | What the connected device's MIDI does: **AUTO** (SMC DAW for a device whose name starts with `SMC`, else MIDI), **SMC DAW** or **MIDI**. Saved when the menu closes. |
| **DEVICE** | The remembered device: its name once it has been seen since power-on, else its address; `NONE`. KNOB 1 left forgets it (and disconnects). |
| **SCAN** | OCT+: scans for 10 s (BLUETOOTH ON only). Up to 8 devices are listed under it, BLE-MIDI ones (`MIDI`) first, then by signal. |
| **1 .. 8** | A device found: OCT+ remembers it (saved at once) and connects to it. |

Above the rows, the state: OFF, STARTING, IDLE, SCANNING, CONNECTING, CONNECTED and the device's name, or FAILED and
the reason (`NO RF TRIM IN FLASH`: this FM-1 cannot run Bluetooth; `BT START TIMEOUT`: switch off and on to retry).
Under it, the last error (`CONNECT TIMEOUT`, `NO BLE-MIDI SERVICE`, `DISCONNECTED 0x08`). `BT CONNECTED` and
`BT LOST` show in the top bar on any page.

**Reconnecting.** With BLUETOOTH ON and a remembered device, the FM-1 connects to it whenever it is in range: when
it is switched on again or comes back after a lost connection, and after a power-on as soon as you turn BLUETOOTH
ON (no scan or pick needed). While it is not found, it scans 10 s at a time with a 2 s pause. A SCAN from the page
pauses this while the page stays open, so you can pick another device.

**MIDI mode.** Notes on the drum channel (GLO: CH) play the tracks whose NOTE matches, as over USB and TRS. GLO:
**CLK BLE** follows the device's MIDI clock (Start, Stop and the tempo), as CLK USB / TRS do; the tempo knobs then say
`CLK BLE`. A project saved with CLK BLE loads with CLK INT in the plain version.

### The SMC-Mixer (DAW mode)

The M-VAVE SMC-Mixer in its DAW (Mackie Control) mode, MODE AUTO or SMC DAW. Only presses act.

| Control | Hortator |
| --- | --- |
| Fader 1–8 | track 1–8 LEVEL, with pickup (below); recorded as motion for the selected track while it is armed and playing |
| Master fader | nothing |
| Knob 1–4 | the selected track's first four knobs (TUNE DECAY TONE and the model's fourth; ACID: TUNE DECAY CUTOF RESO), one step a click; the top bar shows e.g. `T3 DECAY 64`; recorded as motion while the track is armed and playing |
| Knob 5–8 | nothing |
| MUTE 1–8 | mute / unmute the track, as its key on the MUTE screen (MUTE: NEXT BAR applies) |
| SOLO 1–8 | solo / unsolo the track (below) |
| REC 1–8 | arm / disarm the track for recording; arming while stopped starts play (as a REC tap) |
| SELECT 1–8 | select the track |
| PLAY | stopped: play; playing: nothing |
| STOP | stop |
| REC | arm / disarm the selected track |
| BANK ◀ / CHANNEL ◀, BANK ▶ / CHANNEL ▶ | the previous / next track (stops at 1 and 8) |
| REW, FF, the arrows | nothing |

While a song plays or is armed, REC 1–8 and REC say `STOP TO EDIT`, as the FM-1's REC does. An armed track records the
FM-1's keys as live record does.

- **LEDs:** MUTE n lit = track n muted (or a mute waiting for the next bar), SOLO n = soloed, REC n = armed,
  SELECT n = the selected track, PLAY = playing, STOP = stopped, REC = any track armed. They are sent again on every
  connection.
- **Fader pickup:** a fader does nothing until it reaches its track's level (within 2, or passing it); until then
  the mixer blinks its LED. Then it follows. On every connection, and whenever a track's LEVEL changes some other
  way (the FM-1's knob, LOAD, INIT, LEVEL motion), that fader has to be picked up again.
- **Solo:** while any track is soloed, only the soloed tracks are heard; several can be soloed. A silenced track
  is dimmed on HOME and TRACKS, as a muted one; the mutes are unchanged. The FM-1 has no SOLO control, so solo
  clears by itself when the SMC-Mixer disconnects and when MODE leaves SMC DAW (every track audible again; nothing
  is cut). Solo is not saved: INIT and a power-on clear it too.
- **Leaving SMC DAW** (MODE MIDI while connected): Hortator turns the mixer's LEDs off once and sends it nothing
  more.

## Install

1. Open the [installer](https://deadactive.github.io/hortator/webapp/installer/) in Chrome or Edge on a computer.
2. Connect the FM-1 straight to the computer with a USB data cable. No probe or Transporter is needed.
3. Press Install and keep the cable in until it says Done. The FM-1 restarts with Hortator.

Releases are also on [GitHub](https://github.com/DeadActive/hortator/releases) as `hortator-<version>.fwsc`, with
what changed in each ([CHANGELOG.md](CHANGELOG.md)). `tools/fm1_install.py` installs a package from a terminal.

### Back to the official firmware

Use M-VAVE's updater, M-UPGRADE, from <https://www.m-vave.com/download> (Downloads → PC Software → M-UPGRADE), with
the official firmware (PC Firmware → FM-1 V15). If the FM-1 stops answering both the installer and M-UPGRADE, a Mac
can put the official firmware back through the chip's boot mode, with no extra hardware: [docs/RECOVERY.md](docs/RECOVERY.md).
If it does not start at all and never reaches boot mode, recovering it needs a
[Transporter](https://github.com/kurogedelic/FM-1-transporter).

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

- Hortator by DEADACTIVE. BLE MIDI: 2026 DEADACTIVE.
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
hardware; it is not affiliated with, endorsed by or supported by them. The plain version never enables the
hardware's Bluetooth / Wi-Fi radio; the Bluetooth version turns on Bluetooth only when you set BLUETOOTH ON.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; drum machine fork: 2026 DEADACTIVE; BLE MIDI:
2026 DEADACTIVE.
