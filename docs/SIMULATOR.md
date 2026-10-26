# FM-1 web simulator

A web page that runs the FM-1 drum machine firmware in the browser: the 240×240 screen, the panel (14 buttons,
SELECT / ALGORITHM / PRESETS, KNOB 1–4, MASTER, 27 keys, the LEDs) and stereo audio. The UI, sequencer, drum
engines and FX are the firmware's own sources from `firmware/src`, compiled to WebAssembly. Nothing is rewritten
in JS and nothing is copied, so every rebuild follows the firmware.

Design: `docs/superpowers/specs/2026-10-06-websim-design.md`. Plan: `docs/superpowers/plans/2026-10-06-websim.md`.

## Build and run

From a fresh clone, one command:

```sh
tools/build_sim.sh                 # PYTHON=... to pick the Python (it needs Pillow for the generated headers)
cd build/sim && python3 -m http.server 8001
# open http://127.0.0.1:8001/
```

**Prerequisites:**
- Python 3 with Pillow.
- Docker. The script starts Docker Desktop on macOS if it isn't running, and pulls the pinned image `emscripten/emsdk:6.0.11` on first use.
- Node 18 or newer, for the glue test.
- A C compiler (`cc`).

`tools/build_sim.sh --host-only` stops after the headless C test: no Docker, no node.

**Another firmware version:** `tools/build_sim.sh --ref main` (any branch or commit) builds the simulator from that
commit's firmware without touching a branch. It exports the commit into `build/sim/src`, lays this checkout's
simulator files (`tests/sim_*`, `web/sim`) over it, builds from there, and packs that tree as `source.tar.gz`. The
firmware session's uncommitted changes are not in it: commit them first. The page's ABOUT shows the firmware's
`VERSION.txt` (`DRUM-x.y.z`). If the firmware gained a source file or a HAL call the simulator lacks, the build
stops and names it (the drift guard); `tests/sim_host.h` then needs a line.

Everything the script writes goes under `build/sim/`:
- `gen/`: the generated headers, from `tools/build.py`'s generators;
- `host/sim_test`;
- `emcache/`;
- `fm1sim.wasm` and the page files.

`build/sim/` is a self-contained static folder that can be put on any static host. It needs no special headers and no SharedArrayBuffer.

## The site: landing page and installer

`web/sim/index.html` is the project's landing page: the name and one sentence, the simulator as the hero, the
manual (a panel map: a small FM-1, `web/sim/mini.js`, lit by `web/sim/manual.js` from the list of every control;
then ten short guides, the README keeping the full manual), the features as eight interactive pieces on the firmware's own DSP (the features lab, below), how to install, credits and
the source link.

