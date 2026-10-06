# Stage 2 step 1 (fork baseline + panel timing, keys / encoders) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the frozen-code checks to a documented fork baseline tag (`frozen-base-2`, 1e838e1 + cited
upstream hunks), pin the update loader binary, and take upstream Felucca 1.0's key debounce / encoder fix
(`hal/fm1_input.h`) and TIMER5 nesting (`main.c`: no LED flicker, no lost knob steps).

**Architecture:** One file, `tools/frozen_base.txt`, names the baseline and the loader hash; the source check
(`check_untouched.py`), the reference build (`upstream_build.sh`, used by H2 and the stack check) read it. The tag
is a commit on 1e838e1 with only the adopted upstream hunks (plus the one non-frozen glue hunk its build needs);
our tree then takes the same frozen files.

**Tech Stack:** C firmware (JieLi pi32v2, single TU), Python 3 tools, POSIX sh, git tags.

**Spec:** `docs/superpowers/specs/2026-10-06-frozen-baseline-stage2-design.md`

## Global Constraints

- Never brick the FM-1: never run `tools/fm1_install.py`, the web installer or the M-VAVE updater; never install
  mido / python-rtmidi. Packages only with `DRUM_PACKAGE=1 ./build.sh` → `build/felucca-UNTESTED.fwsc` (Docker; on
  "exec format error" run it again, up to 5 times). Put the private venv first in `PATH` for builds and suites:
  `export PYTHON=/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin/python PATH="/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin:$PATH"`.
