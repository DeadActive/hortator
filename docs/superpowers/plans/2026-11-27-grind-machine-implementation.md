# GRIND MACHINE FM-1 Edition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone, GPL-3.0-only extreme-metal drum firmware for the M-VAVE FM-1, with 22 generative styles, four morph controls, eight tracks, protected edits, six live actions, four kits, persistent projects and a matching WASM simulator.

**Architecture:** Extend the pinned Hortator C firmware, preserving the native audio, timing, hardware, loader and transport implementation. Use a portable deterministic Grind Core with a main-loop controller, bounded staged publication at the existing sequencer boundary, versioned storage extensions and the existing shared-source simulator. Deliver seven independently gated milestones through small PRs.

**Tech Stack:** C (portable C99-compatible core; existing upstream C dialect for adapter), JieLi WL82/pi32v2, Python 3, POSIX shell, Node.js, Emscripten (upstream Docker image `emscripten/emsdk:6.0.11`), native Hortator host tests, optional ASan/UBSan, Git and GitHub Actions.

---

**Plan version:** 1.0 | **Date:** 2026-11-27 | **Design status:** approved by user.
**Design:** [2026-11-27-grind-machine-design.md](../specs/2026-11-27-grind-machine-design.md).
**Destination:** `DaFum/grind-machine` (proposed, not created).
**Execution status:** planning only. No source implementation, firmware build, fork creation, push, flash or device test has been performed.

## 1. Evidence, scope and execution rules

### 1.1 Pinned source evidence

