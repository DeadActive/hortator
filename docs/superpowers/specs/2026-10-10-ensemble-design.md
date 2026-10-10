# ENSEMBLE: a performance synth firmware for the FM-1 — design

Date: 2026-10-10. Working name **ENSEMBLE** (the user names it before the first release). A new firmware, not a
Hortator feature: a fork of Felucca 1.5 (`upstream/main`, 129a4cf, the latest on 2026-10-10) in its own repo. This spec holds the whole
design (§1–§8) and stage 1 in full (§9); stages 2–4 each get their own detailed spec and plan when reached. The spec
is written in the Hortator repo only because the new repo does not exist yet; stage 1 moves it there.

## 1. Goal and user decisions

The FM-1 as an instrument for live performance: four melodic tracks that the player plays, loops, or lets the
machine generate, all following one harmony, shaped by macro knobs and macro effects. Both a one-person band (it
accompanies what you play) and a loop-building groovebox. For the user live, and released to FM-1 owners as Hortator
is.

User decisions (2026-10-09/10, in the brainstorm):
- **Use:** one-person band *and* loop-building groovebox (both).
- **Harmony sources:** all four (CHORDS, MELODY, LISTEN, PROG), switchable.
- **Keys:** all keys play the selected track (layout A), switchable to chord keys + melody (layout D).
- **Sound editing:** macros up front; Felucca's deep editor behind one button, saving user presets (B).
- **Macros:** per preset, with a default set per role (assumed, not objected to).
- **Loops:** no scenes. Everything is saved as a **performance** (a "preset" of the whole setup) and played; one set
  of 4 loops per performance. Step editing kept, but secondary.
- **Effects:** basic FX in the voice editor; advanced FX per track; perform FX stay; **macro FX** (several effects
  under one knob, e.g. ISOLATOR + REVERB, STUTTER + HP + DELAY, LP + DIST; the recipes designed later); FX moves
  that drive the macro FX automatically (choose FX, length, curve).