- The frozen files (hal/, loader/, ota.c, usb.c, crt0.S, app.ld, storage.c except ST_MAGIC, main.c except
  `felucca_init()` and the boot titles, core.h's last 5 lines) change **only** by upstream hunks recorded in a
  baseline tag; nothing of the fork's goes into them.
- The update loader `build/loader/ota.bin` stays byte-identical: sha256
  `cc98eed224299fa42ca22a546073e0f8b98d8362e26b5f325a9aaeab287bdf5d`, 6493 B. Moving the pin is the user's decision.
- The worst interrupt stack stays within `tools/stack_depth.py`'s limit (75 %); otherwise stop and tell the user.
- Budget / threshold changes are the user's decision. No LED glow (upstream 1.0.1), no USB audio, nothing of
  upstream's UI.
- New small file-scope globals can break H2 through global merging (`ota_wire`); if H2 fails on a data-layout
  difference, put the state into an existing struct and ledger a ruling.

## Review Focus

1. A reset inside the audio ISR (`.noinit` `felucca_dbg.in_audio` left 1): after boot USB must still be polled
   (the boot reset) — check the hunk is in our `main.c`'s frozen part and the tag.
2. TIMER5 nested in the render must touch nothing the audio ISR touches (only the scan and the ms): review that
   no USB / UART poll runs nested and that `t5_nested_ticks` is only written there and cleared by the audio ISR.
3. The H2 reference build is now the tag, built from a fresh worktree: a stale `build/upstream` built from
   1e838e1 must not be reused (the up-to-date check compares the revision).
4. `check_untouched.py` must still fail on any byte changed in a frozen file (against the tag), and still allow
   only its existing exceptions.
5. The CPU meter / voice shedding see the render time only: with no nesting the figures equal today's.

---

### Task 1: The baseline file, the checks read it, the loader pin

**Files:**
- Create: `tools/frozen_base.txt`, `tools/check_loader.py`
- Modify: `tools/check_untouched.py` (BASE), `tools/upstream_build.sh` (REV), `tests/run_tests.sh` (loader check)

**Interfaces:**
- Produces: `tools/frozen_base.txt` lines `FROZEN_BASE <git rev or tag>` and `LOADER_SHA256 <hex>`;
  `tools/check_loader.py [BUILD_DIR] [--selftest]` (exit 0 / 1).

- [ ] **Step 1: Write the loader check (test first: its self-test)**

`tools/frozen_base.txt`:

```text
# The frozen-code baseline (docs/superpowers/specs/2026-10-06-frozen-baseline-stage2-design.md): the git rev / tag
# whose frozen files ours equal (tools/check_untouched.py) and whose build is H2's and the stack check's
# reference (tools/upstream_build.sh). Moving it, or the loader pin, is the user's decision.
FROZEN_BASE 1e838e1
# the update loader the FM-1 runs (build/loader/ota.bin, 6493 B): it must not change
LOADER_SHA256 cc98eed224299fa42ca22a546073e0f8b98d8362e26b5f325a9aaeab287bdf5d
```

`tools/check_loader.py`:

```python
#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""The update loader (build/loader/ota.bin) is byte-identical to the one the FM-1 runs: its sha256 must be
tools/frozen_base.txt's LOADER_SHA256 (moving the pin is the user's decision).
  check_loader.py [BUILD_DIR]      check_loader.py --selftest [BUILD_DIR]"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def pinned():
    for ln in (ROOT / "tools/frozen_base.txt").read_text().splitlines():
        if ln.startswith("LOADER_SHA256 "):
            return ln.split()[1]
    raise SystemExit("tools/frozen_base.txt: no LOADER_SHA256")


def check(data):
    return hashlib.sha256(data).hexdigest() == pinned()


def main(argv):
    selftest = "--selftest" in argv
    args = [a for a in argv if a != "--selftest"]
    build = Path(args[0]) if args else ROOT / "build"
    data = (build / "loader" / "ota.bin").read_bytes()
    if selftest:
        bad = bytearray(data)
        bad[len(bad) // 2] ^= 1
        ok = check(data) and not check(bytes(bad))
        print(f"loader pin selftest: one flipped bit is {'caught' if ok else 'NOT caught'}")
        return 0 if ok else 1
    ok = check(data)
    print(f"loader: {len(data)} B, sha256 {hashlib.sha256(data).hexdigest()[:16]}... "
          f"{'= the pinned loader' if ok else 'DIFFERS from the pinned loader (tools/frozen_base.txt)'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
```

- [ ] **Step 2: Run it**

Run: `python3 tools/check_loader.py && python3 tools/check_loader.py --selftest`
Expected: `loader: 6493 B, sha256 cc98eed224299fa4... = the pinned loader` and `... one flipped bit is caught`.
Then change one hex digit of `LOADER_SHA256` in the file, run `python3 tools/check_loader.py; echo $?`
Expected: `DIFFERS ...` and `1`; restore the digit.

- [ ] **Step 3: The checks read the baseline file**

`tools/check_untouched.py`: replace `BASE = "1e838e1"` with

```python
def frozen_base():
    for ln in (ROOT / "tools/frozen_base.txt").read_text().splitlines():
        if ln.startswith("FROZEN_BASE "):
            return ln.split()[1]
    raise SystemExit("tools/frozen_base.txt: no FROZEN_BASE")


ROOT = Path(__file__).resolve().parents[1]
BASE = frozen_base()
```

(and delete the later duplicate `ROOT = ...` line), and add `print(f"check_untouched: baseline {BASE}")` as the
first line of its `main()` output. Update its docstring: "...differs from the frozen baseline
(tools/frozen_base.txt)".

`tools/upstream_build.sh`: replace `REV=$(git rev-parse 1e838e1)` with

```sh
BASE=$(awk '$1 == "FROZEN_BASE" { print $2 }' tools/frozen_base.txt)
REV=$(git rev-parse "$BASE^{commit}")
```

and the header comment "Builds upstream Felucca 1e838e1" → "Builds the frozen baseline (tools/frozen_base.txt)";
the final `echo` prints `"baseline build ($BASE): $UP/build"`.

`tests/run_tests.sh`, after the `== target cost` run line:

```sh
run "update loader = the pinned one (tools/frozen_base.txt)" python3 tools/check_loader.py build
run "loader pin self-test (a changed loader is caught)" python3 tools/check_loader.py --selftest build
```

- [ ] **Step 4: Run the full suite (no behaviour change yet)**

Run: `DRUM_PACKAGE=1 ./build.sh && sh tools/upstream_build.sh && sh tests/run_tests.sh 2>&1 | grep -E "baseline|loader|FAIL|ALL HOST"`
Expected: `check_untouched: baseline 1e838e1`, the loader lines ok, `ALL HOST TESTS PASSED`.

- [ ] **Step 5: Commit**

```bash
git add tools/frozen_base.txt tools/check_loader.py tools/check_untouched.py tools/upstream_build.sh tests/run_tests.sh
git commit -m "tools: the frozen baseline in one file (tools/frozen_base.txt); the update loader pinned by its sha256"
```

---

### Task 2: The baseline tag `frozen-base-2`

**Files:**
- Create (in a worktree on 1e838e1, committed and tagged; our branch is not changed): the tag commit with
  `firmware/hal/fm1_input.h` (727f272's), `firmware/src/main.c` (1e838e1's + three 727f272 hunks),
  `firmware/src/audio.c` (1e838e1's + the `t5_nested_ticks` glue).

**Interfaces:**
- Produces: git tag `frozen-base-2`.

- [ ] **Step 1: Make the worktree and take upstream 1.0's HAL file**

```bash
git worktree add --detach /Users/evech/.claude/jobs/d6b478c9/tmp/fb2 1e838e1
cd /Users/evech/.claude/jobs/d6b478c9/tmp/fb2
git show 727f272:firmware/hal/fm1_input.h > firmware/hal/fm1_input.h
```

- [ ] **Step 2: Apply the three `main.c` hunks (upstream 727f272), nothing else**

In the worktree's `firmware/src/main.c`:
1. Replace the whole `fm1_timer5_irq` function (from `void fm1_timer5_irq(void)` to its closing `}`) with
   727f272's comment block and function, exactly (`git show 727f272:firmware/src/main.c | sed -n 9,60p`: from
   `/* TIMER5 outranks ALNK0, ...` to the `}` after `owed = 0;`).
2. In `timer5_start`: `fm1_timer5_start(isr_timer5, 1);   /* below ALNK0 (3): no nesting into audio */` →
   `fm1_timer5_start(isr_timer5, 4);   /* above ALNK0 (3): the scan nests into the render (see fm1_timer5_irq) */`.
3. In `fm1_main`, after `felucca_dbg.max_us = 0;`:

```c
    felucca_dbg.in_audio = 0;                       /* .noinit: a reset inside the audio ISR left it set, and
                                                       TIMER5 would treat every tick as nested (no USB poll) */
```

- [ ] **Step 3: The non-frozen glue the baseline build needs (`audio.c`)**

In the worktree's `firmware/src/audio.c` (1e838e1's): after the `static volatile uint32_t audio_halves, ...`
line add

