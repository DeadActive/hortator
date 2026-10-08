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

   Or with the web installer: every `DRUM_PACKAGE=1 ./build.sh` also rebuilds a local copy of the site in
   `build/site` with that same package, labelled `drum-<version>+<commit>` (docs/VERSIONING.md; `-dirty` when the source had uncommitted
   changes). Serve it with `cd build/site && python3 -m http.server 8000` and open
   http://localhost:8000/webapp/installer/ (Chrome or Edge). Check that the version it shows is the commit you
   noted in step 1.

## 4. First boot (in this order)
1. The start screen shows the **HortatoR** logo / DRUM MACHINE, then the HOME screen appears.
2. The 8 white keys F3..F4 play the 8 tracks (kick, snare, clap, hats, ...).
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
  REVERB, CHORUS, COMP (HOME 2/3 and 3/3: HOME pressed on the HOME screen), STEP, PATTERN, GLOBAL 1/3, SYSTEM,
  GLOBAL 3/3 (MUTE NOW / BAR), LFO 1/2 and 2/2,
  PROJECT, TOOLS, TRACKS — each draws like the screenshots in `build/ui_shots/` (the engines' SOUND pages in
  `build/ui_shots/engines/`, the sequencer in `build/ui_shots/seq/`).
- Every engine: KNOB 1 (MODEL) on EDIT's first page steps through the 21 engines; each plays on its key.
- PRESET knob, on any screen: the section's next / previous page (HOME 1/3 <-> COMP 2/3 <-> 3/3, FX <-> SLICER <->
  RESON <-> DLY ..., the SOUND pages, the LAYER pages in the layer); it stops at the first / last page and no
  longer changes the engine.
- HOME: a white key plays its track and selects it (the knobs and T<n> follow). An LFO on one of the HOME knobs
  shows its live value as a small mark under that gauge, as on EDIT.
- The demo kits: the kits in `tests/drumsim.c` (`KITS`) use other engines than the power-on kit. First set each
  track's engine to the kit's (e.g. kit_808: K808 S808 C808 HATC HATO TOM COWB CYMB, with MODEL (KNOB 1) on
  EDIT), then enter its pattern (one line of x / X per track) on the STEP grid and press PLAY at 120 BPM:
  it sounds like `build/drum_renders/kit_808.wav`.
- Live recording: arm a track (REC tap on TRACKS; on SEQ a REC tap only opens TRACKS) and play its key while
  running, on TRACKS or any page where the keys play: the steps appear.
- PROJECT: save to slot 1, change things, load slot 1: everything comes back.
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
- Every SEQ page (STEP, PATTERN, MOTION, SONG): the white keys are the selected track's steps (tap: on / off, hold:
  accent), OCT+ / OCT- the bank; the footer strip shows them; ALGO picks the track (no sound).
- TRACKS (REC tap from HOME or SEQ): the white keys are the selected track's steps (as on STEP: tap on / off, hold:
  accent; OCT+ / OCT- the bank; no sound); KNOB 1 or ALGO picks the track. With a track armed (REC tap on TRACKS) the
  white keys play their tracks instead and record into the armed one, the selection stays. Hold REC: the keys light
  for the playing tracks, dark for the muted; a white key mutes / unmutes its track (a muted track stops at once), the
  row shows MUTE; that REC press neither arms nor asks to clear. Release REC: the mute lights go.
- Mute on the next bar: hold REC on TRACKS + the top D# (POLY): "MUTE: NEXT BAR"; while playing, a mute waits
  for the next bar (its key blinks) and lands on the downbeat. REC + the top C# (MONO): "MUTE: NOW". The same
  setting is GLOBAL 3/3 MUTE NOW / BAR; it survives a power cycle.
- CPU: a dense pattern (Grids on 3 tracks, the rest with RATCH 4 and PROB, FX on) at 240 BPM: GLOBAL -> SYSTEM CPU
  stays well under 100 % and the sound does not crackle.
- SAVE a project, power off and on, LOAD: PROB / RATCH / SRC / GRIDS come back. A project saved with the M1
  firmware loads with plain steps.

### M3: COMP sidechain (check on the FM-1)

- HOME on the HOME screen: COMP (HOME 2/3: SRC THRSH RATIO REL), HOME again: 3/3 (ATK KNEE MKUP GHOST, the curve),
  HOME again: back to HOME 1/3. FX no longer has the COMP pages. SRC T1 with a kick pattern; on a COMP page the
  white keys 2-8 light for the ducked tracks (key 1, the source, does not toggle). PLAY: the ducked tracks pump
  with the kick, the GR meter moves; REL longer = slower recovery; RATIO higher = deeper duck; MKUP raises the
  ducked tracks (at the end LIMIT).
- Mute T1 (TRACKS, REC held + key 1): the kick is silent, the others still pump (ghost key, GHOST KEEP).
- SRC OFF: the mix sounds exactly as before M3.
- SAVE / power cycle / LOAD: SRC, the COMP knobs and DUCK come back; an M2 project loads with COMP off.

