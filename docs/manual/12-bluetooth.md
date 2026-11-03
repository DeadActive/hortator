# Bluetooth
<!--blurb: The Bluetooth version: BLE-MIDI controllers, the SMC-Mixer, computers and FM-Link.-->

## Overview

Each release has a second version, **Hortator + Bluetooth MIDI** (experimental). The installer offers it next to the plain one; the release has it as `hortator-X.Y.Z-bluetooth.fwsc`. Everything in the manual works the same in it, except what is listed under *What it leaves out*. Bluetooth is **off at every power-on** unless you set AUTOSTART ON; it runs only after you turn it on, until the FM-1 is switched off. The plain version never enables the radio.

### What Bluetooth adds

| Feature | What you get |
| --- | --- |
| **MIDI over Bluetooth** | A BLE-MIDI controller plays and controls the FM-1 as USB MIDI does: notes play the tracks (GLO CH and ROUT), MIDI LEARN learns its CCs and notes, CLK BLE follows its clock. The FM-1's key hits go back to it, SYNC BLE / BOTH sends it the FM-1's clock, THRU passes notes between it and USB. |
| **ROLE HOST** | The FM-1 finds and connects devices itself: up to 8 saved, a controller and another FM-1 linked at once, reconnected by themselves. |
| **ROLE DEVICE** | The FM-1 is itself a Bluetooth MIDI device, **Felucca FM-1**, for a computer, a tablet or another FM-1. |
| **SMC-Mixer** | The M-VAVE SMC-Mixer becomes the FM-1's mixer. |
| **FM-Link** | Two FM-1s in time over Bluetooth, without MIDI clock. |

### What it leaves out

- **USB AUDIO 10 CH MIX and 10 CH FX**: USB audio is always 2 CH and the menu has no USB AUDIO row. Use the plain version for 10 channels.
- **The web simulator** runs the plain version; it has no Bluetooth page.
- It is **experimental**: the audio has less headroom while Bluetooth is on.

> **Known issue.** A saved device that is not connected can make the sound stutter: while BLUETOOTH is ON the FM-1 keeps looking for every saved device it is not linked to. Remove devices you no longer use (on the Bluetooth page turn [[KNOB 1]] left on its line, `FORGET <name>?`, [[OCT+]]) or switch BLUETOOTH OFF when you don't need it.

## The Bluetooth page

Hold [[HOME]] for the menu, [[SELECT]] to **BLUETOOTH**, [[OCT+]]. [[SELECT]] moves the cursor, [[KNOB 1]] changes a row, [[OCT+]] acts on it, [[OCT-]] goes back to the menu.

| Row | What it does |
| --- | --- |
| **BLUETOOTH** | ON starts Bluetooth and connects to the saved devices; OFF drops the links and stops connecting. Not saved: off at every power-on (unless AUTOSTART is ON). |
| **AUTOSTART** | ON: Bluetooth starts by itself 5 s after every power-on. Saved when the menu closes; OFF by default. Not in a safe start (SEQ held), nor while OCT- + OCT+ are held. After a boot that was not clean (a crash in the first 30 s of the last one, or a hang) it is skipped and the top bar says `BT AUTOSTART SKIPPED`; BLUETOOTH ON by hand still works. |
| **ROLE** | **HOST**: the FM-1 connects to its saved devices. **DEVICE**: the FM-1 is a Bluetooth MIDI device that a computer, a tablet or another FM-1 connects to. Saved. |
| **MODE** | **AUTO** (SMC DAW for a device whose name starts with `SMC`, else MIDI), **SMC DAW**, **MIDI**, or **FM-LINK** (no MIDI either way, FM-Link only). Saved. |
| **RECONNECT** | How long a link rests after a drop or refused connect: **FAST** 2 s doubling to 16 s; **SLOW** 8 s then 16 s (each refused try holds the audio off for a moment); **AUTO** SLOW while playing, FAST while stopped. Saved. |
| a saved device | (ROLE HOST) A filled square when connected, empty when not; its name (an FM-1 shows as `FM-1` and the last four digits of its address); `CTRL` or `FM-1`; `ON`, `NEAR` (seen lately) or `AWAY`. [[KNOB 1]] left: `FORGET <name>?`, [[OCT+]] yes, [[OCT-]] no. [[OCT+]] on a `NEAR` one: use it now. |
| **+ ADD A DEVICE** | (ROLE HOST) [[OCT+]]: an 8 s scan (`SCANNING 8 S`) lists nearby unsaved devices, BLE-MIDI ones (`MIDI`) first, then by signal. [[OCT+]] saves one and connects it; [[OCT-]] goes back; `NOTHING FOUND`: [[OCT+]] scans again. |

