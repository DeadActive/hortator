# Landing page HERO: the FM-1 in 3D, arriving into the simulator — design

Date: 2026-10-07. Branch `web-sim`. Refines the landing page (`2026-10-07-websim-landing-design.md`), section 1–2.
Approved in chat: CSS 3D on the live panel (option 1); a recorded reel, not the firmware, until the device lands
and is switched on.

## Choreography

- The hero is a scroll track (about 3 viewport heights) with a sticky full-screen stage.
- At scroll 0 the FM-1 lies far back on a tilted desk plane: rotated and pushed back in 3D, blurred like a shallow-
  focus photograph, dimmed. The headline sits sharp in front of it.
- Scrolling moves a progress p from 0 to 1. The headline lifts and fades in the first third. The device rotates
  flat, comes forward, its blur clears, a light sweep crosses the aluminium.
- At p = 1 the device sits exactly where the simulator lives (flat, sharp, full size). The Switch on button appears
  and the page continues to the next sections.
- "Play it below" scrolls to the end of the track.
- `prefers-reduced-motion`: no track, no 3D, no blur; the device is flat from the start.

## The 3D device (CSS only)

- The live panel (`#device`, its elements from `app.js`) is the 3D object. `transform-style: preserve-3d`, with
  parts lifted on Z: keys, buttons, knob skirts and caps, trays sunk, the LCD recessed in its bezel.
- Thickness: the body outline repeated in about 12 layers behind the face (an extruded rounded slab), darker toward
  the back. A soft contact shadow on the desk plane.
- The blur is on the scene container, never on the 3D object (a filter there would flatten it).
- During p < 1 the panel ignores input (pointer events off).

## The reel (`tests/sim_record.c` → `build/sim/reel.bin`)

- The recorder drives `sim_core.c` headlessly for 30 s at 15 fps: demo 1 playing, visiting HOME (scope), SEQ (step
  grid on track 1, then track 2), EDIT with knob sweeps, FX REVERB, ARP (Grids) with X / Y moving, HOME COMP, REC
  (TRACKS), back to HOME. Start and end on HOME, so the loop's seam is a page change like the others.
- Format "FM1R" v1, little-endian:
  - header: `"FM1R"`, u16 version, u16 fps, u32 frames;
  - per frame: u32 button LEDs, u32 key LEDs, i8 × 8 encoder steps (7 roles, 1 pad), u32 runs, then the runs;
  - a run is u16 start, u16 length and the RGB565 pixels. Frame 0 is one full run; later frames hold only the
    pixels that changed.
- The build gzips it; the page inflates it with `DecompressionStream('gzip')`.
- `web/sim/reel.js`: `parseReel(arrayBuffer)` → `{ fps, frames, apply(i, fb) }`, where `apply` replays frame i's
  runs onto `fb` (frame 0 resets).
- The page plays the reel on the LCD, the LEDs and the knob caps until Switch on; then the worklet starts, as now.

## Tests

- `tests/sim_glue.mjs`: the reel parses, has 450 frames at 15 fps, replaying every frame from frame 0 rebuilds the
  recorder's last frame (its hash, written next to the reel), and the gzip file is under 1.5 MB.
- The existing simulator and site tests stay green.
- By eye: screenshots of the scroll at p = 0, 0.5 and 1, desktop and phone, in Chrome. Safari cannot be driven
  here; the user checks it.
