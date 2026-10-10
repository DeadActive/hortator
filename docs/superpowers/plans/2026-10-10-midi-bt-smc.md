# Bluetooth MIDI: scan / connect page, MIDI in / out, SMC-Mixer DAW mode — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** a Bluetooth build of Hortator in which the FM-1 scans for and connects to BLE-MIDI devices from a menu page,
remembers and reconnects to one, takes its MIDI as a third input, sends MIDI back, and drives the M-VAVE SMC-Mixer in
DAW mode (faders = track levels, knobs 1–4 = the selected voice's first four knobs, MUTE / SOLO / REC / SELECT rows,
PLAY / STOP / REC, track ◀ ▶, LEDs, fader pickup).

**Architecture:** the BLE unit (`firmware/src/ble/`, its own object) gains the out queue, the BLE-MIDI packer, the
auto-connect step and a small API for the page; the core unit gains the page (`bt_page.c`), the SMC layout
(`smc.c`), the MIDI-mode route into the existing input ring (source 3), solo, and the settings bits. All new core code
is under `#if FELUCCA_BLE` except solo (§7) and the settings round-trip (§8).

**Tech stack:** C (C99, pi32v2 clang in Docker for the target, host `cc` for tests), Python build tools.

**Spec:** `docs/superpowers/specs/2026-10-10-midi-bt-smc-design.md` (read it first; it is the authority). Background:
`docs/superpowers/specs/2026-10-08-ble-midi-design.md` (the BLE hard rules and gates), `docs/ble/DEVICE_STEPS.md`
(part 1's device record).

**Plan form (ruling, 2026-10-10):** the user asked Claude to approve its own work ("auto approve youself") and wants a
working firmware. This plan gives each task its exact interfaces, behaviour and test cases, and leaves the code to
the implementer (each task is reviewed); it does not carry full code listings.

## Global constraints

- Hard rules (BLE spec): nothing Bluetooth runs at boot; the only way to `ble_start` is `ble_user_start` (gate 3:
  `tools/check_ble_boot.py`); the update path's sources and machine code unchanged (gates 2a / 2b); the loader pin
  (gate 1); `.ram_text` reaches only RAM (gate 4); no flash writes from BT code; integer / fixed point only; a 64-bit
  divide only by a 32-bit divisor (`grep -c divdi build/felucca.dis` = 0).
- **Gate 5:** the default build is byte-identical with and without the BT files (`tools/check_default_build.sh`).
  Core code that only the BLE build needs goes under `#if FELUCCA_BLE`. New BT files go into the script's `BT` list.
- **Frozen files** change only via upstream hunks or recorded glue: `firmware/hal/`, `firmware/loader/`, `ota.c`,
  `usb.c`, `usb_app.c`, `crt0.S`, `app.ld`, `storage.c` (except ST_MAGIC), `main.c` (except `felucca_init()` and the
  boot titles / `draw_boot_title();`), and `core.h`'s last 5 lines. `tools/check_untouched.py` checks.
- The BLE unit reaches the core only through `ble/ble_core.h` (defined in `core_ble_api.c`); it shares no static data
  with the core.
- Builds: `export PYTHON=$PWD/.venv/bin/python PATH="$PWD/.venv/bin:$PATH"`; default `DRUM_PACKAGE=1 ./build.sh`;
  BLE `FELUCCA_BLE=1 DRUM_PACKAGE=1 ./build.sh`. Retry a build up to 5× when it fails with Docker's
  "exec format error". `tests/run_tests.sh` needs a default build in `build/` (it refuses a BLE build).
- Never run `tools/fm1_install.py`, the web installer, the M-VAVE updater or `tools/rescue/*`; never install anything
  on the device; never install mido / python-rtmidi. Temporary files in `/Users/evech/.claude/jobs/5fa7de86/tmp`.
- Credit lines on new files: `/* SPDX-License-Identifier: GPL-3.0-only` + `BLE MIDI: 2026 DEADACTIVE` +
  `Drum machine fork: 2026 DEADACTIVE`.
- Prefer existing structs over new small globals. Don't wrap test runs in `sh -c`.
- Commits: explicit `git add <paths>` (never `-A`, never the `.venv` symlink); message trailer
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Comments in the code base's style: short, factual, what and why, no narration.

## Review focus

1. A BLE packet burst (faders moved fast) arriving while the UI blocks for a flash save — the 64-entry ring in
   `ble_central.c` must count losses, never corrupt state; the core must never call into the BLE unit from an ISR.
2. The MIDI input ring shared with the TIMER5 ISR (USB, TRS): the main-loop enqueue must be atomic against it and
   leave the 8 slots TRS needs.
3. Reconnect loops: a remembered device that is never found, or that refuses the connection, must not scan back to
   back forever without the 2 s pause, and BLUETOOTH OFF must stop everything at once.
4. Pickup and LEDs across a disconnect and reconnect: the full refresh and the unpicked state on every new link.
5. The settings record: an old record (no BT bits) must load exactly as before; lowcut / zoom keep their meaning.

---

### Task 1: BLE MIDI out (queue, packer, write without response)

**Files:**
- Modify: `firmware/src/ble/ble_midi.c`, `ble_midi.h` (the packer), `ble_central.c` (the queue, the flush in the
  service, `ble` console counters), `ble.c` (the flush call), `ble_sdk.h` (`ble_op_att_get_remain`),
  `ble/ble_core.h` (`ble_midi_send`), `ble/ble_stub.c` (stub for the reference core), `tools/ble_libs.py` (ABI:
  `ATT_OP_WRITE_WITHOUT_RESPOND`, `BLE_CMD_ATT_VAILD_LEN`), `firmware/src/ble/ble_sdk_abi.h` (regenerated).
- Test: `tests/ble/ble_midi_test.c`, `tests/ble/ble_central_test.c`.

**Interfaces:**
- Produces (ble_midi.h): `uint32_t ble_midi_pack(const uint8_t (*msg)[3], const uint8_t *len, uint32_t n,
  uint32_t ts_ms, uint8_t *out, uint32_t max, uint32_t *taken);` — packs as many whole messages as fit in `max`
  bytes (header, then per message a timestamp byte and its 1..3 bytes); returns the packet length, `*taken` = the
  messages packed (0 when none fits; a packet always holds at least one message when `max >= 5`).
- Produces (ble_core.h, BLE unit, called by the core): `int ble_midi_send(const uint8_t *m, uint32_t n);` — one MIDI
  message of 1..3 bytes; 1 = queued, 0 = dropped (not connected: SUBSCRIBED / RECEIVING; or the queue's 64 entries
  full: `out_drops` counts).
- Internal: the service pass flushes: while connected, the queue non-empty and `ble_op_att_get_remain` ≥ the next
  packet's length, pack up to 20 bytes and `ble_op_att_send_data(midi_handle, buf, len,
  BLE_SDK_ATT_OP_WRITE_WITHOUT_RESPOND)`; `out` counts messages sent. On a disconnect the queue empties.

- [ ] **Step 1: write the failing tests.** `ble_midi_test.c`: one note-on packs to `80|ts>>7, 80|ts&7F, 90 3C 7F`
  (5 bytes); 5 three-byte messages with max 20 → 4 taken, 17 bytes; a 2-byte message (`C0 05`); ts wraps at 13 bits;
  max < 5 → 0 taken. `ble_central_test.c`: `ble_midi_send` refused while not connected; 64 queued then the 65th
  dropped and counted; a disconnect empties the queue.
- [ ] **Step 2:** run `tests/ble` the way `tests/run_tests.sh` builds them; expect the new tests to fail (undefined
  functions).
- [ ] **Step 3: implement.** ABI generator entries, then the packer, queue and flush. Add `out` / `out_drops` to the
  console `ble` line.
- [ ] **Step 4:** host tests pass; `FELUCCA_BLE=1 ./build.sh` passes every gate; `FELUCCA_BLE=1 FELUCCA_CORE_REF=1`
  as the build needs; then `DRUM_PACKAGE=1 ./build.sh && tests/run_tests.sh` (default build) green, gate 5 ok.
- [ ] **Step 5: commit** `BLE MIDI out: a 64-message queue, BLE-MIDI packets (MMA 1.0 timestamps), write without
  response to the MIDI characteristic`.

### Task 2: auto-connect and the page's API (BLE unit)

**Files:**
- Modify: `firmware/src/ble/ble_central.c`, `ble.c`, `ble/ble_core.h`, `ble/ble_stub.c`,
  `firmware/src/core_ble_api.c` (the two new core functions: empty bodies here, filled in task 3).
- Test: `tests/ble/ble_central_test.c`.

**Interfaces (ble_core.h; exact names):**
- BLE state numbers for the core: `enum { BLE_ST_OFF, BLE_ST_STARTING, BLE_ST_IDLE, BLE_ST_SCANNING,
  BLE_ST_CONNECTING, BLE_ST_DISCOVERING, BLE_ST_SUBSCRIBED, BLE_ST_RECEIVING, BLE_ST_FAILED };` (= `ble_central.c`'s
  order; a `_Static_assert` there ties them).
- `typedef struct { uint8_t addr[6], type, midi; int8_t rssi; char name[24]; } ble_ui_dev_t;`
- Produced by the BLE unit, called by the core (main loop only):
  - `uint32_t ble_ui_state(void);`
  - `const char *ble_ui_err(uint32_t *code);` — the last error text ("" none), its code.
  - `uint32_t ble_ui_scan_left(void);` — SCANNING: whole seconds left (rounded up), else 0.
  - `uint32_t ble_ui_devs(void);` and `int ble_ui_dev(uint32_t i, ble_ui_dev_t *out);` — the sorted scan list
    (`ble_scan_sorted`'s order), 0 = no such entry.
  - `void ble_ui_scan(void);` — a manual 10 s scan: disconnects first when connected (the scan starts when IDLE is
    reached); sets the pause.
  - `void ble_ui_target(const uint8_t addr[6], uint32_t type);` — the remembered device; all-zero = none; a
    different device than the connected one disconnects it.
  - `void ble_ui_want(int on);` — reconnecting wanted; 0 also disconnects and cancels a scan / connect in progress.
  - `void ble_ui_pause(int on);` — the page's pause of reconnecting (0 ends it).
  - `int ble_ui_conn_name(char out[24]);` — connected (SUBSCRIBED / RECEIVING): 1 and the device's scanned name ("" if
    it advertised none); else 0.
- Produced by the core (core_ble_api.c), called by the BLE unit from `ble_service`:
  - `void core_ble_midi_in(uint32_t pkt);` — each received USB-MIDI packet (cable 0), in order (replaces part 1's
    drain loop that only counted).
  - `void core_ble_link(int up);` — called once when the state enters SUBSCRIBED from below, and once when it leaves
    SUBSCRIBED / RECEIVING.
- Pure, host-tested (ble_central.c): `uint32_t ble_auto_step(const ble_auto_t *a, uint32_t state, int target_listed,
  uint32_t now_ms);` returning one of `BA_NONE, BA_SCAN, BA_CONNECT, BA_STOP_SCAN, BA_DISCONNECT`, with `ble_auto_t`
  holding want, paused, the target (have, addr, type), whether the running scan is manual, the scan-after-disconnect
  request, and `rest_ms` (the time the last scan ended / the link dropped). Rules (spec §3): no want → disconnect if
  connected, else nothing; IDLE + want + target listed → connect; IDLE + want + not listed + 2 s since `rest_ms` +
  not paused → scan (auto); SCANNING (auto) + target listed → stop scan (the next pass connects); a manual scan is
  never stopped early; paused → no auto scan and no auto connect (a pick ends the pause: `ble_ui_target` clears it).
  The service applies the action (`ble_scan`, `ble_connect` by the target's sorted index, `ble_op_scan_enable2(0, 0)`
  then the state to IDLE, `ble_disconnect`).

- [ ] **Step 1: failing tests** for `ble_auto_step`: want off while RECEIVING → DISCONNECT; IDLE, target listed →
  CONNECT; IDLE, not listed, 1.9 s after rest → NONE, 2.0 s → SCAN; auto SCANNING + listed → STOP_SCAN; manual
  SCANNING + listed → NONE; paused + IDLE + listed → NONE; no target → NONE; FAILED / OFF / STARTING → NONE. And for the
  link callback: entering SUBSCRIBED calls `core_ble_link(1)` once, RECEIVING after it no second call, the disconnect
  calls `core_ble_link(0)` once (a host stub counts).
- [ ] **Step 2:** run; fail as expected.
- [ ] **Step 3: implement**; part 1's console commands keep working (the console `ble connect N` is independent of
  the target and want).
- [ ] **Step 4:** host tests; BLE build with every gate (gate 3 must still show `only through ble_user_start`);
  default build + `tests/run_tests.sh` + gate 5.
- [ ] **Step 5: commit** `BLE: reconnect to the remembered device (auto scan, connect when seen, 2 s rest), the
  Bluetooth page's API, received MIDI handed to the core`.