Above the rows, the state: OFF, STARTING, `ON, NO DEVICE SAVED`, `ON, SEARCHING`, `ON, 2 CONNECTED`, the scan, `WAITING FOR A HOST` / `CONNECTED` and the host (ROLE DEVICE), or FAILED and the reason (`NO RF TRIM IN FLASH`: this FM-1 cannot run Bluetooth; `BT START TIMEOUT`: switch off and on to retry). Under it, the last error. `BT CONNECTED` and `BT LOST` show in the top bar on any page.

### Saved devices

Up to 8, controllers and FM-1s, kept over power-off with their names. The FM-1 holds **two links at once**: a controller and an FM-1. For each kind it connects the first saved device that is around, in the order you picked them (ADD, or OCT+ "use now", puts one first). After a lost link it reconnects by itself while BLUETOOTH is ON; a device that dropped keeps its link for 30 s (no other of its kind takes its place meanwhile). Scanning never drops a link. Each FM-1 has one Bluetooth address for good (made the first time Bluetooth starts).

## MIDI mode

Notes play the tracks as over USB and TRS (GLO CH and ROUT); MIDI LEARN learns the device's CCs and notes. GLO **CLK BLE** follows the device's MIDI clock (the tempo knobs then say `CLK BLE`). The FM-1's key hits go to the device. GLO SYNC **BLE** / **BOTH** sends it the MIDI clock. When the link drops, what the device held is released. SYNC BLE loads SYNC OFF and BOTH loads USB in the plain version. In SMC DAW mode nothing but the mixer's LEDs is sent; in FM-LINK mode no MIDI at all.

## Connecting, step by step

### A Bluetooth MIDI controller

1. Switch the controller on and make it discoverable (most BLE-MIDI controllers are, as soon as they are on).
2. On the FM-1: ROLE **HOST**, MODE **AUTO** (or **MIDI**), BLUETOOTH **ON**.
3. SELECT to **+ ADD A DEVICE**, OCT+. After the 8 s scan, SELECT to the controller (it says `MIDI`), OCT+. It is saved and connects; the top bar shows `BT CONNECTED`.
4. Play: notes on the drum channel (GLO CH, 10 at first) play the tracks whose NOTE matches; ROUT SEL plays the selected track pitched, ROUT TRACK plays tracks 1-8 from channels 1-8. To map its knobs use MIDI LEARN (hold GLO, press G4).
5. To play in time with it: GLO CLK **BLE** follows its clock; GLO SYNC **BLE** (or BOTH, USB too) sends it the FM-1's.
6. Next time it connects by itself once BLUETOOTH is ON (or set AUTOSTART ON).

### The SMC-Mixer

1. Switch the SMC-Mixer on in its **DAW** (Mackie Control) mode.
2. On the FM-1: ROLE **HOST**, MODE **AUTO** (recognised by its name) or **SMC DAW**, BLUETOOTH **ON**.
3. + ADD A DEVICE, OCT+, pick the `SMC` device, OCT+. Its LEDs light with the FM-1's state.
4. Move each fader to its track's level to pick it up (its LED blinks until then), then mix. SELECT picks the track whose sound knobs 1-4 change; knobs 5-8 are MIDI LEARN's.

### A computer or tablet (the FM-1 as a Bluetooth MIDI device)

1. On the FM-1: ROLE **DEVICE**, BLUETOOTH **ON**. The page says `WAITING FOR A HOST`.
2. Connect from the computer or tablet; no pairing is needed:
   **macOS**: Audio MIDI Setup > Window > Show MIDI Studio > the Bluetooth icon, then **Connect** next to `Felucca FM-1`. **iPad / iPhone**: in the app's Bluetooth MIDI screen (AUM, GarageBand, Logic Remote and others), tap `Felucca FM-1`. **Android / Windows**: an app that connects BLE-MIDI devices.
