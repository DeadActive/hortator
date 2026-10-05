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

## 4. First boot (in this order)
1. The start screen reads **FM-1 DRUMS** / DRUM MACHINE (UNTESTED), then the HOME screen appears.
2. The 8 white keys F3..F4 play the 8 tracks (kick, snare, clap, hats, ...).
3. **The update path from the drum firmware:** run the step-3 command again (the same file).
   Expected: it installs and the FM-1 restarts into the drum firmware.
   If this fails: go back to stock at once (step 6) while the drum firmware still runs.
4. **Safe start:** power off; hold **SEQ** and power on, keep holding it until the screen shows
   **SAFE START** ("NO AUDIO - USB UPDATE READY"); then let go. (SEQ is read once, right after the "FM-1 DRUMS"
   start screen appears.) While it shows SAFE START, check that USB answers:
   `~/fm1-venv/bin/python tools/fm1_install.py --info` prints the FM-1's identity (this only asks, it writes
   nothing). This rehearses the way out of a crash (step 7). Power off to leave safe start.

After each power-on, let the FM-1 run 30 s before you power it off: the firmware's boot guard counts a restart in
the first 30 s as a failed start, and two in a row send it to the built-in bootloader (step 7).

## 5. Hands-on
For each: what you should see / hear. Note anything different (page, knob, what happened).
- Every page: SOUND 1/3..3/3 (EDIT: MODEL, the engine's knobs, LVL PAN NOTE CHOKE), LAYER, FX, SLICER, DLY,
  REV/CHO, COMP 1/2 and 2/2, STEP, PATTERN, GLOBAL, SYSTEM,
  PROJECT, TOOLS, TRACKS — each draws like the screenshots in `build/ui_shots/` (the engines' SOUND pages in
  `build/ui_shots/engines/`, the sequencer in `build/ui_shots/seq/`).
- Every engine: PRESET on HOME steps through the 21 engines; each plays on its key.
- The demo kits: the kits in `tests/drumsim.c` (`KITS`) use other engines than the power-on kit. First set each
  track's engine to the kit's (e.g. kit_808: K808 S808 C808 HATC HATO TOM COWB CYMB, with PRESET on HOME or MODEL
  on EDIT), then enter its pattern (one line of x / X per track) on the STEP grid and press PLAY at 120 BPM:
  it sounds like `build/drum_renders/kit_808.wav`.
- Live recording: arm a track (REC on TRACKS), then go to another page (HOME, or the track's pages) and play its
  key while running: the steps appear. (On TRACKS the keys are quick mutes.)
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
- TRACKS (REC tap from HOME): white keys 1-8 mute / unmute their tracks (a muted track stops at once); the keys
  of muted tracks are lit, and the row shows MUTE. On TRACKS the keys do not play; leave the page to play them.
- CPU: a dense pattern (Grids on 3 tracks, the rest with RATCH 4 and PROB, FX on) at 240 BPM: GLOBAL -> SYSTEM CPU
  stays well under 100 % and the sound does not crackle.
- SAVE a project, power off and on, LOAD: PROB / RATCH / SRC / GRIDS come back. A project saved with the M1
  firmware loads with plain steps.

### M3: COMP sidechain (check on the FM-1)

- FX (after REV/CHO): COMP 1/2 (SRC THRSH RATIO REL) and 2/2 (ATK KNEE MKUP, the curve). SRC T1 with a kick pattern; on a COMP page the
  white keys 2-8 light for the ducked tracks (key 1, the source, does not toggle). PLAY: the ducked tracks pump
  with the kick, the GR meter moves; REL longer = slower recovery; RATIO higher = deeper duck; MKUP raises the
  ducked tracks (at the end LIMIT).
- Mute T1 (TRACKS quick mute): the kick is silent, the others still pump (ghost key).
- SRC OFF: the mix sounds exactly as before M3.
- SAVE / power cycle / LOAD: SRC, the COMP knobs and DUCK come back; an M2 project loads with COMP off.

### TOOLS (check on the FM-1)

- SAVE twice: TOOLS `CLRSQ` `INIT` (the selected track) and `CLR*` `INIT*` (everything). Each acts on a second
  detent within ~1.5 s ("AGAIN: ..." first). `CLR*`: every pattern empty, LEN 16 / DIV 1/16, sounds unchanged.
  `INIT*`: the power-on kit and settings, playback stopped; saved projects still load.
