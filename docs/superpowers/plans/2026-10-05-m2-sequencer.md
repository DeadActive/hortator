# M2 — step PROB / conditions / RATCH, Grids — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Per-step chance / loop conditions and ratchets on the 8 drum tracks, and one Mutable Instruments Grids
engine (map + Euclidean) that any track can follow, with FDR2 projects that still load M1's FDR1.

**Architecture:** `step_t` grows `cond` / `rat` (zero = default); `seq.c` decides each step once (`step_plays`) and
plays its roll (`step_fire` / `rat_due`); `grids.c` is a faithful port of `grids/pattern_generator` stepped by its
own 1/32 clock in `seq.c` (`grids_tick`), routing its channels to tracks whose `P_SRC` names one; the UI gets a
hold + knob step editor, GRIDS pages on ARP, and read-only "view" helpers so every step display shows a Grids
track's generated pattern.

**Tech Stack:** C (single translation unit `firmware/src/felucca.c`, clang 4 on the pi32v2 target: no float,
no 64÷64 division), host tests in C (`tests/*.c`, `cc`), the Grids reference in C++ (`c++ -std=c++11`), Python 3
(stdlib) for generators.

**Spec:** `docs/superpowers/specs/2026-10-05-m2-sequencer-design.md` (amended at plan review: zero-safe step
encodings, Grids 1/32 clock, 5-character labels).

## Global Constraints

- Never touch the device: no `tools/fm1_install.py`, no web installer, no M-VAVE updater. Never install mido /
  python-rtmidi.
- Packages only with `DRUM_PACKAGE=1` → `build/felucca-UNTESTED.fwsc`.
- Frozen (`tools/check_untouched.py`): `firmware/hal/`, `firmware/loader/`, `ota.c`, `usb.c`, `crt0.S`, `app.ld`,
  `storage.c`, `main.c` outside `felucca_init()` and the boot titles, the last 5 lines of `core.h`.
- Target C: no float, no 64-bit ÷ 64-bit division (`uint64_t` ÷ anything is out), no new runtime calls in ISR code.
- Every source file starts with an SPDX header; ours are `GPL-3.0-only` + `Drum machine fork: 2026 DEADACTIVE`;
  the Grids port and its tables are `GPL-3.0-or-later` with Emilie Gillet's copyright kept.
- Zero-safe encodings: stored `cond` 0 = 100 %, 1..20 = 0 %..95 %, 21 = 1-SHOT, 22..56 = A/B (1/2, 2/2, 1/3 … 8/8);
  stored `rat` = hits − 1 (0..3). The knob shows positions 0..56 (0..20 = 0..100 % ×5, 21 1-SHOT, 22..56 A/B).
- Grids clock: one Grids step = 1/32 note (32 steps = 1 bar), Euclidean evaluates on even steps (1/16s); the
  global swing applies per 1/16, each swung 1/16 split in two equal 1/32s. Seed 0x21 at PLAY.
- Knob → Grids byte: `v * 2 + (v >> 6)` (0..127 → 0..255). Euclidean LEN 1..32 (in 1/16s).
- Labels ≤ 5 chars: `SRC` (`STEP`, `G-KCK`, `G-SNR`, `G-HAT`), `MODE` (`MAP`, `EUCL`), `X`, `Y`, `CHAOS`,
  `FIL K/S/H`, `LEN K/S/H`.
- Project format `FDR2` must fit one flash sector (`_Static_assert`); `FDR1` records in flash load with
  defaults; every loaded value is clamped.
- The user verifies by looking and listening: screenshots go to `build/ui_shots/{seq,grids}/`, WAVs to
  `build/drum_renders/`. Ask before any change to a fidelity metric, cost budget or harness threshold.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Work on branch `m2-sequencer`.

## Review Focus

1. **A tempo change in the middle of a roll** — the step's remaining hits whose time is past the new step end are
   dropped, the next step plays on time, no burst of hits (Task 2: `test_ratchet_tempo_change`).
2. **SRC switched while playing** (STEP → Grids → STEP) — the track's own pattern comes back in step with the
   others, its step keys / recording never write while it follows Grids (Task 5: `test_grids_follow`,
   `test_grids_rec_skip`).
3. **Garbage in flash** — FDR1 and FDR2 records with valid sums and random `cond` / `rat` / `P_SRC` / Grids globals
   load clamped, no table index out of range under ASan / UBSan (Task 1: boot_test F_HEADERS).
4. **MAP ↔ EUCLID and LEN changes while playing** — the Euclidean positions stay inside the length, previews never
   index past the tables (Task 4: `test_grids_mode_switch`).
5. **Hold + knob on a key outside LEN, on a Grids track, or on bank 2** — nothing outside LEN is edited, KNOB 1
   does not turn the bank while a step key is held (Task 3: `test_grid_step_edit`, Task 6: `test_src_readonly`).

## Commands used throughout

```bash
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca
python3 tools/build.py --gen-only >/dev/null          # generated headers (build/gen), once per session
F='-O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src'
mkdir -p build/host
# drum_test (sequencer, models, Grids engine):
cc $F -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test > build/host/dt.txt; grep -v '^ok' build/host/dt.txt | tail -20
# ui_test (pages, grid, projects, screenshots):
mkdir -p build/ui_shots/seq build/ui_shots/grids build/ui_shots/engines
cc $F -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test > build/host/ut.txt; grep -v '^ok' build/host/ut.txt | tail -20
# boot_test (H1, projects in flash), sanitized:
cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function \
   -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/boot_test tests/boot_test.c -lm \
   && build/host/boot_test > build/host/bt.txt 2>&1; grep -v '^ok' build/host/bt.txt | tail -20
# the whole drum host suite:
sh tests/run_drum_tests.sh > build/host/drum_suite.txt 2>&1; tail -5 build/host/drum_suite.txt
```

---

### Task 1: Step data, PROB codes, FDR2 projects

**Files:**
- Modify: `firmware/src/core.h` (step_t, P enum, G enum — not the last 5 lines)
- Modify: `firmware/src/params.c` (N_SRC, N_GMODE, TP[P_SRC], GP grids entries, cond helpers)
- Modify: `firmware/src/project.c` (FDR2, FDR1 conversion, step clamps)
- Modify: `firmware/src/ui.c:track_clear`
- Test: `tests/drum_test.c` (`test_cond_codes`), `tests/ui_test.c` (project tests), `tests/boot_test.c` (FDR1, garbage)

**Interfaces:**
- Produces: `step_t {uint8_t on, acc, cond, rat;}`; `P_SRC` (last before `P_COUNT`); `G_GMODE, G_GX, G_GY, G_GCHAOS,
  G_GFILL1, G_GFILL2, G_GFILL3, G_GLEN1, G_GLEN2, G_GLEN3` (in this order, after `G_DRCH`);
  `#define COND_POS_100 20u`, `COND_1SHOT 21u`, `COND_MAX 56u`; `uint32_t cond_pos(uint32_t stored)`,
  `uint32_t cond_store(uint32_t pos)`, `void cond_ab(uint32_t c, uint32_t *a, uint32_t *b)`,
  `void cond_format(uint32_t c, char *s /* 8 bytes */)`; `PROJ_MAGIC` = "FDR2", `PROJ_MAGIC_V1` = "FDR1",
  `project_v1_t` (FELUCCA_FLASH only), `proj_hash`.

- [ ] **Step 1: Write the failing tests**

`tests/drum_test.c`, before `int main`:

```c
/* PROB codes (params.c): knob position 0..56 <-> stored value; a zeroed step is 100 % */
static void test_cond_codes(void)
{
    uint32_t pos, a, b, n = 0, ok = 1;
    char s[8];
    for (pos = 0; pos <= COND_MAX; pos++)
        ok &= cond_pos(cond_store(pos)) == pos;
    ok &= cond_store(COND_POS_100) == 0u && cond_pos(0) == COND_POS_100 && cond_pos(200) == COND_MAX;
    check("PROB: knob position <-> stored value round trip; stored 0 = 100 %", ok);
    ok = 1;
    for (b = 2; b <= 8u; b++)                        /* 1/2 2/2 1/3 2/3 3/3 1/4 .. 8/8 */
        for (a = 1; a <= b; a++) {
            uint32_t ga, gb;
            cond_ab(22u + n, &ga, &gb);
            ok &= ga == a && gb == b;
            n++;
        }
    check("PROB: codes 22..56 are 1/2, 2/2, 1/3 .. 8/8 in order", ok && n == 35u && 21u + n == COND_MAX);
    cond_format(0, s);
    ok = !strcmp(s, "100%");
    cond_format(cond_store(15), s);
    ok &= !strcmp(s, "75%");
    cond_format(cond_store(0), s);
    ok &= !strcmp(s, "0%");
    cond_format(COND_1SHOT, s);
    ok &= !strcmp(s, "1-SHOT");
    cond_format(33, s);                              /* 22 + 2 + 3 + 4 + 2: the third of B = 5 */
    ok &= !strcmp(s, "3/5");
    check("PROB: shown as 100% / 75% / 0% / 1-SHOT / 3/5", ok);
}
```

and call it first in `main`: `test_cond_codes();`.

`tests/ui_test.c`, extend `test_project_roundtrip` (after `trk[2].p[P_SLEN] = 23;`):

```c
    trk[2].step[7].cond = 33;                        /* 3/5 */
    trk[2].step[7].rat = 2;                          /* 3 hits */
    trk[2].p[P_SRC] = 2;                             /* G-SNR */
    song.g[G_GMODE] = 1;
    song.g[G_GX] = 99;
    song.g[G_GLEN3] = 5;
```

and add a second check after the existing one:

```c
    check("project: load restores PROB / RATCH, SRC and the Grids settings",
          trk[2].step[7].cond == 33 && trk[2].step[7].rat == 2 && trk[2].p[P_SRC] == 2 && song.g[G_GMODE] == 1 &&
              song.g[G_GX] == 99 && song.g[G_GLEN3] == 5);
```

In `test_project_rejects`, after `proj_slot[0].t[3].step[0].on = 7;`:

```c
    proj_slot[0].t[3].step[1].cond = 200;
    proj_slot[0].t[3].step[1].rat = 9;
    proj_slot[0].t[3].p[P_SRC] = 40;
    proj_slot[0].g[G_GLEN1] = -5;
```

and extend its check condition with
`&& trk[3].step[1].cond == COND_MAX && trk[3].step[1].rat == 3 && trk[3].p[P_SRC] == 3 && song.g[G_GLEN1] == 1`.

`tests/boot_test.c`: in `flash_image`, replace the project loop with M1 records on the odd slots:

```c
        for (k = 0; k < 4; k++) {
            if (kind == F_HEADERS && (k & 1u)) {     /* an M1 project ("FDR1"): valid sum over random fields */
                project_v1_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V1;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
            rfill(&pr, sizeof pr);
            pr.magic = kind == F_FELUCCA ? 0x334E5546u : PROJ_MAGIC;   /* Felucca's "FUN3", or ours */
            pr.size = sizeof pr;
            pr.sum = proj_sum(&pr);                   /* ours: valid sum over random tracks */
            st_save(OBJ_PROJECT0 + k, &pr, sizeof pr);
        }
```

and add before `int main`:

```c
/* an M1 project in flash ("FDR1") loads: its steps and settings; PROB 100 %, 1 hit, SRC STEP, Grids defaults */
static int fdr1_converts(void)
{
    project_v1_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V1;
    v.size = sizeof v;
    for (i = 0; i < G_GMODE; i++)
        v.g[i] = song.g[i];
    v.g[G_BPM] = 133;
    v.sel = 2;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_SRC; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[3].p[P_SLEN] = 23;
    v.t[3].step[5].on = v.t[3].step[5].acc = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 1u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    song.g[G_GLEN2] = 3;                             /* the live state differs: the load replaces it */
    trk[3].p[P_SRC] = 2;
    trk[3].step[5].cond = 9;
    project_load(1);
    return trk[3].p[P_SLEN] == 23 && trk[3].step[5].on && trk[3].step[5].acc && trk[3].step[5].cond == 0 &&
           trk[3].step[5].rat == 0 && !trk[3].step[6].on && trk[3].p[P_SRC] == 0 && song.g[G_BPM] == 133 &&
           song.g[G_GLEN2] == 12 && song.g[G_GMODE] == 0 && song.sel == 2;
}
```

and in `main`, before the boot loops:

```c
    check("project: an M1 record (FDR1) loads with PROB 100 %, 1 hit, SRC STEP, Grids defaults", fdr1_converts());
```

- [ ] **Step 2: Run the tests to see them fail**

Run: the drum_test, ui_test and boot_test commands above.
Expected: compile errors — `COND_MAX`, `cond_store`, `cond_pos`, `cond_ab`, `cond_format`, `.cond`, `.rat`, `P_SRC`,
`G_GMODE`, `project_v1_t`, `PROJ_MAGIC_V1` undeclared.

- [ ] **Step 3: Implement**

`firmware/src/core.h` — the step:

```c
typedef struct {                 /* one sequencer step; all zero = a plain step, off */
    uint8_t on, acc;             /* hit; accent (velocity 127, else 96) */
    uint8_t cond;                /* PROB: 0 = 100 %, 1..20 = 0..95 %, 21 = 1-SHOT, 22..56 = A/B (params.c cond_*) */
    uint8_t rat;                 /* RATCH: hits - 1 (0..3) */
} step_t;
```

P enum, after the sample-layer line:

```c
    P_LSET, P_LKEY, P_LLEVEL, P_LTUNE, P_LDEC,       /* sample layer */
    P_SRC,                       /* what the track plays: 0 its steps, 1..3 a Grids channel (kick, snare, hats) */
    P_COUNT
```

