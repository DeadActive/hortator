# Features lab plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [x]`) syntax.

**Goal:** the eight interactive feature pieces of `2026-10-09-websim-features-lab-design.md`, on the firmware's own
DSP.
**Spec:** `docs/superpowers/specs/2026-10-09-websim-features-lab-design.md`
**Tech:** C (lab exports in the simulator's translation unit), wasm (Docker emcc), ES modules, Canvas 2D,
AudioWorklet.

## Global Constraints

- New or changed files: `tests/sim_*`, `web/sim/*` (flat: `tools/build_sim.sh` copies files only), `tools/build_sim.sh`,
  `docs/SIMULATOR.md`, `docs/superpowers/*websim*`. Never `firmware/**`.
- Firmware identifiers as the map found them (2026-10-09): `trk[]`, `P_*` (`P_LEVEL`, not P_LVL), `DM_*`, `N_MODEL`,
  `TP[]`, `GP[]`, `track_desc()`, `drum_set_model`, `drum_hit`, `track_render`, `trk_inserts`, `comp.gr`,
  `t->lfo[l].out`, `grids_preview(ch, i)`, `perf_press(e, down)`, `PF_*`, `motion_capture`, `track_mute_toggle`,
  `settings.mutebar`, `song.mute_q`, `seq_start` / `seq_stop`, `G_CLOCK` 0.
- One piece plays at a time; visuals never wait for audio.
- Build output only under `build/sim/`; no device contact; commits on `web-sim`.

## Review Focus

- A piece left mid-sound (scrolled away, tab hidden): the player stops; nothing keeps running.
- Rapid knob drags on the analyser: renders coalesce (one per frame), the page stays responsive.
- Phones: every piece usable by touch at 390 px; pads and knobs large enough.

### Task 1: Lab C exports and ids

- [x] Test (`tests/sim_lab_test.c`, built and run by `tools/build_sim.sh`): `lab_reset`; every model's single hit via
      `lab_voice` is non-silent and the 909 kick differs from the 808 kick; RESON STRNG rings longer than dry;
      `lab_grids` lanes differ between two X / Y points; a loop with the compressor keyed by track 1 shows `gr < 0`
      in the probes; LFO probe values move over a bar; PROB 50 % hats fire on some bars, not all (8 bars);
      `perf_press(PF_R16)` changes the rendered output. Run: FAIL (no sim_lab.c).
- [x] `tests/sim_lab.c` (included by `sim_core.c`): `lab_reset()` (a blank project, demos off, tempo 120, clock INT),
      `lab_model(t,m)`, `lab_param(t,p,v)`, `lab_global(g,v)`, `lab_step(t,s,on,acc,cond,rat)`, `lab_play(on)`,
      `lab_hit(t,vel)`, `lab_perf(e,down)`, `lab_voice(frames, inserts)` (track 0 alone, raw or with RESON + FILTER),
      `lab_grids(out)` (3 x 32 bytes via `grids_preview`), the probe ring: per audio half `{comp.gr, lfo out x 2 x 8,
      fired bits, seq_idx x 8}` read by `lab_probes()` / `lab_probe_count()`.
- [x] `tests/sim_ids.c`: prints `lab_ids.json` (P_*, G_*, DM_*, PF_*, RS_*, LW_*, FT_* with values; TP / GP labels, min,
      max, def; each model's edit labels / min / max / def; the name tables). `tools/build_sim.sh` builds and runs it;
      exports added to the emcc list. Run: PASS. Commit.

### Task 2: lab.js (analyser and player)

- [x] Node test: the analyser on the built wasm: `voice()` returns a non-silent buffer, `grids(x, y, fills)` 3 lanes,
      ids load. Run: FAIL.
- [x] `web/sim/lab.js`: `Analyser` (an `Fm1Sim`-like instance in the page: lab calls, `voice`, `grids`, `dice(bars)`),
      `Player` (an AudioWorklet `fm1lab` in `worklet.js`: messages for the lab calls, probes posted per frame), one
      AudioContext started on the first gesture, `stopAll()`. Run: PASS. Commit.

### Task 3: The chapters' frame

- [x] Site test: eight chapters `#f-voices … #f-ble`, each with its panel hint. Run: FAIL.
- [x] Replace the features section: chapter markup and CSS (title, two lines, the stage, "on the panel"), a
      play / stop button per chapter, lazy start of the analyser when the section nears the screen, the player stopped
      when a chapter leaves it. Remove the sticky-screen player and its CSS. Run: PASS. Commit.

### Tasks 4 to 11: the pieces, one each

For each: the visuals on the analyser; the sound on the player; a C or node test for what it computes; a check by
eye in Chrome (desktop and phone); a commit; shown to the user.

4. Drum voices: model chips, 4 knobs (the model's edit labels), waveform + spectrum (JS FFT of `lab_voice`), a pad.
5. Resonators: exciter choice, RESON model, TUNE / DECAY / STRCT; dry vs ringing waveform, the comb in the spectrum.
6. Pump compressor: a kick + hats + pad loop keyed by the kick; THR / RATIO / REL, bypass, GHOST; waveform + GR trace.
7. Two LFOs: wave, morph, rate per LFO drawn; a hat loop with LFO 1 on TONE and LFO 2 on DECAY; the probe values
   per step.
8. Sequencer: a 16-step grid (tap, PROB, RATCH, fill); the 8-bar dice view (`dice`); live: mute at the next bar,
   MOTION record.
9. Grids: the X / Y pad and fills; the lanes (`grids`) while dragging; the loop on the player.
10. Effects: the chain switchable on a loop; the PERFORM keys held; the buffer view.
11. Bluetooth (alpha): the animated diagram.

### Task 12: Docs and review

- [x] `docs/SIMULATOR.md` (the lab), full build and tests, the whole page on desktop and phone. Commit.
