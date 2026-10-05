# M3 — Streams compressor with sidechain ducking — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One track (the SOURCE) keys a faithful port of Mutable Instruments' Streams compressor, whose gain ducks
the tracks with DUCK on, before their LEVEL and FX sends; COMP pages on SCL; FDR3 projects.

**Architecture:** `comp.c` ports `streams/compressor.cc` (configuration from our knobs, the detector, the gain
computer, Streams' log2 / exp2) plus a software model of Streams' VCA (`comp_lin`); its tables come from Streams'
resources (`tools/gen_comp_tables.py`, the attack / release table regenerated at 44.1 kHz). `fx.c`'s
`mix_block` renders the SOURCE first, runs the detector over its block (`comp_block`) and multiplies each DUCK
track by the per-sample gain before its level / pan / sends. UI: COMP pages on SCL, white keys toggle DUCK.

**Tech Stack:** C (single translation unit `firmware/src/felucca.c`, pi32v2 clang 4: no float, no 64-bit
division; 64-bit multiplies are fine), host tests in C, the Streams reference in C++ (`c++ -std=c++11`), Python 3
stdlib for the generator.

**Spec:** `docs/superpowers/specs/2026-10-05-m3-compressor-design.md`

## Global Constraints

- Never touch the device (no `tools/fm1_install.py`, no web installer, no M-VAVE updater); never install mido /
  python-rtmidi; packages only with `DRUM_PACKAGE=1` → `build/felucca-UNTESTED.fwsc`.
- Frozen (`tools/check_untouched.py`): `firmware/hal/`, `firmware/loader/`, `ota.c`, `usb.c`, `crt0.S`, `app.ld`,
  `storage.c`, `main.c` outside `felucca_init()` and the boot titles, the last 5 lines of `core.h`.
- Target C: no float, no 64-bit ÷ (64-bit × is fine), bounded loops in ISR code; tables in decimal (build.py's
  MMIO lint reads hex constants as addresses).
- Streams code is MIT: its notice (copyright + permission text) stays in `comp.c` and `comp_tables.h`; credits in
  `LICENSING.md` / `README.md`.
- SRC OFF: output bit-identical to M2 (the goldens must not change).
- Labels ≤ 5 characters (spec §3): `SRC`, `THRSH`, `AMNT`, `REL`, `ATK`, `KNEE`, `DUCK` (the spec's THRESH /
  AMOUNT shortened to fit the rule it also states).
- Knob mapping (ours, fixed): knob 0..127 → Streams 16 bit `v * 65535 / 127`; AMNT 0..63 → amount
  `32767 − v·32767/63` (1:1 … 25:1, no makeup), 64..127 → `32768 + (v−64)·32767/63` (makeup … limiter).
- Defaults: SRC OFF (0), THRSH 26 (−24.0 dB), AMNT 22 (3.9:1), ATK 2 (1.1 ms), REL 26 (151 ms), KNEE SOFT (1),
  DUCK off.
- Ask the user before changing any fidelity metric, cost budget or harness threshold; screenshots to
  `build/ui_shots/comp/`, WAVs to `build/drum_renders/`.
- Host Python: if `python3` lacks Pillow, use the private venv `PYTHON=/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin/python`
  (and put its `bin` first in `PATH` for `./build.sh`).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branch `m3-compressor`.

## Review Focus

1. **SRC changed while playing** (T1 → T3 → OFF) — no stuck gain: the detector restarts, OFF is exactly
   unity at once (Task 3: `test_comp_src_change`).
2. **Extreme settings** (THRSH 0 / 127 with AMNT 100 and 127, every track ducked, a busy kit) — no integer
   overflow, output bounded (Task 3: `test_comp_extremes`, also in the Q24-check build).
3. **The source muted from TRACKS (quick mute) or the MIDI page** — ghost key: silent at once, still ducks
   (Task 3: `test_comp_ghost`).
4. **DUCK keys while SRC is OFF and on the source's own key** — DUCK still toggles with SRC OFF (stored), the
   source's key never toggles, LEDs match (Task 4: `test_comp_keys`).
5. **FDR3 garbage and old records in flash** — clamped, FDR2 / FDR1 converted with COMP off (Task 2: boot_test).

## Commands used throughout

```bash
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca
export PYTHON=/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin/python PATH="/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin:$PATH"
python3 tools/build.py --gen-only >/dev/null
F="-O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src"
mkdir -p build/host build/ui_shots/comp build/ui_shots/seq build/ui_shots/grids build/ui_shots/engines
# drum_test:
rm -f build/host/drum_test; cc $F -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test > build/host/dt.txt; grep -v '^ok' build/host/dt.txt | tail -20
# ui_test:
rm -f build/host/ui_test; cc $F -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test > build/host/ut.txt; grep -v '^ok' build/host/ut.txt | tail -20
# boot_test (ASan / UBSan):
rm -f build/host/boot_test; cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/boot_test tests/boot_test.c -lm && build/host/boot_test > build/host/bt.txt 2>&1; grep -v '^ok' build/host/bt.txt | tail -20
# the drum suite:
sh tests/run_drum_tests.sh > build/host/drum_suite.txt 2>&1; echo "exit $?"; tail -4 build/host/drum_suite.txt
```

(In zsh, `$F` does not split: run these lines with `sh -c '...'` or a small script.)

---

### Task 1: The Streams compressor engine, its tables, the reference test

**Files:**
- Create: `tools/gen_comp_tables.py`, `firmware/src/comp_tables.h` (generated, committed), `firmware/src/comp.c`
- Create: `tests/comp_cases.h`, `tests/comp_ref.cc`, `tests/comp_fidelity.c`
- Modify: `tests/fetch_ref.sh` (+ `/streams/`), `tests/run_drum_tests.sh`, `firmware/src/felucca.c`,
  `tests/drum_host.h` (include `comp.c` after `dsp.c`)
- Test: `tests/drum_test.c` (`test_comp_engine`)

**Interfaces:**
- Produces (comp.c):
  - `typedef struct { int32_t atk, thr, amt, rel, knee; } comp_set_t;` (knobs 0..127, knee 0 / 1)
  - `typedef struct { int64_t atk, dec; int32_t ratio, thr, makeup; uint8_t soft; } comp_cfg_t;` (Streams' state
    after `Configure`; `atk == -1` = instant attack, the limiter)
  - `static uint32_t comp_k16(int32_t v)`, `static uint32_t comp_amount16(int32_t v)` (knob mapping)
  - `static void comp_configure(const comp_set_t *s, comp_cfg_t *c)`
  - `static int32_t comp_log2(int32_t v)`, `static int32_t comp_exp2(int32_t v)` (Streams' Log2 / Exp2)
  - `static int32_t comp_atten(const comp_cfg_t *c, int32_t level)` (Streams' Compress on a level in log2 units,
    65536 per octave, 0 = full scale; returns −attenuation ≤ 0)
  - `static uint32_t comp_process(const comp_cfg_t *c, int64_t *det, int32_t *gr, int32_t x)` (one sample of the
    source; returns Streams' `g`, 0..65535, unity 32767)
  - `static int32_t comp_lin(uint32_t g)` (the VCA model: Q16 linear gain, 65536 = unity)
  - `COMP_LP_TABLE` macro (defaults to `COMP_LP_COEF`, the 44.1 kHz table; the fidelity test sets the 31,089 Hz one)
- Produces (tables): `COMP_LOG2[257]`, `COMP_EXP2[257]` (uint32), `COMP_KNEE[257]`, `COMP_RATIO[257]` (uint16),
  `COMP_LP_COEF[640]` (uint32, 44.1 kHz), `COMP_ATK_MS_X10[128]`, `COMP_REL_MS_X10[128]` (uint32, the knob's time
  in 0.1 ms).

- [ ] **Step 1: Fetch Streams, write the generator, generate**

`tests/fetch_ref.sh`: add `'/streams/'` to the eurorack sparse set and make the stamp
`"$D/.ok-$EURO-$STM-$AVR-s"` (in both the check and the `touch`), so the next run refetches with Streams.

`tools/gen_comp_tables.py`:

```python
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""firmware/src/comp_tables.h from Mutable Instruments Streams' streams/resources.cc (MIT): the compressor's
log2 / exp2 / soft-knee / ratio tables as they are, the attack / release coefficients regenerated with Streams'
own formula (resources/lookup_tables.py, vactrol_time) at 44.1 kHz, and the knob -> time tables for the display.
The regenerated table at Streams' 31,089 Hz must equal Streams' (checked here).
    tools/gen_comp_tables.py RESOURCES_CC OUT_H            the firmware tables
    tools/gen_comp_tables.py RESOURCES_CC OUT_H --lp31k    only COMP_LP_31K (the fidelity test's table)"""
import math
import re
import sys

NOTICE = """/* Generated by tools/gen_comp_tables.py from Mutable Instruments Streams (streams/resources.cc,
 * eurorack 08460a6): do not edit.
 * Copyright 2014 Emilie Gillet.
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 * to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
 * copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED. */"""


def table(text, name):
    m = re.search(r"\b%s\[\] = \{(.*?)\};" % name, text, re.S)
    return [int(v) for v in re.findall(r"\d+", m.group(1))]


def vactrol_time(i):
    return {0: 0.0001, 1: 0.0002, 2: 0.0005, 3: 0.001}.get(i, 0.001 * 10 ** (i / 128.0))


def lp(rate):
    return [int(2 ** 32 / 2 * (1.0 - math.exp(-1 / (vactrol_time(i) * rate)))) for i in range(640)]


def k16(v):
    return v * 65535 // 127


def c_array(ctype, name, values, per=8):
    out = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for r in range(0, len(values), per):
        out.append("    " + ", ".join(f"{v}u" for v in values[r:r + per]) + ",")
    out.append("};")
    return out


def main():
    src, dst = sys.argv[1], sys.argv[2]
    text = open(src).read()
    if lp(31089) != table(text, "lut_lp_coefficients"):
        sys.exit("gen_comp_tables: the coefficient formula does not reproduce Streams' table at 31,089 Hz")
    if "--lp31k" in sys.argv:
        lines = ["/* COMP_LP_31K: Streams' attack / release coefficients at its own 31,089 Hz (tests only) */"]
        lines += c_array("uint32_t", "COMP_LP_31K", lp(31089))
    else:
        atk = [round(vactrol_time(k16(v) * (128 + 128 + 99) >> 16) * 10000) for v in range(128)]
        rel = [round(vactrol_time(128 + 99 + (k16(v) >> 8)) * 10000) for v in range(128)]
        lines = [NOTICE]
        lines += c_array("uint32_t", "COMP_LOG2", table(text, "lut_log2"))
        lines += c_array("uint32_t", "COMP_EXP2", table(text, "lut_exp2"))
        lines += c_array("uint16_t", "COMP_KNEE", table(text, "lut_soft_knee"))
        lines += c_array("uint16_t", "COMP_RATIO", table(text, "lut_compressor_ratio"))
        lines += ["/* attack / release coefficients (Q31) at 44.1 kHz: Streams' formula, its times kept in ms */"]
        lines += c_array("uint32_t", "COMP_LP_COEF", lp(44100))
        lines += ["/* the knob's attack / release time in 0.1 ms (display) */"]
        lines += c_array("uint32_t", "COMP_ATK_MS_X10", atk)
        lines += c_array("uint32_t", "COMP_REL_MS_X10", rel)
    open(dst, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
```

Run: `sh tests/fetch_ref.sh && python3 tools/gen_comp_tables.py build/drum_ref/eurorack/streams/resources.cc firmware/src/comp_tables.h && grep -c "static const" firmware/src/comp_tables.h && grep -A1 "COMP_ATK_MS_X10\[128\]" firmware/src/comp_tables.h | tail -1`
Expected: exit 0; `7`; the printed first ATK row starts `1u, 5u, 11u` (0.1 ms, 0.5 ms, 1.1 ms in 0.1 ms units:
knob 0 → index 0, knob 1 → index 2, knob 2 → index 5). If not, debug the generator before going on.

- [ ] **Step 2: Write the failing engine test (offline)**

`tests/drum_test.c`, before `/* PROB codes (params.c)`:

```c
/* M3 compressor engine (comp.c): the VCA model, the knob mapping, the gain computer's shape, the 44.1 kHz times.
 * The bit-exact check against Streams is tests/comp_fidelity.c. */
static void test_comp_engine(void)
{
    comp_set_t s = {2, 26, 22, 26, 1};
    comp_cfg_t c;
    int64_t det = 0;
    int32_t gr = 0, i, ok = 1;
    uint32_t g;
    double worst = 0;
    check("COMP VCA: unity is 1.0, +990 steps doubles, -1980 quarters (Q16, within 0.3 %)",
          comp_lin(32767) == 65536 && abs(comp_lin(32767 + 990) - 131072) < 400 && abs(comp_lin(32767 - 1980) - 16384) < 60);
    check("COMP knobs: AMNT 0 = 1:1, 63 = the steepest ratio, 64.. = makeup, 127 = 65535",
          comp_amount16(0) == 32767u && comp_amount16(63) == 0u && comp_amount16(64) == 32768u && comp_amount16(127) == 65535u &&
              comp_k16(127) == 65535u && comp_k16(0) == 0u);
    comp_configure(&s, &c);
    check("COMP defaults: THRSH 26 = -24 dB (Streams log2 units), about 3.9:1, no makeup",
          c.thr == (-1280 + 5 * 52) * 256 && c.ratio > 60 && c.ratio < 70 && c.makeup == 0);
    for (i = 0; i < 4410; i++)                       /* 0.1 s of silence: unity */
        g = comp_process(&c, &det, &gr, 0);
    ok &= g == 32767u;
    for (i = 0; i < 4410; i++)                       /* a loud tone: cut */
        g = comp_process(&c, &det, &gr, (i & 32) ? 30000 : -30000);
    ok &= g < 32767u - 990u && gr < 0;
    check("COMP: silence passes at unity, a loud source cuts by more than 6 dB", ok);
    s.amt = 127;
    s.thr = 127;
    comp_configure(&s, &c);
    check("COMP: AMNT 127 at THRSH 127 is Streams' limiter (instant attack, ratio 0)", c.atk == -1 && c.ratio == 0);
    s.thr = 0;
    comp_configure(&s, &c);
    check("COMP: AMNT 127 at THRSH 0 is makeup with a ratio (no limiter: the makeup does not reach 0 dB)",
          c.atk != -1 && c.ratio > 0 && c.makeup > 0);
    for (i = 4; i < 640; i++) {                      /* the 44.1 kHz table keeps Streams' times */
        double t = 0.001 * pow(10.0, i / 128.0);
        double tau = -1.0 / log(1.0 - COMP_LP_COEF[i] / 2147483648.0) / 44100.0;
        double e = fabs(tau / t - 1.0);
        if (e > worst)
            worst = e;
    }
    printf("     COMP attack / release times at 44.1 kHz: worst %.3f %% off Streams' vactrol_time\n", worst * 100.0);
    check("COMP: every attack / release time within 2 % of Streams' at 44.1 kHz", worst < 0.02);
}
```

Call it in `main` right after `test_cond_codes();`.

Run: drum_test. Expected: compile errors (`comp_set_t` unknown).

- [ ] **Step 3: Implement `comp.c` and include it**

`firmware/src/comp.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * The gain computer and detector below are a C port of Mutable Instruments Streams' compressor
 * (streams/compressor.cc, compressor.h, gain.h), under its MIT licence:
 * Copyright 2014 Emilie Gillet.
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 * to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
 * copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED. */
/* M3 sidechain compressor: Streams' compressor keyed by one track (the SOURCE), its gain applied to the DUCK
 * tracks (fx.c). Configuration as Streams' "globals" path, from our knobs; Streams drove an analog VCA with g
 * (unity 32767, 256 steps = 1.55 dB): comp_lin is that VCA in software. */
#include "comp_tables.h"
#ifndef COMP_LP_TABLE
#define COMP_LP_TABLE COMP_LP_COEF               /* 44.1 kHz (tests/comp_fidelity.c sets Streams' 31,089 Hz table) */
#endif
#define COMP_UNITY 32767                         /* Streams kUnityGain */
#define COMP_GAIN_K 990                          /* Streams kGainConstant: 1 / (1.55 / 6 * 65536 / 256) * 65536 */
#define COMP_MAX_EXP_GAIN 218453                 /* Streams kMaxExponentialGain */

typedef struct { int32_t atk, thr, amt, rel, knee; } comp_set_t;   /* ATK THRSH AMNT REL 0..127, KNEE 0 / 1 */
typedef struct {
    int64_t atk, dec;            /* attack / decay coefficients (Q31); atk -1 = instant (the limiter) */
    int32_t ratio, thr, makeup;  /* reciprocal ratio (8:8), threshold and makeup gain (log2, 65536 / octave) */
    uint8_t soft;                /* soft knee (Streams' "alternate") */
} comp_cfg_t;

static uint32_t comp_k16(int32_t v) { return (uint32_t)clamp(v, 0, 127) * 65535u / 127u; }   /* knob -> 16 bit */

/* AMNT: 0..63 the ratio from 1:1 to Streams' steepest, no makeup; 64..127 makeup gain up to the limiter */
static uint32_t comp_amount16(int32_t v)
{
    v = clamp(v, 0, 127);
    return v < 64 ? 32767u - (uint32_t)v * 32767u / 63u : 32768u + (uint32_t)(v - 64) * 32767u / 63u;
}

/* Compressor::Configure (globals path) */
static void comp_configure(const comp_set_t *s, comp_cfg_t *c)
{
    uint32_t atk_t = comp_k16(s->atk) * (128u + 128u + 99u) >> 16;   /* 0.1 ms .. 0.6 s */
    uint32_t dec_t = 128u + 99u + (comp_k16(s->rel) >> 8);           /* 59 ms .. 5.8 s */
    uint32_t amount = comp_amount16(s->amt);
    c->atk = COMP_LP_TABLE[atk_t];
    c->dec = COMP_LP_TABLE[dec_t];
    c->soft = (uint8_t)(s->knee != 0);
    c->thr = (-1280 + 5 * (int32_t)(comp_k16(s->thr) >> 8)) * 256;
    if (amount < 32768u) {                                           /* compression, no makeup */
        c->ratio = COMP_RATIO[(32767u - amount) >> 7];
        c->makeup = 0;
    } else {                                                         /* adaptive compression with makeup */
        int32_t knee_gain;
        amount -= 32768u;
        c->makeup = (int32_t)amount * (COMP_MAX_EXP_GAIN >> 8) >> 7;
        knee_gain = c->thr + c->makeup;
        if (knee_gain >= 0) {
            c->makeup = -c->thr;
            knee_gain = 0;
        }
        if (knee_gain > -4096) {                                     /* brickwall limiter, instant attack */
            c->ratio = 0;
            c->atk = -1;
        } else {
            c->ratio = knee_gain / (c->thr >> 8);
        }
    }
}

/* Compressor::Log2: Streams' shift loops done as one shift (same value) */
static int32_t comp_log2(int32_t v)
{
    uint32_t u = v > 0 ? (uint32_t)v : 1u, w = u;
    int32_t n = 0;                                                   /* bit length of u, minus 1 */
    if (w >> 16) {
        w >>= 16;
        n += 16;
    }
    if (w >> 8) {
        w >>= 8;
        n += 8;
    }
    if (w >> 4) {
        w >>= 4;
        n += 4;
    }
    if (w >> 2) {
        w >>= 2;
        n += 2;
    }
    if (w >> 1)
        n += 1;
    u = n >= 8 ? u >> (n - 8) : u << (8 - n);                        /* into 256 .. 511 */
    return (n - 8) * 65536 + (int32_t)COMP_LOG2[u - 256u];
}

/* Compressor::Exp2: the octave loops done as a shift (same value) */
static int32_t comp_exp2(int32_t v)
{
    int32_t sh = v >> 16, f = v & 0xFFFF, a = (int32_t)COMP_EXP2[f >> 8], b = (int32_t)COMP_EXP2[(f >> 8) + 1];
    int32_t m = a + ((b - a) * (f & 0xFF) >> 8);
    return sh >= 0 ? m << sh : m >> -sh;
}

/* Compressor::Compress on a level (log2 units, 0 = a 15-bit peak at full scale): -attenuation */
static int32_t comp_atten(const comp_cfg_t *c, int32_t level)
{
    int32_t pos = level - c->thr, att;
    if (pos < 0)
        return 0;
    att = pos - (pos * c->ratio >> 8);
    if (att < 65535 && c->soft) {
        int32_t a = COMP_KNEE[att >> 8], b = COMP_KNEE[(att >> 8) + 1];
        int32_t k = a + ((b - a) * (att & 0xFF) >> 8);
        att += (k - att) * ((65535 - att) >> 1) >> 15;
    }
    return -att;
}

/* Compressor::Process for one source sample x (the EXCITE and AUDIO inputs are both the source here, so
 * Streams' "is there a sidechain signal" detector, which only chooses between them, is left out) */
static uint32_t comp_process(const comp_cfg_t *c, int64_t *det, int32_t *gr, int32_t x)
{
    int32_t s = clamp(x, -32768, 32767), g;
    int64_t err = (int64_t)(s * s) - *det;
    if (err > 0)
        *det += c->atk < 0 ? err : err * c->atk >> 31;
    else
        *det += err * c->dec >> 31;
    g = comp_atten(c, (comp_log2((int32_t)*det) >> 1) - 15 * 65536);
    *gr = g >> 3;
    g = COMP_UNITY + ((g + c->makeup) * COMP_GAIN_K >> 16);
    return (uint32_t)(g > 65535 ? 65535 : g);
}

/* Streams' VCA in software: g -> linear gain, Q16 (990 steps of g = one octave = 6.02 dB) */
static int32_t comp_lin(uint32_t g)
{
    int32_t x = ((int32_t)g - COMP_UNITY) * 8465 >> 7;              /* x 65536 / 990.97: log2 units */
    return comp_exp2(clamp(x, -16 * 65536, 6 * 65536));
}
```

`firmware/src/felucca.c`: after `#include "dsp.c"` add `#include "comp.c"           /* Streams compressor (M3) */`.
`tests/drum_host.h`: after `#include "../firmware/src/dsp.c"` add `#include "../firmware/src/comp.c"`.

Run: drum_test. Expected: the six COMP lines `ok`, the printed worst time error well under 2 %; nothing else changes.

- [ ] **Step 4: The bit-exact reference test**

`tests/comp_cases.h` (shared by C and C++):

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The compressor fidelity cases (tests/comp_ref.cc, tests/comp_fidelity.c): knob settings and integer test
 * signals, 4096 samples each: kick-like decaying bursts, a tone stepping through four levels, silence - a loud
 * burst - silence (release). */
#define COMP_N 4096
#define COMP_SIGNALS 3
typedef struct { int atk, thr, amt, rel, knee; } comp_case_t;
static const int CC_ATK[3] = {0, 64, 127}, CC_REL[2] = {0, 127}, CC_THR[3] = {0, 26, 127};
static const int CC_AMT[6] = {0, 22, 63, 64, 100, 127};
#define CC_ALL (3 * 2 * 3 * 6 * 2)
static int comp_case(int i, comp_case_t *c)
{
    if (i < 0 || i >= CC_ALL)
        return 0;
    c->atk = CC_ATK[i % 3];
    c->rel = CC_REL[i / 3 % 2];
    c->thr = CC_THR[i / 6 % 3];
    c->amt = CC_AMT[i / 18 % 6];
    c->knee = i / 108;
    return 1;
}
static unsigned comp_case_k16(int v) { return (unsigned)v * 65535u / 127u; }
static unsigned comp_case_amount16(int v)
{
    return v < 64 ? 32767u - (unsigned)v * 32767u / 63u : 32768u + (unsigned)(v - 64) * 32767u / 63u;
}
static int comp_signal(int sig, int n)
{
    int p = n & 63, tri = p < 32 ? p * 64 - 1024 : (64 - p) * 64 - 1024;      /* -1024 .. 1024 */
    static const int LV[4] = {500, 4000, 16000, 32000};
    if (sig == 0) {
        int env = 1024 - (n & 1023);
        return tri * (31 * env * env / 1024) / 1024;
    }
    if (sig == 1)
        return tri * LV[(n >> 10) & 3] / 1024;
    return n >= 1024 && n < 1536 ? tri * 32000 / 1024 : 0;
}
```

`tests/comp_ref.cc`:

```cpp
// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// M3 compressor fidelity: Mutable Instruments' original streams::Compressor (fetched by tests/fetch_ref.sh) on
// the cases of tests/comp_cases.h; writes every sample's gain (uint16, little endian).   comp_ref OUT.bin
#include <cstdio>
#include <cstdlib>
#include <new>
#include "streams/compressor.h"
#include "tests/comp_cases.h"

int main(int argc, char **argv) {
  FILE *f = fopen(argc > 1 ? argv[1] : "comp_ref.bin", "wb");
  comp_case_t c;
  if (!f) return 1;
  for (int i = 0; comp_case(i, &c); i++)
    for (int sig = 0; sig < COMP_SIGNALS; sig++) {
      void *m = calloc(1, sizeof(streams::Compressor));        // zeroed state, as a fresh module
      streams::Compressor *cp = new (m) streams::Compressor();
      int32_t globals[4] = {(int32_t)comp_case_k16(c.atk), (int32_t)comp_case_k16(c.thr),
                            (int32_t)comp_case_k16(c.rel), (int32_t)comp_case_amount16(c.amt)};
      cp->Init();
      cp->Configure(c.knee != 0, globals, globals);
      for (int n = 0; n < COMP_N; n++) {
        int16_t x = (int16_t)comp_signal(sig, n);
        uint16_t g, fr;
        cp->Process(x, x, &g, &fr);
        fputc(g & 0xff, f);
        fputc(g >> 8, f);
      }
      free(m);
    }
  fclose(f);
  return 0;
}
```

`tests/comp_fidelity.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M3 compressor fidelity: comp.c (with Streams' own 31,089 Hz coefficients) against the original's gain for every
 * sample of every case (tests/comp_ref.cc): identical.   comp_fidelity REF.bin */
#include <stdint.h>
#include "comp_lp31k.h"
#define COMP_LP_TABLE COMP_LP_31K
#include "drum_host.h"
#include "comp_cases.h"

int main(int argc, char **argv)
{
    static uint8_t ref[CC_ALL * COMP_SIGNALS * COMP_N * 2];
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
    comp_case_t c;
    int i, sig, n, bad = 0, first = -1;
    if (!f || fread(ref, 1, sizeof ref, f) != sizeof ref) {
        printf("FAIL  comp fidelity: cannot read %s\n", argc > 1 ? argv[1] : "(none)");
        return 1;
    }
    fclose(f);
    for (i = 0; comp_case(i, &c); i++)
        for (sig = 0; sig < COMP_SIGNALS; sig++) {
            comp_set_t s;
            comp_cfg_t cf;
            int64_t det = 0;
            int32_t gr = 0;
            s.atk = c.atk;
            s.thr = c.thr;
            s.amt = c.amt;
            s.rel = c.rel;
            s.knee = c.knee;
            comp_configure(&s, &cf);
            for (n = 0; n < COMP_N; n++) {
                uint32_t g = comp_process(&cf, &det, &gr, comp_signal(sig, n));
                int k = ((i * COMP_SIGNALS + sig) * COMP_N + n) * 2;
                if (g != (uint32_t)(ref[k] | ref[k + 1] << 8)) {
                    bad++;
                    if (first < 0)
                        first = k / 2;
                }
            }
        }
    if (bad)
        printf("FAIL  comp fidelity: %d of %d samples differ (first: case %d signal %d sample %d)\n", bad,
               CC_ALL * COMP_SIGNALS * COMP_N, first / (COMP_SIGNALS * COMP_N), first / COMP_N % COMP_SIGNALS, first % COMP_N);
    else
        printf("ok    comp fidelity: %d cases x %d signals x %d samples identical to Streams\n", CC_ALL, COMP_SIGNALS, COMP_N);
    return bad ? 1 : 0;
}
```

`tests/run_drum_tests.sh`, in the online branch after the Grids fidelity lines:

```sh
    python3 tools/gen_comp_tables.py "$REF/eurorack/streams/resources.cc" "$OUT/comp_tables.h"
    cmp -s "$OUT/comp_tables.h" firmware/src/comp_tables.h || { echo "FAIL  comp_tables.h differs from the reference (tools/gen_comp_tables.py)"; exit 1; }
    python3 tools/gen_comp_tables.py "$REF/eurorack/streams/resources.cc" "$REF/comp_lp31k.h" --lp31k
    c++ -std=c++11 -O1 -w -DTEST -I"$E" -I. -o "$REF/comp_ref" tests/comp_ref.cc "$E/streams/compressor.cc" "$E/streams/resources.cc"
    "$REF/comp_ref" "$REF/comp_ref.bin"
    cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -Itests -I"$REF" \
        -o "$OUT/comp_fidelity" tests/comp_fidelity.c -lm
    "$OUT/comp_fidelity" "$REF/comp_ref.bin"
```

Run the reference by hand first (same commands with `REF=build/drum_ref E=build/drum_ref/eurorack OUT=build/host`).
Expected: `ok    comp fidelity: 216 cases x 3 signals x 4096 samples identical to Streams`. If samples differ:
systematic-debugging against `streams/compressor.cc` line by line (the shift forms of Log2 / Exp2 first); do not
change cases or the comparison. Then check the test can fail: change `COMP_GAIN_K` to 991 temporarily → FAIL;
restore.

- [ ] **Step 5: Whole suite, commit**

Run: the drum suite. Expected: `ALL DRUM HOST TESTS PASSED` with the comp fidelity line.

```bash
git add tools/gen_comp_tables.py firmware/src/comp_tables.h firmware/src/comp.c firmware/src/felucca.c tests/drum_host.h \
        tests/comp_cases.h tests/comp_ref.cc tests/comp_fidelity.c tests/fetch_ref.sh tests/run_drum_tests.sh tests/drum_test.c
git commit -m "m3: Streams compressor engine (C port, MIT) with its tables, 44.1 kHz times; bit-exact against the original

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Parameters, display formats, FDR3 projects

**Files:**
- Modify: `firmware/src/core.h` (F_ formats, P_DUCK, G_C*), `firmware/src/params.c`, `firmware/src/comp.c`
  (display helpers), `firmware/src/project.c`
- Test: `tests/drum_test.c` (`test_comp_formats`), `tests/ui_test.c` (project tests), `tests/boot_test.c`

**Interfaces:**
- Consumes: `comp_set_t`, `comp_cfg_t`, `comp_configure`, `comp_k16` (Task 1); `COMP_ATK_MS_X10`, `COMP_REL_MS_X10`.
- Produces: `P_DUCK` (after `P_SRC`); `G_CSRC, G_CTHR, G_CAMT, G_CREL, G_CATK, G_CKNEE` (in this order, after
  `G_GLEN3`); formats `F_CTHR, F_CAMT, F_CATK, F_CREL`; `static void fmt_ms10(char *val, const char **unit, uint32_t ms10)`
  (params.c); `static int32_t comp_thr_dbx10(int32_t v)`, `static void comp_amount_text(int32_t amt, int32_t thr,
  char *val, const char **unit)` (comp.c); `PROJ_MAGIC` = "FDR3", `PROJ_MAGIC_V2` = "FDR2", `project_v2_t`.

- [ ] **Step 1: Failing tests**

`tests/drum_test.c`, before `/* PROB codes (params.c)`:

```c
/* the COMP knobs as the columns show them */
static void test_comp_formats(void)
{
    char v[12];
    const char *u;
    int ok;
    host_init();
    param_format(&GP[G_CTHR], GP[G_CTHR].def, v, &u);
    ok = !strcmp(v, "-24.0") && !strcmp(u, "dB");
    param_format(&GP[G_CAMT], GP[G_CAMT].def, v, &u);
    ok &= !strcmp(v, "3.9") && !strcmp(u, ":1");
    param_format(&GP[G_CAMT], 0, v, &u);
    ok &= !strcmp(v, "1.0") && !strcmp(u, ":1");
    song.g[G_CTHR] = 127;
    param_format(&GP[G_CAMT], 127, v, &u);
    ok &= !strcmp(v, "LIMIT");
    song.g[G_CTHR] = 0;
    param_format(&GP[G_CAMT], 80, v, &u);
    ok &= !strcmp(v, "+5.0") && !strcmp(u, "dB");
    param_format(&GP[G_CATK], GP[G_CATK].def, v, &u);
    ok &= !strcmp(v, "1.1") && !strcmp(u, "ms");
    param_format(&GP[G_CREL], GP[G_CREL].def, v, &u);
    ok &= !strcmp(v, "151") && !strcmp(u, "ms");
    param_format(&GP[G_CSRC], 1, v, &u);
    ok &= !strcmp(v, "T1");
    check("COMP columns: THRSH -24.0 dB, AMNT 3.9:1 / 1.0:1 / LIMIT (THRSH 127) / +5.0 dB (THRSH 0, AMNT 80), ATK 1.1 ms, REL 151 ms, SRC T1", ok);
}
```

Call it after `test_comp_engine();`.

`tests/ui_test.c` `test_project_roundtrip`: before `song.g[G_BPM] = 133;` add

```c
    trk[3].p[P_DUCK] = 1;
    song.g[G_CSRC] = 1;
    song.g[G_CAMT] = 99;
    song.g[G_CKNEE] = 0;
```

and a check after the Grids one:

```c
    check("project: load restores DUCK and the COMP settings",
          trk[3].p[P_DUCK] == 1 && song.g[G_CSRC] == 1 && song.g[G_CAMT] == 99 && song.g[G_CKNEE] == 0);
```

`test_project_rejects`: after `proj_slot[0].g[G_GLEN1] = -5;` add `proj_slot[0].g[G_CSRC] = 77; proj_slot[0].t[3].p[P_DUCK] = 5;`
and extend its condition with `&& song.g[G_CSRC] == 8 && trk[3].p[P_DUCK] == 1`.

`tests/boot_test.c` — `flash_image`: slot 1 an FDR1 record (as now: `k == 1`), slot 3 an FDR2 record:

```c
            if (kind == F_HEADERS && k == 1u) {      /* an M1 project ("FDR1"): valid sum over random fields */
                project_v1_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V1;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
            if (kind == F_HEADERS && k == 3u) {      /* an M2 project ("FDR2"): valid sum over random fields */
                project_v2_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V2;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
```

(replacing the `k & 1u` block). Add after `fdr1_converts`:

```c
/* an M2 project in flash ("FDR2") loads: everything it has, COMP off, DUCK off */
static int fdr2_converts(void)
{
    project_v2_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V2;
    v.size = sizeof v;
    for (i = 0; i < G_CSRC; i++)
        v.g[i] = song.g[i];
    v.g[G_GLEN2] = 5;
    v.sel = 3;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_DUCK; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[2].p[P_SRC] = 3;
    v.t[2].step[9].on = 1;
    v.t[2].step[9].cond = 33;
    v.t[2].step[9].rat = 2;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 2u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    song.g[G_CSRC] = 4;                              /* the live state differs: the load replaces it */
    trk[2].p[P_DUCK] = 1;
    project_load(2);
    return song.g[G_GLEN2] == 5 && song.sel == 3 && trk[2].p[P_SRC] == 3 && trk[2].step[9].on &&
           trk[2].step[9].cond == 33 && trk[2].step[9].rat == 2 && song.g[G_CSRC] == 0 && trk[2].p[P_DUCK] == 0 &&
           song.g[G_CAMT] == GP[G_CAMT].def;
}
```

and in `main` after the FDR1 check:

```c
    check("project: an M2 record (FDR2) loads with COMP off and DUCK off", fdr2_converts());
```

Run: drum_test, ui_test, boot_test. Expected: compile errors (`G_CTHR`, `P_DUCK`, `project_v2_t` …).

- [ ] **Step 2: Implement**

`firmware/src/core.h`: the F_ enum gains `F_CTHR, F_CAMT, F_CATK, F_CREL` after `F_STEPS`. P enum:

```c
    P_SRC,                       /* what the track plays: 0 its steps, 1..3 a Grids channel (kick, snare, hats) */
    P_DUCK,                      /* M3: the COMP source ducks this track */
    P_COUNT
```

G enum after `G_GLEN1, G_GLEN2, G_GLEN3,`:

```c
    G_CSRC, G_CTHR, G_CAMT, G_CREL,                  /* COMP (comp.c): source track (0 off), threshold, amount, release */
    G_CATK, G_CKNEE,                                 /* attack, soft knee */
```

`firmware/src/comp.c`, at the end:

```c
/* ------------------------------------------------------- display --- */
static int32_t comp_thr_dbx10(int32_t v)          /* THRSH in 0.1 dB, rounded (Streams: 256 = 6.02 dB / 256) */
{
    int32_t x = -1280 + 5 * (int32_t)(comp_k16(v) >> 8);
    return (x * 60206 + (x < 0 ? -128000 : 128000)) / 256000;
}

/* AMNT as the column shows it: the ratio ("3.9" ":1"), the makeup ("+6.0" "dB") or "LIMIT" (at threshold thr) */
static void comp_amount_text(int32_t amt, int32_t thr, char *val, const char **unit)
{
    comp_set_t s = {0, thr, amt, 0, 1};
    comp_cfg_t c;
    comp_configure(&s, &c);
    *unit = "";
    if (clamp(amt, 0, 127) < 64) {
        fmt_fix(val, 2560 / (c.ratio > 0 ? c.ratio : 1), 1);
        *unit = ":1";
    } else if (c.atk < 0) {
        str_cpy(val, "LIMIT", 6);
    } else {
        char t[8];
        fmt_fix(t, c.makeup / 1088, 1);            /* log2 units -> 0.1 dB (6.02 dB / 65536) */
        val[0] = '+';
        str_cpy(val + 1, t, 6);
        *unit = "dB";
    }
}
```

`firmware/src/params.c` — names:

```c
static const char *const N_CSRC[] = {"OFF", "T1", "T2", "T3", "T4", "T5", "T6", "T7", "T8"};
static const char *const N_KNEE[] = {"HARD", "SOFT"};
```

`TP`: `[P_DUCK] = PE("DUCK", N_ONOFF, 0),`. `GP` after `[G_GLEN3]`:

```c
    [G_CSRC] = PE("SRC", N_CSRC, 0),
    [G_CTHR] = PD("THRSH", F_CTHR, 0, 127, 26),
    [G_CAMT] = PD("AMNT", F_CAMT, 0, 127, 22),
    [G_CREL] = PD("REL", F_CREL, 0, 127, 26),
    [G_CATK] = PD("ATK", F_CATK, 0, 127, 2),
    [G_CKNEE] = PE("KNEE", N_KNEE, 1),
```

Before `param_format`, the shared time format (F_TIME's code, moved):

```c
static void fmt_ms10(char *val, const char **unit, uint32_t ms10)   /* a time in 0.1 ms: "4.5ms", "120ms", "1.20s" */
{
    if (ms10 < 100u) {
        fmt_fix(val, (int32_t)ms10, 1);
        *unit = "ms";
    } else if (ms10 < 10000u) {
        fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
        *unit = "ms";
    } else {
        fmt_fix(val, (int32_t)(ms10 / 100u), 2);
        if (ms10 >= 100000u)
            fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
        *unit = "s";
    }
}
```

`case F_TIME:` becomes `fmt_ms10(val, unit, TIME_MS_X10[v & 127]); break;` and new cases:

```c
    case F_CTHR:
        fmt_fix(val, comp_thr_dbx10(v), 1);
        *unit = "dB";
        break;
    case F_CAMT:
        comp_amount_text(v, song.g[G_CTHR], val, unit);
        break;
    case F_CATK:
        fmt_ms10(val, unit, COMP_ATK_MS_X10[clamp(v, 0, 127)]);
        break;
    case F_CREL:
        fmt_ms10(val, unit, COMP_REL_MS_X10[clamp(v, 0, 127)]);
        break;
```

`firmware/src/project.c`: header comment's format sentence → `Format "FDR3": … (with PROB / RATCH, DUCK, the COMP
settings). M1's "FDR1" and M2's "FDR2" records (in flash) are converted on load.` Magics:

```c
#define PROJ_MAGIC 0x33524446u                 /* "FDR3": + DUCK per track and the COMP settings (M3) */
#define PROJ_MAGIC_V2 0x32524446u              /* "FDR2": M2 projects, converted on load */
#define PROJ_MAGIC_V1 0x31524446u              /* "FDR1": M1 projects, converted on load */
```

Replace the `#if FELUCCA_FLASH` block's v1 types / `proj_v1` / `proj_from_v1` / `proj_fetch` with:

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
/* M2's format: the globals before G_CSRC, the parameters before P_DUCK */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CSRC];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_DUCK]; step_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v2_t;
static union { project_v1_t v1; project_v2_t v2; } proj_old;

/* an old record (proj_old, version ver) -> q: what it has; the rest at the defaults (v1: PROB 100 %, 1 hit,
 * SRC STEP, Grids; both: COMP off, DUCK off) */
static void proj_from_old(project_t *q, uint32_t ver)
{
    uint32_t i, k, ng = ver == 1u ? (uint32_t)G_GMODE : (uint32_t)G_CSRC, np = ver == 1u ? (uint32_t)P_SRC : (uint32_t)P_DUCK;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    for (i = 0; i < G_COUNT; i++)
        q->g[i] = i >= ng ? GP[i].def : ver == 1u ? proj_old.v1.g[i] : proj_old.v2.g[i];
    q->sel = ver == 1u ? proj_old.v1.sel : proj_old.v2.sel;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++)
            q->t[k].p[i] = i >= np ? TP[i].def : ver == 1u ? proj_old.v1.t[k].p[i] : proj_old.v2.t[k].p[i];
        for (i = 0; i < NSTEP; i++) {
            if (ver == 1u) {
                q->t[k].step[i].on = proj_old.v1.t[k].step[i].on;
                q->t[k].step[i].acc = proj_old.v1.t[k].step[i].acc;
            } else {
                q->t[k].step[i] = proj_old.v2.t[k].step[i];
            }
        }
    }
    q->sum = proj_sum(q);
}

