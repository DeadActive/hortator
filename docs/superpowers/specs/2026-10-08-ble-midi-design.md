# Bluetooth MIDI in Hortator: design

Date: 2026-10-08. Branch `ble` (from `main` = drum 0.14.0).
BLE MIDI work: 2026 DEADACTIVE.

## Purpose

The FM-1 can already be a Bluetooth LE MIDI central. The BLE MIDI test firmware (repository `fm1-lsdj`, `main` at
548ce73) proved it on the device:
- scan;
- connect to an M-VAVE SMC-Mixer;
- receive BLE-MIDI;
- show it on screen and forward it to USB MIDI.

Its record is in `docs/ble/DEVICE_STEPS.md` and `docs/ble/LINK_NOTES.md`.

This design brings that into Hortator so that a Bluetooth controller can play and control the drum machine, with
the SMC-Mixer as the first-class controller.

## What the user decided

- **Full integration**, not just a guide.
- **Bluetooth MIDI is a third MIDI input** beside USB and TRS: notes trigger drums, and BLE is selectable as the
  clock source.
- **CC control**:
  - MIDI learn (any controller);
  - a built-in **SMC-Mixer layout** for its DAW (Mackie Control) mode, with lit buttons. The mapping is taken from
    the user's `smc-seq` project, checked against a real unit.
- **Start:** Bluetooth stays **off at boot**. The user turns it on from a menu page, and it then reconnects by
  itself to the last device used, which is remembered with the settings. Scanning and picking are only needed for
  a new device.

## Hard rules (unchanged, they apply to every part)

1. **The update path always stays reachable** (the 2026-10-08 brick):
   - the update loader stays byte-identical, its hash pinned in the build;
   - the update entry (M-UPGRADE), the OCT- + OCT+ 5 s UBOOT key and the SysEx UBOOT request keep working;
   - nothing Bluetooth runs at boot;
   - every BLE build is gated (see part 1).
