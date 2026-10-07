# Sound pack (BASS+, SPRING reverb, slow divisions) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port upstream Felucca 1.0.2's BASS+ speaker EQ, SPRING reverb and slow divisions (1/2, 1/1, 2BAR, 4BAR)
into the drum firmware, checked sample for sample against upstream's code, with today's sound kept bit for bit.

**Architecture:** BASS+ is a third MENU speaker-EQ setting feeding upstream's `spk_bass` into `master_out`. SPRING is
upstream's `rev_spring` beside today's room reverb (moved into its own `rev_room`), sharing ROOM's buffers, chosen
by a new per-project global `G_RTYPE` (project format FDR7) on a new REVERB page. The slow divisions extend `N_DIV`
and `div_samples`, with a display order so the knobs run by length.

**Tech Stack:** C (one translation unit `firmware/src/felucca.c`, host tests with `cc`), Python 3 tools, the pi32v2
toolchain in Docker via `./build.sh`.

**Spec:** `docs/superpowers/specs/2026-10-06-sound-pack-design.md`

## Global Constraints

- Repo `/Users/evech/Desktop/dev/fm1-drummachine/felucca`, branch `sound-pack` (from `trs-midi`). Never push (the
  only remote is upstream Felucca). Never install anything on the FM-1; never run `tools/fm1_install.py`, the web
  installer or the M-VAVE updater; never install mido / python-rtmidi.
- Environment for builds and tests:
  `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"`.
  Docker running (`open -a Docker`); retry a build up to 5× on "exec format error".
- No frozen file changes (hal/, loader/, ota.c, usb.c, usb_app.c, crt0.S, app.ld, storage.c, main.c, core.h's last
  5 lines). `tools/check_untouched.py` must stay ok.
- New small globals can break H2: put new state in existing structs (`fx`), add no new small statics.
- Ask the user before any fidelity-metric, budget or threshold change (the target budget file included).
- The user verifies by ear and eye: WAVs and screenshots.
- Credit the fork as DEADACTIVE in new files.
- Do not wrap test runs in `sh -c '...'`.
- After every firmware change that is committed: `DRUM_PACKAGE=1 ./build.sh` at the end of the task that changes
  firmware (package + `build/site`); checklists have no "install again" step.
- CHANGELOG lines under `## Unreleased`; the bump to 0.7.0 and the tag `drum-v0.7.0` happen at the merge.
- Host test compile line (drum suite style), used below as `CCH`:
  `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src`.

## Review Focus

1. Switching reverb TYPE while sends keep coming (not only a tail): no click, no stuck noise (Task 3 test
   `switch_while_sending`).
2. Loading an FDR6 project while SPRING runs: the reverb goes back to ROOM through the fade, no garbage
   (Task 3 test `fdr6_converts` checks TYPE; `switch_while_sending` covers the fade path).
3. Changing DIV from 1/16 to 4BAR and back while playing: the track keeps playing, no lost / hung step
   (Task 5 test `div_change_while_playing`).
4. A BPM change in the middle of a 4BAR step: the next step comes at the new length, not never
   (Task 5 test `bpm_change_in_slow_step`).
5. Turning SIZE while SPRING rings: the loop length glides like upstream's, output bounded (Task 3 test
   `spring_matches_upstream` changes SIZE and DAMP mid-run).

---

### Task 1: Today's sound, pinned bit for bit

**Files:**
- Create: `tests/sound_pack_test.c`
- Modify: `tests/run_drum_tests.sh` (build and run it after `drum_test`)

**Interfaces:**
- Produces: `tests/sound_pack_test.c` with `check()`, `fnv()`, `kit_hash(uint32_t lowcut)`, constants `HASH_FLAT`,
  `HASH_LOWCUT`; later tasks add cases to it.

- [ ] **Step 1: The test with a recorder**

Create `tests/sound_pack_test.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Sound pack (docs/superpowers/specs/2026-10-06-sound-pack-design.md): BASS+, SPRING and the slow divisions against
 * upstream 1.0.2's code (sound_pack_ref.h), and today's sound kept bit for bit: HASH_FLAT / HASH_LOWCUT are the
 * kit below rendered by the build before the sound pack (trs-midi 08866e0 .. bf98902). */
#include "drum_host.h"
#include <stdlib.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t fnv(const int32_t *x, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)x[i]) * 16777619u;
    return h;
}

#define KIT_N (2u * 16u * (FS * 60 / 120 / 4) / CTL * CTL)   /* 2 bars at 120 BPM */
static int32_t KL[KIT_N], KR[KIT_N];
/* kick, snare (REV), clap (REV + CHO), hats (DLY), 2 bars, every bus and the master in use */
static uint32_t kit_hash(uint32_t lowcut)
{
    uint32_t i, k;
    host_init();
    fx_lowcut = (uint8_t)lowcut;
    for (i = 0; i < 4u; i++)
        for (k = 0; k < 16u; k++)
            trk[i].step[k].on = k % (i + 2u) == 0;
    trk[1].p[P_REV] = 90;
    trk[2].p[P_REV] = 60;
    trk[2].p[P_CHOR] = 70;
    trk[3].p[P_DLY] = 80;
    transport_req = 1;
    render_mix(KL, KR, KIT_N);
    return fnv(KL, KIT_N) ^ fnv(KR, KIT_N) * 31u;
}

#define HASH_FLAT 0x00000000u
#define HASH_LOWCUT 0x00000000u

int main(void)
{
    if (getenv("SOUND_PACK_RECORD")) {
        printf("HASH_FLAT 0x%08xu\nHASH_LOWCUT 0x%08xu\n", kit_hash(0), kit_hash(1));
        return 0;
    }
    check("today's sound: the kit (ROOM reverb, delay, chorus) with FLAT, bit for bit", kit_hash(0) == HASH_FLAT);
    check("today's sound: the kit with LOWCUT, bit for bit", kit_hash(1) == HASH_LOWCUT);
    printf(fails ? "sound_pack_test: %d FAILED\n" : "sound_pack_test: all passed\n", fails);
    return fails ? 1 : 0;
}
```

- [ ] **Step 2: Record today's hashes**

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; python3 tools/build.py --gen-only >/dev/null && cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/sound_pack_test tests/sound_pack_test.c -lm && SOUND_PACK_RECORD=1 build/host/sound_pack_test && build/host/sound_pack_test`
Expected: two `HASH_... 0x........u` lines (two different values), then both checks FAIL (the constants are 0):
the test can fail.

- [ ] **Step 3: Pin them**

Replace the two `0x00000000u` with the printed values. Run the same command without the recorder:
`cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/sound_pack_test tests/sound_pack_test.c -lm && build/host/sound_pack_test`
Expected: both `ok`, `sound_pack_test: all passed`.

- [ ] **Step 4: In the drum suite**

In `tests/run_drum_tests.sh`, after the line `"$OUT/drum_test"` (line 10), add:

```sh
cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o "$OUT/sound_pack_test" tests/sound_pack_test.c -lm
"$OUT/sound_pack_test"
```

Run: `sh tests/run_drum_tests.sh 2>&1 | grep -E "sound_pack_test|ALL DRUM"`
Expected: `sound_pack_test: all passed`, `ALL DRUM HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add tests/sound_pack_test.c tests/run_drum_tests.sh
git commit -m "tests: the sound pack's test, today's kit pinned bit for bit (FLAT and LOWCUT, ROOM / delay / chorus)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

---

### Task 2: BASS+ (SPEAKER EQ)

