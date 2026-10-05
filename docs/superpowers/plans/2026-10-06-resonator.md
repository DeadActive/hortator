# RESON (per-track resonator insert) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every track gets a RESON insert (STRNG / PIPE / CHORD comb resonators) between the engine and DIST, with
its own pages in FX, LFO destinations (a smooth fine pitch for `R.TUN`), a CHORD cap of 2 tracks and project
format FDR5.

**Architecture:** `firmware/src/reson.c` processes a track's rendered block in place inside `mix_part` (before
`track_dist`); its delay lines are one fixed 1352-sample `int16_t` buffer per track in `.pool`, split into 4
lines for CHORD. Per-track state lives in `track_t` (`reson_t rs`, `int32_t rfine`); lookup tables come from a
generator script; seven new per-track parameters are appended after the LFOs.

**Tech Stack:** C (single translation unit `firmware/src/felucca.c`), JieLi pi32v2 target (clang 4, integer
only), host tests in C (`tests/*.c`), Python 3 table generator.

**Spec:** `docs/superpowers/specs/2026-10-06-resonator-design.md`

## Global Constraints

- Never brick the FM-1: never run `tools/fm1_install.py`, the web installer or the M-VAVE updater. Never install
  mido / python-rtmidi.
- Packages only with `DRUM_PACKAGE=1 ./build.sh` → `build/felucca-UNTESTED.fwsc` (Docker; on "exec format error"
  run it again, up to 5 times). Put the private venv first in `PATH` for the build and `tests/run_tests.sh`
  (`export PYTHON=/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin/python PATH="/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin:$PATH"`;
  Homebrew python3.14 lacks Pillow).
- Frozen files (`tools/check_untouched.py`): `hal/`, `loader/`, `ota.c`, `usb.c`, `crt0.S`, `app.ld`,
  `storage.c`, `main.c` outside `felucca_init()` and the boot titles, the last 5 lines of `core.h`.
- Integer only on the target: no float, no 64-bit division (64-bit multiply is fine); bounded loops.
- H2 (frozen code identical to upstream) breaks when new small file-scope globals appear (global merging moves
  `ota_wire`): new state goes into `track_t` / `song`; large buffers go into `__attribute__((section(".pool")))`.
- Cost: the realistic heavy kit stays within `ref` and the extreme case within `extreme_max`
  (`tests/drum_cost_ref.txt`). If the RESON worst case goes over, **stop and ask the user with the numbers**.
  Never change an existing budget / threshold / fidelity metric without asking; the new `reson_block` target
  budget line is added by hand from its first measurement (spec §6).
- MODEL OFF is bit-identical to today (`tests/drum_golden.txt` unchanged).
- Labels ≤ 5 characters. New files carry `Drum machine fork: 2026 DEADACTIVE` in their header.
- Host test build flags: `F="-O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src"`.

## Review Focus

1. Extreme pitch: TUNE C1 / C7 + a chord's top interval + `R.TUN` ±2 octaves + STRCT / TONE at their ends —
   every line length stays inside its segment; no read or write outside `rs_buf` (ASan run in `boot_test`).
2. MODEL changed or set OFF while the ring sounds: the old ring fades out within a block (no click), and no stale
   ring comes back when RESON is switched on again without a hit.
3. Sustained loud input at DECAY max (a noisy hit every 1/32 at 240 BPM, DIST after RESON): output bounded, and
   the ring dies away after the input stops.
4. A muted COMP source with RESON (ghost key): its ring keys the compressor but is not heard in the mix.
5. An LFO on `R.STR` while MODEL is CHORD sweeps the chord types under a ringing chord: bounded, no blow-up.

---

### Task 1: RESON parameters, formats, tables and project format FDR5

**Files:**
- Create: `tools/gen_reson_tables.py`, `firmware/src/reson_tables.h` (generated, committed)
- Modify: `firmware/src/core.h` (P enum, F enum, RS enum, `RS_NCHORD`)
- Modify: `firmware/src/params.c` (names, TP entries, `RS_CHORD_DESC`, `track_desc`, formats)
- Modify: `firmware/src/project.c` (FDR5, FDR4 conversion, CHORD cap on load)
- Test: `tests/drum_test.c` (formats), `tests/boot_test.c` (FDR4 converts)

**Interfaces:**
- Produces: `P_RMODEL, P_RTUNE, P_RDECAY, P_RMIX, P_RTONE, P_RSTRCT, P_RPOS` (in this order, after the LFOs);
  `enum { RS_OFF, RS_STRNG, RS_PIPE, RS_CHORD, RS_NMODEL }`; `#define RS_NCHORD 20`; formats `F_NOTEO`,
  `F_RDECAY`; tables `RS_PER[2049]` (period, Q8 samples, per 1/16 semitone, MIDI 0..128), `RS_DK[128]`
  (log2 decay per sample, Q24), `RS_T60_MS10[128]`, `RS_TONE_K[128]` (Q15), `RS_EXP2N[256]` (Q15 2^(-i/256));
  `N_RCHORD[]` (0-terminated), `RS_CHORD_DESC`; `PROJ_MAGIC` = "FDR5", `PROJ_MAGIC_V4` = "FDR4",
  `project_v4_t`.

- [ ] **Step 1: Write the table generator and generate the header**

`tools/gen_reson_tables.py`:

```python
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""firmware/src/reson_tables.h: RESON (reson.c) at 44.1 kHz. The loop period of a MIDI note in 1/16 semitone
(Q8 samples), the DECAY knob's 60 dB time (10 ms .. 10 s) and its log2 decay per sample (Q24), the TONE knob's
one-pole damping coefficient (Q15, 400 Hz .. 20 kHz, 127 = transparent), 2^(-i/256) (Q15) for the loop gain.
   tools/gen_reson_tables.py OUT_H"""
import math
import sys

FS = 44100


def table(ctype, name, values, per=8):
    out = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for r in range(0, len(values), per):
        out.append("    " + ", ".join(f"{v}u" for v in values[r:r + per]) + ",")
    out.append("};")
    return out


def main():
    per = [round(256 * FS / (440 * 2 ** ((n / 16 - 69) / 12))) for n in range(128 * 16 + 1)]
    t60 = [0.010 * 1000 ** (v / 127) for v in range(128)]
    dk = [round(2 ** 24 * 3 * math.log2(10) / (t * FS)) for t in t60]
    fc = [400 * (20000 / 400) ** (v / 127) for v in range(128)]
    tone = [min(32767, round(32768 * (1 - math.exp(-2 * math.pi * f / FS)))) for f in fc]
    tone[127] = 32767
    lines = ["/* Generated by tools/gen_reson_tables.py: do not edit. RESON (reson.c) at 44.1 kHz. */"]
    lines += table("uint32_t", "RS_PER", per)
    lines += table("uint32_t", "RS_DK", dk)
    lines += table("uint32_t", "RS_T60_MS10", [round(t * 10000) for t in t60])
    lines += table("uint16_t", "RS_TONE_K", tone)
    lines += table("uint16_t", "RS_EXP2N", [round(32767 * 2 ** (-i / 256)) for i in range(256)])
    open(sys.argv[1], "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
```

Run: `python3 tools/gen_reson_tables.py firmware/src/reson_tables.h && grep -c "static const" firmware/src/reson_tables.h`
Expected: `5`

- [ ] **Step 2: Write the failing format tests**

In `tests/drum_test.c`, before `/* PROB codes (params.c)`:

```c
/* RESON parameters (params.c): TUNE as a note, DECAY as a time, STRCT named CHORD (with chord names) on CHORD */
static void test_reson_params(void)
{
    char v[12];
    const char *u;
    host_init();
    param_format(&TP[P_RTUNE], 48, v, &u);
    check("RESON TUNE 48 = C3", str_eq(v, "C3"));
    param_format(&TP[P_RTUNE], 96, v, &u);
    check("RESON TUNE 96 = C7", str_eq(v, "C7"));
    param_format(&TP[P_RTUNE], 25, v, &u);
    check("RESON TUNE 25 = C#1", str_eq(v, "C#1"));
    param_format(&TP[P_RDECAY], 0, v, &u);
    check("RESON DECAY 0 = 10 ms", str_eq(v, "10") && str_eq(u, "ms"));
    param_format(&TP[P_RDECAY], 127, v, &u);
    check("RESON DECAY 127 = 10.0 s", str_eq(v, "10.0") && str_eq(u, "s"));
    check("RESON defaults: OFF, C3, MIX 50 %, STRCT 0",
          trk[0].p[P_RMODEL] == RS_OFF && trk[0].p[P_RTUNE] == 48 && trk[0].p[P_RMIX] == 64 && trk[0].p[P_RSTRCT] == 0);
    check("RESON STRCT on STRNG is STRCT", str_eq(track_desc(&trk[0], P_RSTRCT)->label, "STRCT"));
    trk[0].p[P_RMODEL] = RS_CHORD;
    param_format(track_desc(&trk[0], P_RSTRCT), 3 * 128 / RS_NCHORD + 1, v, &u);
    check("RESON STRCT on CHORD is CHORD, with chord names (MAJ)",
          str_eq(track_desc(&trk[0], P_RSTRCT)->label, "CHORD") && str_eq(v, "MAJ"));
    param_format(track_desc(&trk[0], P_RSTRCT), 127, v, &u);
    check("RESON CHORD 127 = CLUST", str_eq(v, "CLUST"));
}
```

Add `    test_reson_params();` to `main()` after `    test_mute_next_bar();`.

In `tests/boot_test.c`, after `fdr3_converts`:

