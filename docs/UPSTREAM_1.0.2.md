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
  stopped, retried); the flash-erase silence taken with IRQs off first (upstream `st_erase` order).
- **Timing & MIDI:** track + global swing capped at 100 (upstream issue #31, `track_swing`, shared with the
  slicer); TRS MIDI ring overflow resets the running status (`midi_uart.c`), realtime bytes queued, TRS MIDI on by
  default (upstream: "RX verified on hardware"); `audio.c` output shift without UB, CPU meter remainder kept.
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
  `storage.c` (not upstream's FM6 objects).
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
