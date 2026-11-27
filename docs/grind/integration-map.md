# Native integration map

All references use the approved pin `44c9ad4aa57bebf63f412bd552e57143c30b4065` in https://github.com/DeadActive/hortator. The [call-site inventory](callsite-audit.json) records source anchors and execution contexts for the pinned firmware tree. It includes native scheduler, cue, recording, panel edits, MIDI, editor, storage and boot entry points.

| Path | Observed symbols / context | Proposed responsibility and constraints |
|---|---|---|
| `firmware/src/core.h` L147-163 | `step_t`, `MICRO_MAX`, `NT_MIN`, `NT_MAX` | 4-byte stored cell; accent maps to 96/127, rat 0-3 (hits minus 1), micro -50..50% of step. Preserve existing representation; separate G-LOCK metadata. |
| `firmware/src/felucca.c` | Textual includes, `FELUCCA_BLE`, `FELUCCA_ID` | Integrate pure core exactly once; no new loader or radio; retain package identity rules. |
| `firmware/src/seq.c` L152-179, 289, 358, 386, 400, 407, 758-788, 983-1058 | `step_plays`, `seq_next`, `step_fire`, `rat_due`, `step_micro`, `rec_hit`, `seq_start`, `seq_stop`, `events_block` | Existing scheduler remains authoritative; live recording and external edits invalidate pending revisions. Generation in main loop, not ISR. Stop clears pending stages. |
| `firmware/src/cue.c` L38-83, 104-170 | `cue_switch`, `cue_longest`, `cue_tick`, `cue_stop` | Existing cue chooses longest track (LEN x DIV) or next-step mode. `proj_cue_apply` before `seq_restart`; no competing clock. Validate shared arbitration and early micro-hit handling. |
| `firmware/src/project.c` | `proj_cue_apply`, `proj_fetch`, `project_save_go`, `project_load`, `var_write`, `proj_hash`, `var_link_of` | Stage outside IRQ; verify actual atomic publish and rollback. No unbounded copy or flash write in ISR. |
| `firmware/src/varpack.c` | `var_need`, `var_put`, `var_get`, `var_valid` | Sparse bank with strict byte cap. Audit capacity before format changes; invalid save must preserve old data. |
| `firmware/src/ui_input.c` | `step_edit`, `tracks_edit`, `cue_keys`, `rec_tap`, `srec_keys` | Audit user-edit ownership and key collisions. Existing UI must not race controller. |
| `firmware/src/storage.c` | `st_save`, `st_load`, `st_current`, `st_crc32` | A/B commit protocol. Reuse and test; never add unsupported flash address. |
| `tests/drum_host.h`, `tests/sim_core.c` | host textual includes, `host_irq_hook`, `sim_frame`, `sim_render` | Reuse real firmware paths for parity and concurrency tests. |

The native pattern limits are eight tracks and 64 steps. LEN accepts 1–64. The stored division IDs for straight eighth, sixteenth and thirty-second notes are 1, 2 and 3. Swing is 0–100. Conditions 0–20 encode probability, 21 is one-shot, 22–56 encode A/B, and 57–58 are FILL and !FILL. Micro timing uses −50 to +50 percent of a step. Early hits consult `seq_cut_ahead` before the next boundary. These values describe the pinned source ABI. [Parameter table](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/params.c)

The adapter proof concerns buffer ownership. A queued cue owns `st_buf` until its incoming state is swapped and the outgoing state is filed. The pinned host fixture accepts a new cue after the ISR swap and before main-loop file-out, then loses an edited outgoing step. A second probe commits two different lock-bank copies while interrupting both saves before the project record commit. The original project can find its linked bank after the first interruption and loses the link after the second. A/B commit is per object, so linked-record recovery needs its own fault-injection cases.

Reproduce both findings in the host fixture:

```bash
python3 tools/build.py --gen-only
cc -O1 -Wall -Wno-unused-function -Wno-int-to-pointer-cast -Ibuild/gen -Ifirmware/src -Itests \
  tests/grind/t03_host_probe.c -o build/t03-host-probe -lm
build/t03-host-probe
```

