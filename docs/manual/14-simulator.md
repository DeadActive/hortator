# Web simulator
<!--blurb: Playing Hortator in the browser: mouse, touch and computer keyboard.-->

## What it is

The landing page at [deadactive.github.io/hortator](https://deadactive.github.io/hortator/) runs the **real firmware**, compiled to WebAssembly, on a 3D FM-1: the 240 by 240 screen, the panel (buttons, SELECT / ALGORITHM / PRESETS, KNOB 1 to 4, MASTER, 27 keys, the LEDs) and stereo audio. The UI, sequencer, drum engines and effects are the firmware's own sources, so the page follows every release. The simulator runs the **plain version**: it has no Bluetooth page. SCL is labelled SEL there.

The page also has a short guide for each area, and interactive pieces that run the firmware's own DSP: voices, resonators, the compressor, LFOs, the sequencer's chance and live hands, Grids, effects and PERFORM.

## Storage

The simulator's flash lives in your browser (IndexedDB). Anything you save with SAVE stays there; as on the device, the last project saved loads at power-on. With no project remembered the demos are loaded into the slots. **Reset to demos** on the page clears the store. A browser that blocks storage still runs the simulator, with saves lasting only for the session.

## Mouse and touch

- Keys and buttons sound while held; multi-touch works.
- Knobs: drag up or down (12 px per detent), use the mouse wheel, or hover a knob and press the up and down arrow keys.
- MASTER is a pot (0 to 1023, default 800).
- To hold a button for a combination (SEQ held plus a key, OCT- held in TRACKS), **right-click** it or **long-press** it for 600 ms. A dashed outline shows it is held; tap it again to let go.

## Computer keyboard

By physical key, on any layout:

| Keys | Plays |
| --- | --- |
| `Z X C V B N M , . /` | white keys F3 to A4 |
| `S D F H J L ; '` | black keys F#3 G#3 A#3 C#4 D#4 F#4 G#4 A#4 |
| `Q W E R T Y` | white keys B4 to G5 |
| `3 4 6` | C#5 D#5 F#5 |
| Space | PLAY |
| Enter | REC |
| Esc | HOME |
| Left / Right arrow | OCT- / OCT+ |

Autorepeat and shortcuts with Cmd, Ctrl or Alt are ignored. Leaving the tab releases everything held.

## Building and testing it

`tools/build_sim.sh` builds the simulator (needs Docker, Node 18 or newer, Python 3 with Pillow and a C compiler); `--preview` writes the site's layout, `--ref` builds another commit. See `docs/SIMULATOR.md`.

## This manual

This manual is a single page built by `tools/build_manual.py` from the section files in `docs/manual/`. `web/make_site.py` publishes it at `/manual/` on the site. Search is entirely in the page.
