# Settings and menu
<!--blurb: GLOBAL, SYSTEM and the device menu: colour, backlight, speaker EQ, accel, calibration, about.-->

## GLO pages

Tap [[GLO]] for seven pages: **GLOBAL 1/7**, **SYSTEM 2/7**, **GLOBAL 3/7**, then **DLY 4/7**, **DLY 5/7**, **REVERB 6/7** and **CHORUS 7/7** (the last four are described under *Effects*).

| Page | Knob | Label | Values (default) |
| --- | --- | --- | --- |
| GLOBAL 1/7 | **KNOB 1** | BPM | 40 to 240 (120); `CLK <source>` while following an outside clock |
| GLOBAL 1/7 | **KNOB 2** | SWG | global swing 0 to 100 % (0) |
| GLOBAL 1/7 | **KNOB 3** | CLK | INT (default), USB, TRS; BLE and LINK in the Bluetooth version |
| GLOBAL 1/7 | **KNOB 4** | CH | the drum MIDI channel, 1 to 16 (10) |
| SYSTEM 2/7 | **KNOB 1** | USB | read-only status: OFF, WAIT, BUS, ENUM, MIDI |
| SYSTEM 2/7 | **KNOB 2** | SYNC | MIDI clock out: OFF (default), USB; BLE and BOTH in the Bluetooth version |
| SYSTEM 2/7 | **KNOB 3** | ROUT | MIDI-in routing: NOTE (default), SEL, TRACK |
| SYSTEM 2/7 | **KNOB 4** | CPU | read-only load, in % |
| GLOBAL 3/7 | **KNOB 1** | MUTE | NOW or BAR (NEXT BAR): when mutes land |
| GLOBAL 3/7 | **KNOB 2** | METRO | OFF, LOW, MID, HIGH |
| GLOBAL 3/7 | **KNOB 3** | THRU | OFF (default), ON |
| GLOBAL 3/7 | **KNOB 4** | QUANT | 1/16 (default), 1/8, 1/32, OFF: where live recording puts a hit (see Sequencer) |

**METRO** is a click on every quarter note while playing, the bar's first higher, on the FM-1's clock or following one. It comes after the PERFORM effects and follows MASTER, on the speaker, headphones and USB audio. METRO, THRU, MUTE, QUANT and the solo mode are device settings; SYNC and ROUT are saved with each project. See *MIDI* for how they work.

## The menu

Hold [[HOME]] (0.7 s) for the **MENU**. [[SELECT]] moves the cursor, [[KNOB 1]] changes the value (on COLOR it wraps; on SPEAKER EQ, BACKLIGHT and USB AUDIO it stops at the ends; on ON / OFF rows right is ON, left is OFF), [[OCT+]] is OK (it steps on and wraps, or toggles), [[OCT-]] goes back or closes, and holding [[HOME]] again closes it. The menu saves when it closes.

| Row | Values (default) | Notes |
| --- | --- | --- |
| **COLOR** | RED, RED INV, BLUE, BLUE INV, GREEN, GREEN INV (RED) | the screen's two colours; applies at once. RED is the default red ground and dark ink; RED INV red on black; BLUE white on blue; BLUE INV blue on white; GREEN dark on green; GREEN INV green on black |
| **SPEAKER EQ** | FLAT, LOWCUT, BASS+ (FLAT) | LOWCUT is a high-pass at about 110 Hz; BASS+ lifts about 220 Hz and adds bass harmonics for the small speaker |
| **USB LEVEL** | MASTER, FIXED (MASTER) | whether USB follows the MASTER knob |
| **USB AUDIO** | 2 CH, 10 CH MIX, 10 CH FX (2 CH) | standard build only; see *USB audio* |
| **PERFORM** | HOLD, PAGE (HOLD) | PAGE keeps the PERFORM screen up after FX is released |
| **ZOOM** | OFF, ON (OFF) | shows the touched value large in a popup |
| **ACCEL** | OFF, ON (ON) | knob acceleration |
| **BACKLIGHT** | OFF, DIM LO, DIM HI, INV, INV LO, INV HI (OFF) | key and button LED levels |
| **HARDWARE CALIBRATION** | OCT+ runs it | teach each button and knob; also reachable by holding [[OCT-]] and [[OCT+]] at power-on; times out after 30 s idle |
| **ABOUT** | OCT+ opens | the logo, version (`DRUM-1.0.1`), build date; [[SELECT]] scrolls the credits |
| **BACK** | OCT+ closes the menu | the last row |
| **BLUETOOTH** | Bluetooth version only | between HARDWARE CALIBRATION and ABOUT; the USB AUDIO row is absent there; see *Bluetooth* |