**Files:**
- Create: `tests/sound_pack_ref.h` (upstream's `spk_bass`; Task 3 adds `rev_spring`)
- Modify: `firmware/src/fx.c` (`fx` struct, `fx_lowcut` comment, `lowcut1`, `spk_bass`, `master_out`)
- Modify: `firmware/src/panel.c:72-86` (`settings_init`)
- Modify: `firmware/src/ui_menu.c` (SPEAKER EQ item, value column, input)
- Modify: `tests/sound_pack_test.c`, `tests/ui_test.c`

**Interfaces:**
- Consumes: Task 1's `check`, `fnv`, `kit_hash`, `HASH_FLAT`, `HASH_LOWCUT`.
- Produces: `static inline int32_t spk_bass(int32_t m)`; `lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)`;
  `fx_lowcut` 0 FLAT / 1 LOWCUT / 2 BASS+; `fx.sb_lp1 .. fx.sb_hl`; menu enum `MI_SPKEQ` (was `MI_LOWCUT`);
  `ref_spk_bass(int32_t)`, `ref_bass_reset(void)` in `tests/sound_pack_ref.h`.

- [ ] **Step 1: The reference**

Create `tests/sound_pack_ref.h`:

```c
/* Upstream Felucca 1.0.2 (db70550) firmware/src/fx.c, the reference the sound pack's port is checked against
 * (tests/sound_pack_test.c). The arithmetic is upstream's text; only the state's names (ref_*) differ, and
 * rev_spring (below) takes SIZE / DAMP as arguments and has its own buffers, so the reference runs beside the port.
 * Drum machine fork: 2026 DEADACTIVE */
static int32_t ref_lp1, ref_lp2, ref_lp3, ref_lp4, ref_env, ref_h1, ref_h2, ref_hl;
static int32_t ref_spk_bass(int32_t m)
{
    int32_t a, t, u;
    ref_lp1 += ((m - ref_lp1) * 692) >> 15;
    ref_lp2 += ((ref_lp1 - ref_lp2) * 692) >> 15;
    ref_lp3 += ((ref_lp2 - ref_lp3) * 692) >> 15;
    ref_lp4 += ((ref_lp3 - ref_lp4) * 692) >> 15;
    a = ref_lp4 < 0 ? -ref_lp4 : ref_lp4;
    if (a > ref_env)
        ref_env += (a - ref_env) >> 2;
    else if (ref_env > 0)
        ref_env -= (ref_env >> 11) + 1;
    t = clamp(ref_lp4 * 8, -ref_env, ref_env);
    ref_h1 += (t - ref_h1) >> 5;
    u = t - ref_h1;
    ref_h2 += (u - ref_h2) >> 5;
    u -= ref_h2;
    ref_hl += (u - ref_hl) >> 3;
    return ref_hl * 3;
}
static void ref_bass_reset(void) { ref_lp1 = ref_lp2 = ref_lp3 = ref_lp4 = ref_env = ref_h1 = ref_h2 = ref_hl = 0; }
```

- [ ] **Step 2: The BASS+ tests**

In `tests/sound_pack_test.c` add `#include <math.h>` and `#include "sound_pack_ref.h"` after `#include <stdlib.h>`,
and before `#define HASH_FLAT` add:

```c
static double sine(double f, uint32_t i, double amp) { return amp * sin(2.0 * M_PI * f * i / FS); }

/* spk_bass = upstream's, sample for sample: a 55 Hz tone switching on and off every 1/4 s, plus noise, 4 s */
static void test_bass_port(void)
{
    uint32_t i, rng = 1, ok = 1;
    host_init();
    ref_bass_reset();
    for (i = 0; i < 4u * FS; i++) {
        int32_t x;
        rng = rng * 1664525u + 1013904223u;
        x = (int32_t)(sine(55.0, i, 12000.0) * (double)((i / (FS / 4u)) & 1u)) + (int32_t)(rng >> 20) - 2048;
        ok &= spk_bass(x) == ref_spk_bass(x);
    }
    check("BASS+: spk_bass gives upstream 1.0.2's samples (tone bursts + noise, 4 s)", ok);
}

/* #42: BASS+ (its 220 Hz high-pass + spk_bass) against the same high-pass alone, 300..600 Hz: within 0.5 dB */
static void test_bass_mids(void)
{
    uint32_t ok = 1;
    double f;
    for (f = 300.0; f <= 600.0; f += 50.0) {
        int32_t l1 = 0, l2 = 0, e1 = 0, e2 = 0, m1 = 0, m2 = 0, f1 = 0, f2 = 0;
        double s0 = 0, s1 = 0, db;
        uint32_t i;
        host_init();
        for (i = 0; i < 2u * FS; i++) {
            int32_t x = (int32_t)sine(f, i, 8000.0), b = spk_bass(x);
            int32_t y1 = lowcut1(lowcut1(x, &l1, &e1, 5), &l2, &e2, 5) + b;
            int32_t y0 = lowcut1(lowcut1(x, &m1, &f1, 5), &m2, &f2, 5);
            if (i >= FS) {
                s0 += (double)y0 * y0;
                s1 += (double)y1 * y1;
            }
        }
        db = 10.0 * log10(s1 / s0);
        if (db < -0.5) {
            printf("     %.0f Hz: %+.2f dB\n", f, db);
            ok = 0;
        }
    }
    check("BASS+ (#42): 300..600 Hz lose less than 0.5 dB", ok);
}

/* a 60 Hz bass comes out as its harmonics: over 30 % of the input level, the 60 Hz itself under 20 % of the power */
static void test_bass_harmonics(void)
{
    double re = 0, im = 0, so = 0, si = 0, fund;
    uint32_t i;
    host_init();
    for (i = 0; i < 2u * FS; i++) {
        int32_t x = (int32_t)sine(60.0, i, 16000.0), y = spk_bass(x);
        if (i >= FS) {
            si += (double)x * x;
            so += (double)y * y;
            re += y * cos(2.0 * M_PI * 60.0 * i / FS);
            im += y * sin(2.0 * M_PI * 60.0 * i / FS);
        }
    }
    fund = 2.0 * (re * re + im * im) / FS / FS / (so / FS);
    check("BASS+: a 60 Hz bass becomes harmonics (level > 30 % of the input, fundamental < 20 % of the power)",
          sqrt(so / si) > 0.3 && fund < 0.2);
}

/* the master: BASS+ adds harmonics LOWCUT does not have; FLAT and LOWCUT unchanged (main: the kit hashes) */
static void test_bass_master(void)
{
    double h[3];
    uint32_t mode, i;
    for (mode = 1; mode <= 2u; mode++) {
        double re = 0, im = 0, so = 0;
        host_init();
        fx_lowcut = (uint8_t)mode;
        for (i = 0; i < 2u * FS; i++) {
            int32_t l = (int32_t)sine(60.0, i, 12000.0), r = l;
            master_out(&l, &r);
            if (i >= FS) {
                so += (double)l * l;
                re += l * cos(2.0 * M_PI * 60.0 * i / FS);
                im += l * sin(2.0 * M_PI * 60.0 * i / FS);
            }
        }
        h[mode] = so / FS - 2.0 * (re * re + im * im) / FS / FS;   /* the power that is not 60 Hz */
    }
    fx_lowcut = 0;
    check("BASS+ on the master: a 60 Hz bass gets 10x the harmonic power LOWCUT gives it", h[2] > 10.0 * h[1]);
}
```

and in `main`, after the LOWCUT hash check, add:

```c
    test_bass_port();
    test_bass_mids();
    test_bass_harmonics();
    test_bass_master();
```

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/sound_pack_test tests/sound_pack_test.c -lm`
Expected: compile errors — `spk_bass` undeclared, `lowcut1` called with 4 arguments.

- [ ] **Step 3: The menu test**

In `tests/ui_test.c`, before `int main`, add:

```c
/* MENU > SPEAKER EQ (sound pack): KNOB 1 steps FLAT LOWCUT BASS+ and stops at the ends, OCT+ steps and wraps;
 * the master follows (fx_lowcut); a stored value past BASS+ loads as FLAT */
static void test_speaker_eq(void)
{
    static const int8_t TURN[6] = {1, 1, 1, -1, -1, -1};
    static const uint8_t WANT[6] = {1, 2, 2, 1, 0, 0};
    uint32_t ok = 1, i;
    ui_host_init();
    settings.lowcut = 0;
    fx_lowcut = 0;
    ui.menu = 1;
    ui.menu_sel = MI_SPKEQ;
    ui.force = 1;
    for (i = 0; i < 6u; i++) {
        turn(EN_K1, TURN[i]);
        ui_frame();
        ok &= settings.lowcut == WANT[i] && fx_lowcut == WANT[i];
    }
    check("SPEAKER EQ: KNOB 1 steps FLAT LOWCUT BASS+, stops at the ends; the master follows", ok);
    ok = 1;
    for (i = 0; i < 4u; i++) {
        press(B_OCTUP);
        ui_frame();
        release_all();
        ui_frame();
        ok &= settings.lowcut == (i + 1u) % 3u && fx_lowcut == (i + 1u) % 3u;
    }
    check("SPEAKER EQ: OCT+ steps and wraps", ok);
    settings.lowcut = 2;
    fx_lowcut = 2;
    ui.force = 1;
    snap_page("menu_speaker_eq");
    settings.lowcut = 3;
    settings_init();
    check("SPEAKER EQ: a stored value past BASS+ loads as FLAT", settings.lowcut == 0 && fx_lowcut == 0);
    ui.menu = 0;
    ui.force = 1;
}
```

and call `test_speaker_eq();` in `main` before its final `printf`.

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm`
Expected: compile error — `MI_SPKEQ` undeclared.

- [ ] **Step 4: BASS+ in fx.c**

In `firmware/src/fx.c`:

(a) In the `fx` struct (top of the file), after `int32_t comb_lp[4];` add:

```c
    int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;   /* BASS+ (spk_bass) */
```

(b) Replace

```c
static volatile uint8_t fx_lowcut;     /* settings: 12 dB/oct ~110 Hz for the small speaker */
```

with

```c
static volatile uint8_t fx_lowcut;     /* settings (MENU SPEAKER EQ): 0 FLAT, 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the
                                        * small speaker): 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
```

(c) Replace `lowcut1` with:

```c
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+ (upstream 1.0.2): what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is
 * clipped at its own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and
 * added. The low-pass has 4 poles (#42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself, late,
 * and cancelled up to 6 dB of it; now under 0.5 dB). Upstream's text; its state lives in fx (no new globals). */
static inline int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    fx.sb_lp1 += ((m - fx.sb_lp1) * 692) >> 15;
    fx.sb_lp2 += ((fx.sb_lp1 - fx.sb_lp2) * 692) >> 15;
    fx.sb_lp3 += ((fx.sb_lp2 - fx.sb_lp3) * 692) >> 15;
    fx.sb_lp4 += ((fx.sb_lp3 - fx.sb_lp4) * 692) >> 15;
    a = fx.sb_lp4 < 0 ? -fx.sb_lp4 : fx.sb_lp4;
    if (a > fx.sb_env)
        fx.sb_env += (a - fx.sb_env) >> 2;
    else if (fx.sb_env > 0)
        fx.sb_env -= (fx.sb_env >> 11) + 1;
    t = clamp(fx.sb_lp4 * 8, -fx.sb_env, fx.sb_env);
    fx.sb_h1 += (t - fx.sb_h1) >> 5;
    u = t - fx.sb_h1;
    fx.sb_h2 += (u - fx.sb_h2) >> 5;
    u -= fx.sb_h2;
    fx.sb_hl += (u - fx.sb_hl) >> 3;
    return fx.sb_hl * 3;
}
```

(d) In `master_out` replace the `if (fx_lowcut) { ... }` block with:

```c
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        uint32_t sh = fx_lowcut == 2u ? 5u : 6u;    /* rounded step stopped at |x - lc| < 32: an offset) */
        int32_t b = fx_lowcut == 2u ? spk_bass((*l + *r) >> 1) : 0;
        *l = lowcut1(*l, &lc_l1, &lce[0], sh);
        *l = lowcut1(*l, &lc_l2, &lce[1], sh) + b;
        *r = lowcut1(*r, &lc_r1, &lce[2], sh);
        *r = lowcut1(*r, &lc_r2, &lce[3], sh) + b;
    }
```

`host_reset_fx` already clears `fx` (memset), so the BASS+ state resets with it.

- [ ] **Step 5: The setting and the menu**

In `firmware/src/panel.c` `settings_init`, replace

```c
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
```

with

```c
    if (settings.lowcut > 2u)                  /* SPEAKER EQ: FLAT LOWCUT BASS+ */
        settings.lowcut = 0;
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)settings.lowcut;
```

In `firmware/src/ui_menu.c`:
- line 3: `COLOR, LOWCUT, ZOOM,` → `COLOR, SPEAKER EQ (FLAT LOWCUT BASS+), ZOOM,`
- line 5: `MI_LOWCUT` → `MI_SPKEQ`; line 6: `"LOWCUT"` → `"SPEAKER EQ"`; after line 6 add
  `static const char *const SPK_EQ[3] = {"FLAT", "LOWCUT", "BASS+"};   /* settings.lowcut (fx_lowcut) */`
- `menu_onoff`: `return i == MI_LOWCUT ? &settings.lowcut : i == MI_ZOOM ...` →
  `return i == MI_ZOOM ? &settings.zoom : i == MI_ACCEL ? &settings.accel : 0;`
- `draw_menu`: in the item loop, change both `cv_text(90, y, ...` calls to `cv_text(102, y, ...` and after the
  `menu_onoff` text add:

```c
                if (i == MI_SPKEQ)
                    cv_text(102, y, &FONT_S, SPK_EQ[settings.lowcut % 3u], C_HI);
```

- `menu_input`: before `if ((s != 0 || ok) && ui.menu == 1 && menu_onoff(ui.menu_sel)) {` add:

```c
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_SPKEQ) {
        /* KNOB 1 steps FLAT LOWCUT BASS+ and stops at the ends; OCT+ steps and wraps (upstream 1.0.2) */
        settings.lowcut = s > 0 ? (settings.lowcut < 2u ? settings.lowcut + 1u : 2u)
                        : s < 0 ? (settings.lowcut ? settings.lowcut - 1u : 0u) : (settings.lowcut + 1u) % 3u;
        fx_lowcut = (uint8_t)settings.lowcut;
        ok = 0;
        s = 0;
    }
```

  and inside the on/off block delete the line `fx_lowcut = (uint8_t)(settings.lowcut != 0);`.

Run: `grep -n "MI_LOWCUT\|lowcut != 0" firmware/src/*.c`
Expected: no output.

- [ ] **Step 6: Green**

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/sound_pack_test tests/sound_pack_test.c -lm && build/host/sound_pack_test && cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test | grep -E "SPEAKER EQ|ui_test:|FAIL"`
Expected: every sound_pack_test line `ok` (the two kit hashes too: FLAT and LOWCUT unchanged); the three SPEAKER EQ
lines `ok`; `ui_test: all passed` (or the file's own pass line) and no `FAIL`.

- [ ] **Step 7: Whole drum suite, build, commit**

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; sh tests/run_drum_tests.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log 2>&1; tail -2 /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log; DRUM_PACKAGE=1 ./build.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/build.log 2>&1; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/build.log`
Expected: `ALL DRUM HOST TESTS PASSED`; the build's site line (retry on "exec format error").

```bash
git add firmware/src/fx.c firmware/src/panel.c firmware/src/ui_menu.c tests/sound_pack_ref.h tests/sound_pack_test.c tests/ui_test.c
git commit -m "BASS+ (upstream 1.0.2, with #42): MENU SPEAKER EQ FLAT / LOWCUT / BASS+; spk_bass checked against upstream's samples

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

---

### Task 3: SPRING reverb, reverb TYPE, project format FDR7

`G_RTYPE` grows the globals, so the project record grows (3024 → 3028 B): the format change lands in the same
commit, or saved projects would stop loading.

**Files:**
- Modify: `firmware/src/core.h` (`G_RTYPE`), `firmware/src/params.c` (`N_RTYPE`, `GP[G_RTYPE]`)
- Modify: `firmware/src/fx.c` (`rev_u`, `fx` fields, SPRING defines, `rev_room`, `rev_spring`, `rev_clear`,
  `fx_buses`, `part_buf` moved up)
- Modify: `firmware/src/project.c` (FDR7, `project_v6_t`, marker-based version, conversion)
- Modify: `tests/sound_pack_ref.h`, `tests/sound_pack_test.c`, `tests/boot_test.c`

**Interfaces:**
- Consumes: Task 1's hashes; Task 2's `fx` struct with the BASS+ fields.
- Produces: `G_RTYPE` (0 ROOM, 1 SPRING); `rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)`,
  `rev_spring(...)` (same signature, add to `out`), `rev_clear(void)`; `fx.rtype`; `PROJ_MAGIC` "FDR7",
  `PROJ_MAGIC_V6`, `project_v6_t`; `ref_rev_spring(const int32_t *, int32_t *, uint32_t, int32_t rsize, int32_t rdamp)`,
  `ref_spring_reset(void)`.

- [ ] **Step 1: The SPRING reference**

Append to `tests/sound_pack_ref.h`:

```c
#define REF_SP_LEN 4096u
#define REF_SP_MASK (REF_SP_LEN - 1u)
#define REF_SP_N 10u
#define REF_SP_A 2867
static int16_t ref_ln[REF_SP_LEN];
static int32_t ref_ap[4u * (REF_SP_N + 1u)];
static struct { uint16_t sp_w; int32_t sp_lp, sp_hp, sp_he, sp_size; uint32_t sp_ph; } ref_fx;
static void ref_spring_reset(void)
{
    memset(ref_ln, 0, sizeof ref_ln);
    memset(ref_ap, 0, sizeof ref_ap);
    memset(&ref_fx, 0, sizeof ref_fx);
}
static void ref_rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n, int32_t rsize, int32_t rdamp)
{
    uint32_t i, k, s = (uint32_t)rsize;
    int32_t g = 19661 + (int32_t)s * 85;
    int32_t kl = 26000 - rdamp * 160;
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = ref_ln;
    int32_t *ap = ref_ap;
    if (!ref_fx.sp_size)
        ref_fx.sp_size = len;
    ref_fx.sp_size += clamp(len - ref_fx.sp_size, -256, 256);
    L = ref_fx.sp_size >> 8;
    ref_fx.sp_ph += 2u * LFO_INC[24];
    w = (ref_fx.sp_size >> 1) + ((osc_sine(ref_fx.sp_ph) * 3) >> 8);
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = ref_fx.sp_w, j = (wp & 3u) * (REF_SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & REF_SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & REF_SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & REF_SP_MASK];
        ref_fx.sp_lp += mulq15(r - ref_fx.sp_lp, kl);
        o = ref_fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;
        o = x - ref_fx.sp_hp + ref_fx.sp_he;
        ref_fx.sp_he = o & 63;
        ref_fx.sp_hp += o >> 6;
        x -= ref_fx.sp_hp;
        p = ap[j];
        ap[j] = x;
        for (k = 1; k <= REF_SP_N; k++) {
            int32_t v = (x - ap[j + k]) * REF_SP_A;
            o = ap[j + k];
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & REF_SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        ref_fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & REF_SP_MASK] * 2;
    }
}
```

- [ ] **Step 2: The SPRING tests**

In `tests/sound_pack_test.c`, before `#define HASH_FLAT`, add:

```c
/* rev_spring = upstream's, sample for sample: a burst send, 6 s, SIZE and DAMP turned twice while it rings */
static void test_spring_matches_upstream(void)
{
    static int32_t in[CTL], a[CTL], b[CTL];
    uint32_t blk, i, ok = 1;
    int32_t peak = 0;
    host_init();
    ref_spring_reset();
    song.g[G_RSIZE] = 90;
    song.g[G_RDAMP] = 60;
    for (blk = 0; blk < 6u * FS / CTL; blk++) {
        if (blk == 2u * FS / CTL)
            song.g[G_RSIZE] = 10, song.g[G_RDAMP] = 120;
        if (blk == 4u * FS / CTL)
            song.g[G_RSIZE] = 127, song.g[G_RDAMP] = 0;
        for (i = 0; i < CTL; i++) {
            in[i] = blk < 40u ? (int32_t)((i * 2654435761u) >> 16) - 32768 : 0;
            a[i] = b[i] = 0;
        }
        rev_spring(in, a, CTL);
        ref_rev_spring(in, b, CTL, song.g[G_RSIZE], song.g[G_RDAMP]);
        for (i = 0; i < CTL; i++) {
            ok &= a[i] == b[i];
            peak = a[i] > peak ? a[i] : -a[i] > peak ? -a[i] : peak;
        }
    }
    check("SPRING: rev_spring gives upstream 1.0.2's samples (SIZE / DAMP turned while it rings)", ok);
    check("SPRING: bounded (|out| < 2^20) while SIZE glides", peak < (1 << 20));
}

/* after the send stops, SPRING's output reaches exactly 0 (no offset held in its loop): SIZE 0, 64, 127, 10 s */
static void test_spring_silence(void)
{
    static int32_t in[CTL], out[CTL];
    static const int16_t SIZES[3] = {0, 64, 127};
    uint32_t z, blk, i, ok = 1;
    for (z = 0; z < 3u; z++) {
        int32_t last = 0;
        host_init();
        song.g[G_RSIZE] = SIZES[z];
        song.g[G_RDAMP] = 60;
        for (blk = 0; blk < 10u * FS / CTL; blk++) {
            for (i = 0; i < CTL; i++) {
                in[i] = blk < 4u && i == 0u ? 600000 : 0;
                out[i] = 0;
            }
            rev_spring(in, out, CTL);
            for (i = 0; i < CTL; i++)
                last |= blk >= 9u * FS / CTL ? out[i] : 0;
        }
        ok &= last == 0;
    }
    check("SPRING: silent (exactly 0) once its tail has died (SIZE 0 / 64 / 127)", ok);
}

/* review focus 1 / 2: TYPE switched while a steady 200 Hz send keeps coming, straight into the buses (no drums: a
 * hit's attack would hide a click). A 200 Hz tone moves ~3 % of its level a sample; a model cut off hard would jump
 * by the whole level. In the switch block the old model fades out over the block, and the new one is silent for its
 * first ~1100 samples (its delay lines), so the 3 blocks after the switch may not step more than the steady tone. */
static void test_switch_while_sending(void)
{
    static int32_t z[CTL], in[CTL], w[CTL];
    uint32_t blk, i, ok = 1, sw, t = 0, clear = 1;
    host_init();
    for (sw = 0; sw < 2u; sw++) {                    /* ROOM -> SPRING, then SPRING -> ROOM */
        int32_t before = 0, at = 0, prev = 0, d;
        for (blk = 0; blk < 203u; blk++) {
            if (blk == 200u)
                song.g[G_RTYPE] = sw ? 0 : 1;
            for (i = 0; i < CTL; i++, t++) {
                in[i] = (int32_t)sine(200.0, t, 20000.0);
                z[i] = 0;
            }
            fx_buses(z, z, in, w, CTL);
            for (i = 0; i < CTL; i++) {
                d = w[i] - prev;
                d = d < 0 ? -d : d;
                prev = w[i];
                if (blk >= 100u && blk < 200u)
                    before = d > before ? d : before;
                if (blk >= 200u)
                    at = d > at ? d : at;
            }
            if (blk == 200u)
                for (i = 0; i < sizeof rev_comb / 2u; i++)
                    clear &= rev_comb[i] == 0;
        }
        if (at > 2 * before + 64)
            printf("     switch %u: step %d after, %d before\n", sw, at, before);
        ok &= at <= 2 * before + 64 && before > 0 && fx.rtype == (uint8_t)song.g[G_RTYPE];
    }
    check("TYPE switched while sending (ROOM -> SPRING -> ROOM): no click, the new model runs", ok);
    check("TYPE switch: the shared reverb buffer is cleared in the switch block", clear);
}
```