G enum, after `G_DRCH`:

```c
    G_DRCH,                      /* MIDI channel of the drum tracks, 1..16 */
    G_GMODE, G_GX, G_GY, G_GCHAOS,                   /* Grids (grids.c): MAP / EUCLID, the map point, chaos */
    G_GFILL1, G_GFILL2, G_GFILL3,                    /* fill per channel (kick, snare, hats) */
    G_GLEN1, G_GLEN2, G_GLEN3,                       /* Euclidean length per channel, 1..32 sixteenths */
    G_COUNT
```

`firmware/src/params.c` — names after `N_CHOKE`:

```c
static const char *const N_SRC[] = {"STEP", "G-KCK", "G-SNR", "G-HAT"};   /* P_SRC: its steps or a Grids channel */
static const char *const N_GMODE[] = {"MAP", "EUCL"};
```

`TP`: `[P_SRC] = PE("SRC", N_SRC, 0),` after `[P_LDEC]`. `GP` after `[G_DRCH]`:

```c
    [G_GMODE] = PE("MODE", N_GMODE, 0),
    [G_GX] = PD("X", F_INT, 0, 127, 64),
    [G_GY] = PD("Y", F_INT, 0, 127, 64),
    [G_GCHAOS] = PD("CHAOS", F_PCT, 0, 127, 0),
    [G_GFILL1] = PD("FIL K", F_PCT, 0, 127, 64),
    [G_GFILL2] = PD("FIL S", F_PCT, 0, 127, 64),
    [G_GFILL3] = PD("FIL H", F_PCT, 0, 127, 64),
    [G_GLEN1] = PD("LEN K", F_STEPS, 1, 32, 16),
    [G_GLEN2] = PD("LEN S", F_STEPS, 1, 32, 12),
    [G_GLEN3] = PD("LEN H", F_STEPS, 1, 32, 8),
```

At the end of `params.c`:

```c
/* PROB of a step (step_t.cond). The knob runs 0 %, 5 % .. 100 % (positions 0..20), 1-SHOT (21), then A/B
 * (22..56: 1/2, 2/2, 1/3 .. 8/8, "plays on loop A of every B"). Stored so that 0 is 100 % (a zeroed step is
 * plain): 0 = 100 %, 1..20 = 0 .. 95 %, 21.. as the knob. */
#define COND_POS_100 20u
#define COND_1SHOT 21u
#define COND_MAX 56u
static uint32_t cond_pos(uint32_t c) { return !c ? COND_POS_100 : c <= COND_POS_100 ? c - 1u : c < COND_MAX ? c : COND_MAX; }
static uint32_t cond_store(uint32_t pos)
{
    return pos == COND_POS_100 ? 0u : pos < COND_POS_100 ? pos + 1u : pos < COND_MAX ? pos : COND_MAX;
}

static void cond_ab(uint32_t c, uint32_t *a, uint32_t *b)   /* an A/B code (22..56): A of every B */
{
    uint32_t i = c > 22u ? (c < COND_MAX ? c : COND_MAX) - 22u : 0u, n = 2u;
    while (i >= n) {
        i -= n;
        n++;
    }
    *a = i + 1u;
    *b = n;
}

static void cond_format(uint32_t c, char *s)      /* "100%", "35%", "1-SHOT", "3/5"; s holds 8 bytes */
{
    uint32_t a, b;
    if (c < COND_1SHOT) {
        fmt_int(s, !c ? 100 : (int32_t)(c - 1u) * 5);
        str_cpy(s + str_len(s), "%", 2);
    } else if (c == COND_1SHOT) {
        str_cpy(s, "1-SHOT", 8);
    } else {
        cond_ab(c, &a, &b);
        s[0] = (char)('0' + a);
        s[1] = '/';
        s[2] = (char)('0' + b);
        s[3] = 0;
    }
}
```

`firmware/src/project.c` — header comment: replace `Format "FDR1": the globals, ...` with
`Format "FDR2": the globals, the selected track, and per track every parameter and its 64 steps (with PROB / RATCH). M1's "FDR1" records (in flash) are converted on load.`
Replace the magic / struct block with:

```c
#define PROJ_MAGIC 0x32524446u                 /* "FDR2": 8 drum tracks, steps with PROB / RATCH (M2) */
#define PROJ_MAGIC_V1 0x31524446u              /* "FDR1": M1 projects, converted on load */
typedef struct {
    int16_t p[P_COUNT];
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_t;
project_t proj_slot[4] __attribute__((section(".noinit")));
```

Replace `proj_fetch` (inside `#if FELUCCA_FLASH`) with:

```c
/* M1's format: the globals before G_GMODE, the parameters before P_SRC, steps of {on, acc} */
typedef struct { uint8_t on, acc; } step_v1_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_GMODE];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_SRC]; step_v1_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v1_t;
static project_v1_t proj_v1;

static void proj_from_v1(project_t *q)            /* proj_v1 -> q: PROB 100 %, 1 hit, SRC STEP, Grids defaults */
{
    uint32_t i, k;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    for (i = 0; i < G_COUNT; i++)
        q->g[i] = i < G_GMODE ? proj_v1.g[i] : GP[i].def;
    q->sel = proj_v1.sel;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++)
            q->t[k].p[i] = i < P_SRC ? proj_v1.t[k].p[i] : TP[i].def;
        for (i = 0; i < NSTEP; i++) {
            q->t[k].step[i].on = proj_v1.t[k].step[i].on;
            q->t[k].step[i].acc = proj_v1.t[k].step[i].acc;
        }
    }
    q->sum = proj_sum(q);
}

static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), q, sizeof *q);
    if (n == (int)sizeof proj_v1) {
        memcpy(&proj_v1, q, sizeof proj_v1);
        if (proj_v1.magic == PROJ_MAGIC_V1 && proj_v1.size == sizeof proj_v1 &&
            proj_v1.sum == proj_hash(&proj_v1, sizeof proj_v1 - 4u)) {
            proj_from_v1(q);
            return;
        }
    }
    if (n != (int)sizeof *q || !proj_ok(q))
        q->magic = 0;
}
```

In `project_load`, replace the step loop with:

```c
        for (i = 0; i < NSTEP; i++) {                   /* garbage cannot index a table or divide by zero */
            const step_t *q = &s->step[i];
            t->step[i].on = q->on ? 1u : 0u;
            t->step[i].acc = q->acc ? 1u : 0u;
            t->step[i].cond = (uint8_t)(q->cond < COND_MAX ? q->cond : COND_MAX);
            t->step[i].rat = (uint8_t)(q->rat < 3u ? q->rat : 3u);
        }
```

(The parameter loop already clamps `P_SRC` through `TP`, and the globals loop clamps the Grids globals.)

`firmware/src/ui.c:track_clear` — clear every field:

```c
static void track_clear(track_t *t) { memset(t->step, 0, sizeof t->step); }
```

- [ ] **Step 4: Run the tests to see them pass**

Run: drum_test, ui_test, boot_test commands.
Expected: `ok` for the three PROB lines, both project lines (roundtrip + Grids), the clamp line, the FDR1 line, and
`boot_test: all passed` (38 boots, now with FDR1 garbage on odd slots). No other `FAIL`.

- [ ] **Step 5: Run the whole drum suite and commit**

Run: `sh tests/run_drum_tests.sh > build/host/drum_suite.txt 2>&1; tail -5 build/host/drum_suite.txt`
Expected: `ALL DRUM HOST TESTS PASSED` (goldens unchanged: no sound changes).

```bash
git add firmware/src/core.h firmware/src/params.c firmware/src/project.c firmware/src/ui.c tests/drum_test.c tests/ui_test.c tests/boot_test.c
git commit -m "m2: step PROB / RATCH fields (zero = default), SRC, Grids globals; FDR2 projects, FDR1 converted on load

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: PROB, conditions and RATCH in the sequencer

**Files:**
- Modify: `firmware/src/core.h` (track_t: rng, rat_*)
- Modify: `firmware/src/seq.c` (`step_plays`, `step_fire`, `rat_due`, `seq_tick`, `seq_start`, `rec_hit`)
- Test: `tests/drum_test.c`

**Interfaces:**
- Consumes: `step_t.cond/.rat`, `COND_*`, `cond_ab`, `cond_store` (Task 1).
- Produces: `static int step_plays(track_t *t, const step_t *s, uint32_t len)` (one decision; draws from
  `t->rng` only for a percentage below 100 %); track fields `uint32_t rng, rat_len; uint8_t rat_n, rat_k, rat_vel;`;
  `seq_start()` seeds `rng = 0x9E3779B9u * (track + 1)`.

- [ ] **Step 1: Write the failing tests**

`tests/drum_test.c`, after `test_len_change_sync`:

```c
/* PROB as a chance: n loops of a 1-step track through step_plays (the decision the sequencer makes) */
static uint32_t chance_hits(uint32_t ti, uint32_t pct, uint32_t n, uint64_t *bits)
{
    track_t *t = &trk[ti];
    step_t s = {1, 0, 0, 0};
    uint32_t k, hits = 0;
    s.cond = (uint8_t)cond_store(pct / 5u);
    seq_start();                                     /* seeds the tracks' random sequences */
    *bits = 0;
    for (k = 0; k < n; k++) {
        int p;
        t->seq_cnt = k;
        p = step_plays(t, &s, 1u);
        hits += (uint32_t)p;
        if (k < 64u && p)
            *bits |= 1ull << k;
    }
    return hits;
}

static void test_prob_chance(void)
{
    uint64_t b0, b1, b2;
    uint32_t h50, h25;
    host_init();
    h50 = chance_hits(0, 50, 4000, &b0);
    h25 = chance_hits(0, 25, 4000, &b2);
    printf("     PROB 50 %%: %u / 4000, 25 %%: %u / 4000\n", h50, h25);
    check("PROB: 50 % and 25 % play about that often (4000 loops, +-5 %)",
          h50 >= 1800u && h50 <= 2200u && h25 >= 800u && h25 <= 1200u);
    check("PROB: 0 % never plays, 100 % always", chance_hits(0, 0, 500, &b2) == 0u && chance_hits(0, 100, 500, &b2) == 500u);
    chance_hits(0, 50, 64, &b1);
    chance_hits(1, 50, 64, &b2);
    check("PROB: the same after every PLAY (per track), different tracks differ", b0 == b1 && b0 != b2);
}

/* conditions: A/B plays on loop A of every B (loop = steps since PLAY / LEN), 1-SHOT on loop 0; no random draw */
static void test_cond_loops(void)
{
    track_t *t = &trk[0];
    uint32_t a, b, n = 0, loop, len = 3, ok = 1, r0;
    host_init();
    seq_start();
    r0 = t->rng;
    for (b = 2; b <= 8u; b++)
        for (a = 1; a <= b; a++, n++) {
            step_t s = {1, 0, 0, 0};
            s.cond = (uint8_t)(22u + n);
            for (loop = 0; loop < 2u * 8u * 8u; loop++) {
                t->seq_cnt = loop * len + 1u;        /* step 1 of each loop */
                ok &= step_plays(t, &s, len) == (loop % b == a - 1u);
            }
        }
    check("PROB: every A/B condition plays exactly on loop A of every B (128 loops)", ok && t->rng == r0);
    {
        step_t s = {1, 0, COND_1SHOT, 0};
        ok = 1;
        for (loop = 0; loop < 16u; loop++) {
            t->seq_cnt = loop * len;
            ok &= step_plays(t, &s, len) == (loop == 0u);
        }
        check("PROB: 1-SHOT plays only on the first loop after PLAY", ok);
    }
    {
        step_t s = {1, 0, 22, 0};                    /* 1/2 */
        t->seq_cnt = 9;                              /* LEN 4: loop 2 (plays); LEN 3: loop 3 (does not) */
        check("PROB: conditions count loops with the LEN of the moment", step_plays(t, &s, 4) && !step_plays(t, &s, 3));
    }
}

/* a 2/2 step on a 1-step track: hits on every other step, on time */
static void test_cond_render(void)
{
    uint32_t at[8], n, p = FS * 60 / 120 / 4;
    host_init();
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = 1;
    trk[0].step[0].cond = 23;                        /* 2/2 */
    play();
    n = hits_at(0, 8 * p - p / 2, at, 8);
    check("PROB 2/2 on a 1-step track: steps 1, 3, 5, 7", n == 4 && at[0] / CTL == CEILB(p) && at[3] / CTL == CEILB(7 * p));
}

/* RATCH R: R hits at k * length / R of the step, the swung (long / short) steps too */
static void test_ratchet_times(void)
{
    uint32_t r, sw, ok = 1, p = FS * 60 / 120 / 4;
    for (sw = 0; sw <= 50u; sw += 50u)
        for (r = 2; r <= 4u; r++) {
            uint32_t at[16], want[16], n, k, m = 0, l0, l1;
            int32_t s = (int32_t)sw * (int32_t)p / 250;
            host_init();
            song.g[G_SWING] = (int16_t)sw;
            l0 = p + (uint32_t)s;
            l1 = p - (uint32_t)s;
            trk[0].p[P_SLEN] = 2;
            trk[0].step[0].on = trk[0].step[1].on = 1;
            trk[0].step[0].rat = trk[0].step[1].rat = (uint8_t)(r - 1u);
            play();
            n = hits_at(0, l0 + l1 - CTL, at, 16);
            for (k = 0; k < r; k++)
                want[m++] = k * l0 / r;
            for (k = 0; k < r; k++)
                want[m++] = l0 + k * l1 / r;
            ok &= n == 2u * r;
            for (k = 0; k < m && k < n; k++)
                ok &= at[k] / CTL == CEILB(want[k]);
            if (n != 2u * r)
                printf("     RATCH %u swing %u: %u hits\n", r, sw, n);
        }
    check("RATCH: 2, 3, 4 hits evenly over each step, swung steps too", ok);
}

