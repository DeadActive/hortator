# ENSEMBLE Stage 1 (shell and sound) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A new firmware repo, ENSEMBLE (working name), forked from Felucca 1.5, whose HOME shows four role tracks
(PADS BASS KEYS LEAD) with per-preset macros on KNOB 1–4, role-filtered PRESETS, the deep editor (with a MACRO page)
behind EDIT, and a CPU baseline from BENCH.

**Architecture:** Felucca is a C unity build (`firmware/src/felucca.c` `#include`s every `.c`; everything `static`).
Two new units, `role.c` (roles and the preset-list filter) and `macro.c` (macro types, default sets, mapping, the
per-track macro state), are included just before `ui.c`. Felucca's UI is changed at a few named hook points (HOME's
columns, rows and knobs; the page-button taps; the preset list); user-preset macros ride in the spare tail of
Felucca's user-preset bank sector. Felucca's sequencer keeps running underneath (clock, arpeggiator) until stage 2.

**Tech Stack:** C (gnu99, JieLi clang 4.0.1 for the device; host `cc` for tests), Python 3 build tools, Docker (the
build's tools), emcc (web emulator), Felucca's host test harness (`tests/run_tests.sh`, `tests/ui_test.c`).

**Spec:** `docs/superpowers/specs/2026-10-10-ensemble-design.md` (§9 is this stage; §1–§8 the whole design).

## Global Constraints

- Base: upstream Felucca **1.5**, tag `v1.5`, commit `129a4cf` (`https://github.com/hugelton/Felucca`).
- License GPL-3.0-only; Leo Kuroshita / Hügelton Instruments credited (README, splash, ABOUT). Grids (later stages):
  GPL-3.0-or-later.
- Working name **ENSEMBLE**; version string `ENS-<x.y.z>`, first version `0.1.0`.
- Tracks fixed: track 0 PADS, 1 BASS, 2 KEYS, 3 LEAD. 8 voices shared (Felucca's `NVOICE`, unchanged).
- Roles by Felucca category (`category.c`): PADS ← PAD; BASS ← BASS; KEYS ← KEYS + PLUCK; LEAD ← LEAD.
- A macro: a name (from a fixed list) + up to 3 targets `{param, min, max}`; value 0..127 maps each target linearly
  min → max. 4 macros per track.
- Parameter values everywhere lie in −64..127 (Felucca's stored range; `int8_t` holds them).
- Frozen in stage 1 (no edits): engines (`eng_*.c`, `engines.c` except `TRK_DEF`), `voice.c`, `dsp.c`, `fx.c`,
  `mod.c`, `perform.c`, `master.c`, `storage.c`, `storage_hw.c`, `usb.c`, MIDI, OTA, `firmware/loader/`.
- No new flash regions in stage 1: user-preset macros live in the existing user-preset bank objects.
- Commit after every task; messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

**Deviations from the spec, for the user's approval at plan review:**
1. Macro names come from a fixed list of 16 (TONE, MOTION, SWELL, SPACE, DRIVE, DECAY, GLIDE, WIDTH, SHIMMER, AIR,
   SIZE, BITE, BODY, ATTACK, WOBBLE, DEPTH), not free text: no text entry on the MACRO page, and 4 macros fit in
   40 bytes per user preset.
2. Factory presets start with their role's default set (with the engine's TONE); per-preset overrides are added in
   Task 9 after the user listens, not written blind for all 68 now.
3. FX tap keeps opening Felucca's FX pages (DIST, sends, INSERT = the spec's preset and track FX) instead of a
   placeholder; SEQ, ARP, SCL and REC taps show placeholders.
4. User presets with no category or OTHER (most presets saved by older Felucca) appear in every role, so none are
   hidden.
5. Nothing of Felucca's sequencer is deleted in stage 1 (spec §9.2 "cut when nothing reads them"): `project.c`,
   `editor.c` and the UI still read `motion.c` and `song_chain.c`, and the SEQ pages are only unreachable. Stage 2
   removes them when the looper and performances replace projects.
6. HOME's hold keeps opening Felucca's MENU in stage 1; the conductor layer (spec §7) takes the hold in stage 2.

## Review Focus

1. **Old Felucca user presets on the device:** a bank written by Felucca (3080 bytes) loads with every preset kept and
   macros at their role defaults — Task 5, test "old bank".
2. **Uncategorised user presets:** a user preset of CAT_NONE / CAT_OTHER is reachable from every role's PRESETS —
   Task 3, test "uncategorised user preset".
3. **Saving while playing:** `up_store_cat` refuses (`STOP TO SAVE`) and the slot's stored macros stay as they were —
   Task 5, test "refused save".
4. **Reversed ranges and out-of-range values:** a macro with min > max maps endpoints the right way round; a preset
   value outside a macro's range loads the nearest position, never a value outside the parameter's range — Task 4,
   tests "reversed" and "inverse clamps".
5. **Project load:** loading a Felucca project slot recomputes every track's macros for its loaded engine (no stale
   macros from the sound before) — Task 6, test "project load".

---

## File structure

New repo `~/Desktop/dev/fm1-drummachine/ensemble` (paths below are relative to it):

| File | Responsibility |
|---|---|
| `firmware/src/role.c` (new) | roles, role names, which categories a role keeps |
| `firmware/src/macro.c` (new) | macro types, name list, default sets, TONE per engine, map / inverse, per-track state, load on sound change, turn |
| `firmware/src/ui_macro.c` (new) | EDIT > MACRO page: draw, columns, knobs |
| `firmware/src/ui.c` (modify) | preset list filter, `apply_preset_to` hook, HOME tap, placeholder taps |
| `firmware/src/ui_input.c` (modify) | HOME knobs → macros; MACRO page knobs; page-tap placeholder |
| `firmware/src/ui_layer.c` (modify) | layer-tap placeholder |
| `firmware/src/ui_draw.c` (modify) | HOME columns = macros; MACRO page columns; splash text |
| `firmware/src/ui_graph.c` (modify) | HOME rows = role tracks; MACRO page graph |
| `firmware/src/params.c` (modify) | `GR_MACRO`, the MACRO page in `PAGES[]` |
| `firmware/src/upreset.c` (modify) | the bank's macro tail: store, erase, boot, old banks |
| `firmware/src/project.c` (modify) | macros recomputed after a project load |
| `firmware/src/engines.c` (modify) | `TRK_DEF`: one sound per role at power-on |
| `firmware/src/felucca.c`, `tests/ui_test.c`, `web/emu/felucca_web.c` (modify) | include `role.c`, `macro.c`, `ui_macro.c` |
| `firmware/src/bench.c`, `tools/fm1_bench.py`, `tests/bench_host.c` (new, from Hortator) | BENCH with synth cases |
| `tests/ens_role_test.c`, `tests/ens_macro_test.c`, `tests/ens_umac_test.c`, `tests/ens_home_test.c` (new) | host tests |
| `VERSION.txt`, `tools/version.py`, `docs/VERSIONING.md`, `CHANGELOG.md`, `README.md` (new / rewritten) | identity |

---

### Task 1: Fork Felucca 1.5 and record the baseline

**Files:**
- Create: the repo `~/Desktop/dev/fm1-drummachine/ensemble`
- Create: `docs/superpowers/specs/2026-10-10-ensemble-design.md`, `docs/superpowers/plans/2026-10-10-ensemble-stage1.md`
  (copied from the Hortator worktree `.claude/worktrees/ensemble-spec`, branch `worktree-ensemble-spec`)

**Interfaces:**
- Produces: a building repo on branch `main` at Felucca `v1.5`, remote `upstream` = Felucca, no `origin`.

- [ ] **Step 1: Clone at v1.5**

```bash
cd ~/Desktop/dev/fm1-drummachine
git clone https://github.com/hugelton/Felucca ensemble
cd ensemble
git remote rename origin upstream
git checkout -B main v1.5
git log --oneline -1   # expect: 129a4cf Felucca 1.5
```

- [ ] **Step 2: Build and run the host tests unchanged (the baseline)**

```bash
./build.sh 2>&1 | tail -5          # expect build/felucca.fwsc
tests/run_tests.sh 2>&1 | tail -3  # expect: ALL HOST TESTS PASSED
```

