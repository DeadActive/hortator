# Effects
<!--blurb: FILTER, SRR, DIST, INSERT, SLICER, RESON, delay, reverb, chorus and the master chain.-->

## The signal path

Each track runs: voice, then **RESON**, **FILTER**, **SRR**, **DIST**, **INSERT**, **SLICER**, the compressor's duck, then LEVEL and PAN and the three sends (**chorus**, **delay**, **reverb**). The sends and the track mix go into the master: the PERFORM effects, the metronome, SPEAKER EQ, a DC blocker, the limiter and a soft clip.

Press [[FX]] for the track's effect pages. The footer counter counts the whole family: **FILTER 1/8**, **FILTER 2/8**, **FX 3/8**, **INSERT 4/8**, **INSERT 5/8**, **SLICER 6/8**, **RESON 7/8**, **RESON 8/8**. Press [[FX]] again (it wraps to the first page) or turn [[SELECT]] (it stops at the ends) to move. The family reopens at the last page used. Hold [[FX]] for PERFORM instead.

All FX pages edit the selected track. **Delay, reverb and chorus** are global and live on the [[GLO]] pages 4 to 7 of 7 (stored in the project); their send knobs (CHO, DLY, REV) stay on the FX page.

## FILTER (1/8 and 2/8)

A filter on every track, after RESON and before DIST. Each hit sweeps it by ENV (accents sweep further).

| Page | Knob | Label | Values (default) |
| --- | --- | --- | --- |
| 1/8 | **KNOB 1** | TYPE | OFF (default), LP, BP, HP, NOT (notch) |
| 1/8 | **KNOB 2** | CUT | 30 Hz to 16 kHz, exponential (127 = 16 kHz) |
| 1/8 | **KNOB 3** | RESO | 0 to 127 (0) |
| 1/8 | **KNOB 4** | ENV | -64 to +63 (0): cutoff offset by the hit envelope times velocity |
| 2/8 | **KNOB 1** | DECAY | 5 ms to 2 s (40, about 32 ms) |

LFO destinations F.CUT and F.RES move the filter.

## FX (3/8): DIST and the sends

| Knob | Label | Meaning (default 0) |
| --- | --- | --- |
| **KNOB 1** | DST | the track's distortion insert, shown in % |
| **KNOB 2** | CHO | chorus send |
| **KNOB 3** | DLY | delay send |
| **KNOB 4** | REV | reverb send |

## INSERT (4/8 and 5/8)

An effect on every track, after DIST and before the SLICER.

**4/8:** [[KNOB 1]] **TYPE**: OFF (default), SOFT, HARD, FOLD, FUZZ, CRUSH, PHASR, FLANG, CHOR. [[KNOB 2]] to [[KNOB 4]] are **A**, **B**, **C**, named by the TYPE (with TYPE OFF they read A B C):

| TYPE | A | B | C |
| --- | --- | --- | --- |
| SOFT, HARD, FOLD, FUZZ | DRIVE (50 %) | TONE (cutoff, default 96) | LEVEL (dB, default 96) |
| CRUSH | BITS (1 to 16, default 9) | RATE (689 Hz to 44 kHz, default about 11 kHz) | LPF (cutoff) |
| PHASR, FLANG, CHOR | RATE (Hz) | DEPTH (default 75 %) | FDBK (75 %) |

**5/8:** [[KNOB 1]] **MIX** (dry to wet, 100 % by default; for the swept types the wet is the effect added to the dry, so 100 % is the whole effect), [[KNOB 2]] **SRR**.

- A TYPE change fades over 3 ms, without a click. TYPE OFF, MIX 0 and SRR OFF leave the sound exactly as before.
- A, B, C, MIX and SRR take MOTION, step locks, LFO DEST and MIDI LEARN (`T3 I.DRIVE`, `T3 SRR`). TYPE does not.
- Saved per project; projects saved before 1.0.0 load them OFF.

## SRR: sample rate reduction

It samples and holds the track's sound after the FILTER and before DIST, aliasing on purpose, for the gritty sound of the classic drum machines. OFF at 0, down to 347 Hz at 127; the screen shows the rate.

## SLICER (6/8)

An insert after INSERT and before LEVEL, PAN and the sends. It chops the track with one of 16 patterns of 16 steps.

| Knob | Label | Values (default) |
| --- | --- | --- |
| **KNOB 1** | SLCR | OFF (default), GATE, STUT |
| **KNOB 2** | PAT | pattern 1 to 16 (1) |
| **KNOB 3** | RATE | 1/8, 1/16 (default), 1/32, 8T, 16T, 32T |
| **KNOB 4** | DEPTH | 0 to 100 % (100 %). GATE: how far a gated step closes. STUT: the repeat level |

Patterns (x = open or live, . = gated or repeated):

| # | Pattern | # | Pattern |
| --- | --- | --- | --- |
| 1 | `x.x.x.x.x.x.x.x.` | 9 | `xx..xx..xx..x.x.` |
| 2 | `xx.xx.xx.xx.xx.x` | 10 | `x..x.x..x..x.x.x` |
| 3 | `x..x..x..x..x.x.` | 11 | `xxxxxxxx.x.x.x.x` |
| 4 | `xxx.xxx.xxx.xx.x` | 12 | `x.......x.x.x.xx` |
| 5 | `x.xx.xx.x.xx.xx.` | 13 | `.x.x.x.x.x.x.x.x` |
| 6 | `xxxx....xxxx....` | 14 | `xx.x.xx.xx.x.x.x` |
| 7 | `x...x...x...x...` | 15 | `x.xxx.x.xx.xxx.x` |
| 8 | `x.x..x.x..x.x.x.` | 16 | `xxx.x..xx.x.xxx.` |

