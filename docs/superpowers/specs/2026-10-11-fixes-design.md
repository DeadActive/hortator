# Fixes after 0.16.0: navigation, REC, mutes, last project, PERFORM knobs, metronome, CPU priority — design

Date: 2026-10-11. Branch `fixes` (worktree `felucca-fixes`) from `main` = `drum-v0.16.0` (f3d7e5a). Drum machine
fork: 2026 DEADACTIVE.

## 1. The user's request and answers

"select knob to change pages instead of preset, select knob change bpm only on tracks page; tracks page now on the
home page, leaving rec button its dedicated purpose, rec now can be armed from any screen; home page now have 2 modes
changing by pressing home, first is a tracks, second is what home screen now; fix projects fast mute, instead of
pressing OCT -, open mute screen by holding GLO button showing tracks on screen with boxes, filled on, outline off, as
well as keyboard indication; remember what project was saved last, and open it automatically after boot, if cannot
load show error, and confirm to reset project; performance mode knob fx must stay on after performance fx screen is
closed, and continue working; add metronome, it can be enabled on global screen; raise maximum cpu limit to 95% before
dropping channels, add priority option per track (HIGH, LOW), high leaves last, low leaves first on cpu overflow; also
show cpu overload icon in top bar if drops happening."

Answers (2026-10-11): REC keeps step record (STEP page, KEYS on, stopped) and its holds (CLEAR TRACK on SEQ pages,
DELETE ROW / CLEAR SONG on SONG); priority is set on the track's EDIT (SOUND) pages; the metronome is a device setting;
a failed auto-load asks "START NEW PROJECT?" (YES: forget the slot; NO: keep trying next boot), both continue with the
power-on kit.

Hard rules as for every release: frozen files unchanged (check_untouched, gates 2a/2b), H2/H3, the budgets, gate 5,
integer only, no 64-bit divide by a 64-bit divisor.

## 2. Knobs

- **SELECT** turns the pages, everywhere PRESETS did: the pages of a family, HOME's COMP pages (from HOME's second
  mode, §3), the PERFORM layer's page underneath, the MENU cursor, the Bluetooth page's rows. **Except on HOME's
  TRACKS mode**, where SELECT sets the tempo (BPM, with the `CLK USB/TRS/BLE` message when following a clock).
- **PRESETS** does nothing (free for later). **ALGORITHM** stays track select.
- BPM elsewhere: GLOBAL 1/3 KNOB 1 as now.

## 3. HOME's two modes

- HOME has two modes: **1 TRACKS** (the TRACKS page as it is now: the mixer graph, KNOB 1–4 TRACK LEVEL LEN PAN, the
  keys as on TRACKS) and **2 HOME** (the HOME screen as it is now: the scope, KNOB 1–4 the selected track's first four
  knobs, the keys play the tracks).
- A HOME tap on HOME toggles 1 ↔ 2. From any other screen, a HOME tap opens HOME in the mode last shown. From mode 2,
  SELECT right goes to COMP 2/3, 3/3 as PRESETS did; SELECT left from COMP 2/3 returns to HOME mode 2. A HOME tap on a
  COMP page returns to HOME mode 2.
- Power-on: mode 1 (TRACKS). The footer says `TRACKS` / `HOME 1/3`.
- The separate TRACKS page (FAM_MIX) goes away; nothing else opens it.

## 4. REC

- **REC tap = arm / disarm the selected track for recording, on every screen** (as REC on TRACKS today:
  `track_rec_toggle(song.sel)`; arming while stopped starts play; `STOP TO EDIT` while a song plays or is armed).
- Kept: on the STEP page with KEYS on and stopped, a REC tap starts step record, and a REC tap ends it; REC held on the
  SEQ pages asks CLEAR TRACK, on SONG DELETE ROW / CLEAR SONG.
- Gone: REC no longer opens TRACKS and is no longer the mute modifier (REC held + a key). Not in the menu, NAME or a
  confirm. The REC LED: lit while any track is armed or step record runs.

## 5. The MUTE screen (GLO held)

