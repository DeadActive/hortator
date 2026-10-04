# M1-A Host Drum Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the Felucca fork into an 8-track drum sound core (13 integer drum models + sample model + per-track sample layer + Felucca FX chain + drum sequencer/MIDI/keys) that is fully verified in a host simulator and renders WAV files for listening — without touching the FM-1.

**Architecture:** Felucca is one C compilation unit. We replace the synth layer (`engines.c`, `eng_*.c`, `voice.c`, `drums.c`) with `drum_core.c` (tracks, voices, choke, layer, `track_render`) and a model table (`dmodels.c` + `dm_*.c`), keep `fx.c`/`slicer.c` (looped over 8 tracks) and rewrite `seq.c` for drum steps. A host harness (`tests/drum_host.h`) includes the same sources as the firmware and `tests/drum_test.c` checks behaviour; `tests/drumsim.c` writes WAVs.

**Tech Stack:** C (C99, fixed-point, JieLi clang 4 target / Apple clang host), Python 3 (generators, guards), Docker linux/amd64 (JieLi toolchain), shell.

**Spec:** `docs/superpowers/specs/2026-10-05-drum-core-m1-design.md`

**Plan split (agreed scope of this document):** This is plan **M1-A** (host). The spec's remaining M1 items are planned separately once M1-A's facts are known:
- **M1-B** — device UI pages, `felucca.c`/`main.c`/`project.c`/`audio.c` adaptation, real drum firmware build + flash-size check, `tests/run_tests.sh` rewrite.
- **M1-C** — Plaits models behind `DRUM_PLAITS=1` (approach depends on whether the JieLi clang accepts C++, found in Task 1).

## Global Constraints

- No device writes: never run `tools/fm1_install.py`, the web installer, or install `mido`/`python-rtmidi`.
- Frozen (byte-identical to upstream `1e838e1`): `firmware/hal/*`, `firmware/loader/*`, `firmware/src/ota.c`, `firmware/src/usb.c`, `firmware/crt0.S`, `firmware/app.ld`; `firmware/src/storage.c` except the one `ST_MAGIC` line; `firmware/src/main.c` except `felucca_init()` and the two boot-title lines; the last 5 lines of `firmware/src/core.h` (boot guard).
- Flash layout unchanged; package only with `DRUM_PACKAGE=1`, named `felucca-UNTESTED.fwsc`.
- Device identity family `FM-1_9xx`; version string `DRUM-0.1` (applied in M1-B).
- All DSP integer/fixed-point; no `float`/`double` in `firmware/src` (host tests may use them).
- Blocks are always `CTL` = 32 samples at `FS` = 44100.
- UI value strings are at most 5 characters: model names and parameter labels ≤ 5 chars.
- Licence header on new files: `/* SPDX-License-Identifier: GPL-3.0-only` + Felucca copyright line + `Drum machine fork: 2026 DEADACTIVE */`.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Model levels: when `model_health` reports a peak below 1500, scale that model's signal up inside its render (before `dm_put`); never relax a test threshold. Pitch tests failing means wrong increment math, not a loose tolerance.

## Review Focus

1. **Extreme knob values** (TUNE ±24, DECAY/TONE/CHAR/extras at 0 and 127) on every model — must not overflow, must still end. Pinned by `test_extremes` (Task 10).
2. **Model changed while a hit is sounding** (knob turned during playback) — old voices fade, no hang. Pinned by `test_model_change` (Task 5).
3. **Dense retriggers** (two hits in one block, 1/32 at 240 BPM, MIDI flams) on 1- and 2-voice models — voice count bounded, no stuck voices. Pinned by `test_stress` (Task 10).
4. **Sequencer edge settings** (length 1 and 64, every division, track swing 100 + global swing 100) — steps fire on time, never negative length. Pinned by `test_seq_edges` (Task 4).
5. **MIDI oddities** (two tracks on one note, velocity 1, wrong channel, an empty user sample slot as SET) — both tracks fire, quiet hit audible, no crash/silence bug. Pinned by `test_midi` (Task 4) and `test_empty_slot` (Task 3).

---

## File Structure

| File | Responsibility |
|---|---|
| `firmware/src/core.h` | (rewrite) types: params, `dvoice_t`, `step_t`, `track_t`, `dmodel_t`, `dsvf_t`; boot-guard tail kept verbatim |
| `firmware/src/dsp.c` | (edit) drop `amp_at` (synth-only) |
| `firmware/src/eng_sample.c` | (edit) keep the ADPCM decoder + user slots; drop the synth SAMPLE engine |
| `firmware/src/dm_dsp.c` | (new) drum DSP helpers: envelopes, `HZ()`, tuning, noise, `dsvf` (LP/BP/HP) |
| `firmware/src/dm_sample.c` | (new) one-shot sample playback for the SAMPLE model and the layer |
| `firmware/src/dm_kick.c` | (new) shared pitched-body render; 808/909 kick, tom, conga, claves |
| `firmware/src/dm_snare.c` | (new) 808/909 snare, 808/909 clap |
| `firmware/src/dm_metal.c` | (new) shared 6-square metal source; hat C/O, cymbal, cowbell |
| `firmware/src/dm_perc.c` | (new) rimshot |
| `firmware/src/dmodels.c` | (new) includes the `dm_*.c`, model enum, `DMODELS[]`, `N_MODEL[]` |
| `firmware/src/params.c` | (rewrite head) drum `TP[]`/`GP[]`, `track_desc`; page table removed (M1-B adds `pages.c`) |
| `firmware/src/drum_core.c` | (new) kit init, hit, voices, choke, layer, declick tail, `track_render` |
| `firmware/src/fx.c`, `slicer.c` | (edit) 8 tracks, no separate drum bus |
| `firmware/src/seq.c` | (rewrite) drum steps, keys, MIDI, live record |
| `tools/build.py` | (edit) `--gen-only`, `DRUM_PACKAGE` guard |
| `tools/gen_samples.py` | (edit) only the GM kit; no synth preset table |
| `tools/check_untouched.py` | (new) frozen-code guard |
| `tests/drum_host.h` | (new) host build of the DSP/seq/mix + reset helpers |
| `tests/drum_test.c` | (new) all M1-A checks |
| `tests/drumsim.c` | (new) WAV renders |
| `tests/run_drum_tests.sh` | (new) host suite entry point |
| deleted | `engines.c`, `eng_analog.c`, `eng_digital.c`, `eng_phase.c`, `eng_lofi.c`, `eng_formant.c`, `eng_trio.c`, `eng_drawbar.c`, `eng_grain.c`, `eng_slice.c`, `voice.c`, `drums.c`; tests `hostsim.c`, `regress.c`, `scale_test.c`, `slicer_test.c`, `slice_test.c`, `project_test.c`, `golden.txt`, `pitch.py`, `slice_loop.py` |

Include order (host harness now, `felucca.c` in M1-B): `core.h` → `dsp.c` → `eng_sample.c` → `dmodels.c` → `params.c` → `drum_core.c` → `slicer.c` → `fx.c` → `usb.c` → `midi_uart.c` → `seq.c`.

---

### Task 1: Toolchain and unmodified-Felucca baseline

**Files:**
- Modify: `.gitignore` (add `.venv/`)
- Create: `docs/m1-baseline.md`

**Interfaces:**
- Consumes: nothing.
- Produces: working `PYTHON=.venv/bin/python ./build.sh`; `docs/m1-baseline.md` with build output, test result, image size, pool usage, and whether `pi32v2/bin/clang -x c++` works (input to M1-C).

- [ ] **Step 1: Python venv with Pillow**

```bash
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca
python3 -m venv .venv && .venv/bin/pip install --quiet Pillow
grep -qx '.venv/' .gitignore || echo '.venv/' >> .gitignore
.venv/bin/python -c 'import PIL; print(PIL.__version__)'
```
Expected: a version number.

- [ ] **Step 2: JieLi toolchain**

```bash
tools/get_toolchain.sh
ls ~/.jieli/toolchain/pi32v2/bin/clang
```
Expected: the path prints. If the download fails, STOP and report the error (spec §7).

- [ ] **Step 3: JieLi AC79 SDK (only 3 files are used)**

```bash
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
ls ~/fw-AC79_AIoT_SDK/cpu/wl82/tools/uboot.boot
```
Expected: the path prints. On failure STOP and report.

- [ ] **Step 4: Docker linux/amd64 works**

```bash
docker info >/dev/null && docker run --rm --platform linux/amd64 debian:bookworm-slim uname -m
```
Expected: `x86_64`.

- [ ] **Step 5: Build unmodified Felucca**

```bash
PYTHON=.venv/bin/python ./build.sh 2>&1 | tee build/baseline-build.log
```
Expected: ends with `package  .../build/felucca.fwsc ... identity FM-1_900`, all `ok` lines, no `FAIL`.

- [ ] **Step 6: Run the unmodified host test suite**

```bash
PATH="$PWD/.venv/bin:$PATH" sh tests/run_tests.sh 2>&1 | tee build/baseline-tests.log
```
Expected: `ALL HOST TESTS PASSED`. If not, STOP and report (spec §2.6.5).

- [ ] **Step 7: Probe C++ support of the JieLi clang (for M1-C)**

```bash
printf 'struct A{int f(){return 1;}}; int g(){A a; return a.f();}\n' > build/cxx_probe.cc
docker run --rm --platform linux/amd64 -v "$PWD:/work" -v "$HOME/.jieli/toolchain:/opt/jieli:ro" -w /work debian:bookworm-slim \
  /opt/jieli/pi32v2/bin/clang -target pi32v2 -x c++ -c build/cxx_probe.cc -o build/cxx_probe.o && echo CXX_OK || echo CXX_FAIL
```
Record the result.

- [ ] **Step 8: Write `docs/m1-baseline.md`**

Content: date; toolchain dir name (`ls -l ~/.jieli/toolchain`); SDK tag; the `ok` lines and `app/loader/package` lines from `build/baseline-build.log` (image bytes, RAM, pool); the final line of `build/baseline-tests.log`; the C++ probe result; the stock cost reference `cpu/mix/3parts_full_drums 1566` from `tests/cpu_baseline.txt` (the heaviest stock mix, known to run on hardware).

- [ ] **Step 9: Commit**

```bash
git add .gitignore docs/m1-baseline.md
git commit -m "docs: M1 baseline (toolchain, stock build and tests)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Safety guards (frozen code, packaging)

**Files:**
- Create: `tools/check_untouched.py`, `tests/guard_test.sh`
- Modify: `tools/build.py` (main: `--gen-only`, packaging guard)

**Interfaces:**
- Produces: `python3 tools/check_untouched.py [--tree DIR]` (exit 0 = clean, 1 = violation, prints each violation); `python3 tools/build.py --gen-only` (writes `build/gen/*.h`, needs no toolchain/SDK).

- [ ] **Step 1: Write the failing guard test**

`tests/guard_test.sh`:
```sh
#!/bin/sh
# check_untouched.py: passes on the tree, fails when frozen code changes (on a copy)
set -e
cd "$(dirname "$0")/.."
python3 tools/check_untouched.py >/dev/null || { echo "guard: clean tree reported dirty"; exit 1; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
cp -R firmware "$TMP/firmware"
echo '/* x */' >> "$TMP/firmware/src/ota.c"
if python3 tools/check_untouched.py --tree "$TMP" >/dev/null; then echo "guard: ota.c change not caught"; exit 1; fi
cp firmware/src/ota.c "$TMP/firmware/src/ota.c"
sed -i.bak 's/fm1_enter_uboot();/fm1_reboot();/' "$TMP/firmware/src/main.c"
if python3 tools/check_untouched.py --tree "$TMP" >/dev/null; then echo "guard: main.c recovery change not caught"; exit 1; fi
cp firmware/src/main.c "$TMP/firmware/src/main.c"
sed -i.bak 's/^#define ST_MAGIC 0x554C4546u/#define ST_MAGIC 0x4D524446u/' "$TMP/firmware/src/storage.c"
python3 tools/check_untouched.py --tree "$TMP" >/dev/null || { echo "guard: allowed ST_MAGIC change rejected"; exit 1; }
echo "guard: ok"
```

- [ ] **Step 2: Run it to verify it fails**

Run: `sh tests/guard_test.sh`
Expected: FAIL — `python3: can't open file '.../tools/check_untouched.py'`.

- [ ] **Step 3: Implement `tools/check_untouched.py`**

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Drum machine fork: 2026 DEADACTIVE
"""Fail when code the USB update / recovery path depends on differs from upstream Felucca.

  tools/check_untouched.py [--tree DIR]     DIR: a copy of the repo root (default: the repo)

Frozen: hal/, loader/, ota.c, usb.c, crt0.S, app.ld byte for byte; storage.c except the ST_MAGIC
line; main.c except felucca_init() and the two boot-title lines; the boot-guard tail of core.h."""
import argparse
import re
import subprocess
import sys
from pathlib import Path

BASE = "1e838e1"
ROOT = Path(__file__).resolve().parents[1]
FROZEN_DIRS = ["firmware/hal", "firmware/loader"]
FROZEN_FILES = ["firmware/src/ota.c", "firmware/src/usb.c", "firmware/crt0.S", "firmware/app.ld"]
MAGIC = re.compile(r"^#define ST_MAGIC 0x[0-9A-Fa-f]{8}u\b")
TITLE = re.compile(r"draw_text_box\(0, 1[03]0, 240, &FONT_[LS], \"")


def upstream(path):
    r = subprocess.run(["git", "-C", str(ROOT), "show", f"{BASE}:{path}"], capture_output=True)
    return r.stdout.decode() if r.returncode == 0 else None


def upstream_tree(d):
    r = subprocess.run(["git", "-C", str(ROOT), "ls-tree", "-r", "--name-only", BASE, d],
                       capture_output=True, text=True, check=True)
    return r.stdout.split()


