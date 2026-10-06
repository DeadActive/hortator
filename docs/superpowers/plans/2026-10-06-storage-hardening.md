# Stage 2 step 3 (upstream's storage hardening and the `st_erase` order) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Our firmware runs upstream Felucca 1.0's `storage.c` (without the FM6 bank) and upstream's `st_erase`
(IRQs off before the audio buffer is silenced), with the update loader byte-identical.

**Architecture:** A new baseline tag `frozen-base-4` = `frozen-base-3` + the non-FM6 `storage.c` hunks + upstream's
`st_erase` in `felucca.c` (glue: it is inlined into the frozen `st_save`). Our tree takes the same two changes; the
frozen checks move to `frozen-base-4`. A new tool checks the erase order in the built firmware's disassembly.

**Tech Stack:** C firmware (JieLi pi32v2, single TU `firmware/src/felucca.c`), host tests in C, Python 3 tools,
POSIX sh, git tags.

**Spec:** `docs/superpowers/specs/2026-10-06-storage-hardening-design.md`

## Global Constraints

- Never brick the FM-1: never run `tools/fm1_install.py`, the web installer or the M-VAVE updater; never install
  mido / python-rtmidi. Packages only with `DRUM_PACKAGE=1 ./build.sh` → `build/felucca-UNTESTED.fwsc` (Docker; on
  "exec format error" run it again, up to 5 times). Put the private venv first for builds and suites:
  `export PYTHON=/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin/python PATH="/Users/evech/.claude/jobs/d6b478c9/tmp/venv/bin:$PATH"`.
  Do not wrap test runs in `sh -c '...'`.
- The frozen files change **only** by upstream hunks recorded in a baseline tag. `storage.c` = 727f272's minus the
  FM6 bank (the enum entry, its `st_sector` case, its flash-map comment line); `ST_MAGIC` stays `0x554C4546`.
  `ota.c` (`ota_erase`) is unchanged. If any other frozen file would have to change, stop and ask the user.
- The update loader `build/loader/ota.bin` stays byte-identical: sha256
  `cc98eed224299fa42ca22a546073e0f8b98d8362e26b5f325a9aaeab287bdf5d`, 6493 B.
- Changes to the check tools' rules (H2 lists, budgets, thresholds) are the user's decision; the worst interrupt
  stack stays within 75 % (`tools/stack_depth.py`), else stop and tell the user.
- New small file-scope globals in our non-frozen files can break H2: prefer existing structs (this plan adds none).
- New files carry the fork credit `Drum machine fork: 2026 DEADACTIVE`.

## Review Focus

1. `irq_restore` always ends in `sti`: if any caller reaches `st_erase` with IRQs already off (a save inside an
   `fm1_irq_off()` section of `project.c`, or `settings_save` at boot before `fm1_irq_enable_all`), IRQs come back on
   early. Today's `fl_erase4k` already did the same, so this is not new — confirm no caller relies on IRQs staying off.
2. SAVE while the sequencer plays: IRQs are off for the zero loop + the 4 KiB erase (as before, plus ~1024 stores).
   After the erase the audio ISR and TIMER5 resume: the ms count catches up (`fm1_timer5_irq` is robust to a late
   tick), no step is played twice or lost beyond today's behaviour.