`web/index_pkg.html` is the installer page, restyled to match, in English only and named Hortator (the firmware's name since 2026-10-07). This was a
user-granted exception to the simulator's file rules (2026-10-07). Its script is `main`'s at 12a1ba0 with exactly
four changes: the text table, the fixed language, no language switch, and the status label. `tests/sim_site.mjs`
rebuilds that script from 12a1ba0 and requires a byte-for-byte match, so the install logic cannot drift unnoticed.

`tools/build_sim.sh --preview` (with `--ref main` for the current firmware) writes `build/sim/preview/` in the
site's layout. The landing page is at `/` and the installer at `webapp/installer/`, assembled with `make_site.py`'s
own `strip_module`. Its firmware package is a placeholder that does not exist, so the preview installer shows
"Could not load the firmware" and Install stays disabled.

To preview with a real firmware package, pass one the firmware session already built: `tools/build_sim.sh --ref
<its commit> --preview --package <path>/felucca-drum-X.Y.Z-<commit>.fwsc`. The package is copied into `build/sim`
(the original is never touched), and the preview is assembled by `make_site.main` itself, labelled from the file
name (`drum-X.Y.Z+<commit>`). Use the package's own commit as `--ref` so the simulator and the firmware match. That
preview's installer *can* flash an FM-1.

### The look

The page is drawn as the FM-1's screen draws its pages:

- **Colour:** only the firmware's MONO palette (`firmware/src/gfx.c`: five greys and white), in the firmware's roles:
  `#282828` rules, `#505050` inactive, `#828282` labels, `#bababa` secondary text, `#e2e2e2` values and text, white
  only for what is touched or where you are. The device keeps its own neutral greys. `tests/sim_site.mjs` fails on any
  colour with a hue.
- **Type:** the firmware's own Terminus 8 × 16. `tests/sim_font.mjs` turns `assets/fonts/ter-u16n.bdf` into
  `terminus.woff` at build time (pixel squares, 64 units each; 210 glyphs, about 5 KB), published with its OFL
  licence. Sizes are whole multiples of 16 px, so every pixel lands on the grid. Silkscreen remains for the device's
  printed labels.
- **Dither:** blends are ordered dithering (Bayer 4 × 4, 2 px cells, `web/sim/dither.js`): the glow behind the device,
  the band above the footer, the pieces' area fills.
- **Glitch:** headings tear into offset slices once as they arrive; the hero's title now and then. Reduced motion
  turns it off.

## How it works

| Unit | File | Job |
|---|---|---|
| Host harness | `tests/sim_host.h` | `#include`s the firmware sources in `felucca.c`'s order, with the HAL replaced: the audio sample count as the clock, input levels with latched edges, the LCD as a framebuffer, the audio ISR (`audio.c`) run on demand, the flash in RAM with dirty sectors |
| Sim core | `tests/sim_core.c` | the exported C API: `sim_init`, `sim_render`, `sim_frame`, `sim_btn`, `sim_key`, `sim_enc`, `sim_master`, `sim_leds`, `sim_key_leds`, `sim_fb`, `sim_audio`, `sim_flash*`, `sim_playing` |
| Demos | `tests/sim_demo.c` | the first visit's four projects (the drumsim kits 808 / 909 / Plaits / mixed), saved to slots 1–4 with `project_save`, slot 1 loaded and remembered as the last project |
| Engine | `web/sim/engine.js` | loads the wasm (no emscripten JS glue), wraps the API; used by the worklet and by node |
| Worklet | `web/sim/worklet.js` | the firmware runs here, in the audio thread: 128 frames per quantum. The audio ISR runs once per 256-frame half and the UI frame every 15 ms of audio, as on the device. It posts the screen, the LEDs and written flash sectors to the page |
| Storage, audio | `web/sim/store.js`, `web/sim/audio.js` | the flash in IndexedDB (written sectors wait until a write completes; a lost connection is reopened once; a failure shows a note and is retried with the next save); resuming the AudioContext (the clock) after a phone stops it, and the iOS playback session (sound with the silent switch on) |
| Controls | `web/sim/controls.js` | the panel's controls as data: labels in `panel.c` order, the keys, the computer keyboard map, held-state bookkeeping |
| Page | `web/sim/index.html`, `app.js`, `sim.css` | draws the panel (landscape: as the device; portrait phones: stacked), handles input, paints frames, stores the flash in IndexedDB |

Not simulated: USB (MIDI, audio, console), OTA and update mode, TRS MIDI, boot guard, battery (it shows full), CPU
load (it reads 0). The menu's HARDWARE CALIBRATION is a firmware busy-wait for input that cannot arrive inside it
here: `fm1_wdt_feed` moves the clock on, so it times out at once (SETUP CANCELLED) and keeps the panel table.
`fm1_ms` follows the audio clock.

The build also writes `build/sim/source.tar.gz` (`git archive HEAD`), which the page's footer links: a public copy
of the page offers the GPL-3.0 source it was built from. Commit before building a copy to publish (the script warns
otherwise).

### The features lab

The features section (`docs/superpowers/specs/2026-10-09-websim-features-lab-design.md`) drives the firmware's DSP
directly, not through the panel: each piece sets a track's model, knobs and steps, the globals and the transport, and
draws what the firmware computes.

| Unit | File | Job |
|---|---|---|
| Lab C | `tests/sim_lab.c` (in `sim_core.c`'s translation unit) | a blank project at 120 BPM; models, parameters, steps (PROB, RATCH), globals, the transport, a hit, a PERFORM key held; one track's hit rendered alone (`lab_voice`, with or without its RESON and FILTER); the mix; the Grids lanes; TRACKS' mute (now or on the bar); MOTION (armed, a knob turned through `motion_capture`); per audio half a probe: the compressor's gain reduction, both LFOs of every track, the tracks that fired, each track's step, the waiting and applied mutes, a watched knob |
| Ids | `tests/sim_ids.c` → `lab_ids.json` | the parameter, global, model, PERFORM and RESON ids with their labels, ranges, defaults and name tables, the PROB list (`cond_store` / `cond_format`) and the LFO SYNC names, printed from the firmware at build time: the page never copies a number |
| Analyser | `web/sim/lab.js` `Lab` | a second wasm instance in the page, silent; started when the section nears the screen; `voice`, `renderMono`, `grids`, `probes`, `dice` |
| Player | `web/sim/lab.js` `LabPlayer`, `worklet.js` `fm1lab` | a third instance in an AudioWorklet, started by the first Play (one AudioContext): the same calls as messages (a whitelist), probes posted back about 60 times a second, an AnalyserNode on its output |
| Screens' clips | `tests/sim_feature_reel.c` (+ `sim_reel.h`, shared with `sim_record.c`) → `features.bin.gz`, `features.json` | a clip per chapter: from a fresh demo 1, playing, the chapter's page while a scripted hand turns its knobs to their ends (models and waves stepped through, ranges swept, PERFORM keys held); FM1R at 15 fps |
| Frame | `web/sim/features.js` | the chapters, each with the FM-1's own screen beside the text: its clip looped while in view; a change in the piece shows the live screen for 5 s (`Lab.screen`: the panel moves that open the page, run on the analyser's state), then the clip again; one piece plays at a time; a piece off screen or a hidden tab stops; `audition()` starts a piece's sound from a pad or a key without its loop; diagrams (Bluetooth) mount without the analyser |
| Parts | `lab_ui.js`, `lab_dsp.js`, `lab_draw.js` | knobs (drag, arrows, Page, Home / End), chips, the pad, canvases at the pixel ratio; the FFT, spectrum and tail; the traces |
| Pieces | `lab_voices.js`, `lab_reson.js`, `lab_comp.js`, `lab_lfo.js`, `lab_seq.js`, `lab_grids.js`, `lab_fx.js`, `lab_ble.js` | drum voices, resonators, the pump compressor, two LFOs, the sequencer's chance and live hands, Grids, effects and PERFORM, Bluetooth MIDI (an animated diagram of part 1, from `docs/ble/DEVICE_STEPS.md`) |

`lab_reset` stops a transport another piece left running, so each piece starts from its own PLAY, and clears the pages each family remembers (`ui.fam_last`), as at power-on. The lab's styles are scoped to `.plate` (an unscoped `.knob` once restyled the power switch's thumb; `tests/sim_site.mjs` guards it).

