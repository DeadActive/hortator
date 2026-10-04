# Device Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the drum firmware's boot path provably robust on a stock FM-1 (host checks H1–H5), add a safe start (SEQ held at power-on), and give the user a step-by-step device procedure with a rehearsed way back.

**Architecture:** The safe start lives in our code (`drum_boot_init()` in `ui.c`, called from `felucca_init()`), gating audio (`mix_block`), hits (`drum_hit`) and the UI. Host checks are a sanitizer-built boot test against a 1 MiB flash image (`tests/boot_test.c`), two scripts over the target disassembly (`tools/compare_upstream.py` against an upstream build made by `tools/upstream_build.sh`; `tools/stack_depth.py`), and cost checks in `tests/drum_test.c`. `docs/DEVICE_INSTALL.md` is the user's checklist.

**Tech Stack:** C (JieLi clang 4 target / Apple clang host with `-fsanitize=address,undefined`), Python 3 (stdlib only), POSIX sh, Docker (the existing build).

**Spec:** `docs/superpowers/specs/2026-10-05-device-readiness-design.md` (parent: `2026-10-05-drum-core-m1-design.md` §2).

## Global Constraints

- Never run anything that touches the device: no `tools/fm1_install.py`, no web installer, no M-VAVE updater; never install `mido` / `python-rtmidi`. Packages only with `DRUM_PACKAGE=1` (`build/felucca-UNTESTED.fwsc`).
- Frozen (checked by `tools/check_untouched.py`): `hal/`, `loader/`, `ota.c`, `usb.c`, `crt0.S`, `app.ld`, `storage.c`, `main.c` outside `felucca_init()` / the boot titles, the last 5 lines of `core.h`.
- No float in the firmware; no 64-by-64 divide (`__divdi3`): the app links no runtime library.
- Safe start: SEQ read in `felucca_init()` with the HAL's polled `fm1_input_scan()` for `FM1_DEBOUNCE + 4` frames (~7 ms), through the panel map (`panel.btn[B_SEQ]`). In safe mode: `mix_block` silence before any track/model/FX code; `drum_hit` starts nothing; user sample slots unusable; one static screen "SAFE START"; USB / editor / M-UPGRADE / UBOOT entries untouched.
- Pass limits: H3 deepest path ≤ 75 % of each stack (user 24,320 B, system 7,936 B), recursion fails; H4 idle audio ≤ upstream idle × 1.25 (upstream `cpu/mix/idle 286` → 357 host instructions / sample), `drum_boot_init` < 2,000,000 host instructions besides the scan's fixed waits, heaviest UI frame < 4,000,000 host instructions (the 8 s watchdog, 0x0D, at ≥ 50 M instructions / s, with 100× margin).
- Every new file: `/* SPDX-License-Identifier: GPL-3.0-only` + `Drum machine fork: 2026 DEADACTIVE */` (Python / sh: `#` comments).
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- User preference: when something can't be settled by tests, ask; any finding that the frozen code differs from upstream in the binary (H2) STOPS the work and goes to the user.

## Review Focus

1. **Safe start with a learned panel map.** A user who recalibrated the panel (OCT− + OCT+ at power-on) has a different `panel.btn[B_SEQ]`: SEQ must still be found through the map (Task 1 test with a permuted map).
2. **Hits from outside the keys in safe mode.** MIDI notes and the web editor's audition reach `drum_hit` without going through `mix_block`'s events: `drum_hit` itself must refuse (Task 1 test: `midi_in` note + direct `drum_hit`).
3. **A stock FM-1's bytes that happen to look valid.** M-VAVE data at 0xA0000 with a correct `FSMP` magic and nonsense zones, or a `PER2` record with a valid `PAN5` panel of out-of-range ids (Task 2 cases "valid headers, garbage fields").
4. **The upstream comparison hiding a real difference.** Normalising addresses must not normalise away a changed instruction: a self-test mutates one instruction of a frozen function and must see FAIL (Task 3).
5. **The stack tool missing a frame.** An unknown stack-pointer instruction form, or an unresolved call, must fail loudly, not be ignored (Task 4 self-test with a synthetic listing).

---

## File Structure

| File | Responsibility |
|---|---|
| `firmware/src/drum_core.c` | (edit) `safe_start` flag; `drum_hit` refuses in safe mode |
| `firmware/src/fx.c` | (edit) `mix_block` silence in safe mode |
| `firmware/src/ui.c` | (edit) `safe_start_check()`, `drum_boot_init()` |
| `firmware/src/ui_input.c`, `firmware/src/ui_draw.c` | (edit) safe mode: no input handling, LEDs off, the static screen |
| `firmware/src/main.c` | (edit inside `felucca_init()` only) call `drum_boot_init()` |
| `tests/ui_host.h` | (edit) `FELUCCA_FLASH` overridable; host `fm1_input_scan`, `FM1_DEBOUNCE`; flash glue when `FELUCCA_FLASH` |
| `tests/flash_host.h` | (new) host flash glue: `st_read/erase/prog` on the test's image, `fl_*` stubs, `storage.c` |
| `tests/boot_test.c` | (new) H1 + H5 boot runs under ASan/UBSan |
| `tests/ui_test.c` | (edit) safe-start UI tests, screenshot `safe_start.png` |
| `tests/drum_test.c`, `tests/drum_cost_ref.txt` | (edit) H4 cost checks, `upstream_idle 286` |
| `tools/upstream_build.sh` | (new) upstream 1e838e1 build in `build/upstream` (git worktree) |
| `tools/dis_parse.py` | (new) shared parser of the target `objdump -d` listing |
| `tools/compare_upstream.py` | (new) H2 |
| `tools/stack_depth.py` | (new) H3 |
| `tests/run_tests.sh`, `tests/run_drum_tests.sh` | (edit) run H1–H4 |
| `docs/DEVICE_INSTALL.md` | (new) the user's checklist |
| spec | (edit) H4 measured against upstream's recorded idle figure (Task 5) |

---

### Task 1: Safe start

**Files:**
- Modify: `firmware/src/drum_core.c`, `firmware/src/fx.c`, `firmware/src/ui.c`, `firmware/src/ui_input.c`, `firmware/src/ui_draw.c`, `firmware/src/main.c` (inside `felucca_init()`), `tests/ui_host.h`, `tests/ui_test.c`

