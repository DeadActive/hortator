# P01 baseline evidence - BLOCKED

This is a source-backed preparation and integration report, not a build certification.

| Item | Observed status |
|---|---|
| Upstream source | GitHub connector read access, selected files at pinned SHA; full local checkout unavailable |
| Target repository | `DaFum/grind-machine`: verified true fork of `DeadActive/hortator`, push permission confirmed, main commit `44c9ad4aa57bebf63f412bd552e57143c30b4065` and tree `ff6d99c5c5ec82411016f1de02d4e0b5519d563f` |
| Browser alternative | Previously unavailable (MCP SSE HTTP 429) |
| Git transport | `git ls-remote https://github.com/DaFum/grind-machine.git HEAD`: exit 128, could not resolve host github.com |
| Local repo | approved docs commits 0857162 and 20d50ae; preparation commit 02f3340, unrelated to upstream history. Transfer only files via API; never push unrelated history. |
| Subagents | No dispatch capability exposed; sequential preparation, no independent agent review |
| Local tool inventory (preparation) | cc Debian 14.2.0; Python 3.13.5; Node v22.16.0; `gh` and Docker not found on PATH |
| Standalone audit tests | Local preparation checkout: `python -m unittest discover -s tests/grind -p 'test_*.py' -v`: 8 tests passed; blocked record validator exits 2 as intended |
| Firmware/host/WASM | Not built or tested; no local source or qualified target toolchain |

## Build prerequisites and actual limitations

[BUILDING.md](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/BUILDING.md) documents Python/Pillow, JieLi pi32v2 clang 4.0.1, AC79 SDK tag `AC79NN_SDK_V1.2.1_2023-12-13` and Node for web tests. Linux x86-64 supports the native toolchain, while macOS uses Docker for target tools. The host suite includes `libproc.h` in `tests/drum_host.h`; verify portability rather than assuming a Linux runner passes. `tools/get_toolchain.sh` fetches a moving vendor archive and chooses the latest directory by sort; capture exact archive checksum and licensing before execution. WASM uses the upstream `emscripten/emsdk:6.0.11` image tag; resolve immutable digest in the actual runner. No vendor download performed here.

| Baseline command | Result |
|---|---|
| `python3 tools/build.py --gen-only` | NOT RUN (no local source checkout) |
| `sh tests/run_drum_tests.sh` | NOT RUN |
| `bash tools/build_sim.sh --host-only` | NOT RUN |
| `bash tools/build_sim.sh --preview` | NOT RUN |
| `FELUCCA_BLE=0 ./build.sh` | NOT RUN |
| `FELUCCA_BLE=0 DRUM_PACKAGE=1 ./build.sh` | NOT RUN |
| `sh tests/run_tests.sh` | NOT RUN; requires native package |

Use the exact P01/T02 commands on the actual checkout, record toolchain versions, stdout/stderr/exit codes and SHA-256 of outputs. Do not change goldens or bypass failing tests. Full firmware CI has deliberately not been created without the complete checkout, verified toolchain and runner choice. CI job requirements remain in T02. The standalone audit validator can be exercised independently with `python3 tests/grind/test_audit.py`; synthetic fixtures do not count as native evidence. `python3 tools/grind/audit.py docs/grind/audit-blocked.json` must exit 2.

## P01 task status

- T01 partial: true fork and pinned main verified, prepared files transferred to the integration branch; local full checkout remains blocked. Design and plan retain their approved historical text.
- T02 partial: tool inventory and prerequisites recorded; all upstream builds and full firmware CI remain blocked.
- T03 partial: selected source integration/flash/license review and audit schema; complete call-site inventory, linker/stack measurements, all-asset inventory, target tests and final adapter freeze remain blocked.

No native support or device flash approval has been established. P01 is not merge-ready. Do not start P04 or persistent-format integration before P01 resource evidence exists.
