# Machinedrum-style screen UI: design

Date: 2026-10-19. Branch `md-ui` (worktree `felucca-mdui`) from `main` = drum 0.20.0 (2d8dad6). Drum machine fork:
2026 DEADACTIVE. Mockups: `2026-10-19-md-ui/` next to this file (PNG renders + `md.js`, the mockups' drawing code
and the reference font tables). References outside the repo (Elektron material, not for the GPL tree):
`~/Desktop/dev/fm1-drummachine/md-ui-ref/` (emulator LCD captures, knob-turn frames, manual-derived 128×64 bitmaps,
the capture host `mdshot.m`).

## 1. Intent

Redraw the FM-1's 240×240 screen in the visual language of the Elektron Machinedrum (SPS-1/UW) LCD: dark ink on a
red backlight, the MD's square pixel font, round knob dials, dotted dividers, inverted bars and title tabs, shadowed
popup windows, dithered fills. Every screen keeps its current structure and information; only the look changes,
plus the few layout moves below (top bar, knob row, HOME, popups). Other colour schemes stay possible: MD red is
one theme of several.

**Success:** on the device and in the web sim, every screen reads as an MD LCD; nothing a user can see or do today
is lost; all four themes are legible.

**Anti-goals:** a blurry or smoothed look (no anti-aliasing anywhere); a reinvented UI that needs relearning; a
redesign that costs audio CPU or slows the screen visibly.

### The user's decisions (2026-10-19)

