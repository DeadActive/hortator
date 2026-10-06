# Stage 2 step 4: TRS MIDI on by default (`frozen-base-5`) — design

Date: 2026-10-06. Parent: `2026-10-06-frozen-baseline-stage2-design.md` (the baseline-tag mechanism, the loader pin)
and `docs/UPSTREAM_1.0.2.md` §2 ("TRS MIDI on by default, realtime bytes (clock) queued from TRS and USB"). Steps 1-3
(`frozen-base-2` .. `frozen-base-4`) are merged (main e29d387, `drum-v0.5.0`).

## 1. Goal and user decisions

- The TRS MIDI input works without a special build: `FELUCCA_UART` defaults to 1, as in upstream 1.0 (which
  verified the UART RX on hardware).
- TRS bytes enter the same input ring as USB-MIDI through upstream's `midi_enqueue`, with a timestamp and the source
  (TRS = 2). Clock, Start, Continue and Stop are queued too, for the later MIDI clock feature (roadmap §3.2); until
  then the sequencer ignores them, as it ignores USB's today.
- User decisions (design approval, 2026-10-06): this design; the version after the merge is **0.6.0** (a new
  input, docs/VERSIONING.md).
- The update loader stays byte-identical (it does not include `midi_uart.c`; sha cc98eed2…, 6493 B).

## 2. What changes

### 2.1 `firmware/src/felucca.c` (not frozen)

```c
#ifndef FELUCCA_UART
#define FELUCCA_UART 1           /* TRS MIDI IN on UART1 / PH8 (upstream 1.0: on, RX verified on hardware) */
#endif
```

`FELUCCA_UART=0 ./build.sh` still builds without it (`tools/build.py` passes the flag through).

### 2.2 `firmware/src/midi_uart.c` (not frozen; upstream `727f272`'s)

Taken from `727f272:firmware/src/midi_uart.c`, keeping our DEADACTIVE credit line and the "(upstream 1.0)" notes:

- **Realtime:** `F8` Clock, `FA` Start, `FB` Continue, `FC` Stop are queued as one-byte USB-MIDI packets
  (`0xF | b << 8`, the same form `usb_app.c` queues for USB) by `midi_enqueue(pkt, 2)`; `msgs` counts them, a
  refused one counts in `drops`. Other realtime bytes (`FE` Active Sensing, `FF` Reset, undefined `F9` `FD`) are
  dropped. Every realtime byte keeps the running status and a message in progress.
- **Channel messages:** `midi_enqueue(pkt, 2)` instead of writing the ring directly; a refused message sets
  `midi_in_overflow` and counts in `drops`.
- **UART ring overflow** (bytes lost before parsing): as today, the running status ends; also `midi_in_overflow`
  is set, so `seq.c`'s `midi_block` drops the backlog (the same recovery as a USB overflow).

`midi_enqueue` (frozen `usb_app.c`) stamps `fm1_ms` and the source, refuses while `midi_in_overflow` is set, and sets
it when the ring is full. `midi_block` runs in the audio ISR; the TRS poll runs in TIMER5 only outside the audio
render (`main.c` owes it until `in_audio` clears), the same context as `usb_poll`, so no new race.

### 2.3 What the drum firmware does with TRS input (unchanged code in `seq.c`)

`midi_block` reads every packet from the ring whatever its source. A note-on on the drum channel (MENU → MIDI
channel, `G_DRCH`) plays every track whose NOTE equals the note number, with the velocity. Other channels,
note-offs, controllers and realtime packets are read and ignored. TRS therefore behaves exactly like USB.

### 2.4 `firmware/src/console.c` (not frozen; upstream's diagnostics)