and in `main`, after `test_bass_master();`:

```c
    test_spring_matches_upstream();
    test_spring_silence();
    test_switch_while_sending();
```

In `tests/boot_test.c`, after `fdr5_converts`, add:

```c
/* an FDR6 record (GHOST, no reverb TYPE; 3024 B like an FDR5 one) loads: its values; TYPE ROOM */
static int fdr6_converts(void)
{
    project_v6_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V6;
    v.size = sizeof v;
    for (i = 0; i < G_RTYPE; i++)
        v.g[i] = song.g[i];
    v.g[G_CGHOST] = CG_HIDE;
    v.g[G_RSIZE] = 33;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_COUNT; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[4].step[7].on = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 3u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    song.g[G_RTYPE] = 1;                             /* SPRING running: the load sets ROOM */
    project_load(3);
    return song.g[G_CGHOST] == CG_HIDE && song.g[G_RSIZE] == 33 && song.g[G_RTYPE] == 0 && trk[4].step[7].on;
}
/* FDR7 keeps TYPE through a save and a load */
static int fdr7_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    song.g[G_RTYPE] = 1;
    project_save(2);
    song.g[G_RTYPE] = 0;
    memset(proj_slot, 0, sizeof proj_slot);
    project_load(2);
    return song.g[G_RTYPE] == 1;
}
```

and in its `main`, after the FDR5 check:

```c
    check("project: an FDR6 record (same size as FDR5) loads with reverb TYPE ROOM", fdr6_converts());
    check("project: FDR7 keeps the reverb TYPE", fdr7_round_trip());
```

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/sound_pack_test tests/sound_pack_test.c -lm; cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/boot_test tests/boot_test.c -lm`
Expected: compile errors — `rev_spring`, `G_RTYPE`, `fx.rtype`, `project_v6_t`, `PROJ_MAGIC_V6` undeclared.

- [ ] **Step 3: The parameter**

In `firmware/src/core.h`, replace

```c
    G_CGHOST,                                        /* a muted / any source heard: CG_MUTE CG_KEEP CG_HIDE (fx.c) */
    G_COUNT
```

with

```c
    G_CGHOST,                                        /* a muted / any source heard: CG_MUTE CG_KEEP CG_HIDE (fx.c) */
    G_RTYPE,                                         /* REVERB TYPE (fx.c): 0 ROOM, 1 SPRING (sound pack, FDR7) */
    G_COUNT
