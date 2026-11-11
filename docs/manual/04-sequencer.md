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
| **KNOB 4** | NUDGE | -50 to +50 hundredths of a step |

- **Accent with a tap:** hold [[OCT+]] and tap a step: the step is accented (an off step turns on accented), or plain again. While [[OCT+]] is held the keys show the bank's accents.
- **Banks:** [[KNOB 1]] on STEP shows `BANK n/m`. [[OCT+]] alone (on its release) goes to the next bank, [[OCT-]] to the previous.
- **Piano roll:** on an ACID track the STEP page is a piano roll; notes, SLIDE and TIE are described under *Playing and recording*.
- **Grids tracks:** a track whose PATTERN SRC is a Grids lane (`G-KCK`, `G-SNR`, `G-HAT`) shows that pattern read-only.
- **Clear:** hold [[REC]] 0.7 s on a SEQ page to be asked `CLEAR TRACK n?` ([[OCT+]] yes). It and TOOLS **CLRSQ** clear the track's steps, its MOTION events and its parameter locks. An off step's first edit turns it on; a held key beyond LEN, or on a Grids track, takes the turn and does nothing.
- **Where the held-step knobs work:** STEP, PATTERN, MOTION, SONG and TRACKS (with no track armed). Under ENV STEPS on other pages the same hold plus a knob is a parameter lock.

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

- **BPM** 40 to 240 (default 120): on TRACKS [[SELECT]] sets it (a `TEMPO` popup shows); on GLOBAL 1/7 [[KNOB 1]] sets it on any page.
- **SWG** (GLOBAL 1/7) is global swing, 0 to 100 %; each track adds its own.
- When the clock is external, the tempo controls say `CLK USB`, `CLK TRS`, `CLK BLE` or `CLK LINK` instead.
- The four boxes on HOME above `PROJ` show the bar of a four-bar cycle.

## Live cue and variations

While a pattern plays you can queue the next project slot (A to D) or another variation, and it comes in exactly where the pattern ends (its longest track's), every track from its first step, in time.

- On HOME and the SEQ pages the first 8 black keys (F#3 to A#4) queue variations 1 to 8 of the playing slot: the playing one's key lit, a queued one blinking (not with KEYS on).
- **Hold SAVE** (0.4 s, or press a key with it) for the **cue map**: white keys 1 to 4 queue slots A to D; black keys queue variations 1 to 8 (lit: used, winking: playing, blinking: queued, breathing: unsaved edits). A slot key then a variation key queues both (`CUE>B3` on top). The same key again switches at the next step; the playing slot's key cancels; stopped, a cue switches at once; STOP drops a queued cue. Keys pressed with SAVE play no note, send no MIDI and record nothing.
- A **slot** switch brings the slot's sounds, steps, timing and locks; the tempo, swing, clock, MIDI, master effects, Grids, mutes and selected track stay. The old sounds fade out over the last 5 ms. A **variation** switch changes the steps and timing only; the sounds ring on.
- Each slot has **8 variations** (both versions): variation 1 is its own steps; 2 to 8 hold their own steps and timing and share the slot's sounds and locks. A slot comes back on the variation it played last. When a slot's variations are full: `VARIATIONS FULL`.
- Following an external clock (USB or TRS) the switch comes on the pulse that ends the pattern.

## NUDGE: a step early or late

On a step page hold a step and turn [[KNOB 4]] (NUDGE): -50 to +50, the hit moved by that many hundredths of a step (+50 is halfway to the next step, -50 halfway to the previous one, 0 on the step). It follows the track's division and swing (+25 is a quarter of the step as it plays).

- A late hit keeps its PROB, RATCH and locks (its ratchet hits spread over what is left of the step). An early hit plays in the step before, its locks from the hit to the end of its own step.
- A nudged step has a mark beside its box in the grid: on the left if early, on the right if late. A tap that turns a step off makes it plain again.
- NUDGE is part of the step: copied, saved and played everywhere (variations, live cue, song rows, slot COPY / PASTE). In a song an early first step does not reach into the row before. Step record writes NUDGE 0. All steps start at 0 and play exactly as before.

## QUANT: where live recording lands

GLOBAL 3/7 [[KNOB 4]] **QUANT** decides where a live-recorded hit goes (keys, MIDI notes and the SMC-Mixer; step record is not affected). It is a device setting, kept over power-off.

| QUANT | The hit goes to |
| --- | --- |
| **1/16** (default) | the nearest step |
| **1/8** | the nearest even step |
| **1/32** | the nearest half step (NUDGE 0 or +-50) |
| **OFF** | the nearest step, keeping how early or late you played as its NUDGE (up to +-50) |

## Step tools: hold SEQ

Hold [[SEQ]] (0.4 s, or press a key with it) for the **TOOLS** screen for the selected track. A quick SEQ tap still turns the SEQ pages, now on its release. The white keys are the bank's 16 steps, [[OCT-]] / [[OCT+]] the bank, and the black keys are the tools:

| Key | Tool |
| --- | --- |
| **F#3** COPY STEP | hold it and tap a step: it goes into the step buffer |
| **G#3** PASTE STEP | hold it and tap steps: the buffered step goes into each (`NO STEP COPIED` if empty) |
| **A#3** COPY PAGE | the bank's 16 steps into the clipboard |
| **C#4** REPLACE | the clipboard over the bank shown |
| **D#4** MERGE | only the clipboard's steps that are on |
| **F#4** FILL | the clipboard repeated over the whole pattern up to LEN |
| **G#4** COPY SEL | [[KNOB 1]] START, [[KNOB 2]] END (1 to 64); G#4 again copies |
| **A#4** PASTE SEL | then tap a step (`PICK A STEP`); hold A#4 to paste at several steps |
| **C#5** DUPLICATE | every track's pattern twice as long, up to 64 steps, the new half a copy (`DUPLICATED`) |
| **D#5** CLEAR | `CLEAR TRACK n?` ([[OCT+]] yes, [[OCT-]] no) |

A copied step keeps everything: accent, PROB, ratchet, note, slide and tie, its NUDGE and its parameter locks. With the locks full a paste brings what fits and says `LOCKS FULL`. Other messages: `COPIED a-b`, `COPIED, LOCKS CUT`, `CLIPBOARD EMPTY`. Keys pressed with SEQ play nothing, send nothing and record nothing; [[PLAY]] works. While a song plays, pastes are refused (`STOP TO EDIT`), copies are fine. The clipboard is kept until power-off, across projects. Holding SEQ while switching on is still the safe start.

### Which variations exist, and a free one

On HOME and the SEQ pages the eight black keys show the variations of the playing slot: the playing one **lit**, a queued one **blinking**, the others in use **dim**, a free one **dark**. The dim light shows even with BACKLIGHT OFF.

Pressing the key of a variation that was **never used** (a black key, or SAVE held plus a black key) asks what to start with: `VARIATION 3 IS FREE`. [[OCT+]] **COPY** starts it as a copy of the playing variation, [[OCT-]] **CLEAR** starts it empty (every step off, with the playing variation's LEN, DIV, SWG and SRC), [[HOME]] changes nothing. While playing, the answer queues the variation for the end of the pattern. Once you leave it, it counts as in use, even if it is empty. The web editor's cue still starts a free variation as a copy.

To put a copy of one variation over another, see COPY and PASTE under *Projects and tools*.
