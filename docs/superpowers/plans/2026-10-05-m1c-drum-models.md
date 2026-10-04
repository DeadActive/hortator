# M1-C Six More Drum Models Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add six integer drum models (KBOOM, KPUNC, SSNAP, SCRAK, HMETL, HNOIS), fixed-point ports of Mutable Instruments' drum algorithms. Each is proven against the original C++ by a host fidelity test (strict target, loose fallback), and all six are always built into the drum firmware.

**Architecture:** A Q24 toolkit in `dm_dsp.c` has 1.0 = 2^24, 64-bit products, and native 64÷32 divides (probed: `r1_r0 = r1 * r0 (s)`, `r1_r0 = r1_r0 / r2 (s)`, no helper calls). It makes each port a stage-by-stage translation of the float original. Per-voice model state lives in a union (`dm_state.h`, `dvoice_t.ms`). The host-only reference has three parts:
- `tests/fetch_ref.sh` fetches the original code at a pinned commit.
- `tests/drum_ref.cc` renders it and writes metrics.
- `tests/drum_fidelity.c` measures ours with the same metric code (`tests/drum_metrics.h`) and compares them.

**Tech Stack:** C (JieLi clang 4 target / Apple clang host), C++11 (host reference only), shell, git (sparse fetch).

**Spec:** `docs/superpowers/specs/2026-10-05-m1c-drum-models-design.md` (addendum to `2026-10-05-drum-core-m1-design.md`).

## Global Constraints

- No device writes: never run `tools/fm1_install.py` or the web installer; never install `mido`/`python-rtmidi`. Packages only with `DRUM_PACKAGE=1` (`felucca-UNTESTED.fwsc`).
- Frozen (checked by `tools/check_untouched.py`): `hal/`, `loader/`, `ota.c`, `usb.c`, `crt0.S`, `app.ld`, `storage.c`, `main.c` outside `felucca_init()`/boot titles, the last 5 lines of `core.h`.
- No float in the firmware. The app links no runtime library, so any soft-float or 64-bit helper call fails the build. `Q24(x)` is only ever applied to literal constants (folded at compile time); runtime conversions use integer math.
- Mutable Instruments' code is MIT. Every firmware block ported from it carries the MIT notice (Task 3 gives the text). The original code itself is only fetched into `build/drum_ref/` for host tests and is never committed or built into the firmware.
- Nothing on screen mentions Plaits or Mutable Instruments (user decision). Names: KBOOM KPUNC SSNAP SCRAK HMETL HNOIS, appended after DM_SMPL in that order.
- Controls: TUNE = NOTE (base note + semitones, -24..24), DECAY = MORPH, TONE = TIMBRE, CHAR = HARMONICS (each 0..127 → 0..1), and ACCENT = velocity / 127. The model applies the accent, so `dm_putq` adds no velocity gain. P_E4..P_E7 are unused.
- Fidelity tiers (spec §5): a render that misses **loose** fails the suite. **Strict** misses are counted, listed in `build/drum_fidelity.txt`, and reported to the user. Each model gets one round of spec §6 steps 1–2 (find and fix the cause) against strict. A model still missing loose after that means: stop and ask the user (spec §6 step 3).
- Voice lifetime (this plan, spec addendum): the new models fade out from 5.5 s and end at 6 s. At maximum DECAY the originals ring 9–15 s, while the M1 tests require every voice to end within 6.5 s. The fidelity window (1.5 s) is unaffected.
- New files: `/* SPDX-License-Identifier: GPL-3.0-only` + `Drum machine fork: 2026 DEADACTIVE */` (firmware files keep the Felucca copyright line as in M1-B).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Cost (user decision, M1-C): the four heavy models (KBOOM KPUNC SSNAP SCRAK) count **2** toward the voice cap `DRUM_MAXV` 8, so at most 4 of them ring at once. `tests/drum_cost_ref.txt` `extreme_max 2627` must still hold. If it breaks, stop and ask the user; never raise it.

## Review Focus

1. **Q24 overflow at knob corners and fast rolls.** An int32 wrap would be a full-scale click. In host builds with `-DDM_QCHECK` every `qm`/`qdiv` result is range-checked. `drum_fidelity` (all 972 renders) and `drum_test_q` (extremes, stress) assert `dm_qover == 0` (Tasks 1, 2, 8).
2. **Quiet hits must still end.** At velocity 1 the output may never cross the end threshold in an odd way. `test_m1c_ends` checks every new model at velocity 1 and 127 with each knob at min and max: it ends by 6.0 s (Task 8).
3. **Swapping the model while a new-model voice rings.** The state union is reused by a different model. `test_m1c_swap`: KBOOM ringing, swap to SSNAP, hit: bounded, no overflow, the old voice declicked (Task 8).
4. **The lifetime fade.** At maximum DECAY a voice must fade over 5.5–6 s without a click and end at 6 s. `test_q24` (dm_putq) and `test_m1c_ends` cover it (Tasks 1, 8).
5. **Division by zero in per-sample math.** Every `qdiv` denominator must stay non-zero at all knob corners: TUNE -24 (lowest f0), TONE 0, CHAR 0. This is covered by the fidelity grid (all corners) under `DM_QCHECK`, and by the extremes test (Tasks 3–8).

---

## File Structure

| File | Responsibility |
|---|---|
| `tests/fetch_ref.sh` | (new) sparse clone of `pichenettes/eurorack` @ `08460a6` + `pichenettes/stmlib` @ `e3bd7c9` into `build/drum_ref/eurorack` |
| `tests/drum_ref_grid.h` | (new) the fidelity grid shared by both sides: names, base notes, knob defaults, grid points |
| `tests/drum_metrics.h` | (new) the metrics (envelope, decay, brightness, pitch, peak) and the strict / loose comparison, one implementation for both sides |
| `tests/drum_ref.cc` | (new) host C++ driver of the original code: `grid` (metrics file), `wav` (reference WAVs), `selftest` (the metrics accept a perfect 44.1 kHz port, reject a 3 dB error) |
| `tests/drum_fidelity.c` | (new) renders ours over the grid, compares, prints strict / loose counts |
| `tests/run_drum_tests.sh` | (edit) a second `drum_test` build with `-DDM_QCHECK`; fetch, (re)build the reference metrics when stale, run `drum_fidelity`, loud SKIPPED when offline |
| `firmware/src/dm_state.h` | (new) per-voice state union of the new models; `qsvf_t`, `qpole_t` |
| `firmware/src/core.h` | (edit) `#include "dm_state.h"`; `dvoice_t.ms`; `dmodel_t.weight` |
| `firmware/src/drum_core.c` | (edit) the weighted voice cap (`dv_weight`, `dv_make_room(w)`, `dv_alloc(..., w)`) |
| `firmware/src/dm_dsp.c` | (edit) the Q24 toolkit, `dm_putq`, `dm_qend` |
| `firmware/src/dm_kick.c` | (edit) KBOOM, KPUNC |
| `firmware/src/dm_snare.c` | (edit) SSNAP, SCRAK |
| `firmware/src/dm_metal.c` | (edit) HMETL, HNOIS |
| `firmware/src/dmodels.c` | (edit) six entries after DM_SMPL |
| `firmware/src/icons.c` | (edit) labels PUNCH, FM, NOISE |
| `tests/drum_test.c` | (edit) `test_q24`, golden "every model has an entry", health / choke / ends / swap checks of the new models |
| `tests/drum_golden.txt`, `tests/target_budget.py`, `tests/target_budget.txt` | (edit) the new models |

---
### Task 1: Q24 toolkit, per-voice model state, overflow counter

**Files:**
- Create: `firmware/src/dm_state.h`
- Modify: `firmware/src/core.h` (include + `dvoice_t.ms`), `firmware/src/dm_dsp.c` (append the toolkit), `tests/drum_test.c` (`test_q24`, golden entry check, cost skipped in the overflow-check build), `tests/run_drum_tests.sh` (a second `drum_test` build with `-DDM_QCHECK`)

**Interfaces:**
- Produces (used by Tasks 2–8): `QONE`, `Q24(x)`, `qm`, `qdiv`, `qdecay(x, k)` (an envelope step: rounded, never stuck), `qabs`, `qmin`, `qmax`, `qlim`, `qsat`, `qsoftclip`, `qknob(p)`, `qratio(v, st)` (v·2^(st/12), st Q24 semitones), `qtan_dirty/fast/acc(f)`, `qinv12(q12)`, `qsvf_set/qsvf_tick` (returns hp; `*lp`, `*bp`), `qpole_set/qpole_lp`, `qrand(uint32_t *)`, `qsqrt`, `qnote(note)` (f at 44.1 kHz), `q48(f)` (f·44100/48000), `C44_*`, `K44`, `qsine(ph)`, `DM_FLOAT1`, `DM_QEND`, `DM_QQUIET`, `LIFE_A`, `LIFE_B`, `dm_putq(v, out, i, y)`, `dm_qend(v, n, pk, guard)`; types `qsvf_t`, `qpole_t`, `dm_state_t` (union, `dvoice_t.ms`); host: `dm_qover` (counted only when built with `-DDM_QCHECK`: `drum_test_q` and `drum_fidelity`; the plain `drum_test` measures the cost, which the range checks would inflate by ~15 %).

- [ ] **Step 1: Write the failing tests** (append to `tests/drum_test.c` before `int main`, register `test_q24();` first in `main`)

```c
#ifndef DM_QCHECK
static uint32_t dm_qover;                        /* counted only by the -DDM_QCHECK build (drum_test_q) */
#endif

static double svf_ref(double g, double r, double in, double *s1, double *s2)   /* stmlib Svf in double: bp */
{
    double h = 1.0 / (1.0 + r * g + g * g), hp = (in - (r + g) * *s1 - *s2) * h, bp = g * hp + *s1, lp;
    *s1 = g * hp + bp;
    lp = g * bp + *s2;
    *s2 = g * bp + lp;
    return bp;
}

static void test_q24(void)
{
    double e, emax, s1 = 0, s2 = 0, ps = 0;
    int32_t i, lp, bp, out[1];
    uint32_t st = 1;
    qsvf_t f;
    qpole_t p;
    dvoice_t v;
    check("q24: qm / qdiv on exact values",
          qm(QONE, 12345) == 12345 && qm(-QONE / 2, 3 * QONE) == -3 * QONE / 2 && qdiv(QONE, 4 * QONE) == QONE / 4);
    for (emax = 0, i = -256; i <= 256; i++) {
        double x = i / 4.0;
        e = fabs(qsat(Q24(x)) / (double)QONE - x / (1 + fabs(x)));
        emax = e > emax ? e : emax;
    }
    check("q24: qsat = x / (1 + |x|) within 1e-6", emax < 1e-6);
    for (emax = 0, i = -400; i <= 400; i++) {
        double x = i / 100.0, r = x < -3 ? -1 : x > 3 ? 1 : x * (27 + x * x) / (27 + 9 * x * x);
        e = fabs(qsoftclip(Q24(x)) / (double)QONE - r);
        emax = e > emax ? e : emax;
    }
    check("q24: qsoftclip = stmlib SoftClip within 1e-6", emax < 1e-6);
    for (emax = 0, i = -960; i <= 1080; i += 7) {                /* -96 .. +108 semitones */
        double s = i / 10.0, v0 = s < 0 ? 64.0 * QONE : QONE / 1024.0;
        e = fabs(qratio((int32_t)v0, Q24(s)) / v0 / pow(2, s / 12) - 1);
        emax = e > emax ? e : emax;
    }
    check("q24: qratio = 2^(st / 12) within 2e-4 over -96..108 semitones", emax < 2e-4);
    for (emax = 0, i = 1; i <= 450; i++) {                       /* f = 0.001 .. 0.45 */
        double fq = i / 1000.0, x = M_PI * fq;
        e = fabs(qtan_dirty(Q24(fq)) / (double)QONE / (x * (1 + 0.3736 * x * x)) - 1);
        e = fmax(e, fabs(qtan_fast(Q24(fq)) / (double)QONE / (x * (1 + x * x * (0.326 + 0.1823 * x * x))) - 1));
        e = fmax(e, fabs(qtan_acc(Q24(fq)) / (double)QONE /
                            (x * (1 + x * x * (3.333314036e-01 + x * x * (1.333923995e-01 + x * x * (5.33740603e-02 +
                             x * x * (2.900525e-03 + x * x * 9.5168091e-03)))))) - 1));
        emax = e > emax ? e : emax;
    }
    check("q24: qtan_dirty / fast / acc = the stmlib approximations within 1e-4", emax < 1e-4);
    for (emax = 0, i = 0; i <= 400; i++) {
        e = fabs(qsqrt(Q24(i / 100.0)) / (double)QONE - sqrt(i / 100.0));
        emax = e > emax ? e : emax;
    }
    check("q24: qsqrt within 1e-6", emax < 1e-6);
    check("q24: qrand is stmlib's LCG", qrand(&st) == (int32_t)((1u * 1664525u + 1013904223u) >> 8));
    qsvf_set(&f, qtan_acc(Q24(50.0 / 44100.0)), Q24(0.01));      /* 50 Hz, Q 100: the hardest case of the models */
    f.s1 = f.s2 = 0;
    qpole_set(&p, qtan_fast(Q24(1000.0 / 44100.0)));
    p.s = 0;
    for (emax = 0, i = 0; i < 4410; i++) {
        double g = tan(M_PI * 50.0 / 44100.0), in = i == 0 ? 1.0 : 0.0, gp = qtan_fast(Q24(1000.0 / 44100.0)) / (double)QONE;
        double ref = svf_ref(g, 0.01, in, &s1, &s2), lpr = (gp * in + ps) / (1 + gp);
        ps = gp * (in - lpr) + lpr;
        qsvf_tick(&f, i == 0 ? QONE : 0, &lp, &bp);
        e = fmax(fabs(bp / (double)QONE - ref), fabs(qpole_lp(&p, i == 0 ? QONE : 0) / (double)QONE - lpr));
        emax = e > emax ? e : emax;
    }
    check("q24: qsvf (50 Hz, Q 100) and qpole follow the double filters within 1e-4 for 0.1 s", emax < 1e-4);
    check("q24: qnote(69) = 440 Hz at 44.1 kHz", fabs(qnote(69) / (double)QONE * 44100.0 / 440.0 - 1) < 1e-4);
    memset(&v, 0, sizeof v);
    out[0] = 0;
    dm_putq(&v, out, 0, QONE);
    e = out[0];
    v.t = LIFE_A + (LIFE_B - LIFE_A) / 2;
    out[0] = 0;
    dm_putq(&v, out, 0, QONE);
    check("q24: dm_putq maps 1.0 to DM_FLOAT1 x VOICE_FS, half way through the fade to half",
          fabs(e - DM_FLOAT1 * (double)VOICE_FS / 32768) <= 1 && fabs(out[0] - e / 2) <= 1);
    v.t = LIFE_B;
    out[0] = 0;
    dm_putq(&v, out, 0, QONE);
    check("q24: dm_putq is silent from LIFE_B", out[0] == 0);
#ifdef DM_QCHECK
    dm_qover = 0;
    qm(Q24(100), Q24(100));
    check("q24: an overflowing product is counted (DM_QCHECK)", dm_qover == 1);
    dm_qover = 0;
#else
    (void)dm_qover;
#endif
    {
        int32_t x = QONE, n = 0;                          /* a decay reaches 0 (no rounding fixed point) */
        while (x && n < 200000) {
            x = qdecay(x, Q24(1.0 - 1.0 / 4410.0));
            n++;
        }
        check("q24: qdecay reaches 0 (rounding alone would stop at ~2200 LSB)", x == 0);
    }
}
```

