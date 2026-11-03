# Sequencer
<!--blurb: Steps, accents, probability, ratchets, pattern length and division, swing, MOTION and fills.-->

## The model

Each of the eight tracks has its own pattern of up to **64 steps**, played on the 16 white keys in banks of 16 (bank 1 to 4). Each track also has its own **length**, **division** and **swing**, so tracks can run polyrhythms. The transport (PLAY) starts every track from its first step.

Press [[SEQ]] for the sequencer. It has four pages, reached by pressing [[SEQ]] again or turning [[SELECT]]: **STEP 1/4**, **PATTERN 2/4**, **MOTION 3/4** and **SONG 4/4**. On every SEQ page (and on TRACKS when no track is armed, and anywhere in STEPS mode) the white keys are the selected track's steps. [[ALGORITHM]] picks the track.

## STEP: tapping in a pattern

- **Tap** a white key: the step toggles on or off, on the release.
- **Hold** a step (0.3 s): an off step turns on at once and the step can be edited; an on step stays on. While held:

| Knob | Sets | Values |
| --- | --- | --- |
| **KNOB 1** | PROB, the chance the step plays | 0 %, 5 % to 100 % in 5 % steps; **1-SHOT** (plays once after PLAY); **1/2 ... 8/8** (plays on loop A of every B: 1/2 is every other loop, 3/4 three loops of four, and so on); **FILL** (only during a fill); **!FILL** (only outside a fill) |
| **KNOB 2** | RATCH, ratchet repeats inside the step | 1 to 4 |
| **KNOB 3** | ACC, accent | turn right: on, left: off |

- **Accent with a tap:** hold [[OCT+]] and tap a step: the step is accented (an off step turns on accented), or plain again. While [[OCT+]] is held the keys show the bank's accents.
- **Banks:** [[KNOB 1]] on STEP shows `BANK n/m`. [[OCT+]] alone (on its release) goes to the next bank, [[OCT-]] to the previous.
- **Piano roll:** on an ACID track the STEP page is a piano roll; notes, SLIDE and TIE are described under *Playing and recording*.
- **Grids tracks:** a track whose PATTERN SRC is a Grids lane (`G-KCK`, `G-SNR`, `G-HAT`) shows that pattern read-only.
- **Clear:** hold [[REC]] 0.7 s on a SEQ page to be asked CLEAR TRACK. TOOLS **CLRSQ** clears the selected track's pattern.

## Fills

A step with PROB **FILL** plays only while a fill is active; **!FILL** plays only outside one. Hold [[REC]] (0.4 s) on any page other than SEQ's to play a fill while you hold it (`FILL` in the top bar). A shorter [[REC]] press arms as before. A learned REC and the SMC-Mixer's REC are always taps.

## PATTERN: length, division, swing, source

| Knob | Label | Values |
| --- | --- | --- |
| **KNOB 1** | LEN | 1 to 64 steps (default 16) |
| **KNOB 2** | DIV | 4BAR, 2BAR, 1/1, 1/2, 1/4, 1/8, 8T, 1/16 (default), 16T, 1/32 |
| **KNOB 3** | SWG | 0 to 100 %. The effective swing is this plus GLOBAL SWG, capped at 100 |
| **KNOB 4** | SRC | STEP (the track's own steps), or **G-KCK**, **G-SNR**, **G-HAT** to follow a Grids channel (the track's own steps are not played) |

PATTERN edits are blocked while a song plays (`STOP TO EDIT`). TRACKS' LEN knob is the same length.

## MOTION: recorded knob turns

MOTION records knob turns per step while a track is armed and playing, and plays them back with the pattern.

| Knob | Label | Behaviour |
| --- | --- | --- |
| **KNOB 1** | PLAY | right: ON, left: OFF |
| **KNOB 2** | MOTION | the recorded event count (read-only) |
| **KNOB 3** | LOCKS | the parameter-lock count (read-only) |
| **KNOB 4** | CLEAR | a right turn asks `CLEAR MOTION T<n>?`; [[OCT+]] confirms. Clears motion only, not locks |

Up to **1024 events** per project are shared by all tracks and by parameter locks (`MOTION FULL`, `LOCKS FULL`). The graph shows the track's steps with event marks: solid when PLAY is on, dithered when off. Fader and knob moves from the SMC-Mixer and MIDI LEARN are recorded as motion only where stated in their sections. LFO RATE, MORPH and DEPTH knobs are recordable; SRC and DEST are not.

## SONG: chaining projects

SONG chains up to **16 rows**. Each row plays one project slot a number of times.

| Knob | Label | Values |
| --- | --- | --- |
| **KNOB 1** | ROW | `n/count`, or `+` for the add-row position |
| **KNOB 2** | SLOT | A to D |
| **KNOB 3** | REPS | x1 to x16 |
| **KNOB 4** | LOOP | OFF, ON |

- Turn [[KNOB 2]] or [[KNOB 3]] on the `+` row to add a row.
- Hold [[REC]] on a row: `DELETE ROW n?`; on the `+` row: `CLEAR SONG?` ([[OCT+]] yes).
- [[PLAY]] on SONG plays the song. A row ends when its longest track has played its repeats. Each row plays its slot's variation 1 and its own project's locks.
- Messages: `ADD A SONG ROW`, `PATTERN A EMPTY`, `STOP FIRST`, `STOP TO EDIT` (an edit during a song), `STOP SONG TO CUE`.

## Tempo, swing, clock

- **BPM** 40 to 240 (default 120): on TRACKS [[SELECT]] sets it (a `TEMPO` popup shows); on GLOBAL 1/3 [[KNOB 1]] sets it on any page.
- **SWG** (GLOBAL 1/3) is global swing, 0 to 100 %; each track adds its own.
- When the clock is external, the tempo controls say `CLK USB`, `CLK TRS`, `CLK BLE` or `CLK LINK` instead.
- The four boxes on HOME above `PROJ` show the bar of a four-bar cycle.

## Live cue and variations

While a pattern plays you can queue the next project slot (A to D) or another variation, and it comes in exactly where the pattern ends (its longest track's), every track from its first step, in time.

- On HOME and the SEQ pages the first 8 black keys (F#3 to A#4) queue variations 1 to 8 of the playing slot: the playing one's key lit, a queued one blinking (not with KEYS on).
- **Hold SAVE** (0.4 s, or press a key with it) for the **cue map**: white keys 1 to 4 queue slots A to D; black keys queue variations 1 to 8 (lit: used, winking: playing, blinking: queued, breathing: unsaved edits). A slot key then a variation key queues both (`CUE>B3` on top). The same key again switches at the next step; the playing slot's key cancels; stopped, a cue switches at once; STOP drops a queued cue. Keys pressed with SAVE play no note, send no MIDI and record nothing.
- A **slot** switch brings the slot's sounds, steps, timing and locks; the tempo, swing, clock, MIDI, master effects, Grids, mutes and selected track stay. The old sounds fade out over the last 5 ms. A **variation** switch changes the steps and timing only; the sounds ring on.
- Each slot has **8 variations** (both versions): variation 1 is its own steps; 2 to 8 hold their own steps and timing and share the slot's sounds and locks. A variation never used before starts as a copy of the one playing. A slot comes back on the variation it played last. When a slot's variations are full: `VARIATIONS FULL`.
- Following an external clock (USB or TRS) the switch comes on the pulse that ends the pattern.