**Interfaces:**
- Produces: `static uint8_t safe_start;` (drum_core.c), `static void safe_start_check(void)`, `static void drum_boot_init(void)` (ui.c). Task 2 calls `drum_boot_init()` as the boot does.

- [ ] **Step 1: Host stubs.** In `tests/ui_host.h`, replace `#define FELUCCA_FLASH 0` with
```c
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 0
#endif
```
and after `static void fm1_wdt_feed(void) {}` add
```c
#define FM1_DEBOUNCE 8u
static uint32_t host_scans;                          /* the boot's polled scans (tests: SEQ held = fm1_in.buttons) */
static void fm1_input_scan(void) { host_scans++; }
```

- [ ] **Step 2: Write the failing tests** — in `tests/ui_test.c` before `int main` (register `test_safe_start();` first in `main`):
```c
/* SEQ held at power-on: a silent start with USB intact (spec §4) */
static void safe_boot(int seq_held)
{
    ui_host_init();
    fm1_in.buttons = seq_held ? 1u << panel.btn[B_SEQ] : 0u;
    host_scans = 0;
    drum_boot_init();
    fm1_in.buttons = 0;
}

static void test_safe_start(void)
{
    uint32_t i, peak = 0, a;
    safe_boot(1);
    check("safe start: SEQ held through the boot's polled scan (FM1_DEBOUNCE + 4 scans)",
          safe_start && host_scans == FM1_DEBOUNCE + 4u);
    a = dvage;
    keys(1u << KEY_TRK_KEY[0]);                      /* a white key */
    ui_frame();
    keys(0);
    midi_in_q[mi_w % MQ] = 0x09u | (0x90u | 9u) << 8 | (uint32_t)trk[1].p[P_NOTE] << 16 | 100u << 24;
    mi_w++;                                          /* a USB-MIDI note on the drum channel (as drum_test's midi_in) */
    drum_hit(&trk[2], 127);                          /* the editor's audition path */
    for (i = 0; i < 40; i++) {
        ui_frame();
        peak |= (uint32_t)abs(mixo[0]) | (uint32_t)abs(mixo[1]);
    }
    check("safe start: keys, MIDI and direct hits start no voice; the mix is silent", dvage == a && peak == 0);
    snap_page("safe_start");
    check("safe start: the SAFE START screen draws", fb_lit(90, 160) > 200);
    for (i = 0; i < SMP_USER_SLOTS; i++)
        peak |= usr_nz[i];
    check("safe start: user sample slots unusable", peak == 0);
    safe_boot(0);
    check("normal start: SEQ not held: no safe mode, the same scans", !safe_start && host_scans == FM1_DEBOUNCE + 4u);
    {
        panel_t keep = panel;                        /* a recalibrated panel: SEQ on another matrix id */
        uint8_t t = panel.btn[B_SEQ];
        panel.btn[B_SEQ] = panel.btn[B_PLAY];
        panel.btn[B_PLAY] = t;
        safe_boot(1);
        check("safe start: SEQ found through a learned panel map", safe_start);
        panel = keep;
    }
    safe_boot(0);                                    /* leave the other tests a normal start */
}
```

- [ ] **Step 3: Run to verify it fails**
Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh`
Expected: FAIL — `drum_boot_init` / `safe_start` undeclared (compile error).

- [ ] **Step 4: Implement.**
In `firmware/src/drum_core.c` after `static uint32_t dvage;` add
```c
static uint8_t safe_start;                            /* SEQ held at power-on: no drum audio (drum_boot_init) */
```
and at the top of `drum_hit`'s body (`static void drum_hit(track_t *t, uint32_t vel)`):
```c
    if (safe_start)
        return;
```
In `firmware/src/fx.c` at the top of `mix_block`'s body:
```c
    if (safe_start) {                                 /* safe start: silence, no track / model / FX code */
        for (i = 0; i < n; i++)
            out[2u * i] = out[2u * i + 1u] = 0;
        return;
    }
```
In `firmware/src/ui.c` (after `track_select`):
```c
/* power-on, before audio_init (IRQs still off): SEQ held -> safe start. The HAL's polled scan
 * (TIMER4 waits, GPIO only) runs FM1_DEBOUNCE + 4 frames, ~7 ms */
static void safe_start_check(void)
{
    uint32_t k;
    for (k = 0; k < FM1_DEBOUNCE + 4u; k++)
        fm1_input_scan();
    safe_start = (uint8_t)((fm1_in.buttons >> panel.btn[B_SEQ]) & 1u);
    if (safe_start)
        for (k = 0; k < SMP_USER_SLOTS; k++)
            usr_nz[k] = 0;
}

/* felucca_init (main.c): everything the drum firmware sets up before audio and USB start */
static void drum_boot_init(void)
{
    safe_start_check();
    drum_tracks_init();
    ui.home = 1;
    ui.force = 1;
}
```
In `firmware/src/main.c` replace the body of `felucca_init()` (only it) with `drum_boot_init();`:
```c
static void felucca_init(void)
{
    drum_boot_init();
}
```
In `firmware/src/ui_input.c` at the top of `ui_input`'s body: `if (safe_start) return;`; at the top of `ui_leds`'s body:
```c
    if (safe_start) {                                 /* safe start: LEDs off */
        uint32_t c0;
        for (c0 = 0; c0 < FM1_NCOL; c0++)
            fm1_led[c0] = 0;
        return;
    }
```
In `firmware/src/ui_draw.c` at the top of `ui_draw`'s body:
```c
    if (safe_start) {                                 /* safe start: one static screen */
        static uint8_t drawn;
        if (!drawn) {
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 92, 240, &FONT_L, "SAFE START", C_HI, 1);
            draw_text_box(0, 128, 240, &FONT_S, "NO AUDIO - USB UPDATE READY", C_WHITE, 1);
            draw_text_box(0, 148, 240, &FONT_S, "POWER OFF TO LEAVE", C_GRAY, 1);
            drawn = 1;
        }
        return;
    }
```
(In the host, `drawn` stays 1 after the first safe screen; the tests take the screenshot in the first safe run.)

- [ ] **Step 5: Run the tests**
Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "safe|FAIL|PASSED"`
Expected: the five `safe start` / `normal start` checks ok, `ALL DRUM HOST TESTS PASSED`; `build/ui_shots/safe_start.png` shows the three lines. `python3 tools/check_untouched.py` → `check_untouched: ok`. Look at the screenshot (Read tool).