### Task 3: core: MIDI mode (source 3, CLK BLE), solo, the settings bits

**Files:**
- Modify: `firmware/src/core_ble_api.c` (`core_ble_midi_in` in MIDI mode, `core_ble_link` stub for task 4),
  `params.c` (`N_CLOCK` gains "BLE" under `FELUCCA_BLE`), `ui_input.c` (the `CLK …` messages from a table),
  `project.c` (G_CLOCK above the build's range loads as 0; the settings bits in `persist_boot` / `settings_save`),
  `panel.c` (`settings` fields + their sanitising in `settings_init`), `core.h` (`song.solo`, not in the last 5
  lines), `drum_core.c` / `fx.c` (`trk_silent`), `ui_draw.c` (the mixer displays dim a silent track), `ui.c`
  (`init_all` clears solo).
- Test: `tests/ui_test.c` or a new `tests/bt_core_test.c` added to `tests/run_drum_tests.sh` (built like `ui_test`),
  `tests/drum_test.c` for the engine part.

**Interfaces:**
- `static int trk_silent(const track_t *t);` — `t->p[P_MUTE] || (song.solo && !((song.solo >> trk_index(t)) & 1u))`.
- `static void track_solo_toggle(uint32_t k);` — flips bit k of `song.solo`; tracks that become silent get their
  `panic_req` bit (as a mute does).
- `settings` gains `btmode` (0 AUTO, 1 SMC, 2 MIDI), `bt_type`, `bt_lo` (address bytes 0..2), `bt_hi` (3..5), packed
  per spec §8 (`zoom` bits 5..6 mode, bit 7 type, bits 8..31 = bt_lo; `lowcut` bits 8..31 = bt_hi).
- `static uint32_t bt_mode_now(void);` (under FELUCCA_BLE) — 1 = SMC DAW, 0 = MIDI: `btmode` SMC / MIDI, or AUTO by
  the connected name's `SMC` prefix (`ble_ui_conn_name`).
- `core_ble_midi_in(pkt)`: MIDI mode → the USB filter (channel voice with matching CIN and data < 0x80; realtime F8
  FA FB FC) → `midi_enqueue(pkt, 3u)` with interrupts off, only while more than 8 ring slots are free (else counted
  in a drop counter shown by `ble`… via a core function or kept in the core's debug struct); SMC mode → `smc_in(pkt)`
  (task 4; until then a no-op under `#if 0`-free code: an empty static function is fine).

- [ ] **Step 1: failing tests:** a BLE note-on on the drum channel in MIDI mode lands in the ring with source 3 and
  fires the track (midi_block); a note on another channel does not; a BLE F8 with CLK BLE pulses the clock, with CLK
  USB it does not; the ring with only 8 free slots refuses the BLE packet without setting the overflow flag; solo:
  with track 2 soloed track 1's hit is silent (no voice) and track 2 plays; unsolo → both; a soloed + muted track is
  silent; the golden kit hashes unchanged (`run_drum_tests.sh`); settings: a record without bits loads as before
  (lowcut 2 stays BASS+); a record with an address round-trips (save → load) and lowcut keeps its 0..2 value;
  `settings_init` masks the new fields; a project with G_CLOCK 3 loads as 0 in a build whose CLK range ends at 2.
- [ ] **Step 2:** run; fail as expected.
- [ ] **Step 3: implement.**
- [ ] **Step 4:** host suites; default build + `tests/run_tests.sh` (H2, H3, budgets: report any cost change of
  `mix_part` / the hit path in the task report) + gate 5; BLE build with all gates.
- [ ] **Step 5: commit** `MIDI: BLE as input 3 (notes, CLK BLE), solo (song.solo, trk_silent), the remembered
  device and MODE in the settings' spare bits`.

### Task 4: the SMC-Mixer DAW layout (core)

**Files:**
- Create: `firmware/src/smc.c` (included by `felucca.c` under `#if FELUCCA_BLE`, after the UI files it calls).
- Modify: `felucca.c` (the include; `ed_service` calls `smc_frame()` after `ble_service()`), `core_ble_api.c`
  (`core_ble_link` → `smc_link`, `core_ble_midi_in` SMC branch → `smc_in`), `tools/check_default_build.sh` (`BT` list
  gains `firmware/src/smc.c`).
- Test: a new `tests/smc_test.c` (host, ui harness, `FELUCCA_BLE` with stubs for the `ble_*` functions that record
  the sent messages), added to `tests/run_drum_tests.sh`.

**Interfaces:**
- `static void smc_in(uint32_t pkt);` — decode + act (spec §5 table). Pitch bend ch 1..8: value = `(d2 << 7 | d1) >> 7`.
  CC 16..19 on ch 1: delta = `v & 0x40 ? -(v & 0x3F) : v & 0x3F` (0 = nothing). Notes on ch 1, velocity > 0 = press.
- `static void smc_link(int up);` — up: every LED unknown (send all), every fader unpicked, positions to send; also
  `ui_message("BT CONNECTED")` / `"BT LOST"`.
- `static void smc_frame(void);` — when connected and in SMC mode: send LED changes and pending fader positions
  through `ble_midi_send`; a message counts as sent only when `ble_midi_send` returns 1 (else retried next call).
  Also notices LEVEL changes not made by the fader (a per-track copy of the last level the fader set / position sent)
  → unpick and resend that fader's position.
- Knob k (0..3): `d = track_desc(TSEL, P_E0 + k)`; `v = param_turn(d, TSEL->p[P_E0 + k], delta)`; store;
  `motion_capture(TSEL, P_E0 + k)`; top-bar message `T<n> <label> <value><unit>` from `param_format`. A model
  without that knob (empty label or `-`) does nothing.
- Fader n: pickup per spec §5 (within 2 or crossed); once picked, `trk[n].p[P_LEVEL] = v` and
  `motion_capture(&trk[n], P_LEVEL)`.
- Buttons: MUTE → `track_mute_toggle(n)`; SOLO → `track_solo_toggle(n)`; REC n → as `tracks_rec_tap` for track n
  (refactor `tracks_rec_tap` into `track_rec_toggle(uint32_t k)` used by both); SELECT → `track_select(n)`; PLAY
  while stopped → as the FM-1's PLAY when not on SONG (`transport_req = 1`); STOP → when playing or a chain busy
  `transport_req = 2`; REC → `track_rec_toggle(song.sel)`; ◀ / ▶ → `track_select(song.sel ∓ 1)` clamped.
- LED frame: MUTE n = `trk[n].p[P_MUTE]` xor its pending bit in `song.mute_q` (lit = will be / is muted); SOLO n,
  REC n, SELECT n, PLAY = `song.playing`, STOP = `!song.playing`, REC = `song.rec != 0`.

- [ ] **Step 1: failing tests** (`smc_test.c`): fader 3 at 100 moves LEVEL only after pickup (far → no change; within
  2 → picked; crossing → picked; then follows, motion captured when armed); a LEVEL change by the FM-1 unpicks and
  queues `E2 v v`; knob 1 +3 on a KICK track raises TUNE by 3 steps, knob 2 −1 lowers DECAY, an ACID track's knob 3 =
  CUTOF; knob 5 does nothing; MUTE 4 toggles P_MUTE (and with MUTE: NEXT BAR queues `mute_q`); SOLO 2 sets bit 1;
  REC 5 arms track 5 and requests play when stopped; SELECT 7 selects; ◀ at track 1 stays; PLAY / STOP set
  `transport_req`; REC toggles the selected track; note-offs and velocity 0 do nothing; LEDs: after `smc_link(1)` one
  frame sends all 32 + 3 LEDs and 8 positions (count them), a mute then sends exactly one LED change; a refused send
  (stub returns 0) is retried; `smc_link(0)` then `(1)` resends everything; MIDI mode does not call `smc_in`.
- [ ] **Step 2:** run; fail as expected.
- [ ] **Step 3: implement.**
- [ ] **Step 4:** host suites; BLE build with every gate; default build + `tests/run_tests.sh` + gate 5.
- [ ] **Step 5: commit** `SMC-Mixer DAW mode: faders = track levels (pickup), knobs 1-4 = the voice's first four,
  MUTE / SOLO / REC / SELECT, PLAY / STOP / REC, track < >, LEDs`.

### Task 5: the Bluetooth page (core)

**Files:**
- Create: `firmware/src/bt_page.c` (under `#if FELUCCA_BLE`, included after `ui_menu.c`'s needs are declared).
- Modify: `ui_menu.c` (BLE build: `MI_BT` row "BLUETOOTH" before ABOUT; the rows 16 px apart so ten fit above the
  hints; `ui.menu == 3` = the page: `draw_menu` / `menu_input` hand over), `felucca.c` (include),
  `tools/check_default_build.sh` (`BT` list gains `bt_page.c`), `tests/ui_test.c` or `smc_test.c` (screens).
- Test: host tests + screenshots `build/ui_shots/bt/*.png`.

**Behaviour (spec §2):** rows BLUETOOTH, MODE, DEVICE, SCAN, then up to 8 devices; PRESETS moves (wraps), KNOB 1
changes the row value (BLUETOOTH right ON / left OFF; MODE steps AUTO SMC DAW MIDI, saved; DEVICE left FORGET), OCT+
acts (BLUETOOTH toggles; SCAN scans; a device: remember (settings saved, `ble_ui_target`, want on) and connect), OCT-
back to the menu (the pause ends: `ble_ui_pause(0)`). BLUETOOTH ON = `ble_ui_target(saved)`, `ble_ui_want(1)`, then
`ble_user_start()` when the state is OFF. A FAILED state shows its reason and BLUETOOTH ON says `POWER OFF AND ON TO
RETRY`. The page redraws when its signature (state, scan seconds left, the list's count and RSSIs, the connected
name, the cursor, MODE, the saved device) changes. Safe start: the row is shown but BLUETOOTH ON says `NOT IN SAFE
START`.

- [ ] **Step 1: failing tests:** the menu (BLE build) lists BLUETOOTH and opens the page with OCT+; BLUETOOTH ON calls
  target / want / `ble_user_start` once (stubs count); OFF calls want 0; MODE steps and saves; a device row's OCT+
  stores its address and type in `settings` (round-trip through `settings_save` / `persist_boot` in the host flash
  stub if available) and calls `ble_ui_target`; FORGET clears it; SCAN calls `ble_ui_scan` and the pause; OCT- ends
  the pause. Screens (PPM→PNG as the other ui shots): off, scanning with 3 devices, connected (`CONNECTED
  SMC-Mixer`), failed (`NO RF TRIM IN FLASH`), the menu with the BLUETOOTH row.
- [ ] **Step 2:** run; fail as expected.
- [ ] **Step 3: implement.**
- [ ] **Step 4:** host suites + screenshots looked at; BLE build with every gate; default build + `tests/run_tests.sh`
  + gate 5 (the default menu unchanged: its screenshot identical to before).
- [ ] **Step 5: commit** `Bluetooth page: on / off, MODE, the remembered device, scan, pick and connect`.

### Task 6: docs, the device steps, the packages and the installer

**Files:**
- Create: `docs/ble/DEVICE_STEPS_MIDI.md` (spec §11, with a Results table).
- Modify: `docs/MANUAL.md` or the manual the site uses (a Bluetooth section: the page, MODE, the SMC table), the
  CHANGELOG's unreleased section, `console.c`'s `help` line for `ble` (no longer "the console only").
- [ ] **Step 1:** write the docs from the code as built (every control in the SMC table checked against `smc.c`).
- [ ] **Step 2:** `DRUM_BUNDLE=1 DRUM_PACKAGE=1 ./build.sh` (both packages, every gate), then `tests/run_tests.sh`
  green; record the image sizes, `fm1_alnk0_irq` / RAM figures and `divdi` 0 in the task report.
- [ ] **Step 3: commit** `MIDI docs: the Bluetooth page and the SMC-Mixer layout; device steps`.
