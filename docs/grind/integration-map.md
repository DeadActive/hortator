# Native integration map (preliminary source audit)

All references use the approved pin `44c9ad4aa57bebf63f412bd552e57143c30b4065` in https://github.com/DeadActive/hortator. These are observed source anchors from selected file reads, not an exhaustive call graph. Full `git grep` inventory and compiled checks are pending.

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

Remaining inventory: precise callers/IRQ boundaries, native division/condition IDs, all write paths (MIDI, editor restore, copy/paste, project, variations, recording, presets), native staging buffers, model/preset IDs, boot paths, linker and HAL constraints. Verify using T03 grep commands. Proposed adapter contracts are not frozen. No source changes made.

## P04 staging and RAM budget

The plain build uses 88,312 of 98,304 bytes of static app RAM. The 10% reserve caps static usage at 88,473 bytes, leaving **161 bytes**. The planned `gm_pattern` has 2,144 bytes of payload (32 timing, 2,048 cells and 64 lock bytes). A second static pattern would exceed the budget by 1,983 bytes.

The native compiler measures `cue_stage_t` at 3,424 bytes and its backing `st_buf` at 3,840 bytes. `cue_stage` fills staged steps and timing in the main loop. `proj_cue_apply` swaps live and staged state at the cue boundary, and `cue_file_out` uses the same buffer to file the outgoing state. Reuse can only be considered once that lifetime and the storage/editor users of `st_buf` have been audited together. Its 416-byte size difference cannot hold a concurrent full pattern.

For P04, evaluate writing one candidate directly into the existing staging representation outside the ISR, with explicit ownership through cue queue, cancellation, swap and file-out. Keep lock metadata separate and account for it in the 161-byte allowance. Verify that a rejected generation, concurrent edit or transport change preserves the live, staged and saved state. The pool has 12,532 bytes above its upstream minimum in the baseline. Any pool allocation requires a measured lifetime, contention check and new linker and runtime measurements before the adapter is frozen.