In `test_golden`, after the `while (fscanf(...))` loop (before `fclose(f)`), make a model without an entry fail. Replace the loop with:
```c
    {
        uint32_t seen[NMODELS] = {0};
        while (fscanf(f, "%31s %x", name, &want) == 2)
            for (mi = 0; mi < NMODELS; mi++)
                if (!strcmp(name, N_MODEL[mi])) {
                    seen[mi] = 1;
                    if (have[mi] != want) {
                        printf("     %s render changed: %08x, golden %08x\n", name, have[mi], want);
                        ok = 0;
                    }
                }
        for (mi = 0; mi < NMODELS; mi++)
            if (!seen[mi]) {
                printf("     %s has no golden entry (GOLDEN_UPDATE=1 adds it; check the diff adds only it)\n", N_MODEL[mi]);
                ok = 0;
            }
    }
```

- [ ] **Step 2: Run to verify they fail**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `qm` / `QONE` undeclared (compile error).

- [ ] **Step 3: `firmware/src/dm_state.h`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Per-voice state of the M1-C models (dvoice_t.ms): one union member per model (Tasks 3-7 add them), and the
 * Q24 filter states of the toolkit (dm_dsp.c). */
typedef struct { int32_t g, r, h, s1, s2; } qsvf_t;     /* stmlib Svf: coefficients, two states */
typedef struct { int32_t g, gi, s; } qpole_t;           /* stmlib OnePole */
typedef union {
    int32_t raw[2];
} dm_state_t;
```

In `firmware/src/core.h` add after `#include <stdint.h>`:
```c
#include "dm_state.h"
```
and in `dvoice_t` after `voice_t sv;                  /* sample playback (SAMPLE model, layer) */`:
```c
    dm_state_t ms;               /* M1-C model state (dm_state.h) */
```

- [ ] **Step 4: Append the toolkit to `firmware/src/dm_dsp.c`** (after `dm_end`, at the end of the file)

```c
/* ---- M1-C: integer ports of Mutable Instruments' drum algorithms (Plaits, stmlib), MIT licence:
 * Copyright 2012-2016 Emilie Gillet (emilie.o.gillet@gmail.com).
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions: The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE. */
/* ---- Q24 toolkit (the M1-C models). 1.0 = 1 << 24, range +-128; products and quotients in 64 bits (the
 * pi32v2 multiplies, divides and shifts 64-bit natively, no helper calls). Q24(x) is for constants only: the
 * compiler folds it, no float reaches the firmware. */
#define QONE (1 << 24)
#define Q24(x) ((int32_t)((x) * 16777216.0 + ((x) < 0 ? -0.5 : 0.5)))
#ifdef DM_QCHECK                                    /* host test builds: count Q24 results that do not fit (tests assert 0) */
static uint32_t dm_qover;
static inline int32_t qfit(int64_t p)
{
    if (p > INT32_MAX || p < INT32_MIN)
        dm_qover++;
    return (int32_t)p;
}
#else
#define qfit(p) ((int32_t)(p))
#endif
static inline int32_t qm(int32_t a, int32_t b) { return qfit(((int64_t)a * b + (1 << 23)) >> 24); }
static inline int32_t qdiv(int32_t a, int32_t b) { return qfit(((int64_t)a << 24) / b); }
/* x * k for a decay (k < 1), rounded; where rounding stops the decay (below 0.5 / (1 - k) LSB, a tail that never
 * ends) it steps one LSB toward 0 instead */
static inline int32_t qdecay(int32_t x, int32_t k)
{
    int32_t y = qm(x, k);
    return y != x ? y : x > 0 ? x - 1 : x < 0 ? x + 1 : 0;
}
static inline int32_t qabs(int32_t x) { return x < 0 ? -x : x; }
static inline int32_t qmin(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t qmax(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t qlim(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline int32_t qsat(int32_t x) { return qdiv(x, QONE + qabs(x)); }          /* x / (1 + |x|) */
static inline int32_t qsoftclip(int32_t x)                                          /* stmlib SoftClip */
{
    int32_t x2;
    if (x <= -3 * QONE)
        return -QONE;
    if (x >= 3 * QONE)
        return QONE;
    x2 = qm(x, x);
    return qdiv(qm(x, 27 * QONE + x2), 27 * QONE + 9 * x2);
}
/* p (0..127) as 0..1 */
static inline int32_t qknob(int32_t p) { return (int32_t)(((int64_t)qlim(p, 0, 127) << 24) / 127); }
/* v * 2^(st / 12), st in Q24 semitones; saturates at +-2^31 */
static int32_t qratio(int32_t v, int32_t st)
{
    int64_t oct = ((int64_t)st * 1398101) >> 24;          /* st / 12 in Q24 (1398101 = 2^24 / 12) */
    int32_t n = (int32_t)(oct >> 24), f = (int32_t)(oct & 0xFFFFFF);   /* floor octave, fraction Q24 */
    int64_t m = 22370;                                     /* 2^f = sum (f ln 2)^k / k!, k <= 5 (0.3 cent) */
    int64_t y;
    m = 161365 + ((m * f) >> 24);
    m = 931204 + ((m * f) >> 24);
    m = 4030332 + ((m * f) >> 24);
    m = 11629080 + ((m * f) >> 24);
    m = QONE + ((m * f) >> 24);
    y = ((int64_t)v * m) >> 24;
    if (n >= 0)
        y = n > 30 ? (y ? (y > 0 ? INT32_MAX : INT32_MIN) : 0) : y << n;
    else
        y = n < -40 ? 0 : y >> -n;
    return y > INT32_MAX ? INT32_MAX : y < INT32_MIN ? INT32_MIN : (int32_t)y;
}
/* stmlib tan approximations of pi f (f: Q24 normalized frequency) */
static inline int32_t qtan_dirty(int32_t f)
{
    int32_t x = qm(f, Q24(3.14159265358979)), x2 = qm(x, x);
    return qm(x, QONE + qm(Q24(0.3736), x2));
}
static inline int32_t qtan_fast(int32_t f)
{
    int32_t x = qm(f, Q24(3.14159265358979)), x2 = qm(x, x);
    return qm(x, QONE + qm(x2, Q24(0.3260) + qm(Q24(0.1823), x2)));
}
static inline int32_t qtan_acc(int32_t f)
{
    int32_t x = qm(f, Q24(3.14159265358979)), x2 = qm(x, x), p;
    p = Q24(9.5168091e-03);
    p = Q24(2.900525e-03) + qm(p, x2);
    p = Q24(5.33740603e-02) + qm(p, x2);
    p = Q24(1.333923995e-01) + qm(p, x2);
    p = Q24(3.333314036e-01) + qm(p, x2);
    return qm(x, QONE + qm(p, x2));
}
/* 1 / q with q in Q12 (q up to ~5e5): the damping of a resonator */
static inline int32_t qinv12(int32_t q12) { return (int32_t)((1LL << 36) / q12); }
/* stmlib Svf (trapezoidal, Simper); state type qsvf_t in dm_state.h */
static inline void qsvf_set(qsvf_t *f, int32_t g, int32_t r)
{
    f->g = g;
    f->r = r;
    f->h = qdiv(QONE, QONE + qm(r, g) + qm(g, g));
}
/* one sample: returns hp, *lp, *bp */
static inline int32_t qsvf_tick(qsvf_t *f, int32_t in, int32_t *lp, int32_t *bp)
{
    int32_t hp = qm(in - qm(f->r + f->g, f->s1) - f->s2, f->h), b, l;
    b = qm(f->g, hp) + f->s1;
    f->s1 = qm(f->g, hp) + b;
    l = qm(f->g, b) + f->s2;
    f->s2 = qm(f->g, b) + l;
    *lp = l;
    *bp = b;
    return hp;
}
/* stmlib OnePole (trapezoidal); state type qpole_t in dm_state.h */
static inline void qpole_set(qpole_t *p, int32_t g) { p->g = g; p->gi = qdiv(QONE, QONE + g); }
static inline int32_t qpole_lp(qpole_t *p, int32_t in)
{
    int32_t lp = qm(qm(p->g, in) + p->s, p->gi);
    p->s = qm(p->g, in - lp) + lp;
    return lp;
}
/* stmlib Random: the same LCG, per voice; Q24 in [0, 1) */
static inline int32_t qrand(uint32_t *st)
{
    *st = *st * 1664525u + 1013904223u;
    return (int32_t)(*st >> 8);
}
/* sqrt of x (Q24, >= 0) */
static int32_t qsqrt(int32_t x)
{
    uint64_t v = (uint64_t)(x < 0 ? 0 : x) << 24, r = 0, b = 1ULL << 62;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (int32_t)r;
}
/* MIDI note -> normalized frequency at 44.1 kHz (Q24); its 48 kHz equivalent (f * 44100 / 48000) */
static inline int32_t qnote(int32_t note) { return qratio(Q24(8.17579891564 / 44100.0 * 64.0), note << 24) >> 6; }
static inline int32_t q48(int32_t f) { return qm(f, Q24(44100.0 / 48000.0)); }
/* per-sample coefficient c of Plaits (48 kHz) at 44.1 kHz: 1 - (1 - c)^(48000 / 44100) (literals) */
#define C44_75 Q24(0.7788451)
#define C44_50 Q24(0.5297289)
#define C44_10 Q24(0.1083469)
#define C44_05 Q24(0.0542996)
#define C44_04 Q24(0.0434595)
#define C44_005 Q24(0.005441)
#define C44_002 Q24(0.0021767)
#define K44 Q24(48000.0 / 44100.0)          /* small per-sample rates scale by this */
static inline int32_t qsine(int32_t ph) { return sine_i((uint32_t)ph << 8) * 512; }   /* sin(2 pi ph), any phase */

/* ---- output and lifetime of the M1-C models */
#define DM_FLOAT1 24576                    /* Q15 level of a reference sample of 1.0 */
#define DM_QEND 1057                       /* -84 dB re 1.0 (Q24): quiet */
#define DM_QQUIET 64                       /* quiet blocks in a row that end a voice (46 ms: > half a period of 11 Hz) */
#define LIFE_A ((uint32_t)FS * 11u / 2u)   /* M1-C voices fade out from 5.5 s ... */
#define LIFE_B ((uint32_t)FS * 6u)         /* ... and end at 6 s (their longest DECAY rings 9-15 s; the M1 limit is 6.5 s) */

/* add one M1-C sample y (Q24, 1.0 = a reference sample of 1.0): no velocity gain (the model applies its
 * accent itself), the lifetime fade, the declick memory */
static inline void dm_putq(dvoice_t *v, int32_t *out, uint32_t i, int32_t y)
{
    uint32_t t = v->t + i;
    int32_t x = clamp((int32_t)(((int64_t)y * DM_FLOAT1 + (1 << 23)) >> 24), -4 * 32768, 4 * 32768);   /* rounded */
    if (t >= LIFE_A)
        x = t >= LIFE_B ? 0 : (int32_t)((int64_t)x * (int32_t)(LIFE_B - t) / (int32_t)(LIFE_B - LIFE_A));
    x = (int32_t)(((int64_t)x * VOICE_FS + (1 << 14)) >> 15);
    out[i] += x;
    v->last = x;
}

/* after a block of n (peak pk): the voice ends at 6 s, or after guard samples once DM_QQUIET blocks in a row
 * were quiet (a low note spends whole blocks near its zero crossings; v->x[0] counts, unused by these models) */
static inline void dm_qend(dvoice_t *v, uint32_t n, int32_t pk, uint32_t guard)
{
    v->t += n;
    v->x[0] = pk < DM_QEND ? v->x[0] + 1 : 0;
    if (v->t >= LIFE_B || (v->t > guard && v->x[0] >= DM_QQUIET))
        dm_end(v);
}
```

At the start of `test_cost` in `tests/drum_test.c` add (the range checks would inflate the instruction count):
```c
#ifdef DM_QCHECK
    printf("     cost: measured by the build without overflow checks (drum_test)\n");
    return;
#endif
```

In `tests/run_drum_tests.sh` add after the `"$OUT/drum_test"` line:
```sh
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -DDM_QCHECK -Ibuild/gen -Ifirmware/src -o "$OUT/drum_test_q" tests/drum_test.c -lm
"$OUT/drum_test_q" > "$OUT/drum_test_q.txt" || { grep FAIL "$OUT/drum_test_q.txt"; exit 1; }
echo "drum_test with Q24 overflow checks: all passed"
```

- [ ] **Step 5: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: every `q24:` line `ok`, then `drum_test with Q24 overflow checks: all passed`; the golden check still `ok` (every existing model has its entry); `ALL DRUM HOST TESTS PASSED`. The firmware build is checked in Task 8; nothing calls the toolkit yet.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/dm_state.h firmware/src/core.h firmware/src/dm_dsp.c tests/drum_test.c tests/run_drum_tests.sh
git commit -m "drum core: Q24 toolkit (64-bit products), per-voice model state, overflow counter; golden needs every model

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 2: The reference and the fidelity harness

**Files:**
- Create: `tests/fetch_ref.sh`, `tests/drum_ref_grid.h`, `tests/drum_metrics.h`, `tests/drum_ref.cc`, `tests/drum_fidelity.c`
- Modify: `tests/run_drum_tests.sh`

**Interfaces:**
- Consumes: Task 1's `DM_FLOAT1`, `dm_qover`; `render_track`, `drum_set_model`, `drum_hit`, `N_MODEL`, `DMODELS`.
- Produces: `build/drum_ref/metrics.bin` (header `"DMR1"`, 6 × 81 × 2 `dmm_t`), `build/drum_renders/ref_NAME.wav`, `build/drum_fidelity.txt`; per model the line `NAME: strict S / 162, loose L / 162` and the check `fidelity: NAME within the loose tolerances on all 162 renders, bounded`. A model is found by its `N_MODEL` name, so Tasks 3–7 only add the model.
- Metric definitions (all research-verified against the reference itself: a windowed-sinc 44.1 kHz copy passes strict on 126 / 126 sampled renders, a −3 dB copy fails loose on all):
  - envelope: RMS windows of 10 ms (noise-based: 40 ms), but at least two periods of the hit's base pitch (a 12 Hz kick measures its envelope, not its phase);
  - peak: the loudest 1 ms RMS window (noise-based: 40 ms; a short window of noise is luck);
  - the 48 kHz side is low-passed at 20 kHz first (content a 44.1 kHz port cannot have does not count);
  - decay: end of the last window ≥ loudest − 40 dB (± one window of slack);
  - brightness: power centroid 0–20 kHz of the first 200 ms (noise-based: mean of 4 × 50 ms);
  - kick pitch: zero-crossing periods per 50 ms window (≥ 3 crossings, above −40 dB).

- [ ] **Step 1: The fetch script and the shared grid** — create `tests/fetch_ref.sh` (`chmod +x`):

