# BLE build: does it fit? (part 1a)

The BLE build (`FELUCCA_BLE=1 BLE_MEASURE=1 ./build.sh`) measured step by step against the app slot
(`APP_SLOT` = 581564 B). The goal (spec 2026-10-08, amendment 5): image ≤ APP_SLOT − 32768 = **548796 B** (aim:
64 KB free), the pool with ≥ 8 KB headroom, `.data + .bss` ≤ 96 KB. `BLE_MEASURE=1` links against
`build/app_ble_measure.ld` (XIP and POOL widened) so an image that does not fit yet can be measured; it never makes
a package.

| step | image B | slot free B | to the goal B | `.data+.bss` B | pool B (of 344064) |
|------|--------:|------------:|--------------:|---------------:|-------------------:|
| drum-v0.14.0, no BLE | 296948 | 284616 | — | 51048 | 298656 |
| 1: the port, as fm1-lsdj 548ce73 (48 KB heap, 4 KB task stacks) | 737680 | −156116 | −188884 | 68784 | 365920 (−21856 headroom) |
| 2: the SDK config sources and the log stubs as bitcode (LTO) | 422096 | 159468 | 126700 | 63020 | 365920 (−21856 headroom) |
| 3: RAM fit (2 KB task stacks, a 26 KB heap; Task 4), every check on (no `BLE_MEASURE`) | 422268 | 159296 | 126528 | 63028 | 335200 (8864 headroom) |
| 4: part 1b (the console, the service hook, gates 2–3, the stack and IRQ guards) | 424012 | 157552 | 124784 | 63056 | 335200 (8864 headroom) |

## Step 1: where the XIP bytes are (build/felucca.map)

| input | XIP B |
|-------|------:|
| `lto-llvm-*.o` (the BT libraries, through LTO) | 418303 |
| `felucca.o` (the core) | 293237 |
| `ble.o` (the BLE unit) | 12791 |
| `libcompiler_rt.a` (soft double: the libraries') | 2700 |
| the rest (vectors, crt0, ISR, context switch, SDK config objects) | 1319 |

Inside the LTO object, by section:

| section | B | |
|---------|--:|---|
| `.rodata.str1.*` | 78513 | strings: mostly the libraries' log text (Task 2) |
| `.classic_lmp_code`, `.classic_rf_code`, `.classic_tws_code`, `.classic_lmp_auth_*`, `.classic_rf_const` | 108231 | classic BT / TWS (Task 3) |
| `.bt_stack_code` | 56666 | host stack (GATT client, SM, L2CAP) |
| `.ble_ll_code` | 45438 | link layer |
| `.ble_rf_code` | 36356 | radio |
| `.text` | 32718 | |
| `.ble_hci_code`, `.ble_sm_code`, `.hci_controller_code`, `.ble_gatt_code` | 35274 | |

`.ram_text` (the radio calibration, in RAM): 2287 instructions, 211 calls, all inside `.ram_text`, no XIP address.

## Step 2: the config and the log stubs visible to LTO

The SDK's config sources (`lib_btctrler_config.c`, `bt_profile_config.c`, compiled against
`firmware/src/ble/sdkcfg/app_config.h`) and the libraries' log functions (now `firmware/src/ble/ble_lto_stubs.c`:
`printf`, `puts`, `putchar`, `put_buf`, `printf_buf`, `log_print`, empty; the `log_tag_const_*` of lbuf / wlc / TWS,
0) are compiled with `-flto`, as the SDK builds them, and the link passes the SDK demo's
`--plugin-opt=-dont-used-symbol-list=` with that list. The libraries were already linked through LTO; now the
optimizer also sees the `const` configuration (`config_btctler_modules` = LE only, no TWS, the log tags 0) and the
empty log functions, folds them and drops what they switch off. Nothing is stubbed by hand in this step.