static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), q, sizeof *q);
    if (n == (int)sizeof proj_old.v1 || n == (int)sizeof proj_old.v2) {
        uint32_t ver = n == (int)sizeof proj_old.v1 ? 1u : 2u, hdr[2], sum;
        memcpy(&proj_old, q, (uint32_t)n);
        memcpy(hdr, &proj_old, sizeof hdr);
        memcpy(&sum, (const uint8_t *)&proj_old + n - 4, 4);
        if (hdr[0] == (ver == 1u ? PROJ_MAGIC_V1 : PROJ_MAGIC_V2) && hdr[1] == (uint32_t)n &&
            sum == proj_hash(&proj_old, (uint32_t)n - 4u)) {
            proj_from_old(q, ver);
            return;
        }
    }
    if (n != (int)sizeof *q || !proj_ok(q))
        q->magic = 0;
}
```

(`project_load`'s clamping already covers `P_DUCK` through `TP` and the COMP globals through `GP`.)

- [ ] **Step 3: Run to see them pass**

Run: drum_test, ui_test, boot_test. Expected: the COMP format line, both project lines (DUCK / COMP restored,
clamped), the FDR1 and FDR2 lines `ok`; `boot_test: all passed`.

- [ ] **Step 4: Whole suite, commit**

Run: the drum suite. Expected: `ALL DRUM HOST TESTS PASSED`.

```bash
git add firmware/src/core.h firmware/src/params.c firmware/src/comp.c firmware/src/project.c tests/drum_test.c tests/ui_test.c tests/boot_test.c
git commit -m "m3: DUCK per track and the COMP settings (SRC THRSH AMNT REL ATK KNEE); FDR3 projects, FDR2 / FDR1 converted

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The sidechain in the mix: source first, ghost key, ducking before the sends