def strip_main(text):
    """main.c without felucca_init() (and its comment line) and the boot-title lines"""
    out, skip = [], False
    lines = text.splitlines()
    for i, ln in enumerate(lines):
        if ln.startswith("static void felucca_init(void)"):
            skip = True
            if out and out[-1].startswith("/* power-on"):
                out.pop()
        if skip:
            if ln == "}":
                skip = False
            continue
        if TITLE.search(ln):
            continue
        out.append(ln)
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", type=Path, default=ROOT)
    tree = ap.parse_args().tree
    bad = []
    files = FROZEN_FILES + [f for d in FROZEN_DIRS for f in upstream_tree(d)]
    for f in files:
        p = tree / f
        if not p.exists() or p.read_bytes().decode(errors="replace") != upstream(f):
            bad.append(f"{f}: differs from {BASE}")
    up, cur = upstream("firmware/src/storage.c").splitlines(), (tree / "firmware/src/storage.c").read_text().splitlines()
    if len(up) != len(cur) or any(a != b and not (MAGIC.match(a) and MAGIC.match(b)) for a, b in zip(up, cur)):
        bad.append("firmware/src/storage.c: differs beyond the ST_MAGIC line")
    if strip_main(upstream("firmware/src/main.c")) != strip_main((tree / "firmware/src/main.c").read_text()):
        bad.append("firmware/src/main.c: differs outside felucca_init() and the boot titles")
    if upstream("firmware/src/core.h").splitlines()[-5:] != (tree / "firmware/src/core.h").read_text().splitlines()[-5:]:
        bad.append("firmware/src/core.h: the boot-guard tail (last 5 lines) changed")
    for b in bad:
        print("FROZEN", b)
    print("check_untouched: " + ("FAILED" if bad else "ok"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the guard test**

Run: `sh tests/guard_test.sh`
Expected: `guard: ok`.

- [ ] **Step 5: `--gen-only` and the packaging guard in `tools/build.py`**

In `main()`, after `a = ap.parse_args()` add the option and early exit; replace the package write. Exact edits:

```python
    ap.add_argument("--gen-only", action="store_true", help="only the generated headers (host tests)")
    a = ap.parse_args()
    if a.gen_only:
        generate()
        return 0
```
(the `ap.add_argument("--gen-only", ...)` line goes before the existing `a = ap.parse_args()`; the `if a.gen_only` block right after it.)

Replace:
```python
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
```
with:
```python
    if os.environ.get("DRUM_PACKAGE") != "1":     # drum fork: no installable file unless asked
        print(f"app      {OUT / 'felucca.bin'}  {len(img)} B ({APP_SLOT - len(img)} B free)")
        print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
        print("package  skipped (DRUM_PACKAGE=1 writes build/felucca-UNTESTED.fwsc; never install it in M1)")
        return 0
    name = name.replace(".fwsc", "-UNTESTED.fwsc")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
```

- [ ] **Step 6: Verify `--gen-only` and the guard**

```bash
.venv/bin/python tools/build.py --gen-only && ls build/gen/felucca_tables.h build/gen/felucca_samples.h
PYTHON=.venv/bin/python ./build.sh 2>&1 | tail -3
```
Expected: both headers listed; build ends with `package  skipped (...)` and prints the free bytes.

- [ ] **Step 7: Commit**

```bash
git add tools/check_untouched.py tests/guard_test.sh tools/build.py
git commit -m "build: frozen-code guard, --gen-only, packaging only with DRUM_PACKAGE=1

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Drum core skeleton with the SAMPLE model (host)

**Files:**
- Rewrite: `firmware/src/core.h` (all but the last 5 lines)
- Modify: `firmware/src/dsp.c` (remove `amp_at` block), `firmware/src/eng_sample.c` (remove synth engine), `firmware/src/params.c` (head + page section), `firmware/src/fx.c`, `firmware/src/slicer.c`, `tools/gen_samples.py`
- Create: `firmware/src/dm_dsp.c`, `firmware/src/dm_sample.c`, `firmware/src/dmodels.c`, `firmware/src/drum_core.c`, `firmware/src/seq.c` (minimal stub replaced in Task 4), `tests/drum_host.h`, `tests/drum_test.c`, `tests/run_drum_tests.sh`
- Delete: the files listed under "deleted" in File Structure

**Interfaces:**
- Produces (used by every later task):
  - `static void drum_hit(track_t *t, uint32_t vel)`; `static void drum_cut(track_t *t)`; `static void drum_set_model(track_t *t, uint32_t mi)`; `static void drum_tracks_init(void)`; `static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)`; `static void drum_block_begin(void)`.
  - Model contract: `trigger(track_t *t, dvoice_t *v)` (voice already zeroed, `active=1`, `vel`, `age`, `rng` set) and `render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)` (adds to `out`, sets `v->last` to the last added sample, sets `active=0`/`last=0` when done).
  - `dm_dsp.c`: `ENV1`, `ENV_END`, `HZ(f)`, `CUT_ST`, `dk(idx)`, `env_q15(e)`, `vel_gain(v)`, `dnoise(v)`, `inc_tune(inc, semis)`, `dsvf_coef(c, cut, reso)`, `dsvf_tick(c, in, ic1, ic2, &bp, &hp)` → lp.
  - `dm_sample.c`: `smp_find(set, key)` → zone or `0xFFFF`; `smp_start(v, zi, key, tune)`; `smp_render(v, out, n, gain, lp, drv)` → 0 when ended.
  - Host: `host_init()`, `host_reset_fx()`, `render_track(t, dst, frames)`, `render_mix(dst_l, dst_r, frames)`, `hit_age(t)`.

- [ ] **Step 1: Write the failing tests**

`tests/run_drum_tests.sh`:
```sh
#!/bin/sh
# Host suite of the drum fork (no hardware, no toolchain): generated headers, drum_test, guards.
set -e
cd "$(dirname "$0")/.."
PY="${PYTHON:-python3}"
"$PY" tools/build.py --gen-only >/dev/null
OUT=build/host
mkdir -p "$OUT"
cc -O2 -Wall -Wno-unused-function -Ibuild/gen -Ifirmware/src -o "$OUT/drum_test" tests/drum_test.c -lm
"$OUT/drum_test"
sh tests/guard_test.sh
python3 tools/check_untouched.py
echo "ALL DRUM HOST TESTS PASSED"
```

`tests/drum_host.h`:
```c
/* Host build of the drum fork's DSP, models, mix and sequencer (no hardware). Same sources as the
 * firmware; reset helpers for independent tests. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#include "../firmware/src/eng_sample.c"
#include "../firmware/src/dmodels.c"
#include "../firmware/src/params.c"
#include "../firmware/src/drum_core.c"
#include "../firmware/src/slicer.c"
#include "../firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"
#include "../firmware/src/seq.c"

static void host_reset_fx(void)                     /* FX buses, master, slicer, metal: as at power-on */
{
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(sl, 0, sizeof sl);
    memset(sl_buf, 0, sizeof sl_buf);
    lim_env = LIM_T;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
    memset(lce, 0, sizeof lce);
    dvage = 0;
    mi_r = mi_w = 0;
    kb_prev = 0;
    fm1_in.notes = 0;
    transport_req = panic_req = 0;
}

static void host_init(void)
{
    host_reset_fx();
    memset(&song, 0, sizeof song);
    drum_tracks_init();
}

static int32_t blk[CTL];
static void render_track(track_t *t, int32_t *dst, uint32_t frames)   /* one track, no FX */
{
    uint32_t f, i;
    for (f = 0; f < frames; f += CTL) {
        drum_block_begin();
        track_render(t, blk, CTL);
        for (i = 0; i < CTL && f + i < frames; i++)
            if (dst)
                dst[f + i] = blk[i];
    }
}

static int32_t mixo[2 * CTL];
static void render_mix(int32_t *l, int32_t *r, uint32_t frames)      /* the whole mix (events, FX, master) */
{
    uint32_t f, i;
    for (f = 0; f < frames; f += CTL) {
        mix_block(mixo, CTL);
        for (i = 0; i < CTL && f + i < frames; i++) {
            if (l)
                l[f + i] = mixo[2 * i];
            if (r)
                r[f + i] = mixo[2 * i + 1];
        }
    }
}

static uint32_t hit_age(const track_t *t)          /* changes on every hit of t (voice ages) */
{
    uint32_t a = 0, i;
    for (i = 0; i < NDV; i++)
        if (t->v[i].age > a)
            a = t->v[i].age;
    return a;
}

static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}
```

`tests/drum_test.c` (this task's tests; later tasks append functions and register them in `main`):
```c
/* Drum fork host checks. Each test prints one line; any failure exits 1. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int32_t wl[SECS(8)], wr[SECS(8)];

static int32_t peak_of(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t p = 0;
    for (; a < b; a++)
        if (abs(x[a]) > p)
            p = abs(x[a]);
    return p;
}

/* first sample index from which the track is inactive and silent to the end of the render */
static uint32_t end_of(const int32_t *x, uint32_t frames)
{
    uint32_t e = frames;
    while (e > 0 && x[e - 1] == 0)
        e--;
    return e;
}

static int track_idle(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NDV; i++)
        if (t->v[i].active || t->lv[i].active)
            return 0;
    return t->dtail == 0;
}

/* a default hit of model mi at velocity vel on track 0: rendered into wl, returns the end sample */
static uint32_t hit_model(uint32_t mi, uint32_t vel, uint32_t frames)
{
    host_init();
    drum_set_model(&trk[0], mi);
    drum_hit(&trk[0], vel);
    render_track(&trk[0], wl, frames);
    return end_of(wl, frames);
}

static void test_tables(void)
{
    uint32_t i, k, ok = 1;
    for (i = 0; i < NMODELS; i++) {
        const dmodel_t *m = &DMODELS[i];
        ok &= strcmp(N_MODEL[i], m->name) == 0 && strlen(m->name) <= 5;
        ok &= m->voices >= 1 && m->voices <= NDV && m->choke <= 4 && m->trigger && m->render;
        for (k = 0; k < 8; k++)
            ok &= m->edit[k].def >= m->edit[k].min && m->edit[k].def <= m->edit[k].max && strlen(m->edit[k].label) <= 5;
    }
    check("model table: names, voices, choke, defaults in range, labels <= 5 chars", ok);
}

static void test_idle_silence(void)
{
    host_init();
    render_mix(wl, wr, SECS(1));
    check("idle mix (no hits) is digital silence", peak_of(wl, 0, SECS(1)) == 0 && peak_of(wr, 0, SECS(1)) == 0);
}

static void test_sample_hit(void)
{
    uint32_t e = hit_model(DM_SMPL, 127, SECS(6));
    check("SAMPLE: GM kick (key 36) is audible", peak_of(wl, 0, e) > 1500);
    check("SAMPLE: ends within 6 s and stays at 0", e < SECS(6) - SECS(0.5) && track_idle(&trk[0]));
}

static void test_declick_cut(void)
{
    int32_t a[CTL], b[CTL], last;
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], 0, SECS(0.02));
    drum_block_begin();
    track_render(&trk[0], a, CTL);
    last = a[CTL - 1];
    drum_cut(&trk[0]);
    drum_block_begin();
    track_render(&trk[0], b, CTL);
    check("cut: the next sample continues from the last one (declick tail)", abs(b[0] - last) <= 1);
    render_track(&trk[0], wl, SECS(0.02));
    check("cut: the tail is gone within 20 ms", end_of(wl, SECS(0.02)) < SECS(0.02) && track_idle(&trk[0]));
}

static void test_two_voices(void)
{
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    trk[0].p[P_E5] = 49;                             /* a long crash */
    drum_hit(&trk[0], 127);
    render_track(&trk[0], 0, SECS(0.1));
    drum_hit(&trk[0], 127);
    check("2-voice model: the first hit's tail survives a retrigger", trk[0].v[0].active && trk[0].v[1].active);
}

static void test_choke(void)
{
    int32_t last;
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    drum_set_model(&trk[1], DM_SMPL);
    trk[0].p[P_E5] = 46;
    trk[0].p[P_CHOKE] = trk[1].p[P_CHOKE] = 1;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.05));
    last = wl[SECS(0.05) - 1];
    drum_hit(&trk[1], 127);                          /* same group: cuts track 0 */
    render_track(&trk[0], wl, SECS(0.01));
    check("choke: the other track's voices stop", !trk[0].v[0].active && !trk[0].v[1].active);
    check("choke: silent within 100 samples (<= 1 % of its level), 0 after 300",
          abs(wl[100]) <= abs(last) / 100 + 1 && peak_of(wl, 300, SECS(0.01)) == 0);
}

static void test_mute(void)
{
    host_init();
    trk[0].p[P_MUTE] = 1;
    drum_hit(&trk[0], 127);
    check("mute: a muted track ignores hits", !trk[0].v[0].active && !trk[0].v[1].active);
}

static void test_empty_slot(void)
{
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    trk[0].p[P_E4] = SMP_NSETS;                      /* USR1, nothing uploaded */
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.1));
    check("SAMPLE: an empty user slot is silent, no voice left running",
          peak_of(wl, 0, SECS(0.1)) == 0 && track_idle(&trk[0]));
}

static void test_mix_health(void)
{
    uint32_t i;
    host_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        t->p[P_DIST] = 60;
        t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 60;
        t->p[P_SLCR] = 1;                            /* GATE */
        drum_hit(t, 127);
    }
    render_mix(wl, wr, SECS(8));
    check("mix: 8 tracks + FX stay within full scale", peak_of(wl, 0, SECS(8)) <= 32767 && peak_of(wr, 0, SECS(8)) <= 32767);
    check("mix: back to (near) silence after the FX tails (last 1 s <= 2)",
          peak_of(wl, SECS(7), SECS(8)) <= 2 && peak_of(wr, SECS(7), SECS(8)) <= 2);
}

