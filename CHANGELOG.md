# Changelog

The drum firmware's versions (docs/VERSIONING.md). Newest first; notes go under "Unreleased" as work is merged to
`main`, and `python3 tools/version.py bump patch|minor|major` dates them with the new version.

## Unreleased

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