XIP by input (step 1 → step 2): the libraries (LTO) 418303 → 103665 B; `felucca.o` 293237 → 293259; `ble.o`
12791 → 12772; `lib_btctrler_config.o` 371 → 0 (now inside LTO).

Inside the LTO object (B, step 1 → step 2):

| section | step 1 | step 2 | |
|---------|-------:|-------:|---|
| `.rodata` (with `.rodata.str1.*`) | 82537 | 3788 | the log text |
| `.bt_stack_code` | 56666 | 24862 | |
| `.classic_lmp_code` | 49024 | 624 | classic BR/EDR |
| `.ble_ll_code` | 45438 | 14504 | link layer: the roles and features not configured |
| `.ble_rf_code` | 36356 | 15818 | |
| `.classic_rf_code` | 33834 | 2656 | classic BR/EDR |
| `.text` | 32718 | 17646 | |
| `.classic_tws_code` | 20216 | 4 | TWS |
| `.ble_hci_code` | 12204 | 2622 | |
| `.ble_sm_code` | 9366 | 9110 | |
| `.hci_controller_code` | 7610 | 1160 | |
| `.ble_gatt_code` | 6094 | 5914 | |
| `.uECC_code` | 3110 | 0 | ECDH (LE Secure Connections / classic SSP) |
| `.bt_stack_const` | 2428 | 915 | |
| `.link_task_code` | 2204 | 0 | |
| `.bt_rf_code` | 2158 | 758 | |
| `.classic_lmp_auth_code`, `_const` | 3272 | 0 | classic BR/EDR |
| `.classic_rf_const` | 1885 | 54 | classic BR/EDR |
| `.hmac_code`, `.crypto_code`, `.crypto_bigint_code` | 3822 | 0 | |
| `.link_bulk_code` | 1478 | 312 | |
| `.hci_interface_code` | 1106 | 0 | |
| `.vendor_manager_code` | 526 | 0 | |

For the 1b device test (the ones a BLE-MIDI central might still need, if the folding were wrong): `.link_task_code`,
`.hci_interface_code`, `.uECC_code` / `.hmac_code` / `.crypto*` (pairing with LE Secure Connections: a device that
asks for it), `.vendor_manager_code`, and the link layer's 31 KB. Start, scan, connect, discover and notify each have
a console step in 1b; a failure there points here first.

Shrink step 2 of the plan (stubbing classic / TWS references by hand) is not needed: step 1 leaves 126528 B under
the goal.

## RAM (step 3)

| what | where | B |
|------|-------|--:|
| the core's buffers (as drum-v0.14.0) | `.pool` | 298656 |
| the BT heap (`BLE_HEAP_BYTES`, `ble.c`) | `.pool` | 26624 |
| 4 task slots (`ble_os.c`: a 2 KB stack, `BLE_STACK_WORDS` 512, + a 384 B message queue each) | `.pool` | 9920 |
| **pool** | of 344064 (8192 kept spare) | **335200** (8864 spare) |
| the core's `.data + .bss` (as drum-v0.14.0) | RAM | 51048 |
| the libraries' and the BLE unit's `.data + .bss` | RAM | 11980 |
| **`.data + .bss`** | of 98304 | **63028** (35276 spare) |

The heap's 26 KB is the largest round size that keeps the pool's 8 KB headroom (27296 B would leave exactly 8192).
`.bss` had more room (35276 B) but would leave the core's RAM with none; `.pool` keeps both checks with room. The
spec's budget is the measured peak + 25 %: the console's `ble` now prints `heap high <n> of 26624` (the most in use,
blocks and headers), and 1b sizes the heap from the device's number. The task stacks are 2 KB (the device measured a
1.5 KB high-water in fm1-lsdj; `ble` prints each task's unused bytes).

## Checks

- Library pins: a changed pin in a copy of `tools/ble_libs.py` → `ble_libs: cpu/wl82/liba/btctrler.a sha256 … is
  not the pinned …`, exit 1; a missing SDK (`AC79_BT_SDK=/nonexistent`) stops the build before any compile:
  `ble_libs: /nonexistent/cpu/wl82/liba/btctrler.a missing`.