```

In `firmware/src/params.c`, after `static const char *const N_SLDIV[] ...` add
`static const char *const N_RTYPE[] = {"ROOM", "SPRING"};             /* G_RTYPE: the reverb bus's model (fx.c) */`
and after `    [G_CGHOST] = PE("GHOST", N_GHOST, CG_KEEP),` add `    [G_RTYPE] = PE("TYPE", N_RTYPE, 0),`.

- [ ] **Step 4: SPRING in fx.c**

In `firmware/src/fx.c`:

(a) Replace `static int16_t rev_ap[556 + 441] __attribute__((section(".pool")));` with:

```c
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
```

(b) In the `fx` struct, after the BASS+ line add:

```c
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
```

(c) After the `fx` struct add upstream's comment and defines:

```c
/* SPRING (G_RTYPE 1, upstream 1.0.2): one spring of a spring tank, mono like the other buses, in the ROOM's own
 * buffers (no RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass)
 * -> SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");
```

(d) Move `part_buf` up (the switch's fade uses it, as upstream): delete `, part_buf[CTL]` from the line
`static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet[CTL], mix_l[CTL], mix_r[CTL], part_buf[CTL];`
(its new definition is in (e)'s text, before `fx_buses`).

(e) Replace the whole `fx_buses` function with these four functions (ROOM's arithmetic unchanged, moved into
`rev_room`; SPRING, the clear and the switch are upstream 1.0.2's):

```c
/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[fx.comb_i[k]];
            fx.comb_lp[k] = o + mulq15(fx.comb_lp[k] - o, 32767 - damp);
            c[fx.comb_i[k]] = (int16_t)clamp(in + mulq15(fx.comb_lp[k], size), -32768, 32767);
            if (++fx.comb_i[k] >= REV_COMB[k])
                fx.comb_i[k] = 0;
            a += o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[fx.ap_i[k]];
            int32_t v = a + (o >> 1);
            c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v would be a notch comb) */
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept (as */
        fx.sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.sp / 4u; i++)
        rev_u.sp[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.comb_lp[i] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
}

static int32_t part_buf[CTL];                            /* a part's block (mix_part); the fade of a model change */

/* process the three buses for one block; sends in, wet stereo-equal out */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet,
                     uint32_t n)
{
    uint32_t i, dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t cdepth = song.g[G_CDEPTH] * 6, rt;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    for (i = 0; i < n; i++) {
        int32_t y = 0, x, r;
        /* chorus: modulated short delay, 5..15 ms */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        fx.cho_ph += cinc;
        r = (400 << 8) + ((osc_sine(fx.cho_ph) + 32768) * cdepth >> 8);   /* Q8 delay: read between samples */
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            y += (c0 + (((c1 - c0) * f) >> 8)) << 1;
        }
        fx.cho_w++;
        /* delay with a low-passed feedback */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        y += mulq15(x << 1, dmix);
        wet[i] = y;
    }
    rt = song.g[G_RTYPE] == 1;
    if (rt != fx.rtype) {                               /* the model changed: the old one's block fades out, */
        int32_t *t = part_buf, g = 65536, d = 65536 / (int32_t)n;   /* its buffers are cleared, the new */
        for (i = 0; i < n; i++)                                     /* one starts from silence */
            t[i] = 0;
        if (fx.rtype)
            rev_spring(rev_in, t, n);
        else
            rev_room(rev_in, t, n);
        for (i = 0; i < n; i++, g -= d)
            wet[i] += mulq16(t[i], (uint32_t)g);
        rev_clear();
        fx.rtype = (uint8_t)rt;
        return;
    }
    if (rt)
        rev_spring(rev_in, wet, n);
    else
        rev_room(rev_in, wet, n);
}
```

(`part_buf` is now defined once, here.)

- [ ] **Step 5: FDR7**

In `firmware/src/project.c`:

(a) Header comment: `Format "FDR6": the globals (with the COMP's GHOST)` → `Format "FDR7": the globals (with the COMP's GHOST and the reverb TYPE)`;
`and RESON's "FDR5" records` → `RESON's "FDR5" and GHOST's "FDR6" records`.

(b) Replace `#define PROJ_MAGIC 0x36524446u                 /* "FDR6": + the COMP's GHOST */` with:

```c
#define PROJ_MAGIC 0x37524446u                 /* "FDR7": + the reverb TYPE (sound pack) */
#define PROJ_MAGIC_V6 0x36524446u              /* "FDR6": GHOST projects, converted on load */
```

(c) After `project_v5_t`'s typedef add:

```c
/* GHOST's format: the globals before G_RTYPE (the parameters as now). 3024 B, as an FDR5 record: the version is
 * told by the marker, not the size */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_RTYPE];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_v6_t;
```

and change the union to
`static union { project_v1_t v1; project_v2_t v2; project_v3_t v3; project_v4_t v4; project_v5_t v5; project_v6_t v6; } proj_old;`

(d) In `proj_from_old`: the comment's `all: GHOST KEEP` → `v1..v5: GHOST KEEP; all: reverb TYPE ROOM`;

```c
    uint32_t ng = ver == 1u ? (uint32_t)G_GMODE : ver == 2u ? (uint32_t)G_CSRC : (uint32_t)G_CGHOST;
```
→
```c
    uint32_t ng = ver == 1u ? (uint32_t)G_GMODE : ver == 2u ? (uint32_t)G_CSRC : ver == 6u ? (uint32_t)G_RTYPE
                : (uint32_t)G_CGHOST;
```

and in the three selections replace the final `: proj_old.v5.g[i];`, `: proj_old.v5.sel;`,
`: proj_old.v5.t[k].p[i];` and `: proj_old.v5.t[k].step[i];` with
`: ver == 5u ? proj_old.v5.g[i] : proj_old.v6.g[i];`, `: ver == 5u ? proj_old.v5.sel : proj_old.v6.sel;`,
`: ver == 5u ? proj_old.v5.t[k].p[i] : proj_old.v6.t[k].p[i];` and
`: ver == 5u ? proj_old.v5.t[k].step[i] : proj_old.v6.t[k].step[i];`.

(e) Replace the old-record branch of `proj_fetch` (from `if (n == (int)sizeof proj_old.v1 ...` to its closing
brace before `if (n != (int)sizeof *q || !proj_ok(q))`) with:

```c
    if (n > 8 && n <= (int)sizeof proj_old && n != (int)sizeof *q) {   /* an old record: its marker names it */
        static const uint32_t MAGIC[7] = {0, PROJ_MAGIC_V1, PROJ_MAGIC_V2, PROJ_MAGIC_V3, PROJ_MAGIC_V4,
                                          PROJ_MAGIC_V5, PROJ_MAGIC_V6};
        static const uint32_t SIZE[7] = {0, sizeof proj_old.v1, sizeof proj_old.v2, sizeof proj_old.v3,
                                         sizeof proj_old.v4, sizeof proj_old.v5, sizeof proj_old.v6};
        uint32_t ver, hdr[2], sum;
        memcpy(&proj_old, q, (uint32_t)n);
        memcpy(hdr, &proj_old, sizeof hdr);
        memcpy(&sum, (const uint8_t *)&proj_old + n - 4, 4);
        for (ver = 1; ver < 7u; ver++)
            if (hdr[0] == MAGIC[ver] && hdr[1] == (uint32_t)n && SIZE[ver] == (uint32_t)n &&
                sum == proj_hash(&proj_old, (uint32_t)n - 4u)) {
                proj_from_old(q, ver);
                return;
            }
    }
```

Check that `st_load` is called with `sizeof *q` as its maximum (FDR7, 3028 B ≥ every old record), so an old record
loads whole: `grep -n "st_load(OBJ_PROJECT0" firmware/src/project.c`.

- [ ] **Step 6: Green**

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/sound_pack_test tests/sound_pack_test.c -lm && build/host/sound_pack_test && cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/boot_test tests/boot_test.c -lm && build/host/boot_test | grep -E "project:|boot_test:"`
Expected: every sound_pack_test line `ok` — the two kit hashes too (ROOM moved into `rev_room`, same samples); all
`project:` lines `ok` (FDR1..FDR7); `boot_test: all passed`.

- [ ] **Step 7: Whole drum suite, build, commit**

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; sh tests/run_drum_tests.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log 2>&1; tail -2 /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log; DRUM_PACKAGE=1 ./build.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/build.log 2>&1; grep -E "FAIL|image" /Users/evech/.claude/jobs/d6b478c9/tmp/build.log; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/build.log`
Expected: `ALL DRUM HOST TESTS PASSED`; the build's `ok image ...` line (the `_Static_assert` holds), no FAIL; the site
line.