int main(void)
{
    test_tables();
    test_idle_silence();
    test_sample_hit();
    test_declick_cut();
    test_two_voices();
    test_choke();
    test_mute();
    test_empty_slot();
    test_mix_health();
    printf(fails ? "drum_test: %d FAILED\n" : "drum_test: all passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — compile errors (`dmodels.c` not found / unknown `dvoice_t`).

- [ ] **Step 3: Delete the synth layer and obsolete tests**

```bash
git rm -q firmware/src/engines.c firmware/src/eng_analog.c firmware/src/eng_digital.c firmware/src/eng_phase.c \
  firmware/src/eng_lofi.c firmware/src/eng_formant.c firmware/src/eng_trio.c firmware/src/eng_drawbar.c \
  firmware/src/eng_grain.c firmware/src/eng_slice.c firmware/src/voice.c firmware/src/drums.c \
  tests/hostsim.c tests/regress.c tests/scale_test.c tests/slicer_test.c tests/slice_test.c tests/project_test.c \
  tests/golden.txt tests/pitch.py tests/slice_loop.py
```
(`tests/run_tests.sh` and the firmware build are restored in M1-B; until then the host suite is `tests/run_drum_tests.sh`.)

- [ ] **Step 4: Rewrite `firmware/src/core.h`**

Replace everything above the last 5 lines (from `#define RING_PUBLISH()` to the end, which stay byte-identical) with:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum machine core types: 8 drum tracks; each plays one drum model (1 or 2 voices) and an
 * optional sample layer, through Felucca's per-track DIST / SLICER / sends (fx.c). */
#include <stdint.h>
#define NTRK 8                   /* drum tracks */
#define NPART NTRK               /* fx.c / slicer.c: every track is mixed the same way */
#define NDV 2                    /* voices per track; a model uses 1 or 2 */
#define NSTEP 64
#define HALF_FRAMES 256          /* I2S half buffer: 5.8 ms at 44.1 kHz */
#define UP_SLOTS 32u             /* user preset slots (storage layout; presets are off in M1) */

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;
#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

enum {                          /* per-track parameters */
    P_MODEL,
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,   /* the model's: TUNE DECAY TONE CHAR + 4 extras */
    P_LEVEL, P_PAN, P_MUTE, P_CHOKE, P_NOTE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,
    P_SLEN, P_SDIV, P_SSWING,
    P_LSET, P_LKEY, P_LLEVEL, P_LTUNE, P_LDEC,       /* sample layer */
    P_COUNT
};

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_CLRSEQ, G_INITSND,
    G_DRCH,                      /* MIDI channel of the drum tracks, 1..16 */
    G_COUNT
};

/* ----------------------------------------------------------- voices --- */
typedef struct {                 /* sample playback state (eng_sample.c sample_next, dm_sample.c) */
    uint32_t ph[3];              /* ph[0] position, ph[1] fraction Q16 */
    int32_t s[8];                /* s[0] predictor, s[1] step index, s[2..3] interpolation, s[4] zone,
                                  * s[5] step Q16, s[6] tone state */
} voice_t;

typedef struct { int32_t a1, a2, a3, k; } dsvf_t;   /* dm_dsp.c: SVF with LP / BP / HP */

typedef struct {                 /* one drum voice; the fields' meaning is the model's */
    uint8_t active, vel;
    uint32_t age;                /* hit order (voice stealing) */
    uint32_t t;                  /* samples since the hit (models that need it) */
    uint32_t ph[3], inc[3];      /* oscillator phases and increments */
    int32_t env[3];              /* envelopes, Q24 (1 << 24 = full) */
    uint32_t k[3];               /* their per-sample factors, Q16 (DECAY_K) */
    dsvf_t c[2];                 /* filters */
    int32_t f[4];                /* filter states (2 per filter) */
    int32_t x[4];                /* model scratch: gains, amounts */
    int32_t rng;                 /* noise state, seeded per hit */
    int32_t last;                /* last sample added to the output (declick when cut) */
    voice_t sv;                  /* sample playback (SAMPLE model, layer) */
} dvoice_t;

struct track;
typedef struct {
    const char *name;            /* <= 5 chars (UI value) */
    uint8_t voices;              /* 1 or 2 */
    uint8_t choke;               /* default choke group, 0 = none */
    param_desc_t edit[8];        /* P_E0..P_E7: TUNE DECAY TONE CHAR + 4 extras */
    void (*trigger)(struct track *t, dvoice_t *v);
    void (*render)(struct track *t, dvoice_t *v, int32_t *out, uint32_t n);
} dmodel_t;

/* ------------------------------------------------------------ track --- */
typedef struct {                 /* one sequencer step */
    uint8_t on, acc;             /* hit; accent (velocity 127, else 96) */
} step_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t model;               /* model the voices were started with (a change cuts them) */
    dvoice_t v[NDV];             /* model voices */
    dvoice_t lv[NDV];            /* sample-layer voices */
    int32_t dtail;               /* declick: last output of cut voices, decaying */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* samples into the current step */
    uint16_t seq_idx;
    uint8_t rskip, rskip_idx;    /* live recording put a hit into the step about to play: skip it once */
    /* mix runtime (fx.c) */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */
```
followed by the unchanged 5 lines:
```c
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
```

- [ ] **Step 5: `dsp.c` — remove the synth-only amplitude ramp**

Delete from the line `/* amplitude ramp over the block. Blocks are always CTL long, so x / CTL is a` to the end of the file (`CTL_LOG2`, its `#error` check and `amp_at`).

- [ ] **Step 6: `eng_sample.c` — keep only the decoder and user slots**

Delete `static const char *const N_ONOFF_E[] = {"OFF", "ON"};` (line 4) and everything from `static void sample_note_on(track_t *t, voice_t *v)` to the end of the file (`sample_note_on`, `sample_render`, `ENG_SAMPLE`). Keep `smp_zone_t`, `smp_set_t`, the generated include, `IMA_STEP`/`IMA_IDX`, `pow2_q16`, the user-slot code and `sample_next`.

- [ ] **Step 7: `tools/gen_samples.py` — only the GM kit, no synth preset table**

In `main()` delete:
```python
    if have_cc0:
        for name, kind in CC0_SETS:
            if kind != "kit":                       # the CC0 KIT feeds the GM kit
                b.cc0_set(name, kind)
```
In `header()` delete the block from `L.append("static const preset_t SMP_PRESET_TABLE[] = {")` through its closing `L.append("};")` (the 7 lines with `ENV[k]` and `loop`), keeping `named = sets or [("NONE", 0, 0)]` (move that line above `names = ...` if it was inside the deleted block).

Run: `.venv/bin/python tools/build.py --gen-only && grep -c PRESET build/gen/felucca_samples.h; grep SMP_SET_NAMES_INIT build/gen/felucca_samples.h`
Expected: `0`, and `#define SMP_SET_NAMES_INIT "PERC"`.

- [ ] **Step 8: `firmware/src/dm_dsp.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum model building blocks, all fixed point. Envelopes are Q24 (ENV1 = full) and fall by a
 * per-sample factor from DECAY_K (Q16; index 0..127 = 5 ms .. 4 s to -60 dB, exponential). */
#define ENV1 (1 << 24)
#define ENV_END (1 << 10)                    /* -84 dB: the voice ends */
#define HZ(f) ((uint32_t)((f) * 97391.548))  /* phase increment of f Hz (2^32 / 44100); constants only */
#define CUT_ST 299                           /* dsvf cutoff units (0..127 << 8 = 30 Hz..16 kHz) per semitone */
/* cutoffs of the frequencies the models use: 127 * ln(f / 30) / ln(16000 / 30) << 8 */
#define CUT_600 15511
#define CUT_880 17494
#define CUT_1000 18156
#define CUT_1200 19100
#define CUT_2000 21745
#define CUT_3440 24553
#define CUT_4000 25334
#define CUT_6000 27433
#define CUT_7000 28232
#define CUT_7100 28305

static inline uint32_t dk(int32_t idx) { return DECAY_K[clamp(idx, 0, 127)]; }
static inline int32_t env_q15(int32_t e) { return e >> 9; }
static inline int32_t vel_gain(const dvoice_t *v) { return v->vel * 258; }       /* 127 -> 32766 */
static inline int32_t dnoise(dvoice_t *v) { return (int32_t)noise32(&v->rng) >> 16; }
static inline void env_step(dvoice_t *v, uint32_t i) { v->env[i] = mulq16(v->env[i], v->k[i]); }

/* inc * 2^(semis / 12); at trigger time only (a divide in pow2_q16, a 64-bit product) */
static uint32_t inc_tune(uint32_t inc, int32_t semis)
{
    return (uint32_t)(((uint64_t)inc * pow2_q16(clamp(semis, -48, 48) * 16)) >> 16);
}

/* Simper SVF as tsvf (dsp.c), with the band-pass and high-pass outputs */
static void dsvf_coef(dsvf_t *c, int32_t cut, int32_t reso)       /* cut 0..127 << 8, reso 0..127 */
{
    tsvf_t t;
    tsvf_coef(&t, cut, reso);
    c->a1 = t.a1;
    c->a2 = t.a2;
    c->a3 = t.a3;
    c->k = 8192 - reso * 7600 / 127;          /* damping, Q12, as tsvf_coef */
}

static inline int32_t dsvf_tick(const dsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2, int32_t *bp, int32_t *hp)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    *bp = v1;
    *hp = in - ((c->k * v1) >> 12) - v2;
    return v2;
}

/* add one model sample to the output: velocity, voice level; remembers it for the declick */
static inline void dm_put(dvoice_t *v, int32_t *out, uint32_t i, int32_t x)
{
    x = mulq15(mulq15(x, vel_gain(v)), VOICE_FS);
    out[i] += x;
    v->last = x;
}

static inline void dm_end(dvoice_t *v)
{
    v->active = 0;
    v->last = 0;
}
```

- [ ] **Step 9: `firmware/src/dm_sample.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* One-shot IMA ADPCM playback (eng_sample.c decoder) for the SAMPLE model and the sample layer. */

/* zone index of key in set (built-in sets, then USR1..3), 0xFFFF = none */
static uint32_t smp_find(uint32_t set, uint32_t key)
{
    uint32_t i, zi = 0xFFFFu;
    set %= SMP_NALL;
    if (set < SMP_NSETS) {
        const smp_set_t *s = &SMP_SETS[set];
        for (i = 0; i < s->nz; i++)
            if (key >= SMP_ZONES[s->z0 + i].lo && key <= SMP_ZONES[s->z0 + i].hi)
                zi = s->z0 + i;
    } else {
        uint32_t k = set - SMP_NSETS;
        for (i = 0; i < usr_nz[k]; i++)
            if (key >= usr_zone[k][i].lo && key <= usr_zone[k][i].hi)
                zi = 0x8000u | k << 5 | i;
    }
    return zi;
}

/* from the start of zone zi; key and tune (semitones) set the speed */
static void smp_start(dvoice_t *v, uint32_t zi, uint32_t key, int32_t tune)
{
    const smp_zone_t *z = smp_zone(zi);
    voice_t *s = &v->sv;
    int32_t d16 = clamp((int32_t)key * 16 + tune * 16 - z->root16, -1536, 576);   /* <= 3 octaves up */
    s->ph[0] = s->ph[1] = 0;
    s->s[0] = s->s[1] = s->s[2] = s->s[6] = 0;
    s->s[4] = (int32_t)zi;
    s->s[5] = (int32_t)((pow2_q16(d16) >> 8) * (z->rate >> 8));   /* Q16 source samples per output */
    s->s[3] = z->n ? sample_next(z, s, 0) : 0;
}

/* one block: amplitude env[0] (factor k[0]), gain Q15, one-pole tone lp (Q15 coefficient), drive 0..127.
 * Returns 0 when the sample or its envelope has ended (the voice is then off). */
static int smp_render(dvoice_t *v, int32_t *out, uint32_t n, int32_t gain, int32_t lp, int32_t drv)
{
    voice_t *s = &v->sv;
    const smp_zone_t *z = smp_zone((uint32_t)s->s[4]);
    uint32_t i, frac = s->ph[1], stepq = (uint32_t)s->s[5];
    for (i = 0; i < n; i++) {
        int32_t x;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            s->s[2] = s->s[3];
            if (s->ph[0] >= z->n) {
                dm_end(v);
                return 0;
            }
            s->s[3] = sample_next(z, s, 0);
        }
        x = s->s[2] + (((s->s[3] - s->s[2]) * (int32_t)(frac >> 1)) >> 15);
        s->s[6] += mulq15(x - s->s[6], lp);
        x = s->s[6];
        if (drv)
            x = softclip(x + (((x >> 2) * (drv * 150)) >> 11));
        x = mulq15(mulq15(x, env_q15(v->env[0])), gain);
        x = mulq15(x, VOICE_FS);
        out[i] += x;
        v->last = x;
        env_step(v, 0);
    }
    s->ph[1] = frac;
    if (v->env[0] < ENV_END) {
        dm_end(v);
        return 0;
    }
    return 1;
}

/* SAMPLE model: TUNE DECAY TONE DRIVE, SET KEY */
static void smpl_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    uint32_t zi = smp_find((uint32_t)p[4], (uint32_t)p[5]);
    if (zi == 0xFFFFu || !smp_zone(zi)->n) {
        dm_end(v);
        return;
    }
    smp_start(v, zi, (uint32_t)p[5], p[0]);
    v->env[0] = ENV1;
    v->k[0] = dk(p[1]);
    v->x[0] = 4000 + p[2] * 28767 / 127;        /* tone lp coefficient */
}