| Question | Answer |
| --- | --- |
| How literally? | **Crisp 240×240, MD-styled** (not a 120×120 fat-pixel emulation) |
| Scope | **Every screen** |
| Renderer | **Extend ours** (not port upstream Felucca 1.6.1's AA renderer) |
| Font | **Pixel font, no smoothing** |
| Font scale | **MD glyphs at 2×** (3×5 → 6×10); lines, dividers, knob rings stay 1 px |
| Themes | **MD RED** + **RED ON BLACK**, **WHITE ON BLUE**, **BLACK ON GREEN**; "leave space for other schemes" |
| Page layout | **A**: inverted top bar, compact knob row (44 px), graph area, trig strip, breadcrumb |
| Other screens | **Restyle today's screens** (keep structure; MD vocabulary) |
| HOME | **MD main-screen strip** under the top bar, **4 small knobs, values below**, scope takes the rest |
| TRACKS | level boxes with a **dithered background**; **keep everything TRACKS shows today** (steps, live meter, arm dot) |
| Trig strip | steps must **not look tied** (wider gaps) |
| BOOT / ABOUT logo | **a new abstract 1-bit pixel-art logo** (not a conversion of today's glow art) |

## 2. Colours and themes

Sampled from the Gearmulator MD emulator: LCD background `#e0472b`, ink `#38100a` (bezel `#67463c`, surround
`#20211f`, not used on the device).

A theme is **two colours**, BG and INK. There is no third tone: where today's UI uses dim/gray tones, the new UI uses
the MD's tools: dither (a 1-on/1-off checkerboard of INK), dotted lines, outline vs solid, inverted vs plain.
*(My call; it is what makes it read as a monochrome LCD. If a builder finds a screen that cannot work in two tones,
add a derived DIM = 50 % mix as a fallback and say where.)*

| Theme | BG | INK |
| --- | --- | --- |
| MD RED (default) | `#e0472b` | `#080000` (was the emulator's `#38100a`: grey-brown on the FM-1's panel, the user 2026-10-19) |
| RED ON BLACK | `#170605` | `#f0532f` |
| WHITE ON BLUE | `#2445c8` | `#eaf1ff` |
| BLACK ON GREEN | `#a3bf3c` | `#1c2a08` |

- The table replaces today's five palettes (GREEN AMBER CYAN RED MONO) in MENU > COLOR. Adding a theme = one row.
- Stored where COLOR is today (settings `palette` bits 0..7, `project.c`). Old ids map *(my call)*: RED and AMBER →
  MD RED, GREEN → BLACK ON GREEN, CYAN → WHITE ON BLUE, MONO → RED ON BLACK.
- MD RED is the default for new settings *(my call)*.
- The values are mockup values for the panel as rendered in a browser. The FM-1's IPS panel may need adjusting by
  eye on the device (the user reviews by eye; give screenshots and device photos, never a metric).
- `C_ALERT` (CPU) and `C_WHITE` (touched) go: alerts and touched values are shown by inversion (§5, §6).

## 3. Fonts

All 1-bit, drawn INK on BG (or BG on INK when inverted), no blending. Glyph tables are in `md.js` (`F`, `BIG`,
`ICON`); `tools/gen_font.py` should generate the C tables from a source the builder chooses (an ASCII-art text file
like `md.js`'s strings is fine).

- **F3: MD 3×5 caps** (the MD's square font: A = `###/#.#/###/#.#/#.#`, M = `#.#/###/#.#/#.#/#.#`, N looks like an
  `N` squeezed into 3 px) drawn at **2×** (6×10, advance 8 px; `:` `.` `!` `'` are 1 px wide → 4 px). The workhorse:
  labels, values, lists, titles, breadcrumb, top bar. About 28 characters per line.
- **F3 at larger scales** (3×–6×) for big words: engine names (`K909` at 5×), `OFF` (6×), the NAME editor (5×).
- **BIG: MD tempo digits** 5×8 with 2 px vertical strokes (`1` is 3 wide), at 2× (10×16) and 3× (15×24): BPM in
  the TEMPO popup and HOME-style big numbers. Digits only; `.` and the decimal digit use F3.
- Lowercase is drawn as caps. Glyphs needed beyond the table in `md.js`: whatever today's strings use (audit
  `cv_text` call sites; e.g. `&`, `°`, `"`). Non-ASCII Latin-1 in names: map to the nearest cap or `?`.
- Terminus (`assets/fonts/ter-u16n.bdf`) and its OFL notice go once nothing uses it.

## 4. Primitives (gfx.c)

Kept: the strip canvas `cv_px[240*124]`, `cv_blit`, `cv_rect`, `cv_line`, region signatures and dirty checks.
New or changed:

- `cv_text(x, y, font, scale, str, ink)`: 1-bit glyphs at an integer scale; `text_w` to match.
- `cv_dither(x, y, w, h, ink)`: INK where `(x + y) % 2 == 0` in **screen** coordinates (so dither lines up across
  strips).
- `cv_hdot` / `cv_vdot`: 1-on/1-off dotted rules.
- `cv_frame`: 1 px outline.
- `cv_ring(cx, cy, r)`: midpoint circle, 1 px (radii 7, 8, 13 used).
- `cv_knob(cx, cy, r, v, opts)`: ring + 12 o'clock tick (1 px, 2 px above) + two end-stop dots at 7:30 and 4:30
  (2×1 px, 2 px outside) + **value blob** 3×3 at radius r−3.5 on a 270° sweep (7:30 = min, 4:30 = max) + optional
  LFO dot (§6). Integer maths or a small table; no float on the device.
- `cv_popup(x, y, w, h, title)`: BG fill, 1 px top/left edge, **2 px right/bottom edge** (the MD's drop shadow),
  notched corners (1 px BG at three corners), title tab centred on the top edge (INK box, BG text).
- `cv_tab(x, y, str)`: inverted title tab (INK box, BG text, 13 px tall).
- `cv_keycap(key, action)`: 1 px outlined box around a key name, the action after it (`[OCT+] OK`).
- `cv_button(str, on)`: outlined box (off) / INK box with BG text (on).
- Theme: `pal[]` becomes `{bg, ink}`; the canvas stores RGB565 as now. *(Builder's option: a 1-bit canvas expanded
  to RGB565 at blit time would cut the strip from 59 KB to 4 KB and make theme switches free. Not required.)*
- Icons (`icons.c`, 12×12 2-bit): redraw the ones still used as 1-bit MD-style glyphs (5×5 at 2× like `ICON` in
  `md.js`: play, stop, record armed/on, USB plug, CPU chip, battery). Icons next to labels in today's columns go
  (the MD has none); keep only icons that carry state.

## 5. The page layout (layout A)

Every parameter page (`PAGES[]`, `pages.c`), coordinates in screen pixels (see `layout-a-vs-b.png`, card A, and
`screens.png`):

| Region | y | Content |
| --- | --- | --- |
| Top bar | 0..15 | INK fill; BG content (below) |
| Knob row | 17..59 | 4 cells × 60 px, dotted dividers at x 60/120/180 |
| rule | 60 | solid |
| Graph | 61..180 | the page's graph (§8), title tab at its top-left where it has a title |
| rule | 181 | dotted |
| Trig strip | 186..203 | 16 trigs |
| Breadcrumb | 212..238 | INK fill, BG text |

Outer 1 px INK frame around the screen.

**Top bar** (replaces `draw_head`, same items, same signature logic): state glyph at x 4 (■ stopped, ▶ playing,
outlined ● REC armed, solid ● recording), BPM, an inverted **FX** tag when PERFORM's knob effects are on, the mode
text (KEYS / STEP REC nn) or `SONG n`, `T<n>` at x 150, the CPU chip (only on overload, §6), USB plug (when
configured), battery (outline + 3 cells). While BPM changes, the **TEMPO popup** opens instead of brightening the
number (§7).

**Knob cell** (60×43): label centred at y+2 (F3 2×); dotted rule under it; then
- a **continuous** parameter: knob r 8 at (x+17, y+28) and its value right-aligned at x+55;
- a **discrete** parameter (enums: TYPE, WAVE, MODE, ON/OFF, slots…): its text centred, no knob (the MD's rule).
Values are at most 4 characters (`16K`, `-24`, `151`, `1.1`); the unit is implied by the label. *(My call:)* while a
knob is turned, the breadcrumb shows the full value with its unit for the touched time (`CUT 16.0 KHZ`), then
returns.

**Breadcrumb** (replaces the footer text): `T1>K909>SOUND` (track, engine, page; `>` is the MD's ▸ glyph) left,
the page number (`1/3`) right. Global pages: `PROJECT`, `TRACKS`, `MENU`… The MIDI LEARN footer line and other
footer messages use this bar.

**Trig strip** (replaces the footer step strip): 16 boxes 10×10 on a 14 px pitch, +2 px extra between beats
(x = 6 + i·14 + ⌊i/4⌋·2), solid = trig, outline = empty, beat dot (2×2) under steps 1/5/9/13, playhead = an outline
2 px outside the box (it never touches a neighbour). Pages longer than 16 steps keep today's paging.

## 6. Knob behaviour

From the emulator (frames in `md-ui-ref/knob_turn/`) and the MD manual (OS 1.63, p.9, p.48): the MD has no
"selected knob" on its main pages; the blob in the ring is the value; while turning, the number appears in the
cell and stays ~5 s; a parameter lock shows its cell inverted while the trig is held; the arch "clip" links two
dependent parameters (it is **not** an LFO mark).

On the FM-1 (see `md-ui-ref/mockups/knob-states.png`):

1. **Idle:** blob in the ring + the number beside it (the FM-1 cell has room; it stays visible).
2. **Touched** (today's `ui.hot_col` / `hot_t`, which make the value white): the number is drawn **inverted**
   (INK box, BG digits) for the same time, and the breadcrumb shows the full value (§5).
3. *(Dropped, my call after review:)* the MD's "hold a trig → locked cells inverted" has no counterpart today (the
   sound pages have no step keys; MOTION is never shown per held step), so it would be a new interaction, not a
   restyle. A possible later feature. Inversion stays reserved for "touched" and cursors.
4. **LFO-modulated:** a hollow 3×3 dot just outside the ring follows the live modulated value (replaces today's
   amber 2×2 dot on the gauge). Not an MD feature.
5. **Linked pair:** the MD clip (`..###.. / ##...## / ##.#.##` at 2×) over the divider between two dependent
   parameters. Which pairs: the builder lists candidates (e.g. LFO RATE↔DEPTH, COMP THR↔RATIO) and asks the user.
   *(Optional; no pair is required.)*

CPU overload: the chip glyph in the top bar, blinking at 2 Hz *(my call; replaces C_ALERT red)*.

## 7. Popups and messages

- **Messages** (`ui.msg`, today they replace the top bar): a small popup centred over the graph area (`SAVED A1`),
  F3 2×, same timing. The top bar stays.
- **Confirms** (`CLEAR TRACK 1?` etc., today full screen): `cv_popup` 184×74 over the live page, title tab with
  the verb (`CLEAR`), the question, keycaps `[OCT-] NO   [OCT+] YES`.
- **TEMPO popup:** while BPM is changed, a popup with `TEMPO` tab and the value in BIG 3× (`120.0`), MD's help
  icons optional.
- **Zoom popup** (today a black 150×50 box, FONT_L): a `cv_popup` with the value in F3 4× or BIG 3×.
- **MENU** (HOME held): a popup window over the page (`MENU` tab), rows on 15 px, the cursor = inverted row bar,
  values right-aligned, COLOR shows the themes as 10×10 boxes (current filled), keycap help at the bottom.

## 8. Screens

Same structure and information as today (`build/ui_shots/` on main is the reference for "today").

| Screen (graph) | Restyle |
| --- | --- |
| **HOME** 1/3 | see §9 |
| SOUND ×4 (`GR_MODEL`) | engine name in F3 5× centred, `1 VOICE` under it. The knobs stay as today (SOUND 1 = MODEL TUNE DECAY SWEEP, `snd_list`); the mockup's labels are illustrative |
| LAYER ×2, DLY, REVERB, CHORUS, GLOBAL ×2, SYSTEM, TOOLS (`GR_NONE`) | knob row only; graph area empty (or the page's existing text, in F3) |
| FILTER ×2 (`GR_FILTER`) | curve 1 px + dithered area under it; OFF state = `OFF` F3 6× between two 3 px dither bands |
| FX (`GR_FX`) | send levels as LEV-style vertical bars (solid fill, tick dashes) |
| SLICER (`GR_SLCR`) | pattern bars: solid = on, outline = off |
| RESON ×2 (`GR_RESON`) | as FILTER (OFF state the same) |
| COMP ×2 (`GR_COMP`) | `COMP` tab; SRC / DUCK readouts left; GR as a horizontal LEV meter with tick dashes; transfer curve in an 84 px box with a dotted 4×4 grid |
| STEP (`GR_GRID`) | velocity bars: accent = solid, normal = outline + dither, empty = 2 px baseline; `[TAP] ON/OFF  [HOLD] ACCENT` as keycaps |
| PATTERN (`GR_STEPS`), piano roll | bars/boxes as STEP; roll notes as solid 1-row bars, dotted beat lines |
| MOTION (`GR_MOTION`) | 16 event boxes (solid = event), playhead outline |
| SONG (`GR_SONG`) | MD song table: rows on 14 px, ▸ cursor, inverted current row, 1 px column rules |
| PROJECT (`GR_SLOTS`) | `PROJECTS` tab; list rows on 16 px, cursor = inverted bar, `NOW` right |
| GRIDS ×2 (`GR_GRIDS`) | map dots 2×2, density bars solid, dotted axes; EUCLID rings use `cv_ring` |
| LFO ×2 (`GR_LFO`) | `LFO n` tab; wave 1 px over a dotted zero line, dithered area, phase dot 3×3; `RATE:SYNC` etc. in F3; MD's big vertical dithered `LFO` on the right *(optional: the mockup's version needs redrawing)* |
| TRACKS (`GR_MIX`) | **Everything `draw_mix` shows today** (ui_draw.c ~806), restyled; 8 rows: number, arm dot, model (cursor = inverted number+model only); the **16 step cells of the current bank** (small solid/outline cells, playhead outline, as the trig strip but smaller); **level box with a dithered background**, the solid bar = the fader setting, the **live peak meter** as a 1 px INK tick (or a BG gap) moving over it; mute box (as today's meaning). No pan box (not on TRACKS today). The `screens.png` TRACKS card is superseded on content, kept for the level-box look |
| PERFORM | buttons as `cv_button` (outline = off, solid = held/on), `BLACK KEYS: FX  WHITE: MUTE` |
| MUTE (GLO held) | **8 boxes** (one per track), each keeping today's content: track number, model, the key that toggles it; fill keeps today's meaning (filled = audible, *my call*); a mute waiting for the bar blinks as today; the NOW / NEXT BAR line and `G4 LEARN` status below in F3 |
| NAME | name in F3 5× with a block cursor; character grid on 17×20 px cells, cursor = 1 px outline; keycap help (`[F#] LEFT` …) |
| MENU, ABOUT | §7; ABOUT shows the new logo (§9a) above its text in F3 |
| Confirms, messages, zoom | §7 |
| BOOT, SAFE START | the new logo (§9a) in INK on BG of the stored theme; SAFE START text in F3 |
| BT page (BLE build) | same vocabulary: list rows, inverted cursor |

## 9. HOME

See `home.png` (HOME B v3). Top bar as every page. Under it, a 52 px strip in the MD main screen's arrangement:

| x | Content (y 17..67) |
| --- | --- |
| 1..61 | 4 bank boxes (10×6): which 16-step bank of the selected track the playhead is in (as many boxes as the track has banks, 1..4; current solid) · inverted project box: the slot `PROJ A` / the project name (2 lines) · mode line: `KEYS`, `STEP REC nn` or `SONG n` while active, otherwise blank *(my calls after review: the mockup's `PRJ:A1` and `>P01` were invented data)* |
| 62 | solid rule |
| 63..86 | `LEV` + a 6 px vertical bar from the bottom, 2 px tick dashes every 4 px (the selected track's level) |
| 87 | solid rule |
| 88..238 | 4 cells × 38 px (dotted dividers): label (4-letter MD abbreviation: `TUNE DEC SWP CLIK`), knob r 7 at y 41, value centred under it at y 54 |

Then rule y 68, **SCOPE** (tab, level readout right, wave 1 px over a dotted zero line, about 110 px tall), trig
strip, breadcrumb `T1>K909>SOUND   HOME 1/3`. The 4 knobs are what HOME's knobs are today: the selected track's
model's first four sound parameters (`home_param`, ui.c ~508, P_E0..E3). Four-letter labels: each parameter gets
an MD-style abbreviation for narrow cells (`params.c` already has short names, e.g. `COLR`; reuse or add a 4-char
field).

COMP 2/3 and 3/3 (HOME's other pages) use the normal layout A.

## 9a. Logo

The user's answer: **a new abstract 1-bit pixel-art logo** for BOOT and ABOUT, replacing both today's 7-level glow
boot image (`assets/boot.png`, `gen_boot.py`) and ABOUT's 4-bit logo (`LOGO_DATA`, `gen_logo.py`) with one 1-bit
asset drawn in INK. "Abstract": a mark, not a lettered wordmark; the name `HORTATOR` may sit under it in F3.
Before building it, mock 3 to 4 options (in the MD vocabulary: hard pixels, dither allowed, about 96 to 160 px
wide) and let the user pick by eye. The asset tool becomes a 1-bit converter (PNG → packed bits).

## 10. Web sim and tooling

- The sim runs the firmware's drawing code; nothing changes in its pipeline. The page chrome (`web/sim/sim.css`
  `--c1..--c5`) may follow the theme; not required.
- `tests/ui_test.c` `snap_page` and `tests/run_drum_tests.sh` regenerate `build/ui_shots/`; review them by eye for
  every page in every theme (an index page of all shots per theme helps the user review).
- Build the package and `build/site` after each change (the user's rule); no "install again" steps in checklists.

## 11. Constraints

- No audio-side cost: UI drawing stays in the main loop with today's dirty signatures.
- Flash: XIP is 568 KB; 1-bit font tables are tiny (a few KB) and replace Terminus' 4-bit tables; net flash should
  drop. Report the size change.
- RAM: no new large buffers required.
- Licensing: the MD look is a style reference; no Elektron code, fonts files, bitmaps or screenshots go into the
  repo. Glyph shapes are re-drawn by hand (`md.js`), credited to DEADACTIVE.

## 12. Left to the builder

- Exact pixel placement on pages not mocked (follow §5 and the closest mocked screen).
- Order of work (suggested: primitives + fonts + theme → layout A frame → knob cells → pages by graph type →
  popups → HOME → MENU/NAME/PERFORM/MUTE → logo options to the user (§9a) → BOOT/ABOUT).
- Which icons survive (§4), the glyph additions (§3), the 4-letter abbreviations (§9).
- Whether a linked-pair clip is worth it (§6.5), with the user.
