# Stage 2 step 2: upstream's USB-MIDI driver for the app (`frozen-base-3`) — design

Date: 2026-10-06. Parent: `2026-10-06-frozen-baseline-stage2-design.md` (the baseline-tag mechanism, the loader pin;
step 1 = `frozen-base-2`, device-checked and merged at b03c74f) and `docs/UPSTREAM_1.0.2.md` §2 (USB-MIDI: hold the
host off when the ring is full, validate packets, queue realtime bytes, a status byte ends an unfinished SysEx).

## 1. Goal and user decisions

- Take upstream's USB-MIDI fixes into the app without changing the update loader (pinned sha256 `cc98eed2…`).
- User decisions: the app gets a fixed copy and the loader keeps today's `usb.c` (roadmap); the copy is
  **upstream's whole file** (not a hand-picked subset).

## 2. Structure

- New `firmware/src/usb_app.c` = upstream 1.0's `firmware/src/usb.c` (727f272) byte for byte; 1.0.1 and 1.0.2 did
  not change it (`git log 1e838e1..db70550 -- firmware/src/usb.c`). It is a frozen file (`tools/check_untouched.py`
  FROZEN_FILES), compared with the tag.
- `firmware/src/felucca.c` (not frozen) includes `usb_app.c` instead of `usb.c`. The update loader
  (`firmware/loader/loader.c`, frozen) keeps including `../src/usb.c`, which stays frozen and unchanged: the loader
  pin (`tools/check_loader.py`) proves the loader did not move.
- The host tests (`tests/drum_host.h`, `tests/ui_host.h` and any other includer of `usb.c` for the app's behaviour)
  include `usb_app.c`.
- USB audio stays compiled out (`FELUCCA_UAC` 0, its default; its ~350 lines add no code); `FELUCCA_CDC` as today.
  With these flags the descriptors and strings equal today's (checked for CDC 0 and 1: `DEV_DESC`, `CFG_DESC`,
  `STR0..2`), so the FM-1's USB identity and the web installer's matching are unchanged.
- Baseline tag **`frozen-base-3`** = `frozen-base-2` + `usb_app.c` (727f272's) + the include switch in `felucca.c`
  (glue the reference build needs, not frozen) + whatever non-frozen glue upstream's file needs to build (§4).
  `tools/frozen_base.txt` names it with its commit (a moved or missing tag fails the checks). Moving the baseline
  is the user's decision: this spec is that decision for `frozen-base-3`.
- `docs/UPSTREAM_1.0.2.md`'s frozen-change table gets the `frozen-base-3` rows.

## 3. Behaviour (upstream's, as it runs in the app)

- **Back-pressure:** an EP1 OUT packet (≤ 16 events) is taken only when the MIDI ring has room for 16 + 8 events
  (8 kept for TRS MIDI); otherwise the packet stays (the host is NAKed) and is retried next poll: a burst is
  delayed, never dropped.
- **Packet checks:** channel messages need CIN = status high nibble and data bytes < 0x80 (program change / channel
  pressure: one data byte); anything else is dropped.
- **SysEx:** realtime bytes (≥ 0xF8) inside SysEx are skipped; any other status byte (or a channel / system
  common message) ends an unfinished SysEx. The UBOOT key (F0 22 24 35 7D F7) and the M-UPGRADE command
  (F0 22 24 35 7F F7) are recognised as before.
- **Realtime:** Clock, Start, Continue, Stop (CIN F, F8 / FA / FB / FC) go into the ring with their source and ms
  time (`midi_in_source`, `midi_in_ms`) for the MIDI clock feature (roadmap item 2).
- **Overflow:** `midi_enqueue` sets `midi_in_overflow` when the ring is full and then refuses everything until
  the reader clears it.

## 4. Glue in our files (not frozen)

- The sequencer's MIDI reader (`seq.c` `midi_block`): on `midi_in_overflow` it discards the backlog
  (`mi_r = mi_w`) and clears the flag; realtime packets (CIN F) are skipped for now; notes / CCs as today.
- TRS MIDI (`midi_uart.c`) keeps writing the ring as today (it moves to `midi_enqueue` in the TRS step).
- A declaration or definition upstream's file expects from upstream's `main.c` / `core.h` (e.g. `fm1_ms`,
  `RING_PUBLISH`) is provided by our non-frozen files, or by the frozen `main.c` only if the tag carries the same
  upstream hunk. If a frozen file other than `usb_app.c` would have to change, Claude stops and asks.

## 5. Safety

- The loader stays byte-identical (pin + its self-test).
- H2 (`tools/compare_upstream.py`) compares the app's USB functions with the `frozen-base-3` build; the stack check
  (H3) as usual (upstream's `usb_poll` runs in TIMER5, also nested in the audio render).
- `check_untouched` covers `usb_app.c` and the unchanged `usb.c`; `tests/guard_test.sh` gets a `usb_app.c` case.
- The app's update entry (M-UPGRADE SysEx → the loader) is tested on the host (§6) and on the device (§7).

## 6. Testing (host)

New `tests/usb_midi_test.c`, through upstream's own functions (`ep1_take`, `midi_in_event`, `sysex_byte`,
`midi_enqueue`) and our reader:
- a full ring: `ep1_take` refuses the packet and queues nothing; after the reader drains, the same packet is taken
  whole (no event lost);
- invalid packets dropped (CIN ≠ status nibble, a data byte ≥ 0x80, CIN 0–3 junk), valid ones queued;
- a status byte aborts an unfinished SysEx; a realtime byte inside SysEx is skipped and the SysEx completes;
- the UBOOT key and the M-UPGRADE command set `uboot_req` / `ota_req`, also split over USB packets and right after
  an aborted SysEx;
- Clock / Start / Continue / Stop queued with CIN F and source USB; the reader skips them (no note, no change);
- overflow: the flag set when full, the reader recovers (ring empty, flag clear, new events accepted);
- a USB note still plays its drum track (as today's MIDI tests).
Plus check_untouched (with the guard case), H2 against `frozen-base-3`, the loader pin, the stack check, the full
suite (`tests/run_tests.sh`), and `DRUM_PACKAGE=1 ./build.sh`.

## 7. On the device (the user)

- Notes from a DAW over USB, including a dense burst: every hit plays, nothing stuck.
- **After installing, run the web installer again with the same package**: the new app must hand over to the
  update loader (the M-UPGRADE command through `usb_app.c`). The pinned loader and the stock UBOOT path stay the way
  back either way.
- The CDC console (if used) and the installer's device matching unchanged.

## 8. Done when

§6 passes, the firmware builds, and the user has checked §7 on the FM-1.

## 9. Out of scope

USB audio (UAC), TRS MIDI on / TRS through `midi_enqueue`, the MIDI clock feature (consuming the realtime
packets), storage hardening, the `st_erase` order.
