# Installing the drum firmware on an FM-1 (step by step)

Do the steps in order. **Stop at the first step that does not go as written**, go back to stock (step 6) if the
drum firmware is installed, and report what you saw. Nothing here needs extra hardware.

What protects you: the source of the update path (USB-MIDI), the start-up code and the update loader is
byte-for-byte upstream Felucca's (`check_untouched`); in the binary, every one of those functions the compiler keeps
on its own does what upstream's does (H2: the same instructions, or the same effects where only data placement
differs); the ones inlined into other code are checked as source only. The FM-1's built-in bootloader cannot be
erased. The one case that needs extra hardware is a crash on every boot before USB answers — the host tests
(H1–H4) are there to rule it out, and the safe start (step 4.4, step 7) is the way around a crash in the drum
code itself.

## 0. Prepare the way back (before anything else)
1. Download M-VAVE's official FM-1 firmware updater and the stock firmware from M-VAVE's website into a folder
   outside this repo. Keep them.
2. Note which firmware version your FM-1 has now (as M-VAVE's updater or the FM-1 reports it), so you can
   go back to exactly that one.
3. Charge the FM-1. Updates run on its battery; don't install on a low battery.
4. Use a USB data cable you have used with the FM-1 before.

## 1. Rehearse going back (no risk)
Reinstall the **same** stock version with M-VAVE's updater.
- Expected: the update finishes, the FM-1 restarts and works as before.
- This proves the updater, your computer and your cable work together.

## 2. Rehearse a round trip with known-good firmware
1. Install the current upstream Felucca from its web installer: https://hugelton.github.io/Felucca/ (Chrome or
   Edge). Expected: it starts and plays sound.
2. Go back to stock with M-VAVE's updater (as in step 1). Expected: stock firmware, working.

Both directions now work on your FM-1, with firmware many people run.

## 3. Install the drum build
1. In this repo: `git rev-parse --short HEAD` and `shasum -a 256 build/felucca-UNTESTED.fwsc` — note both.
   The build must come from `DRUM_PACKAGE=1 ./build.sh` with `tests/run_tests.sh` ending in
   `ALL HOST TESTS PASSED` (no `SKIPPED`).
2. The installer needs `mido` and `python-rtmidi` in your own Python environment
   (`python3 -m venv ~/fm1-venv && ~/fm1-venv/bin/pip install mido python-rtmidi`).
3. Connect the FM-1 (powered on, stock or Felucca) and run:
   `~/fm1-venv/bin/python tools/fm1_install.py build/felucca-UNTESTED.fwsc`
   Expected: it finds the FM-1, asks to confirm, writes, and the FM-1 restarts.
   (The FM-1's USB name: **Hortator** while the drum firmware runs, in a DAW / AUM / the MIDI port list; older
   drum builds and upstream Felucca: Felucca; while the update is written: **Felucca Update**. The installers and
   the editor find each of them.)

   Or with the web installer: every `DRUM_PACKAGE=1 ./build.sh` also rebuilds a local copy of the site in
   `build/site` with that same package, labelled `drum-<version>+<commit>` (docs/VERSIONING.md; `-dirty` when the source had uncommitted
   changes). Serve it with `cd build/site && python3 -m http.server 8000` and open
   http://localhost:8000/webapp/installer/ (Chrome or Edge). Check that the version it shows is the commit you
   noted in step 1.

## 4. First boot (in this order)
1. The start screen shows the **HortatoR** logo / DRUM MACHINE, then TRACKS (HOME's first mode: the mixer, footer
   `TRACKS`). If a project was saved to flash before, it loads by itself (`LOADED` in the top bar).
2. A HOME tap: the HOME screen (footer `HOME 1/3`); the 8 white keys F3..F4 play the 8 tracks (kick, snare, clap,
   hats, ...).
3. **The update path from the drum firmware:** run the step-3 command again (the same file).
   Expected: it installs and the FM-1 restarts into the drum firmware.
   If this fails: go back to stock at once (step 6) while the drum firmware still runs.
4. **Safe start:** power off; hold **SEQ** and power on, keep holding it until the screen shows
   **SAFE START** ("NO AUDIO - USB UPDATE READY"); then let go. (SEQ is read once, right after the HortatoR
   start screen appears.) While it shows SAFE START, check that USB answers:
   `~/fm1-venv/bin/python tools/fm1_install.py --info` prints the FM-1's identity (this only asks, it writes
   nothing). This rehearses the way out of a crash (step 7). Power off to leave safe start.

After each power-on, let the FM-1 run 30 s before you power it off: the firmware's boot guard counts a restart in
the first 30 s as a failed start, and two in a row send it to the built-in bootloader (step 7).

## 5. Hands-on
For each: what you should see / hear. Note anything different (page, knob, what happened).
- Every page: SOUND 1/3..3/3 (EDIT: MODEL, the engine's knobs, LVL PAN NOTE CHOKE), LAYER 1/2 and 2/2 (EDIT,
  then OCT+; EDIT blinks; EDIT steps, OCT- back), FX, SLICER, DLY,
  REVERB, CHORUS, COMP (HOME 2/3 and 3/3: SELECT right on the HOME screen), STEP, PATTERN, GLOBAL 1/3, SYSTEM,
  GLOBAL 3/3 (MUTE NOW / BAR, METRO), LFO 1/4..3/4 and DEST 4/4,
  PROJECT, TOOLS, TRACKS (HOME's first mode), the MUTE screen (GLO held) — each draws like the screenshots in
  `build/ui_shots/` (the engines' SOUND pages in `build/ui_shots/engines/`, the sequencer in `build/ui_shots/seq/`,
  the new screens in `build/ui_shots/fixes/`).
- Every engine: KNOB 1 (MODEL) on EDIT's first page steps through the 21 engines; each plays on its key.
- SELECT knob, on any screen but TRACKS: the section's next / previous page (HOME 1/3 <-> COMP 2/3 <-> 3/3, FX <->
  SLICER <-> RESON <-> DLY ..., the SOUND pages, the LAYER pages in the layer); it stops at the first / last page.
  In the menu it moves the cursor. On TRACKS it sets the BPM (the top bar follows; with CLK USB it says `CLK USB`).
  The PRESETS knob does nothing anywhere; ALGORITHM picks the track.
- HOME: a HOME tap on TRACKS opens the HOME screen, a HOME tap there TRACKS again; from EDIT, SEQ, GLOBAL ... a HOME
  tap opens the one last shown; on COMP, the HOME screen. Holding HOME still opens the menu.
- The HOME screen: a white key plays its track and selects it (the knobs and T<n> follow). An LFO on one of the HOME
  knobs shows its live value as a small mark under that gauge, as on EDIT.
- The demo kits: the kits in `tests/drumsim.c` (`KITS`) use other engines than the power-on kit. First set each
  track's engine to the kit's (e.g. kit_808: K808 S808 C808 HATC HATO TOM COWB CYMB, with MODEL (KNOB 1) on
  EDIT), then enter its pattern (one line of x / X per track) on the STEP grid and press PLAY at 120 BPM:
  it sounds like `build/drum_renders/kit_808.wav`.
- Live recording: on TRACKS a REC tap arms the selected track (`T3 REC ARMED`); on any other page (HOME, EDIT, FX,
  SEQ, GLOBAL ...) it is REC ALL (every track armed). The REC LED lights; play does not start: press PLAY; the page
  stays. Play the keys while running, on TRACKS or any page where the keys play: the steps appear. A REC tap again
  disarms. REC never opens TRACKS.
- PROJECT: save to slot A, change things, load slot A: everything comes back.
- The master knob changes the volume; the battery icon shows a level.

## 6. Going back to stock (any time USB answers: a normal start or SAFE START)
M-VAVE's updater, as in step 1 (you did this from Felucca in step 2; the drum firmware keeps the same update
code). Expected: stock firmware. If it fails or you cancel it in SAFE START, the SAFE START screen comes back:
try again.

## 7. If the drum firmware crashes, or the screen stays blank
1. A screen titled **FELUCCA CRASH**, the FM-1 restarting by itself, or a blank screen: power off. Hold **SEQ**
   and power on (step 4.4). When it shows **SAFE START**, go back to stock from there (step 6). The safe start
   runs no drum sound code, so a crash in the drum engines, the mix or the sequencer cannot happen there.
2. If SAFE START does not appear: after two crashes within 30 s of power-on the firmware enters the FM-1's
   built-in bootloader on purpose (a blank screen, USB not answering). Power off, wait a few seconds, and try 1
   once more.
3. If that fails too, the FM-1 is safe but needs one of:
- an RP2040 board (Seeed XIAO RP2040) with FM-1-transporter: https://github.com/kurogedelic/FM-1-transporter
  (3 wires to the FM-1's USB D+ / D− / GND, no opening); it writes the stock firmware back;
- a Linux PC with jl-uboot-tool over the FM-1's own USB cable.
Report what you saw first; we'll go through it together.

### M2: PROB, RATCH, Grids (check on the FM-1)

- STEP grid: hold a step's key and turn KNOB 1: the hint line shows `STEP n  75%` (left of 100 %) or `1-SHOT`,
  `1/2` .. `8/8` (right); KNOB 2: `RATCH 2..4`. A step with PROB is drawn striped, RATCH as ticks above it.
- PLAY: a 50 % hat varies, a 1/2 step plays every other loop, a 1-SHOT crash only once after PLAY, a RATCH 3
  step rolls three hits.
- ARP: GRIDS 1/2 (MODE X Y CHAOS; MODE EUCL: LEN K S H) and 2/2 (FIL K S H, routing line). PATTERN KNOB 4 SRC
  `G-KCK` / `G-SNR` / `G-HAT` makes a track follow Grids; its STEP grid shows the pattern, keys do nothing.
- Every SEQ page (STEP, PATTERN, MOTION, SONG): the white keys are the selected track's steps (tap: on / off, OCT+
  held + tap: accent), OCT+ (let go alone) / OCT- the bank; the footer strip shows them; ALGO picks the track (no sound).
