# Microtiming, record quantize, step copy tools — design

Date: 2026-10-31. Base: main aba1bd1 (0.26.0 + the unreleased fx027 work). Decisions are the owner's (2026-10-31).

## 1. Microtiming

- **Per step**, -50 .. +50 (int8). -50 = the hit halfway to the previous step, +50 halfway to the next: the hit
  sounds at the step's start + micro / 100 x the step's length (step_samples: DIV, swing as played).
- **RAM**: step_t's spare byte (`rsv`) becomes the micro (it is free in RAM today). No RAM growth.
- **Project record** (new magic "FDRG", same 3840 B): the 7 parameters past the block (P_NSTORED .. P_COUNT-1: KEYF,
  SRR, ITYPE, IA, IB, IC, IMIX; today in steps 0..6's rsv, stored as offsets from their defaults) move into the
  reserved `rsv_mo` (8 tracks x 7 B = 56 B of its 504), and every step's rsv stores its micro. FDRF (and older)
  records convert on load (micro 0). Downgrade: safe, not supported (an older firmware shows LOAD FAILED).
- **Variations** (varpack.c): the packed step's flags byte bit 6 = "a micro byte follows" (only for micro != 0), so a
  quantized variation costs nothing more and a nudged step 1 B. Old banks never set bit 6: they read as before.
  Capacity (3576 B for variations 2..8 of a slot): a 16-step groove of ~40 hits 218 -> 258 B (7 fit), 64 steps ~150
  hits 548 -> 698 B (6 -> 5 fit), every step set 1634 -> 2146 B (2 -> 1). VARIATIONS FULL as today when it doesn't fit.
- **Playback**: positive: the hit (and its ratchets, its locks, its PROB roll as today) happens late within its step;
  negative: the step's hit fires during the previous step at its offset (the sequencer looks one step ahead). The
  step's locks apply from its hit and snap back at its own step's end as today. Ratchets spread from the hit over the
  rest of its span. CPU: a few checks a track a block; no render-loop budget change (the sequencer is outside them).
- **Editing**: STEP page, a step held: KNOB 4 = MICRO (-50..+50; KNOB 1-3 PROB RATCH ACC as today), the step's value
  shown; a nudged step gets a mark in the grid (left / right of its box).
- Copy / paste, variations, cue, song rows, slot COPY/PASTE carry it (it is part of the step).

## 2. Record quantize

- **QUANT** on GLOBAL 3/7 (the free KNOB 4): 1/16 (default), 1/8, 1/32, OFF. A device setting (the settings record's
  palette word: two free bits), like MUTE NOW/BAR and METRO.
- Relative to the track's own step (exact at DIV 1/16): 1/16 = the nearest step, micro 0 (today's behaviour);
  1/8 = the nearest even step (two-step grid); 1/32 = the nearest half step (micro 0 or +-50); OFF = the nearest step
  and the hit's real offset as its micro (clamped to +-50).
- Applies to live recording (the keys, MIDI notes, the SMC); step record (KEYS, stopped) has no timing: micro 0.

## 3. Step copy tools (SEQ held: the TOOLS screen)

- **SEQ held** 0.4 s (or SEQ + a key at once): the TOOLS screen while held, as GLO's MUTE screen; a SEQ tap still
  turns the SEQ pages (on the release). SEQ at power-on (safe start) unchanged. Keys pressed with SEQ are the screen's
  until let go (no note, no MIDI, nothing recorded).
- Acts on the **selected track**; DUPLICATE on the whole pattern (every track).
- White keys = the 16 steps of the bank shown (OCT- / OCT+ the bank); the screen shows the bank's steps, the tools,
  the clipboard's state and the selection.
- Black keys:
  - F#3 **COPY STEP**: held + a step = that step into the step buffer.
  - G#3 **PASTE STEP**: held + steps = the buffered step into each.
  - A#3 **COPY PAGE**: the bank's 16 steps into the clipboard.
  - C#4 **PASTE REPLACE**: the clipboard over the bank shown.
  - D#4 **PASTE MERGE**: the clipboard's set steps into the bank, the others kept.
  - F#4 **PASTE FILL**: the clipboard into every bank of the pattern (up to its LEN).
  - G#4 **COPY SEL**: a selection page: KNOB 1 START, KNOB 2 END (1..64, shown on the keys of the bank), G#4 again =
    copy (the clipboard holds START..END).
  - A#4 **PASTE SEL**: then a step = where the clipboard starts (clipped at 64).
  - C#5 **DUPLICATE**: every track's pattern doubles: LEN x2 (64 at most), steps LEN.. = steps 0.., their locks and
    micro too.
- A copied step carries **everything**: on, acc, PROB, ratchet, note, slide / tie, micro and its parameter locks
  (the lock bank's events at that step: pasted for the target steps; LOCKS FULL -> as many as fit and the message).
- Buffers in .bss: the step buffer (1 step + its locks) and the clipboard (64 steps + their locks: up to 192 lock
  events, 3 B each): ~900 B (the Bluetooth build has ~5.1 KB free).
- Edits are RAM edits as any (flash is the truth: SAVE keeps them, LOAD asks DISCARD EDITS?).

## Tests (TDD)

Micro: timing of + and - hits at several LEN / DIV / swing and on an external clock; ratchets / PROB / locks with
micro; FDRF -> FDRG conversion (the 7 params intact); variation pack/unpack with micro (+ old banks); capacity.
Quantize: each mode's placement, OFF writes micro. Tools: each tool, the SEQ tap still turning pages, keys silent,
locks copied, DUPLICATE at LEN 16 / 40 / 64, LOCKS FULL partial paste.
