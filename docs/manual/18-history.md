# Version history
<!--blurb: When each feature arrived, and which versions can read which projects.-->

The full changelog is `CHANGELOG.md` in the repository. This is a feature map by version, newest first. The current version is shown on MENU, ABOUT.

## 0.26.0

- **SYNC**: MIDI clock out to USB (and BLE or BOTH in the Bluetooth version).
- **ROUT**: NOTE, SEL and TRACK routing of incoming notes. **THRU**.
- **Live cue and variations**: queue the next slot or variation (black keys, cue map on SAVE held); 8 variations per slot.
- **FILL** probability and the REC-hold fill; **COPY / PASTE** a slot.
- **Edits stay in RAM until SAVE**; unsaved slots marked `*`.
- USR2 shrinks to 48 KiB (variations live in the rest of its flash).
- Bluetooth version: a new Bluetooth page (ROLE, MODE, RECONNECT, up to 8 saved devices, ROLE DEVICE, FM-Link).
- Steadier key backlight.

## 0.25.0

- **USB audio 2 or 10 channels** (MENU, USB AUDIO).
- The landing page gets a panel-map manual.

## 0.24.0

- **Parameter locks** and ENV STEPS / TRACKS.
- MOTION raised to 1024 events (shared with locks). USR3 shrinks to 48 KiB. Projects saved now read as empty on 0.23.x.

## 0.23.x

- 0.23.3: faster knobs (ACCEL x2 / x4 / x8), a better knob decoder, step keys that do not toggle when held, **RESON KEYF**, MUTE NEXT BAR with an external clock, USB name Hortator, COLOR inverses.
- 0.23.2: **REC ALL**; REC no longer starts play.
- 0.23.0: renamed engines, the TRX classic power-on kit, **LFO DEST** with four slots, accent with OCT+, breathing OCT buttons, MENU, ABOUT credits, 151 presets and 128 user slots, the Machinedrum-style screen.

## 0.22.0 to 0.17.0

- **0.22**: a faster screen redraw and a new classic-LCD look.
- **0.21**: **presets**: four banks, the PRESETS list, USER bank.
- **0.20**: eight **EFM** FM voices (BDEFM to CYEFM).
- **0.19**: thirteen **TRX** voices (BDTRX and others).
- **0.18**: **MIDI LEARN** (16 CCs, then 32 controls and actions).
- **0.17**: SELECT turns pages; HOME and TRACKS modes; **MUTE screen**; PERFORM knob effects stay on; the last project loads at power-on; **metronome**; **PRIO**; key backlight; Bluetooth AUTOSTART.

## 0.16.0 to 0.13.0

- **0.16**: **ACID**, notes, SLIDE and TIE, **KEYS**, live and step record, the CRT collapse start screen, two builds per release, Bluetooth MIDI, the SMC-Mixer and SOLO.
- **0.15**: the first Bluetooth MIDI build.
- **0.14**: the **FM** model, white keys as steps on every SEQ page, **FILTER** on every track, COWB tune fix.
- **0.13**: **PHYS** (SKIN model, MODAL resonator); 0.13.1: the Mac recovery tool.

## 0.12.0 to 0.5.0

- **0.12**: **SONG** (16 rows, A to D, x1 to x16, LOOP) and **NAME**.
- **0.11**: **MOTION**.
- **0.10**: **PERFORM** (hold FX).
- **0.9**: MIDI clock in (CLK INT / USB / TRS), exact step timing.
- **0.8**: USB audio input (16-bit stereo, 44.1 kHz) and USB LEVEL.
- **0.7**: SPEAKER EQ (LOWCUT, BASS+), SPRING reverb, chorus page, slow divisions.
- **0.6**: TRS MIDI input on by default.
- **0.5**: the first numbered release: 8 drum tracks, step sequencer with PROB and RATCH, Grids, the compressor, two LFOs per track, RESON.

## Compatibility notes

- Projects saved by 0.17 or later cannot be read by 0.16.0 or older (older projects load with every track PRIO HIGH).
- Projects saved by 0.24 or later show an empty slot on 0.23.x (`LOAD FAILED` at power-on); going back is safe but not supported.
- A project saved by 0.26 loads on 0.25 and 0.24 without its variations.
- Going back to an older Hortator after setting the last project, METRO, BACKLIGHT or Bluetooth AUTOSTART can reset some display settings once (for instance SPEAKER EQ reads FLAT).
- Projects saved with CLK BLE, CLK LINK, SYNC BLE or BOTH load with CLK INT, SYNC OFF or USB in the plain version.