### COLOR

Six themes: each colour, then its inverse. [[KNOB 1]] steps back and on; [[OCT+]] steps on; both wrap. A device setting, saved when the menu closes. A colour chosen on an older Hortator stays the same.

### Knob acceleration (ACCEL)

With ACCEL ON, a knob turned in one direction over a wide value (more than 32 steps) speeds up with how fast it turns. One click at a time, or up to about 12 clicks per second, is one step a click (fine control). Faster, the steps grow with the speed, in fractions of a step: about x2 at 20 clicks per second, x4 at 40, up to x8 (x4 on a value of 64 steps or less). A 0..127 value takes about 65 clicks at 20 a second, 34 at 40, 24 at 60 (upstream Felucca's: 127, 65, 64). The first clicks of a turn, and the first click back after a reversal, are one step. Lists of names (MODEL, TYPE, WAVE...) are one entry a click at any speed; the PRESETS list at most x4. ACCEL OFF: one step a click. Every click counts up to about 300 a second.

### BACKLIGHT

- **OFF**: LEDs lit or dark, as before.
- **DIM LO**: every key LED that is not lit and every idle button glows at about 3 % (a very short flash about 900 times a second: steady, also with Bluetooth on) so the panel can be found in the dark. Where the white keys are the selected track's steps (STEP, PATTERN, MOTION and SONG with KEYS off; TRACKS while no track is armed) the unlit steps 1, 5, 9 and 13 of the bank shown glow at about 25 % (bank 2: 17, 21, 25, 29), marking the beats.
- **DIM HI**: the same, brighter (about 6 %, the beats about 30 %).
- **INV**: as the stock firmware does: idle LEDs lit, active ones (steps that are on, the playhead, held keys, the page's button) dark. **INV LO / INV HI**: the same with the idle ones at about 10 % / 25 %.
- In every mode [[OCT-]] / [[OCT+]] **breathe** (fade in and out) where pressing them would do something: the next / previous bank, LAYER from SOUND, the LFO pages' RATE MODE, YES in a dialog, OK in the menu, NAME's RENAME, MIDI LEARN's DONE, the PRESETS list's LOAD (and DELETE on a used user slot). A way back (NO, CANCEL, BACK, LAYER's OCT-) is lit; KEYS' octave shift is lit.
- A safe start keeps every LED dark.

### ABOUT

Credits everyone whose work is in the firmware: Felucca, Mutable Instruments, DaisySP, the samples, the recovery tool, the web fonts and icons, and the emulator the MD voices were measured from, with a disclaimer.

## CPU and PRIO

When an audio block takes more than 85 % of its time (95 % before 1.0.0, so a sudden peak no longer runs past the deadline), the FM-1 stops one sounding voice to catch up and the top bar shows a red CPU icon for 2 s. A track with **PRIO LOW** loses its voices before a **PRIO HIGH** one, the oldest first; a held ACID note goes last. The same order picks the voice to stop when too many play at once. PRIO is on the track's last EDIT (SOUND) page, after CHOKE: HIGH by default, saved in the project. SYSTEM 2/7 CPU shows the load.

## Start screen

The start screen is a **CRT collapse**: the HortatoR logo squashed through five dithered frames into one white scan line that bursts sideways, in the chosen COLOR. The boot guard: a restart inside the first 30 s counts as a failed start, and two in a row enter the built-in bootloader.