```bash
git add firmware/src/core.h firmware/src/params.c firmware/src/fx.c firmware/src/project.c tests/sound_pack_ref.h tests/sound_pack_test.c tests/boot_test.c
git commit -m "SPRING reverb (upstream 1.0.2) beside ROOM in ROOM's buffers, reverb TYPE per project (FDR7; old records told by their marker: FDR5 and FDR6 are both 3024 B)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

---

### Task 4: REVERB and CHORUS pages

**Files:**
- Modify: `firmware/src/pages.c:31` (the REV/CHO page)
- Modify: `tests/ui_test.c`, `docs/DEVICE_INSTALL.md:69` (page list)

**Interfaces:**
- Consumes: Task 3's `G_RTYPE`.
- Produces: pages titled `"REVERB"` {G_RTYPE, G_RSIZE, G_RDAMP, 0xFF} and `"CHORUS"` {G_CRATE, G_CDEPTH, 0xFF, 0xFF},
  the 6th and 7th FX pages.

- [ ] **Step 1: The page test**

In `tests/ui_test.c`, before `int main`, add:

```c
/* sound pack: REV/CHO split into REVERB (TYPE SIZE DAMP) and CHORUS (RATE DEPTH); FX reaches both, TYPE turns
 * ROOM / SPRING; screens for the user */
static void fx_open(const char *title)              /* FX until the page shows */
{
    uint32_t k;
    for (k = 0; k < NPAGES && (!str_eq(cur_page()->title, title) || ui.home); k++) {
        press(B_FX);
        ui_frame();
        release_all();
        ui_frame();
    }
}
static void test_reverb_pages(void)
{
    ui_host_init();
    fx_open("REVERB");
    check("FX reaches REVERB: TYPE SIZE DAMP", str_eq(cur_page()->title, "REVERB") && cur_page()->id[0] == G_RTYPE &&
          cur_page()->id[1] == G_RSIZE && cur_page()->id[2] == G_RDAMP && cur_page()->id[3] == 0xFF);
    turn(EN_K1, 1);
    ui_frame();
    check("REVERB: KNOB 1 turns TYPE to SPRING", song.g[G_RTYPE] == 1);
    snap_page("fx_reverb_spring");
    fx_open("CHORUS");
    check("FX reaches CHORUS: RATE DEPTH", str_eq(cur_page()->title, "CHORUS") && cur_page()->id[0] == G_CRATE &&
          cur_page()->id[1] == G_CDEPTH && cur_page()->id[2] == 0xFF && cur_page()->id[3] == 0xFF);
    snap_page("fx_chorus");
}
```

and call `test_reverb_pages();` in `main` before its final `printf`.

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test | grep -E "REVERB|CHORUS|ui_test"`
Expected: FAIL on `FX reaches REVERB ...`, `REVERB: KNOB 1 turns TYPE ...` and `FX reaches CHORUS ...` (no such
pages yet).

- [ ] **Step 2: The pages**

In `firmware/src/pages.c` replace

```c
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
```

with

```c
    {"REVERB", FAM_FX, SC_GLOBAL, GR_NONE, {G_RTYPE, G_RSIZE, G_RDAMP, 0xFF}},   /* TYPE: ROOM / SPRING */
    {"CHORUS", FAM_FX, SC_GLOBAL, GR_NONE, {G_CRATE, G_CDEPTH, 0xFF, 0xFF}},
```

In `docs/DEVICE_INSTALL.md` line 69 replace `REV/CHO,` with `REVERB, CHORUS,`.

Run: `grep -rn "REV/CHO" firmware/src docs/DEVICE_INSTALL.md tests/*.c`
Expected: no output (other docs that mention REV/CHO describe history; leave them).

- [ ] **Step 3: Green**

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test | grep -E "REVERB|CHORUS|FAIL|ui_test"`
Expected: the three lines `ok`, no `FAIL`. If an existing test counted FX presses past DLY (e.g. a wrap), it fails
here: update its press count to the new page order and record a Ruling.

- [ ] **Step 4: Whole drum suite, build, commit**

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; sh tests/run_drum_tests.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log 2>&1; tail -2 /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log; DRUM_PACKAGE=1 ./build.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/build.log 2>&1; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/build.log; ls build/ui_shots/fx_reverb_spring.png build/ui_shots/fx_chorus.png build/ui_shots/menu_speaker_eq.png`
Expected: `ALL DRUM HOST TESTS PASSED`; the site line; the three PNGs listed.

```bash
git add firmware/src/pages.c tests/ui_test.c docs/DEVICE_INSTALL.md
git commit -m "FX pages: REV/CHO splits into REVERB (TYPE SIZE DAMP) and CHORUS (RATE DEPTH)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

---

### Task 5: Slow divisions, knobs by length

**Files:**
- Modify: `firmware/src/params.c` (`N_DIV`, `DIV_ORDER`, `enum_order`, `enum_rank`, `param_turn`)
- Modify: `firmware/src/fx.c` (`DIV_DEN` comment, `div_samples`)
- Modify: `firmware/src/ui.c:17` (`RATIO`), `firmware/src/ui_input.c:185, 430` (knob turns)
- Modify: `tests/drum_test.c`, `tests/ui_test.c`

**Interfaces:**
- Consumes: nothing from Tasks 2-4.
- Produces: `N_DIV` ids 6..9 = 1/2 1/1 2BAR 4BAR; `div_samples(uint32_t div)`;
  `static int32_t enum_rank(const param_desc_t *d, int32_t v)`;
  `static int32_t param_turn(const param_desc_t *d, int32_t v, int32_t steps)`.

- [ ] **Step 1: The sequencer tests**

In `tests/drum_test.c`, before `int main`, add:

```c
/* sound pack: the slow divisions (ids 6..9: 1/2 1/1 2BAR 4BAR) */
static void test_slow_divisions(void)
{
    static const uint32_t W120[10] = {22050, 11025, 5512, 2756, 7350, 3675, 44100, 88200, 176400, 352800};
    static const uint32_t W40[10] = {66150, 33075, 16537, 8268, 22050, 11025, 132300, 264600, 529200, 1058400};
    static const uint32_t W240[10] = {11025, 5512, 2756, 1378, 3675, 1837, 22050, 44100, 88200, 176400};
    uint32_t d, ok = 1, n0 = 0, n1 = 0, f, bar = FS * 60 / 240 * 4, a0, a1, at0[12], at1[4];
    host_init();
    for (d = 0; d < 10u; d++) {
        song.g[G_BPM] = 120;
        ok &= div_samples(d) == W120[d];
        song.g[G_BPM] = 40;
        ok &= div_samples(d) == W40[d];
        song.g[G_BPM] = 240;
        ok &= div_samples(d) == W240[d];
    }
    check("divisions: all ten exact at 40, 120, 240 BPM (1/2 .. 4BAR = 2 .. 16 beats)", ok);

    host_init();                                     /* 240 BPM: a bar is 44100 samples */
    song.g[G_BPM] = 240;
    trk[0].step[0].on = 1;                           /* 1/16, one hit a bar */
    trk[1].p[P_SDIV] = 9;                            /* 4BAR, every step on: one hit every 4 bars */
    for (d = 0; d < 16u; d++)
        trk[1].step[d].on = 1;
    play();
    a0 = hit_age(&trk[0]);
    a1 = hit_age(&trk[1]);
    for (f = 0; f < 9u * bar; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[0]) != a0 && n0 < 12u)
            at0[n0++] = f, a0 = hit_age(&trk[0]);
        if (hit_age(&trk[1]) != a1 && n1 < 4u)
            at1[n1++] = f, a1 = hit_age(&trk[1]);
    }
    check("divisions: a 4BAR track fires once every 4 bars, on the 1/16 track's bar hits",
          n0 == 9u && n1 == 3u && at1[0] == at0[0] && at1[1] == at0[4] && at1[2] == at0[8]);

    host_init();
    song.g[G_BPM] = 40;
    trk[0].p[P_SDIV] = 9;
    trk[0].p[P_SSWING] = 100;
    {
        uint64_t p = 1058400u, sw = p * (uint64_t)track_swing(&trk[0]) / 250u;
        check("divisions: swing on a 4BAR step at 40 BPM (no overflow)",
              track_swing(&trk[0]) > 0 && step_samples(&trk[0], 1058400u, 0) == p + sw &&
              step_samples(&trk[0], 1058400u, 1) == p - sw);
    }

    host_init();
    song.g[G_BPM] = 240;
    trk[0].p[P_SDIV] = 8;                            /* 2BAR: 88200 samples a step */
    trk[0].step[0].on = 1;
    trk[0].step[0].rat = 1;                          /* 2 hits */
    play();
    {
        uint32_t at[4];
        check("divisions: a ratchet on a 2BAR step rolls 2 hits inside the step", hits_at(0, 88200u, at, 4) == 2u);
    }

    host_init();
    song.g[G_BPM] = 240;
    trk[0].p[P_SDIV] = 8;
    song.rec = 1u;
    play();
    render_mix(0, 0, 88200u + 88200u * 3u / 4u / CTL * CTL);   /* 3/4 into step 1 */
    fm1_in.notes = 1u << KEY_TRK_KEY[0];
    render_mix(0, 0, CTL);
    fm1_in.notes = 0;
    check("divisions: live record on a 2BAR step: a late hit goes into the next step",
          trk[0].step[2].on && !trk[0].step[1].on);

    host_init();
    song.g[G_BPM] = 120;
    song.g[G_DTIME] = 8;                             /* 2BAR = 4 s: longer than the delay line */
    check("divisions: delay TIME 2BAR is cut to the delay line (1.49 s)", delay_samples() == DLY_LEN - 1u);
    song.g[G_DTIME] = 6;                             /* 1/2 at 120 BPM = 1 s: fits */
    check("divisions: delay TIME 1/2 at 120 BPM is 1 s", delay_samples() == 44100u);
}

