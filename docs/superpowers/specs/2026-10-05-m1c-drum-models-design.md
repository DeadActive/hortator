# M1-C: Six more drum models (integer ports of Mutable Instruments' drum algorithms)

Addendum to `2026-10-05-drum-core-m1-design.md`. It replaces that spec's decision "Plaits models ported to
C (float) behind `DRUM_PLAITS=1`". Everything else in the M1 spec (no device writes, frozen files, voice
cap, cost reference, tests) still applies.

## 1. Decisions (user, 2026-10-05)

| Topic | Decision |
|---|---|
| Source | The drum algorithms of Mutable Instruments Plaits (MIT, `pichenettes/eurorack` @ `08460a6`): analog / synthetic bass drum, analog / synthetic snare drum, hi-hat 1 / 2. |
| Arithmetic | Rewritten in fixed point like the 808/909 models (no float, no FPU flag). Background: the WL82 has an FPU (`-mfprev1` emits hardware float instructions), but whether its error traps are enabled on the FM-1 is unknown until M4; integer avoids that question. |
| Availability | Always built, in the normal model list (no `DRUM_PLAITS` flag). |
| Names | Neutral, by sound; nothing on screen refers to Plaits: KBOOM, KPUNC, SSNAP, SCRAK, HMETL, HNOIS. |
| Reference | Fetched on demand (pinned commits: eurorack `08460a6`, stmlib `e3bd7c9`) into `build/`, used by host tests only; never in the firmware. |
| CPU (plan research) | The heavy models (KBOOM KPUNC SSNAP SCRAK) cost 2–3× an 808/909 voice. They count **2** toward the voice cap of 8, so at most 4 ring at once; the extreme limit (2,627) stays. Hats count 1 (choke keeps them cheap). |

## 2. Models

| Name | Algorithm (Plaits class) | Voices | Choke | CHAR (= HARMONICS) |
|---|---|---|---|---|
| KBOOM | analog bass drum (pulse exciter → high-Q resonator, attack / self FM) + the engine's overdrive | 1 | – | PUNCH: attack FM → self FM → drive |
| KPUNC | synthetic bass drum (sine + pitch / FM envelopes, click, dirtiness from DECAY) | 1 | – | FM: FM amount, then FM decay |
| SSNAP | analog snare drum (resonator modes + filtered noise) | 1 | – | SNAP |
| SCRAK | synthetic snare drum (sines + noise; TONE is its FM amount, label FM) | 1 | – | SNAP |
| HMETL | hi-hat 1 (6 square oscillators → BP / HP, swing VCA) | 1 | 1 | NOISE |
| HNOIS | hi-hat 2 (ring-modulated noise, linear VCA) | 1 | 1 | NOISE |

- Controls: TUNE = NOTE, DECAY = MORPH, TONE = TIMBRE, CHAR = HARMONICS, each mapped over the model's
  edit range as Plaits maps its 0..1 knobs (the per-engine formulas of `bass_drum_engine.cc`,
  `snare_drum_engine.cc`, `hi_hat_engine.cc`). ACCENT = velocity / 127. P_E4..P_E7 unused (SOUND 2 empty).
- A retrigger starts a fresh voice with the declick fade, as every other model (Plaits re-excites the running
  resonator; the difference only shows in very fast rolls).
- Model order: appended after DM_SMPL, so existing model indexes (projects) never shift. KIT_DEF unchanged.

## 3. Fixed-point port

- Signal flow followed stage by stage; matched in time (ms) and frequency (Hz), not sample by sample:
  Plaits runs at 48 kHz (blocks of 12), the FM-1 at 44.1 kHz (blocks of 32). Every time constant and
  frequency is recomputed for 44.1 kHz.