```sh
#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Fetch the M1-C reference: Mutable Instruments' original drum code (MIT) for the host fidelity tests only:
# never built into the firmware, never committed. Pinned commits, sparse, into build/drum_ref/eurorack
# (stmlib inside it, where the sources include it from).
#   tests/fetch_ref.sh       exit 0: the reference is there; exit 1: it cannot be fetched (offline)
#   DRUM_REF_OFFLINE=1       behave as offline (tests the SKIPPED path)
set -e
cd "$(dirname "$0")/.."
EURO=08460a69a7e1f7a81c5a2abcc7189c9a6b7208d4
STM=e3bd7c9cc00e4364166f9905c0509b6ffd0535ec
D=build/drum_ref/eurorack
[ "${DRUM_REF_OFFLINE:-0}" = 1 ] && exit 1
[ -f "$D/.ok-$EURO-$STM" ] && exit 0
rm -rf "$D"
mkdir -p build/drum_ref
git clone -q --filter=blob:none --no-checkout https://github.com/pichenettes/eurorack.git "$D" || exit 1
git -C "$D" sparse-checkout set --no-cone '/plaits/dsp/' '/plaits/resources.h' '/plaits/resources.cc' || exit 1
git -C "$D" checkout -q "$EURO" || exit 1
git clone -q --filter=blob:none --no-checkout https://github.com/pichenettes/stmlib.git "$D/stmlib" || exit 1
git -C "$D/stmlib" checkout -q "$STM" || exit 1
touch "$D/.ok-$EURO-$STM"
```

`tests/drum_ref_grid.h`:
```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The M1-C fidelity grid, shared by tests/drum_ref.cc (the reference renders) and tests/drum_fidelity.c
 * (ours): per model its base MIDI note and knob defaults (= its DMODELS edit[] defaults, checked by
 * drum_fidelity.c), and the grid TUNE / DECAY / TONE / CHAR in {min, default, max}, velocity 127 / 64. */
#include <math.h>
#define REF_NMODELS 6
#define REF_SECONDS 1.5
#define REF_NGRID 81
static const char *const REF_NAME[REF_NMODELS] = {"KBOOM", "KPUNC", "SSNAP", "SCRAK", "HMETL", "HNOIS"};
static const int REF_NOTE[REF_NMODELS] = {31, 31, 55, 55, 60, 72};          /* MIDI note at TUNE 0 */
static const int REF_DEF[REF_NMODELS][4] = {                                 /* TUNE DECAY TONE CHAR */
    {0, 64, 64, 32}, {0, 64, 64, 64}, {0, 64, 64, 64}, {0, 64, 64, 64}, {0, 40, 80, 40}, {0, 40, 80, 40}};
static const int REF_VEL[2] = {127, 64};
static const int REF_KICK[REF_NMODELS] = {1, 1, 0, 0, 0, 0};                  /* pitch track checked */
static const int REF_NOISY[REF_NMODELS] = {0, 0, 1, 1, 1, 1};                 /* noise-based: longer windows */
static double ref_hz(int note) { return 440.0 * pow(2.0, (note - 69) / 12.0); }
/* grid point g (0..80) of model m: knob k's value */
static int ref_knob(int m, int g, int k)
{
    static const int D[4] = {1, 3, 9, 27};
    int lv = (g / D[k]) % 3;                     /* 0 min, 1 default, 2 max */
    if (k == 0)
        return lv == 0 ? -24 : lv == 1 ? REF_DEF[m][0] : 24;
    return lv == 0 ? 0 : lv == 1 ? REF_DEF[m][k] : 127;
}
```

`tests/drum_ref.cc`:
```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Host-only reference for the M1-C fidelity tests: renders Mutable Instruments' original drum code (MIT,
// fetched by tests/fetch_ref.sh into build/drum_ref/, never part of the firmware) at 48 kHz, with the
// engines' knob mappings (plaits/dsp/engine/{bass,snare,hi_hat}_drum_engine.cc @ 08460a6).
//   drum_ref grid FILE   the metrics (tests/drum_metrics.h) of every grid render (tests/drum_ref_grid.h)
//   drum_ref wav DIR     DIR/ref_NAME.wav: the hits of tests/drumsim.c (velocity 127 96 64 32, each knob low / high)
//   drum_ref selftest    the metrics accept a perfect port at 44.1 kHz and reject a 3 dB level error
//   drum_ref peaks       peak |out| of each model's default hit at velocity 127
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include "plaits/dsp/drums/analog_bass_drum.h"
#include "plaits/dsp/drums/synthetic_bass_drum.h"
#include "plaits/dsp/drums/analog_snare_drum.h"
#include "plaits/dsp/drums/synthetic_snare_drum.h"
#include "plaits/dsp/drums/hi_hat.h"
#include "plaits/dsp/fx/overdrive.h"
#include "plaits/dsp/engine/engine.h"
extern "C" {
#include "drum_ref_grid.h"
#include "drum_metrics.h"
}
using namespace plaits;

static std::vector<float> render(int m, int note, int decay, int tone, int chr, int vel, double seconds)
{
  const float f0 = NoteToFrequency((float)note), timbre = tone / 127.0f, morph = decay / 127.0f,
              harmonics = chr / 127.0f, accent = vel / 127.0f;
  const size_t n = (size_t)(seconds * kSampleRate);
  std::vector<float> out(n + kBlockSize);
  float temp[2 * kBlockSize], blk[kBlockSize];
  static AnalogBassDrum abd; static SyntheticBassDrum sbd; static Overdrive od;
  static AnalogSnareDrum asd; static SyntheticSnareDrum ssd;
  static HiHat<SquareNoise, SwingVCA, true, false> hh1;
  static HiHat<RingModNoise, LinearVCA, false, true> hh2;
  abd.Init(); sbd.Init(); od.Init(); asd.Init(); ssd.Init(); hh1.Init(); hh2.Init();
  stmlib::Random::Seed(0x21);
  for (size_t done = 0, b = 0; done < n; b++) {   // block 0: idle (interpolators settle), block 1: trigger
    bool trig = b == 1;
    switch (m) {
    case 0: {
      float afm = std::min(harmonics * 4.0f, 1.0f);
      float sfm = std::max(std::min(harmonics * 4.0f - 1.0f, 1.0f), 0.0f);
      float drive = std::max(harmonics * 2.0f - 1.0f, 0.0f) * std::max(1.0f - 16.0f * f0, 0.0f);
      abd.Render(false, trig, accent, f0, timbre, morph, afm, sfm, blk, kBlockSize);
      od.Process(0.5f + 0.5f * drive, blk, kBlockSize);
      break;
    }
    case 1:
      sbd.Render(false, trig, accent, f0, timbre, morph, 0.4f - 0.25f * morph * morph,
                 std::min(harmonics * 2.0f, 1.0f), std::max(harmonics * 2.0f - 1.0f, 0.0f), blk, kBlockSize);
      break;
    case 2: asd.Render(false, trig, accent, f0, timbre, morph, harmonics, blk, kBlockSize); break;
    case 3: ssd.Render(false, trig, accent, f0, timbre, morph, harmonics, blk, kBlockSize); break;
    case 4: hh1.Render(false, trig, accent, f0, timbre, morph, harmonics, temp, temp + kBlockSize, blk, kBlockSize); break;
    default: hh2.Render(false, trig, accent, f0, timbre, morph, harmonics, temp, temp + kBlockSize, blk, kBlockSize); break;
    }
    if (b >= 1) {                                  // the hit starts at the trigger block
      memcpy(&out[done], blk, sizeof blk);
      done += kBlockSize;
    }
  }
  out.resize(n);
  return out;
}

static void wav_hdr(FILE *f, uint32_t frames)
{
  uint32_t sr = 48000, br = sr * 2, d = frames * 2, r = 36 + d;
  uint16_t one = 1, two = 2, bits = 16;
  uint32_t sixteen = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&r, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
  fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
  fwrite(&two, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
}

int main(int argc, char **argv)
{
  if (argc >= 3 && !strcmp(argv[1], "grid")) {     // the reference metrics of every grid render
    FILE *f = fopen(argv[2], "wb");
    uint32_t hdr[4] = {0x31524D44u, REF_NMODELS, REF_NGRID, (uint32_t)sizeof(dmm_t)};   // "DMR1"
    if (!f) { perror(argv[2]); return 1; }
    fwrite(hdr, sizeof hdr, 1, f);
    for (int m = 0; m < REF_NMODELS; m++)
      for (int g = 0; g < REF_NGRID; g++)
        for (int v = 0; v < 2; v++) {
          int note = REF_NOTE[m] + ref_knob(m, g, 0);
          std::vector<float> y = render(m, note, ref_knob(m, g, 1), ref_knob(m, g, 2), ref_knob(m, g, 3), REF_VEL[v],
                                        REF_SECONDS);
          dmm_t r;
          dmm_measure(y.data(), (int)y.size(), 48000.0, REF_NOISY[m], ref_hz(note), &r);
          fwrite(&r, sizeof r, 1, f);
        }
    fclose(f);
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "wav")) {     // the sequence of tests/drumsim.c: 4 velocities, each knob low / high
    static const int VEL[4] = {127, 96, 64, 32};
    for (int m = 0; m < REF_NMODELS; m++) {
      char path[512];
      int kn[4] = {0, REF_DEF[m][1], REF_DEF[m][2], REF_DEF[m][3]};
      snprintf(path, sizeof path, "%s/ref_%s.wav", argv[2], REF_NAME[m]);
      FILE *f = fopen(path, "wb");
      if (!f) { perror(path); return 1; }
      const size_t hit = (size_t)(REF_SECONDS * kSampleRate);
      wav_hdr(f, (uint32_t)(12 * hit));
      for (int h = 0; h < 12; h++) {
        int p[4] = {kn[0], kn[1], kn[2], kn[3]}, vel = h < 4 ? VEL[h] : 127;
        if (h >= 4)
          p[(h - 4) / 2] = (h - 4) / 2 == 0 ? ((h & 1) ? 12 : -12) : ((h & 1) ? 127 : 0);
        std::vector<float> y = render(m, REF_NOTE[m] + p[0], p[1], p[2], p[3], vel, REF_SECONDS);
        for (float s : y) {
          int16_t q = (int16_t)std::max(-32767.0f, std::min(32767.0f, s * 16384.0f));   // 1.0 = -6 dBFS
          fwrite(&q, 2, 1, f);
        }
      }
      fclose(f);
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "peaks")) {
    for (int m = 0; m < REF_NMODELS; m++) {
      std::vector<float> y = render(m, REF_NOTE[m], REF_DEF[m][1], REF_DEF[m][2], REF_DEF[m][3], 127, REF_SECONDS);
      float p = 0, pmax = 0;
      for (float s : y) p = std::max(p, std::fabs(s));
      for (int g = 0; g < REF_NGRID; g++) {
        std::vector<float> z = render(m, REF_NOTE[m] + ref_knob(m, g, 0), ref_knob(m, g, 1), ref_knob(m, g, 2),
                                      ref_knob(m, g, 3), 127, REF_SECONDS);
        for (float s : z) pmax = std::max(pmax, std::fabs(s));
      }
      printf("%s default %.4f grid_max %.4f\n", REF_NAME[m], p, pmax);
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "selftest")) {   // the metrics accept a perfect port at 44.1 kHz
    int bad = 0, total = 0;
    for (int m = 0; m < REF_NMODELS; m++)
      for (int g = 0; g < REF_NGRID; g += 4) {
        std::vector<float> y = render(m, REF_NOTE[m] + ref_knob(m, g, 0), ref_knob(m, g, 1), ref_knob(m, g, 2),
                                      ref_knob(m, g, 3), 127, REF_SECONDS);
        size_t n44 = (size_t)(REF_SECONDS * 44100);
        std::vector<float> z(n44), q(y.size());
        for (size_t i = 0; i < n44; i++) {             // windowed-sinc resampling 48 -> 44.1 kHz (20 kHz)
          double t = i * 48000.0 / 44100.0, acc = 0.0, fc = 20000.0 / 48000.0;
          long k0 = (long)t;
          for (long k = k0 - 31; k <= k0 + 32; k++) {
            double x = t - k, w;
            if (k < 0 || k >= (long)y.size() || fabs(x) >= 32.0) continue;
            w = 0.5 + 0.5 * cos(M_PI * x / 32.0);
            acc += y[k] * w * (x == 0.0 ? 2.0 * fc : sin(2.0 * M_PI * fc * x) / (M_PI * x));
          }
          z[i] = (float)acc;
        }
        for (size_t i = 0; i < y.size(); i++) q[i] = y[i] * 0.7f;   // -3.1 dB
        dmm_t a, b, c;
        char why[160];
        int noisy = REF_NOISY[m];
        dmm_measure(y.data(), (int)y.size(), 48000.0, noisy, ref_hz(REF_NOTE[m] + ref_knob(m, g, 0)), &a);
        dmm_measure(z.data(), (int)z.size(), 44100.0, noisy, ref_hz(REF_NOTE[m] + ref_knob(m, g, 0)), &b);
        dmm_measure(q.data(), (int)q.size(), 48000.0, noisy, ref_hz(REF_NOTE[m] + ref_knob(m, g, 0)), &c);
        total++;
        if (!dmm_compare(&a, &b, REF_KICK[m], &DMM_STRICT, why, sizeof why)) {
          printf("%s g%d resampled misses strict: %s\n", REF_NAME[m], g, why);
          bad++;
        }
        if (dmm_compare(&a, &c, REF_KICK[m], &DMM_LOOSE, why, sizeof why)) {
          printf("%s g%d -3 dB copy passes loose\n", REF_NAME[m], g);
          bad++;
        }
      }
    printf("selftest: %d of %d bad\n", bad, total);
    return bad != 0;
  }
  fprintf(stderr, "usage: drum_ref grid FILE | wav DIR | selftest | peaks\n");
  return 2;
}
```

- [ ] **Step 2: Run to verify it fails**

Run:
```bash
sh tests/fetch_ref.sh && E=build/drum_ref/eurorack && c++ -std=c++11 -O2 -DTEST -w -I$E -Itests -o build/drum_ref/drum_ref tests/drum_ref.cc $E/plaits/resources.cc $E/stmlib/dsp/units.cc $E/stmlib/utils/random.cc
```
Expected: FAIL — `drum_metrics.h` not found.

- [ ] **Step 3: `tests/drum_metrics.h`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M1-C fidelity metrics, one implementation for both sides: tests/drum_ref.cc measures the reference
 * (48 kHz) with it, tests/drum_fidelity.c measures ours (44.1 kHz). Everything is in time / Hz units, so
 * the two sample rates compare directly. Plain C (also compiles as C++). Signals: 1.0 = a reference
 * sample of 1.0 (ours are scaled back from the track output). */
#include <math.h>
#include <string.h>
#include <stdio.h>
#define DMM_SECONDS 1.5
#define DMM_NENV 150                   /* envelope windows over 1.5 s (10 ms; longer, see dmm_measure) */
#define DMM_NPITCH 30                  /* 50 ms pitch windows */
#define DMM_FFT 16384