```c
/* an FDR4 record (LFOs, no RESON) loads: its parameters; RESON OFF at the defaults */
static int fdr4_converts(void)
{
    project_v4_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V4;
    v.size = sizeof v;
    for (i = 0; i < G_COUNT; i++)
        v.g[i] = song.g[i];
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_RMODEL; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[2].p[P_LFO1 + LF_DEST] = 9;
    v.t[2].step[5].on = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 3u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    trk[2].p[P_RMODEL] = RS_PIPE;                    /* the live state differs: the load replaces it */
    project_load(3);
    return trk[2].p[P_LFO1 + LF_DEST] == 9 && trk[2].step[5].on && trk[2].p[P_RMODEL] == RS_OFF &&
           trk[2].p[P_RTUNE] == TP[P_RTUNE].def && trk[2].p[P_RDECAY] == TP[P_RDECAY].def;
}
```

and in `main()` after the FDR3 check:

```c
    check("project: an FDR4 record loads with RESON off", fdr4_converts());
```

- [ ] **Step 3: Run the tests to see them fail**

Run: `cc $F -o build/host/drum_test tests/drum_test.c -lm 2>&1 | head -5`
Expected: compile errors: `P_RTUNE` / `RS_CHORD` undeclared (the feature is missing).

- [ ] **Step 4: Implement the parameters and formats**

`firmware/src/core.h` — the format enum gains two formats at its end:

```c
    F_ONOFF, F_OCT, F_STEPS, F_CTHR, F_CRAT, F_CATK, F_CREL, F_CMKUP, F_LRATE1, F_LRATE2, F_LDEST, F_LPHASE,
    F_NOTEO, F_RDECAY
```

after the `enum { LT_FREE, LT_HIT, LT_PLAY };` line:

```c
enum { RS_OFF, RS_STRNG, RS_PIPE, RS_CHORD, RS_NMODEL };   /* RESON models (reson.c); modal models come after CHORD */
#define RS_NCHORD 20                                 /* CHORD types (reson.c RS_CHORD_IV, params.c N_RCHORD) */
```

and the per-track enum's end becomes:

```c
    P_LFO1,                      /* LFO 1: LF_N knobs (lfo.c) */
    P_LFO2 = P_LFO1 + LF_N,      /* LFO 2 */
    P_RMODEL = P_LFO2 + LF_N,    /* RESON (reson.c): MODEL TUNE DECAY MIX, TONE STRCT POS */
    P_RTUNE, P_RDECAY, P_RMIX, P_RTONE, P_RSTRCT, P_RPOS,
    P_COUNT
```

`firmware/src/params.c` — names after `N_LSYNC`:

```c
static const char *const N_RMODEL[] = {"OFF", "STRNG", "PIPE", "CHORD"};
static const char *const N_RCHORD[RS_NCHORD + 1] = {"OCT", "5TH", "4TH", "MAJ", "MIN", "SUS2", "SUS4", "DIM", "AUG",
                                                   "MAJ6", "MIN6", "MAJ7", "MIN7", "DOM7", "M7b5", "DIM7", "7SUS4",
                                                   "ADD9", "QUART", "CLUST", 0};
```

in `TP[]` after `LFO_TP(P_LFO2, F_LRATE2),`:

```c
    [P_RMODEL] = PE("MODEL", N_RMODEL, 0),
    [P_RTUNE] = PD("TUNE", F_NOTEO, 24, 96, 48),
    [P_RDECAY] = PD("DECAY", F_RDECAY, 0, 127, 72),
    [P_RMIX] = PD("MIX", F_PCT, 0, 127, 64),
    [P_RTONE] = PD("TONE", F_PCT, 0, 127, 100),
    [P_RSTRCT] = PD("STRCT", F_INT, 0, 127, 0),
    [P_RPOS] = PD("POS", F_PCT, 0, 127, 64),
```

after `GP_ACT`:

```c
/* RESON STRCT on CHORD: the chord type, 0..127 split evenly over N_RCHORD (reson.c rs_chord) */
static const param_desc_t RS_CHORD_DESC = {"CHORD", F_INT, 0, 127, 0, N_RCHORD, 0};
```

`track_desc` becomes:

```c
static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7)
        return &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS].edit[id - P_E0];
    if (id == P_RSTRCT && t->p[P_RMODEL] == RS_CHORD)
        return &RS_CHORD_DESC;
    return &TP[id];
}
```

next to `#include "lfo_tables.h"`:

```c
#include "reson_tables.h"
```

and two cases in `param_format` before `case F_CTHR:`:

```c
    case F_NOTEO:                                     /* a MIDI note as name + octave (60 = C4): "C3", "F#5" */
        str_cpy(val, N_NOTE[(uint32_t)clamp(v, 0, 127) % 12u], 6);
        fmt_int(val + str_len(val), clamp(v, 0, 127) / 12 - 1);
        break;
    case F_RDECAY:
        fmt_ms10(val, unit, RS_T60_MS10[clamp(v, 0, 127)]);
        break;
```

- [ ] **Step 5: Implement FDR5 in `firmware/src/project.c`**

Magics:

```c
#define PROJ_MAGIC 0x35524446u                 /* "FDR5": + RESON per track */
#define PROJ_MAGIC_V4 0x34524446u              /* "FDR4": LFO projects, converted on load */
#define PROJ_MAGIC_V3 0x33524446u              /* "FDR3": M3 projects, converted on load */
```

(update the file's header comment: `Format "FDR5" … (… the LFOs, RESON). … "FDR3" and "FDR4" records … are converted
on load.`)

After `project_v3_t`:

```c
/* the LFO format: the parameters before P_RMODEL */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_RMODEL]; step_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v4_t;
static union { project_v1_t v1; project_v2_t v2; project_v3_t v3; project_v4_t v4; } proj_old;
```

(replacing the old `proj_old` union line). `proj_from_old` becomes:

```c
/* an old record (proj_old, version ver) -> q: what it has; the rest at the defaults (v1: PROB 100 %, 1 hit,
 * SRC STEP, Grids; v1 / v2: COMP off, DUCK off; v1..v3: the LFOs off; all: RESON off) */
