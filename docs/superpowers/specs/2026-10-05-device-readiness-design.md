# Device readiness: the drum firmware on a real FM-1 — design

Date: 2026-10-05. Milestone: before M2 (pulls the safety part of M4 forward). Parent spec:
`2026-10-05-drum-core-m1-design.md` (§2 safety rules stay in force).

## 1. Goal and decisions

The user wants to be sure the drum firmware works on their FM-1 and that they can always go back.

User decisions:
- **No extra hardware (option B).** No XIAO RP2040 / FM-1-transporter, no Linux PC. Going back relies on
  M-VAVE's official updater (or the Felucca installer) over USB-MIDI. No flash dump is made.
- **"Sure" means:** the boot path cannot crash on a stock FM-1, and anything that fails later can be undone over
  USB. Host checks (§3) carry the first part, a device procedure the user runs (§5) the second.
- **A safe start (§4)** is added: SEQ held at power-on starts without drum audio.
- Claude still never runs anything that touches the device (`tools/fm1_install.py`, the web installer, M-VAVE's
  updater); the procedure gives the user exact commands.

## 2. Risk model

The USB update path (`usb.c`, `ota.c`, the loader) and the start-up code (`crt0.S`, `fm1_cstart`, the boot guard,
`fm1_main` up to `felucca_init()`) are frozen, byte-identical to upstream Felucca (runs on many FM-1s). The mask-ROM
bootloader cannot be erased. So no software failure is permanent; what differs is how to get back:

| Failure of the drum firmware | Result | Way back with option B |
|---|---|---|
| Crash later (a model, the sequencer, a page) | reset, starts again | reinstall stock over USB-MIDI |
| Audio overload | glitches; voices are shed | firmware still runs: reinstall |
| Crash on **every** boot before the main loop serves USB | after 2 crashed boots the boot guard enters UBOOT, which M-VAVE's updater cannot talk to | needs an RP2040 board or a Linux PC (inconvenient, not permanent) |

All host work targets the last row. Boot order (`main.c`, frozen): `persist_boot` (JEDEC check, user sample slot
scan, settings record) → `settings_init` → LCD → `fm1_input_init` / `panel_init` → **`felucca_init()`** (ours) →
`audio_init` (audio ISR starts: `mix_block`, ours) → `usb_start` → timer → IRQs on → 430 ms → main loop (our
UI code and the frozen `ota_service` in the same loop). Our code that can run before the update path answers:
`felucca_init`, the audio ISR from then on, and the main loop's UI code from its first iteration (a crash there
on every frame would also repeat on every boot).

On a stock FM-1 the Felucca data regions (user sample slots 0xA0000..0xDBFFF, settings, projects) hold M-VAVE's
bytes. Their readers (`smp_user_scan`, `st_load` / the `PER2` settings record) validate every field; upstream
Felucca installs over stock routinely.

## 3. Host checks (no device)

Each is a host program or script, run by `tests/run_tests.sh`, failing the suite on a miss.

**H1 — boot path against hostile flash** (`tests/boot_test.c`). A host model of the 1 MiB flash (the
`fl_*_ram` / XIP reads the boot path uses, served from a buffer) and the boot sequence of §2 with our real code:
`persist_boot`, `settings_init`, `panel_init`, `felucca_init`, 2 s of audio blocks (`mix_block`), 30 UI frames.
Built with `-fsanitize=address,undefined`. Flash contents:
1. erased (all 0xFF); 2. all 0x00; 3. random bytes (8 seeds); 4. valid headers (sample slot `SMP_USER_MAGIC`,
`PER2` settings, `FDR1` project) with random fields; 5. the records a Felucca install leaves (built by the host
tools that write them).
Pass: no sanitizer report, no out-of-range index, every audio block bounded, the boot reaches the point where
`ota_service` would run.

**H2 — the safety code is upstream's, in the linked binary** (`tools/compare_upstream.py`). Build upstream
Felucca 1e838e1 (a git worktree under `build/`, same toolchain, same `build.sh`), disassemble both `.elf` files,
and compare the machine code of every function in the frozen files (`hal/*`, `usb.c`, `ota.c`, `crt0.S`,
`storage.c`) and of `fm1_cstart`, with addresses and PC-relative offsets normalised. The loader (`ota.bin`)
bytes in both packages must be identical. A function inlined differently is listed by name and fails.

