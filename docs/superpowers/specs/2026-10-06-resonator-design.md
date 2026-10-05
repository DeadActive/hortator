# RESON — a per-track resonator insert for experimental drum sounds — design

Date: 2026-10-06. Parent: `2026-10-05-drum-core-m1-design.md` (§2 safety rules, the frozen files, no device
writes by Claude). Builds on M3 (`2026-10-05-m3-compressor-design.md`), the UI regrouping and the LFOs
(`2026-10-06-lfo-design.md`), all merged to main at 3988271.

## 1. Goal and user decisions

Turn drum hits into pitched, ringing, metallic or chordal sounds: every track gets a resonator insert that its
hits excite.

User decisions (brainstorming):
- **One RESON insert with a MODEL knob.** First version: the comb family — **STRNG** (a string), **PIPE** (a
  tube, odd harmonics), **CHORD** (four sympathetic strings). The modal models (BELL, BAR, DRUM, PLATE) come in a
  later step; the MODEL list leaves room for them without changing projects.
- **Pitch:** its own **TUNE** knob now; the design leaves room for a per-step pitch offset later (the 303
  note-per-step plan, `docs/IDEAS.md`).
- **Position in the chain:** before DIST — engine → **RESON** → DIST → SLICER → (compressor) → LEVEL / PAN /
  sends.
- **LFOs can modulate RESON's knobs** (new DESTs); on the pitch the LFO **sweeps smoothly**.
- **Pages:** in FX, after SLICER.
- **CHORD:** a CHORD knob with many chord types; **at most 2 tracks** use CHORD at a time; its lowest string is
  ~C3 (no extra memory).
- Memory approach **A**: one fixed delay buffer per track, shared by the models.

## 2. Sound

- `firmware/src/reson.c`, integer only. Per track, per sample, in `mix_part` between the engine's render and
  `track_dist`.
- Input (the excitation): the track's rendered sound (engine voices and the sample layer) for the block.
  Output: `dry × (1 − MIX) + ring × MIX`.
- **Memory:** per track one delay buffer of 1352 `int16_t` samples (~2.7 KB; 21.6 KB for 8 tracks), not part of
  projects.
  - **STRNG:** one line over the whole buffer: a feedback comb (Karplus-Strong) with a damping filter in the loop;
    lowest pitch ~C1 (32.7 Hz).
  - **PIPE:** the same comb with inverted feedback (odd harmonics only); for the same pitch it needs half the
    delay (half the buffer). TUNE has the same range (C1 … C7) on STRNG and PIPE.
  - **CHORD:** the buffer as 4 lines of 338 samples, each a STRNG tuned to one note of the chord; the lowest
    string ~C3 (130.8 Hz). TUNE below C3 plays as C3 on CHORD.
- **Pitch:** TUNE (semitones) + a fine offset in 1/256 semitone (from an LFO on `R.TUN`; later also a per-step
  offset), turned into a fractional delay length; the lines are read with interpolation, so the pitch is in tune
  at high notes and glides smoothly under the LFO.
- **DECAY:** the ring's time to fall 60 dB, ~10 ms … ~10 s, exponential over the knob; the loop gain per period
  is derived from it and capped just below 1 at the top (almost self-sustaining, never growing).
- **TONE:** the damping filter's cutoff in the loop (dark, muted … bright, metallic).
- **STRCT** (STRNG, PIPE): stiffness — a first-order all-pass in the loop stretches the overtones (string →
  bell-ish / metal bar-ish). On CHORD this knob is **CHORD** (below).
- **POS:** the pickup position — the output is the sum of two taps of the line (at 0 and at POS × the length),
  which thins out harmonics like a pickup along a string; no extra memory.
- **CHORD types** (4 notes, one per string, above TUNE; a triad's 4th string is the root an octave up):
  `OCT` `5TH` `4TH` `MAJ` `MIN` `SUS2` `SUS4` `DIM` `AUG` `MAJ6` `MIN6` `MAJ7` `MIN7` `DOM7` `M7b5` `DIM7` `7SUS4`
  `ADD9` `QUART` (stacked 4ths) `CLUST` (semitone cluster). Changing it retunes the strings at once (the ring
  goes on).
- **Ring tail:** after the last voice the track keeps rendering while the ring is above a threshold (as the
  DIST tail today); below it the resonator stops computing and is cleared.
- **MODEL OFF:** no resonator code runs; the output is bit-identical to today.
- **Mute:** a muted track's ring stops with its voices (the mute's cut clears the resonator with the declick).

## 3. Controls and screens