- Gate 5: the default build stays byte-identical to `drum-v0.14.0` (`tools/check_default_build.sh`, in
  `tests/run_tests.sh`).
- Gate 1: every build (default and BLE) stops unless the update loader's `ota.bin` has
  `tools/frozen_base.txt`'s `LOADER_SHA256` (a changed pin through `FROZEN_BASE_FILE`: `loader: ota.bin cc98eed2… is
  not the pinned loader`, exit 1).
- The BLE build: `.ram_text` (the radio calibration) reaches nothing outside itself; no BT library RAM-code section
  lands in XIP; register access only in `hal/` (`src/ble/` scanned, except the SDK's radio tables); `divdi` 0. The
  libraries bring their own soft double (`libcompiler_rt.a`: `divdf3`, `adddf3`, `muldf3`, … 2700 B), vendor code.
- Gates 2a, 2b and 3 (part 1b) run in every BLE build, which stops on any of them:
  - 2a: the update path's sources (`usb.c`, `usb_app.c`, `ota.c`, `storage.c`, `libc.c`, `firmware/hal`,
    `firmware/loader`) equal the last `drum-v*` release's; only the new BLE HAL files may be added;
  - 2b: the update path's machine code (45 functions: the main loop, USB, OTA, flash, storage, the timer IRQ,
    `fm1_cstart`, UBOOT, the watchdog) equals the reference core's: the same core object linked with empty BLE entry
    points (`build/felucca_ref.dis`);
  - 3: no BT start-up (`ble_start`, `btstack_init`, …) is reachable from `fm1_cstart`, the ISRs or the fault handlers,
    except through `ble_user_start` (the console's `ble start`).
  Each was broken on purpose once: a comment appended to `ota.c` → `gate 2a: … ota.c differs from drum-v0.14.0 (M)`;
  a `ble_start()` call in `ble_service` → `gate 3: … ble_start, btctrler_task_init, btstack_init`; the BLE listing
  against the default build's → `changed: fm1_main`.
- While BT runs (part 1b): a task that reaches the lowest words of its 2 KB stack stops the FM-1 with `fatal 3` (the
  task named), kept over the reboot with the heap's high-water and its failed allocations (`ble`); a BT interrupt
  asked above priority 2 is attached at 2, below the audio (3) and the timer (4), and counted (`irq_lowered`).
- The SDK config sources are pinned too (`tools/ble_libs.py` `SOURCE_PINS`).
- A gated BLE build may be packaged: `FELUCCA_BLE=1 DRUM_PACKAGE=1 ./build.sh` → `build/felucca-ble-UNTESTED.fwsc`,
  and the local installer is labelled `…+ble`. `BLE_MEASURE=1` builds are never packaged.
- H2 / H3 and the cost budgets run on the default build (byte-identical to 0.14.0's, so unchanged).

## Verdict: GO for part 1b

| | |
|---|---|
| BLE image | **422180 B** (after the review's fault-path fix), the slot 581564 B: **159384 B free** (goal ≥ 32768, aim 65536: both met) |
| pool | 335200 of 344064 B (8864 spare, ≥ 8192) |
| `.data + .bss` | 63028 of 98304 B |
| heap / task stacks | 26624 B (to be sized from the device's high-water + 25 %) / 4 × 2 KB |
| default build | byte-identical to `drum-v0.14.0` |

For 1b's device test: the sections LTO dropped in step 2 (above: `.link_task_code`, `.hci_interface_code`, the
ECDH / HMAC / crypto code, `.vendor_manager_code`, most of the link layer's and the HCI's code) are where a failed
start, scan, connect, discovery or pairing looks first; `ble` prints the heap high-water, each task's unused stack,
the start-up stage and a library fault (`fatal`).