**H3 — worst-case stack** (`tools/stack_depth.py`). From the target disassembly: each function's frame (its
prologue's stack adjustment and pushes) and its direct calls → the deepest path of the main loop (user stack,
`_ustack_lo`..`_ustack_top`, 24,320 B) and of each interrupt (system stack, `_sstack_lo`..`_sstack_top`,
7,936 B; the audio ISR and the timer ISR nested on it). Indirect calls (the model `render` / `trigger` pointers)
resolve to every `dmodel_t` entry. Pass: each deepest path ≤ 75 % of its stack; the report also gives upstream's
figures for comparison (same tool on the H2 build). Recursion fails the check.

**H4 — the first seconds' cost.** On the host (instruction counts, as `test_cost`): (a) `felucca_init` including
the safe-start scan, which delays `usb_start` (pass: the scan is its fixed ~7 ms; the rest under 1 ms of
instructions at the device clock assumed by the target budget); (b) the audio ISR per block with no voice
sounding (the power-on kit, stopped) against upstream Felucca's idle cost measured the same way on the H2 build
(pass: ≤ upstream × 1.25, the M1 regression budget); (c) one idle UI frame against upstream's (same pass rule).

**H5 — the safe start works** (in `tests/drum_test.c` / `tests/ui_test.c`): SEQ held through
`felucca_init` → safe mode; audio blocks output silence and render no voice; the screen shows SAFE START; keys and
MIDI notes start no voice; the USB service path is untouched (the main loop is frozen). SEQ not held → normal start
(the existing tests). H1 runs both ways.

Existing checks stay: installer and update protocol against a simulated FM-1, M-UPGRADE entry, package identity
`FM-1_9xx`, `check_untouched`.

## 4. Safe start

- In `felucca_init()` (ours; before `audio_init`): run the HAL's polled scan `fm1_input_scan()` for
  `FM1_DEBOUNCE + 4` frames (~7 ms; IRQs are still off, the HAL documents this polled use) and read SEQ from
  `fm1_in.buttons` through the panel map. SEQ held → `safe_start = 1`.
- In safe mode: `mix_block` writes silence and returns before any track, model, layer, FX or slicer code;
  `drum_hit` starts no voice; user sample slots are marked unusable (`usr_nz[] = 0`; their scan in
  `persist_boot` is frozen upstream code and runs before); the UI draws one static screen ("SAFE START" and how
  to leave it: power off) and skips the pages. USB-MIDI, the editor protocol, M-UPGRADE and the UBOOT entries
  (OCT− + OCT+ 5 s, SysEx) work as always (main loop, frozen).
- It cannot bypass `persist_boot` or anything before `felucca_init` (frozen order); H1 covers those.

## 5. Device procedure (the user runs it)

`docs/DEVICE_INSTALL.md`, a checklist; stop at the first step that does not go as written.
0. Prepare the way back: download M-VAVE's official updater and the stock firmware (outside the repo); note the
   FM-1's version; charge the battery.
1. Rehearse going back: reinstall the same stock version with M-VAVE's updater.
2. Rehearse a round trip with known-good firmware: install the current upstream Felucca release (its web
   installer); check it starts and plays; back to stock with M-VAVE's updater.
3. Install the drum build: `tools/fm1_install.py build/felucca-UNTESTED.fwsc` (or the web installer with that
   file); the guide names the build's commit and its package checksum.
4. First boot, in order: "FM-1 DRUMS" boot screen; HOME and the 8 white keys play; **reinstall the same drum build
   over itself** (proves the update path from the drum firmware; on failure go back to stock at once); SEQ held at
   power-on shows SAFE START.
5. Hands-on: every page; every engine (the 21 SOUND screens); the demo-kit patterns; live recording; project save /
   load; battery and master knob — each with what to see / hear.
6. If anything is wrong: back to stock (step 1) and report what was seen. If the FM-1 is in the bootloader (blank,
   USB not answering): what that means, and that it needs an RP2040 board (FM-1-transporter) or a Linux PC
   (jl-uboot-tool), with links.

The package keeps the name `felucca-UNTESTED.fwsc` until the user has done step 4.

## 6. Done when

- H1–H5 pass in `tests/run_tests.sh`; H2's and H3's reports are in the build output.
- The firmware still builds (`DRUM_PACKAGE=1`), all host tests pass, `check_untouched` ok.
- `docs/DEVICE_INSTALL.md` written; the user reviews it before running anything.

## 7. Out of scope

A flash backup and FM-1-transporter support (option A, declined); M2 features; renaming the package.
