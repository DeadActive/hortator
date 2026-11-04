# The panel
<!--blurb: Every button, knob and key, and the rules that apply on every page.-->

## Overview

The FM-1 panel has a 240 by 240 screen, 14 buttons, four rotary controls (**SELECT**, **ALGORITHM**, **PRESETS**, **MASTER**), four **KNOB 1 to 4** under the screen, and 27 keys: 16 white and 11 black. The keys are the 16 steps of the selected track in most places; they play tracks, notes or effects elsewhere, as stated in each section.

Pressing a section button opens its first page. Pressing it again (it wraps to the first page), or turning [[SELECT]] (it stops at the ends), steps through its pages; the footer shows `n/m` at the right. A family reopens at the last page used. [[HOME]] returns to the main screen.

## Rotary controls

| Control | What it does |
| --- | --- |
| **MASTER** | The volume: the speaker, the headphones and USB audio. |
| **SELECT** | Turns the pages of a section and moves the menu's cursor. On HOME's TRACKS mode it sets the tempo instead (`CLK USB` / `CLK TRS` when following a clock). |
| **ALGORITHM** | Picks the selected track (T1 to T8), on any page. In MIDI LEARN it picks NEXT / PREV TRACK. |
| **PRESETS** | Loads the next or previous preset onto the selected track at each detent; a popup shows the bank and the presets around it. A factory preset sets the sound only; a USER preset also sets the track's FX. |
| **KNOB 1 to KNOB 4** | Edit the four values on the screen, one knob per column. Knob turns can be recorded as MOTION. |

## Buttons

| Button | Opens |
| --- | --- |
| **HOME** | Two modes: **TRACKS** (KNOB 1 TRACK, 2 LEVEL, 3 LEN, 4 PAN; the selected track's steps on the keys) and **HOME** (the scope, the track's first four knobs, the keys play the tracks; SELECT goes on to the compressor's pages). A tap on HOME switches between them; from any other screen it opens the one last shown. Power-on: TRACKS. **Hold HOME** for the device menu. |
| **EDIT** | The selected track's sound: its model and knobs. **Hold EDIT** (about 0.4 s) for the PRESETS list. |
| **SEQ** | The steps on the white keys, then PATTERN, MOTION and SONG (a short tap turns pages, on its release). **Hold SEQ** (0.4 s, or press a key with it) for the step TOOLS screen. |
| **FX** | The track's FILTER, FX (the sends), INSERT (with SRR), SLICER and RESON: 8 pages. **Hold FX** for PERFORM. |
| **ARP** | Grids, the map of drum patterns. |
| **LFO** | The track's two LFOs side by side, then DEST. |
| **ENV** | STEPS / TRACKS mode and parameter locks (a padlock in the top bar while STEPS). ENV turns KEYS off and SCL turns STEPS off. |
| **SCL** | KEYS: the keys play the selected track chromatically. Labelled SEL in the web simulator. |
| **GLO** | A tap: GLOBAL, SYSTEM, GLOBAL, then DLY, DLY, REVERB and CHORUS (7 pages). **Hold GLO**: the MUTE screen (mutes, solo, MIDI LEARN on G4). |
| **SAVE** | A tap (on the release): PROJECT, TOOLS and COPY (3 pages). **Hold SAVE**: the cue map. |
| **PLAY** | Play and stop. On SONG, plays the song. |
| **REC** | On TRACKS a tap arms the selected track; elsewhere it is REC ALL. Hold 0.4 s off the SEQ pages: a fill. Hold 0.7 s on a SEQ page: CLEAR TRACK. See *Playing and recording*. |
| **OCT- / OCT+** | The previous / next bank of 16 steps; the octave in KEYS; NO / YES, BACK / OK in dialogs. They *breathe* (fade in and out) when pressing them would do something. |

### HOME in detail

HOME has two modes. **TRACKS** is the mixer: for each track LEVEL, LEN (length), PAN, and the track's steps on the keys. **HOME** is the performance screen: a strip under the top bar, the selected track's four main knobs, a large scope, and the four boxes above `PROJ` showing the bar of a four-bar cycle (the current bar solid, moving with the playhead and back to the first on every PLAY). [[SELECT]] on HOME goes on to the compressor's pages (see Grids and compressor).

### EDIT, FX, LFO and the selected track

On HOME, EDIT, FX and LFO the selected track's key (a white key F3 to F4) is lit, and pressing a white key there selects that track. KEYS, COMP, the step pages, GLOBAL, SAVE and GRIDS keep their own key jobs.

## The keys

- **16 white keys**: the steps 1 to 16 of the current bank (up to four banks, 64 steps); on HOME the white keys F3 to F4 play tracks 1 to 8; with [[GLO]] held they mute them.
- **11 black keys**: with [[FX]] held, live effects on the whole mix. In KEYS, the sharps. On HOME and SEQ the first 8 black keys queue variations (see Sequencer).
- **Held buttons** change what the keys do: [[GLO]] held: white keys mute, black keys solo; [[FX]] held: black keys are effects, white keys F3 to F4 repeat a track's step (REPEAT STEP), D5 to G5 pick its rate; [[SEQ]] held: step copy and paste tools; [[SAVE]] held: the cue map; [[OCT+]] held: a step tap sets its accent.

## Screens and the top bar

The screen has an inverted **top bar** (title, track T1 to T8, tempo icon, messages such as `T3 DECAY 64`, `REC ALL`, `LOCK STEP 5`, `LOADED`, `BT CONNECTED`), a compact **knob row** with round dials and their values, the **graph area** (scope, sound graph, piano roll...), a **trig strip** of steps and a **breadcrumb** such as `T1>K909>SOUND`.

| Top-bar sign | Meaning |
| --- | --- |
| padlock (left of the battery) | ENV is in STEPS mode |
| amber `FX` in place of the tempo icon | a PERFORM knob effect (FILTER, CRUSH, THROW, DEPTH) is on |
| red CPU icon for 2 s | an audio block took over 85 %: one voice was stopped |
| Bluetooth rune | whole: linked; struck through: on but not linked; absent: off (Bluetooth version) |
| `MIDI LEARN ON` | MIDI LEARN is active |
| `STEP REC nn` | step record, at step nn |
| `CLK USB` / `CLK TRS` / `CLK BLE` / `CLK LINK` | the tempo follows an outside clock |

## Common rules

- **Step keys.** A step toggles only on a short tap (released within 0.3 s with no knob turned), on the release. Held 0.3 s, an off step turns on at once, ready to edit; an on step stays on.
- **Held step.** While a step is held, [[KNOB 1]] / [[KNOB 2]] / [[KNOB 3]] / [[KNOB 4]] set its PROB / RATCH / ACCENT / NUDGE (-50 to +50). This works on STEP, PATTERN, MOTION, SONG and TRACKS; the page's own knobs rest while a step is held. [[OCT+]] held plus a step toggles that step's accent.
- **Bank.** With KEYS off, [[OCT+]] alone (on its release) and [[OCT-]] move to the next / previous bank of 16. While [[OCT+]] is held the keys show the bank's accents.
- **Knob acceleration.** With MENU ACCEL ON, turning a knob fast over a wide value speeds it up (see Settings).
- **Safe start.** Hold [[SEQ]] while powering on: nothing is loaded, every LED is dark, USB audio is 2 CH, Bluetooth does not autostart.