/* one decision per roll: a 50 % step with 4 hits plays all 4 or none; every hit at the step's velocity */
static void test_ratchet_one_decision(void)
{
    uint32_t at[300], cnt[64] = {0}, n, i, p = FS * 60 / 120 / 4, ok = 1, none = 0, all = 0, vel = 1;
    host_init();
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = trk[0].step[0].acc = 1;
    trk[0].step[0].rat = 3;
    trk[0].step[0].cond = (uint8_t)cond_store(10);   /* 50 % */
    play();
    n = hits_at(0, 64 * p - CTL, at, 300);
    for (i = 0; i < n && i < 300u; i++) {
        cnt[at[i] / p]++;
    }
    for (i = 0; i < 64u; i++) {
        ok &= cnt[i] == 0u || cnt[i] == 4u;
        none += cnt[i] == 0u;
        all += cnt[i] == 4u;
    }
    for (i = 0; i < NDV; i++)
        if (trk[0].v[i].active)
            vel &= trk[0].v[i].vel == 127u;
    check("RATCH + PROB: one decision per roll (0 or 4 hits a step, both occur), accented hits all at 127",
          ok && none > 10u && all > 10u && vel);
}

/* a tempo change in a roll: hits past the step's new end are dropped, the next steps play their rolls */
static void test_ratchet_tempo_change(void)
{
    uint32_t p = FS * 60 / 120 / 4, a;
    host_init();
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = 1;
    trk[0].step[0].rat = 3;                          /* 4 hits a step */
    play();
    a = dvage;
    render_mix(0, 0, p / 2);                         /* hits at 0 and p/4 */
    song.g[G_BPM] = 240;                             /* the step is now p/2 long: its p/2 hit is dropped */
    render_mix(0, 0, p - 2 * CTL);                   /* two new steps of 4 hits */
    printf("     RATCH 4, BPM 120 -> 240 mid-step: %u hits\n", dvage - a);
    check("RATCH: a tempo change mid-roll drops the late hit, then 4 hits per step (2 + 8)", dvage - a == 10u);
}
```

Extend `test_live_record`: before `play();` add `trk[0].step[2].cond = 7; trk[0].step[2].rat = 2;`, and after the
first check add:

```c
    check("live record: the recorded step plays always, one hit (PROB 100 %, RATCH 1)",
          trk[0].step[2].cond == 0 && trk[0].step[2].rat == 0);
```

In `main`, after `test_len_change_sync();`:

```c
    test_prob_chance();
    test_cond_loops();
    test_cond_render();
    test_ratchet_times();
    test_ratchet_one_decision();
    test_ratchet_tempo_change();
```

- [ ] **Step 2: Run to see them fail**

Run: the drum_test command.
Expected: compile errors: `step_plays` undeclared, no member `rng` in `track_t`.

- [ ] **Step 3: Implement**

`firmware/src/core.h`, track_t, after the `rskip` line:

```c
    uint32_t rng;                /* PROB: the track's random sequence (LCG), seeded at PLAY */
    uint32_t rat_len;            /* RATCH: the playing roll's step length (samples) */
    uint8_t rat_n, rat_k, rat_vel;   /* its hits, the next hit, their velocity; rat_n 0 = no roll */
```

`firmware/src/seq.c`: update the header comment's first sentence to
`Drum sequencer and input. 8 tracks x 64 steps (hit / accent / PROB / RATCH), per-track length, division and swing.`
Add after `step_samples`:

```c
/* PROB of step s coming up on track t (LEN len): 100 % plays; a percentage plays when the track's next random
 * number is below it; A/B when loop mod B = A - 1 and 1-SHOT on loop 0, loop = steps since PLAY / LEN */
static int step_plays(track_t *t, const step_t *s, uint32_t len)
{
    uint32_t c = s->cond, loop = t->seq_cnt / (len ? len : 1u), a, b;
    if (!c)
        return 1;
    if (c < COND_1SHOT) {
        t->rng = t->rng * 1664525u + 1013904223u;
        return ((t->rng >> 16) * 100u >> 16) < (c - 1u) * 5u;
    }
    if (c == COND_1SHOT)
        return loop == 0u;
    cond_ab(c, &a, &b);
    return loop % b == a - 1u;
}

/* the step that came up: decided once (step_plays), then RATCH hits spread over its length len_s, the first
 * now (rat_due plays the others). A track following Grids plays nothing from its steps. */
static void step_fire(track_t *t, uint32_t len_s)
{
    const step_t *s = &t->step[t->seq_idx];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u;
    t->rat_n = 0;
    if (t->p[P_SRC] || !s->on || !step_plays(t, s, len))
        return;
    t->rat_vel = (uint8_t)(s->acc ? 127u : 96u);
    drum_hit(t, t->rat_vel);
    t->rat_n = (uint8_t)(s->rat < 3u ? s->rat + 1u : 4u);
    t->rat_k = 1;
    t->rat_len = len_s;
}

/* the roll's hits whose time has come; one not played before the step ends (the tempo changed) is dropped */
static void rat_due(track_t *t, uint32_t cur_len)
{
    while (t->rat_k < t->rat_n) {
        uint32_t at = t->rat_k * t->rat_len / t->rat_n;
        if (at > t->seq_pos || at >= cur_len)
            break;
        drum_hit(t, t->rat_vel);
        t->rat_k++;
    }
}
```

`seq_start`, in the per-track loop:

```c
        t->rskip = 0;
        t->rat_n = 0;
        t->rng = 0x9E3779B9u * (i + 1u);           /* PROB: the same variations after every PLAY */
```

`seq_tick` loop body becomes:

```c
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_cnt);
        rat_due(t, cur_len);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? 0 : t->seq_pos - cur_len;
        t->seq_cnt++;                               /* the step: steps since PLAY mod LEN, so a LEN change keeps
                                                     * the track on the shared clock (and LEN back = in sync) */
        t->seq_idx = (uint16_t)(t->seq_cnt % (len ? len : 1u));
        if (t->rskip && t->rskip_idx == t->seq_idx) {
            t->rskip = 0;
            t->rat_n = 0;
        } else {
            step_fire(t, step_samples(t, period, t->seq_cnt));
        }
    }
```

`rec_hit`, after `t->step[idx].on = 1;`:

```c
    t->step[idx].cond = 0;                          /* a recorded step always plays, once */
    t->step[idx].rat = 0;
```

- [ ] **Step 4: Run to see them pass**

Run: the drum_test command.
Expected: `ok` for all new PROB / RATCH / live-record lines; the printed rates near 2000 and 1000; `RATCH 4, BPM
120 -> 240 mid-step: 10 hits`; no `FAIL` anywhere (the seq timing / swing / golden tests unchanged).

- [ ] **Step 5: Whole suite, commit**

Run: `sh tests/run_drum_tests.sh > build/host/drum_suite.txt 2>&1; tail -5 build/host/drum_suite.txt`
Expected: `ALL DRUM HOST TESTS PASSED`.

```bash
git add firmware/src/core.h firmware/src/seq.c tests/drum_test.c
git commit -m "m2: sequencer plays PROB (chance, A/B, 1-SHOT) and RATCH rolls; one decision per roll

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: STEP grid — hold a step + KNOB 1 / 2 edits PROB / RATCH; drawing

**Files:**
- Modify: `firmware/src/ui.c` (ui struct: `step_prev`, `held`; `step_press`, `step_hold`)
- Modify: `firmware/src/ui_input.c` (`step_edit`, grid key loop, knob loop)
- Modify: `firmware/src/ui_draw.c` (`steps_hash`, `graph_grid`, `graph_steps`, `graph_signature`)
- Test: `tests/ui_test.c`

**Interfaces:**
- Consumes: `cond_pos`, `cond_store`, `cond_format`, `COND_MAX` (Task 1).
- Produces: ui fields `step_t step_prev[16]` (replaces `step_was`), `uint8_t held` (last pressed grid key);
  `static int step_edit(uint32_t knob, int32_t steps)` in ui_input.c (returns 1 when a held step key took the turn).
  Rule: a tap that turns a step off clears its PROB / RATCH; a hold or the first knob turn restores the step as
  before the press (on).

- [ ] **Step 1: Write the failing test**

`tests/ui_test.c`, after `test_grid_hold_accent`:

```c
/* a step key held + KNOB n (n = 0 PROB, 1 RATCH) turned `steps` detents, slowly, then let go */
static void grid_hold_turn(uint32_t key, uint32_t knob, int32_t steps)
{
    int32_t i;
    keys(1u << key);
    ui_frame();
    for (i = 0; i < (steps < 0 ? -steps : steps); i++) {
        turn(EN_K1 + knob, steps < 0 ? -1 : 1);
        host_ticks += 100u * 1000u * FM1_TICKS_PER_US;   /* 0.1 s apart: no acceleration */
        ui_frame();
    }
}

static void test_grid_step_edit(void)
{
    step_t *s = &trk[0].step[4];
    ui_host_init();
    open_step_page();
    grid_hold(WHITE[4], 600);                        /* on, accented */
    grid_hold_turn(WHITE[4], 0, -5);                 /* 100 % -> 75 % */
    check("hold + KNOB 1: PROB of the held step (100 % -> 75 %); its accent stays, no accent flip from the hold",
          s->on && s->acc && s->cond == cond_store(15) && ui.bank == 0);
    snap_page("seq/20_step_held_prob");
    keys(0);
    ui_frame();
    grid_hold_turn(WHITE[4], 1, 2);
    keys(0);
    ui_frame();
    check("hold + KNOB 2: RATCH of the held step (1 -> 3 hits)", s->rat == 2 && s->on && s->acc);
    grid_hold_turn(WHITE[6], 0, 3);                  /* an off step: on, 100 % -> 1-SHOT -> 1/2 -> 2/2 */
    keys(0);
    ui_frame();
    check("hold + KNOB 1 on an off step turns it on (no accent): PROB past 100 % gives 1-SHOT, 1/2, 2/2",
          trk[0].step[6].on && !trk[0].step[6].acc && trk[0].step[6].cond == 23);
    grid_hold(WHITE[6], 0);
    check("a tap turning a step off resets its PROB / RATCH", !trk[0].step[6].on && trk[0].step[6].cond == 0);
    grid_hold(WHITE[4], 600);                        /* accent off by hold: PROB / RATCH kept */
    check("a hold flips the accent and keeps PROB / RATCH", s->on && !s->acc && s->cond == cond_store(15) && s->rat == 2);
    trk[0].p[P_SLEN] = 24;                           /* two banks; on bank 2 keys 9..16 are outside LEN */
    ui_frame();
    press(B_OCTUP);
    ui_frame();
    release_all();
    grid_hold_turn(WHITE[12], 0, -2);                /* step 29: outside LEN */
    keys(0);
    ui_frame();
    check("hold + KNOB 1 on a key outside LEN edits nothing and does not turn the bank",
          ui.bank == 1 && trk[0].step[28].cond == 0 && !trk[0].step[28].on);
    press(B_OCTDN);
    ui_frame();
    release_all();
    trk[0].p[P_SLEN] = 16;
    trk[0].step[0].on = 1;
    trk[0].step[0].cond = 33;                        /* 3/5 */
    trk[0].step[8].on = trk[0].step[8].acc = 1;
    trk[0].step[8].rat = 3;
    trk[0].step[12].on = 1;
    trk[0].step[12].cond = (uint8_t)cond_store(10);
    trk[0].step[12].rat = 1;
    ui.force = 1;
    snap_page("seq/21_step_prob_ratch");
    open_step_page();                                /* PATTERN */
    snap_page("seq/22_pattern_prob_ratch");
}
```

Call it in `main` after `test_grid_hold_accent();`.

- [ ] **Step 2: Run to see it fail**

Run: the ui_test command.
Expected: the PROB / RATCH checks FAIL (KNOB 1 turns the bank instead: `ui.bank` stays 0 only because LEN 16 has
one bank, but `cond` / `rat` stay 0).

- [ ] **Step 3: Implement**

`firmware/src/ui.c`, ui struct: replace `uint8_t step_was[16];` with

```c
    step_t step_prev[16];        /* that step before the press (a hold or a knob turn restores it) */
    uint8_t held;                /* the grid key pressed last (valid while step_t0[held] is set) */
```

`step_press` / `step_hold`:

```c
/* key k of the grid pressed: step bank * 16 + k turns on, or off (then plain again: no accent, PROB 100 %,
 * 1 hit), inside LEN only; the step before the press is kept for a hold / a knob turn */
static void step_press(uint32_t k)
{
    uint32_t si = ui.bank * 16u + k;
    step_t *s;
    ui.step_si[k] = 0xFFFFu;
    if (k >= 16u || si >= (uint32_t)TSEL->p[P_SLEN])
        return;
    s = &TSEL->step[si];
    ui.step_si[k] = (uint16_t)si;
    ui.step_prev[k] = *s;
    if (s->on) {
        memset(s, 0, sizeof *s);
    } else {
        s->on = 1;
        s->acc = 0;
    }
}

/* key k held STEP_HOLD: the step as before the press, on, its accent flipped */
static void step_hold(uint32_t k)
{
    step_t *s;
    if (k >= 16u || ui.step_si[k] == 0xFFFFu)
        return;
    s = &TSEL->step[ui.step_si[k]];
    *s = ui.step_prev[k];
    s->on = 1;
    s->acc = (uint8_t)!ui.step_prev[k].acc;
}
```