If the toolchain or SDK is missing, follow `BUILDING.md` (`tools/get_toolchain.sh`; the AC79 SDK clone) — the Hortator
repo builds with the same ones, so `~/.jieli/toolchain` and `~/fw-AC79_AIoT_SDK` are expected to exist. If any test
fails on the untouched fork, stop and report it (it is upstream's, not ours).

- [ ] **Step 3: Copy the spec and this plan in, commit**

```bash
H=~/Desktop/dev/fm1-drummachine/felucca/.claude/worktrees/ensemble-spec
mkdir -p docs/superpowers/specs docs/superpowers/plans
cp $H/docs/superpowers/specs/2026-10-10-ensemble-design.md docs/superpowers/specs/
cp $H/docs/superpowers/plans/2026-10-10-ensemble-stage1.md docs/superpowers/plans/
git add docs/superpowers
git commit -m "ENSEMBLE: the design and the stage 1 plan (fork of Felucca 1.5)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Identity and versioning

**Files:**
- Create: `VERSION.txt`, `tools/version.py` (from Hortator), `docs/VERSIONING.md` (from Hortator), `CHANGELOG.md`
- Modify: `tools/build.py` (the default `-DFELUCCA_VERSION`), `web/emu/build.sh` (version source),
  `firmware/src/ui_draw.c:~1201` (`SPLASH_LINES`), `firmware/src/ui_menu.c` (ABOUT text), `README.md`

**Interfaces:**
- Produces: `FELUCCA_VERSION` = `"ENS-0.1.0"` in every build; `python3 tools/version.py --check` passes.

- [ ] **Step 1: Copy Hortator's versioning tools**

```bash
H=~/Desktop/dev/fm1-drummachine/felucca
cp $H/tools/version.py tools/version.py
cp $H/docs/VERSIONING.md docs/VERSIONING.md
echo 0.1.0 > VERSION.txt
```

In `tools/version.py` and `docs/VERSIONING.md`, replace every `DRUM-` with `ENS-`, every `drum-` with `ens-`, every
`Hortator` with `ENSEMBLE`, and the package label function's `drum` prefix with `ens`
(`grep -n -i 'drum\|hortator' tools/version.py docs/VERSIONING.md` must print nothing afterwards).

- [ ] **Step 2: Run the version tool's self-test (fails until build.py reads VERSION.txt)**

Run: `python3 tools/version.py --self-test && python3 tools/version.py --check`
Expected: the self-test passes; `--check` FAILS saying the build's version is not `ENS-0.1.0`.

- [ ] **Step 3: Make build.py and the emulator build use VERSION.txt**

In `tools/build.py`, where the default `-DFELUCCA_ID=` / `-DFELUCCA_VERSION=` flags are built (around `:200-202`), when
`--release` is not given pass `-DFELUCCA_VERSION="ENS-<VERSION.txt>"`, read as Hortator's `tools/build.py:187` does
(copy that line and its helper). In `web/emu/build.sh`, replace the `sed` that reads the version out of `felucca.c`
with:

```bash
VERSION="ENS-$(tr -d '[:space:]' < VERSION.txt)"
```

- [ ] **Step 4: Splash, ABOUT, README, CHANGELOG**

- `ui_draw.c` `SPLASH_LINES` (~1201): the product line `ENSEMBLE`, then `on Felucca by Leo Kuroshita`, then the version.
- `ui_menu.c` ABOUT (~116, ~390): the same name and credit.
- `README.md`: replace the top with: name (working), one paragraph (a performance synth for the M-VAVE FM-1: four
  tracks PADS BASS KEYS LEAD, macros; looper, harmony and macro FX to come), "built on Felucca by Leo Kuroshita",
  beta warning, GPL-3.0-only; keep Felucca's install / recovery / build sections below, renamed. Add: *installing
  ENSEMBLE keeps your Felucca user presets; after saving one in ENSEMBLE, Felucca shows that bank as empty until it
  saves into it again.*
- `CHANGELOG.md`:

```markdown
# Changelog

## Unreleased
- ENSEMBLE starts from Felucca 1.5: four role tracks (PADS BASS KEYS LEAD), macros on HOME, role-filtered PRESETS,
  EDIT > MACRO, BENCH.
```

- [ ] **Step 5: The recovery tool (Hortator's: no extra hardware, only the FM-1's boot mode)**

```bash
H=~/Desktop/dev/fm1-drummachine/felucca
cp -R $H/tools/rescue tools/rescue
cp $H/docs/RECOVERY.md docs/RECOVERY.md
cp $H/LICENSES/MIT-jl-uboot-tool.txt LICENSES/
python3 tools/rescue/fm1_uboot_test.py   # expect: its tests pass
```

Replace `Hortator` with `ENSEMBLE` and Hortator's installer URL with "the installer page of this release" in
`docs/RECOVERY.md` (`grep -n -i hortator docs/RECOVERY.md tools/rescue/*` must print nothing). Its flash assumptions
(app area 0x4000–0x93000, stock V15) hold for Felucca 1.5 unchanged. Link it from README's recovery section.

- [ ] **Step 6: Build, check, test, commit**

```bash
./build.sh 2>&1 | tail -2 && python3 tools/version.py --check && tests/run_tests.sh 2>&1 | tail -1
git add -A && git commit -m "ENSEMBLE identity: ENS-0.1.0, VERSION.txt / version.py, splash, README, CHANGELOG, the recovery tool

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Expected: `--check` passes; `ALL HOST TESTS PASSED` (if a UI test pinned the splash or ABOUT text, update that
expectation to the new strings in the same commit, and say which in the commit message).

---

### Task 3: Roles and the role-filtered PRESETS list

**Files:**
- Create: `firmware/src/role.c`, `tests/ens_role_test.c`
- Modify: `firmware/src/felucca.c` (include before `ui.c`), `tests/ui_test.c:148` (include before `ui.c`),
  `web/emu/felucca_web.c` (include before `ui.c`), `firmware/src/ui.c:1314-1360` (list filter),
  `firmware/src/engines.c:190` (`TRK_DEF`), `tests/run_tests.sh` (~:208, add the test)

**Interfaces:**
- Produces:
  - `enum { ROLE_PADS, ROLE_BASS, ROLE_KEYS, ROLE_LEAD, ROLE_N };`
  - `static const char *const ROLE_NAME[ROLE_N];` → `"PADS" "BASS" "KEYS" "LEAD"`
  - `static uint32_t role_of(uint32_t track);` (track 0..3 → its role)
  - `static int role_keeps(uint32_t role, uint32_t cat, int user);` (1 if a sound of category `cat` is listed;
    `user` = it is a user preset)

- [ ] **Step 1: Write the failing test** `tests/ens_role_test.c`

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* ENSEMBLE roles (spec §9.3): each role's factory presets, the PRESETS list kept to the selected track's role,
 * user presets without a category in every role, the power-on sounds. Run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static uint32_t role_count(uint32_t role)          /* factory presets the role lists */
{
    uint32_t all, i, k, e, n = 0;
    preset_all_pos(&all);
    for (i = 0; i < all; i++) {
        e = preset_all_at(i, &k);
        if (e < NENGINES && role_keeps(role, preset_cat(e, k), 0)) n++;
    }
    return n;
}

int main(void)
{
    int bad = 0;
    uint32_t r, i, ok;
    ui_power_on();
    bad += check("roles: PADS lists the 12 PAD presets", role_count(ROLE_PADS) == 12u);
    bad += check("roles: BASS lists the 7 BASS presets", role_count(ROLE_BASS) == 7u);
    bad += check("roles: KEYS lists the 22 KEYS + PLUCK presets", role_count(ROLE_KEYS) == 22u);
    bad += check("roles: LEAD lists the 16 LEAD presets", role_count(ROLE_LEAD) == 16u);
    bad += check("roles: no role lists DRUM or FX", !role_keeps(ROLE_PADS, CAT_DRUM, 0) &&
                 !role_keeps(ROLE_LEAD, CAT_FX, 0) && !role_keeps(ROLE_BASS, CAT_OTHER, 0));
    for (r = 0; r < ROLE_N; r++)
        bad += check("roles: a user preset without a category is in every role",
                     role_keeps(r, CAT_NONE, 1) && role_keeps(r, CAT_OTHER, 1));
    for (i = 0, ok = 1; i < NTRK; i++)
        ok &= role_keeps(role_of(i), track_cat(&trk[i]), 0);
    bad += check("power-on: every track plays a sound of its role", ok);
    for (r = 0; r < ROLE_N; r++) {
        song.sel = (uint8_t)r;
        for (i = 0, ok = 1; i < 9u; i++) {
            turn(EN_PRESET, 1);
            frame();
            ok &= role_keeps(r, track_cat(TSEL), user_of(TSEL) < UP_SLOTS);
        }
        bad += check("PRESETS: nine turns stay in the selected track's role", ok);
    }
    printf("%s\n", bad ? "ENS ROLE TEST FAILED" : "ens role test passed");
    return bad != 0;
}
```

Add to `tests/run_tests.sh`, after the `chord_test` lines (~:208):

```bash
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/ens_role_test" tests/ens_role_test.c -lm
    run "ENSEMBLE roles: factory presets per role, PRESETS kept to the role, user presets, power-on sounds" "$OUT/ens_role_test"
```

- [ ] **Step 2: Run it to see it fail**

Run: `cc -w -Ibuild/gen -Ifirmware/src -o build/host/ens_role_test tests/ens_role_test.c -lm`
Expected: compile error, `role_keeps` undeclared.

- [ ] **Step 3: Write `firmware/src/role.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* ENSEMBLE roles (spec §9.3): the four tracks are PADS BASS KEYS LEAD, fixed. A role lists the sounds of its
 * categories (category.c): PADS PAD; BASS BASS; KEYS KEYS and PLUCK; LEAD LEAD. A user preset of no category (one
 * saved before 1.2: CAT_NONE, listed by engine as OTHER) or of OTHER is in every role, so none is hidden. */
#include "category.c"
enum { ROLE_PADS, ROLE_BASS, ROLE_KEYS, ROLE_LEAD, ROLE_N };
static const char *const ROLE_NAME[ROLE_N] = {"PADS", "BASS", "KEYS", "LEAD"};
_Static_assert(ROLE_N == NTRK, "a role per track");

static uint32_t role_of(uint32_t track) { return track < ROLE_N ? track : ROLE_PADS; }

static int role_keeps(uint32_t role, uint32_t cat, int user)
{
    if (user && (cat == CAT_NONE || cat == CAT_OTHER))
        return 1;
    switch (role) {
    case ROLE_PADS: return cat == CAT_PAD;
    case ROLE_BASS: return cat == CAT_BASS;
    case ROLE_KEYS: return cat == CAT_KEYS || cat == CAT_PLUCK;
    case ROLE_LEAD: return cat == CAT_LEAD;
    }
    return 0;
}
```

Include it: in `firmware/src/felucca.c`, `tests/ui_test.c` (as `#include "../firmware/src/role.c"`) and
`web/emu/felucca_web.c`, add the include on the line just before the one that includes `ui.c`.

- [ ] **Step 4: Filter the list by role** (`firmware/src/ui.c`, the LIST block at ~1314)

Replace `list_set` and `list_keep` with:

```c
static void list_set(uint32_t m)                         /* ENSEMBLE: ALL or FAV; the role is the category */
{
    favorites.filter = m == 1u;
    ui_lcat = 0;
}
```

```c
static int list_keep(uint32_t e, uint32_t k)
{
    if (!role_keeps(role_of(song.sel), sound_cat(e, k), e == NENGINES))
        return 0;
    return list_mode() == 1u ? favorite_has(e, k) : 1;
}
```

In `preset_pos`, `preset_at` and `preset_visible`, delete the unfiltered shortcuts (the lines
`if (!list_mode()) { *total = all; return current; }`, `if (!list_mode()) return preset_all_at(n, k);` and
`if (!list_mode()) return (cur + total * 4u + row - 3u) % total;`), so the list is always the filtered one.

- [ ] **Step 5: One sound per role at power-on** (`firmware/src/engines.c:190`)

```c
static const uint8_t TRK_DEF[NPART][3] = {{0, 1, 0}, {0, 2, 0}, {ENGI_FM6, 0, 0}, {0, 0, 0}};   /* ENSEMBLE: PADS
     * ANALOG SOFT PAD, BASS ANALOG SQR BASS, KEYS FM6 preset 0 (KEYS), LEAD ANALOG SAW LEAD; no patterns */
```

(Keep the comment's style; the old comment's list of sounds goes.)

- [ ] **Step 6: Run the test and the suite**

Run: `cc -w -Ibuild/gen -Ifirmware/src -o build/host/ens_role_test tests/ens_role_test.c -lm && build/host/ens_role_test`
Expected: `ens role test passed`.
Run: `tests/run_tests.sh 2>&1 | tail -15`
Expected: ALL HOST TESTS PASSED, except tests that pinned the old power-on sounds or the category LIST (`ui_test`,
`regress` goldens of the power-on mix). For each: confirm the failure is only the changed default or the removed
category LIST, update that expectation (`GOLDEN_UPDATE=1` for the golden lines of renders that use the power-on
tracks only — check `git diff tests/golden.txt` touches no single-engine line), and list them in the commit message.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "ENSEMBLE roles: PADS BASS KEYS LEAD, PRESETS kept to the track's role, a sound per role at power-on

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Macro core

**Files:**
- Create: `firmware/src/macro.c`, `tests/ens_macro_test.c`
- Modify: `firmware/src/felucca.c`, `tests/ui_test.c`, `web/emu/felucca_web.c` (include `macro.c` after `role.c`),
  `firmware/src/ui.c` (`apply_preset_to` ~1188-1232), `firmware/src/upreset.c` (`up_load` ~455-492),
  `tests/run_tests.sh`

**Interfaces:**
- Consumes: `role_of`, `ROLE_N` (Task 3); Felucca's `track_desc(const track_t *, uint32_t)` (params.c:219),
  `motion_capture(track_t *, uint32_t, int16_t)` (motion.c:219), `NENGINES`, `P_*`.
- Produces:
  - `#define MAC_N 4u`, `#define MAC_TGT 3u`, `#define MAC_NONE 0xFFu`, `#define MAC_TONE 0xFEu`
  - `typedef struct { uint8_t param; int8_t min, max; } mac_tgt_t;`
  - `typedef struct { uint8_t name; mac_tgt_t t[MAC_TGT]; } macro_t;` (10 bytes)
  - `typedef struct { macro_t m[MAC_N]; uint8_t v[MAC_N]; } mac_set_t;`
  - `enum { MN_NONE, MN_TONE, ..., MN_COUNT };` and `static const char *const MAC_NAME[MN_COUNT];`
  - `static mac_set_t mac_trk[NTRK];`
  - `static int16_t mac_map(const track_t *t, const mac_tgt_t *g, uint32_t v);`
  - `static uint32_t mac_inverse(const track_t *t, const mac_tgt_t *g, int16_t value);`
  - `static void mac_defaults(const track_t *t, macro_t *out);` (role default set, TONE resolved for its engine)
  - `static void mac_set_macros(track_t *t, const macro_t *m);` (installs 4 macros, positions from the sound)
  - `static void mac_track_loaded(track_t *t);` (after any sound change: user macros if the slot has them, else defaults)
  - `static void mac_turn(uint32_t track, uint32_t k, int32_t delta);`
  - `static int mac_owns(uint32_t track, uint32_t param);`
  - forward-declared here, defined in Task 5: `static const macro_t *umac_get(uint32_t slot, uint32_t engine);`
    (0 = none)

- [ ] **Step 1: Write the failing test** `tests/ens_macro_test.c`

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* ENSEMBLE macros (spec §9.4): the mapping and its inverse (reversed ranges, clamping), every role's default set valid
 * for every factory preset of the role, a turn writes its targets and what a save reads (motion_base_value), a
 * preset load installs its role's macros. Run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int set_valid(const track_t *t, const mac_set_t *s)
{
    uint32_t k, i, any;
    for (k = 0; k < MAC_N; k++) {
        if (s->m[k].name == MN_NONE || s->m[k].name >= MN_COUNT) return 0;
        for (i = 0, any = 0; i < MAC_TGT; i++) {
            const mac_tgt_t *g = &s->m[k].t[i];
            const param_desc_t *d;
            if (g->param == MAC_NONE) continue;
            if (g->param >= P_COUNT) return 0;
            d = track_desc(t, g->param);
            if (g->min < d->min || g->min > d->max || g->max < d->min || g->max > d->max) return 0;
            any = 1;
        }
        if (!any && s->m[k].name != MN_TONE) return 0;   /* TONE may be empty on an engine without one */
    }
    return 1;
}

int main(void)
{
    int bad = 0;
    uint32_t all, i, k, e, ok;
    track_t *t;
    ui_power_on();
    t = &trk[0];
    {
        mac_tgt_t g = {P_REV, 10, 90}, r = {P_REV, 90, 10};
        bad += check("map: 0 -> min, 127 -> max", mac_map(t, &g, 0) == 10 && mac_map(t, &g, 127) == 90);
        bad += check("map: rising", mac_map(t, &g, 40) < mac_map(t, &g, 41) || mac_map(t, &g, 40) == mac_map(t, &g, 41));
        bad += check("reversed: 0 -> 90, 127 -> 10", mac_map(t, &r, 0) == 90 && mac_map(t, &r, 127) == 10);
        bad += check("inverse: of a mapped value", mac_map(t, &g, mac_inverse(t, &g, 50)) == 50 ||
                     mac_map(t, &g, mac_inverse(t, &g, 50)) == 51 || mac_map(t, &g, mac_inverse(t, &g, 50)) == 49);
        bad += check("inverse clamps: below min -> 0, above max -> 127",
                     mac_inverse(t, &g, 0) == 0 && mac_inverse(t, &g, 127) == 127);
        bad += check("inverse clamps on a reversed range", mac_inverse(t, &r, 127) == 0 && mac_inverse(t, &r, 0) == 127);
    }
    preset_all_pos(&all);
    for (i = 0, ok = 1; i < all; i++) {
        uint32_t r;
        e = preset_all_at(i, &k);
        if (e >= NENGINES) continue;
        for (r = 0; r < ROLE_N; r++) {
            if (!role_keeps(r, preset_cat(e, k), 0)) continue;
            t = &trk[r];
            set_engine_of(t, e);
            apply_preset_to(t, k);
            if (!set_valid(t, &mac_trk[r])) {
                printf("  invalid macros: role %s engine %u preset %u\n", ROLE_NAME[r], (unsigned)e, (unsigned)k);
                ok = 0;
            }
        }
    }
    bad += check("every factory preset of a role gets a valid macro set", ok);
    t = &trk[1];                                                     /* BASS, ANALOG SQR BASS */
    set_engine_of(t, 0);
    apply_preset_to(t, 2);
    bad += check("BASS defaults: TONE DRIVE DECAY GLIDE", mac_trk[1].m[0].name == MN_TONE &&
                 mac_trk[1].m[1].name == MN_DRIVE && mac_trk[1].m[2].name == MN_DECAY && mac_trk[1].m[3].name == MN_GLIDE);
    bad += check("TONE on ANALOG is its CUT (E4)", mac_trk[1].m[0].t[0].param == P_E0 + 4u);
    mac_turn(1, 1, 200);                                             /* DRIVE to the top */
    bad += check("turn: the value clamps at 127", mac_trk[1].v[1] == 127u);
    bad += check("turn: DIST at DRIVE's max", t->p[P_DIST] == mac_map(t, &mac_trk[1].m[1].t[0], 127));
    bad += check("turn: what a save reads follows", motion_base_value(t, P_DIST) == t->p[P_DIST]);
    bad += check("mac_owns: DIST yes, PAN no", mac_owns(1, P_DIST) && !mac_owns(1, P_PAN));
    set_engine_of(t, ENGI_FM6);
    bad += check("an engine change installs its TONE (FM6 MLVL, E2)", mac_trk[1].m[0].t[0].param == P_E0 + 2u);
    printf("%s\n", bad ? "ENS MACRO TEST FAILED" : "ens macro test passed");
    return bad != 0;
}
```

Add to `tests/run_tests.sh` after the role test:

```bash
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/ens_macro_test" tests/ens_macro_test.c -lm
    run "ENSEMBLE macros: mapping, reversed ranges, defaults valid for every role preset, turns, engine changes" "$OUT/ens_macro_test"
```

- [ ] **Step 2: Run it to see it fail**

Run: `cc -w -Ibuild/gen -Ifirmware/src -o build/host/ens_macro_test tests/ens_macro_test.c -lm`
Expected: compile error, `mac_map` undeclared.

- [ ] **Step 3: Write `firmware/src/macro.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* ENSEMBLE macros (spec §9.4). A track has four macros; a macro is a name (MAC_NAME) and up to three targets, each a
 * parameter with a min and a max. Its value 0..127 sets every target linearly from min to max (min > max: falling).
 * A sound loaded (a factory preset, an engine, a user preset, a project) installs its macros: a user preset's own
 * (upreset.c umac_get) if it has them for its engine, else the role's default set, TONE being the engine's
 * brightness parameter. The macros' positions are read back from the sound (mac_inverse of the first target), so a
 * turn starts where the sound is. A turn writes its targets as a knob would (motion_capture: what a save reads). */
#define MAC_N 4u
#define MAC_TGT 3u
#define MAC_NONE 0xFFu                                   /* a target not used */
#define MAC_TONE 0xFEu                                   /* (default sets only) the engine's TONE over its range */

typedef struct { uint8_t param; int8_t min, max; } mac_tgt_t;
typedef struct { uint8_t name; mac_tgt_t t[MAC_TGT]; } macro_t;
typedef struct { macro_t m[MAC_N]; uint8_t v[MAC_N]; } mac_set_t;
_Static_assert(sizeof(macro_t) == 10, "macro_t is stored in user-preset banks");

enum { MN_NONE, MN_TONE, MN_MOTION, MN_SWELL, MN_SPACE, MN_DRIVE, MN_DECAY, MN_GLIDE, MN_WIDTH, MN_SHIMMER, MN_AIR,
       MN_SIZE, MN_BITE, MN_BODY, MN_ATTACK, MN_WOBBLE, MN_DEPTH, MN_COUNT };   /* stored: append only */
static const char *const MAC_NAME[MN_COUNT] = {"-", "TONE", "MOTION", "SWELL", "SPACE", "DRIVE", "DECAY", "GLIDE",
    "WIDTH", "SHIMMER", "AIR", "SIZE", "BITE", "BODY", "ATTACK", "WOBBLE", "DEPTH"};

#define MT_NONE {MAC_NONE, 0, 0}
static const macro_t MAC_DEF[ROLE_N][MAC_N] = {
    {   /* PADS */
        {MN_TONE, {{MAC_TONE, 0, 0}, MT_NONE, MT_NONE}},
        {MN_MOTION, {{P_LD_FLT, 0, 40}, {P_LRATE, 30, 90}, MT_NONE}},
        {MN_SWELL, {{P_ATK, 5, 100}, {P_REL, 40, 110}, MT_NONE}},
        {MN_SPACE, {{P_REV, 10, 100}, {P_CHOR, 0, 80}, MT_NONE}},
    },
    {   /* BASS */
        {MN_TONE, {{MAC_TONE, 0, 0}, MT_NONE, MT_NONE}},
        {MN_DRIVE, {{P_DIST, 0, 90}, MT_NONE, MT_NONE}},
        {MN_DECAY, {{P_DEC, 20, 110}, {P_ED_FLT, 10, 50}, MT_NONE}},
        {MN_GLIDE, {{P_GLIDE, 0, 80}, MT_NONE, MT_NONE}},
    },
    {   /* KEYS */
        {MN_TONE, {{MAC_TONE, 0, 0}, MT_NONE, MT_NONE}},
        {MN_DECAY, {{P_DEC, 20, 110}, {P_REL, 20, 100}, MT_NONE}},
        {MN_WIDTH, {{P_SPRD, 0, 127}, {P_CHOR, 0, 70}, MT_NONE}},
        {MN_SPACE, {{P_DLY, 0, 80}, {P_REV, 0, 90}, MT_NONE}},
    },
    {   /* LEAD */
        {MN_TONE, {{MAC_TONE, 0, 0}, MT_NONE, MT_NONE}},
        {MN_DRIVE, {{P_DIST, 0, 80}, MT_NONE, MT_NONE}},
        {MN_GLIDE, {{P_GLIDE, 0, 90}, MT_NONE, MT_NONE}},
        {MN_SPACE, {{P_DLY, 0, 90}, {P_REV, 0, 80}, MT_NONE}},
    },
};

/* TONE per engine (ENGINES[] numbers): the E parameter (0..7) the shared cutoff or CC74 moves; 8 = none.
 * ANALOG CUT, DIGITAL (retired) none, PHASE DCW, LOFI TONE, SAMPLE CUT, VOICE Q, TRIO CUT, WHEEL TOP, GRAIN TONE,
 * PHYS BRIT, DRUM TONE, NOISE FREQ, FM6 MLVL, SLICE TONE */
static const uint8_t MAC_TONE_E[14] = {4, 8, 2, 7, 4, 6, 5, 3, 7, 2, 2, 2, 2, 7};
_Static_assert(NENGINES <= 14, "a TONE per engine");

static mac_set_t mac_trk[NTRK];

static const macro_t *umac_get(uint32_t slot, uint32_t engine);   /* (upreset.c) */
static uint32_t mac_ti(const track_t *t) { return (uint32_t)(t - trk) % NTRK; }   /* (ui.c's helpers come later) */

static int16_t mac_clamp(const track_t *t, uint32_t param, int32_t v)
{
    const param_desc_t *d = track_desc(t, param);
    return (int16_t)(v < d->min ? d->min : v > d->max ? d->max : v);
}

static int16_t mac_map(const track_t *t, const mac_tgt_t *g, uint32_t v)
{
    int32_t span = (int32_t)g->max - g->min;
    if (v > 127u) v = 127u;
    return mac_clamp(t, g->param, g->min + (span * (int32_t)v + (span >= 0 ? 63 : -63)) / 127);
}

/* the position whose value is nearest `value` (the first such, so a flat range reads 0) */
static uint32_t mac_inverse(const track_t *t, const mac_tgt_t *g, int16_t value)
{
    uint32_t v, best = 0;
    int32_t bd = 0x7FFFFFFF;
    for (v = 0; v < 128u; v++) {
        int32_t d = mac_map(t, g, v) - value;
        if (d < 0) d = -d;
        if (d < bd) { bd = d; best = v; }
    }
    return best;
}

static void mac_defaults(const track_t *t, macro_t *out)
{
    uint32_t k, i, e = MAC_TONE_E[t->eng_req % 14u];
    const macro_t *src = MAC_DEF[role_of(mac_ti(t))];
    for (k = 0; k < MAC_N; k++) {
        out[k] = src[k];
        for (i = 0; i < MAC_TGT; i++) {
            mac_tgt_t *g = &out[k].t[i];
            if (g->param != MAC_TONE) continue;
            if (e < 8u) {
                const param_desc_t *d = track_desc(t, P_E0 + e);
                g->param = (uint8_t)(P_E0 + e);
                g->min = (int8_t)d->min;
                g->max = (int8_t)d->max;
            } else {
                *g = (mac_tgt_t)MT_NONE;
            }
        }
    }
}

static void mac_set_macros(track_t *t, const macro_t *m)
{
    mac_set_t *s = &mac_trk[mac_ti(t)];
    uint32_t k, i;
    for (k = 0; k < MAC_N; k++) {
        s->m[k] = m[k];
        s->v[k] = 0;
        for (i = 0; i < MAC_TGT; i++)
            if (m[k].t[i].param < P_COUNT) {
                s->v[k] = (uint8_t)mac_inverse(t, &m[k].t[i], t->p[m[k].t[i].param]);
                break;
            }
    }
}

static void mac_track_loaded(track_t *t)
{
    macro_t m[MAC_N];
    const macro_t *u = t->user ? umac_get(t->user - 1u, t->eng_req) : 0;
    if (u) {
        mac_set_macros(t, u);
        return;
    }
    mac_defaults(t, m);
    mac_set_macros(t, m);
}

static void mac_turn(uint32_t track, uint32_t k, int32_t delta)
{
    track_t *t = &trk[track % NTRK];
    mac_set_t *s = &mac_trk[track % NTRK];
    int32_t v = (int32_t)s->v[k % MAC_N] + delta;
    uint32_t i;
    s->v[k % MAC_N] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    for (i = 0; i < MAC_TGT; i++) {
        const mac_tgt_t *g = &s->m[k % MAC_N].t[i];
        if (g->param >= P_COUNT) continue;
        t->p[g->param] = mac_map(t, g, s->v[k % MAC_N]);
        motion_capture(t, g->param, t->p[g->param]);
    }
}

static int mac_owns(uint32_t track, uint32_t param)
{
    const mac_set_t *s = &mac_trk[track % NTRK];
    uint32_t k, i;
    for (k = 0; k < MAC_N; k++)
        for (i = 0; i < MAC_TGT; i++)
            if (s->m[k].t[i].param == param) return 1;
    return 0;
}
```

Include it in `felucca.c`, `tests/ui_test.c` and `web/emu/felucca_web.c` on the line after `role.c`'s include.
Add a temporary definition so this task links before Task 5, at the end of `macro.c`:

```c
#ifndef ENS_UMAC                                         /* (Task 5 defines the real one in upreset.c) */
static const macro_t *umac_get(uint32_t slot, uint32_t engine) { (void)slot; (void)engine; return 0; }
#endif
```

- [ ] **Step 4: Hook the sound loads**

`firmware/src/ui.c` `apply_preset_to` (~1188): call `mac_track_loaded(t);` immediately before each of its two
`load_end(t);` calls (the `!e->npresets` early return and the end). `set_engine_of` reaches it through
`apply_preset_to(t, 0)`. The DIGITAL (`fm4_load_preset`) and SAMPLE-PERC early returns end in another load that comes
back here.

`firmware/src/upreset.c` `up_load` (~455): after `t->user = (uint8_t)(k + 1u);` add `mac_track_loaded(t);`.

- [ ] **Step 5: Run the tests**

Run: `cc -w -Ibuild/gen -Ifirmware/src -o build/host/ens_macro_test tests/ens_macro_test.c -lm && build/host/ens_macro_test`
Expected: `ens macro test passed`. If "every factory preset of a role gets a valid macro set" fails, the printed lines
name the role / engine / preset: fix the default set's ranges (they must lie inside every target's descriptor), never
the test.
Run: `tests/run_tests.sh 2>&1 | tail -3` → `ALL HOST TESTS PASSED`.

- [ ] **Step 6: Commit**

```bash
git add -A && git commit -m "ENSEMBLE macros: four per track, role default sets, TONE per engine, installed on every sound load

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: User-preset macros in the bank's tail

**Files:**
- Create: `tests/ens_umac_test.c`
- Modify: `firmware/src/upreset.c` (bank struct ~68, `up_rec`, `up_boot` ~290, `up_put` ~307, `up_store_cat` ~399),
  `firmware/src/macro.c` (remove the temporary `umac_get`), `tests/run_tests.sh`

**Interfaces:**
- Consumes: `macro_t`, `mac_trk`, `MAC_N` (Task 4); Felucca's `up_bank_t`, `UP_PER_BANK`, `UP_SLOTS`, `st_load`,
  `st_save`, `OBJ_UPRESET0`, `ST_PAYLOAD_MAX`.
- Produces:
  - `typedef struct { uint8_t engine, ok; macro_t m[MAC_N]; } mac_rec_t;` (42 bytes)
  - `typedef struct { up_bank_t b; mac_rec_t mac[UP_PER_BANK]; } ens_bank_t;` (3752 bytes ≤ 3840)
  - `static const macro_t *umac_get(uint32_t slot, uint32_t engine);` (defined here; 0 = none or other engine)

- [ ] **Step 1: Write the failing test** `tests/ens_umac_test.c`

Start the file with the preamble of `tests/persistence_test.c` up to and including its `#include` of the test
harness (it defines `FELUCCA_FLASH 1` and the host NOR); if that file includes `ui_test.c`, add
`#define UI_TEST_NO_MAIN 1` before it. Then:

```c
static int macros_eq(const macro_t *a, const macro_t *b)
{
    return memcmp(a, b, sizeof(macro_t) * MAC_N) == 0;
}

int main(void)
{
    int bad = 0;
    macro_t mine[MAC_N], before[MAC_N];
    ui_power_on();
    song.sel = 0;
    memcpy(mine, mac_trk[0].m, sizeof mine);
    mine[1].name = MN_SHIMMER;                                       /* edited, as EDIT > MACRO would */
    mine[1].t[0] = (mac_tgt_t){P_REV, 20, 120};
    memcpy(mac_trk[0].m, mine, sizeof mine);
    bad += check("store: saved", up_store_cat(5, "MYPAD", CAT_PAD) == 0);
    apply_preset_to(TSEL, 0);                                        /* another sound: other macros */
    bad += check("load: the preset's own macros", up_load(5) == 0 && macros_eq(mac_trk[0].m, mine));
    memset(ens_bank, 0, sizeof ens_bank);                            /* power cycle */
    up_boot();
    apply_preset_to(TSEL, 0);
    bad += check("after a reboot: the macros come back", up_load(5) == 0 && macros_eq(mac_trk[0].m, mine));
    {                                                                /* old bank: a Felucca one, 3080 bytes */
        up_bank_t old = ens_bank[0].b;
        bad += check("old bank: written", st_save(OBJ_UPRESET0, &old, sizeof old) == 0);
        memset(ens_bank, 0, sizeof ens_bank);
        up_boot();
        bad += check("old bank: the preset kept", up_used(5));
        bad += check("old bank: no macros", umac_get(5, ens_bank[0].b.r[5].engine) == 0);
        apply_preset_to(TSEL, 0);
        up_load(5);
        mac_defaults(TSEL, before);
        bad += check("old bank: the role's defaults on load", macros_eq(mac_trk[0].m, before));
    }
    memcpy(mac_trk[0].m, mine, sizeof mine);
    up_store_cat(6, "TWO", CAT_PAD);
    memcpy(before, ens_bank[0].mac[6].m, sizeof before);
    song.playing = 1;
    mac_trk[0].m[0].name = MN_AIR;
    bad += check("refused save: STOP TO SAVE", up_store_cat(6, "TWO", CAT_PAD) == 2);
    bad += check("refused save: the slot's macros unchanged", macros_eq(ens_bank[0].mac[6].m, before));
    song.playing = 0;
    bad += check("erase: done", up_put(6, 0) == 0);
    bad += check("erase: no macros", !ens_bank[0].mac[6].ok);
    bad += check("other engine: none", umac_get(5, (ens_bank[0].b.r[5].engine + 1u) % NENGINES) == 0);
    printf("%s\n", bad ? "ENS UMAC TEST FAILED" : "ens umac test passed");
    return bad != 0;
}
```

Add to `tests/run_tests.sh` after the macro test, with the same compiler flags as `persistence_test`'s line there:

```bash
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/ens_umac_test" tests/ens_umac_test.c -lm
    run "ENSEMBLE user-preset macros: store, load, reboot, old banks, refused save, erase" "$OUT/ens_umac_test"
```

- [ ] **Step 2: Run it to see it fail**

Run the compile line. Expected: compile error, `ens_bank` undeclared.

- [ ] **Step 3: The bank with its macro tail** (`firmware/src/upreset.c`)

Replace `static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK] ...;` (~68) with:

```c
/* ENSEMBLE: a bank's sector also holds its presets' macros, after the 3080 bytes Felucca stores (one object, one
 * write). A bank of Felucca's (3080 bytes) loads with no macros; Felucca reads a bank of ENSEMBLE's as too long
 * (empty) until it saves that bank again. */
typedef struct { uint8_t engine, ok; macro_t m[MAC_N]; } mac_rec_t;
typedef struct { up_bank_t b; mac_rec_t mac[UP_PER_BANK]; } ens_bank_t;
_Static_assert(sizeof(ens_bank_t) <= ST_PAYLOAD_MAX, "a bank and its macros in one sector");
static ens_bank_t ens_bank[UP_SLOTS / UP_PER_BANK] __attribute__((section(".pool")));   /* (main loop only) */
#define up_bank(i) (ens_bank[i].b)
#define ENS_UMAC 1
```

Then replace every `up_bank[X]` in `upreset.c` with `up_bank(X)` (`grep -n 'up_bank\[' firmware/src/upreset.c` must
print nothing afterwards; the expressions are `&up_bank[...]` → `&up_bank(...)`), and add after `up_rec`:

```c
static mac_rec_t *umac_rec(uint32_t k) { return &ens_bank[k / UP_PER_BANK].mac[k % UP_PER_BANK]; }
static const macro_t *umac_get(uint32_t slot, uint32_t engine)
{
    const mac_rec_t *m;
    if (slot >= UP_SLOTS || !up_used(slot)) return 0;
    m = umac_rec(slot);
    return m->ok && m->engine == engine ? m->m : 0;
}
```

(`up_used` is defined after `up_rec`; place `umac_get` after `up_used`.)

`up_boot` (~290): replace the `st_load` line with:

```c
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++) {
        int len = flash_ok ? st_load(OBJ_UPRESET0 + b, &ens_bank[b], sizeof ens_bank[b]) : -1;
        if (len != (int)sizeof ens_bank[b])
            memset(ens_bank[b].mac, 0, sizeof ens_bank[b].mac);       /* Felucca's bank, or none: no macros */
        up_bank_check(b, len == (int)sizeof ens_bank[b] ? (int)sizeof(up_bank_t) : len);
        if (len != (int)sizeof ens_bank[b] && len != (int)sizeof(up_bank_t))
            memset(&ens_bank[b], 0, sizeof ens_bank[b]);
    }
```

`up_put` (~307): save the whole `ens_bank` entry, and an erase clears the slot's macros; a failed save restores them:

```c
    mac_rec_t oldm = *umac_rec(k);                    /* (with `old`: restored when the write fails) */
    ...
    if (!r)
        memset(umac_rec(k), 0, sizeof(mac_rec_t));
    ...
    if (flash_ok && st_save(OBJ_UPRESET0 + k / UP_PER_BANK, &ens_bank[k / UP_PER_BANK], sizeof(ens_bank_t))) {
        *up_rec(k) = old;
        *umac_rec(k) = oldm;
        ...
```

(The `if (k >= UP_SLOTS) return 1;` check stays first: `oldm` is read after it. The `transport_busy()` refusal
returns before anything is changed, so it needs no restore.)

`up_store_cat` (~399): before `int rc = up_put(k, &r);`, write the track's macros into the slot, and put them back if
the save fails:

```c
        mac_rec_t prev = *umac_rec(k);
        umac_rec(k)->engine = r.engine;
        umac_rec(k)->ok = 1;
        memcpy(umac_rec(k)->m, mac_trk[song.sel].m, sizeof umac_rec(k)->m);
        int rc = up_put(k, &r);
        if (rc == 1 || rc == 2)
            *umac_rec(k) = prev;
```

Delete the temporary `#ifndef ENS_UMAC ... #endif` block from `macro.c`.

- [ ] **Step 4: Run the tests**

Run the compile line and `build/host/ens_umac_test` → `ens umac test passed`; `tests/run_tests.sh 2>&1 | tail -3` →
`ALL HOST TESTS PASSED`. `persistence_test` and `backup_test` exercise the banks: if one fails on the bank length,
it pinned Felucca's 3080-byte object — update it to `sizeof(ens_bank_t)` only where it checks what is written, and
name it in the commit message.

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "ENSEMBLE: user presets keep their macros (the bank sector's tail; Felucca banks load with defaults)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: HOME — role rows, macro columns, macro knobs, project loads

**Files:**
- Create: `tests/ens_home_test.c`
- Modify: `firmware/src/ui_draw.c:871-880` (HOME columns), `firmware/src/ui_graph.c` (rows, after
  `draw_home_tracks` ~1835; `draw_graph` ~2159), `firmware/src/ui_input.c` (~1583-1589 HOME knobs; ~70-76 the
  momentary helper), `firmware/src/ui.c:679` (`home_tap`), `firmware/src/project.c` (`project_restore_runtime`
  ~967-1041), `tests/run_tests.sh`

**Interfaces:**
- Consumes: `mac_trk`, `MAC_NAME`, `mac_turn`, `mac_track_loaded` (Task 4); `ROLE_NAME`, `role_of` (Task 3);
  Felucca's `draw_column`, `fmt_int`, `VAL`, `trk_short_name`, `meter_px`, `tr_meter`, `ts`, `cv_*`, `TR_*`.
- Produces: `static void ens_draw_rows(void);` (ui_graph.c).

- [ ] **Step 1: Write the failing test** `tests/ens_home_test.c`

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* ENSEMBLE HOME (spec §7, §9.5): KNOB 1-4 turn the selected track's macros, ALGORITHM picks the track, HOME stays
 * HOME, a project load recomputes the macros. Run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

int main(void)
{
    int bad = 0;
    uint32_t v0;
    ui_power_on();
    bad += check("power-on: HOME", ui.home == 1);
    v0 = mac_trk[0].v[0];
    turn(EN_K1, 5);
    frame();
    bad += check("KNOB 1: PADS' TONE turned", mac_trk[0].v[0] > v0);
    bad += check("KNOB 1: its target follows", trk[0].p[mac_trk[0].m[0].t[0].param] ==
                 mac_map(&trk[0], &mac_trk[0].m[0].t[0], mac_trk[0].v[0]));
    turn(EN_ALGO, 1);
    frame();
    bad += check("ALGORITHM: BASS selected", song.sel == 1);
    turn(EN_K4, 3);
    frame();
    bad += check("KNOB 4 on BASS: GLIDE", trk[1].p[P_GLIDE] == mac_map(&trk[1], &mac_trk[1].m[3].t[0], mac_trk[1].v[3]));
    press(B_HOME);
    bad += check("HOME on HOME: stays HOME (no MIXER)", ui.home == 1);
    press(B_EDIT);
    bad += check("EDIT: the sound's pages", ui.home == 0 && PAGES[ui.page].fam == FAM_EDIT);
    press(B_HOME);
    bad += check("HOME from EDIT: HOME", ui.home == 1);
    {                                                                /* project load */
        trk[0].eng_req = ENGI_FM6;                                   /* as a project would restore it */
        project_restore_runtime();
        bad += check("project load: PADS' TONE is FM6's (E2)", mac_trk[0].m[0].t[0].param == P_E0 + 2u);
    }
    printf("%s\n", bad ? "ENS HOME TEST FAILED" : "ens home test passed");
    return bad != 0;
}
```

(If `project_restore_runtime` takes arguments, read its definition in `project.c` and pass what a slot load passes;
keep the check.) Add to `tests/run_tests.sh`:

```bash
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/ens_home_test" tests/ens_home_test.c -lm
    run "ENSEMBLE HOME: macro knobs, track select, HOME stays, project loads" "$OUT/ens_home_test"
```

- [ ] **Step 2: Run it to see it fail**

Expected: it compiles and FAILS on "KNOB 1: PADS' TONE turned" (HOME's knobs still edit the engine's parameters).

- [ ] **Step 3: HOME knobs → macros** (`firmware/src/ui_input.c` ~1583)

Replace the `if (ui.home) { ... }` block (the one with `home_param(k, &vp)` and `param_turn`) with:

```c
        if (ui.home) {
            mac_turn(song.sel, k, accel(EN_K1 + k, s, 127));          /* ENSEMBLE: the track's macros */
        } else {
```

In the momentary helper (~70-76, the function containing `const track_t *t = ui.home ? home_trk(k) : TSEL;`), make
HOME take no momentary turns: replace `if (ui.home) { d = home_param(k, &vp); } else {` with
`if (ui.home) { return 0; } else {`.

- [ ] **Step 4: HOME's columns = the macros** (`firmware/src/ui_draw.c` `draw_columns`, ~871)

Replace the body of `if (ui.home) { ... }` (the loop over `home_param`) with:

```c
    if (ui.home) {                                    /* ENSEMBLE: the selected track's four macros */
        const mac_set_t *ms = &mac_trk[song.sel];
        for (c = 0; c < 4u; c++) {
            fmt_int(val, ms->v[c]);
            draw_column(c, MAC_NAME[ms->m[c].name], val, "", ms->m[c].name ? VAL(c) : T_DIM,
                        (int32_t)ms->v[c] * 1000 / 127, ICON_NONE);
        }
        return;
    }
```

- [ ] **Step 5: HOME's rows = the role tracks** (`firmware/src/ui_graph.c`)

After `draw_home_tracks` (~1835) add:

```c
/* ENSEMBLE HOME: a row per track: its role, its state (stage 1: PLAY), its sound, its meter; the selected one THEME */
static uint32_t ens_row_sig[NTRK];
static void ens_track_row(uint32_t c)
{
    track_t *t = &trk[c];
    uint32_t sel = c == song.sel, mute = t->p[P_MUTE] != 0, sig;
    int32_t y = Y_GRAPH + (int32_t)c * TR_PITCH, m, ny = TR_NY(0) - AF_S_CAP_Y;
    uint16_t bg = sel ? T_THEME : T_SURF, fg = sel ? T_INK : mute ? T_DIM : T_TEXT;
    uint16_t cs = sel ? ux_mix(T_THEME, T_INK, 35) : T_RAISE, cm = sel ? T_INK : T_MID;
    char name[16];
    trk_short_name(c, name);
    m = mute ? 0 : meter_px(t->peak);
    t->peak = 0;
    if (m < ts.meter[c] - 1)
        m = ts.meter[c] - 1;
    sig = str_hash(1u + sel + mute * 2u + t->eng_req * 8u, name);
    if (!ui.force && sig == ens_row_sig[c]) {
        if (m != ts.meter[c]) {
            ts.meter[c] = (uint8_t)m;
            cv_begin(4, TR_MH, bg);
            tr_meter(0, 0, c, cs, cm, bg);
            cv_blit(3 + TR_MX, (uint32_t)(y + TR_MY));
        }
        return;
    }
    ens_row_sig[c] = sig;
    ts.meter[c] = (uint8_t)m;
    cv_begin(TR_W, TR_H, T_BG);
    cv_rrect(0, 0, TR_W, TR_H, 5, bg, T_BG);
    cv_free_text(8, ny, &AF_S, ROLE_NAME[role_of(c)], fg, bg, 44);
    cv_free_text(58, ny, &AF_S, "PLAY", cm, bg, 40);
    cv_free_text(104, ny, &AF_S, name, fg, bg, TR_MX - 108);
    tr_meter(TR_MX, TR_MY, c, cs, cm, bg);
    cv_blit(3, (uint32_t)y);
}
static void ens_draw_rows(void)
{
    uint32_t c;
    if (ui.force) {                                  /* as draw_home_tracks: the BG round the rows */
        lcd_fill(0, Y_GRAPH, 3, H_GRAPH, T_BG);
        lcd_fill(3 + TR_W, Y_GRAPH, 240 - 3 - TR_W, H_GRAPH, T_BG);
        for (c = 0; c < NTRK; c++)
            ts.meter[c] = 0;
        for (c = 1; c < NTRK; c++)
            lcd_fill(3, (uint32_t)(Y_GRAPH + (int32_t)c * TR_PITCH - 2), TR_W, 2u, T_BG);
    }
    for (c = 0; c < NTRK; c++)
        ens_track_row(c);
}
```

In `draw_graph` (~2159), replace `if (ui.home && home_tracks()) { draw_home_tracks(); return; }` with:

```c
    if (ui.home) {                                   /* ENSEMBLE HOME: the role tracks */
        ens_draw_rows();
        return;
    }
```

(`tr_meter`'s argument order is the one `home_track_row` uses at ~1755: `x, y, track, stub colour, bar colour, bg`.
If `cv_free_text`'s last argument is not a width, match `home_track_row`'s call at ~1778.)

- [ ] **Step 6: HOME tap goes HOME; project loads recompute macros**

`firmware/src/ui.c:679`, replace `home_tap`'s body with `go_home();` (keep its comment, reworded: *ENSEMBLE: HOME
always goes HOME; the MIXER and CLOCK pages are left for stage 2's conductor layer*).

`firmware/src/project.c` `project_restore_runtime` (~967): at its end add

```c
    for (uint32_t i = 0; i < NTRK; i++)               /* ENSEMBLE: each sound's macros (a project keeps none) */
        mac_track_loaded(&trk[i]);
```

- [ ] **Step 7: Run the tests and look at HOME**

Run: the compile line + `build/host/ens_home_test` → `ens home test passed`; `tests/run_tests.sh` → `ALL HOST TESTS
PASSED` (UI tests that drove HOME's old knobs, MIXER from HOME or the HOME TRACKS view: update or delete those checks
only, list them in the commit message). Render HOME: `build/host/ui_render build/ui_new build/ui_slot` and
`python3 tests/ui_render.py ...` as `run_tests.sh:249-254` does; open the HOME PNG and check: four rows PADS BASS KEYS
LEAD with their sound names, the selected row highlighted, four macro cards (TONE MOTION SWELL SPACE on PADS), no
text clipped (the layout lint reports none).

- [ ] **Step 8: Commit**

```bash
git add -A && git commit -m "ENSEMBLE HOME: role rows, the selected track's macros on KNOB 1-4, HOME stays HOME, project loads set macros

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Page buttons — placeholders for later stages

**Files:**
- Modify: `firmware/src/ui.c` (after `ui_message`, ~426), `firmware/src/ui_input.c:1193` (`page_tap`),
  `firmware/src/ui_layer.c:306` (`layer_tap`), `tests/ens_home_test.c`

**Interfaces:**
- Produces: `static int ens_soon(uint32_t b);` (1 = a placeholder message was shown for button `b`).

- [ ] **Step 1: Add the failing checks** to `tests/ens_home_test.c`, before the `printf`:

```c
    press(B_SEQ);
    bad += check("SEQ: LOOPER SOON, HOME stays", msg_is("LOOPER SOON") && ui.home == 1);
    press(B_REC);
    bad += check("REC: LOOPER SOON", msg_is("LOOPER SOON"));
    press(B_ARP);
    bad += check("ARP: AUTO SOON", msg_is("AUTO SOON"));
    press(B_SCL);
    bad += check("SCL: HARMONY SOON", msg_is("HARMONY SOON"));
    press(B_FX);
    bad += check("FX: Felucca's FX pages", ui.home == 0 && PAGES[ui.page].fam == FAM_FX);
    press(B_HOME);
```

- [ ] **Step 2: Run to see them fail** — expected FAIL at "SEQ: LOOPER SOON" (SEQ opens Felucca's SEQ page).

- [ ] **Step 3: Implement**

`firmware/src/ui.c`, after `ui_message`:

```c
/* ENSEMBLE stage 1: the buttons whose pages later stages bring say so (spec §9.2); their held layers stay Felucca's */
static int ens_soon(uint32_t b)
{
    const char *s = b == B_SEQ || b == B_REC ? "LOOPER SOON" : b == B_ARP ? "AUTO SOON" : b == B_SCL ? "HARMONY SOON" : 0;
    if (!s)
        return 0;
    ui_message(s);
    return 1;
}
```

`ui_input.c` `page_tap(uint32_t b)`: first line `if (ens_soon(b)) return 1;` (1 = no page opened).
`ui_layer.c` `layer_tap(uint32_t l)`: first line `if (ens_soon(LAYERS[l].btn)) return;`.

- [ ] **Step 4: Run** `ens_home_test` → passed; `tests/run_tests.sh` → ALL PASSED (UI tests that opened SEQ, SCL, ARP
  pages or armed REC by a tap: change them to reach those pages through `go_page`/`open_family` directly if they test
  page content, delete them if they test the tap; list them in the commit message).

- [ ] **Step 5: Commit**

```bash
git add -A && git commit -m "ENSEMBLE: SEQ, REC, ARP, SCL taps say which stage brings them; FX opens Felucca's FX pages

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: EDIT > MACRO page

**Files:**
- Create: `firmware/src/ui_macro.c`, `tests/ens_macro_page_test.c`
- Modify: `firmware/src/params.c:426-489` (`GR_MACRO`, the page), `firmware/src/ui_input.c` (`edit_param` ~909; the
  hot-column list ~1576; the momentary helper's refusal list ~77), `firmware/src/ui_graph.c` (`draw_graph` ~2159),
  `firmware/src/ui_draw.c` (`draw_columns` ~882; the owned-parameter mark), `firmware/src/felucca.c`,
  `tests/ui_test.c`, `web/emu/felucca_web.c` (include `ui_macro.c` after `ui_graph.c`), `tests/run_tests.sh`

**Interfaces:**
- Consumes: `mac_trk`, `macro_t`, `MAC_NAME`, `MN_COUNT`, `mac_set_macros`, `mac_owns` (Task 4).
- Produces: `GR_MACRO`; `static uint8_t mac_row;` (0..15: macro `row / 4`, part `row % 4`: 0 its name, 1..3 a
  target); `static void mac_page_knob(uint32_t slot, int32_t steps);`, `static void mac_page_columns(void);`,
  `static void mac_page_draw(void);`, `static const uint8_t MAC_PARAMS[]` (the parameters a macro may target).

- [ ] **Step 1: Write the failing test** `tests/ens_macro_page_test.c`

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* ENSEMBLE EDIT > MACRO (spec §9.4): KNOB 1 the row, KNOB 2 a name or a target's parameter, KNOB 3 / 4 its min / max
 * inside the parameter's range; a new target starts at the parameter's whole range. Run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

int main(void)
{
    int bad = 0;
    uint32_t i, page = NPAGES;
    ui_power_on();
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_MACRO) page = i;
    bad += check("MACRO: a page of EDIT", page < NPAGES && PAGES[page].fam == FAM_EDIT);
    ui.home = 0;
    ui.page = (uint8_t)page;
    page_entered();
    frame();
    mac_row = 0;
    mac_page_knob(1, 1);                                             /* KNOB 2 on macro 1's name */
    bad += check("KNOB 2 on a name: the next name", mac_trk[0].m[0].name == MN_TONE + 1u);
    mac_page_knob(0, 4);                                             /* KNOB 1: row 4 = macro 2's name */
    mac_page_knob(0, 2);                                             /* row 6: macro 2, its second target */
    bad += check("KNOB 1: the row", mac_row == 6u);
    mac_page_knob(1, 1);                                             /* a parameter for that target */
    {
        const mac_tgt_t *g = &mac_trk[0].m[1].t[1];
        const param_desc_t *d = g->param < P_COUNT ? track_desc(&trk[0], g->param) : 0;
        bad += check("a new target: the parameter's whole range", d && g->min == d->min && g->max == d->max);
        mac_page_knob(2, 500);
        bad += check("KNOB 3: min clamps to the range", g->min == d->max);
        mac_page_knob(3, -500);
        bad += check("KNOB 4: max clamps to the range", g->max == d->min);
        bad += check("the parameter is owned (marked on its pages)", mac_owns(0, g->param));
    }
    printf("%s\n", bad ? "ENS MACRO PAGE TEST FAILED" : "ens macro page test passed");
    return bad != 0;
}
```

Add it to `tests/run_tests.sh` as the other ENSEMBLE tests.

- [ ] **Step 2: Run it to see it fail** — compile error, `GR_MACRO` undeclared.

- [ ] **Step 3: The page in the table** (`firmware/src/params.c`)

Append `GR_MACRO` to the `GR_*` enum (after `GR_INS`). In `PAGES[]`, after the last `FAM_EDIT` entry
(`grep -n 'FAM_EDIT' firmware/src/params.c`), add:

```c
    {"MACRO", FAM_EDIT, SC_TRACK, GR_MACRO, {0xFF, 0xFF, 0xFF, 0xFF}},   /* ENSEMBLE: the track's macros (ui_macro.c) */
```

- [ ] **Step 4: Write `firmware/src/ui_macro.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* ENSEMBLE EDIT > MACRO (spec §9.4): the selected track's four macros as 16 rows, a macro's name then its three
 * targets. KNOB 1 the row; on a name KNOB 2 the name (MAC_NAME); on a target KNOB 2 its parameter (MAC_PARAMS, "-"
 * first), KNOB 3 its min, KNOB 4 its max (inside the parameter's range). A new parameter starts at its whole range.
 * The macro's position is read back from the sound after every change (mac_set_macros). */
static uint8_t mac_row;
static const uint8_t MAC_PARAMS[] = {MAC_NONE, P_LEVEL, P_ATK, P_DEC, P_SUS, P_REL, P_ED_FLT, P_LRATE, P_LD_PIT,
    P_LD_FLT, P_LD_SHP, P_LD_AMP, P_DIST, P_CHOR, P_DLY, P_REV, P_GLIDE, P_PAN, P_DETUNE, P_SPRD, P_IA, P_IB, P_IC,
    P_IMIX, P_E0, P_E0 + 1, P_E0 + 2, P_E0 + 3, P_E0 + 4, P_E0 + 5, P_E0 + 6, P_E0 + 7};
#define MAC_NPARAMS (sizeof MAC_PARAMS / sizeof MAC_PARAMS[0])

static uint32_t mac_param_rank(uint8_t p)
{
    uint32_t i;
    for (i = 0; i < MAC_NPARAMS; i++)
        if (MAC_PARAMS[i] == p) return i;
    return 0;
}

static void mac_page_knob(uint32_t slot, int32_t steps)
{
    mac_set_t *s = &mac_trk[song.sel];
    macro_t m[MAC_N];
    macro_t *mm;
    mac_tgt_t *g;
    int32_t v;
    if (slot == 0u) {
        v = (int32_t)mac_row + steps;
        mac_row = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
        ui.force = 1;
        return;
    }
    memcpy(m, s->m, sizeof m);
    mm = &m[mac_row / 4u];
    if (mac_row % 4u == 0u) {
        if (slot == 1u) {
            v = (int32_t)mm->name + steps;
            mm->name = (uint8_t)(v < 1 ? 1 : v >= (int32_t)MN_COUNT ? MN_COUNT - 1 : v);
        }
    } else {
        g = &mm->t[mac_row % 4u - 1u];
        if (slot == 1u) {
            v = (int32_t)mac_param_rank(g->param) + steps;
            v = v < 0 ? 0 : v >= (int32_t)MAC_NPARAMS ? (int32_t)MAC_NPARAMS - 1 : v;
            g->param = MAC_PARAMS[v];
            if (g->param < P_COUNT) {
                const param_desc_t *d = track_desc(TSEL, g->param);
                g->min = (int8_t)d->min;
                g->max = (int8_t)d->max;
            }
        } else if (g->param < P_COUNT) {
            const param_desc_t *d = track_desc(TSEL, g->param);
            int8_t *b = slot == 2u ? &g->min : &g->max;
            v = (int32_t)*b + steps;
            *b = (int8_t)(v < d->min ? d->min : v > d->max ? d->max : v);
        }
    }
    mac_set_macros(TSEL, m);
    ui.force = 1;
}

static void mac_tgt_text(const mac_tgt_t *g, char *b, uint32_t n)
{
    char x[8];
    if (g->param >= P_COUNT) { str_cpy(b, "-", n); return; }
    str_cpy(b, track_desc(TSEL, g->param)->label, n);
    str_cpy(b + str_len(b), " ", n - str_len(b));
    fmt_int(x, g->min);
    str_cpy(b + str_len(b), x, n - str_len(b));
    str_cpy(b + str_len(b), "..", n - str_len(b));
    fmt_int(x, g->max);
    str_cpy(b + str_len(b), x, n - str_len(b));
}

static void mac_page_columns(void)
{
    const macro_t *mm = &mac_trk[song.sel].m[mac_row / 4u];
    char val[12];
    fmt_int(val, (int32_t)mac_row / 4 + 1);
    draw_column(0, "MACRO", val, "", VAL(0u), -1, ICON_NONE);
    if (mac_row % 4u == 0u) {
        draw_column(1, "NAME", MAC_NAME[mm->name], "", VAL(1u), -1, ICON_NONE);
        draw_column(2, "", "", "", T_THEME, -1, ICON_NONE);
        draw_column(3, "", "", "", T_THEME, -1, ICON_NONE);
    } else {
        const mac_tgt_t *g = &mm->t[mac_row % 4u - 1u];
        int on = g->param < P_COUNT;
        draw_column(1, "PARAM", on ? track_desc(TSEL, g->param)->label : "-", "", VAL(1u), -1, ICON_NONE);
        fmt_int(val, g->min);
        draw_column(2, "MIN", on ? val : "-", "", on ? VAL(2u) : T_DIM, -1, ICON_NONE);
        fmt_int(val, g->max);
        draw_column(3, "MAX", on ? val : "-", "", on ? VAL(3u) : T_DIM, -1, ICON_NONE);
    }
}

static uint32_t mac_page_sig;
static void mac_page_draw(void)
{
    const mac_set_t *s = &mac_trk[song.sel];
    uint32_t k, i, sig = str_hash(mac_row + 1u, MAC_NAME[s->m[0].name]);
    char b[24];
    for (k = 0; k < MAC_N; k++)
        for (i = 0; i < MAC_TGT; i++)
            sig = sig * 31u + s->m[k].name * 7u + s->m[k].t[i].param * 131u + (uint8_t)s->m[k].t[i].min * 17u +
                  (uint8_t)s->m[k].t[i].max;
    if (!ui.force && sig == mac_page_sig)
        return;
    mac_page_sig = sig;
    cv_begin(240, graph_h(), T_BG);
    cv_rrect(3, 0, 234, graph_h(), 5, T_SURF, T_BG);
    for (k = 0; k < MAC_N; k++) {                     /* a line per macro: its name, then its targets */
        int32_t y = 6 + (int32_t)k * 28;
        uint16_t c = mac_row / 4u == k ? T_ACCENT : T_TEXT;
        cv_free_text(10, y, &AF_S, MAC_NAME[s->m[k].name], c, T_SURF, 70);
        for (i = 0; i < MAC_TGT; i++) {
            mac_tgt_text(&s->m[k].t[i], b, sizeof b);
            cv_free_text(84 + (int32_t)i * 52, y, &AF_S, b,
                         mac_row == k * 4u + i + 1u ? T_ACCENT : T_MID, T_SURF, 50);
        }
    }
    cv_blit(0, (uint32_t)Y_GRAPH);
}
```

(Use `graph_h()`, `Y_GRAPH`, `cv_free_text` exactly as `draw_graph` and `home_track_row` use them; if a helper's name
differs in 1.5, match its use there.)

Include `ui_macro.c` in `felucca.c`, `tests/ui_test.c` and `web/emu/felucca_web.c` on the line after `ui_graph.c`.

- [ ] **Step 5: Wire the page in**

- `ui_input.c` `edit_param` (~909), before `if (fop_page(pg->graph)) {`:
  `if (pg->graph == GR_MACRO) { mac_page_knob(slot, steps); return; }`
- `ui_input.c` hot-column condition (~1576): add `|| pg->graph == GR_MACRO` to the list.
- `ui_input.c` momentary helper's refusal list (~77, the line with `pg->graph == GR_EVENTS || pg->graph == GR_SONG`):
  add `pg->graph == GR_MACRO ||`.
- `ui_graph.c` `draw_graph`, after the `GR_TRK` branch: `if (!ui.home && pg->graph == GR_MACRO) { mac_page_draw(); return; }`
- `ui_draw.c` `draw_columns`, before `if (cur_page()->graph == GR_SONG) {`:
  `if (cur_page()->graph == GR_MACRO) { mac_page_columns(); return; }`
- The owned mark: in `draw_columns`' page branch, where a parameter column is drawn with `draw_column(c, d->label, ...)`
  for a page of `SC_TRACK` or `SC_ENGINE`, use a label buffer: if `mac_owns(song.sel, id)` (`id = vp - TSEL->p`), draw
  the label as `"M "` + `d->label` (`char lb[16]`, `str_cpy`); else `d->label`.

- [ ] **Step 6: Run** the page test → passed; `tests/run_tests.sh` → ALL PASSED. Render the MACRO page with
  `ui_render` (as Task 6 Step 7) and check the four macro lines and the cards read correctly, nothing clipped.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "ENSEMBLE: EDIT > MACRO edits the track's macros (name, targets, ranges); owned parameters marked M

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Listening check of the default macros (the user)

**Files:**
- Create: `tools/mac_wavs.c` (host render), `build/mac/*.wav` (not committed)
- Modify (only if the user asks): `firmware/src/macro.c` (`MAC_DEF`, or a per-preset override table)

**Interfaces:**
- Consumes: `mac_turn`, `mac_trk`, Felucca's `tests/hostsim.c` render path (as `regress.c` renders a preset).

- [ ] **Step 1: Write the renderer** `tools/mac_wavs.c`: include `tests/hostsim.c` (as `regress.c` does), and for each
  role's first 3 factory presets and each of the 4 macros, render a 4 s held chord (PADS, KEYS: C4 E4 G4; BASS, LEAD:
  C3 / C4) with the macro swept 0 → 127 over the 4 s (`mac_turn(track, k, 1)` every 1/32 s), writing
  `build/mac/<role>_<preset>_<macro>.wav` with hostsim's WAV writer.
- [ ] **Step 2: Build and render**

```bash
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/mac_wavs tools/mac_wavs.c -lm && mkdir -p build/mac && build/host/mac_wavs build/mac
ls build/mac | wc -l   # expect 48
```

- [ ] **Step 3: Ask the user** to listen (`open build/mac`) and say which macros sound wrong (too subtle, too harsh,
  a dead range). Change `MAC_DEF` ranges for what they name; for a preset-specific wish, add a sparse override table
  `MAC_OVR[] = {{engine, preset, role, {4 macros}}}` consulted in `mac_defaults` before the role's set, with a test in
  `ens_macro_test.c` that the override is installed. Re-render, re-listen until the user approves.
- [ ] **Step 4: Commit** (the renderer and any changes)

```bash
git add tools/mac_wavs.c firmware/src/macro.c tests/ens_macro_test.c
git commit -m "ENSEMBLE macros: the listening renders; the default ranges as the user approved them

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: BENCH with synth cases, the CPU baseline

**Files:**
- Create: `firmware/src/bench.c`, `tools/fm1_bench.py`, `tests/bench_host.c` (from Hortator), `docs/PERFORMANCE.md`,
  `docs/bench/<the run's date, YYYY-MM-DD>-baseline.md`
- Modify: `firmware/src/felucca.c` (include `bench.c` before `audio.c`), `firmware/src/audio.c` (the ISR hook),
  `firmware/src/console.c` (`bench` command), `tests/run_tests.sh` (bench_host case-list check)

**Interfaces:**
- Consumes: Felucca's `trk`, `set_engine_of`, `apply_preset_to`, `trk_note_on(track_t *, uint32_t note, uint32_t vel)`,
  `NVOICE`, `NPART`, `P_VOICE`, `V_POLY`, `P_SUS`, `shed_check`.
- Produces: console `bench yes` / `bench stop`; `tools/fm1_bench.py --yes` writing `build/bench/report.md`.

- [ ] **Step 1: Copy Hortator's BENCH**

```bash
H=~/Desktop/dev/fm1-drummachine/felucca
cp $H/firmware/src/bench.c firmware/src/bench.c
cp $H/tools/fm1_bench.py tools/fm1_bench.py
cp $H/tests/bench_host.c tests/bench_host.c
```

Keep in `bench.c`: the state machine (HOLD → SETUP → WARM 86 halves → MEASURE 344 halves), `bench_task`,
`bench_time`, the result line format `bench N name halves sum_us max_us late`, `bench_mute`. Wire as in Hortator:
`felucca.c` includes `bench.c` just before `audio.c`; `console.c` `con_exec` takes `bench yes` / `bench stop` and
`cdc_task` calls `bench_task()` (copy Hortator's lines; `grep -n bench $H/firmware/src/console.c $H/firmware/src/audio.c`);
in `audio.c` wrap Felucca's `us = shed_check(...)` so that in a bench run the ISR renders into `bench.out`, mutes the
DAC buffer, never sheds, and calls `bench_time(us, late)` (Hortator `audio.c` ~65-87). Delete the drum parts
(everything that names `drum_*`, `DM_*`, `RS_*`, `N_MODEL`, `grids`, `gclk`, `comp_reset`, `P_RMODEL`, `P_LLEVEL`,
`P_DUCK`, `safe_start`).

- [ ] **Step 2: The synth cases** — replace Hortator's `bench_name` / `bench_setup` / `bench_reset` with:

```c
/* the cases: 0 idle; 1..N_ENG_CASES one engine's heaviest preset (BENCH_PRESET) on track 0, 8 notes held (POLY,
 * SUS full; one-shot engines re-struck every 64 blocks as regress.c does); then the power-on mix (all four tracks, two
 * notes each) */
static const uint8_t BENCH_ENG[] = {0, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13};
static const uint8_t BENCH_PRESET[] = {   /* per BENCH_ENG: the heaviest in tests/cpu_baseline.txt (1.5) */
    6,  /* ANALOG RAVE 1461 */      5,  /* PHASE WIRE 1558 */       3,  /* LOFI WAVE LEAD 807 */
    3,  /* SAMPLE SAX 907 */        0,  /* VOICE CHOIR AAH 975 */   4,  /* TRIO CHIP CHOIR 1478 */
    0,  /* WHEEL FULL ORGAN 1418 */ 3,  /* GRAIN SHIMMER 906 */      3,  /* PHYS BOWED METAL 1451 */
    1,  /* NOISE RAIN 1395 */       4,  /* FM6 PAD 1475 */          1,  /* SLICE STUTTER 1008 */
};
_Static_assert(sizeof BENCH_PRESET == sizeof BENCH_ENG, "a preset per engine case");
#define N_ENG_CASES (sizeof BENCH_ENG)
#define BENCH_N (2u + N_ENG_CASES)
static const uint8_t BENCH_NOTES[8] = {48, 52, 55, 59, 60, 64, 67, 71};
```

`bench_reset()` does what
`host_tracks_init` (`tests/hostsim.c:71`) does plus Felucca's fx / master / voice reset (the same calls `felucca_init`
makes before its track loop). `bench_setup(i)`: case 0 nothing; an engine case: `set_engine_of(&trk[0], e)`,
`apply_preset_to(&trk[0], BENCH_PRESET[i - 1])`, `trk[0].p[P_VOICE] = V_POLY`, `trk[0].p[P_SUS] = 127`, then
`trk_note_on(&trk[0], BENCH_NOTES[n], 100)` for n < 8; the mix case: `felucca_init`'s sounds, two notes per track.
`bench_name(i)`: `"idle"`, the engine's name + `" "` + the preset's name, `"mix"`. `tests/bench_host.c`: rebuild on
`tests/hostsim.c` and regress.c's `instr_now`, same cases, printing instructions per sample per case.

- [ ] **Step 3: Host check** — add to `tests/run_tests.sh`:

```bash
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/bench_host" tests/bench_host.c -lm
    run "BENCH: every case renders on the host" "$OUT/bench_host" --list
```

(`--list` renders each case for 1 s and prints its name and instructions per sample; non-zero exit if a case renders
silence where notes are held.) Run `tests/run_tests.sh` → ALL PASSED.

- [ ] **Step 4: On the FM-1 (the user)** — build, install with `python3 tools/fm1_install.py build/felucca.fwsc`, then
  ask the user to keep the FM-1 on USB and run `python3 tools/fm1_bench.py --yes` (~3 min, silent). Copy
  `build/bench/report.md` to `docs/bench/<date>-baseline.md`.
- [ ] **Step 5: `docs/PERFORMANCE.md`** — the measured table (idle, each engine case, mix: CPU average, worst half,
  late halves) and the spec §8 targets restated in measured numbers (the "100 %" in host instructions per sample for
  Felucca's 128-frame halves; which engines leave room for track FX and macro FX). Commit:

```bash
git add -A && git commit -m "BENCH for ENSEMBLE: synth cases (each engine's heaviest preset x 8 notes, the mix), the FM-1 baseline

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: The web emulator and the release check

**Files:**
- Modify: `web/emu/felucca_web.c` (includes already added in Tasks 3, 4, 8), `web/emu/index.html` (title), `CHANGELOG.md`

- [ ] **Step 1: Build the emulator**

```bash
web/emu/build.sh 2>&1 | tail -3
```

If `emcc` is missing: `docker run --rm -v "$PWD":/src -w /src emscripten/emsdk:6.0.11 web/emu/build.sh`.

- [ ] **Step 2: Its tests**

```bash
cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/host/emu_native web/emu/native_check.c -lm
build/host/emu_native build/host/emu_native.f32
node web/emu/emu_test.mjs build/emu/felucca.wasm build/host/emu_native.f32
```

Expected: emu_test passes (bit-exact with native, real-time cost under its limit).

- [ ] **Step 3: Play it** — `python3 -m http.server -d build/emu 8790`, open `http://localhost:8790`: HOME shows PADS
  BASS KEYS LEAD, the keys play the selected track, ALGORITHM changes track, PRESETS stays in the role, KNOB 1–4 move
  the macros (audible), EDIT → the MACRO page edits a macro, SEQ / ARP / SCL / REC say SOON. Change `index.html`'s
  title to `ENSEMBLE`.
- [ ] **Step 4: Full check and commit**

```bash
./build.sh && python3 tools/version.py --check && tests/run_tests.sh 2>&1 | tail -1
git add -A && git commit -m "ENSEMBLE 0.1.0 stage 1: the web emulator; CHANGELOG

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

Expected: build ok, version ok, `ALL HOST TESTS PASSED`. Then ask the user to install `build/felucca.fwsc` on the
FM-1 (installer page or `tools/fm1_install.py`) and play the same checklist as Step 3 on the hardware.
