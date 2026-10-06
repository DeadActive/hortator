# Stage 2 step 3: upstream's storage hardening and the `st_erase` order (`frozen-base-4`) — design

Date: 2026-10-06. Parent: `2026-10-06-frozen-baseline-stage2-design.md` (the baseline-tag mechanism, the loader pin)
and `docs/UPSTREAM_1.0.2.md` §2 (storage hardening, `st_erase` order). Steps 1-2 (`frozen-base-2`, `frozen-base-3`)
are device-checked and merged (main 55b5b5e).

## 1. Goal and user decisions

- Take upstream 1.0's `storage.c` fixes and its erase order into our firmware; the update loader stays
  byte-identical (it does not include `storage.c`).
- User decision (design approval): upstream's `storage.c` hunks **except the FM6 patch bank**; upstream's
  `st_erase`; `ota_erase` unchanged, as upstream left it.

## 2. What changes in `firmware/src/storage.c` (frozen)

From `git diff 1e838e1 727f272 -- firmware/src/storage.c`, taken byte for byte except where noted:

- **Header comment:** upstream's new wording of the A/B scheme (newest seq wins, including wrap).
- **Header layout check:** `_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");`.
- **`st_head`:** refuses `obj >= OBJ_COUNT || copy > 1u` before any flash read, and a header whose `slot` is not
  the copy it sits in (`h->slot != copy`).
- **`st_current`:** copy B wins when `b.seq != a.seq && b.seq - a.seq < 0x80000000u` (the newer save also across
  the 32-bit wrap; A on a tie, as before). The comment above it as upstream.
- **`st_load`:** refuses an invalid object, and a record longer than the caller's buffer (`h.len > max` → −1,
  `dst` untouched) instead of truncating it. Its comment as upstream.
- **`st_save`:** refuses an invalid object before any flash access; the read-back compares the whole header
  (`memcmp(&chk, &h, sizeof h)`) instead of only `seq`.
- **Skipped:** `OBJ_FM6BANK` in the enum, its `st_sector` case and the flash-map comment line that names it.
  `OBJ_COUNT` stays `OBJ_UPRESET0 + 2`.
- `ST_MAGIC` stays `0x554C4546` ("FELU", equal to upstream's).

Compatibility: every header our firmware wrote has `slot` = its copy (`st_save` sets it), so saved projects and
settings still load. The only behaviour change for stored data: a record longer than our buffer (only possible from
another firmware) is refused rather than truncated. Projects already rejected such records by their format marker;
a bigger settings record with our `PERSIST_MAGIC` used to half-load and now gives the defaults (incl. the default
panel table). `memcmp` exists in `libc.c`.

## 3. The erase order (`firmware/src/felucca.c`, not frozen)

`st_erase` becomes upstream's (`727f272:firmware/src/storage_hw.c`):

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

Today `audio_silence()` runs with IRQs on and `fl_erase4k` disables them only afterwards: the audio ISR can refill
`abuf` in between, and the DMA then loops that stale chunk for the whole erase (a buzz on SAVE). With IRQs off first,
the buffer stays silent. `ota_erase` (frozen `ota.c`) is unchanged, as upstream left it.

## 4. Baseline and safety

- Tag **`frozen-base-4`** = `frozen-base-3` + the §2 hunks in `storage.c` + the §3 `st_erase` in `felucca.c`
  (glue: `st_erase` is inlined into the frozen `st_save`, so the reference build needs the same text for H2).
  `tools/frozen_base.txt` names it with its full commit; the checks fail on a moved or missing tag.
- Our `storage.c` equals the tag's (`ST_MAGIC` too); `check_untouched` as usual.
- `docs/UPSTREAM_1.0.2.md`'s frozen-change table gets the `frozen-base-4` rows; DEVICE_INSTALL.md the §7 checks.
- Loader pin + self-test, H2 against `frozen-base-4`, the stack check, target budgets, the full suite,
  `DRUM_PACKAGE=1 ./build.sh`.

## 5. Erase-order check (built firmware)

A small check on `build/felucca.dis` (the app disassembly `tools/build.py` writes). Today `st_save` (where
`st_erase` is inlined) reads: `abuf` loaded, the 1024-word zero loop, `cli`, `fl_erase4k_ram` loaded and called,
`csync`, `sti`. The check requires, in `st_save`, a `cli` before the `abuf` load and the `sti` after the call to
`fl_erase4k_ram`; it fails with what it found. `ota_erase` keeps today's order (§3) and is not checked. Its
self-test feeds it today's `st_save` text and expects a failure.

## 6. Testing (host, failing first)

`tests/storage_test.c` takes upstream's new cases, without the FM6 ones, and its flash model (power cut after any
programmed byte, erase error, write protection, an I/O call counter):
- every payload / header byte cut keeps the old data; an erase failure and write protection are save errors that
  keep the old data;
- the save sequence wraps (seq 0xFFFFFFFE / 0xFFFFFFFF) and the newer data loads, and saves continue;
- an invalid object (`OBJ_COUNT`, `0xFFFFFFFF`) is refused with no flash access;
- a destination one byte short refuses the whole object and leaves `dst` untouched; an oversized save is refused.

Plus ours:
- a valid header (correct CRC) naming the other copy is ignored: the other copy loads;
- a flash that changes one header field other than `seq` on its way back (`rsv`) makes the save fail (−7), and the
  previous data still loads;
- settings: a stored settings record one `persist_t` too long loads the defaults (project.c path).

## 7. On the device (the user)

- SAVE a project and the settings while reverb / delay tails are still sounding after STOP: no buzz during the save.
- Projects and settings survive a power cycle; a project saved before this build still loads.
- The panel layout (OCT- + OCT+ at power-on) is still remembered.

## 8. Done when

§5 and §6 pass, the firmware builds, H2 / loader / stack checks are green, and the user has checked §7 on the FM-1.

## 9. Out of scope

The FM6 patch bank, `ota_erase`, TRS MIDI on (next step), the features.