GitHub `main` returned commit [`44c9ad4aa57bebf63f412bd552e57143c30b4065`](https://github.com/DeadActive/hortator/commit/44c9ad4aa57bebf63f412bd552e57143c30b4065) on 2026-11-27 (commit dated 2026-11-05). The recursive tree `ff6d99c5c5ec82411016f1de02d4e0b5519d563f` was retrieved without truncation. Selected source files were inspected at that SHA. All upstream paths in this plan are from that tree, not invented filenames. Local workspace contains only the approved design commit `0857162`; a local `git ls-remote` failed with DNS resolution error. Consequently, actual native and host build results, toolchain licenses, RAM map, installer behaviour and timing measurements are **not** established. Upstream commit is a proposed reproducible baseline, not a claim that it is the latest release tag or already validated on the user's FM-1.

| Verified source | Implication |
|---|---|
| [core.h L147-163](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/core.h#L147-L163) | `step_t` is a stored 4-byte cell: `on`, `acc`, `slide`, `tie`, `rat` (hits minus 1), `cond`, `nt`, `micro`. Accent gives 96/127 nominal velocity. Do not invent continuous step velocity or use padding as a lock. |
| [felucca.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/felucca.c) and [tests/drum_host.h](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tests/drum_host.h) | Application and host use textual inclusion of C modules. Integrate new core exactly once per build path. `FELUCCA_BLE=0` is plain build; retain package identity `FM-1_900`. |
| [seq.c L750-820](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/seq.c#L750-L820), [seq.c L983-1058](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/seq.c#L983-L1058), [cue.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/cue.c) | `events_block` processes transport, MIDI, cue and track events; `cue_tick`, `cue_longest`, `cue_switch`, `seq_restart` implement the native switch; `seq_cut_ahead` suppresses early hits across a boundary. Never add a parallel timer. Clock loss currently uses 500 ms, with USB reconnect exception. |
| [project.c L1-55](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/project.c#L1-L55), [storage.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/storage.c), [varpack.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/varpack.c) | FDRG project record uses tight payload; A/B sectors (4096 B, 3840 B payload) with header-last CRC. Variation object capped at 3584 B to keep loader-scan tail erased. Project links lock and variation banks. `FL_JEDEC_OK` checks 0x856014 and 0xB36014 in source; this is NOT a hardware qualification. |
| [tests/sim_core.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tests/sim_core.c), [tools/build_sim.sh](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tools/build_sim.sh) | Simulator executes firmware ISR and UI frames; real C is compiled into WASM. Upstream supports `--host-only` and `--preview`; preview without package cannot flash. |
| [tests/run_drum_tests.sh](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tests/run_drum_tests.sh), [tests/run_tests.sh](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tests/run_tests.sh) | Drum host suite generates headers without target toolchain; full suite explicitly requires `DRUM_PACKAGE=1 ./build.sh` first. Some host fixtures include `libproc.h`; verify actual supported OS/compiler, do not assume all Linux jobs work unmodified. |

The design's 40-350 BPM target, 80% deadline, 10% memory reserve, four kits and 24 acceptance tests are requirements, not measured values. Read the approved spec in full. New symbol names below are proposed Grind contracts, NOT claims of existing Upstream API. Line references apply only to pinned SHA. If an implementation anchor is absent, verify SHA and inspect the actual call graph; do not guess or silently change scope.

### 1.2 Plan decomposition and gates

Each PR builds a usable intermediate, or adds an isolated tested library that cannot change firmware behaviour until integrated. PR labels here are proposed work identifiers, not existing GitHub PR numbers. PRs may be split further for review. Tasks within a PR are separate commits. Do not implement all PRs in one branch. Native target must pass at integration gates; host/WASM work may continue if explicitly labelled simulator-only and if storage/real-time assumptions remain blocked. The new core remains disabled from production boot until UI/controller/integration are tested.

| Workstream | PR | Deliverable and dependency |
|---|---|---|
| v0.1 Foundation | P01 (T01-T03) | Fork/pin, baseline, build/toolchain/resource/asset audit. No device writes. |
| v0.2 Grind Core | P02 (T04-T05), P03 (T06-T08), P04 (T09-T10) | Pure contracts, PRNG, styles, morph/locks, adapter and cue. P04 needs P01 resource audit. |
| v0.3 Grind UI | P05 (T11-T12) | Working native and simulated GRIND/STYLE/SEQ screens, controls, factory presets, offline simulator. |
| v0.4 Sound Design | P06 (T13-T14) | Four legal, measured kits; native sound paths. |
| v0.5 Live Assault | P07 (T15-T16), P08 (T17-T18), P09 (T19-T20) | Performance, undo, transactional projects, sync/editor/export regression. Storage architecture depends on P01 audit. |
| v0.9 Hardware Beta | P10 (T21-T22) | Safety gates, package qualification, hardware measurement and compatibility. |
| v1.0 Release | P11 (T23-T24) | Auditable release bundle, handover and user demo. Requires all mandatory acceptance evidence. |

Parallelizable after P01: pure core and legal asset sourcing; documentation and test-fixture development. Only one active integrator modifies `seq.c`, `project.c`, `ui_input.c` at a time. The optional note-out, legacy Hortator import, editor extensions and additional layers are explicitly conditional; no VIBE port, Bluetooth build or dualboot in v1.0. If blocked, publish only a clearly labelled simulator preview with no installable package.

### 1.3 Test/commit protocol (applies to every task)

1. Read specified files and design requirement; add concrete failing test/fixture before code. A compile/link failure for a new API is acceptable for the first red only; then obtain behavioural red, not just missing symbol. Preserve original baseline results. For pure docs/audit tasks, negative evidence is a missing/inconsistent required field or explicit command failure rather than a fabricated failing software test.
2. Run exact command, retain stdout/stderr/exit status in `build/grind-evidence/<task>/` (ignored, attach selected logs to PR). Use `set -o pipefail` in Bash with `tee`. Record input commit and compiler versions. Do not bypass failed or skipped test.
3. Make minimal implementation. Run same test green, sanitizers when supported, relevant native regressions and shared-core simulator tests. For any target-related change, include target link/budget reports before marking native support.
4. Review diff for unrelated edits, `git diff --check`, commit only task paths (`git add -- <listed files>`); commit messages below. Open PR after milestone integration with test evidence. No force-push, auto-merge, upstream mutation, secret publication, or automatic device upload.

For new tests use `assert` with `NDEBUG` explicitly unset, or upstream's assertion macros, no test which merely compares two results of the same implementation for correctness. Expected pattern vectors are independent hand-authored fixtures. Golden changes require reason and review. In commands below `sh tests/run_grind_tests.sh <suite>` is introduced by T04; suites are extended in later tasks. `GRIND_NATIVE` is a future build switch only if needed to preserve baseline; do not assume existing flag. A command whose prerequisite is not met records BLOCKED, not PASS.

## 2. File ownership map and API contracts

Paths marked **new** are proposed exact locations. Paths marked **existing** were found in the pinned tree. No HAL/loader/partition changes without separate reviewed decision. New modules use `gm_` prefix to avoid colliding with static upstream symbols. Add SPDX and appropriate notices. Keep standalone core independent of `core.h` and target headers.

| Path | Role |
|---|---|
| `firmware/src/grind/grind.h` (new) | Core types, stable enum values, typed results and pure public declarations. |
| `firmware/src/grind/rng.c`, `grid.c`, `locks.c`, `rules.c`, `core.c`, `serialize.c` (new) | Integer RNG, grid math, G-LOCK validation, explicit style rules, morph pipeline and canonical encoding. |
| `firmware/src/grind/rule_table.h` (new) | 22 style IDs and rule descriptors; factory names/defaults. |
| `firmware/src/grind/controller.h`, `controller.c` (new) | Main-loop config, pending revisions, dirty state and undo; no target calls in portable code. |
| `firmware/src/grind/seq_adapter.c`, `runtime.c` (new) | Actual native conversion, staged publication, native sequence boundaries and runtime reset, IRQ ownership. |
| `firmware/src/grind/performance.c`, `performance.h` (new) | Bounded live state and overlay planning; no persisted overlays. |
| `firmware/src/grind/kit_registry.c`, `kit_registry.h` (new) | 4 kits, fixed roles, native preset mapping and validation. |
| `firmware/src/grind/meta_codec.c`, `meta_codec.h` (new) | Versioned little-endian metadata records; no IO. |
| `firmware/src/grind/persistence_adapter.c` (new) | Existing project/variation transaction integration; fail closed on unsupported formats. |
| `firmware/src/grind/ui.c` (new) | Shared GRIND, STYLE, LIVE, KIT render/input helpers. |
| `firmware/src/grind/amalgam.c` (new) | Includes pure modules once for native, host and WASM. Adapter is included separately at dependency-correct native anchors. |
| `firmware/src/felucca.c`, `seq.c`, `cue.c`, `song.c`, `main.c` (existing) | Integration at confirmed include/lifecycle/cue points. Retain original functions and unrelated song behavior. |
| `firmware/src/pages.c`, `ui.c`, `ui_draw.c`, `ui_input.c`, `ui_tools.c`, `panel.c` (existing) | Native page IDs, rendering, routing, manual lock tracking and physical key map. |
| `firmware/src/project.c`, `varpack.c`, `lockbank.c`, `storage.c` (existing) | Linked records, preflight, codec and fault handling; preserve sector map. |
| `firmware/src/perform.c`, `drum_core.c`, `dmodels.c`, `presets.c`, `fx.c`, `midi_clock.c`, `midi_out.c` (existing) | Native effects, sample/voice/preset mapping, clock and regression surfaces. |
| `firmware/src/editor_backup.c`, `editor_pattern.c`, `editor_proto.h`, `web/editor/*` (existing) | Prevent metadata-loss through legacy editor; optional explicit new protocol with version negotiation. |
| `tests/grind/` (new), `tests/run_grind_tests.sh` (new) | Unit, property, adapter, golden, storage and performance tests. Test suite selector. |
| `tests/drum_host.h`, `tests/sim_host.h`, `tests/sim_core.c`, `tests/sim_test.c`, `tests/sim_glue.mjs` (existing) | Shared inclusion/test hooks, real WASM vectors, UI test surface. |
| `tools/grind/verify_assets.py`, `tools/grind/release_check.py`, `tools/grind/audit.py` (new) | Inventory, release gate, recorded evidence validation. No flash routines. |
| `tools/build.py`, `tools/build_sim.sh`, `tools/publish.sh`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `web/fm1ota.js`, `web/fm1pkg.js`, `web/index_pkg.html` (existing) | Build and package, conditional installer protections; never bypass existing validation. |
| `assets/grind/manifest.json`, `assets/grind/kits.json` (new) | Asset provenance and kit mapping; generated artifacts use native pipeline. |
| `docs/grind/{integration-map,baseline,storage-layout,compatibility,recovery,acceptance,release-checklist}.md` (new) | Recorded decisions/evidence, manuals under `docs/manual/`. |
| `UPSTREAM.md`, `README.md`, `LICENSING.md`, `CHANGELOG.md`, `.github/workflows/grind.yml` | Project identity, source pin, license notices, CI. |

### 2.1 Portable data contract (new, implementation in T04)

These types are logical in-memory representations, not flash or wire format. `step_t` is never cast to `gm_cell` or serialized via its C bitfield ABI. Store cells compactly in four explicit bytes; store locks in a separate 64-byte mask. `length`, `division` and `swing`/source mapping are adapter-owned validated fields; representation shown here uses normalized musical divisions, not Upstream enum values. Additional native P-LOCK/motion entries remain in native banks and are preserved by edits and publication. No new continuous-velocity byte per cell for v1.0; map quantized accents and document limitations.

```c
/* firmware/src/grind/grind.h: core contract, excerpt */
#include <stdint.h>
#include <stddef.h>
#define GM_TRACKS 8u
#define GM_STEPS 64u
#define GM_LOCK_BYTES 64u
#define GM_ENGINE_VERSION 1u
#define GM_ALGORITHM_COUNT 22u
#define GM_ON 1u
#define GM_ACC 2u
#define GM_SLIDE 4u
#define GM_TIE 8u
#define GM_RAT_SHIFT 4u
#define GM_FLAGS_MASK 63u

typedef enum { GM_OK=0, GM_INVALID, GM_LOCK_CONFLICT, GM_LIMIT,
               GM_STALE, GM_NO_CHANGE, GM_NO_SPACE, GM_IO, GM_UNSUPPORTED } gm_result;
typedef enum { GM_KICK=0, GM_SNARE, GM_HAT, GM_RIDE, GM_CRASH,
               GM_CHINA, GM_TOM_HIGH, GM_TOM_LOW } gm_role;
typedef struct { uint8_t flags, cond; int8_t note, micro; } gm_cell;
typedef struct { uint8_t num, den, division, bars; } gm_grid;
typedef struct {
    uint32_t seed, mutation;
    uint16_t bpm, engine_version;
    uint8_t algorithm, aggression, density, chaos, humanize;
    gm_grid grid;
} gm_config;
typedef struct { uint8_t length, division, swing, source; } gm_timing;
typedef struct {
    gm_timing timing[GM_TRACKS];
    gm_cell cell[GM_TRACKS][GM_STEPS];
    uint8_t lock[GM_LOCK_BYTES];
} gm_pattern;
/* all lengths checked before writing; out != base; failed output not publishable */
gm_result gm_grid_length(gm_grid grid, uint8_t *length);
uint32_t gm_mix32(uint32_t x);
uint32_t gm_random(const gm_config *c, uint8_t track, uint8_t step, uint32_t domain);
int gm_is_locked(const gm_pattern *p, unsigned track, unsigned step);
gm_result gm_set_lock(gm_pattern *p, unsigned track, unsigned step, int locked);
gm_result gm_generate(const gm_config *c, const gm_pattern *base, gm_pattern *out);
gm_result gm_validate(const gm_config *c, const gm_pattern *p);
size_t gm_encode(const gm_config *c, const gm_pattern *p, uint8_t *out, size_t cap);
uint32_t gm_hash(const uint8_t *data, size_t length);
```

Rules for the final API: `gm_generate` validates config and base before indexing; returns an error without mutating base or active state. Caller may discard partially built output, never publish it. No heap or VLA in core. For known `gm_cell` fields canonical encoding uses explicit byte writes (including signed values as uint8_t); static assert size 4 on both compilers, but no raw struct dump. Canonical encoder includes config, all track timing, all 512 normalized cells and 64 lock bytes in a fixed order, including locked OFF. Validate unused steps are zero or explicitly retained without execution according to native length rules; do not discard a locked cell. `gm_hash` uses FNV-1a over these bytes as non-cryptographic equality/diagnostic hash; release files use SHA-256. Golden tests compare full bytes, not hash alone. Engine-version mismatch can play stored pattern but prevents regeneration until explicit upgrade.

### 2.2 Command and ownership contracts

`gm_controller` has an active config + pattern revision, one latest request, one prepared native stage, one undo state if budget permits, and an explicit completion/rejection status. Authoritative native track edits, recording, project loads and editor writes are input transactions to the controller; they cannot leave a hidden stale mirror. Revision increments on every accepted base modification, including manual edits and native import. Compare generation `base_revision` and target revision at commit. Main loop owns creation/staging, ISR owns reading active native steps and boundary publication. Do not copy large snapshots or calculate algorithms in IRQ. Use upstream interrupt guard protocol, verified with `host_irq_hook` and native budget reports, not unverified atomics. A stale snapshot must be cancelled before the actual cue boundary. A slot/variation cue and Grind cue share one arbitration path, not two simultaneous commits. Pending command cancelled on STOP or explicit context reset; if stopped, validated new config may be resubmitted immediately by user.

Proposed adapter interface (types defined in new `controller.h`, includes `grind.h`):

```c
typedef enum { GM_BOUNDARY_STEP, GM_BOUNDARY_BAR, GM_BOUNDARY_PATTERN } gm_boundary;
typedef enum { GM_REQUEST_MORPH, GM_REQUEST_STYLE, GM_REQUEST_MUTATE,
               GM_REQUEST_UNDO, GM_REQUEST_EDIT } gm_request_kind;
typedef struct { uint32_t revision, base_revision; gm_boundary at;
                 gm_request_kind kind; gm_config config; } gm_request;
/* main-loop-only; immutable prepared stage handed to native scheduler */
gm_result gm_request_submit(const gm_request *r);
void gm_controller_poll(void);
void gm_controller_native_edit(unsigned track, unsigned step);
void gm_controller_cancel(void);
/* ISR-side: bounded publish + acknowledgement, no rule generation/flash/GUI */
int gm_adapter_commit_at_boundary(uint32_t revision, uint32_t sample_offset);
```

Concrete C adapter bodies require the T03 native audit and P04 measured staging path. Code snippets in this plan specify new core contracts, deterministic fixtures and algorithms; integration instructions identify confirmed functions and exact behavioural assertions rather than pretending a compiled patch exists for a checkout not available locally.

## 3. P01 / v0.1 FOUNDATION

### Task T01: Establish fork, pinned base and isolated development

**Files:** create `UPSTREAM.md`, `docs/grind/baseline.md`, copy approved spec and this plan to their exact paths; no source edits.

- [ ] On the user's authorised GitHub account, create a fork of `DeadActive/hortator` preserving full history under `DaFum/grind-machine` (check name collision; if existing, inspect relationship, never overwrite). Use GitHub UI fork or authenticated `gh repo fork DeadActive/hortator --clone=false --fork-name grind-machine`; only if account and license/asset policy allow. Do not publish new media assets. User has approved design/plan, not a device flash. If fork permission or name fails, stop remote mutation and continue a local clone when available.
- [ ] Clone fork into a new workspace (NOT into the current spec-only directory). Run:

```bash
git clone https://github.com/DaFum/grind-machine.git grind-machine-fw
cd grind-machine-fw
git remote add upstream https://github.com/DeadActive/hortator.git
git fetch upstream main --tags
git cat-file -t 44c9ad4aa57bebf63f412bd552e57143c30b4065
git switch -c foundation/pinned-base 44c9ad4aa57bebf63f412bd552e57143c30b4065
git rev-parse HEAD
git status --short
```

Expected `commit`, exact pinned SHA and no local modifications. If network fails, record the error and do not fabricate source checkout. Use worktree skill before executing separate feature branches. If fork main has advanced, base first integration PR on a verified pin and negotiate branch policy rather than resetting remote main. Configure review/required checks once CI exists. Avoid `gh` execution on behalf of unauthorised accounts.
- [ ] Copy the two design documents from the delivered artifacts. Write `UPSTREAM.md` with upstream URL, SHA, parent lineage, license notice, remote setup, deviations and planned update policy. Baseline file records repo SHA and current state, not invented measurements. Commit `docs: pin Hortator baseline and approved Grind design`.

### Task T02: Capture reproducible baseline builds and test runners

**Files:** existing `build.sh`, `BUILDING.md`, `tools/get_toolchain.sh`, `tools/build.py`, `tools/build_sim.sh`, `tests/run_drum_tests.sh`, `tests/run_tests.sh`; create `docs/grind/baseline.md`, `.github/workflows/grind.yml`, `tools/grind/audit.py` (audit validator introduced T03). No production changes until baseline reproduced.

- [ ] Record `git rev-parse HEAD`, `uname -a`, `cc --version`, `python3 --version`, `node --version`, Docker version, actual upstream toolchain and SDK source/hash/license. Inspect toolchain acquisition script before running; use isolated build machine, not arbitrary SDK redistribution. Build scripts have OS-dependent host fixtures (`libproc.h`); use supported host or test-only portability patch as separate PR with identical assertions, not a blind disabled test.
- [ ] Run these commands independently, record exit codes and full logs:

```bash
python3 tools/build.py --gen-only
sh tests/run_drum_tests.sh
bash tools/build_sim.sh --host-only
bash tools/build_sim.sh --preview
FELUCCA_BLE=0 ./build.sh
FELUCCA_BLE=0 DRUM_PACKAGE=1 ./build.sh
sh tests/run_tests.sh
```

Expected: each supported baseline command exits 0, native package and linker artifacts appear under `build/`, `build/sim/fm1sim.wasm` and `build/sim/preview/` exist. Verify preview has no package. Last suite requires package; if target toolchain/SDK unavailable mark native BLOCKED, do not count full suite as passed. Run `find build -type f \( -name '*.map' -o -name '*.elf' -o -name '*.fwsc' -o -name '*.wasm' \) -print` and hash actual outputs (never choose an artifact by guess).
- [ ] CI: jobs `host-core`, `host-upstream`, `wasm`, `native` (controlled runner with verified licensed tools), `release-gates` (T23). Pin action versions/SHAs and compiler images by resolved digest recorded in baseline, use minimum `contents: read`, no USB upload in any workflow. Configure check to fail when mandatory jobs are absent on release; preview job can report explicit native BLOCKED without pretending full CI green. No third-party SDK secrets in untrusted PR workflow. Test workflow in a draft PR; deliberately introduce a failing host assertion locally to verify CI fails, revert before commit.
- [ ] Commit `ci: capture pinned upstream build and regression baseline` with measured outputs summary and unsupported host/platform blockers.

### Task T03: Audit memory, event timing, flash and assets; freeze adapter interfaces

**Files:** new `docs/grind/integration-map.md`, `docs/grind/storage-layout.md`, `docs/grind/compatibility.md`, `assets/grind/manifest.json`, `tools/grind/audit.py`; read existing native files in section 2 plus `firmware/hal/`, `firmware/loader/`, linker scripts found using `git ls-files '*.ld'`, `tools/stack_depth.py`, `tests/target_budget.py`, `tools/check_loader.py`, `tools/check_erase_order.py`, `LICENSING.md`, `docs/RECOVERY.md`.

- [ ] Build a call-site inventory, not just names:

```bash
git grep -n -E 'cue_tick|cue_switch|seq_cut_ahead|proj_cue_apply|seq_restart|events_block|seq_steps|step_fire|rat_due|seq_stop|cue_poll'
git grep -n -E 'ST_PAYLOAD_MAX|VAR_BYTES|VAR_CAP|FL_JEDEC_OK|proj_save|project_save_go|var_write|var_link|lk_link|proj_fetch|st_save|st_load'
git grep -n -E 'step_edit|rec_hit|editor_pattern|ui_input|ui_draw|drum_boot_init|FAM_'
git ls-files 'firmware/*.ld' 'firmware/**/*.ld' 'assets/*' 'tests/*budget*'
```

Record exact function/owner, caller context (main/IRQ), mutation surfaces (including recording, tools, copy/paste, MIDI, backup/restore, variation changes), native division enum, `P_SLEN`, swing, `cond` enum range, `seq_next`/early micro-hit path, stage/cue cancellation, key masks, boot paths. Capture map and static sizes (`sizeof(step_t)==4`, actual `sizeof(project_t)`, native buffers/stack), App vs RAM budget, flash sectors/record sizes/limits, loader-protected tail and existing user data. Read `tools/check_*` scripts before using with correct argument shape; retain output. Identify whether cue staging can reuse existing project buffers without hidden double allocations; reject allocation plan if 10% reserve gate cannot be met. Record concrete native IDs for models and preset parameters, assets/licenses, and actual device revision as unknown until read-only HW inventory. No reading/writing physical flash in T03.
- [ ] Use explicit JSON audit input `build/grind-evidence/T03/audit.json` with `upstream_sha`, `native_build`, `host_build`, `wasm_build`, `step_bytes`, `project_bytes`, `var_cap`, `ram_app_used`, `ram_app_limit`, `flash_app_used`, `flash_app_limit`, `integration_points` (non-empty path/symbol/owner/callers), and `unknowns`. Use JSON `null` for unknown measurements while native evidence is unavailable. Run [the audit validator](../../../tools/grind/audit.py) with `python3 tools/grind/audit.py build/grind-evidence/T03/audit.json` and [its tests](../../../tests/grind/test_audit.py) with `python3 tests/grind/test_audit.py`. The validator exits 0 for a structurally complete record with passing build statuses, 1 for invalid input, and 2 for a valid record with incomplete build evidence. It requires at least 10% reserve in each measured app budget. A lower reserve requires explicit design approval and measured evidence under NFR-03.

Review the raw build evidence, budget and map manually. Separate licensing gate: enumerate all shipped files, including generated samples/fonts/media and SDK attribution; if an asset cannot be cleared, exclude or replace before distribution. Inspect `LICENSING.md` for separate Felucca rights. Preserve all notices. Propose storage solution T17 based on actual map; if no safe fit, halt native persistence and request design decision, not a new address.
- [ ] Commit `docs: audit native integration, flash, resource and license constraints`. P01 exits only with pinned source, evidence for supported baseline and explicit blockers. Hardware installation remains forbidden.

## 4. P02 / v0.2 GRIND CORE - data and deterministic foundation

### Task T04: Portable types, grid validation and test harness

**Files:** create `firmware/src/grind/grind.h`, `grid.c`, `tests/grind/test_grid.c`, `tests/run_grind_tests.sh`, `tests/grind/common.h`.

- [ ] Write types from section 2.1. Test `gm_grid_length` for all six meters and divisions 8/16/32, bars 1..4. Concrete reference cases:

```c
#include <assert.h>
#include "grind.h"
int main(void) {
    static const uint8_t meters[][3] = {{3,4,12},{4,4,16},{5,4,20},
                                      {6,8,12},{7,8,14},{9,8,18}};
    unsigned i; uint8_t n=99;
    for (i=0;i<6;i++) {
        gm_grid g={meters[i][0],meters[i][1],16,1};
        assert(gm_grid_length(g,&n)==GM_OK && n==meters[i][2]);
        g.division=8; assert(gm_grid_length(g,&n)==GM_OK && n==meters[i][2]/2);
        g.division=32; assert(gm_grid_length(g,&n)==GM_OK && n==meters[i][2]*2);
    }
    assert(gm_grid_length((gm_grid){9,8,32,2},&n)==GM_LIMIT);
    assert(gm_grid_length((gm_grid){4,0,16,1},&n)==GM_INVALID);
    assert(gm_grid_length((gm_grid){4,4,7,1},&n)==GM_INVALID);
    assert(gm_grid_length((gm_grid){4,4,16,0},&n)==GM_INVALID);
    assert(gm_grid_length((gm_grid){4,4,16,4},&n)==GM_OK && n==64);
    assert(gm_grid_length((gm_grid){4,4,16,255},&n)==GM_INVALID);
    return 0;
}
```

- [ ] New shell runner (extend explicit suite cases as tasks land; use separate compilation for pure core):

```sh
#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/grind-host
CC=${CC:-cc}
CFLAGS="-std=c99 -O1 -g -Wall -Wextra -Werror -UNDEBUG ${GRIND_SAN_FLAGS:-}"
case "${1:-all}" in
  grid|all) $CC $CFLAGS -Ifirmware/src/grind tests/grind/test_grid.c \
      firmware/src/grind/grid.c -o build/grind-host/test_grid
      build/grind-host/test_grid ;;
  *) echo "unknown suite" >&2; exit 2 ;;
esac
```

Run `sh tests/run_grind_tests.sh grid`; red due to missing grid implementation, then add behavioural negative cases. Implement `gm_grid_length` with allowlist `(num,den)`, division 8/16/32, bars 1..4; `uint32_t product=num*division*bars`; require `product%den==0` and result 1..64. Return `GM_LIMIT` for representable but oversized lengths, `GM_INVALID` for unsupported values/null out. Only write out on success. The table above exercises the 7/8 eighth-note grid correctly (7 steps). Config validation: algorithm 1..22, current engine, BPM within approved 40..350 target, each knob <=100, compatible track timing; effective native bounds later intersect measured limits. Add compile-time cell/lock size checks via `_Static_assert` where supported or C99 typedef checks.
- [ ] Run same command green and `GRIND_SAN_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' sh tests/run_grind_tests.sh grid` on supported host. Add actual suite output. Commit `feat(core): define bounded rhythm data and validated musical grid`.

### Task T05: Keyed PRNG and canonical byte encoding

**Files:** create `rng.c`, `serialize.c` under `firmware/src/grind/`, `tests/grind/test_rng.c`, `tests/grind/test_codec.c`, `tests/grind/golden/rng-v1.txt`, `tools/grind/rng_reference.py`; modify test runner.

- [ ] Define PRNG v1 using fixed-width unsigned arithmetic, independent of traversal order. Use avalanche mixer (all operations modulo 2^32):

```c
uint32_t gm_mix32(uint32_t x) {
    x ^= x >> 16; x *= UINT32_C(0x7feb352d);
    x ^= x >> 15; x *= UINT32_C(0x846ca68b);
    return x ^ (x >> 16);
}
uint32_t gm_random(const gm_config *c, uint8_t t, uint8_t s, uint32_t domain) {
    uint32_t x = gm_mix32(c->seed ^ UINT32_C(0x9e3779b9));
    x = gm_mix32(x ^ c->mutation);
    x = gm_mix32(x ^ ((uint32_t)c->engine_version << 16) ^ c->algorithm);
    x = gm_mix32(x ^ ((uint32_t)t << 8) ^ s);
    return gm_mix32(x ^ domain);
}
```

Domain constants: 1 optional-hit density, 2 chaos choice, 3 accent threshold, 4 micro timing, 5 accent humanization, 6 fill, 7 alternate cymbal choice. Freeze constants after review. Use `%100u` for stable threshold (not crypto); no time, `rand()`, or float dependence. Mutation changes only seed/revision as specified; no hidden global RNG. Independent Python reference (`tools/grind/rng_reference.py`, added in this task): implement mask `& 0xffffffff` after multiplication, emit outputs for seeds 0,1,0xffffffff, alg 1/22, track 0/7, step 0/63, domains 1..7. Check hand-verifiable `gm_mix32(0)==0`, `gm_mix32(1)==0x688990c0`; compare generated reference fixture exactly, include at least one nonzero full keyed vector, review it before freezing. Test that loops reversed produce same per-cell output and 100 reruns identical.
- [ ] Canonical stream format v1, little endian: bytes `G M C 1`, engine u16, algorithm u8, BPM u16, seed u32, mutation u32, four knobs u8, grid num/den/div/bars u8, then each track timing 4 bytes, 64 cells x 4 bytes (flags masked, cond, note, micro), finally lock 64 bytes. Fixed size 2169 bytes (25 header + 32 timing + 2048 cells + 64 locks). Serialize via `put_u8`, `put_u16`, `put_u32` and checked capacity; verify header offsets in tests (sum: 4+2+1+2+4+4+4+4=25). Use single constant `GM_ENCODED_SIZE = 25 + 8*4 + 8*64*4 + 64`. Refuse cap shorter than this and invalid input; return 0, no overflow. No `sizeof(gm_config)` or endian dependence. FNV-1a offset 2166136261, multiply 16777619 unsigned; test empty equals offset, `"hello"` equals `0x4f9f2cab`.
- [ ] Write tests for exact first/header and final lock bytes, negative micro, all-zero off cell, nonzero fields, buffer canaries, cap 0/size-1/size, out-of-range config rejected, independently constructed expected bytes. Run `sh tests/run_grind_tests.sh rng` and `... codec` red; implement and extend runner so `all` runs both. Run same green with sanitizer, then `all`. Commit `feat(core): version keyed PRNG and canonical pattern encoding`.

## 5. P03 / v0.2 GRIND CORE - styles, morphing, locks and validation

### Task T06: Implement 22 style rules and factory defaults

**Files:** new `firmware/src/grind/rule_table.h`, `rules.c`, `tests/grind/test_rules.c`, `tests/grind/golden/styles-v1.csv`; modify `grind.h`, runner. Implement in three commits (blast/grind, death/PV/hardcore, experimental/doom).

- [ ] Establish fixed role indices in section 2.1. Table is the independent reference grid at 4/4, 16th, density=0, chaos=0, humanize=0, no locks, one bar except marked. Numbers are zero-based step indices; `all` = 0..15, `even` = 0,2,...14, `quarter` = 0,4,8,12. Crash on step 0 unless specified; unlisted roles off. Optional cymbals/fills never count as core. Each row becomes a C descriptor of masks per role plus optional/forbidden masks, `rat` policies, native timing exceptions and `bars`. Use explicit `UINT64_C(1)<<step` with checked index. Define table independently in tests (do not derive expected masks from implementation). Rule choices below refine the approved stylistic descriptions; audible validation is mandatory.

| ID/name | Kick | Snare | Lead cymbal | Additional core/style constraints |
|---|---|---|---|---|
| 01 Traditional | even | odd | H even | S/K alternate; no mandatory overlap. |
| 02 Hammer | even | even | R even | Simultaneous K/S; optional odd paired hits. |
| 03 Bomb | all | even | R even | Kick rat=1 on all steps, snare subdivision remains coarser. At 1/32 use single kick hits (avoid multiplying density beyond validated budget). |
| 04 Gravity | quarter | even | R quarter | S rat=1 (2 hits), sparse K/R; at 1/32 adapt to single rolls. Accents alternate across S hits (native cell accent cannot accent individual ratchets). |
| 05 Grind Blast | even + 15 | odd | H even | K step 15 double optional as budget allows; China on final beat; tom fills optional. |
| 06 D-Beat | 0,3,6,8,11,14 | 4,12 | H even | D-beat anchor kicks/backbeats. |
| 07 Skank | 0,4,8,12 | 2,6,10,14 | H even | Fast alternating eighths. |
| 08 Doublebass | all | 4,12 | R quarter | Optional finer kick subdivisions, backbeat preserved. |
| 09 Technical | 0,3,6,11,14 | 4,12 | R 0,3,6,9,12,15 | High tom 10, low tom 15; groups of 3/5. |
| 10 Brutal | 0,1,2,6,7,10,11,14 | 4,8,12 | H 0,4,8,12 | China 0,8; kick bursts. |
| 11 Fast-Slow (2 bars) | 0..15 even; 16,19,22,28 | 0..15 odd; 24 | H even first; 16,20,24,28 second | Default 32 steps; second half sparse and half-time, C 0,16. |
| 12 Abrupt Break | 0,2,4,6,12,14 | 1,3,5,7,12,15 | H 0,2,4,6,12,14 | 8..11 absolute planned silence; C 0,12. |
| 13 Stop-and-Go | 0,1,4,8,9,10,14 | 0,4,8,14 | X 0,4,8,14 | Entire cells 2,3,5,6,7,11,12,13,15 protected as intentional rests unless manually locked ON; no random fill. |
| 14 Breakdown | 0,3,6,10,12,15 | 8 | H quarter | X 0,8; half-time. |
| 15 Two-Step | 0,3,6,8,11,14 | 4,10,12 | H even | Offbeat drive distinct from D-beat. |
| 16 Half-Time | 0,2,6,12 | 8 | R even | No dense snare roll; optional K groups. |
| 17 Chaos | 0,5,9,14 | 4,11 | R quarter | T high 7, low 13; stable pulse reference, variation zones. |
| 18 Industrial | 0,4,8,12 | 4,12 | H even | X 2,10; no default humanize, repetitive levels. |
| 19 Polymetric | 0,4,8,12 (16 steps) | 4,10 (12 steps) | H even (16 steps) | X 0,5,10 (15 steps); other tracks length 16, same division. |
| 20 Sludge | 0,3,8,14 | 4,12 | R 0,6,8,14 | Low tom 15; sparse, heavy pulse. |
| 21 Funeral | 0,10 | 8 | R 0,8 | Low tom 15; planned rest 4..7; slower preset tempo. |
| 22 Stoner | 0,3,7,8,11 | 4,12 | H even | Optional swing native; ghost/accent variation. |

- [ ] Make each style recognisable across supported grids. Use musical position in 96 ticks/quarter, not arbitrary percentage of entire pattern: repeat/crop beat-relative motifs for altered meter, anchor snare at musically meaningful backbeats, remap accent groups and ensure no duplicate events on coarser grids. For pattern-specific pauses/segments (11-13) map segment endpoints in bar-relative positions; preserve planned silence. ALG-11 requires two bars; reject incompatible lengths rather than silently truncate. Polymetric derives track lengths from normal length L: K/H/others L, S `max(1,floor(3L/4))`, X `max(1,L-1)`; use explicit common division. When coarser grid cannot express alternation or mandatory pulse distinctly, reject with `GM_UNSUPPORTED` or use native ratchets only if exact musical semantics and safe; document which combinations are supported. Every style default must be valid. Factory defaults: aggression 75/density 65/chaos 20/humanize 25 unless style sets lower; Industrial humanize 0; Sludge 90 BPM, Funeral 55, Stoner 110, Breakdown 150, others default 240 for fast styles or sensible documented tempo. Defaults are bounded by measured device range.
- [ ] Red/green per style family: `sh tests/run_grind_tests.sh rules`. Assertions compare role masks, paired vs alternating hits, ratchet counts (2 total for rat=1), silence zones, length 32 for Fast-Slow, 16/12/15 polymetric cycles; table covers all 22 IDs and names (unique IDs). In addition assert `gm_grid_length` across grids, no nonzero generated cells beyond length, no missing pulse anchors. D-beat mask expectation must be based on the listed positions; compute independently in test as `(1u<<0)|(1u<<3)|(1u<<6)|(1u<<8)|(1u<<11)|(1u<<14)` rather than trusting a copied literal. Confirm golden files before enabling default density. Implement each family, run red then green, commit `feat(core): add blast and grind styles`, `feat(core): add death and hardcore families`, `feat(core): add experimental and doom styles`. No arbitrary random fills.

### Task T07: Morph pipeline, G-LOCK and exact rejection semantics

**Files:** new `firmware/src/grind/locks.c`, `core.c`, `tests/grind/test_morph.c`, `tests/grind/test_locks.c`; modify rules/grid/runner as required.

- [ ] Write red cases: for 0..100 density increasing with same seed/config, optional ON set is a superset (except locked cells unchanged); aggression changing 0 to 100 never changes ON/OFF; chaos 0 deterministic base, chaos 100 never touches anchors or prohibited rests; Humanize 0 sets no new micro (locked cells unchanged). Check full ON and OFF lock payload (`flags`, `cond`, `note`, `micro`) and mask unchanged. For lock index use `i=track*64+step`, byte `i>>3`, bit `1u<<(i&7)` only after `track<8 && step<64`. Test boundary 7/63 and reject 8/0, 0/64. OFF locked with arbitrary valid native fields remains OFF. Changing grid so lock outside new length or non-equivalent cell time => `GM_LOCK_CONFLICT`; source still byte-identical. This is stricter than silently moving a locked step.
- [ ] Implement fixed precedence: validate input and config -> fill style anchors/optional/forbidden maps -> add optional candidates using `gm_random(domain=1)%100 < density` -> use chaos domain for optional substitutions/accents only (choose candidate family before density threshold, independent of density value) -> dynamic accent threshold (`aggression` mapped to native binary accent; preserve mandatory phrase accents) -> bounded micro and humanized accent -> copy protected cells/mask verbatim -> validate output and native-event budget. Planned silence only overridden by explicit user lock. Never change manual P-LOCK/motion data. Reject if native adapter cannot represent required accent/timing. `gm_cell` native quantization uses `acc` 0/1 rather than pretending +/-12% continuous velocity; for unlocked notes choose 96/127 with deterministic accent selection and document that native two-level dynamic granularity exceeds fine target, require audible acceptance. For micro, bound by min(6 ms, step/8) scaled by Humanize. Compute native percent limit as `min(12, floor(6*bpm*division*100/240000))`; multiply by humanize/100, cap native `MICRO_MAX`; use 32-bit arithmetic with checked inputs, signed offset generated from domain 4. Reduce bound for shortest swung adjacent interval and prevent negative first hit, cross-cue early hits and ordering inversion via adapter checks. No new hits from humanization. Native ratchets may reduce safe micro limit; test worst case. Aggression optional DSP mapping deferred until kit review; if used, apply native smoothing and no master gain jumps.
- [ ] `gm_validate`: allowlist config, meter and grid, validate every timing/step, `(flags & ~GM_FLAGS_MASK) == 0`, native cond/note/micro range from audit, no unrepresentable division, protected range checked. Count `1+rat` hits per active step and compute conservative per-track and simultaneous event budgets using native voice cost/limits; reject `GM_LIMIT` before publication. Never drop locks to meet budget. Distinguish event budget from voices already decaying. Set generated `cond=0` unless expressly needed; no uncontrolled conditional randomness for generated base. Use fixed arrays, no allocation. Test corrupt inputs and no stale output publication. Run `sh tests/run_grind_tests.sh morph`, `... locks`, `... rules`, all with sanitizer; expected red before changes, green after. Commit `feat(core): add morph controls and protected step semantics`.

### Task T08: Property testing, stable presets and cross-platform vectors

**Files:** new `tests/grind/test_properties.c`, `tests/grind/test_golden.c`, `tests/grind/golden/patterns-v1.csv`, `tests/grind/golden/patterns-v1.bin`, `tools/grind/check_vectors.py`; modify runner, `rule_table.h`.

- [ ] Build fixture per all 22 default IDs plus seeds 0,1,42,UINT32_MAX; exercise 6 meters x 3 divisions x supported bars, boundary knobs 0/1/50/99/100 and lock combinations. Deterministic test PRNG (separate from product RNG) generates at least 10,000 bounded cases including invalid IDs, invalid lengths, signed-to-unsigned bad inputs, corrupted flags, locked OFF, seed wrap, hostile record-derived values. For success assert output validated, source unchanged, each cell within bounds, repeated result byte-equal, no overflow. For failure assert no state publication. Capture failure seed/config in diagnostic. Use independent masks from T06 and explicit byte fixtures from T05, not merely self-goldens. Fixed golden version change must be reviewed.
- [ ] Add `gm_factory_config(id, &cfg)` and `gm_factory_pattern(id, &out)` through same core, no flash and no user slot consumption. Test 22 distinct valid IDs, names and preset defaults, no invalid zero; initial Bomb Blast preset ALG-03. Test 100 repetitions for each selected golden. `sh tests/run_grind_tests.sh properties`, `... golden` then `GRIND_SAN_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' sh tests/run_grind_tests.sh all`. Run with GCC and Clang where available; compare encoded fixtures. Expected 0 assertion failures, ASan/UBSan clean. CI pure job runs without native SDK. Commit `test(core): lock style vectors, presets and adversarial input properties`.

## 6. P04 / v0.2 GRIND CORE - native publication and controller

### Task T09: Map core to real Hortator native steps

**Files:** create `firmware/src/grind/amalgam.c`, `seq_adapter.c`, `runtime.c`, `tests/grind/test_adapter.c`; modify `firmware/src/felucca.c`, `tests/drum_host.h`, `tests/sim_host.h` at audited inclusion anchors (avoid duplicate includes), `tests/run_grind_tests.sh`. Inspect `params.c`, `seq.c`, `core.h`, `project.c`.

- [ ] Add host adapter tests using actual native `step_t` and `track_t` from upstream fixture (not a fake copy of ABI). Include pure amalgam once; convert `step_t` to/from explicit cell field assignments:

```c
static gm_cell gm_from_native(const step_t *s) {
    gm_cell c;
    c.flags=(uint8_t)(s->on | (s->acc<<1) | (s->slide<<2) |
             (s->tie<<3) | (s->rat<<GM_RAT_SHIFT));
    c.cond=s->cond; c.note=s->nt; c.micro=s->micro; return c;
}
static void gm_to_native(step_t *s, gm_cell c) {
    /* call only after validation; assign all fields, do not copy padding */
    s->on=c.flags&1u; s->acc=(c.flags>>1)&1u;
    s->slide=(c.flags>>2)&1u; s->tie=(c.flags>>3)&1u;
    s->rat=(c.flags>>GM_RAT_SHIFT)&3u;
    s->cond=c.cond; s->nt=c.note; s->micro=c.micro;
}
```

Test roundtrip all legal flag values, boundary note -60..67, micro +/-50 native on imported data (generated data stricter), native `cond` allowlist, locked OFF with P-LOCK separately retained. Inspect audited `P_SDIV` enum mapping; test every allowed musical denominator maps back exactly. For new generated timing force native sequencer source rather than Grids; preserve manually set source only when compatible. Existing Grids cannot concurrently trigger a second set of hits on generated tracks. Route song-chain ownership explicitly: leave native song mode intact, refuse Grind generation during active song (or transition with visible STOP). Do not delete unrelated code. Add fixture for a Bomb 16th at 240 BPM: check two kick hits per step, single snare events on even steps and actual event timestamps from `seq_tick`, including start and tail. No synthetic duplicate timer.
- [ ] Red `sh tests/run_grind_tests.sh adapter` (host fixture compiled with generated headers via `python3 tools/build.py --gen-only` and same flags as upstream). Implement adapter, `amalgam.c` includes portable `.c` files once for each path, `#include` placed at verified static symbol dependencies. Keep `step_t` and native persistent sizes unchanged. Build host adapter, full drum suite, `bash tools/build_sim.sh --host-only`, native link when available. Compare map (including static memory), no duplicated code or unresolved symbols. No hardware commit yet. Commit `feat(adapter): map deterministic patterns to the native step ABI`.

### Task T10: Bounded controller, latest-wins and native cue publication

**Files:** new `firmware/src/grind/controller.h`, `controller.c`, `tests/grind/test_controller.c`, `tests/grind/test_cue.c`; modify `seq_adapter.c`, `runtime.c`, `firmware/src/seq.c`, `cue.c`, `project.c`, `main.c` only at audited ownership/lifecycle points; `tests/drum_host.h`, `tests/sim_core.c` shared lifecycle.

- [ ] Red tests: stage revision 1 -> request revision 2 before boundary -> only 2 publishes; stage revision 3 -> native manual edit increments base revision -> 3 rejected, edit remains; stage then STOP -> no late commit; simultaneous native cue and Grind request -> deterministic cancellation/ownership; 16/12/15 same division -> switch at longest 16 (not LCM 240), all restart Step 0 at exact sample offset; mixed divisions and swing choose longest actual native cycle; early micro hit crossing replacement blocked (`seq_cut_ahead` regression); use host IRQ injection when guard released. Count each scheduled hit (no lost/doubled event), compare outgoing/incoming pattern signatures at every simulated block. Test repeated 1000 knob updates and revision wrap (use equality and explicit invalidation rather than signed ordering across wrap). Assertions: `active_revision` stable until commit; `pending.base_revision == native_revision` immediately before apply; no active mutation on stage failure.
- [ ] Implement one bounded mailbox: main `submit` replaces pending intent, `poll` creates output into reserved stage, validates then publishes stage identity under upstream short IRQ guard. ISR consumes exactly once at native cue boundary and acknowledges; main reconciles config/dirty only for matching ack. Staging buffer ownership FREE -> WRITING -> READY -> READING -> RETIRED -> FREE, transitions under existing guard; never reuse READING buffer; cancel stale READY before publishing new stage. Native revision and owner token checked on both publish and consume. No busy wait, heap, generation, flash or UI in callback. Reuse native staging path and `cue_tick`/`cue_longest`/`cue_switch` boundary calculation with explicit Grind variant request (not fabricated existing API). Integrate `seq_cut_ahead` and native `seq_restart(x,...)` using same real sample offset. If `proj_cue_apply` path would write variation banks or copy too much in ISR, separate an in-RAM staged Grind commit using the same boundary arbitration, and document exact code path in integration map; measure it before enable. Commit must be all-or-none, preserve native P-LOCK and motion, kit state and unrelated tracks. STOP clears pending and overlay; stopped new validated command publishes synchronously with native guard. When a native editor/recording path touches a step, notify/invalidate controller before the mutation becomes visible and prior to next publication; avoid hidden second source of truth. UI shows requested versus committed config. On conflict cancel with visible status rather than auto-overwriting edits. Dirty acknowledgement tied to successful base change, not merely dial movement. Use existing guard style, not unchecked C11 atomics on pi32v2. Map budget gate: use measured stage allocation and single undo reuse later; if insufficient, stop to revise design, no unverified memory reuse.
- [ ] Run `sh tests/run_grind_tests.sh controller`, `... cue`, `... adapter`; upstream `sh tests/run_drum_tests.sh` includes cue, song, micro, sync, IRQ tests. `bash tools/build_sim.sh --host-only`; native budget checks when available. Inspect ISR call graph and max copy time, no loops over generator in IRQ. Commit `feat(controller): stage revision-safe Grind changes at native cue boundaries`. P04 exit: host AT-07/08 and native build/budget evidence or explicit simulator-only mode.

## 7. P05 / v0.3 GRIND UI - hardware-first and shared simulator

### Task T11: GRIND/STYLE screens, boot and input routing

**Files:** create `firmware/src/grind/ui.c`, `tests/grind/test_ui.c`; modify `firmware/src/felucca.c`, `pages.c`, `ui.c`, `ui_draw.c`, `ui_input.c`, `panel.c` only where T03 maps real display/input hooks. Modify `tests/sim_core.c`, `tests/sim_test.c`, `web/sim/layout.js`, `web/sim/controls.js` for matching labels, not rhythm logic.

- [ ] Red host UI fixtures: boot fresh -> GRIND, ALG-03 selected, transport stopped, no save/erase; select STYLE -> preview candidate -> cancel preserves base; confirm while playing -> pending only, native cue confirms revision; turn knob 1..4 -> exactly one of Aggression/Density/Chaos/Humanize changes; press HOME, ARP tap/hold, SEQ, EDIT, PLAY, REC, FX and GLO/SAVE -> single routed action, never steals recovery combination. Assert visible state and native transport; drive the real key bitmasks discovered in T03, not simulated mouse-only commands. Preserve `REC` and existing FX/MIDI semantics. Input ownership table in test fixture: GRIND owns morph knobs; STYLE owns selection/confirm; SEQ owns white keys; LIVE owns six black keys; GLOBAL owns system settings. Globally reserved transport and recovery always take precedence. Explicitly test short/long thresholds and release after page change. Test UI starts without keyboard focus and no audio until user PLAY.
- [ ] Implement GRIND default layout in native 240x240 coordinates: header y=0..25 (name/project/BPM/play/clock/dirty); style/status y=26..52; 8x16 grid x=44..234, y=56..164, visible playhead and G-LOCK with outline shape distinct from native P-LOCK; four compact controls y=166..217; footer y=218..239 for pending/error and navigation. Use existing gfx/text routines and font sizes verified on device. Color: near-black background, warm-white text, red kick/primary, amber snare, gray cymbal; never encode lock/play state by color alone. Selected role and bank displayed. If labels cannot fit, use T03-approved abbreviations and selected-track scroll layout. STATUS priority: safety error > pending > normal. No full-screen blocking delay; use native redraw budget. GRIND home and STYLE open via audited ARP/HOME path; long ARP enters LIVE page shell without action before P07. STYLE lists seven categories, IDs and meter/divisions; valid confirm dispatches controller only, error stays visible; cancel does not mutate config. Clamp encoder increments 0..100, acceleration in UI, no repeated flash writes. Existing track selector stays track selector. Project/kit/MIX/GLOBAL routes use native pages. Boot Factory Bomb only if no valid stored state and transport stopped; corrupted project shows warning, never writes defaults automatically.
- [ ] Run `sh tests/run_grind_tests.sh ui`, `sh tests/run_drum_tests.sh`, `bash tools/build_sim.sh --host-only`, then `bash tools/build_sim.sh --preview`; inspect rendered real simulator screen at 240x240 and zoomed browser view with mouse/keyboard. Confirm original `tests/ui_test.c`, `tests/redraw_test.c`, `tests/input_test.c`, `tests/boot_test.c` remain valid. Native budget: UI feedback target <=100 ms; redraw during dense audio. Commit `feat(ui): add hardware-first Grind and style views`.

### Task T12: Step editor, factory selection and WASM parity

**Files:** create `tests/grind/test_step_edit.c`, `tests/grind/test_sim.mjs`; modify `firmware/src/grind/ui.c`, `controller.c`, `runtime.c`, `firmware/src/ui_tools.c`, `ui_input.c`, `seq.c`, `tests/sim_core.c`, `tests/sim_glue.mjs`, `tools/build_sim.sh`, `web/sim/app.js`, `web/sim/index.html`, `web/sim/sim.css`, `web/sim/manual.js`; retain the upstream simulator loader/AudioWorklet.

- [ ] Red tests: all 16 white-key positions in banks 1..4 for selected track, 64th step maps to index 63; out-of-length key rejects visibly; toggling ON and OFF sets G-LOCK; explicit LOCK tool toggles lock without toggling hit; P-LOCK glyph/automation not changed; UNLOCK ALL confirmation cancel preserves all bits. Native step knob edits, copy/paste, record, editor operations must trigger revision updates and protect manual edits; if an operation cannot be mapped safely, disable with clear message in GRIND context rather than losing locks. Stale pending canceled on edit. Native mute does not change cell. Factory presets 22 accessible in STYLE without consuming A-D user slots; restore/load error safe. `gm_factory_config`/`gm_generate` from T08 only. Host test for focus, pointer cancel and keyup; browser JS invokes existing simulator panel inputs, never implements a second generator.
- [ ] Add diagnostic-only simulator API: write canonical encoded pattern bytes into bounded native buffer; expose size and pointer via WASM export for host comparison. Use existing `tests/sim_core.c` export convention and add symbols to `tools/build_sim.sh` exported function list. `gm_encode` output compare in `tests/grind/test_sim.mjs` against T08 independent vectors; never rely on a second JS generator. Use same core version in `version.json` and native build. Real WASM boot and test via upstream loader/test glue, then use `node tests/grind/test_sim.mjs` with built `build/sim/fm1sim.wasm`; fail if export missing or bytes differ. Preserve simulator local persistence and panel controls. Disable package install in preview; remove or clearly label generic upstream demo/installer links which could mislead a user into loading non-Grind data. Update landing labels without changing protocol identity. Test offline load from local built assets after initial file delivery, no remote runtime dependencies for core.
- [ ] `sh tests/run_grind_tests.sh step-edit`, `bash tools/build_sim.sh --preview`, `node tests/grind/test_sim.mjs`, native host suite. Red missing API/incorrect lock then green. Browser preview checks: 4 knob controls, style selector, bank switch, STOP, key context, no installer package. Validate desktop/mobile display manually and with reproducible browser tests (if browser tooling available). Commit `feat(ui): expose protected step editing and shared WASM parity`. P05 exit AT-01/04/05/16 in host/WASM; hardware still pending.

## 8. P06 / v0.4 SOUND DESIGN - four kits and safe audio

### Task T13: Factory kit registry, legal assets and native presets

**Files:** create `firmware/src/grind/kit_registry.h`, `kit_registry.c`, `assets/grind/kits.json`, `assets/grind/manifest.json`, `tools/grind/verify_assets.py`, `tests/grind/test_kits.c`; modify `tools/presets.json` and generated `firmware/src/preset_bank.h` via `tools/gen_presets.py` (verify real generator inputs), `tools/gen_samples.py`/`tools/build.py` only if necessary for cleared assets, `LICENSING.md`, `firmware/src/grind/ui.c`.

- [ ] Resolve native models/preset IDs and valid parameter ranges from `dmodels.c`, `presets.c`, `params.c`, `tools/presets.json`, existing CC0 asset records. Select 8 roles per kit; do not hardcode unknown model IDs. Use this sound matrix as acceptance intent (parameters normalized to percent of verified native ranges, tuned by audio measurement, not raw register values):

| Role | BRUTAL DEATH | RAW GRIND | INDUSTRIAL HELL | DOOM ABYSS |
|---|---|---|---|---|
| KICK | tight transient, short decay, clear click | dry, mid-heavy, mild saturation | synthetic hard kick, metallic edge | low pitch, long body |
| SNARE | tight noise/body and crack | raw acoustic/noise with room kept low | gated noise or metallic snare | low-tuned heavy snare |
| HAT | short closed + native choke | grainy dry hat | short filtered metal | darker slower hat |
| RIDE | clear bell with controlled decay | raw brighter lead | repetitive metallic resonator | dark long ride |
| CRASH | controlled bright phrase hit | rough crash | noise-metal burst | long dark crash |
| CHINA | aggressive trashy distinct timbre | short metallic crash | processed metallic percussion | low trashy swell |
| HIGH TOM | quick high body | dry high tom | tuned metallic perc | resonant high tom |
| LOW TOM | quick deep body | dry low tom | synthetic low impact | long low tom |

Factory kits use existing native presets/models or approved short CC0/own samples; they are four maps, not four complete large sample banks. Preserve native choke behavior, K/S re-trigger clarity and voice cost. Start with conservative per-track level (e.g. normalized <=65% of validated non-clipping range), low effect sends except Doom, unity-or-lower master and verified limiter. No arbitrary new DSP. Use existing synthesis when a legal distinct sample is unavailable. Do not claim multi-layer velocity or individual Doublebass L/R voices unless measured and implemented.
- [ ] `kits.json` schema: four stable IDs (1..4), name, eight entries with `role` 0..7, audited `model_id`, `preset_id`, bounded native parameters, `asset_ids` list, `gain_defaults`, optional `choke_group`. Test role set equals 0..7 for each kit; reject duplicate IDs, missing role, invalid model/parameter, unknown asset. Store immutable manifest entries `{id,path,sha256,source,author,license,modifications}` with paths relative to repo; licenses assessed manually. Verify exact bytes, do not treat license string alone as clearance. For release inventory include inherited assets as well. Minimal validator logic:

```python
import hashlib, json
from pathlib import Path
root = Path.cwd().resolve()
data = json.loads((root/'assets/grind/manifest.json').read_text())
seen = set()
for entry in data['assets']:
    path = (root/entry['path']).resolve()
    assert path.is_relative_to(root) and path.is_file()
    assert entry['id'] not in seen
    seen.add(entry['id'])
    assert all(entry.get(k) for k in ('source','author','license','modifications'))
    assert hashlib.sha256(path.read_bytes()).hexdigest() == entry['sha256']
for kit in json.loads((root/'assets/grind/kits.json').read_text())['kits']:
    assert sorted(x['role'] for x in kit['tracks']) == list(range(8))
    assert all(a in seen for x in kit['tracks'] for a in x['asset_ids'])
```

Extend with schema/types, stable IDs, reviewed license allowlist per asset kind, no traversal, symlinks or unlisted distributed files; derived files link source+generator+hash. Python version pinned T02; if older than 3.9 replace `is_relative_to` with safe parent check. Reject altered hash, unknown license, unlisted file, duplicate role, incorrect native range. `python3 tools/grind/verify_assets.py` red on malformed fixture, green valid. `python3 tools/gen_presets.py --check` and actual generation commands identified by T03. No re-distribution of non-GPL Felucca media without license. Record source at version/hash and author notices.
- [ ] Implement `gm_kit_registry` with fixed IDs, validate all eight native preset descriptors before any apply; show KIT page, selection/confirm, same role names; STOP-only switching initially (`STOP TO LOAD KIT` while playing). If safe native fade/smoothing measured, enable quantized switch later. No step/lock/seed change on kit selection, dirty on successful change only. Run `sh tests/run_grind_tests.sh kits`, upstream sound pack, preset and sample tests, WASM UI. Commit `feat(sound): add four licensed extreme-metal factory kits`.

### Task T14: Audio regression, headroom, voices and choke

**Files:** create `tests/grind/test_audio.c`, `tests/grind/fixtures/audio-cases.csv`, `docs/grind/sound.md`; modify `kit_registry.c`, `assets/grind/kits.json`, native preset inputs only as needed; read `firmware/src/drum_core.c`, `dmodels.c`, `fx.c`, `dm_sample.c`, `eng_sample.c`, `bench.c`, `tools/fm1_bench.py`, `tests/drum_test.c`, `tests/target_budget.py`.

- [ ] Red fixtures: all four kits x eight tracks trigger correct role; peak/finite PCM and decay; simultaneous hits and fast 16th/32nd kick at target 240 and maximum supported BPM; test retrigger at minimum safe interval, voice stealing count, hats/choke, China and Crash separately, no clipping from arithmetic overflow masked by limiter. Compare current native reference output on non-Grind kit to baseline. Test kit selection while playing leaves old kit unchanged. Log per-track peak/RMS and zero/clip counts, event counts versus scheduler, maximum voices, target timing. Example test assertion for recorded native sample output: after triggering each role, accumulate absolute peak in int64, assert peak > 0 and <= native documented output full scale, ensure no unexpected saturated runs and event counter equals expected; thresholds derived from validated reference and review, not a claim that output quality follows peak only.
- [ ] Tune only native mapped parameters: lower decay/levels if high-tempo voices overlap excessively, reduce optional FX before losing required features. If native accent two levels produce poor Humanize, retain exact quantized behavior and record hearing test; any continuous velocity change requires new measured scope. Use native choke group and existing declick path; ensure CHOKE feature T15 can suppress eligible new cymbal events without mutating stored steps. Review samples audibly in simulator (native output same core, hardware confirmation later). Document audio preview methodology and actual results; no sample clip or waveform claim without evidence.
- [ ] `sh tests/run_grind_tests.sh audio`, upstream `sh tests/run_drum_tests.sh`, `bash tools/build_sim.sh --preview`, native build + map/target budget if available. Commit `test(sound): validate retrigger, choke, voice budget and kit headroom`. P06 exit all 4 kits validated in host/WASM; hardware hearing and max-tempo gate outstanding.

## 9. P07 / v0.5 LIVE ASSAULT - reversible performance

### Task T15: Hold/release actions and native event overlays

**Files:** create `firmware/src/grind/performance.h`, `performance.c`, `tests/grind/test_performance.c`; modify `runtime.c`, `controller.c`, `seq_adapter.c`, `firmware/src/seq.c`, `cue.c`, `perform.c`, `drum_core.c`, `ui_input.c`, `firmware/src/grind/ui.c`, simulator panel fixture. Inspect native hit/early micro/ratchet paths (including `step_fire`, `seq_next`, `rat_due`, `seq_steps`) before editing.

- [ ] Define explicit action IDs 0..5 in order BLAST, DOUBLE, FILL, BREAK, MUTATE, CHOKE. `gm_live_state` has held mask, pending mask, active mask, per-action deadline/heartbeat, one fill window, one mutation latch; no saved overlay. Test each action, all 15 pairs, all-held, release, duplicate press, page exit, external STOP and lost release. Test boundaries with exact sample/tick trace: BLAST/DOUBLE/FILL next step; BREAK next bar (native musical tick mapped to current meter); CHOKE beginning of next audio block; MUTATE next pattern boundary T16. Watchdog resets unobserved held actions by eight current pattern/takt cycles, refreshed only by physical observed state (not UI timer). Clock loss resets all. While STOP action must not trigger transport. Stored base bytes remain identical for temporary actions, including locked OFF, native P-LOCK and timing. CHOKE and mute may suppress output but not data. FILL only in end zone, at most one active window.
- [ ] Implement priority as explicit pipeline (selected at boundary): choose base or BREAK over BLAST; apply DOUBLE only to eligible kick cells not forbidden by BREAK; FILL only within end zone; copy protected cells; CHOKE last on emission and native declick of active cymbal voices. Use existing native fill flags where they meet semantics, not global `fill_on` leaking into unrelated saved conditions. Prepare bounded overlay descriptors outside callback; if actual current step needs overlay, resolver selects prevalidated event/cell at native step-read point, no full-pattern generation in `events_block`. Ensure early micro and ratchet paths use same effective cell semantics and cancel future overlay ratchets at release boundaries; cannot simply edit `step_fire` and miss earlier pre-scheduled events. For CHOKE use native safe cut/fade on role H/R/C/X only; suppress new cymbal hits while held, not kick/snare/toms. Implement control heartbeat and release through real key state; context exit cancels before dispatching another page's key. Native song/slot change resets overlay. Priorities explicit in tests (BREAK+BLAST yields BREAK; CHOKE+locked cymbal suppresses sound yet cell unchanged; MUTATE+overlay changes only base). Avoid second sequencer, reuse native timing and cue mechanisms; if exact boundaries cannot be implemented safely, block integration until adapter audit resolved. Live action must never trigger same physical note/step edit. `LIVE` six black keys as audited in T03; label and active/pending states visible. Validate `gm_live_state` reset in boot, transport stop and simulator reset.
- [ ] `sh tests/run_grind_tests.sh performance`, `... cue`, upstream perform/seq/micro/guard regression and WASM UI (`bash tools/build_sim.sh --preview`). Native target measure worst-case overlay resolver bounded work; CHOKE start <= one audio block; require real device check T22. Commit `feat(live): add quantized non-destructive rhythm overlays`.

### Task T16: Mutation, undo, acknowledgement and state conflicts

**Files:** modify `firmware/src/grind/controller.h`, `controller.c`, `performance.c`, `runtime.c`, `ui.c`, `seq_adapter.c`; create `tests/grind/test_undo.c`.

- [ ] Red: start base A, hold BLAST -> mutate request -> current A until boundary, commit B only when valid, overlay unchanged; duplicate mutate before boundary coalesces; next undo restores A including config, mutation seed, timing, locks and every native step field at same boundary; repeat undo -> unavailable (one-level); manual edit after B invalidates undo; STOP with pending mutation -> no late change; no change result -> `NO CHANGE`, active seed, mutation and previous undo unchanged. Test generated candidate fails budget/lock validation -> preserve active pattern and dirty state. Test two identical mutations from same starting seed/counter produce identical B; mutation wrap safe (use uint32_t modular arithmetic). Test project/variation load clears or replaces undo consistently.
- [ ] Define new seed candidate `gm_mix32(seed ^ (mutation+1u) ^ 0xa511e9b3u)` and next counter increment, both only committed with changed pattern. Compare canonical cells/timing/locks for no-change ignoring candidate seed/counter; compare native bank state too where relevant. Store exactly one preceding committed snapshot+config, reuse audited bounded stage/undo buffers only with proven lifetime; no large IRQ copy. If no room for undo, resource gate blocks required FR-16 until memory-safe design (not omit feature). Explicit regen updates undo only on successful change; morph-only moves need not replace mutation undo, but ensure spec's manual-edit invalidation. Dirty computed from committed project baseline; undo returning exact saved state clears dirty appropriately. UI `UNDO` enable/disabled reason and `NO CHANGE`, pending status. Never save on mutation/undo. Use controller ack to publish revision, config and undo atomically from main loop; on failed ack free staged buffers without changing base.
- [ ] `sh tests/run_grind_tests.sh undo`, controller, performance, golden, all; `node tests/grind/test_sim.mjs` after WASM rebuild. Commit `feat(live): make mutation reproducible and undo revision-safe`.

## 10. P08 / v0.5 PERSISTENCE - storage format and transaction safety

### Task T17: Design and test versioned Grind metadata within existing storage

**Files:** create `firmware/src/grind/meta_codec.h`, `meta_codec.c`, `tests/grind/test_meta.c`, `tests/grind/test_varpack.c`; modify `firmware/src/varpack.c`, `project.c` only after T03 audit, `docs/grind/storage-layout.md`.

- [ ] Do not enlarge `project_t` blindly: existing FDRG layout and A/B `ST_PAYLOAD_MAX=3840` are tight. Proposed candidate (not confirmed free space): store versioned per-variation metadata **inside the existing variation-bank payload**, including variation 1 (which keeps base step cells in the project record); use new bank magic `GVR2` and explicit project format discriminator `GMD1` with same record size if the audit confirms loader and all consumers accept it. Preserve sector layout, `VAR_BYTES<=3584`, tail erased, existing project->variation+P-LOCK links. New metadata participates in bank content hash (`var_link_of` currently hashes `8+used`); require bank even when only variation 1 exists. Never use `step_t.rsv` (it aliases micro) or unproven reserved bytes, no new raw flash address. If existing sector/link mechanism cannot safely express this format, stop P08 and obtain explicit storage redesign approval. Pure metadata codec can still proceed.
- [ ] Fixed proposed `GM_META_V1_BYTES=88` record, explicit little endian bytes: magic `G M` (2), codec version u8 (1), algorithm u8 (1), engine version u16 (2), seed u32 (4), mutation u32 (4), four knobs (4), meter num/den + division + bars (4), BPM u16 (2), 64-byte lock mask. Sum 2+1+1+2+4+4+4+4+2+64=88; use **88 bytes** as single named constant and exact offsets: magic 0..1, version 2, algorithm 3, engine 4..5, seed 6..9, mutation 10..13, knobs 14..17, meter 18..21, BPM 22..23, locks 24..87. Track timing and steps remain native bank/project fields (project-wide globals authoritative for shared tempo; stored variation BPM is generation provenance, not independent tempo override). Reject metadata that conflicts with native timing or locks. Kit choice tracked project-wide via audited native preset references and explicit edition metadata where required; include stable kit ID as project-level byte in bank header extension with schema-defined offset, not hidden padding. `meta_decode` validates size/version/ranges/lock extents and engine compatibility; older stored pattern playable only if known codec and native fields validated, unsupported engine prohibits regeneration. No native `memcpy` of C structs into flash.
- [ ] Candidate packed GVR2 payload: native 8-byte bank header (`magic,slot,reserved,used` with new magic and reserved must zero), d starts with one byte codec version, one byte kit ID, then length-prefixed entry for variation 1 (metadata only, native steps in project record), followed by entries 2..8. Each u16 little-endian length=0 means unused (allowed only 2..8); used entry: 88-byte metadata then native sparse step/timing data for 2..8 (as in verified VAR1 encoding). Use checked remaining-length subtraction before reading. Whole bank `8+used<=3584`; `used<=3576`. Worst metadata cost 704 bytes for 8 variations plus length prefixes (16) and 2 bytes bank prefix; available sparse payload reduced accordingly. Example worst dense variation 2 entry: 96 timing/map + 512*4=2144 bytes, plus 88 metadata + 2 prefix =2234, so do not promise 8 dense variations. Preflight all entries including outgoing variation and project-linked P-LOCK bank before writes; user sees actual available capacity and `NO SPACE`. Slot A-D remain. No silent deletion. Test exact-fit/one byte over, hostile lengths/used, truncated data, unknown magic, locked OFF encoded despite sparse cell being zero, v1 with no other variants, seven small variations, high density. `var_need/var_put/var_get/var_valid/var_find/var_mask` updated consistently; `var_link_of` covers all metadata and steps; old format not silently treated as Grind. Add explicit conversion only optional T20. Protect existing `VAR_XIP`, user sample clamp guards and loader tail.
- [ ] Use independent byte fixture (known config/lock bits at positions 0 and 511) and round-trip, capacity and fuzz tests. `sh tests/run_grind_tests.sh meta`, `... varpack` red then green, `... all` sanitizer, native `tests/var_test.c`/`tests/slot_test.c` via `sh tests/run_drum_tests.sh`. Check record sizes with static asserts and measured linker map, preserve original loader/data map. Commit `feat(storage): encode versioned Grind metadata within linked variation budget`.

### Task T18: Save/load, dirty, A/B transaction and power-cut recovery

**Files:** create `firmware/src/grind/persistence_adapter.c`, `tests/grind/test_storage.c`, `tests/grind/test_powercut.c`; modify `firmware/src/project.c`, `varpack.c`, `storage.c` only if audited necessary, `firmware/src/grind/controller.c`, `ui.c`, `firmware/src/editor_backup.c`; read `tests/flash_host.h`, `tests/storage_test.c`, `tests/slot_test.c`, `tests/editor_backup_test.c`.

- [ ] Red fixtures: save slots A-D, v1..capacity limit; load validates metadata, steps, timing, seed, G-LOCK, kit, native project and lock bank; after restart exact bytes. Save only STOP, reject while transport request START pending (native `transport_busy()`), dirty stays until verify success. Pending/live/undo excluded from serialized state. Corrupt magic/version/CRC/length/link, invalid kit ID, invalid lock extent, unknown engine and incompatible legacy records -> retain current working state or safe stopped factory fallback with error and no flash rewrite. Simulate `st_read/st_erase/st_prog` failures at every operation/page boundary and truncation, including successful A -> attempted B -> attempted C without recovery, ensuring prior committed linked triple cannot be erased by selecting latest orphan bank. Test 100 save/load cycles with reproducible failure schedule; verify both A/B sequence wrap and failed verification. Do not simulate by destructive real-device power cut. Hostmodel before any flash test.
- [ ] Reuse native `storage.c` A/B sector header-last CRC and project linked lock+variation bank commit (`project_save_go`, `var_write`, `proj_fetch`, `proj_var_load`, `proj_var_link`, `st_save`, `st_load`). Preflight complete metadata/steps/locks, storage capacity and write permission before erasing anything. Stage new bank copies in *non-current linked* sectors, read back and validate CRC/link, then commit project record last. Confirm selected bank matches committed project, not simply newest independent object; retain last fully linked valid record and its banks on subsequent failed saves. Audit native code for this property; if existing transaction insufficient, implement bounded fix using current map and add regression, or stop hardware release. Do not mark SAVE success until reading the committed linked set and canonical comparison succeeds; preserve prior confirmed record on fail. No flash write while playing (unless separately approved upstream path proven safe); no erase from parsing/load/preview or import failure. `GM_NO_SPACE` and `GM_IO` distinct; display dirty remains; explicit confirm before overwrite and discard. Native A/B storage must continue to honor unknown JEDEC RAM-only state; Grind installer rejects unknown device. Project/version IDs and old-format handling documented, no silent compatibility promise. Hook native load/cue/save/restore paths to controller revision and metadata; no stale active stage or lost undo across load. Browser mock/host storage and simulator use same codec and transaction tests.
- [ ] `sh tests/run_grind_tests.sh storage`, `... powercut`, `... varpack`, upstream storage/var/slot/editor_backup suites and full host; WASM Save/Load/reload test. Verify record sizes/tail, no HAL/loader edits (`git diff <base> -- firmware/hal firmware/loader` reviewed), budget. Commit `feat(storage): persist Grind projects with verified linked commits`. P08 exit AT-12/13/21 host; no actual device flash.

## 11. P09 / v0.5 INTEGRATION - MIDI, clock, editor and regression

### Task T19: Clock and USB audio regression, explicit clock-loss handling

**Files:** create `tests/grind/test_sync.c`, `docs/grind/midi.md`; modify `firmware/src/seq.c`, `midi_clock.c`, `firmware/src/grind/runtime.c`, `controller.c`, `performance.c`, `ui.c` where needed. Read `midi_out.c`, `usb_app8.c`, `tests/midi_clock_test.c`, `tests/sync_test.c`, `tests/usb_audio_test.c`, `tests/usbmode_test.c`, `tests/usb8_test.c`.

- [ ] Red integration traces: internal clock ignores incoming timing, external USB/TRS clock follows 24 pulses/quarter, START/STOP and reconnect, jittered pulses, pending mutation at clock loss, no unintended restart until valid START or deliberate source change, no stale overlays. Test mode change transport reset. Derive timeout `max(500ms, 3*expected_interval_ms)` using native clock estimate (`pulse_samples` and FS after audit, overflow-safe). When no valid interval available use native 500ms. Preserve verified USB-audio reconnect exception for legitimate controlled reconnect; don't indefinitely suspend lost-clock safety. Native event block already detects 500ms; extend that path to set visible `CLOCK LOST`, invalidate tempo, cancel stage and live, stop exactly once. No automatic source switch. Test below/above deadline with wrap-safe monotonic subtraction, including no pulse after Start. External and internal clock timing tests at low/mid/high validated tempos. Native cue must use native `seq_n` clock advance (not wall-clock BPM).
- [ ] Confirm existing USB stereo 44.1kHz path and actual upstream 10-channel mode, MIDI input/clock out, existing device IDs/protocol. Use existing USB simulator/host fixtures; no new USB descriptor unless separate review. USB disconnect with internal clock does not halt audio; external clock loss as above. Optional Note-Out FR-10 SOLL only if resource gate permits: map fixed roles to GM notes (36,38,42,51,49,52,50,45), default channel 10, explicit configuration. Implement bounded per-note active state and deterministic Note-Off on retrigger/STOP, no Echo/Thru feedback, preserve input. Test AT-23 with rapid repeated notes, STOP, USB disconnect and THRU. If not implemented document `AT-23 NOT_APPLICABLE (Note-Out not shipped)` and do not advertise it. This plan does not require new Note-Out for v1.0.
- [ ] `sh tests/run_grind_tests.sh sync`, `... performance`, `... cue`, upstream full drum test and USB/MIDI/sync regression, native `DRUM_PACKAGE=1 ./build.sh` and full `sh tests/run_tests.sh` if available. Physical TRS/USB recording checks reserved T22. Commit `fix(integration): preserve native clock and USB behavior under Grind load`.

### Task T20: Backup/export/editor compatibility and integrated acceptance run

**Files:** create `tests/grind/test_backup.c`, `tests/grind/test_export.mjs`, `docs/grind/editor-compatibility.md`, `docs/grind/acceptance.md`; modify `firmware/src/editor_backup.c`, `editor_pattern.c`, `editor_proto.h`, `firmware/src/grind/persistence_adapter.c`, `web/EDITOR_PROTOCOL.md`, `web/editor/session.mjs`, `web/editor/tab_projects.mjs`, `web/editor/mock_device.mjs` only where required. Inspect legacy reader and write paths including raw backup/restore, preset and step writes.

- [ ] Mandatory negative tests: old editor/backup writes must not strip G-LOCK, config, new magic or linked metadata. A version mismatch refuses unsafe write before any state/flash modification. Read-only operations where compatible remain available; explicit capability negotiation, not product identity spoofing. Restore payload validation before applying, project file may not encode raw flash address/write command. New versioned export includes format version, project data, meta codec, asset/kit IDs, length+SHA-256 or CRC appropriate to local file format; import checks lengths, version, every ID, timing/lock bounds, checksum; transactional apply only after confirmation to explicit slot in STOP. If existing backup supports complete linked objects, preserve exact-byte export/restore and validate through actual host fixture; if unsupported, safely disable incompatible write and provide documented backup/export path (required to preserve user data). Optional compatible Hortator import: read known VAR1/FDRG with separate decoder, map roles transparently, set seed/defaults and locks only by explicit user decision; never overwrite old record in place. Felucca formats not imported by assumption. If optional import not done record limitation and safe reject. No implicit downgrade migration.
- [ ] Red tests for truncated/wrong-version/wrong-checksum/wrong slot, incompatible editor write, old/new backup roundtrip, unknown asset, copy/restore while PLAY, canceled confirm. Existing `tests/editor_backup_test.c`, `tests/editor_pattern_test.c`, `tests/editor_sample_test.c`, `web/editor/test_editor.mjs`, `web/editor/test_tab_projects.mjs` (run actual upstream commands from `tests/run_tests.sh`). Keep native protocol version and explicit new capability aligned JS/C tests. If entire editor feature is deferred, provide safe version-gated backup path, clear UI message and documentation. Test native user workflow U-01..U-08 in simulator: Bomb -> morph -> lock OFF -> LIVE BREAK -> mutation/undo -> save -> reload. Capture versioned golden hashes and audio event trace. Run `sh tests/run_grind_tests.sh all`, `sh tests/run_drum_tests.sh`, `bash tools/build_sim.sh --preview`, `node tests/grind/test_sim.mjs`, `node tests/grind/test_export.mjs`, plus full native suite when possible. Produce machine-readable acceptance evidence (T23 schema) for host/WASM only; hardware rows remain BLOCKED. Commit `feat(integration): guard backup protocol and exercise end-to-end workflow`.

## 12. P10 / v0.9 HARDWARE BETA - separate device authorization gates

### Task T21: GATE-HW-0/HW-1 - read-only inventory, recovery and package preflight

**Files:** create `docs/grind/compatibility.md`, `docs/grind/recovery.md`, `tests/grind/test_installer.py`, `tools/grind/package_check.py`; read/modify only necessary validation paths in `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `web/fm1pkg.js`, `web/fm1ota.js`, `web/index_pkg.html`; read `docs/RECOVERY.md`, `tools/check_loader.py`, `tools/check_update_path.py`, `tools/check_erase_order.py`, existing `tests/install_test.py`, `tests/ota_test.c`, `tests/fm1_uboot_test.py`. Do not change loader just to customize branding.

- [ ] Read pinned docs and actual command help on the controlled machine. Inventory device **read-only**: FM-1 hardware revision/board, present Felucca version, JEDEC ID, connection method/OS, package/loader identity, backup availability. Source recognizes P25Q80 0x856014 and UC25HQ80 0xB36014; neither is GRIND-qualified merely because macro allows it. Unknown ID => installer blocked. Record user device locally, no serial/backup contents in public repo. Backup projects/presets/samples using version-appropriate Felucca tooling; verify exact file hashes and recovery readability, full flash readback only by documented read-only method and where supported. Preserve exact known-good firmware and matching installer/recovery instructions. Confirm required OS/hardware (upstream boot-ROM recovery may need Mac and extra transport hardware), power/data cable, no concurrent editor. Stop if readback or recovery prerequisites unavailable. **No device writes in T21.** User must approve a specific artifact/hash for T22, not just this plan. No raw write/erase or recovery addresses in plan guessed from source comments.
- [ ] Package validator red cases: unknown/unqualified hardware ID; wrong board/identity; missing hash or SHA-256 mismatch; malformed/truncated package; wrong loader/app/data range; altered protected tail; missing source/build SHA; unsupported downgrade/import state; no backup/recovery confirmation. Require manifest version, exact firmware SHA, compatible package identity, tested hardware allowlist (empty until controlled beta explicitly qualifies candidate for test only), toolchain/SDK hashes, map evidence, package hash, expected native app/loader ranges derived from actual verified scripts. `package_check.py` parses package through native existing format validation; rejects any mismatch before invoking installer. Keep default web preview package-free. CLI/browser normal path enforces same checks and explicit user consent, no auto-flash. Mock device records write calls: assert zero on every red case and on cancelled confirmation. Verify existing updater checks retained, no `--force` bypass. Native package path resolved from actual build artifact, not fabricated name. Execute:

```bash
FELUCCA_BLE=0 DRUM_PACKAGE=1 ./build.sh
sh tests/run_tests.sh
python3 tests/grind/test_installer.py
```

Run native upstream guard/loader/erase-order/target budget checks with audited CLI args, include actual checksums. Two clean build directories, compare artifact bytes or explain normalized differences. `bash tools/build_sim.sh --preview` must not include installable native payload. If native build cannot run, T21 gate remains BLOCKED. Write explicit candidate evidence package and review checklist; no public firmware release and no real flash yet. Commit `test(release): require explicit device and package safety gates`.

### Task T22: Controlled hardware verification and recovery qualification

**Files:** `docs/grind/compatibility.md`, `docs/grind/recovery.md`, `docs/grind/acceptance.md`, `tests/grind/fixtures/hardware-run-schema.json`; add narrowly scoped instrumentation in `firmware/src/bench.c`, `console.c`, `audio.c`, `grind/runtime.c` only if needed. Existing `tools/fm1_bench.py`, `tools/fm1_serial_log.py`, `tools/stack_depth.py`, `tests/target_budget.py` reviewed before use.

- [ ] GATE-HW-0 pass: exact device revision + Flash-ID, verified backup, recovery environment and known-good firmware. GATE-HW-1 pass: specific package SHA-256, map/loader/format checks, native/host/WASM tests, mock installer safety, consent to that exact package from person operating the device. Record signed-off checklist with timestamp and operator (non-sensitive ID). If any condition fails do not flash. Install only via verified upstream-supported installer and exact documented workflow for that revision, supervised by operator. Do not provide automatic flashing from CI, do not retry blindly. At first error stop, preserve log, use approved recovery procedure only with operator consent. No experiments on user's only data-bearing device until backup and recovery proven. Recovery test may require separate sacrificial/test unit; if no safe test route, release remains blocked.
- [ ] Hardware smoke (AT-01/16/17/18/24): boot stopped, display readability, keys and encoders including hold/release, bank navigation, 8 tracks, sample/audio output, headphone/USB, STOP priority, no accidental write. Test guarded save in isolated designated slot, load/reboot; validate native core golden vectors via bounded diagnostic export (not release debug leak). Verify all supported meters/raster/tempo and concrete max BPM; start below target and increase only while observed stable, reduce validated range if needed. Check all 4 kits by ear and event trace, voice steal/retrigger, no surprise gain. `AT-03` compare canonical bytes/hash host/WASM/device for exact golden inputs; don't compare platform-specific PCM as pattern hash. Record actual values, operator hearing observations separate from objective metrics.
- [ ] NFR stress measurement: 30 min per qualified hardware/clock profile, eight tracks 64 steps dense, max stable tempo, allowed high-cost FX, UI updates, repeated knob movement, all LIVE actions, USB audio/MIDI connected. Measure audio half-buffer deadline `HALF_FRAMES/FS` seconds and actual maximum ISR duration; validate worst case <=80% deadline target, no underruns/resets/dropouts. Measure memory reserves from actual linker map and runtime stack/heap high-water; target >=10% each relevant available RAM/app budget, not chip totals. UI input-to-feedback <=100ms target; CHOKE fade begins <= one audio block. Log effective tempo, latency, clock jitter, voice steals and event counts. External clock 10 min normal and jittered separately; START/STOP, disconnect, reconnect, no auto-source switch; USB stereo master and 10-channel where retained, correct channels. Log mode, firmware SHA, board/flash type, clock, sample rate, duration, max block time, deadline, underruns, resets, memory use and limits, UI latency, event counts, notes. Raw evidence kept under controlled location; summary published without personal data. Test high load with native bench/debug counters, no instrumentation in release that changes critical timing unnoticed. Compare instrumentation overhead or measure on release-equivalent build.
- [ ] Persist 10 regular save/load/reboot cycles on device, exact canonical state and slot separation; hostmodel T18 handles injected power loss. Recovery qualification: restore known-good Felucca firmware and backed-up data using documented method on approved test device, confirm boot/keys/audio/presets, then if needed reinstall exact GRIND candidate with separate consent. No unsafe deliberate power cuts on user device. Record pass/fail and only qualify tested revision. A single successful board does not qualify all FM-1s. If failed, preserve old firmware and return defect to relevant PR, retest affected matrix (do not hide with reduced test). Resource mitigation order: optional layers, extra effects/editor, Note-Out, dense variation capacity; dropping FR requires new approval. Lower BPM validated range documented and user approval if materially below design target. Commit `docs(test): record measured FM-1 qualification and recovery evidence` after hardware run (not fabricated). P10 exit GATE-HW-0/1 and recorded results; GATE-HW-2 reviewed T23.

## 13. P11 / v1.0 RELEASE - reproducible evidence and handover

### Task T23: Traceability, release gates and audited build bundle

**Files:** create `tools/grind/release_check.py`, `tests/grind/test_release_check.py`, `docs/grind/release-checklist.md`, `docs/grind/acceptance.json` (evidence index, no invented pass), `docs/grind/acceptance.md`; modify `.github/workflows/grind.yml`, `tools/publish.sh`, `web/make_site.py`, `LICENSING.md`, `CHANGELOG.md`, `README.md` as needed. Review pinned `tools/version.py` and actual package outputs.

- [ ] Machine-readable evidence index entries keyed AT-01..AT-24 with `status` (`PASS`, `FAIL`, `BLOCKED`, `NOT_APPLICABLE`), required layers, commit, command/procedure, artifact/log path+hash, result summary, reviewer and date. NFR-01..10 separate entries with measured metric/unit/limit. `NOT_APPLICABLE` only AT-23 when Note-Out not shipped; other optional exceptions require explicit approval. Mandatory AT with device layer cannot pass with only host/WASM evidence. Gate HW-0/1/2 per revision, actual firmware SHA, package hash, backup/recovery signoff, legal assets and toolchain inventory. Candidate package allowlist for T22 distinct from public release qualification. Keep evidence immutable by build ID and commit, checks verify paths and hashes. Unit tests negative: missing AT, stale SHA, missing hardware, fake zero/no log, wrong hash, unreviewed license, unknown revision, mismatched WASM/native core version, skipped recovery, optional N/A improperly used, excessive measured resource values. Valid fixture uses **synthetic data only in test fixture**, never copied to real evidence.
- [ ] Minimal check structure (expand to require every declared layer and actual verified artifact/hash):

```python
required = {f'AT-{i:02}' for i in range(1, 25)}
entries = evidence['acceptance']
assert set(entries) == required
for key, row in entries.items():
    if key == 'AT-23' and not evidence['features']['note_out']:
        assert row['status'] == 'NOT_APPLICABLE'
        continue
    assert row['status'] == 'PASS' and row['commit'] == evidence['commit']
    assert row['evidence'] and row['reviewer']
assert all(evidence['gates'][k] == 'PASS' for k in ('HW-0','HW-1','HW-2'))
```

This fragment is not sufficient on its own; implement layer and artifact verification as above. A PASS field is not proof absent log and reviewer. Required gates fail closed on missing data. Run `python3 tests/grind/test_release_check.py` before implementation (red) then green, run checker on actual evidence (expected BLOCKED until hardware evidence exists). Build two clean reproducible outputs with exact pinned tools, compare normalized artifacts only where explicitly justified; include full corresponding source, GPL/third-party texts and asset inventory; SHA-256 manifest, actual native artifact, WASM preview with same core version, documentation and release notes. Validate no secrets/private backups in archive (`git status`, inspect artifact listing); verify archive hashes from separate working directory. Never ship non-cleared SDK or asset. CI release requires mandatory host/native/WASM jobs and hardware evidence/manual release approval; native installer payload absent from public preview until gate passes. Commit `ci: enforce measured release gates and reproducible distribution`.

### Task T24: Manuals, acceptance walkthrough and release handoff

**Files:** `README.md`, `CHANGELOG.md`, `docs/manual/grind-quickstart.md`, `docs/manual/grind-reference.md`, `docs/grind/recovery.md`, `docs/grind/compatibility.md`, `docs/grind/acceptance.md`, `docs/grind/release-checklist.md`; actual web manuals linked through existing `tools/build_manual.py` and simulator manual link, not an unlinked file.

- [ ] Document English on-device labels; German quickstart plus concise English reference. Include exact physical key table as tested, 22 styles, 4 controls, 8 roles, six live actions/priority, locks vs native P-LOCK, undo limits, project slots/actual capacity, MIDI/sync, clock loss, max qualified BPM/division, kit asset licenses, offline usage, native vs simulator limitations, compatibility table by tested hardware, backup/restore and safe update. No claims about continuous velocity, Bluetooth, note-out, legacy compatibility or all-device support if not proven. Demo U-01..U-09: fresh boot -> ALG-03 -> PLAY -> knobs -> protected OFF step -> LIVE BREAK -> MUTATE -> UNDO -> STOP -> SAVE -> restart -> restore; separate recovery walkthrough on controlled device. Test every button description against actual behavior; web Guide opens manual and works offline from built site. Update release notes with known limits, test evidence and upstream SHA. Test links and manual build (`python3 tools/build_manual.py` with audited args; actual upstream web tests from T02). `git diff --check`, full regressions, release checker on exact final commit. Docs-only changes after binary build require updated provenance or rebuilding exact final source; do not claim same SHA if source differs.
- [ ] Before public release: verify actual artifacts/manifest, release approval and GitHub fork/repo permissions. Create `v1.0.0` tag only for the qualified commit and publish release bundle only if T23 passed and user approves publication; attach source, checksums, licenses, native package, simulator link and measured compatibility. If gates fail, report exact blockers and publish at most package-free preview if approved, not a hardware release. No auto-merge, no installation. Commit `docs: publish Grind Machine manual and verified release handoff`. Follow finishing-a-development-branch workflow for merge choice. Handover actual PR URLs, commits, test outputs, known defects and artifact hashes. Final project done only under spec section 19 criteria.

## 14. Traceability matrix: all spec requirements

All mandatory acceptance items require their specified layers, not just existence of a planned test. The numbers below are task IDs, not completed results.

| FR | Implementation tasks | Acceptance |
|---|---|---|
| FR-01 own boot/navigation | T11-T12, T22, T24 | AT-01,24 |
| FR-02 eight tracks | T04,T06,T09,T11,T13-T14,T22 | AT-01,11 |
| FR-03 22 algorithms | T06-T08 | AT-02,03 |
| FR-04 morph | T07-T08,T10-T12,T14 | AT-04,08 |
| FR-05 manual + G-LOCK | T04,T07,T09-T12,T17-T18 | AT-05,06,22 |
| FR-06 deterministic | T05-T10,T12,T22 | AT-03,08 |
| FR-07 six live actions | T15-T16,T19,T22 | AT-09,10,24 |
| FR-08 four kits | T13-T14,T17,T22 | AT-11 |
| FR-09 projects | T17-T18,T20,T22 | AT-12,13,21 |
| FR-10 MIDI/USB audio | T19,T22 | AT-14,15; AT-23 conditional |
| FR-11 meters and grid | T04,T06-T09,T10,T22 | AT-06,07 |
| FR-12 simulator parity | T02,T09,T11-T12,T20,T23 | AT-03,16 |
| FR-13 recovery | T03,T17-T18,T20-T22 | AT-13,17,18 |
| FR-14 distribution | T01-T03,T13,T21,T23-T24 | AT-19 |
| FR-15 factory presets | T06,T08,T12 | AT-02 |
| FR-16 undo | T10,T16,T20 | AT-10 |

| NFR | Evidence tasks | Required evidence |
|---|---|---|
| 01 audio stability | T14,T19,T22 | 30-min measured stress, no unexpected dropout/reset/underrun |
| 02 realtime budget | T03,T09-T10,T15,T22 | measured max audio block <=80% deadline target |
| 03 memory | T03,T09-T10,T17-T18,T22 | map+high-water, >=10% relevant reserves target |
| 04 transport | T10,T15,T19,T22 | event counts, 10-min external sync, no duplicate/lost hits |
| 05 UI | T11-T12,T15,T22 | input feedback <=100ms target, STOP priority, readable hardware |
| 06 determinism | T05,T08,T12,T22 | canonical byte/hash parity host/WASM/device |
| 07 persistence | T17-T18,T22 | 100 host cycles, injected faults, 10 device cycles |
| 08 safety | T03,T17-T18,T20-T23 | layout audit, blocked invalid package, verified backup/recovery |
| 09 maintainability | T04-T10,T15-T18,T23 | bounded core, properties/sanitizers, regression checks |
| 10 offline/license | T03,T12-T14,T20,T23-T24 | offline run, inventory all distributed assets/code |

### 14.1 Acceptance runbook (AT-01 through AT-24)

| ID | Primary test and expected result | Layer / task |
|---|---|---|
| AT-01 | Fresh boot stopped, GRIND + PLAY, eight named roles/select correct. | WASM T11/T12; device T22 |
| AT-02 | 22 independent reference masks and valid default presets, anchors/rests as specified. | Host rules/golden T06/T08 |
| AT-03 | Fixed PRNG, 100 repetitions, canonical bytes/hash same on host/WASM/device. | Host T05/T08; WASM T12; device T22 |
| AT-04 | 0..100 density monotonic optional hits, aggression no ON change, bounded chaos, Humanize 0. Hear quantized accents. | Host morph T07; audio T14/T22 |
| AT-05 | ON/OFF lock preserves full cell through style/morph/mutate/live; separate P-LOCK. | Host T07/T15/T16; WASM T12/T20 |
| AT-06 | Six meters, three rasters, length/lock conflicts rejected without truncation. | Host grid/locks T04/T07 |
| AT-07 | Native cue 16/12/15 -> longest actual cycle; step 0 exact, mixed divisions. | Adapter cue T09/T10 |
| AT-08 | Latest-wins 1000 inputs, stale manual edit, no half commit/OOB. | Host controller T10; stress T22 |
| AT-09 | 6 actions + 15 pairs, release/stop/lost release, no double action. | Host T15; WASM T20; device T22 |
| AT-10 | Mutation -> undo exact, no undo after manual edit, no-change preserves seed. | Host T16; WASM T20 |
| AT-11 | Four legal kits x eight roles, peaks/voice/choke and correct kit swap. | Host/audio T13/T14; device T22 |
| AT-12 | A-D, variations to real cap, dirty/verified Save, no-space leaves old. | Flashmodel T17/T18; device T22 |
| AT-13 | Truncated/corrupt/unknown/legacy/fault-injection, no silent loss. | Host T17/T18/T20 |
| AT-14 | USB/TRS/clock/start/stop/loss, no auto-source switch. | Host T19; device T22 |
| AT-15 | USB stereo and retained multichannel under actual stress. | Device T22 (host baseline T19) |
| AT-16 | Actual shared WASM core plays offline, labels, keyboard/focus and vectors. | WASM T12/T20 |
| AT-17 | Invalid revision/package/hash refuses writes; verify on approved controlled target. | Installer mock T21; device T22 |
| AT-18 | Safe install, approved recovery, restored known-good Felucca/data. | Device T22 only |
| AT-19 | Clean pinned builds, hashes, licenses, source and manual. | CI/release T23/T24 |
| AT-20 | 30-min stress + measured timing, memory, UI, transport gates. | Device T22 |
| AT-21 | 100 host cycles with injected faults, 10 regular device cycles. | T18/T22 |
| AT-22 | 10k+ bounded adversarial cases incl invalid IDs, lock/steps/overflow, sanitizer. | Host T08/T17 |
| AT-23 | If Note-Out shipped, rapid on/off, STOP, no echo or hanging notes. | Optional host/device T19/T22 |
| AT-24 | Every physical key/hold/bank/recovery context with no double action. | Device T22 |

## 15. PR sequence, risk decisions and execution checklist

| PR | Suggested branch | Commit groups / required review focus |
|---|---|---|
| P01 | `foundation/pinned-baseline` | T01-T03, baseline, exact paths, memory/storage/SDK/license audit |
| P02 | `feat/grind-core-contracts` | T04-T05, integer types, canonical byte format, reference vectors |
| P03 | `feat/grind-rhythm-engine` | T06-T08, style anchors, morph/lock invariants, property tests |
| P04 | `feat/grind-native-adapter` | T09-T10, native ABI, ISR ownership, sample boundary and budgets |
| P05 | `feat/grind-ui-simulator` | T11-T12, panel routing, lock tracking, shared WASM parity |
| P06 | `feat/grind-kits` | T13-T14, provenance, voices, headroom, native sound |
| P07 | `feat/grind-live-assault` | T15-T16, held actions, overlay priorities, undo, stale state |
| P08 | `feat/grind-persistence` | T17-T18, capacity, A/B linked commits, power-loss safety |
| P09 | `feat/grind-integration` | T19-T20, native sync, USB, editor and export trust boundary |
| P10 | `test/grind-hardware-beta` | T21-T22, exact package + device gates, actual measurement |
| P11 | `release/grind-v1` | T23-T24, traceability, licensing, release gate and manual |

Every PR body: goal, spec FR/AT IDs, base/pinned SHA and current head, files touched, explicit scope exclusions, red/green evidence, baseline regression output, native resource delta or BLOCKED, test artifact IDs, risks and release status. Review highest risk areas first: flash formats, IRQ ownership, device update and data loss; no unrelated refactor. Resolve comments against current code and current head. Merge only after required checks and user/reviewer decision. Do not auto-merge or create a public release without permission. Use actual PR number after creation; don't fabricate links. At hardware blockers continue only isolated host/sim tasks whose assumptions are not invalidated. Record decisions in docs with rationale and approval.

### Decisions refined in this implementation plan

- Proposed baseline exact SHA based on GitHub `main` lookup; actual fork/checkout/build remains T01/T02. User can select another verified pin before implementation, requiring re-audit.
- Pure core compact `gm_cell`, keyed integer mixer, canonical byte stream and explicit reference patterns are new plan-level choices; not pre-existing upstream interfaces. Golden values and styles receive code review and audible validation.
- Initial kit switch STOP-only (safe option allowed by design). Native accent 96/127, not advertised as continuous velocity. Optional sound smoothing, note-out and additional layers stay conditional.
- Proposed GVR2 storage extension reduces variable capacity rather than borrowing flash sectors. T03/T17 must confirm safe representation and transaction; otherwise new approved design. Native project format discriminator and editor compatibility need audit before changing code.
- Use current native 500ms clock path with explicit computed timeout and safe reset, preserving verified reconnect exception. No duplicate scheduler.
- No native Bluetooth/VIBE port, no dualboot, no new HAL/loader unless separate approved review. Public native release blocked until measured device compatibility and recovery. The user's existing Felucca remains untouched at planning stage.
- Plan is a detailed implementation blueprint, not a compilable firmware patch. Selected upstream integration anchors and exact paths were retrieved; full source checkout and executable native/adapter code must be validated in Foundation. For integration tasks, implementation steps intentionally specify exact behavioral tests, ownership and real anchor functions rather than fabricate final code against unavailable checkout. Any unverified native symbol or resource assumption is a gate, not permission to guess.

### Final preflight before execution

- [ ] Review this plan against the approved design and agree the proposed pin, codec and initial STOP-only kit switching.
- [ ] Choose execution mode: separate subagent tasks with review gates (recommended if available) or sequential in-session execution with `executing-plans`. Use worktree isolation for actual implementation; keep original spec commit/history.
- [ ] Start P01 only. Stop at unresolved native-toolchain, storage, compatibility or authorization blockers; simulator-only work explicitly marked.
- [ ] Maintain one acceptance evidence index; no hardware success claim without hardware result.
- [ ] Do not flash the M-VAVE FM-1 as part of plan creation or ordinary CI.

## 16. Source links

- Approved spec: `docs/superpowers/specs/2026-11-27-grind-machine-design.md`.
- Pinned upstream: <https://github.com/DeadActive/hortator/tree/44c9ad4aa57bebf63f412bd552e57143c30b4065>.
- Branch SHA evidence: <https://api.github.com/repos/DeadActive/hortator/branches/main> (retrieved on 2026-11-27); tree evidence: <https://api.github.com/repos/DeadActive/hortator/git/trees/ff6d99c5c5ec82411016f1de02d4e0b5519d563f?recursive=1>.
- Upstream build docs, licensing, recovery and simulator at pinned commit: `BUILDING.md`, `LICENSING.md`, `docs/RECOVERY.md`, `tools/build_sim.sh`. Verify docs paths in checkout.
- Felucca upstream and existing firmware reference: <https://github.com/hugelton/Felucca>.

**End of plan.** This document authorizes planning/implementation sequencing only; hardware actions and public release are separately gated.
