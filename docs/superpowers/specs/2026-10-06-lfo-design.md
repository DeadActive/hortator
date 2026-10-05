# LFOs — two per track, assignable to the track's knobs — design

Date: 2026-10-06. Parent: `2026-10-05-drum-core-m1-design.md` (§2 safety rules, the frozen files, no device
writes by Claude). Builds on M3 (`2026-10-05-m3-compressor-design.md`) and the UI regrouping on branch
`ui-regroup` (EDIT holds the track's knobs, LAYER inside EDIT; the LFO button is free).

## 1. Goal and user decisions

Movement inside a pattern without programming it step by step: every track gets two LFOs, each modulating one of
the track's knobs.

User decisions (brainstorming):
- **2 LFOs per track**, on the **LFO** button.
- Waveforms: SQUARE, SAW, REV SAW, SINE, TRIANGLE, S&H, WANDER, EXP UP, EXP DN, RANDOM WALK.
- **MORPH** per waveform (the user's list, plus the agreed extras): see §2.
- **RATE** in three modes, **OCT+ cycles SYNC → Hz → TIME**, the mode shown on screen; ranges as proposed.
- **PHASE** (start offset).
- Routing: **one DEST + bipolar DEPTH per LFO**; DEST = the track's engine knobs, LVL or PAN (not MODEL, NOTE,
  CHOKE).
- **TRIG** per LFO: FREE / HIT / PLAY.
- Approach **A**: a modulated copy of the track's knobs, updated every audio block; the stored knobs untouched.

## 2. The LFOs

- Per track, LFO 1 and LFO 2, each with WAVE, RATE MODE, RATE, MORPH, DEPTH, DEST, TRIG, PHASE.
- WAVE and what MORPH (0..127) does:

| WAVE | MORPH |
| --- | --- |
| SQUARE | softens the corners (0 = hard square, max = smooth, near a sine) |
| SAW | tension: the ramp's curve (0 = straight) |
| REV SAW | tension, as SAW |
| SINE | towards a square (the opposite of SQUARE's morph; 0 = pure sine) |
| TRIANGLE | tides: skews the peak (middle = triangle, the ends = rising / falling ramps) |
| S&H | smoothing: 0 = instant change at each new value, max = full glide (more = slower retune) |
| WANDER | speed of change relative to the rate |
| EXP UP | curvature (0 = straight rise, max = strongly exponential) |
| EXP DN | curvature, falling |
| RANDOM WALK | step size, from tiny drift to large jumps |

- Output: bipolar, −1 … +1 (full scale), the same for every waveform at every MORPH.
- RATE MODE / RATE:
  - **SYNC**: 8 bars, 4, 2, 1 bar, 1/2, 1/4., 1/4, 1/4T, 1/8., 1/8, 1/8T, 1/16., 1/16, 1/16T, 1/32, 1/32T, 1/64:
    one cycle per division at the song tempo.
  - **Hz**: 0.02 … 40 Hz, exponential over the knob.
  - **TIME**: one cycle takes 25 ms … 60 s, exponential over the knob.
  - OCT+ on an LFO page cycles that LFO's mode; the RATE knob keeps its position (re-read in the new mode).
- TRIG: **FREE** never restarts (runs from power-on); **HIT** restarts on every hit of its own track (sequencer,
  keys, MIDI, Grids, ratchet hits); **PLAY** restarts at PLAY (SYNC LFOs then stay on the bar).
- PHASE 0 … 360°: where a restart starts the cycle; in FREE it shifts the running cycle.
- DEST: OFF, or one of the track's engine knobs (the 4 main knobs and its extras), LVL, PAN. DEPTH −100 … +100 %:
  the knob moves by DEPTH × LFO × the knob's range around its set value; the result is clamped to the knob's range.
  Both LFOs on one knob add before the clamp. DEST OFF or DEPTH 0: the LFO is not computed.
- A DEST the track's current engine does not have (an extra knob after an engine change) does nothing until it
  exists again; the setting is kept.

## 3. How modulation acts

- Every audio block (32 samples), before the engines render: each track's LFOs advance and a modulated copy of the
  track's knobs is written (only the knobs an LFO targets differ from the stored values).
- The engines, the sample layer and the mixer read the modulated copy; knobs read while a sound plays (LVL, PAN, the
  Plaits engines' continuous controls) move within it; knobs read when a hit starts take the value of that moment.
- The stored knobs (what the user set, shows, saves) never change.
- S&H / WANDER / RANDOM WALK use a random generator per track, reseeded at PLAY: a session repeats exactly.

## 4. Pages and screen

- LFO button: two pages, `LFO 1/2` `WAVE` `RATE` `MORPH` `DEPTH` and `2/2` `DEST` `TRIG` `PHASE`, of the LFO shown
  (revised 2026-10-06 by user decision; was four pages, two per LFO).
- OCT− on an LFO page: shows LFO 1 ↔ LFO 2 on the same page. The picture names the LFO in large type; the LFO
  button blinks while LFO 2 is shown. The choice is screen state (not saved) and stays when a white key selects
  another track.
- OCT+ on an LFO page: the shown LFO's RATE MODE SYNC → Hz → TIME; the RATE column shows `1/8T` / `2.5 Hz` /
  `450 ms`, and the picture `RATE: SYNC` / `Hz` / `TIME`.
- Picture: one cycle of the waveform as morphed and phase-shifted, a dot at the LFO's live position, and the
  routing, e.g. `LFO 1 → TONE +40 %`, with TRIG. S&H / WANDER / RANDOM WALK (user decision): a scope instead, the
  live value as a dot at the right edge and its trail to the left, one point per 1/136 of a cycle (the width = one
  cycle, at most a point a frame); the trail starts over on another track, LFO or wave.
- White keys on the LFO pages select the track (the LFOs are per track), as on EDIT.
- EDIT pages: a modulated knob shows its set value; its gauge gets a second small marker at the live modulated
  value.
- Labels ≤ 5 characters (`WAVE`, `RATE`, `MORPH`, `DEPTH`, `DEST`, `TRIG`, `PHASE`); wave names ≤ 5 (`SQUAR`,
  `SAW`, `RSAW`, `SINE`, `TRI`, `S&H`, `WANDR`, `EXP+`, `EXP-`, `RWALK`), DEST shows the knob's label.
- Defaults (both LFOs): SINE, SYNC 1 bar, MORPH 0, DEPTH 0, DEST OFF, TRIG PLAY, PHASE 0: nothing changes until
  DEST and DEPTH are set.

## 5. Data and projects

- 16 new per-track parameters (8 per LFO), appended before `P_COUNT`; zero-safe where the default is 0 (DEST OFF,
  DEPTH 0, PHASE 0), the others take their defaults from the parameter table as today.
- Track state: per LFO its phase, the random values (current / next / slewed), plus one random generator per
  track; a modulated copy of the knobs per track.
- Project format **`FDR4`**: FDR3 plus the LFO parameters; `FDR3`, `FDR2`, `FDR1` records in flash still load, with
  the LFOs at their defaults (DEST OFF: they sound as saved). Every loaded value is clamped. One flash sector
  (`_Static_assert`); the RAM slots fit `.noinit`.
- No frozen file changes; no change to the update path.

## 6. Cost and safety

- Per block: up to 16 LFOs (a few dozen instructions each) and the modulated copy; LFOs with DEST OFF or DEPTH 0
  cost nothing. Expected 1–2 % of the audio work; measured on the host, and the target cost check gets a line for
  the LFO update.
- Integer only, no float, no 64-bit division; bounded loops; the modulated values are clamped, so no engine sees a
  value outside its knob's range.

## 7. Testing

- Waveforms: each at MORPH 0 / middle / max: shape at fixed phases, range −1 … +1, no jumps where the wave is
  continuous; S&H smoothing 0 jumps / max glides; random waves repeat after PLAY.
- Rates: SYNC divisions against the tempo (cycle length in samples), Hz, TIME, and a tempo change while playing.
- TRIG FREE / HIT (restarts on a hit) / PLAY (restarts at PLAY) and PHASE.
- Routing: DEPTH ±, clamping, both LFOs on one knob, DEST an absent knob (no effect), DEST OFF / DEPTH 0 (no cost,
  output bit-identical).
- Sound: LVL / PAN modulation moves within a sound; a start-of-hit knob differs per hit; the stored knobs unchanged.
- Projects: FDR4 round trip; FDR3 / FDR2 / FDR1 converted (LFOs off); garbage clamped (H1).
- UI: the two pages, OCT− switching the LFO, OCT+ cycling the mode, the random waves' trail, the RATE text per mode, the EDIT gauge marker, white keys select;
  screenshots of every waveform's page for the user.
- Listening: WAVs with LFOs on TONE, LVL and PAN, each waveform in SYNC.
- H1–H4 as for M2 / M3.

## 8. Done when

All of §7 passes in `tests/run_tests.sh`, the firmware builds (`DRUM_PACKAGE=1`), and the user has looked at the
screenshots and listened to the WAVs.

## 9. Out of scope

More than one DEST per LFO, a modulation matrix, LFOs modulating each other or global settings (FX, COMP, Grids),
audio-rate modulation inside the engines, a one-shot envelope shape.
