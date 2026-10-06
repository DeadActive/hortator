# Stage 2 step 2 (upstream's USB-MIDI driver for the app) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The app runs upstream Felucca 1.0's `usb.c` (back-pressure, packet checks, SysEx ends, realtime queued) as
a new frozen file `firmware/src/usb_app.c`, while the update loader keeps today's `usb.c` and stays byte-identical.

**Architecture:** A new baseline tag `frozen-base-3` = `frozen-base-2` + `usb_app.c` (727f272's `usb.c`, byte for
byte) + `felucca.c` including it. Our tree takes the same file and include; the frozen checks (source, H2, loader
pin) move to `frozen-base-3`. The only behaviour glue is in our sequencer's MIDI reader (overflow recovery).

**Tech Stack:** C firmware (JieLi pi32v2, single TU `firmware/src/felucca.c`), host tests in C, Python 3 tools,
POSIX sh, git tags.

**Spec:** `docs/superpowers/specs/2026-10-06-usb-midi-stage2-design.md`

## Global Constraints

- Never brick the FM-1: never run `tools/fm1_install.py`, the web installer or the M-VAVE updater; never install
  mido / python-rtmidi. Packages only with `DRUM_PACKAGE=1 ./build.sh` → `build/felucca-UNTESTED.fwsc` (Docker; on
  "exec format error" run it again, up to 5 times). Put the private venv first for builds and suites:
  `export PYTHON=/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin/python PATH="/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin:$PATH"`.
  Do not wrap test runs in `sh -c '...'`.
- The frozen files change **only** by upstream hunks recorded in a baseline tag. `usb_app.c` = 727f272's
  `firmware/src/usb.c` byte for byte. `firmware/src/usb.c` (the loader's) stays unchanged. If any frozen file other
  than `usb_app.c` would have to change, stop and ask the user.
- The update loader `build/loader/ota.bin` stays byte-identical: sha256
  `cc98eed224299fa42ca22a546073e0f8b98d8362e26b5f325a9aaeab287bdf5d`, 6493 B.
- `FELUCCA_UAC` stays 0 (its default); `FELUCCA_CDC` as today (1 in the firmware). No USB audio.
- Changes to the check tools' rules (H2 lists, budgets, thresholds) are the user's decision; the worst interrupt
  stack stays within 75 % (`tools/stack_depth.py`), else stop and tell the user.
- New small file-scope globals in our non-frozen files can break H2 (global merging is off, but watch for it):
  prefer existing structs.

## Review Focus

1. A USB bus reset or replug while an EP1 packet is held back (`usb.rx_pend`): after re-enumeration MIDI works and
   no stale packet is replayed — check `usb_poll`'s reset path clears `rx_pend` (upstream's hunk) in `usb_app.c`.
2. TRS MIDI (`midi_uart.c`) still writes the ring directly, not through `midi_enqueue`: while `midi_in_overflow` is
   set it can still add events; the reader's recovery (`mi_r = mi_w`, flag clear) must not lose the ring's
   consistency — check producer / consumer contexts (TIMER5 polls are never nested in the render, so they do not
   interleave with the reader).
3. The web editor and the installer's identity reads use SysEx frames (`sx_frame`, `sx_ready`) through
   `usb_app.c`: a realtime byte or an aborted frame in between must not corrupt the next frame.
4. `fm1_ms` is defined twice in one translation unit (`core.h` and `usb_app.c`, both `static volatile uint32_t`):
   legal tentative definitions of one object — check the target build really has one object (the TIMER5 ms count
   and `midi_in_ms` see the same value).
5. The loader must not see any of this: `firmware/loader/loader.c` still includes `../src/usb.c`, and the pin holds.

---

### Task 1: The baseline tag `frozen-base-3`

