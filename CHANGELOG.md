# Changelog

The drum firmware's versions (docs/VERSIONING.md). Newest first; notes go under "Unreleased" as work is merged to
`main`, and `python3 tools/version.py bump patch|minor|major` dates them with the new version.

## Unreleased

- SELECT turns the pages: SELECT does what PRESETS did (a section's pages, HOME 1/3 to COMP 2/3 and 3/3, the page
  under PERFORM, the menu's cursor, the Bluetooth page's rows), except on HOME's TRACKS mode, where it sets the
  tempo. PRESETS does nothing; ALGORITHM still picks the track.
- HOME has two modes: TRACKS (the mixer: TRACK LEVEL LEN PAN, the steps on the keys) and HOME (the scope, the
  selected track's knobs, the compressor after it). A HOME tap on HOME switches between them, from elsewhere it
  opens the one last shown; power-on shows TRACKS. REC no longer opens TRACKS.
- REC arms on every screen: a REC tap arms / disarms the selected track (arming while stopped starts play; `STOP
  TO EDIT` during a song). Kept: step record on STEP with KEYS on and stopped, and REC held for CLEAR TRACK on SEQ,
  DELETE ROW / CLEAR SONG on SONG. REC is no longer the mute modifier.
- The MUTE screen (GLO held): the eight tracks as boxes, filled = heard, outline = muted, a waiting mute blinking,
  the key under each; the white keys F3..F4 toggle them, the top C# / D# set MUTE: NOW / NEXT BAR. A GLO tap still
  opens GLOBAL. Replaces REC held + a key.
- The last project loads at power-on: every SAVE to flash remembers its slot; at power-on (not in safe start) it
  loads, stopped. If it can't, `LOAD FAILED: SLOT n` / `START NEW PROJECT?`: YES forgets the slot, NO asks again at
  the next power-on; both carry on with the power-on kit.
- PERFORM knob effects stay on: FILTER, CRUSH, THROW and DEPTH keep working after FX is let go or the PERFORM page
  closes, on every page, until turned back to 0 on the PERFORM screen (or TOOLS `INIT*`). An amber `FX` in the top
  bar shows while any is on. The black keys' effects still stop when let go.
- Metronome: GLOBAL 3/3 METRO OFF / LOW / MID / HIGH (a device setting, not in projects). While playing, a click on
  every quarter note, higher on the bar's first, after the PERFORM effects and at the MASTER volume, on the speaker,
  the headphones and USB audio.
- CPU: voices are shed at 95 % of an audio block (was 85 %). PRIO per track (EDIT, after CHOKE): HIGH / LOW, a LOW
  track's voices go first at a shed and at the voice cap (a held ACID note still last). A red CPU icon shows in the
  top bar for 2 s after a shed.
- Project format FDRC (for PRIO): older projects load as before, every track PRIO HIGH; projects saved by this
  build can't be read by 0.16.0. Project data only in RAM (a slot whose flash save failed) is not carried over.
- Key backlight: MENU BACKLIGHT OFF / ON (a device setting, default OFF). ON: every unlit key glows at about 5 %,
  the unlit steps 1, 5, 9 and 13 of the bank shown at about 25 % (wherever the keys are steps); lit keys stay full,
  the buttons unchanged. The LEDs are dimmed by the 10 kHz key scan (5-frame PWM, ~180 Hz).
- Device settings: the last project, METRO and BACKLIGHT are kept in spare bits. Going back to an older Hortator with
  any of them set: its SPEAKER EQ reads FLAT.
- Bluetooth version: the Bluetooth page gains **AUTOSTART** OFF / ON (after BLUETOOTH; KNOB 1 right ON / left OFF,
  OCT+ toggles; saved when the menu closes; default OFF). ON: 5 s after power-on, Bluetooth starts by itself as if
  BLUETOOTH ON were chosen, and reconnects to the remembered device. Not in a safe start (SEQ held). If the last
  Bluetooth start before this power-on did not complete or ended in a fatal error, it is skipped and the top bar says
  `BT AUTOSTART SKIPPED`; BLUETOOTH ON by hand still works. Kept in the palette word's bit 8 of the settings, by both
  versions: going back to an older Hortator with AUTOSTART ON resets its display settings (COLOR, SPEAKER EQ, ZOOM,
  KNOB ACCEL, USB LEVEL, PERFORM) to their defaults once (its COLOR check).
- Frozen baseline moved to `frozen-base-8` (user decision 2026-10-12): the frozen `main.c` gains one line, the key
  backlight's hook in the TIMER5 scan ISR right after `fm1_input_tick()` (`led_dim_tick()`, `firmware/src/led_dim.c`),
  the only way to dim LEDs while the audio ISR renders. `frozen-base-8` = `frozen-base-7` + that line + the file it
  calls; gate 2a accepts `main.c` differing from `drum-v0.16.0` by exactly that line (nothing else).

## 0.16.0 - 2026-10-11

- ACID: every step holds a note, SLIDE and TIE (format FDRB; older projects load and play as before, but projects
  saved by this build can't be read by 0.15.0); drum tracks play their steps transposed; ACID, a 303-style bass voice
  (TUNE DECAY CUTOF RESO ENVMD ACCNT WAVE GATE), plays them with accent, slide and tie on up to two tracks ("ACID: 2
  TRACKS MAX"). KEYS (an SCL tap): the keys play the selected track chromatically, OCT- / OCT+ the octave (-2 to +2;
  LAYER from EDIT and the LFO pages' OCT buttons need KEYS off, the STEP bank stays on KNOB 1, TRACKS keeps OCT as its bank); KEYS notes
  send no MIDI out; an ACID note (or a tied chain) over 5.5 s is cut. Live record (a REC tap on TRACKS arms the track,
  and starts the transport when stopped): a key writes the nearest step, a held key TIEs the later steps, a legato
  overlap SLIDEs (TIE / SLIDE on ACID tracks only). Step
  record: KEYS on, STEP page, stopped, a REC tap (KNOB 1..4 STEP NOTE TIME FLAG, EDIT a rest; REC, PLAY or leaving
  the page ends it). A loaded project keeps ACID on its first two ACID tracks, the others return as their power-on
  kit drum. A step the white keys turn on takes the last KEYS note on an ACID track, C4 on a drum (the last note
  resets on LOAD and on clearing the pattern). An ACID track's STEP page is a piano roll, the note shows in the footer; ACID's SOUND page shows its filter
  and the note's shape.
- A new start screen, CRT COLLAPSE: the HortatoR logo squashed through five dithered frames into one white scan line
  that bursts sideways, in the chosen COLOR (`assets/boot.png`, made by `tools/boot_variants.py --ship`; the study's
  other screens: `tools/boot_variants.py OUTDIR`). The logo, there and on ABOUT, is now UnifrakturMaguntia (SIL OFL).
- Releases can carry two versions: the plain firmware and the same build with Bluetooth MIDI. The web installer
  offers a choice (the plain one by default), the release a second file `hortator-X.Y.Z-bluetooth.fwsc`
  (`BLE=1 tools/publish.sh`; first used for the Bluetooth version with parts 2, 3 and 5).
- Bluetooth MIDI (the Bluetooth version only): MENU (HOME held) > BLUETOOTH turns Bluetooth on (off at every
  power-on), scans, lists the devices found (BLE-MIDI ones first) and connects to the one picked. It remembers it
  (saved) and reconnects whenever it is in range, also after a power-on once BLUETOOTH is ON; KNOB 1 left on DEVICE
  forgets it. MODE (saved): AUTO (SMC DAW for a device named `SMC…`, else MIDI), SMC DAW, MIDI. `BT CONNECTED` /
  `BT LOST` in the top bar. The console's `ble` stays for testing.
- The M-VAVE SMC-Mixer in its DAW mode: faders 1–8 = the track levels (pickup: a fader acts once it reaches the
  level; the mixer blinks it until then), knobs 1–4 = the selected track's first four knobs, MUTE / SOLO / REC /
  SELECT per track, PLAY / STOP / REC, BANK / CHANNEL ◀ ▶ the track; its LEDs show the mutes, solos, armed tracks,
  the selected track and the transport. Fader and knob moves record as motion on the selected track while armed.
- MIDI mode: the device's notes on the drum channel play the tracks, as USB and TRS; GLO CLK BLE follows its clock (a
  project saved with CLK BLE loads with CLK INT in the plain version).
- SOLO (from the SMC-Mixer): while any track is soloed only the soloed ones are heard; not saved, cleared by INIT,
  and cleared (nothing cut) when the SMC-Mixer disconnects or MODE leaves SMC DAW, as the FM-1 has no unsolo. Leaving
  SMC DAW while connected turns the mixer's LEDs off.
- MODE and the remembered device are kept in spare bits of the device settings, in both versions. Going back to an
  older Hortator after picking a device: its SPEAKER EQ reads FLAT.

## 0.15.0 - 2026-10-09

- Bluetooth MIDI, part 1 (a separate build, `FELUCCA_BLE=1`; the normal firmware does not contain it, gate 5): the
  JieLi BLE stack fits the app slot (424 KB, 157 KB free), runs in the main loop in 5 ms slices, and is driven from
  the USB console: `ble start / scan / list / connect N / stop`, `ble` for its status. Off at every power-on. Tested
  on the FM-1 with the M-VAVE SMC-Mixer: connected and receiving, no audio glitches, an update and UBOOT with
  Bluetooth on. Received MIDI is counted, not played yet (part 2). Docs: `docs/ble/FIT.md`, `docs/ble/DEVICE_STEPS.md`.
- Build: every build stops unless the update loader is the pinned one (`tools/frozen_base.txt`); a Bluetooth build
  also stops on its update-path and no-BT-at-boot gates.

## 0.14.0 - 2026-10-08

- FM: a 2-operator FM percussion model (TUNE DECAY INDEX RATIO, MDEC SWEEP FBK VEL): bells, metallic toms,
  woodblocks, zaps, FM kicks; 2 voices; the SOUND graph shows the ratio's sidebands.
- SEQ: the white keys are the selected track's steps on every SEQ page (STEP, PATTERN, MOTION, SONG) and on TRACKS
  (tap on / off, hold accent, OCT+ / OCT- the bank); ALGO or TRACKS' KNOB 1 picks the track. TRACKS mutes: REC held + a
  key (+ top C# / D#: now / next bar), was OCT- held.
- PERFORM: the knobs are the macros whenever the PERFORM screen shows (a held layer key no longer let them edit the
  page under it).
- FILTER: a filter on every track (FX > FILTER: TYPE OFF LP BP HP NOT, CUT, RESO, ENV, DECAY), after RESON, before
  DIST; each hit sweeps it by ENV (accents further); LFO DEST F.CUT / F.RES. Projects: format FDRA (older ones load
  with the filter OFF). BENCH: filter, compressor and sidechain cases.
- COWB: TUNE now moves its pitch (its own 540 / 800 Hz oscillators, the band-pass follows); before, it only moved
  the filter over fixed partials. At TUNE 0 it sounds as before.

## 0.13.1 - 2026-10-08

- Recovery from a Mac with no extra hardware: when the FM-1 stops answering both the web installer and M-UPGRADE,
  `tools/rescue/fm1_uboot.py` backs up its flash and puts M-VAVE's stock V15 back through the chip's boot mode
  ([docs/RECOVERY.md](docs/RECOVERY.md)). The installer page and the README link to the guide. No firmware change.

## 0.13.0 - 2026-10-08

- PHYS (upstream Felucca 1.0's physical models, MIT DaisySP): MEMB, a struck drum head as a drum model (TUNE DECAY
  TONE HEAD on HOME; POS, BEND, STICK; toms, timpani, tabla), and MODAL, a bell / bar / plate body as a RESON model
  (STRCT from harmonic to bell, DECAY its ring time as STRNG's; 2 tracks at most). The RESON MODEL knob now skips a
  model that is full. No project format change.
- BENCH: the console's `bench yes` plays the performance cases on the FM-1; `tools/fm1_bench.py` compares them with
  the host's (render time, CPU %, per case: every model, RESON model, the heavy kits and their FX).
- The start screen and ABOUT show the HortatoR logo (UnifrakturCook Bold, SIL OFL); the "untested" labels are gone.

## 0.12.0 - 2026-10-07

- SONG + NAME (upstream Felucca 1.0's song chain and naming): SEQ > SONG plays up to 16 rows of {project A..D,
  repeat x1..x16} with the sounds loaded now (each row: that project's steps, LEN / DIV / SWING / SRC and motion; a
  row lasts its longest track; LOOP ON / OFF; STOP brings the current pattern back). Projects get names (the PROJECT page's
  NAME knob opens the NAME screen: keys type phone-style, KNOB 1 / 2 cursor and character; SAVE saves as before);
  slots shown as A..D. Saved with the project (format FDR9; older projects load with no song and no name).

## 0.11.0 - 2026-10-07

- MOTION (upstream Felucca 1.0's motion recording): with a track armed (REC on TRACKS) and playing, its sound knobs
  turned are recorded per step and played back with the pattern (a value holds until the knob's next event; the
  patch comes back at the loop start and on STOP; the LFOs keep modulating on top). SEQ > MOTION: PLAY ON / OFF, the
  event count, CLEAR. 128 events for the 8 tracks, saved with the project (format FDR8; older projects load without
  motion).

## 0.10.0 - 2026-10-07

- PERFORM (upstream Felucca 1.0's FX hold layer): hold FX, then the black keys play master effects while held
  (REPEAT 1/8 1/16 1/32, REVERSE, TAPE STOP, LPF, HPF, FREEZE, OCT UP, OCT DN), the white track keys mute their
  track while held, KNOB 1..4 are FILTER / CRUSH / THROW / DEPTH (SHIMMER with OCT); a tap on FX still opens the FX
  pages (now on the release). MENU > PERFORM PAGE: holding FX opens a PERFORM screen that stays (keys and knobs
  work without FX) until another screen is chosen. REPEAT / REVERSE start on the 1/16 (the MIDI clock's when
  following); the buffer is the slicer's (743 ms). Nothing changes when the layer is idle.

## 0.9.0 - 2026-10-07

- MIDI clock in (upstream Felucca 1.0, adapted from contributions by ChanceTheMaker and keremimo): GLOBAL CLK INT /
  USB / TRS; the steps (every division), Grids, the slicer, synced LFOs and the delay follow the source's tempo,
  its Start / Continue / Stop drive the transport (step 0 on the first pulse; stops if the clock stops for 0.5 s).
- Exact step timing: each track carries its step-length remainder, so divisions never drift against each other
  (e.g. a 1/16 and a 1/4 track at 120 BPM) or against an external clock.

## 0.8.0 - 2026-10-07

- USB audio input (upstream Felucca 1.0, UAC1): the FM-1 records into a DAW over its USB cable (16-bit stereo,
  44.1 kHz; the headphone signal, SPEAKER EQ included). MENU USB LEVEL: MASTER (the recording follows the volume
  knob) or FIXED (always full level; the knob sets only the speaker / headphones, upstream 1.0.2 #42). Frozen
  baseline `frozen-base-6`.

## 0.7.0 - 2026-10-07

- Sound pack (upstream Felucca 1.0.2): MENU SPEAKER EQ FLAT / LOWCUT / BASS+ (the bass heard through its harmonics
  on the small speaker); SPRING reverb (shown SPRNG) beside ROOM on the new REVERB page (TYPE SIZE DAMP; chorus on
  its own CHORUS page); slow divisions 1/2, 1/1, 2BAR, 4BAR on PATTERN DIV and delay TIME, division knobs ordered
  by length. Projects are FDR7 (older ones load with ROOM).

## 0.6.0 - 2026-10-07

- TRS MIDI input on by default: notes from the jack play like USB's; clock bytes are queued for the coming MIDI
  clock feature (upstream Felucca 1.0, stage 2 step 4, frozen baseline `frozen-base-5`).

## 0.5.0 - 2026-10-06

The first numbered version: everything on `main` so far.

- Drum machine on Felucca: 8 tracks, drum models (808 / 909 kicks and snares, samples, FM and more), step
  sequencer with per-track length, swing, live recording, quick mutes, MUTE NEXT BAR.
- M2: step PROB / RATCH, Grids (Mutable Instruments) pattern generator as a track source.
- M3: Streams compressor with sidechain ducking keyed by a track, COMP GHOST (MUTE / KEEP / HIDE).
- Two LFOs per track (10 waves with morph, SYNC / Hz / TIME rates, one destination each).
- RESON: per-track resonator insert (STRNG / PIPE / CHORD).
- PRESET knob turns the current section's pages; the regrouped EDIT / COMP / FX pages.
- From upstream Felucca 1.0-1.0.2: safe fixes (calm knob acceleration, swing cap, STOP TO SAVE, installer
  resume); key debounce and encoder fix (#23); TIMER5 nesting; USB-MIDI driver; storage hardening (sequence wrap,
  slot check, no truncated loads, full header read-back) and the erase order that keeps a save silent.
- Project format FDR6; older projects are converted on load.