- One number format (plan research): Q24 in int32 (1.0 = 2^24, range ±128) with 64-bit products. The
  pi32v2 multiplies, divides (64 ÷ 32) and shifts 64-bit natively, so the port is a stage-by-stage
  translation of the float original, not hand-scaled per stage; precision is ample for Q in the thousands.
  A divisor must be a 32-bit value (a 64 ÷ 64 divide is a helper call, which the app cannot link).
- The toolkit in `dm_dsp.c`, each unit-tested against double math: `qm` / `qdiv` / `qdecay` (an envelope step
  that rounds yet never sticks: rounding alone stops a decay at 0.5 / (1 − k) LSB, a tail that never ends),
  stmlib's Svf / OnePole / tan approximations / SoftClip, x / (1 + |x|), semitones → ratio (trigger time; no
  per-sample exponentials are needed), the LCG noise of stmlib, sqrt.
- KBOOM's resonator follows its pitch every 2 samples (22 kHz), not every sample (cost; every 4 lost strict
  fidelity at the self-FM corners).
- Per-voice state: a union of the models' states in `dvoice_t` (`dm_state.h`).
- Reused: per-voice noise, `dsvf` (snare / hat filters at moderate Q), the metal square-source pattern
  (HMETL), `dm_put` / `dm_end`.
- Every stage documents its fixed-point range (Plaits values span pulse heights of 10 down to 0.001 / f0
  scales); the extremes test enforces bounded output.
- A voice ends after a guard time once its output stays below −84 dB re 1.0 for 46 ms (a low note spends
  whole blocks near its zero crossings), and in any case at 6 s: the new models fade out from 5.5 s (their
  longest DECAY rings 9–15 s; the M1 tests require every voice to end within 6.5 s). The fidelity window
  (1.5 s) is unaffected.
- Output: a reference sample of 1.0 = Q15 24576 before the voice level; no velocity gain on top (the model
  applies its accent).

## 4. Files

- `dm_dsp.c`: the toolkit above, `dm_putq`, `dm_qend`; `dm_state.h`: the per-voice state union; `core.h`: `dvoice_t.ms`, `dmodel_t.weight`; `drum_core.c`: the weighted voice cap.
- `dm_kick.c` (+KBOOM, KPUNC), `dm_snare.c` (+SSNAP, SCRAK), `dm_metal.c` (+HMETL, HNOIS): grouped by
  instrument as the M1 spec says. Each ported block carries Mutable Instruments' MIT copyright and
  permission notice in a source comment (licence requirement); nothing on screen.
- `dmodels.c`: six entries after DM_SMPL (enum, DMODELS, N_MODEL).
- `tests/fetch_ref.sh`: sparse clones of `pichenettes/eurorack` @ `08460a6` and `pichenettes/stmlib` @ `e3bd7c9` (a submodule) into `build/drum_ref/`.
- `tests/drum_ref.cc`: host C++ driver of the six originals: the reference metrics of the grid, reference WAVs, a self-test of the metrics. `tests/drum_metrics.h`: the metrics, one implementation for both sides. `tests/drum_ref_grid.h`: the grid. `tests/drum_fidelity.c`: ours against the reference.
- `tests/drum_test.c` (+ toolkit, health, weighted cap, lifetime / swap checks; golden requires every model), `tests/drumsim.c`
  (WAV pairs ours / reference), `tests/target_budget.py` (five render functions).

## 5. Verification