**Files:**
- Create (in a worktree on `frozen-base-2`, committed and tagged; our branch is not changed):
  `firmware/src/usb_app.c` (727f272's `usb.c`), `firmware/src/felucca.c` (the include switch).

**Interfaces:**
- Consumes: tag `frozen-base-2` (5988fe4).
- Produces: git tag `frozen-base-3` (a commit whose parent is 5988fe4).

- [ ] **Step 1: Make the worktree and take upstream 1.0's USB driver as the app's copy**

```bash
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca
git worktree add --detach /Users/evech/.claude/jobs/d6b478c9/tmp/fb3 frozen-base-2
cd /Users/evech/.claude/jobs/d6b478c9/tmp/fb3
git show 727f272:firmware/src/usb.c > firmware/src/usb_app.c
git diff --quiet 727f272:firmware/src/usb.c db70550:firmware/src/usb.c && echo "1.0.2 = 1.0"
grep -n '#include "usb.c"' firmware/src/felucca.c
```

Expected: `1.0.2 = 1.0`, and one `#include "usb.c"` line in `felucca.c`.

- [ ] **Step 2: The app includes the copy (non-frozen glue)**

In the worktree's `firmware/src/felucca.c` replace the line `#include "usb.c"` with:

```c
#include "usb_app.c"         /* upstream 1.0's USB driver (the update loader keeps usb.c) */
```

- [ ] **Step 3: Build the worktree (it must build as a whole firmware)**

Run: `cd /Users/evech/.claude/jobs/d6b478c9/tmp/fb3 && PYTHON=$PYTHON ./build.sh > build.log 2>&1; tail -3 build.log; ls build/felucca.dis && sha256sum build/loader/ota.bin 2>/dev/null || shasum -a 256 build/loader/ota.bin`
Expected: a successful build; the loader sha256 starts `cc98eed2`. If the build fails on a symbol upstream's file
expects from upstream's other files, add the smallest declaration to a **non-frozen** file of the worktree
(`felucca.c` before the include, or `core.h` above its last 5 lines), record it in the tag message, and continue;
if only a frozen file could provide it, stop and ask the user.

- [ ] **Step 4: Commit and tag in the worktree**

```bash
cd /Users/evech/.claude/jobs/d6b478c9/tmp/fb3
git add firmware/src/usb_app.c firmware/src/felucca.c
git commit -m "frozen-base-3: frozen-base-2 + upstream Felucca 1.0 (727f272) usb.c as the app's usb_app.c

Upstream file, byte for byte (1.0.1 / 1.0.2 did not change it): EP1 back-pressure (the host is NAKed while the
MIDI ring lacks 16 + 8 free slots), USB-MIDI packet checks, realtime bytes skipped inside SysEx and any other status
ending an unfinished SysEx, Clock / Start / Continue / Stop queued (midi_enqueue, midi_in_source / midi_in_ms), the
ring overflow flag. USB audio compiled out (FELUCCA_UAC 0). The update loader keeps firmware/src/usb.c (unchanged).
Not frozen, what the reference build needs:
- firmware/src/felucca.c: includes usb_app.c instead of usb.c.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag frozen-base-3
git diff --stat frozen-base-2 frozen-base-3
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca && git worktree remove --force /Users/evech/.claude/jobs/d6b478c9/tmp/fb3
git rev-parse frozen-base-3^{commit}
```

Expected: the diff stat lists `firmware/src/usb_app.c` (new) and `firmware/src/felucca.c` (1 line), plus any glue
recorded in Step 3; a commit hash is printed (note it for Task 2).

---

### Task 2: Our tree on `frozen-base-3` (the driver test first)

**Files:**
- Create: `tests/usb_midi_test.c`, `firmware/src/usb_app.c` (from the tag).
- Modify: `firmware/src/felucca.c` (include), `tests/drum_host.h:33` (include), `tools/frozen_base.txt`,
  `tools/check_untouched.py` (FROZEN_FILES), `tools/compare_upstream.py:34` (C_FILES), `tests/guard_test.sh`,
  `tests/run_tests.sh` (after the `midi_uart_test` lines). `tests/midi_uart_test.c` stays on `usb.c` (it tests the
  loader's copy's SysEx path, which the loader still runs).

**Interfaces:**
- Consumes: tag `frozen-base-3` and its commit hash (Task 1).
- Produces: the app built with `usb_app.c`; host builds (`drum_host.h`) with `usb_app.c`, so `midi_enqueue(uint32_t
  pkt, uint32_t source)`, `midi_in_overflow`, `midi_in_source[]`, `ep1_take(const uint8_t *, uint32_t)`,
  `midi_in_event(uint32_t)` exist for Task 3.

- [ ] **Step 1: Write the failing driver test**

Create `tests/usb_midi_test.c`:

```c
/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Host test of the app's USB-MIDI driver, firmware/src/usb_app.c (upstream Felucca 1.0's usb.c): EP1 back-pressure,
 * packet checks, SysEx ends, the update commands and frames, realtime queued, the ring overflow flag. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 1
#define FELUCCA_CDC 0
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"   /* SIE register macros (never touched here) */
#include "../firmware/src/usb_app.c"

static uint32_t now_ms;
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }

static int check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static uint32_t ev(uint32_t cin, uint32_t st, uint32_t d1, uint32_t d2) { return cin | st << 8 | d1 << 16 | d2 << 24; }

static void put(uint8_t *b, uint32_t i, uint32_t pkt)       /* event i of an EP1 packet buffer */
{
    b[4u * i] = (uint8_t)pkt;
    b[4u * i + 1u] = (uint8_t)(pkt >> 8);
    b[4u * i + 2u] = (uint8_t)(pkt >> 16);
    b[4u * i + 3u] = (uint8_t)(pkt >> 24);
}

static void reset(void)
{
    mi_r = mi_w = 0;
    midi_in_overflow = 0;
    memset(&usb, 0, sizeof usb);
    sx_ready = sx_collect = 0;
    sx_pos = sx_frame_len = 0;
}

static void sysex_usb(const uint8_t *s, uint32_t n)         /* as USB-MIDI SysEx packets (CIN 4, then 5 / 6 / 7) */
{
    while (n > 3u) {
        midi_in_event(ev(4u, s[0], s[1], s[2]));
        s += 3;
        n -= 3u;
    }
    midi_in_event(ev(n == 1u ? 5u : n == 2u ? 6u : 7u, s[0], n > 1u ? s[1] : 0u, n > 2u ? s[2] : 0u));
}

int main(void)
{
    static const uint8_t UBOOT[6] = {0xF0, 0x22, 0x24, 0x35, 0x7D, 0xF7};
    static const uint8_t UPGRADE[6] = {0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7};
    uint8_t pk[64], fr[102];
    uint32_t i, k, w0;
    int bad = 0;

    /* back-pressure: 41 queued leaves 23 free (< 16 + 8): the packet stays; one more free: taken whole */
    reset();
    for (i = 0; i < 41u; i++)
        midi_enqueue(ev(9u, 0x99, 36, 100), 1u);
    for (i = 0; i < 16u; i++)
        put(pk, i, ev(9u, 0x99, 40u + i, 1u + i));
    w0 = mi_w;
    bad += check("EP1: a full ring holds the packet back (host NAKed), nothing queued",
                 ep1_take(pk, 64) == 0 && mi_w == w0);
    mi_r++;
    k = ep1_take(pk, 64);
    for (i = 0; i < 16u && k; i++)
        k = midi_in_q[(w0 + i) % MQ] == ev(9u, 0x99, 40u + i, 1u + i);
    bad += check("EP1: once 24 slots are free the same packet is taken whole, in order", k && mi_w == w0 + 16u);

    /* packet checks: a note-on, a program change (one data byte) kept; the rest dropped */
    reset();
    put(pk, 0, ev(9u, 0x90, 38, 90));
    put(pk, 1, ev(8u, 0x90, 38, 90));                       /* CIN 8 with a note-on status */
    put(pk, 2, ev(9u, 0x90, 0x80, 90));                     /* a data byte >= 0x80 */
    put(pk, 3, ev(9u, 0x90, 38, 0x80));
    put(pk, 4, ev(0xCu, 0xC0, 5, 0x80));                    /* program change: its 2nd byte is not data */
    put(pk, 5, ev(2u, 0xF2, 1, 2));                         /* CIN 2: system common, not queued */
    put(pk, 6, ev(0u, 0x90, 38, 90));                       /* CIN 0: reserved */
    put(pk, 7, ev(0xFu, 0xFE, 0, 0));                       /* active sensing: not queued */
    bad += check("packets: only the valid note-on and program change are queued",
                 ep1_take(pk, 32) == 1 && mi_w == 2u && midi_in_q[0] == ev(9u, 0x90, 38, 90) &&
                     (midi_in_q[1] & 0xFFFFu) == (0xCu | 0xC0u << 8));

    /* SysEx: a status byte ends an unfinished frame (the rest of the key then means nothing) */
    reset();
    for (i = 0; i < 4u; i++)
        sysex_byte(UBOOT[i]);
    sysex_byte(0x90);
    sysex_byte(0x7D);
    sysex_byte(0xF7);
    bad += check("SysEx: a status byte aborts an unfinished frame (no UBOOT request)", !usb.uboot_req && !usb.sx_on);
    reset();
    sysex_usb(UBOOT, 3);
    midi_in_event(ev(9u, 0x90, 38, 90));                    /* a note between the SysEx packets */
    sysex_usb(UBOOT + 3, 3);
    bad += check("SysEx over USB: a channel message ends it; the note is queued", !usb.uboot_req && mi_w == 1u);

    /* realtime inside SysEx is skipped; the UBOOT key and the M-UPGRADE command still work */
    reset();
    for (i = 0; i < 6u; i++) {
        sysex_byte(UBOOT[i]);
        if (i == 2u)
            sysex_byte(0xF8);
    }
    bad += check("SysEx: a clock byte inside is skipped, the UBOOT key works", usb.uboot_req == 1);
    reset();
    sysex_usb(UPGRADE, 3);
    midi_in_event(ev(0xFu, 0xF8, 0, 0));                    /* a clock packet between the SysEx packets */
    sysex_usb(UPGRADE + 3, 3);
    bad += check("M-UPGRADE command over USB with a clock packet in between: ota_req, clock queued",
                 usb.ota_req == 1 && mi_w == 1u && midi_in_q[0] == ev(0xFu, 0xF8, 0, 0));
    reset();
    for (i = 0; i < 3u; i++)
        sysex_byte(UPGRADE[i]);
    sysex_byte(0xB0);                                       /* aborted */
    for (i = 0; i < 6u; i++)
        sysex_byte(UPGRADE[i]);
    bad += check("M-UPGRADE command right after an aborted frame: ota_req", usb.ota_req == 1);

    /* an update / editor frame (100 data bytes) with a clock packet inside arrives whole */
    reset();
    fr[0] = 0xF0;
    for (i = 0; i < 100u; i++)
        fr[1 + i] = (uint8_t)((i * 37u + 5u) & 0x7Fu);
    fr[101] = 0xF7;
    sysex_usb(fr, 51);
    midi_in_event(ev(0xFu, 0xF8, 0, 0));
    sysex_usb(fr + 51, 51);
    bad += check("a 100-byte SysEx frame with a clock packet inside: ready, whole",
                 sx_ready == 1 && sx_frame_len == 100u && !memcmp(sx_frame, fr + 1, 100));

    /* realtime: Clock / Start / Continue / Stop queued with their source (USB = 1) */
    reset();
    midi_in_event(ev(0xFu, 0xF8, 0, 0));
    midi_in_event(ev(0xFu, 0xFA, 0, 0));
    midi_in_event(ev(0xFu, 0xFB, 0, 0));
    midi_in_event(ev(0xFu, 0xFC, 0, 0));
    bad += check("realtime: Clock, Start, Continue, Stop queued (CIN F), source USB",
                 mi_w == 4u && midi_in_q[1] == ev(0xFu, 0xFA, 0, 0) && midi_in_q[3] == ev(0xFu, 0xFC, 0, 0) &&
                     midi_in_source[0] == 1u && midi_in_source[3] == 1u);

    /* overflow: the 65th event sets the flag; nothing is accepted until the reader clears it */
    reset();
    for (i = 0; i < MQ; i++)
        midi_enqueue(ev(9u, 0x99, 36, 100), 1u);
    k = midi_enqueue(ev(9u, 0x99, 36, 100), 1u);
    mi_r = mi_w;                                            /* drained, but the flag is still set */
    bad += check("overflow: a full ring sets the flag and refuses until it is cleared",
                 k == 0 && midi_in_overflow == 1 && midi_enqueue(ev(9u, 0x99, 36, 100), 1u) == 0);

    if (!bad)
        printf("usb_midi_test: all passed\n");
    return bad;
}
```

- [ ] **Step 2: Run it to see it fail**

Run: `cd /Users/evech/Desktop/dev/fm1-drummachine/felucca && cc -O1 -Wall -Wno-unused-function -o build/host/usb_midi_test tests/usb_midi_test.c 2>&1 | head -3`
Expected: FAIL — `'../firmware/src/usb_app.c' file not found`.

- [ ] **Step 3: Take the file from the tag; the app and the host builds include it**

```bash
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca
git show frozen-base-3:firmware/src/usb_app.c > firmware/src/usb_app.c
```

In `firmware/src/felucca.c` replace `#include "usb.c"` with the tag's line
(`git show frozen-base-3:firmware/src/felucca.c | grep usb_app`):

```c
#include "usb_app.c"         /* upstream 1.0's USB driver (the update loader keeps usb.c) */
```

plus any glue the tag recorded in its Step 3 (the same text). In `tests/drum_host.h` replace
`#include "../firmware/src/usb.c"` with `#include "../firmware/src/usb_app.c"`.

- [ ] **Step 4: Run the driver test**

Run: `cc -O1 -Wall -Wno-unused-function -o build/host/usb_midi_test tests/usb_midi_test.c && ./build/host/usb_midi_test`
Expected: every line `ok`, then `usb_midi_test: all passed`. If a line fails, the test's expectation is checked
against `usb_app.c`'s code (the file is upstream's and is not edited); a wrong expectation is fixed in the test
and ledgered as a ruling.

