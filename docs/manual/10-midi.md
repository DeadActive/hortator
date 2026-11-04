# MIDI
<!--blurb: MIDI in over USB and TRS, clock in and out, routing, THRU and MIDI LEARN.-->

## Connections

- **USB MIDI** appears as **Hortator** (earlier versions: Felucca) in a DAW, AUM or Audio MIDI Setup. While an update is being written it appears as **Felucca Update**.
- **TRS jack**: MIDI in only. The FM-1's TRS jack is an input, so there is no TRS out.
- **Bluetooth MIDI** in the Bluetooth version (see that section).

## MIDI settings pages

| Setting | Where |
| --- | --- |
| **CH** (drum channel, 10 by default) | GLOBAL 1/7 |
| **CLK** (INT / USB / TRS, BLE, LINK) | GLOBAL 1/7 |
| **SYNC** (clock out) | SYSTEM 2/7 |
| **ROUT** (note routing) | SYSTEM 2/7 |
| **MUTE** (NOW / NEXT BAR) | GLOBAL 3/7 |
| **METRO** | GLOBAL 3/7 |
| **THRU** | GLOBAL 3/7 |
| **QUANT** (live-record grid) | GLOBAL 3/7 |

SYNC and ROUT are saved with each project; projects saved before them load SYNC OFF and ROUT NOTE. THRU is a setting of the FM-1 itself, kept over power-off, OFF until you turn it on.

## ROUT: how MIDI notes in play the tracks

Applies to notes from USB, the TRS jack and, in MIDI mode, Bluetooth.

| ROUT | Notes |
| --- | --- |
| **NOTE** (default) | On the drum channel (CH), each note plays the tracks whose NOTE it is, at their tuning. |
| **SEL** | On the drum channel, every note plays the selected track, pitched: C4 (note 60) is its tuning, a semitone per note. |
| **TRACK** | MIDI channels 1 to 8 play tracks 1 to 8, pitched from C4 as SEL (CH plays no part; channels 9 to 16 do nothing). |

Note-offs are ignored (a hit is a hit; an ACID note ends after its GATE). A note into an armed track while playing is recorded, with its pitch on SEL and TRACK. The white keys send their notes to USB: on the drum channel with the track's NOTE, and with ROUT TRACK on the track's own channel at C4 (so a recording in a DAW plays back the same tracks).

The power-on kit notes are: kick 36, snare 38, clap 39, closed hat 42, open hat 46, tom 45, rim 37, cymbal 49.

## CLK: following a clock

