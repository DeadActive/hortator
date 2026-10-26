# Live cue, variations, FILL, slot copy — design

Date: 2026-10-26. Status: draft for the owner's approval. Base: main 2690e74 (drum 0.24.0, published).
Prior art: mono2k by Iñigo Hernández (github.com/InigoHernandez/mono2k, GPL-3.0-only, also an FM-1 firmware), whose
queued pattern switch, key feedback and FILL behaviour we follow. Any code taken from it keeps its SPDX header and
credit; mostly behaviour is ported, since its data model differs.

## Intent

Play Hortator live like a pattern-based groovebox: queue the next slot or a variation while it plays and have it
come in at the pattern's end, play fills on demand, and copy whole slots, without stopping the transport.

## Owner's decisions

- **Live cue of slots A–D:**
  - SAVE held + white key 1–4 queues slot A–D.
  - A cued switch changes everything in the slot: sounds, steps, timing, locks.
  - It comes in at the end of the pattern, the longest track's, as SONG rows already end.
  - The same key again while queued switches at once.
  - Stopped: the switch is immediate.
- **Variations:** 8 per slot (variation 1 is the slot's own steps).
  - SAVE held + white key 5–12 queues variation 1–8 of the playing slot, with the same queue / again = now rules.
  - A variation holds steps and timing only, and shares the slot's kit and the slot's lock bank (locks apply per step
    in every variation where that step plays).
  - Room comes from user sample slot 2, which shrinks from 80 KiB to 48 KiB, as USR3 did in 0.24.0.
- **FILL:** REC held (on pages where REC held does nothing today, i.e. off the SEQ and SONG pages) plays a fill while
  held. Steps set to FILL play only during a fill; steps set to !FILL play only outside one.
- **Copy / paste:** of a whole slot (A–D).

## Non-goals

- mono2k's full per-pattern sound model (64 patterns each with their own sounds): it does not fit Hortator's flash
  (about 7 KB per pattern).
- Per-variation locks; copying a track or a bank of steps; "fill on the next loop"; a song made of variations.
- MIDI program change for cueing (later).

## Anti-goals

- Bricking, or touching any flash outside the ranges below. A failed or partial write must never be misread.
- Clicks or timing slips at a switch. A switch lands exactly on the pattern boundary; ringing voices are declicked.
- Losing unsaved edits silently when cueing away from a slot.
- Changing what an existing gesture does where the owner did not ask: SAVE tap (projects page), REC tap (arm / REC
  ALL), REC held on SEQ / SONG (CLEAR / DELETE ROW).

## Constraints

As every recent change:
- frozen files identical except an approved storage.c object-table change (frozen-base-11, a local tag, as -9 / -10)
- no 64-bit divide
- budgets never raised
- TDD; all six suites, web, sim
- loader erase-scan safety: payloads ≤ 3584 B

## Storage for variations (owner: USR2 → 48 KiB)

- **USR2:** 0xB4000–0xC7FFF (80 KiB) becomes 0xB4000–0xBFFFF (48 KiB). The freed range 0xC0000–0xC7FFF is 8
  sectors.
- **New objects:** OBJ_VAR0..3, one per project slot, two copies, at 0xC0000 + slot × 0x2000 + copy × 0x1000.
  - inner magic "VAR1" + the owning slot
  - a link to the project record, as the lock banks do (a torn save keeps a matching pair)
  - payload ≤ 3584 B
- **The USR2 one-time clamp** follows exactly 0.24.0's USR3 clamp:
  - zones past 48 KiB dropped, and the stored length rewritten once
  - "USR2 SHORTENED TO 48K"
  - on a clamp failure: no variation writes that boot ("USR2 CLAMP FAILED")
  - a power cut during it empties only USR2
- **Content:** variations 2–8 of the slot (variation 1 stays in the project record's steps), each packed sparse:
  - a 64-bit trig bitmap per track
  - 3 B per on-step: the step fields
  - the per-track timing: LEN / DIV / SWING etc. as stored per track
- **Capacity:** about 400 B for a typical variation, 1.6 KB dense, so 7 variations fit typically. A save that would
  exceed the record says "VARIATIONS FULL" and keeps RAM. An empty variation costs 2 B.
- **RAM:** the playing slot's packed variations stay in RAM, about 3.6 KB, plus ONE "parked" buffer of the same size
  (owner). The BLE build has about 12 KB free, so all four slots (14.4 KB) do not fit.
  - The other slots' packed variations are read from flash (XIP) on a slot cue, in the main loop, before the switch
    point. A parked slot is read from the park buffer instead.
  - **Parking:** cueing away from a slot whose variations have unsaved edits parks them in the park buffer. If the
    park buffer already holds another slot's unsaved edits, the cue is refused with "SAVE A FIRST" (A = the parked
    slot; saving is done while stopped, as today). No silent loss.
  - SONG rows play a slot's variation 1.
  - **USB AUDIO 10 CH (owner, 2026-10-26):** the parallel branch usbmode (MENU > USB AUDIO 2 CH / 10 CH) will be
    merged with this one. 10 CH exists only in the build without Bluetooth: the BLE build's RAM stays as main's
    (85868 of 98304 B, about 12 KB free); the plain build gets 10 CH (+1.5 KB RAM, +10 KiB pool; usbmode 417f79d's
    build: 74720 of 98304 B, pool 317556 of 344064 B). The variations' ~7.2 KB (playing + park buffer) must fit both:
    the BLE build without 10 CH (~93.2 KB, ~5 KB spare) and the plain build with it (~82 KB, ~16 KB spare). The
    buffers live in .bss, not the pool. Settings bits: this work adds none; usbmode owns the palette word's bit 12
    (a later need takes bit 13 or above).
- Old projects have no variation object: variations 2–8 are empty.
- Downgrade is safe, not supported (as 0.24.0).

## Behaviour

### Cue

- **SAVE held:** the white keys show the cue map:
  - 1–4 the slots: lit = used, wink = playing, blink = queued
  - 5–12 the variations of the playing slot: lit = not empty, wink = playing, blink = queued
  - the top line reads "CUE → B" or "CUE → A3"
- **SAVE tap:** still opens the projects page. The cue acts only while SAVE is held with a key; the SAVE release then
  does nothing else.
- **A queued slot switch:**
  - At the boundary, the slot's tracks become the working state: each track's sounds and parameters, steps, timing
    and locks, applied the way SONG rows apply a slot (`chain_apply`).
  - **Globals stay (owner):** tempo, swing, clock / MIDI / sync / routing, master FX (delay, reverb, comp), Grids, the
    current mutes and the selected track are not changed by a cue.
  - The old voices fade out over 5 ms at the boundary (declick), not cut hard.
- **Unsaved edits:**
  - The playing slot's unsaved edits are first written back to its RAM image, so cueing back finds them.
  - **Flash is the truth (owner):** edits live in RAM until SAVE. LOAD of a slot with unsaved edits asks
    "DISCARD EDITS?". After a reset or power cut, every slot is its saved flash version, with or without locks; the
    builder makes persist_boot consistent with that.
  - A slot with unsaved edits shows a mark in the cue map (its LED pattern) and "*" in the top line.
- **Which variation (owner):** a slot comes back on the variation it last played (remembered in RAM, not saved; after
  power-on, variation 1).
- **Queueing both (owner):** holding SAVE, a slot key then a variation key (e.g. B then 3) queues both, shown as
  "CUE → B3". The queue holds one slot target plus one variation target. A variation key alone queues a variation of
  the playing slot (or of the queued slot, if a slot is queued).
- **A queued variation switch:** at the boundary the tracks' steps and timing become that variation's. Sounds and locks
  are unchanged. The outgoing variation's edits are packed back into RAM (VARIATIONS FULL keeps the unpacked edits and
  says so).