- Fidelity, per model: one hit, 1.5 s, TUNE / DECAY / TONE / CHAR ∈ {min, default, max} (81 renders),
  velocity 127 and 64, against the reference. Two tiers (user decision): **strict** is the target,
  **loose** the fallback when strict fails too much.

  | Metric | Strict (target) | Loose (fallback) |
  |---|---|---|
  | Level envelope, RMS windows (10 ms; noise-based SSNAP SCRAK HMETL HNOIS: 40 ms; at least two periods of the hit's base pitch) | ±1.5 dB above −50 dB of the reference peak | ±3 dB above −40 dB |
  | Decay time to −40 dB (± one envelope window of slack) | ±10 % | ±20 % |
  | Brightness: spectral centroid, first 200 ms (noise-based: 50 ms window average) | ±8 % | ±15 % |
  | Kicks: pitch track (zero-crossing periods per 50 ms window, above −40 dB) | ±3 % | ±5 % (≈ a semitone) |
  | Peak level: the loudest 1 ms RMS window (noise-based: 40 ms) | ±1 dB | ±2 dB |

  - Metric details settled by plan research (the metrics, applied to the reference itself resampled to
    44.1 kHz, must pass strict; a −3 dB copy must fail loose — the harness self-test): the 48 kHz reference
    is low-passed at 20 kHz before it is measured (content a 44.1 kHz port cannot have); envelope windows
    span at least two periods of the base pitch (a 12 Hz kick: its envelope, not its phase); peaks are RMS
    windows, not sample peaks (sample phase); for the noise-based models 40 ms (a short window of noise is
    luck — the §6 step 4 case, approved with this spec). The kick pitch track's first window starts at
    10 ms: before that it counts the zero crossings of the click / noise transient, which flip with the noise
    (KPUNC research: the body pitch matched within 0.2 %, the first-window reading moved up to 30 % either
    way) — §6 step 4, approved by the user after an A/B listening check. The reference renders 0.1 s idle
    before each hit, so its filters start from rest as in the running module (the ports start from the same
    rest state). The noise-based models' brightness averages only the 50 ms windows within 40 dB of the
    loudest envelope window: at the shortest DECAY the later windows are at −85 to −156 dB in the reference,
    far below hearing, yet each weighed a quarter (SSNAP research: ours/reference 0.74–0.77 from one such
    window) — §6 step 4, approved by the user after an A/B listening check.
  - The test prints, per model, how many renders meet strict and how many only loose; a render that
    misses loose fails the suite.
  - Procedure: a model first gets one round of §6 steps 1–2 (find and fix the cause) against strict.
    Whatever still misses strict after that passes on loose; its strict misses (model, metric, corner)
    are listed for the user with the listening review. Missing loose → §6 step 3 (stop and ask).
- Offline (reference not fetchable): the fidelity section prints `SKIPPED (offline)` and the suite's last
  line says so; it never passes silently.
- The usual checks for the new models: golden hashes, extremes, stress, voice cap, choke (hats), project
  clamping, UI model list / screenshots.
- Cost: the extreme-kit bound (2,627 host instructions/sample) holds with the weighted voice cap (heavy
  models count 2); over it → stop and ask the user. Target budget: the five render functions (the two hats
  share one). Q24 overflow: every product range-checked in host builds (`-DDM_QCHECK`), none allowed.
- Listening review: WAV pairs (ours / reference) in `build/drum_renders/`; the user approves.

## 6. When fidelity fails

1. Find the cause first: the report names model, metric and knob corner; the reference driver can dump
   each stage (exciter, resonator, envelope, shaper) so ours and the reference are compared stage by
   stage to the first one that diverges (precision, an approximation at its extremes, a 48 → 44.1 kHz
   conversion, a stage clipping in its fixed-point range).
2. Fix that cause (more bits, a better approximation, a re-scaled range) — never widen a tolerance or
   tune a constant just to pass.
3. If a corner still misses the loose tier at a sensible CPU cost: stop and bring the user the numbers and the WAV pair,
   with the options: accept a documented exception for that corner, spend more CPU on that model, narrow
   that knob's range, or leave the model out until after M4.
4. A metric that measures randomness instead of sound (noise-based models) is changed only with the
   user's approval, never loosened silently.

## 7. Done when

1. All six models pass fidelity (loose tier at least, strict misses listed), health, cost and the existing suites (`tests/run_drum_tests.sh`,
   `tests/run_tests.sh`), and the drum firmware builds and fits.
2. The user approves the renders.
3. Nothing has been installed on the FM-1.
