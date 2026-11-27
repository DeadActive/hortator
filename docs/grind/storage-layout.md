# Storage layout - preliminary, no write authorization

Source references at the pinned Hortator SHA: [storage.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/storage.c), [project.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/project.c), [varpack.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/varpack.c).

| Source-described region | Use |
|---|---|
| 0x97000-0x9EFFF | Projects |
| 0xA0000-0xD3FFF | User samples, except variation carve-out below |
| 0xC0000-0xC7FFF | Variation banks within former sample space |
| 0xD4000-0xDBFFF | Lock banks |
| 0xDC000-0xDFFFF | User presets |
| 0xFC000 onward | Settings (confirm exact bounds in complete HAL map) |

Do not infer unused regions from this table. Storage uses 4096-byte sectors, payload offset 256, maximum payload 3840 and 32-byte header with CRC, committed last. `VAR_BYTES=3584`, `VAR_CAP=3576`; reserved tail from sector +0xF00 stays erased for the loader scan. Four slots, one base pattern and up to seven sparse variations per slot; dense patterns reduce capacity. Project format FDRG does not read Felucca FUN1-FUN3. `sizeof(project_t)`, app flash budget, RAM usage and stack are unmeasured in this session.

P08/T17 candidate remains a versioned, capacity-checked extension of existing storage, not a new address, pending complete map and loader safety checks. Preserve active and saved state on rejected writes; test A/B torn-write and old-format handling. G-LOCKs must cover OFF as well as ON cells; never repurpose `step_t` padding or overwrite parameter locks. If the native budget cannot accommodate metadata and the approved behavior, stop for design revision. No device flash read/write, no partition changes, no release package.
