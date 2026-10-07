# Song chain + project names (roadmap §3.6) — design

Date: 2026-10-07. Parent: `docs/UPSTREAM_1.0.2.md` §3.6. Upstream reference: Felucca 1.0 (`upstream/main`)
`firmware/src/song_chain.c` (the chain), `ui_name.c` (the NAME screen), their calls in `seq.c` (`chain_start`),
`project.c` (`chain_prepare`, the name bytes), `ui_input.c` / `ui_draw.c` / `ui_graph.c` (the SONG page). Branch `song`
from main `e3fd712` (`drum-v0.11.0`). No frozen file changes.

## 1. Goal and user decisions

A song plays the patterns of the four project slots in an order: rows of {slot, repeat}. Projects get names, typed
on the device.

User decisions (2026-10-07):
- **A row switches only the steps** (upstream): the sounds loaded now play every row.
- **LOOP ON / OFF** per song (KNOB 4 of SONG): OFF stops after the last row (upstream), ON goes back to row 1.
- **A row lasts as long as its longest track** (not track 1 as upstream).
- **Names typed with keys and knobs** (upstream's NAME screen).
- Project format **FDR9**. Version after the merge: **0.12.0**.

## 2. Playing a song

- **Rows:** up to 16 (`CHAIN_ROWS`), each {slot A..D, repeat 1..16}. The order and the LOOP bit are part of the
  current project (saved, loaded, cleared with it).
- **What a row plays:** per track, the slot's steps, its `P_SLEN`, `P_SDIV`, `P_SSWING`, `P_SRC` and its motion. The
  sounds, `song.g` (BPM, swing, FX, ...), mutes and PERFORM stay as they are. The steps and motion are read in place
  from the slot's RAM copy (`proj_slot[]`, all four always in RAM): no copy buffer, no flash read while playing.
- **Motion per row:** the row's slot's events play (motion.c's rules: a value holds until the knob's next event;
  the base comes back at each track's step 0, at a row change and on STOP). Events for `P_E0..P_E7` of a track
  whose model in the slot differs from the track's model now are skipped (upstream: another model's knob would
  change the sound unexpectedly).
- **Start:** PLAY on the SONG page prepares the song in the main loop (every row's slot must be used, else
  "PATTERN B EMPTY" and nothing starts; no rows: "ADD A SONG ROW") and arms it; `seq_start` starts it at row 1.
  With an external clock PLAY waits for the clock as now (the song starts on its first pulse). PLAY elsewhere plays
  the current pattern as now. MIDI Start while armed starts the song; MIDI Continue never starts one.
- **Row length:** at a row's start the reference track is the one whose pattern lasts longest (LEN × step length at
  its DIV; ties: the lowest track). The row moves on when that track has completed its pattern `repeat` times;
  then every track starts step 0 of the next row together (the sample remainder carried, as the clock's exact
  timing). Shorter tracks loop inside the row.
- **End:** after the last row's last repeat: LOOP OFF stops the transport; LOOP ON starts row 1 again.
- **STOP** (PLAY, MIDI Stop, the clock lost): the song ends; every track's `P_SLEN..P_SRC` as before the song and
  `song.rec` (REC arming) come back. The current steps are never changed by a song.
- **While a song plays (or is armed):** step, PATTERN and MOTION edits, TOOLS' clears and the SONG page's
  edits say "STOP TO EDIT"; motion recording is off (`song.rec` is 0 during the song); LOAD says "STOP TO LOAD"
  (the song reads the slots); sound knobs, mutes, PERFORM and live key hits work as usual.

## 3. The SONG page (SEQ family, after MOTION)