Knob drags redraw at most once a frame (each piece coalesces its renders). Three firmware facts the pictures follow:
a hit is set up a block ahead of its sound (the probes see it up to one half early); Grids' 32 steps are one bar of
32nds; PROB's dice are seeded at PLAY, so the first 8 bars are the same after every PLAY (no re-roll on the page).

### Storage

The firmware's 1 MiB flash image lives in the wasm memory. After every UI frame, the sectors the firmware wrote go to IndexedDB (`fm1-sim` / `flash`, key = offset).

On a later visit the page loads them and boots from them; as on the device, the last project saved loads at power-on (the demos count as a save of slot 1), so PLAY plays what was left there. With no project remembered, slot 1 becomes the current project (the device would power on with an empty pattern).

"Reset to demos" clears the store and reloads. A browser that blocks storage still runs the sim, with saves lasting only for the session.

## Controls

**Mouse and touch:**
- Keys and buttons play while held; multi-touch works.
- Knobs: drag up / down (12 px per detent), the mouse wheel, or hover a knob and press ↑ / ↓.
- MASTER is a pot, 0–1023 (default 800).
- To hold a button for a combination (SEQ held + a key, OCT− held in TRACKS), right-click it or long-press it for 600 ms. A dashed outline shows it is held; tap it again to let go.

**Computer keyboard** (by physical key, any layout):

| Keys | Notes |
|---|---|
| `Z X C V B N M , . /` | white keys F3–A4 |
| `S D F H J L ; '` | F#3 G#3 A#3 C#4 D#4 F#4 G#4 A#4 |
| `Q W E R T Y` | white keys B4–G5 |
| `3 4 6` | C#5 D#5 F#5 |
| Space / Enter / Esc | PLAY / REC / HOME |
| ← / → | OCT− / OCT+ |

Autorepeat and shortcuts with Cmd / Ctrl / Alt are ignored. Leaving the tab releases everything held.

## Tests

