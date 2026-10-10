# Bluetooth MIDI: the device test (the page, the SMC-Mixer DAW mode, MIDI mode)

BLE MIDI: 2026 DEADACTIVE.

The build is the Bluetooth package `felucca-ble-*.fwsc` from `DRUM_BUNDLE=1 DRUM_PACKAGE=1 ./build.sh` on branch
`midi`. Its local web installer (`build/site`) offers two versions; this test uses the second, **Hortator + Bluetooth
MIDI (experimental)**, whose label ends in `+ble`. It is Hortator 0.15.0 with ACID, plus Bluetooth: the page
MENU → BLUETOOTH, MIDI in from a BLE-MIDI device, and the M-VAVE SMC-Mixer in its DAW mode. Bluetooth is **off at
every power-on**; only BLUETOOTH ON on the page turns it on, until the next power-off.

## The page

MENU (hold HOME), PRESETS to **BLUETOOTH**, OCT+. On the page: PRESETS moves the cursor, KNOB 1 changes the row,
OCT+ acts on it, OCT- goes back to the menu (HOME held closes the menu).

| Row | KNOB 1 | OCT+ |
| --- | --- | --- |
| BLUETOOTH | right ON, left OFF | ON / OFF |
| MODE | AUTO, SMC DAW, MIDI (stops at the ends) | the next one (wraps) |
| DEVICE | left: FORGET (the remembered device; a link drops) | - |
| SCAN | - | a 10 s scan (Bluetooth ON only; dim while OFF) |
| 1 .. 8 (the devices found, BLE-MIDI ones first) | - | remember it and connect to it |

Above the rows: the state line (OFF, STARTING, IDLE, SCANNING n S, CONNECTING, CONNECTED + the name, FAILED + the
reason) and, under it, the last error (for example `DISCONNECTED 0x08`). The head shows `BT CONNECTED` / `BT LOST`.

## The console

The FM-1's USB serial console (CDC), for the counters only (use the page, not `ble start`, for this test):

```sh
screen /dev/cu.usbmodem* 115200        # any baud works; leave with Ctrl-A then K, then y
```

