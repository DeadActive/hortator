# MIDI LEARN: design

Date: 2026-10-13. Branch `learn` (worktree `felucca-learn`) from `main` = drum 0.17.0 (40daee2). Drum machine fork:
2026 DEADACTIVE. Part 4 of the Bluetooth MIDI design (`2026-10-08-ble-midi-design.md`, "Part 4: MIDI learn").

## 1. The user's request and answers

"work on midi learn", then "check how upstream midi learn does that". Upstream Felucca 1.5's MIDI LEARN
(`midi_learn.c`, `midi_control.c` `midi_learned` / `midi_cc_set`, `editor_learn.c`) is the model: GLO held + a key
turns it on and off; a knob turned picks its parameter; the next CC received is bound to it; OCT- clears the picked
parameter's CC, OCT+ is done; the footer says what is picked and its CC; up to 16 CCs, one CC sets one parameter and a
parameter has one CC.

Answer (2026-10-13): **a per-track parameter is bound to the track it was learned on** (the controller always moves
that track, whichever is selected).

This narrows part 4 of the BLE design: CCs only (no pitch bend: the SMC-Mixer's faders are part 5's layout), on any
channel (no channel stored), 16 bindings (upstream's `ML_N`), in their own flash object (not the settings record).

## 2. Turning it on and off

- **GLO held** (the MUTE screen, 0.17.0 §5) **+ G4** (the first white key above F4, key 14): LEARN on / off. The MUTE
  screen shows `G4 LEARN: OFF` / `G4 LEARN: ON` under its mode line, and G4's LED is lit while it is on. Keys pressed
  with GLO are the screen's (nothing plays), as for the mute keys.
- On: the top bar says `MIDI LEARN ON`. Off: `LEARN DONE`.
- LEARN ends (as off, no message) when the menu opens (HOME held), NAME opens, or any confirm opens other than
  LEARN's own `CLEAR ALL LEARNED?`. It is never saved: power-on is always off.

## 3. While LEARN is on

- **Picking.** A knob turned that edits a per-track parameter picks that parameter **on the track it edits** (the
  selected track), and still edits it as usual:
  - HOME (the HOME screen): KNOB 1..4, the track's first four sound knobs;
  - EDIT SOUND pages (not MODEL), LAYER, FILTER, FX (DST CHO DLY REV), SLICER, RESON (not MODEL), LFO (LFO 1 and 2),
    PATTERN (LEN DIV SWG SRC);
  - HOME's TRACKS mode: LEVEL, LEN, PAN (KNOB 1 TRACK picks nothing).
  MODEL and RESON MODEL are not learnable (a model change loads its sound, a capped RESON model is refused: not
  something a CC can do from the audio interrupt): their turn says `NOT LEARNABLE` and picks nothing. Global pages,
  PERFORM's knobs, a held step's PROB / RATCH, step record, MOTION, SONG: no pick. The MUTE screen's knobs edit
  nothing.
- **Binding.** Once a parameter is picked, the next CC received is bound to it: any channel, from USB, TRS, or
  Bluetooth in MIDI mode. Not Bluetooth in SMC DAW mode: the SMC-Mixer's layout keeps its own controls. Top bar:
  `CC74 -> T3 DECAY`. The pick then ends (`LEARN: TURN A KNOB` again); turn the next knob. While a parameter is
  picked, CCs only bind (they set nothing); of the CCs in one audio block the first learnable one is taken (a bank
  select before it is skipped).
- **Names:** `T<track> <label>`; a knob whose label is on other pages too carries its section: `L1.` / `L2.` (the
  LFOs), `F.` (FILTER), `R.` (RESON), `S.` (SLICER): `T3 L1.DEPTH`, `T3 F.DECAY`.
- **Rules** (upstream `ml_learn` / `ml_drop`): one CC sets one parameter, a parameter has one CC: learning a CC
  again, or a parameter again, replaces both old bindings. 16 bindings at most: a 17th says `LEARN FULL` (32 / a 33rd since §7). CC 0 and 32
  (bank select) and 120..127 (channel mode) are never learned: `CC NOT LEARNABLE`. (Hortator gives no other CC a
  meaning: `seq.c` `midi_block` dropped every CC.)
- **OCT- / OCT+ are LEARN's while it is on** (their page uses wait: pick the page first, LAYER, LFO 2, the step
  bank, then turn LEARN on):
  - OCT- with a parameter picked: its CC cleared (`T3 DECAY CC CLEARED`; none: `NO CC TO CLEAR`); the parameter
    stays picked (the next CC binds it again, as upstream);
  - OCT- with nothing picked: the confirm `CLEAR ALL LEARNED?` (`OCT- NO  OCT+ YES`; YES: `LEARN CLEARED`); nothing
    learned: `NOTHING LEARNED`, no confirm. LEARN stays on through its own confirm;
  - OCT+: done (`LEARN DONE`), as GLO + G4.