**Files:**
- Modify: `firmware/src/comp.c` (runtime: `comp`, `comp_on`, `comp_src`, `comp_knobs`, `comp_reset`, `comp_block`)
- Modify: `firmware/src/fx.c` (`mix_part`, `mix_block`), `firmware/src/drum_core.c` (`drum_hit`: ghost key)
- Modify: `tests/drum_host.h` (`host_reset_fx`: `comp_reset()`)
- Test: `tests/drum_test.c`

**Interfaces:**
- Consumes: Task 1's engine, Task 2's `G_C*`, `P_DUCK`.
- Produces: `static struct { comp_cfg_t cfg; uint32_t sig; int64_t det; int32_t gr, peak; int32_t gain[CTL]; } comp;`
  `static int comp_on(void)`, `static uint32_t comp_src(void)` (the source track 0..7, `NTRK` = off),
  `static comp_set_t comp_knobs(void)`, `static void comp_reset(void)`, `static void comp_block(const int32_t *x, uint32_t n)`
  (x = 0: silence); `mix_part(track_t *t, uint32_t n, uint32_t src)`.

- [ ] **Step 1: Failing tests**

`tests/drum_test.c`, before `/* PROB codes (params.c)`:

```c
/* a kick on T1 (the COMP source) under a long cymbal on T2: T2's peak (post LEVEL) right after the kick and later,
 * after a fresh cymbal hit; options: T2 ducked, the source muted / at LEVEL 0, T2's reverb send measured */
/* (src_before: the SRC during the first 0.25 s, then src; src_duck: DUCK on the source itself) */
typedef struct { int32_t after, late, send, src_peak; } comp_meas_t;
static comp_meas_t comp_scene(int src, int duck, int src_muted, int src_level0, int src_duck, int src_before)
{
    comp_meas_t m = {0, 0, 0, 0};
    uint32_t f, i;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_CYMB);
    trk[1].p[P_REV] = 127;
    song.g[G_CSRC] = (int16_t)src_before;
    trk[1].p[P_DUCK] = (int16_t)duck;
    trk[0].p[P_DUCK] = (int16_t)src_duck;
    trk[0].p[P_MUTE] = (int16_t)src_muted;
    if (src_level0)
        trk[0].p[P_LEVEL] = 0;
    drum_hit(&trk[1], 127);
    render_mix(0, 0, SECS(0.25));
    song.g[G_CSRC] = (int16_t)src;
    drum_hit(&trk[0], 127);
    for (f = 0; f < SECS(0.06); f += CTL) {
        trk[1].peak = trk[0].peak = 0;
        render_mix(0, 0, CTL);
        if (trk[1].peak > m.after)
            m.after = trk[1].peak;
        if (trk[0].peak > m.src_peak)
            m.src_peak = trk[0].peak;
        for (i = 0; i < CTL; i++)
            if (abs(send_r[i]) > m.send)
                m.send = abs(send_r[i]);
    }
    render_mix(0, 0, SECS(1.6));
    drum_hit(&trk[1], 127);
    for (f = 0; f < SECS(0.05); f += CTL) {
        trk[1].peak = 0;
        render_mix(0, 0, CTL);
        if (trk[1].peak > m.late)
            m.late = trk[1].peak;
    }
    return m;
}

static void test_comp_ducks(void)
{
    comp_meas_t dry = comp_scene(1, 0, 0, 0, 0, 1), duck = comp_scene(1, 1, 0, 0, 0, 1);
    printf("     COMP: T2 peak after the kick %d -> %d, its reverb send %d -> %d, later %d -> %d\n", dry.after,
           duck.after, dry.send, duck.send, dry.late, duck.late);
    check("COMP: a DUCK track drops by more than 6 dB under the source's hit", duck.after * 2 < dry.after);
    check("COMP: its FX send is ducked too", duck.send * 2 < dry.send);
    check("COMP: it comes back after the release (within 1 dB)", duck.late * 10 > dry.late * 9);
}

static void test_comp_off_identical(void)
{
    static int32_t a[SECS(1)], b[SECS(1)];
    uint32_t k;
    for (k = 0; k < 2; k++) {
        uint32_t i;
        host_init();
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_DUCK] = (int16_t)k;           /* every track ducked, but SRC OFF */
        trk[0].step[0].on = trk[1].step[4].on = trk[3].step[2].on = 1;
        play();
        render_mix(k ? b : a, 0, SECS(1));
    }
    check("COMP: SRC OFF leaves the mix bit-identical, DUCK or not", !memcmp(a, b, sizeof a));
}

static void test_comp_source_not_ducked(void)
{
    comp_meas_t a = comp_scene(1, 1, 0, 0, 0, 1), b = comp_scene(1, 1, 0, 0, 1, 1);
    check("COMP: the source is never ducked by itself (DUCK on the source changes nothing)",
          a.src_peak == b.src_peak && a.after == b.after);
}

static void test_comp_ghost(void)
{
    comp_meas_t heard = comp_scene(1, 1, 0, 0, 0, 1), ghost = comp_scene(1, 1, 1, 0, 0, 1);
    comp_meas_t quiet = comp_scene(1, 1, 0, 1, 0, 1);
    check("COMP ghost key: a muted source is silent and still ducks exactly as heard",
          ghost.src_peak == 0 && ghost.after == heard.after && ghost.late == heard.late);
    check("COMP: a source at LEVEL 0 still ducks exactly as heard", quiet.after == heard.after);
}

/* SRC changed while playing: T2 (the cymbal) as the source for 0.25 s charges the detector, then T1: the same
 * as T1 from the start (a fresh detector); and OFF is unity at once */
static void test_comp_src_change(void)
{
    comp_meas_t a = comp_scene(1, 1, 0, 0, 0, 1), b = comp_scene(1, 1, 0, 0, 0, 2);
    uint32_t i, unity = 1;
    song.g[G_CSRC] = 0;
    render_mix(0, 0, CTL);
    for (i = 0; i < CTL; i++)
        unity &= comp.gain[i] == 65536;
    check("COMP: SRC changed while playing restarts the detector; SRC OFF is unity at once",
          a.after == b.after && a.late == b.late && unity);
}

static void test_comp_extremes(void)
{
    static const int16_t S[4][2] = {{0, 127}, {0, 100}, {127, 100}, {127, 127}};   /* THRSH, AMNT */
    uint32_t c, i, ok = 1;
    for (c = 0; c < 4u; c++) {
        int32_t pk;
        host_init();
        song.g[G_CSRC] = 1;
        song.g[G_CTHR] = S[c][0];
        song.g[G_CAMT] = S[c][1];
        for (i = 0; i < NTRK; i++) {
            uint32_t k;
            trk[i].p[P_DUCK] = 1;
            trk[i].p[P_LEVEL] = 127;
            for (k = 0; k < 16u; k++)
                trk[i].step[k].on = 1;
        }
        play();
        render_mix(wl, wr, SECS(2));
        pk = peak_of(wl, 0, SECS(2));
        ok &= pk <= 32767 && peak_of(wr, 0, SECS(2)) <= 32767;
    }
    check("COMP extremes (THRSH 0 / 127 x AMNT 100 / 127, all tracks ducked and busy): output bounded", ok);
}
```