static void proj_from_old(project_t *q, uint32_t ver)
{
    uint32_t i, k;
    uint32_t ng = ver == 1u ? (uint32_t)G_GMODE : ver == 2u ? (uint32_t)G_CSRC : (uint32_t)G_COUNT;
    uint32_t np = ver == 1u ? (uint32_t)P_SRC : ver == 2u ? (uint32_t)P_DUCK : ver == 3u ? (uint32_t)P_LFO1 : (uint32_t)P_RMODEL;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    for (i = 0; i < G_COUNT; i++)
        q->g[i] = i >= ng ? GP[i].def
                : ver == 1u ? proj_old.v1.g[i] : ver == 2u ? proj_old.v2.g[i] : ver == 3u ? proj_old.v3.g[i] : proj_old.v4.g[i];
    q->sel = ver == 1u ? proj_old.v1.sel : ver == 2u ? proj_old.v2.sel : ver == 3u ? proj_old.v3.sel : proj_old.v4.sel;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++)
            q->t[k].p[i] = i >= np ? TP[i].def
                         : ver == 1u ? proj_old.v1.t[k].p[i] : ver == 2u ? proj_old.v2.t[k].p[i]
                         : ver == 3u ? proj_old.v3.t[k].p[i] : proj_old.v4.t[k].p[i];
        for (i = 0; i < NSTEP; i++) {
            if (ver == 1u) {
                q->t[k].step[i].on = proj_old.v1.t[k].step[i].on;
                q->t[k].step[i].acc = proj_old.v1.t[k].step[i].acc;
            } else {
                q->t[k].step[i] = ver == 2u ? proj_old.v2.t[k].step[i]
                                : ver == 3u ? proj_old.v3.t[k].step[i] : proj_old.v4.t[k].step[i];
            }
        }
    }
    q->sum = proj_sum(q);
}
```

`proj_fetch`'s first lines become:

```c
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), q, sizeof *q);
    if (n == (int)sizeof proj_old.v1 || n == (int)sizeof proj_old.v2 || n == (int)sizeof proj_old.v3 ||
        n == (int)sizeof proj_old.v4) {
        uint32_t ver = n == (int)sizeof proj_old.v1 ? 1u : n == (int)sizeof proj_old.v2 ? 2u
                     : n == (int)sizeof proj_old.v3 ? 3u : 4u, hdr[2], sum;
        memcpy(&proj_old, q, (uint32_t)n);
        memcpy(hdr, &proj_old, sizeof hdr);
        memcpy(&sum, (const uint8_t *)&proj_old + n - 4, 4);
        if (hdr[0] == (ver == 1u ? PROJ_MAGIC_V1 : ver == 2u ? PROJ_MAGIC_V2 : ver == 3u ? PROJ_MAGIC_V3 : PROJ_MAGIC_V4) &&
            hdr[1] == (uint32_t)n && sum == proj_hash(&proj_old, (uint32_t)n - 4u)) {
```

In `project_load`, before `song.sel = …`:

```c
    for (k = 0, i = 0; k < NTRK; k++)                   /* RESON CHORD on 2 tracks at most: the first two keep it */
        if (trk[k].p[P_RMODEL] == RS_CHORD && ++i > 2u)
            trk[k].p[P_RMODEL] = RS_STRNG;
```

- [ ] **Step 6: Run the tests to see them pass**

Run: `cc $F -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "RESON|^FAIL|drum_test:"`
Expected: every `RESON …` line `ok`, `drum_test: all passed`.

Run: `cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all $F -o build/host/boot_test tests/boot_test.c -lm && build/host/boot_test | grep -E "FDR4|^FAIL|boot_test:"`
Expected: `ok    project: an FDR4 record loads with RESON off`, `boot_test: all passed`.

Run: `sh tests/run_drum_tests.sh 2>&1 | tail -2`
Expected: `ALL DRUM HOST TESTS PASSED`

- [ ] **Step 7: Commit**

```bash
git add tools/gen_reson_tables.py firmware/src/reson_tables.h firmware/src/core.h firmware/src/params.c firmware/src/project.c tests/drum_test.c tests/boot_test.c
git commit -m "reson: parameters, formats, tables, project format FDR5 (FDR4 loads with RESON off, CHORD on 2 tracks at most)"
```

---

### Task 2: The resonator (reson.c) in the track chain

**Files:**
- Create: `firmware/src/reson.c`
- Modify: `firmware/src/core.h` (`reson_t`, `track_t` fields), `firmware/src/felucca.c` (include),
  `tests/drum_host.h` (include), `firmware/src/fx.c` (`mix_part`), `firmware/src/drum_core.c` (`drum_cut`)
- Modify: `tests/target_budget.py` (FUNCS), `tests/target_budget.txt` (new line)
- Test: `tests/drum_test.c`, `tests/boot_test.c` (ASan extremes)

**Interfaces:**
- Consumes: Task 1's parameters, `RS_*` enums and tables.
- Produces: `reson_t` (`len[4], g[4], lp[4], apx[4], apy[4], k, a, tap[4], w, seg, quiet, peak, ns, model, ring,
  kill`); `track_t.rs`, `track_t.rfine` (1/256 semitone, written by the LFOs in Task 3);
  `static uint32_t reson_block(track_t *t, int32_t *b, uint32_t n)`; `static void reson_clear(track_t *t)`;
  `static uint32_t reson_chords(const track_t *except)`; `static uint32_t rs_chord(const track_t *t)`;
  `static const int8_t RS_CHORD_IV[RS_NCHORD][4]`; `#define RS_FINE 6144`.

- [ ] **Step 1: Write the failing tests**

`tests/drum_test.c`, before `/* PROB codes (params.c)`:

```c
/* RESON (reson.c) helpers: a rim click through track 0's resonator, ring only, bright, no stiffness */
static void rs_setup(uint32_t model, int32_t tune)
{
    host_init();
    drum_set_model(&trk[0], DM_RIM);
    trk[0].p[P_RMODEL] = (int16_t)model;
    trk[0].p[P_RTUNE] = (int16_t)tune;
    trk[0].p[P_RDECAY] = 110;
    trk[0].p[P_RMIX] = 127;
    trk[0].p[P_RTONE] = 127;
    trk[0].p[P_RSTRCT] = 0;
    trk[0].p[P_RPOS] = 0;
}

static double rs_goertzel(const int32_t *x, uint32_t n, double f)   /* magnitude of f in x[0..n) */
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}

static double rs_freq(const int32_t *x, uint32_t n, double f0)       /* the autocorrelation peak near FS / f0 */
{
    uint32_t lo = (uint32_t)(FS / f0 * 0.8), hi = (uint32_t)(FS / f0 * 1.25) + 2, lag, best = lo, i, j;
    double bv = -1e300, c[3], d;
    for (lag = lo; lag <= hi; lag++) {
        double s = 0;
        for (i = 0; i < n; i++)
            s += (double)x[i] * x[i + lag];
        if (s > bv) {
            bv = s;
            best = lag;
        }
    }
    for (j = 0; j < 3; j++) {
        c[j] = 0;
        for (i = 0; i < n; i++)
            c[j] += (double)x[i] * x[i + best - 1 + j];
    }
    d = c[0] - 2 * c[1] + c[2];
    return FS / (best + (d != 0 ? 0.5 * (c[0] - c[2]) / d : 0));
}

static double rs_note_hz(double note) { return 440.0 * pow(2.0, (note - 69) / 12); }

static void test_reson_pitch(void)
{
    static const int32_t NOTES[] = {24, 36, 48, 60, 72, 84};
    uint32_t i, ok = 1;
    double worst = 0;
    for (i = 0; i < sizeof NOTES / sizeof NOTES[0]; i++) {
        double f, want = rs_note_hz(NOTES[i]), cents;
        rs_setup(RS_STRNG, NOTES[i]);
        drum_hit(&trk[0], 127);
        render_mix(wl, wr, SECS(0.6));
        f = rs_freq(wl + SECS(0.2), 8192, want);
        cents = fabs(1200 * log2(f / want));
        worst = cents > worst ? cents : worst;
        ok &= cents < 10;
    }
    printf("     RESON STRNG pitch C1..C6: worst %.1f cents\n", worst);
    check("RESON STRNG: the ring is at TUNE (C1..C6, within 10 cents)", ok);
    rs_setup(RS_PIPE, 48);
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.6));
    {
        double f0 = rs_note_hz(48), h1 = rs_goertzel(wl + SECS(0.2), 16384, f0), h2 = rs_goertzel(wl + SECS(0.2), 16384, 2 * f0);
        double h3 = rs_goertzel(wl + SECS(0.2), 16384, 3 * f0), f = rs_freq(wl + SECS(0.2), 8192, f0);
        printf("     RESON PIPE C3: %.1f Hz, harmonics 1 / 2 / 3: %.0f / %.0f / %.0f\n", f, h1, h2, h3);
        check("RESON PIPE: at TUNE, odd harmonics only", fabs(1200 * log2(f / f0)) < 10 && h2 * 10 < h1 && h2 * 5 < h3);
    }
}

static void test_reson_decay(void)
{
    static const int16_t DK[2] = {40, 80};
    uint32_t j;
    for (j = 0; j < 2u; j++) {
        uint32_t w, pkw = 0, t40 = 0;
        double pk = 0, want = RS_T60_MS10[DK[j]] / 10000.0, got;
        rs_setup(RS_STRNG, 48);
        trk[0].p[P_RDECAY] = DK[j];
        drum_hit(&trk[0], 127);
        render_mix(wl, wr, SECS(3));
        for (w = 0; w + 441 <= SECS(3); w += 441) {   /* 10 ms windows: RMS */
            double s = 0;
            uint32_t i;
            for (i = 0; i < 441; i++)
                s += (double)wl[w + i] * wl[w + i];
            s = sqrt(s / 441);
            if (s > pk) {
                pk = s;
                pkw = w;
            }
            if (!t40 && pk > 0 && w > pkw && s < pk / 100)
                t40 = w - pkw;
        }
        got = t40 * 1.5 / FS;                        /* -40 dB x 1.5 = -60 dB */
        printf("     RESON DECAY %d: 60 dB in %.3f s (knob %.3f s)\n", DK[j], got, want);
        check(j ? "RESON DECAY 80: the ring's 60 dB time as the knob (25 %)" : "RESON DECAY 40: the ring's 60 dB time as the knob (25 %)",
              t40 && fabs(got - want) <= 0.25 * want);
    }
}

static void test_reson_chord(void)
{
    static const int8_t MAJ[4] = {0, 4, 7, 12}, OFF[3] = {2, 6, 10};
    uint32_t i, ok = 1;
    double lo = 1e300, hi = 0;
    rs_setup(RS_CHORD, 48);
    trk[0].p[P_RSTRCT] = 3 * 128 / RS_NCHORD + 1;   /* MAJ */
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.8));
    for (i = 0; i < 4u; i++) {
        double m = rs_goertzel(wl + SECS(0.1), 16384, rs_note_hz(48 + MAJ[i]));
        lo = m < lo ? m : lo;
    }
    for (i = 0; i < 3u; i++) {
        double m = rs_goertzel(wl + SECS(0.1), 16384, rs_note_hz(48 + OFF[i]));
        hi = m > hi ? m : hi;
    }
    ok = lo > 4 * hi;
    printf("     RESON CHORD MAJ C3: weakest chord note %.0f, strongest other note %.0f\n", lo, hi);
    check("RESON CHORD MAJ on C3: C3 E3 G3 C4 ring, D3 F#3 A#3 do not", ok);
    rs_setup(RS_CHORD, 30);                           /* TUNE below C3: plays as C3 */
    drum_hit(&trk[0], 127);
    render_mix(wl, wr, SECS(0.6));
    check("RESON CHORD below C3 plays at C3", fabs(1200 * log2(rs_freq(wl + SECS(0.2), 8192, rs_note_hz(48)) / rs_note_hz(48))) < 15);
}

static void test_reson_mix_off_tail(void)
{
    static int32_t dry[SECS(1)];
    uint32_t i, same = 1;
    int32_t late;
    rs_setup(RS_OFF, 48);                             /* MIX 0 == OFF, bit for bit */
    drum_hit(&trk[0], 127);
    render_mix(dry, 0, SECS(1));
    rs_setup(RS_STRNG, 48);
    trk[0].p[P_RMIX] = 0;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(1));
    for (i = 0; i < SECS(1); i++)
        same &= wl[i] == dry[i];
    check("RESON MIX 0: the dry sound, bit for bit", same);
    rs_setup(RS_STRNG, 48);                           /* the ring outlives the voice */
    trk[0].p[P_RDECAY] = 100;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(1));
    late = peak_of(wl, SECS(0.8), SECS(1));
    check("RESON: the ring sounds on after the hit's voice (0.8 s on)", late > 200 && trk[0].rs.ring);
    rs_setup(RS_STRNG, 48);
    trk[0].p[P_RDECAY] = 20;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(1.5));
    check("RESON: a short ring ends (the track stops computing it)", !trk[0].rs.ring);
}

static void test_reson_switch_and_cut(void)
{
    int32_t before = 0, after = 0, i, pk;
    rs_setup(RS_STRNG, 48);                           /* MODEL changed while ringing: the old ring fades out */
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.3));
    for (i = SECS(0.3) - 2000; i < (int32_t)SECS(0.3); i++)
        before = abs(wl[i] - wl[i - 1]) > before ? abs(wl[i] - wl[i - 1]) : before;
    trk[0].p[P_RMODEL] = RS_CHORD;
    render_mix(wl, 0, CTL * 4);
    for (i = 1; i < CTL * 4; i++)
        after = abs(wl[i] - wl[i - 1]) > after ? abs(wl[i] - wl[i - 1]) : after;
    printf("     RESON model switch while ringing: largest step %d (ring before %d)\n", after, before);
    check("RESON: switching MODEL while ringing does not click", after <= before + 64);
    trk[0].p[P_RMODEL] = RS_OFF;                      /* OFF while ringing, then on again without a hit */
    render_mix(wl, 0, CTL * 4);
    trk[0].p[P_RMODEL] = RS_STRNG;
    render_mix(wl, 0, SECS(0.2));
    pk = peak_of(wl, 0, SECS(0.2));
    check("RESON: no stale ring after OFF and on again", pk < 64);
    rs_setup(RS_STRNG, 48);                           /* a cut (mute) fades the ring out within a block */
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.3));
    drum_cut(&trk[0]);
    render_mix(wl, 0, CTL * 3);
    check("RESON: a cut (mute) ends the ring within a block", peak_of(wl, CTL * 2, CTL * 3) < 64 && !trk[0].rs.ring);
}

/* spec §7: TONE darkens the ring, STRCT stretches its overtones, POS thins harmonics (POS 64: a tap at a quarter
 * of the line, every 4th harmonic notched) */
static double rs_peak_near(const int32_t *x, uint32_t n, double f, double span)   /* the strongest frequency near f */
{
    double best = f, bv = -1, g;
    for (g = f * (1 - span); g <= f * (1 + span); g += 0.5) {
        double m = rs_goertzel(x, n, g);
        if (m > bv) {
            bv = m;
            best = g;
        }
    }
    return best;
}

static void test_reson_knobs(void)
{
    double f0 = rs_note_hz(48), h1, h6, dark, bright, h4, h4pos, f4, stretch;
    rs_setup(RS_STRNG, 48);                           /* TONE: the 6th harmonic against the 1st */
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.5));
    h1 = rs_goertzel(wl + SECS(0.1), 16384, f0);
    h6 = rs_goertzel(wl + SECS(0.1), 16384, 6 * f0);
    h4 = rs_goertzel(wl + SECS(0.1), 16384, 4 * f0);
    bright = h6 / h1;
    rs_setup(RS_STRNG, 48);
    trk[0].p[P_RTONE] = 30;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.5));
    dark = rs_goertzel(wl + SECS(0.1), 16384, 6 * f0) / rs_goertzel(wl + SECS(0.1), 16384, f0);
    printf("     RESON TONE: 6th / 1st harmonic bright %.3f, dark %.3f\n", bright, dark);
    check("RESON TONE: darker damps the upper harmonics", dark * 3 < bright);
    rs_setup(RS_STRNG, 48);                           /* STRCT: the 4th partial moves off 4 x f0 */
    trk[0].p[P_RSTRCT] = 127;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.5));
    f4 = rs_peak_near(wl + SECS(0.1), 16384, 4 * rs_freq(wl + SECS(0.1), 8192, f0), 0.08);
    stretch = 1200 * log2(f4 / (4 * rs_freq(wl + SECS(0.1), 8192, f0)));
    printf("     RESON STRCT 127: the 4th partial %.0f cents off 4 x f0\n", stretch);
    check("RESON STRCT: stretches the overtones (4th partial > 20 cents off)", fabs(stretch) > 20);
    rs_setup(RS_STRNG, 48);                           /* POS 64: the 4th harmonic notched */
    trk[0].p[P_RPOS] = 64;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.5));
    h4pos = rs_goertzel(wl + SECS(0.1), 16384, 4 * f0) / rs_goertzel(wl + SECS(0.1), 16384, f0);
    printf("     RESON POS: 4th / 1st harmonic at POS 0 %.3f, POS 64 %.3f\n", h4 / h1, h4pos);
    check("RESON POS 64: thins the 4th harmonic", h4pos * 4 < h4 / h1);
}

/* review focus 3: sustained loud input at DECAY max with DIST after: bounded, and the ring dies after */
static void test_reson_sustain_bounded(void)
{
    uint32_t k;
    int32_t pk, after;
    rs_setup(RS_STRNG, 48);
    drum_set_model(&trk[0], DM_HNOIS);
    trk[0].p[P_RMODEL] = RS_STRNG;
    trk[0].p[P_RDECAY] = 127;
    trk[0].p[P_DIST] = 127;
    for (k = 0; k < 16u; k++)
        trk[0].step[k].on = 1;
    trk[0].p[P_SDIV] = 3;                             /* 1/32 */
    song.g[G_BPM] = 240;
    play();
    render_mix(wl, 0, SECS(8));
    pk = peak_of(wl, 0, SECS(8));
    transport_req = 2;
    render_mix(wl, 0, SECS(8));
    for (k = 0; k < 4u; k++)
        render_mix(wl, 0, SECS(8));                   /* 40 s after the input stopped */
    after = peak_of(wl, SECS(6), SECS(8));
    printf("     RESON DECAY max, noisy hits every 1/32, DIST: peak %d, 40 s later %d\n", pk, after);
    check("RESON at DECAY max: bounded, and the ring dies after the input stops", pk <= 32767 && after * 100 < pk);
}

/* review focus 4: a muted COMP source with RESON: the ring keys the compressor, the mix does not hear it */
static void test_reson_ghost_source(void)
{
    uint32_t k;
    rs_setup(RS_STRNG, 48);
    song.g[G_CSRC] = 1;
    trk[0].p[P_MUTE] = 1;
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    play();
    render_mix(wl, 0, SECS(2));
    check("RESON on a muted COMP source: not heard in the mix", peak_of(wl, SECS(0.1), SECS(2)) < 64);
}
```

Add to `main()` after `    test_reson_params();`:

```c
    test_reson_pitch();
    test_reson_decay();
    test_reson_chord();
    test_reson_mix_off_tail();
    test_reson_switch_and_cut();
    test_reson_knobs();
    test_reson_sustain_bounded();
    test_reson_ghost_source();
```

`tests/boot_test.c` (ASan), after `fdr4_converts`:

```c
/* review focus 1: RESON at every extreme (pitch with a chord's top note and the fine offset, STRCT, TONE, POS):
 * the lines stay inside rs_buf (ASan) and the output bounded */
static int reson_extremes(void)
{
    static const int16_t TUNE[2] = {24, 96}, END[2] = {0, 127};
    static const int32_t FINE[3] = {-RS_FINE, 0, RS_FINE};
    uint32_t m, a, b, c, d, f, i;
    int ok = 1;
    for (m = RS_STRNG; m < RS_NMODEL; m++)
        for (a = 0; a < 2u; a++)
            for (b = 0; b < 2u; b++)
                for (c = 0; c < 2u; c++)
                    for (d = 0; d < 2u; d++)
                        for (f = 0; f < 3u; f++) {
                            host_init();
                            trk[0].p[P_RMODEL] = (int16_t)m;
                            trk[0].p[P_RTUNE] = TUNE[a];
                            trk[0].p[P_RSTRCT] = END[b];
                            trk[0].p[P_RTONE] = END[c];
                            trk[0].p[P_RPOS] = END[d];
                            trk[0].p[P_RDECAY] = 127;
                            trk[0].rfine = FINE[f];
                            drum_hit(&trk[0], 127);
                            for (i = 0; i < 64u; i++) {
                                render_mix(L, R, CTL);
                                trk[0].rfine = FINE[f];   /* (the LFOs rewrite it every block) */
                                ok &= L[0] <= 32767 && L[0] >= -32768;
                            }
                        }
    return ok;
}
```

and in `main()`:

```c
    check("RESON at every extreme: inside its lines (ASan), bounded", reson_extremes());
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `cc $F -o build/host/drum_test tests/drum_test.c -lm 2>&1 | head -3`
Expected: compile errors: `rs` is not a member of `track_t`, `RS_FINE` undeclared.

- [ ] **Step 3: Implement the resonator**

`firmware/src/core.h`, after the `lfo_state_t` typedef:

```c
typedef struct {                 /* RESON runtime (reson.c): per line (STRNG / PIPE 1, CHORD 4) */
    uint32_t len[4];             /* the line's delay, Q8 samples (the loop filters' delay taken off) */
    int32_t g[4];                /* loop gain, Q15 (PIPE: negative) */
    int32_t lp[4], apx[4], apy[4];   /* damping low-pass; the stiffness all-pass' x[n-1], y[n-1] */
    int32_t k, a;                /* damping coefficient (Q15), all-pass coefficient (Q15, <= 0) */
    uint16_t tap[4];             /* POS: the second pickup tap (0 = none) */
    uint16_t w, seg, quiet, peak;   /* write position, segment length, quiet blocks, last block's line peak */
    uint8_t ns, model, ring, kill;  /* lines, the model running, ringing, fade out this block */
} reson_t;
```

in `struct track` after `lfo_state_t lfo[2];`:

```c
    reson_t rs;                  /* RESON runtime (reson.c) */
    int32_t rfine;               /* RESON fine pitch from the LFOs (R.TUN), 1/256 semitone (lfo.c) */
```

`firmware/src/felucca.c`, before `#include "lfo.c"`:

```c
#include "reson.c"          /* RESON: the per-track resonator insert */
```

`tests/drum_host.h`, before `#include "../firmware/src/lfo.c"`:

```c
#include "../firmware/src/reson.c"
```

Create `firmware/src/reson.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* RESON: a per-track resonator insert, before DIST (spec 2026-10-06-resonator-design.md). The track's sound
 * excites a comb: STRNG (Karplus-Strong: a delay line with a damping low-pass and a stiffness all-pass in the
 * loop), PIPE (the same with inverted feedback: odd harmonics), CHORD (four STRNG lines tuned to a chord). One
 * fixed line per track (1352 samples), split into four for CHORD. Integer only. */
#define RS_LEN 1352u                                  /* STRNG down to C1 (1348.6 samples) */
#define RS_SEG 338u                                   /* CHORD: 4 lines, the lowest string C3 (337.2) */
#define RS_GMAX 32700                                 /* the loop gain's cap (0.998): never self-oscillating */
#define RS_FLOOR 2u                                   /* the line's level (int16) under which the ring is over */
#define RS_FINE 6144                                  /* R.TUN at full DEPTH: 2 octaves, in 1/256 semitone */
static int16_t rs_buf[NTRK][RS_LEN] __attribute__((section(".pool")));

static const int8_t RS_CHORD_IV[RS_NCHORD][4] = {   /* semitones above TUNE, one note per line (N_RCHORD) */
    {0, 12, 24, 36}, {0, 7, 12, 19}, {0, 5, 12, 17}, {0, 4, 7, 12}, {0, 3, 7, 12}, {0, 2, 7, 12}, {0, 5, 7, 12},
    {0, 3, 6, 12}, {0, 4, 8, 12}, {0, 4, 7, 9}, {0, 3, 7, 9}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10},
    {0, 3, 6, 10}, {0, 3, 6, 9}, {0, 5, 7, 10}, {0, 4, 7, 14}, {0, 5, 10, 15}, {0, 1, 2, 3},
};

static uint32_t rs_chord(const track_t *t) { return (uint32_t)clamp(t->p[P_RSTRCT], 0, 127) * RS_NCHORD / 128u; }

static uint32_t reson_chords(const track_t *except)   /* tracks with MODEL CHORD, other than except */
{
    uint32_t i, n = 0;
    for (i = 0; i < NTRK; i++)
        n += &trk[i] != except && trk[i].p[P_RMODEL] == RS_CHORD;
    return n;
}

static int32_t rs_period(int32_t nq)                  /* note in 1/256 semitone -> the loop period, Q8 samples */
{
    int32_t i, f;
    nq = clamp(nq, 0, 128 * 256 - 1);
    i = nq >> 4;
    f = nq & 15;
    return (int32_t)RS_PER[i] - (int32_t)(((RS_PER[i] - RS_PER[i + 1]) * (uint32_t)f) >> 4);
}

static void reson_clear(track_t *t)                   /* silence: the line and the loop filters */
{
    reson_t *r = &t->rs;
    memset(rs_buf[t - trk], 0, sizeof rs_buf[0]);
    memset(r->lp, 0, sizeof r->lp);
    memset(r->apx, 0, sizeof r->apx);
    memset(r->apy, 0, sizeof r->apy);
    r->quiet = r->peak = r->w = 0;                    /* w: a CHORD line is shorter than STRNG's */
    r->ring = r->kill = 0;
}

/* this block's lines for model m: lengths from TUNE (+ the chord, + the fine offset), the gains from DECAY, the
 * damping from TONE, the stiffness from STRCT, the pickup tap from POS */
static void reson_setup(track_t *t, uint32_t m)
{
    reson_t *r = &t->rs;
    uint32_t s, seg = m == RS_CHORD ? RS_SEG : RS_LEN;
    int32_t k = RS_TONE_K[clamp(t->p[P_RTONE], 0, 127)], a = m == RS_CHORD ? 0 : -clamp(t->p[P_RSTRCT], 0, 127) * 180;
    int32_t comp = ((32768 - k) << 8) / k + ((32768 - a) << 8) / (32768 + a);   /* the loop filters' delay, Q8 */
    int32_t base = (clamp(t->p[P_RTUNE], 24, 96) << 8) + t->rfine, dk = (int32_t)RS_DK[clamp(t->p[P_RDECAY], 0, 127)];
    if (m == RS_CHORD && base < (48 << 8))
        base = 48 << 8;                               /* CHORD: the lowest string C3 */
    r->ns = (uint8_t)(m == RS_CHORD ? 4u : 1u);
    r->seg = (uint16_t)seg;
    r->k = k;
    r->a = a;
    for (s = 0; s < r->ns; s++) {
        int32_t per = rs_period(base + (m == RS_CHORD ? RS_CHORD_IV[rs_chord(t)][s] << 8 : 0));
        int32_t len = clamp((m == RS_PIPE ? per / 2 : per) - comp, 2 << 8, (int32_t)(seg - 2u) << 8);
        int64_t e = ((int64_t)(len + comp) * dk) >> 16;   /* the loop's decay, log2 units Q16 */
        int32_t g = e >= (15 << 16) ? 0 : (int32_t)(RS_EXP2N[(e >> 8) & 255] >> (e >> 16));
        g = g > RS_GMAX ? RS_GMAX : g;
        r->len[s] = (uint32_t)len;
        r->g[s] = m == RS_PIPE ? -g : g;
        r->tap[s] = (uint16_t)(t->p[P_RPOS] > 0 ? ((uint32_t)len >> 8) * (uint32_t)clamp(t->p[P_RPOS], 0, 127) / 256u + 1u : 0u);
    }
}

/* track t's block b (its rendered sound) through RESON, in place: b = dry + (ring - dry) x MIX. A model change
 * or a cut (kill) fades the old ring out over the block, then clears it. Returns non-zero while it rings. */
static uint32_t reson_block(track_t *t, int32_t *b, uint32_t n)
{
    reson_t *r = &t->rs;
    uint32_t m = (uint32_t)clamp(t->p[P_RMODEL], 0, RS_NMODEL - 1), i, s, peak = 0;
    int32_t ring[CTL], mix = t->p[P_RMIX] * 258, fade = 32767, fstep;
    int16_t *buf = rs_buf[t - trk];
    if (m != r->model) {
        if (r->ring && r->model)
            r->kill = 1;                              /* the old ring fades out with its own lines */
        else {
            reson_clear(t);
            r->model = (uint8_t)m;
        }
    }
    if (!r->model) {                                  /* OFF and quiet */
        r->model = (uint8_t)m;
        return 0;
    }
    reson_setup(t, r->model);
    fstep = r->kill ? 32767 / (int32_t)n + 1 : 0;
    for (i = 0; i < n; i++)
        ring[i] = 0;
    for (s = 0; s < r->ns; s++) {
        int16_t *ln = buf + s * r->seg;
        uint32_t li = r->len[s] >> 8, fr = r->len[s] & 255u, w = r->w, seg = r->seg, tap = r->tap[s];
        int32_t lp = r->lp[s], apx = r->apx[s], apy = r->apy[s], g = r->g[s], k = r->k, a = r->a;
        for (i = 0; i < n; i++) {
            uint32_t p0 = w >= li ? w - li : w + seg - li, p1 = p0 ? p0 - 1u : seg - 1u;
            int32_t x0 = ln[p0], d = x0 + (((ln[p1] - x0) * (int32_t)fr) >> 8), y, v;
            lp += ((d - lp) * k) >> 15;
            y = (int32_t)(((int64_t)(lp - apy) * a) >> 15) + apx;
            apx = lp;
            apy = y;
            v = clamp((r->kill ? 0 : b[i] >> 2) + (int32_t)(((int64_t)y * g) >> 15), -32767, 32767);
            ring[i] += tap ? v - ln[w >= tap ? w - tap : w + seg - tap] : v;
            ln[w] = (int16_t)v;
            peak = (uint32_t)(v < 0 ? -v : v) > peak ? (uint32_t)(v < 0 ? -v : v) : peak;
            if (++w == seg)
                w = 0;
        }
        r->lp[s] = lp;
        r->apx[s] = apx;
        r->apy[s] = apy;
    }
    r->w = (uint16_t)((r->w + n) % r->seg);
    for (i = 0; i < n; i++) {
        int32_t o = r->ns == 4u ? ring[i] : ring[i] << 2;   /* back to the track's scale (CHORD: 4 lines summed) */
        if (r->kill) {
            o = (int32_t)(((int64_t)o * fade) >> 15);
            fade = fade > fstep ? fade - fstep : 0;
        }
        b[i] += (int32_t)(((int64_t)(o - b[i]) * mix) >> 15);
    }
    r->peak = (uint16_t)peak;
    r->quiet = (uint16_t)(peak < RS_FLOOR ? r->quiet + 1u : 0u);
    r->ring = (uint8_t)(r->quiet <= r->seg / n + 2u);
    if (r->kill || !r->ring) {
        reson_clear(t);
        r->model = (uint8_t)m;
    }
    return r->ring;
}
```

`firmware/src/drum_core.c`, `drum_cut` gains at its start:

```c
    if (t->rs.ring)
        t->rs.kill = 1;                                  /* RESON: the ring fades out with the voices */
```

`firmware/src/fx.c`, `mix_part` — replace

```c
    if (track_render(t, b, n))
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if (
```

with

```c
    uint32_t snd = track_render(t, b, n);
    if (t->rs.ring || (snd && t->p[P_RMODEL]))
        snd |= reson_block(t, b, n);                    /* RESON (reson.c), before DIST: its ring keeps the track on */
    if (snd)
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if (
```

(declare `snd` with the function's other locals at its top if the compiler asks for C89 order:
`uint32_t i, is_src = …, snd;` and `snd = track_render(t, b, n);`).

- [ ] **Step 4: Run the tests to see them pass**

Run: `cc $F -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "RESON|^FAIL|drum_test:"`
Expected: every `RESON …` check `ok`; `drum_test: all passed` (the goldens unchanged: MODEL OFF is bit-identical).
If a pitch check fails by a constant offset, print `trk[0].rs.len[0]` against `rs_period()` and check the
compensation `comp` (the loop filters' delay) before changing anything else.

Run: `cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all $F -o build/host/boot_test tests/boot_test.c -lm && build/host/boot_test | grep -E "RESON|^FAIL|boot_test:"`
Expected: `ok    RESON at every extreme …`, `boot_test: all passed`.

Run: `cc $F -DDM_QCHECK -o build/host/drum_test_q tests/drum_test.c -lm && build/host/drum_test_q | grep -E "^FAIL|drum_test"`
Expected: `drum_test with Q24 overflow checks: all passed`.

- [ ] **Step 5: Target build, H2 and the budget line**

Add `"reson_block"` to `FUNCS` in `tests/target_budget.py` (after `"lfo_track"`). Then:

Run: `DRUM_PACKAGE=1 ./build.sh` (again on "exec format error"), then
`python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt | grep reson_block`
Expected: a line `target: reson_block … cost N (budget -)`. Append `reson_block N` (that measured N) to
`tests/target_budget.txt` by hand (spec §6; do not run `BUDGET_UPDATE=1`, it would rewrite the other lines).

Run: `sh tests/run_tests.sh 2>&1 | grep -E "FAIL|OVER|H2|ALL HOST"`
Expected: no FAIL / OVER; `ALL HOST TESTS PASSED`. If H2 fails, the `.pool` buffer or `track_t` growth moved a
global: move the failing state into an existing struct, and ledger a ruling.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/reson.c firmware/src/core.h firmware/src/felucca.c firmware/src/fx.c firmware/src/drum_core.c tests/drum_host.h tests/drum_test.c tests/boot_test.c tests/target_budget.py tests/target_budget.txt
git commit -m "reson: the resonator insert (STRNG, PIPE, CHORD) before DIST; its ring keeps the track running; a cut or a model change fades it out"
```

---

### Task 3: LFO destinations R.TUN … R.POS (R.TUN smooth)

**Files:**
- Modify: `firmware/src/lfo.c` (`lfo_dest_param`, `lfo_track`, `lfo_live`), `firmware/src/params.c`
  (`LFO_TP` DEST max, `N_RDEST`, `lfo_dest_label`, `F_LDEST`), `firmware/src/ui_draw.c` (routing line)
- Test: `tests/drum_test.c`

**Interfaces:**
- Consumes: `track_t.rfine`, `RS_FINE`, `P_RDECAY … P_RPOS`, `reson_t.len`.
- Produces: DEST 11 `R.TUN` (fine pitch: `t->rfine`), 12 `R.DCY` → `P_RDECAY`, 13 `R.MIX` → `P_RMIX`,
  14 `R.TON` → `P_RTONE`, 15 `R.STR` → `P_RSTRCT`, 16 `R.POS` → `P_RPOS`;
  `static const char *lfo_dest_label(const track_t *t, int32_t dest)` (params.c).

- [ ] **Step 1: Write the failing tests**

`tests/drum_test.c`, before `/* PROB codes (params.c)`:

```c
static void test_reson_lfo(void)
{
    static const char *const NAME[6] = {"R.TUN", "R.DCY", "R.MIX", "R.TON", "R.STR", "R.POS"};
    char v[12];
    const char *u;
    uint32_t i, ok = 1, k;
    int32_t mv = 0, before = 0, during = 0;
    uint32_t len0, lmin = 0xFFFFFFFFu, lmax = 0, jump = 0, prev;
    host_init();
    for (i = 0; i < 6u; i++) {
        param_format(&TP[P_LFO1 + LF_DEST], 11 + (int32_t)i, v, &u);
        ok &= str_eq(v, NAME[i]);
    }
    param_format(&TP[P_LFO1 + LF_DEST], 0, v, &u);
    ok &= str_eq(v, "OFF");
    check("LFO DEST 11..16: R.TUN R.DCY R.MIX R.TON R.STR R.POS; 0 OFF", ok && TP[P_LFO1 + LF_DEST].max == 16);
    rs_setup(RS_STRNG, 48);                           /* R.TUN: a slow sweep moves the line length smoothly */
    trk[0].p[P_RDECAY] = 127;
    trk[0].p[P_LFO1 + LF_WAVE] = LW_TRI;
    trk[0].p[P_LFO1 + LF_MODE] = LM_TIME;
    trk[0].p[P_LFO1 + LF_RATE] = 100;
    trk[0].p[P_LFO1 + LF_DEST] = 11;
    trk[0].p[P_LFO1 + LF_DEPTH] = 64;
    trk[0].p[P_LFO1 + LF_TRIG] = LT_FREE;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, CTL);
    len0 = prev = trk[0].rs.len[0];
    for (k = 0; k < 4000u; k++) {                     /* ~2.9 s */
        uint32_t l;
        render_mix(wl, 0, CTL);
        l = trk[0].rs.len[0];
        lmin = l < lmin ? l : lmin;
        lmax = l > lmax ? l : lmax;
        jump = (l > prev ? l - prev : prev - l) > jump ? (l > prev ? l - prev : prev - l) : jump;
        prev = l;
        if (k % 400u == 0u)
            drum_hit(&trk[0], 127);
    }
    printf("     RESON R.TUN sweep: line %u .. %u (Q8), largest block step %u (start %u)\n", lmin, lmax, jump, len0);
    check("LFO R.TUN: the resonator pitch sweeps smoothly (no semitone steps, over a semitone in total)",
          lmax - lmin > len0 / 17u && jump * 300u < len0);
    check("LFO R.TUN: the TUNE knob is untouched", trk[0].p[P_RTUNE] == 48);
    check("LFO R.TUN: the EDIT marker reports the live pitch", lfo_live(&trk[0], P_RTUNE, &mv) && mv != 48);
    rs_setup(RS_STRNG, 48);                           /* R.MIX: the modulated copy */
    trk[0].p[P_LFO1 + LF_WAVE] = LW_SQUARE;
    trk[0].p[P_LFO1 + LF_DEST] = 13;
    trk[0].p[P_LFO1 + LF_DEPTH] = -64;
    render_mix(wl, 0, CTL * 4);
    check("LFO R.MIX: modulates RESON MIX", lfo_live(&trk[0], P_RMIX, &mv) && mv != trk[0].p[P_RMIX]);
    rs_setup(RS_CHORD, 48);                           /* review focus 5: R.STR sweeping the chord types */
    trk[0].p[P_RDECAY] = 127;
    trk[0].p[P_LFO1 + LF_WAVE] = LW_SAW;
    trk[0].p[P_LFO1 + LF_MODE] = LM_HZ;
    trk[0].p[P_LFO1 + LF_RATE] = 90;
    trk[0].p[P_LFO1 + LF_DEST] = 15;
    trk[0].p[P_LFO1 + LF_DEPTH] = 64;
    trk[0].p[P_RSTRCT] = 64;
    drum_hit(&trk[0], 127);
    render_mix(wl, 0, SECS(0.2));
    before = peak_of(wl, 0, SECS(0.2));
    render_mix(wl, 0, SECS(3));
    during = peak_of(wl, 0, SECS(3));
    check("LFO R.STR on CHORD: sweeping the chords under a ring stays bounded", during <= 32767 && during < before * 4 + 1000);
}
```

Add `    test_reson_lfo();` to `main()` after `    test_reson_ghost_source();`.

- [ ] **Step 2: Run the tests to see them fail**

Run: `cc $F -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "LFO DEST 11|R\\.TUN|R\\.MIX|R\\.STR"`
Expected: `FAIL  LFO DEST 11..16 …` and the R.TUN checks FAIL (DEST stops at 10, nothing writes `rfine`).

- [ ] **Step 3: Implement**

`firmware/src/params.c` — in `LFO_TP` the DEST entry becomes `PD("DEST", F_LDEST, 0, 16, 0)`; after `N_RCHORD`:

```c
static const char *const N_RDEST[] = {"R.TUN", "R.DCY", "R.MIX", "R.TON", "R.STR", "R.POS"};   /* LFO DEST 11..16 */
```

after the `lfo_dest_param` prototype:

```c
/* an LFO DEST by name: OFF, the track's knob by its label ("--" where its engine has none), R.TUN .. R.POS */
static const char *lfo_dest_label(const track_t *t, int32_t dest)
{
    const param_desc_t *d;
    if (dest <= 0)
        return "OFF";
    if (dest >= 11)
        return N_RDEST[clamp(dest, 11, 16) - 11];
    d = track_desc(t, lfo_dest_param((uint32_t)clamp(dest, 1, 10)));
    return d->label && d->label[0] != '-' ? d->label : "--";
}
```

and the `F_LDEST` case becomes:

```c
    case F_LDEST:                                     /* the selected track's knob, by its label */
        str_cpy(val, lfo_dest_label(TSEL, v), 6);
        break;
```

`firmware/src/lfo.c` — `lfo_dest_param`:

```c
/* DEST -> the knob an LFO writes; R.TUN (11) writes no knob: the resonator's fine pitch (lfo_track) */
static uint32_t lfo_dest_param(uint32_t dest)
{
    return dest >= 1u && dest <= 8u ? P_E0 + dest - 1u : dest == 9u ? (uint32_t)P_LEVEL : dest == 10u ? (uint32_t)P_PAN
         : dest >= 12u && dest <= 16u ? P_RDECAY + dest - 12u : 0xFFu;
}
```

in `lfo_track`, the clamp `clamp(q[LF_DEST], 0, 10)` becomes `clamp(q[LF_DEST], 0, 16)`; at the function's start
(before `if (on & ~…)`):

```c
    t->rfine = 0;
```

and after the final write loop (`for (k = 0; k < t->lnum; k++) { … }`):

```c
    for (l = 0; l < 2u; l++) {                        /* R.TUN: the resonator's fine pitch, smooth (TUNE untouched) */
        const int16_t *q = &t->p[l ? P_LFO2 : P_LFO1];
        if (q[LF_DEST] == 11 && q[LF_DEPTH])
            t->rfine += t->lfo[l].out * clamp(q[LF_DEPTH], -64, 64) / 64 * RS_FINE / 32767;
    }
```

`lfo_live` gains at its start:

```c
    if (pid == P_RTUNE) {                             /* R.TUN: TUNE + the fine offset of the last block */
        if ((t->p[P_LFO1 + LF_DEST] == 11 && t->p[P_LFO1 + LF_DEPTH]) || (t->p[P_LFO2 + LF_DEST] == 11 && t->p[P_LFO2 + LF_DEPTH])) {
            *val = clamp(t->p[P_RTUNE] + t->rfine / 256, 24, 96);
            return 1;
        }
        return 0;
    }
```

`firmware/src/ui_draw.c` — in `graph_lfo`'s routing block, replace

```c
        const param_desc_t *d = q[LF_DEST] ? track_desc(t, lfo_dest_param((uint32_t)clamp(q[LF_DEST], 1, 10))) : 0;
        str_cpy(b, "-> ", sizeof b);
        str_cpy(b + 3, d && d->label && d->label[0] != '-' ? d->label : "OFF", sizeof b - 3);
```

with

```c
        str_cpy(b, "-> ", sizeof b);
        str_cpy(b + 3, lfo_dest_label(t, q[LF_DEST]), sizeof b - 3);
```

(this also fixes the parked minor "the routing line shows OFF for a DEST the engine lacks": it shows `--`).

- [ ] **Step 4: Run the tests to see them pass**

Run: `cc $F -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "LFO|RESON|^FAIL|drum_test:"`
Expected: all `ok`, `drum_test: all passed`.

Run: `sh tests/run_drum_tests.sh 2>&1 | tail -1`
Expected: `ALL DRUM HOST TESTS PASSED`

Run (target layout of `lfo_track`): `DRUM_PACKAGE=1 ./build.sh && python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt | grep -E "lfo_track|reson_block"`
Expected: both within budget. If `lfo_track` reads far over (the compiler moved the wave dispatch out of line,
as in the LFO final review), move the R.TUN loop into a `noinline` helper `lfo_rfine(t)` called after the
write loop, and ledger a ruling.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/lfo.c firmware/src/params.c firmware/src/ui_draw.c tests/drum_test.c
git commit -m "reson: LFO destinations R.TUN (smooth fine pitch) R.DCY R.MIX R.TON R.STR R.POS; DEST names in one place (absent knob: --)"
```

---

### Task 4: RESON pages, picture and the CHORD cap

**Files:**
- Modify: `firmware/src/pages.c` (GR enum, two pages after SLICER), `firmware/src/ui_input.c` (`edit_param`
  cap), `firmware/src/ui_draw.c` (`graph_reson`, `draw_graph`, `graph_signature`)
- Test: `tests/ui_test.c`

**Interfaces:**
- Consumes: `P_R*`, `RS_*`, `reson_chords()`, `rs_chord()`, `RS_CHORD_IV`, `reson_t.peak`, `N_RCHORD`, `N_NOTE`.
- Produces: `GR_RESON`; pages `RESON 1/2` `{P_RMODEL, P_RTUNE, P_RDECAY, P_RMIX}` and `RESON 2/2`
  `{P_RTONE, P_RSTRCT, P_RPOS, 0xFF}` in FAM_FX after SLICER; `static void graph_reson(uint16_t c)`.

- [ ] **Step 1: Write the failing tests**

`tests/ui_test.c`, before `/* HOME: a white key selects its track and plays it;`:

```c
/* FX: RESON 1/2 and 2/2 after SLICER; STRCT is CHORD on CHORD; CHORD on 2 tracks at most (knob and load) */
static void test_reson_pages(void)
{
    uint32_t k, n;
    ui_host_init();
    for (k = 0; k < 8u && (ui.home || page_id(cur_page(), 0) != P_RMODEL); k++)
        tap(B_FX);
    check("FX: RESON 1/2 after SLICER (MODEL TUNE DECAY MIX)",
          str_eq(cur_page()->title, "RESON") && page_id(cur_page(), 0) == P_RMODEL && page_id(cur_page(), 3) == P_RMIX &&
              str_eq(PAGES[ui.page - 1u].title, "SLICER"));
    turn(EN_K1, 1);
    ui_frame();
    check("RESON: KNOB 1 picks STRNG", TSEL->p[P_RMODEL] == RS_STRNG);
    keys(1u << KEY_TRK_KEY[0]);                      /* play it: the ring for the picture */
    ui_frame();
    keys(0);
    for (k = 0; k < 10u; k++)
        ui_frame();
    snap_page("reson/01_strng");
    tap(B_FX);
    check("FX again: RESON 2/2 (TONE STRCT POS)", page_id(cur_page(), 0) == P_RTONE && page_id(cur_page(), 1) == P_RSTRCT);
    TSEL->p[P_RMODEL] = RS_CHORD;
    ui_frame();
    {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), 1, &vp);
        check("RESON 2/2 on CHORD: the second knob is CHORD", d && str_eq(d->label, "CHORD"));
    }
    snap_page("reson/02_chord_page2");
    trk[1].p[P_RMODEL] = RS_CHORD;                   /* tracks 1 and 2 CHORD: track 3 cannot */
    trk[2].p[P_RMODEL] = RS_PIPE;
    for (k = 0; k < 8u && (ui.home || page_id(cur_page(), 0) != P_RMODEL); k++)
        tap(B_FX);                                   /* round the FX pages back to RESON 1/2 */
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    ui_frame();
    turn(EN_K1, 1);
    ui_frame();
    check("RESON CHORD cap: a 3rd track's MODEL knob stays on PIPE, with the message",
          song.sel == 2 && trk[2].p[P_RMODEL] == RS_PIPE && str_eq(ui.msg, "CHORD: 2 TRACKS MAX"));
    snap_page("reson/03_chord_cap");
    trk[1].p[P_RMODEL] = RS_STRNG;                   /* a place frees */
    turn(EN_K1, 1);
    ui_frame();
    check("RESON CHORD cap: once a place frees, the 3rd track gets CHORD", trk[2].p[P_RMODEL] == RS_CHORD);
    for (k = 0; k < NTRK; k++)                       /* a project with 8 CHORD tracks loads with 2 */
        trk[k].p[P_RMODEL] = RS_CHORD;
    project_save(0);
    project_load(0);
    for (k = 0, n = 0; k < NTRK; k++)
        n += trk[k].p[P_RMODEL] == RS_CHORD;
    check("RESON CHORD cap on load: the first two keep CHORD, the others play STRNG",
          n == 2u && trk[0].p[P_RMODEL] == RS_CHORD && trk[1].p[P_RMODEL] == RS_CHORD && trk[7].p[P_RMODEL] == RS_STRNG);
}
```

Add `    test_reson_pages();` to `main()` after `    test_tracks_rec_keys();`.

- [ ] **Step 2: Run the test to see it fail**

Run: `cc $F -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test | grep -E "RESON|^FAIL|ui_test:"`
Expected: `FAIL  FX: RESON 1/2 after SLICER …` (no RESON pages yet), and the cap checks fail.

- [ ] **Step 3: Implement the pages, the cap and the picture**

`firmware/src/pages.c` — the GR enum's end becomes `GR_COMP, GR_LFO, GR_RESON };`; after the SLICER page:

```c
    {"RESON", FAM_FX, SC_TRACK, GR_RESON, {P_RMODEL, P_RTUNE, P_RDECAY, P_RMIX}},
    {"RESON", FAM_FX, SC_TRACK, GR_RESON, {P_RTONE, P_RSTRCT, P_RPOS, 0xFF}},   /* STRCT: CHORD on CHORD */
```

`firmware/src/ui_input.c` — in `edit_param`, between `v = clamp(…);` and `*vp = (int16_t)v;`:

```c
    if (pg->scope == SC_TRACK && id == P_RMODEL && v == RS_CHORD && *vp != RS_CHORD && reson_chords(TSEL) >= 2u) {
        ui_message("CHORD: 2 TRACKS MAX");            /* RESON CHORD on 2 tracks at most */
        return;
    }
```

`firmware/src/ui_draw.c` — before `graph_signature`:

```c
/* RESON pages: the model in large type, the pitch (CHORD: its four notes), the ring's partials on a 4-octave
 * axis (STRNG all, PIPE odd, CHORD its notes), a meter of the ring */
static void graph_reson(uint16_t c)
{
    static const char *const NM[RS_NMODEL] = {"OFF", "STRING", "PIPE", "CHORD"};
    static const uint8_t HX[16] = {0, 34, 54, 68, 79, 88, 95, 102, 108, 113, 118, 122, 126, 129, 133, 136};
    const track_t *t = TSEL;
    uint32_t m = (uint32_t)clamp(t->p[P_RMODEL], 0, RS_NMODEL - 1), h, n;
    int32_t note = clamp(t->p[P_RTUNE], 24, 96);
    char b[24];
    cv_text(4, 2, &FONT_L, NM[m], m ? C_WHITE : C_GRAY);
    if (!m)
        return;
    b[0] = 0;
    if (m == RS_CHORD) {
        note = note < 48 ? 48 : note;
        for (h = 0; h < 4u; h++) {
            str_cpy(b + str_len(b), N_NOTE[(uint32_t)(note + RS_CHORD_IV[rs_chord(t)][h]) % 12u], sizeof b - str_len(b));
            str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        }
    } else {
        str_cpy(b, N_NOTE[(uint32_t)note % 12u], sizeof b);
        fmt_int(b + str_len(b), note / 12 - 1);
    }
    cv_text(4, 44, &FONT_S, b, C_GRAY);
    cv_rect(100, 74, 136, 1, C_LINE);
    for (h = 0; h < (m == RS_CHORD ? 4u : 16u); h++) {
        uint32_t x = m == RS_CHORD ? (uint32_t)RS_CHORD_IV[rs_chord(t)][h] * 136u / 48u : HX[h];
        if (m == RS_PIPE && (h & 1u))
            continue;                                 /* PIPE: odd partials only (h = 0 is the 1st) */
        n = m == RS_CHORD ? 50u : 56u - h * 3u;
        cv_rect(100 + (int32_t)x, 74 - (int32_t)n, 2, (int32_t)n, c);
    }
    cv_rect(4, 84, 92, 1, C_LINE);
    cv_rect(4, 82, (int32_t)((uint32_t)t->rs.peak * 92u / 32767u), 5, C_AMB);
}
```

In `graph_signature`, before `return h;`:

```c
    if (pg->graph == GR_RESON)
        h ^= ((uint32_t)t->rs.peak >> 9) * 2654435761u + (uint32_t)rs_chord(t) * 7919u;
```

In `draw_graph`'s switch, before `case GR_SLOTS:`:

```c
        case GR_RESON:
            graph_reson(c);
            break;
```

- [ ] **Step 4: Run the tests to see them pass**

Run: `cc $F -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test | grep -E "RESON|^FAIL|ui_test:"`
Expected: all `RESON …` checks `ok`, `ui_test: all passed` (the page walk reaches the two new pages).

Look at `build/ui_shots/reson/*.ppm` (and the page walk's RESON shots): the model name, the pitch, the
partials, the meter.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/pages.c firmware/src/ui_input.c firmware/src/ui_draw.c tests/ui_test.c
git commit -m "reson: RESON 1/2 and 2/2 in FX after SLICER, its picture, CHORD on 2 tracks at most (knob, load)"
```

---

### Task 5: Cost, demos, device checklist, full suite

**Files:**
- Modify: `tests/drum_test.c` (`kit_cost`, `test_cost`), `tests/drumsim.c` (RESON demos),
  `docs/DEVICE_INSTALL.md`, `docs/IDEAS.md`

**Interfaces:**
- Consumes: everything above.
- Produces: cost lines for RESON; `build/drum_renders/reson_*.wav`.

- [ ] **Step 1: Write the failing cost checks**

`tests/drum_test.c` — `kit_cost` gains a third parameter `int reson`; inside its per-track loop:

```c
        t->p[P_RMODEL] = (int16_t)(!reson ? RS_OFF : i < 2u ? RS_CHORD : RS_STRNG);   /* RESON: 2 CHORD + 6 STRNG */
        t->p[P_RDECAY] = 127;
```

(its existing callers pass `0`). In `test_cost`, after the extreme loop:

```c
    c = kit_cost(wm, (int)wl_, 1);                   /* the same worst kit, every track ringing (2 CHORD) */
    printf("     extreme kit with RESON on all 8 (2 CHORD): %.0f (limit %.0f)\n", c, emax);
    check("cost: the extreme kit with RESON on all 8 tracks within the recorded limit", !i0 || emax == 0 || c <= emax);
```

and in its realistic-kit setup loop:

```c
        t->p[P_RMODEL] = (int16_t)(i == 1u ? RS_CHORD : i == 3u ? RS_STRNG : RS_OFF);   /* RESON on 2 tracks */
```

- [ ] **Step 2: Run the cost test**

Run: `cc $F -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "cost|kit"`
Expected: the new lines print. If `cost: … RESON … within the recorded limit` or the realistic check FAILs:
**stop and ask the user with the numbers** (Global Constraints). Do not change `tests/drum_cost_ref.txt`.

- [ ] **Step 3: Demo WAVs**

`tests/drumsim.c`, before `int main`:

```c
/* RESON demos (120 BPM): a kick / snare / hat kit with RESON on the snare and hats */
static void reson_kit(uint32_t model)
{
    uint32_t k;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_S909);
    drum_set_model(&trk[2], DM_HATC);
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    trk[1].step[4].on = trk[1].step[12].on = 1;
    for (k = 2; k < 16u; k += 4u)
        trk[2].step[k].on = 1;
    for (k = 1; k < 3u; k++) {
        trk[k].p[P_RMODEL] = (int16_t)model;
        trk[k].p[P_RDECAY] = 96;
        trk[k].p[P_RMIX] = 90;
    }
    trk[2].p[P_RTUNE] = 72;
}
static void reson_model_bar(uint32_t b) { trk[1].p[P_RMODEL] = trk[2].p[P_RMODEL] = (int16_t)(RS_STRNG + b % 3u); }
static void reson_chord_bar(uint32_t b) { trk[1].p[P_RSTRCT] = (int16_t)((b % RS_NCHORD) * 128u / RS_NCHORD + 3u); }
static void reson_dist_bar(uint32_t b) { trk[1].p[P_DIST] = trk[2].p[P_DIST] = (int16_t)(b & 1u ? 110 : 0); }
static void reson_none_bar(uint32_t b) { (void)b; }
```

in `main` before `return 0;`:

```c
    reson_kit(RS_STRNG);
    write_demo(dir, "reson_models.wav", 3, reson_model_bar);   /* STRNG, PIPE, CHORD, one bar each */
    reson_kit(RS_CHORD);
    write_demo(dir, "reson_chords.wav", 8, reson_chord_bar);   /* a chord type per bar */
    reson_kit(RS_STRNG);
    trk[1].p[P_LFO1 + LF_DEST] = 11;                           /* R.TUN: a slow triangle sweep */
    trk[1].p[P_LFO1 + LF_DEPTH] = 48;
    trk[1].p[P_LFO1 + LF_WAVE] = LW_TRI;
    trk[1].p[P_LFO1 + LF_RATE] = 16;                           /* SYNC 2 bars (16 * 17 / 128 = 2) */
    write_demo(dir, "reson_sweep.wav", 4, reson_none_bar);
    reson_kit(RS_PIPE);
    write_demo(dir, "reson_dist.wav", 4, reson_dist_bar);      /* RESON into DIST, every other bar */
```

Run: `sh tests/run_drum_tests.sh 2>&1 | tail -2 && ls build/drum_renders/reson_*.wav`
Expected: `ALL DRUM HOST TESTS PASSED` and four `reson_*.wav` files.

- [ ] **Step 4: Device checklist and IDEAS**

`docs/DEVICE_INSTALL.md`, a new section after `### LFOs (check on the FM-1)`:

```markdown
### RESON (check on the FM-1)

- FX: after SLICER, RESON 1/2 (MODEL TUNE DECAY MIX) and 2/2 (TONE STRCT POS). MODEL STRNG on the snare: each
  hit rings at TUNE (C3 = a low string); DECAY longer = longer ring; MIX 100 % = only the ring; TONE darker =
  muted; STRCT = a stretched, bell-ish ring; POS = a hollower / fuller ring.
- PIPE: a hollow, odd-harmonic tube. CHORD: four strings; on CHORD the second knob of RESON 2/2 is CHORD (OCT …
  CLUST), the picture shows the notes. A third track cannot take CHORD ("CHORD: 2 TRACKS MAX").
- LFO DEST R.TUN: the ring's pitch sweeps smoothly; R.DCY / R.MIX / R.TON / R.STR / R.POS move those knobs.
- Mute a ringing track: the ring stops at once without a click. MODEL OFF: the track sounds as before.
- CPU: RESON on all tracks (2 CHORD) with a dense pattern: GLOBAL -> SYSTEM CPU well under 100 %, no crackle.
- SAVE / power cycle / LOAD: RESON comes back; an older project loads with RESON OFF.
```

`docs/IDEAS.md` — remove the bullet `the routing line shows "OFF" for a DEST the engine lacks (should be "--");`
(fixed in Task 3).

- [ ] **Step 5: Full suite and package**

Run: `DRUM_PACKAGE=1 ./build.sh` (again on "exec format error"), then `sh tests/run_tests.sh 2>&1 | grep -E "FAIL|OVER|SKIP|ALL HOST"`
Expected: no FAIL / OVER / SKIP; `ALL HOST TESTS PASSED` (H1–H4, the target budget with `reson_block`).

- [ ] **Step 6: Commit**

```bash
git add tests/drum_test.c tests/drumsim.c docs/DEVICE_INSTALL.md docs/IDEAS.md
git commit -m "reson: cost checks (extreme kit with RESON on all 8), demo WAVs, device checklist"
```
