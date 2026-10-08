# BLE part 1: the device test

BLE MIDI: 2026 DEADACTIVE.

The build is `felucca-ble-UNTESTED.fwsc`, from `FELUCCA_BLE=1 DRUM_PACKAGE=1 ./build.sh` on branch `ble`. The local
web installer for it shows a label ending in `+ble`. It is Hortator 0.14.0 plus the Bluetooth stack. Bluetooth is
**off at boot**; only the console's `ble start` turns it on, until the next power-off.

## The console

The FM-1's USB serial console (CDC). On a Mac:

```sh
screen /dev/cu.usbmodem* 115200        # any baud works; leave with Ctrl-A then K, then y
```

Type `help`. The Bluetooth commands:

| Command | What it does |
| --- | --- |
| `ble start` | Starts Bluetooth: the stored radio calibration, then the stack's tasks. Reaches IDLE in a few seconds. |
| `ble scan` | Scans for 10 s. |
| `ble list` | The devices found, numbered: address, type, RSSI, `MIDI` if it offers BLE-MIDI, name. |
| `ble connect N` | Connects to device N of the list, finds BLE-MIDI, subscribes. |
| `ble stop` | Disconnects. Bluetooth stays on until power-off. |
| `ble` | The status: state, counters, `audio_late`, the radio calibration, the start-up record, `fatal`, the heap, the main-loop period, the longest BT task run, each task's free stack, the device list. |

A command in the wrong state answers with one line (for example `ble: not started (ble start)`) and does nothing
else.

## The steps

Copy the console text of each step back (the whole `ble` output where asked).

1. **Install** the BLE build over 0.14.0 with the web installer for it. Hortator works as before: play a pattern,
   change pages, save. Then type `ble`. Expected:
   - `state OFF`;
   - `heap: no record yet` after a power-on (or the last run's figures after a restart);
   - `loop last … ms, max … ms`: the main loop's period with BT off, the reference for step 5.

   Copy the `ble` output.
2. **Way back, BT off:** hold OCT- + OCT+ for 5 s. Expected: UBOOT. Leave it by switching off and on. This proves the
   way back on this build before anything riskier.
3. **Start:** `ble start`. Expected: `ble: STARTING`, and within 5 s `ble` shows `state IDLE` and `rf trim stored`,
   with no tone, no crash and no reset. Copy the whole `ble` output.
   - `NO RF TRIM IN FLASH`: this FM-1 has no stored radio calibration, so BT cannot start on it at all (a power
     cycle does not change that). Copy `ble` and stop here.
   - `state FAILED … BT START TIMEOUT`: the stack did not come up in 5 s. Copy `ble` and `crash`, then power off and
     on (BT is off again) and stop here.
4. **Scan and connect:**
   1. Switch the SMC-Mixer on (in its DAW mode).
   2. `ble scan`, wait 10 s, `ble list`. Expected: the SMC-Mixer is listed with `MIDI`.
   3. `ble connect N`, with its number. Expected: `ble` shows `RECEIVING` while you move knobs and faders; `msgs`
      counts up; `errs 0`.

   `ble stop` disconnects. It answers with the state it saw (for example `ble: RECEIVING`); `ble` a moment later
   shows `IDLE`.
5. **Under load:** a pattern plays through steps 3 and 4, with knobs moving on the mixer. Expected: no audible
   glitch, and `ble` shows `audio_late 0`. Copy the `ble` output, which has:
   - `loop last / max` (compare with step 1);
   - `longest BT task run`;
   - `heap high … failed allocations …`;
   - each task's `stack free`;
   - `irq_lowered` (expected 0).
6. **Update path, BT on and connected:** stay connected (step 4) and ask me for the next build. I rebuild it with a
   new label. Install it through the web installer. Expected: the update completes and the FM-1 restarts on the new
   build (its ABOUT / the installer label).
   - If the update stops or the FM-1 restarts midway and Hortator comes back: type `ble` and `crash`, copy both, and
     install again with BT off (BT is off after a power-on).
   - If Hortator does not come back: do not run any other tool on it; tell me what the screen shows.
7. **Way back, BT on:** `ble start`, connect (step 4), then hold OCT- + OCT+ for 5 s. Expected: UBOOT. Leave it by
   switching off and on.
8. **After any crash or unexpected reset:** power on, then type `ble` and `crash` and copy both. `ble` keeps its
   start-up record (`starts / stage / task / trim …`), `fatal` (1 a library assert, 2 a library reset request, 3 a
   BT task's stack overflow, with `task` naming it) and the heap figures over the reboot.

## Results

| Step | Date | Result | Notes |
| --- | --- | --- | --- |
| 1 install | | | |
| 2 UBOOT, BT off | | | |
| 3 start | | | |
| 4 scan / connect | | | |
| 5 under load | | | |
| 6 update, BT on | | | |
| 7 UBOOT, BT on | | | |