Call them in `main` after `test_comp_formats();`: `test_comp_ducks(); test_comp_off_identical();
test_comp_source_not_ducked(); test_comp_ghost(); test_comp_src_change(); test_comp_extremes();`.

Run: drum_test. Expected: compile errors (`comp` / `send_r` usage fine, `comp.gain` undeclared).

- [ ] **Step 2: Implement**

`firmware/src/comp.c`, after `comp_lin`:

```c
/* --------------------------------------------------- the sidechain --- */
static struct {
    comp_cfg_t cfg;              /* configured for the knobs whose signature is sig */
    uint32_t sig;                /* 0 = not configured */
    int64_t det;                 /* the detector (Streams detector_) */
    int32_t gr;                  /* gain reduction for the meter (Streams gain_reduction_) */
    int32_t peak;                /* the source's peak this block (meter) */
    int32_t gain[CTL];           /* this block's gain per sample, Q16, for the DUCK tracks */
} comp;

static int comp_on(void) { return song.g[G_CSRC] >= 1 && song.g[G_CSRC] <= NTRK; }
static uint32_t comp_src(void) { return comp_on() ? (uint32_t)song.g[G_CSRC] - 1u : NTRK; }   /* NTRK = off */
static comp_set_t comp_knobs(void)
{
    comp_set_t s;
    s.atk = clamp(song.g[G_CATK], 0, 127);
    s.thr = clamp(song.g[G_CTHR], 0, 127);
    s.amt = clamp(song.g[G_CAMT], 0, 127);
    s.rel = clamp(song.g[G_CREL], 0, 127);
    s.knee = song.g[G_CKNEE] ? 1 : 0;
    return s;
}

static void comp_reset(void)                     /* a fresh detector (PLAY of the module), unity gain */
{
    uint32_t i;
    comp.det = 0;
    comp.gr = 0;
    comp.peak = 0;
    comp.sig = 0;
    for (i = 0; i < CTL; i++)
        comp.gain[i] = 65536;
}

/* the source's block x (0: silence): the gain per sample for the DUCK tracks */
static void comp_block(const int32_t *x, uint32_t n)
{
    comp_set_t s = comp_knobs();
    uint32_t i, sig = 1u + (uint32_t)s.atk + (uint32_t)s.thr * 128u + (uint32_t)s.amt * 16384u +
                      (uint32_t)s.rel * 2097152u + (uint32_t)s.knee * 268435456u;
    if (sig != comp.sig) {
        comp_configure(&s, &comp.cfg);
        comp.sig = sig;
    }
    comp.peak = 0;
    for (i = 0; i < n && i < CTL; i++) {
        int32_t v = x ? x[i] : 0, a = v < 0 ? -v : v;
        if (a > comp.peak)
            comp.peak = a;
        comp.gain[i] = comp_lin(comp_process(&comp.cfg, &comp.det, &comp.gr, v));
    }
}
```