CLK chooses where the tempo comes from: **INT** (the FM-1's own), **USB**, **TRS** (or **BLE** in the Bluetooth version, and **LINK** for FM-Link). While following, the tempo knobs say `CLK USB` / `CLK TRS` / `CLK BLE`. Following **CLK USB**, a USB reconnect pauses the clock (`CLK USB PAUSED`); the steps wait up to 2 s for it to come back. A project saved with CLK BLE or CLK LINK loads with CLK INT in the plain version.

## SYNC: MIDI clock out

Set SYNC to OFF or USB (and BLE or BOTH in the Bluetooth version).

- **On its own clock (CLK INT)**: PLAY sends Start, then 24 clocks per quarter note; STOP sends Stop. PLAY always starts from the top, so it never sends Continue or a song position. The clocks keep to the sequencer's grid through tempo changes and never swing, whatever SWING says. Each clock is sent when the step it goes with is heard; if the receiving DAW plays slightly early, give its sync a few ms of delay.
- **Following a clock (CLK USB / TRS / BLE)**: incoming Clock, Start, Continue and Stop go on to the SYNC outputs as they arrive, never back out of the port they came in on.
- **Timing**: within a quarter of a millisecond with a light kit; a busy kit adds up to half the render time (about 1.5 ms when the CPU shows 50 %). Bluetooth MIDI is much looser: use it for tempo, not tight sync.
- If the computer does not read the FM-1's MIDI port, the clock waits no more than 8 messages deep. During a firmware update (and 3 s after) nothing is sent.

## THRU

GLOBAL 3/7 **THRU ON** passes notes and CCs from each MIDI input to the other outputs (the TRS jack to USB; Bluetooth and USB to each other in the Bluetooth version), never back to the input.

## MIDI LEARN

Up to **32** controls of any controller. A learned CC moves one knob of one track; a CC or a note presses PLAY, REC, next / previous track, or holds a PERFORM effect.

| Learn | How to pick it (LEARN on) | What it takes | What it does |
| --- | --- | --- | --- |
| a knob of a track | turn the knob (it edits as usual) | a CC | moves that knob of that track, 0..127 over its range |
| PLAY | press PLAY (nothing plays) | a CC or a note | a press = a PLAY tap: play / stop (on SONG: the song) |
| REC | press REC (nothing arms) | a CC or a note | a press = a REC tap: TRACKS arms the selected track, elsewhere REC ALL |
| NEXT TRACK / PREV TRACK | turn ALGORITHM right / left | a CC or a note | selects the next / previous track |
| a PERFORM effect | hold FX, press its black key | a CC or a note | held while the controller's button is held (FX need not be held) |
| a track's REPEAT STEP | hold FX, press its white key F3..F4 | a CC or a note | the track repeats its step while held, as the key (a control learned to a PERFORM mute in an older version does this now) |
| a PERFORM macro | on the PERFORM screen, turn KNOB 1..4 | a CC | FILTER (0 at 64: left LPF, right HPF), CRUSH, THROW, DEPTH; stays on |

### How to learn a control

1. **Turn it on:** hold [[GLO]] (the MUTE screen) and press **G4**: `G4 LEARN: ON`, and `MIDI LEARN ON` in the top bar. Let go of GLO; the footer's first row shows `LEARN: TURN OR PRESS`.
2. **Pick a knob:** turn a knob of the selected track on any page: HOME, EDIT (SOUND, LAYER), FX (FILTER, the sends, INSERT and SRR, SLICER, RESON), LFO, PATTERN, or LEVEL / LEN / PAN on TRACKS. It edits as usual and the footer says `T3 DECAY: SEND A CC`.
3. **Or pick an action:** while LEARN is on, PLAY, REC, ALGORITHM and the PERFORM keys pick their action instead of doing their job. The footer says `PLAY: SEND CC/NOTE`, `FX R1/16: SEND CC/NOTE`, `FX RPT T3: SEND CC/NOTE`.
4. **Move the controller's knob, or press its button or pad.** The first CC (or, for an action, note) that arrives on any channel is learned: `CC74 -> T3 DECAY`, `NOTE36 -> PLAY`, `CC20 -> FX FILTER`. Then pick the next one. An already learned one shows `T3 DECAY = CC74`; sending another replaces it.
5. **Finish:** [[OCT+]] ends LEARN (`LEARN DONE`), as GLO + G4 does.

### Rules

- **The binding is to that track:** CC74 moves track 3's DECAY whichever track is selected later.
- **One control, one thing:** learning again moves it to the new pick. `LEARN FULL` after 32. CC 0 and 32 (bank select) and 120 to 127 (channel mode) are never learned (`CC NOT LEARNABLE`). MODEL and RESON's MODEL cannot be learned (`NOT LEARNABLE`).
- **While LEARN is on, [[OCT-]] clears** the pick's CC or note (`T3 DECAY CC CLEARED`, `PLAY CLEARED`); with nothing picked it asks `CLEAR ALL LEARNED?` ([[OCT+]] yes, [[OCT-]] no). So pick the page first (LAYER, the step bank, a DEST slot), then turn LEARN on. The menu, a confirm, or NAME also end it. EDIT held does nothing during LEARN. It is always off at power-on.
- **What a CC does on a knob:** 0 to 127 spans the knob's whole range; a centred knob (PAN, an LFO's DEPTH, FILTER ENV) is 0 at 64; a list (FILTER TYPE, DIV, WAVE) steps through it in order. A model's knob follows the track's model. An LFO on the same knob keeps moving it around the new value. During a song, PATTERN's knobs stay as they are. CC moves are not recorded as motion.
- **Buttons:** a CC of 64 or more is a press, under 64 a release (only the change counts); a note on is a press, its note off (or velocity 0) the release. A learned note plays no drum. Learned names read like `T3 I.DRIVE`, `T3 SRR`, `T3 L1.RATE`, `T3 L.SLOT`, `T3 R.TUNE`.
- Learned CCs and notes act whether LEARN is on or off, on every input except Bluetooth in SMC DAW mode. PLAY, REC and track buttons act where the FM-1's own would (not in the menu or a confirm).
- They are saved in the FM-1's flash (**not in projects**) after each change, when stopped. The 16 CCs of the first MIDI LEARN load as they were.