- **The footer's first row** (the steps' row) shows LEARN's line, as upstream's `ml_line`: `LEARN: TURN A KNOB` (§7:
  `LEARN: TURN OR PRESS`),
  `T3 DECAY: SEND A CC`, `T3 DECAY = CC74` (a picked parameter already learned: a new CC replaces it).

## 4. A learned CC

- On any channel, from USB, TRS, or Bluetooth in MIDI mode (whatever the drum channel and CLK are), a learned CC sets
  its parameter **on its track**, whichever track is selected, whether LEARN is on or off.
- Value 0..127 maps linearly onto the parameter's range (`track_desc`: min..max, rounded): `min + (v * (max - min) +
  63) / 127`. An enum moves by step over its shown order (DIV in its length order, `enum_order`). A bipolar knob
  (min < 0 < max: PAN -64..63, an LFO's DEPTH -64..64, FILTER ENV, LAYER LTUNE) has its 0 at 64: 0..63 map onto
  min..0, 64..127 onto 0..max (upstream's single line would skip DEPTH's 0: 63 -> -1, 64 -> +1). A model knob is the track's model's (P_E0..
  P_E7: a model without that knob ignores it). RESON STRCT on CHORD steps the chords (its own range).
- A PATTERN parameter (LEN DIV SWG SRC) while a song plays or is armed: nothing (the knob says `STOP TO EDIT`).
- Applied in the audio interrupt (`seq.c` `midi_block`, inside `events_block`, after `lfo_apply`): the value goes to
  the parameter's **base**: the track's LFO copies are put back (`lfo_restore_track`), the base written, the LFOs
  applied again with no advance (`lfo_track(t, 0)`), so an LFO on that knob keeps modulating around the new value and
  `lfo_restore` at the block's end does not undo it (as motion's `motion_step` does). While motion plays, the value is
  the knob's new base (as a knob turn: `mo.base`). CC moves are not recorded into motion.

## 5. The map and its storage

- 16 entries in RAM (32 since §7, with the actions' codes and a note source), `{cc, track, code, 0}` (4 B each, the code 0 = empty), read by the interrupt, written by the UI
  with the interrupts off.
- **The code is the parameter's P_* id + 1.** Hortator's P_* ids never move: projects store every track's
  parameters by id (FDR1 .. FDRC), so new parameters are only ever appended before `P_COUNT` (FDRC added P_PRIO at the
  end). The model's knobs are P_E0..P_E7 (codes 2..9) whatever the model. A build check pins the ids (P_E0 1, P_LEVEL 9,
  P_LFO1 32, P_RMODEL 48, P_FTYPE 55, P_PRIO 60): moving one fails the build. Unlike upstream (whose P_E0 moves when
  common parameters are added, hence its `ML_E0`), no offset is needed.
- **Flash:** its own object, `OBJ_UPRESET0` (storage.c's user-preset object, 0xDC000..0xDDFFF, unused by Hortator;
  storage.c unchanged, only `st_save` / `st_load`). The record: magic `HLRN` (0x4E524C48), version 1, count 16, the
  16 entries (72 B). Loaded in `persist_boot` (interrupts still off, after the flash check), the RAM map cleared
  first: no flash, a blank or older flash (no record), a wrong magic / version / length (Felucca's user presets
  there), all read as nothing learned. An entry with an unlearnable CC, a track past 8, a code this build does not
  know or cannot learn, or a CC / parameter already in an earlier entry is dropped.
- **Saved** after each learn, clear and clear all, and only when the map changed; while playing (or PLAY pending)
  deferred until stopped, as the settings (`settings_poll`): a storage write erases a 4 KiB sector with the audio
  silenced.

## 6. Tests, docs

- `tests/learn_test.c` (host, with the Bluetooth core and the host flash): the flow (GLO + G4 on, a knob picks, a
  CC binds, the message, OCT+ done, GLO + G4 off), the replace rules, unlearnable CCs, a full map, OCT- clears one,
  OCT- clear all with its confirm (NO keeps, YES clears), LEARN ends on the menu / NAME / another confirm, the CC over
  a parameter's range (a plain knob, an enum, DIV's order, a bipolar knob), the bound track not the selected one, an
  LFO on the same parameter (the base moves, the modulation kept, through the block's end), motion's base, a song
  locks PATTERN parameters, USB / TRS / Bluetooth MIDI mode apply and SMC DAW mode does not, the map saved and read
  back (round trip, deferred while playing), a blank / older / foreign record and no flash (nothing learned).
- Screenshots: `build/ui_shots/learn/` (the MUTE screen with G4 LEARN off and on, the footer's three lines, the
  confirm).
- README (a MIDI LEARN section), CHANGELOG (Unreleased), docs/DEVICE_INSTALL.md (a short check).

Hard rules as every release: frozen files unchanged (`check_untouched`, gates 2a / 2b), H2 / H3, the budgets, gate
5, integer only.

## 7. Actions (amendment, 2026-10-13)

User: "i also want to map buttons, like play and rec, as well as current track change, and performance".

- **Learnable actions** besides parameters. While LEARN is on, the FM-1 control picks its action instead of doing
  its job (it works again after OCT+):
  - PLAY pressed: `PLAY` (a press = a PLAY tap: play / stop, SONG as ui_input's B_PLAY);
  - REC pressed: `REC` (a press = a REC tap: arm / disarm the selected track, step record as the button; REC held
    while learning is no CLEAR TRACK);
  - ALGORITHM right / left: `NEXT TRACK` / `PREV TRACK` (a press = track_select +- 1, clamped);
  - FX held (or the PERFORM page) + a black key: that effect, `FX R1/16`, `FX TAPE`..; + a white key F3..F4: that
    track's PERFORM mute, `FX MUTE T3`. The key plays nothing while learning (seq.c keyboard_block: `ml_keys`, the
    press handed to the UI in `ml_kb_hit`);
  - on the PERFORM screen a macro knob: `FX FILTER`, `FX CRUSH`, `FX THROW`, `FX DEPTH`. Like a parameter's knob it
    still turns its macro.
- **Sources.** A button action takes a CC (any channel; a press when it rises to >= 64, a release when it falls
  below 64: edges only) or a note (any channel; note on with velocity > 0 a press, note off / velocity 0 the
  release; every note on is a press). A parameter or a macro takes a CC only (a note while one is picked plays as
  usual and binds nothing). A learned note is consumed (no drum plays it). CC 0, 32, 120..127 are never learned.
  One source, one thing; a thing has one source (learning replaces both, as §3).
- **Names:** top bar `NOTE36 -> PLAY`, `CC22 -> FX R1/16`, `CC20 -> FX FILTER`; footer `PLAY: SEND CC/NOTE`, `FX
  FILTER: SEND A CC`, `PLAY = NOTE36`; idle `LEARN: TURN OR PRESS`. OCT- clears the picked action's source (`PLAY
  CLEARED`, none: `NOTHING TO CLEAR`).
- **Where they run.** PLAY, REC, NEXT / PREV need the UI: the audio interrupt queues their presses (`ml_q`, 8 deep,
  codes only); the UI drains it each frame and runs them as the button would on that screen (PLAY: its press bit;
  REC: a REC tap; NEXT / PREV: as an ALGORITHM step). So none acts in the menu or a confirm, PLAY acts on the MUTE
  screen and in NAME as the button does. PERFORM effects and macros are set in the audio interrupt, as the layer's
  keys are: `perf_held` = the keys' (`perf_kh`) | MIDI's (`perf_mh`, ml_holds: the entries whose button is down);
  `perf_ord` takes the press (the last pressed buffer effect plays); a macro CC writes `perf_k` (FILTER -100..100,
  0 at 64; the others 0..100) and stays, as the knob's.
- **Never stuck.** A MIDI-held effect ends on its release; while PERFORM's effects are off (`perf_kill`: the menu, a
  confirm, NAME, the MUTE screen) no press holds one and each block drops those held (learn_block): none comes back
  after, until pressed again. A binding cleared or replaced while held releases its hold (ml_put). Safe start reads
  no MIDI (mix_block drops it). STOP does not end them (as the keys).
- **The map:** 32 entries `{num, trk, code, src}` (src 0 CC, 1 note; the byte version 1 kept 0). Codes: a parameter
  P_* id + 1 (1..P_COUNT, kept under 0x80), PLAY 0x80, REC 0x81, NEXT TRACK 0x82, PREV TRACK 0x83, a PERFORM
  effect / mute 0x90 + PF_* (0x90..0xA1: PF_* pinned by a build check), a macro 0xB0 + 0..3. An action's track is 0.
- **Flash:** version 2, count 32 (136 B). A version 1 record (count 16, 72 B) loads into it (its entries as CCs);
  the next change saves version 2. Dropped at load as §5, and: a note on a parameter or a macro, a source kind
  past 1, an action this build does not know, an action with a track.
- **Tests** (`tests/learn_test.c`): learning each action by its control (PLAY, REC, ALGORITHM +-, FX + a black key,
  FX + a white key, a PERFORM knob) and that the control does nothing else while learning; a CC button and a note
  button trigger them (press edges only, the release ends a hold); a learned note plays no drum; a MIDI-held effect
  renders bit for bit as its key (REPEAT, a mute, TAPE); none stuck after the release, the menu, a confirm, NAME, a
  binding cleared or replaced; key and MIDI holding one effect; version 1 loads into version 2; 32 entries, a 33rd
  `LEARN FULL`; OCT+ gives the buttons back.
