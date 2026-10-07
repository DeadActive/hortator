# Sound pack: BASS+, SPRING reverb, slow divisions — design

Date: 2026-10-06. Parent: `docs/UPSTREAM_1.0.2.md` §3.1. Upstream reference: Felucca 1.0.2 (`db70550`),
`firmware/src/fx.c`, `params.c`, `ui_menu.c`. Branch `sound-pack`, from `trs-midi` (stage 2 step 4, awaiting the
device check; if that fails, this branch is rebased onto main).

## 1. Goal and user decisions

Three sound features from upstream 1.0.2, each ported (not rewritten) and checked against upstream's own code:

1. **BASS+** for the FM-1's small speaker (upstream's SPEAKER EQ, with 1.0.2's #42 fix).
2. **SPRING reverb** beside the room reverb, chosen per project by a reverb TYPE.
3. **Slow divisions** 1/2, 1/1, 2BAR, 4BAR.

User decisions (design approval, 2026-10-06):
- The reverb TYPE knob: REV/CHO splits into two FX pages, **REVERB** (TYPE SIZE DAMP) and **CHORUS** (RATE DEPTH).
- Delay **TIME** gets all ten divisions, as upstream: anything longer than the delay line (1.49 s) is cut to it.
- Division knobs step **by length** (upstream's order): 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32.
- Version after the merge: **0.7.0**. Project format **FDR7** (the reverb TYPE); song chain + names becomes FDR8.

No frozen file changes (fx.c, params.c, pages.c, ui_menu.c, panel.c, project.c, seq.c are ours).

## 2. BASS+ (speaker EQ)

- MENU's **LOWCUT** item becomes **SPEAKER EQ** with three values: **FLAT** (0), **LOWCUT** (1, as today:
  12 dB/oct ~110 Hz), **BASS+** (2). KNOB 1 steps them (right: up, left: down, stopping at the ends) and OCT+
  steps and wraps, as upstream (today's on/off items: right = ON, left = OFF, OCT+ toggles). `settings.lowcut` keeps
  its place and meaning for 0 / 1, so the stored settings record does not change; `settings_init` loads a stored
  value above 2 as FLAT.
- `fx_lowcut` = the setting (0 / 1 / 2) instead of `!= 0`.
- `master_out` becomes upstream's: in BASS+ the two one-pole high-passes run one octave higher (~220 Hz, step
  `>> 5` instead of `>> 6`) and `spk_bass((l + r) >> 1)` is added to both sides after them. `spk_bass` is
  db70550's, byte for byte: a 4-pole low-pass (~150 Hz), a level-following clip at its own envelope (odd
  harmonics), a band-pass ~220 Hz..1 kHz, × 3. Its state lives in the existing `fx` struct (`fx.sb_lp1..4`,
  `fx.sb_env`, `fx.sb_h1`, `fx.sb_h2`, `fx.sb_hl`: no new small globals, which can upset H2); only those names
  differ from upstream's text.
- The MENU's value column moves from x 90 to x 102 so "SPEAKER EQ" (80 px in the 8 px font) fits; the longest
  value (5 characters) still ends before COLOR's swatches at x 160.
- `lowcut1` takes the step as an argument (`sh`), as upstream.
- It applies to the whole master (headphones too), as upstream.

## 3. SPRING reverb

### 3.1 Parameter, format, pages

- New global **`G_RTYPE`** ("TYPE", names ROOM / SPRING, default ROOM), appended after `G_CGHOST` (before
  `G_COUNT`), so every existing global keeps its id (the editor protocol addresses globals by id).
- Project format **FDR7** (`PROJ_MAGIC` "FDR7"): the globals array grows by one. FDR6 projects are converted on
  load like the earlier steps (TYPE = ROOM); FDR1..FDR5 keep their conversion paths, ending with TYPE = ROOM.
  An old record's version is chosen by its format marker, not by its size: an FDR5 record and an FDR6 record are
  both 3024 B (FDR7 is 3028 B), so the size alone would read an FDR6 record as FDR5 and drop it.
- Pages: `REV/CHO` is replaced by `REVERB` {G_RTYPE, G_RSIZE, G_RDAMP, —} and `CHORUS` {G_CRATE, G_CDEPTH, —, —},
  in that order in the FX family (PRESET / page turning reaches both). The empty knobs show nothing, as on other
  pages with empty slots.

### 3.2 Sound

- **ROOM** is today's reverb, bit-identical. It moves from inside `fx_buses`' per-sample loop into its own
  `rev_room(rev_in, out, n)` (noinline, adds to `out`), as upstream; chorus and delay stay in `fx_buses`' loop.
  The wet sum is the same integers in another order: identical output.
- **SPRING** is db70550's `rev_spring`, byte for byte: the send → a ~110 Hz low cut → 10 stretched allpasses
  (the chirp) → a loop delay (30 .. 60 ms by SIZE, gliding) with a one-pole low-pass (DAMP) and the decay gain
  (SIZE); out: the far end (with a slow 1.5-sample wobble) plus a quieter pickup at three quarters.
