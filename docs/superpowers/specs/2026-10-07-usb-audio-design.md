# USB audio input (roadmap §3.2, `frozen-base-6`) — design

Date: 2026-10-07. Parent: `docs/UPSTREAM_1.0.2.md` §3.2 and `2026-10-06-frozen-baseline-stage2-design.md` (the
baseline-tag mechanism, the loader pin). Upstream reference: Felucca 1.0 (`727f272`: `usb.c` UAC, `audio.c` hooks,
`console.c` `con_uac`, `tests/uac_test.c`) and 1.0.2 (`db70550`: USB LEVEL FIXED, #42). Branch `usb-audio` from main
`222372f` (`drum-v0.7.0`).

## 1. Goal and user decisions

The FM-1 also shows up on the computer as a class-compliant USB audio input (UAC1): its output, 16-bit stereo at
44.1 kHz, recorded into a DAW over the same cable as MIDI. Record only (no playback into the FM-1).

User decisions (2026-10-07):
- **USB LEVEL** in the MENU, as upstream 1.0.2: **MASTER** (default: the recording follows the volume knob) or
  **FIXED** (the recording always at full level; the knob sets only the speaker / headphones).
- The recording **includes SPEAKER EQ**, as upstream: it is exactly the headphone / speaker signal (FLAT, the
  default, for a plain recording). The master chain is not reordered.
- Version after the merge: **0.8.0**.

Cost (answered for the user): nothing measurable unless a program on the computer opens the FM-1's input (the hooks
test one flag and return; the USB poll returns while the stream is closed); ~2.2 KB RAM reserved always; while a DAW
records, ~10 instructions a sample plus one 1 ms packet. USB LEVEL FIXED: one multiply a sample for the DAC.

## 2. The baseline `frozen-base-6`

- `frozen-base-5` + the frozen hunk **`firmware/hal/fm1_usb.h`** = `727f272`'s (EP4: `FM1_USB_EP4_CNT`,
  `FM1_USB_EP4_TADR`, `fm1_usb_ep4_txbuf`, `fm1_usb_ep4_send`, the header comment). 1.0.1 / 1.0.2 did not change it,
  nor `usb.c`.
- Glue the reference build needs (not frozen): `felucca.c` defines `FELUCCA_UAC 1` (before `usb_app.c`), and the
  tag's `audio.c` gets upstream's two hooks (`uac_render_start()` before a half renders, `uac_tap(out, n)` after
  `mix_block`), so the frozen `usb_app.c` UAC code and `main.c`'s `#if FELUCCA_UAC` (the nested `uac_service`)
  compile alike in both builds.
- The update loader includes `hal/fm1_usb.h` but none of the new parts (defines and inline functions): it must
  stay byte-identical (sha cc98eed2…, 6493 B); the loader pin proves it.
- `tools/frozen_base.txt` names the tag with its commit; `docs/UPSTREAM_1.0.2.md` gets its rows.

## 3. Our firmware

### 3.1 USB audio on

- `firmware/src/felucca.c`: `#define FELUCCA_UAC 1` default (`#ifndef`), before `#include "usb_app.c"`;
  `tools/build.py` passes `FELUCCA_UAC` (and `FELUCCA_UAC_TONE`, the bench triangle) through from the environment
  like the other flags, so `FELUCCA_UAC=0 ./build.sh` builds MIDI-only.
- `firmware/src/audio.c`: in the render loop, `uac_render_start()` before the half's blocks; in `audio_block`,
  `uac_tap(out, n)` right after `mix_block(out, n)`, then (§3.2) the FIXED scaling, then today's scope and shift.
- The device: one composite USB device, MIDI + audio input (+ the CDC console), upstream's descriptors (bcdDevice
  x.1x for the audio input). The identity cannot be switched at run time (descriptors are frozen code).

### 3.2 USB LEVEL (MASTER / FIXED)

- `fx.c`: `fx_usb_fixed` (volatile uint8); `mix_block` scales the mix by `MASTER_FULL` (4096) instead of
  `song.master_q12` when FIXED; `usb_fixed_dac(out, n)` (noinline) scales the block, `(out * song.master_q12) >> 12`,
  after the USB tap (upstream 1.0.2's text). MASTER: exactly today's path.
- In FIXED the limiter always works on the full-level mix, so a low MASTER makes the speaker quieter without easing
  the limiting (upstream's behaviour).
- MENU item **USB LEVEL** with values MASTER / FIXED (an on/off item: KNOB 1 right = FIXED, left = MASTER, OCT+
  toggles), after SPEAKER EQ. Eight items: the row pitch goes from 24 to 20 px (checked on a screenshot).
- Saved like MUTE NEXT BAR: `settings.usbfix` (`.noinit`, clamped to 0 / 1 at init) and **bit 3 of the stored
  `zoom` word**; the settings record's format does not change, older saves read MASTER, the panel table is
  untouched.

### 3.3 Console

`status` prints upstream's `con_uac()` lines (stream state, packets, frames, underruns, overruns, missed, stalls,
the fill range), after the UART lines.

## 4. Testing (host, failing first)

- **`tests/uac_test.c`**, upstream's (727f272), on `usb_app.c` with our `HALF_FRAMES`, with and without CDC: the
  descriptors as a host parses them (lengths, interfaces, the UAC1 chain, EP 0x84 isochronous 184 B, the MIDI
  endpoints unchanged) and the ring (not fed before the host reads; renders late under load: every frame once and
  in order, packets 43..46, 44.1 on average; underrun repeats, overrun drops, restart primes again).
- **USB LEVEL** (drum suite): MASTER: the pinned kit hashes unchanged (today's sound); FIXED with MASTER at a quarter:
  the tap gets the full-level block and the DAC block is a quarter of it; the hooks do nothing (no ring writes)
  while the stream is closed.
- **MENU / settings** (ui_test, boot_test): the item steps and saves; an older save (bit 3 clear) reads MASTER;
  FIXED survives a power cycle; the other settings and the panel table unchanged.
- **Build check**: USB audio compiled in (the ring and the console's UAC string in the app).
- H2 against `frozen-base-6` (0 different), the loader pin, the stack check (the nested `uac_service` in TIMER5),
  the full suite, `DRUM_PACKAGE=1 ./build.sh`.
- **Cost**: the target budget for the audio ISR and the new functions (`uac_tap`, `uac_render_start`,
  `usb_fixed_dac`) is shown to the user, who approves any budget file change before it is written.

## 5. On the device (the user)

- The Mac lists an "FM-1" audio input, 2 channels, 44.1 kHz (Audio MIDI Setup).
- A DAW records 5 minutes of a heavy kit (8 tracks, RESON, SPRNG, the compressor): no clicks or dropouts in the
  recording; the speaker does not stutter; the CPU meter close to before.
- MIDI from the DAW still plays while it records; the next firmware install finds the FM-1 as usual.
- USB LEVEL FIXED: MASTER down, the recording level stays; MASTER mode: the recording follows the knob.

## 6. Done when

§4 passes, the firmware builds, H2 / loader / stack checks are green (budget changes approved), CHANGELOG has the
line, the user has checked §5; merged as 0.8.0 (tag `drum-v0.8.0`).

## 7. Out of scope

Playback from the computer (USB audio output), other sample rates, switching USB audio off at run time, a
recording without SPEAKER EQ (user decision), upstream's other 1.0.x MENU items.
