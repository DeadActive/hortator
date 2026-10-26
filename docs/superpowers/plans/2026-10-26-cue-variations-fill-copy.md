# Live Cue, Variations, FILL, Slot Copy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Queue slots A–D and 8 variations per slot from SAVE held + the white keys, switching exactly at the
longest track's pattern end; FILL / !FILL step conditions played while REC is held off SEQ / SONG; COPY / PASTE /
undo of a whole slot on the projects page. Edits live in RAM until SAVE; flash is the truth after a reset.

**Architecture:** A pure module `firmware/src/varpack.c` packs variations 2–8 of a slot sparsely into one 3584 B
`var_bank_t` (as lockbank.c is a pure bank). Each slot's packed variations are a new flash object pair
(storage.c OBJ_VAR0..3 at 0xC0000.., cut from USR2, frozen-base-11), saved before the lock bank and the record,
which links it (as the lock bank). RAM holds two banks: the loaded slot's and one park buffer. `project.c` owns the
RAM model (write-back of the live state into the slot's RAM image, dirty bits, park, LOAD / SAVE / RENAME / PASTE);
a new `firmware/src/cue.c` (included from seq.c after song.c, so felucca.c and drum_host.h stay untouched) owns the
queue and the ISR switch at the boundary, reusing song.c's restart-on-the-grid code. The UI adds the SAVE-held cue
map (glo_input's pattern), REC held = FILL, and a COPY page.

**Tech Stack:** C (unity build: felucca.c includes every .c; seq.c includes lockbank.c / motion.c / song.c), host
tests in C (ASan / UBSan where the suite uses them), Python gate tools, the web sim (tools/build_sim.sh, node).

**Spec:** `docs/superpowers/specs/2026-10-26-cue-variations-fill-copy-design.md` (binding; read it with this plan).
Prior art for the pattern of every persistence step: `docs/superpowers/plans/2026-10-24-parameter-locks.md` and
commit 70f44a5 (frozen-base-10, the USR3 clamp, the lock-bank link).

## Global Constraints

- Never brick: no installers, no flashing, no device commands. Writes only to: settings 0xFC000/0xFD000, projects
  0x97000–0x9EFFF, USR2's header sector 0xB4000 (its one-time clamp only), variations 0xC0000–0xC7FFF (slot s, copy c
  at `0xC0000 + s*0x2000 + c*0x1000`), USR3's header sector 0xC8000 (its clamp), lock banks 0xD4000–0xDBFFF, user
  objects 0xDC000–0xDFFFF. A host test asserts no erase / program outside these (tests/flash_host.h
  `host_fl_in_map`, `host_fl_stray == 0`).
- Every variation payload ≤ 3584 B, so a sector's +0xF00.. stays 0xFF (the loader's erase scan, ldr_core.c).
- Frozen files byte-identical to the baseline except the approved storage.c object-table change, which moves the
  baseline to the local lightweight tag `frozen-base-11` (Task 1) exactly as frozen-base-10 did.
  `python3 tools/check_untouched.py` passes.