- TRACKS (HOME's first mode): the white keys are the selected track's steps (as on STEP: tap on / off, OCT+ held +
  tap: accent; OCT+ / OCT- the bank; no sound); KNOB 1 or ALGO picks the track. With a track armed (REC tap on TRACKS) the white
  keys play their tracks instead and record into the armed one, the selection stays.
- The MUTE screen: hold GLO (0.4 s, or GLO + a key at once): eight boxes, filled = heard, outline = muted, the keys
  F3 .. F4 under them; the 8 white keys lit for the heard tracks. A white key mutes / unmutes its track (a muted
  track stops at once) and plays nothing; nothing is recorded. Let go of GLO: the page before, no GLOBAL page. A
  quick GLO tap still opens GLOBAL.
- Mute on the next bar: on the MUTE screen the top D# (POLY): `MUTE: NEXT BAR`; while playing, a mute waits for the
  next bar (its box and key blink) and lands on the downbeat. The top C# (MONO): `MUTE: NOW`. The same setting is
  GLOBAL 3/3 MUTE NOW / BAR; it survives a power cycle.
- CPU: a dense pattern (Grids on 3 tracks, the rest with RATCH 4 and PROB, FX on) at 240 BPM: GLOBAL -> SYSTEM CPU
  stays well under 100 % and the sound does not crackle.
- SAVE a project, power off and on, LOAD: PROB / RATCH / SRC / GRIDS come back. A project saved with the M1
  firmware loads with plain steps.

### M3: COMP sidechain (check on the FM-1)

- SELECT right on the HOME screen: COMP (HOME 2/3: SRC THRSH RATIO REL), again: 3/3 (ATK KNEE MKUP GHOST, the
  curve); SELECT left back to HOME 1/3 (or a HOME tap). FX no longer has the COMP pages. SRC T1 with a kick pattern; on a COMP page the
  white keys 2-8 light for the ducked tracks (key 1, the source, does not toggle). PLAY: the ducked tracks pump
  with the kick, the GR meter moves; REL longer = slower recovery; RATIO higher = deeper duck; MKUP raises the
  ducked tracks (at the end LIMIT).
- Mute T1 (GLO held + F3): the kick is silent, the others still pump (ghost key, GHOST KEEP).
- SRC OFF: the mix sounds exactly as before M3.
- SAVE / power cycle / LOAD: SRC, the COMP knobs and DUCK come back; an M2 project loads with COMP off.

### COMP GHOST (check on the FM-1)

- HOME 3/3, KNOB 4 GHOST: KEEP at start; MUTE, KEEP, HIDE. The COMP picture shows the source HEARD / GHOST / MUTED
  next to its IN level.
- KEEP: T1 muted (GLO held + F3) is silent and the others pump (as before), GHOST shown.
- MUTE: T1 muted is silent and nothing pumps (MUTED); unmuted it is heard and pumps (HEARD).
- HIDE: T1 is never heard, muted or not, and the others pump (GHOST).
- Turning GHOST while the kick plays: no click. Mute on the next bar (GLOBAL 3/3 MUTE NEXT BAR) in each mode:
  the kick changes on the bar, no click.
- SAVE / power cycle / LOAD: GHOST comes back; a project saved before (FDR5) loads with KEEP.

### USB-MIDI (stage 2 step 2: check on the FM-1)

- Notes from a DAW over USB on the drum channel: every hit plays; a dense burst (e.g. a 1/64 roll on several notes,
  or a pasted block of notes): every hit plays, nothing stuck, the FM-1 stays responsive.
- **Run the web installer again with the same package after installing**: it must find the FM-1 and hand over to
  the update loader (the M-UPGRADE command now goes through the new driver). The pinned loader and the stock UBOOT
  path stay the way back either way.
- The installer finds the FM-1 and reads its identity while a DAW keeps sending notes / clock (its SysEx frames go
  through the new driver).
- Safe start (SEQ held at power-on) with a DAW sending notes or clock: the installer still finds the FM-1.
- Unplug / replug while notes are sent: MIDI works again after the replug.

### Storage (stage 2 step 3: check on the FM-1)

- Play a pattern with long REVERB / DELAY tails, press STOP and SAVE the project at once (tails still sounding):
  no buzz or looped grain during the save. (While playing, SAVE still shows STOP TO SAVE; a setting changed
  while playing is written at STOP: no buzz then either.)
- Change a setting (palette or MUTE NEXT BAR) so the settings are saved: no buzz.
- Power off and on: the last project saved loads by itself (`LOADED`), the settings and the panel layout (OCT- +
  OCT+ at power-on) are kept; a project saved with the previous build still loads.

### TRS MIDI (stage 2 step 4: check on the FM-1)

- A keyboard or sequencer on the TRS MIDI input sending on the drum channel (GLOBAL page, CH; 10 by default):
  every note plays the tracks whose NOTE matches, as over USB; a keyboard's note-off plays nothing.
- USB MIDI from a DAW at the same time: both play.
- Nothing plugged into TRS (also plugging / unplugging the cable while idle): no stray hits. Optional: right after
  power-on with nothing ever plugged in, console `status` shows `uart_enabled 1` and `uart_rx_bytes 0`; with the
  cable out it does not grow.
- If a TRS keyboard plays nothing: check console `status` while you play. If `uart_rx_bytes` stays 0, try the other
  TRS adapter type (A / B); if it grows but nothing plays, check the channel (GLOBAL page, CH).
- A MIDI clock on TRS: nothing follows it yet (the MIDI clock feature comes later); notes still play while it runs,
  the panel and audio behave as before.

### Sound pack (check on the FM-1)

- MENU (HOME held) > SPEAKER EQ: FLAT, LOWCUT, BASS+ (KNOB 1; OCT+ steps round). On the speaker, BASS+ makes the
  kick's bass audible without the mids getting thinner; the choice survives a power cycle.
- FX > REVERB: TYPE SPRNG (the spring) on a snare (send on the FX page, REV): the spring's chirp and drip; SIZE and
  DAMP change it; switching TYPE while it rings: no click. FX > CHORUS: RATE and DEPTH as before.
- PATTERN DIV on a hi-hat track: the knob runs 4BAR 2BAR 1/1 1/2 1/4 … 1/32; at 2BAR / 4BAR the hat plays once per
  2 / 4 bars, in time with a 1/16 kick. DLY TIME at 1/2 and slower: long echoes (the longest cut to 1.49 s).
- A project saved before this build loads with ROOM and sounds as before. With the heaviest kit and SPRNG, the
  CPU meter stays close to where it was.
- Listen first on the computer: build/drum_renders/sp_speaker_eq.wav (FLAT, LOWCUT, BASS+, 2 bars each),
  sp_reverb.wav (ROOM, then SPRNG at SIZE 0 / 64 / 127, DAMP 0 / 127), sp_slow_div.wav (a hat on 2BAR then 4BAR,
  the snare's delay at 1/2).

### USB audio (check on the FM-1)

- Audio MIDI Setup (Mac) lists an FM-1 audio input: 2 channels, 44.1 kHz. MIDI ports as before.
- Record 5 minutes of a heavy kit (8 tracks, RESON, SPRNG, the compressor) into a DAW: no clicks or dropouts in the
  recording; the speaker does not stutter; the CPU meter close to before. Optional: console `status` after it:
  `uac_underruns 0` and `uac_missed 0`; note `uac_fill_lo` (the buffer's lowest fill: near 50 or below would mean
  the band upstream tuned for shorter renders is tight here). `uac_overruns` may count when the DAW stops reading
  without closing the input: harmless; what matters is no clicks while it records.
- MIDI from the DAW plays while it records. The next firmware install finds the FM-1 as usual.
- MENU > USB LEVEL: MASTER: turning MASTER down lowers the recording; FIXED: the recording stays at full level, only
  the speaker / headphones follow the knob.
- With nothing recording (DAW closed or another input chosen), the CPU meter is as without USB audio.

### USB AUDIO 10 CH (check on the FM-1)

- MENU > USB AUDIO shows 2 CH after the install (an older settings record reads 2 CH); Audio MIDI Setup lists
  **Hortator**, 2 channels, as before.
- Set 10 CH MIX and close the menu (OCT-) while a pattern plays: `USB RECONNECT: 10 CH`; the sound and the steps go on;
  about a second later Audio MIDI Setup lists **Hortator 8ch**, 10 channels, 44.1 kHz.
- In a DAW / AUM: 10 inputs (named Main L, Main R, T1 .. T8 where the host shows names). Record T1 .. T8 on eight
  mono tracks and the master on a stereo one for 5 minutes of a heavy kit: no tears or dropouts; each track on its
  own channel, nothing of the others in it; with the pans centred and no sends, the eight summed sound like the master.
  Optional: console `status`: `usb_audio_10ch 1`, `uac8_underruns 0`, `uac8_missed 0` (or a few), `uac8_rate_hz`
  near 44100.
- MIDI from the DAW plays after the reconnect (choose the Hortator 8ch port if the DAW lists it as new). CLK USB: the
  steps pause with the reconnect and go on with the DAW's clock; without it they stop 2 s after.
- Power off and on: 10 CH is kept (Hortator 8ch). SAFE START (SEQ held): **Hortator**, 2 channels, and the installer
  finds it there. The installer and the editor also find Hortator 8ch.
- 10 CH FX (KNOB 1 once more, no reconnect: the DAW keeps recording): channels 1-2 now only the echoes and the
  reverb (nothing with every send at 0). T1 .. T8 panned like the FM-1 (DAW pan law 0 dB) + the FX pair sound like
  the master (not PERFORM's master effects, METRO, SPEAKER EQ or the limiter). Switching MIX <-> FX while playing:
  no click on channels 1-2. Power off and on: 10 CH FX kept.
- Back to 2 CH, close the menu: Hortator again; the CPU meter as before 10 CH. Windows: note whether it accepts 10
  CH (it may not).

### MIDI clock (check on the FM-1)

- GLOBAL > CLK USB, a DAW sending MIDI clock to the FM-1: the DAW's Start / Stop / Continue start, stop and resume
  the FM-1 (step 0 on the downbeat); a 1/16 hat stays tight against the DAW's metronome for 5+ minutes; a tempo
  change in the DAW is followed within a beat; the header shows the DAW's BPM; the BPM knob says CLK USB.
- Unplug the cable while it plays: the FM-1 stops within half a second.
- CLK TRS with a hardware sequencer on the TRS input: the same.
- CLK INT: as before; a 1/16 track and a 1/4 track never drift apart.
- PLAY on the FM-1 while CLK USB: it waits for the DAW's clock (stops again after 0.5 s if none comes).

### PERFORM (check on the FM-1)
- MENU > PERFORM PAGE: hold FX, let go: the PERFORM screen stays; the keys and knobs work without FX; PLAY / OCT±
  keep it; an FX tap, another page button or HOME leaves it (the knob effects stay on). Set it back to HOLD after.
- Hold FX alone: after a moment the map shows (two rows of effects, eight tracks); let go: back to the page.
- Tap FX: the FX pages, as before.
- Playing, FX held: each black key's effect while held — F#3 G#3 A#3 REPEAT 1/8 1/16 1/32 (starting on the next
  1/16), C#4 REVERSE, D#4 TAPE STOP, F#4 LPF, G#4 HPF (sweeping over a bar), A#4 FREEZE, C#5 OCT UP, D#5 OCT DN;
  let go: back to the dry sound without a click. Two buffer effects held: the last pressed plays, letting it go
  returns to the other.
- FX + a white track key (F3 .. F4): that track silent while held; no note plays, nothing is recorded.
- FX + KNOB 1..4: FILTER (left LPF, right HPF), CRUSH, THROW (into the delay / reverb), DEPTH (SHIMMER while OCT
  plays); let go of FX: they stay on and keep working on every page (amber `FX` in the top bar); back to 0 on the
  PERFORM screen: off, the `FX` mark goes.
- A STUT slicer track keeps playing while a buffer effect plays and stutters again afterwards.
- At 40 BPM REPEAT 1/8 and REVERSE are dimmed and do nothing.
- With CLK USB from a DAW: REPEAT starts on the DAW's 1/16 grid.

### MOTION (check on the FM-1)
- Select a track and arm it (a REC tap); play; open SOUND and sweep DECAY over a bar: the sweep comes back every
  bar. STOP: the knob back where it was before.
- SEQ > MOTION: KNOB 1 PLAY OFF (the sweep stops, the events kept) / ON; MOTION shows how many; KNOB 4 CLEAR asks
  (OCT- keeps, OCT+ clears).
- Turn DECAY without REC while it plays: that is the new value the loop comes back to.
- Save the project (stopped), power off and on, load it: the motion plays again.

### SONG / NAME (check on the FM-1)
- SAVE > PROJECT, slot A, SAVE twice: saved at once (USED). KNOB 2 NAME: the NAME screen opens ("PROJECT A"). Type
  a name on the keys (a white key again within 0.8 s: its next letter; F# / A# move, G# space, C# deletes, D#
  digits); KNOB 1 / 2 move and change a character; OCT+ writes it, OCT- cancels. The keys make no sound while it is
  open. PROJECT shows the name next to A; SAVE again keeps it.
- Make another pattern, save it to B and name it the same way.
- SEQ > SONG: KNOB 2 on the `+` row adds a row; rows A x2, B x1; KNOB 4 LOOP ON. PLAY: A twice, then B, then A
  again; the rows change on the bar, the header shows SONG and the row, the playing row "n LEFT".
- LOOP OFF: it stops after B. STOP during a song: the current pattern (LEN etc.) is as before.
- In a slot, a 16-step kick with a 64-step hats track: the row lasts the hats' 4 bars.
- While a song plays: STEP / PATTERN edits say STOP TO EDIT; LOAD says STOP TO LOAD. REC held on a SONG row:
  DELETE ROW; on the `+` row: CLEAR SONG.
- Save, power off and on, load: the song and the name are there.

### PHYS (check on the FM-1)
- A track to SKIN (SOUND, MODEL after HISS): TUNE / DECAY / TONE / HEAD on HOME. HEAD from a tom (0) to a tabla
  (127); BEND drops the pitch after each hit; POS from the centre (round) to the rim; STICK the click of the strike.
  Hit it fast: the head rings on, no clicks.
- RESON MODAL (after CHORD) on a kick, a snare, a clap: STRCT from harmonic to bell, DECAY the ring time (as STRNG),
  TONE, POS. A third MODAL track is refused ("MODAL: 2 TRACKS MAX"); with CHORD full the MODEL knob skips to MODAL.
- Four SKIN tracks and two MODAL RESONs playing: no dropouts (CPU on SYSTEM INFO).

### TOOLS (check on the FM-1)

- SAVE twice: TOOLS `CLRSQ` `INIT` (the selected track) and `CLR*` `INIT*` (everything). Each acts on a second
  detent within ~1.5 s ("AGAIN: ..." first). `CLR*`: every pattern empty, LEN 16 / DIV 1/16, sounds unchanged.
  `INIT*`: the power-on kit and settings, playback stopped; saved projects still load.

### LFOs (check on the FM-1)

- LFO (since the DEST slots, below): both LFOs on every page, LFO 1 on KNOB 1-2, LFO 2 on KNOB 3-4: 1/4 WAVE RATE,
  2/4 MORPH PHASE, 3/4 TRIG MODE, then DEST 4/4. OCT+ switches the LFO turned last between SYNC / Hz / TIME (its RATE
  value and the mode over its picture change). On DEST set slot 1's DEST to TONE and its DEPTH: LFO 1's dot moves,
  the sound changes; on EDIT the TONE gauge shows a second moving mark.
- S&H, WANDR, RWALK: that LFO's picture is a scope, the dot at the right edge of its half, the trail behind it (one
  cycle wide); it starts over on another track or wave.
- Each WAVE with MORPH turned: SQUAR softer, SAW bent, SINE squarer, TRI from falling to rising, S&H from steps to
  glides; TRIG HIT restarts on each hit, PLAY at PLAY.
- SAVE / power cycle / LOAD: the LFOs come back; an older project loads with the LFOs off.


### RESON (check on the FM-1)

- FX: after SLICER, RESON 1/2 (MODEL TUNE DECAY MIX) and 2/2 (TONE STRCT POS). MODEL STRNG on the snare: each
  hit rings at TUNE (C3 = a low string); DECAY longer = longer ring; MIX 100 % = only the ring; TONE darker =
  muted; STRCT = a stretched, bell-ish ring; POS = a hollower / fuller ring.
- PIPE: a hollow, odd-harmonic tube. CHORD: four strings; on CHORD the second knob of RESON 2/2 is CHORD (OCT …
  CLUST), the picture shows the notes. A third track cannot take CHORD ("CHORD: 2 TRACKS MAX"); a fifth track
  cannot take RESON at all ("RESON: 4 TRACKS MAX").
- LFO DEST R.TUN: the ring's pitch sweeps smoothly; R.DCY / R.MIX / R.TON / R.STR / R.POS move those knobs.
- Mute a ringing track: the ring stops at once without a click. MODEL OFF: the track sounds as before.
- CPU: RESON on 4 tracks (2 CHORD) with a dense pattern: GLOBAL -> SYSTEM CPU under 100 %, no crackle (note the
  CPU % you see: the stress case on the host is above the stock reference, by decision).
- SAVE / power cycle / LOAD: RESON comes back; an older project loads with RESON OFF.
- KEYF (KEYFOLLOW, RESON 2/2 KNOB 4; the screen: `build/ui_shots/reson/04_keyf.png`): STRNG on the snare, TUNE C3,
  KEYF 100 %; steps at C4, C5, G4 (STEP: the step's NOTE): the ring plays C3, C4, G3 (the melody of
  the steps). KEYF 50 %: half the intervals. KEYS on the track: the ring follows the keys. A fast run of notes with a
  long DECAY: each new note slides into place without a click. KEYF 0 %: the ring stays at TUNE (as before).
- A project saved before KEYF loads with KEYF 0 % on every track and sounds as before; KEYF set, SAVE, power
  cycle, LOAD: it comes back. MIDI LEARN on KEYF names it `T1 R.KEYF`.

### Panel (stage 2 step 1: check on the FM-1)

- LEDs: with a dense pattern playing (8 tracks, RESON / FX on), no flicker on the key / button LEDs.
- Knobs: turning slowly or fast, no skipped steps and no double steps; a knob left half-way into a click for a
  few seconds, then turned, still moves one step per click; a slow turn with short pauses keeps moving.
- Pads: presses respond at once; a held pad does not retrigger; fast repeats all play.
- Safe start (SEQ held at power-on) still works; USB and the installer still work as before (the update loader is
  unchanged).

### FILTER (check on the FM-1)

- FX opens on FILTER 1/2 (TYPE CUT RESO ENV), FILTER 2/2 has DECAY; the graph shows each TYPE's curve, the ENV
  line where the hit sweeps to.
- On a snare with a pattern: LP darkens as CUT goes down, HP thins it, BP narrows it, NOT takes out a band; RESO
  high whistles at the cutoff; turning CUT with RESO high sweeps without clicks.
- A kick with LP, CUT ~60, ENV -40, DECAY ~50: each hit thumps darker, then opens; accented steps further.
- LFO 1 DEST F.CUT on a clap: the brightness wobbles; F.RES moves the resonance.
- A project saved before this firmware loads unchanged (FILTER OFF everywhere); SAVE / power cycle / LOAD keeps the
  FILTER settings.
- `tools/fm1_bench.py --yes`: the five new cases (heavy+filter, heavy+comp, heavy-fx+comp, heavy+sidechain,
  heavy-fx+sidechain).

### FM (check on the FM-1)

- A track to MODEL FM: SOUND 1/4 MODEL TUNE DECAY INDEX, 2/4 RATIO MDEC SWEEP FBK, 3/4 VEL LVL PAN NOTE, 4/4
  CHOKE; the graph shows RATIO and
  the carrier with its sidebands (they move with RATIO and INDEX).
- The five sounds of the WAVs (TUNE DECAY INDEX RATIO MDEC SWEEP FBK VEL; RATIO as shown):
  bell 19 110 70 3.5 90 0 0 64; metallic tom -10 80 50 1.41 30 20 20 64; woodblock 12 25 40 2.76 10 0 0 64;
  zap 12 50 90 1 25 110 0 64; FM kick -24 70 30 1 15 60 0 64.
- RATIO's inharmonic steps (1.41 2.76 5.4 9.2) ring like metal; INDEX 0 is a pure sine; SWEEP drops the pitch
  (zaps, toms); FBK adds grit up to noise; accented steps are brighter (VEL).
- `tools/fm1_bench.py --yes`: the cases FM and FM+fx (8 tracks of the model).

### BDTRX (check on the FM-1)

- The MODEL list after ACID: BDTRX SDTRX TMTRX CGTRX CPTRX RSTRX CBTRX CHTRX OHTRX CYTRX SHTRX CLTRX B2TRX (B2TRX
  last).
- A track to MODEL BDTRX (after ACID): SOUND 1/4 MODEL TUNE DECAY SWEEP, 2/4 SDEC PUNCH NOISE SUB,
  3/4 CLIP LVL PAN NOTE; the graph shows the pitch (falling from SWEEP over SDEC) and the level over 300 ms
  (`build/ui_shots/ktrx/sound_1.png`, `sweep127.png`).
- By ear against the A/B files (`~/Desktop/hortator-md/ktrx-ab`: the emulator, then BDTRX): the defaults a round
  61 Hz kick with a short click; SWEEP up a deeper zap (SDEC longer); PUNCH a harder first cycle; NOISE a short hiss;
  SUB a growl an octave down that fades; CLIP above 64 a buzzy square that gets quieter towards 127.
- The demo WAVs (`build/drum_renders/ktrx_*.wav`; TUNE DECAY SWEEP SDEC PUNCH NOISE SUB CLIP): default 0 64 64 32 0 0 0
  0; boom_sub -5 100 40 90 0 0 90 0; punch_noise 3 50 80 20 127 70 0 0; zap 7 60 127 50 0 0 0 0; clip 0 70 60 30 60
  40 0 120.
- `tools/fm1_bench.py --yes`: the cases BDTRX and BDTRX+fx (8 tracks of the model).

### SDTRX (check on the FM-1)

- A track to MODEL SDTRX: SOUND 1/4 MODEL TUNE DECAY SNAP, 2/4 TONE BUMP BENV CLIP, 3/4 - LVL PAN NOTE.
- By ear against the A/B files (`~/Desktop/hortator-md/sdtrx-ab`: the emulator, then SDTRX): the defaults a tight
  snare (a 342 Hz body with a metallic overtone, a bright noise burst); TONE towards 127 more of the metallic osc B;
  BENV changes that overtone erratically (low at 40 and 120, very high at most values: the machine's own table) and
  stretches BUMP's pitch blip; SNAP louder noise, above 64 a ringing 1.4 kHz part; CLIP above ~56 a square-ish body
  that cleans up within ~250 ms whatever DECAY is. Every hit has the same noise (as the machine's).
- The demo WAVs (`build/drum_renders/sdtrx_*.wav`; TUNE DECAY SNAP TONE BUMP BENV CLIP): default 0 64 64 64 0 64 0;
  ring -3 100 40 100 60 40 0; crack 2 40 127 30 90 0 0; clip 0 64 80 64 0 64 120.
- `tools/fm1_bench.py --yes`: the cases SDTRX and SDTRX+fx (8 tracks of the model).

### B2TRX (check on the FM-1)

- A track to MODEL B2TRX (the last model): SOUND 1/4 MODEL TUNE DECAY RAMP, 2/4 HOLD TICK NOISE DIRT,
  3/4 DIST LVL PAN NOTE.
- By ear against the A/B files (`~/Desktop/hortator-md/b2trx-ab`: the emulator, then B2TRX; the emulator's sine runs
  free, B2TRX's starts at 0, so the first ~10 ms differ hit to hit there, not here): the defaults a plain 50 Hz sine
  kick; RAMP a pitch drop from up to ~470 Hz; HOLD a held tone (seconds at the top; the voice still ends within 6 s:
  a long DECAY shortens it); TICK a click pinned by the output's saturation; NOISE a short hiss; DIRT a bit-crushed
  stair (3 levels from 104 up); DIST a square from ~56 up.
- The demo WAVs (`build/drum_renders/b2trx_*.wav`; TUNE DECAY RAMP HOLD TICK NOISE DIRT DIST): default 0 64 0 0 0 0
  0 0; boom -5 90 60 80 40 0 0 0; dirt 0 70 40 0 60 40 104 0; dist 3 64 90 20 80 0 0 64.
- `tools/fm1_bench.py --yes`: the cases B2TRX and B2TRX+fx (8 tracks of the model).

### TMTRX, CGTRX, CLTRX, RSTRX (check on the FM-1)

- A track to MODEL TMTRX, CGTRX, CLTRX, RSTRX: TMTRX / CGTRX SOUND 1/4 MODEL TUNE DECAY
  RAMP, 2/4 RDEC DAMP DIST DTYP; CLTRX 1/4 MODEL TUNE DECAY DUAL, 2/4 ENH TUNE2 CLICK; RSTRX 1/4 MODEL TUNE DECAY
  DIST.
- By ear against the A/B files (`~/Desktop/hortator-md/trx-xt-ab`, `trx-xc-ab`, `trx-cl-ab`, `trx-rs-ab`: the
  emulator, then the model): TMTRX's defaults a round 145 Hz tom with a soft click, CGTRX a shorter 173 Hz conga;
  RAMP a pitch drop at the start (RDEC slower); DAMP a duller body, a louder click; DIST a buzzier, quieter tone that
  clears as it decays, DTYP (with DIST) hollower. CLTRX a short 2 kHz click-tone; TUNE2 away from 64 a second
  pitch; DUAL a flam; ENH louder; CLICK a tick at the start. RSTRX a 258 Hz knock with a woody burst; DIST grittier,
  most at low TUNE. A step's note moves the pitch by semitones.
- The demo WAVs (`build/drum_renders/xttrx_*.wav`, `xctrx_*`, `cltrx_*`, `rstrx_*`; the knobs in order): xttrx
  default 0 64 0 64 0 0 0; xttrx_fill the same, notes 7 .. -9; xttrx_ramp -5 90 110 40 0 0 0; xttrx_dist / xctrx_dist
  0 80 30 64 40 110 127; xctrx_default the defaults; cltrx_default 0 64 0 0 64 0; cltrx_dual 0 80 64 90 100 127;
  rstrx_default 0 64 0; rstrx_dist -5 90 127.
- `tools/fm1_bench.py --yes`: the cases TMTRX, CGTRX, CLTRX, RSTRX and their +fx (8 tracks of the model).

### CPTRX, SHTRX (check on the FM-1)

- A track to MODEL CPTRX: SOUND 1/4 MODEL TUNE DECAY CLPY, 2/4 HARD RATE ROOM RSIZ, 3/4 RTUN LVL PAN
  NOTE. MODEL SHTRX: 1/4 MODEL TUNE DECAY ATT, 2/4 REV DAMP RATL TONE, 3/4 HARD LVL PAN NOTE.
- By ear against the A/B files (`~/Desktop/hortator-md/cptrx-ab`, `matrx-ab`: the emulator, then the model; the
  machine's knobs: CP TONE t is TUNE (t - 64) x 12 / 50, MA SUS is DECAY, its RTYP left at 0). CPTRX: the defaults a
  tight 4-clap; RATE spreads the claps (merged below ~16); DECAY longer claps up to a wash; CLPY louder first claps;
  HARD a ringing, louder clap; TUNE brighter / darker; ROOM 0 dry, up a short resonant tail (RSIZ longer, RTUN
  brighter). SHTRX: the defaults a 0.1 s shake; DECAY longer; ATT a swell in; REV a dip and a second shake; DAMP
  sparser grains; RATL a growing rattle; TONE and TUNE brighter.
- The demo WAVs (`build/drum_renders/cptrx_*.wav`, TUNE DECAY CLPY HARD RATE ROOM RSIZ RTUN: default 0 64 64 64 64 0
  64 64; room -3 70 90 80 56 110 100 60; snap 6 20 40 100 30 0 64 64; wide -6 110 127 40 110 60 80 40.
  `matrx_*.wav`, TUNE DECAY ATT REV DAMP RATL TONE HARD: default 0 64 0 0 0 0 64 64; shaker 0 30 0 0 60 0 90 80; swell
  -2 90 70 60 30 90 50 64; rattle 3 127 10 0 100 127 40 30).
- `tools/fm1_bench.py --yes`: the cases CPTRX, CPTRX+fx, SHTRX and SHTRX+fx (8 tracks of the model).

### CBTRX, CHTRX, OHTRX, CYTRX (check on the FM-1)

- MODEL CBTRX, CHTRX, OHTRX, CYTRX. CBTRX SOUND 1/4 MODEL TUNE DECAY ENH, 2/4 DAMP TONE BUMP PTCH;
  CHTRX / OHTRX 1/4 MODEL TUNE DECAY GAP, 2/4 HPF LPF MTAL; CYTRX 1/4 MODEL TUNE DECAY RICH, 2/4 TOP TTUN SIZE PEAK.
- By ear against the A/B files (`~/Desktop/hortator-md/trx-cb-ab`, `trx-ch-ab`, `trx-oh-ab`, `trx-cy-ab`: the
  emulator, then the float model; the voices here follow the float models): CBTRX's defaults a short bright cowbell;
  ENH a ringing, beating bell; DAMP a pure tone (B alone); TONE the low or the high tone; BUMP a slight pitch blip.
  CHTRX a tight metallic hat (MTAL 0 pure noise, 127 pure metal), DECAY its body, GAP its tail; OHTRX the same, long:
  DECAY above ~82 fades out, dips to silence and swells back (the machine does that). CYTRX a cymbal: RICH from thin
  to dense, PEAK a ringing 5 kHz bell, SIZE smaller (brighter) up, TOP / TTUN the bright top that fades first.
- Choke: with CHOKE 1 on both tracks (EDIT, the last SOUND page; no voice starts in a choke group any more), a closed
  hat cuts a ringing open hat.
- TUNE and a step's note move the oscillators by semitones (TUNE 0: the measured pitches).
- The demo WAVs (`build/drum_renders/`; knobs in page order): cbtrx_default 0 64 0 0 64 0 64; cbtrx_enh 0 80 64 0 90
  40 64; cbtrx_damp -5 50 0 100 64 0 64; chtrx_default 0 64 64 0 127 64; chtrx_metal 0 30 40 64 100 127; chtrx_noise 0
  20 30 100 127 0; ohtrx_default 0 64 64 0 127 64; ohtrx_dip 0 127 64 0 127 100; cytrx_default 0 64 64 64 64 64 0;
  cytrx_peak 0 100 100 90 80 64 100; cytrx_small -3 40 127 127 127 20 30; trx_hats_choke (CHTRX on track 1 cut by,
  and cutting, OHTRX on track 2; both CHOKE 1).
- `tools/fm1_bench.py --yes`: the cases CBTRX, CHTRX, OHTRX, CYTRX and their +fx (8 tracks of the model).

### BDEFM, TMEFM (check on the FM-1)

- MODEL BDEFM, TMEFM (after B2TRX): SOUND 1/4 MODEL TUNE DECAY RAMP, 2/4 RDEC MOD MFRQ MDEC, 3/4 MFB (TMEFM: CLIC)
  LVL PAN NOTE.
- By ear against the A/B files (`~/Desktop/hortator-md/efm-bd-ab`, `efm-xt-ab`: the emulator, then the model):
  BDEFM's defaults a thump whose pitch rises from 0 Hz to 103 Hz with a bright FM edge; RAMP 16 a steady pitch, above
  it a drop (RDEC slower); MOD brighter, MFRQ moves the overtones, MDEC keeps them longer; MFB grittier, noise-like
  from ~40 (the machine's own chaos: the character, not the same noise). TMEFM's defaults a quieter rising tom with
  grit; MOD 0 a clean tom (exact); CLIC towards 127 a click at the hit. TMEFM's FM (MOD above 0) is an approximation:
  the machine's FM tom is not understood, the model gives its kind of grit, not its tone. A step's note moves the
  pitch by semitones.
- The demo WAVs (`build/drum_renders/bdefm_*.wav`, `xtefm_*`; TUNE DECAY RAMP RDEC MOD MFRQ MDEC MFB / CLIC):
  bdefm_default 0 64 0 64 64 64 64 0; boom -12 100 48 40 24 16 30 0; zap -5 80 127 30 64 32 40 0; grit 0 70 32 50 80
  40 50 96. xtefm_default the same defaults; clean_fill 0 90 32 50 0 64 64 64, notes 7 .. -9; grit 3 80 64 40 40 40
  60 127.
- `tools/fm1_bench.py --yes`: the cases BDEFM, TMEFM and their +fx (8 tracks of the model).

### SDEFM, CPEFM, RSEFM (check on the FM-1)

- The MODEL list after B2TRX: SDEFM CPEFM RSEFM. SDEFM SOUND 1/4 MODEL TUNE DECAY NOIS, 2/4 NDEC MOD MFRQ MDEC,
  3/4 HPF LVL PAN NOTE; CPEFM 1/4 MODEL TUNE DECAY CLPS, 2/4 CDEC MOD MFRQ MDEC, 3/4 HPF; RSEFM 1/4 MODEL TUNE DECAY
  MOD, 2/4 HPF SNAR SPTC SDEC, 3/4 SMOD.
- By ear against the A/B files (`~/Desktop/hortator-md/efm-sd-ab`, `efm-cp-ab`, `efm-rs-ab`: the emulator, then the
  float model; the voices here follow the float models, their FM noise another fixed sequence of the same colour):
  SDEFM's defaults a hissy 290 Hz snare; MOD 0 a pure tone, up more hiss and spread; MDEC longer hiss; MFRQ low a
  broad bump; NOIS a separate burst (different on every hit), NDEC longer; HPF thinner. CPEFM: a buzzy 820 Hz clap,
  CLPS more claps (none below 8), CDEC slower claps; MOD 0 a pure tone. RSEFM: a 976 Hz rim with a snare-like second
  voice (SNAR 0 the rim alone; SPTC its pitch, SMOD its hiss); MOD a noisier rim. Every hit of a patch sounds the same
  (SDEFM's NOIS aside). DECAY 127 rings long (1.6 s decay) and fades out by 6 s.
- TUNE and a step's note move the pitches by semitones (TUNE 0: the measured pitches).
- The demo WAVs (`build/drum_renders/`; knobs in page order): sdefm_default 0 64 64 64 64 64 64 0; sdefm_ring -3 96
  30 50 24 32 90 0; sdefm_crush 2 50 110 80 127 100 40 24; cpefm_default 0 64 64 64 64 64 64 0; cpefm_wide -5 80 127
  100 90 16 100 0; cpefm_tone -12 50 32 40 0 64 64 0 (notes); rsefm_default 0 64 64 0 64 64 64 64; rsefm_rim 0 40 20
  16 0 64 64 64 (notes); rsefm_snare -2 50 100 32 127 40 80 127.
- `tools/fm1_bench.py --yes`: the cases SDEFM, CPEFM, RSEFM and their +fx (8 tracks of the model).

### CBEFM, HHEFM, CYEFM (check on the FM-1)

- MODEL CBEFM, HHEFM, CYEFM (after B2TRX). CBEFM SOUND 1/4 MODEL TUNE DECAY SNAP, 2/4 FB MOD MFRQ MDEC; HHEFM 1/4
  MODEL TUNE DECAY TREM, 2/4 TFRQ MOD MFRQ MDEC, 3/4 FB; CYEFM 1/4 MODEL TUNE DECAY FB, 2/4 HPF MOD MFRQ MDEC.
- By ear against the A/B files (`~/Desktop/hortator-md/efm-cb-ab`, `efm-hh-ab`, `efm-cy-ab`: the emulator, then the
  float model; the voices here follow the float models): CBEFM's defaults a bright two-tone FM cowbell (410 + 607 Hz);
  SNAP a sharper attack that drops ~14 dB in its first 40 ms; FB a buzzier, saw-like tone (chaotic noise near 127);
  MOD / MFRQ / MDEC the FM's brightness, its sidebands' spacing and how long the brightness lasts. HHEFM a buzzy FM hat
  (the carrier restarts at a random phase every 32 samples); DECAY from a tick to a long open hat; TREM / TFRQ a
  tremolo from slow swells to a fast flutter; FB first stalls the tone to a 1.4 kHz buzz, then turns to noise from ~40.
  CYEFM a dense FM cymbal; FB brighter and rougher; HPF thinner (off at 0); MOD 127 a dense, aliased wash; MFRQ the
  sideband spacing; MDEC how long the brightness lasts (up to seconds).
- Choke: HHEFM and HATO / OHTRX in the same CHOKE group (set on each track; no voice starts in one): HHEFM cuts a
  ringing open hat and is cut by it.
- TUNE and a step's note move every frequency by semitones (TUNE 0: the machines' PTCH 64).
- The demo WAVs (`build/drum_renders/`; knobs in page order): cbefm_default 0 64 0 0 64 64 64; cbefm_snap 0 96 96 0 80
  64 40; cbefm_fb -5 70 40 40 40 32 80; hhefm_default 0 64 0 64 64 64 64 0; hhefm_trem 0 110 100 100 64 64 64 0;
  hhefm_fb 0 40 0 64 32 96 64 16; cyefm_default 0 64 0 0 64 64 64; cyefm_bright 0 100 24 64 96 20 100; cyefm_short 3 40
  8 96 32 127 32; efm_hats_choke (HHEFM on track 1 cut by, and cutting, HATO on track 2; both CHOKE 1).
- `tools/fm1_bench.py --yes`: the cases CBEFM, HHEFM, CYEFM and their +fx (8 tracks of the model).

### Fixes after 0.16.0 (check on the FM-1)

- Last project: SAVE to slot B (stopped), power off and on: B loads by itself (`LOADED`, stopped; PROJECT's SLOT
  shows B). SAVE to C, power cycle: C. A safe start (SEQ held) loads nothing.
- A remembered slot that cannot load asks `LOAD FAILED: SLOT B` / `START NEW PROJECT?` (hard to cause on purpose;
  the host tests cover it, the screen is `build/ui_shots/fixes/load_failed.png`). OCT- (NO): the power-on kit, and
  the question again at the next power-on; OCT+ (YES): the power-on kit, and the next power-on is plain.
- METRO (GLOBAL 3/3): LOW / MID / HIGH while playing: a click on each quarter, higher on the bar's first, in time
  with a 1/4 kick; nothing while stopped; the MASTER knob turns it down with the mix; PERFORM's FILTER / CRUSH /
  REPEAT leave it alone; with CLK USB it follows the DAW's beats. OFF: no click. Survives a power cycle; not in
  projects. Listen first on the computer: `metro_120.wav` (2 bars each LOW, MID, HIGH at 120 BPM).
- PRIO (EDIT, the last SOUND page, after CHOKE): HIGH / LOW. A heavy kit that sheds voices (GLOBAL > SYSTEM CPU near
  100 %): the LOW tracks' voices drop out first; the top bar shows a red CPU icon right of T<n> for 2 s after each
  shed. A light kit: no icon.
- An older project (0.16.0's) loads and plays as before, every track PRIO HIGH; a project saved now cannot be loaded
  by 0.16.0.
- BACKLIGHT (MENU, HOME held; screen `build/ui_shots/fixes/menu_backlight.png`): DIM LO (was ON), on STEP with a few
  steps on: the unlit keys glow dimly, steps 1 5 9 13 (unlit) brighter, the on steps full; no visible flicker, also
  with a dense pattern playing and while turning knobs; the playhead still reads. The MUTE screen and PERFORM: unlit
  keys dim. OFF: the LEDs as before (OCT- / OCT+ breathe where they act: 2026-10-21 below). Survives a power cycle; a
  safe start keeps every LED dark.

### MIDI LEARN (check on the FM-1)

- Hold GLO, press G4: `G4 LEARN: ON` on the MUTE screen (`build/ui_shots/learn/mute_learn_on.png`), G4 lit, no
  sound. Let go: the footer says `LEARN: TURN OR PRESS`.
- On HOME, turn KNOB 2: `T1 DECAY: SEND A CC`. Move a knob on a USB MIDI controller (or a DAW's CC lane): the top bar
  says `CC.. -> T1 DECAY`. Move it: track 1's DECAY follows over its whole range, also with track 3 selected. Same from
  the TRS jack, and from a Bluetooth MIDI controller in MIDI mode (the Bluetooth build); with the SMC-Mixer in SMC DAW
  mode its knobs keep their layout.
- An LFO on that knob (DEST DECAY): it keeps modulating while the controller moves it. A centred knob (PAN): the
  controller's middle is centre.
- OCT- with the knob picked: `T1 DECAY CC CLEARED`; OCT- with nothing picked: `CLEAR ALL LEARNED?` (OCT- keeps).
  OCT+: `LEARN DONE`; the CCs still work.
- Learn while playing, stop, power off and on: the CCs still work (LEARN itself off). No audio glitch from moving a
  learned knob quickly while a dense pattern plays (`status`: `audio_late 0`).
- Actions: with LEARN on, press PLAY (nothing plays; `PLAY: SEND CC/NOTE`), hit a pad: `NOTE36 -> PLAY`. Do the same
  for REC, ALGORITHM right, FX + G#3 (`FX R1/16`) and FX + A3 (`FX MUTE T3`) with buttons, and FX + KNOB 1 with a
  knob (`FX FILTER`). OCT+: the pad starts / stops, its drum silent; the buttons arm, step the track, hold REPEAT and
  the mute while held; HOME held (the menu) ends a held REPEAT, which stays off after the menu.

### PRESETS (check on the FM-1)

The screens: `build/ui_shots/presets/`. The factory banks (151 presets; `tools/presets.json`, the header generated by
`tools/gen_presets.py`); each voice's first preset is its default sound (a fresh track, a MODEL change):

- MACHINEDRUM (55, from the Machinedrum's factory kits; SHTRX and CLTRX have none and keep their sound): BDTRX
  SUBTHUMP; SDTRX HARDSNAP POPSNARE; TMTRX SWOOPTOM FUZZTOM SHORTTOM BUZZLOW; CGTRX GRITCONG; CPTRX ROOMCLAP WIDECLAP;
  RSTRX KNOCK GRITRIM; CBTRX PUREBELL LONGBELL; CHTRX NOISEHAT METALHAT THINHAT TINHAT; OHTRX AIROPEN DARKOPEN SIZZLE
  HALFOPEN; CYTRX WASHCYM PINGBELL THINCRSH LONGRIDE; B2TRX BIGBOOM ZAPKICK TICKKICK SUBDIRT; BDEFM FMSUB NOISEZAP
  FMBOOM RISEKICK; SDEFM TONESNR; TMEFM LONGTOM DROPTOM RISETOM GRITTOM; CPEFM BUZZCLAP FMZAP DEEPBLIP RATTLE; RSEFM
  FMRIM RIMSNARE; CBEFM CLANK GONG; HHEFM FMHAT TRASHHAT TREMHAT SPITHAT; CYEFM FMCRASH LOWCHIME FMSPLASH SIZZRIDE.
  These 19 voices' defaults are now their first preset (a fresh BDTRX is SUBTHUMP, CPEFM is BUZZCLAP at TUNE +24, ...).
- CLASSIC (56, 4 per voice, the first = the voice's sound so far): K808 ROUNDBD SUBBOOM PUNCHBD DIRTBD; K909 HOUSEBD
  HARDBD TIGHTBD DEEPBD; S808 WARMSN TONALSN BRITESN SHORTSN; S909 CRISPSN FATSN NOISYSN TIGHTSN; C808 HANDCLAP
  DARKCLAP DRYCLAP BIGCLAP; C909 SNAPCLAP LOOSECLP TIGHTCLP LOWCLAP; HATC CLOSEDHH TICK DARKHAT LOOSEHAT; HATO OPENHAT
  HALFHAT WASHHAT DUSTYHAT; CYMB CRASH RIDE DARKCRSH SPLASH; COWB COWBELL CLINK LOWBELL RINGBELL; TOM MIDTOM FLOORTOM
  HITOM PEWTOM; CONGA CONGA TUMBA SLAPCONG BONGO; RIM RIMSHOT STICK HOTRIM WOODRIM; CLAVE CLAVE WOODBLK PIP BLIP.
- MODERN (40, 4 per voice, the first = the voice's sound so far): BOOM BOOMBD LONGBOOM PUNCHER OVERDRV; PUNCH SYNTHBD
  CLEANBD FMKICK SUBKICK; SNAP SHELLSN DRUMSHEL SNAPPY PICCOLO; CRACK CRACKSN METALSN HISSNARE LOWCRACK; METAL
  SHINYHAT CHIPHAT BRASSHAT GRITHAT; HISS STATIC SPRAY STEAM RINGHAT; SKIN HEADTOM TIMPANI TABLA EDGEHIT; FM FMPERC
  FMBELL FMNOISE FMTOM; ACID SQUELCH DEEPBASS SCREAMER SQUAREAC; SAMPL SMPKICK TAMBO SHAKER LOFICONG.
- Check: a fresh BDTRX / SDTRX / CHTRX track sounds as its first preset did in the demos; the power-on kit's BDTRX shows
  `PRESET SUBTHUMP` (before the TRX classic kit: its K909 `PRESET HOUSEBD`); turning through the banks every name is readable (8 characters) in the popup and the list.


- PRESETS (the encoder that did nothing): each detent loads the next / previous preset onto the selected track at once
  (MODEL and the 8 SOUND knobs; FILTER, LFOs, LEVEL, PAN, NOTE, CHOKE and the steps stay), through every bank in order
  and round. A popup over the graph (`01_popup.png`, `03_popup_bank_edge.png`, on TRACKS `04_popup_tracks.png`): the
  bank in amber, the track, the preset large, two before and two after it; it goes ~1.5 s after the last detent. A
  fresh track, or a track of an older project, starts at its voice's first preset.
- Turn PRESETS fast while a dense pattern plays: the voice changes every detent, no click beyond a MODEL change's;
  after STOP nothing keeps sounding.
- ACID on two tracks: on a third track the popup skips ACID's 4 presets (`05_popup_acid_capped.png`), the list
  refuses them (`ACID: 2 TRACKS MAX`).
- EDIT held ~0.4 s: the PRESETS list (`06_list.png`) over the page you were on (an EDIT tap still opens SOUND / its
  next page; in step record EDIT stays the rest, held too). Seven presets on screen, no `K1 MOVE  K2 BANK` line any
  more (the knobs still work). KNOB 1 or PRESETS the cursor, KNOB 2 or SELECT the bank (tabs MD MODERN CLASSIC USER),
  ALGORITHM or a white key the track (the keys play), the track's own preset marked amber.
- Scrub the list: every cursor move (PRESETS, KNOB 1, a bank jump with KNOB 2 / SELECT) plays the preset under the
  cursor once on the selected track (`15_list_preview.png`): the track takes its voice and 8 knobs for now (FILTER,
  LEVEL, CHOKE, ... stay). The track's key plays that sound; with the pattern playing the track plays it. On an empty
  USER slot (`EMPTY SLOT`) or ACID refused on a third track (`ACID: 2 TRACKS MAX`) nothing plays, the last sound stays.
- OCT+ LOAD keeps the sound under the cursor (`PRESET` its name, no `*`) and closes the list at once
  (`16_list_loaded.png`). EDIT or HOME instead: the list closes and the track sounds exactly as before it opened (its
  preset and `*` too), nothing plays. Another track picked in the list (ALGORITHM, a white key): the one before gets its
  sound back, the new one plays the preset under the cursor.
- SAVE stores the track's own sound (as before the list opened, not the one being auditioned): on a factory preset
  into the first free USER slot (`SAVED K909 01`), on an empty USER slot there, on a used one after a second SAVE
  (`SAVE AGAIN: OVERWRITE`, `08_list_save_over_armed.png`); the track then sounds as saved. OCT- twice deletes a USER
  preset (`OCT- AGAIN: DELETE`, `09_list_delete_armed.png`).
- The SOUND pages show `PRESET HOUSEBD` above the voice, with an amber `*` once a SOUND knob moved
  (`12_sound_edited.png`); TRACKS' footer `HOUSEBD*` (`13_tracks_preset.png`). A MODEL change: the new voice's first
  preset, no `*`. A project keeps each track's preset and `*`; an older project loads with `PRESET -`, its sound as
  before.
- SAVE while playing: in the list at once (`.. : FLASH AT STOP`), in flash at STOP: STOP, power off and on: it is
  there. SAVE while playing and power off before STOP: only that preset is gone. SAVE stopped, power cycle: there.
  MIDI LEARN's CCs still work after preset saves and a power cycle.
- Delete a USER preset a track was loaded from: the track keeps its sound and shows `PRESET -`. Save another voice over
  it (e.g. a TOM over a K909's slot): the K909 track also shows `PRESET -`, never the TOM's name; a TOM loaded from it
  shows `TOM 04`.
- `*` only when a knob really moved: turning a SOUND knob already at its end (on HOME, a SOUND page or the SMC-Mixer)
  leaves no `*`.
- MOTION playing a SOUND knob on the track: SAVE stores the knob as you set it, not the value motion moved it to (load
  it on another track while stopped: the knob is where you left it).
- MIDI LEARN on (GLO + G4): EDIT held does nothing more than a tap (no list; LEARN stays on). LEARN off: the hold opens
  the list again.
- No default choke: the power-on kit's CHTRX / OHTRX (tracks 4, 5; HATC / HATO before the TRX classic kit) do not
  cut each other; CHOKE 1 on both (EDIT, the
  last SOUND page) and they do. A MODEL change or a preset load never changes CHOKE; an older project keeps its stored
  CHOKE (its hats still choke).
- A project saved now cannot be loaded by 0.20.0. It stores each track's factory preset by a fixed number of its own
  (not its place in the banks) and a USER preset by its slot, so a later firmware with more or reordered presets still
  shows the same names.

### LFO DEST slots (check on the FM-1)

- LFO: four pages. 1/4 WAVE RATE | WAVE RATE, 2/4 MORPH PHASE | MORPH PHASE, 3/4 TRIG MODE | TRIG MODE: LFO 1 on
  KNOB 1-2, LFO 2 on KNOB 3-4, the picture split the same way ("LFO 1" left, "LFO 2" right, each its wave, live dot,
  rate mode, TRIG and where it goes: "> TONE", "> 2 DEST", "> OFF"). OCT- does nothing here any more (dark) and the
  LFO button no longer blinks. OCT+ (breathing) switches the RATE MODE of the LFO whose knob was turned last (LFO 1 at
  first; on DEST: the selected slot's source).
- 4/4 DEST: four rows, one per slot ("1 LFO1 > SWEEP +40%" and a small meter of what the slot adds now). KNOB 1 picks
  the slot (the row inverts, SLOT n/4), KNOB 2 its SRC (LFO1 / LFO2), KNOB 3 its DEST (any knob an LFO could target
  before: the engine's knobs, LVL, PAN, R.TUN .. R.POS, F.CUT, F.RES), KNOB 4 its DEPTH. Set slot 3 to LFO2 > DECAY and
  slot 4 to LFO1 > PAN: four knobs can move at once, from the same LFO or both; two slots on one knob add.
- An LFO no slot uses costs nothing (its dot stands still).
- A project saved before this firmware: its LFO 1 / LFO 2 routing comes back as slots 1 / 2 (sources LFO1 / LFO2, the
  same DEST and DEPTH), slots 3 / 4 OFF; it sounds as before; MOTION on an LFO's DEPTH and a MIDI LEARNed LFO DEPTH
  now move slot 1 / 2's DEPTH (the same thing). A project saved now cannot be loaded by 0.22.0.
- MIDI LEARN on DEST: pick the slot with KNOB 1, LEARN on, turn KNOB 4: "T1 D3.DEPTH"; a CC moves that slot's depth.
  MOTION records a slot's DEPTH (not its SRC / DEST).

### Fixes 2026-10-21: accent, track keys, OCT breath, PERFORM knobs, LED levels (check on the FM-1)

- Accent (item 1; screen `build/ui_shots/fixes/step_oct_accent_hint.png`, the hint `[OCT+] +TAP ACCENT`): on STEP hold
  OCT+ and tap a step key: an on step gets its accent (the tall bar), again: plain; an off step turns on accented.
  While OCT+ is held the keys show only the accented steps. A step key held 1 s: no accent, an on step as it was
  (Unreleased: a hold never turns a step off, see "Step keys: tap or hold" below); held + KNOB 1 / 2 / 3: PROB /
  RATCH / accent. OCT+ tapped alone (32 steps): bank 2 when let go; OCT+ held while
  accenting: the bank stays. The same on PATTERN, MOTION, SONG and TRACKS (unarmed). Play: accented steps louder.
- Track keys (item 2): on HOME (the HOME screen), EDIT (SOUND and LAYER pages), FX (every page, DLY / REVERB / CHORUS
  too) and LFO the selected track's white key is lit (track 1: F3, track 2: G3 .. track 8: F4); ALGORITHM moves it, a
  white key selects its track and its key stays lit (on DLY / REVERB / CHORUS, the global FX pages, a white key only
  plays its drum: the selection stays, as before). KEYS on (SCL): no track key, the keyboard as before. COMP: the
  DUCK keys as before; GLOBAL, SAVE, GRIDS: dark.
- OCT breath (item 5): OCT+ / OCT- fade in and out (~1.1 s, smooth, never a hard blink) where they would act: STEP
  with 2+ banks (OCT+ on bank 1, both in the middle, OCT- on the last), SOUND (OCT+: LAYER), the LFO pages (OCT+: RATE
  MODE; OCT- dark), a CLEAR TRACK dialog
  (OCT+ YES breathes, OCT- NO lit), the menu (OCT+ breathes, OCT- lit), NAME, MIDI LEARN, the PRESETS list (OCT+;
  OCT- too on a used USER slot). LAYER: OCT- lit. KEYS: OCT+ lit at octave +1, as before. Look for flicker at the
  breath's dark end (a short fade, a few flickers there are the floor of the 1.1 ms scan frames) and with
  BACKLIGHT OFF / DIM LO / INV alike.
- PERFORM knobs (item 7): on HOME, hold FX and turn KNOB 2 fast back and forth for a few seconds: CRUSH moves, DECAY
  (HOME's KNOB 2) never; let go, HOME's DECAY is where it was. MENU PERFORM PAGE: FX held and let go over FILTER 1/2,
  turn KNOB 1 fast: FILTER's macro only, TYPE never; SELECT does nothing while the PERFORM screen is up; HOME, a page
  button or an FX tap close it as before.
- LED levels (item 9; screen `build/ui_shots/fixes/menu_backlight.png`, BACKLIGHT DIM HI): MENU BACKLIGHT, KNOB 1
  steps OFF, DIM LO, DIM HI, INV, INV LO, INV HI (stops at the ends; OCT+ steps and wraps). On STEP with a few steps
  on: DIM LO the unlit keys and the idle buttons glow (the old ON's keys), beats 1 5 9 13 brighter; DIM HI the same,
  brighter; INV the idle keys and buttons lit, the on steps and SEQ's button dark; INV LO / INV HI the idle ones dim
  / mid. No visible flicker with a dense pattern playing. Power off and on: the level kept. An older Hortator
  (0.22.0) after this one: BACKLIGHT ON for any level but OFF.

### BENCH: the performance cases on the FM-1

`tools/fm1_bench.py --yes` (FM-1 on USB, running a build of this tree) sends `bench yes` to the USB console: the
FM-1 plays each case of `firmware/src/bench.c` for ~2.5 s with the speaker silent (~4 min, BENCH and the case's
name in the top bar), then returns to its power-on state (the pattern in memory is lost; flash, saved projects
untouched). The tool runs the same cases on the host and writes `build/bench/report.md` / `.csv`: host
instructions per sample next to the FM-1's render time, average / worst CPU % (as SYSTEM CPU) and late halves.
Voices are never shed during a run (overloaded cases show their true cost; a half above 80 % is followed by a
silent one so the FM-1 stays responsive). `bench stop` on the console ends a run early.

### Default kit: the TRX classic kit (check on the FM-1)

The screens: `build/ui_shots/fixes/trx_kit_home.png`, `trx_kit_tracks.png`.

- Power on with no project to load (or TOOLS INIT ALL, or START NEW PROJECT? YES): the tracks are 1 BDTRX, 2 SDTRX,
  3 CPTRX, 4 CHTRX, 5 OHTRX, 6 TMTRX, 7 RSTRX, 8 CYTRX (TRACKS), each on its first preset (`SUBTHUMP`, `HARDSNAP`,
  `ROOMCLAP`, `NOISEHAT`, `AIROPEN`, `SWOOPTOM`, `KNOCK`, `WASHCYM`), no `*`.
- The roles and MIDI notes as before: kick 36, snare 38, clap 39, closed hat 42, open hat 46, tom 45, rim 37,
  cymbal 49 (a MIDI keyboard's GM drum keys play the same tracks).
- No choke: CHOKE OFF on every track (EDIT, the last SOUND page); a closed hat does not cut the open hat.
- A saved project loads its own kit, unchanged. BENCH's heavy cases keep the old kit (K909 S808 C808 HATC HATO TOM
  RIM CYMB), so a run compares with the earlier ones.

### REC: TRACKS and REC ALL (check on the FM-1)

The screens: `build/ui_shots/fixes/rec_all.png` (the `REC ALL` message on FX), `rec_tracks_armed.png`
(`T3 REC ARMED` on TRACKS), `rec_all_tracks.png`.

- TRACKS (HOME's first mode), stopped: a REC tap arms the selected track at once: `T3 REC ARMED`, REC lit, play does
  NOT start (press PLAY). A REC tap again: `T3 REC OFF`. Its key records while playing.
- Any other page (HOME screen, EDIT, FX, SEQ ...): a REC tap = `REC ALL`, REC lit, still stopped; press PLAY.
  Finger-drum the white keys F3..F4 on HOME, SOUND or FX (or TRACKS): each key's own track records (open STEP after:
  the hits are on each track). A muted track's key records too.
- KEYS on (SCL) during REC ALL: the keys play and record the selected track only; a knob turn records motion on the
  selected track only.
- A REC tap off TRACKS during REC ALL: it ends; `REC OFF` (nothing was armed before) or `T3 REC ARMED` (track 3 was
  armed before: armed again). On TRACKS during REC ALL: it ends, the selected track armed (`T3 REC ARMED`).
- On STEP with KEYS on, stopped: REC tap = step record on, again = off (no REC ALL). REC held on a SEQ page: CLEAR
  TRACK, as before. While a song plays: REC says `STOP TO EDIT`.
- SMC-Mixer: REC acts as the FM-1's REC on the page shown (REC ALL: REC 1-8 all lit); REC 3 during REC ALL: it ends,
  track 3 dark, the others lit. A learned REC (MIDI LEARN) acts as the button.

### HOME: the bar boxes (check on the FM-1)

The screen: `build/ui_shots/fixes/home_bar3.png` (the third bar).

- HOME (the main screen): the 4 small boxes above PROJ are always 4. Stopped: the first is solid.
- PLAY: the solid box moves one box a bar (every 16 steps) with the playhead, 1 2 3 4 1 2 ..., whatever the selected
  track's LEN (16 or 64 alike); STOP: back to the first; PLAY again (also while playing): from the first.
- Following MIDI clock: the boxes move with the clock's bars.

### MODEL list and preset banks: MD, MODERN, CLASSIC (check on the FM-1)

The screens: `build/ui_shots/presets/06_list.png` (the tabs MD MODERN CLASSIC USER), `03_popup_bank_edge.png`.

- The MODEL knob (EDIT, the first SOUND page) runs BDTRX SDTRX TMTRX CGTRX CPTRX RSTRX CBTRX CHTRX OHTRX CYTRX SHTRX
  CLTRX B2TRX BDEFM SDEFM TMEFM CPEFM RSEFM CBEFM HHEFM CYEFM, then BOOM PUNCH SNAP CRACK METAL HISS SKIN FM ACID
  SAMPL, then K808 K909 S808 S909 C808 C909 HATC HATO CYMB COWB TOM CONGA RIM CLAVE, and round (CLAVE to BDTRX, both
  ways). ACID on 2 other tracks: skipped (FM to SAMPL).
- PRESETS and the list's banks: MACHINEDRUM, then MODERN, then CLASSIC, then USER (MACHINEDRUM's last preset to
  MODERN's `BOOMBD`; CLASSIC's last `BLIP` to the first USER slot).
- A project saved on 0.22.0 or earlier loads with the same presets and sound (projects store each model by its
  number and each preset by its fixed uid, neither of which moved): load one, every track's `PRESET` name and `*` as
  before. USER presets unchanged.

### Step keys: tap or hold (check on the FM-1)

The screens: `build/ui_shots/seq/23_step_held_long.png` (step 5 held: `STEP 5  100%  RATCH 1`, the step still on),
`24_step_held_acc.png` (step 6 held: the knob boxes `PROB 95%`, `RATCH 2`, `ACC ON`; the line `STEP 6  95%  RATCH 2
ACC`).

- STEP, KEYS off: a quick tap on a step key turns the step on / off when the key is let go (it should still feel
  immediate). Tap fast runs of steps, two fingers alternating: every tap counts.
- Hold an ON step's key (about 0.3 s or more), let go: it stays ON, accent / PROB / RATCH unchanged (before: the
  press turned it off). While held past 0.3 s the hint line shows `STEP n  PROB  RATCH n` and the knob boxes PROB,
  RATCH, ACC; a tap shows neither.
- Hold an OFF step's key: at 0.3 s, still held, it turns ON (its key lit, the line and boxes up); let go: it stays on.
  Hold it short of 0.3 s: nothing until let go (then a tap: on).
- While a step is held (STEP page): KNOB 1 PROB, KNOB 2 RATCH, KNOB 3 ACCENT (a click right: `ACC ON`, the tall bar,
  `ACC` at the end of the line; left: `ACC OFF`; more clicks the same way change nothing), even right after pressing;
  the step stays on when let go. An off step with KNOB 3 turned right at once: on and accented. KNOB 4: nothing.
- OCT+ held + a step key: its accent flips at once, as before (let go: no further change). Hold one step, tap
  another: the tapped one toggles, the held one not.
- The same tap / hold on PATTERN, MOTION, SONG and TRACKS (unarmed); there the knobs stay the page's (PATTERN KNOB 3
  SWING, not the accent). KEYS on (SCL): the keys play notes, no step changes; step record (KEYS on, stopped, REC) as
  before.

### Parameter locks (check on the FM-1)

- USR3 at 48 KiB: with an 80 KiB sample in USR3 (uploaded under upstream Felucca before), the first power-on shows
  `USR3 SHORTENED TO 48K` once; SAMPLE's USR3 still plays (its first 48 KiB). The next power-on says nothing. With
  USR3 48 KiB or less (or empty), nothing is shown. (If the FM-1 ever says `USR3 CLAMP FAILED` at power-on, SAVE
  says it too and writes nothing to flash that boot: power off and on.)
- A project saved by 0.23.3 (FDRE) with MOTION loads (`LOADED`) and its motion plays as before. RENAME it (NAME)
  before any SAVE, power off and on, load it: still its motion (not `LOCKS LOST`). Save it, power off
  and on, load it: the same. MOTION's count (SEQ > MOTION, KNOB 2) can now pass 128 (record a long sweep on several tracks).
- ENV tap (let go): its LED lights and a padlock shows in the top bar, left of the battery (STEPS), at the first
  press, every time (tap it 10 times: 10 toggles). On SOUND the white keys now tap the selected track's steps (a short tap
  on / off on the release, a 0.3 s hold turns an off step on), they select no track and play no drum; the key LEDs
  show the bank's steps. ENV again: LED off, the keys select / play tracks as before. Same on HOME, FX, LFO, COMP and
  TRACKS with a track armed.
- STEPS with LEN 64: hold ENV, press OCT+: the next bank (the key LEDs move on); OCT-: back; letting ENV go after that
  keeps STEPS. OCT+ alone on SOUND: LAYER when OCT+ is let go; OCT+ held + a step key: its accent, no LAYER. On LAYER
  OCT-: back to SOUND. On an LFO page OCT+: the RATE MODE cycles. On SEQ STEP OCT+ / OCT- are the bank as before.
- KEYS (SCL) on, ENV: KEYS goes off, STEPS on; SCL again: STEPS off, KEYS on. Power off and on: STEPS off.
- Hold a step key in STEPS, tap ENV while holding it: TRACKS; letting the key go changes nothing.
- STEPS on SOUND, play a 4-on-the-floor kick: hold step 5's key, turn DECAY up a lot: the top bar says
  `LOCK STEP 5`, the DECAY cell shows the new value inverted; let go: the cell shows the knob's own value again. Step
  5's kick is long, the others as before (snap back). Repeat a lock on each family: LAYER (LEVEL), FX (DIST, REV),
  SLICER, RESON (TUNE, MIX), FILTER (CUT), LFO (RATE, DEPTH), LFO DEST slot 3 DEPTH, HOME (a macro), TRACKS (LEVEL,
  PAN): each plays on its step only. MODEL, FILTER TYPE, RESON MODEL, an LFO's WAVE / DEST / SRC: nothing happens while
  a step is held.
- An LFO on DECAY plus a lock on DECAY: on the locked step the wobble is round the locked value.
- Hold the step, tap OCT-: `STEP 5 LOCKS CLEARED`, this page's locks gone (another page's kept). Hold the step, hold
  OCT- 0.6 s: `STEP 5 ALL CLEARED`. OCT- with no step held: LAYER back to SOUND as usual.
- With the project full (1024 events), a lock turn says `LOCKS FULL`.
- Locks on two tracks and a few pages, SAVE the project (stopped), power off and on, LOAD it: every lock plays as
  before; `LOADED` (not `LOCKS LOST`).
- A song of two rows, slot A (with locks) and slot B (without): PLAY on SONG: A's rows play their locks, B's rows play
  the knobs' own values, and nothing of A's locks is left on in B.
- Optional (downgrade is not supported): on an older Hortator (0.23.x) a project saved now reads as an empty slot
  (`EMPTY SLOT` on LOAD; `LOAD FAILED` at power-on if it was the last one saved); nothing else breaks; back on this
  build it loads.
- STEPS: a locked step has a short line under its box in the footer's step strip (another bank: ENV + OCT+ shows its
  own); ENV off: no lines. SEQ > MOTION: KNOB 2 `MOTION` and KNOB 3 `LOCKS` count the selected track's.

### Live cue, variations, FILL, COPY / PASTE (check on the FM-1)

- USR2 at 48 KiB: with an 80 KiB sample in USR2 (uploaded under upstream Felucca before), the first power-on shows
  `USR2 SHORTENED TO 48K` once (before `USR3 SHORTENED TO 48K` if USR3 is long too); SAMPLE's USR2 still plays (its
  first 48 KiB). The next power-on says nothing. With USR2 48 KiB or less (or empty), nothing is shown.
- Edits in RAM: load A, change a knob and a step: PROJECTS shows `A*`. LOAD B: B loads at once (no question); PROJECTS
  shows `A*`. LOAD A: `DISCARD EDITS?` / `LOAD SLOT A SAVED`; OCT- (NO): nothing changes; LOAD A again, OCT+ (YES): A
  as saved (the knob and the step back), `A` without `*`. Edit A, power off and on, load A: as saved.
- SAVE to another slot: load A, edit it, set SLOT B, SAVE: `SAVED`; B now holds the edited A, A is as saved (no `*`).
  RENAME an edited slot (`*`): `RENAMED (SAVE TO KEEP)`; power off and on: the old name; RENAME a saved one: `RENAMED`.
- Cue map: hold SAVE: the top line `CUE A1` (the slot loaded, variation 1), white keys 1–4 show the used slots
  (A winking), 5 winks (variation 1), 6–12 dark (no variation used yet). Let go: no page change. A SAVE tap (short)
  opens PROJECT on the release; again: TOOLS. SAVE held in the menu, a dialog, NAME or the MUTE screen: nothing.
- Cue a slot while playing: hold SAVE, press key 2 (B): `CUE>B`, key 2 blinks; let SAVE go. B comes in exactly at
  the end of A's pattern (the longest track's): no flam, no gap; tempo, swing, the master FX and the mutes stay; the
  old sounds fade quickly (no click). Hold SAVE, key 1 (A), then key 1 again before the end: A at the next step.
  While queued, SAVE + the playing slot's key cancels.
- Variations: on A, hold SAVE, press key 6 (variation 2): at the pattern's end it plays variation 2, a copy of
  variation 1 (key 6 now lit). Edit some steps, LEN of a track. SAVE + key 5: back to variation 1 at the boundary
  (the edits stay in variation 2: SAVE + key 6 brings them back). Fill several variations with dense patterns until
  `VARIATIONS FULL` shows (the playing one plays on). SAVE + key 2 then key 8: `CUE>B4`. Stopped, a cue switches at
  once. During SONG: `STOP SONG TO CUE`.
- Edited slots: edit A, cue B, cue A: the edit is still there; the PROJECTS list shows `A*`; SAVE A: the `*` goes.
  With C's variations edited and left unsaved (cue away from C), cueing any slot but C says `SAVE C FIRST`.
- SAVE onto an edited slot that is not loaded (`B*`): `OVERWRITE B*?`; OCT- nothing, OCT+ saved. LOAD A after only
  changing the tempo: `DISCARD EDITS?`.
- Following CLK USB (a DAW at 120 BPM): queue variation 2 and slot B: each comes in on the bar's pulse, in time.
- The keys pressed with SAVE held never sound, never send MIDI, never record (REC ALL on, playing).