2. **No overclock and no experimental compiler flags.** The core stays `-Os`.
3. **Integer / fixed point only** in project code. A 64-bit divide only by a 32-bit divisor. (The JieLi radio
   library's soft-float maths at BT start is vendor code; the user has been told about it.)
4. **No flash writes from Bluetooth code.** The stock config store is read only. Hortator's own settings are saved
   by Hortator's storage, as now.
5. **Performance budgets and thresholds are the user's:** the numbers below marked *(budget)* are proposals for
   the user to approve with this spec.
6. **The firmware, the repository and the site contain no LSDj material** (the parked LSDj project); the BLE work
   contains none.

## The parts

The work splits into five parts. Each has its own plan, device test and release.

| Part | What | Depends on |
| --- | --- | --- |
| 1 | **Fit and gate:** the BLE stack inside Hortator, shrunk to fit, with Hortator's own gates; a console test interface | — |
| 2 | **BLE MIDI in:** source 3; the Bluetooth page (on/off, scan, pick, remembered device, auto-reconnect on enable); BLE clock source | 1 |
| 3 | **BLE MIDI out:** Hortator sends to the connected device (needed for the SMC LEDs) | 2 |
| 4 | **MIDI learn:** bind a CC or a pitch-bend fader to a parameter, from any MIDI input; saved with the settings | 2 (USB/TRS work without BT) |
| 5 | **SMC-Mixer layout:** DAW mode controls and LEDs | 3, 4 |

This spec details **part 1** fully. Parts 2 to 5 are fixed here at the level the user decided. Each gets its own
short design round (screens, exact controls) before its plan.

## Part 1: fit and gate

### Where it stands

Measured, to be re-measured on 0.14.0 as the first task:

| Item | Size |
| --- | --- |
| Hortator app | about 297 KB |
| App slot | 581,564 B |
| BLE stack (test E image 509,768 B, minus the stripped core's 66,320 B) | about 443 KB |

That is **about 160 KB over the slot.** Of the stack:
- about 103 KB is classic BR/EDR, TWS and LMP code that BLE-only never runs;
- about 77 KB is the libraries' log strings, printed by stubs that print nothing.

**RAM.** Hortator's pool uses about 272 KB of 344 KB, and the build requires 8 KB to stay free. The BLE test build
used:
- a 48 KB heap;
- 4 tasks with 4 KB stacks each;
- about 15 KB of library `.data` + `.bss`.

### Goal

- **Flash:** a Hortator build with the BLE stack linked fits the slot with **at least 32 KB free** *(budget)*. The
  aim is 64 KB.
- **RAM:**
  - the pool keeps the existing 8 KB headroom check;
  - the BLE heap is sized from its **measured peak plus 25 %** *(budget)*;
  - `.data` + `.bss` stays within the 96 KB region.
- **Default build:** the default Hortator build (no BLE) stays **byte-identical** to the same commit without this
  work.

### How the stack is shrunk

In order, each step measured, and kept only if the device test (below) still passes:

1. **Configuration that LTO can see.** Compile the SDK configuration sources (`lib_btctrler_config.c`,
   `bt_profile_config.c`) and the port layer's no-op stubs as LLVM bitcode, so the link-time optimiser sees the
   BLE-only constants and the empty log functions. The classic, TWS and LMP paths then become dead code, and the
   calls to the log stubs and their strings go with them. Expected: most of the 180 KB.
2. **Classic, TWS and profile objects.** If the libraries still pull classic or TWS objects in through tables:
   - give the exact references stubs in the port layer, checked against the library IR as in the test firmware;
   - drop the zero-size pool symbols' users.
3. **Optimisation level.** The libraries' LTO code generation is at `-Os` (as the SDK's) unless a measured gain
   from another level is shown to the user first.

Not used, because each touches the update path or the installer:
- moving code into the plain flash area (the update loader only writes the app slot);
- a second app image;
- compressing the image.

### What moves into the Hortator repository

From `fm1-lsdj` `main` 548ce73, adapted to Hortator's tree.

**Module, `firmware/src/ble/`.** Its own compilation unit, `ble.c`, reaching the core only through
`ble_core.h` / `firmware/src/core_ble_api.c`. It contains:
- `ble_os.c`: the cooperative OS for the JieLi libraries;
- `ble_port.c`: libc pieces, IRQs, clocks, RAM delays, `__wrap_memcpy`, the trim hooks;
- `ble_central.c`: the state machine, the SDK glue, and the monitor text;
- `ble_midi.c`, `ble_scan.c`, `ble_vm.c`;
- `ble_sdk.h` and the generated `ble_sdk_abi.h`;
- `sdkcfg/app_config.h`, `ble_rf_tables.c`;
- `ble_stub.c` for the reference build.

The test firmware's screens (`ble_ui.c`) are not brought over: part 2 draws Hortator's page.

**HAL:**
- `fm1_ble_hal.h`, `fm1_ctx.h` / `.S`;
- `fm1_guard_stack_window` (`fm1_guard.h`);
- `fm1_irq_unmask` and `fm1_irq_is_masked` (`fm1_irq.h`).

**Tools:**
- `tools/ble_libs.py` (pinned SDK libraries, member extraction, the ABI generator);
- `tools/check_update_path.py`;
- `tools/check_ble_boot.py`;
- the BLE parts of `tools/build.py`:
  - flags;
  - the gold-plugin link;
  - `--wrap=memcpy`;
  - long calls for `ble.c`;
  - the `.ram_text` reach check;
  - the in-build gates;
- `firmware/app_ble.ld`, made from Hortator's `app.ld`.

**Tests:** the host tests `tests/ble/*.c` (OS, parser, scan, central, VM record) join `tests/run_tests.sh`.

### What the device work taught (kept as is)

These are the reasons behind code that may look odd. Do not undo them:
- **Stack window:** the BT task stacks live in `.pool`, so `ble_start` widens the core's stack-limit window.
  `request_irq` unlocks the vector table around its write.
- **Calibration:** the radio calibration (`wf_rf_trim`) is never run. It crashes from XIP and hangs from RAM on
  this core's clock setup. The stock firmware's stored calibration (config store item 187, flash 0xE5000..0xFBFFF,
  read only) is used instead. Hortator's storage never touches that range (it uses 0x97000..0xDFFFF and 0xFC000..).
- **No record:** with no stored record, BT does not start, and the user sees why.
- **RAM rule:** `.ram_text` code calls only `.ram_text`. A build check proves it on every BLE build.
- **Startup task:** `btstack_init` runs in its own `app_core` task.
- **Interrupt guard:** a task that hands back with interrupts off gets them turned back on.

### Gates (every BLE build stops on any of them)

1. **Loader pin.** The update loader's sha256 is pinned in `tools/build.py`. Hortator has no pin yet; the first
   task adds one, from 0.14.0's loader.
2. **Update path.**
   - **2a:** the update path's sources equal Hortator's last release tag (`drum-v0.14.0`, then each later release
     tag).
   - **2b:** the BLE build's core machine code for the update path equals the reference core. The reference is
     `FELUCCA_CORE_REF=1`: the same core unit, linked with empty BLE entry points. Gate 2b covers:
     - the main loop (`fm1_main`: the update session, the UBOOT key, the SysEx UBOOT request);
     - USB, OTA, flash, storage and the timer IRQ;
     - `fm1_cstart`, `enter_uboot` and the watchdog feed.
3. **No BT at boot.** No BT start-up function is reachable from `fm1_cstart` or the ISRs except through the user's
   triggers (part 1: the console's `ble start`; part 2: the Bluetooth page).
4. **RAM code.** `.ram_text` reaches nothing outside RAM.
5. **Default build.** The default build stays byte-identical. Checked with `cmp` against the build of the same
   commit without `FELUCCA_BLE`.

### Part 1's test interface

There is no screen in part 1: the console (USB CDC) drives the stack. Every command is typed by the user:

| Command | What it does |
| --- | --- |
| `ble start` | Start BT. Stored calibration, the app_core task. |
| `ble scan` | 10 s scan. |
| `ble list` | The devices: address, RSSI, MIDI tag, name. |
| `ble connect N` | Connect to device N, find BLE-MIDI, subscribe. |
| `ble stop` | Disconnect. |
| `ble` | Status: state, counters, stack and heap high-water, start-up stage, `irq_leaks`, the crash record. |

Received MIDI is counted and parsed, not yet played.

### Hortator while BT runs

Hortator renders audio all the time, unlike the test firmware.
- **Main-loop slice:** the BT stack runs in the main loop with a slice of at most **5 ms per pass** *(budget)*,
  instead of the test firmware's 50 ms. The tasks are cooperative; the slice is checked between task runs, and the
  longest single run is measured and reported.
- **Audio:** must not suffer. While scanning and while connected to the SMC-Mixer with knobs moving, `status`
  shows `audio_late 0`, and the render budgets (`tests/target_budget.py`) are unchanged.
- **UI:** stays responsive. The main-loop period is measured with BT off and on, and reported.
- **Interrupts:** the BT interrupt handlers stay at priority 2, below the audio and timer IRQs.

### Device test for part 1

Run by the user, from the console:
1. **Install.** Install the BLE build over 0.14.0. Hortator works as before. With BT off, nothing differs.
2. **Start.** `ble start`: the state reaches IDLE, with no tone, no crash and no reset. `ble` shows
   `rf trim stored`.
3. **Scan and connect.** `ble scan` then `ble list`: the SMC-Mixer is listed with MIDI. `ble connect N`: the state
   reaches RECEIVING as knobs move. `msgs` counts up, `errs 0`.
4. **Under load.** Play a pattern during steps 2 and 3: no audible glitch, `audio_late 0`.
5. **Update path.** Install the next build from this one through the web installer, which proves the update path
   in a BLE build.
6. **Way back.** OCT- + OCT+ 5 s gives UBOOT, with BT off and with BT on.

Results go to `docs/ble/DEVICE_STEPS.md` in this repository.

## Parts 2 to 5: what is decided now

**Part 2: BLE MIDI in and the Bluetooth page.**
- BLE-MIDI events enter Hortator's MIDI input queue as **source 3**, next to 1 = USB and 2 = TRS. They behave as
  today:
  - note-ons on the drum channel trigger the tracks whose NOTE matches;
  - real-time messages count only from the chosen clock source (`G_CLOCK` gains **BLE**).
- **A Bluetooth page:**
  - BT on/off;
  - the state;
  - scan, and pick from the list;
  - the remembered device: its address, plus its name for display.
- **Turning BT on reconnects to the remembered device** when it is seen in a scan. It never starts at boot.
- **Saving:** the remembered device and the on/off choice are saved with Hortator's settings. Settings writes are
  Hortator's storage, not BT code. The off-at-boot rule holds even when the saved choice is "on": the page shows
  the choice, and the user turns it on.

**Part 3: BLE MIDI out.**
- Write-without-response to the connected device's BLE-MIDI characteristic, with BLE-MIDI packet framing and
  timestamps.
- A small queue, which drops when full and counts the drops.

**Part 4: MIDI learn.**
- Learn binds a CC number (with its channel) or a channel's pitch bend to one Hortator parameter. The parameter is
  the one being edited when learn is pressed; the next CC or bend received is bound to it.
- At most 32 bindings, saved with the settings. A binding can be cleared, and so can all of them.
- Bindings apply to every MIDI input (USB, TRS, BLE).

**Part 5: the SMC-Mixer layout** (DAW / Mackie Control mode, from `smc-seq`'s checked mapping):

| Control | MIDI |
| --- | --- |
| Faders 1–8 | 14-bit pitch bend on channels 1–8 |
| Knobs 1–8 | relative CCs 16–23, sign-magnitude |
| Button rows, channel 1 notes | MUTE 0x10–0x17, SOLO 0x08–0x0F, REC 0x00–0x07, SELECT 0x18–0x1F |
| Transport | REW 0x5B, FF 0x5C, PAUSE 0x5D, PLAY 0x5E, REC 0x5F |
| Navigation | bank 0x2E / 0x2F, channel 0x30 / 0x31, arrows 0x60–0x63 |
| LEDs (sent to the mixer) | the same note, velocity 127 (on) or 0 (off) |
| Fader pickup position (sent to the mixer) | `E0+ch, v, v`; the mixer blinks a fader's LED until the physical fader reaches it |

What each control does in Hortator (for example fader n = track n level; the button rows = mute, solo, step
keys…) is part 5's design round. So is how the layout is chosen: automatically when the connected device names
itself SMC-Mixer, or by a setting.

## Licensing

The JieLi BT libraries linked into the image come from the AC79 SDK (Apache-2.0), like the SDK files the package
already carries. `LICENSING.md` gets a row for them, and `LICENSES/Apache-2.0.txt` already covers them. The
`ble_rf_tables.c` copy is Apache-2.0, credited in the file. The SMC mapping values come from the user's own
`smc-seq` project.
