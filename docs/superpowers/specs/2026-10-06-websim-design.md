# FM-1 web simulator — design

Date: 2026-10-06. Branch `web-sim` (worktree `felucca-websim`), parallel to the firmware session.

## Goal

A public demo page: anyone opens a URL, taps once and plays the FM-1 drum machine in the browser — the real
firmware (UI, sequencer, drum engines, FX from `firmware/src`) compiled to WebAssembly, never rewritten in JS and
never a copy of the sources, so it follows firmware changes on every rebuild. Works on phones and desktops, on any
static host (no special server headers).

Success:
- the page shows the 240×240 screen, the FM-1 panel (14 buttons, 7 encoders, MASTER, 27 keys, LEDs), plays stereo
  audio; mouse, touch (multi-touch) and computer keyboard all work;
- first visit: the machine boots with a demo pattern in the current project; pressing PLAY grooves at once;
- saves persist in the browser across visits;
- one command builds it from a fresh clone; headless C and node tests pass.

Out of scope: USB (MIDI, audio, console), OTA / update mode, TRS MIDI, panel calibration, boot guard, battery,
WebMIDI (only input may come later, on request).

## Architecture

The whole firmware runs inside the AudioWorklet, one thread, as on the device (audio ISR and main loop take turns,
no races, no SharedArrayBuffer).

| Unit | File | Job |
|---|---|---|
| Host harness | `tests/sim_host.h` | `#include`s the firmware sources `felucca.c` uses, same order, with HAL shims: clock, LCD → `fb`, input state, LEDs, flash in RAM (`tests/flash_host.h` + `storage.c`) |
| Sim core | `tests/sim_core.c` | the exported C API (below); builds with plain `cc` (tests) and emcc (browser) |
| Demos | `tests/sim_demo.c` | 4 demo projects, built through `song` / `trk` like `tests/drumsim.c`, stored with the firmware's own `project_save` |
| Engine | `web/sim/engine.js` | loads the `.wasm`, wraps the API; runs in the worklet and in node (tests) |
| Worklet | `web/sim/worklet.js` | `process()`: apply queued input, `sim_render(128)`, a UI frame every 15 ms of audio time, post `{fb, leds}` and dirty flash sectors to the page |
| Page | `web/sim/index.html`, `panel.js`, `app.js`, `sim.css` | draws the panel, maps pointer / keyboard to events, paints the screen and LEDs, keeps flash in IndexedDB |

### C API (`tests/sim_core.c`)

```
void      sim_init(int fresh);          /* boot: persist_boot, settings_init, panel_init, drum_boot_init;
                                           fresh = 1: install the demos (blank flash) */
void      sim_render(uint32_t frames);  /* frames % CTL == 0; writes interleaved int16 stereo to sim_audio() */
void      sim_frame(void);              /* one main-loop pass: ui_input, ui_leds, ui_draw, settings_poll */
void      sim_btn(uint32_t label, int down);   /* label = B_FX .. B_OCTUP (panel.c) */
void      sim_key(uint32_t n, int down);       /* note key 0 (F3) .. 26 (G5) */
void      sim_enc(uint32_t role, int32_t steps); /* role = EN_SELECT .. EN_K4, + = clockwise */
void      sim_master(uint32_t v);       /* 0..1023, the MASTER pot (song.master_q12 as main.c computes it) */
uint32_t  sim_leds(void);               /* bit i = button label i lit, bit 14 + n = note key n lit */
pointers: sim_fb() (RGB565 240×240), sim_audio(), sim_flash() (1 MiB image), sim_flash_dirty() (bit per 4 KB sector,
          cleared by sim_flash_clean())
```

### Shims (in `tests/sim_host.h`)

- **Clock:** a sample counter; `fm1_ticks()` = samples × 24 MHz / 44.1 kHz, `fm1_ms` from it. A backgrounded page
  suspends audio, and time stops with it.
- **Input:** `fm1_in.buttons` / `notes` hold the level; press edges latch (a tap shorter than one UI frame still
  registers), `fm1_enc_take` returns accumulated steps. Buttons by printed label through `panel.btn[]` (the
  default table, no calibration).
- **LEDs:** `ui_leds` writes `fm1_led[]`; `sim_leds()` decodes it with `FM1_KEYMAP`, which the build extracts from
  `firmware/hal/fm1_input.h` into `build/sim/gen/sim_keymap.h` (not hand-copied).
- **LCD:** `lcd_fill` / `lcd_blit` into `fb` as `tests/ui_host.h` does.
- **Flash:** `hflash[1 MiB]` with `flash_host.h`; every erase / program marks the sector dirty.
- **Drift guard:** the host test parses `felucca.c`'s `#include "*.c"` list and fails, naming the file, when it
  has a source `sim_host.h` lacks (the excluded ones listed: usb, OTA, console, main, lcd, midi_uart).

## Panel and interaction

