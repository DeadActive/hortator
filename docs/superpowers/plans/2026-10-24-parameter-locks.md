# Parameter Locks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Elektron-style parameter locks: an ENV-toggled STEPS key mode on every page, hold a step and turn any
MOTION-recordable knob to lock it for that step (snap back after), with 1024 lock / motion events per project in new
per-slot lock banks cut from the top 32 KiB of user sample slot 3.

**Architecture:** A new pure-data module `firmware/src/lockbank.c` holds a sorted, packed event bank (16-bit key +
int8 value + a lock bitmap; binary-search lookup, O(n) validation). motion.c plays the bank in the audio ISR (motion
events hold, lock events snap back, both through `motion_slot` so the LFO copy is respected). project.c saves each
slot's bank as its own storage object (OBJ_LOCKS0..3, A/B copies) before the project record ("FDRF"), which carries
a hash link to its bank so a torn save keeps a matching pair. The UI adds `ui.env` (STEPS / TRACKS), lock entry on
held steps, and the display marks.

**Tech Stack:** C (unity build: firmware/src/felucca.c includes every .c), host tests in C (cc, ASan/UBSan where the
suite already uses them), Python gate tools, the web sim (emscripten via tools/build_sim.sh, node tests).

**Spec:** `docs/superpowers/specs/2026-10-24-parameter-locks-design.md` (binding; read it with this plan).

## Global Constraints

- Never brick the FM-1: no installers, no flashing, no device commands. Every flash address written is one of the
  spec's map and nothing else: settings 0xFC000/0xFD000, projects 0x97000–0x9EFFF, USR3's header sector 0xC8000
  (the one-time clamp only), lock banks 0xD4000–0xDBFFF (slot s, copy c at `0xD4000 + s*0x2000 + c*0x1000`), user
  objects 0xDC000–0xDFFFF. 0x9F000 and 0xFE000 stay unused; nothing in OTA staging 0xE0000–0xE4FFF, the loader, the
  app slot, stock config / BT calibration or key_mac.