`firmware/src/drum_core.c`, `drum_hit`:

```c
    if (t->p[P_MUTE] && (uint32_t)(t - trk) != comp_src())   /* a muted COMP source still plays (ghost key, fx.c) */
        return;
```

`firmware/src/fx.c` — `mix_part` gains the source index:

```c
/* one track into the mix. src: the COMP source (NTRK = off). The source's block (after DIST / SLICER, before
 * LEVEL and MUTE) feeds the compressor; a muted source is heard by it only (ghost key). A DUCK track is
 * multiplied by the compressor's gain before its level, pan and sends. */
static void mix_part(track_t *t, uint32_t n, uint32_t src)
{
    int32_t *b = part_buf;
    uint32_t i, is_src = (uint32_t)(t - trk) == src;
    if (track_render(t, b, n))
        t->tail = 16;                                   /* blocks of DIST state to run out after the last voice */
    else if ((!t->tail || !t->p[P_DIST] || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        if (is_src)
            comp_block(0, n);                           /* silence: the detector decays */
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN];
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
        int32_t xmax = c > d ? c : d;
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        track_dist(t, b, n);
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
        if (is_src) {
            comp_block(b, n);
            if (t->p[P_MUTE])
                return;                                 /* ghost key: the compressor heard it, nobody else */
        } else if (src < NTRK && t->p[P_DUCK]) {
            for (i = 0; i < n; i++)
                b[i] = clamp((int32_t)(((int64_t)b[i] * comp.gain[i]) >> 16), -(1 << 19), 1 << 19);
        }
        for (i = 0; i < n; i++) {
```