typedef struct {
    int nenv;
    double env_db[DMM_NENV];           /* RMS per window, dB re 1.0 (-200 = silence) */
    double env_max;                    /* the loudest window */
    double win;                        /* envelope window, s */
    double peak_db;                    /* loudest 1 ms RMS window (noise-based: 40 ms), dB */
    double decay_s;                    /* end of the last window >= env_max - 40 dB */
    double centroid;                   /* Hz: power-weighted, 0..20 kHz, first 200 ms (noise-based: 4 x 50 ms) */
    double pitch[DMM_NPITCH];          /* Hz per 50 ms window from zero-crossing periods, 0 = not measurable */
} dmm_t;

static double dmm_db(double x) { return x > 1e-10 ? 20.0 * log10(x) : -200.0; }

/* in-place radix-2 FFT (re, im), n a power of two */
static void dmm_fft(double *re, double *im, int n)
{
    int i, j, k, len;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2.0 * M_PI / len, wr = cos(a), wi = sin(a);
        for (i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                double t;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

/* power-weighted centroid (Hz, 0..20 kHz) of y[a, a + len) with a Hann window */
static double dmm_centroid(const float *y, int a, int len, double sr)
{
    static double re[DMM_FFT], im[DMM_FFT];
    double num = 0.0, den = 0.0;
    int i;
    memset(re, 0, sizeof re);
    memset(im, 0, sizeof im);
    for (i = 0; i < len && i < DMM_FFT; i++)
        re[i] = y[a + i] * (0.5 - 0.5 * cos(2.0 * M_PI * i / len));
    dmm_fft(re, im, DMM_FFT);
    for (i = 1; i < DMM_FFT / 2; i++) {
        double f = i * sr / DMM_FFT, p = re[i] * re[i] + im[i] * im[i];
        if (f > 20000.0)
            break;
        num += f * p;
        den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

/* the audible band both sample rates share: a signal above 44.1 kHz is low-passed at 20 kHz first
 * (64-tap windowed sinc), so content a 44.1 kHz port cannot have does not count against it */
#define DMM_MAXN 80000
static const float *dmm_band(const float *y, int n, double sr)
{
    static float b[DMM_MAXN];
    static double tap[65], tap_sr;
    int i, k;
    if (sr <= 44100.0 || n > DMM_MAXN)
        return y;
    if (tap_sr != sr) {
        double fc = 20000.0 / sr;
        for (k = -32; k <= 32; k++)
            tap[k + 32] = (0.5 + 0.5 * cos(M_PI * k / 32.0)) * (k == 0 ? 2.0 * fc : sin(2.0 * M_PI * fc * k) / (M_PI * k));
        tap_sr = sr;
    }
    for (i = 0; i < n; i++) {
        double acc = 0.0;
        for (k = -32; k <= 32; k++)
            if (i - k >= 0 && i - k < n)
                acc += y[i - k] * tap[k + 32];
        b[i] = (float)acc;
    }
    return b;
}

/* y: n samples at sr from the hit; noisy: the noise-based models (40 ms windows); base_hz: the hit's base
 * pitch (0 = none): envelope windows span at least two of its periods, so a sub-bass tone measures its
 * envelope, not where in its cycle a window falls */
static void dmm_measure(const float *y, int n, double sr, int noisy, double base_hz, dmm_t *m)
{
    double win = noisy ? 0.040 : 0.010;
    int w, i, wlen, plen = (int)(sr * 0.050 + 0.5);
    double pk = 0.0;
    y = dmm_band(y, n, sr);
    memset(m, 0, sizeof *m);
    if (base_hz > 0.0 && 2.0 / base_hz > win)
        win = 2.0 / base_hz;
    wlen = (int)(sr * win + 0.5);
    m->win = win;
    m->nenv = (int)(DMM_SECONDS / win);
    m->env_max = -200.0;
    for (w = 0; w < m->nenv; w++) {
        double s = 0.0;
        for (i = 0; i < wlen && w * wlen + i < n; i++)
            s += (double)y[w * wlen + i] * y[w * wlen + i];
        m->env_db[w] = dmm_db(sqrt(s / wlen));
        if (m->env_db[w] > m->env_max)
            m->env_max = m->env_db[w];
    }
    {
        int l1 = (int)(sr * (noisy ? 0.040 : 0.001) + 0.5);   /* noise-based: 40 ms (a short window of noise is luck) */
        for (i = 0; i + l1 <= n; i += l1 / 2) {     /* 1 ms windows, half overlapping */
            double s = 0.0;
            int k;
            for (k = 0; k < l1; k++)
                s += (double)y[i + k] * y[i + k];
            if (s > pk)
                pk = s;
        }
        m->peak_db = dmm_db(sqrt(pk / l1));
    }
    for (w = 0; w < m->nenv; w++)
        if (m->env_db[w] >= m->env_max - 40.0)
            m->decay_s = (w + 1) * win;
    if (noisy) {
        for (w = 0; w < 4; w++)
            m->centroid += dmm_centroid(y, w * (int)(sr * 0.05), (int)(sr * 0.05), sr) / 4.0;
    } else {
        m->centroid = dmm_centroid(y, 0, (int)(sr * 0.2), sr);
    }
    for (w = 0; w < DMM_NPITCH; w++) {           /* rising zero crossings, interpolated; >= 2 per window */
        double first = -1.0, last = -1.0, s = 0.0;
        int c = 0;
        for (i = w * plen + 1; i < (w + 1) * plen && i < n; i++)
            s += (double)y[i] * y[i];
        if (dmm_db(sqrt(s / plen)) < m->env_max - 40.0)
            continue;
        for (i = w * plen + 1; i < (w + 1) * plen && i < n; i++)
            if (y[i - 1] < 0.0f && y[i] >= 0.0f) {
                double t = (i - 1 + y[i - 1] / (double)(y[i - 1] - y[i])) / sr;
                if (first < 0.0)
                    first = t;
                last = t;
                c++;
            }
        if (c >= 3)
            m->pitch[w] = (c - 1) / (last - first);
    }
}

/* tolerances: strict (target) and loose (fallback), spec §5 */
typedef struct { double env_db, env_floor, decay, centroid, pitch, peak_db; } dmm_tol_t;
static const dmm_tol_t DMM_STRICT = {1.5, 50.0, 0.10, 0.08, 0.03, 1.0};
static const dmm_tol_t DMM_LOOSE = {3.0, 40.0, 0.20, 0.15, 0.05, 2.0};

/* 1 = within tolerance; else 0 and why names the first metric that misses */
static int dmm_compare(const dmm_t *ref, const dmm_t *our, int kick, const dmm_tol_t *t, char *why, size_t wn)
{
    int w;
    double wsec = ref->win;
    if (fabs(our->peak_db - ref->peak_db) > t->peak_db) {
        snprintf(why, wn, "peak %.1f dB vs %.1f", our->peak_db, ref->peak_db);
        return 0;
    }
    for (w = 0; w < ref->nenv && w < our->nenv; w++)
        if (ref->env_db[w] > ref->env_max - t->env_floor && fabs(our->env_db[w] - ref->env_db[w]) > t->env_db) {
            snprintf(why, wn, "envelope at %.2f s: %.1f dB vs %.1f", w * wsec, our->env_db[w], ref->env_db[w]);
            return 0;
        }
    if (fabs(our->decay_s - ref->decay_s) > t->decay * ref->decay_s + wsec) {
        snprintf(why, wn, "decay %.3f s vs %.3f", our->decay_s, ref->decay_s);
        return 0;
    }
    if (ref->centroid > 0.0 && fabs(our->centroid / ref->centroid - 1.0) > t->centroid) {
        snprintf(why, wn, "brightness %.0f Hz vs %.0f", our->centroid, ref->centroid);
        return 0;
    }
    if (kick)
        for (w = 0; w < DMM_NPITCH; w++)
            if (ref->pitch[w] > 0.0 && our->pitch[w] > 0.0 && fabs(our->pitch[w] / ref->pitch[w] - 1.0) > t->pitch) {
                snprintf(why, wn, "pitch at %.2f s: %.1f Hz vs %.1f", w * 0.05, our->pitch[w], ref->pitch[w]);
                return 0;
            }
    return 1;
}
```

- [ ] **Step 4: The metric self-test**

Run the build command of Step 2 again, then `build/drum_ref/drum_ref selftest | tail -3`
Expected: `selftest: 0 of 126 bad` (takes ~6 s).

- [ ] **Step 5: `tests/drum_fidelity.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M1-C fidelity (docs/superpowers/specs/2026-10-05-m1c-drum-models-design.md §5): each new model against
 * the reference metrics tests/drum_ref.cc wrote (build/drum_ref/metrics.bin), over the grid of
 * tests/drum_ref_grid.h. Two tiers: a render that misses the loose tolerances fails; strict misses are
 * counted and listed in build/drum_fidelity.txt.   drum_fidelity METRICS.bin */
#define DM_QCHECK                                    /* every Q24 result range-checked (dm_qover) */
#include "drum_host.h"
#include "drum_ref_grid.h"
#include "drum_metrics.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define NREN ((uint32_t)(REF_SECONDS * FS))
static dmm_t ref[REF_NMODELS][REF_NGRID][2];
static int32_t buf[NREN + CTL];
static float y[NREN + CTL];

static int model_of(const char *name)
{
    uint32_t mi;
    for (mi = 0; mi < NMODELS; mi++)
        if (str_eq(N_MODEL[mi], name))
            return (int)mi;
    return -1;
}

int main(int argc, char **argv)
{
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0, *log = fopen("build/drum_fidelity.txt", "w");
    uint32_t hdr[4];
    int m, g, v, k, built = 0;
    if (!f || fread(hdr, sizeof hdr, 1, f) != 1 || hdr[0] != 0x31524D44u || hdr[1] != REF_NMODELS ||
        hdr[2] != REF_NGRID || hdr[3] != sizeof(dmm_t) || fread(ref, sizeof ref, 1, f) != 1) {
        check("fidelity: the reference metrics are readable (tests/drum_ref.cc grid)", 0);
        return 1;
    }
    fclose(f);
    for (m = 0; m < REF_NMODELS; m++) {
        int mi = model_of(REF_NAME[m]), ns = 0, nl = 0, shown = 0, defs = 1, bounded = 1;
        char what[160], why[160];
        if (mi < 0) {
            printf("     fidelity: %s not built yet\n", REF_NAME[m]);
            continue;
        }
        built++;
        for (k = 0; k < 4; k++)
            defs &= DMODELS[mi].edit[k].def == REF_DEF[m][k];
        snprintf(what, sizeof what, "fidelity: %s knob defaults match tests/drum_ref_grid.h", REF_NAME[m]);
        check(what, defs);
        for (g = 0; g < REF_NGRID; g++)
            for (v = 0; v < 2; v++) {
                dmm_t our;
                uint32_t i;
                host_init();
                drum_set_model(&trk[0], (uint32_t)mi);
                for (k = 0; k < 4; k++)
                    trk[0].p[P_E0 + k] = (int16_t)ref_knob(m, g, k);
                drum_hit(&trk[0], (uint32_t)REF_VEL[v]);
                render_track(&trk[0], buf, NREN);
                for (i = 0; i < NREN; i++) {                 /* back to "1.0 = a reference sample of 1.0" */
                    y[i] = (float)(buf[i] * (32768.0 / ((double)DM_FLOAT1 * VOICE_FS)));
                    bounded &= fabsf(y[i]) < 4.0f;
                }
                dmm_measure(y, (int)NREN, FS, REF_NOISY[m], ref_hz(REF_NOTE[m] + ref_knob(m, g, 0)), &our);
                if (dmm_compare(&ref[m][g][v], &our, REF_KICK[m], &DMM_STRICT, why, sizeof why)) {
                    ns++;
                    nl++;
                    continue;
                }
                if (log)
                    fprintf(log, "%s TUNE %d DECAY %d TONE %d CHAR %d vel %d: strict miss: %s\n", REF_NAME[m],
                            ref_knob(m, g, 0), ref_knob(m, g, 1), ref_knob(m, g, 2), ref_knob(m, g, 3), REF_VEL[v], why);
                if (dmm_compare(&ref[m][g][v], &our, REF_KICK[m], &DMM_LOOSE, why, sizeof why)) {
                    nl++;
                    continue;
                }
                if (shown++ < 5)
                    printf("     %s TUNE %d DECAY %d TONE %d CHAR %d vel %d: %s\n", REF_NAME[m], ref_knob(m, g, 0),
                           ref_knob(m, g, 1), ref_knob(m, g, 2), ref_knob(m, g, 3), REF_VEL[v], why);
                if (log)
                    fprintf(log, "%s TUNE %d DECAY %d TONE %d CHAR %d vel %d: LOOSE MISS: %s\n", REF_NAME[m],
                            ref_knob(m, g, 0), ref_knob(m, g, 1), ref_knob(m, g, 2), ref_knob(m, g, 3), REF_VEL[v], why);
            }
        printf("     %s: strict %d / %d, loose %d / %d (strict misses: build/drum_fidelity.txt)\n", REF_NAME[m], ns,
               2 * REF_NGRID, nl, 2 * REF_NGRID);
        snprintf(what, sizeof what, "fidelity: %s within the loose tolerances on all %d renders, bounded", REF_NAME[m],
                 2 * REF_NGRID);
        check(what, nl == 2 * REF_NGRID && bounded);
    }
    if (log)
        fclose(log);
#ifdef DM_QCHECK
    check("fidelity: no Q24 product overflowed in any render", dm_qover == 0);
#endif
    printf("fidelity: %d of %d models built\n", built, REF_NMODELS);
    printf(fails ? "drum_fidelity: %d FAILED\n" : "drum_fidelity: all passed\n", fails);
    return fails ? 1 : 0;
}
```

In `tests/run_drum_tests.sh` insert before `sh tests/guard_test.sh`:
```sh
REF=build/drum_ref
FID=""
if sh tests/fetch_ref.sh >/dev/null 2>&1; then
    if [ ! -f "$REF/metrics.bin" ] || [ tests/drum_ref.cc -nt "$REF/metrics.bin" ] ||
       [ tests/drum_ref_grid.h -nt "$REF/metrics.bin" ] || [ tests/drum_metrics.h -nt "$REF/metrics.bin" ]; then
        E="$REF/eurorack"
        c++ -std=c++11 -O2 -DTEST -w -I"$E" -Itests -o "$REF/drum_ref" tests/drum_ref.cc "$E/plaits/resources.cc" \
            "$E/stmlib/dsp/units.cc" "$E/stmlib/utils/random.cc"
        "$REF/drum_ref" selftest > "$REF/selftest.txt" || { tail -5 "$REF/selftest.txt"; exit 1; }
        tail -1 "$REF/selftest.txt"
        "$REF/drum_ref" grid "$REF/metrics.bin"
        mkdir -p build/drum_renders
        "$REF/drum_ref" wav build/drum_renders
    fi
    cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -Itests \
        -o "$OUT/drum_fidelity" tests/drum_fidelity.c -lm
    "$OUT/drum_fidelity" "$REF/metrics.bin"
else
    echo "fidelity: SKIPPED (offline: tests/fetch_ref.sh could not fetch the reference)"
    FID=" (fidelity SKIPPED: offline)"
fi
```
and change the last line to:
```sh
echo "ALL DRUM HOST TESTS PASSED$FID"
```

- [ ] **Step 6: Run the suite, online and offline**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh | tail -12`
Expected: `selftest: 0 of 126 bad`, six `fidelity: NAME not built yet` lines, `fidelity: 0 of 6 models built`, `drum_fidelity: all passed`, `ALL DRUM HOST TESTS PASSED`; `build/drum_renders/ref_KBOOM.wav` .. `ref_HNOIS.wav` exist.

Run: `DRUM_REF_OFFLINE=1 PYTHON=.venv/bin/python sh tests/run_drum_tests.sh | tail -2`
Expected: `fidelity: SKIPPED (offline: ...)` and `ALL DRUM HOST TESTS PASSED (fidelity SKIPPED: offline)`.

Run: `git status --short build/ ; git check-ignore -q build/drum_ref && echo ignored`
Expected: `ignored` (the original code is never committed).

- [ ] **Step 7: Commit**

```bash
git add tests/fetch_ref.sh tests/drum_ref_grid.h tests/drum_metrics.h tests/drum_ref.cc tests/drum_fidelity.c tests/run_drum_tests.sh
git commit -m "tests: M1-C reference (fetched, host only) and the two-tier fidelity harness

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 3: KBOOM and the weighted voice cap

**Files:**
- Modify: `firmware/src/dm_state.h`, `firmware/src/dm_kick.c`, `firmware/src/dmodels.c`, `tests/drum_test.c`, `tests/drum_golden.txt`, `firmware/src/core.h` (`dmodel_t.weight`), `firmware/src/drum_core.c` (the weighted voice cap)

**Interfaces:**
- Consumes: Task 1's toolkit (`qm` .. `dm_qend`, `qsvf_t`, `qpole_t`), Task 2's harness (finds the model by its name `"KBOOM"`).
- Produces: `DM_KBOOM` (appended after the last model in the enum), `kboom_t`, `kboom_start`, `kboom_tick`, `kboom_trigger`, `kboom_render`, `DM_KBOOM_DEF` (weight 2). Also `dmodel_t.weight`, `dv_weight`, `dv_make_room(w)`, `dv_alloc(t, pool, nv, w)` (Tasks 4–7 give their heavy models weight 2 in the DEF).

- [ ] **Step 1: Write the failing test** — in `test_kicks` of `tests/drum_test.c` add after the existing `model_health(...)` lines:
```c
    model_health(DM_KBOOM);
```
and append before `int main` (register `test_heavy_cap();` after `test_voice_cap();`; `voices_sounding` is defined above it):
```c
/* heavy models count 2 toward DRUM_MAXV (user decision, M1-C): 8 tracks of KBOOM held long ring as 4 */
static void test_heavy_cap(void)
{
    track_t *ot;
    dvoice_t *ov;
    uint32_t i;
    host_init();
    for (i = 0; i < NTRK; i++) {
        drum_set_model(&trk[i], DM_KBOOM);
        trk[i].p[P_E1] = 127;
        drum_hit(&trk[i], 127);
        render_mix(0, 0, CTL);
    }
    check("voice cap: a heavy model counts 2 (8 long KBOOM hits: 4 sound, weight 8)",
          voices_sounding() == DRUM_MAXV / 2 && dv_oldest(&ot, &ov) == DRUM_MAXV);
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_KBOOM` undeclared.

- [ ] **Step 3: The state** — in `firmware/src/dm_state.h` add before `typedef union {`:
```c
typedef struct {
    int32_t f0, q, scale, tone_f, leak, pulse_h, afm, sfm, pre, post;
    int32_t pulse, pulse_lp, fm_lp, retrig, lp_out, tone_lp;
    int32_t rem, fmrem, n;
    qsvf_t res;
} kboom_t;
```
and inside the union, after `int32_t raw[2];`:
```c
    kboom_t kb;
```

- [ ] **Step 4: The model** — append to `firmware/src/dm_kick.c` (after its existing `#define DM_..._DEF` lines):
```c
/* ---- M1-C: integer ports of Mutable Instruments' drum algorithms (Plaits, stmlib), MIT licence:
 * Copyright 2012-2016 Emilie Gillet (emilie.o.gillet@gmail.com).
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions: The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE. */

static void kboom_start(kboom_t *k, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel)
{
    int32_t h = qknob(chr), morph = qknob(decay), timbre = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), f48 = q48(f0), drive, d, d2, pga, pgb, pre, sq;
    k->f0 = f0;
    k->q = qratio(Q24(1500.0 / 65536.0), qm(morph, Q24(80.0)));          /* q / 2^16 */
    k->scale = qdiv(Q24(0.001), f48);
    k->tone_f = qmin(qratio(4 * f0, qm(timbre, Q24(108.0))), QONE);
    k->leak = qm(Q24(0.08), timbre + Q24(0.25));
    k->pulse_h = Q24(3.0) + qm(Q24(7.0), acc);
    k->afm = qm(qmin(4 * h, QONE), Q24(1.7));
    k->sfm = qm(qlim(4 * h - QONE, 0, QONE), Q24(0.08));
    drive = qm(qmax(2 * h - QONE, 0), qmax(QONE - 16 * f48, 0));
    d = Q24(0.5) + drive / 2;
    d2 = qm(d, d);
    pga = d / 2;
    pgb = qm(qm(qm(d2, d2), d), Q24(24.0));
    pre = pga + qm(pgb - pga, d2);
    sq = qm(d, 2 * QONE - d);
    k->pre = pre;
    k->post = qdiv(QONE, qsoftclip(Q24(0.33) + qm(sq, pre - Q24(0.33))));
    k->pulse = k->pulse_lp = k->fm_lp = k->retrig = k->lp_out = k->tone_lp = 0;
    k->res.s1 = k->res.s2 = 0;
    k->n = 0;
    k->rem = FS / 1000;                                                    /* 1 ms */
    k->fmrem = 6 * FS / 1000;                                              /* 6 ms */
}

static int32_t kboom_tick(kboom_t *k)
{
    static const int32_t PDEC = Q24(1.0 - 1.0 / (0.2e-3 * FS)), PFILT = Q24(1.0 / (0.1e-3 * FS));
    static const int32_t RDEC = Q24(1.0 - 1.0 / (0.05 * FS));
    int32_t pulse = 0, fm_pulse = 0, punch, f, ro, lp, d;
    if (k->rem) {
        k->rem--;
        pulse = k->rem ? k->pulse_h : k->pulse_h - QONE;
        k->pulse = pulse;
    } else if (k->pulse) {
        k->pulse = qdecay(k->pulse, PDEC);
        pulse = k->pulse;
    }
    if (pulse || k->pulse_lp) {
        k->pulse_lp = pulse ? k->pulse_lp + qm(PFILT, pulse - k->pulse_lp) : 0;
        d = (pulse - k->pulse_lp) + qm(pulse, Q24(0.044));
        pulse = d >= 0 ? d : qm(Q24(0.7), qsat(2 * d));                     /* Diode */
    }
    if (k->fmrem) {
        k->fmrem--;
        fm_pulse = QONE;
        k->retrig = k->fmrem ? 0 : Q24(-0.8);
    } else {
        k->retrig = qdecay(k->retrig, RDEC);
    }
    k->fm_lp += qm(PFILT, fm_pulse - k->fm_lp);
    if (!(k->n++ & 1)) {                       /* the resonator follows the pitch every 2 samples (22 kHz): cost */
        d = 10 * k->lp_out - QONE;
        punch = Q24(0.7) + (d >= 0 ? d : qm(Q24(0.7), qsat(2 * d)));
        f = k->f0 + qm(k->f0, qm(k->fm_lp, k->afm) + qm(punch, k->sfm));
        f = qlim(f, 0, Q24(0.4));
        qsvf_set(&k->res, qtan_dirty(f), qinv12(4096 + (int32_t)(((int64_t)k->q * q48(f)) >> 20)));
    }
    qsvf_tick(&k->res, qm(pulse - qm(k->retrig, Q24(0.2)), k->scale), &lp, &ro);
    k->lp_out = lp;
    k->tone_lp += qm(k->tone_f, qm(pulse, k->leak) + ro - k->tone_lp);
    return qm(qsoftclip(qm(k->pre, k->tone_lp)), k->post);
}

/* KBOOM: TUNE DECAY TONE PUNCH (attack FM -> self FM -> drive); analog bass drum: a pulse into a resonator, overdrive */
#define KBOOM_NOTE 31                                     /* MIDI note at TUNE 0 (49 Hz) */
static void kboom_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    kboom_start(&v->ms.kb, KBOOM_NOTE + p[0], p[2], p[1], p[3], v->vel);
}

static void kboom_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t y = kboom_tick(&v->ms.kb);
        dm_putq(v, out, i, y);
        pk = qmax(pk, qabs(y));
    }
    dm_qend(v, n, pk, FS * 3 / 10);
}

#define DM_KBOOM_DEF {"KBOOM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("PUNCH", F_INT, 0, 127, 32), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, kboom_trigger, kboom_render, 2}
```

- [ ] **Step 4b: The weighted voice cap** (user decision: the heavy models count 2 voices, so at most 4 ring at once)

In `firmware/src/core.h` add to `dmodel_t` after the `render` member:
```c
    uint8_t weight;              /* what a voice counts toward DRUM_MAXV: 0 / 1 = 1, 2 = a heavy model (M1-C) */
```
In `firmware/src/drum_core.c` replace `dv_oldest`, `dv_make_room` and `dv_alloc` with:
```c
/* what a sounding voice counts toward DRUM_MAXV: a layer voice 1, a model voice its model's weight */
static uint32_t dv_weight(const track_t *t, const dvoice_t *v)
{
    uint32_t w = v >= t->lv && v < t->lv + NDV ? 1u : DMODELS[t->model % NMODELS].weight;
    return w ? w : 1u;
}

/* the oldest sounding voice of any track (model or layer): *ot its track; returns the sounding weight */
static uint32_t dv_oldest(track_t **ot, dvoice_t **ov)
{
    uint32_t i, k, n = 0;
    *ot = 0;
    *ov = 0;
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < 2u * NDV; k++) {
            dvoice_t *v = k < NDV ? &trk[i].v[k] : &trk[i].lv[k - NDV];
            if (!v->active)
                continue;
            n += dv_weight(&trk[i], v);
            if (!*ov || v->age < (*ov)->age) {
                *ov = v;
                *ot = &trk[i];
            }
        }
    return n;
}

/* a voice of weight w is about to start: the oldest sounding voices stop (declick tail) until it fits the cap */
static void dv_make_room(uint32_t w)
{
    track_t *ot;
    dvoice_t *ov;
    while (dv_oldest(&ot, &ov) + w > DRUM_MAXV && ov)
        dv_cut(ot, ov);
}

static dvoice_t *dv_alloc(track_t *t, dvoice_t *pool, uint32_t nv, uint32_t w)   /* free, else the oldest */
{
    uint32_t i;
    dvoice_t *v = &pool[0];
    for (i = 0; i < nv; i++) {
        if (!pool[i].active) {
            dv_make_room(w);
            return &pool[i];
        }
        if (pool[i].age < v->age)
            v = &pool[i];
    }
    dv_cut(t, v);
    return v;
}
```
and in `drum_hit` change `v = dv_alloc(t, t->v, nv);` to `v = dv_alloc(t, t->v, nv, m->weight ? m->weight : 1u);` and `v = dv_alloc(t, t->lv, nv);` to `v = dv_alloc(t, t->lv, nv, 1u);`. (`drum_shed`, between `dv_make_room` and `dv_alloc`, stays as it is.) Update the comment of `DRUM_MAXV`: append `; a heavy model's voice counts 2 (M1-C, user decision)`.

- [ ] **Step 5: Register it** — in `firmware/src/dmodels.c` append `DM_KBOOM` to the enum before `NMODELS`, `DM_KBOOM_DEF` at the end of `DMODELS[]`, and `"KBOOM"` at the end of `N_MODEL[]` (same position in all three). Also change the comment at the top of `dmodels.c` to `/* The drum model table. New models are appended at the end (projects store model numbers), in the enum, DMODELS and N_MODEL (same order). */`.

- [ ] **Step 6: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "KBOOM|golden|cost|FAIL|PASSED"`
Expected: `ok    KBOOM: audible, bounded, ends in 6 s ...` and `ok    voice cap: a heavy model counts 2 ...`; the golden check FAILS with `KBOOM has no golden entry`. Then run `GOLDEN_UPDATE=1 PYTHON=.venv/bin/python sh tests/run_drum_tests.sh >/dev/null; git diff tests/drum_golden.txt`
Expected: the diff adds exactly one line, `KBOOM xxxxxxxx`; no other line changes.

Run the suite again. Expected:
- `fidelity: KBOOM knob defaults match tests/drum_ref_grid.h` ok;
- the line `KBOOM: strict S / 162, loose L / 162` (the plan research ran exactly this code through the firmware path: **162 / 162 strict, 162 / 162 loose**);
- `fidelity: KBOOM within the loose tolerances ... bounded` ok, and `fidelity: no Q24 product overflowed` ok;
- both cost checks ok. If `cost: the extreme case has not grown past its recorded limit` FAILS (the printed extreme kit is `8 x KBOOM`): STOP and ask the user (Global Constraints). Never raise `extreme_max` yourself.

- [ ] **Step 7: One round of spec §6 steps 1–2 against strict** (only if strict < 162 or loose < 162)

Read `build/drum_fidelity.txt` (every strict miss: corner and metric). Leads from the research:
None expected: the research prototype of exactly this code (resonator coefficients every 2 samples) passed strict on all 162 renders. Every-4-samples was tried and lost three strict renders at TUNE −24 with PUNCH 127 (the self-FM loop), so keep 2.
Fix causes only (precision, an approximation at its extremes, a 48 → 44.1 kHz conversion, a stage clipping); never a tolerance or a constant tuned to pass. Ledger each cause found as a ruling. Rerun Step 6. A loose miss that remains after this round: STOP and ask the user (spec §6 step 3) with the numbers and `build/drum_renders/ref_KBOOM.wav` vs our render.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/dm_state.h firmware/src/dm_kick.c firmware/src/dmodels.c tests/drum_test.c tests/drum_golden.txt firmware/src/core.h firmware/src/drum_core.c
git commit -m "models: KBOOM (integer port, fidelity strict S / loose L of 162)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
(fill S and L from Step 6.)

---
### Task 4: KPUNC

**Files:**
- Modify: `firmware/src/dm_state.h`, `firmware/src/dm_kick.c`, `firmware/src/dmodels.c`, `tests/drum_test.c`, `tests/drum_golden.txt`

**Interfaces:**
- Consumes: Task 1's toolkit (`qm` .. `dm_qend`, `qsvf_t`, `qpole_t`), Task 2's harness (finds the model by its name `"KPUNC"`).
- Produces: `DM_KPUNC` (appended after the last model in the enum), `kpunc_t`, `kpunc_start`, `kpunc_tick`, `kpunc_trigger`, `kpunc_render`, `DM_KPUNC_DEF` (weight 2).

- [ ] **Step 1: Write the failing test** — in `test_kicks` of `tests/drum_test.c` add after the existing `model_health(...)` lines:
```c
    model_health(DM_KPUNC);
```

- [ ] **Step 2: Run to verify it fails**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_KPUNC` undeclared.

- [ ] **Step 3: The state** — in `firmware/src/dm_state.h` add before `typedef union {`:
```c
typedef struct {
    int32_t f0, dirt, fm_amt, fm_dec, body_dec, tone_f, tlevel;
    int32_t phase, pnoise, fm, fm_lp, body, body_lp, trans, trans_lp, tone_lp;
    int32_t c_lp, c_hp, n_lp, n_hp;
    int32_t bpw, fpw;
    uint32_t rng;
    qsvf_t click;
} kpunc_t;
```
and inside the union, after `int32_t raw[2];`:
```c
    kpunc_t kp;
```

- [ ] **Step 4: The model** — append to `firmware/src/dm_kick.c` (after its existing `#define DM_..._DEF` lines):
```c
static void kpunc_start(kpunc_t *k, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel, uint32_t seed)
{
    int32_t h = qknob(chr), morph = qknob(decay), timbre = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), dec = qm(morph, morph), fed = qmax(2 * h - QONE, 0), lvl;
    k->f0 = f0;
    k->dirt = qm(Q24(0.4) - qm(Q24(0.25), dec), qmax(QONE - 8 * q48(f0), 0));
    k->fm_amt = qm(qmin(2 * h, QONE), Q24(3.5));
    fed = qm(fed, fed);
    /* 1 - 1 / (0.008 (1 + 4 fed) SR) */
    k->fm_dec = QONE - (int32_t)((1LL << 40) / (int32_t)(((int64_t)23121101 * (QONE + 4 * fed)) >> 24));   /* 352.8 = 0.008 SR, Q16; a 32-bit divisor: native divide */
    k->body_dec = QONE - qratio(Q24(1.0 / (0.02 * FS)), -qm(dec, Q24(60.0)));
    k->tone_f = qmin(qratio(4 * f0, qm(timbre, Q24(108.0))), QONE);
    k->tlevel = timbre;
    lvl = Q24(0.3) + qm(Q24(0.7), acc);
    k->fm = QONE;
    k->body = k->trans = lvl;
    k->phase = k->pnoise = k->fm_lp = k->body_lp = k->trans_lp = k->tone_lp = 0;
    k->c_lp = k->c_hp = k->n_lp = k->n_hp = 0;
    k->bpw = FS / 1000;                                   /* 1 ms */
    k->fpw = (int32_t)(FS * 0.0013);                      /* 1.3 ms */
    k->rng = seed;
    qsvf_set(&k->click, qtan_fast(Q24(5000.0 / FS)), Q24(0.5));
    k->click.s1 = k->click.s2 = 0;
}

static int32_t kpunc_dsine(int32_t ph, int32_t pn, int32_t dirt)   /* DistortedSine */
{
    int32_t x = ph + qm(pn, dirt), p = x >= 0 ? x & (QONE - 1) : -((-x) & (QONE - 1)), tri, s;   /* x - trunc(x) */
    tri = (p < QONE / 2 ? p : QONE - p) * 4 - QONE;
    s = qdiv(2 * tri, QONE + qabs(tri));
    return s + qm(QONE - dirt, qsine(p + Q24(0.75)) - s);
}

static int32_t kpunc_tick(kpunc_t *k)
{
    static const int32_t TDEC = Q24(1.0 - 1.0 / (0.005 * FS));
    int32_t mix, body, tr, s, g, lp, bp;
    k->pnoise += qm(C44_002, qrand(&k->rng) - QONE / 2 - k->pnoise);
    if (k->fpw) {
        k->fpw--;
        k->phase = QONE / 4;
    } else {
        k->fm = qdecay(k->fm, k->fm_dec);
        k->phase += qmin(k->f0 + qm(k->f0, qm(k->fm_amt, k->fm_lp)), QONE / 2);
        if (k->phase >= QONE)
            k->phase -= QONE;
    }
    if (k->bpw) {
        k->bpw--;
    } else {
        k->body = qdecay(k->body, k->body_dec);
        k->trans = qdecay(k->trans, TDEC);
    }
    k->body_lp += qm(C44_10, k->body - k->body_lp);
    k->trans_lp += qm(C44_10, k->trans - k->trans_lp);
    k->fm_lp += qm(C44_10, k->fm - k->fm_lp);
    body = kpunc_dsine(k->phase, k->pnoise, k->dirt);
    s = (k->bpw ? 0 : QONE) - k->c_lp;                       /* click: SLOPE, ONE_POLE, Svf LP */
    k->c_lp += qm(s > 0 ? C44_50 : C44_10, s);
    k->c_hp += qm(C44_04, k->c_lp - k->c_hp);
    qsvf_tick(&k->click, k->c_lp - k->c_hp, &lp, &bp);
    k->n_lp += qm(C44_05, qrand(&k->rng) - k->n_lp);         /* attack noise */
    k->n_hp += qm(C44_005, k->n_lp - k->n_hp);
    tr = lp + k->n_lp - k->n_hp;
    g = k->body_lp;                                          /* TransistorVCA */
    s = qm(body - Q24(0.6), g);
    mix = -(qdiv(3 * s, 2 * QONE + qabs(s)) + qm(g, Q24(0.3)));
    mix -= qm(qm(tr, k->trans_lp), k->tlevel);
    k->tone_lp += qm(k->tone_f, mix - k->tone_lp);
    return k->tone_lp;
}

/* KPUNC: TUNE DECAY TONE FM (FM amount, then FM decay); synthetic bass drum: a distorted sine, FM and pitch envelopes, click */
#define KPUNC_NOTE 31                                     /* MIDI note at TUNE 0 (49 Hz) */
static void kpunc_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    kpunc_start(&v->ms.kp, KPUNC_NOTE + p[0], p[2], p[1], p[3], v->vel, (uint32_t)v->rng);
}

static void kpunc_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t y = kpunc_tick(&v->ms.kp);
        dm_putq(v, out, i, y);
        pk = qmax(pk, qabs(y));
    }
    dm_qend(v, n, pk, FS / 20);
}

#define DM_KPUNC_DEF {"KPUNC", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("FM", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, kpunc_trigger, kpunc_render, 2}
```

- [ ] **Step 5: Register it** — in `firmware/src/dmodels.c` append `DM_KPUNC` to the enum before `NMODELS`, `DM_KPUNC_DEF` at the end of `DMODELS[]`, and `"KPUNC"` at the end of `N_MODEL[]` (same position in all three).

- [ ] **Step 6: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "KPUNC|golden|cost|FAIL|PASSED"`
Expected: `ok    KPUNC: audible, bounded, ends in 6 s ...`; the golden check FAILS with `KPUNC has no golden entry`. Then run `GOLDEN_UPDATE=1 PYTHON=.venv/bin/python sh tests/run_drum_tests.sh >/dev/null; git diff tests/drum_golden.txt`
Expected: the diff adds exactly one line, `KPUNC xxxxxxxx`; no other line changes.

Run the suite again. Expected:
- `fidelity: KPUNC knob defaults match tests/drum_ref_grid.h` ok;
- the line `KPUNC: strict S / 162, loose L / 162` (the plan research ran exactly this code through the firmware path: **133 / 162 strict, 147 / 162 loose**);
- `fidelity: KPUNC within the loose tolerances ... bounded` ok, and `fidelity: no Q24 product overflowed` ok;
- both cost checks ok. If `cost: the extreme case has not grown past its recorded limit` FAILS (the printed extreme kit is `8 x KPUNC`): STOP and ask the user (Global Constraints). Never raise `extreme_max` yourself.

- [ ] **Step 7: One round of spec §6 steps 1–2 against strict** (only if strict < 162 or loose < 162)

Read `build/drum_fidelity.txt` (every strict miss: corner and metric). Leads from the research:
Every prototype miss was "pitch at 0.00 s" (the first 50 ms window), at TONE 127 or with FM. First decide whether that window measures the body or the transient. Dump the body (`kpunc_dsine`) and the transient (`tr`) separately on both sides for TUNE 0 / TONE 127 / CHAR 64: (a) if the body pitch matches and the extra zero crossings belong to the click + noise, the metric is measuring the transient, which is spec §6 step 4: stop and ask the user before changing the metric; (b) otherwise compare the FM path stage by stage: `fpw` (1.3 ms phase hold), `fm_dec`, `fm_lp`, `fm_amt` (3.5 × min(2·CHAR, 1)).
Fix causes only (precision, an approximation at its extremes, a 48 → 44.1 kHz conversion, a stage clipping); never a tolerance or a constant tuned to pass. Ledger each cause found as a ruling. Rerun Step 6. A loose miss that remains after this round: STOP and ask the user (spec §6 step 3) with the numbers and `build/drum_renders/ref_KPUNC.wav` vs our render.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/dm_state.h firmware/src/dm_kick.c firmware/src/dmodels.c tests/drum_test.c tests/drum_golden.txt
git commit -m "models: KPUNC (integer port, fidelity strict S / loose L of 162)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
(fill S and L from Step 6.)

---
### Task 5: SSNAP

**Files:**
- Modify: `firmware/src/dm_state.h`, `firmware/src/dm_snare.c`, `firmware/src/dmodels.c`, `tests/drum_test.c`, `tests/drum_golden.txt`

**Interfaces:**
- Consumes: Task 1's toolkit (`qm` .. `dm_qend`, `qsvf_t`, `qpole_t`), Task 2's harness (finds the model by its name `"SSNAP"`).
- Produces: `DM_SSNAP` (appended after the last model in the enum), `ssnap_t`, `ssnap_start`, `ssnap_tick`, `ssnap_trigger`, `ssnap_render`, `DM_SSNAP_DEF` (weight 2).

- [ ] **Step 1: Write the failing test** — in `test_snares_claps` of `tests/drum_test.c` add after the existing `model_health(...)` lines:
```c
    model_health(DM_SSNAP);
```

- [ ] **Step 2: Run to verify it fails**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_SSNAP` undeclared.

- [ ] **Step 3: The state** — in `firmware/src/dm_state.h` add before `typedef union {`:
```c
typedef struct {
    int32_t gain[5], snappy, leak, ndec, pulse_h;
    int32_t pulse, pulse_lp, nenv;
    int32_t rem;
    uint32_t rng;
    qsvf_t res[5], nf;
} ssnap_t;
```
and inside the union, after `int32_t raw[2];`:
```c
    ssnap_t ss;
```

- [ ] **Step 4: The model** — append to `firmware/src/dm_snare.c` (after its existing `#define DM_..._DEF` lines):
```c
/* ---- M1-C: integer ports of Mutable Instruments' drum algorithms (Plaits, stmlib), MIT licence:
 * Copyright 2012-2016 Emilie Gillet (emilie.o.gillet@gmail.com).
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions: The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE. */

static void ssnap_start(ssnap_t *k, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel, uint32_t seed)
{
    static const int32_t MODE[5] = {Q24(1.00), Q24(2.00), Q24(3.18), Q24(4.16), Q24(5.62)};
    int32_t sn = qknob(chr), d = qknob(decay), t = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), dxt = qm(d, QONE + qm(d, d - QONE)), q, fn, i;
    q = qratio(Q24(2000.0 / 65536.0), qm(dxt, Q24(84.0)));     /* q / 2^16 */
    k->ndec = QONE - qm(qratio(Q24(0.0017), -qm(d, Q24(50.0) + qm(sn, Q24(10.0)))), K44);
    k->leak = qm(qm(sn, 2 * QONE - sn), Q24(0.1));
    k->snappy = qlim(qm(sn, Q24(1.1)) - Q24(0.05), 0, QONE);
    k->pulse_h = Q24(3.0) + qm(Q24(7.0), acc);
    for (i = 0; i < 5; i++) {
        int32_t f = qmin(qm(f0, MODE[i]), Q24(0.499)), qi = i == 0 ? q : q / 4;
        qsvf_set(&k->res[i], qtan_fast(f), qinv12(4096 + (int32_t)(((int64_t)qi * q48(f)) >> 20)));
        k->res[i].s1 = k->res[i].s2 = 0;
    }
    if (t < Q24(0.666667)) {
        t = qm(t, Q24(1.5));
        k->gain[0] = Q24(1.5) + qm(qm(QONE - t, QONE - t), Q24(4.5));
        k->gain[1] = 2 * t + Q24(0.15);
        k->gain[2] = k->gain[3] = k->gain[4] = 0;
    } else {
        t = qm(t - Q24(0.666667), Q24(3.0));
        k->gain[0] = Q24(1.5) - t / 2;
        k->gain[1] = Q24(2.15) - qm(t, Q24(0.7));
        for (i = 2; i < 5; i++) {
            k->gain[i] = t;
            t = qm(t, t);
        }
    }
    fn = qlim(16 * f0, 0, Q24(0.499));
    qsvf_set(&k->nf, qtan_fast(fn), qdiv(QONE, QONE + qm(q48(fn), Q24(1.5))));
    k->nf.s1 = k->nf.s2 = 0;
    k->rem = FS / 1000;
    k->nenv = 2 * QONE;
    k->pulse = k->pulse_lp = 0;
    k->rng = seed;
}

static int32_t ssnap_tick(ssnap_t *k)
{
    static const int32_t PDEC = Q24(1.0 - 1.0 / (0.1e-3 * FS));
    int32_t pulse, shell = 0, noise, i, lp, bp;
    if (k->rem) {
        k->rem--;
        pulse = k->rem ? k->pulse_h : k->pulse_h - QONE;
        k->pulse = pulse;
    } else {
        k->pulse = qdecay(k->pulse, PDEC);
        pulse = k->pulse;
    }
    k->pulse_lp += qm(C44_75, pulse - k->pulse_lp);
    for (i = 0; i < 5; i++) {
        int32_t ex = i == 0 ? (pulse - k->pulse_lp) + qm(Q24(0.006), pulse) : qm(Q24(0.026), pulse);
        if (!k->gain[i])
            continue;
        qsvf_tick(&k->res[i], ex, &lp, &bp);
        shell += qm(k->gain[i], bp + qm(ex, k->leak));
    }
    shell = qsoftclip(shell);
    noise = qmax(2 * qrand(&k->rng) - QONE, 0);
    k->nenv = qdecay(k->nenv, k->ndec);
    noise = qm(qm(noise, k->nenv), 2 * k->snappy);
    qsvf_tick(&k->nf, noise, &lp, &bp);
    return bp + qm(shell, QONE - k->snappy);
}

/* SSNAP: TUNE DECAY TONE SNAP; analog snare: five resonator modes of the shell, a pulse exciter, band-passed noise */
#define SSNAP_NOTE 55                                     /* MIDI note at TUNE 0 (196 Hz) */
static void ssnap_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    ssnap_start(&v->ms.ss, SSNAP_NOTE + p[0], p[2], p[1], p[3], v->vel, (uint32_t)v->rng);
}

static void ssnap_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t y = ssnap_tick(&v->ms.ss);
        dm_putq(v, out, i, y);
        pk = qmax(pk, qabs(y));
    }
    dm_qend(v, n, pk, FS / 20);
}

#define DM_SSNAP_DEF {"SSNAP", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("SNAP", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, ssnap_trigger, ssnap_render, 2}
```

- [ ] **Step 5: Register it** — in `firmware/src/dmodels.c` append `DM_SSNAP` to the enum before `NMODELS`, `DM_SSNAP_DEF` at the end of `DMODELS[]`, and `"SSNAP"` at the end of `N_MODEL[]` (same position in all three).

- [ ] **Step 6: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "SSNAP|golden|cost|FAIL|PASSED"`
Expected: `ok    SSNAP: audible, bounded, ends in 6 s ...`; the golden check FAILS with `SSNAP has no golden entry`. Then run `GOLDEN_UPDATE=1 PYTHON=.venv/bin/python sh tests/run_drum_tests.sh >/dev/null; git diff tests/drum_golden.txt`
Expected: the diff adds exactly one line, `SSNAP xxxxxxxx`; no other line changes.

Run the suite again. Expected:
- `fidelity: SSNAP knob defaults match tests/drum_ref_grid.h` ok;
- the line `SSNAP: strict S / 162, loose L / 162` (the plan research ran exactly this code through the firmware path: **121 / 162 strict, 138 / 162 loose**);
- `fidelity: SSNAP within the loose tolerances ... bounded` ok, and `fidelity: no Q24 product overflowed` ok;
- both cost checks ok. If `cost: the extreme case has not grown past its recorded limit` FAILS (the printed extreme kit is `8 x SSNAP`): STOP and ask the user (Global Constraints). Never raise `extreme_max` yourself.

- [ ] **Step 7: One round of spec §6 steps 1–2 against strict** (only if strict < 162 or loose < 162)

Read `build/drum_fidelity.txt` (every strict miss: corner and metric). Leads from the research:
Prototype misses: brightness (23), peak (15), envelope (10). Brightness first: compare each mode's band-pass output on both sides at TONE 127 (all five modes); then `qtan_fast` near Nyquist (mode 5 at TUNE +24 is 5.62 × 784 Hz); then the noise filter (cutoff 16·f0 with f0 at 44.1 kHz, Q from the 48 kHz f). For noise-dominated corners (SNAP 127), re-render ours with 3 different seeds: a miss that moves with the seed is luck, which is spec §6 step 4 (ask the user).
Fix causes only (precision, an approximation at its extremes, a 48 → 44.1 kHz conversion, a stage clipping); never a tolerance or a constant tuned to pass. Ledger each cause found as a ruling. Rerun Step 6. A loose miss that remains after this round: STOP and ask the user (spec §6 step 3) with the numbers and `build/drum_renders/ref_SSNAP.wav` vs our render.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/dm_state.h firmware/src/dm_snare.c firmware/src/dmodels.c tests/drum_test.c tests/drum_golden.txt
git commit -m "models: SSNAP (integer port, fidelity strict S / loose L of 162)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
(fill S and L from Step 6.)

---
### Task 6: SCRAK

**Files:**
- Modify: `firmware/src/dm_state.h`, `firmware/src/dm_snare.c`, `firmware/src/dmodels.c`, `tests/drum_test.c`, `tests/drum_golden.txt`

**Interfaces:**
- Consumes: Task 1's toolkit (`qm` .. `dm_qend`, `qsvf_t`, `qpole_t`), Task 2's harness (finds the model by its name `"SCRAK"`).
- Produces: `DM_SCRAK` (appended after the last model in the enum), `scrak_t`, `scrak_start`, `scrak_tick`, `scrak_trigger`, `scrak_render`, `DM_SCRAK_DEF` (weight 2).

- [ ] **Step 1: Write the failing test** — in `test_snares_claps` of `tests/drum_test.c` add after the existing `model_health(...)` lines:
```c
    model_health(DM_SCRAK);
```

- [ ] **Step 2: Run to verify it fails**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_SCRAK` undeclared.

- [ ] **Step 3: The state** — in `firmware/src/dm_state.h` add before `typedef union {`:
```c
typedef struct {
    int32_t f0, fm_amt, ddec, sdec, dlvl, slvl, rna;
    int32_t ph0, ph1, damp, samp, fm;
    int32_t hold, t;
    uint32_t rng;
    qpole_t dlp, shp;
    qsvf_t slp;
} scrak_t;
```
and inside the union, after `int32_t raw[2];`:
```c
    scrak_t sc;
```

- [ ] **Step 4: The model** — append to `firmware/src/dm_snare.c` (after its existing `#define DM_..._DEF` lines):
```c
static void scrak_start(scrak_t *k, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel, uint32_t seed)
{
    int32_t sn = qknob(chr), d = qknob(decay), fa = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), dxt = qm(d, QONE + qm(d, d - QONE)), r;
    fa = qm(fa, fa);
    k->f0 = f0;
    k->fm_amt = 4 * fa;
    k->ddec = QONE - qratio(Q24(1.0 / (0.015 * FS)), -qm(dxt, Q24(72.0)) - qm(fa, Q24(12.0)) + qm(sn, Q24(7.0)));
    k->sdec = QONE - qratio(Q24(1.0 / (0.01 * FS)), -qm(d, Q24(60.0)) - qm(sn, Q24(7.0)));
    sn = qlim(qm(sn, Q24(1.1)) - Q24(0.05), 0, QONE);
    k->dlvl = qsqrt(QONE - sn);
    k->slvl = qsqrt(sn);
    r = qlim(qm(Q24(0.125) - q48(f0), Q24(8.0)), 0, QONE);
    k->rna = qm(qm(r, r), fa);
    qpole_set(&k->shp, qtan_fast(qmin(10 * f0, QONE / 2)));
    qsvf_set(&k->slp, qtan_fast(qmin(35 * f0, QONE / 2)), qdiv(QONE, Q24(0.5) + 2 * sn));
    qpole_set(&k->dlp, qtan_fast(3 * f0));
    k->dlp.s = k->shp.s = k->slp.s1 = k->slp.s2 = 0;
    k->samp = k->damp = Q24(0.3) + qm(Q24(0.7), acc);
    k->fm = QONE;
    k->ph0 = k->ph1 = 0;
    k->hold = (int32_t)(((int64_t)(Q24(0.04) + qm(d, Q24(0.03))) * FS) >> 24);
    k->t = 0;
    k->rng = seed;
}

static int32_t scrak_dsine(int32_t p)
{
    int32_t tri = (p < QONE / 2 ? p : QONE - p) * 4 - Q24(1.3);
    return qdiv(2 * tri, QONE + qabs(tri));
}

static int32_t scrak_tick(scrak_t *k)
{
    static const int32_t FDEC = Q24(1.0 - 1.0 / (0.007 * FS));
    int32_t rn, f, drum, noise, snare, lp, bp;
    if (k->damp > Q24(0.03) || (k->t & 1))
        k->damp = qdecay(k->damp, k->ddec);
    if (k->hold)
        k->hold--;
    else
        k->samp = qdecay(k->samp, k->sdec);
    k->fm = qdecay(k->fm, FDEC);
    k->t++;
    rn = (k->ph0 > QONE / 2 ? -QONE : QONE) + (k->ph1 > QONE / 2 ? -QONE : QONE);
    rn = qm(rn, qm(k->rna, Q24(0.025)));
    f = k->f0 + qm(k->f0, qm(k->fm_amt, k->fm));
    k->ph0 += f;
    k->ph1 += qm(f, Q24(1.47));
    if (k->rna > Q24(0.1)) {
        if (k->ph0 >= QONE + rn)
            k->ph0 = QONE - k->ph0;
        if (k->ph1 >= QONE + rn)
            k->ph1 = QONE - k->ph1;
    } else {
        if (k->ph0 >= QONE)
            k->ph0 -= QONE;
        if (k->ph1 >= QONE)
            k->ph1 -= QONE;
    }
    drum = -Q24(0.1) + qm(scrak_dsine(k->ph0), Q24(0.60)) + qm(scrak_dsine(k->ph1), Q24(0.25));
    drum = qpole_lp(&k->dlp, qm(qm(drum, k->damp), k->dlvl));
    noise = qrand(&k->rng);
    qsvf_tick(&k->slp, noise, &lp, &bp);
    snare = lp - qpole_lp(&k->shp, lp);                      /* OnePole high-pass */
    snare = qm(qm(snare + Q24(0.1), k->samp + k->fm), k->slvl);
    return snare + drum;
}

/* SCRAK: TUNE DECAY FM SNAP; synthetic snare: two distorted sines with FM, filtered noise, a hold */
#define SCRAK_NOTE 55                                     /* MIDI note at TUNE 0 (196 Hz) */
static void scrak_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    scrak_start(&v->ms.sc, SCRAK_NOTE + p[0], p[2], p[1], p[3], v->vel, (uint32_t)v->rng);
}

static void scrak_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t y = scrak_tick(&v->ms.sc);
        dm_putq(v, out, i, y);
        pk = qmax(pk, qabs(y));
    }
    dm_qend(v, n, pk, FS / 10);
}

#define DM_SCRAK_DEF {"SCRAK", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("FM", F_INT, 0, 127, 64), PD("SNAP", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, scrak_trigger, scrak_render, 2}
```

- [ ] **Step 5: Register it** — in `firmware/src/dmodels.c` append `DM_SCRAK` to the enum before `NMODELS`, `DM_SCRAK_DEF` at the end of `DMODELS[]`, and `"SCRAK"` at the end of `N_MODEL[]` (same position in all three).

- [ ] **Step 6: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "SCRAK|golden|cost|FAIL|PASSED"`
Expected: `ok    SCRAK: audible, bounded, ends in 6 s ...`; the golden check FAILS with `SCRAK has no golden entry`. Then run `GOLDEN_UPDATE=1 PYTHON=.venv/bin/python sh tests/run_drum_tests.sh >/dev/null; git diff tests/drum_golden.txt`
Expected: the diff adds exactly one line, `SCRAK xxxxxxxx`; no other line changes.

Run the suite again. Expected:
- `fidelity: SCRAK knob defaults match tests/drum_ref_grid.h` ok;
- the line `SCRAK: strict S / 162, loose L / 162` (the plan research ran exactly this code through the firmware path: **120 / 162 strict, 136 / 162 loose**);
- `fidelity: SCRAK within the loose tolerances ... bounded` ok, and `fidelity: no Q24 product overflowed` ok;
- both cost checks ok. If `cost: the extreme case has not grown past its recorded limit` FAILS (the printed extreme kit is `8 x SCRAK`): STOP and ask the user (Global Constraints). Never raise `extreme_max` yourself.

- [ ] **Step 7: One round of spec §6 steps 1–2 against strict** (only if strict < 162 or loose < 162)

Read `build/drum_fidelity.txt` (every strict miss: corner and metric). Leads from the research:
Prototype misses: peak (32, mostly SNAP 127, ours about 1.5 dB louder in the loudest 40 ms window), brightness (19). The research found 50 ms windows match within 0.4 dB for SNAP 127. Test the peak miss with 3 seeds first (luck: spec §6 step 4, ask the user); if it is stable, compare the snare path: the hold (40–70 ms), `samp + fm` (both 1 at the hit), the +0.1 DC term, the `shp` high-pass at 10·f0.
Fix causes only (precision, an approximation at its extremes, a 48 → 44.1 kHz conversion, a stage clipping); never a tolerance or a constant tuned to pass. Ledger each cause found as a ruling. Rerun Step 6. A loose miss that remains after this round: STOP and ask the user (spec §6 step 3) with the numbers and `build/drum_renders/ref_SCRAK.wav` vs our render.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/dm_state.h firmware/src/dm_snare.c firmware/src/dmodels.c tests/drum_test.c tests/drum_golden.txt
git commit -m "models: SCRAK (integer port, fidelity strict S / loose L of 162)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
(fill S and L from Step 6.)

---
### Task 7: HMETL and HNOIS

**Files:**
- Modify: `firmware/src/dm_state.h`, `firmware/src/dm_metal.c`, `firmware/src/dmodels.c`, `tests/drum_test.c`, `tests/drum_golden.txt`

**Interfaces:**
- Consumes: Task 1's toolkit, Task 2's harness.
- Produces: `DM_HMETL`, `DM_HNOIS` (appended in that order), `qbosc_t`, `hh_t`, `hh_start(k, ring, note, tone, decay, chr, vel, seed)`, `qbosc_tick`, `hh_tick`, `hmetl_trigger`, `hnois_trigger`, `hh_render`, `DM_HMETL_DEF`, `DM_HNOIS_DEF`. Both default to choke group 1, like HATC / HATO.

- [ ] **Step 1: Write the failing tests** — in `test_metal` of `tests/drum_test.c` add after the existing `model_health(...)` lines:
```c
    model_health(DM_HMETL);
    model_health(DM_HNOIS);
    host_init();
    drum_set_model(&trk[0], DM_HNOIS);
    drum_set_model(&trk[1], DM_HMETL);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.05));
    drum_hit(&trk[1], 127);
    render_track(&trk[0], wl, SECS(0.01));
    check("HMETL / HNOIS: choke group 1 by default, one cuts the other",
          trk[0].p[P_CHOKE] == 1 && trk[1].p[P_CHOKE] == 1 && !trk[0].v[0].active);
```

- [ ] **Step 2: Run to verify it fails**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_HMETL` undeclared.

- [ ] **Step 3: The state** — in `firmware/src/dm_state.h` add before `typedef union {`:
```c
typedef struct { int32_t ph, f, next; int32_t lp, hp; uint8_t high; } qbosc_t;   /* Plaits Oscillator, square / saw */
typedef struct {
    int32_t env, edec, cdec, noisy, nf, nclk, nsmp;
    uint32_t ph[6], inc[6];
    qbosc_t osc[6];
    uint8_t ring;                       /* 0 HMETL (squares, swing VCA, resonance), 1 HNOIS (ring mod, linear, 2-stage) */
    uint32_t rng;
    qsvf_t col, hpf;
} hh_t;
```
and inside the union, after the last member:
```c
    hh_t hh;
```

- [ ] **Step 4: The models** — append to `firmware/src/dm_metal.c` (after its existing `#define DM_..._DEF` lines):
```c
/* ---- M1-C: integer ports of Mutable Instruments' drum algorithms (Plaits, stmlib), MIT licence:
 * Copyright 2012-2016 Emilie Gillet (emilie.o.gillet@gmail.com).
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions: The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE. */

static void hh_start(hh_t *k, int ring, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel, uint32_t seed)
{
    static const int32_t RAT[6] = {Q24(1.0), Q24(1.304), Q24(1.466), Q24(1.787), Q24(1.932), Q24(2.536)};
    int32_t n = qknob(chr), d = qknob(decay), t = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), f = 2 * f0, cut, i;
    k->ring = (uint8_t)ring;
    k->edec = QONE - qm(qratio(Q24(0.003), -qm(d, Q24(84.0))), K44);
    k->cdec = QONE - qm(qratio(Q24(0.0025), -qm(d, Q24(36.0))), K44);
    k->env = qm(Q24(1.5) + (QONE - d) / 2, Q24(0.3) + qm(Q24(0.7), acc));
    cut = qlim(qratio(Q24(150.0 / FS), qm(t, Q24(72.0))), 0, Q24(16000.0 / FS));
    qsvf_set(&k->col, qtan_acc(cut), ring ? QONE : qdiv(QONE, Q24(3.0) + qm(Q24(3.0), t)));
    qsvf_set(&k->hpf, qtan_acc(cut), 2 * QONE);
    k->col.s1 = k->col.s2 = k->hpf.s1 = k->hpf.s2 = 0;
    n = qm(n, n);
    k->noisy = n;
    k->nf = qlim(qm(f0, Q24(16.0) + qm(Q24(16.0), QONE - n)), 0, QONE / 2);
    k->nclk = k->nsmp = 0;
    k->rng = seed;
    if (!ring) {
        for (i = 0; i < 6; i++) {
            int32_t fi = qmin(qm(f, RAT[i]), Q24(0.499));
            k->inc[i] = (uint32_t)fi << 8;
            k->ph[i] = 0;
        }
    } else {
        int32_t r = qdiv(q48(f), Q24(0.01) + q48(f));
        static const int32_t HZ[6] = {Q24(200.0 / FS), Q24(7530.0 / FS), Q24(510.0 / FS), Q24(8075.0 / FS),
                                      Q24(730.0 / FS), Q24(10500.0 / FS)};
        for (i = 0; i < 6; i++) {
            k->osc[i].f = qlim(qm(HZ[i], r), 1, QONE / 4);
            k->osc[i].ph = k->osc[i].next = k->osc[i].lp = k->osc[i].hp = 0;
            k->osc[i].high = 0;
        }
    }
}

/* Plaits Oscillator, pw 0.5: square (sq = 1) or saw, polyBLEP, one sample late */
static int32_t qbosc_tick(qbosc_t *o, int sq)
{
    int32_t th = o->next, t;
    o->next = 0;
    o->ph += o->f;
    if (sq) {
        if (o->high ^ (o->ph >= QONE / 2)) {
            t = qdiv(o->ph - QONE / 2, o->f);
            th += qm(t, t) / 2;
            t = QONE - t;
            o->next -= qm(t, t) / 2;
            o->high = o->ph >= QONE / 2;
        }
        if (o->ph >= QONE) {
            o->ph -= QONE;
            t = qdiv(o->ph, o->f);
            th -= qm(t, t) / 2;
            t = QONE - t;
            o->next += qm(t, t) / 2;
            o->high = 0;
        }
        o->next += o->high ? QONE : 0;
        return 2 * th - QONE;
    }
    if (o->ph >= QONE) {
        o->ph -= QONE;
        t = qdiv(o->ph, o->f);
        th -= qm(t, t) / 2;
        t = QONE - t;
        o->next += qm(t, t) / 2;
    }
    o->next += o->ph;
    return 2 * th - QONE;
}

static int32_t hh_tick(hh_t *k)
{
    int32_t x, lp, bp, i;
    if (!k->ring) {
        int32_t s = 0;
        for (i = 0; i < 6; i++) {
            k->ph[i] += k->inc[i];
            s += (int32_t)(k->ph[i] >> 31);
        }
        x = qm(Q24(0.33), s * QONE) - QONE;
    } else {
        x = 0;
        for (i = 0; i < 6; i += 2)
            x += qm(qbosc_tick(&k->osc[i], 1), qbosc_tick(&k->osc[i + 1], 0));
    }
    qsvf_tick(&k->col, x, &lp, &bp);
    x = bp;
    k->nclk += k->nf;
    if (k->nclk >= QONE) {
        k->nclk -= QONE;
        k->nsmp = qrand(&k->rng) - QONE / 2;
    }
    x += qm(k->noisy, k->nsmp - x);
    k->env = qm(k->env, k->env > Q24(0.5) || !k->ring ? k->edec : k->cdec);
    if (!k->ring) {                                           /* SwingVCA */
        x = qm(x, x > 0 ? 4 * QONE : Q24(0.1));
        x = qm(qsat(x) + Q24(0.1), k->env);
    } else {
        x = qm(x, k->env);
    }
    return qsvf_tick(&k->hpf, x, &lp, &bp);
}

/* HMETL: TUNE DECAY TONE NOISE; six square oscillators, a resonant band-pass, a swing VCA (metallic, 808-like).
 * HNOIS: TUNE DECAY TONE NOISE; ring-modulated square x saw pairs, a two-stage envelope (noisy, trashy). */
#define HMETL_NOTE 60                                     /* MIDI note at TUNE 0 (262 Hz) */
#define HNOIS_NOTE 72                                     /* (523 Hz) */
static void hmetl_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    hh_start(&v->ms.hh, 0, HMETL_NOTE + p[0], p[2], p[1], p[3], v->vel, (uint32_t)v->rng);
}

static void hnois_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    hh_start(&v->ms.hh, 1, HNOIS_NOTE + p[0], p[2], p[1], p[3], v->vel, (uint32_t)v->rng);
}

static void hh_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t y = hh_tick(&v->ms.hh);
        dm_putq(v, out, i, y);
        pk = qmax(pk, qabs(y));
    }
    dm_qend(v, n, pk, FS / 50);
}

#define DM_HMETL_DEF {"HMETL", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 40), \
    PD("TONE", F_INT, 0, 127, 80), PD("NOISE", F_INT, 0, 127, 40), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, hmetl_trigger, hh_render}
#define DM_HNOIS_DEF {"HNOIS", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 40), \
    PD("TONE", F_INT, 0, 127, 80), PD("NOISE", F_INT, 0, 127, 40), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, hnois_trigger, hh_render}
```

- [ ] **Step 5: Register them** — in `firmware/src/dmodels.c` append `DM_HMETL, DM_HNOIS` to the enum before `NMODELS`, `DM_HMETL_DEF, DM_HNOIS_DEF` at the end of `DMODELS[]`, `"HMETL", "HNOIS"` at the end of `N_MODEL[]`.

- [ ] **Step 6: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "HMETL|HNOIS|golden|FAIL|PASSED"`
Expected: both health checks and the choke check `ok`; the golden check FAILS for the two missing entries. Run `GOLDEN_UPDATE=1 PYTHON=.venv/bin/python sh tests/run_drum_tests.sh >/dev/null; git diff tests/drum_golden.txt`
Expected: exactly two added lines (`HMETL ...`, `HNOIS ...`).

Rerun. Expected: both `fidelity: ... knob defaults match` ok; `HMETL: strict S / 162, loose L / 162` and the same for HNOIS. The plan research ran exactly this code through the firmware path: **HMETL 86 / 162 strict, 106 / 162 loose; HNOIS 80 / 162 strict, 95 / 162 loose** — the most work of the six. Both loose checks must be `ok` after Step 7; no Q24 overflow.

- [ ] **Step 7: One round of spec §6 steps 1–2 against strict**

Read `build/drum_fidelity.txt`. Leads from the research:
- **Both, first:** at DECAY 0 (the shortest hits) ours is about one octave darker than the reference (e.g. 133 vs 270 Hz at TUNE −24, TONE 0). The research found this only after the envelopes were made to reach 0 (`qdecay`); before, a stuck envelope tail of noise had lifted the brightness. Compare the first 20 ms stage by stage (metallic source, colour band-pass `col`, the VCA, the high-pass `hpf`) for that corner.
- **HMETL** The metric self-test shows its TUNE +24 corners are fragile even for a perfect port, because the naive squares alias differently at 44.1 and 48 kHz. Then check whether the remaining misses cluster at TUNE +24: if so, that is inherent (the loose tier exists for it). Elsewhere, compare the colour band-pass (`qtan_acc`, Q = 3 + 3·TONE) and the swing VCA stage by stage.
- **HNOIS**: compare one oscillator pair (square × saw) spectrum against Plaits' `Oscillator` (polyBLEP, one sample late, `kMaxFrequency` 0.25), then the sum of the three pairs, then the two-stage envelope.
- For noise-dominated corners (NOISE 127), re-render with 3 seeds: a miss that moves with the seed is luck, which is spec §6 step 4 (ask the user).

Fix causes only; ledger each as a ruling; a loose miss that remains: STOP and ask the user.

- [ ] **Step 8: Commit**

```bash
git add firmware/src/dm_state.h firmware/src/dm_metal.c firmware/src/dmodels.c tests/drum_test.c tests/drum_golden.txt
git commit -m "models: HMETL, HNOIS (integer ports, fidelity strict / loose of 162 each)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---
### Task 8: Integration: lifetime, swap, overflow, cost, UI, the real build

**Files:**
- Modify: `tests/drum_test.c`, `tests/drum_fidelity.c`, `tests/ui_test.c`, `firmware/src/icons.c`, `tests/target_budget.py`, `tests/target_budget.txt`

**Interfaces:**
- Consumes: Tasks 1–7 (`DM_KBOOM` .. `DM_HNOIS`, `dm_qover`, `LIFE_A/B`).
- Produces: the Review Focus checks; the target budget of the five render functions (the hats share one); `build/ui_shots/93_sound_kboom.png`.

- [ ] **Step 1: Write the failing checks** — append to `tests/drum_test.c` before `int main`, register `test_m1c_ends(); test_m1c_swap();` after `test_step_mode_note_off();`, and as the very last check of `main` (before the summary `printf`) add
```c
#ifdef DM_QCHECK
    check("q24: no Q24 overflow in any drum test render", dm_qover == 0);
#endif
```

```c
/* every M1-C model at velocity 1 and 127, each knob at min and max: it ends by 6.0 s, faded (no click) */
static void test_m1c_ends(void)
{
    static const uint32_t M[] = {DM_KBOOM, DM_KPUNC, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS};
    uint32_t a, k, j, vv, ok = 1;
    for (a = 0; a < 6; a++)
        for (vv = 0; vv < 2; vv++)
            for (k = 0; k < 4; k++)
                for (j = 0; j < 2; j++) {
                    const param_desc_t *d = &DMODELS[M[a]].edit[k];
                    int32_t val = j ? d->max : d->min;
                    uint32_t e;
                    host_init();
                    drum_set_model(&trk[0], M[a]);
                    trk[0].p[P_E0 + k] = (int16_t)val;
                    drum_hit(&trk[0], vv ? 127u : 1u);
                    render_track(&trk[0], wl, SECS(6.5));
                    e = end_of(wl, SECS(6.5));
                    if (e > SECS(6.01) || !track_idle(&trk[0]) || peak_of(wl, SECS(5.98), SECS(6.0)) > VOICE_FS / 25) {
                        printf("     %s %s=%d vel %u: end %.2f s, last 20 ms peak %d\n", N_MODEL[M[a]], d->label, val,
                               vv ? 127u : 1u, e / (double)FS, peak_of(wl, SECS(5.98), SECS(6.0)));
                        ok = 0;
                    }
                }
    check("M1-C models: velocity 1 and 127, every knob at min and max: end by 6.0 s, faded", ok);
}

/* a ringing KBOOM, the model swapped to SSNAP (the voice state union is reused), a hit: clean */
static void test_m1c_swap(void)
{
    uint32_t q0 = dm_qover;
    host_init();
    drum_set_model(&trk[0], DM_KBOOM);
    trk[0].p[P_E1] = 127;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.2));
    drum_set_model(&trk[0], DM_SSNAP);               /* as model_step does (the IRQ off on the device) */
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(1));
    check("M1-C: model swap while a voice rings: the new model starts clean (bounded, no overflow)",
          peak_of(wl, 0, SECS(1)) <= 3 * VOICE_FS && dm_qover == q0 && trk[0].model == DM_SSNAP);
}
```

In `tests/drum_fidelity.c`, before `if (log)` at the end of `main`, add:
```c
    check("fidelity: all six M1-C models are built", built == REF_NMODELS);
```

In `tests/ui_test.c` at the end of `test_screens` (before its final `check`), add a screen of a new model:
```c
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    fm1_irq_off();
    drum_set_model(TSEL, DM_KBOOM);
    fm1_irq_on();
    press(B_EDIT);
    ui_frame();
    release_all();
    snap_page("93_sound_kboom");
    ok &= fb_lit(26, 70) > 100;
```

- [ ] **Step 2: Run the suite**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "M1-C|q24: no|all six|FAIL|PASSED"`
Expected: all `ok` (these pin behaviour Tasks 1–7 already built: if one FAILS, it is a real defect in that model; fix it there with a test-first change, and ledger it). `fidelity: all six M1-C models are built` ok.

- [ ] **Step 3: Labels and icons** — in `firmware/src/icons.c` `ICON_MAP[]`, on the `/* drum models and pages ... */` lines add:
```c
    {"PUNCH", ICON_DRIVE}, {"FM", ICON_MOD}, {"NOISE", ICON_NOISE},
```
Run the suite; open `build/ui_shots/93_sound_kboom.png` (Read tool): the SOUND page reads TUNE / DECAY / TONE / PUNCH with values, the model graph says KBOOM.

- [ ] **Step 4: Cost**

From the drum suite output, read `extreme kit: N (8 x MODEL...)` and the realistic line. Expected: `cost: realistic heavy use within the stock Felucca reference` ok and `cost: the extreme case has not grown past its recorded limit` ok (≤ 2627; the heavy models count 2 in the voice cap, Task 3). If the extreme check FAILS: STOP and ask the user (Global Constraints). Do not raise `extreme_max`.

In `tests/target_budget.py` append to `FUNCS`: `"kboom_render", "kpunc_render", "ssnap_render", "scrak_render", "hh_render"`.

- [ ] **Step 5: The real build, budget, whole suite**

```bash
for i in 1 2; do DRUM_PACKAGE=1 PYTHON=.venv/bin/python ./build.sh > build/m1c-build.log 2>&1 && break; done
grep -E " ok |^app|^package|error|warning" build/m1c-build.log
BUDGET_UPDATE=1 python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt | tail -8
PATH="$PWD/.venv/bin:$PATH" PYTHON=.venv/bin/python sh tests/run_tests.sh > build/m1c-tests.log 2>&1; grep -E "^==|FAIL|PASSED|SKIPPED" build/m1c-tests.log
```
Expected: the build links (no helper calls: a float or 64-bit helper would fail the link), no warnings, `ok` for `.ram_text`, image / RAM / pool (RAM grows by about 32 × `sizeof(dm_state_t)`, ~9 KB) and register access; `check_untouched: ok`; the budget lists the five render functions (a function inlined away: drop it from `FUNCS` and ledger it, as M1-B did for `track_render`); `ALL HOST TESTS PASSED` with no `SKIPPED`. (The Docker toolchain sometimes fails the first start after a pause with `exec format error`; the loop retries once.)

- [ ] **Step 6: Commit**

```bash
git add tests/drum_test.c tests/drum_fidelity.c tests/ui_test.c firmware/src/icons.c tests/target_budget.py tests/target_budget.txt
git commit -m "M1-C integration: lifetime / swap / overflow checks, labels, target budget; drum firmware builds with the six models

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Listening review (the user)

- [ ] **Step 1:** Run `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh` and give the user:
  - the WAV pairs in `build/drum_renders/`: ours `15_KBOOM.wav` .. `20_HNOIS.wav` (4 velocities, then each knob low / high) next to the reference `ref_KBOOM.wav` .. `ref_HNOIS.wav` (the same sequence of hits, 48 kHz, 1.5 s each);
  - per model the fidelity line (strict S / 162, loose L / 162), and the strict misses grouped by metric and corner from `build/drum_fidelity.txt`;
  - every ruling made in Tasks 3–7's spec §6 rounds;
  - the build numbers (image, RAM, pool) and the cost lines.
- [ ] **Step 2:** The user listens and approves (spec §7 item 2), or names models to revisit. A revisit is a new test-first change in that model's file, with the suite green, the fidelity lines re-reported, and the WAVs re-rendered.