static void smpl_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    smp_render(v, out, n, vel_gain(v), v->x[0], t->p[P_E3]);
}

#define DM_SMPL_DEF {"SMPL", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 127), \
    PD("TONE", F_INT, 0, 127, 127), PD("DRIVE", F_PCT, 0, 127, 0), PE("SET", SMP_ALL_NAMES, 0), \
    PD("KEY", F_INT, 0, 127, 36), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, smpl_trigger, smpl_render}
```

- [ ] **Step 10: `firmware/src/dmodels.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The drum model table. New models go above DM_SMPL in the enum, DMODELS and N_MODEL (same order). */
#include "dm_dsp.c"
#include "dm_sample.c"

enum { DM_SMPL, NMODELS };
static const dmodel_t DMODELS[NMODELS] = {DM_SMPL_DEF};
static const char *const N_MODEL[NMODELS] = {"SMPL"};
```

- [ ] **Step 11: `firmware/src/params.c` — drum parameter tables**

Replace lines 1–122 (from the SPDX header through the end of `track_desc`) with:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Parameter descriptions and value formatting. P_E0..P_E7 are described by the track's model. */
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_CLOCK[] = {"INT"};
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_CHOKE[] = {"OFF", "1", "2", "3", "4"};

static const param_desc_t TP[P_COUNT] = {
    [P_MODEL] = PE("MODEL", N_MODEL, 0),
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_CHOKE] = PE("CHOKE", N_CHOKE, 0),
    [P_NOTE] = PD("NOTE", F_INT, 0, 127, 36),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_LSET] = PE("LSET", SMP_ALL_NAMES, 0),
    [P_LKEY] = PD("LKEY", F_INT, 0, 127, 36),
    [P_LLEVEL] = PD("LLVL", F_PCT, 0, 127, 0),
    [P_LTUNE] = PD("LTUNE", F_SEMI, -24, 24, 0),
    [P_LDEC] = PD("LDEC", F_INT, 0, 127, 127),
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_DTIME] = PE("TIME", N_DIV, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("MIDI", N_DASH, 0),
    [G_SYNC] = PE("SYNC", N_DASH, 0),
    [G_ROUTE] = PE("ROUT", N_DASH, 0),
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_DRCH] = PD("CH", F_INT, 1, 16, 10),
};

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7)
        return &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS].edit[id - P_E0];
    return &TP[id];
}
```
Keep `param_format` unchanged. Delete everything from the line `/* ------------------------------------------------------------ pages --- */` to the end of the file (page enums, `page_t`, `PAGES`, `page_for_drum`, `page_desc`; M1-B re-adds pages in `pages.c`).

- [ ] **Step 12: `firmware/src/drum_core.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The 8 drum tracks: power-on kit, hits (voice choice, choke groups, sample layer), and the per-track
 * render fx.c mixes (track_render). A cut voice is not dropped: its last sample decays in dtail. */

static uint32_t dvage;                                   /* hit counter: voice ages, noise seeds */
static uint32_t dblock;                                  /* blocks rendered (dm_metal.c: once per block) */

/* power-on kit: model and MIDI note of each track */
static const uint8_t KIT_DEF[NTRK][2] = {
    {DM_SMPL, 36}, {DM_SMPL, 38}, {DM_SMPL, 39}, {DM_SMPL, 42},
    {DM_SMPL, 46}, {DM_SMPL, 45}, {DM_SMPL, 37}, {DM_SMPL, 49},
};

static const dmodel_t *trk_model(const track_t *t) { return &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS]; }

static void dv_cut(track_t *t, dvoice_t *v)
{
    if (v->active)
        t->dtail += v->last;
    v->active = 0;
    v->last = 0;
}

static void drum_cut(track_t *t)                         /* every voice of t, with the declick tail */
{
    uint32_t i;
    for (i = 0; i < NDV; i++) {
        dv_cut(t, &t->v[i]);
        dv_cut(t, &t->lv[i]);
    }
}

static void model_follow(track_t *t)                     /* the model changed: the old voices stop */
{
    if (t->model != (uint8_t)t->p[P_MODEL]) {
        drum_cut(t);
        t->model = (uint8_t)t->p[P_MODEL];
    }
}

static dvoice_t *dv_alloc(track_t *t, dvoice_t *pool, uint32_t nv)   /* free, else the oldest */
{
    uint32_t i;
    dvoice_t *v = &pool[0];
    for (i = 0; i < nv; i++) {
        if (!pool[i].active)
            return &pool[i];
        if (pool[i].age < v->age)
            v = &pool[i];
    }
    dv_cut(t, v);
    return v;
}

static void dv_init(dvoice_t *v, uint32_t vel)
{
    memset(v, 0, sizeof *v);
    v->active = 1;
    v->vel = (uint8_t)(vel > 127u ? 127u : vel < 1u ? 1u : vel);
    v->age = ++dvage;
    v->rng = (int32_t)(dvage * 2654435761u) | 1;
}

/* one hit of track t at velocity vel (1..127) */
static void drum_hit(track_t *t, uint32_t vel)
{
    const dmodel_t *m = trk_model(t);
    uint32_t i, nv = m->voices < NDV ? m->voices : NDV;
    dvoice_t *v;
    if (t->p[P_MUTE])
        return;
    if (t->p[P_CHOKE])
        for (i = 0; i < NTRK; i++)
            if (&trk[i] != t && trk[i].p[P_CHOKE] == t->p[P_CHOKE])
                drum_cut(&trk[i]);
    model_follow(t);
    v = dv_alloc(t, t->v, nv);
    dv_init(v, vel);
    m->trigger(t, v);
    if (t->p[P_LLEVEL]) {
        uint32_t zi = smp_find((uint32_t)t->p[P_LSET], (uint32_t)t->p[P_LKEY]);
        if (zi != 0xFFFFu && smp_zone(zi)->n) {
            v = dv_alloc(t, t->lv, nv);
            dv_init(v, vel);
            smp_start(v, zi, (uint32_t)t->p[P_LKEY], t->p[P_LTUNE]);
            v->env[0] = ENV1;
            v->k[0] = dk(t->p[P_LDEC]);
        }
    }
}

/* track t's voices and tail into out (cleared first); returns non-zero while anything sounds */
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    const dmodel_t *m = trk_model(t);
    uint32_t i, nr = 0;
    for (i = 0; i < n; i++)
        out[i] = 0;
    model_follow(t);
    for (i = 0; i < NDV; i++)
        if (t->v[i].active) {
            m->render(t, &t->v[i], out, n);
            nr++;
        }
    for (i = 0; i < NDV; i++)
        if (t->lv[i].active) {
            smp_render(&t->lv[i], out, n, mulq15(t->p[P_LLEVEL] * 258, vel_gain(&t->lv[i])), 32767, 0);
            nr++;
        }
    if (t->dtail) {
        for (i = 0; i < n && t->dtail; i++) {
            int32_t d = t->dtail >> 4;
            out[i] += t->dtail;
            t->dtail -= d ? d : (t->dtail > 0 ? 1 : -1);
        }
        nr++;
    }
    return nr;
}

static void drum_block_begin(void) { dblock++; }

/* model mi with its default sound on t */
static void drum_set_model(track_t *t, uint32_t mi)
{
    const dmodel_t *m = &DMODELS[mi % NMODELS];
    uint32_t i;
    drum_cut(t);
    t->p[P_MODEL] = (int16_t)(mi % NMODELS);
    t->model = (uint8_t)t->p[P_MODEL];
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = m->edit[i].def;
    t->p[P_CHOKE] = m->choke;
}

static void drum_tracks_init(void)
{
    uint32_t i, k;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        memset(t, 0, sizeof *t);
        for (i = 0; i < P_COUNT; i++)
            if (i < P_E0 || i > P_E7)
                t->p[i] = TP[i].def;
        drum_set_model(t, KIT_DEF[k][0]);
        t->p[P_NOTE] = KIT_DEF[k][1];
        if (KIT_DEF[k][0] == DM_SMPL)
            t->p[P_E5] = KIT_DEF[k][1];             /* the GM kit's sound for that note */
    }
    song.sel = 0;
    song.master_q12 = 2048;
}
```

- [ ] **Step 13: `fx.c` and `slicer.c` — 8 tracks, no separate drum bus**

In `fx.c` `mix_block` replace:
```c
    events_block(n);
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
    slicer_drums(mix_l, mix_r, send_r, n);              /* drums_render, through the SLICER when on */
```
with:
```c
    events_block(n);
    drum_block_begin();
    for (i = 0; i < NTRK; i++)
        mix_part(&trk[i], n);
```
and change the comment above `mix_block`'s group (`/* one block of the whole mix ...`) to `/* one block of the whole mix (shared with the host tests): events -> each drum track -> dist -> SLICER -> level / pan / sends -> buses -> master; out: stereo Q15 */`.

In `slicer.c` delete the line `static int32_t sl_dbuf[CTL];     /* the drum track's dry mono signal (slicer_drums) */` and the whole `slicer_drums` function (from its comment `/* the drum track: as drums_render, ...` to the end of the file).

Verify: `grep -n "TDRUM\|TRK_DRUM\|is_drum\|drums_\|G_DRREV\|G_DRLVL" firmware/src/fx.c firmware/src/slicer.c` prints nothing.

- [ ] **Step 14: Minimal `firmware/src/seq.c` (Task 4 replaces it)**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum sequencer and input (replaced in Task 4 of the M1-A plan) */
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: cut its voices */
static uint32_t kb_prev;
static void events_block(uint32_t n)
{
    uint32_t i, pr = panic_req;
    (void)n;
    panic_req = 0;
    for (i = 0; i < NTRK; i++)
        if ((pr >> i) & 1u)
            drum_cut(&trk[i]);
}
```

- [ ] **Step 15: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: every line `ok`, `drum_test: all passed`, `guard: ok`, `check_untouched: ok`, `ALL DRUM HOST TESTS PASSED`. Fix compile errors by following the interfaces above (the usual causes: an old `P_*`/`G_*` name in `fx.c`/`slicer.c`, or `usb.c` needing a stub the old `hostsim.c` defined — copy that stub from `git show 1e838e1:tests/hostsim.c` into `drum_host.h`).

- [ ] **Step 16: Commit**

```bash
git add -A firmware/src tests tools/gen_samples.py
git commit -m "drum core: 8 tracks, SAMPLE model, choke, declick, host test harness

Synth engines, voice.c, drums.c, the arpeggiator and their tests removed.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Drum sequencer, keys and MIDI

**Files:**
- Rewrite: `firmware/src/seq.c`
- Modify: `tests/drum_test.c` (add tests, register in `main`)

**Interfaces:**
- Consumes: `drum_hit`, `drum_cut`, `div_samples` (fx.c), `slicer_start` (slicer.c), `midi_out_event`, `midi_in_q`, `mi_r`, `mi_w`, `MQ` (usb.c), `fm1_in.notes`.
- Produces: `events_block(n)`, `transport_req`, `panic_req`, `KEY_TRK_KEY[NTRK]`, `step_samples(t, period, idx)`, `input_hit(t, vel)`.

- [ ] **Step 1: Write the failing tests** (append to `tests/drum_test.c`, register each in `main` before the `printf`)

