# M2 — step probability, conditions, ratchets, Grids — design

Date: 2026-10-05. Parent: `2026-10-05-drum-core-m1-design.md` (§2 safety rules, the frozen files, no device
writes by Claude). Builds on the LEN-sync fix (a track's step = steps since PLAY mod LEN).

## 1. Goal and user decisions

Make patterns vary and grow without reprogramming: per-step chance and loop conditions, per-step rolls, and a
Mutable Instruments Grids pattern engine that any track can follow.

User decisions (brainstorming):
- Per-step **PROB** with percentages and, past 100 %, **conditions**: 1-SHOT, 1/2, 2/2, 1/3 … 8/8 ("A/B: plays
  on loop A of every B loops"). A "loop" is the track's own pass through its pattern (LEN steps). 1-SHOT plays
  only on the first loop after PLAY.
- Per-step **RATCH** 1–4 hits.
- Editing: on the STEP grid, **hold a step's key and turn KNOB 1 (PROB) / KNOB 2 (RATCH)**.
- **One Grids instance**; each track picks its source: its steps or a Grids channel (kick / snare / hats).
- **Faithful port** of Grids (map mode) **plus its Euclidean mode**.
- The GRIDS pages open with **ARP**.

## 2. Probability, conditions, ratchets

- Every step gains `cond` and `rat` (as well as `on`, `acc`).
  - PROB, as the knob shows it (position 0..56): 0 %, 5 % … 100 % (positions 0..20, ×5); 21: 1-SHOT; 22..56:
    A/B in the order 1/2, 2/2, 1/3, 2/3, 3/3, 1/4 … 8/8 (B = 2..8, A = 1..B). Default 100 %.
  - RATCH 1..4 hits, default 1.
  - Stored so that a zeroed step is the default (amendment, plan review: tracks and loaded records start from
    zeroed memory): `cond` 0 = 100 %, 1..20 = 0 % … 95 %, 21 = 1-SHOT, 22..56 = A/B as above; `rat` = hits − 1
    (0..3). Helpers convert between the knob position and the stored value.
- When a step comes up (it is on): percentage → it plays if a draw from the track's random sequence is below the
  chance (100 % always, 0 % never); A/B → it plays if `loop mod B == A - 1`, where `loop` = steps since PLAY
  ÷ LEN (integer; LEN as it is at that moment); 1-SHOT → it plays if `loop == 0`.
- Random sequence: one per track (LCG), seeded at PLAY from the track number, so a session's variations repeat
  exactly for the same edits (testable). One draw per step that is on and has a percentage below 100 %.
- A playing step with `rat` = R plays R hits evenly spaced over its own duration (the swung step length): hit k
  at k × length / R. All at the step's velocity (127 accented, else 96). The decision above is made once, for
  the whole roll. A hit not yet played when the next step starts (only possible if timing changed mid-step) is
  dropped.
- Live recording writes 100 % and 1 hit. A tap that turns a step off resets its PROB and RATCH (a step turned on
  later is plain); a hold restores the step as it was before the press.
- Editing (STEP grid): while a step's key is held, KNOB 1 turns `cond` and KNOB 2 `rat` of that step. The first
  knob turn of a hold cancels that hold's accent toggle (a hold without a turn still flips the accent) and turns
  an off step on. While held, the screen's hint line reads e.g. `STEP 5  3/5  RATCH 2` (or `75%`, `1-SHOT`).
- STEP grid drawing: a step with `cond` other than 100 % is drawn hatched / dimmer than a plain step; `rat` > 1
  shows R small ticks above the bar.

## 3. Grids

- One engine, a port of `grids/pattern_generator` (Mutable Instruments, GPL-3.0-or-later; its notice kept in the
  ported file), with its node tables (`grids/resources`) and Euclidean table, and avrlib's random generator for
  chaos.
- Modes:
  - **MAP**: X, Y (0–127 → Grids' 0–255) choose a point on the 5 × 5 node map (interpolated); FILL KICK / SNARE
    / HATS (0–127) the density per channel (0 = silent); CHAOS (0–127) the random perturbation of the fills.
    32-step patterns (1/32 notes); Grids' accent bits.
  - **EUCLID**: per channel a length 1–32 in 1/16s (LEN K / S / H) and a fill (the same three FILL knobs); accent on the
    first note of each cycle.
  - Both modes' settings are kept (separate parameters).
- **Clock (design change from brainstorming, see note):** the engine steps as the original: one Grids step per
  1/32 note (32 steps = one bar; Euclidean mode advances and plays on every other step, i.e. in 1/16s), at the
  song tempo, counted since PLAY, and evaluates all three channels per step exactly as the original does. The
  global swing applies per 1/16 (as on a 1/16 step track); each swung 1/16 is split into two equal 1/32s.
  (Amendment, plan review: the spec said one step per 1/16; the original's pattern step is a 1/32.)
  A track whose source is a Grids channel plays that channel's triggers on this clock (accent → velocity
  127, else 96). Its own LEN, DIV and track swing apply only to its step pattern, which is kept untouched and
  comes back when SRC returns to STEP. Note: brainstorming said Grids tracks would use their own DIV and swing;
  a single engine stepping all channels together is what makes the chaos sequence identical to the original's
  (and two tracks on one channel play the same notes), so Grids tracks share the Grids clock.
- Per track: PATTERN page, 4th knob **SRC** = `STEP`, `G-KCK`, `G-SNR`, `G-HAT`. Several tracks may follow one
  channel. Probability / conditions / ratchets do not apply to Grids notes.
- Chaos uses Grids' random generator, reseeded at PLAY: a session repeats exactly (testable).

### Pages (ARP)
- Knob labels and values fit the 5-character columns (amendment, plan review): `MODE` MAP / EUCL, `X`, `Y`,
  `CHAOS`, `FIL K` / `FIL S` / `FIL H`, `LEN K` / `LEN S` / `LEN H`; SRC values `STEP`, `G-KCK`, `G-SNR`, `G-HAT`.
- **GRIDS 1/2** — MAP: knobs `MODE MAP`, `X`, `Y`, `CHAOS`; middle left: the 5 × 5 node map as faint dots and a
  bright dot at X / Y; middle right: three 32-cell rows K / S / H of the current pattern (before chaos): hit =
  short bar, accent = tall bar, the playhead cell inverted while playing. EUCLID: knobs `MODE EUCLID`, `LEN K`,
  `LEN S`, `LEN H`; middle: three rings of LEN dots (hits filled, the accent larger, the position inverted).
- **GRIDS 2/2** — knobs `FILL K`, `FILL S`, `FILL H`, (empty); middle: the same picture as page 1; under it the
  routing, e.g. `K: T1 T6   S: T2   H: T4 T5`.
- PATTERN page: 4th column SRC. STEP grid on a Grids track: the generated pattern read-only (MAP: 32 steps over two
  banks; EUCLID: LEN steps), hint line `GRIDS KICK`; keys do nothing. TRACKS mixer: a Grids track's row shows its generated steps.

## 4. Data and projects

- `step_t` becomes `{on, acc, cond, rat}` (4 bytes; `cond` / `rat` stored as in §2). Track parameter `P_SRC` (0 STEP, 1–3 G-KICK/SNR/HAT).
  Globals: `G_GMODE`, `G_GX`, `G_GY`, `G_GCHAOS`, `G_GFILL1..3`, `G_GLEN1..3` (defaults: MAP, 64, 64, 0, 64 ×3,
  16 / 12 / 8).
- Project format **`FDR2`**: as FDR1 with the new step fields, `P_SRC` and the Grids globals; it must fit one
  flash sector (`_Static_assert`, ≈ 2.9 KB of 4 KB). An `FDR1` record (M1 projects, in flash) still loads: 100 %,
  1 hit, SRC STEP, Grids defaults. Every loaded value is clamped to its range (a garbage `cond` / `rat` /
  `P_SRC` cannot index out of a table or divide by zero).
- No new flash regions; `storage.c` and the frozen files are untouched.

## 5. Cost and safety

- Grids work happens once per 1/32 step (three lookups, a few multiplies): negligible next to the voices.
  Ratchets add hits; the voice cap and overload shedding already bound them.
- The boot path is unchanged; H1 (boot test) gains FDR2 / FDR1 records with garbage fields; H2 / H3 / H4 must
  still pass (`fm1_alnk0_irq` within its budget, stack within 75 %).

## 6. Testing

- Sequencer (host): a percentage's hit rate over many loops within statistical bounds and repeatable per seed;
  every A/B condition plays exactly on its loops over 2 × 8 × 8 loops; 1-SHOT only on loop 0; conditions follow a
  LEN change; ratchet hit times for R = 2, 3, 4 with and without swing; one decision per roll; live recording
  writes 100 % / 1.
- UI: hold + KNOB 1 / KNOB 2 edit the held step (and cancel the accent toggle); an off step turns on; the hint
  line; SRC on PATTERN; GRIDS pages in both modes; read-only grid for a Grids track. Screenshots of each, for the
  user.
- Projects: FDR2 round trip; FDR1 load with defaults; garbage fields clamped; fits a sector.
- Grids fidelity: `tests/fetch_ref.sh` also fetches `grids/` and `avrlib/` (same pinned commit); a host
  reference driver runs the original `PatternGenerator` (AVR headers stubbed) and ours over a grid of X / Y /
  fills / chaos (MAP) and lengths / fills (EUCLID), 64+ steps each, with the same seed: triggers and accents must
  be identical. Offline: SKIPPED loudly, as the Plaits fidelity.
- Listening: a WAV of a kit with kick / snare / hats on Grids (MAP, a few X / Y points, chaos off and on) and a
  Euclidean demo, for the user.

## 7. Done when

All of §6 passes in `tests/run_tests.sh` (with H1–H4), the firmware builds (`DRUM_PACKAGE=1`), and the user has
looked at the screenshots and listened to the WAVs.

## 8. Out of scope

Grids' clock input / output, tap tempo, its swing option (the global swing applies), MIDI clock sync; per-step
parameter locks other than PROB / RATCH.