- `tests/sim_test.c` is built with plain `cc` from the same sources and run by `tools/build_sim.sh`. It checks:
  - boot draws the screen;
  - a button, an encoder and a note key change it;
  - PLAY gives audio within 1 s, MASTER at 0 is much quieter, and a stopped machine is silent;
  - PLAY's LED blinks, and key LEDs follow held keys (F5 included);
  - the demos fill four slots;
  - flash survives a power cycle, and hostile flash boots;
  - the drift guard: `felucca.c` gaining a source the simulator lacks fails, by name;
  - the menu's HARDWARE CALIBRATION comes back (a hang fails within 10 s), and `fm1_ms` follows the audio clock.

  It also prints the cost of a UI frame (about 70–140 µs, against 2.9 ms per audio quantum).
- `tests/sim_glue.mjs` runs on the built wasm through `engine.js` (node). It checks:
  - boot, input and audio;
  - dirty sectors, and a new instance booting from them;
  - `releaseAll`;
  - `controls.js` against `panel.c`'s enums;
  - the keyboard map (complete, no duplicates);
  - refcounted holds;
  - autorepeat;
  - the touch long-press latch;
  - the IndexedDB store against a fake database (lost connection, repeated failures, blocked storage, reset);
  - resuming the AudioContext and the iOS playback session;
  - the footer links `source.tar.gz`, which holds the firmware and the sim.

- `tests/sim_lab_test.c` (built and run by `tools/build_sim.sh`) checks the lab on the firmware: every model is
  heard and models differ, DECAY lengthens a hit, RESON rings longer than dry, Grids' lanes follow X / Y, the
  compressor reduces gain when keyed, the LFO probes move, PROB 50 % fires sometimes, PERFORM changes the output, a
  mute waits for the bar line, a recorded knob move plays back at its step.
- `tests/sim_glue.mjs`'s lab tests run the page's own `lab.js` / `lab_dsp.js` on the built wasm: the ids, the FFT (a
  909 kick low, a closed hat high), a string tuned to A3 ringing on 220 Hz's harmonics, GHOST MUTE / KEEP, an LFO on
  DECAY, the 8-bar dice (1/2, 1-SHOT, RATCH 4, the same after PLAY), tracks following Grids playing exactly its lanes,
  every PERFORM key and chain stage changing the output.
- `tests/sim_site.mjs` checks the page: the manual before the features, its map listing every knob and button, ten
  guides; the eight chapters, each with its plate; every panel hint (guides and chapters) naming real controls.

## Requests for the firmware session

1. **Done on `web-sim` (user-approved, 2026-10-07): the landing page is the site's front page.** `web/make_site.py`
   copies the published files of `build/sim/` to the site's root when `tools/build_sim.sh` has built it (the landing
   page, its scripts, `fm1sim.wasm`, the reel, `version.json`, `source.tar.gz`; never `reel.bin`, `reel.hash` or the
   package copy), and keeps the redirect to the installer when it has not. It warns when `build/sim` was built from
   another commit than the tree's. Order for a release: `tools/build_sim.sh`, then `DRUM_PACKAGE=1 ./build.sh` (whose
   `make_site` call then publishes all of it).

2. **The button printed SEL is named SCL in the firmware** (`panel.c`: `B_SCL`, `B_NAME[1] = "SCL"`, shown by HARDWARE
   CALIBRATION's "PRESS SCL"; `docs/panel.jpg`'s caption says "SCL: Scale"). The simulator's panel prints SEL, as the
   device does. If the firmware should match, change `B_NAME[1]` to `"SEL"` (the enum name can stay):

   ```diff
   -static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
   +static const char *const B_NAME[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
   ```

3. **The firmware's name is Hortator** (user decision, 2026-10-07). The landing page and the installer use it;
   the device shows it on the start screen (CRT COLLAPSE, `assets/boot.png`: `ui_menu.c` `draw_boot_title`, `tools/gen_boot.py`) and as the HortatoR logo (UnifrakturMaguntia) on ABOUT (`tools/gen_logo.py`).
   The version labels
   (`DRUM-x.y.z` on the device, `drum-x.y.z+<commit>` in the installer and package names; `tools/build.py`
   `drum_label`, `tools/version.py` and its checks) can stay as they are or become `HORTATOR-` / `hortator-`. That is
   the firmware session's call: `tools/build_sim.sh --package` reads either form from the file name. README.md
   (still upstream Felucca's) would be the place to introduce the name.

4. Nothing else in `firmware/**` is needed. The simulator includes the sources as they are.

   Tidy-up suggestion, also optional: `tests/drum_host.h` includes `<libproc.h>` / `<sys/resource.h>` (macOS only) for `instr_now`. Guarding those with `#ifdef __APPLE__` would let other hosts and emscripten reuse it, but `tests/sim_host.h` works without it.