```c
/* blocks at which track t was hit while the mix runs for frames samples */
static uint32_t hits_at(uint32_t ti, uint32_t frames, uint32_t *at, uint32_t max)
{
    uint32_t f, n = 0, a = hit_age(&trk[ti]);
    for (f = 0; f < frames; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[ti]) != a) {
            a = hit_age(&trk[ti]);
            if (n < max)
                at[n] = f;
            n++;
        }
    }
    return n;
}

static void play(void) { transport_req = 1; }
#define CEILB(x) (((x) + CTL - 1) / CTL)            /* a step fires in the first block at or after its start */

static void test_seq_timing(void)
{
    uint32_t at[8], n, p = FS * 60 / 120 / 4;      /* 1/16 at 120 BPM: 5512 samples */
    host_init();
    trk[0].step[0].on = trk[0].step[4].on = 1;
    play();
    n = hits_at(0, 17 * p, at, 8);
    check("seq: steps 0 and 4 of 16 fire at 0 and 4 steps, then loop at 16",
          n == 3 && at[0] == 0 && at[1] / CTL == CEILB(4 * p) && at[2] / CTL == CEILB(16 * p));
    host_init();
    trk[1].p[P_SLEN] = 3;
    trk[1].step[0].on = 1;
    play();
    n = hits_at(1, 7 * p, at, 8);
    check("seq: per-track length 3 loops every 3 steps", n == 3 && at[1] / CTL == CEILB(3 * p) && at[2] / CTL == CEILB(6 * p));
    host_init();
    trk[2].p[P_SDIV] = 1;                           /* 1/8 */
    trk[2].step[1].on = 1;
    play();
    n = hits_at(2, 3 * p, at, 8);
    check("seq: per-track rate 1/8 puts step 1 at one eighth", n == 1 && at[0] / CTL == CEILB(div_samples(1)));
}

static void test_seq_edges(void)
{
    uint32_t at[80], n, d, ok = 1;
    for (d = 0; d < 6; d++) {
        host_init();                                 /* length 1, no swing: a hit every step */
        trk[0].p[P_SDIV] = (int16_t)d;
        trk[0].p[P_SLEN] = 1;
        trk[0].step[0].on = 1;
        play();
        n = hits_at(0, 8 * div_samples(d), at, 80);
        ok &= n >= 7 && n <= 9;
        host_init();                                 /* length 2, swing 100 + 100: long / short pairs */
        trk[0].p[P_SDIV] = (int16_t)d;
        trk[0].p[P_SLEN] = 2;
        trk[0].p[P_SSWING] = 100;
        song.g[G_SWING] = 100;
        trk[0].step[0].on = trk[0].step[1].on = 1;
        play();
        n = hits_at(0, 8 * div_samples(d), at, 80);
        ok &= n >= 7 && n <= 9;
    }
    check("seq: lengths 1 and 2, every division, swing 100+100: one hit per step, never stuck", ok);
    host_init();
    trk[0].p[P_SLEN] = 64;
    trk[0].step[63].on = 1;
    play();
    n = hits_at(0, 65 * (FS * 60 / 120 / 4), at, 80);
    check("seq: length 64 reaches step 63 once", n == 1);
}

static void test_keys(void)
{
    uint32_t a1, a0;
    host_init();
    a0 = hit_age(&trk[0]);
    a1 = hit_age(&trk[1]);
    fm1_in.notes = 1u << KEY_TRK_KEY[1];
    render_mix(0, 0, CTL);
    check("keys: white key 2 (G3) hits track 2 only", hit_age(&trk[1]) != a1 && hit_age(&trk[0]) == a0);
    fm1_in.notes = 1u << 1;                          /* F#3, a black key */
    a0 = dvage;
    render_mix(0, 0, CTL);
    check("keys: black keys do nothing", dvage == a0);
}

static void midi_in(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)
{
    midi_in_q[mi_w % MQ] = 0x09u | (st | ch) << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}

static void test_midi(void)
{
    uint32_t a0, a3;
    host_init();
    a0 = hit_age(&trk[1]);
    midi_in(0x90, 9, 38, 90);                        /* ch 10, the snare note */
    render_mix(0, 0, CTL);
    check("MIDI: note 38 on ch 10 hits the track with NOTE 38 at its velocity",
          hit_age(&trk[1]) != a0 && (trk[1].v[0].vel == 90 || trk[1].v[1].vel == 90));
    a0 = dvage;
    midi_in(0x90, 0, 38, 90);                        /* ch 1 */
    render_mix(0, 0, CTL);
    check("MIDI: other channels are ignored", dvage == a0);
    trk[3].p[P_NOTE] = 38;
    a0 = hit_age(&trk[1]);
    a3 = hit_age(&trk[3]);
    midi_in(0x90, 9, 38, 1);
    render_mix(0, 0, CTL);
    check("MIDI: two tracks on one note both fire, velocity 1 accepted",
          hit_age(&trk[1]) != a0 && hit_age(&trk[3]) != a3);
}

static void test_live_record(void)
{
    uint32_t p = FS * 60 / 120 / 4, a, at[4], n;
    host_init();
    song.rec = 1u;                                   /* track 1 armed */
    play();
    render_mix(0, 0, p + p * 3 / 4 / CTL * CTL);     /* 3/4 into step 1: records into step 2 */
    a = hit_age(&trk[0]);
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    fm1_in.notes = 0;
    check("live record: a late hit goes into the next step", trk[0].step[2].on && !trk[0].step[1].on);
    n = hits_at(0, p, at, 4);
    check("live record: that step does not hit again this time round", hit_age(&trk[0]) != a && n == 0);
}

static void test_accent(void)
{
    host_init();
    trk[0].step[0].on = trk[0].step[0].acc = 1;
    play();
    render_mix(0, 0, CTL);
    check("seq: an accented step hits at velocity 127", trk[0].v[0].vel == 127 || trk[0].v[1].vel == 127);
}
```
Register: `test_seq_timing(); test_seq_edges(); test_keys(); test_midi(); test_live_record(); test_accent();`

- [ ] **Step 2: Run to verify they fail**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `KEY_TRK_KEY` undeclared (compile error).

- [ ] **Step 3: Rewrite `firmware/src/seq.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum sequencer and input. 8 tracks x 64 steps (hit / accent), per-track length, division and swing.
 * The 8 white keys F3..F4 hit tracks 1..8; MIDI notes on the drum channel hit every track whose NOTE
 * matches; an armed track records hits into the nearest step while playing. Runs in the audio ISR
 * before each block (events_block). */
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: cut its voices */
static uint32_t kb_prev;
/* key index (0 = F3, the lowest key) of the white keys F3 G3 A3 B3 C4 D4 E4 F4 -> tracks 1..8 */
static const uint8_t KEY_TRK_KEY[NTRK] = {0, 2, 4, 6, 7, 9, 11, 12};

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }
static uint32_t drum_ch(void) { return (uint32_t)clamp(song.g[G_DRCH], 1, 16) - 1u; }

/* the length of step idx in samples: SWING (the track's + the global) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    int32_t sw = (t->p[P_SSWING] + song.g[G_SWING]) * (int32_t)period / 250;
    return period + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* live recording: into the nearest step, as swung (the playing one, or the next one past its middle) */
static void rec_hit(track_t *t, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len;
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]);
    if (t->seq_pos > step_samples(t, period, t->seq_idx) / 2u) {
        idx = (idx + 1u) % len;
        t->rskip = 1;                               /* it sounds now: that step must not hit again */
        t->rskip_idx = (uint8_t)idx;
    }
    t->step[idx].on = 1;
    if (vel > 110u)
        t->step[idx].acc = 1;
}

static void input_hit(track_t *t, uint32_t vel)
{
    if (((song.rec >> trk_index(t)) & 1u) && song.playing)
        rec_hit(t, vel);
    drum_hit(t, vel);
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch = cur ^ kb_prev, i;
    kb_prev = cur;
    for (i = 0; ch && i < NTRK; i++) {
        uint32_t k = KEY_TRK_KEY[i], note = (uint32_t)trk[i].p[P_NOTE] & 127u;
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {
            input_hit(&trk[i], 100u);
            midi_out_event(0x09u | (0x90u | drum_ch()) << 8 | note << 16 | 100u << 24);
        } else {
            midi_out_event(0x08u | (0x80u | drum_ch()) << 8 | note << 16);
        }
    }
}

static void midi_block(void)
{
    while (mi_r != mi_w) {
        uint32_t pkt = midi_in_q[mi_r % MQ], st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, i;
        mi_r++;
        if (st != 0x90u || !d2 || ch != drum_ch())
            continue;                               /* note-offs: one-shots ignore them */
        for (i = 0; i < NTRK; i++)
            if ((uint32_t)trk[i].p[P_NOTE] == d1)
                input_hit(&trk[i], d2);
    }
}

static void seq_start(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {                    /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                    /* step 0 fires on the first block */
        t->rskip = 0;
    }
    song.tick = 0;
    song.playing = 1;
    slicer_start();
}

static void seq_stop(void) { song.playing = 0; }

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]), len = (uint32_t)t->p[P_SLEN];
    if (!song.playing)
        return;
    t->seq_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_idx);
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? 0 : t->seq_pos - cur_len;
        t->seq_idx = (uint16_t)((t->seq_idx + 1u) % (len ? len : 1u));
        if (t->rskip && t->rskip_idx == t->seq_idx)
            t->rskip = 0;
        else if (t->step[t->seq_idx].on)
            drum_hit(t, t->step[t->seq_idx].acc ? 127u : 96u);
    }
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr;
    if (transport_req == 1u) {
        seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
    pr = panic_req;
    panic_req = 0;
    for (i = 0; i < NTRK; i++)
        if ((pr >> i) & 1u)
            drum_cut(&trk[i]);
    keyboard_block();
    midi_block();
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], n);
    if (song.playing)
        song.tick++;
}
```

- [ ] **Step 4: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: all `ok`, `ALL DRUM HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/seq.c tests/drum_test.c
git commit -m "seq: drum steps (hit/accent), per-track length/rate/swing, keys, MIDI notes, live record

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Kicks (808, 909) and the shared pitched-body render

**Files:**
- Create: `firmware/src/dm_kick.c`
- Modify: `firmware/src/dmodels.c`, `firmware/src/drum_core.c` (`KIT_DEF[0]` = `DM_K909`), `tests/drum_test.c`

**Interfaces:**
- Produces: `body_render(t, v, out, n)` — pitched body used by kicks (Task 5) and tom/conga/claves (Task 8). Voice fields it reads: `inc[0]` base increment, `x[0]` pitch-sweep increment amount, `env[0..2]`/`k[0..2]` (amp, pitch, transient), `x[1]` transient gain Q15, `x[2]` drive 0..127, `x[3]` transient mode (0 raw noise, 1 sine blip at `inc[1]`, 2 noise through `c[0]` low-pass). Ends when `env[0]` and `env[2]` are below `ENV_END`.
- Models: `DM_K808`, `DM_K909`.
- Test helpers: `rising(x, a, b)` (zero-crossing count), `model_health(mi)`.

- [ ] **Step 1: Write the failing tests** (append; register in `main`)

```c
static uint32_t rising(const int32_t *x, uint32_t a, uint32_t b)   /* upward zero crossings */
{
    uint32_t c = 0;
    for (a++; a < b; a++)
        if (x[a - 1] < 0 && x[a] >= 0)
            c++;
    return c;
}

static double freq_of(const int32_t *x, uint32_t a, uint32_t b) { return rising(x, a, b) * (double)FS / (b - a); }

/* default hit at velocity 127: audible, bounded, ends within 6 s, then stays silent */
static void model_health(uint32_t mi)
{
    char what[96];
    uint32_t e = hit_model(mi, 127, SECS(7));
    int32_t pk = peak_of(wl, 0, SECS(7));
    snprintf(what, sizeof what, "%s: audible, bounded, ends in 6 s (peak %d, end %.2f s)", N_MODEL[mi], pk, e / (double)FS);
    check(what, pk >= 1500 && pk <= 3 * VOICE_FS && e < SECS(6) && track_idle(&trk[0]));
}

static void test_kicks(void)
{
    double f0, f12, fs;
    model_health(DM_K909);
    model_health(DM_K808);
    host_init();
    drum_set_model(&trk[0], DM_K909);
    trk[0].p[P_E1] = 127;                            /* long, for a precise pitch */
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(1.2));
    f0 = freq_of(wl, SECS(0.1), SECS(1.1));
    fs = freq_of(wl, 0, SECS(0.015));
    trk[0].p[P_E0] = 12;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(1.2));
    f12 = freq_of(wl, SECS(0.1), SECS(1.1));
    check("909 kick: settles near 52 Hz", f0 > 49 && f0 < 55);
    check("909 kick: TUNE +12 doubles it", f12 / f0 > 1.9 && f12 / f0 < 2.1);
    check("909 kick: the start sweeps from above", fs > 1.5 * f0);
    hit_model(DM_K808, 127, SECS(0.01));
    check("808 kick: starts without a click (|first sample| < 2 % of full)", abs(wl[0]) < VOICE_FS / 50);
    hit_model(DM_K909, 127, SECS(1));
    {
        int32_t hi = peak_of(wl, 0, SECS(1));
        hit_model(DM_K909, 40, SECS(1));
        check("909 kick: velocity 40 is clearly quieter than 127", peak_of(wl, 0, SECS(1)) * 2 < hi);
    }
}

static void test_model_change(void)
{
    host_init();
    drum_set_model(&trk[0], DM_K808);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], 0, SECS(0.05));
    trk[0].p[P_MODEL] = DM_K909;                     /* the knob turns while it sounds */
    render_track(&trk[0], wl, SECS(0.05));
    check("model change: the old voices stop, the tail fades, nothing hangs",
          !trk[0].v[0].active && !trk[0].v[1].active && trk[0].model == DM_K909 && end_of(wl, SECS(0.05)) < SECS(0.02));
}
```
Register: `test_kicks(); test_model_change();`

- [ ] **Step 2: Run to verify they fail**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_K909` undeclared.

- [ ] **Step 3: `firmware/src/dm_kick.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Pitched-body drums: a sine whose pitch falls from a sweep to its base, a transient (noise, a sine
 * blip or low-passed noise) and a soft-clip drive. 808 / 909 kick here; tom, conga, claves in dm_perc.c. */

/* inc[0] base, x[0] sweep amount (increment), env[0] amp, env[1] pitch, env[2] transient,
 * x[1] transient gain, x[2] drive, x[3] transient mode: 0 noise, 1 sine blip (inc[1]), 2 lp noise (c[0]) */
static void body_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t drv = v->x[2];
    (void)t;
    for (i = 0; i < n; i++) {
        uint32_t inc = v->inc[0] + (uint32_t)(v->x[0] >> 8) * (uint32_t)(v->env[1] >> 16);
        int32_t x = mulq15(sine_i(v->ph[0]), env_q15(v->env[0])), c;
        if (v->x[3] == 1)
            c = sine_i(v->ph[1]);
        else if (v->x[3] == 2) {
            int32_t bp, hp;
            c = dsvf_tick(&v->c[0], dnoise(v), &v->f[0], &v->f[1], &bp, &hp);
        } else
            c = dnoise(v);
        x += mulq15(mulq15(c, env_q15(v->env[2])), v->x[1]);
        v->ph[0] += inc;
        v->ph[1] += v->inc[1];
        if (drv)
            x = softclip(x + ((x * drv) >> 6));
        dm_put(v, out, i, x);
        env_step(v, 0);
        env_step(v, 1);
        env_step(v, 2);
    }
    if (v->env[0] < ENV_END && v->env[2] < ENV_END)
        dm_end(v);
}

/* 909 KICK: TUNE DECAY SWEEP CLICK, SWEEP-TIME DRIVE */
static void k909_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(52), p[0]);
    v->x[0] = (int32_t)inc_tune(HZ(4) * (uint32_t)p[2], p[0]);   /* up to ~500 Hz above */
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(50 + p[1] * 60 / 127);
    v->k[1] = dk(10 + p[4] * 50 / 127);
    v->k[2] = dk(0);
    v->x[1] = p[3] * 200;
    v->x[2] = p[5];
    v->x[3] = 0;
}

/* 808 KICK: TUNE DECAY TONE (blip) DIP (pitch dip), DRIVE */
static void k808_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(49), p[0]);
    v->x[0] = (int32_t)(v->inc[0] / 200u * (uint32_t)p[3]);      /* starts up to ~60 % higher */
    v->inc[1] = v->inc[0] * 24u;                                  /* ~1.2 kHz blip */
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(60 + p[1] * 67 / 127);
    v->k[1] = dk(14);
    v->k[2] = dk(0);
    v->x[1] = p[2] * 150;
    v->x[2] = p[4];
    v->x[3] = 1;
}

#define DM_K808_DEF {"K808", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 80), \
    PD("TONE", F_INT, 0, 127, 40), PD("DIP", F_INT, 0, 127, 40), PD("DRIVE", F_PCT, 0, 127, 30), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, k808_trigger, body_render}
#define DM_K909_DEF {"K909", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("SWEEP", F_INT, 0, 127, 70), PD("CLICK", F_INT, 0, 127, 50), PD("SWPT", F_INT, 0, 127, 40), \
    PD("DRIVE", F_PCT, 0, 127, 20), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, k909_trigger, body_render}
```