(the rest of the loop and the function unchanged).

`mix_block`:

```c
    events_block(n);
    drum_block_begin();
    {
        uint32_t src = comp_src();
        if (src != comp_was) {                          /* another source, or OFF: a fresh detector (comp_was:
                                                         * file scope, before mix_block, = NTRK at start) */
            comp_reset();
            comp_was = src;
        }
        if (src < NTRK)
            mix_part(&trk[src], n, src);                /* the source first: its block keys the others */
        for (i = 0; i < NTRK; i++)
            if (i != src)
                mix_part(&trk[i], n, src);
    }
    fx_buses(send_c, send_d, send_r, wet, n);
```

In fx.c, just before `mix_block`: `static uint32_t comp_was = NTRK;                 /* the source of the last block */`.
`tests/drum_host.h` `host_reset_fx`: add `comp_reset();` and `comp_was = NTRK;`.

- [ ] **Step 3: Run to see them pass**

Run: drum_test, then the Q24-check build (`-DDM_QCHECK`, as in run_drum_tests.sh).
Expected: all COMP lines `ok` (printed: T2 drops by well over 6 dB, its send too, comes back); the goldens and every
existing test unchanged; the Q24-check build passes.

- [ ] **Step 4: Whole suite, commit**

Run: the drum suite. Expected: `ALL DRUM HOST TESTS PASSED`.