/* review focus 3: DIV 1/16 -> 4BAR -> 1/16 while playing: the track keeps playing */
static void test_div_change_while_playing(void)
{
    uint32_t at[8], d;
    host_init();
    song.g[G_BPM] = 240;
    for (d = 0; d < 16u; d++)
        trk[0].step[d].on = 1;
    play();
    render_mix(0, 0, 3u * 2756u);
    trk[0].p[P_SDIV] = 9;
    render_mix(0, 0, 2u * 2756u);
    trk[0].p[P_SDIV] = 2;
    check("divisions: 1/16 -> 4BAR -> 1/16 while playing: hits resume within two 1/16 steps",
          hits_at(0, 2u * 2756u + CTL, at, 8) >= 1u);
}

/* review focus 4: a BPM change in the middle of a 4BAR step: the next step comes at the new length */
static void test_bpm_change_in_slow_step(void)
{
    uint32_t at[4], d;
    host_init();
    song.g[G_BPM] = 120;
    trk[0].p[P_SDIV] = 9;                            /* 4BAR at 120 BPM: 352800 samples */
    for (d = 0; d < 16u; d++)
        trk[0].step[d].on = 1;
    play();
    render_mix(0, 0, CTL);                           /* step 0 */
    render_mix(0, 0, 100000u / CTL * CTL);
    song.g[G_BPM] = 240;                             /* 4BAR now 176400 */
    check("divisions: BPM doubled inside a 4BAR step: the next step within the new length",
          hits_at(0, 176400u, at, 4) == 1u);
}
```

and in `main`, after `test_live_record();`:

```c
    test_slow_divisions();
    test_div_change_while_playing();
    test_bpm_change_in_slow_step();
```

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; python3 tools/build.py --gen-only >/dev/null && cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "divisions:|drum_test:"`
Expected: FAIL on `all ten exact` (ids 6..9 wrap to 0..3 today), `a 4BAR track fires once every 4 bars`, the
swing, ratchet, live-record and both delay lines may pass or fail (the id wraps today: 8 % 6 = 2 is 1/16); the
review-focus lines may pass. At least the first two FAIL; `drum_test: N FAILED`.

- [ ] **Step 2: The knob-order test**

In `tests/ui_test.c`, before `int main`, add:

```c
/* sound pack: division knobs run by length (4BAR .. 1/32), the stored value stays the id; the gauge follows */
static void test_div_order(void)
{
    static const int16_t WANT[10] = {9, 8, 7, 6, 0, 1, 4, 2, 5, 3};
    const param_desc_t *d = &TP[P_SDIV];
    uint32_t i, ok = 1;
    for (i = 0; i + 1u < 10u; i++)
        ok &= param_turn(d, WANT[i], 1) == WANT[i + 1] && param_turn(d, WANT[i + 1], -1) == WANT[i];
    ok &= param_turn(d, 3, 1) == 3 && param_turn(d, 9, -1) == 9 && param_turn(d, 9, 20) == 3;
    check("DIV: the knob steps 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32, stopping at the ends", ok);
    check("DLY TIME: the same order", param_turn(&GP[G_DTIME], 6, 1) == 0 && param_turn(&GP[G_DTIME], 0, -1) == 6);
    check("DIV gauge: by length (4BAR empty, 1/32 full, 1/4 at 4/9)",
          RATIO(d, 9) == 0 && RATIO(d, 3) == 1000 && RATIO(d, 0) == 444);
    check("other knobs keep their order (SLICER RATE, ids as shown)", param_turn(&TP[P_SLRATE], 1, 1) == 2);
    ui_host_init();
    settings.accel = 0;
    seq_open("PATTERN");
    trk[song.sel].p[P_SDIV] = 9;
    ok = 1;
    for (i = 1; i < 10u; i++) {
        turn(EN_K2, 1);
        ui_frame();
        ok &= trk[song.sel].p[P_SDIV] == WANT[i];
    }
    check("PATTERN DIV: turning right walks the length order", ok);
    trk[song.sel].p[P_SDIV] = 8;
    snap_page("seq/pattern_div_2bar");
    settings.accel = 1;
}
```

and call `test_div_order();` in `main` before its final `printf`.

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm`
Expected: compile error — `param_turn` undeclared.

- [ ] **Step 3: The divisions**

In `firmware/src/params.c`:
- line 5: `static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};` →
  `static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};   /* ids fixed: slow rates appended */`
- after `track_desc` (before `fmt_ms10`), add:

```c
/* (upstream 1.0.2, #48) the note divisions in the order of their length, longest first (triplets between their
 * neighbours), on the knobs and the gauges; the stored values (N_DIV ids: projects) stay.
 * -> the shown order of d's values (index: position, entry: value), 0 = the values' own order */
static const uint8_t DIV_ORDER[10] = {9, 8, 7, 6, 0, 1, 4, 2, 5, 3};   /* 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32 */
static const uint8_t *enum_order(const param_desc_t *d) { return d->names == N_DIV ? DIV_ORDER : 0; }
static int32_t enum_rank(const param_desc_t *d, int32_t v)   /* v's place in the shown order (+ min): the gauges */
{
    const uint8_t *o = enum_order(d);
    int32_t r;
    for (r = 0; o && r < d->max - d->min; r++)
        if (o[r] == v - d->min)
            break;
    return o ? r + d->min : v;
}
/* a knob turned `steps` (signed) from v: clamped, over the shown order */
static int32_t param_turn(const param_desc_t *d, int32_t v, int32_t steps)
{
    const uint8_t *o = enum_order(d);
    if (o)
        return o[clamp(enum_rank(d, v) - d->min + steps, 0, d->max - d->min)] + d->min;
    return clamp(v + steps, d->min, d->max);
}
```

In `firmware/src/fx.c` replace

```c
/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* beats = 1 / DEN */
static uint32_t div_samples(uint32_t div)
{
    return (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM] / DIV_DEN[div % 6u];
}
```

with

```c
/* length of one division (N_DIV id) in samples at the song tempo: ids 0..5 a quarter / DEN, the slow ids 6..9
 * (1/2 1/1 2BAR 4BAR) a quarter x 2, 4, 8, 16 (upstream 1.0.2) */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* beats = 1 / DEN; original ids fixed, slow rates append */
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}
```

In `firmware/src/ui.c` line 17 replace the `RATIO` macro with:

```c
#define RATIO(d, v) ((d)->max > (d)->min ? (enum_rank((d), (int32_t)(v)) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
```

In `firmware/src/ui_input.c`:
- line 185: `v = clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);` →
  `v = param_turn(d, *vp, accel(EN_K1 + slot, steps, d->max - d->min));`
- line 430: `*vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max);` →
  `*vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->max - d->min));`

Run: `grep -n "clamp(\*vp + accel" firmware/src/ui_input.c`
Expected: no output.

- [ ] **Step 4: Green**

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/drum_test tests/drum_test.c -lm && build/host/drum_test | grep -E "divisions:|drum_test:" && cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test | grep -E "DIV|DLY TIME|other knobs|FAIL"`
Expected: every `divisions:` line `ok`, `drum_test: all passed`; the five DIV / DLY TIME / other-knobs lines `ok`,
no `FAIL`.

- [ ] **Step 5: Whole drum suite, build, commit**

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; sh tests/run_drum_tests.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log 2>&1; tail -2 /Users/evech/.claude/jobs/d6b478c9/tmp/drum.log; DRUM_PACKAGE=1 ./build.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/build.log 2>&1; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/build.log`
Expected: `ALL DRUM HOST TESTS PASSED` (sound_pack_test's kit hashes still ok: `div_samples` for ids 0..5 gives the
same integers); the site line.

```bash
git add firmware/src/params.c firmware/src/fx.c firmware/src/ui.c firmware/src/ui_input.c tests/drum_test.c tests/ui_test.c
git commit -m "slow divisions 1/2 1/1 2BAR 4BAR (upstream 1.0.2) on PATTERN DIV and delay TIME; division knobs and gauges by length

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

