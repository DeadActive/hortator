# Landing page features: eight interactive pieces on the firmware's own DSP ("the lab") — design

Date: 2026-10-09. Branch `web-sim` (firmware 0.15.0). Replaces the features section of
`2026-10-07-websim-landing-design.md` (the sticky screen of recorded clips). Approved in chat.

## Goal

Each main feature of Hortator gets an interactive picture computed by the firmware's own code (compiled to wasm), with
sound on demand: a visitor turns a knob, drags a pad or holds a key and sees and hears what the FM-1 does. Bluetooth,
which has no sound to compute, is an animated diagram. ACID is left out for now.

## The eight pieces

| # | Piece | Interactive part |
|---|---|---|
| 1 | Drum voices | model chips (808 / 909, FM, MEMB, the Plaits ports, samples…) and the model's 4 knobs; the hit's waveform and spectrum redraw; a pad plays it |
| 2 | Resonators | an exciter through STRNG / PIPE / CHORD / MODAL: the ring's decay and harmonic comb; TUNE / DECAY / STRCT |
| 3 | Pump compressor | a loop, its waveform and the live gain-reduction trace; THR / RATIO / REL, bypass, GHOST |
| 4 | Two LFOs per voice | two wave displays (wave, morph, rate) and a hat pattern whose steps show the modulated values as it plays |
| 5 | Sequencer: probability and live | a 16-step grid with PROB / RATCH / fill per step; an 8-bar "dice" view of the steps that fired; live: mute at the next bar (waiting shown), MOTION knob recording |
| 6 | Grids | an X / Y pad and 3 densities; the K / S / H lanes update while dragging, looping |
| 7 | Effects, live and per track | the chain FILTER → RESON → DIST → SLICER → sends as a switchable signal path; PERFORM keys held while the loop runs, the buffer view showing repeat / reverse / freeze |
| 8 | Bluetooth host (alpha) | animated: scan, connect to an SMC-Mixer, MIDI flowing into the tracks; marked alpha; no sound |

Each is a full-width chapter: a title, two lines, the picture, "on the panel: …".

## Engine: the lab

- **Two instances of `fm1sim.wasm`** (no new download):
  - the **analyser**, in the page, silent: the pictures (a hit's waveform and spectrum, Grids lanes, the sequencer's
    dice over 8 bars), live from the first sight, no gesture needed;
  - the **player**, an AudioWorklet started on the first click on any piece: the piece's sound in real time (holding
    a PERFORM key, a mute at the next bar, dragging the Grids pad are heard at once), streaming probes back (gain
    reduction, LFO values, steps fired).
- **Lab C functions** (`tests/sim_lab.c`, in the same translation unit as `sim_core.c`): reset to a blank project;
  set a track's model, parameters and steps, globals; hold keys / buttons; render; probes. Parameter, global and model
  ids are generated from the firmware's enums at build time (`lab_ids.json`), so the page cannot drift.
- One piece plays at a time; leaving it (scrolling away) stops it. The hero's simulator is separate.

## Testing

- C (`tests/sim_test.c` / `tests/sim_lab_test.c`): every model renders a non-silent hit and models differ; RESON rings
  longer than dry; the compressor reduces gain when keyed; LFO probes move; PROB 50 % fires sometimes, not always; Grids
  lanes change with X / Y; PERFORM repeat changes the output.
- Node (`tests/sim_glue.mjs`): the lab JS on the built wasm (ids, analyser functions).
- Site (`tests/sim_site.mjs`): the eight chapters exist, each names real panel controls.
- Browser: each piece by eye, desktop and phone, in Chrome.

## Order

The lab foundation, then the pieces 1 to 8, each shown to the user as it lands.