- **Graph:** 4 rows visible, each `2  B  ×4  BREAK` (row number, slot, repeat, the slot's name). The selected row
  is highlighted; after the last row an empty `+` row. While the song plays: ▶ on the playing row and "3 LEFT" (its
  repeats still to play, the current one included).
- **KNOB 1 ROW:** selects a row, up to the `+` row (16 rows: no `+`). **KNOB 2 SLOT:** A..D. **KNOB 3 REPEAT:**
  ×1..×16. **KNOB 4 LOOP:** OFF / ON. On the `+` row, a turn of KNOB 2 or KNOB 3 adds a row (the previous row's
  slot, else A; ×1); the turn sets nothing else.
- **PLAY** on this page starts the song (§2); PLAY / STOP during a song stops it.
- **REC held** on a row: "DELETE ROW 3?"; on the `+` row (with rows): "CLEAR SONG?"; OCT+ yes, OCT- no (as "CLEAR
  TRACK n?"). Deleting a row moves the later ones up. TOOLS CLR ALL and INIT ALL clear the song too.
- **While a song plays:** the header shows the song icon and the row number where the octave is (every page); on
  SONG, KNOB 1 still scrolls, the other knobs say "STOP TO EDIT".
- Rows are added at the end only.

## 4. Names

- **PROJECT page:** the slots are shown as **A..D** (were 1..4). Each line: the slot's name, or USED (a project
  with no name), or EMPTY. The page header shows the current project's name (as loaded or last saved).
- **SAVE** (two detents as now) saves at once with the current name (user decision 2026-10-07, after the first
  build: no NAME screen on every save).
- **KNOB 2 NAME** (one detent) on a used slot opens the NAME screen, prefilled with the slot's name, or "PROJECT A"
  (the slot's letter) when it has none. OCT+ writes only the name (to RAM and flash; the slot loaded or saved last:
  the current name too), OCT- cancels; the transport stopped, else "STOP TO SAVE" and the screen stays; an empty
  slot: "EMPTY SLOT".
- **The NAME screen** (upstream's `ui_name.c`): the name large with its cursor, the key map below.
  - White keys (ABC): AB CD EF GH IJK LM NO PQ RS TU VW XYZ, then 123 456 789 0-. ; a tap types the group's first
    character, another tap of the same key within 0.8 s the next (cycling); another key or 0.8 s keeps it.
  - White keys (123): `1234567890-._/#+`, one each.
  - Black keys, by name, both octaves: F# cursor left, G# SPACE, A# cursor right, C# DELETE (held: repeats after
    0.45 s every 90 ms, as the arrows), D# ABC / 123.
  - KNOB 1 the cursor; KNOB 2 the character at the cursor (` ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._/#+`; at the
    end: a new one).
  - At most 12 characters, upper case; spaces at either end dropped when written; empty = no name.
  - While it is open the keys are silent (no hits, no recording, no MIDI out, PERFORM's keys off); PLAY / STOP
    work; leaving the PROJECT page or HOME closes it (cancel).
- **SONG** shows each row's slot name. **INIT ALL** clears the current name.

## 5. Storage (FDR9)

- `chain_config_t {uint8_t count, loop, rsv[2]; chain_row_t row[16]}` (`chain_row_t {uint8_t
  slot, repeat}`) = 36 B after the motion, then `char name[12]` (ASCII 32..126 from the NAME set, 0-padded).
  The project grows from 3544 B to 3592 B (one flash slot holds 3840 B).
- FDR8 (and older) records load as before with an empty song (LOOP OFF) and no name.
- A record whose song is invalid (count > 16, a slot > 3, a repeat 0 or > 16 within count, loop > 1) loads with
  the song dropped (empty); a name with a byte outside the NAME set loads with no name.
- RAM: the running song's state (row, repeats left, reference track, the saved timing `int16_t [8][4]`, REC) and
  the NAME screen's state, about 120 B, in existing structs where possible (new small globals have broken H2).

## 6. Testing (host, failing first)

- New `tests/song_test.c` (drum suite): rows in order with their repeats; the row length follows the longest track
  (a 16-step 1/16 kick + a 64-step hats: the row is 64 steps); LOOP OFF stops after the last row, LOOP ON returns
  to row 1; STOP restores `P_SLEN..P_SRC` and `song.rec`; motion per row; `P_E*` events skipped after a model
  change; an empty slot refused; an invalid order refused; MIDI Start while armed starts the song, Continue does not;
  step / pattern edits blocked during a song; the exact timing at a row change (no drift against a plain loop).
- `ui_test`: SONG page knobs (add row on `+`, SLOT, REPEAT, LOOP), REC-hold DELETE ROW / CLEAR SONG, PLAY on SONG;
  NAME: multi-tap and its timeout, 123 mode, cursor, DELETE (and its repeat), the knobs, trimming, cancel, SAVE
  without the screen, rename, STOP TO SAVE, keys silent while open; PROJECT page names. Screens: SONG (empty, rows, playing),
  NAME (ABC, 123), PROJECT with names.
- `boot_test`: FDR9 round trip (song + name); FDR8 converts (empty song, no name); invalid song dropped; bad name
  dropped.
- The kit hashes (`sound_pack_test`) unchanged with no song.
- For the user's ears: a drumsim WAV of a song A ×2, B ×1 with LOOP ON, 6 bars.
- Firmware: H2 0 different, stack, loader pinned; a target budget line for the song's per-block step (the number
  approved by the user).

## 7. On the device (the user)

- Save two projects with different patterns (A, B), name them with the NAME knob; PROJECT shows the names.
- SONG: rows A ×2, B ×1, LOOP ON; PLAY: the rows change on the bar, the header shows the row; LOOP OFF: stops after B.
- A 16-step kick with 64-step hats in a slot: the row lasts the hats' 64 steps.
- STOP: the current pattern (LEN etc.) as before. Rename a slot; save, power off, load: song and name are there.

## 8. Done when

§6 passes, the firmware builds, the frozen / loader / stack checks are green, CHANGELOG has the line,
DEVICE_INSTALL.md has a SONG / NAME section, the user has checked §7; merged as 0.12.0 (tag `drum-v0.12.0`).

## 9. Out of scope

Inserting rows mid-song, songs longer than 16 rows, rows that change sounds, naming anything but projects, the
web editor's song / name commands, upstream's user presets.
