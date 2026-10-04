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
| Reference | Fetched on demand (pinned commit) into `build/`, used by host tests only; never in the firmware. |

## 2. Models

| Name | Algorithm (Plaits class) | Voices | Choke | CHAR (= HARMONICS) |
|---|---|---|---|---|
| KBOOM | analog bass drum (pulse exciter → high-Q resonator, attack / self FM) + the engine's overdrive | 1 | – | attack FM → self FM → drive |
| KPUNC | synthetic bass drum (sine + pitch / FM envelopes, click, dirtiness) | 1 | – | attack FM, then dirtiness |
| SSNAP | analog snare drum (resonator modes + filtered noise) | 1 | – | snappy |
| SCRAK | synthetic snare drum (sines + noise) | 1 | – | snappy |
| HMETL | hi-hat 1 (6 square oscillators → BP / HP, swing VCA) | 1 | 1 | noisiness |
| HNOIS | hi-hat 2 (ring-modulated noise, linear VCA) | 1 | 1 | noisiness |

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
- New building blocks in `dm_dsp.c`, each unit-tested:
  1. High-precision resonator: Q30 states, 64-bit products; holds Q in the thousands at ~40–60 Hz without
     limit cycles or loss of decay (KBOOM).
  2. Per-sample frequency → filter coefficient (tan polynomial), for per-sample pitch modulation.
  3. Fast semitones → ratio (interpolated table), for per-sample pitch envelopes / FM.
  4. Diode, soft clip and overdrive curves as integer polynomials.
- Reused: per-voice noise, `dsvf` (snare / hat filters at moderate Q), the metal square-source pattern
  (HMETL), `dm_put` / `dm_end`.
- Every stage documents its fixed-point range (Plaits values span pulse heights of 10 down to 0.001 / f0
  scales); the extremes test enforces bounded output.
- A voice ends when its Q24 envelope or the resonator energy falls below −84 dB.

## 4. Files

- `dm_dsp.c`: the building blocks above.
- `dm_kick.c` (+KBOOM, KPUNC), `dm_snare.c` (+SSNAP, SCRAK), `dm_metal.c` (+HMETL, HNOIS): grouped by
  instrument as the M1 spec says. Each ported block carries Mutable Instruments' MIT copyright and
  permission notice in a source comment (licence requirement); nothing on screen.
- `dmodels.c`: six entries after DM_SMPL (enum, DMODELS, N_MODEL).
- `tests/fetch_ref.sh`: sparse clone of `pichenettes/eurorack` @ `08460a6` into `build/drum_ref/`.
- `tests/drum_ref.cc`: host C++ driver rendering the six originals (float WAV / raw) for a parameter grid.
- `tests/drum_test.c` (+ fidelity, building-block, golden, extremes, cost checks), `tests/drumsim.c`
  (WAV pairs ours / reference), `tests/target_budget.py` (six render functions).

## 5. Verification

- Fidelity, per model: one hit, 1.5 s, TUNE / DECAY / TONE / CHAR ∈ {min, default, max} (81 renders),
  velocity 127 and 64, against the reference. Two tiers (user decision): **strict** is the target,
  **loose** the fallback when strict fails too much.

  | Metric | Strict (target) | Loose (fallback) |
  |---|---|---|
  | Level envelope, RMS windows (10 ms; noise-based SSNAP SCRAK HMETL HNOIS: 40 ms) | ±1.5 dB above −50 dB of the reference peak | ±3 dB above −40 dB |
  | Decay time to −40 dB | ±10 % | ±20 % |
  | Brightness: spectral centroid, first 200 ms (noise-based: 50 ms window average) | ±8 % | ±15 % |
  | Kicks: pitch track | ±3 % | ±5 % (≈ a semitone) |
  | Peak level | ±1 dB | ±2 dB |

  - The test prints, per model, how many renders meet strict and how many only loose; a render that
    misses loose fails the suite.
  - Procedure: a model first gets one round of §6 steps 1–2 (find and fix the cause) against strict.
    Whatever still misses strict after that passes on loose; its strict misses (model, metric, corner)
    are listed for the user with the listening review. Missing loose → §6 step 3 (stop and ask).
- Offline (reference not fetchable): the fidelity section prints `SKIPPED (offline)` and the suite's last
  line says so; it never passes silently.
- The usual checks for the new models: golden hashes, extremes, stress, voice cap, choke (hats), project
  clamping, UI model list / screenshots.
- Cost: each new model's worst case ≤ the heaviest existing model, so the extreme-kit bound (2,627 host
  instructions/sample) holds; over it → stop and ask the user. Target budget: the six render functions.
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