`ble` prints the state and the counters: `msgs` (MIDI received), `errs`, `lost`, `out` (MIDI sent to the device: the
mixer's LEDs and fader positions), `out_drops`, `in_drops` (MIDI mode: messages the input queue refused),
`audio_late`, the heap, the main-loop period, each task's free stack. `crash` prints the last crash record.

## The steps

Copy the console text where asked. After a crash or an unexpected reset at any step, power on, type `ble` and
`crash`, and copy both: they keep the start-up record (`starts / stage / task / trim …`), `fatal` (1 a library
assert, 2 a library reset request, 3 a BT task's stack overflow) and the crash over the reboot.

1. **Install** the Bluetooth package with the local web installer: choose **Hortator + Bluetooth MIDI**, Install.
   Expected: Done, the FM-1 restarts on the `+ble` label (ABOUT / the installer). Hortator works as before: play a
   pattern, change pages, save. MENU shows **BLUETOOTH** above ABOUT. `ble` shows `state OFF`.
2. **Way back, BT off:** hold OCT- + OCT+ for 5 s. Expected: UBOOT. Leave it by switching off and on.
3. **Bluetooth on:** MENU → BLUETOOTH. Expected: the state line `OFF`, BLUETOOTH `OFF`, MODE `AUTO`, DEVICE `NONE`
   (or the device of an earlier test), SCAN dim. KNOB 1 right on BLUETOOTH. Expected: `ON`, the state `STARTING`,
   then `IDLE` within 5 s; no tone, no crash, no reset.
   - `FAILED NO RF TRIM IN FLASH` or `FAILED BT START TIMEOUT`: copy `ble` and `crash`, power off and on, and stop
     here.
4. **Scan and pick:** switch the SMC-Mixer on in its DAW mode. PRESETS to SCAN, OCT+. Expected: `SCANNING 10 S`
   counting down; the SMC-Mixer is listed first, with `MIDI` and its signal. PRESETS to it, OCT+. Expected:
   `CONNECTING`, then `CONNECTED SMC-Mixer` and `BT CONNECTED` in the head; DEVICE shows `SMC-Mixer`. On the mixer:
   the selected track's SELECT lit, STOP lit, MUTE lit for any muted track, SOLO and REC dark; the fader LEDs blink
   where a fader is away from its track's level. Hold HOME to close the menu.
5. **Faders** (REC opens TRACKS: the levels): move fader 1. Expected: nothing changes, its LED blinks, until the
   fader comes within 2 of track 1's LEVEL or passes it; from then the LED is steady and LEVEL follows the fader.
   Then change track 1's LEVEL on the FM-1 (TRACKS). Expected: fader 1's LED blinks again; the fader does nothing
   until it reaches the new level, then follows. Faders 2–8 the same for tracks 2–8; the master fader does nothing.
6. **Knobs 1–4:** select a drum track (e.g. SELECT 1), EDIT. Turn knob 1 one click right. Expected: TUNE one step
   up; the top bar shows e.g. `T1 TUNE …`. Knobs 2, 3, 4: DECAY, TONE and the model's fourth knob, one step a
   click, stopping at the ends. On an ACID track: TUNE DECAY CUTOF RESO. Knobs 5–8 do nothing.
7. **The track rows:**
   - MUTE n: track n mutes / unmutes as the TRACKS key does, and MUTE n's LED shows it. With MUTE: NEXT BAR (TRACKS),
     the LED changes at once and the track at the next bar.
   - SOLO n: only the soloed tracks are heard (several can be); SOLO n again unsolos. TRACKS and HOME dim the
     silenced tracks; SOLO n's LED is lit while soloed.
   - REC n: arms track n (REC n lit, the FM-1's REC LED lit); while stopped it also starts play. Again: disarms.
   - SELECT n: selects track n on the FM-1; SELECT n's LED moves to it.
8. **Transport and track ◀ ▶:**
   - PLAY while stopped: plays (PLAY lit, STOP dark). PLAY while playing: nothing.
   - STOP while playing: stops (STOP lit, PLAY dark).
   - REC: arms / disarms the selected track (REC lit while any track is armed).
   - BANK ◀ / CHANNEL ◀ and BANK ▶ / CHANNEL ▶: the previous / next track; they stop at tracks 1 and 8. REW, FF and
     the arrows do nothing.
   - With a song playing (SONG), REC n and REC show `STOP TO EDIT` and do nothing, as REC on TRACKS.
9. **Record:** stopped, SELECT 2, then REC 2. Expected: the transport starts, track 2 armed. Play track 2 on the
   FM-1's keys as for live record now. Expected: the hits are written to track 2's steps. Turn knob 2 a few clicks,
   and move fader 2 once it is picked up: both recorded as motion (SEQ → MOTION shows it). REC 2 again disarms;
   STOP.
10. **The SMC off and on:** with the transport playing, switch the SMC-Mixer off. Expected: `BT LOST` in the top
    bar, Hortator plays on. Switch it on again (DAW mode). Expected: within about 15 s `BT CONNECTED`; the LEDs are
    right again and every fader is picked up again (step 5).
11. **Under load:** a pattern playing, move several faders fast and turn the knobs for 20 s, then type `ble`.
    Expected: no audible glitch; `audio_late 0`, `out` rising from one `ble` to the next while the faders move,
    `in_drops 0`, `errs 0`, `out_drops 0`. Copy the whole `ble` output.
12. **Power cycle:** switch the FM-1 off and on (Bluetooth is off), the SMC-Mixer on. MENU → BLUETOOTH: DEVICE shows
    the remembered device (its address until it is seen). KNOB 1 right on BLUETOOTH. Expected: it reconnects by
    itself, with no SCAN or pick: `CONNECTED SMC-Mixer` within about 15 s. MODE kept what it was set to before.
13. **MIDI mode** (optional, with a BLE-MIDI keyboard or app): MODE `MIDI` (AUTO picks MIDI for any device whose
    name does not start with `SMC`), SCAN, pick the keyboard. Expected: notes on the drum channel (GLO: CH, 10 by
    default) play the tracks whose NOTE (EDIT) matches, as USB and TRS do. GLO: CLK `BLE` with a device that sends
    MIDI clock: the tempo follows it, Start / Stop drive the transport, the BPM knob says `CLK BLE`. Without a
    keyboard, the SMC-Mixer works too: MODE `MIDI`, GLO CH 1, a track's NOTE 16; MUTE 1 (note 16) then plays that
    track, and Hortator sends the mixer no LEDs or fader positions. Afterwards: CH and NOTE back, MODE back to AUTO
    (saved when the menu closes), and, after a keyboard, SCAN and pick the SMC-Mixer again so it is the remembered
    device for the next steps.
14. **Way back, BT on:** connected (step 4 or 12), hold OCT- + OCT+ for 5 s. Expected: UBOOT. Leave it by switching
    off and on.
15. **Update path, BT on and connected:** BLUETOOTH ON (it reconnects, step 12) and ask me for the next build; I rebuild it with a new
    label. Install it with the web installer (Hortator + Bluetooth MIDI) while connected. Expected: the update
    completes and the FM-1 restarts on the new label.
    - If the update stops or the FM-1 restarts midway and Hortator comes back: type `ble` and `crash`, copy both,
      and install again with BT off (BT is off after a power-on).
    - If Hortator does not come back: do not run any other tool on it; tell me what the screen shows.
16. **FORGET:** MENU → BLUETOOTH, ON, connected; PRESETS to DEVICE, KNOB 1 left. Expected: DEVICE `NONE`, the link
    drops (`BT LOST`), no reconnect. A power cycle and BLUETOOTH ON stay `IDLE` until a device is picked again.

## On a failure

Copy back: the step, what the screen showed (a photo of the page helps), `ble` and `crash`. For a reconnect that
never happens: the state line and the error line under it.

## Results

| Step | Date | Result | Notes |
| --- | --- | --- | --- |
| 1 install | | | |
| 2 UBOOT, BT off | | | |
| 3 Bluetooth on | | | |
| 4 scan / pick | | | |
| 5 faders, pickup | | | |
| 6 knobs 1–4 | | | |
| 7 MUTE / SOLO / REC / SELECT | | | |
| 8 PLAY / STOP / REC, ◀ ▶ | | | |
| 9 record | | | |
| 10 SMC off / on | | | |
| 11 under load (`ble`) | | | |
| 12 power cycle, reconnect | | | |
| 13 MIDI mode (optional) | | | |
| 14 UBOOT, BT on | | | |
| 15 update, BT on | | | |
| 16 FORGET | | | |