- Drawn in HTML/CSS (no photo): the FM-1's dark grey body, layout from `docs/panel.jpg`. Landscape: as the device.
  Portrait phones: screen with SELECT / ALGORITHM / PRESETS / MASTER beside it, KNOB 1–4 row, the 12-button block,
  OCT− / OCT+, the keyboard full width at the bottom. The screen is a `<canvas>` 240×240, scaled with
  `image-rendering: pixelated`.
- **Keys** (pills, the centre stripe is the LED): pointer down / up per pointer, so chords and multi-touch work;
  sliding across keys is not glissando (keeps step entry exact).
- **Buttons:** press / release; the label lights with its LED. Right-click (desktop) or long-press 600 ms (touch)
  latches a button held, for hold gestures with one mouse (SEQ held + key, OCT− held in TRACKS); a latched
  button shows an outline, a second tap releases it.
- **Encoders:** vertical drag (1 step per 12 px), mouse wheel, or hover + ↑ / ↓. MASTER: drag 0..1023, default 800.
- **Computer keyboard** (shown on a "?" overlay):
  - white keys F3–A4: `Z X C V B N M , . /`; B4–G5: `Q W E R T Y`;
  - black keys F#3 `S`, G#3 `D`, A#3 `F`, C#4 `H`, D#4 `J`, F#4 `L`, G#4 `;`, A#4 `'`, C#5 `3`, D#5 `4`, F#5 `6`;
  - Space PLAY, Enter REC, Esc HOME, ← / → OCT− / OCT+ (held while the key is held).
- **Start:** an overlay "Tap to power on" (browsers need a gesture to start audio); then boot.
- A "reset to demos" link (confirms in-page, no browser dialog) wipes the stored flash and reboots fresh.
- Page footer: GPL-3.0, Felucca credit, DEADACTIVE fork credit, link to the source.

## Storage

The 1 MiB flash image lives in the wasm memory. After each UI frame the worklet posts the dirty 4 KB sectors; the
page writes them to IndexedDB (`fm1-sim` / `flash`, key = sector offset). On load the page reads all stored sectors
and passes them in before `sim_init(0)`; none stored → `sim_init(1)`. IndexedDB missing or failing (private
mode): the sim still runs, saves last for the session, a small note says so.

## Demos (first visit)

`sim_init(1)` builds four projects and saves them to slots 1–4 with `project_save`, then loads slot 1, so the
current project is demo 1 and PLAY grooves at once: 1 = the drumsim 808 kit, 2 = 909, 3 = Plaits, 4 = mixed
(patterns as `tests/drumsim.c` KITS, 120 BPM, the same FX sends).

## Build

`tools/build_sim.sh` (one command from a fresh clone; needs Python 3 + Pillow, Docker, Node for tests):
1. generated headers into `build/sim/gen` (imports `tools/build.py`, sets its `GEN`, calls `generate()`), plus
   `sim_keymap.h`;
2. host test: `cc` builds `tests/sim_test.c` → runs it;
3. wasm: `emscripten/emsdk:<pinned tag>` in Docker, `-O2 -sSTANDALONE_WASM --no-entry`, exported API only, no JS
   glue → `build/sim/fm1sim.wasm`;
4. copies `web/sim/*` into `build/sim/`; node runs `tests/sim_glue.mjs` on the built wasm.

Output `build/sim/` is a self-contained static folder: `cd build/sim && python3 -m http.server 8001`.

## Testing

- `tests/sim_test.c` (plain cc, same sources):
  - a button press (EDIT), an encoder turn and a note key each change the framebuffer;
  - PLAY on demo 1 gives non-silent audio within 1 s; a stopped machine is silent (peak < 4 LSB) once the tails decay;
  - demos: four used slots; flash round trip — save, reboot on the same image (`sim_init(0)`), the pattern is back;
  - LEDs: PLAY's LED blinks while playing; a held note key lights its LED;
  - the include drift guard; UI frame cost measured and printed (budget: well under a 2.9 ms quantum).
- `tests/sim_glue.mjs` (node, the built wasm through `engine.js`): boot fresh, input events → frame changes,
  render non-silent after PLAY, dirty-sector messages after a save, a stored-sector reload round trip, the computer
  keyboard map is complete and has no duplicate keys.
- By eye and ear: the local URL and screenshots for the user.

## Requests for the firmware session (recorded in `docs/SIMULATOR.md`)

1. `web/make_site.py`: publish `build/sim/` under `build/site/sim/` when it exists (optional, for hosting next to
   the installer).
2. None in `firmware/**` needed so far.

## Isolation

New files only: `web/sim/**`, `tests/sim_*.{c,h,mjs}`, `tools/build_sim.sh`, `docs/SIMULATOR.md`,
`docs/superpowers/{specs,plans}/*websim*`. Build output only in `build/sim/`. Commits only on `web-sim`; no push,
no merge, no device contact.