3. Records written by other firmwares in our sectors: upstream Felucca 1.0.x writes the same `ST_MAGIC` and sets
   `slot` like ours; its FM6 bank sits at 0x9F000 / 0xFE000, which our object table never reads. The stock M-VAVE
   data there fails the header CRC (boot_test's random / header images).
4. The erase-order tool must fail loudly, never pass by accident: if `st_save` is inlined away or the zero loop
   changes form, it reports "not found" instead of passing.
5. `ota_erase` keeps the old order (as upstream). `main.c` sets `panic_req` for every track before `ota_session`,
   but nothing visible stops the audio ISR: check whether tails can still sound during an update's erases (a buzz
   there is out of this step's scope; report it, do not change frozen `ota.c`).

---

### Task 1: The baseline tag `frozen-base-4`

**Files:**
- Create (in a worktree on `frozen-base-3`, committed and tagged; our branch is not changed):
  `firmware/src/storage.c` (727f272's minus FM6), `firmware/src/felucca.c` (upstream's `st_erase`).

**Interfaces:**
- Consumes: tag `frozen-base-3` (56ad656a52862674016e82725519c4ad9e95df31).
- Produces: git tag `frozen-base-4` (a commit whose parent is 56ad656) and its full hash.

- [ ] **Step 1: Make the worktree; check the upstream files did not move after 1.0**

```bash
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca
git worktree add --detach /Users/evech/.claude/jobs/d6b478c9/tmp/fb4 frozen-base-3
git log --oneline 727f272..db70550 -- firmware/src/storage.c firmware/src/storage_hw.c tests/storage_test.c | wc -l
git diff --quiet 1e838e1:firmware/src/storage.c frozen-base-3:firmware/src/storage.c && echo "fb3 storage = 1e838e1"
```

Expected: `0`, then `fb3 storage = 1e838e1`.

- [ ] **Step 2: Upstream 1.0's `storage.c` without the FM6 bank**

```bash
cd /Users/evech/.claude/jobs/d6b478c9/tmp/fb4
git show 727f272:firmware/src/storage.c > firmware/src/storage.c
python3 - firmware/src/storage.c <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
for old, new in [
    ("user preset banks 0xDC000..0xDFFFF (upreset.c), the FM6\n * patch bank (fm6_bank.c): copy A 0x9F000, copy B 0xFE000 (the two free sectors) */",
     "user preset banks 0xDC000..0xDFFFF (upreset.c) */"),
    ("OBJ_FM6BANK = OBJ_UPRESET0 + 2, OBJ_COUNT };", "OBJ_COUNT = OBJ_UPRESET0 + 2 };"),
    ("    if (obj == OBJ_FM6BANK)\n        return copy ? 0xFE000u : 0x9F000u;\n", ""),
]:
    assert s.count(old) == 1, old
    s = s.replace(old, new)
assert "FM6" not in s
open(p, "w").write(s)
EOF
git diff --stat; git diff firmware/src/storage.c | grep '^[-+]' | grep -c FM6
```

Expected: `1 file changed, 15 insertions(+), 13 deletions(-)` (the hunks: header comment,
`_Static_assert`, `st_head` object/copy check + `h->slot != copy`, `st_current` wrap, `st_load` refuses oversize,
`st_save` object check + `memcmp` read-back), then `0`.

- [ ] **Step 3: Upstream's `st_erase` in `felucca.c` (glue, not frozen)**

In the worktree's `firmware/src/felucca.c` replace

```c
static int st_erase(uint32_t off)
{
    uint32_t took;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    audio_silence();
    return fl_erase4k(off, &took);
}
```

with (727f272's `firmware/src/storage_hw.c`):

```c
static int st_erase(uint32_t off)
{
    uint32_t took, f;
    int rc;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    f = irq_save();
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
}
```

Run: `git -C /Users/evech/.claude/jobs/d6b478c9/tmp/fb4 show 727f272:firmware/src/storage_hw.c | grep -n "static int st_erase" -A11`
Expected: the same text as the replacement.

- [ ] **Step 4: Build the worktree**

Run: `cd /Users/evech/.claude/jobs/d6b478c9/tmp/fb4 && PYTHON=$PYTHON ./build.sh > build.log 2>&1; tail -3 build.log; ls build/felucca.dis && shasum -a 256 build/loader/ota.bin`
Expected: a successful build; the loader sha256 starts `cc98eed2`. A build failure here is a stop: ask the user.

- [ ] **Step 5: Commit and tag in the worktree**

```bash
cd /Users/evech/.claude/jobs/d6b478c9/tmp/fb4
git add firmware/src/storage.c firmware/src/felucca.c
git commit -m "frozen-base-4: frozen-base-3 + upstream Felucca 1.0 (727f272) storage.c hardening, without the FM6 bank

Upstream hunks: the A/B header comment; _Static_assert on the 32-byte commit record; st_head refuses an invalid
object / copy and a header whose slot is not its copy; st_current takes the newer seq across the 32-bit wrap;
st_load refuses an invalid object and a record longer than the buffer (no truncation); st_save refuses an invalid
object and its read-back compares the whole header. Not taken: OBJ_FM6BANK, its st_sector case, its comment line.
1.0.1 / 1.0.2 did not change storage.c.
Not frozen, what the reference build needs (inlined into the frozen st_save):
- firmware/src/felucca.c: st_erase = 727f272's storage_hw.c (IRQs off, then audio_silence, the RAM erase, IRQs on).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git tag frozen-base-4
git diff --stat frozen-base-3 frozen-base-4
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca && git worktree remove --force /Users/evech/.claude/jobs/d6b478c9/tmp/fb4
git rev-parse frozen-base-4^{commit}
```

Expected: the diff stat lists `firmware/src/storage.c` and `firmware/src/felucca.c` only; a commit hash is printed
(note it for Task 2).

---

### Task 2: Our tree on `frozen-base-4` (the storage tests first)

**Files:**
- Modify: `tests/storage_test.c` (upstream's 1.0 test minus FM6, plus two cases), `tests/boot_test.c` (one case),
  `firmware/src/storage.c` (from the tag), `tools/frozen_base.txt`.

**Interfaces:**
- Consumes: tag `frozen-base-4` and its full hash (Task 1).
- Produces: `storage.c` with `st_load(obj, dst, max)` returning −1 when the record is longer than `max`; `st_head`
  checking `slot`; the baseline line `FROZEN_BASE frozen-base-4 <hash>`.

- [ ] **Step 1: Write the failing storage test**

```bash
cd /Users/evech/Desktop/dev/fm1-drummachine/felucca
git show 727f272:tests/storage_test.c > tests/storage_test.c
python3 - tests/storage_test.c <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
def rep(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)
a = s.index("    /* the FM6 patch bank")                       # upstream's FM6 cases: not our object table
b = s.index("    {\n        uint8_t copies[2 * ST_SECTOR];")
s = s[:a] + s[b:]
rep(" * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */\n",
    " * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments\n"
    " * Drum machine fork: 2026 DEADACTIVE */\n")
rep("static uint32_t io_calls;\n",
    "static uint32_t io_calls;\n"
    "static int hdr_mangle;                 /* the part stores a different, self-consistent header (rsv[0] byte 0 cleared) */\n"
    "static uint32_t st_crc32(const void *p, uint32_t n);\n")
rep("    io_calls++;\n    if (fail_after == 0)",
    "    uint8_t hb[32];\n"
    "    io_calls++;\n"
    "    if (hdr_mangle && n == sizeof hb && (off & 0xFFFu) == 0u) {   /* a header: magic .. rsv[1], hcrc at 28 */\n"
    "        uint32_t c;\n"
    "        memcpy(hb, src, sizeof hb);\n"
    "        hb[20] = 0;\n"
    "        c = st_crc32(hb, 28u);\n"
    "        memcpy(hb + 28, &c, 4);\n"
    "        s = hb;\n"
    "    }\n"
    "    if (fail_after == 0)")
rep('    printf("%s\\n", bad ? "STORAGE TEST FAILED"',
'''    {   /* a valid header that names the other copy is not this copy's commit record */
        st_hdr_t h;
        uint32_t off = st_sector(OBJ_PROJECT0, 1);
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_PROJECT0, a, sizeof a);              /* copy A (slot 0) */
        st_save(OBJ_PROJECT0, b, sizeof b);              /* copy B (slot 1), newer */
        memcpy(&h, nor + off, sizeof h);
        h.slot = 0;                                      /* B's header names copy A, CRC valid */
        h.hcrc = st_crc32(&h, sizeof h - 4u);
        memcpy(nor + off, &h, sizeof h);
        n = st_load(OBJ_PROJECT0, got, sizeof got);
        bad += check("header naming the other copy is ignored", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    }
    memset(nor, 0xFF, sizeof nor);                       /* read-back compares the whole header */
    st_save(OBJ_PROJECT0, a, sizeof a);
    hdr_mangle = 1;
    bad += check("read-back: a changed header field is an error", st_save(OBJ_PROJECT0, b, sizeof b) == -7);
    hdr_mangle = 0;
    printf("%s\\n", bad ? "STORAGE TEST FAILED"''')
assert "FM6" not in s
open(p, "w").write(s)
EOF
grep -c "FM6" tests/storage_test.c
```

Expected: `0`.

- [ ] **Step 2: Run it to see it fail**

Run: `cc -O1 -Wall -Wno-unused-function -o build/host/storage_test tests/storage_test.c && ./build/host/storage_test | grep -v " ok$"`
Expected: exactly these FAIL lines, then `STORAGE TEST FAILED`:
`wrapped sequence loads new data`, `invalid object load does no flash access`, `invalid object save does no flash
access`, `wrapped object rejected`, `small destination rejects whole object`, `header naming the other copy is
ignored`, `read-back: a changed header field is an error`.

- [ ] **Step 3: Write the failing settings case**

In `tests/boot_test.c`, before the comment `/* an M1 project in flash ("FDR1") loads: ...` add:

```c
/* a settings record longer than ours (another firmware's, same marker): the defaults, not a half-read record */
static int settings_oversize_refused(void)
{
    struct {
        persist_t p;
        uint8_t more[16];
    } r;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();                                  /* finds the flash */
    memset(&r, 0, sizeof r);
    r.p.magic = PERSIST_MAGIC;
    r.p.palette = 2;                                 /* not the default (4, MONO) */
    r.p.panel = PANEL_DEFAULT;
    st_save(OBJ_SETTINGS, &r, sizeof r);
    memset(&settings, 0, sizeof settings);           /* power off */
    persist_boot();
    settings_init();
    return settings.palette == 4u;
}
```

and in `main`, after the `mutebar_persists()` check line:

```c
    check("settings: a record longer than ours loads the defaults (not truncated)", settings_oversize_refused());
```

Run (venv exported): `cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/boot_test tests/boot_test.c -lm && ./build/host/boot_test | grep "longer than ours\|boot_test:"`
Expected: `FAIL  settings: a record longer than ours loads the defaults (not truncated)` and `boot_test: 1 FAILED`.

- [ ] **Step 4: Take `storage.c` from the tag; move the baseline**

```bash
git show frozen-base-4:firmware/src/storage.c > firmware/src/storage.c
```

`tools/frozen_base.txt`: the line `FROZEN_BASE frozen-base-3 56ad656a52862674016e82725519c4ad9e95df31` becomes
`FROZEN_BASE frozen-base-4 <the full hash from Task 1 Step 5>`.

- [ ] **Step 5: Run the tests to see them pass**

Run: `cc -O1 -Wall -Wno-unused-function -o build/host/storage_test tests/storage_test.c && ./build/host/storage_test | tail -1; cc -O1 -g -fsanitize=address,undefined -fno-sanitize=shift-base -fno-sanitize-recover=all -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Wno-macro-redefined -Ibuild/gen -Ifirmware/src -o build/host/boot_test tests/boot_test.c -lm && ./build/host/boot_test | tail -1; python3 tools/check_untouched.py; sh tests/guard_test.sh`
Expected: `storage test passed`, `boot_test: all passed`, `check_untouched: baseline frozen-base-4`,
`check_untouched: ok`, `guard: ok`. (H2 waits for Task 3: the tag's `st_save` inlines the new `st_erase`.)

- [ ] **Step 6: Commit**

```bash
git add firmware/src/storage.c tests/storage_test.c tests/boot_test.c tools/frozen_base.txt
git commit -m "stage 2 step 3: frozen baseline frozen-base-4 — upstream 1.0's storage hardening (seq wrap, slot check, no truncated loads, whole-header read-back), without the FM6 bank

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: The erase order (the check first)

**Files:**
- Create: `tools/check_erase_order.py`.
- Modify: `firmware/src/felucca.c:89-96` (`st_erase`), `tests/run_tests.sh` (after the loader pin self-test).

**Interfaces:**
- Consumes: `tools/dis_parse.py` `functions(path) -> {name: [(addr, text)]}`; the frozen-base-4 baseline (Task 2).
- Produces: `python3 tools/check_erase_order.py build/felucca.dis` (rc 0 / 1) and `--selftest`.

- [ ] **Step 1: Write the failing check**

Create `tools/check_erase_order.py`:

```python
# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""The storage erase order in the built app (docs/superpowers/specs/2026-10-06-storage-hardening-design.md §5).
In st_save, where felucca.c's st_erase is inlined, IRQs go off (cli) before the audio buffer is zeroed and come
back on (sti) only after the erase call. Zeroing first lets the audio ISR refill abuf before the cli, and the DMA
then loops that chunk for the whole erase (a buzz on SAVE). ota_erase (frozen ota.c) keeps the old order.
  python3 tools/check_erase_order.py build/felucca.dis
  python3 tools/check_erase_order.py --selftest"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from dis_parse import functions  # noqa: E402

ZERO_RE = re.compile(r"^\[r\d+\+r\d+<<2\] = r\d+$")    # the zero loop's indexed word store

# st_save's erase in the frozen-base-3 build (zeroing first): the self-test's failing case
OLD = ["r0 = 0", "r1 = 29392936 <abuf : 1c08028 >", "r2 = 0", "[r1+r2<<2] = r0", "r2 += 1",
       "if (r2 != 1024) goto -12 <st_save+0x98 : 2005bea >", "cli",
       "r0 = 29360824 <fl_erase4k_ram : 1c002b8 >", "[sp+68] = r0", "r2 = [sp+68]", "r1 = sp + 64", "call r2",
       "csync", "sti", "if (r0 != 0) goto 166 <st_save+0x162 : 2005cb4 >"]
NEW = OLD[6:7] + OLD[:6] + OLD[7:]                       # cli first: upstream's order


def check(text):
    """None when the order is right, else what was found."""
    ab = next((i for i, t in enumerate(text) if "<abuf " in t), None)
    er = next((i for i, t in enumerate(text) if "<fl_erase4k_ram " in t), None)
    if ab is None or er is None:
        return "no inlined st_erase (abuf, fl_erase4k_ram) in st_save"
    zero = next((i for i in range(ab, er) if ZERO_RE.match(text[i])), None)
    if zero is None:
        return "no abuf zero loop before the erase"
    cli = [i for i in range(zero) if text[i] == "cli"]
    if not cli or any(text[i] == "sti" for i in range(cli[-1], zero)):
        return "abuf zeroed with IRQs on (the cli comes after it): the audio ISR can refill it"
    if any(text[i] == "sti" for i in range(zero, er)):
        return "IRQs back on between the zeroing and the erase"
    call = next((i for i in range(er, len(text)) if text[i].startswith("call")), None)
    if call is None or "sti" not in text[call + 1:]:
        return "no sti after the erase call"
    return None


def selftest():
    cases = [("frozen-base-3's order", OLD, False), ("upstream's order", NEW, True),
             ("sti before the erase", NEW[:7] + ["sti"] + NEW[7:], False),
             ("no sti after the erase", [t for t in NEW if t != "sti"], False)]
    bad = 0
    for name, text, good in cases:
        ok = (check(text) is None) == good
        print("%-28s %s" % (name, "ok" if ok else "FAIL"))
        bad += not ok
    return bad


def main(argv):
    if argv[1:] == ["--selftest"]:
        return 1 if selftest() else 0
    st = functions(argv[1]).get("st_save")
    msg = "st_save not found in %s" % argv[1] if st is None else check([t for _, t in st])
    if msg:
        print("erase order: FAIL: " + msg)
        return 1
    print("erase order: st_save zeroes the audio buffer with IRQs off, through the erase")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

In `tests/run_tests.sh`, after `run "loader pin self-test (a changed loader is caught)" python3 tools/check_loader.py --selftest build` add:

```sh
run "storage erase: IRQs off before the audio is silenced (st_save)" python3 tools/check_erase_order.py build/felucca.dis
run "erase-order self-test (the old order is caught)" python3 tools/check_erase_order.py --selftest
```

- [ ] **Step 2: Run it on today's build to see it fail**

Run (venv exported): `./build.sh > build/host/build.txt 2>&1; tail -1 build/host/build.txt; python3 tools/check_erase_order.py build/felucca.dis; python3 tools/check_erase_order.py --selftest`
Expected: `erase order: FAIL: abuf zeroed with IRQs on (the cli comes after it): the audio ISR can refill it`;
the self-test's four lines `ok`.

- [ ] **Step 3: Upstream's `st_erase` in our `felucca.c`**

Replace `st_erase` in `firmware/src/felucca.c` (lines 89-96) with exactly the frozen-base-4 text:

```c
static int st_erase(uint32_t off)
{
    uint32_t took, f;
    int rc;
    if (!FL_STORE_OK(off, 0x1000u))
        return -8;
    f = irq_save();
    audio_silence();
    rc = FL_FAR(fl_erase4k_ram)(off, &took);
    irq_restore(f);
    return rc;
}
```

Run: `diff <(git show frozen-base-4:firmware/src/felucca.c | sed -n '/^static int st_erase/,/^}/p') <(sed -n '/^static int st_erase/,/^}/p' firmware/src/felucca.c) && echo same`
Expected: `same`.

- [ ] **Step 4: Build, the reference build, the check, the whole suite**

Run (venv exported):
`DRUM_PACKAGE=1 ./build.sh > build/host/pkg.txt 2>&1; tail -3 build/host/pkg.txt; sh tools/upstream_build.sh | tail -2; sh tests/run_tests.sh > build/host/suite.txt 2>&1; grep -n "FAIL\|erase order\|compare_upstream:\|stack: within\|loader: 6493\|ALL HOST" build/host/suite.txt`
Expected: the package builds; `baseline build (frozen-base-4)` built fresh; `erase order: st_save zeroes the audio
buffer with IRQs off, through the erase`; `loader: 6493 B, sha256 cc98eed2... = the pinned loader`;
`compare_upstream: ... 0 different`; `stack: within 75 % of both stacks`; `ALL HOST TESTS PASSED`.
If H2 reports a difference, find which function and why (`python3 tools/compare_upstream.py build
build/upstream/build`) before anything else; changes to H2's rules are the user's decision. If only the noisy host
idle-cost test fails (limit 357.5), run the suite once more and ledger it.

- [ ] **Step 5: Commit**

```bash
git add firmware/src/felucca.c tools/check_erase_order.py tests/run_tests.sh
git commit -m "storage erase: IRQs off before the audio buffer is silenced (upstream's st_erase): no stale chunk looped during a save; a check on the built st_save

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Records, device checklist, package

**Files:**
- Modify: `docs/UPSTREAM_1.0.2.md` (the "Frozen-file changes (by baseline tag)" table, after the `frozen-base-3`
  rows), `docs/DEVICE_INSTALL.md` (after the "USB-MIDI (stage 2 step 2: check on the FM-1)" section).

- [ ] **Step 1: The frozen-change rows**

Append after the two `frozen-base-3` rows in `docs/UPSTREAM_1.0.2.md`:

```markdown
| frozen-base-4 | 727f272 (1.0) | `firmware/src/storage.c` | every hunk except the FM6 bank (`OBJ_FM6BANK`, its `st_sector` case, its comment line) | the newer save wins across the 32-bit seq wrap; a header must name its own copy; an invalid object is refused before any flash access; a record longer than the buffer is refused, not truncated; the read-back compares the whole header (a failing part does not report SAVED) |
| frozen-base-4 | 727f272 (1.0) | `firmware/src/felucca.c` (not frozen) | `st_erase` = 727f272's `storage_hw.c` (IRQs off, `audio_silence`, the RAM erase, IRQs on) | no stale audio chunk looped during a save; inlined into the frozen `st_save`, so the reference build carries it |
```

- [ ] **Step 2: The device checklist**

In `docs/DEVICE_INSTALL.md`, after the USB-MIDI section, add:

```markdown
### Storage (stage 2 step 3: check on the FM-1)

- Play a pattern with long REVERB / DELAY tails, press STOP and SAVE the project at once (tails still sounding):
  no buzz or looped grain during the save. The same while the sequencer runs.
- Change a setting (palette or MUTE NEXT BAR) so the settings are saved: no buzz.
- Power off and on: the saved project loads, the settings and the panel layout (OCT- + OCT+ at power-on) are kept;
  a project saved with the previous build still loads.
```

- [ ] **Step 3: Package and loader check**

Run (venv exported): `DRUM_PACKAGE=1 ./build.sh > build/host/pkg.txt 2>&1; tail -1 build/host/pkg.txt; python3 tools/check_loader.py build`
Expected: the package line; `loader: 6493 B, sha256 cc98eed2... = the pinned loader`.

- [ ] **Step 4: Commit**

```bash
git add docs/UPSTREAM_1.0.2.md docs/DEVICE_INSTALL.md
git commit -m "docs: frozen-base-4 rows; storage checks for stage 2 step 3

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```