### COMP GHOST (check on the FM-1)

- HOME 3/3, KNOB 4 GHOST: KEEP at start; MUTE, KEEP, HIDE. The COMP picture shows the source HEARD / GHOST / MUTED
  next to its IN level.
- KEEP: T1 muted (TRACKS, REC held + key 1) is silent and the others pump (as before), GHOST shown.
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
- Power off and on: the saved project loads, the settings and the panel layout (OCT- + OCT+ at power-on) are kept;
  a project saved with the previous build still loads.

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
  keep it; an FX tap, another page button or HOME leaves it (the knobs back to off). Set it back to HOLD after.
- Hold FX alone: after a moment the map shows (two rows of effects, eight tracks); let go: back to the page.
- Tap FX: the FX pages, as before.
- Playing, FX held: each black key's effect while held — F#3 G#3 A#3 REPEAT 1/8 1/16 1/32 (starting on the next
  1/16), C#4 REVERSE, D#4 TAPE STOP, F#4 LPF, G#4 HPF (sweeping over a bar), A#4 FREEZE, C#5 OCT UP, D#5 OCT DN;
  let go: back to the dry sound without a click. Two buffer effects held: the last pressed plays, letting it go
  returns to the other.
- FX + a white track key (F3 .. F4): that track silent while held; no note plays, nothing is recorded.
- FX + KNOB 1..4: FILTER (left LPF, right HPF), CRUSH, THROW (into the delay / reverb), DEPTH (SHIMMER while OCT
  plays); let go of FX: all back to off.
- A STUT slicer track keeps playing while a buffer effect plays and stutters again afterwards.
- At 40 BPM REPEAT 1/8 and REVERSE are dimmed and do nothing.
- With CLK USB from a DAW: REPEAT starts on the DAW's 1/16 grid.

### MOTION (check on the FM-1)
- TRACKS: select a track and arm it (REC); play; open SOUND and sweep DECAY over a bar: the sweep comes back every
  bar. STOP: the knob back where it was before.
- SEQ > MOTION: KNOB 1 PLAY OFF (the sweep stops, the events kept) / ON; EVENT shows how many; KNOB 4 CLEAR asks
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
- A track to MEMB (SOUND, MODEL after HNOIS): TUNE / DECAY / TONE / HEAD on HOME. HEAD from a tom (0) to a tabla
  (127); BEND drops the pitch after each hit; POS from the centre (round) to the rim; STICK the click of the strike.
  Hit it fast: the head rings on, no clicks.
- RESON MODAL (after CHORD) on a kick, a snare, a clap: STRCT from harmonic to bell, DECAY the ring time (as STRNG),
  TONE, POS. A third MODAL track is refused ("MODAL: 2 TRACKS MAX"); with CHORD full the MODEL knob skips to MODAL.
- Four MEMB tracks and two MODAL RESONs playing: no dropouts (CPU on SYSTEM INFO).

### TOOLS (check on the FM-1)

- SAVE twice: TOOLS `CLRSQ` `INIT` (the selected track) and `CLR*` `INIT*` (everything). Each acts on a second
  detent within ~1.5 s ("AGAIN: ..." first). `CLR*`: every pattern empty, LEN 16 / DIV 1/16, sounds unchanged.
  `INIT*`: the power-on kit and settings, playback stopped; saved projects still load.

### LFOs (check on the FM-1)

- LFO: LFO 1/2 (WAVE RATE MORPH DEPTH) and 2/2 (DEST TRIG PHASE). OCT- switches LFO 1 / LFO 2 on the same page
  (the picture says "LFO 2" in large type, the LFO button blinks; a white key keeps LFO 2 shown). OCT+ switches the
  shown LFO between SYNC / Hz / TIME (the RATE value and "RATE: ..." change). Set DEST TONE and DEPTH: the
  picture's dot moves, the sound changes; on EDIT the TONE gauge shows a second moving mark.
- S&H, WANDR, RWALK: the picture is a scope, the dot at the right edge, the trail behind it (one cycle wide); it
  starts over on another track, LFO or wave.
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

### BENCH: the performance cases on the FM-1

`tools/fm1_bench.py --yes` (FM-1 on USB, running a build of this tree) sends `bench yes` to the USB console: the
FM-1 plays each case of `firmware/src/bench.c` for ~2.5 s with the speaker silent (~4 min, BENCH and the case's
name in the top bar), then returns to its power-on state (the pattern in memory is lost; flash, saved projects
untouched). The tool runs the same cases on the host and writes `build/bench/report.md` / `.csv`: host
instructions per sample next to the FM-1's render time, average / worst CPU % (as SYSTEM CPU) and late halves.
Voices are never shed during a run (overloaded cases show their true cost; a half above 80 % is followed by a
silent one so the FM-1 stays responsive). `bench stop` on the console ends a run early.
