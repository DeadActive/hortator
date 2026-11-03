# Troubleshooting
<!--blurb: Symptoms, causes and fixes; every message the screen can show.-->

## Common problems

| Symptom | Cause and fix |
| --- | --- |
| No sound from a track | The track is muted (MUTE screen: hold [[GLO]]; or PERFORM mute), a solo is active from the SMC-Mixer, LEVEL is `MUTE`/0, its pattern is empty, or its PRIO LOW voice was shed. A sample model with an empty USR slot or a KEY with no zone is silent. |
| A mute does not land | MUTE is NEXT BAR: it blinks and lands on the next downbeat. |
| REC did not start playing | By design: [[REC]] arms only; [[PLAY]] starts the transport. |
| `STOP TO EDIT` / `STOP TO LOAD` / `STOP TO SAVE` / `STOP TO PASTE` | You are playing a song or the transport is running. Stop first. |
| A step does not toggle | It toggles on a short tap only: held 0.3 s or with a knob turned it does not toggle off. |
| Knobs do nothing but the PERFORM macros | The PERFORM screen is up (FX held, or PAGE mode). Press a page button or [[HOME]]. |
| A knob effect is stuck on | PERFORM FILTER, CRUSH, THROW or DEPTH stay on until turned to 0 (amber `FX` in the top bar). TOOLS `INIT*` switches them off. |
| LFO or lock seems to ignore a knob | MODEL, FILTER TYPE, RESON MODEL, WAVE, DEST and SRC cannot be locked, recorded or learned. |
| `ACID: 2 TRACKS MAX` | At most two ACID tracks. |
| `RESON: 4 TRACKS MAX` / `CHORD: 2 TRACKS MAX` / `MODAL: 2 TRACKS MAX` | The resonator limits. |
| Red CPU icon | An audio block took over 95 %; a voice was stopped. Set less important tracks to PRIO LOW, reduce RESON, SLICER or heavy models (BOOM, PUNCH, SNAP, CRACK, SKIN count double). |
| Sound stutters with Bluetooth on | A saved device that is not connected is searched for again and again: FORGET it or switch BLUETOOTH OFF. |
| No `Hortator` in the DAW | A computer may list Hortator and Hortator 8ch as two devices after switching 2 CH / 10 CH: choose the one shown. Windows may not accept 10 CH: use 2 CH. |
| MIDI notes play nothing | Check GLO CH (10 by default) and ROUT (NOTE needs the track's NOTE to match; SEL plays the selected track; TRACK uses channels 1 to 8). |
| The FM-1 follows no tempo | GLO CLK is INT; set USB, TRS or BLE to follow a clock. |
| A project says `LOAD FAILED` at power-on | The last-saved slot is empty or unreadable: YES forgets it. Projects saved since 0.24 look empty on 0.23.x. |
| Hortator is unstable or crashes | Use a safe start (hold [[SEQ]] while powering on), then install the plain version or go back to stock. |
| The FM-1 will not answer the installer | Try a safe start. If both the installer and M-UPGRADE fail, see *Install, update and recovery*. |

## Screen messages

| Message | Meaning |
| --- | --- |
| `T3 REC ARMED` / `T3 REC OFF` / `REC ALL` / `REC OFF` | recording state |
| `STEP REC nn` | step record at step nn |
| `LOCK STEP n`, `STEP n LOCKS CLEARED`, `STEP n ALL CLEARED`, `LOCKS FULL`, `MOTION FULL`, `LOCKS LOST`, `VARIATIONS LOST` | parameter locks and motion |
| `MIDI LEARN ON`, `LEARN: TURN OR PRESS`, `LEARN DONE`, `LEARN FULL`, `CC NOT LEARNABLE`, `NOT LEARNABLE`, `CLEAR ALL LEARNED?` | MIDI LEARN |
| `CUE>B3`, `VARIATIONS FULL`, `STOP SONG TO CUE`, `SAVE C FIRST` | live cue |
| `DISCARD EDITS?`, `OVERWRITE B*?`, `RENAMED (SAVE TO KEEP)`, `AGAIN: <action>` | projects and tools |
| `USB RECONNECT: 10 CH`, `CLK USB PAUSED` | USB audio mode switch |
| `BT CONNECTED`, `BT LOST`, `BT AUTOSTART SKIPPED`, `WAITING FOR A HOST`, `NOTHING FOUND` | Bluetooth |
| `FM-LINK: LEADING` / `FM-LINK: FOLLOWING`, `FOLLOWING LINK` | FM-Link |
| `NO RF TRIM IN FLASH`, `BT START TIMEOUT` | Bluetooth cannot start: this FM-1 lacks RF data, or switch off and on |
| `USR2 SHORTENED TO 48K`, `USR3 SHORTENED TO 48K`, `USR2 CLAMP FAILED`, `USR3 CLAMP FAILED`, `USR2 ERROR: NO SAVE` | sample slot trimmed at power-on |
| `SAFE START`, `NO AUDIO - USB UPDATE READY` | safe start screen |
| `FELUCCA CRASH` | the firmware crashed; power off and use a safe start |

## Known issues

- The Bluetooth version is experimental; while Bluetooth is on the audio has less headroom, and a saved but absent device can make the sound stutter.
- Hortator is beta software and untested on most hardware.
- The Felucca web editor on the site does not work with Hortator (no sample upload).
- 10 CH USB audio may not work on Windows.