```c
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
```

and in the audio ISR, right after `t0` is taken, `t5_nested_ticks = 0;`, and where the half's time `us` is
computed, `us = (fm1_ticks() - t0 - t5_nested_ticks) / FM1_TICKS_PER_US;` (upstream 727f272 audio.c's
`shed_check` subtracts it the same way; only the subtraction is taken here).

- [ ] **Step 4: Build the worktree (it must build as a whole firmware)**

Run: `cd /Users/evech/.claude/jobs/d6b478c9/tmp/fb2 && PYTHON=$PYTHON ./build.sh > build.log 2>&1; tail -3 build.log; ls build/felucca.dis`
Expected: a successful build (`build/felucca.dis` exists). If it fails on a missing symbol, take the smallest
further upstream hunk that defines it, record it in the tag message, and continue.

- [ ] **Step 5: Commit and tag in the worktree**

```bash
git add firmware/hal/fm1_input.h firmware/src/main.c firmware/src/audio.c
git commit -m "frozen-base-2: 1e838e1 + upstream Felucca 1.0 (727f272) hal/fm1_input.h (key debounce 2 / 8 frames, encoder detent learning #23) and main.c's TIMER5 nesting (fm1_timer5_irq, timer5_start priority 4, the in_audio reset at boot); audio.c's t5_nested_ticks (glue the build needs)"
git tag frozen-base-2
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca && git worktree remove --force /Users/evech/.claude/jobs/d6b478c9/tmp/fb2
git tag --list frozen-base-2
```

Expected: `frozen-base-2`.

---

### Task 3: Our tree on `frozen-base-2` (input test first)

