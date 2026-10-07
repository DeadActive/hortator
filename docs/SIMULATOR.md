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

`web/sim/index.html` is the project's landing page: the name and one sentence, the simulator as the hero, what the
firmware does (with where to find it on the panel), how to install, credits and the source link.

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

## How it works

| Unit | File | Job |
|---|---|---|
| Host harness | `tests/sim_host.h` | `#include`s the firmware sources in `felucca.c`'s order, with the HAL replaced: the audio sample count as the clock, input levels with latched edges, the LCD as a framebuffer, the audio ISR (`audio.c`) run on demand, the flash in RAM with dirty sectors |
| Sim core | `tests/sim_core.c` | the exported C API: `sim_init`, `sim_render`, `sim_frame`, `sim_btn`, `sim_key`, `sim_enc`, `sim_master`, `sim_leds`, `sim_key_leds`, `sim_fb`, `sim_audio`, `sim_flash*`, `sim_playing` |
| Demos | `tests/sim_demo.c` | the first visit's four projects (the drumsim kits 808 / 909 / Plaits / mixed), saved to slots 1–4 with `project_save`, slot 1 loaded |
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

### Storage

The firmware's 1 MiB flash image lives in the wasm memory. After every UI frame, the sectors the firmware wrote go to IndexedDB (`fm1-sim` / `flash`, key = offset).

On a later visit the page loads them and boots from them; slot 1 becomes the current project, so PLAY plays what was left there. The device itself powers on with an empty pattern.

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
   on the device it still reads FM-1 DRUMS. Proposed:

   ```diff
   --- a/firmware/src/main.c                        (boot screen)
   -    draw_text_box(0, 100, 240, &FONT_L, "FM-1 DRUMS", C_HI, 1);
   +    draw_text_box(0, 100, 240, &FONT_L, "HORTATOR", C_HI, 1);
   --- a/firmware/src/ui_menu.c                     (ABOUT)
   -            cv_text(4, 4, &FONT_L, "FM-1 DRUMS", C_HI);
   +            cv_text(4, 4, &FONT_L, "HORTATOR", C_HI);
   ```

   "HORTATOR" is 8 characters, 128 px in FONT_L: it fits where "FM-1 DRUMS" (10) did. The version labels
   (`DRUM-x.y.z` on the device, `drum-x.y.z+<commit>` in the installer and package names; `tools/build.py`
   `drum_label`, `tools/version.py` and its checks) can stay as they are or become `HORTATOR-` / `hortator-`. That is
   the firmware session's call: `tools/build_sim.sh --package` reads either form from the file name. README.md
   (still upstream Felucca's) would be the place to introduce the name.

4. Nothing else in `firmware/**` is needed. The simulator includes the sources as they are.

   Tidy-up suggestion, also optional: `tests/drum_host.h` includes `<libproc.h>` / `<sys/resource.h>` (macOS only) for `instr_now`. Guarding those with `#ifdef __APPLE__` would let other hosts and emscripten reuse it, but `tests/sim_host.h` works without it.