`firmware/src/ui_input.c`, after `accel`:

```c
/* a step key held + KNOB 1: its PROB, KNOB 2: its RATCH. The first turn of a hold cancels the hold's accent
 * flip: the step is as before the press, and on. A held key outside LEN takes the turn and does nothing. */
static int step_edit(uint32_t knob, int32_t steps)
{
    uint32_t k = ui.held;
    step_t *s;
    if (knob > 1u || k >= 16u || !ui.step_t0[k])
        return 0;
    if (ui.step_si[k] == 0xFFFFu)
        return 1;
    s = &TSEL->step[ui.step_si[k]];
    if (!(ui.step_t0[k] & 4u)) {
        *s = ui.step_prev[k];
        s->on = 1;
        ui.step_t0[k] |= 6u;                          /* edited: no hold accent from now on */
    }
    if (knob == 0u)
        s->cond = (uint8_t)cond_store((uint32_t)clamp((int32_t)cond_pos(s->cond) + accel(EN_K1, steps, COND_MAX), 0,
                                                      (int32_t)COND_MAX));
    else
        s->rat = (uint8_t)clamp((int32_t)s->rat + (steps > 0 ? 1 : -1), 0, 3);
    return 1;
}
```

Grid key loop in `ui_input` (the press branch sets `held`; the time mask leaves bit 2 out):

```c
            if ((notes >> STEP_KEY[k]) & 1u) {
                step_press(k);
                *t0 = (now | 1u) & ~6u;
                ui.held = (uint8_t)k;
            } else if (!((fm1_in.notes >> STEP_KEY[k]) & 1u)) {
                *t0 = 0;
            } else if (*t0 && !(*t0 & 2u) && now - (*t0 & ~7u) > STEP_HOLD_MS * 1000u * FM1_TICKS_PER_US) {
```

Knob loop: first statement after `if ((s = panel_enc(EN_K1 + k)) == 0) continue;`:

```c
        if (grid_mode() && step_edit(k, s))
            continue;
```

`firmware/src/ui_draw.c` — `steps_hash`:

```c
        h = (h ^ (t->step[i].on + t->step[i].acc * 2u + t->step[i].cond * 4u + t->step[i].rat * 256u)) * 16777619u;
```

A drawing helper before `graph_steps`:

```c
/* a step's bar: hatched (every third row dark) when its PROB is not 100 %; RATCH > 1: that many ticks above */
static void step_bar(int32_t x, int32_t y, int32_t w, int32_t h, const step_t *st, uint16_t col, int32_t tick)
{
    int32_t r;
    if (st->cond) {
        for (r = 0; r < h; r += 3)
            cv_rect(x, y + r, w, r + 2 <= h ? 2 : 1, col);
    } else {
        cv_rect(x, y, w, h, col);
    }
    for (r = 0; st->rat && r <= st->rat; r++)
        cv_rect(x + r * tick, y - 4, tick > 2 ? tick - 1 : 1, 2, col);
}
```

In `graph_steps` replace `cv_rect(x, st->acc ? y : y + 4, 2, st->acc ? 14 : 10, st->acc ? C_WHITE : c);` with
`step_bar(x, st->acc ? y : y + 4, 2, st->acc ? 14 : 10, st, st->acc ? C_WHITE : c, 3);` (ticks 3 px apart, 2 px
wide). In `graph_grid` replace the on-branch `cv_rect(...)` with
`step_bar(x, st->acc ? y : y + 10, 11, st->acc ? 40 : 30, st, st->acc ? C_WHITE : c, 3);` and replace the hint line
with:

```c
    if (ui.held < 16u && ui.step_t0[ui.held] && ui.step_si[ui.held] != 0xFFFFu) {   /* a step held: its settings */
        const step_t *hs = &t->step[ui.step_si[ui.held]];
        char h[32], cs[8];
        str_cpy(h, "STEP ", sizeof h);
        fmt_int(h + str_len(h), (int32_t)ui.step_si[ui.held] + 1);
        str_cpy(h + str_len(h), "  ", sizeof h - str_len(h));
        cond_format(hs->cond, cs);
        str_cpy(h + str_len(h), cs, sizeof h - str_len(h));
        str_cpy(h + str_len(h), "  RATCH ", sizeof h - str_len(h));
        fmt_int(h + str_len(h), (int32_t)hs->rat + 1);
        cv_text(4, 84, &FONT_S, h, C_WHITE);
    } else {
        cv_text(4, 84, &FONT_S, "TAP: ON/OFF  HOLD: ACCENT", C_DIM);
    }
```

`graph_signature`, in the `GR_STEPS || GR_GRID` branch add:

```c
        h ^= (ui.held < 16u && ui.step_t0[ui.held] ? ui.held + 1u : 0u) * 977u;
```

- [ ] **Step 4: Run to see it pass**

Run: the ui_test command.
Expected: all `test_grid_step_edit` lines `ok`, the existing `test_grid_hold_accent` lines still `ok`;
`build/ui_shots/seq/20_step_held_prob.ppm`, `21_step_prob_ratch.ppm`, `22_pattern_prob_ratch.ppm` written.

- [ ] **Step 5: Whole suite, commit**

Run: `sh tests/run_drum_tests.sh > build/host/drum_suite.txt 2>&1; tail -5 build/host/drum_suite.txt`
Expected: `ALL DRUM HOST TESTS PASSED`; the PNGs in `build/ui_shots/seq/`.

```bash
git add firmware/src/ui.c firmware/src/ui_input.c firmware/src/ui_draw.c tests/ui_test.c
git commit -m "m2: STEP grid: hold a step + KNOB 1 / 2 sets PROB / RATCH; hatched and ticked steps, hint line

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Grids engine port, tables and the reference test

**Files:**
- Create: `tools/gen_grids_tables.py`, `firmware/src/grids_tables.h` (generated, committed), `firmware/src/grids.c`
- Create: `tests/grids_stub/avr/pgmspace.h`, `tests/grids_stub/avr/eeprom.h`, `tests/grids_stub/grids/hardware_config.h`,
  `tests/grids_stub/avrlib/op.h`, `tests/grids_ref.cc`, `tests/grids_fidelity.c`
- Modify: `tests/fetch_ref.sh`, `tests/run_drum_tests.sh`, `firmware/src/felucca.c`, `tests/drum_host.h`,
  `LICENSING.md`, `README.md`
- Test: `tests/drum_test.c` (`test_grids_engine`, `test_grids_mode_switch`), `tests/grids_fidelity.c`

**Interfaces:**
- Consumes: `G_GMODE .. G_GLEN3` (Task 1).
- Produces (grids.c): `void grids_start(void)` (seed 0x21, step 0); `uint32_t grids_step(void)` (one 1/32 step:
  bits 0..2 triggers kick/snare/hats, bits 3..5 accents); `uint32_t grids_preview(uint32_t ch, uint32_t i)`
  (bit 0 hit, bit 1 accent; MAP: step i of 32 without chaos; EUCLID: position i of LEN); `uint32_t grids_len(uint32_t ch)`
  (32 or LEN); `uint32_t grids_pos(uint32_t ch)` (last evaluated step / Euclidean position);
  `struct grids` fields `step`, `last`, `estep[3]`, `elast[3]` visible to the UI.

- [ ] **Step 1: Fetch the reference and generate the tables**

`tests/fetch_ref.sh`: add the avril commit and the Grids files (the stamp changes, so the next run refetches):

```sh
AVR=276b2887e4110ca913294fcbb313163dfb28a448
...
[ -f "$D/.ok-$EURO-$STM-$AVR" ] && exit 0
...
git -C "$D" sparse-checkout set --no-cone '/plaits/dsp/' '/plaits/resources.h' '/plaits/resources.cc' '/grids/' || exit 1
...
git clone -q --filter=blob:none --no-checkout https://github.com/pichenettes/avril.git "$D/avrlib" || exit 1
git -C "$D/avrlib" checkout -q "$AVR" || exit 1
touch "$D/.ok-$EURO-$STM-$AVR"
```

and the header comment's first line: `Fetch the references: Mutable Instruments' drum code (MIT) for M1-C and Grids
with avrlib (GPL-3.0-or-later) for M2, for the host fidelity tests only: ...`.

`tools/gen_grids_tables.py`:

```python
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""firmware/src/grids_tables.h from Mutable Instruments Grids' grids/resources.cc (GPL-3.0-or-later): the 25 drum
map nodes and the Euclidean pattern table. The header is committed; tests/run_drum_tests.sh regenerates it from the
fetched reference and fails when they differ.
    tools/gen_grids_tables.py RESOURCES_CC OUT_H"""
import re
import sys

HEADER = """/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2012 Emilie Gillet (Mutable Instruments Grids, grids/resources.cc, eurorack 08460a6)
 * Generated by tools/gen_grids_tables.py: do not edit. */