The pinned source returns exit 1 with `outgoing step preserved=0` and `old linked lock bank status=-1`. The T03 evidence keeps both items open until the relevant invariants pass.

## P04 staging and RAM budget

The plain build uses 88,312 of 98,304 bytes of static app RAM, leaving 9,992 raw bytes within the region. The previously reported 161 bytes came from the superseded project-specific reserve. T03a checks the actual RAM and flash limits and retains the existing 8 KiB pool floor. Subtracting the planned 2,144-byte `gm_pattern` payload (32 timing, 2,048 cells and 64 lock bytes) leaves an estimated 7,848 B. Link the real adapter and measure the allocation with alignment and other state included.

The native compiler measures `cue_stage_t` at 3,424 bytes and its backing `st_buf` at 3,840 bytes. `cue_stage` fills staged steps and timing in the main loop. `proj_cue_apply` swaps live and staged state at the cue boundary, and `cue_file_out` uses the same buffer to file the outgoing state. Reuse can only be considered once that lifetime and the storage/editor users of `st_buf` have been audited together. Its 416-byte size difference cannot hold a concurrent full pattern.

For P04, evaluate writing one candidate directly into the existing staging representation outside the ISR, with explicit ownership through cue queue, cancellation, swap and file-out. Keep lock metadata separate and account for it in the actual native build. Verify that a rejected generation, concurrent edit or transport change preserves the live, staged and saved state. The pool has 12,532 bytes above its upstream minimum in the baseline. Any pool allocation requires a measured lifetime, contention check and new linker and runtime measurements before the adapter is frozen.

## T10 runtime design (decided, not yet implemented)

Done on this branch: `controller.c` (portable state machine, `tests/grind/test_controller.c`) and the adapter. Still to
build, in this order, each with a red test on the real sequencer first:

1. **Switch.** `GRIND_NATIVE` (default 0) gates everything below, so the default image and its RAM/flash budget do not
   change. With 0, `gm_touch()` is an empty inline.
2. **Stage.** One RAM stage, no heap: `union { gm_pattern base; step_t st[NTRK][NSTEP] + timing }` plus one `gm_pattern
   out`. `base` is only used while WRITING and the native stage only while READY/READING, and the controller never
   has both, so they share bytes (about 4.3 KB, to be measured by the native link, not assumed). The main loop
   generates `base -> out`, checks it with `gm_native_check`, then converts `out` into the native stage; the ISR only
   copies it (no generator, no conversion loop, no flash in the callback).
3. **Boundary.** A third queue flag `cue.q_gm` next to `q_slot`/`q_var`: `cue_tick` takes it with the same
   longest-track (`cue_longest`) or next-step (`cue.now`) boundary, `cue_switch` calls the Grind commit instead of
   `proj_cue_apply`, then runs the unchanged `seq_restart(x, b24)`; `seq_cut_ahead` treats it like a queued cue so an
   early micro hit cannot cross the replacement; `cue_stop` and `cue_cancel` drop it. BAR boundaries are refused
   (`GM_UNSUPPORTED`) until the sequencer has a bar boundary of its own.
4. **Arbitration.** A native slot/variation cue and a Grind stage never queue together: a native cue key cancels a
   pending Grind stage (visible status), and a Grind request waits while a native cue is queued.
5. **Revisions.** `gm_touch()` before every write of a native step by the user, the editor, a tool, a load or a
   recorded hit (ISR callers use the guard-free variant). Audited sites: `ui.c` step_hold/step_tap/step_accent/
   track_clear, `ui_input.c` step parameter edits, `ui_tools.c` `tl_step_in`, `editor_pattern.c`, `project.c` loads and
   `proj_cue_apply`, `seq.c` `rec_hit`/key recording. A missed site is a data-loss risk, so the audit gets its own
   test that greps the firmware for step writers and fails on an unlisted one.
6. **Native build.** Link with `GRIND_NATIVE=1` in CI, read the real app RAM, pool floor and ISR budget, and only then
   decide whether the image ships the engine. Nothing is enabled by default until that evidence exists.