- Every lock-bank payload ≤ 3584 B, so sector offset +0xF00 stays 0xFF (the loader's erase scan, ldr_core.c:129-134).
- Frozen files byte-identical to the frozen baseline, except the approved storage.c object-table change, which moves
  the baseline to `frozen-base-10` exactly as frozen-base-9 did for usb_app.c: a local lightweight tag on a commit
  on top of frozen-base-9 that changes only storage.c, the `tools/frozen_base.txt` pin, and a gate 2a allowance with
  self-tests in tools/check_update_path.py. `python3 tools/check_untouched.py` passes.
- No 64-bit divide: `grep -c divdi build/felucca.dis` prints 0.
- CPU, stack and RAM budgets are never raised (tests/target_budget.txt, H3 stack, build.py's RAM / pool checks):
  if one is exceeded, stop and report the numbers instead of raising it.
- TDD: each behaviour's test is written and seen failing before the code.
- TRACKS mode (ENV off) is byte-for-byte today's key behaviour: every existing ui_test assertion passes unchanged.
- ISR safety: the main loop writes the bank and the lock state with the audio IRQ off (`fm1_irq_off()` /
  `fm1_irq_on()`), as motion.c does; the ISR only reads.
- New files carry `SPDX-License-Identifier: GPL-3.0-only` and `Drum machine fork: 2026 DEADACTIVE`.
- Suites, every task (in this order; gate 5 builds committed files, so commit first and fix up after):
  1. `sh tests/run_drum_tests.sh`
  2. `export PYTHON=$PWD/.venv/bin/python PATH="$PWD/.venv/bin:$PATH"; DRUM_PACKAGE=1 DRUM_BUNDLE=1 ./build.sh`
     (Docker running; retry up to 5x on "exec format error")
  3. `sh tests/run_tests.sh` (run `sh tools/upstream_build.sh` first when H2/H3 report no or a stale baseline build)
  4. `tools/build_sim.sh`
  5. `(cd web && node --test test_web.mjs)`
  6. `python3 tools/check_untouched.py`
- Commits: explicit `git add <files>` (never `.venv`, never `git add -A`), message ends with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. No push, merge, publish, release or tag, except the
  local `frozen-base-10` tag of Task 1.
- The task that changes behaviour updates README.md, the docs/DEVICE_INSTALL.md checklist and CHANGELOG.md
  "## Unreleased" in the same commit.
- Messages fit `ui.msg` (23 chars): "USR3 SHORTENED TO 48K", "LOCKS LOST", "LOCKS FULL", "STEP 5 LOCKS CLEARED",
  "STEP 5 ALL CLEARED"; top line "LOCK STEP 5" (1-based step numbers).

## Review Focus

1. Power loss mid-save: the bank written but not the project record (or the bank torn) must leave the previous
   project + bank pair loading together, never new locks on an old project or a misread bank. Pinned in Task 3
   (`bank_torn_keeps_pair`).
2. Someone runs upstream Felucca and uploads an 80 KiB USR3 over the banks: next boot shortens USR3 to 48 KiB once,
   every bank loads empty with "LOCKS LOST" once, projects still load, nothing is misread. Pinned in Task 3
   (`usr3_upload_over_banks`) on top of Task 1's clamp tests.
3. LFO + motion + lock on one knob and one step: the locked value is the LFO's centre on that step, the motion value
   holds after it, STOP puts the base back. Pinned in Task 4 (`lock_lfo_motion_same_knob`).
4. Song mode switching slots mid-pattern while a lock is active: the locked value never leaks into the next row's
   slot; each row plays its own slot's locks. Pinned in Task 4 (`lock_song_row_change`).
5. ENV pressed while a step key is held: holds drop, the release toggles nothing, no lock is written after the mode
   change. Pinned in Task 5 (`test_env_while_step_held`).

---

### Task 1: Flash map, frozen-base-10, gate 2a, USR3 at 48 KiB

**Files:**
- Modify: `firmware/src/storage.c:19-21` (map comment, object enum), `:50-57` (`st_sector`)
- Modify: `tools/frozen_base.txt`, `tools/check_untouched.py` (docstring: frozen-base-10's storage.c change),
  `tools/check_update_path.py` (allowance, `source_note`, self-test)
- Modify: `firmware/src/eng_sample.c:36-42, 72-75` (USR3's maximum), `firmware/src/project.c` (`smp_usr3_clamp`,
  `persist_note`, `persist_boot` order), `firmware/src/ui_input.c` (notes shown), `firmware/src/felucca.c`
  (only if needed: see Step 9)
- Modify: `tests/storage_test.c`, `tests/flash_host.h` (a write log and a failure hook), `tests/boot_test.c`
- Docs: README.md (USR3 is 48 KiB), docs/DEVICE_INSTALL.md, CHANGELOG.md

**Interfaces:**
- Produces: `OBJ_LOCKS0` (= `OBJ_UPRESET0 + 2`, 7), `OBJ_COUNT = OBJ_LOCKS0 + 4`;
  `st_sector(OBJ_LOCKS0 + s, c) == 0xD4000u + s * 0x2000u + c * 0x1000u`.
- Produces: `#define SMP_USR3_SIZE 0xC000u` and `static uint32_t smp_user_size(uint32_t k)` (slot 2: 0xC000, else
  `SMP_USER_SIZE`); slot bases unchanged (`SMP_USER_BASE + k * SMP_USER_SIZE`).
- Produces: `static int smp_usr3_clamp(void)` (project.c, FELUCCA_FLASH): 1 rewritten, 0 nothing to do, < 0 flash
  error (then USR3 scans as empty).
- Produces: `static uint8_t persist_note;` with `PN_USR3 1u`, `PN_LOCKS 2u`; `static void persist_note_show(void)`
  (ui_input.c, every frame: when no message is up, the lowest bit's message, then the bit cleared).
- Test hooks (tests/flash_host.h): `host_flog[64]` of `{uint8_t erase; uint32_t off, n;}`, `host_nflog`,
  `host_fail_at` (the Nth erase/program op returns -8; -1 never).

- [ ] **Step 1: Write the failing storage-map test** in tests/storage_test.c, `lock_bank_sectors`: for s 0..3, c 0..1,
  `st_sector(OBJ_LOCKS0+s, c) == 0xD4000 + s*0x2000 + c*0x1000`; every object's two sectors are distinct across all
  `OBJ_COUNT` objects, 4 KiB aligned, inside FL_DATA or FL_GLOB, outside 0xA0000–0xD3FFF, 0xE0000–0xE4FFF, and
  never 0x9F000 / 0xFE000; a save + load of a 3208-B payload into OBJ_LOCKS0+3 round-trips.
- [ ] **Step 2: Run it:** `sh tests/run_tests.sh` → FAIL ("OBJ_LOCKS0 undeclared").
- [ ] **Step 3: Change storage.c** exactly (this text is pinned by gate 2a; keep it verbatim):

```c
/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, projects 0x97000..0x9EFFF,
 * user sample slots 0xA0000..0xD3FFF (eng_sample.c), lock banks 0xD4000..0xDBFFF (project.c),
 * user preset banks 0xDC000..0xDFFFF (upreset.c) */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_LOCKS0 = OBJ_UPRESET0 + 2,
       OBJ_COUNT = OBJ_LOCKS0 + 4 };
```
  and in `st_sector`, before the `OBJ_UPRESET0` test:
```c
    if (obj >= OBJ_LOCKS0)
        return 0xD4000u + (obj - OBJ_LOCKS0) * 2u * ST_SECTOR + copy * ST_SECTOR;
```
- [ ] **Step 4: Move the baseline.** `git worktree add --detach /tmp/fb10 frozen-base-9`; copy the new storage.c
  there (frozen-base-9's storage.c is byte-identical to main's, so the copy is the whole change); commit there with
  message "frozen-base-10: frozen-base-9 + storage.c's lock-bank objects OBJ_LOCKS0..3 at 0xD4000..0xDBFFF (user
  decision 2026-10-24, option D)" + trailer; `git tag frozen-base-10 <that sha>` (lightweight, local);
  `git worktree remove /tmp/fb10`. Pin in tools/frozen_base.txt: `FROZEN_BASE frozen-base-10 <sha>` with a comment
  line in the frozen-base-9 style. Run `sh tools/upstream_build.sh`.
- [ ] **Step 5: Gate 2a self-test first.** In `selftest()` add storage.c cases mirroring the usb_app.c ones: the
  recorded change → allowed; the change + another edit → flagged; only the enum hunk → flagged; another address
  (0xD5000) → flagged; the change twice → flagged; a release that has it, storage.c equal → allowed, the old text
  back → flagged; the allowance excusing ota.c → flagged. Run `python3 tools/check_update_path.py --selftest` → FAIL.
- [ ] **Step 6: Implement** `STORAGE_C`, `ST_MAP_OLD/NEW`, `ST_ENUM_OLD/NEW`, `ST_SECTOR_OLD/NEW` (the exact
  release text and the Step 3 text) and `storage_locks_only(base_text, cur_text) -> bool` (cur is base with each
  OLD replaced by its NEW exactly once, and base has none of the NEWs); wire it into `source_errors` and add
  " + storage.c's recorded lock-bank objects (frozen-base-10)" to `source_note`. Self-test and `--sources` pass.
- [ ] **Step 7: Write the failing USR3 tests** in tests/boot_test.c (the log hook added to tests/flash_host.h):
  - `usr3_clamped_once`: USR3 header (magic FSMP, version 1) with `data_len 0x13E00` and 3 zones: A inside 48 KiB,
    B straddling 0xBE00, C starting past it. After `persist_boot`: the header at 0xC8000 has `data_len == 0xBE00`,
    `nz == 2`, B's `n == (0xBE00 - B.off) * 2` with `le < n` and `ls <= le` (`looped` 0 if its `ls` was cut);
    `crc` = CRC-32 of the first 0xBE00 data bytes; sector bytes 512..4095 equal the originals; `usr_nz[2] == 2`;
    `persist_note & PN_USR3`; the only writes are one erase of 0xC8000 and programs inside it. A second
    `persist_boot` writes nothing and sets no note.
  - `usr3_short_untouched`: `data_len 0xBE00` → no write, no note.
  - `usr3_clamp_fails_safe`: `host_fail_at` = the erase → `usr_nz[2] == 0` (USR3 unusable, never read past
    0xD3FFF), USR1/USR2 unchanged, nothing written outside 0xC8000.
  - `usr3_note_shown`: after boot + 3 `ui_frame()`s, the message is "USR3 SHORTENED TO 48K", then the note is 0.
- [ ] **Step 8: Run them** → FAIL.
- [ ] **Step 9: Implement.** eng_sample.c: `smp_user_scan` checks `data_len > smp_user_size(k) - SMP_USER_DATA`.
  project.c `smp_usr3_clamp()`, called in `persist_boot` after `fl_plain_window_init()` and before the
  `smp_user_scan` loop (and so before any lock-bank write): only for a header with FSMP, version 1, `nz` 1..16 and
  `data_len > 0xBE00`. The sector image is split across a static 256-B head buffer (bytes 0..255) and `st_buf`
  (bytes 256..4095, 3840 B); fields are edited in place by offset (`nz` +6, `data_len` +16, `crc` +20, zone i at
  +32+28i: `off` +0, `n` +4, `ls` +8, `le` +12, `looped` +27). Zones: one starting at or past 0xBE00 is dropped
  (later ones move down, `nz` shrinks); one straddling gets `n = (0xBE00 - off) * 2`, `le = min(le, n - 1)`, and if
  `ls > le`: `ls = le`, `looped = 0`. CRC over the new length is computed from the XIP window before the erase.
  Then `st_erase(0xC8000)`, program bytes 256..4095 first and the 256-B head last (the header is the commit).
  Set `PN_USR3` on success. `persist_note_show()` called from `ui_input` after `settings_poll()`. If check_erase_order
  or H2 then reports `st_save` changed (st_erase no longer inlined), mark felucca.c's `st_erase`
  `__attribute__((always_inline))` (felucca.c is not frozen).
- [ ] **Step 10: Run the Global Constraints suites** → all pass; `--sources` prints the frozen-base-10 note.
- [ ] **Step 11: Docs + commit.** README: USR3 holds up to 48 KiB (the rest holds the lock banks; an older upstream
  Felucca upload there overwrites them). DEVICE_INSTALL: a "Parameter locks" section, first item: boot with an
  80 KiB USR3 → "USR3 SHORTENED TO 48K" once, USR3 still plays. CHANGELOG line.
  `git add firmware/src/storage.c firmware/src/eng_sample.c firmware/src/project.c firmware/src/ui_input.c
  tools/frozen_base.txt tools/check_untouched.py tools/check_update_path.py tests/storage_test.c tests/flash_host.h
  tests/boot_test.c README.md docs/DEVICE_INSTALL.md CHANGELOG.md` (+ felucca.c if touched);
  commit "Lock banks: storage objects at 0xD4000..0xDBFFF (frozen-base-10), USR3 48 KiB".

### Task 2: The lock bank (sorted packed events)

**Files:**
- Create: `firmware/src/lockbank.c`
- Modify: `firmware/src/seq.c:516` (`#include "lockbank.c"` before motion.c), `firmware/src/motion.c:13-16`
  (the legacy `motion_event_t` / `motion_store_t` typedefs and size check move to lockbank.c, unchanged)
- Create: `tests/lock_test.c`; Modify: `tests/run_drum_tests.sh` (build + run it, as motion_test)

**Interfaces:**
- Produces (lockbank.c; pure data, no IRQ handling, callers guard):

```c
#define LK_MAX 1024u
#define LK_MAGIC 0x31424B4Cu                   /* "LKB1" */
enum { LK_MOTION = 1, LK_LOCK = 2, LK_BOTH = 3 };   /* kinds */
typedef struct {
    uint32_t magic;                             /* LK_MAGIC (lk_seal) */
    uint8_t slot, on;                           /* owning project slot 0..3; MOTION PLAY bits */
    uint16_t count;
    uint16_t key[LK_MAX];                       /* trk << 13 | step << 7 | param, ascending by (key, lock) */
    int8_t val[LK_MAX];
    uint8_t lock[LK_MAX / 8u];                  /* bit i: event i is a lock (snap back); else motion (hold) */
} lk_bank_t;                                    /* 3208 B: _Static_assert == 3208 and <= 3584 */
```
  `static uint32_t lk_key(uint32_t trk, uint32_t step, uint32_t param)`;
  `static int lk_is_lock(const lk_bank_t *b, uint32_t i)`;
  `static uint32_t lk_seek(const lk_bank_t *b, uint32_t trk, uint32_t step)` (binary search: first i with
  `key[i] >= lk_key(trk, step, 0)`; ≤ 11 probes at 1024);
  `static int lk_find(const lk_bank_t *b, uint32_t key, int lock)` (index or -1);
  `static int lk_put(lk_bank_t *b, uint32_t key, int32_t v, int lock)` (0 inserted, 1 replaced, -1 full: bank
  unchanged); `static void lk_del(lk_bank_t *b, uint32_t i)`;
  `static uint32_t lk_clear_track(lk_bank_t *b, uint32_t trk, uint32_t kinds)` (returns removed);
  `static uint32_t lk_clear_step(lk_bank_t *b, uint32_t trk, uint32_t step, const uint32_t *ids)` (locks only; `ids`
  a `MO_W`-word id mask, 0 = every id; returns removed);
  `static uint32_t lk_count(const lk_bank_t *b, uint32_t trk, uint32_t kinds)`;
  `static uint32_t lk_step_mask(const lk_bank_t *b, uint32_t trk, uint32_t first)` (bit j: step first+j has a lock,
  j < 16); `static int lk_valid(const lk_bank_t *b, uint32_t slot)`;
  `static void lk_seal(lk_bank_t *b, uint32_t slot)` (magic, slot, every entry ≥ count zeroed incl. bits);
  `static int lk_from_motion(lk_bank_t *b, uint32_t slot, const motion_store_t *m)` (1 converted, 0 broken → empty).
- Consumes: `motion_param(id)` (motion.c; forward-declared in lockbank.c), `MO_W`.

- [ ] **Step 1: Write the failing tests** in tests/lock_test.c (includes drum_host.h like motion_test.c):
  `lk_size` (`sizeof(lk_bank_t) == 3208`); `lk_put_keeps_order` (500 random puts: ascending `(key<<1)|lock`, counts
  right); `lk_put_replaces` (same key and kind: value replaced, count unchanged, returns 1); `lk_motion_and_lock_coexist`
  (same key, both kinds: 2 events, motion first, both found by `lk_find`); `lk_full_at_1024` (1024 distinct puts
  return 0, the 1025th returns -1 with the bank byte-identical; a replace at 1024 still returns 1);
  `lk_seek_matches_scan` (1024 random events: for every trk/step, `lk_seek` equals a linear scan's first index);
  `lk_valid_rejects` (each fails: magic, slot mismatch, count 1025, unsorted pair, duplicate (key, kind), param not
  `motion_param`, value -65; a sealed random bank passes); `lk_clear_kinds` (`lk_clear_track(…, LK_MOTION)` leaves
  locks, `LK_BOTH` leaves other tracks); `lk_clear_step_ids` (mask with E1 only removes E1's lock at that step, not
  its motion event, not CUT's lock); `lk_step_mask_bank` (locks at steps 3, 18 → mask(first 0) = 1<<3,
  mask(first 16) = 1<<2; a motion event sets no bit); `lk_from_motion_ok` (128 old events → 128 motion events, `on`
  copied); `lk_from_motion_broken` (an old event with trk 9, or a duplicate → 0, empty bank);
  `lk_seal_deterministic` (two banks, same events, different garbage tails: identical bytes after `lk_seal`).
- [ ] **Step 2: Run** `sh tests/run_drum_tests.sh` → FAIL (lockbank.c missing).
- [ ] **Step 3: Implement lockbank.c** with the signatures above. `lk_put`: binary search on `(key<<1)|lock`;
  insertion moves `key`/`val` with memmove and the lock bits one by one from `count` down to `i+1`; `lk_valid` is a
  single pass comparing each `(key<<1)|lock` with the previous one (strictly ascending ⇒ no duplicates).
- [ ] **Step 4: Run** → PASS (all old suites unchanged: nothing uses the bank yet).
- [ ] **Step 5: Commit** `firmware/src/lockbank.c firmware/src/seq.c firmware/src/motion.c tests/lock_test.c
  tests/run_drum_tests.sh`: "Lock bank: 1024 sorted packed events, binary-search lookup, O(n) validation".

### Task 3: Motion on the bank, FDRF projects, per-slot banks in flash

**Files:**
- Modify: `firmware/src/motion.c` (`mo.s` → `mo.b`, `mo.src` type, `motion_count`, `motion_add`, `motion_clear`,
  `motion_set_play`; `motion_valid` removed), `firmware/src/song.c:20-26, 90` (`chain_src_t.m`, `LK_NONE`),
  `firmware/src/project.c` (FDRF, FDRE converter, bank save / fetch / boot, `chain_prepare`),
  `firmware/src/ui_draw.c:1832-1862` (graph_motion reads the bank), `firmware/src/bench.c:168` (unchanged memset is
  fine: check), `firmware/src/ui_input.c` (LOCKS LOST on load)
- Modify (port, same assertions): `tests/motion_test.c` (`has_event` via `lk_find`; `test_full` at 1024),
  `tests/boot_test.c` (fdr8..fdre tests read `mo.b`), `tests/song_test.c`, `tests/sim_lab.c` (compile)
- Test: `tests/boot_test.c` (new tests below)
- Docs: README (1024 events per project; downgrade note), DEVICE_INSTALL, CHANGELOG

**Interfaces:**
- Consumes: Task 2's bank API; Task 1's `OBJ_LOCKS0`, `PN_LOCKS`.
- Produces: `mo.b` (`lk_bank_t`, the working bank, PLAY bits in `mo.b.on`), `mo.src` (`const lk_bank_t *`,
  0 = `&mo.b`), `static const lk_bank_t LK_NONE` (song.c, replaces `MOTION_NONE`);
  `motion_count(k)` = `lk_count(&mo.b, k, LK_MOTION)`; `motion_clear(k)` removes motion events only;
  `motion_add` capacity is the shared `LK_MAX` ("MOTION FULL" when `lk_put` returns -1).
- Produces (project.c): `#define PROJ_MAGIC 0x46524446u` ("FDRF"), `PROJ_MAGIC_VE 0x45524446u` ("FDRE");
  project_t's 516-B motion block becomes `uint32_t lk_link; uint16_t lk_n; uint8_t rsv_mo[510];` (zeroed; size
  stays 3840); `project_ve_t` (FDRE's layout, in `proj_old`); `static lk_bank_t lk_slot[4];` (.bss);
  `static uint8_t lk_lost;` (bit per slot); `static uint32_t proj_link(const lk_bank_t *b)` =
  `proj_hash(b, sizeof *b) | 1u`; `static int proj_bank_fetch(uint32_t slot)` (1 matched, 0 none needed (`lk_n`
  0): empty sealed bank, -1 lost: empty sealed bank and `lk_lost` bit); `proj_from_old(project_t *q, lk_bank_t *b,
  uint32_t ver)` (ver 14 = FDRE).

- [ ] **Step 1: Port the existing tests** (motion_test, boot_test fdr8/fdr9/fdra/fdrb/fdrc/fdrd, song_test) to the
  bank API with their assertions unchanged except `test_full` (now 1024). Run → FAIL to compile.
- [ ] **Step 2: Write the failing new tests** in tests/boot_test.c:
  - `fdrf_round_trip`: motion events on 3 tracks, 2 lock events (`lk_put(..., 1)`), `mo.b.on = 0xA1` →
    `project_save(2)`; wipe `mo`, `proj_slot`, `lk_slot`; `persist_boot(); project_load(2)` → `mo.b` events, bits
    and `on` equal; `proj_slot[2].magic == PROJ_MAGIC`, `rsv_mo` all 0, `sizeof(project_t) == 3840`.
  - `fdre_migrates`: an FDRE record (project_ve_t, 3 motion events, on 0x05) in slot 1's flash → load: 3 motion
    events (no lock bits), `on == 5`, every parameter and step as stored; no `lk_lost`.
  - `fdre_ram_refetched`: after that migration, unsaved, a second `persist_boot` (proj_slot kept as .noinit,
    `lk_slot` zeroed) → the same bank again, no LOCKS LOST.
  - `bank_scan_offset_erased`: 1024 events saved twice in slot 0 and once in slot 3 → for every written lock-bank
    sector, bytes +0xF00..+0xF4F are all 0xFF.
  - `bank_torn_keeps_pair` (Review Focus 1): save pair A (E1 lock 100); then save B (E1 lock 50) with
    `host_fail_at` on (i) the project's first program, (ii) the bank's header program, (iii) mid bank payload →
    after each, `persist_boot` + load: project A's knobs and lock 100 (never 50 with A, never A with an empty bank),
    no LOCKS LOST.
  - `usr3_upload_over_banks` (Review Focus 2): save slots 0..3 with locks; write an upstream-style 80 KiB USR3 image
    (valid FSMP header, data_len 0x13E00, random data) over 0xC8000..0xDBFFF; boot → USR3 shortened, every slot's
    project loads with an empty bank, `persist_note & PN_LOCKS`, first frames show "LOCKS LOST" once (after the
    USR3 note), loading each slot later says "LOADED"; no write outside the map.
  - `bad_bank_never_misread`: a bank with a valid storage header but (a) inner magic wrong, (b) slot 2's bank in
    slot 1's object, (c) unsorted, (d) count 1025, (e) hash ≠ the project's link → loads empty, LOCKS LOST.
  - `writes_stay_in_map`: boot + save of all 4 slots + rename: every `host_flog` entry inside the Global
    Constraints map.
- [ ] **Step 3: Run** → FAIL.
- [ ] **Step 4: Implement motion.c on the bank.** `motion_step` reads `mo.src ? mo.src : &mo.b`, finds the step's
  events with `lk_seek` and walks while `key >> 7 == trk << 6 | step` (motion events only in this task; behaviour
  otherwise as today). `chain_prepare`: `d->m = lk_valid(&lk_slot[i], i) ? &lk_slot[i] : &LK_NONE`.
- [ ] **Step 5: Implement FDRF.** `project_save(slot)`: `lk_slot[slot] = mo.b`, `lk_seal`, then
  `st_save(OBJ_LOCKS0 + slot, ...)` (error → "SAVE ERROR", project not written), then the project record with
  `lk_link = proj_link(&lk_slot[slot])`, `lk_n = count` (the project record is the commit of the pair).
  `proj_bank_fetch(slot)`: `lk_n == 0` → empty; else the current copy (`st_load`), and if it is not `lk_valid` for
  the slot or its link differs, the other copy via `st_head` + `st_body` (storage.c, callable in the unity build);
  neither → -1. `proj_fetch` calls it for an FDRF record; an old record converts through `proj_from_old` (motion →
  `lk_from_motion` into `lk_slot[slot]`, link set). `persist_boot`, after the USR3 clamp: per slot, `proj_fetch` if
  the RAM slot is not `proj_ok`, else `proj_bank_fetch`, and if that returns -1 a full `proj_fetch` (an FDRE
  converted in RAM but never saved converts again); still -1 → `persist_note |= PN_LOCKS`. `proj_load`:
  `mo.b = lk_slot[slot]` when `lk_valid`, else empty; a set `lk_lost` bit → "LOCKS LOST" (then cleared) instead of
  "LOADED". Showing the boot note `PN_LOCKS` clears every `lk_lost` bit (the loss is said once, not again per load).
- [ ] **Step 6: Run** the drum suite → PASS. Then the full suites. In the build output note `RAM .data+.bss` for the
  default and BLE builds against main's (expect about +17 KiB: 5 banks + 128 B of `proj_old`); over budget → stop
  and report.
- [ ] **Step 7: Docs + commit** (the files above + docs): "Motion in per-slot lock banks: 1024 events, FDRF
  projects, FDRE and older migrate, LOCKS LOST on a bad bank".

### Task 4: Lock playback (snap back, LFO, dormant steps, song rows, clears)

**Files:**
- Modify: `firmware/src/motion.c` (lock state, `motion_step`, `motion_restore`, `motion_end`, `motion_rebase`,
  `motion_capture`, `motion_forget`, the lock API), `firmware/src/song.c:93-96` (`chain_apply`),
  `firmware/src/ui.c:456-462` (`track_clear`), `firmware/src/project.c` (`preset_user_save` uses `motion_rest`)
- Test: `tests/lock_test.c`, `tests/ui_test.c` (the clear paths); `tests/drumsim.c` (an A/B render)
- Docs: CHANGELOG

**Interfaces:**
- Consumes: Task 3's `mo.b`, `mo.src`, `lk_*`.
- Produces (motion.c): state `mo.lact[NTRK][MO_W]` (knobs a lock holds now), `mo.under[NTRK][P_COUNT]` (the value
  each returns to), `mo.lfull` (a lock found the bank full);
  `static void lock_set(uint32_t k, uint32_t s, uint32_t id, int32_t v)` (IRQ off; full → `mo.lfull = 1`);
  `static int lock_get(uint32_t k, uint32_t s, uint32_t id, int16_t *v)` (1 locked);
  `static uint32_t lock_clear_step(uint32_t k, uint32_t s, const uint32_t *ids)` (IRQ off; returns removed);
  `static void lock_clear_track(uint32_t k)` (IRQ off; locks of k gone, its held ones snapped back);
  `static uint32_t lock_count(uint32_t k)`; `static void lock_release(uint32_t k)` (caller has IRQs off / is the
  ISR: every held lock of k back to `under`); `static int16_t motion_rest(uint32_t k, uint32_t id)` (`under` if
  locked now, else the base if taken, else `*motion_slot`).

`motion_step(t, s)` order (the signature does not determine it):
1. Snap back: every id in `lact[k]` → `*motion_slot(t, id) = under[k][id]`; bits cleared.
2. Motion as today, only when the PLAY bit is on (base taken first; step 0 restores the base).
3. One pass over the step's events (motion before lock at a key): motion events as today (PLAY on, model check);
   lock events only if `seq_steps(t)[s].on` (forward-declare `seq_steps`; dormant on an off step) and the model check
   passes: `under[k][id] = *motion_slot(t, id)`, then the clamped value, bit set.
4. Anything written in 1–3 and an LFO holds a knob this block: `lfo_restore_track` + `lfo_track(t, 0)` (as today).

Elsewhere: `motion_end` releases every track's locks before restoring the base; `motion_restore` writes `under`
instead of the knob for an id a lock holds; `motion_rebase` releases k's locks (non-E ids back to `under`, E ids
keep the new model's values); `motion_capture` (either branch) sets `under[k][id] = t->p[id]` when a lock holds
id; `motion_forget` clears `lact`; `chain_apply` calls `lock_release(k)` before `motion_restore(k)`;
`track_clear` calls `lock_clear_track` after `motion_clear` (CLEAR TRACK, TOOLS CLR*); `init_all` / BENCH's
memset of `mo` already empties both.

- [ ] **Step 1: Write the failing tests** in tests/lock_test.c (T1 kick on every 1/4, E1 base 64, as motion_test's
  `fresh`): `lock_snaps_back` (lock step 4 E1 100: 100 during step 4, 64 at step 5 and 0); `lock_plays_motion_off`
  (PLAY bit off: the lock plays; `motion_clear(0)` keeps `lock_count(0)`); `lock_over_motion` (motion step 2 E1 90,
  lock step 4 E1 110: steps 3/4/5 = 90/110/90; next bar's step 0 = 64);
  `lock_lfo_motion_same_knob` (Review Focus 3: LFO 1 on E1, motion step 4 E1 30, lock step 4 E1 100: during step 4
  the slot `motion_slot` returns is 100 and `lfo_live` swings round 100; step 5: 30; after `stop()`: E1 64, lsave
  64); `lock_dormant_off_step` (step 4 off: E1 64 there; on again: 100; `lock_count` unchanged throughout);
  `lock_song_row_change` (Review Focus 4: slot 0: T1 LEN 5, lock step 4 E1 100; slot 1: no locks; ref track LEN 16;
  rows 0,1 looped: in slot 1's row E1 is 64 at every step, back in slot 0's row step 4 is 100; the working bank
  untouched); `lock_turn_while_held` (playing, unarmed, E1 turned to 80 during locked step 4 via `motion_capture`:
  step 5 = 80, not 64); `lock_stop_releases` (STOP during step 4 → 64); `lock_model_change` (model change during
  step 4: no 100 in the new base); `lock_preset_rest` (`preset_user_save` during step 4 stores 64).
  In tests/ui_test.c: `test_clears_and_locks` (CLEAR TRACK and TOOLS CLR* remove T’s locks and motion; MOTION CLEAR
  keeps locks; INIT ALL leaves an empty bank; other tracks' locks kept).
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** as specified above.
- [ ] **Step 4: Run** → PASS; full suites; `tests/target_budget.py` within budget (report `motion_step`'s loop cost
  if the tool lists it), H3 stack unchanged.
- [ ] **Step 5: A/B render.** tests/drumsim.c writes `build/drum_renders/locks_off.wav` and `locks_on.wav` (the same
  4-bar pattern; on: locks on DECAY and CUT at steps 3, 7, 11, 15) for the owner to listen to.
- [ ] **Step 6: Commit** (files above + CHANGELOG): "Locks play: snap back after their step, LFO round the locked
  value, dormant on off steps, independent of MOTION PLAY / CLEAR, per song row".

### Task 5: ENV: STEPS / TRACKS key mode

**Files:**
- Modify: `firmware/src/ui.c:55-100` (ui fields), `:156-210` (`seq_keys`, `step_keys`, `env_steps`, `env_toggle`,
  `keys_toggle`), `firmware/src/ui_input.c:60-90` (`oct_leds`), `:137-160` (ENV LED), `:838-846`
  (`acc_oct_poll`), `:1011-1040` (`case B_ENV`, the OCT branch, `oct_up_job`), `env_poll`
- Test: `tests/ui_test.c`
- Docs: README (ENV), DEVICE_INSTALL, CHANGELOG

**Interfaces:**
- Produces: `ui.env` (0 TRACKS, 1 STEPS; not saved, 0 at power-on), `ui.env_t0` (press time | 1; bit 1: used by
  OCT±, no toggle on release); `static int seq_keys(void)` (today's `step_keys` expression, renamed);
  `static int env_steps(void)` (`ui.env && !ui.menu && !name_on()`); `static int step_keys(void)` =
  `seq_keys() || env_steps()`; `static void env_toggle(void)` (flips `ui.env`; turning on clears `ui.keys`;
  `grid_drop_holds()`; `song.seq_mode = seq_mode_now()`; `ui.force = 1`); `keys_toggle` turning KEYS on clears
  `ui.env`; `static void env_poll(uint32_t now)` (ENV let go: toggle unless used, or pressed under the menu / NAME /
  PRESETS / MUTE / PERFORM / a confirm); `static void oct_up_job(void)` (today's OCT+ page job: SOUND → LAYER,
  LFO RATE MODE; extracted unchanged).
- OCT branch order: MIDI LEARN; KEYS; ENV held → `bank_set(ui.bank ± 1)` and mark used; `seq_keys()` → today's;
  `env_steps()` → OCT+ sets `acc_oct = 1` (its page job on a lone release: `acc_oct_poll` calls `oct_up_job()`
  when `!seq_keys()`, else the bank as today), OCT- does its page job at the press (LAYER → SOUND; elsewhere
  nothing; Task 6 adds the held-step clear before this); otherwise today's.
- `oct_leds` in `env_steps() && !seq_keys()`: the page's own LEDs, OCT+ lit while `acc_oct`.

- [ ] **Step 1: Write the failing tests** in tests/ui_test.c: `test_env_toggle` (SOUND page: ENV tap → `ui.env 1`,
  ENV LED lit, a white key tap toggles T’s step in bank 0, selects no track, triggers no voice; ENV again → keys
  select tracks as before); `test_env_tracks_unchanged` (ENV toggled twice: LEDs, `song.seq_mode` and a key press on
  every family identical to before); `test_env_banks` (LEN 64: ENV held + OCT+ → bank 1, ENV released → still STEPS;
  ENV held + OCT- → bank 0; with ENV off the same on SOUND changes no bank);
  `test_env_oct_jobs` (STEPS on SOUND: OCT+ pressed and released alone → LAYER on the release, not the press; OCT+
  held + step key → accent toggled, no LAYER; on LAYER OCT- → SOUND; on an LFO page OCT+ alone → RATE MODE cycles;
  on SEQ STEP OCT+ alone → next bank as today); `test_env_keys_exclusive` (KEYS on, ENV tap → KEYS off, STEPS on;
  SCL → STEPS off, KEYS on); `test_env_not_saved` (save + reboot → `ui.env 0`); `test_env_home_and_tracks` (HOME
  screen and TRACKS with a track armed: keys are steps in STEPS mode; REC tap off TRACKS still gives REC ALL and
  key presses record no hits); `test_env_while_step_held` (Review Focus 5: STEPS on SOUND, key held 0.4 s (an off
  step turned on), ENV pressed and released while held → TRACKS mode, the key's release leaves the step on, a KNOB 2
  turn afterwards edits the knob normally and `lock_count` stays 0).
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** the interfaces above.
- [ ] **Step 4: Run** → PASS, every existing ui_test unchanged; full suites.
- [ ] **Step 5: Docs + commit** (README's key table gets ENV; DEVICE_INSTALL: ENV toggles, LED, banks with ENV +
  OCT±, OCT+ accent / page job; CHANGELOG): "ENV: the white keys as the selected track's steps on every page".

### Task 6: Lock entry and clearing

**Files:**
- Modify: `firmware/src/ui.c` (`lock_page`, `lock_step`, `lock_id`, `lock_shown`, ui fields),
  `firmware/src/ui_input.c:1125-1160` (knob loop: lock path first), the OCT- branch (clear), `mo.lfull` message,
  `firmware/src/ui_draw.c:186-205` (`head_mode`), `:2459` (HOME cells), `:2590-2600` (TRACKS LEVEL / PAN),
  `:2636-2660` (SC_TRACK cells)
- Test: `tests/ui_test.c`
- Docs: README, DEVICE_INSTALL, CHANGELOG

**Interfaces:**
- Consumes: Task 4's `lock_set`, `lock_get`, `lock_clear_step`, `motion_rest`, `mo.lfull`; Task 5's `env_steps`.
- Produces: `static int lock_page(void)` (`env_steps() && cur_fam() != FAM_SEQ`: HOME, TRACKS, SOUND, LAYER, FX,
  LFO, COMP); `static uint32_t lock_step(void)` (in `lock_page()`, the step of the last-pressed key still down
  (`ui.held`, `ui.step_t0`, `ui.step_si`), else `NSTEP`); `static uint32_t lock_id(uint32_t slot)` (the P_* id KNOB
  slot+1 shows here when `motion_param` allows it: `page_desc` on SC_TRACK, `home_param` on HOME, LEVEL / PAN on
  TRACKS; else `P_COUNT`); `static int16_t lock_shown(uint32_t id, int16_t live, int *locked)` (while
  `lock_step() < NSTEP`: the lock's value or `motion_rest`; else `live`); `#define LOCK_CLR_MS 600u`;
  `ui.ocd_t0` (OCT- press time | 1; bit 1 fired), `ui.ocd_si` (the step held at the press).
- Behaviour: with `lock_step() < NSTEP`, a knob turn on a lockable slot → `step_edit`'s preamble (every key down
  marked no-tap, `step_hold_on`), value = `knob_turn` from `lock_shown`, `lock_set`; the base and `t->p` untouched.
  A non-lockable knob while a step is held does nothing (owner's "where motion sequencing is possible"; the held
  step is its own context). OCT- pressed with a held step → no page job; released before 0.6 s → clear this page's
  lockable ids (`lock_id` of slots 0..3) at `ui.ocd_si`, "STEP n LOCKS CLEARED"; held 0.6 s → all of the step's
  locks at once, "STEP n ALL CLEARED", the release then does nothing. `mo.lfull` → "LOCKS FULL". SEQ pages: no lock
  path (STEP keeps PROB / RATCH / ACC).

- [ ] **Step 1: Write the failing tests** in tests/ui_test.c: `test_lock_entry_every_page` (STEPS mode, key for
  step 5 held, one detent right on each lockable knob of: every SOUND page (E0..E7), LAYER (LLEVEL LTUNE LDEC),
  FX (DIST CHOR DLY REV), SLICER, RESON (TUNE DECAY MIX TONE STRCT POS KEYF), FILTER (CUT RESO ENV DEC), each LFO
  page (RATE MORPH DEPTH), LFO DEST slots 3 / 4 DEPTH, HOME macros, TRACKS LEVEL / PAN → `lock_get(T, 4, id)` =
  rest + 1, `t->p[id]` and the base unchanged; MODEL, FILTER TYPE, RESON MODEL, LFO WAVE / DEST / SRC turned while
  held → nothing changes, no lock); `test_lock_before_hold` (a turn 0.1 s after the press: lock written, off step
  turned on, the release toggles nothing); `test_lock_cells` (held: the locked column's `ui.col` key ends in the
  CV_HOT mark with the locked value, an unlocked one shows the rest value; top line "LOCK STEP 5"; released: base
  values, no LOCK line); `test_lock_full` (1024 events, a held turn → "LOCKS FULL", count 1024);
  `test_lock_clear_page` (locks E1 (SOUND) and CUT (FILTER) at step 5, a motion event E1 at step 5; on SOUND: hold
  step 5 + OCT- tap → E1 lock gone, CUT lock and the motion event kept, "STEP 5 LOCKS CLEARED", no page job);
  `test_lock_clear_all` (hold + OCT- 0.6 s → both locks gone, "STEP 5 ALL CLEARED", the later release on LAYER does
  not go back to SOUND); `test_lock_oct_dn_alone` (no step held: LAYER OCT- → SOUND); `test_lock_step_page`
  (SEQ STEP in STEPS mode: held + KNOB 1/2/3 = PROB / RATCH / ACC, no lock); `test_lock_song_busy` (a song
  playing: a key hold gives no lock step, a turn edits nothing locked).
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement** the interfaces above; every cell that draws a lockable knob's value reads
  `lock_shown` and draws a locked one with `CV_HOT` (the spec's inverted value); `head_mode` returns "LOCK STEP n"
  first when `lock_step() < NSTEP`.
- [ ] **Step 4: Run** → PASS; full suites.
- [ ] **Step 5: Docs + commit** (README: hold a step + turn = lock; OCT- tap / 0.6 s clear; DEVICE_INSTALL checks
  per page; CHANGELOG): "Locks: hold a step and turn a knob on any recordable page; OCT- clears".

### Task 7: Display: footer lock marks, MOTION / LOCKS counts, screenshots

**Files:**
- Modify: `firmware/src/gfx.c:396-414` (`cv_lock_marks`), `firmware/src/ui_draw.c:2160-2240` (`draw_foot`: `ft`
  gets `lock_sig`), `:1864-1873` (`motion_columns`)
- Modify: `tests/run_drum_tests.sh` (`build/ui_shots/locks` in the mkdir and PNG lists)
- Test: `tests/ui_test.c`, `tests/redraw_test.c`
- Docs: README, CHANGELOG

**Interfaces:**
- Consumes: `lk_step_mask`, `lock_count`, `motion_count`, `env_steps`.
- Produces: `static void cv_lock_marks(int32_t x0, int32_t y, uint32_t n, uint32_t mask16, uint16_t ink)` (a
  10 x 1 px underline per marked step at `y + 12`, between the box (rows y..y+9) / playhead frame (to y+11) and the
  beat dot (y+14); the builder may adjust the pixels after the screenshot).
- Behaviour: the strip draws marks only in STEPS mode (`env_steps()`), mask =
  `lk_step_mask(&mo.b, song.sel, ui.bank * 16)` (a song row: its slot's bank); the strip redraws whole when the mask
  changes (`ft.lock_sig`). MOTION page: KNOB 2 "MOTION" = `motion_count(sel)`, KNOB 3 "LOCKS" = `lock_count(sel)`
  (the selected track's, as EVENT was).

- [ ] **Step 1: Write the failing tests:** `test_foot_lock_marks` (STEPS mode, locks at steps 3 and 18: bank 0
  shows a mark under step 4's box only (pixel check on the framebuffer row), bank 1 under step 3's; ENV off: no
  marks; shots `build/ui_shots/locks/foot_bank1.ppm`, `sound_held.ppm` (LOCK STEP 5, an inverted cell),
  `motion_counts.ppm`); `test_motion_page_counts` (columns 1 / 2 read "MOTION" 40 and "LOCKS" 212 for a bank built
  with those counts on the selected track); in tests/redraw_test.c `lock_turn_partial` (a held step's knob turn
  sends no more LCD traffic than a plain knob turn on the same page, plus the top line).
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** → PASS; look at `build/ui_shots/locks/*.png`; full suites.
- [ ] **Step 5: Commit** (files above + README / CHANGELOG): "Locks shown: footer underline in STEPS mode, MOTION
  page MOTION / LOCKS counts".

### Task 8: Web sim, docs, final integration

**Files:**
- Modify: `web/sim/controls.js:8` (ENV tip: "STEPS / TRACKS: the keys as the track's steps; hold a step + turn = a
  lock"), `web/sim/features.js` (a Parameter locks entry), `web/test_web.mjs` (ENV has a tip; the features list
  names locks)
- Modify: README.md, docs/DEVICE_INSTALL.md, CHANGELOG.md (final read-through: one coherent section each)

- [ ] **Step 1: Write the failing web test** (`node --test` assertion on the ENV tip and the feature entry); run → FAIL.
- [ ] **Step 2: Implement**; `tools/build_sim.sh`; run → PASS; open the sim once and check ENV, a lock, the marks.
- [ ] **Step 3: DEVICE_INSTALL "Parameter locks" checklist complete:** USR3 shortened note (Task 1); an FDRE project
  loads with its motion; save / power-cycle / load keeps locks; ENV LED and modes on SOUND, FX, LFO, HOME, TRACKS;
  hold + turn on each family; snap back audible; LFO on a locked knob; OCT- tap / 0.6 s; LOCKS FULL wording;
  footer marks; a song with two slots; an older Hortator shows LOAD FAILED for a saved project (optional, downgrade
  is not supported). No install / reinstall steps.
- [ ] **Step 4: Full verification:** every suite in Global Constraints; `grep -c divdi build/felucca.dis` = 0;
  `python3 tools/check_update_path.py --sources` shows the frozen-base-10 note; report RAM (.data+.bss, default and
  BLE, against main 2d7a0b1), target_budget and H3 numbers, and the A/B WAV and screenshot paths.
- [ ] **Step 5: Commit** "Parameter locks: web sim ENV, docs, checklist"; then one whole-branch review.

## Self-review notes

- Spec coverage: storage map / frozen change (T1), record format / 1024 / sorted lookup / O(n) validation (T2),
  FDRF / migration / LOCKS LOST / RAM (T3), lock vs motion playback / LFO / dormant / independent of PLAY & CLEAR /
  song rows / clears (T4), ENV / LED / banks / OCT+ accent / KEYS / REC ALL (T5), hold + turn / cells / LOCK STEP /
  LOCKS FULL / OCT- clears / STEP page unchanged (T6), footer marks / MOTION page counts (T7), docs / sim (T8).
- Decisions the spec left open or that needed resolving: (1) the 16-bit key has no spare bit and the value needs 8
  bits, and motion + lock on one knob and step must coexist, so the lock flag is a parallel 128-B bitmap and events
  sort by (key, flag): 3208 B per bank, RAM ≈ 5 × 3208 + 1120 B (`under`) ≈ 17.2 KiB, not the spec's ≈ 15.4 KiB;
  (2) 6 of the freed 516 B hold the project → bank link (hash, count) so a torn save keeps a matching pair; the rest
  stay zeroed; (3) the PLAY bits move into the bank header; (4) "length rewritten to 48 KiB" needs an erase of
  USR3's header sector (NOR cannot raise bits) and zones past 48 KiB dropped / truncated (otherwise the slot fails
  validation); a power cut during that one-time rewrite loses USR3's sample, never anything else; (5) off SEQ /
  TRACKS pages OCT± do the page's job and banks need ENV + OCT±; on SEQ / TRACKS their job is the bank, as today;
  (6) "LOCKS LOST once" is per slot: shown once (boot or that slot's next LOAD); (7) non-lockable knobs ignore turns
  while a step is held; (8) the MOTION page counts are the selected track's.