- [ ] **Step 6: Commit**
```bash
git add firmware/src/drum_core.c firmware/src/fx.c firmware/src/ui.c firmware/src/ui_input.c firmware/src/ui_draw.c firmware/src/main.c tests/ui_host.h tests/ui_test.c
git commit -m "safe start: SEQ held at power-on starts without drum audio (USB and updates as always)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: H1 — the boot path against hostile flash (and H5 in the boot)

**Files:**
- Create: `tests/flash_host.h`, `tests/boot_test.c`
- Modify: `tests/ui_host.h` (include the glue), `tests/run_drum_tests.sh`

**Interfaces:**
- Consumes: Task 1's `drum_boot_init`, `safe_start`; `persist_boot`, `settings_init`, `panel_init` (project.c / panel.c); `st_save`, `OBJ_SETTINGS`, `OBJ_PROJECT0` (storage.c); `PERSIST_MAGIC`, `PANEL_MAGIC`, `PROJ_MAGIC`, `project_t`, `proj_sum`, `SMP_USER_MAGIC`, `smp_user_hdr_t`.
- Produces: `build/host/boot_test`; its lines `boot: CASE (normal|safe): ok`.

- [ ] **Step 1: The host flash glue** — create `tests/flash_host.h`:
```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Host flash for tests/boot_test.c: the firmware's flash glue (felucca.c st_read / st_erase / st_prog, the
 * HAL entry points persist_boot calls) served from the test's 1 MiB image hflash (declared by the test). */