## RESON (7/8 and 8/8)

A resonator insert, before FILTER: the track rings through a string, pipe, chord or modal bar, tuned in semitones. Limits: RESON on **4 tracks**, CHORD on **2**, MODAL on **2** (`RESON: 4 TRACKS MAX`, `CHORD: 2 TRACKS MAX`, `MODAL: 2 TRACKS MAX`).

| Page | Knob | Label | Values (default) |
| --- | --- | --- | --- |
| 7/8 | **KNOB 1** | MODEL | OFF (default), STRNG, PIPE, CHORD, MODAL |
| 7/8 | **KNOB 2** | TUNE | notes C1 to C7 (C3) |
| 7/8 | **KNOB 3** | DECAY | T60 from 10 ms to 10 s (72) |
| 7/8 | **KNOB 4** | MIX | % (50 %) |
| 8/8 | **KNOB 1** | TONE | % (79 %) |
| 8/8 | **KNOB 2** | STRCT | structure (0). On CHORD this becomes the chord type |
| 8/8 | **KNOB 3** | POS | excitation position, % (50 %) |
| 8/8 | **KNOB 4** | KEYF | KEYFOLLOW, 0 to 100 % (0 = off) |

**KEYF** makes the ring follow the note of each hit from C4: at 100 % a step at C5 (or a KEYS note, or a ratchet's note) rings an octave above TUNE, at 50 % half an octave. MIDI notes play their track at C4, so they ring at TUNE. A new note under a ringing tail glides to it in a few ms, without a click. It works on every RESON model. KEYF 0 sounds as before.

CHORD types (0 to 127 split into 20): OCT, 5TH, 4TH, MAJ, MIN, SUS2, SUS4, DIM, AUG, MAJ6, MIN6, MAJ7, MIN7, DOM7, M7b5, DIM7, 7SUS4, ADD9, QUART, CLUST.

LFO destinations R.TUN, R.DCY, R.MIX, R.TON, R.STR and R.POS move RESON's knobs.

## Delay (GLO DLY 4/7 and 5/7)

Tap [[GLO]] and step to the delay pages. The page draws the echoes after a hit: their spacing, how fast FDBK lets them fade, COLR dulling the later ones, and in PING the echoes left and right in turn, with TIME's length in milliseconds.

| Page | Knob | Label | Values (default) |
| --- | --- | --- | --- |
| 4/7 | **KNOB 1** | TIME | 4BAR 2BAR 1/1 1/2 1/4D 1/4 1/8D 4T 1/8 1/16D 8T 1/16 16T 1/32 32T, tempo-synced (D dotted, T triplet), 1/8 by default. A time longer than the delay holds is cut to 1.49 s |
| 4/7 | **KNOB 2** | FDBK | feedback, 0 to 100 % (50 %) |
| 4/7 | **KNOB 3** | COLR | the repeats' colour, % (55 %) |
| 4/7 | **KNOB 4** | MIX | the return level, % (71 %) |
| 5/7 | **KNOB 1** | MODE | **NORM** (default) or **PING** (ping-pong) |

In PING the echoes alternate between left and right (the first on the left), with FDBK, COLR and the longest time working as in NORM; they are a little softer in the top end. Changing MODE clears the delay. MODE is saved with the project; older projects load NORM.

## Reverb (GLO REVERB 6/7)

| Knob | Label | Values (default) |
| --- | --- | --- |
| **KNOB 1** | TYPE | ROOM (default), SPRNG (spring) or **HALL** |
| **KNOB 2** | SIZE | % (71 %) |
| **KNOB 3** | DAMP | % (47 %) |

**HALL** is a smooth, dense reverb in stereo; its tail is wide on headphones and fades to exact silence. SIZE sets its length (from about half a second to several seconds), DAMP how soon its top end fades. Switching TYPE fades the old tail out without a click. TYPE is saved with the project; older projects keep their ROOM or SPRNG. The ROOM tail also fades to true silence now. The page draws the tail over 4 seconds and its decay time (RT), with DAMP's faster-fading top end and the spring's drips.

## Chorus (GLO CHORUS 7/7)

| Knob | Label | Values (default) |
| --- | --- | --- |
| **KNOB 1** | CRT | rate, 0.05 to 40 Hz, exponential (about 0.4 Hz) |
| **KNOB 2** | CDP | depth, % (47 %) |

The page draws the sweep over 4 seconds, as fast as the rate and as deep as the depth, with the range of its delay in milliseconds.

## Master chain

- **SPEAKER EQ** (MENU): FLAT, LOWCUT (12 dB/oct high-pass at about 110 Hz) and BASS+ (a lift at about 220 Hz with bass harmonics for the small speaker).
- **Limiter and soft clip** protect the output; the metronome follows MASTER and comes after PERFORM.
- **USB LEVEL** (MENU): MASTER (USB follows the MASTER knob) or FIXED (USB at full level, the speaker and headphones follow MASTER).

## PERFORM effects

The live master effects (REPEAT, REVERSE, TAPE STOP, filter sweeps, FREEZE, OCT UP and DN, CRUSH, THROW) and REPEAT STEP are described under *Playing and recording*.
