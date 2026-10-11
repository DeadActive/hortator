# ACID plan A (the engine: notes on the steps, drum transpose, the ACID voice, gate / slide / tie) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** steps carry a note, SLIDE and TIE (format FDRB, older projects converted and sounding as before); drum
tracks play their steps transposed; a new ACID model plays a 303-style line with gate, accent, slide and tie; its
cost, budget lines and A/B WAVs go to the user.

**Architecture:** `step_t` stays 4 bytes: its first byte becomes bitfields (`on acc slide tie rat`), its third the
note offset `nt` (NOTE − 60, 0 = C4), so all-zero is still a plain C4 step and the existing `s->on` / `s->acc` /
`s->rat` code keeps compiling. A track's playing note offset `t->nt` reaches every model through one helper
(`dm_tune`). ACID (`dm_acid.c`) is one more model; the model table gains two optional hooks (`glide`, `gate`) that
only ACID fills, and the sequencer drives them per step (`acid_step`). Plan B (KEYS mode, step / live record, the
piano roll) follows after this plan, on its interfaces.

**Tech Stack:** C (pi32v2 clang, integer / fixed point, `-Os`), the host test harness (`tests/drum_host.h`,
`tests/ui_host.h`), Python checks.

**Spec:** `docs/superpowers/specs/2026-10-09-acid-design.md` (§2 steps, §3 playback, §4 the voice, §8 tests; §5–7 are
plan B's)

## Global Constraints

- Worktree `/Users/evech/Desktop/dev/fm1-drummachine/felucca-acid`, branch `acid` (from `main` = `drum-v0.14.0`).
  Build env: `export PYTHON=$PWD/.venv/bin/python PATH="$PWD/.venv/bin:$PATH"` (`.venv` is a symlink to felucca's;
  never commit it: explicit `git add` paths). Builds only through `./build.sh` / `DRUM_PACKAGE=1 ./build.sh`, Docker
  running, retry up to 5× on "exec format error". Temp files in `/Users/evech/.claude/jobs/d6b478c9/tmp` (`$T`).
- Never install firmware; never run `tools/fm1_install.py`, the web installer, the M-VAVE updater, `tools/rescue/*`.
- Frozen files unchanged (`python3 tools/check_untouched.py` in `sh tests/run_drum_tests.sh`): `main.c`, `usb.c`,
  `usb_app.c`, `ota.c`, `crt0.S`, `app.ld`, `storage.c` (except `ST_MAGIC`), `firmware/hal/*`, `firmware/loader/*`,
  `core.h`'s last 5 lines.
- Integer / fixed point only; a 64-bit divide only by a 32-bit divisor (`grep -c divdi build/felucca.dis` = 0); the
  core `-Os`; no overclock.
- No new `.pool` buffers (the `ble` branch has about 670 B of pool to spare); the ACID voice's state lives in
  `dvoice_t`.
- Budgets and thresholds are the user's: new `tests/target_budget.txt` lines, `tests/drum_cost_ref.txt` limits and any
  test threshold change are asked first (Task 5 stops for them). The golden kit hashes (`tests/drum_golden.txt`) must
  not change for any existing model; `GOLDEN_UPDATE=1` may only add the ACID line.
- The user verifies by ear and eye: A/B WAVs (Task 5).
- Credit the fork as `Drum machine fork: 2026 DEADACTIVE` in changed / new file headers.
- Suites: `sh tests/run_drum_tests.sh`, `sh tests/run_tests.sh` (never inside `sh -c`; `run_tests.sh` needs
  `DRUM_PACKAGE=1 ./build.sh` first).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. The transport stopped, a song row changing, the track muted or its model changed while an ACID gate is open: the
   note ends (no hanging drone). Test: Task 4 `acid_seq_stop_closes`, `acid_model_change_cuts`.
2. An FDRA project in the RAM slots (.noinit, left by 0.14.0) and in flash after the update: both load converted,
   same sound. Test: Task 1 `fdra_flash_converts`, `fdra_ram_converts`.
3. Extreme ACID settings (RESO 127, CUTOF 127, ENVMD 127, ACCNT 127, accent chains, notes 0 and 127, TUNE ±24): no
   signed overflow (UBSan build), the output bounded, no NaN-like runaway. Test: Task 3 `acid_extremes`.
4. PROB / RATCH with SLIDE / TIE: a TIE after a step PROB skipped plays nothing; a ratcheted step restarts; a SLIDE
   into a skipped step closes the gate. Test: Task 4 `acid_seq_prob_tie`.
5. Corrupt step bytes in a stored project (reserved bits set, `nt` out of −60..67): cleared / clamped on load. Test:
   Task 1 `fdrb_clamps`.

---

### Task 1: Steps with a note, SLIDE and TIE; the FDRB format

**Files:**
- Modify: `firmware/src/core.h` (`step_t`), `firmware/src/project.c` (FDRB, the old step type, FDRA conversion,
  the RAM slot upgrade, load clamps), `firmware/src/ui.c:232` (positional initializer)
- Test: `tests/boot_test.c` (format), `tests/drum_test.c` (positional initializers at 1312, 1354, 1363, 1372),
  `tests/ui_test.c` (`test_project_rejects`)

**Interfaces:**
- Produces: `step_t { uint8_t on : 1, acc : 1, slide : 1, tie : 1, rat : 2, : 2; uint8_t cond; int8_t nt; uint8_t
  rsv; }` (4 B; byte 0 bit 0 on, bit 1 acc, bit 2 slide, bit 3 tie, bits 4..5 rat); `#define NT_MIN -60`, `#define
  NT_MAX 67`; `PROJ_MAGIC` = "FDRB" `0x42524446u`, `PROJ_MAGIC_VA` = "FDRA" `0x41524446u`; `step_v9_t { uint8_t on, acc,
  cond, rat; }` (the steps of FDR2..FDRA); `static void proj_upgrade(project_t *q)` (a valid FDRA image in place →
  FDRB).

- [ ] **Step 1: Failing tests** in `tests/boot_test.c`, after `fdra_clamps`:

```c
/* FDRB (ACID): the step's bytes: byte 0 = on | acc << 1 | slide << 2 | tie << 3 | rat << 4, byte 1 cond, byte 2 nt */
static int fdrb_step_layout(void)
{
    step_t s;
    const uint8_t *b = (const uint8_t *)&s;
    memset(&s, 0, sizeof s);
    s.on = 1;
    s.tie = 1;
    s.rat = 2;
    s.cond = 5;
    s.nt = -3;
    return sizeof(step_t) == 4u && b[0] == (1u | 8u | 0x20u) && b[1] == 5u && (int8_t)b[2] == -3 && b[3] == 0u &&
           sizeof(project_t) == 3672u;
}
/* an FDRA record in flash (same size as FDRB: told by its marker): steps into the flags, NOTE C4, no SLIDE / TIE */
static int fdra_flash_converts(void)
{
    static project_va_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_VA;
    v.size = sizeof v;
    for (i = 0; i < G_COUNT; i++)
        v.g[i] = song.g[i];
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_COUNT; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[3].p[P_FCUT] = 33;
    v.t[3].step[7] = (step_v9_t){1, 1, 5, 2};
    v.t[3].step[8] = (step_v9_t){0, 1, 0, 0};
    memcpy(v.name, "FDRA ONE", 8);
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 2u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(2);
    return sizeof v == 3672u && trk[3].p[P_FCUT] == 33 && trk[3].step[7].on && trk[3].step[7].acc &&
           trk[3].step[7].cond == 5u && trk[3].step[7].rat == 2u && trk[3].step[7].nt == 0 && !trk[3].step[7].slide &&
           !trk[3].step[7].tie && !trk[3].step[8].on && trk[3].step[8].acc && str_eq(chain.name, "FDRA ONE");
}
/* an FDRA image left in a RAM slot by the previous firmware (.noinit): converted at boot, not dropped */
static int fdra_ram_converts(void)
{
    project_va_t *v = (project_va_t *)(void *)&proj_slot[1];
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    v->magic = PROJ_MAGIC_VA;
    v->size = sizeof *v;
    for (i = 0; i < G_COUNT; i++)
        v->g[i] = song.g[i];
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_COUNT; i++)
            v->t[k].p[i] = trk[k].p[i];
    v->t[6].step[3] = (step_v9_t){1, 0, 0, 3};
    v->sum = proj_hash(v, sizeof *v - 4u);
    persist_boot();
    project_load(1);
    return trk[6].step[3].on && trk[6].step[3].rat == 3u && trk[6].step[3].nt == 0;
}
/* FDRB: SLIDE, TIE and the note through a save and a load (flash) */
static int fdrb_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[2].step[4].on = 1;
    trk[2].step[4].slide = 1;
    trk[2].step[4].nt = -12;
    trk[2].step[5].tie = 1;
    project_save(0);
    host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(0);
    return trk[2].step[4].on && trk[2].step[4].slide && trk[2].step[4].nt == -12 && trk[2].step[5].tie &&
           !trk[2].step[5].on;
}
/* Review Focus 5: a stored step's reserved bits / byte and an out-of-range note: cleared and clamped on load */
static int fdrb_clamps(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    project_save(0);
    ((uint8_t *)&p->t[4].step[2])[0] = 0xFFu;        /* every flag, the reserved bits too */
    ((uint8_t *)&p->t[4].step[2])[3] = 0x5Au;
    p->t[4].step[3].nt = 100;                        /* past NT_MAX (C4 + 67 = G9) */
    p->t[4].step[5].nt = -100;
    p->sum = proj_sum(p);
    project_load(0);
    return trk[4].step[2].on && trk[4].step[2].acc && trk[4].step[2].slide && trk[4].step[2].tie &&
           trk[4].step[2].rat == 3u && ((const uint8_t *)&trk[4].step[2])[0] == 0x3Fu &&
           ((const uint8_t *)&trk[4].step[2])[3] == 0u && trk[4].step[3].nt == NT_MAX && trk[4].step[5].nt == NT_MIN;
}
```

  and in `main`, beside the FDRA lines:

```c
    check("project: FDRB's step layout (flags, cond, note offset) in 4 bytes", fdrb_step_layout());
    check("project: an FDRA record in flash converts (NOTE C4, no SLIDE / TIE)", fdra_flash_converts());
    check("project: an FDRA image in a RAM slot converts at boot", fdra_ram_converts());
    check("project: FDRB keeps SLIDE, TIE and the note through a save and a load", fdrb_round_trip());
    check("project: a stored step's reserved bits are cleared, its note clamped", fdrb_clamps());
```

- [ ] **Step 2: Run them.** Run: `sh tests/run_drum_tests.sh > $T/d.log 2>&1; tail -30 $T/d.log`. Expected: the
  boot test fails to compile (`project_va_t`, `step_v9_t`, `PROJ_MAGIC_VA`, `slide`, `tie`, `nt`, `NT_MAX` undeclared).

- [ ] **Step 3: The step** in `firmware/src/core.h`, replacing `step_t`:

```c
typedef struct {                 /* one sequencer step (4 B, stored as is); all zero = a plain step, off, at C4 */
    uint8_t on : 1, acc : 1;     /* hit; accent (velocity 127, else 96) */
    uint8_t slide : 1, tie : 1;  /* ACID: glide into the next note / hold the sounding note through this step */
    uint8_t rat : 2;             /* RATCH: hits - 1 (0..3) */
    uint8_t : 2;
    uint8_t cond;                /* PROB: 0 = 100 %, 1..20 = 0..95 %, 21 = 1-SHOT, 22..56 = A/B (params.c cond_*) */
    int8_t nt;                   /* NOTE - 60 (C4 = 0): a drum's transpose, ACID's note (NT_MIN..NT_MAX) */
    uint8_t rsv;                 /* 0 */
} step_t;
#define NT_MIN (-60)
#define NT_MAX 67
```

- [ ] **Step 4: FDRB in `firmware/src/project.c`.**
  - header comment: `Format "FDRB" (the 11th): … + each step's note offset, SLIDE and TIE (ACID)`; the list of
    converted formats gains `the FILTER's "FDRA"`.
  - `#define PROJ_MAGIC 0x42524446u /* "FDRB" (the 11th): + the step's note, SLIDE and TIE (ACID) */` and
    `#define PROJ_MAGIC_VA 0x41524446u /* "FDRA": the FILTER, the old steps; converted on load (same size) */`.
  - inside `#if FELUCCA_FLASH`, before `project_v1_t`: `typedef struct { uint8_t on, acc, cond, rat; } step_v9_t;
    /* a step as FDR2..FDRA stored it */`; in `project_v2_t` .. `project_v4_t` and `proj_trk_v9_t` replace `step_t
    step[NSTEP]` with `step_v9_t step[NSTEP]`; add after `project_v9_t`:

```c
/* FDRA: everything, the steps as FDR2..FDR9 (same size as FDRB: told by the marker) */
typedef struct {
    int16_t p[P_COUNT];
    step_v9_t step[NSTEP];
} proj_trk_va_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_va_t t[NTRK];
    motion_store_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_va_t;
typedef char project_va_size[sizeof(project_va_t) == sizeof(project_t) ? 1 : -1];
```

    and `project_va_t va;` in the `proj_old` union.
  - a step converter, before `proj_from_old`:

```c
static step_t step_up(const step_v9_t *o)          /* an FDR2..FDRA step: the flags, NOTE C4, no SLIDE / TIE */
{
    step_t s;
    memset(&s, 0, sizeof s);
    s.on = o->on ? 1u : 0u;
    s.acc = o->acc ? 1u : 0u;
    s.rat = (uint8_t)(o->rat < 3u ? o->rat : 3u);
    s.cond = o->cond;
    return s;
}
```

  - `proj_from_old`: `ver >= 8u` handles 8, 9 and 10 (FDRA: `proj_old.va`, every parameter kept: `i < (ver == 10u ?
    P_COUNT : P_FTYPE)`); its step `memcpy` becomes `for (i = 0; i < NSTEP; i++) q->t[k].step[i] = step_up(&ot[k].step[i]);`
    with `ot` a `const step_v9_t *` per track (`ver == 10u ? proj_old.va.t[k].step : …`); the motion, song and name
    as FDR9 for ver 10. The v2..v7 branch: `q->t[k].step[i] = step_up(ver == 2u ? &proj_old.v2.t[k].step[i] : …)`.
    v1 keeps writing `on` / `acc` (the rest zero).
  - `proj_fetch`: try the old formats whenever the record is not a valid FDRB one (an FDRA record has FDRB's size):
    the condition `n > 8 && n <= (int)sizeof proj_old && n != (int)sizeof *q` becomes `n > 8 && n <= (int)sizeof
    proj_old && !proj_ok(q)`, the `MAGIC` / `SIZE` tables gain index 10 (`PROJ_MAGIC_VA`, `sizeof proj_old.va`), the
    loop runs `ver < 11u`.
  - the RAM slots: `static void proj_upgrade(project_t *q)` (outside `#if FELUCCA_FLASH`, with its own copy of
    `project_va_t` / `step_v9_t` visible: move those two types and `step_up` above the `#if FELUCCA_FLASH`):

```c
/* an FDRA image in a RAM slot (.noinit, left by the previous firmware): converted in place */
static void proj_upgrade(project_t *q)
{
    static project_va_t o;
    uint32_t i, k;
    if (q->magic != PROJ_MAGIC_VA || q->size != sizeof *q || q->sum != proj_hash(q, sizeof *q - 4u))
        return;
    memcpy(&o, q, sizeof o);
    memcpy(q, &o, sizeof *q);                          /* the globals, parameters, motion, song, name */
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++)
            q->t[k].step[i] = step_up(&o.t[k].step[i]);
    q->magic = PROJ_MAGIC;
    q->sum = proj_sum(q);
}
```

    (the two layouts differ only in the step type, same size: the copy keeps every other field at its offset);
    `persist_boot`'s slot loop and `project_load` call `proj_upgrade(&proj_slot[i])` / `proj_upgrade(p)` first.
    `static project_va_t o` is .bss, not .pool (3672 B of .bss: check the build's `.data+.bss` stays within 96 KB).
  - `project_load`'s step loop: as now plus `t->step[i].slide = q->slide; t->step[i].tie = q->tie; t->step[i].nt =
    (int8_t)clamp(q->nt, NT_MIN, NT_MAX); t->step[i].rsv = 0;` (field by field: the reserved bits are not copied).

- [ ] **Step 5: The positional initializers** (bitfields shift them): `firmware/src/ui.c:232` `step_t r = {0, 0, 0,
  0};` → `step_t r = {0};`; `tests/drum_test.c` 1312 / 1354 `{1, 0, 0, 0}` → `{.on = 1}`, 1363 → `{.on = 1, .cond =
  COND_1SHOT}`, 1372 → `{.on = 1, .cond = 22}`. Find any other: `grep -rn "step_t [a-z_]* *= *{[^}0]" firmware/src
  tests`. `tests/ui_test.c` `test_project_rejects`: `step[0].on = 7` → `((uint8_t *)&proj_slot[0].t[3].step[0])[0]
  = 0xC1u;` (on + both reserved bits) and `step[1].rat = 9` → `((uint8_t *)&proj_slot[0].t[3].step[1])[0] = 0x30u;`
  (rat 3); keep its expectations if they still hold, else adjust them to the clamp (ledger it).

- [ ] **Step 6: Run.** `sh tests/run_drum_tests.sh > $T/d.log 2>&1; tail -5 $T/d.log; grep -c FAIL $T/d.log`.
  Expected: the five new lines `ok`, every old one `ok` (the golden kit hashes unchanged), `ALL DRUM HOST TESTS
  PASSED`. Then `DRUM_PACKAGE=1 ./build.sh` (expected: every check ok, the image within ±1 KB of 296948 B, `.data +
  .bss` within 96 KB) and `sh tests/run_tests.sh` (expected: `ALL HOST TESTS PASSED`).

- [ ] **Step 7: Commit**

```bash
git add firmware/src/core.h firmware/src/project.c firmware/src/ui.c tests/boot_test.c tests/drum_test.c tests/ui_test.c
git commit -m "ACID: steps with a note offset, SLIDE and TIE (FDRB); FDRA and older convert, in flash and in RAM"
```

### Task 2: Drum tracks play their steps transposed

**Files:**
- Modify: `firmware/src/core.h` (`track_t.nt`), `firmware/src/drum_core.c` (`drum_play`), `firmware/src/dm_dsp.c`
  (`dm_tune`), every `firmware/src/dm_*.c` that reads TUNE (`p[0]` of `&t->p[P_E0]`), `firmware/src/seq.c`
  (`step_fire`, `rat_due`)
- Test: `tests/drum_test.c`

**Interfaces:**
- Consumes: Task 1's `step_t.nt`.
- Produces: `int8_t nt;` in `track_t` (the playing note offset, 0 = C4); `static inline int32_t dm_tune(const
  track_t *t)` = `t->p[P_E0] + clamp(t->nt, -24, 24)`; `static void drum_play(track_t *t, uint32_t vel, int32_t nt)`
  (sets `t->nt`, then the hit); `drum_hit(t, vel)` = `drum_play(t, vel, 0)` (MIDI, the keys, BENCH: C4).

- [ ] **Step 1: Failing test** in `tests/drum_test.c` (near `test_golden`):

```c
/* ACID spec §3: a drum step's note transposes the model as TUNE does: nt = +7 with TUNE 0 renders exactly as nt 0
 * with TUNE 7 (every model), and the transpose is clamped to +-24 */
static void test_drum_transpose(void)
{
    static int32_t a[SECS(0.5)];
    uint32_t mi, ok = 1, clamp_ok = 1;
    for (mi = 0; mi < NMODELS; mi++) {
        if (mi == DM_ACID)
            continue;
        host_init();
        drum_set_model(&trk[0], mi);
        trk[0].p[P_E0] = 0;
        drum_play(&trk[0], 96, 7);
        render_track(&trk[0], a, SECS(0.5));
        host_init();
        drum_set_model(&trk[0], mi);
        trk[0].p[P_E0] = 7;
        drum_play(&trk[0], 96, 0);
        render_track(&trk[0], wl, SECS(0.5));
        if (memcmp(a, wl, sizeof a)) {
            printf("     %s: nt +7 != TUNE +7\n", N_MODEL[mi]);
            ok = 0;
        }
    }
    trk[0].nt = 40;
    clamp_ok = dm_tune(&trk[0]) == trk[0].p[P_E0] + 24;
    trk[0].nt = -40;
    clamp_ok &= dm_tune(&trk[0]) == trk[0].p[P_E0] - 24;
    check("drum transpose: a step's note = TUNE by as many semitones, every model", ok);
    check("drum transpose: clamped to +-24 semitones", clamp_ok);
}
```

  (`host_init`, `drum_set_model`, `render_track` and `wl` as `hit_model` uses them, drum_test.c 44.) Until Task 3
  exists, write the `DM_ACID` skip as `if (0)` and change it to `mi == DM_ACID` in Task 3. Call it from `main` after
  `test_golden()`.

- [ ] **Step 2: Run** `sh tests/run_drum_tests.sh`. Expected: compile errors (`drum_play`, `dm_tune`, `nt` in
  `track_t`).

- [ ] **Step 3: Implement.**
  - `core.h` `track_t`, after `step[NSTEP]`: `int8_t nt; /* the playing note's offset from C4 (the step's, 0 for MIDI,
    keys): models read TUNE through dm_tune */`.
  - `dm_dsp.c`, after `inc_tune`:

```c
/* TUNE as the models read it: the knob plus the playing step's note offset (a drum: +-24 semitones) */
static inline int32_t dm_tune(const struct track *t) { return t->p[P_E0] + clamp(t->nt, -24, 24); }
```

    (`dm_dsp.c` is included before `track_t` is complete? Check the include order in `dmodels.c`; if `track_t` is not
    yet defined there, define `dm_tune` at the top of `dmodels.c` after the includes that need it, or in `core.h`
    after `track_t`, and ledger where.)
  - every model's TUNE read: `grep -n "p\[0\]" firmware/src/dm_*.c` — each `p[0]` that is the TUNE knob (`p =
    &t->p[P_E0]`) becomes `dm_tune(t)` (in `trigger` and in `render` alike). Do not change other arrays' `[0]`.
  - `drum_core.c`: rename the body of `drum_hit` to `static void drum_play(track_t *t, uint32_t vel, int32_t nt)`
    with `t->nt = (int8_t)clamp(nt, NT_MIN, NT_MAX);` as its first line, and `static void drum_hit(track_t *t,
    uint32_t vel) { drum_play(t, vel, 0); }`.
  - `seq.c`: `step_fire`'s `drum_hit(t, t->rat_vel)` → `drum_play(t, t->rat_vel, s->nt)`; `rat_due`'s →
    `drum_play(t, t->rat_vel, t->nt)` (a roll keeps its step's note).

- [ ] **Step 4: Run** `sh tests/run_drum_tests.sh`. Expected: the two new lines `ok`; the golden kit hashes unchanged
  (`nt` is 0 everywhere they hit); `ALL DRUM HOST TESTS PASSED`. Then `DRUM_PACKAGE=1 ./build.sh` and `sh
  tests/run_tests.sh`: pass; `tests/target_budget.py` within +10 % (the render loops read `dm_tune` once per block or
  hit; if a loop grows past +10 %, hoist the read out of the loop: no budget change).

- [ ] **Step 5: Commit**

```bash
git add firmware/src/core.h firmware/src/dm_dsp.c firmware/src/dmodels.c firmware/src/dm_*.c firmware/src/drum_core.c firmware/src/seq.c tests/drum_test.c
git commit -m "ACID: drum steps play transposed by their note (dm_tune, +-24 semitones); MIDI and keys at C4"
```

### Task 3: The ACID voice

**Files:**
- Create: `firmware/src/dm_acid.c`, `tests/acid_test.c`
- Modify: `firmware/src/core.h` (`dmodel_t` hooks), `firmware/src/dmodels.c` (enum, table, names, include),
  `tests/run_drum_tests.sh` (acid_test with UBSan), `tests/drum_golden.txt` (the ACID line only)
- Test: `tests/acid_test.c`

**Interfaces:**
- Consumes: Task 2's `dm_tune`, `drum_play`, `t->nt`.
- Produces: `DM_ACID` (appended after `DM_FM`), `N_MODEL` `"ACID"`; in `dmodel_t` after `weight`: `void (*glide)(struct
  track *t, dvoice_t *v);` (the sounding voice moves to the track's current `nt` + TUNE without restarting) and `void
  (*gate)(struct track *t, dvoice_t *v, int open);` (0: release); ACID's knobs `P_E0..P_E7` = TUNE DECAY CUTOF RESO
  ENVMD ACCNT WAVE GATE; `acid_trigger`, `acid_render`, `acid_glide`, `acid_gate`; voice fields: `ph[0]` oscillator
  phase, `x[0]` pitch now (1/16 semitone), `x[1]` pitch target, `x[2]` flags (bit 0 gate open, bit 1 attack done,
  bit 2 SQR), `x[3]` accent amount (Q15), `env[0]` amp (Q24), `env[1]` filter envelope (Q24), `env[2]` the accent
  "wow" smoother (Q24), `k[1]` DECAY factor, `f[0..3]` the ladder's states (Q24).

- [ ] **Step 1: Failing tests** `tests/acid_test.c` (built with UBSan like `filter_test`):

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* ACID (dm_acid.c, spec docs/superpowers/specs/2026-10-09-acid-design.md §4): the pitch, the filter's stability and
 * self-oscillation, the band-limited oscillator, gate, slide, accent, the level, the extremes. One line per test;
 * any failure exits 1. Built with UBSan: a signed overflow aborts. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
#define N (FS * 2)
static int32_t buf[N];

static double goertzel(const int32_t *x, uint32_t n, double f)
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / n)) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}
static double rms(const int32_t *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}
static int32_t peak(const int32_t *x, uint32_t n)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        m = abs(x[i]) > m ? abs(x[i]) : m;
    return m;
}

static track_t *acid(void)                          /* track 0 as ACID at its defaults, note C4 */
{
    host_init();
    drum_set_model(&trk[0], DM_ACID);
    return &trk[0];
}
static void play(track_t *t, uint32_t vel, int32_t nt, int32_t *out, uint32_t n)   /* a note, the gate open */
{
    memset(out, 0, n * sizeof *out);
    drum_play(t, vel, nt);
    dm_render_n(t, out, n);
}

static void test_pitch(void)                         /* A2 (nt -15): 110 Hz is the strongest partial near it */
{
    track_t *t = acid();
    double f;
    t->p[P_E0 + 2] = 127;                            /* CUTOF open: the fundamental clear */
    play(t, 96, -15, buf, N / 2);
    f = goertzel(buf + FS / 10, N / 2 - FS / 10, 110.0);
    check("acid: A2 sounds at 110 Hz (the step's note + TUNE)",
          f > 4 * goertzel(buf + FS / 10, N / 2 - FS / 10, 104.0) && f > 4 * goertzel(buf + FS / 10, N / 2 - FS / 10, 116.0));
}

static void test_gate(void)                          /* gate closed: silent within ~15 ms; open: still sounding */
{
    track_t *t = acid();
    play(t, 96, 0, buf, FS / 4);
    check("acid: the gate open holds the note (amp not decayed at 250 ms)", rms(buf + FS / 5, FS / 20) > 200.0);
    DMODELS[DM_ACID].gate(t, &t->v[0], 0);
    memset(buf, 0, FS / 4 * sizeof *buf);
    dm_render_n(t, buf, FS / 4);
    check("acid: the gate closed ends the note within 15 ms", rms(buf + FS * 15 / 1000, FS / 20) < 20.0 &&
          !t->v[0].active);
}

static void test_slide(void)                         /* a glide from C3 to C4 reaches C4 in 40..90 ms, no restart */
{
    track_t *t = acid();
    int32_t fe;
    uint32_t i, at = 0;
    play(t, 96, -12, buf, FS / 10);
    fe = t->v[0].env[1];
    t->nt = 0;
    DMODELS[DM_ACID].glide(t, &t->v[0]);
    check("acid: a slide does not restart the filter envelope", t->v[0].env[1] <= fe);
    for (i = 0; i < FS / 5 / CTL; i++) {
        dm_render_n(t, buf, CTL);
        if (!at && abs(t->v[0].x[0] - t->v[0].x[1]) < 16)   /* within 1/16 semitone of C4 */
            at = (i + 1u) * CTL;
    }
    check("acid: a slide reaches the next note in 40..90 ms", at >= FS * 40 / 1000 && at <= FS * 90 / 1000);
}

static void test_accent(void)                        /* ACC: louder and brighter than a plain note */
{
    track_t *t = acid();
    double plain, acc, bp, ba;
    play(t, 96, -24, buf, FS / 5);
    plain = rms(buf, FS / 5);
    bp = goertzel(buf, FS / 5, 65.4 * 8);
    t = acid();
    play(t, 127, -24, buf, FS / 5);
    acc = rms(buf, FS / 5);
    ba = goertzel(buf, FS / 5, 65.4 * 8);
    check("acid: accent is louder (> +2 dB)", acc > plain * 1.26);
    check("acid: accent opens the filter (the 8th harmonic > +3 dB)", ba > bp * 1.41);
}

static void test_self_osc(void)                      /* RESO 127, CUTOF at ~1 kHz: a ringing sine near the cutoff */
{
    track_t *t = acid();
    t->p[P_E0 + 3] = 127;
    t->p[P_E0 + 4] = 0;                              /* ENVMD 0: the cutoff stays */
    play(t, 96, -36, buf, N / 2);
    check("acid: RESO 127 self-oscillates (energy near the cutoff, the output bounded)",
          rms(buf + FS / 2, FS / 2) > 500.0 && peak(buf, N / 2) < 32767 * 4);
}

static void test_alias(void)                         /* a high saw (C7): little energy at an alias frequency */
{
    track_t *t = acid();
    double f0, al;
    t->p[P_E0 + 2] = 127;
    t->p[P_E0 + 3] = 0;
    play(t, 96, 36, buf, FS / 2);
    f0 = goertzel(buf, FS / 2, 2093.0);
    al = goertzel(buf, FS / 2, 44100.0 - 2093.0 * 11);   /* the 11th harmonic folded back */
    check("acid: a high saw is band-limited (the folded 11th < -30 dB of the fundamental)", al < f0 * 0.0316);
}

static void test_extremes(void)                      /* Review Focus 3: no overflow (UBSan), bounded */
{
    static const int16_t V[4] = {0, 1, 64, 127};
    uint32_t a, b, c, d, ok = 1;
    for (a = 0; a < 4u; a++)
        for (b = 0; b < 4u; b++)
            for (c = 0; c < 4u; c++)
                for (d = 0; d < 4u; d++) {
                    track_t *t = acid();
                    t->p[P_E0 + 2] = V[a];
                    t->p[P_E0 + 3] = V[b];
                    t->p[P_E0 + 4] = V[c];
                    t->p[P_E0 + 5] = V[d];
                    t->p[P_E0] = (int16_t)(d & 1u ? 24 : -24);
                    t->p[P_E0 + 6] = (int16_t)(c & 1u);
                    play(t, 127, (a & 1u) ? NT_MAX : NT_MIN, buf, FS / 10);
                    drum_play(t, 127, 0);                 /* an accent chain */
                    dm_render_n(t, buf, FS / 10);
                    if (peak(buf, FS / 10) >= 32767 * 4)
                        ok = 0;
                }
    check("acid: every extreme of CUTOF x RESO x ENVMD x ACCNT x note x TUNE stays bounded", ok);
}

static void test_level(void)                         /* the defaults' peak within +-3 dB of TOM's */
{
    track_t *t = acid();
    int32_t pa, pt;
    play(t, 96, -24, buf, FS / 4);
    pa = peak(buf, FS / 4);
    host_init();
    drum_set_model(&trk[0], DM_TOM);
    play(&trk[0], 96, 0, buf, FS / 4);
    pt = peak(buf, FS / 4);
    check("acid: the defaults' level is within +-3 dB of TOM's", pa > pt * 0.708 && pa < pt * 1.413);
}

int main(void)
{
    test_pitch();
    test_gate();
    test_slide();
    test_accent();
    test_self_osc();
    test_alias();
    test_extremes();
    test_level();
    printf(fails ? "acid: %d FAILED\n" : "acid: all checks passed\n", fails);
    return fails != 0;
}
```

  `dm_render_n(t, out, n)` renders a track's model voices only (no mix) into `out` in `CTL` blocks: if
  `drum_host.h` / `fm_test.c` already has such a helper, use its name; else add it to `tests/drum_host.h`:

```c
static void dm_render_n(track_t *t, int32_t *out, uint32_t n)   /* the track's model voices only, CTL blocks */
{
    uint32_t i, k;
    for (i = 0; i + CTL <= n; i += CTL)
        for (k = 0; k < NDV; k++)
            if (t->v[k].active)
                DMODELS[t->model].render(t, &t->v[k], out + i, CTL);
}
```

  In `tests/run_drum_tests.sh`, after `fm_test`, as `filter_test` is built:

```sh
cc -O1 -g -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/acid_test" tests/acid_test.c -lm
"$OUT/acid_test"
```

- [ ] **Step 2: Run** `sh tests/run_drum_tests.sh`. Expected: `acid_test.c` fails to compile (`DM_ACID`, `glide`,
  `gate` undeclared).

- [ ] **Step 3: The model hooks and the table.** `core.h` `dmodel_t`, after `weight`:

```c
    void (*glide)(struct track *t, dvoice_t *v);         /* ACID: the voice moves to the track's note, no restart */
    void (*gate)(struct track *t, dvoice_t *v, int open); /* ACID: 0 = the gate closes (release) */
```

  (every existing `DM_*_DEF` stays as it is: the trailing members are zero). `dmodels.c`: `#include "dm_acid.c" /*
  ACID: a 303-style bass line */` after `dm_fm.c`; `DM_ACID` after `DM_FM` in the enum; `DM_ACID_DEF` after
  `DM_FM_DEF` in `DMODELS`; `"ACID"` after `"FM"` in `N_MODEL`.

- [ ] **Step 4: The voice** `firmware/src/dm_acid.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* ACID (spec docs/superpowers/specs/2026-10-09-acid-design.md §4): a 303-style mono bass voice. A band-limited saw or
 * square (polyBLEP) into a 24 dB 4-pole ladder (zero-delay feedback, Q24, a cubic soft clip in the feedback path)
 * whose cutoff follows CUTOF + ENVMD x the filter envelope + the accent; the amp opens fast, holds while the gate is
 * open, releases in ~8 ms. ACC: more level and filter envelope (ACCNT), a shorter filter decay, and a slow smoother
 * that charges on consecutive accents (the 303's "wow"). Slide (glide hook): the pitch moves in the log domain over
 * ~60 ms, nothing restarts. The sequencer drives the gate (seq.c acid_step). One voice per track. */
#define ACID_ATT (QONE / 132)                           /* the amp's attack: ~3 ms linear to full */
#define ACID_REL 63000u                                 /* the amp's release per sample, Q16: ~8 ms to -60 dB */
#define ACID_GLIDE Q24(0.05)                            /* per block: ~95 % of a slide in ~60 ms (82 blocks) */
#define ACID_WOW 65300u                                 /* the accent smoother's decay per block, Q16: ~200 ms */
#define ACID_GAIN 3                                     /* the output x3: the defaults level with TOM's (test) */

static inline int32_t acid_clip(int32_t x)              /* x - 4/27 x^3, x held to +-1.5 (Q24): out +-1, smooth */
{
    x = qlim(x, -Q24(1.5), Q24(1.5));
    return x - qm(qm(qm(x, x), x), Q24(4.0 / 27.0));
}
static inline int32_t acid_blep(uint32_t ph, uint32_t inc)   /* polyBLEP residual at a wrap (Q24), ph / inc Q32 */
{
    int32_t t;
    if (ph < inc) {                                     /* just after the wrap */
        t = (int32_t)(((uint64_t)ph << 24) / inc);
        return t + t - qm(t, t) - QONE;
    }
    if (ph > 0xFFFFFFFFu - inc) {                       /* just before it */
        t = (int32_t)(((uint64_t)(ph - (0xFFFFFFFFu - inc)) << 24) / inc) - QONE;
        return qm(t, t) + t + t + QONE;
    }
    return 0;
}
static uint32_t acid_inc(int32_t p16)                   /* pitch in 1/16 semitone (0 = MIDI 0) -> increment */
{
    p16 = qlim(p16, 0, 127 * 16);
    return (uint32_t)(((uint64_t)qnote_inc(p16 >> 4) * pow2_q16(p16 & 15)) >> 16);
}
static int32_t acid_pitch(const track_t *t)             /* the target: C4 + the note + TUNE, 1/16 semitone */
{
    return (60 + t->nt + t->p[P_E0]) * 16;
}

/* ACID: TUNE DECAY CUTOF RESO, ENVMD ACCNT WAVE GATE */
static void acid_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    int32_t acc = v->vel > 110u ? clamp(p[5], 0, 127) * 258 : 0;   /* ACCNT (Q15) on an accented note */
    v->x[0] = v->x[1] = acid_pitch(t);
    v->x[2] = 1 | (p[6] ? 4 : 0);                         /* the gate open, the attack running; WAVE */
    v->x[3] = acc;
    v->ph[0] = 0;
    v->env[0] = 0;
    v->env[1] = ENV1;
    v->env[2] = qmin(v->env[2] + (acc << 9) / 2, ENV1);    /* the "wow" charges on accents (kept across notes) */
    v->k[1] = dk(acc ? clamp(p[1], 0, 127) / 3 : p[1]);  /* DECAY; an accent: shorter */
    v->f[0] = v->f[1] = v->f[2] = v->f[3] = 0;
}

static void acid_glide(track_t *t, dvoice_t *v)          /* slide: a new target, nothing restarts */
{
    const int16_t *p = &t->p[P_E0];
    v->x[1] = acid_pitch(t);
    if (v->vel > 110u) {                                 /* the slid-to note's accent still counts */
        v->x[3] = clamp(p[5], 0, 127) * 258;
        v->env[2] = qmin(v->env[2] + (v->x[3] << 9) / 2, ENV1);
    }
}

static void acid_gate(track_t *t, dvoice_t *v, int open)
{
    (void)t;
    v->x[2] = open ? (v->x[2] | 1) : (v->x[2] & ~1);
}

static void acid_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    const int16_t *p = &t->p[P_E0];
    uint32_t i, inc;
    int32_t cut, g, G, G2, G3, G4, k, inv, s0 = v->f[0], s1 = v->f[1], s2 = v->f[2], s3 = v->f[3];
    int32_t sq = v->x[2] & 4, amp = v->env[0];
    /* pitch: toward the target in the log domain */
    v->x[0] += (int32_t)(((int64_t)(v->x[1] - v->x[0]) * ACID_GLIDE) >> 24);
    if (abs(v->x[1] - v->x[0]) < 2)
        v->x[0] = v->x[1];
    inc = acid_inc(v->x[0]);
    /* cutoff in semitones over ~20 Hz: CUTOF + ENVMD x env + accent (env + wow), then the ladder's coefficients */
    cut = clamp(p[2], 0, 127) * 7 / 8 * 16 +
          (int32_t)(((int64_t)clamp(p[4], 0, 127) * 16 * 48 / 127 * env_q15(v->env[1])) >> 15) +
          (int32_t)(((int64_t)v->x[3] * 16 * 36 / 32767 * env_q15(v->env[1])) >> 15) +
          (env_q15(v->env[2]) * 16 * 12 >> 15);
    cut = qlim(cut, 0, 112 * 16);                          /* ~20 Hz .. ~13 kHz */
    g = qtan_acc(qlim((int32_t)(((uint64_t)acid_inc(cut + 16 * 16) << 24) >> 32), 1, Q24(0.45)) / 2);
    G = qdiv(g, QONE + g);
    G2 = qm(G, G);
    G3 = qm(G2, G);
    G4 = qm(G3, G);
    k = clamp(p[3], 0, 127) * (int32_t)(Q24(4.0) / 127);  /* RESO: 0 .. 4 (self-oscillation near 4) */
    inv = qdiv(QONE, QONE + qm(k, G4));
    for (i = 0; i < n; i++) {
        int32_t x, S, y, u, a;
        uint32_t ph = v->ph[0];
        if (sq) {                                        /* square: two saws half a cycle apart */
            x = (int32_t)(ph >> 8) - (int32_t)((ph + 0x80000000u) >> 8);
            x -= acid_blep(ph, inc) - acid_blep(ph + 0x80000000u, inc);
        } else {
            x = (int32_t)(ph >> 7) - QONE;                /* saw -1 .. +1 */
            x -= acid_blep(ph, inc);
        }
        v->ph[0] = ph + inc;
        S = qm(QONE - G, qm(G3, s0) + qm(G2, s1) + qm(G, s2) + s3);
        y = qm(qm(G4, x) + S, inv);
        u = acid_clip(x - qm(k, y));
        a = qm(u - s0, G); u = a + s0; s0 = u + a;       /* four TPT one-poles */
        a = qm(u - s1, G); u = a + s1; s1 = u + a;
        a = qm(u - s2, G); u = a + s2; s2 = u + a;
        a = qm(u - s3, G); u = a + s3; s3 = u + a;
        /* the amp: attack, hold while the gate is open, release */
        if (v->x[2] & 1) {
            if (!(v->x[2] & 2)) {
                amp += ACID_ATT;
                if (amp >= ENV1) {
                    amp = ENV1;
                    v->x[2] |= 2;
                }
            }
        } else {
            amp = mulq16(amp, ACID_REL);
        }
        a = (int32_t)(((int64_t)u * (ENV1 + (v->x[3] << 9) / 2)) >> 24);   /* accent: up to +50 % level */
        dm_put(v, out, i, (int32_t)(((int64_t)(a >> 9) * (amp >> 9)) >> 15) * ACID_GAIN);
        env_step(v, 1);                                  /* the filter envelope: DECAY */
    }
    v->f[0] = s0; v->f[1] = s1; v->f[2] = s2; v->f[3] = s3;
    v->env[0] = amp;
    v->env[2] = mulq16(v->env[2], ACID_WOW);
    if (!(v->x[2] & 1) && amp < ENV_END)
        dm_end(v);
}

#define DM_ACID_DEF {"ACID", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 50), \
    PD("CUTOF", F_INT, 0, 127, 40), PD("RESO", F_INT, 0, 127, 64), PD("ENVMD", F_INT, 0, 127, 64), \
    PD("ACCNT", F_INT, 0, 127, 64), PE("WAVE", N_ACIDWAVE, 0), PD("GATE", F_INT, 0, 127, 56)}, acid_trigger, \
    acid_render, 1, acid_glide, acid_gate}
static const char *const N_ACIDWAVE[2] = {"SAW", "SQR"};
```

  This is the starting point, not the tuned voice: the constants (`ACID_GAIN`, the cutoff map, `k`'s scale, the
  glide rate) are set by the tests, and the TPT ladder's algebra must be checked against its derivation (y4 = (G⁴x +
  S) / (1 + kG⁴), S = (1 − G)(G³s₀ + G²s₁ + Gs₂ + s₃), each one-pole v = (u − s)G, y = v + s, s ← y + v) before the
  numbers are tuned. `PE` / `PD` as `dm_fm.c` uses them (`N_ACIDWAVE` is used only where `DMODELS` expands the
  macro, after this file). Keep the
  per-block 64-bit divides (`qdiv`, `qtan_acc`) out of the sample loop; the sample loop has no division but the
  polyBLEP's (only within one increment of a wrap: twice per cycle).

- [ ] **Step 5: Run and tune.** `sh tests/run_drum_tests.sh` (expected: `acid_test` builds; then tune until every
  line is `ok`): record in the ledger each constant changed and why. A test threshold that cannot be met is a
  finding for the user, not a change to the test (Global Constraints). Then `GOLDEN_UPDATE=1` for the ACID line only:
  `GOLDEN_UPDATE=1 build/host/drum_test >/dev/null; git diff tests/drum_golden.txt` shows one added line `ACID …`
  and nothing else. Change Task 2's `if (0)` skip to `mi == DM_ACID`.

- [ ] **Step 6: The cost.** `FELUCCA_BLE= ./build.sh > $T/b.log` (a default build; no package needed), then
  `python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt > $T/tb.log; grep -i acid $T/tb.log`
  (expected: `acid_render` reported as new, no budget yet) and the host BENCH figure: `build/host/bench_host | grep
  -i acid` (the 8 x ACID case: instructions per sample). Ledger both numbers; they go to the user in Task 5.

- [ ] **Step 7: Commit**

```bash
git add firmware/src/dm_acid.c firmware/src/core.h firmware/src/dmodels.c tests/acid_test.c tests/drum_host.h tests/run_drum_tests.sh tests/drum_golden.txt tests/drum_test.c
git commit -m "ACID: the 303-style voice (polyBLEP saw / square, 24 dB ZDF ladder, gate, accent, slide)"
```

### Task 4: The sequencer plays ACID: gate, slide, tie, rests

**Files:**
- Modify: `firmware/src/core.h` (`track_t`: `gate_n`, `slide_in`), `firmware/src/seq.c` (`acid_step`, the gate
  countdown, `rat_due`, stop), `firmware/src/drum_core.c` (`drum_cut` clears the gate state)
- Test: `tests/acid_test.c` (sequencer tests)

**Interfaces:**
- Consumes: Task 3's hooks, Task 1's `slide` / `tie`, Task 2's `drum_play`.
- Produces: in `track_t`: `uint32_t gate_n;` (samples until the gate closes; `GATE_HELD` = `0xFFFFFFFFu` while the
  next step decides; 0 = closed) and `uint8_t slide_in;` (the previous step slides into this one); `static void
  acid_step(track_t *t, const step_t *s, const step_t *next, uint32_t len_s)`; `static uint32_t acid_gate_len(const
  track_t *t, uint32_t len)` (GATE: 10..100 % of `len`).

- [ ] **Step 1: Failing tests** in `tests/acid_test.c`, a small sequencer driver and the cases (before `main`, and
  their calls in `main`):

```c
/* the sequencer on track 0 (ACID), 16th steps at 120 BPM: run n steps, render the track's voices */
static void seq_run(track_t *t, uint32_t steps, int32_t *out)
{
    uint32_t i, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0) * steps;
    for (i = 0; i + CTL <= len; i += CTL) {
        seq_tick(t, CTL);
        dm_render_n(t, out + i, CTL);
    }
}
static track_t *acid_seq(void)
{
    track_t *t = acid();
    memset(t->step, 0, sizeof t->step);
    t->p[P_SLEN] = 16;
    t->p[P_SRC] = 0;
    song.playing = 1;
    t->seq_pos = 0x7FFFFFFFu;                         /* PLAY: step 0 next block */
    t->seq_cnt = (uint32_t)-1;
    t->gate_n = 0;
    t->slide_in = 0;
    return t;
}

static void test_seq_gate(void)                      /* a plain note: the gate closes at GATE (50 %) of the step */
{
    track_t *t = acid_seq();
    uint32_t len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    t->step[0].on = 1;
    seq_run(t, 1, buf);
    check("acid seq: the gate closes at GATE of the step, the note ends in that step",
          rms(buf + len * 3 / 4, len / 8) < 20.0 && rms(buf + len / 8, len / 4) > 200.0);
}

static void test_seq_slide(void)                     /* SLIDE on step 0: step 1 glides, the envelopes not restarted */
{
    track_t *t = acid_seq();
    int32_t fe;
    t->step[0].on = 1;
    t->step[0].slide = 1;
    t->step[0].nt = -12;
    t->step[1].on = 1;
    t->step[1].nt = 0;
    seq_run(t, 1, buf);
    fe = t->v[0].env[1];
    seq_run(t, 1, buf);
    check("acid seq: a slide keeps the note sounding into the next step (no restart, the gate held)",
          t->v[0].active && t->v[0].env[1] < fe && t->v[0].x[1] == 60 * 16);
}

static void test_seq_tie(void)                       /* TIE: no restart, the same pitch, the gate held through it */
{
    track_t *t = acid_seq();
    int32_t fe, p;
    uint32_t len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    t->step[0].on = 1;
    t->step[1].tie = 1;
    t->step[1].nt = 12;                                /* a TIE's own note is ignored */
    seq_run(t, 1, buf);
    fe = t->v[0].env[1];
    p = t->v[0].x[1];
    seq_run(t, 1, buf);
    check("acid seq: a TIE holds the note (no restart, the same pitch, sounding at its end)",
          t->v[0].env[1] < fe && t->v[0].x[1] == p && rms(buf + len * 3 / 4, len / 8) > 200.0);
}

static void test_seq_rest(void)                      /* a rest after a held gate closes it */
{
    track_t *t = acid_seq();
    t->step[0].on = 1;
    t->step[0].slide = 1;                              /* held into step 1 */
    seq_run(t, 2, buf);
    check("acid seq: a rest after a slide closes the gate", !(t->v[0].x[2] & 1) && t->gate_n == 0u);
}

static void test_seq_prob_tie(void)                  /* Review Focus 4 */
{
    track_t *t = acid_seq();
    t->step[0].on = 1;
    t->step[0].cond = 1;                               /* PROB 0 %: never plays */
    t->step[1].tie = 1;
    seq_run(t, 2, buf);
    check("acid seq: a TIE after a skipped step plays nothing", !t->v[0].active && peak(buf, FS / 4) == 0);
    t = acid_seq();
    t->step[0].on = 1;
    t->step[0].slide = 1;
    t->step[1].on = 1;
    t->step[1].cond = 1;                               /* the slid-to step skipped */
    seq_run(t, 2, buf);
    check("acid seq: a slide into a skipped step closes the gate", !(t->v[0].x[2] & 1));
    t = acid_seq();
    t->step[0].on = 1;
    t->step[0].rat = 1;                                /* 2 hits */
    seq_run(t, 1, buf);
    check("acid seq: a ratcheted step restarts the note", t->rat_k == 2u);
}

static void test_seq_stop_closes(void)              /* Review Focus 1: STOP / a model change: no hanging note */
{
    track_t *t = acid_seq();
    t->step[0].on = 1;
    t->step[0].slide = 1;
    seq_run(t, 1, buf);
    seq_stop_all();                                    /* the transport stops */
    memset(buf, 0, FS / 10 * sizeof *buf);
    dm_render_n(t, buf, FS / 10);
    check("acid seq: STOP closes an open gate (the note ends)", rms(buf + FS / 20, FS / 20) < 20.0);
    t = acid_seq();
    t->step[0].on = 1;
    t->step[0].slide = 1;
    seq_run(t, 1, buf);
    t->p[P_MODEL] = DM_K808;
    model_follow(t);                                   /* a model change cuts the voices */
    check("acid seq: a model change cuts the note and clears the gate", !t->v[0].active && t->gate_n == 0u &&
          !t->slide_in);
}

static void test_seq_drum_tie(void)                  /* a drum track: a TIE step plays nothing */
{
    track_t *t = acid_seq();
    t->p[P_MODEL] = DM_K808;
    t->model = DM_K808;
    t->step[0].tie = 1;
    seq_run(t, 1, buf);
    check("seq: a TIE step on a drum track plays nothing", !t->v[0].active && !t->v[1].active);
}
```

  `seq_stop_all()` is the name of the function the STOP path should call (`seq.c`): if the transport's stop already
  has a function, use its name and add the gate closing there.

- [ ] **Step 2: Run** `sh tests/run_drum_tests.sh`. Expected: compile errors (`gate_n`, `slide_in`, `seq_stop_all`).

- [ ] **Step 3: Implement.**
  - `core.h` `track_t`, after `nt`: `uint32_t gate_n; /* ACID: samples until its gate closes, GATE_HELD while the next
    step decides, 0 closed */` and `uint8_t slide_in; /* ACID: the previous step slides into this one */`; `#define
    GATE_HELD 0xFFFFFFFFu`.
  - `seq.c`, before `step_fire`:

```c
/* ACID's GATE knob: 10..100 % of len (samples) */
static uint32_t acid_gate_len(const track_t *t, uint32_t len)
{
    return len / 100u * (10u + (uint32_t)clamp(t->p[P_E0 + 7], 0, 127) * 90u / 127u);
}

/* a gated model's step (spec §3): TIE holds the sounding note, a slid-into ON step glides, any other ON step
 * restarts; the gate stays open into the next step when this one slides or the next is a TIE */
static void acid_step(track_t *t, const step_t *s, const step_t *next, uint32_t len_s)
{
    const dmodel_t *m = trk_model(t);
    dvoice_t *v = &t->v[0];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, held = v->active && (v->x[2] & 1);
    int plays = s->on && !s->tie && step_plays(t, s, len);
    t->rat_n = 0;
    if (s->tie) {
        if (!held)
            return;
        t->gate_n = s->slide || next->tie ? GATE_HELD : acid_gate_len(t, len_s);
        return;
    }
    if (!plays) {                                      /* a rest, or PROB skipped it */
        if (held)
            m->gate(t, v, 0);
        t->gate_n = 0;
        t->slide_in = 0;
        return;
    }
    t->rat_vel = (uint8_t)(s->acc ? 127u : 96u);
    if (held && t->slide_in) {
        t->nt = (int8_t)clamp(s->nt, NT_MIN, NT_MAX);
        v->vel = t->rat_vel;
        m->glide(t, v);
    } else {
        drum_play(t, t->rat_vel, s->nt);
    }
    t->slide_in = s->slide;
    t->rat_n = (uint8_t)(s->rat < 3u ? s->rat + 1u : 4u);
    t->rat_k = 1;
    t->rat_len = len_s;
    t->gate_n = s->slide || next->tie ? GATE_HELD : acid_gate_len(t, t->rat_n > 1u ? len_s / t->rat_n : len_s);
}
```

  - `step_fire`: at its top, `if (trk_model(t)->gate && !t->p[P_SRC]) { uint32_t len = t->p[P_SLEN] > 0 ?
    (uint32_t)t->p[P_SLEN] : 1u; acid_step(t, s, &seq_steps(t)[(t->seq_idx + 1u) % len], len_s); return; }`; a drum
    track's TIE step: `if (... || !s->on || s->tie || !step_plays(...)) return;`.
  - `rat_due`: for a gated model, each roll hit `drum_play(t, t->rat_vel, t->nt)` and `t->gate_n =
    acid_gate_len(t, t->rat_len / t->rat_n)` (the last hit keeps `GATE_HELD` when the step slides: check
    `t->slide_in`).
  - the gate countdown, at the top of `seq_tick` before `if (!song.playing) return;`:

```c
    if (t->gate_n && t->gate_n != GATE_HELD) {        /* ACID: the gate closes this block */
        if (t->gate_n <= n) {
            t->gate_n = 0;
            if (trk_model(t)->gate && t->v[0].active)
                trk_model(t)->gate(t, &t->v[0], 0);
        } else {
            t->gate_n -= n;
        }
    }
```

  - STOP: the transport's stop (find where `song.playing` becomes 0 in `seq.c` / `ui_input.c`; the existing stop
    function if any, else a new `static void seq_stop_all(void)` called there) closes every gated track's gate:
    `gate(t, &t->v[0], 0)`, `gate_n = 0`, `slide_in = 0`. A song's row change (`song.c`, `chain.cut`) does the same
    for the tracks it restarts.
  - `drum_core.c` `drum_cut(t)` (the model change, choke, mute's cut): `t->gate_n = 0; t->slide_in = 0;`.

- [ ] **Step 4: Run** `sh tests/run_drum_tests.sh`. Expected: every acid seq line `ok`, all old lines `ok` (the golden
  hashes unchanged: drum steps never reach `acid_step`). Then `DRUM_PACKAGE=1 ./build.sh` and `sh tests/run_tests.sh`.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/core.h firmware/src/seq.c firmware/src/drum_core.c firmware/src/song.c tests/acid_test.c
git commit -m "ACID: the sequencer's gate, SLIDE, TIE and rests; STOP and a model change end the note"
```

### Task 5: Cost, budgets, BENCH, WAVs — the user's numbers

**Files:**
- Modify: `firmware/src/bench.c` (a "heavy+2acid" case), `tests/drumsim.c` (ACID demos), `tests/target_budget.txt`
  and `tests/drum_cost_ref.txt` (only with the user's numbers), `docs/PERFORMANCE.md` (the measured costs)

- [ ] **Step 1: BENCH case.** `bench.c`: `BENCH_MORE_OFF` / `BENCH_MORE_NAME` gain `"heavy+2acid"`: the heavy kit with
  tracks 7 and 8 switched to ACID (defaults, a 16-step line with slides and accents written into their steps) —
  follow how `BH_FILTER` / `BH_COMP` flags change the heavy kit (a new flag `BH_ACID`). `tests/bench_host.c` runs it:
  `build/host/bench_host | grep acid`. Ledger the host instructions per sample.

- [ ] **Step 2: The demos.** `tests/drumsim.c`: three ACID renders through `write_demo` (16 bars each at the default
  tempo, track 0 ACID, the others muted): `acid_line` (a 303-style line: notes, accents, slides, ties, rests),
  `acid_reso` (the same line, RESO 0 → 127 across the bars: one knob step per bar), `acid_sqr` (WAVE SQR). Run
  `build/host/drumsim build/drum_renders`; copy the three WAVs to `$T/acid/`.

- [ ] **Step 3: Stop for the user (budgets and sound).** Report: `acid_render`'s target cost (Task 3 step 6), the BENCH
  8 x ACID and heavy+2acid figures against the 1566 limit and the realistic heavy kit, the proposed
  `target_budget.txt` line (`acid_render <measured>`) and any `drum_cost_ref.txt` change, and send the three WAVs
  (SendUserFile). Wait for the user's approval of the numbers and their verdict by ear; ledger both verbatim.

- [ ] **Step 4: Apply the approved numbers**: `acid_render <n>` in `tests/target_budget.txt` (by hand, the user's
  number); `docs/PERFORMANCE.md`: ACID's measured costs. Run both suites (`DRUM_PACKAGE=1 ./build.sh` first). Expected:
  all pass.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/bench.c tests/bench_host.c tests/drumsim.c tests/target_budget.txt tests/drum_cost_ref.txt docs/PERFORMANCE.md
git commit -m "ACID: BENCH heavy+2acid, demo renders, the approved budget lines"
```
