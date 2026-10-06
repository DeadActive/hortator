# PERFORM layer (roadmap §3.4) — design

Date: 2026-10-07. Parent: `docs/UPSTREAM_1.0.2.md` §3.4. Upstream reference: Felucca 1.0 `727f272`
`firmware/src/perform.c` (579 lines, all of it), its hooks in `fx.c` (`perf_begin`, `perf_mute`, `perf_pre`,
`perf_master`), `seq.c` (`keyboard_block`, `perf_key`), `slicer.c` (`sl_lent`), `ui_layer.c` (the FX layer only)
and `ui_input.c` (`perf_kill`). Branch `perform` from main `4d32869` (`drum-v0.9.0`). No frozen file changes.

## 1. Goal and user decisions

Holding FX turns the keyboard and KNOB 1..4 into a live effects layer on the master: stutter repeats, reverse,
tape stop, freeze, filters, octave shifting, bit crush, a throw into the delay / reverb, and momentary track
mutes. Letting go leaves the sound exactly as before.

User decisions (2026-10-07):
- **All of upstream's PERFORM**: the 10 effects, the 4 knob macros, the mutes (8 tracks here, 4 upstream).
- **Key layout:** the white keys F3..F4 (the keys that play tracks 1..8, `KEY_TRK_KEY`) mute their track while
  held; the black keys play the effects, in this order (TAPE STOP moved after REVERSE):

  | key | F#3 | G#3 | A#3 | C#4 | D#4 | F#4 | G#4 | A#4 | C#5 | D#5 |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | effect | REPEAT 1/8 | REPEAT 1/16 | REPEAT 1/32 | REVERSE | TAPE STOP | LPF | HPF | FREEZE | OCT UP | OCT DN |

  F#5 and the white keys G4..G5 do nothing.
- **The buffer:** the SLICER's recordings (`sl_buf`, 8 x 4096 int16 = 64 KB) borrowed, as upstream. With 8
  tracks the loop is 16384 stereo frames at 22.05 kHz (743 ms, upstream 371 ms): REPEAT 1/8 / REVERSE run from
  41 BPM. While borrowed, STUT tracks play live; their recordings are dropped when it is given back. No new RAM.
- Version after the merge: **0.10.0**.

## 2. The FX button: tap or hold

- **Tap** (let go within 0.4 s, nothing else touched meanwhile): the FX pages, as today, but on the release
  (today: on the press).
- **Hold:** the layer. A key or a knob touched with FX down opens it at once (a combo: no tap afterwards); FX
  held alone 0.4 s shows the map (letting go then does nothing).
- Keys pressed while FX is down are the layer's (`seq.c keyboard_block`): no note, no MIDI out, not recorded,
  not the STEP grid's, TRACKS', COMP's or a track select (`ui_input.c` skips them). Keys held before FX keep their
  notes. A layer key keeps its effect until it is let go, even after FX is (the map stays while one is held).