- FX family: FX → SLICER → **RESON 1/2** → **RESON 2/2** → DLY → REV/CHO.
- **RESON 1/2:** `MODEL` (OFF, STRNG, PIPE, CHORD), `TUNE` (C1 … C7, semitones), `DECAY` (ms / s), `MIX` (%).
- **RESON 2/2:** `TONE`, `STRCT` (on CHORD: `CHORD` with the chord names), `POS`.
- Defaults: MODEL OFF, TUNE C3, DECAY middle (~0.5 s), MIX 50 %, TONE bright-ish, STRCT 0, POS middle.
- **CHORD cap:** at most 2 tracks with MODEL CHORD. On a 3rd track the MODEL knob skips CHORD and the screen says
  `CHORD: 2 TRACKS MAX`; switching a CHORD track to another model frees its place at once. A loaded project with
  more than 2 CHORD tracks: the first two keep CHORD, the others play as STRNG.
- **Picture:** the model name in large type and the pitch (`C3`; on CHORD the chord's notes); a sketch of the
  ring's harmonics (STRNG all, PIPE odd only, CHORD the 4 notes); a live meter of the ring level.
- White keys on the RESON pages select the track, as on the other per-track pages.
- Labels ≤ 5 characters.

## 4. LFO destinations

- LFO DEST gains, after `LVL` (9) and `PAN` (10): `R.TUN` (11), `R.DCY` (12), `R.MIX` (13), `R.TON` (14),
  `R.STR` (15), `R.POS` (16). MODEL is not a destination.
- `R.DCY` … `R.POS` work as the other destinations (the modulated copy of the knob, clamped to its range).
- `R.TUN` is smooth: it does not move TUNE in semitones; the LFO writes the resonator's fine pitch offset
  (± DEPTH × 2 octaves at full DEPTH, in 1/256 semitone) every block.
- A project's existing DESTs (0 … 10) keep their meaning.

## 5. Data and projects

- 7 new per-track parameters (`P_RMODEL`, `P_RTUNE`, `P_RDECAY`, `P_RMIX`, `P_RTONE`, `P_RSTRCT`, `P_RPOS`),
  appended before `P_COUNT` (after the LFOs).
- Project format **`FDR5`**: FDR4 plus the RESON parameters; `FDR4`, `FDR3`, `FDR2`, `FDR1` records load with
  MODEL OFF (they sound as saved). Every loaded value is clamped; the CHORD cap is applied on load. One flash
  sector (`_Static_assert`): about 3024 B of 3840.
- Track state (not saved): the delay buffer, write position, the damping / all-pass filter states, the ring
  level, the fine pitch offset.
- No frozen file changes; no change to the update path.

## 6. Cost and safety

- Estimates (host instructions per sample per track while it rings): STRNG / PIPE ~20, CHORD ~80. Worst case to
  measure: all 8 tracks ringing, 2 of them CHORD, every FX on — against `tests/drum_cost_ref.txt` (realistic
  heavy kit within `ref`, the extreme case within `extreme_max`). If it goes over, Claude stops and asks the user
  with the numbers.
- The target cost check (`tests/target_budget.txt`) gets a line for the resonator's per-sample loop.
- Integer only, no float, no 64-bit division; bounded loops; the loop gain is capped below 1 and the line
  values are clamped, so the ring cannot run away; the idle cost (all MODEL OFF) stays within H4.
- RAM: +21.6 KB (`.bss`), with RAM at 43 KB of 96 KB today.

## 7. Testing

- Pitch: STRNG, PIPE and every CHORD string within a few cents of TUNE (+ the chord's intervals) across the
  range; PIPE has odd harmonics only.
- DECAY: the ring's 60 dB time against the knob; DECAY at max stays bounded for minutes of input.
- MIX 0 % equals the dry sound; TONE changes the brightness; STRCT stretches the overtones; POS changes the
  harmonic balance.
- CHORD: each type sounds its 4 notes; the cap (knob skips CHORD on a 3rd track, the message; on load the 3rd
  plays as STRNG; freeing a place).
- LFO: `R.TUN` sweeps smoothly (no clicks, no steps); the other R. DESTs modulate and clamp.
- MODEL OFF bit-identical (goldens unchanged); the ring tail keeps a track rendering and then stops.
- Projects: FDR5 round trip; FDR4 / 3 / 2 / 1 converted (RESON off); garbage clamped (H1).
- UI: the RESON pages, the STRCT / CHORD label, the cap message, the DEST names; screenshots for the user.
- Listening: WAVs of a kit through STRNG, PIPE and CHORD, a chord change while ringing, an `R.TUN` LFO sweep,
  RESON into DIST.
- H1–H4 as for M2 / M3 / LFO.

## 8. Done when

All of §7 passes in `tests/run_tests.sh`, the firmware builds (`DRUM_PACKAGE=1`), and the user has looked at the
screenshots and listened to the WAVs.

## 9. Out of scope (now)

The modal models (BELL, BAR, DRUM, PLATE — the next step), per-step pitch (with the 303 plan), stereo
resonators, external audio input as the excitation, RESON as a master or send effect.
