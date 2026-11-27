# Hardware compatibility and recovery - unqualified

User reports M-VAVE FM-1 running Felucca. Exact revision, flash ID, backup status and recovery host are unknown. Upstream `project.c` accepts 1 MiB flash JEDEC IDs 0x856014 (Puya P25Q80) and 0xB36014 (UC25HQ80); other IDs use RAM-only saves according to source. This is not evidence that either variant has been tested with GRIND MACHINE.

- P01 requires no device connection or write. Native build/package and hardware stress tests have not run.
- Preserve Felucca settings/presets via a verified supported backup procedure before any future installation. A stock restoration does not itself guarantee recovery of user presets.
- Upstream [recovery guide](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/docs/RECOVERY.md) describes a macOS boot-mode path with a separate loader and a stock package; it has prerequisites, checks and documented limits. No recovery action has been attempted here.
- Record read-only device identity/JEDEC, validate exact package and rollback route, check the current device and tool versions, then obtain explicit device-write approval at hardware beta. Never flash automatically from CI or preview.
- Standard non-BLE build only in v1.0. Verify USB/MIDI and clock behavior after native integration. Preserve loader/HAL guards and protected regions.

Release is blocked until measured hardware evidence, legal asset manifest and approved recovery path exist.