- [ ] **Step 5: The guard test catches a changed `usb_app.c` (failing first)**

In `tests/guard_test.sh`, after the `ota.c` case (`cp firmware/src/ota.c "$TMP/firmware/src/ota.c"`) add:

```sh
echo '/* x */' >> "$TMP/firmware/src/usb_app.c"
if python3 tools/check_untouched.py --tree "$TMP" >/dev/null; then echo "guard: usb_app.c change not caught"; exit 1; fi
cp firmware/src/usb_app.c "$TMP/firmware/src/usb_app.c"
```

Run: `sh tests/guard_test.sh`
Expected: FAIL — `guard: usb_app.c change not caught` (the baseline is still `frozen-base-2` and `usb_app.c` is not
in FROZEN_FILES).

- [ ] **Step 6: Move the baseline; make `usb_app.c` frozen**

`tools/frozen_base.txt`: the line `FROZEN_BASE frozen-base-2 5988fe4622496f9e597974448e8c21054b425f5c` becomes
`FROZEN_BASE frozen-base-3 <the full hash from Task 1 Step 4>`.

`tools/check_untouched.py`:

```python
FROZEN_FILES = ["firmware/src/ota.c", "firmware/src/usb.c", "firmware/src/usb_app.c", "firmware/crt0.S", "firmware/app.ld"]
```