3. The page says `CONNECTED` and the host's name. `Felucca FM-1` is now a MIDI port in the DAW or app.
4. From the DAW: send notes on the drum channel (or use ROUT SEL / TRACK); to have the FM-1 follow the DAW's tempo and transport set GLO CLK **BLE** and turn on the DAW's MIDI clock out to `Felucca FM-1`.
5. To the DAW: the FM-1's key hits come in as notes (record them); with GLO SYNC **BLE** the DAW can follow the FM-1's clock instead (one clock direction at a time).

### Two FM-1s with FM-Link

1. On FM-1 **B**: ROLE **DEVICE**, BLUETOOTH **ON**.
2. On FM-1 **A**: ROLE **HOST**, BLUETOOTH **ON**, + ADD A DEVICE, pick `Felucca FM-1` (shown as `FM-1` and the last four digits of its address), OCT+.
3. Choose the leader and follower: the leader's GLO CLK is **INT** (or USB / TRS / BLE to follow a DAW); the follower's GLO CLK is **LINK** (after BLE). Either FM-1 can lead. The screens say `FM-LINK: LEADING` and `FM-LINK: FOLLOWING`.
4. PLAY on the leader: both start together. The follower takes the leader's tempo; STOP on the leader stops both. With the leader following a DAW, the DAW's Start and Stop drive both.
5. MODE **FM-LINK** keeps the link for FM-Link only; in MIDI or AUTO the same FM-1 can still have a MIDI controller linked.

## FM-Link in detail

No clock pulses: the two FM-1s measure the difference between their clocks and share a timeline. PLAY on the leader starts both together, about 80 ms after the press; the leader's BPM is the follower's; STOP on the leader stops both; PLAY / STOP on a linked follower say `FOLLOWING LINK`. A leader that follows a MIDI clock (CLK USB, TRS or BLE) starts both from beat 1 on the clock's Start, with its tempo, and stops both on its Stop. A follower that joins while the leader plays starts on its next bar. If the link drops the follower plays on at the last BPM; CLK LINK with no other FM-1 plays as INT. It works with FM-1s running Felucca's Bluetooth build too.

## The SMC-Mixer (DAW mode)

The M-VAVE SMC-Mixer in its DAW (Mackie Control) mode, MODE AUTO or SMC DAW. Only presses act.

| Control | Hortator |
| --- | --- |
| Fader 1-8 | track 1-8 LEVEL, with pickup; recorded as motion for the selected track while armed and playing |
| Master fader | nothing |
| Knob 1-4 | the selected track's first four knobs (TUNE DECAY TONE and the model's fourth; ACID: TUNE DECAY CUTOF RESO), one step a click; the top bar shows e.g. `T3 DECAY 64`; recorded as motion while armed and playing |
| Knob 5-8 | MIDI LEARN's (CC 20-23): learned to a parameter, a click steps it by one value; while LEARN waits for a CC, turning one learns it; not learned: nothing |
| MUTE 1-8 | mute / unmute the track, as on the MUTE screen (MUTE: NEXT BAR applies) |
| SOLO 1-8 | solo / unsolo the track |
| REC 1-8 | arm / disarm the track (no play started). During REC ALL: REC ALL ends and that track is disarmed, the others stay armed |
| SELECT 1-8 | select the track |
| PLAY | stopped: play; playing: nothing |
| STOP | stop |
| REC | as the FM-1's REC tap |
| BANK / CHANNEL left, right | the previous / next track (stops at 1 and 8) |
| REW, FF, the arrows | nothing |

- While a song plays or is armed, REC 1-8 and REC say `STOP TO EDIT`. While a dialog or NAME is on screen, the mixer's buttons and knobs do nothing.
- **LEDs:** MUTE n lit = muted (or waiting for the next bar), SOLO n = soloed, REC n = armed, SELECT n = selected, PLAY = playing, STOP = stopped, REC = any track armed. They are sent again on every connection.
- **Fader pickup:** a fader does nothing until it reaches its track's level (within 2, or passing it); until then the mixer blinks its LED. On every connection, and whenever a track's LEVEL changes some other way (knob, LOAD, INIT, LEVEL motion), that fader has to be picked up again.
- **Solo:** while any track is soloed, only the soloed tracks are heard; several can be soloed. A silenced track is dimmed on HOME and TRACKS; the mutes are unchanged. Solo clears itself when the SMC-Mixer disconnects and when MODE leaves SMC DAW (nothing is cut). Solo is not saved: INIT and a power-on clear it too.
- **Leaving SMC DAW** while connected: Hortator turns the mixer's LEDs off once and sends it nothing more.