- [ ] **Step 4: Register the models**

`dmodels.c`:
```c
#include "dm_dsp.c"
#include "dm_sample.c"
#include "dm_kick.c"

enum { DM_K808, DM_K909, DM_SMPL, NMODELS };
static const dmodel_t DMODELS[NMODELS] = {DM_K808_DEF, DM_K909_DEF, DM_SMPL_DEF};
static const char *const N_MODEL[NMODELS] = {"K808", "K909", "SMPL"};
```
`drum_core.c` `KIT_DEF[0]` → `{DM_K909, 36}`.

- [ ] **Step 5: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: all `ok`. If the pitch window check fails, print `f0`, `f12`, `fs` and fix the increment math (do not loosen the tolerance).

- [ ] **Step 6: Commit**

```bash
git add firmware/src/dm_kick.c firmware/src/dmodels.c firmware/src/drum_core.c tests/drum_test.c
git commit -m "models: 808 and 909 kick (pitched body with sweep, transient, drive)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Snares and claps (808, 909)

**Files:**
- Create: `firmware/src/dm_snare.c`
- Modify: `firmware/src/dmodels.c`, `firmware/src/drum_core.c` (`KIT_DEF[1]` = `DM_S808`, `KIT_DEF[2]` = `DM_C808`), `tests/drum_test.c`

**Interfaces:**
- Consumes: `dm_dsp.c` helpers, `CUT_*` constants.
- Produces: models `DM_S808`, `DM_S909`, `DM_C808`, `DM_C909`; test helper `hf_of(x, a, b)`.

- [ ] **Step 1: Write the failing tests** (append; register in `main`)

```c
static double hf_of(const int32_t *x, uint32_t a, uint32_t b)   /* high-frequency energy proxy */
{
    double s = 0;
    for (a++; a < b; a++)
        s += abs(x[a] - x[a - 1]);
    return s;
}

static double snappy_ratio(uint32_t mi)
{
    double lo, hi;
    host_init();
    drum_set_model(&trk[0], mi);
    trk[0].p[P_E3] = 0;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.3));
    lo = hf_of(wl, 0, SECS(0.3));
    trk[0].p[P_E3] = 127;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(0.3));
    hi = hf_of(wl, 0, SECS(0.3));
    return hi / (lo + 1);
}

static uint32_t clap_bursts(uint32_t mi)       /* 1 ms energy maxima > 30 % in the first 35 ms */
{
    double w[40] = {0}, mx = 0;
    uint32_t i, n = 0;
    hit_model(mi, 127, SECS(0.05));
    for (i = 0; i < SECS(0.035); i++)
        w[i / 44] += abs(wl[i]);
    for (i = 0; i < 40; i++)
        if (w[i] > mx)
            mx = w[i];
    for (i = 1; i + 1 < 40; i++)
        if (w[i] > 0.3 * mx && w[i] >= w[i - 1] && w[i] > w[i + 1])
            n++;
    return n;
}

static void test_snares_claps(void)
{
    uint32_t e0, e1;
    model_health(DM_S808);
    model_health(DM_S909);
    model_health(DM_C808);
    model_health(DM_C909);
    check("808 snare: SNAPPY 127 has > 3x the noise of SNAPPY 0", snappy_ratio(DM_S808) > 3);
    check("909 snare: SNAPPY 127 has > 3x the noise of SNAPPY 0", snappy_ratio(DM_S909) > 3);
    host_init();
    drum_set_model(&trk[0], DM_S808);
    trk[0].p[P_E1] = 0;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(3));
    e0 = end_of(wl, SECS(3));
    trk[0].p[P_E1] = 127;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(3));
    e1 = end_of(wl, SECS(3));
    check("808 snare: DECAY 127 rings > 1.5x longer than DECAY 0", e1 > e0 * 3 / 2);
    check("808 clap: >= 3 bursts in the first 35 ms", clap_bursts(DM_C808) >= 3);
    check("909 clap: >= 3 bursts in the first 35 ms", clap_bursts(DM_C909) >= 3);
}
```
Register: `test_snares_claps();`

- [ ] **Step 2: Run to verify they fail**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_S808` undeclared.

- [ ] **Step 3: `firmware/src/dm_snare.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Snares (two decaying tones + filtered noise) and claps (band-passed noise in sawtooth bursts + tail). */

/* ph/inc[0..1] tones, env[0..1] their decays, env[2] noise; x[0], x[1] tone gains, x[2] noise gain;
 * c[0] noise filter (808: band-pass; 909: low-pass, then c[1] high-pass); x[3] 1 = 909 noise path */
static void snare_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t bp, hp, lp, nz, x;
        x = mulq15(mulq15(sine_i(v->ph[0]), env_q15(v->env[0])), v->x[0]) +
            mulq15(mulq15(sine_i(v->ph[1]), env_q15(v->env[1])), v->x[1]);
        v->ph[0] += v->inc[0];
        v->ph[1] += v->inc[1];
        lp = dsvf_tick(&v->c[0], dnoise(v), &v->f[0], &v->f[1], &bp, &hp);
        if (v->x[3]) {
            dsvf_tick(&v->c[1], lp, &v->f[2], &v->f[3], &bp, &hp);
            nz = hp;
        } else {
            nz = bp;
        }
        x += mulq15(mulq15(nz, env_q15(v->env[2])), v->x[2]);
        dm_put(v, out, i, x);
        env_step(v, 0);
        env_step(v, 1);
        env_step(v, 2);
    }
    if (v->env[0] < ENV_END && v->env[1] < ENV_END && v->env[2] < ENV_END)
        dm_end(v);
}

/* 808 SNARE: TUNE DECAY TONE SNAPPY, NOISE-TONE */
static void s808_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(173), p[0]);
    v->inc[1] = inc_tune(HZ(336), p[0]);
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = v->k[1] = dk(33);                      /* ~29 ms, as measured on a TR-808 */
    v->k[2] = dk(40 + p[1] * 50 / 127);
    v->x[0] = 32767 - p[2] * 200;
    v->x[1] = 8000 + p[2] * 190;
    v->x[2] = p[3] * 258;
    v->x[3] = 0;
    dsvf_coef(&v->c[0], CUT_4000 + (p[4] - 64) * 80, 40);
}

/* 909 SNARE: TUNE DECAY BODY SNAPPY, NOISE-TONE */
static void s909_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(185), p[0]);
    v->inc[1] = inc_tune(HZ(330), p[0]);
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(38);
    v->k[1] = dk(30);
    v->k[2] = dk(35 + p[1] * 55 / 127);
    v->x[0] = p[2] * 200;
    v->x[1] = p[2] * 120;
    v->x[2] = p[3] * 258;
    v->x[3] = 1;
    dsvf_coef(&v->c[0], CUT_7000, 0);
    dsvf_coef(&v->c[1], CUT_2000 + (p[4] - 64) * 80, 0);
}

/* claps: t samples since the hit; ph[0] position in the burst, inc[0] burst length, ph[1] bursts done,
 * x[0] number of bursts, x[1] jitter (samples), x[2] tail gain, x[3] burst slope (Q15 per sample);
 * env[0] the last burst's decay (from the end of the bursts), env[1] the tail */
static void clap_next_burst(dvoice_t *v, uint32_t base)
{
    uint32_t j = (uint32_t)v->x[1];
    v->inc[0] = j ? base - j / 2u + (uint32_t)noise32(&v->rng) % (j + 1u) : base;
    v->x[3] = 32767 / (int32_t)v->inc[0];
    v->ph[0] = 0;
}

static void clap_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t bp, hp, e, x;
        dsvf_tick(&v->c[0], dnoise(v), &v->f[0], &v->f[1], &bp, &hp);
        if (v->ph[1] < (uint32_t)v->x[0]) {          /* sawtooth bursts */
            e = ((int32_t)v->inc[0] - (int32_t)v->ph[0]) * v->x[3];
            if (++v->ph[0] >= v->inc[0]) {
                v->ph[1]++;
                clap_next_burst(v, v->inc[2]);
            }
        } else {
            e = env_q15(v->env[0]);
            env_step(v, 0);
        }
        x = mulq15(bp, e) + mulq15(mulq15(bp, env_q15(v->env[1])), v->x[2]);
        dm_put(v, out, i, x);
        env_step(v, 1);
    }
    if (v->ph[1] >= (uint32_t)v->x[0] && v->env[0] < ENV_END && v->env[1] < ENV_END)
        dm_end(v);
}

static void clap_start(dvoice_t *v, uint32_t burst, uint32_t nb, uint32_t jitter)
{
    v->inc[2] = burst;
    v->x[0] = (int32_t)nb;
    v->x[1] = (int32_t)jitter;
    v->env[0] = v->env[1] = ENV1;
    v->k[0] = dk(26);                                /* the last burst: ~20 ms */
    clap_next_burst(v, burst);
}

/* 808 CLAP: - DECAY TONE TAIL */
static void c808_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    clap_start(v, 441, 3, 0);                        /* 3 x 10 ms */
    v->k[1] = dk(40 + p[1] * 40 / 127);
    v->x[2] = p[3] * 200;
    dsvf_coef(&v->c[0], CUT_1000 + (p[2] - 64) * 80 + p[0] * CUT_ST, 60);
}

/* 909 CLAP: TUNE DECAY TONE SPREAD */
static void c909_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    clap_start(v, 353, 3, (uint32_t)p[3] * 2u);      /* 3 x ~8 ms, randomised */
    v->k[1] = dk(35 + p[1] * 40 / 127);
    v->x[2] = 9000;
    dsvf_coef(&v->c[0], CUT_1200 + (p[2] - 64) * 80 + p[0] * CUT_ST, 70);
}

#define DM_S808_DEF {"S808", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("SNAP", F_INT, 0, 127, 90), PD("NTONE", F_INT, 0, 127, 64), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, s808_trigger, snare_render}
#define DM_S909_DEF {"S909", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("BODY", F_INT, 0, 127, 80), PD("SNAP", F_INT, 0, 127, 80), PD("NTONE", F_INT, 0, 127, 64), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, s909_trigger, snare_render}
#define DM_C808_DEF {"C808", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("TAIL", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, c808_trigger, clap_render}
#define DM_C909_DEF {"C909", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("SPRD", F_INT, 0, 127, 40), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, c909_trigger, clap_render}
```

- [ ] **Step 4: Register the models**

`dmodels.c` — add `#include "dm_snare.c"` after `dm_kick.c`; enum `{DM_K808, DM_K909, DM_S808, DM_S909, DM_C808, DM_C909, DM_SMPL, NMODELS}`; `DMODELS` and `N_MODEL` in the same order (`"S808", "S909", "C808", "C909"`). `drum_core.c` `KIT_DEF[1]` → `{DM_S808, 38}`, `KIT_DEF[2]` → `{DM_C808, 39}`.

- [ ] **Step 5: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: all `ok`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/dm_snare.c firmware/src/dmodels.c firmware/src/drum_core.c tests/drum_test.c
git commit -m "models: 808/909 snare and clap

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Metal source — hats, cymbal, cowbell

**Files:**
- Create: `firmware/src/dm_metal.c`
- Modify: `firmware/src/dmodels.c`, `firmware/src/drum_core.c` (`KIT_DEF[3]` = `{DM_HATC, 42}`, `[4]` = `{DM_HATO, 46}`, `[7]` = `{DM_CYMB, 49}`), `tests/drum_test.c`

**Interfaces:**
- Consumes: `dblock` (drum_core.c, bumped by `drum_block_begin`). `dm_metal.c` is included by `dmodels.c` before `drum_core.c`, so it declares `static uint32_t dblock;` itself and `drum_core.c` drops its own definition of `dblock` (keep `drum_block_begin` there).
- Produces: `metal_make(n)` fills `metal_buf[CTL]` (six 808 squares) and `cow_buf[CTL]` (540 + 800 Hz) once per block; models `DM_HATC`, `DM_HATO`, `DM_CYMB`, `DM_COWB`.

- [ ] **Step 1: Write the failing tests** (append; register in `main`)

