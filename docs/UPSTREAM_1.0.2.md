# Upstream Felucca 1.0 – 1.0.2: what the drum firmware takes (roadmap)

Date: 2026-10-06. Upstream `hugelton/Felucca` moved from our base 1e838e1 to 1.0.2 (727f272 "Felucca 1.0 source",
20c275e 1.0.1, db70550 1.0.2; 154 files, ~40k lines). Upstream labels 1.0.2 "field testing" (1.0.1 was rolled back
once, no reason given). The update path is unchanged (same package format, `ota.c` / loader comments only); the
loader *binary* changed only because `usb.c` (included by the loader) changed. Our packages still install with the
web installer; upstream's "Return to official V15" needs a backup protocol we do not have (M-UPGRADE and
`fm1_install.py` still return to stock).

The user chose (2026-10-06) everything below, in this order; each item gets its own design (and spec / plan where
it is large) before any code.

## 1. Fixes in our own code (no frozen file)

- **Saving & flash:** no project / settings save while playing (upstream "STOP TO SAVE"; settings saved once
  stopped). Done.
- **Timing & MIDI:** track + global swing capped at 100 (upstream issue #31, `track_swing`, shared with the
  slicer); TRS MIDI ring overflow resets the running status (`midi_uart.c`); `audio.c` output shift without UB,
  CPU meter remainder kept. Done.
- Moved to §2 (they change the compiled frozen code, so they need the new baseline): the flash-erase silence
  with IRQs off first (`st_erase` is inlined into frozen `st_save`), TRS MIDI on by default (`FELUCCA_UART`
  compiles extra code into frozen `main.c`), queuing realtime bytes (needs upstream's `midi_enqueue` in `usb.c`).
- **Controls:** `panel_init` refuses a button / knob learned twice; knob acceleration only on large ranges, same
  direction, slower of two detents, capped (upstream #23 / #52), with a MENU switch.
- **Installer:** `fm1ota.js` (no "only output" pairing, resume waits for `ota-FM-1` and checks the identity),
  `fm1_install.py` (stock V15 by SHA-256), the site ships LICENSES / LICENSING.md.

## 2. Fixes inside the frozen files (user: all, including USB-MIDI)

Needs a short spec first: the frozen-code check (H2, `tools/check_untouched.py`) moves from "identical to
1e838e1" to a documented fork baseline (1e838e1 + these upstream hunks); the update loader stays byte-identical
(sha cc98eed2…, 6493 B) by building it from the 1e838e1 `usb.c` while the app gets the fixed copy.
- Key debounce (2-frame press, 8-frame release) and the encoder learning fix (double / dead clicks, #23)
  — `hal/fm1_input.h`.
- TIMER5 above the audio render (no LED flicker, no lost encoder steps under load) — `main.c`; stack depth checked.
- USB-MIDI: hold the host off when the ring is full (no dropped / stuck notes), validate packets, realtime bytes
  queued (USB clock in), a status byte ends an unfinished SysEx — `usb.c` (the app's copy).
- Storage hardening: sequence wrap, header slot check, oversize loads refused, full read-back compare —
  `storage.c` (not upstream's FM6 objects); with it the `st_erase` order (IRQs off before the silence).
- TRS MIDI on by default, realtime bytes (clock) queued from TRS and USB.
Skipped: the LED glow (1.0.1), USB audio, the minsize build marks on frozen functions.

## 3. Features (each its own design / spec / plan)

1. **Sound pack:** BASS+ speaker EQ (`fx.c spk_bass`, with 1.0.2's #42 fix), SPRING reverb (`rev_spring`, the room
   reverb's buffers), slower divisions 1/2, 1/1, 2BAR, 4BAR (appended IDs).
2. **MIDI clock in** (`midi_clock.c`): INT / USB / TRS; Grids clock and the LFOs follow it.
3. **PERFORM layer** (`perform.c`): hold-to-play master FX (REPEAT, REVERSE, TAPE STOP, FREEZE, filters, OCT,
   THROW, CRUSH), using our SLICER buffers; 8 mute bits.
4. **Motion recording** (`motion.c`): knob moves recorded per step; places widened for 8 tracks; coexists with
   the LFOs' modulated copy.
5. **Song chain + project names** (`song_chain.c`, `ui_name.c`): one project format step (FDR6) for both.
6. **PHYS percussion** (`eng_phys.c`, `phys_dsp.c`, MIT DaisySP / Rings): MEMB and MODAL, e.g. as RESON's modal
   models; its 64/32 divisions replaced (integer target rule).
7. **Anti-aliased UI** (`gfx.c`, Inter Tight OFL, themes, Fukiai icons, keycaps, render lint): last, after the
   screens settle; ~45 KB flash; `main.c`'s FONT_S / FONT_L / C_* kept by shims.

Skipped: FM6, upstream's DRUM engine (duplicates ours), melodic / synth engines and editors, USB audio, quick
layers, LED glow. Parked separately: the 303 voice (`docs/IDEAS.md`).

## Frozen-file changes (by baseline tag)

The frozen-code baseline is named in `tools/frozen_base.txt` (with the pinned update loader's sha256). A tag is
1e838e1 + exactly the upstream hunks below (and, marked "not frozen", what its reference build needs).

| Tag | Upstream | File | Hunk | Why |
| --- | --- | --- | --- | --- |
| frozen-base-2 | 727f272 (1.0) | `firmware/hal/fm1_input.h` | the whole file (1.0's, without 1.0.1's LED glow) | key presses after 2 frames, releases after 8; the encoder learns one detent state (#23: double counts, dead knob) |
| frozen-base-2 | 727f272 (1.0) | `firmware/src/main.c` | `fm1_timer5_irq`; `timer5_start` priority 4; `felucca_dbg.in_audio = 0` at boot | the scan nests into the audio render: no ~16 Hz LED flicker, no lost encoder frames; USB / UART polls after the render |
| frozen-base-2 | 727f272 (1.0) | `firmware/src/audio.c` (not frozen) | `t5_nested_ticks` | the render's time excludes the nested scan |
| frozen-base-2 | — (user decision) | `tools/build.py` (not frozen) | the app with `-mllvm -enable-global-merge=false` (the loader's flags unchanged) | the merged small statics differed between the fork's and the baseline's builds, so H2 could not name the frozen code's data alike |
| frozen-base-2 | — (user decision) | `tools/compare_upstream.py` (not frozen) | `PTR_ARG`: in the reviewed inlining `ota_send_msg` + `ota_wire_send`, the callee's pointer argument is `ota_wire` | the inlined call reads `ota_wire` through its pointer argument; the binding is one recorded fact, and a self-test shows a wrong binding fails |
| frozen-base-3 | 727f272 (1.0) | `firmware/src/usb_app.c` (new, frozen) | 727f272's `usb.c`, the whole file (USB audio compiled out) | the app's USB-MIDI: the host held off while the ring is full (no dropped / stuck notes), packet checks, a status byte ends an unfinished SysEx, realtime queued (USB clock in); the update loader keeps `usb.c` (pinned) |
| frozen-base-3 | 727f272 (1.0) | `firmware/src/felucca.c` (not frozen) | `#include "usb_app.c"` instead of `usb.c` | the app compiles upstream's driver |