and in its docstring `hal/, loader/, ota.c, usb.c, crt0.S, app.ld byte for byte` →
`hal/, loader/, ota.c, usb.c (the loader's), usb_app.c (the app's), crt0.S, app.ld byte for byte`.

`tools/compare_upstream.py` line 34:

```python
C_FILES = sorted((ROOT / "firmware/hal").glob("*.h")) + [ROOT / "firmware/src" / f for f in ("usb.c", "usb_app.c", "ota.c", "storage.c")]
```

Run: `sh tests/guard_test.sh && python3 tools/check_untouched.py`
Expected: `guard: ok`; `check_untouched: baseline frozen-base-3`, `check_untouched: ok`.

- [ ] **Step 7: Wire the driver test into the suite**

In `tests/run_tests.sh`, after `run "TRS MIDI parser" "$OUT/midi_uart_test"` add:

```sh
$CC -o "$OUT/usb_midi_test" tests/usb_midi_test.c
run "USB-MIDI driver (usb_app.c): back-pressure, packet checks, SysEx, realtime, overflow" "$OUT/usb_midi_test"
```

- [ ] **Step 8: Build, the reference build, the whole suite**

Run (with the venv exported):
`DRUM_PACKAGE=1 ./build.sh > build/host/pkg.txt 2>&1; tail -3 build/host/pkg.txt; sh tools/upstream_build.sh | tail -2; sh tests/run_tests.sh > build/host/suite.txt 2>&1; grep -n "FAIL\|compare_upstream:\|stack: within\|loader: 6493\|usb_midi\|ALL HOST" build/host/suite.txt`
Expected: the package builds; `baseline build (frozen-base-3)` built fresh; `loader: 6493 B, sha256 cc98eed2... = the
pinned loader`; `compare_upstream: ... 0 different`; `stack: within 75 % of both stacks`; `ALL HOST TESTS PASSED`.
If H2 reports a difference, find which function and why (`python3 tools/compare_upstream.py build
build/upstream/build`) before anything else; changes to H2's rules are the user's decision.