static uint8_t flash_ok;
#define FL_FAR(fn) (fn)
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's flash id */
static void fl_plain_window_init(void) {}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof hflash || n > sizeof hflash - off)
        return -1;
    memcpy(dst, hflash + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off > sizeof hflash - 4096u)
        return -8;
    memset(hflash + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off > sizeof hflash || n > sizeof hflash - off)
        return -8;
    for (i = 0; i < n; i++)
        hflash[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"
```
In `tests/ui_host.h`, just before the `#ifdef UI_NO_PROJECT` line, add:
```c
#if FELUCCA_FLASH
#include "flash_host.h"                               /* tests/boot_test.c: the boot path with flash */
#endif
```

- [ ] **Step 2: Write the boot test** — create `tests/boot_test.c`:
```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* H1 (spec docs/superpowers/specs/2026-10-05-device-readiness-design.md §3): the drum firmware's boot path,
 * main.c's order, with our real code, against hostile flash and .noinit contents, built with ASan + UBSan.
 * H5 in the boot: the same cases with SEQ held. A sanitizer report aborts the run (nonzero exit). */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t rs = 1;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static void rfill(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = (uint8_t)(rnd() >> 24);
}

/* a sample slot k with a valid header and random (or plausible) zones */
static void slot_header(uint32_t k, int plausible)
{
    smp_user_hdr_t h;
    uint32_t i;
    rfill(&h, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = (uint8_t)(1u + rnd() % 16u);
    h.data_len = plausible ? 0x8000u : rnd();
    for (i = 0; plausible && i < h.nz; i++) {
        smp_zone_t *z = &h.zone[i];
        z->off = 0;
        z->n = 1000u + rnd() % 30000u;
        z->idx = 0;
        z->rate = 1u << 16;
        z->ls = 0;
        z->le = z->n - 1u;
        z->lo = 0;
        z->hi = 127;
    }
    memcpy(hflash + 0xA0000u + k * 0x14000u, &h, sizeof h);
}

enum { F_ERASED, F_ZERO, F_RANDOM, F_HEADERS, F_FELUCCA, F_KINDS };
static const char *const KIND[F_KINDS] = {"erased", "zeros", "random", "valid headers, garbage fields", "felucca records"};

static void flash_image(int kind, uint32_t seed)
{
    rs = seed * 2654435761u + 7u;
    memset(hflash, kind == F_ZERO ? 0x00 : 0xFF, sizeof hflash);
    if (kind == F_RANDOM)
        rfill(hflash + 0x93000u, sizeof hflash - 0x93000u);   /* the data area (the app area is ours) */
    if (kind == F_HEADERS || kind == F_FELUCCA) {
        persist_t p;
        project_t pr;
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            slot_header(k, kind == F_FELUCCA);
        rfill(&p, sizeof p);
        p.magic = PERSIST_MAGIC;
        if (kind == F_FELUCCA)
            p.panel = PANEL_DEFAULT;
        else
            p.panel.magic = PANEL_MAGIC;              /* a valid panel magic over random ids */
        st_save(OBJ_SETTINGS, &p, sizeof p);
        for (k = 0; k < 4; k++) {
            rfill(&pr, sizeof pr);
            pr.magic = kind == F_FELUCCA ? 0x334E5546u : PROJ_MAGIC;   /* Felucca's "FUN3", or ours */
            pr.size = sizeof pr;
            pr.sum = proj_sum(&pr);                   /* ours: valid sum over random tracks */
            st_save(OBJ_PROJECT0 + k, &pr, sizeof pr);
        }
    }
}

static int32_t L[CTL], R[CTL];

/* main.c's order (fm1_main): persist_boot, settings_init, (LCD), input / ADC init, panel_init,
 * felucca_init = drum_boot_init, audio, USB, IRQs; then the main loop (UI + the update service) */
static int boot(int kind, uint32_t seed, int seq)
{
    uint32_t f, i, bounded = 1, peak = 0;
    flash_image(kind, seed);
    rfill(&settings, sizeof settings);               /* .noinit RAM after the stock firmware: anything */
    rfill(proj_slot, sizeof proj_slot);
    host_init();
    memset(&ui, 0, sizeof ui);
    persist_boot();
    settings_init();
    panel_init();
    fm1_in.buttons = seq ? 1u << panel.btn[B_SEQ] : 0u;
    drum_boot_init();
    fm1_in.buttons = 0;
    for (f = 0; f < 2u * FS; f += CTL) {             /* 2 s of the audio ISR */
        render_mix(L, R, CTL);
        for (i = 0; i < CTL; i++) {
            bounded &= L[i] <= 32767 && L[i] >= -32768 && R[i] <= 32767 && R[i] >= -32768;
            peak |= (uint32_t)abs(L[i]) | (uint32_t)abs(R[i]);
        }
    }
    for (i = 0; i < 30; i++)                         /* the main loop's first UI frames */
        ui_frame();
    for (i = 0; !seq && i < 4; i++) {                /* the first hands-on minutes: load each slot, play */
        project_load(i);
        transport_req = 1;
        render_mix(L, R, CTL * 64);
        ui_frame();
    }
    return bounded && (seq ? safe_start && peak == 0 : !safe_start);
}

int main(void)
{
    char what[96];
    int k, s, seq;
    for (k = 0; k < F_KINDS; k++)
        for (s = 0; s < (k == F_RANDOM || k == F_HEADERS ? 8 : 1); s++)
            for (seq = 0; seq < 2; seq++) {
                snprintf(what, sizeof what, "boot: %s #%d (%s)", KIND[k], s, seq ? "safe" : "normal");
                check(what, boot(k, (uint32_t)s + 1u, seq));
            }
    printf(fails ? "boot_test: %d FAILED\n" : "boot_test: all passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 3: Wire it** — in `tests/run_drum_tests.sh` after the `ui_test` lines add:
```sh
cc -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Wall -Wno-unused-function \
    -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/boot_test" tests/boot_test.c -lm
"$OUT/boot_test" > "$OUT/boot_test.txt" 2>&1 || { tail -30 "$OUT/boot_test.txt"; exit 1; }
tail -1 "$OUT/boot_test.txt"
```

- [ ] **Step 4: Run it**
Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "boot|FAIL|PASSED|ERROR|runtime error"`
Expected: `boot_test: all passed`, 38 cases (2 + 16 + 16 + 2 + 2, normal and safe). This is the first run of new checks against existing code: a sanitizer report or a FAIL is a **real finding** — use superpowers:systematic-debugging, find the cause in our code (or in the test's model of the boot: compare with `main.c`), fix test-first (a smaller case in `boot_test.c` that fails first), ledger it. A finding inside frozen code (`storage.c`, the HAL): STOP and ask the user.

- [ ] **Step 5: Commit**
```bash
git add tests/flash_host.h tests/boot_test.c tests/ui_host.h tests/run_drum_tests.sh
git commit -m "tests: H1 boot path against hostile flash and .noinit (ASan + UBSan), normal and safe start

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: H2 — the frozen code in the binary is upstream's

**Files:**
- Create: `tools/upstream_build.sh`, `tools/dis_parse.py`, `tools/compare_upstream.py`
- Modify: `tests/run_tests.sh`

**Interfaces:**
- Produces: `dis_parse.functions(path) -> dict[name] = list[(addr:int, text:str)]` (text = the instruction column, tabs stripped), `dis_parse.FUNC_RE`; `build/upstream/build/{felucca.dis,text.bin,data.bin,ramtext.bin}`; `compare_upstream.py OURS.dis UPSTREAM.dis` exit 0 / 1, and `--selftest`.

- [ ] **Step 1: The upstream build** — create `tools/upstream_build.sh` (`chmod +x`):
```sh
#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
# Builds upstream Felucca 1e838e1 (the origin of the frozen code) in build/upstream (a detached git
# worktree, ignored) with the same toolchain, for tools/compare_upstream.py and tools/stack_depth.py.
# Re-running is free when it is already built.
set -e
cd "$(dirname "$0")/.."
UP=build/upstream
REV=$(git rev-parse 1e838e1)
if [ -f "$UP/build/felucca.dis" ] && [ "$(git -C "$UP" rev-parse HEAD 2>/dev/null)" = "$REV" ]; then
    echo "upstream build: up to date ($UP)"
    exit 0
fi
git worktree remove --force "$UP" 2>/dev/null || rm -rf "$UP"
git worktree prune
git worktree add --detach "$UP" "$REV" >/dev/null
for i in 1 2; do                                     # Docker's first start after a pause can fail once
    (cd "$UP" && PYTHON="$PWD/../../.venv/bin/python" ./build.sh > build.log 2>&1) && break
done
[ -f "$UP/build/felucca.dis" ] || { tail -20 "$UP/build.log"; exit 1; }
echo "upstream build: $UP/build"
```
Run: `sh tools/upstream_build.sh` → `upstream build: build/upstream/build` (a few minutes the first time). `ls build/upstream/build/felucca.dis build/upstream/build/text.bin`.

- [ ] **Step 2: The listing parser** — create `tools/dis_parse.py`:
```python
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""Parser of the JieLi objdump -d listing (build/felucca.dis): functions and their instructions.
A function starts at a line "name:" in column 0; an instruction line is " ADDR:    BYTES \tTEXT";
"}" lines of the pretty-printer belong to the instruction before them and are kept as text."""
import re

FUNC_RE = re.compile(r"^([A-Za-z_.$][\w.$]*):$")
INSN_RE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")


def functions(path):
    funcs, cur = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        m = FUNC_RE.match(line)
        if m:
            cur = funcs.setdefault(m.group(1), [])
            continue
        m = INSN_RE.match(line)
        if m and cur is not None:
            cur.append((int(m.group(1), 16), m.group(2).replace("\t", " ").strip()))
        elif cur is not None and line.strip() == "}":
            cur.append((cur[-1][0] if cur else 0, "}"))
    return funcs
```

- [ ] **Step 3: Write the comparison with its self-test first** — create `tools/compare_upstream.py`:
```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""H2 (docs/superpowers/specs/2026-10-05-device-readiness-design.md): the machine code of the frozen code in
the drum build equals upstream Felucca's (tools/upstream_build.sh), addresses normalised.
  compare_upstream.py OURS.dis UPSTREAM.dis      compare_upstream.py --selftest OURS.dis UPSTREAM.dis
Frozen functions: every function defined in firmware/hal/*, firmware/src/{usb,ota,storage}.c, the asm entry
labels (crt0.S, hal/*.S), and fm1_cstart. One present upstream but missing here, or different: FAIL."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import dis_parse  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
C_FILES = sorted((ROOT / "firmware/hal").glob("*.h")) + [ROOT / "firmware/src" / f for f in ("usb.c", "ota.c", "storage.c")]
S_FILES = [ROOT / "firmware/crt0.S"] + sorted((ROOT / "firmware/hal").glob("*.S"))
CDEF = re.compile(r"^(?:static\s+|inline\s+|RAMFN\s+|FM1_INLINE\s+|RAMINL\s+|__attribute__\(\([^)]*\)\)\s+)*"
                  r"[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^;]*$")
ANNOT = re.compile(r"(-?\d+ )?<([^<>:]+?) : ([0-9a-f]+) >")
RAM = (0x01C00000, 0x01D00000)
XIP = (0x02000000, 0x02400000)


def frozen_names():
    names = {"fm1_cstart"}
    for f in C_FILES:
        for line in f.read_text(errors="replace").splitlines():
            m = CDEF.match(line)
            if m and m.group(1) not in ("if", "while", "for", "switch", "return", "sizeof"):
                names.add(m.group(1))
    for f in S_FILES:
        for line in f.read_text(errors="replace").splitlines():
            m = re.match(r"^\s*([A-Za-z_]\w*):", line) or re.match(r"^\s*FM1_ISR\s+([A-Za-z_]\w*)", line)
            if m:
                names.add(m.group(1))
    return names


def normalise(text, labels):
    """an annotation <sym+off : hex>: a function label keeps its name (+offset); an address in RAM or XIP
    that is not one (data, section ends) becomes <DATA>; anything else (SFRs, constants) keeps its value"""
    def sub(m):
        sym, val = m.group(2).strip(), int(m.group(3), 16)
        base = sym.split("+")[0]
        if base in labels:
            return "<" + sym + ">"
        if RAM[0] <= val < RAM[1] or XIP[0] <= val < XIP[1]:
            return "<DATA>"
        return "<" + format(val, "x") + ">"
    return ANNOT.sub(sub, text)


def body(funcs, name, labels):
    return [normalise(t, labels) for _, t in funcs[name]]


def compare(ours, up):
    labels = set(ours) | set(up)
    frozen = frozen_names()
    bad, same, only_ours = [], 0, []
    for name in sorted(frozen):
        if name not in up:
            if name in ours:
                only_ours.append(name)
            continue
        if name not in ours:
            bad.append(f"{name}: in upstream's binary, not in ours (inlined or removed)")
            continue
        a, b = body(ours, name, labels), body(up, name, labels)
        if a != b:
            diff = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
            bad.append(f"{name}: differs at instruction {diff}: ours {a[diff:diff+1]} upstream {b[diff:diff+1]}")
        else:
            same += 1
    return bad, same, only_ours


def main(argv):
    selftest = argv[:1] == ["--selftest"]
    if selftest:
        argv = argv[1:]
    ours, up = dis_parse.functions(argv[0]), dis_parse.functions(argv[1])
    if selftest:                                     # a changed instruction in a frozen function must FAIL
        victim = next(n for n in sorted(frozen_names()) if n in ours and n in up and len(ours[n]) > 3)
        a, t = ours[victim][2]
        ours[victim][2] = (a, t + " ; mutated")
        bad, _, _ = compare(ours, up)
        ok = any(b.startswith(victim + ":") for b in bad)
        print(f"compare_upstream selftest: a mutated {victim} is {'caught' if ok else 'NOT caught'}")
        return 0 if ok else 1
    bad, same, only_ours = compare(ours, up)
    for b in bad:
        print("FAIL  " + b)
    if only_ours:
        print("info  frozen helpers only in our binary (our code calls them now): " + ", ".join(only_ours))
    print(f"compare_upstream: {same} frozen functions identical to upstream, {len(bad)} different")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 4: Run the self-test, then the comparison**
Run: `python3 tools/compare_upstream.py --selftest build/felucca.dis build/upstream/build/felucca.dis`
Expected: `a mutated NAME is caught`, exit 0.
Run: `python3 tools/compare_upstream.py build/felucca.dis build/upstream/build/felucca.dis`
Expected: `N frozen functions identical to upstream, 0 different` (N in the dozens), maybe an `info` line naming `fm1_input_scan` (Task 1 calls it now).
If a function differs: read both listings. A difference that is only a data / section address the normaliser missed (e.g. a new annotation form): extend `normalise` for that form, add the form to the self-test (a second victim line built from it), ledger the ruling. Any other difference (an instruction, a register, a branch): **STOP and ask the user** — the frozen code would not be what runs on many FM-1s.

- [ ] **Step 5: Wire it** — in `tests/run_tests.sh` after the target-cost line:
```sh
if [ -f build/upstream/build/felucca.dis ]; then
    run "frozen code in the binary = upstream's (H2)" python3 tools/compare_upstream.py build/felucca.dis build/upstream/build/felucca.dis
    run "H2 self-test (a changed instruction is caught)" python3 tools/compare_upstream.py --selftest build/felucca.dis build/upstream/build/felucca.dis
else
    echo "== H2/H3 SKIPPED: no upstream build (sh tools/upstream_build.sh)"
    UPSKIP=" (H2/H3 SKIPPED: no upstream build)"
fi
```
and change the last line to `[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED$UPSKIP" || { echo "HOST TESTS FAILED"; exit 1; }` (add `UPSKIP=""` near `fail=0`).

- [ ] **Step 6: Commit**
```bash
git add tools/upstream_build.sh tools/dis_parse.py tools/compare_upstream.py tests/run_tests.sh
git commit -m "tools: H2 — the frozen code in the linked binary equals upstream Felucca's (normalised, self-tested)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: H3 — worst-case stack

**Files:**
- Create: `tools/stack_depth.py`
- Modify: `tests/run_tests.sh`

**Interfaces:**
- Consumes: `dis_parse.functions`, `FUNC_RE`; `build/{felucca.dis,text.bin,data.bin,ramtext.bin}` and the same in `build/upstream/build/`.
- Produces: `stack_depth.py DIR [--compare UPDIR]` → per root the deepest path and bytes; exit 1 over 75 % / recursion / an unknown stack form; `--selftest`.

- [ ] **Step 1: Write the tool with its self-test** — create `tools/stack_depth.py`:
```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""H3 (docs/superpowers/specs/2026-10-05-device-readiness-design.md): worst-case stack of the main path
(fm1_cstart, user stack) and of the interrupts (isr_timer5 + isr_alnk0 nested: timer5 runs below the audio
priority, the audio ISR can preempt it; system stack), from the target listing.
A function's frame: every push "[--sp] = {list}" (4 B per register) and every "sp += -N" in it (a
conservative sum). Calls: "call N <f ...>" and a "goto" into another function (a tail call). An indirect
"call rN": every .ram_text function (FL_FAR) and every address-taken .text function (its address in
text.bin / data.bin / ramtext.bin, or loaded by an instruction) that makes no indirect call itself.
Any other instruction that writes sp, a call that cannot be resolved, or recursion: FAIL.
  stack_depth.py BUILD_DIR [--compare UPSTREAM_BUILD_DIR]      stack_depth.py --selftest"""
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import dis_parse  # noqa: E402

USER_STACK = 0x01C7A000 - 0x01C74100                 # app.ld _ustack_lo .. _ustack_top
SYS_STACK = 0x01C7C000 - 0x01C7A100                  # _sstack_lo .. _sstack_top
LIMIT = 0.75
PUSH = re.compile(r"^\[--sp\] = \{([^}]*)\}$")
POP = re.compile(r"^\{[^}]*\} = \[sp\+\+\]$")
ADJ = re.compile(r"^sp \+= (-?\d+)$")
CALL = re.compile(r"^call -?\d+ <([^+ >]+)")
GOTO = re.compile(r"\bgoto -?\d+ <([^+ >]+)")
ICALL = re.compile(r"^call r\d+$")
LOAD = re.compile(r"<([^+ :>]+) : ([0-9a-f]+) >")
WRITES_SP = re.compile(r"(^|[^\w])sp\s*(\+|-)?=(?!=)")


class StackError(Exception):
    pass


def regs(lst):
    n = 0
    for part in lst.split(","):
        part = part.strip()
        m = re.match(r"r(\d+)-r(\d+)$", part)
        n += abs(int(m.group(1)) - int(m.group(2))) + 1 if m else 1
    return n


def frame(name, insns):
    size = 0
    for _, t in insns:
        m = PUSH.match(t)
        if m:
            size += 4 * regs(m.group(1))
            continue
        m = ADJ.match(t)
        if m:
            size += max(0, -int(m.group(1)))
            continue
        if POP.match(t) or ("sp" in t and not WRITES_SP.search(t)):
            continue
        if WRITES_SP.search(t):
            raise StackError(f"{name}: unknown stack instruction: {t}")
    return size


def address_taken(funcs, starts, blobs):
    taken = set()
    by_addr = {a: n for n, a in starts.items()}
    for blob in blobs:
        for i in range(0, len(blob) - 3, 2):
            v = struct.unpack_from("<I", blob, i)[0]
            if v in by_addr:
                taken.add(by_addr[v])
    for insns in funcs.values():
        for _, t in insns:
            if CALL.match(t):
                continue
            for m in LOAD.finditer(t):
                if m.group(1) in funcs and int(m.group(2), 16) == starts.get(m.group(1)):
                    taken.add(m.group(1))
    return taken


def analyse(funcs, blobs):
    starts = {n: ins[0][0] for n, ins in funcs.items() if ins}
    frames = {n: frame(n, ins) for n, ins in funcs.items()}
    indirect = {n for n, ins in funcs.items() if any(ICALL.match(t) for _, t in ins)}
    ram = {n for n, a in starts.items() if 0x01C00000 <= a < 0x01D00000}
    taken = address_taken(funcs, starts, blobs)
    targets = (ram | taken) - indirect
    edges = {}
    for n, ins in funcs.items():
        e = set()
        for _, t in ins:
            m = CALL.match(t)
            if m:
                if m.group(1) not in funcs:
                    raise StackError(f"{n}: call to an unknown function {m.group(1)}")
                e.add(m.group(1))
            m = GOTO.search(t)
            if m and m.group(1) != n and m.group(1) in funcs:
                e.add(m.group(1))
            if ICALL.match(t):
                e |= targets
        edges[n] = e
    memo, onpath = {}, set()

    def deep(n):
        if n in memo:
            return memo[n]
        if n in onpath:
            raise StackError(f"recursion through {n}")
        onpath.add(n)
        best, path = 0, []
        for c in edges.get(n, ()):
            d, p = deep(c)
            if d > best:
                best, path = d, p
        onpath.discard(n)
        memo[n] = (frames[n] + best, [n] + path)
        return memo[n]
    return deep


def report(build):
    d = Path(build)
    funcs = dis_parse.functions(d / "felucca.dis")
    blobs = [(d / f).read_bytes() for f in ("text.bin", "data.bin", "ramtext.bin") if (d / f).exists()]
    deep = analyse(funcs, blobs)
    main_b, main_p = deep("fm1_cstart")
    t5_b, t5_p = deep("isr_timer5")
    au_b, au_p = deep("isr_alnk0")
    return main_b, main_p, t5_b + au_b, (t5_p, au_p)


def selftest():
    lines = ["a:", " 1000:    00 00             \t[--sp] = {rets, r5-r4}", " 1002:    00 00             \tsp += -16",
             " 1004:    00 00             \tcall 2 <b : 1010 >", "b:", " 1010:    00 00             \tsp = r3"]
    p = Path("build/host/stack_selftest.dis")
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("\n".join(lines) + "\n")
    try:
        analyse(dis_parse.functions(p), [])
    except StackError as e:
        print(f"stack_depth selftest: an unknown stack form is caught ({e})")
        return 0
    print("stack_depth selftest: an unknown stack form was NOT caught")
    return 1


def main(argv):
    if argv[:1] == ["--selftest"]:
        return selftest()
    try:
        mb, mp, sb, sp = report(argv[0])
    except StackError as e:
        print("FAIL  " + str(e))
        return 1
    print(f"stack: main path {mb} B of {USER_STACK} ({100 * mb / USER_STACK:.0f} %): {' > '.join(mp[:12])}")
    print(f"stack: interrupts {sb} B of {SYS_STACK} ({100 * sb / SYS_STACK:.0f} %): timer5 {' > '.join(sp[0][:6])}; "
          f"audio {' > '.join(sp[1][:10])}")
    if len(argv) > 2 and argv[1] == "--compare":
        try:
            ub, _, us, _ = report(argv[2])
            print(f"stack: upstream Felucca: main {ub} B, interrupts {us} B")
        except StackError as e:
            print("info  upstream not analysed: " + str(e))
    ok = mb <= LIMIT * USER_STACK and sb <= LIMIT * SYS_STACK
    print("stack: within 75 % of both stacks" if ok else "FAIL  stack: over 75 % of a stack")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```
(Self-calls stay in `edges`, so direct recursion is caught.)

- [ ] **Step 2: Run the self-test**
Run: `python3 tools/stack_depth.py --selftest`
Expected: `an unknown stack form is caught (b: unknown stack instruction: sp = r3)`.

- [ ] **Step 3: Run on both builds**
Run: `python3 tools/stack_depth.py build --compare build/upstream/build`
Expected: two `stack:` lines with paths, the upstream line, `within 75 % of both stacks`. If it FAILS on an unknown stack form that is a real instruction form of this compiler (e.g. `sp += r`?): read the listing, add the form to `frame` with its true size (or as unbounded = FAIL if it is dynamic allocation), extend the self-test, ledger it. If a real path exceeds 75 %: STOP and report the path to the user. If upstream itself cannot be analysed for a reason ours can: the `info` line is enough (it is a comparison only).

- [ ] **Step 4: Wire it** — in `tests/run_tests.sh` inside the `if [ -f build/upstream/build/felucca.dis ]` branch add:
```sh
    run "worst-case stack (H3)" python3 tools/stack_depth.py build --compare build/upstream/build
```
and after that `if/else` block (it does not need upstream):
```sh
run "H3 self-test (an unknown stack form is caught)" python3 tools/stack_depth.py --selftest
```

- [ ] **Step 5: Commit**
```bash
git add tools/stack_depth.py tests/run_tests.sh
git commit -m "tools: H3 — worst-case stack of the main path and the interrupts from the target listing

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: H4 — the first seconds' cost

**Files:**
- Modify: `tests/drum_test.c`, `tests/drum_cost_ref.txt`, the spec (§3 H4)

**Interfaces:**
- Consumes: `instr_now()`, `cost_ref(key)` (drum_test.c), `drum_boot_init` (Task 1) — drum_test.c does not include the UI, so the boot's cost is measured on `drum_tracks_init()` (the rest of `drum_boot_init` is the scan's fixed waits and two assignments).

- [ ] **Step 1: Write the failing test** — in `tests/drum_test.c` before `int main` (register `test_boot_cost();` after `test_cost();`):
```c
/* H4: the first seconds cost no more than upstream's (spec: device readiness) */
static void test_boot_cost(void)
{
    double up = cost_ref("upstream_idle"), idle;
    uint64_t i0, init;
#ifdef DM_QCHECK
    return;
#endif
    i0 = instr_now();
    host_init();                                     /* drum_tracks_init and the power-on state */
    init = instr_now() - i0;
    render_mix(0, 0, SECS(0.5));
    i0 = instr_now();
    render_mix(0, 0, SECS(4));
    idle = i0 ? (double)(instr_now() - i0) / SECS(4) : 0;
    printf("     boot: power-on init %llu host instructions; idle audio %.0f / sample (upstream idle %.0f)\n",
           (unsigned long long)init, idle, up);
    check("boot cost: power-on init under 2,000,000 host instructions", !i0 || init < 2000000u);
    check("boot cost: idle audio within 1.25 x upstream's idle (tests/drum_cost_ref.txt upstream_idle)",
          !i0 || (up > 0 && idle <= 1.25 * up));
}
```

- [ ] **Step 2: Run to verify it fails**
Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "boot|FAIL"`
Expected: FAIL `boot cost: idle audio within 1.25 x upstream's idle` (no `upstream_idle` in the reference file yet: `upstream idle 0`).

- [ ] **Step 3: The reference** — append to `tests/drum_cost_ref.txt`:
```
# upstream Felucca's idle mix (its tests/cpu_baseline.txt, cpu/mix/idle, host instructions / sample): the
# first seconds of the drum firmware stay within 1.25 x of it (device-readiness spec H4)
upstream_idle 286
```
Run: `PYTHON=.venv/bin/python sh tests/run_drum_tests.sh 2>&1 | grep -E "boot|FAIL|PASSED"`
Expected: `boot: power-on init N … idle audio ~254 / sample (upstream idle 286)`, both checks ok (research: idle 254 on this Mac).

- [ ] **Step 4: The heaviest UI frame** — in `tests/ui_test.c` before `int main` (register `test_ui_frame_cost();` last):
```c
/* H4: the heaviest page's frame (TRACKS while playing, 8 rows) is far inside the 8 s watchdog */
static void test_ui_frame_cost(void)
{
    uint64_t i0, worst = 0, c;
    uint32_t i;
    ui_host_init();
    kit_session();
    transport_req = 1;
    press(B_REC);                                    /* REC tap on HOME: the TRACKS mixer */
    ui_frame();
    release_all();
    for (i = 0; i < 60; i++) {
        i0 = instr_now();
        ui_input();
        ui_leds();
        ui_draw();
        c = instr_now() - i0;
        worst = c > worst ? c : worst;
        render_mix(0, 0, CTL);
    }
    printf("     ui: heaviest frame %llu host instructions (TRACKS, playing)\n", (unsigned long long)worst);
    check("ui frame cost: under 4,000,000 host instructions (8 s watchdog, 100x margin)", !i0 || worst < 4000000u);
}
```
Run the suite; expected: the `ui:` line and the check ok.

- [ ] **Step 5: Amend the spec** — in `docs/superpowers/specs/2026-10-05-device-readiness-design.md` §3, replace the H4 paragraph with:
```
**H4 — the first seconds' cost** (`tests/drum_test.c`, `tests/ui_test.c`). On the host (instruction counts):
(a) the power-on init (`drum_tracks_init`; the safe-start scan adds only its fixed ~7 ms of waits) under
2,000,000 instructions; (b) the audio with no voice sounding (the power-on kit, stopped) within 1.25 × upstream
Felucca's own idle figure — `cpu/mix/idle 286` in upstream's `tests/cpu_baseline.txt`, measured by its
`regress.c` the same way (instead of rebuilding upstream's host harness; amended in the plan); (c) the heaviest
UI frame (TRACKS while playing) under 4,000,000 instructions: the main loop feeds the 8 s watchdog (0x0D) every
frame, and the device runs ≥ 50 M instructions / s, a 100× margin. Upstream recorded no UI-frame figure.
```

- [ ] **Step 6: Commit**
```bash
git add tests/drum_test.c tests/drum_cost_ref.txt tests/ui_test.c docs/superpowers/specs/2026-10-05-device-readiness-design.md
git commit -m "tests: H4 — power-on init, idle audio (vs upstream's 286) and the heaviest UI frame cost

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The device procedure

**Files:**
- Create: `docs/DEVICE_INSTALL.md`

- [ ] **Step 1: Write the guide** — create `docs/DEVICE_INSTALL.md`:
```markdown
# Installing the drum firmware on an FM-1 (step by step)

Do the steps in order. **Stop at the first step that does not go as written**, go back to stock (step 6) if the
drum firmware is installed, and report what you saw. Nothing here needs extra hardware.

What protects you: the update path (USB-MIDI), the start-up code and the update loader are byte-for-byte
upstream Felucca's (checked in the binary, H2); the FM-1's built-in bootloader cannot be erased. The one case that
needs extra hardware is a crash on every boot before USB answers — the host tests (H1–H4) are there to rule it out.

## 0. Prepare the way back (before anything else)
1. Download M-VAVE's official FM-1 firmware updater and the stock firmware from M-VAVE's website into a folder
   outside this repo. Keep them.
2. Note the version your FM-1 shows (M-VAVE's updater shows it when connected).
3. Charge the FM-1. Updates run on its battery; don't install on a low battery.
4. Use a USB data cable you have used with the FM-1 before.

## 1. Rehearse going back (no risk)
Reinstall the **same** stock version with M-VAVE's updater.
- Expected: the update finishes, the FM-1 restarts and works as before.
- This proves the updater, your computer and your cable work together.

## 2. Rehearse a round trip with known-good firmware
1. Install the current upstream Felucca from its web installer: https://hugelton.github.io/Felucca/ (Chrome or
   Edge). Expected: it starts and plays sound.
2. Go back to stock with M-VAVE's updater (as in step 1). Expected: stock firmware, working.

Both directions now work on your FM-1, with firmware many people run.

## 3. Install the drum build
1. In this repo: `git rev-parse --short HEAD` and `shasum -a 256 build/felucca-UNTESTED.fwsc` — note both.
   The build must come from `DRUM_PACKAGE=1 ./build.sh` with `tests/run_tests.sh` ending in
   `ALL HOST TESTS PASSED` (no `SKIPPED`).
2. The installer needs `mido` and `python-rtmidi` in your own Python environment
   (`python3 -m venv ~/fm1-venv && ~/fm1-venv/bin/pip install mido python-rtmidi`).
3. Connect the FM-1 (powered on, stock or Felucca) and run:
   `~/fm1-venv/bin/python tools/fm1_install.py build/felucca-UNTESTED.fwsc`
   Expected: it finds the FM-1, asks to confirm, writes, and the FM-1 restarts.

## 4. First boot (in this order)
1. The start screen reads **FM-1 DRUMS** / DRUM MACHINE (UNTESTED), then the HOME screen appears.
2. The 8 white keys F3..F4 play the 8 tracks (kick, snare, clap, hats, ...).
3. **The update path from the drum firmware:** run the step-3 command again (the same file).
   Expected: it installs and the FM-1 restarts into the drum firmware.
   If this fails: go back to stock at once (step 6) while the drum firmware still runs.
4. **Safe start:** power off; hold **SEQ**; power on; keep holding until the screen shows **SAFE START**
   ("NO AUDIO - USB UPDATE READY"). Power off to leave it.

## 5. Hands-on
For each: what you should see / hear. Note anything different (page, knob, what happened).
- Every page: SOUND 1/2 and 2/2, TRACK, MIDI, LAYER, FX, SLICER, DLY, REV/CHO, STEP, PATTERN, GLOBAL, SYSTEM,
  PROJECT, TOOLS, TRACKS — each draws like the screenshots in `build/ui_shots/` (and `engine_screens/`).
- Every engine: PRESET on HOME steps through the 21 engines; each plays on its key.
- The demo kits: build the kit_808 pattern (see `m1c_listen/kits/README.txt`) on the STEP grid and play it.
- Live recording: arm a track (REC on TRACKS), play keys while running: the steps appear.
- PROJECT: save to slot 1, change things, load slot 1: everything comes back.
- The master knob changes the volume; the battery icon shows a level.

## 6. Going back to stock (any time the drum firmware runs)
M-VAVE's updater, as in step 1. Expected: stock firmware.

## 7. If the FM-1 shows nothing and USB does not answer
After two crashes within 30 s of power-on, the firmware enters the FM-1's built-in bootloader on purpose. Power
off and on once: if it starts the drum firmware, go to step 6. If not, the FM-1 is safe but needs one of:
- an RP2040 board (Seeed XIAO RP2040) with FM-1-transporter: https://github.com/kurogedelic/FM-1-transporter
  (3 wires to the FM-1's USB D+ / D− / GND, no opening); it writes the stock firmware back;
- a Linux PC with jl-uboot-tool over the FM-1's own USB cable.
Report what you saw first; we'll go through it together.
```

- [ ] **Step 2: Check the links and commands** — `grep -n "http" docs/DEVICE_INSTALL.md`: only the two URLs above (the README's web installer, the transporter repo). `grep -n "fm1_install.py" tools/fm1_install.py | head -2` shows the usage line matches (`fm1_install.py PACKAGE.fwsc`).

- [ ] **Step 3: Commit**
```bash
git add docs/DEVICE_INSTALL.md
git commit -m "docs: DEVICE_INSTALL.md — the user's step-by-step install with a rehearsed way back

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Integration — the real build, everything green

- [ ] **Step 1: Build and test**
```bash
for i in 1 2; do DRUM_PACKAGE=1 PYTHON=.venv/bin/python ./build.sh > build/dr-build.log 2>&1 && break; done
grep -E " ok |^app|^package|error|warning" build/dr-build.log
sh tools/upstream_build.sh
python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt | grep -E "FAIL|irq"
PATH="$PWD/.venv/bin:$PATH" PYTHON=.venv/bin/python sh tests/run_tests.sh > build/dr-tests.log 2>&1; grep -E "^==|FAIL|PASSED|SKIPPED|stack|compare_upstream" build/dr-tests.log
```
Expected: links, no warnings (the polled scan adds no helper call); `fm1_alnk0_irq` within budget (the safe-start test at the top of `mix_block` is one load and branch; if `BUDGET` flags it, `BUDGET_UPDATE=1` and ledger the new figure); the H2 line `0 different`, H3 within 75 % with upstream's figures shown; `ALL HOST TESTS PASSED` without `SKIPPED`.

- [ ] **Step 2: Report for the user** — the H2 count, H3 paths and bytes (ours / upstream), H4 figures, H1 case count, the safe-start screenshot, the package checksum and commit; ask the user to read `docs/DEVICE_INSTALL.md` before doing anything with the FM-1.
```
