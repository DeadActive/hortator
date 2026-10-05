# M3 — Streams compressor with sidechain ducking — design

Date: 2026-10-05. Parent: `2026-10-05-drum-core-m1-design.md` (§2 safety rules, the frozen files, no device
writes by Claude). Builds on M2 (`2026-10-05-m2-sequencer-design.md`, merged at 7dd547e).

## 1. Goal and user decisions

A sidechain "pump": one track (usually the kick) ducks the tracks you pick, with the character of Mutable
Instruments' Streams compressor.

User decisions (brainstorming):
- Use: **sidechain pump** (not master glue, not per-track inserts).
- Ducked tracks: **picked per track** (DUCK on / off).
- FX: **duck before the sends** — a ducked track's sends into reverb / delay / chorus pump too; tails already in
  the effects ring on; FX returns are not ducked.
- Source: **ghost key** — the duck listens to the SOURCE track's sound before its LEVEL and MUTE, so a muted (or
  turned-down) source still pumps the others.
- Approach: **A, a faithful port of the Streams compressor**, keyed by the SOURCE track, its gain applied to each
  ducked track.

## 2. Sound path and engine

- `firmware/src/comp.c`: a C port of `streams/compressor.cc` (Mutable Instruments, MIT; its notice kept), with
  the Streams lookup tables it uses (`lut_log2`, `lut_exp2`, `lut_soft_knee`, `lut_compressor_ratio`,
  `lut_lp_coefficients`) in `firmware/src/comp_tables.h`, generated from the fetched Streams sources by
  `tools/gen_comp_tables.py` and committed (as the Grids tables).
- Configuration as Streams' "globals" path (`Compressor::Configure` with globals): ATK → attack time
  (1–500 ms), REL → decay time (50 ms – 5 s), THRESH → threshold, AMOUNT → ratio below the middle, adaptive makeup
  gain above it (up to Streams' brick-wall limiter with instant attack at the end); KNEE SOFT = Streams'
  `alternate` (soft knee).
- Sample rate: Streams runs at 31,089 Hz. Its gain computer (log2 / exp2 / ratio / knee tables) does not depend on
  the rate and is ported unchanged. Its attack / release coefficient table (`lp_coefficients`, from
  `vactrol_time`) is regenerated with Streams' own formula at 44,100 Hz, so the times stay the same in ms.
- Gain: Streams drives an analog exponential VCA with `g` (unity 32767, 256 steps = 1.55 dB). The port turns
  `g − unity` into a linear gain with Streams' `Exp2` and that scale: below 1 when cutting, above 1 with makeup
  (fixed point with headroom for Streams' largest makeup, `kMaxExponentialGain`; `g` clipped at 65535 as Streams
  does).
- Detector input: the SOURCE track's signal after its DIST and SLICER, before LEVEL, pan, MUTE and the sends; one
  detector, one gain per sample.
- Ghost key: the SOURCE track still plays (renders) its hits when muted; a muted source is not added to the mix
  or the sends, but keys the detector. (Other muted tracks behave as today.)
- Ducking: every track with DUCK on is multiplied by the gain before its LEVEL, pan and sends. The SOURCE track is
  never ducked (its DUCK is ignored while it is the source). Because the gain depends only on the source, this
  equals one compressor on the sum of the ducked tracks.
- Order: in each block the SOURCE track is rendered and analysed first, then the other tracks.
- SRC OFF: no detector, no gain, no extra cost; the output is bit-identical to M2.
- The master low-cut and safety limiter are unchanged, after everything.

## 3. Controls and screens

- Button **SCL** (the last free page button) opens the COMP pages.
- **COMP 1/2**: `SRC` (OFF, T1 … T8), `THRESH` (dB), `AMOUNT`, `REL` (50 ms – 5 s).
- **COMP 2/2**: `ATK` (1–500 ms), `KNEE` (HARD / SOFT), two empty columns.
- On both COMP pages the white keys 1–8 toggle DUCK of tracks 1–8 (lit = ducked), as the quick mutes on TRACKS;
  they do not play there. The SOURCE track's key does not toggle (its DUCK would be ignored).
- Picture: a live gain-reduction meter (dB now pulled down), a small source-level meter and the routing line, e.g.
  `SRC: T1   DUCK: T2 T4 T5 T6`; COMP 2/2 adds the transfer curve (input vs output level) for the current
  THRESH / AMOUNT / KNEE.
- Labels ≤ 5 characters; values in dB / ms / s as the existing formats allow.
- Defaults: SRC OFF; THRESH −24 dB; AMOUNT a moderate ratio (about 4:1, no makeup); ATK 1 ms; REL 150 ms;
  KNEE SOFT; DUCK off on every track.

## 4. Data and projects

- Track parameter `P_DUCK` (0 / 1), appended before `P_COUNT`. Globals `G_CSRC`, `G_CTHR`, `G_CAMT`, `G_CATK`,
  `G_CREL`, `G_CKNEE`, appended before `G_COUNT`. Zero-safe where the default is 0 (SRC OFF, DUCK off); the others
  get their defaults from the parameter tables as today.
- Project format **`FDR3`**: FDR2 plus `P_DUCK` and the COMP globals; `FDR2` (M2) and `FDR1` (M1) records in flash
  still load, with COMP off and DUCK off. Every loaded value is clamped. Must fit one flash sector
  (`_Static_assert`). `storage.c` and the frozen files are untouched.

## 5. Cost and safety

- Per sample while SRC is set: one detector update (64-bit multiply, as Streams; already used on the target),
  one log2 / exp2 gain computation (short normalising loops + table reads), one multiply per ducked track.
  Measured on the host per sample; the target ISR estimate is checked against its budget, and if it goes over,
  Claude stops and asks the user with the numbers.
- No float, no 64-bit division; bounded loops only in the ISR.

## 6. Testing

- Fidelity (`tests/fetch_ref.sh` also fetches `streams/`, same pinned eurorack commit): a host driver runs the
  original `streams::Compressor` (Configure with the same knob → globals mapping) on test signals (kick-like
  bursts, steady tones at several levels, silence) over THRESH / AMOUNT (both halves, the limiter end) / ATK / REL
  / KNEE. The gain computer is bit-exact with the coefficient table the reference is given; the attack / release
  times at 44.1 kHz are measured in ms against Streams' at 31,089 Hz (within 2 %). Offline: SKIPPED loudly.
- Behaviour (host): SRC OFF output bit-identical (goldens unchanged); a ducked track drops after a source hit
  and recovers over REL; DUCK-off tracks untouched; the source never ducks itself; ghost key (muted source:
  silent, still ducks; LEVEL 0 source: still ducks); the sends of a ducked track are ducked.
- Projects: FDR3 round trip; FDR2 and FDR1 converted (COMP off); garbage clamped (H1 boot test gains FDR3 records
  with garbage fields).
- UI: SCL opens COMP 1/2 and 2/2; knobs edit the settings; keys toggle DUCK and their LEDs; the source key does not
  toggle; screenshots (idle, pumping, limiter end) for the user.
- Listening: A/B WAVs — the same kit dry vs pumping, a REL sweep, the AMOUNT limiter end.
- H1–H4 as for M2.

## 7. Done when

All of §6 passes in `tests/run_tests.sh`, the firmware builds (`DRUM_PACKAGE=1`), and the user has looked at the
screenshots and listened to the WAVs.

## 8. Out of scope

Master-bus glue compression, per-track compressor inserts, ducking the FX returns, an external (MIDI / audio)
sidechain, Streams' other modes (envelope, vactrol, filter).