```bash
git add firmware/src/comp.c firmware/src/fx.c firmware/src/drum_core.c tests/drum_host.h tests/drum_test.c
git commit -m "m3: sidechain in the mix: the COMP source is rendered first and keys the compressor (also muted: ghost key); DUCK tracks are ducked before level and sends

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: COMP pages on SCL, DUCK keys, meters and curve

**Files:**
- Modify: `firmware/src/pages.c`, `firmware/src/ui.c`, `firmware/src/ui_input.c`, `firmware/src/ui_draw.c`
- Modify: `tests/run_drum_tests.sh` (`build/ui_shots/comp`)
- Test: `tests/ui_test.c`

**Interfaces:**
- Consumes: `comp`, `comp_on`, `comp_src`, `comp_knobs`, `comp_configure`, `comp_atten` (Tasks 1–3), `G_C*`, `P_DUCK`.
- Produces: `FAM_COMP` (between `FAM_GRIDS` and `FAM_MIX`), `GR_COMP`; `static int comp_mode(void)`,
  `static void track_duck_toggle(uint32_t k)` (ui.c); `static void graph_comp(uint16_t c, int curve)` (ui_draw.c).

- [ ] **Step 1: Failing tests**

`tests/ui_test.c` — `test_families`: add `{B_SCL, FAM_COMP}` to `MAP`, the check text gains `, SCL COMP`, and the
`SCL does nothing` block becomes:

```c
    press(B_SCL);
    ui_frame();
    release_all();
    check("SCL opens COMP", !ui.home && cur_page()->fam == FAM_COMP);
```

New tests (before `static void test_grid_keys(void)`):

```c
static void test_comp_pages(void)
{
    ui_host_init();
    press(B_SCL);
    ui_frame();
    release_all();
    check("SCL opens COMP 1/2 (SRC THRSH AMNT REL)", str_eq(cur_page()->title, "COMP") && cur_page()->id[0] == G_CSRC);
    snap_page("comp/01_off");
    turn(EN_K1, 1);
    ui_frame();
    turn(EN_K1 + 1, -2);
    ui_frame();
    check("COMP: KNOB 1 picks the source (T1), KNOB 2 the threshold", song.g[G_CSRC] == 1 && song.g[G_CTHR] == 24);
    press(B_SCL);
    ui_frame();
    release_all();
    turn(EN_K1 + 1, -1);
    ui_frame();
    check("SCL again: COMP 2/2 (ATK KNEE); KNOB 2 sets the knee HARD", cur_page()->id[0] == G_CATK && song.g[G_CKNEE] == 0);
    snap_page("comp/03_page2_curve_hard");
}

/* COMP pages: white keys toggle DUCK (lit = ducked), not on the source; with SRC OFF too; elsewhere they play */
static void test_comp_keys(void)
{
    uint32_t a;
    ui_host_init();
    press(B_SCL);
    ui_frame();
    release_all();
    a = hit_age(&trk[1]);
    keys(1u << KEY_TRK_KEY[1]);
    ui_frame();
    keys(0);
    ui_frame();
    check("COMP (SRC OFF): key 2 sets DUCK on T2, no hit, its LED lit",
          trk[1].p[P_DUCK] == 1 && hit_age(&trk[1]) == a && led_lit(14u + KEY_TRK_KEY[1]));
    song.g[G_CSRC] = 1;
    keys(1u << KEY_TRK_KEY[0]);
    ui_frame();
    keys(0);
    ui_frame();
    check("COMP: the source's key (T1) does not toggle DUCK", trk[0].p[P_DUCK] == 0 && !led_lit(14u + KEY_TRK_KEY[0]));
    keys(1u << KEY_TRK_KEY[1]);
    ui_frame();
    keys(0);
    ui_frame();
    check("COMP: key 2 again: DUCK off", trk[1].p[P_DUCK] == 0 && !led_lit(14u + KEY_TRK_KEY[1]));
    trk[1].p[P_DUCK] = trk[3].p[P_DUCK] = trk[4].p[P_DUCK] = 1;
    drum_set_model(&trk[1], DM_HATO);
    trk[0].step[0].on = trk[0].step[4].on = trk[0].step[8].on = trk[0].step[12].on = 1;
    trk[1].step[2].on = trk[1].step[6].on = trk[1].step[10].on = trk[1].step[14].on = 1;
    transport_req = 1;
    seq_play_to(0, 1);
    snap_page("comp/02_pumping");
    song.g[G_CAMT] = 127;
    song.g[G_CTHR] = 127;                            /* Streams' limiter: AMNT at the end with a high THRSH */
    seq_play_to(0, 5);
    snap_page("comp/04_limiter");
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    a = hit_age(&trk[2]);
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    check("leaving COMP: the keys play again", hit_age(&trk[2]) != a);
}
```

Call them in `main` after `test_quick_mute();`. In `tests/run_drum_tests.sh`: `mkdir -p … build/ui_shots/comp` and
add `build/ui_shots/comp/*.ppm` to the `rm -f` list.

Run: ui_test. Expected: compile errors (`FAM_COMP`).

- [ ] **Step 2: Implement**

`firmware/src/pages.c`:

```c
enum { FAM_HOME, FAM_SND, FAM_TRK, FAM_LAY, FAM_FX, FAM_SEQ, FAM_GLO, FAM_SAVE, FAM_GRIDS, FAM_COMP, FAM_MIX, FAM_COUNT };
...
enum { GR_NONE, GR_MODEL, GR_FX, GR_SLCR, GR_GRID, GR_STEPS, GR_SLOTS, GR_MIX, GR_GRIDS, GR_COMP };
```

PAGES, before TRACKS:

```c
    {"COMP", FAM_COMP, SC_GLOBAL, GR_COMP, {G_CSRC, G_CTHR, G_CAMT, G_CREL}},   /* keys: DUCK per track */
    {"COMP", FAM_COMP, SC_GLOBAL, GR_COMP, {G_CATK, G_CKNEE, 0xFF, 0xFF}},
