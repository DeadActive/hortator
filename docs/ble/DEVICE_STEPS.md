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
   change pages, save. Then type `ble`. Expected: `state OFF`, `loop last … ms, max … ms`. That is the main loop's
   period with BT off, the reference for step 4. Copy the `ble` output.
2. **Start:** `ble start`. Expected: `ble: STARTING`. Within 5 s, `ble` shows `state IDLE` and `rf trim stored`, with
   no tone, no crash and no reset. Copy the whole `ble` output.
   - If it says `NO RF TRIM IN FLASH`: this FM-1 has no stored radio calibration and BT does not start. That is the
     intended refusal. Copy `ble` and stop here.
3. **Scan and connect:**
   1. Switch the SMC-Mixer on (in its DAW mode).
   2. `ble scan`, wait 10 s, `ble list`. Expected: the SMC-Mixer is listed with `MIDI`.
   3. `ble connect N`, with its number. Expected: `ble` shows `RECEIVING` while you move knobs and faders; `msgs`
      counts up; `errs 0`.
4. **Under load:** a pattern plays through steps 2 and 3, with knobs moving on the mixer. Expected: no audible
   glitch, and `ble` shows `audio_late 0`. Copy the `ble` output, which has:
   - `loop last / max` (compare with step 1);
   - `longest BT task run`;
   - `heap high … failed allocations …`;
   - each task's `stack free`.
5. **Update path, BT on and connected:** stay connected (step 3) and ask me for the next build. I rebuild it with a
   new label. Install it through the web installer. Expected: the update completes and the FM-1 restarts on the new
   build (its ABOUT / the installer label).
6. **Way back:** hold OCT- + OCT+ for 5 s. Expected: UBOOT. Do it twice, once with BT off (after a power-on) and once
   with BT on and connected. Leave UBOOT by switching off and on.
7. **After any crash or unexpected reset:** power on, then type `ble` and `crash` and copy both. `ble` keeps its
   start-up record (`starts / stage / task / trim …`), `fatal` (1 a library assert, 2 a library reset request, 3 a
   BT task's stack overflow) and the heap figures over the reboot.

## Results

| Step | Date | Result | Notes |
| --- | --- | --- | --- |
| 1 install | | | |
| 2 start | | | |
| 3 scan / connect | | | |
| 4 under load | | | |
| 5 update, BT on | | | |
| 6 UBOOT off / on | | | |
