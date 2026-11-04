# Version history
<!--blurb: When each feature arrived, and which versions can read which projects.-->

The full changelog is `CHANGELOG.md` in the repository. This is a feature map by version, newest first. The current version is shown on MENU, ABOUT.

## 1.0.1

- **Newer FM-1s save again**: FM-1s sold recently carry a different flash chip of the same size. Hortator now recognises it, so projects, presets and settings are saved on these units.

## 1.0.0

- **Web editor**: edit the FM-1 from the browser over USB: Sound (every page, both ways), Presets, Projects (backups and restores), Patterns (steps, locks, remote cue, copy a variation) and Samples (your own sounds, Chop). See the Web editor section.
- **SAMPLE model**: SLOT steps through a set's samples (it replaces KEY); START and END play part of a sample; ATK, SUS and REL shape the hit; LOOP is OFF, FWD, PING, LOOPI or ONE; PLAY is KEY or CHOP (the 16 white keys play a chopped set); CUT lets each hit cut the previous one. The LAYER knobs are SET, SLOT, LEVEL, TUNE and DECAY.
- **User sample slots hold sets** of up to 16 samples; the editor's Samples tab writes them. Old projects and presets load with KEY turned into SLOT and sound the same.
- **USER presets keep the track's FX** (sends, FILTER, INSERT, SRR, RESON), in **96 slots** (was 128).
- **INSERT effect and SRR** on every track (SOFT, HARD, FOLD, FUZZ, CRUSH, PHASR, FLANG, CHOR; sample rate reduction down to 347 Hz).
- **More LFO destinations**: SRR, DST, the CHO, DLY and REV sends, the INSERT's MIX, A, B and C.
- **Delay**: dotted and triplet times and a ping-pong MODE. **Reverb**: a third type, HALL (stereo); the ROOM tail now fades to true silence. **Graphs** for DLY, REVERB and CHORUS. Their settings moved to GLO pages 4 to 7.
- **NUDGE**: move a step's hit early or late (-50 to +50 hundredths of a step), with a mark in the grid. **QUANT** (GLOBAL 3/7 KNOB 4) sets where live recording puts a hit: 1/16, 1/8, 1/32 or OFF.
- **Step tools** (hold SEQ): copy and paste a step, a page or a selection; replace, merge, fill, duplicate and clear.
- **REPEAT STEP** on the PERFORM keys replaces the track mutes there (use the MUTE screen); the last four white keys pick the rate.
- **Solo** on the MUTE screen (black keys), toggle or momentary.
- **Step editing on every step page**: holding a step turns KNOB 1 to 4 into PROB, RATCH, ACCENT and NUDGE on TRACKS, PATTERN, MOTION and SONG too.
- **USB audio 10 CH**: the tracks T1..T8 now come first (channels 1 to 8) and the stereo pair is on 9 and 10. Re-pick the inputs in your DAW if you set them up before.
- **Fewer dropouts under heavy load**: a voice is stopped when an audio block takes more than 85 % of its time (was 95 %).
- Projects saved by 1.0.0 do not load on 0.26.0 or older (they show an empty slot there).

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

- Projects saved by 1.0.0 or later (they carry NUDGE) show an empty slot on 0.26.0 and older (`LOAD FAILED` at power-on). Going back is safe but not supported. Every older project loads on 1.0.0, its steps at NUDGE 0, its SAMPLE tracks with KEY turned into SLOT (same sound).
- 1.0.0 keeps 96 USER presets (was 128). At the first start, slots 97 to 128 move into the first empty slots; if all 96 are full the rest are lost and the screen says how many (`3 PRESETS LOST`). A project that used a moved preset follows it; one that used a lost preset shows `PRESET -` with its sound unchanged. Presets saved before 1.0.0 have no FX and load as the sound only until you save over them.
- An older firmware shows the new USER bank as empty, and a preset saved there replaces the whole bank. The editor reads such an FM-1 but will not write it: update first.
- Editor backups and preset files from before the FX (128 presets) and test-build backups from before NUDGE still open and restore. Files the editor makes now are refused by an FM-1 on an older firmware (`UPDATE THE FM-1 FIRST`) and by an older editor.
- USB audio 10 CH: the channel order changed in 1.0.0 (T1..T8 on 1 to 8, the pair on 9 and 10).
- PERFORM: FX + a white key no longer mutes a track; use the MUTE screen (hold GLO). A controller button learned to a PERFORM mute now holds that track's REPEAT STEP.
- Projects saved by 0.26 load on 0.25 and 0.24 without their variations. Projects saved by 0.24 or later show an empty slot on 0.23.x. Projects saved by 0.17 or later cannot be read by 0.16.0 or older.
- Going back to an older Hortator after setting the last project, METRO, BACKLIGHT or Bluetooth AUTOSTART can reset some display settings once (for instance SPEAKER EQ reads FLAT).
- Projects saved with CLK BLE, CLK LINK, SYNC BLE or BOTH load with CLK INT, SYNC OFF or USB in the plain version.
