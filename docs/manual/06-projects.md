# Projects and tools
<!--blurb: Saving, loading, naming, copying slots, TOOLS, and what survives power-off.-->

## Projects

A project holds the eight tracks' sounds, patterns, timing, locks, motion, variations, effects settings, Grids, compressor and song. There are **four slots, A to D**, saved in the FM-1's flash. Tap [[SAVE]] (on the release) for **PROJECT 1/3**; **TOOLS 2/3** and **COPY 3/3** follow.

| Knob | Label | Behaviour |
| --- | --- | --- |
| **KNOB 1** | SLOT | A to D |
| **KNOB 2** | NAME | a right turn opens the NAME screen |
| **KNOB 3** | LOAD | GO action: turn right once to arm (`AGAIN: LOAD`), again within about 1.5 s to act |
| **KNOB 4** | SAVE | GO action, armed the same way |

- The list rows show `A*` when a slot has unsaved edits and `#` when it is the copied slot; `NOW` shows the current project; `EMPTY` marks an unused slot.
- **The last project saved loads at power-on** (`LOADED` in the top bar, stopped). If it cannot be loaded, the FM-1 asks `LOAD FAILED: SLOT B` / `START NEW PROJECT?`: [[OCT+]] (YES) forgets it, [[OCT-]] (NO) tries again at the next power-on; both carry on with the power-on kit. A safe start loads nothing.
- **Edits stay in RAM until SAVE.** A reset or power cut brings every slot back as last saved. Leaving a slot by LOAD or a cue keeps its edits in RAM, so going back finds them. LOAD of a slot with unsaved edits asks `DISCARD EDITS?` (a tempo change counts). SAVE onto another slot with unsaved edits asks `OVERWRITE B*?`. SAVE to another slot moves the edits there. RENAME of a slot with unsaved edits renames it in RAM only (`RENAMED (SAVE TO KEEP)`). Messages include `SAVED`, `SAVED (RAM)`, `SAVE A FIRST`, `STOP TO LOAD`, `STOP TO SAVE`. Unsaved variations of one slot can wait while another plays; a cue of a third slot then says `SAVE C FIRST`. A power cut during SAVE keeps the previous save whole.
- **Device settings are not in projects:** the colour theme, ACCEL, BACKLIGHT, SPEAKER EQ, USB LEVEL, USB AUDIO, PERFORM mode, ZOOM, METRO, THRU, MUTE timing, MIDI LEARN bindings and Bluetooth settings.

## NAME

Rename a project on the device (up to 12 upper-case characters, transport stopped).

- White keys type with multi-tap (0.8 s): AB CD EF GH IJK LM NO PQ RS TU VW XYZ and 123 456 789 0-. . A mode toggles ABC and 123.
- Black keys: F# cursor left, G# space, A# cursor right, C# delete (held repeats), D# toggles ABC / 123.
- [[KNOB 1]] moves the cursor, [[KNOB 2]] changes the character. [[OCT+]] writes the name; [[OCT-]] or [[HOME]] cancels.

## TOOLS

TOOLS 2/3. All four are GO actions: the first right-turn detent arms (`AGAIN: <label>`), a second within about 1.5 s acts. A left turn does nothing.

| Knob | Label | What it does |
| --- | --- | --- |
| **KNOB 1** | CLRSQ | clears the selected track's pattern (`PATTERN CLEARED`) |
| **KNOB 2** | INIT | resets the selected track's sound to its model default, including FILTER (`SOUND INIT`) |
| **KNOB 3** | CLR* | clears all patterns, resets LEN, DIV, SWG and SRC to defaults, and clears the song (`ALL PATTERNS CLEARED`); blocked during a song |
| **KNOB 4** | INIT* | everything back to power-on: kit, sounds, patterns, FX, Grids, COMP, BPM, motion, song, PERFORM knobs; saved projects and device settings are untouched (`ALL INIT`) |

## COPY and PASTE

COPY 3/3 copies a whole slot.

| Knob | Label | Behaviour |
| --- | --- | --- |
| **KNOB 1** | SLOT | A to D |
| **KNOB 2** | COPY | remembers SLOT's slot (`COPIED A`; `EMPTY SLOT` if unused) |
| **KNOB 3** | PASTE | turn twice: makes SLOT's slot a copy (kit, steps, locks, variations and song), named `... COPY`, unsaved until SAVE |

PASTE again on the same slot before saving undoes it (`PASTE UNDONE`). Not while playing (`STOP TO PASTE`); `COPY A SLOT FIRST`, `SAME SLOT`. The tempo and other globals are kept when pasting onto the loaded slot.

## Compatibility

- Projects saved by a newer build may not load in an older one; each version's notes say so (see *Version history*). Projects saved since 0.24 show an empty slot on 0.23.x and older: going back is safe but not supported.
- If a project's lock bank cannot be read, it loads without its locks and says `LOCKS LOST` once; unreadable variations say `VARIATIONS LOST`.
- **User sample slots** USR2 and USR3 hold 48 KiB each: the rest of their flash holds the projects' variations and lock banks (see Sounds).
