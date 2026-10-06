# Landing HERO plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** the hero of `2026-10-07-websim-hero-design.md`: a recorded reel, and the live panel as a CSS 3D object
that arrives into the simulator on scroll.
**Spec:** `docs/superpowers/specs/2026-10-07-websim-hero-design.md`

## Global Constraints

Same file rules as the landing plan; output under `build/sim/`; no device contact; commits on `web-sim` only.

## Review Focus

- Scrolling back up after Switch on: the live device tilts away again but keeps running (sound continues).
- A window resize mid-track: the end state still matches the simulator's layout (landscape ↔ portrait).
- Reduced motion: no pinned track, the device is flat and usable at once.

### Task 1: The reel

- [ ] Test (`tests/sim_glue.mjs`): parse `build/sim/reel.bin.gz` via `web/sim/reel.js`; 450 frames, 15 fps; full
      replay equals the recorder's `reel.hash`; size < 1.5 MB. Run: FAIL (no reel).
- [ ] `tests/sim_record.c` (the performance script, the FM1R writer) and `web/sim/reel.js`; `tools/build_sim.sh`
      builds and runs the recorder, gzips the reel. Run: PASS.
- [ ] Commit.

### Task 2: The 3D hero

- [ ] The hero track and sticky stage in `index.html`; the 3D layers and depth in `app.js` / `sim.css`; the scroll
      progress driver; the reel on the LCD, LEDs and knobs until Switch on; reduced motion. Site test: the hero
      section exists and the device is inside it. Run: FAIL, then PASS.
- [ ] Screenshots at p = 0, 0.5, 1, desktop and phone. Commit.