- **GLO tap** (released within 400 ms, no key pressed meanwhile): the GLOBAL pages as now (on release).
- **GLO held** 400 ms, or a key pressed while GLO is down: the **MUTE screen**, shown while GLO is held:
  - the 8 tracks as boxes: **filled = audible, outline = muted**; a mute waiting for the bar blinks; the track number
    and its model in each;
  - under each box the key that toggles it (`F3 G3 A3 B3 C4 D4 E4 F4`, the white keys F3..F4 as `KEY_TRK_KEY`);
  - the mode line: `MUTE: NOW` / `MUTE: NEXT BAR`, with its keys shown (top C# = NOW, top D# = NEXT BAR, as REC + C# /
    D# today).
- The keys toggle the mutes (`track_mute_toggle`, so NEXT BAR applies); the key LEDs: lit = audible, blinking =
  waiting. While the screen is up the keys play nothing. Releasing GLO closes it. Not in the menu, NAME, a confirm or
  the PERFORM layer.

## 6. The last project

- Every successful project SAVE remembers its slot in the device settings (spare bits, §9); a SAVE in RAM only (no
  flash) remembers nothing.
- **At power-on** (not in safe start), after the power-on kit is set, the remembered slot is loaded as a LOAD does
  (top bar `LOADED`), stopped.
- **If it cannot load** (empty or unreadable): a confirm screen `LOAD FAILED: SLOT n` / `START NEW PROJECT?`, `OCT- NO
  OCT+ YES`. YES forgets the slot (saved); NO keeps it (tried again at the next power-on). Both continue with the
  power-on kit.

## 7. PERFORM knobs stay on

- The PERFORM knob effects (FILTER, CRUSH, THROW, DEPTH: `perf_k`) **keep their values and keep working** after the
  PERFORM screen closes: FX released (HOLD), the PAGE screen closed, the menu, a confirm, NAME. They change only on
  the PERFORM screen (turn back to 0 to switch one off), with INIT ALL and BENCH resetting them.
- The buffer effects (the black keys) still stop when their key is released and when the menu / a confirm / NAME
  opens, as now.
- The top bar shows `FX` while any knob effect is on, so they are never invisible.

## 8. Metronome

- GLOBAL 3/3 gains **METRO: OFF / LOW / MID / HIGH** (a device setting like MUTE, saved, not in projects).
- While playing (own clock or following one), a click on every quarter note, accented on the bar's first: a short
  decaying sine (about 15 ms; 1.6 kHz on the bar, 1 kHz on the other beats), at the chosen level, mixed into the master after the PERFORM
  effects (they leave it alone), at the MASTER level (the volume knob turns it down with the mix; amended 2026-10-12). It goes to the speaker, the line out and USB audio. Nothing
  while stopped.

## 9. Settings bits

In the existing settings record (no size or magic change), in `lowcut`'s spare bits:
- bits 2..4: the last saved project (bit 2 = set, bits 3..4 = slot 0..3);
- bits 5..6: METRO (0 OFF, 1 LOW, 2 MID, 3 HIGH).
Older Hortator reading a record with any of these set reads SPEAKER EQ as FLAT (as with the Bluetooth bits).
Kept in the non-`.noinit` static `btset`-style storage; `settings` stays 32 B.

## 10. CPU: 95 % and priority

- Voices are shed when a render half takes more than **95 %** of its time (was 85 %).
- **PRIO** per track: **HIGH / LOW**, default HIGH, on the track's SOUND (EDIT) list after CHOKE. When a voice must go
  (the voice cap or a CPU shed), an ACID voice with its gate open still goes last, then a LOW track's voice goes before
  a HIGH track's, then the oldest.
- PRIO is a track parameter (`P_PRIO`), saved in projects: format **FDRC** (FDR1..FDRB load as before, PRIO HIGH).
- **Overload icon:** after any shed the top bar shows the CPU icon (`ICON_CHIP`, red) for 2 s.

## 11. Tests, docs

- Host tests for each item (ui_test: knobs, HOME modes, REC everywhere, the mute screen; boot_test: last project,
  settings bits, FDRC; drum_test: priority shedding, the 95 % threshold in audio_isr_test; perform_test: knobs stay;
  a metronome test: timing on the beat, levels, nothing while stopped, the golden kit hashes unchanged with METRO OFF).
- Screenshots of the new screens (TRACKS on HOME, the MUTE screen, the load-failed confirm, the FX and CPU marks).
- README (the panel table, knobs, mutes, PERFORM, projects), DEVICE_INSTALL, the web simulator's hints and reel
  script, CHANGELOG.

## 12. Key backlight (added 2026-10-12)

The user's request (2026-10-12): "add a keyboard backlight option in HOME long press menu, only 2 options, off and
on; when on, lit unselected keys 20 %, main beats (1, 5, 9, 13) 40 % brightness". The user approved changing the
frozen `main.c` with a hook in the TIMER5 key-scan ISR and moving the frozen baseline to a new tag.

- **MENU** (HOME held): a row **BACKLIGHT OFF / ON** after ACCEL (an ON / OFF row: KNOB 1 right ON, left OFF, OCT+
  toggles; it shows at once). Default menu: ten rows, 16 px apart (were nine, 18 px). Bluetooth build: eleven rows,
  15 px apart, the two hints moved down to y 177 / 193 (screenshots `build/ui_shots/fixes/menu_backlight.png`,
  `menu_backlight_ble.png`).
- **ON:** every key LED (the 27 keys) that is not lit glows at **5 %**; on every screen where the white keys are the
  selected track's steps (`step_keys()`, the keys' own test and the branch where `ui_leds` draws steps: STEP,
  PATTERN, MOTION and SONG while KEYS is off, armed or not; HOME's TRACKS mode while no track is armed, KEYS on or
  off), an unlit step whose number in the bank shown is **1, 5, 9 or 13** glows at **25 %** (bank 2: steps 17, 21,
  25, 29; past the track's LEN as well), from the same `STEP_KEY` / `ui.bank` mapping the key presses use. Lit keys stay full
  (steps on, the playhead, held keys, KEYS, the MUTE screen's heard tracks and mode key, PERFORM's effects and
  tracks, NAME's keys). On the playhead's step that is on (shown dark), the step dims like any unlit one. The
  buttons' LEDs are unchanged. Where the keys are not steps (the MUTE screen, PERFORM, NAME, the menu, KEYS, COMP,
  the HOME screen, the per-track and global pages, TRACKS armed): unlit keys 5 %, no beat marks. Safe start: every LED
  off, as before. **OFF:** exactly as before (`fm1_led` written by `ui_leds`, the hook idle).
- **Setting:** `backlight` (panel.c, a static, not `.noinit`), in the settings record's `lowcut` **bit 7** (`lc_pack`
  / `lc_unpack`, project.c; bits 0..1 SPEAKER EQ, 2..4 the last project, 5..6 METRO). Only from the flash record:
  `persist_boot` clears it, so an older record (bit 7 clear) or none reads OFF. Saved when the menu closes. An older
  Hortator reading a record with it set reads SPEAKER EQ FLAT (§9).
