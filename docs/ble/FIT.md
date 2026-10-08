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

## Checks

- Library pins: a changed pin in a copy of `tools/ble_libs.py` → `ble_libs: cpu/wl82/liba/btctrler.a sha256 … is
  not the pinned …`, exit 1; a missing SDK (`AC79_BT_SDK=/nonexistent`) stops the build before any compile:
  `ble_libs: /nonexistent/cpu/wl82/liba/btctrler.a missing`.
- Gate 5: the default build stays byte-identical to `drum-v0.14.0` (`tools/check_default_build.sh`).