`status` prints `uart_enabled` and, when it is on, `uart_rx_bytes`, `uart_rx_msgs` and `uart_rx_drops`, after
`midi_tx_pkts` (upstream 1.0's lines).

### 2.5 `firmware/src/main.c` (frozen; no text change)

Its `#if FELUCCA_UART` code now compiles: TIMER5 owes the UART poll on every 5th tick (2 kHz) and `fm1_main` calls
`uart_midi_init()` before `timer5_start()`. That code is upstream's text, already in our `main.c`.

## 3. Baseline and safety

- Tag **`frozen-base-5`** = `frozen-base-4` + `FELUCCA_UART 1` in the tag's `felucca.c` + `727f272`'s
  `midi_uart.c` (glue: the reference build must compile the same `main.c` UART code, and `uart_midi_poll` may be
  inlined into the frozen TIMER5 handler). `tools/frozen_base.txt` names it with its full commit; the checks fail
  on a moved or missing tag. `docs/UPSTREAM_1.0.2.md`'s frozen-change table gets its rows.
- H2 (compare_upstream) against `frozen-base-5`: 0 different. `check_untouched`: no frozen file's text changes.
- Loader pin + self-test; the stack check (TIMER5 path with the UART poll, within 75 %); target budgets; the full
  suite; `DRUM_PACKAGE=1 ./build.sh` (the package and `build/site`).
- Pins: `uart_midi_init` sets up PH8 as upstream does; nothing else in our firmware uses PH8 or UART1.

## 4. Testing (host, failing first)

`tests/midi_uart_test.c` (parser):
- switches its driver include from `usb.c` to `usb_app.c` (where `midi_enqueue` lives, as upstream's test does
  with its `usb.c`); its SysEx / OTA cases keep testing the same functions, which `usb_app.c` also has. The
  loader's own `usb.c` stays covered end to end by `ldr_test`.
- the mixed-stream case expects the `F8` inside a running-status note stream as packet `0x0000F80F`, with the notes
  around it intact (upstream's expectation);
- a TRS `FA` is queued with `midi_in_ms` = `fm1_ms` and `midi_in_source` = 2; a USB realtime packet with source 1;
  `FE` and `FF` queue nothing;
- the ring overflow case (kept from ours): bytes lost to a UART ring overflow end the running status **and** set
  `midi_in_overflow`;
- a full input ring: the next TRS message is refused, sets `midi_in_overflow` and counts one drop.

Drum suite (`tests/drum_host.h` already compiles `midi_uart.c`), a new case:
- bytes fed through `um_byte` — a note-on on the drum channel for a track's NOTE, a clock byte in the middle of it,
  a note-on on another channel — then `midi_block`: the track with that NOTE is hit once with the velocity, no other
  track is hit, the ring is empty;
- after a TRS overflow, `midi_block` drops the backlog, clears the flag, and the next TRS note plays.

Build check: `FELUCCA_UART` is on in the built app (`uart_midi_poll` / `uart_midi_init` code present in
`build/felucca.dis`), so a default that silently flips back is caught.

## 5. On the device (the user)

- A keyboard or sequencer on the TRS MIDI input (the FM-1's TRS type, as upstream documents) sending on the drum
  channel plays the tracks whose NOTE matches, as over USB. USB MIDI still works at the same time.
- Nothing plugged into TRS: nothing changes (no stray hits; console `status` shows `uart_rx_bytes 0` if you use it).
- A clock from TRS does nothing yet (the MIDI clock feature comes later); notes still play while it runs.
- Panel, audio and saving behave as before (the extra 2 kHz poll is small).

## 6. Done when

§4 passes, the firmware builds, H2 / loader / stack checks are green, `CHANGELOG.md` has the TRS line and the
version is bumped to 0.6.0 at the merge (tag `drum-v0.6.0`), and the user has checked §5 on the FM-1.

## 7. Out of scope

Following the MIDI clock (roadmap §3.2: `midi_clock.c`, INT / USB / TRS source choice), MIDI out on TRS, SysEx over
TRS (never: no update path from DIN), the `ota_erase` buzz during updates (separate follow-up).
