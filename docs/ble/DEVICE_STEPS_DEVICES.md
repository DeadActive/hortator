# Saved devices, two links, FM-Link, the device role: the device test

BLE MIDI: 2026 DEADACTIVE.

The Bluetooth build of branch `ble` after the upgrade to felucca-ble's current Bluetooth (ble-kit 109fda7):
`FELUCCA_BLE=1 DRUM_PACKAGE=1 ./build.sh`. Not yet run on an FM-1 in this form: the host tests and the build's gates
are all that has checked it. The earlier sheets ([DEVICE_STEPS.md](DEVICE_STEPS.md),
[DEVICE_STEPS_MIDI.md](DEVICE_STEPS_MIDI.md)) still hold for what they test; this one is for what changed.

The console is always on (USB serial). `tools/fm1_serial_log.py session.log` (needs `pip install pyserial`) logs every
line with the time, waits for the FM-1 across resets, asks `crash`, `ble` and `fml` each time it comes back and `ble`
every 30 s; `--period 2 --poll ble,status` for a stall around a link drop. Send the log with what you did and when.

## The steps

1. **Install over 0.23.2's Bluetooth version** (a device remembered there, MODE and AUTOSTART set). Expected: MENU >
   BLUETOOTH shows BLUETOOTH, AUTOSTART, ROLE `HOST`, MODE and RECONNECT `AUTO`, then the old device as the first saved
   device (`DEVICE` and the last four digits of its address, `CTRL`) and `+ ADD A DEVICE`; AUTOSTART and MODE as
   before. BLUETOOTH ON: once the device is seen its line shows its name (kept from then on).
2. **Way back, Bluetooth off.** Hold OCT- + OCT+ for 5 s: UPDATE MODE / UBOOT. Switch off and on.
3. **Add a device.** BLUETOOTH ON, SELECT to `+ ADD A DEVICE`, OCT+. Expected: `SCANNING 8 S` counting down, BLE-MIDI
   devices first (`MIDI`), the saved ones left out; after 8 s `ADD: PICK A DEVICE` and the list stays still. OCT+ on
   the SMC-Mixer: back on the page on its line, `SMC-MIXER CTRL`, then a filled square and `ON`; `ON, 1 CONNECTED`;
   `BT CONNECTED`; the top bar shows the Bluetooth rune (struck through while nothing is connected).
4. **The mixer.** As in DEVICE_STEPS_MIDI.md; new: knobs 5-8 with MIDI LEARN: GLO + G4 (LEARN), turn the FM-1's knob
   for a parameter, turn mixer knob 6: learned (`CC21 -> ...`); leave LEARN; knob 6 now moves that parameter a step a
   click, from where it is. While a dialog or NAME is up, the mixer's knobs and buttons do nothing.
5. **Reconnect, RECONNECT.** Switch the mixer off for 10 s and on: it comes back by itself (`AWAY`, then `ON`). With a
   second controller saved and on: the second does not take its place for 30 s, then does; the mixer back takes its
   place again. RECONNECT `SLOW` / `FAST` / `AUTO` (SLOW while playing): `ble`'s `ctrl` line shows `tries` growing more
   slowly with SLOW. Power-cycle with AUTOSTART ON: about 5 s after the start it connects by itself, names shown.
6. **FORGET and use now.** On a device line KNOB 1 left: `FORGET <name>?` on the state line; OCT- keeps it; again and
   OCT+: the line goes (a connected one drops, the next of its kind takes its place). OCT+ on a `NEAR` line: it moves
   to the top and is used now; on an `AWAY` one: `NOT AROUND`.
7. **ROLE DEVICE.** ROLE `DEVICE`: no device lines; BLUETOOTH ON: `WAITING FOR A HOST`. On a Mac (Audio MIDI Setup >
   MIDI Studio > Bluetooth) **Felucca FM-1** is listed (every FM-1 advertises that name); Connect. Expected:
   `CONNECTED` and an address; the FM-1's key hits reach the Mac on the drum channel; the Mac's notes play the tracks;
   with CLK BLE the Mac's clock runs the transport; with CLK INT and GLO SYNC `BLE` (or `BOTH`) the Mac follows the
   FM-1's clock (Start, 24 a beat, Stop); SYNC `OFF` / `USB`: no clock to the Mac. Disconnect on the Mac: `WAITING FOR A HOST` again.
8. **FM-Link** (two FM-1s with this build, or this one and a Felucca BLE build). B: ROLE DEVICE, BLUETOOTH ON, CLK
   `LINK` (GLO page: CLK, past BLE). A: ROLE HOST, `+ ADD A DEVICE`, pick `Felucca FM-1`: its line `FM-1 xxxx FM-1`,
   then `ON`. Expected: `FM-LINK: FOLLOWING` on B, `FM-LINK: LEADING` on A; `fml` on B: `fml peer 1`,
   `fml offset_ok 1`. PLAY on A: both start together (about 80 ms after the press); the same 16ths on both by ear for
   minutes; A's BPM: B follows. PLAY / STOP on B: `FOLLOWING LINK`. STOP on A stops both. A playing, B off and on and
   linked again: B starts on A's next bar. MODE `FM-LINK` on A: the mixer's MIDI is ignored, FM-Link goes on.
9. **Two links.** A: the mixer and B saved and connected, `ON, 2 CONNECTED`, both lines `ON`; the mixer works while A
   and B play in time. Open `+ ADD A DEVICE` with both connected: neither drops. Add them in the other order: the same.
10. **Under load.** A pattern plays with the mixer moving and B linked. `ble`: `audio_late 0`, `out_drops 0`,
    `in_drops 0`, `alloc fails 0`, `fatal 0`; copy `heap high` (of 23,552), `audio gap max` (5805 us nominal: one half
    of 256 samples) and `irq_off max`.
11. **Saving with Bluetooth on.** Stop; save a project and a user preset; power off and on: both there, the settings
    and the saved devices too. The Bluetooth store (flash 0x9F000 / 0xFE000) is written only while stopped.
12. **Way back, an update, Bluetooth on and two links up.** Hold OCT- + OCT+ for 5 s: UBOOT. Then install a package
    (this one again, or the plain one): it completes and the FM-1 restarts on it. The plain version keeps MODE and
    AUTOSTART (and the old remembered device) as they were; going back to the Bluetooth version finds the saved devices.

## Results

| Step | Date | Result | Notes |
| --- | --- | --- | --- |
| 1 install over 0.23.2 BT | | | |
| 2 UBOOT, BT off | | | |
| 3 add | | | |
| 4 mixer, knobs 5-8 | | | |
| 5 reconnect | | | |
| 6 forget, use now | | | |
| 7 device role | | | |
| 8 FM-Link | | | |
| 9 two links | | | |
| 10 under load | | | |
| 11 saving | | | |
| 12 UBOOT, update | | | |