- [ ] **Step 9: Commit**

```bash
git add firmware/src/usb_app.c firmware/src/felucca.c tests/drum_host.h tests/usb_midi_test.c tests/guard_test.sh \
        tests/run_tests.sh tools/frozen_base.txt tools/check_untouched.py tools/compare_upstream.py
git commit -m "stage 2 step 2: frozen baseline frozen-base-3 — the app runs upstream 1.0's USB driver (usb_app.c): EP1 back-pressure, packet checks, SysEx ends, realtime queued; the update loader keeps usb.c

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The MIDI reader recovers from an overflow; realtime skipped

**Files:**
- Modify: `firmware/src/seq.c` (`midi_block`, ~line 165).
- Test: `tests/drum_test.c` (after `test_midi`, and its call in `main`).

**Interfaces:**
- Consumes: `midi_enqueue(uint32_t pkt, uint32_t source)`, `midi_in_overflow`, `mi_r`, `mi_w`, `MQ` (usb_app.c via
  drum_host.h); `hit_age(const track_t *)`, `dvage`, `render_mix` (drum_host.h); `test_midi`'s note 38 / channel
  10 setup (`trk[1]` has NOTE 38 after `host_init`).

- [ ] **Step 1: Write the failing tests**

In `tests/drum_test.c`, after the end of `test_midi` add:

```c
/* the USB-MIDI ring (usb_app.c): an overflow drops the broken backlog and the ring works again; Clock / Start /
 * Stop are queued but nothing uses them yet (the MIDI clock feature will) */
