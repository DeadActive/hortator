# Effects
<!--blurb: FILTER, DIST, SLICER, RESON, delay, reverb, chorus and the master chain.-->

## The signal path

Each track runs: voice, then **RESON**, **FILTER**, **DIST**, **SLICER**, then LEVEL and PAN and the three sends (**chorus**, **delay**, **reverb**). The sends and the track mix go into the master: the PERFORM effects, the metronome, SPEAKER EQ, a DC blocker, the limiter and a soft clip. The compressor (COMP) ducks tracks from a key track.

Press [[FX]] for the effect pages. The footer counter counts the whole family: **FILTER 1/9**, **FILTER 2/9**, **FX 3/9**, **SLICER 4/9**, **RESON 5/9**, **RESON 6/9**, **DLY 7/9**, **REVERB 8/9**, **CHORUS 9/9**. Press [[FX]] again or turn [[SELECT]] to move. The family reopens at the last page used. Hold [[FX]] for PERFORM instead.

All pages except DLY, REVERB and CHORUS edit the selected track. DLY, REVERB and CHORUS are global and stored in the project.

## FILTER (1/9 and 2/9)

A filter on every track, after RESON and before DIST. Each hit sweeps it by ENV (accents sweep further).

| Page | Knob | Label | Values (default) |
| --- | --- | --- | --- |
| 1/9 | **KNOB 1** | TYPE | OFF (default), LP, BP, HP, NOT (notch) |
| 1/9 | **KNOB 2** | CUT | 30 Hz to 16 kHz, exponential (127 = 16 kHz) |
| 1/9 | **KNOB 3** | RESO | 0 to 127 (0) |
| 1/9 | **KNOB 4** | ENV | -64 to +63 (0): cutoff offset by the hit envelope times velocity |
| 2/9 | **KNOB 1** | DECAY | 5 ms to 2 s (40, about 32 ms) |

LFO destinations F.CUT and F.RES move the filter.

## FX (3/9): DIST and the sends

| Knob | Label | Meaning (default 0) |
| --- | --- | --- |
| **KNOB 1** | DST | the track's distortion insert, shown in % |
| **KNOB 2** | CHO | chorus send |
| **KNOB 3** | DLY | delay send |
| **KNOB 4** | REV | reverb send |

## SLICER (4/9)

An insert after DIST and before LEVEL, PAN and the sends. It chops the track with one of 16 patterns of 16 steps.

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

## RESON (5/9 and 6/9)

A resonator insert, before FILTER: the track rings through a string, pipe, chord or modal bar, tuned in semitones. Limits: RESON on **4 tracks**, CHORD on **2**, MODAL on **2** (`RESON: 4 TRACKS MAX`, `CHORD: 2 TRACKS MAX`, `MODAL: 2 TRACKS MAX`).

| Page | Knob | Label | Values (default) |
| --- | --- | --- | --- |
| 5/9 | **KNOB 1** | MODEL | OFF (default), STRNG, PIPE, CHORD, MODAL |
| 5/9 | **KNOB 2** | TUNE | notes C1 to C7 (C3) |
| 5/9 | **KNOB 3** | DECAY | T60 from 10 ms to 10 s (72) |
| 5/9 | **KNOB 4** | MIX | % (50 %) |
| 6/9 | **KNOB 1** | TONE | % (79 %) |
| 6/9 | **KNOB 2** | STRCT | structure (0). On CHORD this becomes the chord type |
| 6/9 | **KNOB 3** | POS | excitation position, % (50 %) |
| 6/9 | **KNOB 4** | KEYF | KEYFOLLOW, 0 to 100 % (0 = off) |

**KEYF** makes the ring follow the note of each hit from C4: at 100 % a step at C5 (or a KEYS note, or a ratchet's note) rings an octave above TUNE, at 50 % half an octave. MIDI notes play their track at C4, so they ring at TUNE. A new note under a ringing tail glides to it in a few ms, without a click. It works on every RESON model. KEYF 0 sounds as before.

CHORD types (0 to 127 split into 20): OCT, 5TH, 4TH, MAJ, MIN, SUS2, SUS4, DIM, AUG, MAJ6, MIN6, MAJ7, MIN7, DOM7, M7b5, DIM7, 7SUS4, ADD9, QUART, CLUST.

LFO destinations R.TUN, R.DCY, R.MIX, R.TON, R.STR and R.POS move RESON's knobs.

## Delay (DLY 7/9)

| Knob | Label | Values (default) |
| --- | --- | --- |
| **KNOB 1** | TIME | 4BAR, 2BAR, 1/1, 1/2, 1/4, 1/8 (default), 8T, 1/16, 16T, 1/32; tempo-synced |
| **KNOB 2** | FDBK | feedback, 0 to 100 % (50 %) |
| **KNOB 3** | COLR | the repeats' colour, % (55 %) |
| **KNOB 4** | MIX | the return level, % (71 %) |

## Reverb (REVERB 8/9)

| Knob | Label | Values (default) |
| --- | --- | --- |
| **KNOB 1** | TYPE | ROOM (default) or SPRNG (spring) |
| **KNOB 2** | SIZE | % (71 %) |
| **KNOB 3** | DAMP | % (47 %) |

## Chorus (CHORUS 9/9)

| Knob | Label | Values (default) |
| --- | --- | --- |
| **KNOB 1** | CRT | rate, 0.05 to 40 Hz, exponential (about 0.4 Hz) |
| **KNOB 2** | CDP | depth, % (47 %) |

## Master chain

- **SPEAKER EQ** (MENU): FLAT, LOWCUT (12 dB/oct high-pass at about 110 Hz) and BASS+ (a lift at about 220 Hz with bass harmonics for the small speaker).
- **Limiter and soft clip** protect the output; the metronome follows MASTER and comes after PERFORM.
- **USB LEVEL** (MENU): MASTER (USB follows the MASTER knob) or FIXED (USB at full level, the speaker and headphones follow MASTER).

## PERFORM effects

The live master effects (REPEAT, REVERSE, TAPE STOP, filter sweeps, FREEZE, OCT UP and DN, CRUSH, THROW) are described under *Playing and recording*.
