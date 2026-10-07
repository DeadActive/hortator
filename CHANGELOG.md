# Changelog

The drum firmware's versions (docs/VERSIONING.md). Newest first; notes go under "Unreleased" as work is merged to
`main`, and `python3 tools/version.py bump patch|minor|major` dates them with the new version.

## Unreleased

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