- **Editing while playing a cued slot or variation is allowed** (my call; SONG still forbids it). Edits go to the
  playing variation.
- **Cue during SONG:** cueing is ignored ("STOP SONG TO CUE") (my call).
- **The boundary:** the end of the longest track's pattern (its LEN × repeats, as `chain_tick`). The same key again =
  switch at the next step.

### FILL

- **The conditions:** the PROB knob's list grows after 8/8 with FILL and !FILL, so stored values 57 and 58 are new
  codes. Older firmware clamps them, which is fine since downgrade isn't supported.
- **REC:** REC held (0.4 s, my call) on any page except SEQ and SONG = FILL active while held, shown as "FILL" in the
  top bar.
- **REC tap:** keeps today's behaviour (arm on TRACKS / REC ALL elsewhere). It acts on the release when shorter than
  the hold. That adds up to 0.4 s of decision latency to the REC tap off SEQ and SONG (my call; the alternative is a
  different FILL key).
- **SMC REC and learned REC:** a short press only (no fill), as today.

### Copy / paste a slot

- **On the SAVE (projects) page:** new actions COPY and PASTE. My call: the projects page, not a key chord, so an
  overwrite is visible and deliberate.
- **COPY:** remembers the source slot (no RAM copy; all slots are resident).
- **PASTE on a target slot:** the target's RAM image becomes the source's (kit, steps, locks, variations, song; the
  name gets "+" or stays: my call, "COPY" appended if it fits).
- **Undo:** PASTE again on the same target, before any SAVE of it, restores the target from its flash copy
  ("PASTE UNDONE").
- **Flash:** only on SAVE. Paste is refused while playing, with "STOP TO PASTE".
- **The target's variations:** they go to the playing slot's buffer if the target is the playing slot, else to the park
  buffer. If the park buffer holds another slot's unsaved edits, paste says "SAVE A FIRST" (my call). Undo restores
  the variations from flash as well.

## What is left to the builder

- the exact pixels of the cue map and the marks
- the variation packing details
- the internal split: a new `cue.c` / `var.c`, or within song.c / project.c
- message wording

The builder keeps the tests:
- switch exactly on the boundary, at several LENs and DIVs, and on an external clock
- again = now
- unsaved edits survive a cue round trip
- variation pack / unpack round trip
- VARIATIONS FULL
- USR2 clamp, with no write outside 0xC0000–0xC7FFF for the new objects
- FILL / !FILL
- the REC tap is unchanged on SEQ and SONG
- paste and undo, paste refused while playing

Device checklist; README; CHANGELOG.