- **Dimming** (`firmware/src/led_dim.c`, included by `ui_input.c`): the LEDs are on / off, multiplexed by the frozen
  HAL's scan (`fm1_input_tick`, 10 kHz, one of 11 columns per tick, column n lit with `fm1_led[n]` until the next
  tick, also while the audio ISR renders). With BACKLIGHT ON, `ui_leds` writes one word per column, `led_lay[c]` =
  full | 25 % << 8 | 5 % << 16 (one 32-bit store: the scan never sees half a column), then `led_dim_on = 1`. The hook
  `led_dim_tick()`, called by `fm1_timer5_irq` right after `fm1_input_tick()` (before the `in_audio` early return),
  composes `fm1_led[n]` for **the column the next tick lights**, `n = fm1__tick_col + 1` (the HAL sets
  `fm1__tick_col` to the column it has just lit), so a column's value is written one tick before it is used and never
  changes while lit. PWM over 20 scan frames (220 ticks, 22 ms): the phase advances at column 0; full in every
  frame, 25 % in phases 0 4 8 12 16 (every 4th frame, ~230 Hz), 5 % in phase 0 (1 in 20, ~45 Hz). While `led_dim_on` is 0 the hook returns at once and `fm1_led` is the UI's,
  bit for bit. Non-frozen code in the TIMER5 path otherwise runs only when not nested in the audio ISR
  (`uart_midi_poll`), hence the hook in the frozen file.
- **Cost** (measured in the build's listing): the hook is inlined into `fm1_timer5_irq`, no call, no loop. OFF: 3
  instructions (the flag's address, a byte load, a branch). ON: 18 instructions per tick, 24 at the tick that starts
  a frame (the phase step), i.e. ~18.5 per 100 µs tick on average, well under 1 % of a tick.
- **The frozen baseline moved** (user-approved 2026-10-12): `frozen-base-8` (local tag) = `frozen-base-7` + the
  hook line in `main.c` + the glue the reference build needs (`led_dim.c` as is, included by `felucca.c` before
  `main.c`; idle there: nothing sets `led_dim_on`), made as the earlier `frozen-base-N` tags. `tools/frozen_base.txt`
  points at it; `build/upstream` (H2 / H3's reference) is rebuilt from it by `tools/upstream_build.sh`;
  `check_untouched.py` compares against it. Gate 2a (`check_update_path.py --sources`) accepts `main.c` differing
  from the last release (`drum-v0.16.0`) by exactly that one line after `fm1_input_tick();` and nothing else (a
  self-test: any other change, the line twice, elsewhere or altered, or a second one once a release has it, fails).
  Gate 2b (the update path's machine code = the reference core's) and gate 3 are unchanged.
- **Tests:** `tests/led_dim_test.c` runs the real HAL scan on host GPIO registers with the hook after each tick (OFF
  bit-exact, the duty per layer and column from any start, the one-ahead write, a layer change never torn);
  ui_test (`test_backlight_leds`, `test_backlight_beats_everywhere`, `test_backlight_menu`): the layers on STEP, bank
  2, the playhead, HOME, the MUTE screen, PERFORM, safe start, OFF again = the full layer; every page of the table and
  the HOME screen with KEYS off / on, armed or not: where `step_keys()` the beats at 25 % and the on steps full in
  bank 1 and bank 2, elsewhere no 25 % (the menu, the MUTE screen and PERFORM over STEP too); the menu row; bt_page_test: eleven rows above the hints;
  boot_test: bit 7 round trip with the other bits, an older record and none read OFF.

  Amended 2026-10-12 (user): 5 % and 30 % instead of 20 % / 40 %: PWM over 20 frames, 30 % in phases 0 3 7 10 13 17, 5 % in phase 0.
  Amended again 2026-10-12 (user): 5 % and 25 %: 25 % in phases 0 4 8 12 16.
  2026-10-12 (user): "the divisions must be visible on every screen when sequencer on keys available": the beat
  marks on every screen where the keys are steps, as above (each such screen already had them; the test now walks
  every page and HOME mode). The text above now gives the levels in force (5 % / 25 %, 20 frames).