```c
static void test_metal(void)
{
    uint32_t ec, eo;
    int32_t last;
    model_health(DM_HATC);
    model_health(DM_HATO);
    model_health(DM_CYMB);
    model_health(DM_COWB);
    ec = hit_model(DM_HATC, 127, SECS(3));
    eo = hit_model(DM_HATO, 127, SECS(3));
    check("hats: the open hat rings > 2x longer than the closed one", eo > 2 * ec);
    host_init();                                     /* power-on kit: tracks 4 / 5 are HATC / HATO, group 1 */
    check("hats: closed and open default to choke group 1", trk[3].p[P_CHOKE] == 1 && trk[4].p[P_CHOKE] == 1);
    drum_hit(&trk[4], 127);
    render_track(&trk[4], wl, SECS(0.05));
    last = wl[SECS(0.05) - 1];
    drum_hit(&trk[3], 127);
    render_track(&trk[4], wl, SECS(0.01));
    check("hats: a closed hat silences the open one within 100 samples",
          !trk[4].v[0].active && abs(wl[100]) <= abs(last) / 100 + 1 && peak_of(wl, 300, SECS(0.01)) == 0);
    host_init();
    drum_set_model(&trk[0], DM_CYMB);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], 0, SECS(0.2));
    drum_hit(&trk[0], 127);
    check("cymbal: 2 voices, the first keeps ringing over a retrigger", trk[0].v[0].active && trk[0].v[1].active);
}
```
Register: `test_metal();`

- [ ] **Step 2: Run to verify they fail**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_HATC` undeclared.

- [ ] **Step 3: `firmware/src/dm_metal.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The TR-808 metal source: six square waves (205.3 .. 800 Hz), made once per block for every hat,
 * cymbal and cowbell voice; each model shapes it with its own band-passes and envelopes. */
static uint32_t dblock;                              /* blocks rendered (drum_core.c drum_block_begin) */
static const uint32_t METAL_INC[6] = {HZ(205.3), HZ(304.4), HZ(369.6), HZ(522.7), HZ(540.0), HZ(800.0)};
static uint32_t metal_ph[6], metal_blk = 0xFFFFFFFFu;
static int32_t metal_buf[CTL], cow_buf[CTL];

static void metal_make(uint32_t n)
{
    uint32_t i, k;
    if (metal_blk == dblock)
        return;
    metal_blk = dblock;
    for (i = 0; i < n; i++) {
        int32_t s = 0, c;
        for (k = 0; k < 6u; k++) {
            metal_ph[k] += METAL_INC[k];
            s += (int32_t)(metal_ph[k] >> 31);
        }
        c = (int32_t)(metal_ph[4] >> 31) + (int32_t)(metal_ph[5] >> 31);
        metal_buf[i] = (2 * s - 6) * 5461;           /* -32766 .. 32766 */
        cow_buf[i] = (2 * c - 2) * 16383;
    }
}

/* hats: metal -> band-pass c[0] -> high-pass c[1] -> env[0] */
static void hat_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    metal_make(n);
    for (i = 0; i < n; i++) {
        int32_t bp, hp, b2, h2;
        dsvf_tick(&v->c[0], metal_buf[i], &v->f[0], &v->f[1], &bp, &hp);
        dsvf_tick(&v->c[1], bp, &v->f[2], &v->f[3], &b2, &h2);
        dm_put(v, out, i, mulq15(h2, env_q15(v->env[0])));
        env_step(v, 0);
    }
    if (v->env[0] < ENV_END)
        dm_end(v);
}

static void hat_start(track_t *t, dvoice_t *v, int32_t dec)
{
    const int16_t *p = &t->p[P_E0];
    v->env[0] = ENV1;
    v->k[0] = dk(dec);
    dsvf_coef(&v->c[0], CUT_7100 + p[0] * CUT_ST + (p[2] - 64) * 60, 30);
    dsvf_coef(&v->c[1], CUT_6000 + p[0] * CUT_ST + (p[3] - 64) * 60, 0);
}

/* HAT C / HAT O: TUNE (filters) DECAY TONE (band) BRIGHT (high-pass) */
static void hatc_trigger(track_t *t, dvoice_t *v) { hat_start(t, v, 30 + t->p[P_E1] * 30 / 127); }
static void hato_trigger(track_t *t, dvoice_t *v) { hat_start(t, v, 50 + t->p[P_E1] * 50 / 127); }

/* cymbal: band 3.44 kHz with env[0] (DECAY), band 7.1 kHz with env[1]; x[0], x[1] their gains */
static void cymb_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    metal_make(n);
    for (i = 0; i < n; i++) {
        int32_t b1, h1, b2, h2, x;
        dsvf_tick(&v->c[0], metal_buf[i], &v->f[0], &v->f[1], &b1, &h1);
        dsvf_tick(&v->c[1], metal_buf[i], &v->f[2], &v->f[3], &b2, &h2);
        x = mulq15(mulq15(b1, env_q15(v->env[0])), v->x[0]) + mulq15(mulq15(h2, env_q15(v->env[1])), v->x[1]);
        dm_put(v, out, i, x);
        env_step(v, 0);
        env_step(v, 1);
    }
    if (v->env[0] < ENV_END && v->env[1] < ENV_END)
        dm_end(v);
}

/* CYMBAL: TUNE DECAY TONE BAL */
static void cymb_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    int32_t sh = p[0] * CUT_ST + (p[2] - 64) * 60;
    v->env[0] = v->env[1] = ENV1;
    v->k[0] = dk(80 + p[1] * 24 / 127);
    v->k[1] = dk(75);
    v->x[0] = 32767 - p[3] * 200;
    v->x[1] = 7000 + p[3] * 200;
    dsvf_coef(&v->c[0], CUT_3440 + sh, 20);
    dsvf_coef(&v->c[1], CUT_7100 + sh, 20);
}

/* cowbell: 540 + 800 Hz squares -> band-pass ~880 Hz; a fast and a slow decay mixed (x[0], x[1]) */
static void cowb_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    metal_make(n);
    for (i = 0; i < n; i++) {
        int32_t bp, hp, e;
        dsvf_tick(&v->c[0], cow_buf[i], &v->f[0], &v->f[1], &bp, &hp);
        e = mulq15(env_q15(v->env[0]), v->x[0]) + mulq15(env_q15(v->env[1]), v->x[1]);
        dm_put(v, out, i, mulq15(bp, e));
        env_step(v, 0);
        env_step(v, 1);
    }
    if (v->env[0] < ENV_END && v->env[1] < ENV_END)
        dm_end(v);
}

/* COWBELL: TUNE (filter) DECAY TONE TAIL */
static void cowb_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->env[0] = v->env[1] = ENV1;
    v->k[0] = dk(14);
    v->k[1] = dk(40 + p[1] * 50 / 127);
    v->x[0] = 32767 - p[3] * 150;
    v->x[1] = 6000 + p[3] * 150;
    dsvf_coef(&v->c[0], CUT_880 + p[0] * CUT_ST + (p[2] - 64) * 40, 70);
}

#define DM_HATC_DEF {"HATC", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("BRITE", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, hatc_trigger, hat_render}
#define DM_HATO_DEF {"HATO", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("BRITE", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, hato_trigger, hat_render}
#define DM_CYMB_DEF {"CYMB", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("BAL", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, cymb_trigger, cymb_render}
#define DM_COWB_DEF {"COWB", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("TAIL", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, cowb_trigger, cowb_render}
```

- [ ] **Step 4: Register; move `dblock`**

`dmodels.c`: add `#include "dm_metal.c"` after `dm_snare.c`; enum `{DM_K808, DM_K909, DM_S808, DM_S909, DM_C808, DM_C909, DM_HATC, DM_HATO, DM_CYMB, DM_COWB, DM_SMPL, NMODELS}`; `DMODELS`/`N_MODEL` in that order. `drum_core.c`: delete `static uint32_t dblock;  ...` (now in `dm_metal.c`); `KIT_DEF[3]` → `{DM_HATC, 42}`, `[4]` → `{DM_HATO, 46}`, `[7]` → `{DM_CYMB, 49}`.

`tests/drum_host.h` `host_reset_fx()`: add, so every test starts the metal source from the same phase:
```c
    memset(metal_ph, 0, sizeof metal_ph);
    metal_blk = 0xFFFFFFFFu;
    dblock = 0;
```

- [ ] **Step 5: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: all `ok`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/dm_metal.c firmware/src/dmodels.c firmware/src/drum_core.c tests/drum_test.c tests/drum_host.h
git commit -m "models: 808 metal source, closed/open hat (choke 1), cymbal, cowbell

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Tom, conga, rimshot, claves

**Files:**
- Create: `firmware/src/dm_perc.c`
- Modify: `firmware/src/dmodels.c`, `firmware/src/drum_core.c` (`KIT_DEF[5]` = `{DM_TOM, 45}`, `[6]` = `{DM_RIM, 37}`), `tests/drum_test.c`

**Interfaces:**
- Consumes: `body_render` (Task 5) and its voice-field contract.
- Produces: models `DM_TOM`, `DM_CONGA`, `DM_RIM`, `DM_CLAVE`.

- [ ] **Step 1: Write the failing tests** (append; register in `main`)

```c
static double late_freq(uint32_t mi, double a, double b)   /* DECAY 127, pitch between a and b s */
{
    host_init();
    drum_set_model(&trk[0], mi);
    trk[0].p[P_E1] = 127;
    drum_hit(&trk[0], 127);
    render_track(&trk[0], wl, SECS(b));
    return freq_of(wl, SECS(a), SECS(b));
}

static void test_perc(void)
{
    double f;
    model_health(DM_TOM);
    model_health(DM_CONGA);
    model_health(DM_RIM);
    model_health(DM_CLAVE);
    f = late_freq(DM_TOM, 0.1, 0.4);
    check("tom: settles near 120 Hz", f > 112 && f < 128);
    f = late_freq(DM_CONGA, 0.05, 0.2);
    check("conga: settles near 310 Hz", f > 295 && f < 325);
    f = late_freq(DM_CLAVE, 0.005, 0.045);
    check("claves: near 2.5 kHz", f > 2400 && f < 2600);
}
```
Register: `test_perc();`

- [ ] **Step 2: Run to verify they fail**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `DM_TOM` undeclared.

- [ ] **Step 3: `firmware/src/dm_perc.c`**

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Tuned percussion: tom, conga, claves (body_render, dm_kick.c) and the rimshot. */

/* TOM: TUNE DECAY NOISE DROP */
static void tom_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(120), p[0]);
    v->x[0] = (int32_t)(v->inc[0] / 256u * (uint32_t)p[3]);      /* starts up to ~50 % higher */
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(40 + p[1] * 50 / 127);
    v->k[1] = dk(40);
    v->k[2] = dk(45 + p[1] * 50 / 127);
    v->x[1] = p[2] * 60;
    v->x[2] = 0;
    v->x[3] = 2;
    dsvf_coef(&v->c[0], CUT_600, 0);
}

/* CONGA: TUNE DECAY SLAP DROP */
static void conga_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(310), p[0]);
    v->x[0] = (int32_t)(v->inc[0] / 512u * (uint32_t)p[3]);
    v->inc[1] = v->inc[0] * 2u;
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(30 + p[1] * 45 / 127);
    v->k[1] = dk(30);
    v->k[2] = dk(8);
    v->x[1] = p[2] * 200;
    v->x[2] = 0;
    v->x[3] = 1;
}

/* CLAVE: TUNE DECAY CLICK DROP */
static void clave_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(2500), p[0]);
    v->x[0] = (int32_t)(v->inc[0] / 256u * (uint32_t)p[3]);
    v->inc[1] = v->inc[0] * 2u;
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(31 + p[1] * 25 / 127);
    v->k[1] = dk(5);
    v->k[2] = dk(0);
    v->x[1] = p[2] * 150;
    v->x[2] = 0;
    v->x[3] = 1;
}

/* rimshot: 1667 + 455 Hz under one decay, driven, high-passed at ~600 Hz; x[0], x[1] tone gains, x[2] drive */
static void rim_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t bp, hp, x;
        x = mulq15(sine_i(v->ph[0]), v->x[0]) + mulq15(sine_i(v->ph[1]), v->x[1]);
        v->ph[0] += v->inc[0];
        v->ph[1] += v->inc[1];
        x = mulq15(x, env_q15(v->env[0]));
        x = softclip(x + ((x * v->x[2]) >> 4));
        dsvf_tick(&v->c[0], x, &v->f[0], &v->f[1], &bp, &hp);
        dm_put(v, out, i, hp);
        env_step(v, 0);
    }
    if (v->env[0] < ENV_END)
        dm_end(v);
}

/* RIM: TUNE DECAY TONE DRIVE */
static void rim_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(1667), p[0]);
    v->inc[1] = inc_tune(HZ(455), p[0]);
    v->env[0] = ENV1;
    v->k[0] = dk(13 + p[1] * 20 / 127);
    v->x[0] = 32767 - p[2] * 150;
    v->x[1] = 8000 + p[2] * 150;
    v->x[2] = p[3];
    dsvf_coef(&v->c[0], CUT_600, 0);
}

#define DM_TOM_DEF {"TOM", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("NOISE", F_INT, 0, 127, 30), PD("DROP", F_INT, 0, 127, 50), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, tom_trigger, body_render}
#define DM_CONGA_DEF {"CONGA", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("SLAP", F_INT, 0, 127, 60), PD("DROP", F_INT, 0, 127, 30), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, conga_trigger, body_render}
#define DM_RIM_DEF {"RIM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("DRIVE", F_INT, 0, 127, 40), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, rim_trigger, rim_render}
#define DM_CLAVE_DEF {"CLAVE", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("CLICK", F_INT, 0, 127, 30), PD("DROP", F_INT, 0, 127, 20), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, clave_trigger, body_render}
```

- [ ] **Step 4: Register**

`dmodels.c`: add `#include "dm_perc.c"` after `dm_metal.c`; enum `{DM_K808, DM_K909, DM_S808, DM_S909, DM_C808, DM_C909, DM_HATC, DM_HATO, DM_CYMB, DM_COWB, DM_TOM, DM_CONGA, DM_RIM, DM_CLAVE, DM_SMPL, NMODELS}`; `DMODELS`/`N_MODEL` in that order. `drum_core.c` `KIT_DEF[5]` → `{DM_TOM, 45}`, `[6]` → `{DM_RIM, 37}`.