- No 64-bit divide (`grep -c divdi build/felucca.dis` → 0). No memmove (the firmware's libc has none).
- Budgets never raised (tests/target_budget.txt, H3 stack, build.py RAM / pool): over budget → stop and report.
  RAM per build (`.data+.bss` of 98304 B, build.py's note line; or `nm build/felucca.elf`: `_bss_end - 0x01C08000`):
  - BLE build (no USB AUDIO 10 CH, owner): main 85868 B → this work may add ≤ 7.5 KB (target ≈ 93.2 KB).
  - Plain build: main ≈ 73196 B (0.24.0's build); with usbmode's 10 CH (417f79d: 74720 B, pool 317556 of 344064 B)
    → ≈ 82.0 KB after the merge. Same ≤ 7.5 KB rule. The two `var_bank_t` buffers (7168 B) go in .bss, not the pool.
  - Each task that adds RAM records both builds' numbers in its commit message.
- ISR work at a switch is in `__attribute__((noinline))` functions with small frames (no array locals), so
  fm1_alnk0_irq's and chain_apply's target_budget lines and the H3 stack stay within tolerance.
- ISR safety: the main loop writes shared state with `fm1_irq_off()` / `fm1_irq_on()`; flash is never written while
  the transport runs (`transport_busy()`), so a validated flash (XIP) copy stays readable by the ISR until STOP.
- TDD: each behaviour's test is written and seen failing before the code.
- New files: `SPDX-License-Identifier: GPL-3.0-only` and `Drum machine fork: 2026 DEADACTIVE`; cue.c's header
  credits the behaviour to mono2k by Iñigo Hernández (GPL-3.0-only); no mono2k code is copied (if any is, its SPDX
  line and credit go with it).
- Messages fit `ui.msg` (23 chars): "STOP SONG TO CUE", "SAVE A FIRST", "VARIATIONS FULL", "SAVE TO A SLOT FIRST",
  "SLOT B EMPTY", "STOP TO PASTE", "COPY A SLOT FIRST", "SAME SLOT", "COPIED A", "PASTED A TO B", "PASTE UNDONE",
  "USR2 SHORTENED TO 48K", "USR2 CLAMP FAILED", "VARIATIONS LOST", "RENAMED (SAVE TO KEEP)"; confirm
  "DISCARD EDITS?"; top line "CUE>B", "CUE>B3", "CUE A3*", "FILL".
- Merge with usbmode (worktree ../felucca-usbmode, 417f79d) must stay easy. Files both touch and the rule here:
  felucca.c, ui_menu.c, drum_host.h: not touched (new .c files are included from seq.c); project.c: no edit within
  3 lines of persist_boot's `backlight = 0;` or of settings_save's palette word (usbmode's bit 12); ui_input.c: new
  calls go after `glo_input(now, notes);`, never next to ui_input's first lines; seq.c: cue code next to
  `chain_tick(seq_n)`, not the clock-lost test above it; ui.c: struct fields only, not drum_boot_init; tests: new
  files (var_test.c, cue_test.c), boot_test additions next to the USR3 checks; README / CHANGELOG / DEVICE_INSTALL:
  conflicts there are expected and trivial. **Settings bits: none added** (palette bit 12 is usbmode's).
- Suites, every task (in this order; gate 5 builds committed files, so commit first and fix up after):
  1. `sh tests/run_drum_tests.sh`
  2. `export PYTHON=$PWD/.venv/bin/python PATH="$PWD/.venv/bin:$PATH"; DRUM_PACKAGE=1 DRUM_BUNDLE=1 ./build.sh`
     (Docker running; retry up to 5x on "exec format error")
  3. `sh tests/run_tests.sh` (`sh tools/upstream_build.sh` first when H2/H3 report no or a stale baseline build)
  4. `tools/build_sim.sh`
  5. `(cd web && node --test test_web.mjs)`
  6. `python3 tools/check_untouched.py`
- Commits: explicit `git add <files>` (never `.venv`, never `-A`), message ends with
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. No push, merge, publish, release or tag except the
  local `frozen-base-11` (Task 1).
- The task that changes behaviour updates README.md, the docs/DEVICE_INSTALL.md checklist and CHANGELOG.md
  "## Unreleased" in the same commit.

## Review Focus

1. Power cut during a SAVE (variations → lock bank → record) or the USR2 clamp: the previous record, bank and
   variations load together; a cue, a write-back or a paste never writes flash. Pinned: Task 3
   `var_torn_keeps_triple`, Task 5 `cue_never_writes_flash`.
2. Cueing while following an external clock (CLK USB / TRS): the boundary comes from the clock's real advance (the
   MUTE NEXT BAR lesson, 0.23.x), never a block late or never. Pinned: Task 5 `cue_follow` (midi_clock_test.c).
3. REC held vs REC ALL: a tap off SEQ still toggles REC ALL / arms on the release; a hold ≥ 0.4 s is FILL and leaves
   the arm state alone; learned / SMC REC never fill. Pinned: Task 7 `test_rec_hold_vs_rec_all`.
4. PASTE then LOAD of the target: LOAD asks DISCARD EDITS?; NO keeps the paste, YES loads flash's and drops the
   undo; a cue of the target plays the paste. Pinned: Task 8 `paste_then_load`.
5. Variation edits while a cue is queued (and an edit that makes the playing slot dirty after its slot cue was
   accepted while the park holds another slot): the edits are packed, never lost; the switch is refused at the
   boundary with "SAVE C FIRST". Pinned: Task 5 `cue_var_edits_while_queued`, `cue_park_refused`.

## Decisions this plan makes where the spec is silent or contradicts the code

- REC taps already act on the release today (ui_input.c btn_hold): no added latency; only FILL's 400 ms hold is new.
- SAVE today opens the projects page on the press; it moves to the release (as FX / GLO) so SAVE can be held.
- "The same key again = at the next step": the soonest next step of any track.
- An empty variation plays no steps with variation 1's timing (LEN / DIV / SWG / SRC). (Owner may prefer "starts
  as a copy of the playing one"; one function, `cue_var_src`, to change.)
- Nothing loaded (power-on kit, no slot): a cue is refused ("SAVE TO A SLOT FIRST") — no silent loss.
- LOAD of another slot writes the loaded slot's live state back to its RAM image first (as a cue does).
- SAVE to another slot ("save as"): the old slot's RAM image goes back to its flash version (its edits are in the
  new slot). RENAME of a slot with RAM-only edits renames in RAM ("RENAMED (SAVE TO KEEP)").
- LOAD plays variation 1; a cue returns to the slot's last-played variation.
- A queued cue is dropped on STOP.
- PASTE's name: the source's + " COPY" if it fits in 12, else unchanged; no name → "COPY".
- COPY / PASTE live on a third SAVE page "COPY" (SLOT, COPY, PASTE) after TOOLS, so no existing page moves.
- Following a clock, the slot switch's 5 ms fade starts on the boundary's last pulse (as the COMP source's mute).

---

### Task 1: Flash map OBJ_VAR0..3, frozen-base-11, gate 2a, USR2 at 48 KiB

**Files:**
- Modify: `firmware/src/storage.c:19-23` (map comment, enum), `:51-60` (`st_sector`)
- Modify: `tools/frozen_base.txt`, `tools/check_untouched.py` (docstring), `tools/check_update_path.py`
- Modify: `firmware/src/eng_sample.c:36-56` (USR2's room), `firmware/src/project.c:848-990` (clamp, persist_boot),
  `firmware/src/ui_input.c:39-60` (notes)
- Test: `tests/storage_test.c`, `tests/flash_host.h:22-35` (the map), `tests/boot_test.c` (next to the USR3 checks)
- Docs: README.md (USR2 48 KiB), docs/DEVICE_INSTALL.md, CHANGELOG.md

**Interfaces:**
- Produces: `OBJ_VAR0 = OBJ_LOCKS0 + 4` (11), `OBJ_COUNT = OBJ_VAR0 + 4`;
  `st_sector(OBJ_VAR0 + s, c) == 0xC0000u + s * 0x2000u + c * 0x1000u`.
- Produces: `#define SMP_USR2_SIZE 0xC000u`; `smp_user_size(k)`: 0xC000 for k 1 and 2, else `SMP_USER_SIZE`.
- Produces: `static int smp_usr_clamp(uint32_t k)` (project.c; k 1 = USR2, 2 = USR3; 1 rewritten, 0 nothing, < 0
  flash error) replacing `smp_usr3_clamp`; `static uint8_t var_blocked;` (USR2's clamp failed: no variation write
  this boot); notes `PN_USR2 8u` ("USR2 SHORTENED TO 48K"), `PN_USR2F 16u` ("USR2 CLAMP FAILED").

- [ ] **Step 1: Failing map test** (tests/storage_test.c `var_sectors`): for s 0..3, c 0..1,
  `st_sector(OBJ_VAR0+s, c) == 0xC0000 + s*0x2000 + c*0x1000`; all `OBJ_COUNT` objects' sectors distinct, 4 KiB
  aligned, inside FL_DATA / FL_GLOB, never 0x9F000 / 0xFE000, none in 0xA0000–0xBFFFF or 0xC8000–0xD3FFF; a 3584 B
  payload into OBJ_VAR0+3 round-trips and its sector's bytes 0xF00..0xFFF stay 0xFF. Run `sh tests/run_tests.sh` →
  FAIL ("OBJ_VAR0 undeclared").
- [ ] **Step 2: storage.c**, exactly (gate 2a pins this text):
```c
/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000..): settings 0xFC000, projects 0x97000..0x9EFFF,
 * user sample slots 0xA0000..0xD3FFF (eng_sample.c) but 0xC0000..0xC7FFF the variations (project.c),
 * lock banks 0xD4000..0xDBFFF (project.c), user preset banks 0xDC000..0xDFFFF (upreset.c) */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_LOCKS0 = OBJ_UPRESET0 + 2,
       OBJ_VAR0 = OBJ_LOCKS0 + 4, OBJ_COUNT = OBJ_VAR0 + 4 };
```
  and in `st_sector`, before the `OBJ_LOCKS0` test:
```c
    if (obj >= OBJ_VAR0)
        return 0xC0000u + (obj - OBJ_VAR0) * 2u * ST_SECTOR + copy * ST_SECTOR;
```
- [ ] **Step 3: Move the baseline.** `git worktree add --detach /tmp/fb11 frozen-base-10`; copy the new storage.c
  there (frozen-base-10's storage.c equals main's); commit "frozen-base-11: frozen-base-10 + storage.c's variation
  objects OBJ_VAR0..3 at 0xC0000..0xC7FFF (owner decision 2026-10-26, USR2 → 48 KiB)" + trailer;
  `git tag frozen-base-11 <sha>`; `git worktree remove /tmp/fb11`. tools/frozen_base.txt: `FROZEN_BASE
  frozen-base-11 <sha>` with a comment line in the frozen-base-10 style; check_untouched.py's docstring names it.
  Run `sh tools/upstream_build.sh`.
- [ ] **Step 4: Gate 2a self-test first.** check_update_path.py `selftest()`: storage.c cases mirroring
  frozen-base-10's (base = 0.24.0's text): the recorded change → allowed; + another edit → flagged; only the enum
  hunk → flagged; another address (0xC1000) → flagged; twice → flagged; a release that has it, equal → allowed, the
  old text back → flagged. `python3 tools/check_update_path.py --selftest` → FAIL.
- [ ] **Step 5: Implement** `SV_MAP_OLD/NEW`, `SV_ENUM_OLD/NEW`, `SV_SECTOR_OLD/NEW` (0.24.0's text → Step 2's),
  `storage_vars_only(base_text, cur_text) -> bool` (as `storage_locks_only`; base must lack "OBJ_VAR0"); wire into
  `source_errors`; `source_note` adds " + storage.c's recorded variation objects (frozen-base-11)". Self-test and
  `--sources` pass.
- [ ] **Step 6: Failing USR2 tests** (tests/boot_test.c; tests/flash_host.h's map gains {0xB4000, 0xB5000} and
  {0xC0000, 0xC8000}): `usr2_clamped_once`, `usr2_short_untouched`, `usr2_clamp_fails_safe` — the three USR3 tests
  with k = 1 (header at 0xB4000, cut `0xC000 - 512` = 0xBE00 data bytes, zones past it dropped, one across it cut,
  CRC over the kept data, bytes 512..4095 of the sector kept; a failed erase → `usr_nz[1] == 0`, `var_blocked`,
  `persist_note & PN_USR2F`, nothing written outside 0xB4000's sector); `usr2_usr3_both`: both long → one boot
  shortens both, notes said USR2 then USR3, a second boot writes nothing. Run → FAIL.
- [ ] **Step 7: Implement** `smp_usr_clamp(k)` (smp_usr3_clamp with the slot as a parameter; its buffers renamed
  `usr_head` / `usr_at` / `usr_rd` / `usr_wr`), `smp_user_size`, persist_boot calling `smp_usr_clamp(1)` then
  `(2)` before any lock-bank or variation write, `persist_note_show` saying the two new notes (USR2 before USR3).
  `host_fl_stray == 0` holds in every boot test (its check text names USR2's header and the variations).
- [ ] **Step 8: Docs** (README: USR2 holds 48 KiB, its last 32 KiB are the projects' variations; an upload under
  upstream Felucca overwrites them; DEVICE_INSTALL: an 80 KiB USR2 shows `USR2 SHORTENED TO 48K` once; CHANGELOG).
  Suites 1–6; commit (storage.c, tools/*, eng_sample.c, project.c, ui_input.c, tests/*, docs).

### Task 2: Variation packing (varpack.c)

**Files:**
- Create: `firmware/src/varpack.c` (pure data, no IRQ handling; included by seq.c right after lockbank.c)
- Modify: `firmware/src/seq.c:516` (the include)
- Create: `tests/var_test.c`; Modify: `tests/run_drum_tests.sh` (built with `-fsanitize=address,undefined` as
  lock_test, right after song_test)

**Interfaces:**
- Produces:
```c
#define VAR_N 8u                                /* variations a slot: 1 (the record's steps) + 7 in the bank */
#define VAR_BYTES 3584u                         /* the object, +0xF00 of its sector stays erased */
#define VAR_MAGIC 0x31524156u                   /* "VAR1" */
typedef struct {
    uint32_t magic;
    uint8_t slot, rsv;
    uint16_t used;                              /* bytes of d in use: variations 2..8 in order */
    uint8_t d[VAR_BYTES - 8u];
} var_bank_t;                                   /* stored length: 8 + used */
```
  An entry: `uint16_t n` (0 = empty: 2 B), then per track: LEN, DIV, SWG, SRC (one byte each, CHAIN_TIMING order),
  the 64-bit trig bitmap (8 B, little-endian, bit i = step i on), then 3 B per on step in step order: flags
  (`on | acc<<1 | slide<<2 | tie<<3 | rat<<4`, written from the fields, not the bitfield's bytes), `cond`, `nt`.
- `static void var_empty(var_bank_t *b, uint32_t slot)` — 7 empty entries (`used == 14`), the rest 0.
- `static uint32_t var_need(const step_t *const st[NTRK])` — the bytes an entry takes (2 when no step is on).
- `static int var_put(var_bank_t *b, uint32_t v, const step_t *const st[NTRK], const int16_t tm[NTRK][4])` —
  v 1..7 (variation v + 1): 0 stored (bytes changed), 1 the same bytes (nothing changed), -1 VARIATIONS FULL (b
  untouched). The tail moves in place (no memmove).
- `static int var_get(const var_bank_t *b, uint32_t v, step_t *const st[NTRK], int16_t tm[NTRK][4])` — 1 written
  (off steps all zero, values clamped: LEN 1..NSTEP, DIV 0..9, SWG 0..100, SRC its range, cond ≤ COND_MAX, nt
  NT_MIN..NT_MAX), 0 empty (steps cleared, tm untouched), -1 malformed (as empty). Reads only `b` (works on XIP).
- `static int var_valid(const var_bank_t *b, uint32_t slot, uint32_t len)` — magic, slot, `len == 8 + used`,
  `used <= VAR_BYTES - 8`, exactly 7 entries ending at `used`, each entry's size = 32 + 3 × its bitmaps' popcount.
- `static uint32_t var_mask(const var_bank_t *b)` — bit v (1..7) set: variation v + 1 not empty.

- [ ] **Step 1: Failing tests** (tests/var_test.c, `check("...", fn())` as lock_test.c):
  - `var_round_trip`: seeded random steps (cond incl. 57 / 58, nt −60 / 67, rat 3, slide, tie, acc), LEN 1 / 64,
    DIV 9, SWG 100 → `var_put(b, 3, …) == 0`, `var_get(b, 3, …) == 1`: on steps equal field by field, off steps
    all zero, timing equal; v 1, 2, 4..7 still `var_get == 0`.
  - `var_sizes`: after `var_empty`, `used == 14`; 16 on steps per track → `var_need == 2 + 8*12 + 3*128 == 482`;
    all 512 on → `1634`.
  - `var_full`: two dense variations fit (`used == 14 - 4 + 2*1634`), the third → -1 and the bank's bytes equal a
    copy taken before.
  - `var_replace_shrink`: dense at v 2, then sparse at v 2 → `used` shrinks by the difference; v 3..7 decode as
    before.
  - `var_same_bytes`: the same content twice → second `var_put == 1`.
  - `var_valid_rejects`: a valid bank → 1; wrong magic, slot, `len`, `used` past the end, an entry past `used`, a
    popcount mismatch, an all-0xFF buffer → 0; `var_get` on each malformed one never reads past `VAR_BYTES`
    (ASan).
  Run `sh tests/run_drum_tests.sh` → FAIL (no varpack.c).
- [ ] **Step 2: Implement** varpack.c with `_Static_assert(sizeof(var_bank_t) == VAR_BYTES && VAR_BYTES <= 3584u)`
  and `_Static_assert(NSTEP == 64 && NTRK == 8)`; the include in seq.c.
- [ ] **Step 3:** var_test passes; suites 1–6; commit (varpack.c, seq.c, tests/var_test.c, run_drum_tests.sh).

### Task 3: Persistence — the variation object, its link, flash is the truth at power-on

**Files:**
- Modify: `firmware/src/project.c` (project_t `:57-61`, the RAM banks after `lk_slot` `:88`, `proj_fetch`
  `:441-471`, `project_save` `:491-541`, `persist_boot` `:918-992`, `project_rename` `:1004-1040`)
- Modify: `firmware/src/ui_input.c:39-60` (`PN_VARS 32u`, "VARIATIONS LOST")
- Create: `tests/cue_test.c` (FELUCCA_FLASH 1, `hflash` + `SMP_USER_XIP` + `VAR_XIP` as boot_test.c, ui_host.h;
  ASan + UBSan), add it to `tests/run_drum_tests.sh` after boot_test
- Modify: `tests/boot_test.c` (the FDRF check: 504 reserved bytes, 0)
- Docs: CHANGELOG (project format note), README (variations saved with SAVE)

**Interfaces:**
- project_t: `uint32_t lk_link; uint32_t var_link; uint16_t var_len; uint8_t rsv_mo[504];` (still 3840 B, still
  "FDRF": a 0.24.0 record has 0 there = no variations). `var_len`: the object's stored length, 0 none.
- `static var_bank_t var_buf[2];` `static uint8_t var_who[2];` (slot + 1 each holds, 0 none),
  `static uint8_t var_play;` (index of the loaded slot's bank), `static uint8_t var_dirty, var_lost;` (bits per
  slot: its RAM variations are not flash's; its linked object was unreadable).
- `#ifndef VAR_XIP` `#define VAR_XIP(off) fm1_xip_ptr(off)` (tests / sim: `hflash + off`).
- `static uint32_t var_link_of(const var_bank_t *b)` = `proj_hash(b, 8 + b->used) | 1u`.
- `static const var_bank_t *proj_var_flash(uint32_t slot)` — the copy of OBJ_VAR0 + slot (either copy, as
  proj_bank_fetch) that `var_valid` accepts with the record's `var_len` and `var_link`, as an XIP pointer; 0 when
  `var_len == 0` or none matches (then `var_lost` bit).
- `static int proj_var_load(uint32_t slot, var_bank_t *dst)` — dst = that copy or `var_empty`: 1, 0 none, -1 lost.
- `project_save` (stopped, flash_ok): the loaded slot's bank in `var_buf[var_play]` (Task 4 fills the live
  variation in) → `st_save(OBJ_VAR0 + s, b, 8 + used)`, then the lock bank, then the record with `var_link` /
  `var_len` (the record last: the commit). `var_blocked` → "USR2 CLAMP FAILED", RAM kept, nothing written.
- `persist_boot` (flash_ok): every slot `proj_fetch` from flash (the .noinit image is not trusted any more), the
  variation link checked (`proj_var_flash`; lost → `PN_VARS`), `var_who[] = 0`, `var_dirty = 0`. Without flash
  (`!flash_ok`) the RAM slots stay as today.

- [ ] **Step 1: Failing tests** (tests/cue_test.c):
  - `var_save_load_round_trip`: LOAD A, set variation 3 in `var_buf[var_play]` with `var_put`, SAVE A; wipe RAM
    (`memset` proj_slot / var_buf / lk_slot, `persist_boot`) → LOAD A → `var_get(v 3)` equal; the record's
    `var_link == var_link_of(flash payload)` and `var_len == 8 + used`.
  - `var_torn_keeps_triple`: for every erase / program index i of one SAVE (`host_fail_at = i`), then
    `persist_boot` + LOAD: record, bank and variations are all the old save's or all the new one's (compare
    against both), never mixed; `host_fl_stray == 0`.
  - `var_old_record_empty`: a 0.24.0 FDRF record (rsv zero) loads; variations 2–8 empty; no VARIATIONS LOST.
  - `var_upload_over_vars`: after a SAVE with variations, an 80 KiB USR2 image written over 0xB4000..0xC7FFF →
    boot: "USR2 SHORTENED TO 48K" then "VARIATIONS LOST" once; the record and its locks load; variations empty.
  - `flash_is_truth_after_reset`: change `proj_slot[1]` (a step, its sum redone, as a write-back will), then
    `persist_boot` again with .noinit kept → `proj_slot[1]` equals the flash record byte for byte.
  - `var_writes_in_map`: boot, four SAVEs with dense variations, a RENAME → `host_fl_stray == 0`; every variation op
    inside 0xC0000..0xC7FFF; each variation sector's bytes 0xF00..0xFFF erased.
  - `var_blocked_refuses`: USR2's clamp fails → SAVE says "USR2 CLAMP FAILED" and writes nothing.
  Run → FAIL.
- [ ] **Step 2: Implement** the record fields, the banks, `var_link_of`, `proj_var_flash`, `proj_var_load`, the
  SAVE order, persist_boot's refetch, `PN_VARS`. LOAD (`proj_load`) fills `var_buf[var_play]` with the slot's
  variations (`proj_var_load`) and sets `var_who[var_play]`.
- [ ] **Step 3:** boot_test's FDRF check updated (504 reserved bytes stay 0); suites 1–6; record RAM (both builds)
  in the commit message; commit.

### Task 4: The RAM model — write-back, unsaved marks, park, LOAD / SAVE-as / RENAME

**Files:**
- Modify: `firmware/src/project.c` (`project_save` split, `proj_load`, `project_rename`, new functions)
- Modify: `firmware/src/ui_input.c:556-572` (G_LOAD → confirm), the confirm handler `:1065-1100`
- Modify: `firmware/src/ui_draw.c:1153-1170` (`graph_slots`: "*" after a dirty slot's letter), `:2750-2765`
  (confirm text), `firmware/src/ui.c:75-80` (the confirm codes comment)
- Test: `tests/cue_test.c`
- Docs: README (edits live in RAM until SAVE; LOAD asks), DEVICE_INSTALL, CHANGELOG

**Interfaces:**
- Consumes: Task 3's banks, `var_put` / `var_get`, `proj_var_load`.
- Produces:
  - `static uint8_t proj_dirty;` bit per slot: its RAM image (record, lock bank or variations) is not flash's.
  - `static uint32_t proj_fsum[4];` the flash record's `sum` (set by fetch and SAVE).
  - `static uint8_t cue_var;` the variation playing (0 = variation 1) — moves into cue.c's struct in Task 5.
  - `static void proj_image(project_t *p, uint32_t slot, int globals)` — the record from the live state with the
    knobs at rest (what STOP leaves: the motion base, a held lock's under value, an LFO's lsave); playing a
    variation other than 1, the steps and CHAIN_TIMING values come from the slot's image (variation 1); `globals`
    0 keeps the image's `g[]` and `sel`.
  - `static int proj_writeback(void)` — the loaded slot's live state into its image (`proj_image(…, 0)`), its lock
    bank (`mo.b` at rest), the playing variation into `var_buf[var_play]` (v ≥ 1) or the image's steps (v 0);
    sets `proj_dirty` / `var_dirty` when bytes changed; 0, or -1 VARIATIONS FULL (nothing changed). Callable with
    the IRQ off from the main loop and from the ISR (no message, no flash).
  - `static int var_room(uint32_t out)` — the variations of slot `out` (leaving) can be kept: 0 (its bank becomes
    the park when it is dirty, else it is just dropped), or `1 + c` when the park holds slot c's unsaved variations.
  - `static uint32_t live_sum(void)`, `static uint32_t live_sum0;` — FNV over what `proj_writeback` would write;
    taken at LOAD, SAVE and a switch.
  - `static int slot_dirty(uint32_t s)` (main loop) — `proj_dirty`/`var_dirty` bit, or for the loaded slot also
    `live_sum() != live_sum0`.
  - `#define CONFIRM_DISCARD 7u` (ui.confirm, `confirm_trk` = the slot); `static void project_discard(uint32_t s)`
    — image, bank, variations back to flash's (`proj_fetch`, its park dropped), bits cleared.
- Behaviour: LOAD of a dirty slot asks "DISCARD EDITS?" (YES: `project_discard`, then the load; NO: nothing).
  LOAD of slot s ≠ the loaded A: `proj_writeback` + `var_room(A)` first ("VARIATIONS FULL" / "SAVE C FIRST":
  nothing loads). SAVE of slot s: `proj_image(…, 1)`, the live variation packed (VARIATIONS FULL: no save, says
  so); s ≠ A ("save as"): after a good save, A → `project_discard(A)`, `var_who[var_play] = s + 1`, a park of s
  dropped. RENAME of a slot with `proj_dirty` set: the name in RAM only, "RENAMED (SAVE TO KEEP)", no flash op.

- [ ] **Step 1: Failing tests** (tests/cue_test.c):
  - `writeback_keeps_edits`: LOAD A, toggle a step and turn P_LEVEL, `proj_writeback()` → the image has both,
    `proj_dirty & 1`, `host_nflog` unchanged.
  - `writeback_rest_values`: playing, motion moving P_E0 and a lock on step 0 held → the image's P_E0 equals what
    STOP leaves (`motion_end` on a copy of the state).
  - `writeback_var_not_record`: playing variation 3 (`cue_var = 2`, live steps / LEN 12) → the image's steps and
    LEN are variation 1's, `var_get(v 2)` equals the live ones.
  - `load_dirty_asks`: dirty B; LOAD B (two detents) → `ui.confirm == CONFIRM_DISCARD`, nothing loaded; OCT- →
    unchanged; again + OCT+ → `proj_slot[1]` equals flash's record, live = it, "LOADED".
  - `load_other_writes_back`: A loaded with an edit, LOAD clean B → no confirm; A's image has the edit; LOAD A →
    asks.
  - `load_park_full_refused`: A with dirty variations, the park holding C dirty → LOAD B says "SAVE C FIRST", the
    live state unchanged.
  - `save_as_moves_edits`: A loaded with edits and variations, SAVE to B → flash B decodes to A's live state and
    variations; A equals its flash record, `!slot_dirty(0)`; `chain.from == 2`.
  - `rename_dirty_ram_only`: dirty B renamed → no flash op, RAM name new, "RENAMED (SAVE TO KEEP)"; clean C renamed →
    written as before.
  - `slot_dirty_marks`: `slot_dirty` true for a live edit of the loaded slot, a written-back slot, a parked dirty
    slot; false after SAVE and after `project_discard`.
  Run → FAIL.
- [ ] **Step 2: Implement** the interfaces; `project_save` uses `proj_image(…, 1)`; `proj_load` and the G_LOAD path
  as above; `graph_slots` shows "*" for `slot_dirty` (the exact pixels are the builder's; give the owner a sim
  screenshot).
- [ ] **Step 3: Docs** (README: edits stay in RAM until SAVE, a reset or power cut brings the saved version back;
  LOAD of an edited slot asks DISCARD EDITS?; SAVE to another slot; DEVICE_INSTALL items for each). Suites 1–6;
  RAM numbers; commit.

### Task 5: The cue engine (cue.c): queue, boundary, switch, declick

**Files:**
- Create: `firmware/src/cue.c` (included from seq.c right after song.c)
- Modify: `firmware/src/song.c:83-115` (`chain_apply`'s restart loop factored out), `firmware/src/seq.c`
  (`seq_start` / `seq_stop` hooks `:540-560`, `cue_tick` next to `chain_tick` `:686`), `firmware/src/fx.c` (mix_part: the fade gain), `firmware/src/project.c`
  (`proj_cue_out`, `proj_cue_in`; `cue_var` moves into `cue`), `tests/target_budget.py` + `tests/target_budget.txt`
  (one new line, `cue_switch`, its first measured value; no existing line changed)
- Test: `tests/cue_test.c`, `tests/midi_clock_test.c`
- Docs: README (cue, boundary, again = now, globals kept), DEVICE_INSTALL, CHANGELOG

**Interfaces:**
- Consumes: `proj_writeback`, `var_room`, `var_get`, `proj_var_flash`, `live_sum`, song.c's `step24`, `B24_WRAP`,
  seq.c's `MUTE_LEAD`, `midi_clock.pulse_samples`.
- Produces (cue.c):
```c
static struct {
    volatile uint8_t q_slot, q_var;   /* queued: slot + 1 / variation + 1, 0 none (main writes, the ISR takes) */
    volatile uint8_t now;             /* the same key again: switch at the next step */
    uint8_t var;                      /* the variation playing, 0 = variation 1 (the record's steps) */
    uint8_t last[4];                  /* each slot's last-played variation (RAM only; power-on 0) */
    uint16_t b24;                     /* 1/24 beats since PLAY at the last restart, mod B24_WRAP */
    const var_bank_t *src;            /* the queued slot's variations: var_buf[..] or a flash (XIP) copy, 0 none */
    volatile uint8_t done, err;       /* ISR -> main: CUE_SW_SLOT | CUE_SW_VAR; a refusal: CUE_E_FULL, CUE_E_PARK + c */
} cue;
```
  - `static int cue_slot(uint32_t s)`, `static int cue_var(uint32_t v)` (main) — queue, again = now (same key while
    queued), cancel (the playing slot's / variation's key with something queued), stopped: switch at once (IRQ
    off). A variation key queues a variation of the queued slot if one is queued, else of the playing slot.
    Returns 0 or the refusal the UI says: `CUE_E_SONG` ("STOP SONG TO CUE"), `CUE_E_NOSLOT` ("SAVE TO A SLOT
    FIRST"), `CUE_E_EMPTY` ("SLOT B EMPTY"), `CUE_E_PARK + c` ("SAVE C FIRST", checked at queue time with
    `var_room`).
  - `static void cue_tick(uint32_t adv, uint32_t lead)` (ISR, `events_block`, when playing and `!chain.running`;
    `adv` = `seq_n`, the real advance; `lead` = `MUTE_LEAD` on the own clock, `max(pulse_samples, MUTE_LEAD)`
    following, as `mute_bar`).
  - `static __attribute__((noinline)) int cue_switch(uint32_t x)` (ISR at x samples into the block, or the main
    loop with the IRQ off when stopped): checks first, mutates after — `proj_cue_out()` (-1: `CUE_E_FULL`, no
    switch); `var_room` (taken: `CUE_E_PARK + c`, no switch); then `proj_cue_in`, the restart, `cue.b24`,
    `cue.last[]`, `done`.
  - `static void cue_poll(void)` (main, first thing in `ui_input` after `glo_input`): finishes a switch — the old
    slot's `sum`, its dirty bits, `chain.from` / `chain.name` / `chain.cfg` from the new slot, the variations'
    roles (`var_play` / `var_who`; a flash-sourced slot's object copied into `var_buf[var_play]` with the IRQ off),
    `live_sum0`; says `err` once.
  - `static void cue_start(void)` / `cue_stop(void)` (seq_start / seq_stop): b24 0; STOP drops a queued cue.
  - song.c: `static uint32_t seq_restart(uint32_t x, uint32_t b24)` — every track at step 0, x samples into the
    block, its remainder / swing pair from b24 (chain_apply's per-track timing code, moved), returns the reference
    (longest LEN×DIV, ties the lowest track); `chain_apply` calls it.
  - project.c: `static int proj_cue_out(void)` (= `proj_writeback` + `var_room` check, nothing changed on failure);
    `static void proj_cue_in(uint32_t s, uint32_t v, const var_bank_t *src)` — every track's sound, presets and
    model from s's image (the caps of `proj_load`), the steps and CHAIN_TIMING of variation v (`var_get`; empty: no
    steps, variation 1's timing), `mo.b = lk_slot[s]` (valid) with the motion base retaken; P_MUTE, `song.g`,
    `song.sel` and the master FX untouched. `proj_load` shares the per-track code (`proj_tracks_in`).
  - fx.c: `static uint16_t mix_fade = 32767u;` Q15, every track's block multiplied before its sends while < 32767.
- Boundary rules: each block the reference is re-picked; the switch is in the block where the reference's last step
  (seq_idx + 1 == LEN) ends (`seq_pos + adv >= cur`, as `chain_tick`), x = `cur - seq_pos`; `now`: where the
  soonest next step of any track begins. `b24 += (t.seq_cnt + 1) * step24(t's DIV)` (mod B24_WRAP) for the track
  that set the boundary. Slot switches only: `mix_fade` ramps 32767 → 0 over FS×5/1000 samples starting when the
  boundary is within `lead`, every track `drum_cut` at the switch, 32767 again after it. Variation switches: no
  fade, voices ring on.

- [ ] **Step 1: Failing tests** (tests/cue_test.c, rendering with `render_mix` as song_test.c):
  - `cue_boundary_lens`: {LEN, DIV} = {16, 1/16}, {12, 1/16}, {7, 1/8T}, {64, 1/32}, {3, 1/4}, a shorter second
    track: variation 2 queued mid-pattern → its first step-0 hit is at exactly PLAY + LEN × step samples (hit_age);
    every old hit before it played.
  - `cue_grid_kept`: SWG 60, 1/16: 4 bars after a switch, every hit on the same sample as a never-switched loop.
  - `cue_again_now`: B queued, B pressed again mid-step → the switch at the soonest next step (to the sample).
  - `cue_stopped_immediate`: stopped, cue B → live = B's image at once, variation = `cue.last[1]`.
  - `cue_globals_kept`: playing, cue B → BPM, SWING, CLOCK, MIDI / SYNC / ROUTE, DLY / REV / COMP, Grids, every
    P_MUTE and `song.sel` as before the switch.
  - `cue_round_trip_edits`: A plays variation 3, a step edited, cue B, cue A → A comes back on variation 3 with the
    edit.
  - `cue_var_edits_while_queued` (Review Focus 5): variation 2 plays, variation 3 queued, a variation-2 step
    edited before the boundary → after the switch `var_get(v 1)` has it; cue variation 2 → it plays.
  - `cue_var_full`: the bank nearly full, live variation 2 made dense, variation 3 queued → at the boundary no
    switch, variation 2 plays on with its edits, "VARIATIONS FULL" once.
  - `cue_park_refused` (Review Focus 5): C parked dirty, A dirty → cue B refused "SAVE C FIRST", nothing queued; A
    clean → accepted; an edit to A afterwards → the switch refused at the boundary, "SAVE C FIRST", A's edit kept.
  - `cue_song_refused`: SONG running → "STOP SONG TO CUE", nothing queued.
  - `cue_fade`: slot switch with a long-decay voice ringing → peak |out| of the last 64 samples before the boundary
    < 1/8 of the peak 20 ms before; B's step-0 hit equals a fresh PLAY of B's first block within 1 %. Variation
    switch → output up to the boundary bit-identical to an unswitched run.
  - `song_plays_var1`: the loaded slot playing variation 3, written back; a SONG row of that slot plays variation
    1's steps (chain_prepare reads the image), and STOP returns to variation 3's live steps.
  - `cue_never_writes_flash` (Review Focus 1): 20 cues (slot and variation, playing and stopped, again = now) →
    `host_nflog` unchanged.
  - midi_clock_test.c `cue_follow(bpm, src)` (Review Focus 2), CLK USB and TRS at 90, 120 and 174.6 BPM, as
    `mute_follow`: variation 2 queued mid-pattern → its step 0 on the pulse that ends the pattern (the sample a plain
    loop's step 0 has there), never a block late, never missed; a slot switch's fade starts on the boundary's last
    pulse.
  Run → FAIL.
- [ ] **Step 2: Implement** cue.c, `seq_restart`, `proj_cue_out` / `proj_cue_in` / `proj_tracks_in`, the fx.c
  gain, the hooks. song_test passes unchanged (chain_apply's behaviour).
- [ ] **Step 3: Budgets.** `python3 tests/target_budget.py` (inside suite 3): fm1_alnk0_irq and chain_apply within
  tolerance; add `cue_switch` to FUNCS and its one line (first measured value) to target_budget.txt; H3 stack
  within budget. Over → stop and report.
- [ ] **Step 4: Docs** (README: cue rules, globals kept, the 5 ms declick and the clock-follow note; DEVICE_INSTALL;
  CHANGELOG). Suites 1–6; RAM numbers; commit.

### Task 6: The cue map on SAVE held

**Files:**
- Modify: `firmware/src/ui_input.c` (`cue_input` after `glo_input`; the family-button loop `:1170-1175`: B_SAVE on
  the release; `ui_leds` `:184-200`: the map's keys), `firmware/src/seq.c:12-15` (mute_mask's comment: GLO's or
  SAVE's bit), `firmware/src/ui.c` (struct: `uint32_t cue_t0; uint8_t cue_map;`), `firmware/src/ui_draw.c:189-210`
  (`head_mode`)
- Test: `tests/ui_test.c` (RAM build), `tests/cue_test.c` (with flash)
- Docs: README (SAVE held), DEVICE_INSTALL, CHANGELOG

**Interfaces:**
- Consumes: `cue_slot`, `cue_var`, `slot_dirty`, `var_mask`, `project_used`.
- Produces: `#define CUE_HOLD_MS 400u`; `static void cue_input(uint32_t now, uint32_t hits)` (SAVE as GLO:
  `CUE_DOWN / CUE_OPEN / CUE_DEAD / CUE_PLAIN` flags; a key pressed with SAVE or a hold ≥ 400 ms opens the map;
  a tap opens the projects page on the release; dead in the menu, a confirm, NAME, the PRESETS list, the MUTE
  screen; plain with PERFORM up or FX held); `static void cue_keys(uint32_t hits)` (white key n = `STEP_KEY[n-1]`:
  1–4 slots A–D, 5–12 variations 1–8; others nothing); `static int cue_led(uint32_t n, uint32_t now)` (slots: lit
  = used, wink = playing, blink = queued, a dirty slot breathes; variations of the playing — or queued — slot: lit =
  not empty, wink = playing, blink = queued). `mute_mask` carries SAVE's bit while the map may take keys; the
  frame's `kb_mute_hit` goes to `cue_keys` when SAVE is the one held, else to `mute_keys`.
- head_mode, first match: map open and nothing queued "CUE A3" (+ "*" when `slot_dirty` of the loaded slot);
  something queued "CUE>B" / "CUE>B3" / "CUE>A3"; then LOCK STEP / STEP REC / KEYS as today.

- [ ] **Step 1: Failing tests:**
  - ui_test `test_save_tap_opens_projects`: SAVE down / up within 400 ms, no key → the projects page on the release
    (not the press); a second tap steps to TOOLS; held 500 ms → map, release → no page change.
  - ui_test `test_cue_keys_no_notes`: SAVE held + white key 1 → no hit on track 1, no MIDI note out, nothing
    recorded with REC ALL on.
  - ui_test `test_save_dead`: in the menu / a confirm / NAME → SAVE and the keys act as before; GLO held → the MUTE
    screen's keys still toggle mutes.
  - cue_test `cue_map_leds`: A, B used, A playing variation 1, B queued, variation 3 of A not empty → `cue_led` 1
    wink, 2 blink, 3 / 4 dark, 5 wink, 7 lit, 6 / 8..12 dark; an edit to A → slot key 1 breathes.
  - cue_test `cue_top_line`: B then 3 → `head_mode` "CUE>B3"; nothing queued with an edit → "CUE A1*".
  - cue_test `cue_keys_queue`: SAVE + key 2 then key 7 → `q_slot == 2`, `q_var == 3`; key 2 again → `now`.
  Run → FAIL.
- [ ] **Step 2: Implement.** Existing ui_test assertions that press SAVE expecting the page on the press move to the
  release (say so in the commit message). Give the owner sim screenshots of the map's top line.
- [ ] **Step 3: Docs**; suites 1–6; commit.

### Task 7: FILL and !FILL; REC held off SEQ and SONG

**Files:**
- Modify: `firmware/src/params.c:365-403` (codes, cond_* functions), `firmware/src/seq.c:122-135` (`step_plays`;
  `static volatile uint8_t fill_on;` next to `transport_req`),
  `firmware/src/ui_input.c` (`rec_fill` after `cue_input`), `firmware/src/ui_draw.c` (head_mode "FILL")
- Test: `tests/drum_test.c` (`:3270-3300`, the PROB test uses `COND_AB_MAX`), `tests/ui_test.c`,
  `tests/smc_test.c` (or wherever SMC REC is tested), `tests/sim_ids.c` (only if its loop needs the new names)
- Docs: README (FILL), DEVICE_INSTALL, CHANGELOG

**Interfaces:**
- Produces: `#define COND_AB_MAX 56u` (cond_ab's last code), `#define COND_FILL 57u`, `#define COND_NFILL 58u`,
  `#define COND_MAX 58u`; `cond_pos` / `cond_store` identity for 57 / 58; `cond_format` "FILL" / "!FILL";
  `step_plays`: FILL plays iff `fill_on`, !FILL iff `!fill_on`. `#define FILL_HOLD_MS 400u`;
  `static void rec_fill(uint32_t now)` — REC down ≥ 400 ms where `cur_fam() != FAM_SEQ` and not menu / confirm /
  NAME / PRESETS list / MUTE screen → `fill_on = 1`, `ui.rec_t0 |= 2u` (no tap); REC up → `fill_on = 0`.

- [ ] **Step 1: Failing tests:**
  - drum_test `fill_conditions`: cond 57 plays only with `fill_on`, 58 only without; the PROB knob turned past 8/8
    gives FILL then !FILL and stops there; `cond_format` strings; a step with 57 survives a project save / load.
  - ui_test `test_rec_fill_hold`: on HOME, SOUND, FX, LFO, TRACKS: REC held 400 ms → `fill_on`, top line "FILL";
    release → 0, `song.rec` and `ui.rec_all` unchanged.
  - ui_test `test_rec_hold_vs_rec_all` (Review Focus 3): REC tapped 300 ms off SEQ → REC ALL toggles on the release
    (as today); REC ALL on, REC held 500 ms → fill while held, REC ALL still on after; on TRACKS a 300 ms tap still
    arms the selected track.
  - ui_test `test_rec_seq_unchanged`: STEP, PATTERN, MOTION, SONG: 700 ms hold → the confirm as today, never fill;
    a tap as today.
  - ui_test / smc_test `test_rec_learned_short`: a learned REC and the SMC's REC → a tap, never fill.
  Run → FAIL.
- [ ] **Step 2: Implement**; adjust drum_test's PROB count to `COND_AB_MAX`. Suites 1–6; docs; commit.

### Task 8: COPY / PASTE / undo of a slot

**Files:**
- Modify: `firmware/src/core.h:85` (`G_COPY`, `G_PASTE` after `G_METRO`), `firmware/src/params.c:137-142`
  (`GP_ACT[6]`: `PE("COPY", N_GO, 0)`, `PE("PASTE", N_GO, 0)`), `firmware/src/pages.c` (a page after TOOLS:
  `{"COPY", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, G_COPY, G_PASTE, 0xFF}}`; grep for hard-coded page indexes past
  it), `firmware/src/ui_input.c:550-605` (the actions; PASTE uses the "AGAIN:" arm), `firmware/src/project.c`,
  `firmware/src/ui_draw.c` (`graph_slots`: the copied slot marked)
- Test: `tests/cue_test.c`, `tests/ui_test.c` (the page order)
- Docs: README (COPY / PASTE / undo), DEVICE_INSTALL, CHANGELOG

**Interfaces:**
- Consumes: `proj_writeback`, `var_room`, `proj_var_load`, `project_discard`, `proj_cue_in`, `slot_dirty`.
- Produces: `static uint8_t copy_src, paste_undo;` (slot + 1; `paste_undo` = the last paste's target until its
  SAVE or a paste elsewhere); `static void project_copy(uint32_t s)` ("COPIED A"; empty → "EMPTY SLOT");
  `static void project_paste(uint32_t t)` — refused while `transport_busy()` ("STOP TO PASTE"), no copy ("COPY A
  SLOT FIRST"), t == source ("SAME SLOT"); the loaded source written back first; t's image, lock bank, song,
  variations ← the source's (variations: into `var_buf[var_play]` when t is loaded — then `proj_cue_in` with
  globals kept — else into the park, `var_room` refusing "SAVE C FIRST"); the name rule; `proj_dirty`/`var_dirty`
  bits of t; "PASTED A TO B". PASTE on `paste_undo`'s target → `project_discard(t)` (+ reload when loaded),
  "PASTE UNDONE".

- [ ] **Step 1: Failing tests** (cue_test):
  - `paste_copies_all`: A (kit, steps, locks, variations, song, name "BEAT") → COPY A, PASTE B → B's image equals A's
    but the name "BEAT COPY" (and sum); `lk_slot[1]` = A's; B's variations (park) decode as A's; no flash op;
    `slot_dirty(1)`.
  - `paste_refused_playing`: "STOP TO PASTE", nothing changed.
  - `paste_undo`: PASTE B twice → B equals flash's (record, bank, variations), "PASTE UNDONE", not dirty; after
    SAVE B a PASTE pastes again.
  - `paste_onto_loaded`: t = the loaded slot → live tracks = the source's, globals as before, `var_buf[var_play]`
    = the source's variations.
  - `paste_park_full`: park holds C dirty, t not loaded → "SAVE C FIRST".
  - `paste_from_live`: the source is loaded with unsaved live edits → the paste has them.
  - `paste_then_load` (Review Focus 4): PASTE B, LOAD B → DISCARD EDITS?; NO → B still the paste; YES → flash B
    loaded, `paste_undo == 0`; separately, a cue of the pasted B plays the paste.
  - ui_test: SAVE taps step PROJECT → TOOLS → COPY → PROJECT; the COPY page's knobs.
  Run → FAIL.
- [ ] **Step 2: Implement.** Suites 1–6; docs; commit.

### Task 9: Web sim, docs, integration, the usbmode merge check

**Files:**
- Modify: `web/sim/controls.js` (SAVE's tip: tap projects, hold = cue map; REC's tip: hold off SEQ = FILL),
  `web/sim/index.html` (the features' steps chapter: cue, variations, FILL, COPY / PASTE), `web/test_web.mjs`
  (`simCue()`), `tests/sim_host.h` (`VAR_XIP` on `hflash`, next to `SMP_USER_XIP`)
- Modify: README.md, docs/DEVICE_INSTALL.md (one checklist block for the whole feature), CHANGELOG.md
  ("## Unreleased": one section, credit mono2k for the behaviour)

- [ ] **Step 1: Failing web test** `simCue`: SAVE's tip names the cue map and the projects page; REC's tip names
  FILL; the steps chapter names variations, FILL and COPY / PASTE. Run `(cd web && node --test test_web.mjs)` →
  FAIL; implement; pass.
- [ ] **Step 2: Owner review material** (user verifies by ear and eye): `tests/sim_record.c` renders two WAVs of
  the same slot switch, with the 5 ms fade and with a hard cut (a local build flag), and one of FILL held for a bar;
  sim screenshots of the cue map's top line, the projects list with "*", the COPY page. Put them in `build/review/`
  (not committed) and list the paths in the report.
- [ ] **Step 3: Full suites 1–6**; RAM ledger: both builds' `.data+.bss` (and pool) against Global Constraints'
  budgets, in the commit message.
- [ ] **Step 4: Merge check** (no merge): `git merge-tree --write-tree HEAD usbmode` → conflicts only in README.md,
  CHANGELOG.md, docs/DEVICE_INSTALL.md (expected); any code conflict → move the hunk per Global Constraints' merge
  rules and re-run. Plain build after merge (estimate): this branch's plain `.data+.bss` + 1524 B ≤ 98304.
- [ ] **Step 5: Commit** (web/*, tests/sim_host.h, docs).
