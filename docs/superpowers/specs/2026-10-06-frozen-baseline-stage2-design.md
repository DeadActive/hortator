# Stage 2: upstream fixes inside the frozen files — fork baseline tags — design

Date: 2026-10-06. Parent: `docs/UPSTREAM_1.0.2.md` §2 (the user chose to take upstream Felucca 1.0–1.0.2's fixes
inside the frozen files, including USB-MIDI) and `2026-10-05-device-readiness-design.md` (H1–H4, the frozen
files, "never brick the FM-1"). This spec covers the baseline mechanism and **step 1** (panel timing,
keys / encoders); later steps (USB-MIDI, storage, the `st_erase` order, TRS MIDI on) reuse the mechanism, each
with its own short design.

## 1. Goal and user decisions

- Bring upstream's fixes for bugs our firmware shares, inside the files we keep frozen, without weakening the
  protection of the update and boot path.
- User decisions: **all** stage-2 fixes; **step 1 = the LED flicker / lost knob steps (TIMER5 nesting) and the
  key debounce / encoder learning**; the baseline moves by **fork baseline tags** (approach A).

## 2. The baseline mechanism

- `frozen-base-2` is a git tag on a commit made from 1e838e1 that changes **only frozen files**, by exactly the
  adopted upstream hunks, applied as upstream wrote them (nothing of the fork's). Its commit message cites the
  upstream commits and lists each hunk. Later steps add `frozen-base-3`, `-4`, … on top of the previous tag.
- One place names the baseline: `tools/frozen_base.txt` (a line `FROZEN_BASE <tag>`). `tools/check_untouched.py`
  (source text) and `tools/upstream_build.sh` / `tools/compare_upstream.py` (H2, the compiled frozen code) read it
  instead of the hard-coded 1e838e1, and print the baseline they used. `tools/stack_depth.py`'s upstream reference
  is the same build.
- The fork's frozen files are those of the tag (copied from it in the step's implementation commit), so the
  source check stays "byte for byte" (with its existing exceptions: `felucca_init()`, the boot titles, the
  `ST_MAGIC` line, the boot-guard tail of `core.h`).
- **The update loader is pinned:** a new check compares `build/loader/ota.bin` with the shipped loader
  (sha256 `cc98eed2…`, 6493 B, recorded in `tools/frozen_base.txt` as `LOADER_SHA256`). A step whose hunks would
  change the loader fails this check; moving the pin is the user's decision (like the cost budgets).
- `docs/UPSTREAM_1.0.2.md` gets a table of every frozen-file change: baseline tag, upstream commit, file, hunk,
  why.

## 3. Step 1: panel timing, keys and encoders (`frozen-base-2`)

- `firmware/hal/fm1_input.h` becomes upstream 1.0's file (727f272): key presses count after 2 frames
  (~1.1–2.2 ms), releases after 8 (~9 ms), per column as they are read; the encoder learns one detent state only
  (no double counting after a knob held mid-click, no dead knob after short mid-click pauses, #23) and recovers a
  lost transition; the `fm1_in_stat` diagnostics. Not 1.0.1's LED glow (`fm1_led_dim`, its 595 shift timing) nor
  1.0.2's glow levels.
- `firmware/src/main.c`, upstream 727f272's hunks for:
  - `fm1_timer5_irq`: the input tick and the ms count run on every tick, also while the audio ISR renders (TIMER5
    nested: only the scan and the ms, nothing the audio ISR touches); the USB and UART polls are owed and run at
    the first tick after the render; the time spent nested is added to `t5_nested_ticks`.
  - `timer5_start`: priority 4 (above ALNK0's 3).
  - the boot: `felucca_dbg.in_audio = 0` (a reset inside the audio ISR left it set in `.noinit`).
  Nothing else of upstream's `main.c` (its UI, crash screen, `felucca_init`, USB audio): `uac_service` stays
  behind `FELUCCA_UAC`, which this firmware never defines.
- `firmware/src/audio.c` (not frozen): upstream's `t5_nested_ticks`, subtracted from the measured render time
  (the CPU meter and the voice shedding see the render only) and cleared per half.
- The loader includes none of these files (`loader.c`: `fm1_time.h`, `fm1_sys.h`, `fm1_flash.h`, `libc.c`,
  `usb.c`, `ota.c`, `ldr_core.c`), so it stays byte-identical.

## 4. Safety and cost

- Nesting the scan in the audio ISR deepens the worst interrupt stack: `tools/stack_depth.py` must show it within
  its limit (75 % of the interrupt stack); if not, Claude stops and tells the user.
- The audio ISR's static cost (`tests/target_budget.txt`) is unchanged by the HAL; any budget change is the user's
  decision.
- The update path (`ota.c`, `usb.c`, the loader) is untouched in step 1; the loader pin proves it.

## 5. Testing

- Upstream's `tests/input_test.c` (727f272), adapted to our harness: clean and bouncy presses of every key and
  button (one press, one release, latency ≤ 2.3 ms, release ≤ 10 ms), a 150 µs glitch plays nothing, fast repeats
  all heard, an encoder detent is one step; plus encoder checks for #23 (a knob held mid-click, then turned: one
  step per detent; short mid-click pauses: the knob keeps counting) if upstream's test has them, else written here.
- `check_untouched.py` against `frozen-base-2`; H2 against the `frozen-base-2` build; the loader pin; the stack
  check; the full suite (`tests/run_tests.sh`).
- On the device (the user): no LED flicker with a dense pattern playing; knobs neither skip nor double; pads
  respond as fast or faster.

## 6. Done when

All of §5 passes, the firmware builds (`DRUM_PACKAGE=1`), and the user has checked LEDs, knobs and pads on the
FM-1.

## 7. Out of scope (this step)

USB-MIDI (`usb.c`: the loader would change; needs the app / loader split, its own step), storage hardening, the
`st_erase` order, TRS MIDI on, the LED glow, USB audio.