---

### Task 6: Cost, demo WAVs, records, package

**Files:**
- Modify: `tests/target_budget.py` (FUNCS), `tests/target_budget.txt` (only after the user approves)
- Modify: `tests/drumsim.c` (sound pack demos)
- Modify: `CHANGELOG.md`, `docs/DEVICE_INSTALL.md` (Sound pack section), `docs/UPSTREAM_1.0.2.md` (FDR8 note)

**Interfaces:**
- Consumes: Tasks 2-5 (the firmware), `write_demo(dir, name, bars, bar_cb)` and `L`, `R`, `GAP` in drumsim.c.
- Produces: `build/drum_renders/sp_speaker_eq.wav`, `sp_reverb.wav`, `sp_slow_div.wav`.

- [ ] **Step 1: The cost figures**

In `tests/target_budget.py`, extend `FUNCS` with `"rev_room", "rev_spring"` (after `"reson_block"`).

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt`
Expected: every existing line `ok` or a `note:` (fm1_alnk0_irq probably lower: the reverb left it; BASS+ added a
little), `rev_room` and `rev_spring` with `no budget (BUDGET_UPDATE=1 adds it)`. A FAIL is a stop: show it.

- [ ] **Step 2: Ask the user (stop)**

Show the user the output: the new `rev_room` / `rev_spring` costs and the `fm1_alnk0_irq` change, and ask to write
them into `tests/target_budget.txt` (`BUDGET_UPDATE=1` rewrites every line to today's figures). Wait for the answer.
Only after a yes:

Run: `BUDGET_UPDATE=1 python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt && git diff --stat tests/target_budget.txt && python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt | tail -3`
Expected: the file rewritten; the check then all `ok`.

- [ ] **Step 3: The demo WAVs**

In `tests/drumsim.c`, before `int main`, add:

```c
/* sound pack demos (120 BPM, whole mix). sp_speaker_eq.wav: the 808 kit, 2 bars each FLAT, LOWCUT, BASS+;
 * sp_reverb.wav: a snare on 2 and 4 into the reverb, 2 bars each ROOM, SPRING at SIZE 0 / 64 / 127 (DAMP 60), SPRING
 * SIZE 127 DAMP 0 and DAMP 127; sp_slow_div.wav: a 1/16 kick with a hat on 2BAR (4 bars) then 4BAR (8 bars), the
 * snare's delay at TIME 1/2 */
static void sp_kit(void)
{
    static const char *const PAT[3] = {"X...x...x...x...", "....X.......X...", "x.x.x.x.x.x.x.x."};
    static const uint32_t MODEL[3] = {DM_K808, DM_S808, DM_HATC};
    uint32_t i, k;
    host_init();
    for (i = 0; i < 3u; i++) {
        drum_set_model(&trk[i], MODEL[i]);
        for (k = 0; k < 16u; k++) {
            trk[i].step[k].on = PAT[i][k] != '.';
            trk[i].step[k].acc = PAT[i][k] == 'X';
        }
    }
}
static void sp_eq_bar(uint32_t b) { fx_lowcut = (uint8_t)(b / 2u < 3u ? b / 2u : 2u); }
static void sp_rev_bar(uint32_t b)
{
    static const int16_t TYPE[6] = {0, 1, 1, 1, 1, 1}, SIZE[6] = {90, 0, 64, 127, 127, 127}, DAMP[6] = {60, 60, 60, 60, 0, 127};
    uint32_t s = b / 2u < 6u ? b / 2u : 5u;
    song.g[G_RTYPE] = TYPE[s];
    song.g[G_RSIZE] = SIZE[s];
    song.g[G_RDAMP] = DAMP[s];
}
static void sp_div_bar(uint32_t b) { trk[2].p[P_SDIV] = b < 4u ? 8 : 9; }
static void write_sound_pack(const char *dir)
{
    sp_kit();
    write_demo(dir, "sp_speaker_eq.wav", 6, sp_eq_bar);
    fx_lowcut = 0;
    sp_kit();
    drum_set_model(&trk[0], DM_K808);
    memset(trk[0].step, 0, sizeof trk[0].step);      /* the snare alone */
    memset(trk[2].step, 0, sizeof trk[2].step);
    trk[1].p[P_REV] = 110;
    write_demo(dir, "sp_reverb.wav", 12, sp_rev_bar);
    song.g[G_RTYPE] = 0;
    sp_kit();
    trk[1].p[P_DLY] = 90;
    song.g[G_DTIME] = 6;                             /* 1/2 */
    for (uint32_t k = 0; k < 16u; k++)
        trk[2].step[k].on = 1;
    write_demo(dir, "sp_slow_div.wav", 12, sp_div_bar);
}
```

and in `main`, right before its final `return 0;`, add `write_sound_pack(dir);`.

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/drumsim tests/drumsim.c -lm && build/host/drumsim build/drum_renders | grep sp_ && ls -l build/drum_renders/sp_*.wav`
Expected: three `drumsim: build/drum_renders/sp_....wav` lines; three files (~2 MB, ~4 MB, ~4 MB).

- [ ] **Step 4: Records**

`CHANGELOG.md`, under `## Unreleased` (after the TRS line), add:

```markdown
- Sound pack (upstream Felucca 1.0.2): MENU SPEAKER EQ FLAT / LOWCUT / BASS+ (the bass heard through its harmonics
  on the small speaker); SPRING reverb beside ROOM on the new REVERB page (TYPE SIZE DAMP; chorus on its own
  CHORUS page); slow divisions 1/2, 1/1, 2BAR, 4BAR on PATTERN DIV and delay TIME, division knobs ordered by
  length. Projects are FDR7 (older ones load with ROOM).
```

`docs/DEVICE_INSTALL.md`, after the `### TRS MIDI (stage 2 step 4: check on the FM-1)` section (before
`### TOOLS (check on the FM-1)`), add:

```markdown
### Sound pack (check on the FM-1)

- MENU (HOME held) > SPEAKER EQ: FLAT, LOWCUT, BASS+ (KNOB 1; OCT+ steps round). On the speaker, BASS+ makes the
  kick's bass audible without the mids getting thinner; the choice survives a power cycle.
- FX > REVERB: TYPE SPRING on a snare (send on FX page REV): the spring's chirp and drip; SIZE and DAMP change it;
  switching TYPE while it rings: no click. FX > CHORUS: RATE and DEPTH as before.
- PATTERN DIV on a hi-hat track: the knob runs 4BAR 2BAR 1/1 1/2 1/4 … 1/32; at 2BAR / 4BAR the hat plays once per
  2 / 4 bars, in time with a 1/16 kick. DLY TIME at 1/2 and slower: long echoes (the longest cut to 1.49 s).
- A project saved before this build loads with ROOM and sounds as before. With the heaviest kit and SPRING, the
  CPU meter stays close to where it was.
```

`docs/UPSTREAM_1.0.2.md`, item 6 of §3 (`Song chain + project names`): `one project format step (FDR6) for both`
→ `one project format step (FDR8, after the sound pack's FDR7) for both`.

- [ ] **Step 5: The full suite, the package, commit**

Run: `export PYTHON=/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin/python PATH="/Users/evech/Desktop/dev/fm1-drummachine/felucca/.venv/bin:$PATH"; DRUM_PACKAGE=1 ./build.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/build.log 2>&1; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/build.log; sh tools/upstream_build.sh; tests/run_tests.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/suite.log 2>&1; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/suite.log; grep "0 different\|within 75\|pinned loader\|target cost" /Users/evech/.claude/jobs/d6b478c9/tmp/suite.log`
Expected: the site line; `ALL HOST TESTS PASSED`; H2 `0 different`, stack within 75 %, the pinned loader.

```bash
git add tests/target_budget.py tests/target_budget.txt tests/drumsim.c CHANGELOG.md docs/DEVICE_INSTALL.md docs/UPSTREAM_1.0.2.md
git commit -m "sound pack: cost lines for rev_room / rev_spring (user approved), demo WAVs, device checks, changelog

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01BGWb4f19EttazmDKtgYxeD"
```

Then rebuild once more so `build/site` carries the final commit:
`DRUM_PACKAGE=1 ./build.sh > /Users/evech/.claude/jobs/d6b478c9/tmp/build.log 2>&1; tail -1 /Users/evech/.claude/jobs/d6b478c9/tmp/build.log`.
