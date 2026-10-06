# FM-1 Drums landing page and installer — design (first prototype)

Date: 2026-10-07. Branch `web-sim`. Builds on `2026-10-06-websim-design.md`. Approved in chat. This is a first
prototype; the sections get refined one at a time afterwards.

## Goal

The project's public home: a visitor learns what FM-1 Drums is, plays it in the browser (the simulator is the
hero), sees what it does, and can install it on their FM-1 through a restyled English installer.

## Name and voice

"FM-1 Drums" (the firmware's boot screen): custom drum-machine firmware for the M-VAVE FM-1, by DEADACTIVE, built
on Felucca. Plain, sentence-case English; the panel's own words (PRESETS, FX, SEQ…) where the device uses them.

## Landing page (`web/sim/index.html`, `sim.css`, `app.js`)

1. Top: name, one sentence, two actions: "Play it below" (scrolls to the device) and "Install on your FM-1"
   (`webapp/installer/`).
2. Hero: the simulator as it is (Switch on, panel, Controls, Reset to demos).
3. What it does: 8 tracks of drum models; the sequencer (64 steps, PROB / RATCH, swing, live recording, mutes);
   Grids; the compressor with sidechain and GHOST; LFOs; RESON; FX (SLICER, delay, ROOM / SPRING reverb, chorus,
   BASS+). Each with where to find it on the panel. Text only: the page does not drive the simulator.
4. Install: the steps (Chrome or Edge, USB cable straight to the computer, Install), beta / own-risk warning,
   recovery (Transporter), back to official (M-UPGRADE); a button to `webapp/installer/`.
5. Footer: credits (FM-1 Drums by DEADACTIVE on Felucca by Leo Kuroshita / Hügelton Instruments; Terminus font;
   CC0 samples VSCO-2 CE / VCSL), trademarks, GPL-3.0 with the `source.tar.gz` link.

## Installer (`web/index_pkg.html`, user-granted exception)

- Same visual system as the landing page; numbered steps; status and progress prominent; the log in a closed
  `<details>`; warnings grouped.
- English only: the `ja` strings and the language switch go. Copy rebranded to FM-1 Drums; the link to Felucca's
  synth editor goes (the drum firmware has none yet).
- Script: changed only in (a) the `TEXT` table, (b) the language lines (`lang` fixed to `"en"`, no switch),
  (c) the status label `Felucca ${meta.version}` → `FM-1 Drums ${meta.version}`. `/*LIB*/` and `/*META*/` stay.
- Guard (`tests/sim_site.mjs`): `main`'s script at 12a1ba0 with exactly those changes applied (the `TEXT` table
  masked in both) must equal the new script byte for byte; every key the old English table had (except `other`,
  `editor`) still exists; no `ja` table; the page names FM-1 Drums.

## Site layout (request for the firmware session: `web/make_site.py`)

`/` the landing page (with `fm1sim.wasm` and its files), `/webapp/installer/` the installer, `/firmware/` as now.
Local preview: `tools/build_sim.sh --preview` writes `build/sim/preview/` in that layout, the installer assembled
with `make_site.py`'s own `strip_module` and a placeholder package (it shows "could not load"; Install is never
pressed in the preview).

## Testing

`tests/sim_site.mjs`: the installer guard above; the landing page links `webapp/installer/` and `source.tar.gz`;
the existing simulator tests stay green. Screenshots of both pages, desktop and phone.
