# Installing the drum firmware on an FM-1 (step by step)

Do the steps in order. **Stop at the first step that does not go as written**, go back to stock (step 6) if the
drum firmware is installed, and report what you saw. Nothing here needs extra hardware.

What protects you: the update path (USB-MIDI), the start-up code and the update loader are byte-for-byte
upstream Felucca's (checked in the binary, H2); the FM-1's built-in bootloader cannot be erased. The one case that
needs extra hardware is a crash on every boot before USB answers — the host tests (H1–H4) are there to rule it out.

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
   **SAFE START** ("NO AUDIO - USB UPDATE READY"); then let go. Power off to leave it. (SEQ is read once,
   right after the "FM-1 DRUMS" start screen appears.)

## 5. Hands-on
For each: what you should see / hear. Note anything different (page, knob, what happened).
- Every page: SOUND 1/2 and 2/2, TRACK, MIDI, LAYER, FX, SLICER, DLY, REV/CHO, STEP, PATTERN, GLOBAL, SYSTEM,
  PROJECT, TOOLS, TRACKS — each draws like the screenshots in `build/ui_shots/` (and `engine_screens/`).
- Every engine: PRESET on HOME steps through the 21 engines; each plays on its key.
- The demo kits: enter one of the kit patterns from `tests/drumsim.c` (`KITS`, e.g. kit_808: one line of
  x / X per track) on the STEP grid, press PLAY: it sounds like `build/drum_renders/kit_808.wav`.
- Live recording: arm a track (REC on TRACKS), play keys while running: the steps appear.
- PROJECT: save to slot 1, change things, load slot 1: everything comes back.
- The master knob changes the volume; the battery icon shows a level.

## 6. Going back to stock (any time the drum firmware runs)
M-VAVE's updater, as in step 1. Expected: stock firmware.

## 7. If the FM-1 shows nothing and USB does not answer
After two crashes within 30 s of power-on, the firmware enters the FM-1's built-in bootloader on purpose. Power
off and on once: if it starts the drum firmware, go to step 6. If not, the FM-1 is safe but needs one of:
- an RP2040 board (Seeed XIAO RP2040) with FM-1-transporter: https://github.com/kurogedelic/FM-1-transporter
  (3 wires to the FM-1's USB D+ / D− / GND, no opening); it writes the stock firmware back;
- a Linux PC with jl-uboot-tool over the FM-1's own USB cable.
Report what you saw first; we'll go through it together.