- It runs in ROOM's own buffers: `rev_comb` holds SPRING's loop line, `rev_ap` becomes upstream's union
  `rev_u` (ROOM's int16 allpasses / SPRING's int32 allpass states), with upstream's `_Static_assert` that it fits.
  No new RAM except a few state words in `fx` (`rtype`, `sp_w`, `sp_lp`, `sp_hp`, `sp_he`, `sp_size`, `sp_ph`).
- **Changing TYPE:** the old model renders one more block into a scratch buffer, faded out linearly over the
  block, then both buffers and the states are cleared and the new model starts from silence (upstream's
  `rev_clear` and switch). No click.

## 4. Slow divisions

- `N_DIV` gets **1/2, 1/1, 2BAR, 4BAR** appended (ids 6..9); ids 0..5 are unchanged, so stored projects keep their
  values.
- `div_samples(div)`: ids 0..5 as today; 6..9 are a quarter × 2, 4, 8, 16 (upstream's
  `quarter << (div - 5)`).
- **Knob order by length:** turning a division knob steps 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32 (upstream's
  `DIV_ORDER`, a display order for `N_DIV` knobs and their gauges; the stored value stays the id). Used by PATTERN
  DIV and DLY TIME.
- **Delay TIME:** all ten; `delay_samples` already clamps to `DLY_LEN - 1` (1.49 s).
- **Sequencer:** a step can now last up to 16 beats (4BAR); at 40 BPM that is 1 058 400 samples. Swing
  (`track_swing * period / 250`, ≤ 100 × 1 058 400) fits in int32; ratchets divide the step; live recording
  compares against half a step. No change needed; the tests pin it.

## 5. Testing (host, failing first)

- **BASS+:** our `spk_bass` and the BASS+ `master_out` give the same samples as db70550's code (a reference copy
  in `tests/`, its origin noted) on a test signal; #42: a 300..600 Hz sine through BASS+ loses < 0.5 dB; a 60 Hz
  sine produces energy between 220 Hz and 1 kHz; FLAT and LOWCUT outputs are bit-identical to today's; a stored
  setting 3 loads as FLAT.
- **SPRING:** ROOM's wet output is bit-identical to today's on a kit render; `rev_spring` gives the same samples
  as db70550's on a test signal (with SIZE / DAMP changes); a TYPE switch while the tail rings: the block after it
  has no step larger than the signal's own (no click), and the buffers are silent before the new model starts;
  a silent send leaves SPRING's output at exactly 0 after its tail (no stuck offset).
- **Format:** an FDR6 project loads with TYPE ROOM and everything else as before; FDR7 round-trips TYPE; FDR1..5
  still load.
- **Pages:** REVERB and CHORUS show their knobs; page turning reaches both; screenshots for the user.
- **Divisions:** `div_samples` exact at 40, 120, 240 BPM for all ten; a 4BAR track fires one step per 4 bars in
  time with a 1/16 track; swing, a ratchet and a live-recorded hit on a 2BAR step; turning DIV steps in length
  order; DLY TIME at 2BAR = `DLY_LEN - 1`; an old project's DIV ids unchanged.
- **Cost:** the target budget check; SPRING and ROOM become their own functions, so `fm1_alnk0_irq`'s figure
  moves and two lines (`rev_room`, `rev_spring`) are new: their numbers are shown to the user, who approves the
  budget file change (BUDGET_UPDATE) before it is written. The CPU meter on the device is part of §6.
- **WAVs for the user:** a kit through FLAT / LOWCUT / BASS+; a snare and a kit through ROOM and SPRING at
  SIZE / DAMP low, mid, high; a 1/16 kick with a 2BAR / 4BAR hat; the delay at 1/2.

## 6. On the device (the user)

- SPEAKER EQ: FLAT, LOWCUT, BASS+ on the speaker: BASS+ gives the kick a bass you can hear on the small speaker,
  without the mids getting thinner; the setting survives a power cycle.
- REVERB TYPE SPRING on a snare: the spring's chirp / drip; SIZE and DAMP change it; switching TYPE while it rings:
  no click; CHORUS page works as before.
- A hi-hat track on DIV 2BAR / 4BAR against a 1/16 kick: in time; the knob runs from 4BAR to 1/32; delay TIME at
  1/2 and slower: long echoes (the longest cut to 1.49 s).
- A project saved before this build loads with ROOM and sounds as before. The CPU meter stays below where it was
  with the heaviest kit, or close to it.

## 7. Done when

§5 passes, the firmware builds, the target / stack / H2 / loader checks are green (budget changes approved by the
user), CHANGELOG has the lines, the user has checked §6 on the FM-1; merged as 0.7.0 (tag `drum-v0.7.0`).

## 8. Out of scope

The web editor (`web/editor.html` is upstream 0.9's synth editor, unchanged in this fork; the drum firmware does
not speak its protocol), USB LEVEL FIXED (upstream 1.0.2's MASTER-after-USB switch: comes with USB audio, §3.2), upstream's arpeggiator
RATE (we have no arpeggiator), upstream removing the drum MIDI channel (we keep G_DRCH), new reverb RAM.