- [ ] **Step 5: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: all `ok`.

- [ ] **Step 6: Commit**

```bash
git add firmware/src/dm_perc.c firmware/src/dmodels.c firmware/src/drum_core.c tests/drum_test.c
git commit -m "models: tom, conga, rimshot, claves

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Sample layer checks

The layer code is in `drum_core.c` since Task 3; this task proves it on synth models.

**Files:**
- Modify: `tests/drum_test.c`

**Interfaces:**
- Consumes: `P_LSET`, `P_LKEY`, `P_LLEVEL`, `P_LTUNE`, `P_LDEC`, `drum_hit`, `track_render`.

- [ ] **Step 1: Write the tests** (append; register in `main`)

```c
static void test_layer(void)
{
    double a, b;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_hit(&trk[0], 127);
    check("layer: LLEVEL 0 starts no layer voice", !trk[0].lv[0].active && !trk[0].lv[1].active);
    render_track(&trk[0], wl, SECS(0.3));
    a = hf_of(wl, 0, SECS(0.3));
    host_init();
    drum_set_model(&trk[0], DM_K909);
    trk[0].p[P_LLEVEL] = 100;
    trk[0].p[P_LSET] = 0;                            /* PERC */
    trk[0].p[P_LKEY] = 38;                           /* the GM snare sample on top of the kick */
    drum_hit(&trk[0], 127);
    check("layer: a hit starts the model and the layer", trk[0].v[0].active && trk[0].lv[0].active);
    render_track(&trk[0], wl, SECS(6));
    b = hf_of(wl, 0, SECS(0.3));
    check("layer: adds the sample (more high-frequency energy)", b > 1.5 * a);
    check("layer: ends with the hit, no voice left", track_idle(&trk[0]));
    host_init();
    drum_set_model(&trk[0], DM_K909);
    trk[0].p[P_LLEVEL] = 100;
    trk[0].p[P_LSET] = SMP_NSETS;                    /* USR1, empty */
    drum_hit(&trk[0], 127);
    check("layer: an empty user slot adds nothing", !trk[0].lv[0].active && trk[0].v[0].active);
}
```
Register: `test_layer();`

- [ ] **Step 2: Run the tests**

Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: all `ok`. A failure here is a bug in Task 3's layer code: fix `drum_hit`/`track_render`, not the test.

- [ ] **Step 3: Commit**

```bash
git add tests/drum_test.c
git commit -m "tests: sample layer on synth models

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Kit demo, stress, extremes, cost, golden hashes, WAV renders

**Files:**
- Create: `tests/drumsim.c`, `tests/drum_golden.txt` (generated), `tests/drum_cost_ref.txt`
- Modify: `tests/drum_test.c`, `tests/run_drum_tests.sh`

**Interfaces:**
- Consumes: everything above.
- Produces: `build/host/drumsim OUTDIR` writes `OUTDIR/<MODEL>.wav` (4 hits at velocity 127/96/64/32, then TUNE −12/+12, DECAY 0/127, TONE 0/127, CHAR 0/127) and `OUTDIR/kit.wav` (8 bars of the demo pattern through the FX); `GOLDEN_UPDATE=1` rewrites `tests/drum_golden.txt`.

- [ ] **Step 1: Cost reference**

`tests/drum_cost_ref.txt`:
```
# host instructions per sample (proc_pid_rusage) of stock Felucca's heaviest mix
# cpu/mix/3parts_full_drums in tests/cpu_baseline.txt @ 1e838e1: it runs on the FM-1.
1566
```

- [ ] **Step 2: Write the tests** (append; register in `main`)

```c
static void test_extremes(void)
{
    static const int16_t V[] = {-24, 0, 127};
    uint32_t mi, k, j, ok = 1;
    for (mi = 0; mi < NMODELS; mi++)
        for (k = 0; k < 8; k++)
            for (j = 0; j < 3; j++) {
                const param_desc_t *d = &DMODELS[mi].edit[k];
                int32_t val = clamp(V[j], d->min, d->max);
                uint32_t e;
                host_init();
                drum_set_model(&trk[0], mi);
                trk[0].p[P_E0 + k] = (int16_t)val;
                drum_hit(&trk[0], 127);
                render_track(&trk[0], wl, SECS(7));
                e = end_of(wl, SECS(7));
                if (e >= SECS(6.5) || !track_idle(&trk[0]) || peak_of(wl, 0, SECS(7)) > 4 * VOICE_FS) {
                    printf("     %s %s=%d: end %.2f s peak %d\n", N_MODEL[mi], d->label, val, e / (double)FS,
                           peak_of(wl, 0, SECS(7)));
                    ok = 0;
                }
            }
    check("every model, every knob at min / 0 / max: bounded and ends", ok);
}

static void test_stress(void)
{
    uint32_t i, k, ok = 1;
    host_init();
    for (i = 0; i < NTRK; i++)
        drum_set_model(&trk[i], i % 2 ? DM_CYMB : DM_TOM);
    for (k = 0; k < 400; k++) {                      /* two hits per block on every track, 400 blocks */
        for (i = 0; i < NTRK; i++) {
            drum_hit(&trk[i], 127);
            drum_hit(&trk[i], 1 + k % 127);
        }
        render_mix(wl, wr, CTL);
        ok &= peak_of(wl, 0, CTL) <= 32767;
    }
    render_mix(wl, wr, SECS(7));
    for (i = 0; i < NTRK; i++)
        ok &= track_idle(&trk[i]);
    check("stress: double hits every block on 8 tracks: bounded, every voice ends", ok);
}

static void test_cost(void)
{
    FILE *f = fopen("tests/drum_cost_ref.txt", "r");
    char line[128];
    double ref = 0, ipc;
    uint64_t i0;
    uint32_t i;
    while (f && fgets(line, sizeof line, f))
        if (line[0] != '#')
            ref = atof(line);
    if (f)
        fclose(f);
    host_init();                                     /* worst case: 2-voice models + layers + every FX */
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        drum_set_model(t, i % 2 ? DM_CYMB : DM_TOM);
        t->p[P_LLEVEL] = 100;
        t->p[P_LKEY] = 38;
        t->p[P_DIST] = 60;
        t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 60;
        t->p[P_SLCR] = 1;
        t->p[P_SDIV] = 3;                            /* 1/32 */
        memset(t->step, 0, sizeof t->step);
        t->step[0].on = t->step[1].on = 1;
        t->p[P_SLEN] = 2;
    }
    song.g[G_BPM] = 240;
    transport_req = 1;
    render_mix(0, 0, SECS(0.5));                     /* every voice busy */
    i0 = instr_now();
    render_mix(wl, wr, SECS(4));
    ipc = i0 ? (double)(instr_now() - i0) / SECS(4) : 0;
    printf("     worst-case kit: %.0f host instructions / sample (reference %.0f)\n", ipc, ref);
    check("cost: worst-case kit within the stock Felucca reference", !i0 || ref == 0 || ipc <= ref);
}

static uint32_t fnv(const int32_t *x, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)x[i]) * 16777619u;
    return h;
}

static void test_golden(void)
{
    FILE *f;
    char name[32];
    unsigned want;
    uint32_t mi, ok = 1, have[NMODELS], upd = getenv("GOLDEN_UPDATE") != 0;
    for (mi = 0; mi < NMODELS; mi++) {
        hit_model(mi, 127, SECS(1));
        have[mi] = fnv(wl, SECS(1));
    }
    if (upd) {
        f = fopen("tests/drum_golden.txt", "w");
        for (mi = 0; mi < NMODELS; mi++)
            fprintf(f, "%s %08x\n", N_MODEL[mi], have[mi]);
        fclose(f);
        check("golden: tests/drum_golden.txt rewritten", 1);
        return;
    }
    f = fopen("tests/drum_golden.txt", "r");
    if (!f) {
        check("golden: tests/drum_golden.txt exists (GOLDEN_UPDATE=1 writes it)", 0);
        return;
    }
    while (fscanf(f, "%31s %x", name, &want) == 2)
        for (mi = 0; mi < NMODELS; mi++)
            if (!strcmp(name, N_MODEL[mi]) && have[mi] != want) {
                printf("     %s render changed: %08x, golden %08x\n", name, have[mi], want);
                ok = 0;
            }
    fclose(f);
    check("golden: every model's default render is unchanged", ok);
}
```
Register: `test_extremes(); test_stress(); test_cost(); test_golden();`

- [ ] **Step 3: `tests/drumsim.c`**

```c
/* WAV renders for listening: one file per model and the kit demo.  drumsim OUTDIR */
#include "drum_host.h"
#include <sys/stat.h>

static void wav_hdr(FILE *f, uint32_t frames)
{
    uint32_t v;
    uint16_t a = 1, ch = 2, ba = 4, bits = 16;
    uint32_t sr = FS, br = FS * 4;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}

static void wav_put(FILE *f, int32_t l, int32_t r)
{
    int16_t s[2] = {(int16_t)clamp(l, -32768, 32767), (int16_t)clamp(r, -32768, 32767)};
    fwrite(s, 2, 2, f);
}

#define GAP (FS * 6 / 10 / CTL * CTL)
static int32_t L[GAP], R[GAP];

static void hit_and_write(FILE *f, track_t *t, uint32_t vel, uint32_t *frames)
{
    uint32_t i;
    drum_hit(t, vel);
    render_mix(L, R, GAP);
    for (i = 0; i < GAP; i++)
        wav_put(f, L[i], R[i]);
    *frames += GAP;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/drum_renders";
    char path[256];
    uint32_t mi, frames, i, k;
    mkdir(dir, 0755);
    for (mi = 0; mi < NMODELS; mi++) {
        static const uint8_t VEL[4] = {127, 96, 64, 32};
        FILE *f;
        snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, mi, N_MODEL[mi]);
        f = fopen(path, "wb");
        wav_hdr(f, 0);
        frames = 0;
        host_init();
        drum_set_model(&trk[0], mi);
        for (i = 0; i < 4; i++)
            hit_and_write(f, &trk[0], VEL[i], &frames);
        for (k = 0; k < 4; k++) {                    /* TUNE, DECAY, TONE, CHAR at low then high */
            const param_desc_t *d = &DMODELS[mi].edit[k];
            int16_t keep = trk[0].p[P_E0 + k];
            trk[0].p[P_E0 + k] = k == 0 ? -12 : d->min;
            hit_and_write(f, &trk[0], 127, &frames);
            trk[0].p[P_E0 + k] = k == 0 ? 12 : d->max;
            hit_and_write(f, &trk[0], 127, &frames);
            trk[0].p[P_E0 + k] = keep;
        }
        fseek(f, 0, SEEK_SET);
        wav_hdr(f, frames);
        fclose(f);
        printf("drumsim: %s\n", path);
    }
    {
        static const char *const PAT[NTRK] = {
            "x...x...x...x..x", "....x.......x...", "....x.......x..x", "x.x.x.x.x.x.x.x.",
            "..x...x...x...x.", "......x....x....", ".x.....x..x.....", "x...............",
        };
        uint32_t total = 8 * 16 * (FS * 60 / 120 / 4) / CTL * CTL;
        FILE *f;
        snprintf(path, sizeof path, "%s/kit.wav", dir);
        f = fopen(path, "wb");
        wav_hdr(f, total);
        host_init();
        for (i = 0; i < NTRK; i++)
            for (k = 0; k < 16; k++)
                trk[i].step[k].on = PAT[i][k] == 'x';
        trk[1].p[P_REV] = 40;
        trk[2].p[P_REV] = 50;
        trk[3].p[P_DLY] = 25;
        transport_req = 1;
        for (frames = 0; frames < total; frames += GAP < total - frames ? GAP : total - frames) {
            uint32_t n = GAP < total - frames ? GAP : total - frames;
            render_mix(L, R, n);
            for (i = 0; i < n; i++)
                wav_put(f, L[i], R[i]);
        }
        fclose(f);
        printf("drumsim: %s\n", path);
    }
    return 0;
}
```

- [ ] **Step 4: Extend `tests/run_drum_tests.sh`**

After the `"$OUT/drum_test"` line add:
```sh
cc -O2 -Wall -Wno-unused-function -Ibuild/gen -Ifirmware/src -o "$OUT/drumsim" tests/drumsim.c -lm
"$OUT/drumsim" build/drum_renders >/dev/null && echo "renders: build/drum_renders"
```

- [ ] **Step 5: Create the golden file and run everything**

```bash
GOLDEN_UPDATE=1 PYTHON=.venv/bin/python sh tests/run_drum_tests.sh
PYTHON=.venv/bin/python sh tests/run_drum_tests.sh
ls build/drum_renders
```
Expected: second run all `ok` (golden compared, cost line printed and within reference), `ALL DRUM HOST TESTS PASSED`; 16 WAV files listed (15 models + `kit.wav`). If `test_cost` fails: measure each model alone (one track, both voices busy) and STOP — report the numbers to the user and let them choose (optimise the heaviest models, cap simultaneous voices, or accept it and rely on M1-B's overload shedding). Never raise the reference on your own.

- [ ] **Step 6: Commit**

```bash
git add tests/drum_test.c tests/drumsim.c tests/run_drum_tests.sh tests/drum_golden.txt tests/drum_cost_ref.txt
git commit -m "tests: extremes, stress, cost vs stock reference, golden hashes, WAV renders

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

- [ ] **Step 7: Send the renders for the listening review (spec §6.4)**

Send `build/drum_renders/kit.wav` and the 15 model files to the user with one line on what each file contains (4 velocities, then TUNE/DECAY/TONE/CHAR low→high). Sound changes requested by the user are made in the model files, then `GOLDEN_UPDATE=1` re-records the hashes in the same commit.