- **Off:** in the menu, a confirm dialog or the UPDATE MODE countdown no layer opens and every effect is off until
  its keys are let go (upstream's `perf_kill`). FX pressed there stays a dead press (no effect, no map).
- Not taken: upstream's other quick layers (GLO / SCL / EDIT, skipped in §3), the GLO solo, the HOLD time menu
  (fixed 0.4 s, as the STEP grid's hold), the "HOLD [FX] QUICK" hint.

## 3. The effects (upstream's, ported)

- **Mutes** (white keys): the track's signal ramps to 0 over 2.9 ms (`SL_SLOPE`) after the SLICER, before its
  level, pan and sends, and back when let go. `P_MUTE`, MUTE NEXT BAR and the project are untouched.
- **REPEAT 1/8, 1/16, 1/32, REVERSE** (a 1/8 played backwards, looped): start on the next 1/16 (at once while
  stopped), record that length of the master, then loop it, windowed at both ends. A shorter REPEAT over a
  recorded loop switches with a cross-fade. A length longer than the loop at the tempo does nothing (the map
  shows it dimmed).
- **TAPE STOP:** reads behind the recording slower and slower, to silence in one beat (at most 32000 samples),
  the last eighth fading out.
- **FREEZE:** records 186 ms, then two overlapping 46 ms grains at random places in it, triangle windows.
- **OCT UP / OCT DN:** the rotating-delay harmonizer (two taps half a sweep apart, raised-cosine cross-fade):
  0.56 the live mix + 0.7 the shifted copy. While it plays, KNOB 4 is SHIMMER (the shifted copy fed back,
  0 .. 0.82, soft-clipped).
- **LPF / HPF:** a ZDF state-variable filter, 30 Hz .. 15.4 kHz; the LPF key sweeps down from open over a bar,
  the HPF key up, gliding ~45 ms.
- Effects of different kinds stack. Of the buffer effects (REPEAT, REVERSE, TAPE STOP, FREEZE, OCT) the last
  pressed plays; letting it go returns to the one held before (faded out, then the next started).
- **Knob macros** (KNOB 1..4 with FX held; never saved or recorded; back to off when FX is let go):
  FILTER -100..100 (left: LPF, right: HPF), CRUSH 0..100 (fewer bits, a held sample), THROW 0..100 (the dry mix
  into the delay and reverb sends), DEPTH 100..0 (the buffer effect's level; SHIMMER while OCT plays).
- **Chain:** mutes in `mix_part` after `slicer_track`; THROW into the sends before `fx_buses`; on the master
  after its level (`mg`: with USB LEVEL FIXED the effects sit before the master knob, so the computer gets them)
  and before `master_out`: buffer effect -> LPF -> HPF -> CRUSH.
- **Idle** (no key, no knob, no ramp or fade left): every stage is skipped; the output is bit-identical to today.

## 4. Differences from upstream

- **The 1/16 for REPEAT / REVERSE** is the Grids clock's (`gclk`): `grids_tick` notes where in the block a 1/16
  started. Upstream counts its own `beat / 4` (truncated), which drifts against our exact steps (0.9.0) by up
  to a sample a 1/16 and does not follow MIDI clock pulses; `gclk` does both, and carries the global swing as the
  steps do. Stopped, or playing but waiting for a clock's first pulse: as upstream stopped / playing (at once /
  on the next 1/16).
- **8 tracks:** 8 mute bits, 8 mute gains; the loop sized from `NTRK * SL_LEN` (16384 frames).
- **State:** the new state goes in perform.c's `pf` struct and a few flags shared between the UI and the render
  (`perf_held`, `perf_act`, `perf_kill`, `perf_k`, `kb_layer`, `perf_mask`), as upstream. H2 is checked after each
  task (new small globals have broken it before; if one does, it moves into a struct).
- `perform.c` is included by `felucca.c` (one translation unit) after `slicer.c` and `fx.c`'s helpers it uses,
  as upstream; the header comment keeps upstream's copyright and gains the DEADACTIVE fork notes.

## 5. The screen and LEDs

- **While the layer shows:** the header "[FX] HOLD"; a key map: the 10 effect cells (R 1/8, R 1/16, R 1/32, REV,
  TAPE, LPF, HPF, FRZ, OCT+, OCT-) laid out as their black keys, and the 8 track cells (track icons / numbers)
  as their white keys. Held: lit; running (a 1/16 one started): highlighted; unavailable at the tempo: dimmed;
  muted track: marked. Below, the four knob columns FILTER (LPF / HPF / OFF), CRUSH, THROW, DEPTH or SHIMMER.
- **Key LEDs** (as upstream): effect keys blinking, held keys lit, an unavailable REPEAT / REVERSE dark; the track keys
  lit while their track sounds, dark while muted by the layer.

## 6. Testing (host, failing first)

New `tests/perform_test.c` (drum suite), driving `fm1_in.notes` / `fm1_in.buttons` and the knob encoders, and
rendering blocks:
- idle: FX tapped, held alone, knobs at 0: the rendered output equals a render without the layer (and
  `sound_pack_test`'s kit hashes stay as pinned);
- keys with FX held: no note on the track, no MIDI out, nothing recorded (live REC armed), no step toggled; a key
  held before FX still plays; FX tap opens the FX pages on the release, a hold or a combo does not;
- each mute: the track's contribution goes to 0 within 128 samples and comes back; the others untouched;
- REPEAT 1/16 at 120 BPM: starts on the next 1/16 (playing) or at once (stopped), the output then periodic with
  period beat / 4; 1/8, 1/32 likewise; REVERSE: the loop's samples in reverse order; a 1/8 at 40 BPM does nothing
  (unavailable), at 41 BPM works;
- REPEAT under a MIDI clock (CLK USB, simulated pulses as `midi_clock_test`): starts on the step's 1/16;
- TAPE STOP: silent within one beat; FREEZE: output continues, with the input silenced; OCT UP / DN: a sine's
  zero crossings double / halve in the shifted copy;
- LPF / HPF keys and FILTER: a high / low test tone attenuated; CRUSH: quantized output; THROW: the delay /
  reverb sends get the dry mix; DEPTH: the buffer effect's share;
- stacking: two buffer effects, the last pressed plays, letting it go returns to the other;
- the buffer: STUT records nothing while lent; given back, its recordings are dropped and it records again;
- off: the menu opened with an effect held: silent of effects until the key is let go;
- `ui_test`: screens of the map (idle, keys held, an unavailable REPEAT, OCT with SHIMMER) and the LEDs.

For the user's ears and eyes: a drumsim WAV per effect on the demo kit (`pf_*.wav`) and the map screenshots.
The target budget gains PERFORM's lines (`perf_begin`, `perf_mute`, `perf_pre`, `perf_block`, `perf_master`, the
harmonizer): the user approves them before they are written (upstream's: 28 / 21 / 27 / 814 / 31).

## 7. On the device (the user)

- Hold FX: the map; each black key's effect while held, on the 1/16 for REPEAT / REVERSE; white keys mute.
- The knobs with FX held; let go: everything back, no click.
- Tap FX: the FX pages. Keys pressed with FX make no notes.
- A STUT slicer track plays on while an effect uses the buffer and stutters again afterwards.
- With a MIDI clock (CLK USB): REPEAT on the DAW's grid.

## 8. Done when

§6 passes, the firmware builds, the frozen / loader / stack checks are green, the budget lines are approved,
CHANGELOG has the line, DEVICE_INSTALL.md has a PERFORM section, the user has checked §7; merged as 0.10.0 (tag
`drum-v0.10.0`).

## 9. Out of scope

Upstream's GLO / SCL / EDIT layers and the GLO solo, recording the effects (motion recording is §3.5), MIDI
control of the effects, the HOLD time setting, effects on separate tracks.