```

FAM_BTN: `/* the button of each family */` and `{B_HOME, B_EDIT, B_ENV, B_LFO, B_FX, B_SEQ, B_GLO, B_SAVE, B_ARP, B_SCL, B_REC}`.

`firmware/src/ui.c`, after `mix_mode`:

```c
static int comp_mode(void) { return !ui.home && !ui.menu && cur_page()->fam == FAM_COMP; }   /* COMP: keys DUCK */
```

`page_entered`: `song.seq_mode = (uint8_t)(grid_mode() || mix_mode() || comp_mode());`. After `track_mute_toggle`:

```c
/* COMP: white key k turns DUCK of track k on / off (not the source's: it is never ducked) */
static void track_duck_toggle(uint32_t k)
{
    if (k < NTRK && k != comp_src())
        trk[k].p[P_DUCK] = (int16_t)!trk[k].p[P_DUCK];
}
```

`firmware/src/ui_input.c` — `ui_leds`, after the `mix_mode()` branch:

```c
    } else if (comp_mode()) {                         /* COMP: the ducked tracks' keys */
        for (k = 0; k < NTRK; k++)
            led_put(nl, 14u + KEY_TRK_KEY[k], trk[k].p[P_DUCK] && k != comp_src());
```

`ui_input`, after the `mix_mode()` key branch:

```c
    } else if (comp_mode()) {
        for (k = 0; k < NTRK; k++)                      /* COMP: a white key ducks / unducks its track */
            if ((notes >> KEY_TRK_KEY[k]) & 1u)
                track_duck_toggle(k);
```

`firmware/src/seq.c` keyboard_block comment: `the STEP grid / TRACKS mutes / COMP ducks (ui_input.c) own presses`.

`firmware/src/ui_draw.c`, after `draw_mix` (it uses `meter_px` and `MX_LW`):

```c
/* COMP pages: the routing, the gain reduction now and the source's level; page 2 adds the curve (input -> output
 * level, -48..0 dB in, -48..+12 dB out) for the knobs' THRSH / AMNT / KNEE */
static void graph_comp(uint16_t c, int curve)
{
    char r[48], v[12];
    uint32_t k, src = comp_src(), n, any = 0;
    int32_t dbx10 = comp_on() ? (-comp.gr) * 241 / 32768 : 0;
    str_cpy(r, "SRC: ", sizeof r);
    n = str_len(r);
    if (src < NTRK) {
        r[n++] = 'T';
        r[n++] = (char)('1' + src);
        r[n] = 0;
    } else {
        str_cpy(r + n, "OFF", sizeof r - n);
    }
    str_cpy(r + str_len(r), "  DUCK:", sizeof r - str_len(r));
    n = str_len(r);
    for (k = 0; k < NTRK && n + 4u < sizeof r; k++)
        if (trk[k].p[P_DUCK] && k != src) {
            r[n++] = ' ';
            r[n++] = 'T';
            r[n++] = (char)('1' + k);
            r[n] = 0;
            any = 1;
        }
    if (!any)
        str_cpy(r + n, " -", sizeof r - n);
    cv_text(4, 2, &FONT_S, r, C_AMB);
    cv_text(4, 24, &FONT_S, "GR", C_GRAY);
    cv_rect(36, 31, curve ? 70 : 150, 1, C_LINE);
    cv_rect(36, 27, clamp(dbx10, 0, 240) * (curve ? 70 : 150) / 240, 9, c);
    fmt_fix(v, -dbx10, 1);
    str_cpy(v + str_len(v), "dB", sizeof v - str_len(v));
    cv_text(curve ? 36 : 192, curve ? 40 : 24, &FONT_S, v, C_WHITE);
    cv_text(4, curve ? 62 : 46, &FONT_S, "IN", C_GRAY);
    cv_rect(36, curve ? 69 : 53, curve ? 70 : 150, 1, C_LINE);
    cv_rect(36, curve ? 65 : 49, (comp_on() ? meter_px(comp.peak) : 0) * (curve ? 70 : 150) / MX_LW, 9, C_AMB);
    if (curve) {
        comp_set_t s = comp_knobs();
        comp_cfg_t cf;
        comp_configure(&s, &cf);
        cv_rect(118, 22, 1, 77, C_LINE);
        cv_rect(118, 98, 118, 1, C_LINE);
        for (k = 0; k < 116u; k++) {
            int32_t in10 = -480 + (int32_t)k * 480 / 116, lvl = in10 * 65536 / 60;
            int32_t out10 = (lvl + comp_atten(&cf, lvl) + cf.makeup) * 60 / 65536;
            int32_t y = 98 - (out10 + 480) * 76 / 600;
            if (y >= 22 && y <= 97)
                cv_rect(120 + (int32_t)k, y, 1, 2, c);
        }
    }
}
```

`draw_graph`: add

```c
        case GR_COMP:
            graph_comp(c, pg->id[0] == G_CATK);
            break;
```

`graph_signature`, before `return h;`:

```c
    if (pg->graph == GR_COMP) {
        for (i = G_CSRC; i <= G_CKNEE; i++)
            h = (h ^ (uint32_t)song.g[i]) * 16777619u;
        for (i = 0; i < NTRK; i++)
            h = (h ^ (uint32_t)trk[i].p[P_DUCK]) * 16777619u;
        h ^= ui.page * 389u;
        if (comp_on())
            h ^= ((uint32_t)(-comp.gr) >> 7) * 2654435761u + (uint32_t)meter_px(comp.peak) * 40503u;
    }
```

(`graph_signature` is defined after `draw_mix`, so `meter_px` is visible there; if not, move `graph_comp` and the
signature lines accordingly.)

- [ ] **Step 3: Run to see them pass**

Run: ui_test. Expected: `SCL opens COMP`, the COMP page / key / leaving checks `ok`; `test_ui_frame_cost` within its
bound; `build/ui_shots/comp/01..04` written. Convert and look at them (the suite does) before committing.

- [ ] **Step 4: Whole suite, commit**

Run: the drum suite. Expected: `ALL DRUM HOST TESTS PASSED`.

```bash
git add firmware/src/pages.c firmware/src/ui.c firmware/src/ui_input.c firmware/src/ui_draw.c firmware/src/seq.c tests/ui_test.c tests/run_drum_tests.sh
git commit -m "m3: COMP pages on SCL (SRC THRSH AMNT REL / ATK KNEE), white keys toggle DUCK, gain-reduction meter and curve

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Demos, credits, checklist, full suite, firmware, the user's review

**Files:**
- Modify: `tests/drumsim.c`, `docs/DEVICE_INSTALL.md`, `LICENSING.md`, `README.md`

- [ ] **Step 1: A/B WAVs**

`tests/drumsim.c`, before `int main` (it already has `write_demo`, `prob_bar`):

```c
/* M3: the same kit dry and pumping: T1 K909 four on the floor keys the compressor; open hats, toms and the snare's
 * reverb are ducked (4 bars each); then a REL sweep (100 ms .. 1 s over 4 bars) and the AMNT limiter end */
static void pump_kit(int src, int amt, int thr)
{
    uint32_t k;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_S909);
    drum_set_model(&trk[2], DM_HATO);
    drum_set_model(&trk[3], DM_TOM);
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
    trk[1].step[4].on = trk[1].step[12].on = 1;
    for (k = 2; k < 16u; k += 4u)
        trk[2].step[k].on = 1;
    trk[2].p[P_E1] = 110;                            /* long open hats: the pump is heard */
    trk[3].step[7].on = trk[3].step[15].on = 1;
    trk[1].p[P_REV] = 70;
    song.g[G_CSRC] = (int16_t)src;
    song.g[G_CAMT] = (int16_t)amt;
    song.g[G_CTHR] = (int16_t)thr;
    trk[1].p[P_DUCK] = trk[2].p[P_DUCK] = trk[3].p[P_DUCK] = 1;
}

static void rel_bar(uint32_t b) { song.g[G_CREL] = (int16_t)(5 + b * 12); }
```

In `main`:

```c
    pump_kit(0, 22, 26);
    write_demo(dir, "pump_dry.wav", 4, prob_bar);
    pump_kit(1, 22, 26);
    write_demo(dir, "pump_on.wav", 4, prob_bar);
    pump_kit(1, 40, 20);
    write_demo(dir, "pump_rel_sweep.wav", 4, rel_bar);
    pump_kit(1, 127, 110);
    write_demo(dir, "pump_limit.wav", 4, prob_bar);
```

Run: build and run drumsim (as in M2). Expected: the four `drumsim: build/drum_renders/pump_*.wav` lines; check
each bar has sound (per-bar peaks, as for M2's demos).

- [ ] **Step 2: Credits and checklist**

`LICENSING.md`, third-party table, after the Grids row:

```markdown
| Streams by Emilie Gillet / Mutable Instruments (<https://github.com/pichenettes/eurorack>): the COMP sidechain compressor is a C port of `streams/compressor.cc` with its tables | MIT | `firmware/src/comp.c`, `firmware/src/comp_tables.h` |
```

`README.md` credits, after the GRIDS line:

```markdown
- COMP sidechain compressor: C port of [Streams](https://github.com/pichenettes/eurorack/tree/master/streams)' compressor by Emilie Gillet, Mutable Instruments (MIT)
```

`docs/DEVICE_INSTALL.md`, a section after the M2 one:

```markdown
### M3: COMP sidechain (check on the FM-1)

- SCL: COMP 1/2 (SRC THRSH AMNT REL) and 2/2 (ATK KNEE, the curve). SRC T1 with a kick pattern; on a COMP page the
  white keys 2-8 light for the ducked tracks (key 1, the source, does not toggle). PLAY: the ducked tracks pump
  with the kick, the GR meter moves; REL longer = slower recovery; AMNT past the middle = louder (makeup); AMNT
  at the end with a high THRSH shows LIMIT.
- Mute T1 (TRACKS quick mute): the kick is silent, the others still pump (ghost key).
- SRC OFF: the mix sounds exactly as before M3.
- SAVE / power cycle / LOAD: SRC, the COMP knobs and DUCK come back; an M2 project loads with COMP off.
```

- [ ] **Step 3: Firmware and the full suite**

```bash
for i in 1 2 3; do DRUM_PACKAGE=1 ./build.sh > build/build.log 2>&1 && break; grep -q "exec format error" build/build.log || break; done; grep -E "image|FAIL" build/build.log
tests/run_tests.sh > build/host/all.txt 2>&1; echo "exit $?"; grep -E "FAIL|OVER|PASSED|compare_upstream:|within 75|irq" build/host/all.txt | tail -10
```

Expected: the image builds (about +5 KB: tables and code), `ALL HOST TESTS PASSED` (H2 0 different, H3 within
75 %, target budget ok). If the target ISR estimate (`fm1_alnk0_irq`, budget 15188) or the stack check fails: STOP,
report the numbers to the user (a budget change is the user's decision).

- [ ] **Step 4: Commit**

```bash
git add tests/drumsim.c docs/DEVICE_INSTALL.md LICENSING.md README.md
git commit -m "m3: pump A/B demo WAVs, Streams credit, the M3 device checklist

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 5: The user's review (stop here)**

Build the installer page: `rm -rf build/site && python3 web/make_site.py build/felucca-UNTESTED.fwsc "drum-$(git rev-parse --short HEAD)" build/site`.
Hand over: `build/ui_shots/comp/01..04`, `build/drum_renders/pump_dry.wav` / `pump_on.wav` / `pump_rel_sweep.wav` /
`pump_limit.wav`, the site command (`python3 -m http.server 8000 --directory build/site`, hard reload, check the
package name), and the M3 checklist. Wait for the user's verdict before merging.
