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

## Checks

- Library pins: a changed pin in a copy of `tools/ble_libs.py` → `ble_libs: cpu/wl82/liba/btctrler.a sha256 … is
  not the pinned …`, exit 1; a missing SDK (`AC79_BT_SDK=/nonexistent`) stops the build before any compile:
  `ble_libs: /nonexistent/cpu/wl82/liba/btctrler.a missing`.
- Gate 5: the default build stays byte-identical to `drum-v0.14.0` (`tools/check_default_build.sh`).
