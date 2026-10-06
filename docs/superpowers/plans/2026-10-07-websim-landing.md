# FM-1 Drums landing page + installer — prototype plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** the first prototype of the landing page (simulator as hero) and the restyled English installer.
**Spec:** `docs/superpowers/specs/2026-10-07-websim-landing-design.md`
**Tech:** static HTML / CSS / ES modules, node tests, the existing `tools/build_sim.sh`.

## Global Constraints

- Allowed files: `web/sim/**`, `tests/sim_*`, `tools/build_sim.sh`, `docs/SIMULATOR.md`, `docs/superpowers/*websim*`,
  and (user exception, 2026-10-07) `web/index_pkg.html` with its script changed only as the spec lists.
- Never press Install, never request MIDI, never contact a device. Output only under `build/sim/`.
- Commits only on `web-sim`.

## Review Focus

- The installer still works in a browser with no MIDI (Safari / Firefox): the "Chrome or Edge" message shows.
- The installer's status / error texts all exist in the English table (a missing key would print `undefined`).
- Phone widths: the landing sections and the installer read at 390 px without horizontal scroll.

### Task 1: Installer guard and restyle

- [ ] Write `tests/sim_site.mjs`: guard (main's script at 12a1ba0 + the listed changes == new script, `TEXT` masked),
      keys kept, no `ja`, brand in `<title>` / `<h1>`. Run: FAIL (the page is unchanged).
- [ ] Restyle `web/index_pkg.html` (markup + CSS, English `TEXT`, the language lines, the label). Run: PASS.
- [ ] Commit.

### Task 2: Landing page

- [ ] Add to `tests/sim_site.mjs`: the landing page links `webapp/installer/` and `source.tar.gz`, has the sections
      (`#play`, `#features`, `#install`). Run: FAIL.
- [ ] Restructure `web/sim/index.html` / `sim.css` (top, hero, features, install, footer); `app.js` unchanged
      except what the layout needs. Run: PASS, plus `tools/build_sim.sh --ref main`.
- [ ] Commit.

### Task 3: Preview and docs

- [ ] `tools/build_sim.sh --preview`: `build/sim/preview/` = the landing page + `webapp/installer/index.html`
      (`make_site.strip_module`, placeholder meta). `docs/SIMULATOR.md`: the preview, the make_site request diff
      (landing at `/`). Screenshots desktop + phone of both pages.
- [ ] Commit.
