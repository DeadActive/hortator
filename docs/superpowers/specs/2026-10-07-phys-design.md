# PHYS percussion (roadmap §3.7) — design

Date: 2026-10-07. Parent: `docs/UPSTREAM_1.0.2.md` §3.7. Upstream reference: Felucca 1.0 (`upstream/main`)
`firmware/src/phys_dsp.c` (DaisySP's modal resonator and Felucca's membrane, MIT, fixed point), `eng_phys.c` (the
engine around it), `tests/phys_ref.cpp` (the fixed-point models against DaisySP's float originals). Branch `phys`
from main `15307ef` (`drum-v0.12.0` + publishing). No frozen file changes.

## 1. Goal and user decisions

Two physical models from upstream's PHYS, sharing one modal core:
- **MEMB**, a new drum model: a struck drum head (toms, timpani, tabla).
- **MODAL**, a new RESON model: a bell / bar / plate body that the track's own sound rings.

User decisions (2026-10-07):
- **Both** (MEMB as a drum model, MODAL as a RESON model); upstream's STRNG and SYMP are not ported (RESON has
  strings).
- **Upstream's divisions kept as they are**: they divide by 32-bit values (the pi32v2 does 64 ÷ 32 natively), once
  per block. Each divisor is checked to be 32-bit (a 64 ÷ 64 divide needs a helper the app cannot link); one is
  replaced by a table only if the prototype's cost says so (the user is asked first).
- Version after the merge: **0.13.0**, then published with `tools/publish.sh`.

## 2. The modal core (`firmware/src/phys_dsp.c`)

Upstream's `phys_dsp.c`, the parts MODAL and MEMB use, kept as upstream wrote them (MIT header kept): the math
(`px_m`, `px_exp2`, `px_sin`, `px_isqrt64`, the xorshift), the SVF coefficients (`px_svf_coef`), the strike exciter
(`px_modal_exciter`, struck only: BOW / Dust not ported), `px_modal_block`, `px_memb_block`, `px_modal_run`. The string,
Dust and sympathetic parts are left out. 12 modes (`PX_NMODE`), MEMB 10. Number formats as upstream (signals Q20,
the modes Q22, frequencies Q32 cycles). Coefficients once per 32-sample block (CTL).

