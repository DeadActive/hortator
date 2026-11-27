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