/* The drum map's 25 nodes (3 instruments x 32 steps: kick, snare, hats) and the Euclidean patterns
 * (index (length - 1) * 32 + density, bit n = step n). */"""


def arrays(text):
    out = {}
    for m in re.finditer(r"const prog_uint(?:8|32)_t (\w+)\[\] PROGMEM = \{(.*?)\};", text, re.S):
        out[m.group(1)] = [int(v) for v in re.findall(r"\d+", m.group(2))]
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    a = arrays(open(src).read())
    nodes = [a[f"node_{i}"] for i in range(25)]
    eu = a["lut_res_euclidean"]
    assert all(len(n) == 96 for n in nodes) and len(eu) == 1024, "unexpected table sizes"
    out = [HEADER, "static const uint8_t GRIDS_NODE[25][96] = {"]
    for i, n in enumerate(nodes):
        out.append(f"    {{   /* node_{i} */")
        for r in range(0, 96, 16):
            out.append("        " + ", ".join(f"{v:3d}" for v in n[r:r + 16]) + ",")
        out.append("    },")
    out += ["};", "static const uint32_t GRIDS_EUCLID[1024] = {"]
    for r in range(0, 1024, 8):
        out.append("    " + ", ".join(f"0x{v:08X}u" for v in eu[r:r + 8]) + ",")
    out.append("};")
    open(dst, "w").write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
```

Run: `sh tests/fetch_ref.sh && python3 tools/gen_grids_tables.py build/drum_ref/eurorack/grids/resources.cc firmware/src/grids_tables.h && head -12 firmware/src/grids_tables.h && grep -c "node_" firmware/src/grids_tables.h`
Expected: exit 0; the header lines, `static const uint8_t GRIDS_NODE[25][96] = {`; `25`.

- [ ] **Step 2: Write the failing engine tests (offline, in drum_test)**

`tests/drum_test.c`, after the RATCH tests:

```c
/* Grids engine (grids.c), structure only: the bit-exact check against the original is tests/grids_fidelity.c */
static void test_grids_engine(void)
{
    uint32_t k, a, bits, ok = 1, hits = 0, acc = 0, seq1[64], same = 1;
    host_init();
    song.g[G_GFILL1] = song.g[G_GFILL2] = song.g[G_GFILL3] = 0;
    song.g[G_GCHAOS] = 127;
    grids_start();
    for (k = 0; k < 64u; k++)
        ok &= (grids_step() & 7u) == 0u;
    check("Grids MAP: fill 0 is silent, chaos or not", ok);
    song.g[G_GMODE] = 1;                             /* EUCLID, kick LEN 4, full fill: 1/16s, accent every 4th */
    song.g[G_GLEN1] = 4;
    song.g[G_GFILL1] = 127;
    grids_start();
    for (k = 0; k < 32u; k++) {
        bits = grids_step();
        hits += bits & 1u;
        acc += (bits >> 3) & 1u;
        ok &= (k & 1u) ? !(bits & 1u) : 1;           /* nothing on the odd 1/32s */
    }
    check("Grids EUCLID: plays on 1/16s only; LEN 4 full: 16 hits, 4 accents a bar", ok && hits == 16u && acc == 4u);
    song.g[G_GMODE] = 0;                             /* MAP, chaos: the same after every start */
    song.g[G_GFILL1] = song.g[G_GFILL2] = song.g[G_GFILL3] = 90;
    grids_start();
    for (k = 0; k < 64u; k++)
        seq1[k] = grids_step();
    grids_start();
    for (k = 0; k < 64u; k++)
        same &= grids_step() == seq1[k];
    check("Grids: a start reseeds the chaos (a session repeats) and the step wraps at 32", same && grids.step == 0u);
    ok = 1;
    song.g[G_GCHAOS] = 0;                            /* without chaos the preview is what plays */
    grids_start();
    for (k = 0; k < 32u; k++) {
        bits = grids_step();
        for (a = 0; a < 3u; a++)
            ok &= grids_preview(a, k) == (((bits >> a) & 1u) | ((bits >> (a + 3u)) & 1u) << 1);
    }
    check("Grids MAP: the preview (no chaos) is exactly what the engine plays; 32 steps", ok && grids_len(0) == 32u);
}

/* MAP <-> EUCLID and LEN changes while running: positions stay inside the length */
static void test_grids_mode_switch(void)
{
    uint32_t k, ch, ok = 1;
    host_init();
    grids_start();
    for (k = 0; k < 300u; k++)
        grids_step();                                /* MAP: the Euclidean counters run on (uint8, as the original) */
    song.g[G_GMODE] = 1;
    song.g[G_GLEN1] = 32;
    song.g[G_GLEN2] = 1;
    song.g[G_GLEN3] = 7;
    for (k = 0; k < 70u; k++) {
        grids_step();
        if (k == 30u)
            song.g[G_GLEN1] = 3;
        for (ch = 0; ch < 3u; ch++)
            ok &= grids_pos(ch) < grids_len(ch) && grids_preview(ch, grids_pos(ch)) < 4u;
    }
    check("Grids: MAP -> EUCLID and a LEN change while running keep every position inside its length", ok);
}
```

Call both in `main` after `test_ratchet_tempo_change();`.

Run: the drum_test command. Expected: compile errors (`grids_start` undeclared).

- [ ] **Step 3: Implement the port**

`firmware/src/grids.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2012 Emilie Gillet (Mutable Instruments Grids: grids/pattern_generator.cc, avrlib/random.h)
 * C port for the FM-1 drum firmware: 2026 DEADACTIVE (GPL-3.0-or-later, built into this GPL-3.0-only firmware) */
/* Grids: the topographic drum pattern generator (map mode) and its Euclidean mode, one engine for the song.
 * grids_step() is one Grids step (a 1/32 note: 24 ppqn, 3 pulses a step) exactly as the original evaluates it:
 * three clock ticks, each advancing the random generator, the pattern evaluated on the first. The settings are
 * read from song.g (G_GMODE ..) at every step. Output: bits 0..2 triggers (kick, snare, hats), 3..5 accents. */
#include "grids_tables.h"

static const uint8_t GRIDS_MAP[5][5] = {         /* drum_map[x][y]: node numbers */
    {10, 8, 0, 9, 11}, {15, 7, 13, 12, 6}, {18, 14, 4, 5, 3}, {23, 16, 21, 1, 2}, {24, 19, 17, 20, 22},
};

static struct {
    uint16_t rng;                /* avrlib Random: 16-bit Galois LFSR, seed 0x21 */
    uint8_t step;                /* the step evaluated next, 0..31 */
    uint8_t last;                /* the step evaluated last (the playhead) */
    uint8_t estep[3];            /* Euclidean positions (count 1/16s, wrap at the length when evaluated) */
    uint8_t elast[3];            /* the positions evaluated last */
    uint8_t perturb[3];          /* chaos per channel, drawn at step 0 */
} grids;

static uint32_t grids_byte(int32_t v) { return (uint32_t)clamp(v, 0, 127) * 2u + ((uint32_t)clamp(v, 0, 127) >> 6); }
static uint32_t grids_mix(uint32_t a, uint32_t b, uint32_t bal) { return (a * (255u - bal) + b * bal) >> 8; }   /* U8Mix */
static void grids_rng(void) { grids.rng = (uint16_t)((grids.rng >> 1) ^ (-(uint32_t)(grids.rng & 1u) & 0xB400u)); }
static uint32_t grids_elen(uint32_t ch) { return (uint32_t)clamp(song.g[G_GLEN1 + ch], 1, 32); }
static uint32_t grids_len(uint32_t ch) { return song.g[G_GMODE] ? grids_elen(ch) : 32u; }
static uint32_t grids_pos(uint32_t ch) { return song.g[G_GMODE] ? grids.elast[ch % 3u] : grids.last; }

static void grids_start(void)
{
    memset(&grids, 0, sizeof grids);
    grids.rng = 0x21;
}

/* ReadDrumMap: the level (0..255) of channel ch at step, between the four nodes around x, y (bytes) */
static uint32_t grids_level(uint32_t step, uint32_t ch, uint32_t x, uint32_t y)
{
    uint32_t i = x >> 6, j = y >> 6, off = ch * 32u + (step & 31u), xb = (x << 2) & 255u, yb = (y << 2) & 255u;
    const uint8_t *a = GRIDS_NODE[GRIDS_MAP[i][j]], *b = GRIDS_NODE[GRIDS_MAP[i + 1u][j]];
    const uint8_t *c = GRIDS_NODE[GRIDS_MAP[i][j + 1u]], *d = GRIDS_NODE[GRIDS_MAP[i + 1u][j + 1u]];
    return grids_mix(grids_mix(a[off], b[off], xb), grids_mix(c[off], d[off], xb), yb);
}

/* MAP: hit when the level (plus the step's chaos) is above ~fill; accent above 192 */
static uint32_t grids_map_bits(uint32_t step, int chaos)
{
    uint32_t ch, bits = 0, x = grids_byte(song.g[G_GX]), y = grids_byte(song.g[G_GY]);
    for (ch = 0; ch < 3u; ch++) {
        uint32_t level = grids_level(step, ch, x, y), p = chaos ? grids.perturb[ch] : 0u;
        level = level < 255u - p ? level + p : 255u;
        if (level > (~grids_byte(song.g[G_GFILL1 + ch]) & 255u)) {
            bits |= 1u << ch;
            if (level > 192u)
                bits |= 8u << ch;
        }
    }
    return bits;
}

static uint32_t grids_euclid_hit(uint32_t ch, uint32_t pos)   /* the table's bit for position pos */
{
    uint32_t len = grids_elen(ch), dens = grids_byte(song.g[G_GFILL1 + ch]) >> 3;
    return pos < 32u && (GRIDS_EUCLID[(len - 1u) * 32u + dens] >> pos) & 1u;
}

/* EUCLID (on even steps): each channel's position wraps at its length; accent (reset bit) at position 0 */
static uint32_t grids_euclid_bits(void)
{
    uint32_t ch, bits = 0;
    for (ch = 0; ch < 3u; ch++) {
        uint32_t len = grids_elen(ch);
        while (grids.estep[ch] >= len)
            grids.estep[ch] = (uint8_t)(grids.estep[ch] - len);
        if (grids_euclid_hit(ch, grids.estep[ch]))
            bits |= 1u << ch;
        if (!grids.estep[ch])
            bits |= 8u << ch;
        grids.elast[ch] = grids.estep[ch];
    }
    return bits;
}

static uint32_t grids_step(void)
{
    uint32_t bits, ch;
    grids_rng();                                     /* tick 1: Evaluate() */
    if (song.g[G_GMODE]) {
        bits = (grids.step & 1u) ? 0u : grids_euclid_bits();
    } else {
        if (!grids.step)                             /* a new bar: chaos per channel (Random::GetByte) */
            for (ch = 0; ch < 3u; ch++) {
                grids_rng();
                grids.perturb[ch] = (uint8_t)(((uint32_t)(grids.rng >> 8) * (grids_byte(song.g[G_GCHAOS]) >> 2)) >> 8);
            }
        bits = grids_map_bits(grids.step, 1);
    }
    grids.last = grids.step;
    grids_rng();                                     /* ticks 2 and 3 */
    grids_rng();
    if (!(grids.step & 1u))
        for (ch = 0; ch < 3u; ch++)
            grids.estep[ch]++;
    grids.step = (uint8_t)((grids.step + 1u) & 31u);
    return bits;
}

/* the pattern as drawn (no chaos): bit 0 hit, bit 1 accent. MAP: step i of 32; EUCLID: position i of LEN */
static uint32_t grids_preview(uint32_t ch, uint32_t i)
{
    ch %= 3u;
    if (song.g[G_GMODE]) {
        uint32_t hit = i < grids_elen(ch) && grids_euclid_hit(ch, i);
        return hit | (hit && !i) << 1;
    }
    {
        uint32_t level = grids_level(i, ch, grids_byte(song.g[G_GX]), grids_byte(song.g[G_GY]));
        uint32_t hit = level > (~grids_byte(song.g[G_GFILL1 + ch]) & 255u);
        return hit | (hit && level > 192u) << 1;
    }
}
```

Include it: `firmware/src/felucca.c`, before `#include "seq.c"`: `#include "grids.c"           /* Grids pattern engine (M2) */`;
`tests/drum_host.h`, before `#include "../firmware/src/seq.c"`: `#include "../firmware/src/grids.c"`, and in
`host_reset_fx` add `memset(&grids, 0, sizeof grids);`.

Run: the drum_test command. Expected: `test_grids_engine` / `test_grids_mode_switch` lines `ok`. If the EUCLID
line fails on the counts, print `GRIDS_EUCLID[3 * 32 + 31]` and stop: the counts were derived from the table's
meaning (full density at length 4 = every position), and the fidelity test in step 4 is the authority.

- [ ] **Step 4: The reference test**

Stubs (`tests/grids_stub/`), each starting with `/* SPDX-License-Identifier: GPL-3.0-only\n * Drum machine fork: 2026 DEADACTIVE */`
and `/* host stand-in for the AVR header the original includes (tests/grids_ref.cc only) */`:

`avr/pgmspace.h`:

```c
#pragma once
#include <stdint.h>
#include <string.h>
#define PROGMEM
typedef char prog_char;
typedef uint8_t prog_uint8_t;
typedef uint16_t prog_uint16_t;
typedef uint32_t prog_uint32_t;
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#define strncpy_P strncpy
#define memcpy_P memcpy
```

`avr/eeprom.h`:

```c
#pragma once
#include <stdint.h>
static inline uint8_t eeprom_read_byte(const uint8_t *a) { (void)a; return 0x08 | 0x40; }   /* no swing, drums */
static inline void eeprom_write_byte(uint8_t *a, uint8_t v) { (void)a; (void)v; }
```

`grids/hardware_config.h`:

```c
#pragma once
#include "avrlib/base.h"
namespace grids { enum LedBits { LED_CLOCK = 1, LED_BD = 8, LED_SD = 4, LED_HH = 2, LED_ALL = 15 }; }
```

`avrlib/op.h` (the original forces AVR assembly; these are its C definitions):

```c
#pragma once
#include <stdint.h>
namespace avrlib {
static inline uint8_t U8Mix(uint8_t a, uint8_t b, uint8_t balance) { return (uint16_t)(a * (255 - balance) + b * balance) >> 8; }
static inline uint8_t U8U8MulShift8(uint8_t a, uint8_t b) { return (uint16_t)(a * b) >> 8; }
static inline uint16_t U8U8Mul(uint8_t a, uint8_t b) { return (uint16_t)(a * b); }
}
```

`tests/grids_ref.cc` (the original, driven as the firmware drives ours; writes the expected bitstreams):

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// M2 Grids fidelity: Mutable Instruments' original PatternGenerator (fetched by tests/fetch_ref.sh, AVR headers
// stubbed in tests/grids_stub) over a grid of settings; per case the state (bits 0..5) after the first clock tick
// of 96 steps, three ticks a step, seed 0x21. tests/grids_fidelity.c runs grids.c the same way and compares.
//   grids_ref OUT.bin
#include <cstdio>
#include "grids/pattern_generator.h"
#include "avrlib/random.h"
#include "tests/grids_cases.h"
using namespace grids;

static uint8_t byte_of(int v) { return (uint8_t)(v * 2 + (v >> 6)); }    // knob 0..127 -> 0..255

int main(int argc, char **argv) {
  FILE *f = fopen(argc > 1 ? argv[1] : "grids_ref.bin", "wb");
  grids_case_t c;
  if (!f) return 1;
  PatternGenerator::Init();
  PatternGenerator::set_swing(0);
  PatternGenerator::set_output_clock(0);
  for (int i = 0; grids_case(i, &c); i++) {
    PatternGeneratorSettings *s = PatternGenerator::mutable_settings();
    PatternGenerator::set_output_mode(c.mode ? OUTPUT_MODE_EUCLIDEAN : OUTPUT_MODE_DRUMS);
    if (c.mode) {
      for (int k = 0; k < 3; k++) s->options.euclidean_length[k] = (uint8_t)((c.len[k] - 1) << 3);
    } else {
      s->options.drums.x = byte_of(c.x);
      s->options.drums.y = byte_of(c.y);
      s->options.drums.randomness = byte_of(c.chaos);
    }
    for (int k = 0; k < 3; k++) s->density[k] = byte_of(c.fill[k]);
    avrlib::Random::Seed(0x21);
    PatternGenerator::Reset();
    for (int step = 0; step < GRIDS_STEPS; step++) {
      PatternGenerator::TickClock(1);
      fputc(PatternGenerator::state() & 0x3f, f);
      PatternGenerator::TickClock(1);
      PatternGenerator::TickClock(1);
    }
  }
  fclose(f);
  return 0;
}
```

`tests/grids_cases.h` (shared by both sides; C and C++):

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The Grids fidelity cases (tests/grids_ref.cc, tests/grids_fidelity.c): MAP over X, Y, fills, chaos; EUCLID
 * over lengths and fills. Knob values (0..127, LEN 1..32). */
#define GRIDS_STEPS 96
typedef struct { int mode, x, y, chaos, fill[3], len[3]; } grids_case_t;
static const int GC_XY[5] = {0, 21, 64, 96, 127};
static const int GC_FILL[4][3] = {{0, 0, 0}, {64, 64, 64}, {127, 100, 30}, {127, 127, 127}};
static const int GC_CHAOS[3] = {0, 64, 127};
static const int GC_LEN[4][3] = {{1, 1, 1}, {16, 12, 8}, {32, 5, 7}, {13, 32, 3}};
static const int GC_EFILL[4][3] = {{0, 0, 0}, {64, 64, 64}, {127, 127, 127}, {10, 90, 50}};
#define GC_MAP (5 * 5 * 4 * 3)
#define GC_ALL (GC_MAP + 4 * 4)
static int grids_case(int i, grids_case_t *c)    /* case i (0 .. GC_ALL - 1); 0 past the end */
{
    int k;
    if (i < 0 || i >= GC_ALL)
        return 0;
    if (i < GC_MAP) {
        c->mode = 0;
        c->x = GC_XY[i % 5];
        c->y = GC_XY[i / 5 % 5];
        for (k = 0; k < 3; k++) {
            c->fill[k] = GC_FILL[i / 25 % 4][k];
            c->len[k] = 1;
        }
        c->chaos = GC_CHAOS[i / 100];
    } else {
        i -= GC_MAP;
        c->mode = 1;
        c->x = c->y = c->chaos = 0;
        for (k = 0; k < 3; k++) {
            c->len[k] = GC_LEN[i % 4][k];
            c->fill[k] = GC_EFILL[i / 4][k];
        }
    }
    return 1;
}
```

`tests/grids_fidelity.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M2 Grids fidelity: grids.c against the original's bitstreams (tests/grids_ref.cc): every case, every step,
 * triggers and accents identical.   grids_fidelity REF.bin */
#include "drum_host.h"
#include "grids_cases.h"

int main(int argc, char **argv)
{
    static uint8_t ref[GC_ALL * GRIDS_STEPS];
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
    grids_case_t c;
    int i, k, bad = 0, first = -1;
    if (!f || fread(ref, 1, sizeof ref, f) != sizeof ref) {
        printf("FAIL  grids fidelity: cannot read %s\n", argc > 1 ? argv[1] : "(none)");
        return 1;
    }
    fclose(f);
    host_init();
    for (i = 0; grids_case(i, &c); i++) {
        song.g[G_GMODE] = (int16_t)c.mode;
        song.g[G_GX] = (int16_t)c.x;
        song.g[G_GY] = (int16_t)c.y;
        song.g[G_GCHAOS] = (int16_t)c.chaos;
        for (k = 0; k < 3; k++) {
            song.g[G_GFILL1 + k] = (int16_t)c.fill[k];
            song.g[G_GLEN1 + k] = (int16_t)c.len[k];
        }
        grids_start();
        for (k = 0; k < GRIDS_STEPS; k++)
            if (grids_step() != ref[i * GRIDS_STEPS + k]) {
                bad++;
                if (first < 0)
                    first = i * GRIDS_STEPS + k;
            }
    }
    if (bad)
        printf("FAIL  grids fidelity: %d of %d steps differ (first: case %d step %d)\n", bad, GC_ALL * GRIDS_STEPS,
               first / GRIDS_STEPS, first % GRIDS_STEPS);
    else
        printf("ok    grids fidelity: %d cases x %d steps identical to the original (MAP + EUCLID)\n", GC_ALL, GRIDS_STEPS);
    return bad ? 1 : 0;
}
```

`tests/run_drum_tests.sh`, inside the `if sh tests/fetch_ref.sh` branch, after the `drum_fidelity` run:

```sh
    python3 tools/gen_grids_tables.py "$REF/eurorack/grids/resources.cc" "$OUT/grids_tables.h"
    cmp -s "$OUT/grids_tables.h" firmware/src/grids_tables.h || { echo "FAIL  grids_tables.h differs from the reference (tools/gen_grids_tables.py)"; exit 1; }
    E="$REF/eurorack"
    c++ -std=c++11 -O1 -w -Itests/grids_stub -I"$E" -I. -o "$REF/grids_ref" tests/grids_ref.cc "$E/grids/pattern_generator.cc" \
        "$E/grids/resources.cc" "$E/avrlib/random.cc"
    "$REF/grids_ref" "$REF/grids_ref.bin"
    cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -Itests \
        -o "$OUT/grids_fidelity" tests/grids_fidelity.c -lm
    "$OUT/grids_fidelity" "$REF/grids_ref.bin"
```

(`-I.` makes `#include "tests/grids_cases.h"` resolve from the repo root; `-Itests/grids_stub` comes before `-I"$E"`
so the stubs replace `avrlib/op.h` and `grids/hardware_config.h`. `$E/avrlib/random.cc` defines `rng_state_ = 0x21`.)

Run: `sh tests/run_drum_tests.sh > build/host/drum_suite.txt 2>&1; grep -E "grids|FAIL|PASSED" build/host/drum_suite.txt`
Expected: `ok    grids fidelity: 316 cases x 96 steps identical to the original (MAP + EUCLID)` and
`ALL DRUM HOST TESTS PASSED`. If cases differ: debug the port against the original line by line
(systematic-debugging); do not change the cases or the comparison.

- [ ] **Step 5: Credits, commit**

`LICENSING.md`, third-party table, after the klattsch row:

```markdown
| Grids by Emilie Gillet / Mutable Instruments (<https://github.com/pichenettes/eurorack>): the GRIDS pattern engine is a C port of `grids/pattern_generator.cc` with its pattern tables (and avrlib's random generator) | GPL-3.0-or-later | `firmware/src/grids.c`, `firmware/src/grids_tables.h` |
```

`README.md` credits, after the VOICE engine line:

```markdown
- GRIDS pattern engine: C port of [Grids](https://github.com/pichenettes/eurorack/tree/master/grids) by Emilie Gillet, Mutable Instruments (GPL-3.0-or-later)
```

```bash
git add tools/gen_grids_tables.py firmware/src/grids_tables.h firmware/src/grids.c firmware/src/felucca.c tests/drum_host.h \
        tests/grids_stub tests/grids_ref.cc tests/grids_cases.h tests/grids_fidelity.c tests/fetch_ref.sh \
        tests/run_drum_tests.sh tests/drum_test.c LICENSING.md README.md
git commit -m "m2: Grids engine: C port of Mutable Instruments' pattern generator (map + Euclidean), bit-exact against the original

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Grids clock and track routing

**Files:**
- Modify: `firmware/src/seq.c` (`gclk`, `grids_samples`, `grids_tick`, `seq_start`, `events_block`, `input_hit`)
- Modify: `tests/drum_host.h` (`host_reset_fx`: `gclk`)
- Test: `tests/drum_test.c`

**Interfaces:**
- Consumes: `grids_start`, `grids_step`, `grids` (Task 4); `P_SRC` (Task 1); `step_fire` skips `P_SRC` tracks (Task 2).
- Produces: `static struct { uint32_t pos, cnt; } gclk;` (samples into the current 1/32, 1/32s since PLAY);
  `static uint32_t grids_samples(uint32_t cnt)`.

- [ ] **Step 1: Write the failing tests**

`tests/drum_test.c`, after the Grids engine tests:

```c
/* EUCLID 1/1 full on G-KCK: a hit every 1/16 on the same samples as a 1/16 step track (global swing 50);
 * MAP: 32 Grids steps = one bar */
static void test_grids_clock(void)
{
    uint32_t a1[40], a2[40], n1, n2, i, ok, p = FS * 60 / 120 / 4;
    host_init();
    song.g[G_SWING] = 50;
    song.g[G_GMODE] = 1;
    song.g[G_GLEN1] = 1;
    song.g[G_GFILL1] = 127;
    trk[0].p[P_SRC] = 1;
    play();
    n1 = hits_at(0, 32 * p - p / 2, a1, 40);
    host_init();
    song.g[G_SWING] = 50;
    trk[0].p[P_SLEN] = 1;
    trk[0].step[0].on = 1;
    play();
    n2 = hits_at(0, 32 * p - p / 2, a2, 40);
    ok = n1 == 32u && n2 == 32u;
    for (i = 0; ok && i < n1; i++)
        ok &= a1[i] == a2[i];
    check("Grids: EUCLID LEN 1 full plays every 1/16 on the same samples as a 1/16 step track (swing 50)", ok);
    host_init();
    play();
    render_mix(0, 0, 16 * p - p / 4);
    check("Grids: MAP steps are 1/32s: 32 steps in 16 sixteenths", gclk.cnt == 31u && grids.step == 0u);
}

/* two tracks on G-SNR play the same hits; their own steps are silent meanwhile; SRC back: the steps play in sync */
static void test_grids_follow(void)
{
    uint32_t f, a1, a4, n = 0, same = 1, p = FS * 60 / 120 / 4, k;
    host_init();
    song.g[G_GFILL2] = 110;
    song.g[G_GCHAOS] = 127;
    trk[1].p[P_SRC] = trk[4].p[P_SRC] = 2;
    for (k = 0; k < 16u; k++)
        trk[1].step[k].on = 1;                       /* its own steps: every step, silent while it follows */
    play();
    a1 = hit_age(&trk[1]);
    a4 = hit_age(&trk[4]);
    for (f = 0; f < 32u * p; f += CTL) {
        uint32_t h1, h4;
        render_mix(0, 0, CTL);
        h1 = hit_age(&trk[1]) != a1;
        h4 = hit_age(&trk[4]) != a4;
        same &= h1 == h4;
        n += h1;
        a1 = hit_age(&trk[1]);
        a4 = hit_age(&trk[4]);
    }
    printf("     Grids snare (fill 110, chaos 127): %u hits in 2 bars\n", n);
    check("Grids: two tracks on one channel play the same hits, not their own steps", same && n > 4u);
    trk[1].p[P_SRC] = 0;
    check("Grids: back to STEP, the track's step is the others' (in sync)", trk[1].seq_idx == trk[0].seq_idx);
    a1 = hit_age(&trk[1]);
    render_mix(0, 0, p);
    check("Grids: back to STEP, its own steps play again", hit_age(&trk[1]) != a1);
}

/* chaos: the same session after every PLAY */
static void test_grids_repeat(void)
{
    uint32_t r1[64], r2[64], n1, n2, i, same, p = FS * 60 / 120 / 4;
    host_init();
    song.g[G_GCHAOS] = 127;
    song.g[G_GFILL3] = 90;
    trk[3].p[P_SRC] = 3;
    play();
    n1 = hits_at(3, 32 * p, r1, 64);
    transport_req = 2;
    render_mix(0, 0, CTL);
    play();
    n2 = hits_at(3, 32 * p, r2, 64);
    same = n1 == n2 && n1 > 0u;
    for (i = 0; same && i < n1 && i < 64u; i++)
        same &= r1[i] == r2[i];
    check("Grids: with chaos, every PLAY repeats the same hats", same);
}

/* live recording on a Grids track: its key plays, no step is written */
static void test_grids_rec_skip(void)
{
    uint32_t p = FS * 60 / 120 / 4, a;
    host_init();
    trk[0].p[P_SRC] = 1;
    song.rec = 1u;
    play();
    render_mix(0, 0, p + p * 3 / 4 / CTL * CTL);
    a = hit_age(&trk[0]);
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    fm1_in.notes = 0;
    check("live record: a Grids track's key plays it but writes no step",
          hit_age(&trk[0]) != a && !trk[0].step[1].on && !trk[0].step[2].on);
}
```

Call the four in `main` after `test_grids_mode_switch();`.

Run: the drum_test command. Expected: compile error `gclk` undeclared.

- [ ] **Step 2: Implement**

`firmware/src/seq.c` — header comment, add a sentence: `Grids (grids.c) runs on its own 1/32 clock and plays
the tracks whose SRC is one of its channels.` After `step_samples`:

```c
/* Grids' clock: a 1/32 per Grids step, counted since PLAY; the global swing makes every other 1/16 long (as on a
 * 1/16 step track) and each 1/16 is two equal 1/32s */
static struct { uint32_t pos, cnt; } gclk;
static uint32_t grids_samples(uint32_t cnt)
{
    uint32_t p16 = div_samples(2u);
    int32_t sw = song.g[G_SWING] * (int32_t)p16 / 250;
    uint32_t l16 = p16 + (uint32_t)(((cnt >> 1) & 1u) ? -sw : sw);
    return (cnt & 1u) ? l16 - l16 / 2u : l16 / 2u;
}

static void grids_tick(uint32_t n)
{
    if (!song.playing)
        return;
    gclk.pos += n;
    for (;;) {
        uint32_t cur = grids_samples(gclk.cnt), bits, i;
        if (gclk.pos < cur && gclk.pos != 0x7FFFFFFFu + n)
            break;
        gclk.pos = gclk.pos >= 0x7FFFFFFFu ? 0 : gclk.pos - cur;
        gclk.cnt++;
        bits = grids_step();
        for (i = 0; bits & 7u && i < NTRK; i++) {    /* the tracks on a channel that fired: accent 127, else 96 */
            uint32_t ch = (uint32_t)trk[i].p[P_SRC];
            if (ch >= 1u && ch <= 3u && ((bits >> (ch - 1u)) & 1u))
                drum_hit(&trk[i], ((bits >> (ch + 2u)) & 1u) ? 127u : 96u);
        }
    }
}
```

`input_hit`: record only for step tracks:

```c
    if (((song.rec >> trk_index(t)) & 1u) && song.playing && !t->p[P_SRC])
        rec_hit(t, vel);
```

`seq_start`, before `song.tick = 0;`:

```c
    gclk.pos = 0x7FFFFFFF;                          /* Grids step 0 on the first block too */
    gclk.cnt = 0xFFFFFFFFu;
    grids_start();
```

`events_block`, after the `seq_tick` loop: `grids_tick(n);`.

`tests/drum_host.h` `host_reset_fx`: `memset(&gclk, 0, sizeof gclk);`.

- [ ] **Step 3: Run to see them pass**

Run: the drum_test command.
Expected: the four Grids lines `ok`, the printed snare count above 4; no `FAIL` (`test_boot_cost` idle
audio still within 1.25 × upstream: Grids does nothing while stopped).

- [ ] **Step 4: Whole suite, commit**

Run: `sh tests/run_drum_tests.sh > build/host/drum_suite.txt 2>&1; tail -5 build/host/drum_suite.txt`
Expected: `ALL DRUM HOST TESTS PASSED`.

```bash
git add firmware/src/seq.c tests/drum_host.h tests/drum_test.c
git commit -m "m2: Grids clock (1/32, global swing per 1/16) plays the tracks whose SRC is a Grids channel

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: GRIDS pages (ARP), SRC on PATTERN, read-only views of Grids tracks

**Files:**
- Modify: `firmware/src/pages.c` (FAM_GRIDS, GR_GRIDS, pages, FAM_BTN, `page_id`, `page_desc`)
- Modify: `firmware/src/ui.c` (view helpers, `bank_count`, `step_press` guard)
- Modify: `firmware/src/ui_draw.c` (`steps_hash`, `graph_steps`, `graph_grid`, `graph_grids`, `draw_mix`, `draw_foot`,
  `graph_signature`, `draw_graph`)
- Modify: `firmware/src/ui_input.c` (`ui_leds` grid)
- Modify: `tests/run_drum_tests.sh` (`build/ui_shots/grids`)
- Test: `tests/ui_test.c`

**Interfaces:**
- Consumes: `grids_preview`, `grids_len`, `grids_pos`, `grids` (Task 4); `gclk` not needed.
- Produces: `FAM_GRIDS` (before `FAM_MIX`), `GR_GRIDS`; `static uint32_t page_id(const page_t *pg, uint32_t slot)`;
  `static uint32_t view_src(const track_t *t)` (0 or channel 1..3), `view_len`, `static step_t view_step(const
  track_t *t, uint32_t si)`, `view_idx`.

- [ ] **Step 1: Write the failing tests**

`tests/ui_test.c` — `test_families`: add `{B_ARP, FAM_GRIDS}` to `MAP` and change the check text to
`"buttons open their page families (EDIT SOUND, ENV TRACK, LFO LAYER, FX, SEQ, GLO, SAVE, ARP GRIDS)"`; the
following `press(B_SCL)` check expects `FAM_GRIDS` (the last family opened): `cur_page()->fam == FAM_GRIDS`.

New tests after `test_grid_step_edit`:

```c
static void test_grids_pages(void)
{
    ui_host_init();
    press(B_ARP);
    ui_frame();
    release_all();
    check("ARP opens GRIDS 1/2 (MODE X Y CHAOS)", !ui.home && str_eq(cur_page()->title, "GRIDS") && cur_page()->id[0] == G_GMODE);
    snap_page("grids/01_map_page1");
    turn(EN_K1 + 1, 3);
    ui_frame();
    check("GRIDS MAP: KNOB 2 turns X", song.g[G_GX] == 67);
    turn(EN_K1, 1);
    ui_frame();
    check("GRIDS: KNOB 1 switches MODE to EUCL", song.g[G_GMODE] == 1);
    turn(EN_K1 + 1, -2);
    ui_frame();
    check("GRIDS EUCLID: KNOB 2 turns LEN K (X kept)", song.g[G_GLEN1] == 14 && song.g[G_GX] == 67);
    snap_page("grids/03_euclid_page1");
    press(B_ARP);
    ui_frame();
    release_all();
    check("ARP again: GRIDS 2/2 (FIL K FIL S FIL H)", cur_page()->id[0] == G_GFILL1);
    turn(EN_K1 + 2, 5);
    ui_frame();
    check("GRIDS 2/2: KNOB 3 turns FIL H", song.g[G_GFILL3] == 69);
    trk[0].p[P_SRC] = 1;
    trk[5].p[P_SRC] = 1;
    trk[1].p[P_SRC] = 2;
    trk[3].p[P_SRC] = 3;
    ui.force = 1;
    snap_page("grids/04_euclid_page2_routing");
    song.g[G_GMODE] = 0;
    transport_req = 1;
    seq_play_to(1, 6);
    snap_page("grids/02_map_page2_playing");
    press(B_ARP);
    ui_frame();
    release_all();
    snap_page("grids/05_map_page1_playing");
}

/* a track on a Grids channel: STEP / PATTERN / the footer / the LEDs show the channel, read-only */
static void test_src_readonly(void)
{
    uint32_t k, ok = 1;
    ui_host_init();
    trk[0].step[0].on = 1;
    seq_open("PATTERN");
    turn(EN_K1 + 3, 1);
    ui_frame();
    check("PATTERN: KNOB 4 is SRC (STEP -> G-KCK)", trk[0].p[P_SRC] == 1);
    snap_page("grids/07_pattern_src_kick");
    song.g[G_GFILL1] = 127;
    seq_open("STEP");
    check("a Grids track's STEP grid shows the channel: MAP, 32 steps on 2 banks", bank_count() == 2u);
    for (k = 0; k < 16u; k++)
        ok &= led_lit(14u + WHITE[k]) == (int)(grids_preview(0, k) & 1u);
    check("the keys' LEDs show the channel's steps", ok);
    snap_page("grids/06_step_grids_track");
    grid_hold(WHITE[3], 0);
    grid_hold_turn(WHITE[3], 0, 2);
    keys(0);
    ui_frame();
    check("keys and hold + knob do nothing on a Grids track", !trk[0].step[3].on && trk[0].step[3].cond == 0 && trk[0].step[0].on);
    song.g[G_GMODE] = 1;
    song.g[G_GLEN1] = 20;
    ui_frame();
    check("EUCLID: the grid shows LEN K steps (20: 2 banks)", bank_count() == 2u && view_len(&trk[0]) == 20u);
    trk[0].p[P_SRC] = 0;
    ui_frame();
    check("SRC back to STEP: its own steps again", bank_count() == 1u && led_lit(14u + WHITE[0]));
    song.g[G_GMODE] = 0;
    trk[0].p[P_SRC] = 1;
    trk[2].p[P_SRC] = 3;
    press(B_REC);                                    /* TRACKS */
    ui_frame();
    release_all();
    transport_req = 1;
    seq_play_to(1, 5);
    snap_page("grids/08_tracks_grids_rows");
}
```

Call in `main` after `test_grid_step_edit();`: `test_grids_pages(); test_src_readonly();`.

`tests/run_drum_tests.sh`: `mkdir -p build/ui_shots/engines build/ui_shots/seq build/ui_shots/grids` and add
`build/ui_shots/grids/*.ppm` to the `rm -f` list.

Run: the ui_test command. Expected: compile errors (`FAM_GRIDS`, `view_len` undeclared).

- [ ] **Step 2: Implement pages and views**

`firmware/src/pages.c`:

```c
enum { FAM_HOME, FAM_SND, FAM_TRK, FAM_LAY, FAM_FX, FAM_SEQ, FAM_GLO, FAM_SAVE, FAM_GRIDS, FAM_MIX, FAM_COUNT };
...
enum { GR_NONE, GR_MODEL, GR_FX, GR_SLCR, GR_GRID, GR_STEPS, GR_SLOTS, GR_MIX, GR_GRIDS };
```

PAGES: PATTERN becomes `{"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SRC}},`; before TRACKS:

```c
    {"GRIDS", FAM_GRIDS, SC_GLOBAL, GR_GRIDS, {G_GMODE, G_GX, G_GY, G_GCHAOS}},   /* EUCLID: MODE LEN K S H */
    {"GRIDS", FAM_GRIDS, SC_GLOBAL, GR_GRIDS, {G_GFILL1, G_GFILL2, G_GFILL3, 0xFF}},
```

FAM_BTN: `/* the button of each family (SCL has none) */` and
`{B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_SEQ, B_GLO, B_SAVE, B_ARP, B_REC}`. Then:

```c
/* the parameter of a column: GRIDS 1/2 in EUCLID mode turns LEN K / S / H where MAP has X / Y / CHAOS */
static uint32_t page_id(const page_t *pg, uint32_t slot)
{
    uint32_t id = pg->id[slot & 3u];
    if (pg->graph == GR_GRIDS && id >= G_GX && id <= G_GCHAOS && song.g[G_GMODE])
        id = G_GLEN1 + (id - G_GX);
    return id;
}
```

and in `page_desc` replace `uint32_t id = pg->id[slot & 3u];` with `uint32_t id = page_id(pg, slot);`.

`firmware/src/ui.c`, before the STEP grid section:

```c
/* --------------------------------------------- what a track shows --- */
/* the STEP grid, PATTERN, the footer, TRACKS and the keys show a track's steps, or, when its SRC is a Grids
 * channel, that channel's pattern (read-only: Grids makes it) */
static uint32_t view_src(const track_t *t) { return t->p[P_SRC] >= 1 && t->p[P_SRC] <= 3 ? (uint32_t)t->p[P_SRC] : 0u; }
static uint32_t view_len(const track_t *t)
{
    uint32_t s = view_src(t);
    return s ? grids_len(s - 1u) : (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP);
}
static step_t view_step(const track_t *t, uint32_t si)
{
    uint32_t s = view_src(t), b;
    step_t r = {0, 0, 0, 0};
    if (!s)
        return t->step[si % NSTEP];
    b = grids_preview(s - 1u, si);
    r.on = (uint8_t)(b & 1u);
    r.acc = (uint8_t)((b >> 1) & 1u);
    return r;
}
static uint32_t view_idx(const track_t *t) { uint32_t s = view_src(t); return s ? grids_pos(s - 1u) : t->seq_idx; }
```

`bank_count`: `return (view_len(TSEL) + 15u) / 16u;`. `step_press`: the guard becomes
`if (k >= 16u || si >= (uint32_t)TSEL->p[P_SLEN] || view_src(TSEL))`.

`firmware/src/ui_input.c` `ui_leds`, grid branch:

```c
        for (k = 0; k < 16u; k++) {
            uint32_t si = ui.bank * 16u + k;
            int on = si < view_len(t) && view_step(t, si).on;
            if (song.playing && si == view_idx(t))
                on = !on;
            led_put(nl, 14u + STEP_KEY[k], on);
        }
```

`firmware/src/ui_draw.c`:

`steps_hash`:

```c
static uint32_t steps_hash(const track_t *t)
{
    uint32_t h = 2166136261u, i, n = view_len(t);
    for (i = 0; i < n; i++) {
        step_t s = view_step(t, i);
        h = (h ^ (s.on + s.acc * 2u + s.cond * 4u + s.rat * 256u)) * 16777619u;
    }
    return h ^ n * 7919u;
}
```

`graph_steps`: `len = view_len(t)`; `const step_t *st = &t->step[i];` becomes
`step_t sv = view_step(t, i); const step_t *st = &sv;`; the playhead test `i == t->seq_idx` becomes `i == view_idx(t)`.

`graph_grid` as a whole:

```c
/* STEP page: the bank's 16 steps as the keys show them; bank n/m on the left. Hint line: a held step's PROB /
 * RATCH, or a Grids track's channel (its grid is read-only) */
static void graph_grid(const track_t *t, uint16_t c)
{
    uint32_t i, len = view_len(t);
    char b[8];
    for (i = 0; i < 16u; i++) {
        uint32_t si = ui.bank * 16u + i;
        int32_t x = 4 + (int32_t)i * 14 + (int32_t)(i / 4u) * 2, y = 30;
        step_t sv = view_step(t, si);
        const step_t *st = &sv;
        if (si >= len) {
            cv_rect(x, y + 20, 11, 1, C_LINE);
            continue;
        }
        if (st->on)
            step_bar(x, st->acc ? y : y + 10, 11, st->acc ? 40 : 30, st, st->acc ? C_WHITE : c, 3);
        else
            cv_rect(x, y + 36, 11, 4, C_DIM);
        if (song.playing && si == view_idx(t))
            cv_rect(x, y + 46, 11, 3, C_WHITE);
    }
    fmt_int(b, (int32_t)ui.bank + 1);
    str_cpy(b + str_len(b), "/", 4);
    fmt_int(b + str_len(b), (int32_t)bank_count());
    cv_text(4, 4, &FONT_S, "BANK", C_GRAY);
    cv_text(48, 4, &FONT_S, b, C_HI);
    if (view_src(t)) {
        static const char *const GN[3] = {"GRIDS KICK", "GRIDS SNARE", "GRIDS HATS"};
        cv_text(4, 84, &FONT_S, GN[view_src(t) - 1u], C_AMB);
    } else if (ui.held < 16u && ui.step_t0[ui.held] && ui.step_si[ui.held] != 0xFFFFu) {
        const step_t *hs = &t->step[ui.step_si[ui.held]];
        char h[32], cs[8];
        str_cpy(h, "STEP ", sizeof h);
        fmt_int(h + str_len(h), (int32_t)ui.step_si[ui.held] + 1);
        str_cpy(h + str_len(h), "  ", sizeof h - str_len(h));
        cond_format(hs->cond, cs);
        str_cpy(h + str_len(h), cs, sizeof h - str_len(h));
        str_cpy(h + str_len(h), "  RATCH ", sizeof h - str_len(h));
        fmt_int(h + str_len(h), (int32_t)hs->rat + 1);
        cv_text(4, 84, &FONT_S, h, C_WHITE);
    } else {
        cv_text(4, 84, &FONT_S, "TAP: ON/OFF  HOLD: ACCENT", C_DIM);
    }
}
```

`draw_mix`: `len = view_len(t)`, `bank = song.playing ? (view_idx(t) % len) / 16u : ...`, the signature's
`t->seq_idx + 1u` → `view_idx(t) + 1u`, cells `step_t sv = view_step(t, si); const step_t *st = &sv;`,
`ph = song.playing && si == view_idx(t)`.
`draw_foot`: the sig's `t->seq_idx / 16u == ui.bank ? t->seq_idx + 1u` → `view_idx(t) / 16u == ui.bank ? view_idx(t) + 1u`;
cells: `if (si >= view_len(t)) continue;`, `step_t sv = view_step(t, si); const step_t *st = &sv;`,
`si == view_idx(t)`.

The GRIDS picture, before `graph_fx`:

```c
/* GRIDS pages. MAP: the 5 x 5 node map (faint) with the X / Y point, and the three channels' 32 steps without
 * chaos (accent tall, playhead underlined). EUCLID: three rings of LEN positions (accent large, playhead marked).
 * Page 2 adds the routing: which tracks play each channel. */
static void graph_grids(uint16_t c, int routing)
{
    static const char *const CH[3] = {"K", "S", "H"};
    uint32_t ch, i;
    if (!song.g[G_GMODE]) {
        int32_t px = 6 + song.g[G_GX] * 72 / 127, py = 6 + song.g[G_GY] * 72 / 127;
        for (i = 0; i < 25u; i++)
            cv_rect(5 + (int32_t)(i % 5u) * 18, 5 + (int32_t)(i / 5u) * 18, 2, 2, C_DIM);
        cv_rect(px - 3, py - 3, 7, 7, C_WHITE);
        for (ch = 0; ch < 3u; ch++) {
            int32_t y = 4 + (int32_t)ch * 24;
            cv_text(92, y + 2, &FONT_S, CH[ch], C_GRAY);
            for (i = 0; i < 32u; i++) {
                uint32_t b = grids_preview(ch, i);
                int32_t x = 106 + (int32_t)i * 4 + (int32_t)(i / 8u) * 2;
                int ph = song.playing && i == grids.last;
                if (b & 1u)
                    cv_rect(x, (b & 2u) ? y : y + 8, 3, (b & 2u) ? 18 : 10, ph ? C_WHITE : (b & 2u) ? C_WHITE : c);
                else
                    cv_rect(x, y + 17, 3, 1, C_DIM);
                if (ph)
                    cv_rect(x, y + 20, 3, 2, C_WHITE);
            }
        }
    } else {
        for (ch = 0; ch < 3u; ch++) {
            uint32_t len = grids_len(ch);
            int32_t cx = 40 + (int32_t)ch * 80, cy = 36;
            char b[8];
            for (i = 0; i < len; i++) {
                uint32_t ph = i * (0xFFFFFFFFu / len), bb = grids_preview(ch, i);
                int32_t x = cx + sine_i(ph) * 28 / 32768, y = cy - sine_i(ph + 0x40000000u) * 28 / 32768;
                if (bb & 2u)
                    cv_rect(x - 3, y - 3, 7, 7, C_WHITE);
                else if (bb & 1u)
                    cv_rect(x - 2, y - 2, 5, 5, c);
                else
                    cv_rect(x - 1, y - 1, 2, 2, C_DIM);
                if (song.playing && i == grids.elast[ch])
                    cv_rect(x - 4, y + 5, 9, 2, C_WHITE);
            }
            str_cpy(b, CH[ch], sizeof b);
            str_cpy(b + 1, " ", sizeof b - 1);
            fmt_int(b + 2, (int32_t)len);
            cv_text(cx - text_w(&FONT_S, b) / 2, 68, &FONT_S, b, C_GRAY);
        }
    }
    if (routing) {                                   /* "K: T1 T6  S: T2  H: -" */
        char r[48];
        uint32_t n = 0, k;
        r[0] = 0;
        for (ch = 0; ch < 3u; ch++) {
            uint32_t any = 0;
            str_cpy(r + n, ch ? "  " : "", sizeof r - n);
            n = str_len(r);
            str_cpy(r + n, CH[ch], sizeof r - n);
            str_cpy(r + str_len(r), ":", sizeof r - str_len(r));
            n = str_len(r);
            for (k = 0; k < NTRK && n + 4u < sizeof r; k++)
                if (trk[k].p[P_SRC] == (int16_t)(ch + 1u)) {
                    r[n++] = ' ';
                    r[n++] = 'T';
                    r[n++] = (char)('1' + k);
                    r[n] = 0;
                    any = 1;
                }
            if (!any && n + 3u < sizeof r) {
                str_cpy(r + n, " -", sizeof r - n);
                n = str_len(r);
            }
        }
        cv_text(4, 84, &FONT_S, r, C_AMB);
    }
}
```

`draw_graph`: add the case

```c
        case GR_GRIDS:
            graph_grids(c, pg->id[0] == G_GFILL1);
            break;
```

`graph_signature`: replace `song.playing ? t->seq_idx + 1u : 0u` with `song.playing ? view_idx(t) + 1u : 0u` and
add before `return h;`:

```c
    if (pg->graph == GR_GRIDS) {
        for (i = G_GMODE; i <= G_GLEN3; i++)
            h = (h ^ (uint32_t)song.g[i]) * 16777619u;
        for (i = 0; i < NTRK; i++)
            h = (h ^ (uint32_t)trk[i].p[P_SRC]) * 16777619u;
        h ^= ui.page * 389u;
        if (song.playing)
            h ^= (grids.last + 1u + (grids.elast[0] | grids.elast[1] << 8 | (uint32_t)grids.elast[2] << 16) * 64u) *
                 2654435761u;
    }
```

- [ ] **Step 3: Run to see them pass**

Run: the ui_test command.
Expected: all `test_families`, `test_grids_pages`, `test_src_readonly` lines `ok`; the existing UI checks still `ok`
(`test_ui_frame_cost` under its bound); `build/ui_shots/grids/01..08_*.ppm` written.

- [ ] **Step 4: Whole suite, commit**

Run: `sh tests/run_drum_tests.sh > build/host/drum_suite.txt 2>&1; tail -5 build/host/drum_suite.txt`
Expected: `ALL DRUM HOST TESTS PASSED`; PNGs in `build/ui_shots/grids/`.

```bash
git add firmware/src/pages.c firmware/src/ui.c firmware/src/ui_draw.c firmware/src/ui_input.c tests/ui_test.c tests/run_drum_tests.sh
git commit -m "m2: GRIDS pages on ARP (map / rings, routing), SRC on PATTERN, Grids tracks shown read-only

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Demos, device checklist, full suite with H1–H4, firmware, the user's review

**Files:**
- Modify: `tests/drumsim.c` (Grids and PROB / RATCH demos)
- Modify: `docs/DEVICE_INSTALL.md` (M2 hardware checks)

**Interfaces:**
- Consumes: everything above.

- [ ] **Step 1: Demo WAVs**

`tests/drumsim.c`, before `int main`:

```c
/* M2 demos, 120 BPM through the whole mix. grids_map.wav: kick / snare / hats on Grids MAP at four map points
 * (2 bars each, chaos off) then the same with chaos 100; grids_euclid.wav: EUCLID 16/12/8 then 5/7/3 (4 bars each);
 * prob_ratch.wav: a kit with 50 % hats, 3-hit rolls, a 1/2 and 2/2 snare fill and a 1-SHOT crash (8 bars) */
static void write_demo(const char *dir, const char *name, uint32_t bars, void (*bar)(uint32_t b))
{
    char path[256];
    uint32_t bar_len = 16 * (FS * 60 / 120 / 4), total = bars * bar_len / CTL * CTL, frames, i, b;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    wav_hdr(f, total);
    transport_req = 1;
    for (b = 0, frames = 0; frames < total; b++) {
        uint32_t end = (b + 1) * bar_len / CTL * CTL < total ? (b + 1) * bar_len / CTL * CTL : total;
        bar(b);
        while (frames < end) {
            uint32_t n = GAP < end - frames ? GAP : end - frames;
            render_mix(L, R, n);
            for (i = 0; i < n; i++)
                wav_put(f, L[i], R[i]);
            frames += n;
        }
    }
    fclose(f);
    printf("drumsim: %s\n", path);
}

static void grids_kit(void)                          /* 808 kick / snare / closed hat on the three channels */
{
    host_init();
    drum_set_model(&trk[0], DM_K808);
    drum_set_model(&trk[1], DM_S808);
    drum_set_model(&trk[2], DM_HATC);
    trk[0].p[P_SRC] = 1;
    trk[1].p[P_SRC] = 2;
    trk[2].p[P_SRC] = 3;
    trk[1].p[P_REV] = 40;
    song.g[G_GFILL1] = 80;
    song.g[G_GFILL2] = 70;
    song.g[G_GFILL3] = 100;
}

static void map_bar(uint32_t b)
{
    static const int16_t XY[4][2] = {{64, 64}, {0, 0}, {127, 30}, {30, 127}};
    song.g[G_GX] = XY[(b / 2u) % 4u][0];
    song.g[G_GY] = XY[(b / 2u) % 4u][1];
    song.g[G_GCHAOS] = b < 8u ? 0 : 100;
}

static void euclid_bar(uint32_t b)
{
    static const int16_t LN[2][3] = {{16, 12, 8}, {5, 7, 3}};
    uint32_t k;
    song.g[G_GMODE] = 1;
    for (k = 0; k < 3u; k++) {
        song.g[G_GLEN1 + k] = LN[b / 4u % 2u][k];
        song.g[G_GFILL1 + k] = (int16_t)(b / 4u ? 60 : 40);
    }
}

static void prob_bar(uint32_t b) { (void)b; }

static void prob_kit(void)
{
    uint32_t k;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_S909);
    drum_set_model(&trk[2], DM_HATC);
    drum_set_model(&trk[3], DM_CYMB);
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    trk[1].step[4].on = trk[1].step[12].on = 1;
    trk[1].step[14].on = 1;
    trk[1].step[14].cond = 22;                       /* 1/2: a fill every other bar */
    trk[1].step[15].on = 1;
    trk[1].step[15].cond = 23;                       /* 2/2 */
    trk[1].step[15].rat = 2;
    for (k = 0; k < 16u; k++) {
        trk[2].step[k].on = 1;
        trk[2].step[k].cond = (uint8_t)((k & 1u) ? cond_store(10) : 0u);   /* off-beats 50 % */
    }
    trk[2].step[7].rat = 2;
    trk[2].step[15].rat = 3;
    trk[3].step[0].on = trk[3].step[0].acc = 1;
    trk[3].step[0].cond = COND_1SHOT;
    trk[1].p[P_REV] = 40;
}
```

In `main`, after the KITS loop:

```c
    grids_kit();
    write_demo(dir, "grids_map.wav", 16, map_bar);
    grids_kit();
    write_demo(dir, "grids_euclid.wav", 8, euclid_bar);
    prob_kit();
    write_demo(dir, "prob_ratch.wav", 8, prob_bar);
```

Run: `cc $F -o build/host/drumsim tests/drumsim.c -lm && build/host/drumsim build/drum_renders | tail -3`
Expected: the three `drumsim: build/drum_renders/...wav` lines.

- [ ] **Step 2: Device checklist**

`docs/DEVICE_INSTALL.md`, after the existing check list, a section:

```markdown
### M2: PROB, RATCH, Grids (check on the FM-1)

- STEP grid: hold a step's key and turn KNOB 1: the hint line shows `STEP n  75%` (left of 100 %) or `1-SHOT`,
  `1/2` .. `8/8` (right); KNOB 2: `RATCH 2..4`. A step with PROB is drawn striped, RATCH as ticks above it.
- PLAY: a 50 % hat varies, a 1/2 step plays every other loop, a 1-SHOT crash only once after PLAY, a RATCH 3
  step rolls three hits.
- ARP: GRIDS 1/2 (MODE X Y CHAOS; MODE EUCL: LEN K S H) and 2/2 (FIL K S H, routing line). PATTERN KNOB 4 SRC
  `G-KCK` / `G-SNR` / `G-HAT` makes a track follow Grids; its STEP grid shows the pattern, keys do nothing.
- SAVE a project, power off and on, LOAD: PROB / RATCH / SRC / GRIDS come back. A project saved with the M1
  firmware loads with plain steps.
```

- [ ] **Step 3: Firmware build and the full suite (H1–H4)**

```bash
for i in 1 2 3; do DRUM_PACKAGE=1 ./build.sh > build/build.log 2>&1 && break; grep -q "exec format error" build/build.log || break; done; tail -6 build/build.log
tests/run_tests.sh > build/host/all.txt 2>&1; grep -E "^==|FAIL|PASSED|SKIPPED|budget|over" build/host/all.txt | tail -40
```

Expected: the build ends with the package `build/felucca-UNTESTED.fwsc`, the image size and `RAM .data+.bss … of
98304` (about +3 KB); `ALL HOST TESTS PASSED` with H2 (frozen code identical / reviewed), H3 (stack within 75 %),
the target budget (`fm1_alnk0_irq` within +10 %). If the target budget or the stack check fails, STOP: report the
numbers to the user and ask (a budget change is the user's decision); do not run `BUDGET_UPDATE=1`.

- [ ] **Step 4: Commit**

```bash
git add tests/drumsim.c docs/DEVICE_INSTALL.md
git commit -m "m2: demo WAVs (Grids map / Euclidean, PROB / RATCH kit) and the M2 device checklist

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 5: The user's review (stop here)**

Build the local installer site so the user can install it themselves (Claude never installs):
`python3 web/make_site.py build/felucca-UNTESTED.fwsc "drum-$(git rev-parse --short HEAD)" build/site`.
Then hand over: the screenshots (`build/ui_shots/seq/20..22`, `build/ui_shots/grids/01..08`), the WAVs
(`build/drum_renders/grids_map.wav`, `grids_euclid.wav`, `prob_ratch.wav`), the site command
(`python3 -m http.server 8000 --directory build/site`, hard reload), and the checklist in `docs/DEVICE_INSTALL.md`.
Wait for the user's verdict before merging.