**Files:**
- Create: `tests/input_test.c` (upstream 727f272's + the #23 encoder checks)
- Modify: `firmware/hal/fm1_input.h` (= the tag's), `firmware/src/main.c` (the tag's three hunks),
  `firmware/src/audio.c` (`t5_nested_ticks`), `tools/frozen_base.txt` (`FROZEN_BASE frozen-base-2`),
  `tests/run_tests.sh` (the input test)

**Interfaces:**
- Consumes: tag `frozen-base-2`; `tools/frozen_base.txt`.

- [ ] **Step 1: Write the failing input test**

`git show 727f272:tests/input_test.c > tests/input_test.c`, then add a header line
`* Drum machine fork: 2026 DEADACTIVE (the #23 encoder checks)` under its copyright, and before its final
`if (fails) {` add:

```c
    /* #23 (upstream 1.0): the encoder learns one detent (rest) state only. A knob held mid-click for over the
     * rest time, then turned, still counts one step per detent; a slow turn with short mid-click pauses keeps
     * counting (it used to learn the mid states and go dead) */
    {
        static const uint8_t SEQ[] = {1, 3, 2, 0};     /* one detent from the rest 0 (A<<1|B) */
        const uint8_t *m = FM1_ENC[0];
        uint32_t c, i, f;
        int32_t s;
#define ENC_HOLD(st, frames)                                                     \
        for (f = 0; f < (frames); f++) {                                         \
            memset((void *)fm1_in.raw, 0, sizeof fm1_in.raw);                    \
            fm1_in.raw[m[0]] |= (uint8_t)((((st) >> 1) & 1u) << m[1]);           \
            fm1_in.raw[m[2]] |= (uint8_t)(((st) & 1u) << m[3]);                  \
            fm1__frame();                                                         \
        }
        reset();
        ENC_HOLD(0u, FM1_REST_FRAMES + 100u);          /* at rest: the detent is learned */
        ENC_HOLD(1u, FM1_REST_FRAMES + 100u);          /* held mid-click for over the rest time */
        ENC_HOLD(3u, 4u);
        ENC_HOLD(2u, 4u);
        ENC_HOLD(0u, 4u);                              /* that click ends */
        for (c = 0; c < 4u; c++)
            for (i = 0; i < 4u; i++)
                ENC_HOLD(SEQ[i], 4u);
        s = fm1_in.enc_steps[0];
        printf("encoder 0: held mid-click, then 5 detents -> %d step(s)\n", (int)s);
        check("#23: a knob held mid-click, then turned: one step per detent (5)", s == 5 || s == -5);
        reset();
        ENC_HOLD(0u, FM1_REST_FRAMES + 100u);
        for (c = 0; c < 4u; c++)
            for (i = 0; i < 4u; i++)
                ENC_HOLD(SEQ[i], i < 3u ? 200u : 4u);   /* ~0.2 s pauses on the mid states */
        s = fm1_in.enc_steps[0];
        printf("encoder 0: slow turn with mid-click pauses, 4 detents -> %d step(s)\n", (int)s);
        check("#23: short mid-click pauses: the knob keeps counting (4)", s == 4 || s == -4);
#undef ENC_HOLD
    }
```

In `tests/run_tests.sh`, next to the `midi_uart_test` lines:

```sh
$CC -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts, glitches; encoders (#23)" "$OUT/input_test"
```

- [ ] **Step 2: Run it against our current HAL**

Run: `cc -O1 -w -Ibuild/gen -Ifirmware/src -o build/host/input_test tests/input_test.c && build/host/input_test | tail -12`
Expected: FAIL — it does not compile against the 1e838e1 HAL (fields such as `fm1_in_stat.press_n`,
`FM1_REST_FRAMES` missing) or its checks fail. Either is the RED (the fix is missing).

- [ ] **Step 3: Take the tag's frozen files**

```bash
git show frozen-base-2:firmware/hal/fm1_input.h > firmware/hal/fm1_input.h
```

`firmware/src/main.c`: apply the same three hunks as Task 2 Step 2 (copy them from
`git show frozen-base-2:firmware/src/main.c`), leaving `felucca_init()` and the boot titles ours. Then
`firmware/src/audio.c` (ours): add `static volatile uint32_t t5_nested_ticks;` next to `audio_halves`,
`t5_nested_ticks = 0;` after `uint32_t t0 = fm1_ticks();` in `fm1_alnk0_irq`, and
`us = (fm1_ticks() - t0 - t5_nested_ticks) / FM1_TICKS_PER_US;` for the half's time. Set
`FROZEN_BASE frozen-base-2` in `tools/frozen_base.txt`.

- [ ] **Step 4: Run the input test**

Run: `cc -O1 -w -Ibuild/gen -Ifirmware/src -o build/host/input_test tests/input_test.c && build/host/input_test | tail -14`
Expected: every check `ok`, including both `#23` lines; `input debounce: all ok`. If a `#23` count is off by one
(the first click's accounting), read the decoder's comments in `fm1_input.h` and correct the test's expected
count to what one-step-per-detent means there; ledger a ruling.

- [ ] **Step 5: Full suite against the new baseline**

Run: `DRUM_PACKAGE=1 ./build.sh && sh tools/upstream_build.sh && sh tests/run_tests.sh 2>&1 | grep -E "baseline|loader|stack:|frozen|H2|keys and buttons|FAIL|ALL HOST"`
Expected: `check_untouched: baseline frozen-base-2` ok; H2 ok against the `frozen-base-2` build; the loader
`= the pinned loader`; `stack: within 75 % of both stacks`; the input test ok; `ALL HOST TESTS PASSED`. If the
stack check fails, stop and tell the user. If H2 fails only on data layout from `t5_nested_ticks`, see Global
Constraints.

- [ ] **Step 6: Commit**

```bash
git add tests/input_test.c tests/run_tests.sh firmware/hal/fm1_input.h firmware/src/main.c firmware/src/audio.c tools/frozen_base.txt
git commit -m "stage 2 step 1: frozen baseline frozen-base-2 — upstream 1.0's key debounce / encoder fix (#23) and TIMER5 nesting (no LED flicker, no lost knob steps); the CPU meter sees the render only"
```

---

### Task 4: Records, device checklist, package

**Files:**
- Modify: `docs/UPSTREAM_1.0.2.md` (frozen-change table), `docs/DEVICE_INSTALL.md`

- [ ] **Step 1: The frozen-change table**

Append to `docs/UPSTREAM_1.0.2.md`:

```markdown
## Frozen-file changes (by baseline tag)

| Tag | Upstream | File | Hunk | Why |
| --- | --- | --- | --- | --- |
| frozen-base-2 | 727f272 (1.0) | `firmware/hal/fm1_input.h` | the whole file (1.0's, without 1.0.1's LED glow) | key presses after 2 frames, releases after 8; the encoder learns one detent state (#23: double counts, dead knob) |
| frozen-base-2 | 727f272 (1.0) | `firmware/src/main.c` | `fm1_timer5_irq`; `timer5_start` priority 4; `felucca_dbg.in_audio = 0` at boot | the scan nests into the audio render: no ~16 Hz LED flicker, no lost encoder frames; USB / UART polls after the render |
| frozen-base-2 | 727f272 (1.0) | `firmware/src/audio.c` (not frozen; glue) | `t5_nested_ticks` | the render's time excludes the nested scan |
```

- [ ] **Step 2: The device checklist**

In `docs/DEVICE_INSTALL.md`, a section after `### RESON (check on the FM-1)`:

```markdown
### Panel (stage 2 step 1: check on the FM-1)

- LEDs: with a dense pattern playing (8 tracks, RESON / FX on), no flicker on the key / button LEDs.
- Knobs: turning slowly or fast, no skipped steps and no double steps; a knob left half-way into a click for a
  few seconds, then turned, still moves one step per click; a slow turn with short pauses keeps moving.
- Pads: presses respond at once; a held pad does not retrigger; fast repeats all play.
- USB and the installer still work as before (the update loader is unchanged).
```

- [ ] **Step 3: Package and commit**

Run: `DRUM_PACKAGE=1 ./build.sh && python3 tools/check_loader.py`
Expected: `= the pinned loader`.

```bash
git add docs/UPSTREAM_1.0.2.md docs/DEVICE_INSTALL.md
git commit -m "docs: frozen-file changes by baseline tag; panel checks for stage 2 step 1"
```