For RESON a second run function feeds the modes from an input signal instead of the exciter (the same per-mode
code; the input scaled to the modes' Q22).

## 3. MEMB, the drum model

- A new model after HNOIS: `MEMB` (`DM_MEMB`, appended: stored model numbers unchanged). Plays from steps, keys,
  MIDI like the others.
- Knobs (`edit[8]`; the first four are HOME's macros):

  | Knob | Range | Upstream |
  | --- | --- | --- |
  | TUNE | ±24 semitones around ~110 Hz (A2) | the note |
  | DECAY | 0..127 | DAMP |
  | TONE | 0..127 | BRIT (the strike's brightness: soft mallet to hard stick) |
  | HEAD | 0..127 | HARM: ideal head (inharmonic: toms, timpani) to loaded (harmonic: tabla) |
  | POS | 0..127 | centre (round, the circular modes only) to rim |
  | BEND | 0..127 = 0..12 semitones | the pitch drop after the strike (~60 ms) |
  | STICK | 0..127 | EXC: the strike itself mixed into the output |
  | (8th) | empty | |

- Accent / velocity: upstream's ACC at full velocity, scaled by the hit's velocity (127 accented, 96 plain, MIDI).
- One body per track: a new hit strikes the ringing head again (upstream's retrigger); the voice counts weight 2
  toward `DRUM_MAXV`. Its state (the modes, the exciter, the bend envelope) in `dm_state_t` (a new union member).
- Output gain and soft knee as upstream (`PHYS_GAIN` for MEMB, the knee at 24000), into the voice's usual path.

## 4. MODAL, the RESON model

- A new RESON model after CHORD: `RS_MODAL` (appended to the enum; `RS_NMODEL` moves; stored values unchanged).
- Knobs (RESON's pages): TUNE the fundamental (the LFOs' R.TUN fine pitch as now), DECAY the damping, MIX dry / wet,
  TONE the brightness, STRCT the partials (compressed / harmonic near 1/4 / bar, chime / tuned bar / more and more
  inharmonic), POS the strike position (mode levels cos(2π·pos·i)).
- Input: the track's sound before DIST (as STRNG / PIPE / CHORD), into the modes.
- State: the 12 modes' states (~100 B) in the track's existing `rs_buf` line (2.7 KB, unused by MODAL): no new RAM.
- Limits: counts toward the 4-track RESON cap; at most **2 MODAL tracks** ("MODAL: 2 TRACKS MAX", as CHORD), both
  numbers confirmed by the prototype's cost.
- Ring-out as RESON's: below the floor for a whole block → off (no cost); a cut, a model change: silent.
- The RESON graph: the modes' frequencies as partials (as STRNG's overtones).

## 5. Storage

No format change (FDR9): MEMB is a new model number, MODAL a new RESON value; older projects load as before. An
older firmware loading a project with MEMB / MODAL clamps them (as any out-of-range value). The kit hashes
(`sound_pack_test`) unchanged: no default track uses either.

## 6. Testing (host, failing first)

- **Fidelity (`tests/phys_ref.cpp`, ported from upstream):** DaisySP (MIT) fetched at a pinned commit by
  `tests/fetch_ref.sh` (never committed; offline: skipped as the other references). MODAL, struck, over notes C1..C7
  and the corners of STRUCTURE, BRIGHTNESS, DAMPING: the three strongest float peaks found in the fixed render
  within **5 cents** (`PEAK_CT`), the decay within **10 % + 1 dB** (`DECAY_TOL`), no sample beyond ±64, no DC above 1 %
  of the peak; upstream's informational cases stay informational. Any threshold change: the user first.
- **MEMB (its own: no float original):** the peaks at the head's mode ratios (ideal at HEAD 0, loaded at 127, the
  blend between), BEND drops the pitch by its semitones and returns within ~60 ms, POS at the centre leaves only the
  circular modes (m = 0), bounded at every knob extreme, a retrigger while ringing, weight-2 stealing, accent brighter.
- **RESON MODAL:** the 2-track limit and its message, ring-out to all-zero and off, a cut / model change silent,
  R.TUN moves the modes, bounded at every extreme (ASan), the idle cost (H4) unchanged with neither in use.
- **Every 64-bit divide's divisor is 32-bit:** the firmware links (no helper), checked by the build.
- Screens: SOUND with MEMB, RESON with MODAL. WAVs: MEMB tom, timpani, tabla with BEND, rim strike; MODAL on RESON:
  kick → plate, snare → bell, clap → bar.
- Firmware: H2 0 different, stack, loader pinned; budget lines `memb_render` and the MODAL part of RESON with the
  numbers the user approves after the prototype measurement.

## 7. On the device (the user)

- A track to MEMB: TUNE / DECAY / TONE / HEAD on HOME; HEAD from tom to tabla; BEND drops; POS centre to rim; STICK.
- RESON MODAL on a kick, a snare, a clap: STRCT from harmonic to bell; a third MODAL track refused.
- Four MEMB tracks and two MODAL RESONs playing: no dropouts (CPU on SYSTEM INFO).

## 8. Done when

§6 passes, the firmware builds, the frozen / loader / stack checks are green, CHANGELOG has the line,
DEVICE_INSTALL.md has a PHYS section, the user has checked §7; merged as 0.13.0 (tag `drum-v0.13.0`), published.

## 9. Out of scope

Upstream's STRNG and SYMP, BOW (Dust-sustained bowing / blowing), MEMB as a RESON model, MODAL as a drum model,
upstream's PHYS presets and polyphony (3 voices per part).