static void test_midi_ring(void)
{
    uint32_t i, a0;
    host_init();
    for (i = 0; i <= MQ; i++)                        /* one more than fits: the overflow flag */
        midi_enqueue(0x09u | 0x99u << 8 | 38u << 16 | 90u << 24, 1u);
    a0 = dvage;
    render_mix(0, 0, CTL);
    check("MIDI ring overflow: the backlog is dropped (no hits), the ring empty and open again",
          midi_in_overflow == 0 && mi_r == mi_w && dvage == a0);
    a0 = hit_age(&trk[1]);
    midi_enqueue(0x09u | 0x99u << 8 | 38u << 16 | 90u << 24, 1u);
    render_mix(0, 0, CTL);
    check("MIDI ring after an overflow: a new note plays", hit_age(&trk[1]) != a0);
    a0 = dvage;
    midi_enqueue(0x0Fu | 0xFAu << 8, 1u);           /* Start, Clock, Stop */
    midi_enqueue(0x0Fu | 0xF8u << 8, 1u);
    midi_enqueue(0x0Fu | 0xFCu << 8, 1u);
    render_mix(0, 0, CTL);
    check("MIDI realtime (Start / Clock / Stop) is read and ignored for now: no hit, transport unchanged",
          mi_r == mi_w && dvage == a0 && !song.playing);
}
```

and in `main`, after `test_midi();`, add `test_midi_ring();`.

- [ ] **Step 2: Run them to see the overflow test fail**

Run: `cc -O2 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/drum_test tests/drum_test.c -lm && ./build/host/drum_test | grep -n "MIDI ring\|MIDI realtime"`
Expected: `FAIL  MIDI ring overflow: ...` (today's reader plays the 64 queued notes and never clears the flag) and
`FAIL  MIDI ring after an overflow: a new note plays` (the flag refuses it); `ok    MIDI realtime ...` already
(a CIN F packet's status nibble is not 0x90: a guard for the clock feature, not new behaviour).

- [ ] **Step 3: The reader's recovery**

In `firmware/src/seq.c` `midi_block`, at the top of the function (before `while (mi_r != mi_w) {`) add:

```c
    if (midi_in_overflow) {                         /* (upstream 1.0, usb_app.c) the ring overflowed: the stream is
                                                     * broken, drop the backlog and open the ring again */
        mi_r = mi_w;
        midi_in_overflow = 0;
        return;
    }
```

- [ ] **Step 4: Run the tests**

Run: the Step 2 command.
Expected: all three `ok`.

- [ ] **Step 5: The whole suite, then commit**

Run (venv exported): `DRUM_PACKAGE=1 ./build.sh > build/host/pkg.txt 2>&1; sh tests/run_tests.sh > build/host/suite.txt 2>&1; grep -n "FAIL\|compare_upstream:\|ALL HOST" build/host/suite.txt`
Expected: `0 different`, `ALL HOST TESTS PASSED`.

```bash
git add firmware/src/seq.c tests/drum_test.c
git commit -m "MIDI reader: recover from a USB-MIDI ring overflow (drop the backlog, reopen); realtime packets read and ignored until the clock feature

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Records, device checklist, package

**Files:**
- Modify: `docs/UPSTREAM_1.0.2.md` (the "Frozen-file changes (by baseline tag)" table), `docs/DEVICE_INSTALL.md`
  (after "### COMP GHOST (check on the FM-1)").

- [ ] **Step 1: The frozen-change rows**

Append to the table in `docs/UPSTREAM_1.0.2.md`:

```markdown
| frozen-base-3 | 727f272 (1.0) | `firmware/src/usb_app.c` (new, frozen) | 727f272's `usb.c`, the whole file (USB audio compiled out) | the app's USB-MIDI: the host held off while the ring is full (no dropped / stuck notes), packet checks, a status byte ends an unfinished SysEx, realtime queued (USB clock in); the update loader keeps `usb.c` (pinned) |
| frozen-base-3 | 727f272 (1.0) | `firmware/src/felucca.c` (not frozen) | `#include "usb_app.c"` instead of `usb.c` | the app compiles upstream's driver |
```

- [ ] **Step 2: The device checklist**

In `docs/DEVICE_INSTALL.md`, after the "COMP GHOST" section, add:

```markdown
### USB-MIDI (stage 2 step 2: check on the FM-1)

- Notes from a DAW over USB on the drum channel: every hit plays; a dense burst (e.g. a 1/64 roll on several notes,
  or a pasted block of notes): every hit plays, nothing stuck, the FM-1 stays responsive.
- **Run the web installer again with the same package after installing**: it must find the FM-1 and hand over to
  the update loader (the M-UPGRADE command now goes through the new driver). The pinned loader and the stock UBOOT
  path stay the way back either way.
- The web editor connects and reads / writes as before (its SysEx frames go through the new driver).
- Unplug / replug while notes are sent: MIDI works again after the replug.
```

- [ ] **Step 3: Package and loader check**

Run: `DRUM_PACKAGE=1 ./build.sh > build/host/pkg.txt 2>&1; tail -1 build/host/pkg.txt; python3 tools/check_loader.py build`
Expected: the package line; `loader: 6493 B, sha256 cc98eed2... = the pinned loader`.

- [ ] **Step 4: Commit**

```bash
git add docs/UPSTREAM_1.0.2.md docs/DEVICE_INSTALL.md
git commit -m "docs: frozen-base-3 rows; USB-MIDI checks for stage 2 step 2

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