- **FX moves:** both within a performance (fired on demand) and on switching performances (the old one's OUT move,
  the next one's IN move) (C).
- **AUTO rhythm:** a style library per role with DENSITY / VARIATION, plus a generative rhythm map (A + C).
- **Drums / sync:** none; a click and count-in only (C). Felucca's existing MIDI stays as it is; no new clock work.
- **Build approach:** fork the latest Felucca (1.5), keep its sound layer, replace the UI and the sequencer (approach 1).
- **Storage:** performances take the user-sample flash region (A, the recommendation; the user's "yes").
- **Stages:** shell and sound → looper and performances → harmony and AUTO → FX.

## 2. The parts

| Unit | Job | From |
|---|---|---|
| Sound layer | engines, 8 shared voices, filter / envelopes, DIST + INSERT, sends, storage, USB | Felucca, kept |
| Transport | tempo, bar / beat, bar-quantised launches, click and count-in | new (Felucca's click.c reused) |
| Note bus | the one path of every note: keys (PLAY), loops (LOOP), generators (AUTO) → voices | new |
| Key router | key presses → notes of the selected track (layout A) or chord triggers + melody (layout D) | new |
| Harmony state | key, scale, current and next chord; written by one source, read by everything else | new |
| Track | role, preset, macros, track FX, state PLAY / LOOP / AUTO | new, wraps a Felucca part |
| Performance | the whole setup: 4 tracks, harmony, PROG chords, loops, FX moves | new, replaces projects |
| Generators | one per role: harmony + clock → notes on the note bus | new |
| FX chain | track FX → master macro FX → perform FX; FX moves automate the macro FX | partly new |
| UI | new screens and a new mapping of keys, knobs and buttons | new |

The note bus is the centre: PLAY, LOOP and AUTO produce the same note events into the same place, so a track's state
can change mid-performance, LISTEN hears loops and live playing alike, and anything can be recorded (PRINT).

Tracks are fixed: track 1 PADS, 2 BASS, 3 KEYS, 4 LEAD. The 8 voices stay shared across the four (Felucca's
cross-part stealing).

## 3. Harmony

**State:** key root (12), scale (Felucca's 16), current chord (root degree, quality, extension), the next chord (when
known: PROG). Chord changes land on the **change grid**: BEAT, ½ BAR or BAR (per performance).

**Sources** (one active, per performance):
- **CHORDS:** layout D: the 7 white keys of the chord octave are I..vii of the key; a black key held with one alters
  it (7, sus4, major / minor swap). Layout A on PADS or KEYS: the chord is recognised from the held notes (2 notes are
  enough; the chord stays after release).
- **MELODY:** at each grid point, the chord of the key that best contains the notes just played (long notes and strong
  beats weigh more), biased to stay on the current chord (no flicker).
- **LISTEN:** a decaying pitch-class tally of every note on the note bus over ~one bar, matched against chord
  templates; the key detected more slowly (or locked).
- **PROG:** up to 8 chords per performance, each with a length in beats; advanced by the clock, or one chord per key
  press (STEP).

## 4. Generators (AUTO)

Per role, a style list; DENSITY (how many notes) and VARIATION (seeded per-bar variation; 0: the same every bar)
shape it.

| Role | Styles |
|---|---|
| BASS | ROOT 8THS, OCTAVE, WALK (chord tones + approach notes), SYNC, SUB, GEN |
| PADS | HOLD, SWELL, PULSE, GEN |
| KEYS | STAB, PULSE, BROKEN, ARP (Felucca's arpeggiator), GEN |
| LEAD | ANSWER, HARMONY (a 3rd / 6th under your line), FILLS, OSTINATO, GEN |

- **GEN:** a Grids-style X / Y rhythm map plus a Euclidean mode (Hortator's Grids port, MIT), pitches from the
  harmony.
- **ANSWER** (the countermelody): it waits for a gap in the player's phrase (~½ beat), answers with a short phrase of
  chord tones on the rhythm of the last phrase, and stops when the player plays again.
- PADS and KEYS voice-lead: each chord moves to the nearest inversion of the last.

## 5. Looper and performances

**Loops:** one per track per performance.
- REC arms the selected track; it starts on the next bar (with a count-in from stop). The first REC's close sets the
  length, rounded to whole bars, or a preset length (1 / 2 / 4 / 8 bars). The track goes to LOOP.
- Overdub (REC on a looping track), one level of undo, clear. Raw timing recorded; input quantise optional.
- Notes are stored relative to the chord they were played over: a **FOLLOW** loop moves onto new chords, a **FIXED**
  loop keeps its pitches (riffs, drones).
- **PRINT:** an AUTO track's current bars captured as its loop.
- Step editing of a loop: one level down from the LOOP page (quantise, move, delete notes).

**Performance:** the 4 tracks (preset, macros, state, AUTO style and knobs, loop, track FX), the harmony (source, key,
scale, change grid, PROG chords), the macro FX settings, up to 8 FX moves, and its OUT / IN moves.
- 16 performances. Switching: queued, it lands on the next bar after the current one's OUT move, then the next one's
  IN move plays. The next performance is loaded into a second RAM slot before the switch, so the switch is instant;
  the old notes end on their own release.

## 6. Effects

```
voice → preset FX (DIST, in the voice editor)
      → track FX (1 insert per track)
      → sends: chorus · delay · reverb (Felucca's buses)
      → master → MACRO FX → perform FX → limiter
```
- **Preset FX:** Felucca's DIST and send levels, saved in the preset.
- **Track FX:** one insert per track, Felucca's INSERT (fx.c: SOFT HARD FOLD FUZZ CRUSH PHASER FLANGER CHORUS) as the
  base; the list grows in stage 4 (FILTER with envelope follow, COMP, SLICER are candidates).
- **Macro FX:** on the master, one recipe active; a recipe is up to 3 parts under one knob, each with its own range
  and curve. Parts: ISOLATOR (3-band kill), HP, LP, DIST, CRUSH, STUTTER, delay and reverb *throws* (into Felucca's
  existing buses: no second delay or reverb). The recipes are designed in stage 4.
- **Perform FX:** Felucca's hold-FX layer, unchanged. STUTTER (macro FX) and REPEAT (perform FX) share one buffer:
  one at a time.
- **FX moves and OUT / IN:** a move drives the macro FX knob along a curve (RISE, FALL, EXP, S, RISE+SNAP) over 1, 2,
  4 or 8 bars. OUT-type: ends snapped back to zero on the bar. IN-type: from its peak down to zero.

## 7. UI

Rule: **encoders shape, buttons open pages, holding a button turns the keys into a layer.**

Encoders: ALGORITHM the track; PRESETS the track's role library (heard while turning); SELECT the tempo; KNOB 1–4
the page's four values (on HOME: the track's macros). MASTER the volume.

HOME screen (240 × 240):
```
▶ 120  A minor      C ─→ F      3.2
──────────────────────────────────
 PADS  AUTO  SOFT PAD   HOLD   ▮▮▯
 BASS  LOOP  SQR BASS   4 BAR  ▮▮▮
▸KEYS  PLAY  EP TINE           ▮▯▯
 LEAD  AUTO  GLASS LD   ANSWER ▯▯▯
──────────────────────────────────
 SHIMMER   MOTION    AIR     SIZE
   62        18       40      75
 03 NIGHT DRIVE         src CHORDS
```

| Button (press) | Page |
|---|---|
| HOME | the screen above |
| SCL | HARMONY: source, key, scale, change grid; the PROG chords |
| ARP | AUTO of the selected track: style, DENSITY, VARIATION, GEN X / Y |
| SEQ | LOOP: length, FOLLOW / FIXED, input quantise; PRESETS → step edit |
| FX | track FX, macro FX (recipe, amount), FX moves |
| EDIT / ENV / LFO | Felucca's deep sound editor; the MACRO page; save as user preset |
| GLO / SAVE | global settings / performances and user presets |
| PLAY/STOP, REC | transport; record (§5) |

Layers (hold):
- **HOME (conductor):** white keys 1–16 queue performances 1–16; black keys set the tracks' states (PLAY → LOOP →
  AUTO) and mutes.
- **FX:** white keys the perform FX (momentary); black keys fire FX moves 1–8.
- **REC:** OCT− undo, OCT+ clear the selected track's loop.
- **OCT− + OCT+ together:** toggle layout D (the lowest octave: I..vii chord keys, chord names on the screen).

The UI is built and tried in the web simulator first.

## 8. CPU, storage, testing

**CPU** (targets; measured with BENCH in stage 1, which fixes the real numbers for Felucca's 128-frame halves):

| Part | Target |
|---|---|
| 8 voices, heaviest engines | ≤ 50 % |
| buses + master (always on) | ~12 % |
| track FX, 4 × | ≤ 12 % |
| macro FX + perform FX | ≤ 8 % |
| harmony, generators, looper (control rate) | ≤ 2 % |
| **peak** | **≤ 85 %** (the voice-shedding line) |

Every new DSP part gets a cost cap and a regression check (as Hortator's FILTER and FM).

**Storage** (Felucca's flash map, storage.c): user sample slots 0xA0000..0xDBFFF (240 KB) become the performance
store. A performance: ~1 KB of settings + loops capped at 2,048 notes (~12 KB). Log-structured with one spare area
and a CRC commit record (no A / B pair per performance). The SAMPLE engine keeps its built-in samples, loses user
uploads. Felucca's 4 project slots (0x97000..0x9EFFF) are freed in stage 2. Installing over Felucca erases the
user samples: the installer and README say so.

**Testing:**
- Host (Felucca's suite, extended): chord recognition against known chord sets; generators' note streams from fixed
  seeds against stored ones; the looper's timing, quantise and chord-following; the performance store under power
  cuts mid-write (fuzzed, as Felucca's loader tests).
- CPU: cost caps per DSP part; BENCH on the device.
- UI: the web simulator.
- Felucca's own tests keep passing where the code they test is kept.

## 9. Stage 1 — shell and sound (in full)

Ends with: ENSEMBLE installs on the FM-1 and in the web simulator; four tracks PADS / BASS / KEYS / LEAD playable
from the keys, each choosing from its role library with PRESETS and shaped by 4 macros on HOME; the deep editor
behind EDIT; a CPU baseline.

**9.1 Repo.** `~/Desktop/dev/fm1-drummachine/ensemble`, cloned from upstream Felucca at 1.5, tag `v1.5` (history kept,
GPL-3.0-only, Leo Kuroshita / Hügelton credited as Hortator does). No remote until the user creates one. From
Hortator, where upstream has no equivalent: BENCH (`tools/fm1_bench.py`, `bench.c`, with synth cases), CHANGELOG.md
and VERSION.txt / docs/VERSIONING.md (starting at 0.1.0), the recovery tool (`tools/rescue`, docs/RECOVERY.md).
Where both have one (web simulator / emulator, installer), the better-fitting one is chosen in the plan, with the
reason.

**9.2 What is kept, cut, replaced.**
- Kept untouched: engines (`eng_*.c`, `engines.c`, `fm6_*`, `phys_*`), `voice.c`, `dsp.c`, `fx.c`, `mod.c`,
  `perform.c`, `master.c`, `storage*.c`, `upreset.c`, `category.c`, `usb.c`, MIDI, OTA, the loader.
- Kept underneath for now: `seq.c` (the clock and the arpeggiator run from it; no patterns are shown or edited);
  `project.c` (SAVE stores the 4 tracks in Felucca's project slots until stage 2's performances).
- Replaced: the UI's page set. HOME and the new encoder rules are new; Felucca's EDIT, ENV, LFO and GLO pages are
  reached from their buttons as they are; SEQ, ARP, SCL and FX open placeholders that say which stage brings them,
  except that holding FX keeps Felucca's perform FX.
- Cut when nothing reads them: `motion.c`, `song_chain.c`, Felucca's step-sequencer pages. What is removed and why is
  listed in the plan.

**9.3 Roles.** A track's role filters the PRESETS list by Felucca's categories (category.c): PADS ← PAD; BASS ←
BASS; KEYS ← KEYS + PLUCK; LEAD ← LEAD. User presets by their category byte. FX, OTHER and DRUM sounds are not in any
role. Every role must hold at least 4 factory presets (a host test).

**9.4 Macros.**
- A macro: a name (≤ 7 characters) and up to 3 targets, each a parameter (common `P_*` or engine `P_E0..P_E7`) with a
  min and max. The macro's value 0..127 sets every target linearly between its min and max.
- Each role has a default set; each factory preset has its own 4 macros (a table in a new `macros.c`), written in the
  plan and checked by ear by the user (WAVs, as for FM).

| Role | Default macros |
|---|---|
| PADS | TONE · MOTION (LFO depth / rate) · SWELL (attack / release) · SPACE (reverb + chorus sends) |
| BASS | TONE · DRIVE (DIST) · DECAY (decay + filter env) · GLIDE |
| KEYS | TONE · DECAY · WIDTH (spread / chorus) · SPACE (delay + reverb sends) |
| LEAD | TONE · DRIVE · GLIDE · SPACE (delay + reverb sends) |

  TONE's target is per engine (each engine's brightness or cutoff parameter: a table in `macros.c`).
- A user preset stores its 4 macros: the user-preset record grows by a macro block, appended (a record of before
  loads with its role's defaults).
- In the deep editor, a MACRO page edits a macro: its name, its targets and their ranges. A parameter a macro owns is
  marked on the editor's pages; turning the macro overwrites it.

**9.5 HOME.** As §7, without the states, loops and harmony that later stages bring: all tracks are PLAY; the top line
shows transport, tempo and bar.beat; the keys play the selected track.

**9.6 CPU baseline.** BENCH on the FM-1 with synth cases: each engine's heaviest factory preset with 8 voices, a
4-track mix, idle; the report in `docs/bench/`, and §8's targets restated in measured numbers.

**9.7 Tests.** Felucca's host tests pass (those of cut code removed with it); new: role filtering, macro mapping
(value → targets, min > max reversed ranges), the user-preset macro block (old records load with defaults), web
simulator smoke (boot, HOME, PRESETS, a macro turn, a note).

## 10. Open, owned by a later stage

- The firmware's name (the user, before the first release).
- The macro FX recipes and the track FX list (stage 4).
- The exact performance count and loop cap, if stage 1's flash measurement differs (stage 2).
- How the hold-HOME black keys split between states and mutes, key by key (stage 2, tried in the simulator).
