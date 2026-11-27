# GRIND MACHINE FM-1 Edition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a standalone, GPL-3.0-only extreme-metal drum firmware for the M-VAVE FM-1, with 22 generative styles, four morph controls, eight tracks, protected edits, six live actions, four kits, persistent projects and a matching WASM simulator.

**Architecture:** Extend pinned Hortator C firmware with the portable Grind Core, existing native scheduling, bounded publication, safe persistence and shared-source simulator. Use the applicable checks and packaging practices of other FM-1 firmwares, not an additional global certification regime.

**Tech Stack:** C (portable C99-compatible core; existing upstream C dialect for adapter), JieLi WL82/pi32v2, Python 3, POSIX shell, Node.js, Emscripten (upstream Docker image `emscripten/emsdk:6.0.11`), native Hortator host tests, optional ASan/UBSan, Git and GitHub Actions.

---
**Plan version:** 1.1 | **Date:** 2026-11-27 | **Design status:** product scope approved; quality policy revised at user direction.
**Design:** [2026-11-27-grind-machine-design.md](../specs/2026-11-27-grind-machine-design.md).
**Destination:** `DaFum/grind-machine` (existing Hortator fork).
**Execution status:** P01 and the stacked P02/P03 and community-policy PRs #2-#5 were merged into their respective target branches. The follow-up integration PR consolidates those commits and review corrections into main. Native baseline and portable Core evidence exist; verify the actual CI for the integrated commit. No physical device test has been established.

## 1. Evidence, scope and execution rules

### 1.1 Pinned source evidence

Hortator reference commit `44c9ad4aa57bebf63f412bd552e57143c30b4065` is available in the fork and native plain-build evidence is recorded in `docs/grind/baseline.md`. The missing historical frozen-base-11 object remains unavailable. Do not equate the available reference with the missing one. Actual build, compatibility and test results are per commit; line references below are from the available pin.

| Verified source | Implication |
|---|---|
| [core.h L147-163](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/core.h#L147-L163) | `step_t` is a stored 4-byte cell: `on`, `acc`, `slide`, `tie`, `rat` (hits minus 1), `cond`, `nt`, `micro`. Accent gives 96/127 nominal velocity. Do not invent continuous step velocity or use padding as a lock. |
| [felucca.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/felucca.c) and [tests/drum_host.h](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tests/drum_host.h) | Application and host use textual inclusion of C modules. Integrate new core exactly once per build path. `FELUCCA_BLE=0` is plain build; retain package identity `FM-1_900`. |
| [seq.c L750-820](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/seq.c#L750-L820), [seq.c L983-1058](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/seq.c#L983-L1058), [cue.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/cue.c) | `events_block` processes transport, MIDI, cue and track events; `cue_tick`, `cue_longest`, `cue_switch`, `seq_restart` implement the native switch; `seq_cut_ahead` suppresses early hits across a boundary. Never add a parallel timer. Clock loss currently uses 500 ms, with USB reconnect exception. |
| [project.c L1-55](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/project.c#L1-L55), [storage.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/storage.c), [varpack.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/firmware/src/varpack.c) | FDRG project record uses tight payload; A/B sectors (4096 B, 3840 B payload) with header-last CRC. Variation object capped at 3584 B to keep loader-scan tail erased. Project links lock and variation banks. `FL_JEDEC_OK` checks 0x856014 and 0xB36014 in source; this is NOT a hardware qualification. |
| [tests/sim_core.c](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tests/sim_core.c), [tools/build_sim.sh](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tools/build_sim.sh) | Simulator executes firmware ISR and UI frames; real C is compiled into WASM. Upstream supports `--host-only` and `--preview`; preview without package cannot flash. |
| [tests/run_drum_tests.sh](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tests/run_drum_tests.sh), [tests/run_tests.sh](https://github.com/DeadActive/hortator/blob/44c9ad4aa57bebf63f412bd552e57143c30b4065/tests/run_tests.sh) | Drum host suite generates headers without target toolchain; full suite explicitly requires `DRUM_PACKAGE=1 ./build.sh` first. Some host fixtures include `libproc.h`; verify actual supported OS/compiler, do not assume all Linux jobs work unmodified. |

The musical design values, four kits and product acceptance cases remain unchanged. Quality checks use the Community quality policy below. New symbol names are proposed contracts, not claims of existing APIs; verify anchors in the actual source.

### 1.2 Plan decomposition and gates

Each PR builds a usable intermediate or isolated tested library. Native development may proceed in branches with applicable tests and documented missing evidence. Enable affected runtime paths only after their relevant native regressions pass; no unrelated milestone is stopped by a historical tag or optional host counter.

| Workstream | PR | Deliverable and dependency |
|---|---|---|
| v0.1 Foundation | P01 (T01-T03) | Fork/pin, baseline, build/toolchain/resource/asset audit. No device writes. |
| v0.2 Grind Core | P02 (T04-T05), P03 (T06-T08), P04 (T09-T10) | Portable core, styles, morph/locks and native adapter. P04 uses measured source/build facts and fixes the demonstrated cue regression; no global P01 certification dependency. |
| v0.3 Grind UI | P05 (T11-T12) | Working native and simulated GRIND/STYLE/SEQ screens, controls, factory presets, offline simulator. |
| v0.4 Sound Design | P06 (T13-T14) | Four legal, measured kits; native sound paths. |
| v0.5 Live Assault | P07 (T15-T16), P08 (T17-T18), P09 (T19-T20) | Performance, undo, transactional projects, sync/editor/export regression. Storage architecture depends on P01 audit. |
| v0.9 Hardware Beta | P10 (T21-T22) | Safety gates, package qualification, hardware measurement and compatibility. |
| v1.0 Release | P11 (T23-T24) | Community package build, tests, provenance, licensing, real hardware results and manual. |

Parallelize portable, native adapter, UI and legal asset work in isolated branches as dependencies permit. Only one active integrator modifies shared scheduler/storage/input code at a time. No VIBE port, Bluetooth build or dualboot in v1.0. Do not distribute an unqualified firmware or restricted asset.

## Community quality policy (user-approved direction, revision 1.1)

Apply the practices actually found in the following reference projects, not an additional GRIND-specific certification regime. Product features, deterministic core contracts, regression fixtures and proven bug fixes remain unchanged. The current Foundation branch still runs the old gates until their implementation is changed and reviewed; this document does not declare a passing CI or authorize device writes.

| Practice | Reference implementation (inspected 2026-11-27) | GRIND rule |
|---|---|---|
| Native Linux build followed by host tests and package checks | [Dinghy release workflow](https://github.com/zvenson/dinghy/blob/main/.github/workflows/release.yml), [Dinghy tests](https://github.com/zvenson/dinghy/blob/main/tests/run_tests.sh), [SLOOP DX workflow](https://github.com/zvenson/dxsloop/blob/main/.github/workflows/build.yml) | Use the existing pinned JieLi build and applicable host tests. |
| CPU regressions | [SLOOP DX regress.c](https://github.com/zvenson/dxsloop/blob/main/tests/regress.c), [SLOOP DX suite](https://github.com/zvenson/dxsloop/blob/main/tests/run_tests.sh), [Felucca suite](https://github.com/hugelton/Felucca/blob/main/tests/run_tests.sh) | Use existing target disassembly budgets and functional audio tests. Host instruction counts are optional when unavailable; do not misreport them as measured. Do not transplant Apple budgets to other architectures. |
| Memory and protected areas | [Dinghy build.py](https://github.com/zvenson/dinghy/blob/main/tools/build.py), pinned Hortator tools/build.py, tests/target_budget.py and tools/check_loader.py | Enforce the actual native linker/app limits, inherited pool floor (8 KiB) and existing applicable target/loader checks. Keep existing stack analysis as diagnostics where usable; no new percentage headroom, deadline, latency, soak duration or cycle-count threshold. |
| Persistence and update | [Dinghy tests](https://github.com/zvenson/dinghy/blob/main/tests/run_tests.sh), [Felucca tests](https://github.com/hugelton/Felucca/blob/main/tests/run_tests.sh), [SLOOP DX tests](https://github.com/zvenson/dxsloop/blob/main/tests/run_tests.sh) | Use host flash/OTA/loader simulations, torn-write and functional regressions, test installation and return to known-good firmware on real supported hardware before distribution. |
| Packaging and provenance | [fm1-build v1](https://github.com/zvenson/fm1-build/blob/v1/.github/workflows/build.yml) | Use GitHub Actions package build, SHA-256 and build provenance attestation; release on a version tag. No mandatory separate custom release checker or two-build byte identity requirement. |
| Licensing | [Felucca licensing](https://github.com/hugelton/Felucca/blob/main/LICENSING.md), [Dinghy licensing](https://github.com/zvenson/dinghy/blob/main/LICENSING.md) | Preserve corresponding source, notices and actual rights; replace or exclude uncleared assets before distribution. The current Felucca CC0 gen_waves.py is a replacement candidate, not retrospective clearance of the old Hortator copy. |

**Reference source decision.** Retain the available, already built Hortator project pin `44c9ad4aa57bebf63f412bd552e57143c30b4065`. The missing `frozen-base-11` object (`33ef0f3f2e2461d9092fb2394896aa48352f2294`) remains historical evidence, not a universal dependency. Do not fabricate or move that tag. Check preserved loader bytes against the existing loader digest and compare protected HAL/loader source and the application boot-recovery path (using the existing main.c init/title exclusions) to the available pinned tree. If the exact historical object is available, prepare and verify its build and run H2; a missing build in that case is BLOCKED. Report H3 estimates separately. Do not claim H2/H3 equivalence to the missing object. Native tests and actual protection/compatibility checks still apply.

**Stage scope.** A required applicable test failure blocks the affected integration/release; unavailable optional instrumentation or an unavailable historical object is reported as SKIPPED/unavailable, not PASS. If a counter loss interrupts functional tests, rerun those tests without instrumentation; do not turn an incomplete suite into a successful skip. Source and release licensing issues constrain distribution and the affected assets, not unrelated local code development. Work on P04 can proceed on a development branch while the known cue/data-loss problems are fixed. Do not enable an affected path until its regression passes. Do not install or distribute a candidate with known unresolved safety/data-loss defects or unverified hardware compatibility. Existing tests and evidence remain useful; do not remove assertions to turn red tests green. No additional VIBE port, new hardware target or change of product scope.

**Reference limits.** The recorded plain build uses 88,312 / 98,304 bytes of app RAM: 9,992 bytes of raw space, not 161 bytes. The 161-byte figure came from the superseded project-specific 10% rule. The 2,144-byte pattern is an estimated payload, not a verified allocation including padding, other state or stack. Measure the actual link and existing pool/stack lifetimes; do not treat the arithmetic as permission to allocate. Preserve the 8 KiB pool floor already enforced by Hortator.


### 1.3 Test/commit protocol (applies to every task)

1. Read specified files and design requirement; add concrete failing test/fixture before code. A compile/link failure for a new API is acceptable for the first red only; then obtain behavioural red, not just missing symbol. Preserve original baseline results. For pure docs/audit tasks, negative evidence is a missing/inconsistent required field or explicit command failure rather than a fabricated failing software test.
2. Run exact command, retain stdout/stderr/exit status in `build/grind-evidence/<task>/` (ignored, attach selected logs to PR). Use `set -o pipefail` in Bash with `tee`. Record input commit and compiler versions. Do not bypass applicable failing tests; optional unavailable measurements are SKIPPED, never PASS.
3. Make minimal implementation. Run same test green, sanitizers when supported, relevant native regressions and shared-core simulator tests. For any target-related change, include target link/budget reports before marking native support.
4. Review diff for unrelated edits, `git diff --check`, commit only task paths (`git add -- <listed files>`); commit messages below. Open PR after milestone integration with test evidence. No force-push, auto-merge, upstream mutation, secret publication, or automatic device upload.

For new tests use `assert` with `NDEBUG` explicitly unset, or upstream's assertion macros, no test which merely compares two results of the same implementation for correctness. Expected pattern vectors are independent hand-authored fixtures. Golden changes require reason and review. In commands below `sh tests/run_grind_tests.sh <suite>` is introduced by T04; suites are extended in later tasks. `GRIND_NATIVE` is a future build switch only if needed to preserve baseline; do not assume existing flag. Required unavailable prerequisites are BLOCKED for the affected path; optional unavailable checks are SKIPPED, not PASS.

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
| `tools/grind/check_assets.py`, `tools/grind/audit.py`, `tools/grind/resource_report.py` (existing Foundation helpers) | Inventory and diagnostic evidence. Reuse existing tools; no new independent release certification program. |
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

**Files:** existing `build.sh`, `BUILDING.md`, `tools/get_toolchain.sh`, `tools/build.py`, `tools/build_sim.sh`, `tests/run_drum_tests.sh`, `tests/run_tests.sh`; create `docs/grind/baseline.md`, `.github/workflows/grind.yml`, `tools/grind/audit.py` (audit validator introduced T03). Use the already reproduced native baseline; record applicable tests for new changes.

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

Expected: applicable functional commands exit 0 and outputs appear as documented. Missing historical comparisons or optional host instruction counters are separately reported as unavailable. A failed actual build or applicable test is not a passing result. Retain logs and artifact hashes; package-free preview remains available.
- [ ] CI: keep existing Linux host/native and WASM checks. Follow SLOOP DX for optional host counters; keep native target budget, loader/erase-order, flash/OTA and applicable functional tests. For release, follow the community fm1-build workflow (T23). Do not require a separate performance runner or complete T03 certification for unrelated changes. See migration task T03a.
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

Record exact function/owner, caller context (main/IRQ), mutation surfaces (including recording, tools, copy/paste, MIDI, backup/restore, variation changes), native division enum, `P_SLEN`, swing, `cond` enum range, `seq_next`/early micro-hit path, stage/cue cancellation, key masks, boot paths. Capture map and static sizes (`sizeof(step_t)==4`, actual `sizeof(project_t)`, native buffers/stack), App vs RAM budget, flash sectors/record sizes/limits, loader-protected tail and existing user data. Read `tools/check_*` scripts before using with correct argument shape; retain output. Identify whether cue staging can reuse existing project buffers without hidden double allocations; validate allocation against actual native linker/app/pool limits and existing target checks, not an extra percentage reserve. Record concrete native IDs for models and preset parameters, assets/licenses, and actual device revision as unknown until read-only HW inventory. No reading/writing physical flash in T03.
- [ ] Keep the current T03 audit and native layout reports as factual diagnostics. Run `python3 tools/grind/audit.py docs/grind/audit-blocked.json` and `python3 -m unittest discover -s tests/grind -p 'test_*.py' -v`; adapt the previous percentage and global-completeness policy in T03a. Use actual build measurements, not a fabricated passing record. Call-site review is relevant where a feature changes buffer ownership, timing or storage.

Review the raw build evidence, budget and map manually. Distribution licensing: enumerate all shipped files, including generated samples/fonts/media and SDK attribution; if an asset cannot be cleared, exclude or replace before distribution. Inspect `LICENSING.md` for separate Felucca rights. Preserve all notices. Propose storage solution T17 based on actual map; if no safe fit, halt native persistence and request design decision, not a new address.
- [ ] Record baseline, measured constraints, known defects and unavailable evidence. Continue affected-feature fixes and independent development; hardware installation is a separate candidate decision.


### Task T03a: Align existing Foundation checks with FM-1 community practice

**Files:** modify `.github/workflows/grind.yml`, `tools/grind/run_ci.sh`, `tools/grind/audit.py`, `tools/grind/resource_report.py`, `tests/grind/test_audit.py`, `tests/grind/test_resource_report.py`, `tests/run_drum_tests.sh`, `tests/run_tests.sh` and documentation as needed. Inspect `tools/frozen_base.txt`, `tools/check_untouched.py`, `tools/compare_upstream.py`, `tools/stack_depth.py`, `tools/check_loader.py` and `tools/check_erase_order.py` first. Do not change firmware/loader, actual linker boundaries or license classifications in this policy migration.

- [ ] Capture current behavior: run `bash tools/grind/run_ci.sh host-core`, `bash tools/grind/run_ci.sh host-upstream`, and the native lane with existing documented vendor inputs. Record the missing historical object, optional host counter, and actual native build results. Do not mark these old checks green.
- [ ] Add boundary fixtures to existing audit/resource tests: RAM at the native 98,304-byte region limit and one byte beyond; flash at actual APP_SLOT and one byte beyond; pool at its inherited 8 KiB minimum free space and one byte beyond that permitted usage. The expected status is within-limit vs failure, without the former added reserve. Run `python3 -m unittest discover -s tests/grind -p 'test_*.py' -v`; the old reserve tests/logic must be updated together, not simply removed. In the existing audit loop retain `require(used <= limit, ...)` and remove only the separate 90% predicate; in resource_report use `capacity(flash, APP_SLOT, 0)`, `capacity(ram, RAM_LIMIT, 0)`, and retain the existing pool floor. Use native linker/target checks to catch any other protected limit. Preserve the actual 4/3840/3576 layout observations.
- [ ] Replace the mandatory historical source comparison dependency with an available-reference comparison. Verify source ancestor with `git merge-base --is-ancestor 44c9ad4aa57bebf63f412bd552e57143c30b4065 HEAD`; compare protected paths with `git diff --exit-code 44c9ad4aa57bebf63f412bd552e57143c30b4065 HEAD -- firmware/hal firmware/loader` while unchanged. Preserve existing loader hash and erase/OTA checks. If future changes legitimately touch these paths, review those specific changes and their native tests. Keep frozen-base-11 check runnable when the exact object exists; report its absence as unavailable, not proof of equivalence or a universal failure. Also compare main.c using the existing historical guard's init/title exclusions against the available pin, and reuse the negative fixture replacing fm1_enter_uboot() with fm1_reboot(). Verify an intentional protected-path diff and changed recovery entry fail the available-reference check and wrong loader bytes still fail the original loader check. Do not rewrite the historical pin.
- [ ] Use SLOOP DX behavior for unavailable host counters. Keep all functional tests active; allow the performance lane to report SKIPPED only after functional tests finish. If the counter disappears mid-run, rerun the suite without instruction measurements; report actual failures and missing required checks from that run. Remove only the optional performance/historical dependencies from the mandatory aggregate; a real applicable regression or native build failure must still fail. T03 reports remain diagnostics for the affected components, not an independent global release approval. Asset status blocks distribution of affected uncleared files, not unrelated development. Exercise negative fixtures for actual test failure and missing optional counter; inspect CI status and retain logs.
- [ ] Run the existing native build and test package, `sh tests/run_tests.sh`, `sh tests/run_drum_tests.sh`, `bash tools/build_sim.sh --preview` and existing Core tests in their corresponding branches. Record applicable results. Keep code fixes for known cue/storage defects separate with failing probes first (`tests/grind/t03_host_probe.c`), not hidden by policy migration. Update baseline/integration docs to explain the old 161-byte number and actual raw RAM/pool capacity. Commit `ci: align Foundation checks with FM-1 community practice`. Rebase or merge the reviewed Foundation update into stacked P02/P03 branches without changing their core behavior.


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

- [ ] Build fixture per all 22 default IDs plus seeds 0,1,42,UINT32_MAX; exercise 6 meters x 3 divisions x supported bars, boundary knobs 0/1/50/99/100 and lock combinations. Deterministic test PRNG (separate from product RNG) runs the existing bounded property cases including invalid IDs, invalid lengths, signed-to-unsigned bad inputs, corrupted flags, locked OFF, seed wrap, hostile record-derived values. For success assert output validated, source unchanged, each cell within bounds, repeated result byte-equal, no overflow. For failure assert no state publication. Capture failure seed/config in diagnostic. Use independent masks from T06 and explicit byte fixtures from T05, not merely self-goldens. Keep the existing P03 test vectors; their count is not a separate release criterion.
- [ ] Add `gm_factory_config(id, &cfg)` and `gm_factory_pattern(id, &out)` through same core, no flash and no user slot consumption. Test 22 distinct valid IDs, names and preset defaults, no invalid zero; initial Bomb Blast preset ALG-03. Verify repeated generation for the selected goldens using the existing suite. `sh tests/run_grind_tests.sh properties`, `... golden` then `GRIND_SAN_FLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all' sh tests/run_grind_tests.sh all`. Run with GCC and Clang where available; compare encoded fixtures. Expected 0 assertion failures, ASan/UBSan clean. CI pure job runs without native SDK. Commit `test(core): lock style vectors, presets and adversarial input properties`.

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

- [ ] Red tests: stage revision 1 -> request revision 2 before boundary -> only 2 publishes; stage revision 3 -> native manual edit increments base revision -> 3 rejected, edit remains; stage then STOP -> no late commit; simultaneous native cue and Grind request -> deterministic cancellation/ownership; 16/12/15 same division -> switch at longest 16 (not LCM 240), all restart Step 0 at exact sample offset; mixed divisions and swing choose longest actual native cycle; early micro hit crossing replacement blocked (`seq_cut_ahead` regression); use host IRQ injection when guard released. Count each scheduled hit (no lost/doubled event), compare outgoing/incoming pattern signatures at every simulated block. Test repeated knob updates and revision wrap (use equality and explicit invalidation rather than signed ordering across wrap). Assertions: `active_revision` stable until commit; `pending.base_revision == native_revision` immediately before apply; no active mutation on stage failure.
- [ ] Implement one bounded mailbox: main `submit` replaces pending intent, `poll` creates output into reserved stage, validates then publishes stage identity under upstream short IRQ guard. ISR consumes exactly once at native cue boundary and acknowledges; main reconciles config/dirty only for matching ack. Staging buffer ownership FREE -> WRITING -> READY -> READING -> RETIRED -> FREE, transitions under existing guard; never reuse READING buffer; cancel stale READY before publishing new stage. Native revision and owner token checked on both publish and consume. No busy wait, heap, generation, flash or UI in callback. Reuse native staging path and `cue_tick`/`cue_longest`/`cue_switch` boundary calculation with explicit Grind variant request (not fabricated existing API). Integrate `seq_cut_ahead` and native `seq_restart(x,...)` using same real sample offset. If `proj_cue_apply` path would write variation banks or copy too much in ISR, separate an in-RAM staged Grind commit using the same boundary arbitration, and document exact code path in integration map; measure it before enable. Commit must be all-or-none, preserve native P-LOCK and motion, kit state and unrelated tracks. STOP clears pending and overlay; stopped new validated command publishes synchronously with native guard. When a native editor/recording path touches a step, notify/invalidate controller before the mutation becomes visible and prior to next publication; avoid hidden second source of truth. UI shows requested versus committed config. On conflict cancel with visible status rather than auto-overwriting edits. Dirty acknowledgement tied to successful base change, not merely dial movement. Use existing guard style, not unchecked C11 atomics on pi32v2. Map budget gate: use measured stage allocation and single undo reuse later; if insufficient, stop to revise design, no unverified memory reuse.
- [ ] Run `sh tests/run_grind_tests.sh controller`, `... cue`, `... adapter`; upstream `sh tests/run_drum_tests.sh` includes cue, song, micro, sync, IRQ tests. `bash tools/build_sim.sh --host-only`; native budget checks when available. Inspect ISR call graph and max copy time, no loops over generator in IRQ. Commit `feat(controller): stage revision-safe Grind changes at native cue boundaries`. P04 exit: host AT-07/08 and native build/budget evidence or explicit simulator-only mode.

## 7. P05 / v0.3 GRIND UI - hardware-first and shared simulator

### Task T11: GRIND/STYLE screens, boot and input routing

**Files:** create `firmware/src/grind/ui.c`, `tests/grind/test_ui.c`; modify `firmware/src/felucca.c`, `pages.c`, `ui.c`, `ui_draw.c`, `ui_input.c`, `panel.c` only where T03 maps real display/input hooks. Modify `tests/sim_core.c`, `tests/sim_test.c`, `web/sim/layout.js`, `web/sim/controls.js` for matching labels, not rhythm logic.

- [ ] Red host UI fixtures: boot fresh -> GRIND, ALG-03 selected, transport stopped, no save/erase; select STYLE -> preview candidate -> cancel preserves base; confirm while playing -> pending only, native cue confirms revision; turn knob 1..4 -> exactly one of Aggression/Density/Chaos/Humanize changes; press HOME, ARP tap/hold, SEQ, EDIT, PLAY, REC, FX and GLO/SAVE -> single routed action, never steals recovery combination. Assert visible state and native transport; drive the real key bitmasks discovered in T03, not simulated mouse-only commands. Preserve `REC` and existing FX/MIDI semantics. Input ownership table in test fixture: GRIND owns morph knobs; STYLE owns selection/confirm; SEQ owns white keys; LIVE owns six black keys; GLOBAL owns system settings. Globally reserved transport and recovery always take precedence. Explicitly test short/long thresholds and release after page change. Test UI starts without keyboard focus and no audio until user PLAY.
- [ ] Implement GRIND default layout in native 240x240 coordinates: header y=0..25 (name/project/BPM/play/clock/dirty); style/status y=26..52; 8x16 grid x=44..234, y=56..164, visible playhead and G-LOCK with outline shape distinct from native P-LOCK; four compact controls y=166..217; footer y=218..239 for pending/error and navigation. Use existing gfx/text routines and font sizes verified on device. Color: near-black background, warm-white text, red kick/primary, amber snare, gray cymbal; never encode lock/play state by color alone. Selected role and bank displayed. If labels cannot fit, use T03-approved abbreviations and selected-track scroll layout. STATUS priority: safety error > pending > normal. No full-screen blocking delay; use native redraw budget. GRIND home and STYLE open via audited ARP/HOME path; long ARP enters LIVE page shell without action before P07. STYLE lists seven categories, IDs and meter/divisions; valid confirm dispatches controller only, error stays visible; cancel does not mutate config. Clamp encoder increments 0..100, acceleration in UI, no repeated flash writes. Existing track selector stays track selector. Project/kit/MIX/GLOBAL routes use native pages. Boot Factory Bomb only if no valid stored state and transport stopped; corrupted project shows warning, never writes defaults automatically.
- [ ] Run `sh tests/run_grind_tests.sh ui`, `sh tests/run_drum_tests.sh`, `bash tools/build_sim.sh --host-only`, then `bash tools/build_sim.sh --preview`; inspect rendered real simulator screen at 240x240 and zoomed browser view with mouse/keyboard. Confirm original `tests/ui_test.c`, `tests/redraw_test.c`, `tests/input_test.c`, `tests/boot_test.c` remain valid. Use native UI and redraw regressions during dense audio, without an added latency threshold. Commit `feat(ui): add hardware-first Grind and style views`.

### Task T12: Step editor, factory selection and WASM parity

**Files:** create `tests/grind/test_step_edit.c`, `tests/grind/test_sim.mjs`; modify `firmware/src/grind/ui.c`, `controller.c`, `runtime.c`, `firmware/src/ui_tools.c`, `ui_input.c`, `seq.c`, `tests/sim_core.c`, `tests/sim_glue.mjs`, `tools/build_sim.sh`, `web/sim/app.js`, `web/sim/index.html`, `web/sim/sim.css`, `web/sim/manual.js`; retain the upstream simulator loader/AudioWorklet.

- [ ] Red tests: all 16 white-key positions in banks 1..4 for selected track, 64th step maps to index 63; out-of-length key rejects visibly; toggling ON and OFF sets G-LOCK; explicit LOCK tool toggles lock without toggling hit; P-LOCK glyph/automation not changed; UNLOCK ALL confirmation cancel preserves all bits. Native step knob edits, copy/paste, record, editor operations must trigger revision updates and protect manual edits; if an operation cannot be mapped safely, disable with clear message in GRIND context rather than losing locks. Stale pending canceled on edit. Native mute does not change cell. Factory presets 22 accessible in STYLE without consuming A-D user slots; restore/load error safe. `gm_factory_config`/`gm_generate` from T08 only. Host test for focus, pointer cancel and keyup; browser JS invokes existing simulator panel inputs, never implements a second generator.
- [ ] Add diagnostic-only simulator API: write canonical encoded pattern bytes into bounded native buffer; expose size and pointer via WASM export for host comparison. Use existing `tests/sim_core.c` export convention and add symbols to `tools/build_sim.sh` exported function list. `gm_encode` output compare in `tests/grind/test_sim.mjs` against T08 independent vectors; never rely on a second JS generator. Use same core version in `version.json` and native build. Real WASM boot and test via upstream loader/test glue, then use `node tests/grind/test_sim.mjs` with built `build/sim/fm1sim.wasm`; fail if export missing or bytes differ. Preserve simulator local persistence and panel controls. Disable package install in preview; remove or clearly label generic upstream demo/installer links which could mislead a user into loading non-Grind data. Update landing labels without changing protocol identity. Test offline load from local built assets after initial file delivery, no remote runtime dependencies for core.
- [ ] `sh tests/run_grind_tests.sh step-edit`, `bash tools/build_sim.sh --preview`, `node tests/grind/test_sim.mjs`, native host suite. Red missing API/incorrect lock then green. Browser preview checks: 4 knob controls, style selector, bank switch, STOP, key context, no installer package. Validate desktop/mobile display manually and with reproducible browser tests (if browser tooling available). Commit `feat(ui): expose protected step editing and shared WASM parity`. P05 exit AT-01/04/05/16 in host/WASM; hardware still pending.

## 8. P06 / v0.4 SOUND DESIGN - four kits and safe audio

### Task T13: Factory kit registry, legal assets and native presets

**Files:** create `firmware/src/grind/kit_registry.h`, `kit_registry.c`, `assets/grind/kits.json`, `tests/grind/test_kits.c`; extend existing `assets/grind/manifest.json`, `tools/grind/check_assets.py`, `tests/grind/test_check_assets.py`; modify `tools/presets.json` and generated `firmware/src/preset_bank.h` via `tools/gen_presets.py` (verify real generator inputs), `tools/gen_samples.py`/`tools/build.py` only if necessary for cleared assets, `LICENSING.md`, `firmware/src/grind/ui.c`.

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
- [ ] `kits.json` schema: four stable IDs (1..4), name, eight entries with `role` 0..7, audited `model_id`, `preset_id`, bounded native parameters, `asset_ids` list, `gain_defaults`, optional `choke_group`. Test role set equals 0..7 for each kit; reject duplicate IDs, missing role, invalid model/parameter, unknown asset. Reuse the existing asset manifest schema (kind, status, path, origin, license, notices and SHA-256); add stable identifiers for kit references where needed without discarding tracked/generated/SDK provenance. Extend the existing `tools/grind/check_assets.py` and its tests to check kit references, duplicate/missing roles, invalid model/parameter IDs and the asset bytes. Keep its normalized path and notice checks and source/release distinction. Run `python3 tools/grind/check_assets.py --source-only` and the relevant existing asset tests; exit 2 for pending source rights is distribution evidence, not a failure of unrelated development. Run the existing release-mode command with the actual release manifest and SDK for distribution. Run `python3 tools/gen_presets.py --check` and relevant native preset/sample tests. Do not add a second asset validator or treat a license string alone as clearance.
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

- [ ] Red fixtures: save slots A-D, v1..capacity limit; load validates metadata, steps, timing, seed, G-LOCK, kit, native project and lock bank; after restart exact bytes. Save only STOP, reject while transport request START pending (native `transport_busy()`), dirty stays until verify success. Pending/live/undo excluded from serialized state. Corrupt magic/version/CRC/length/link, invalid kit ID, invalid lock extent, unknown engine and incompatible legacy records -> retain current working state or safe stopped factory fallback with error and no flash rewrite. Simulate `st_read/st_erase/st_prog` failures at every operation/page boundary and truncation, including successful A -> attempted B -> attempted C without recovery, ensuring prior committed linked triple cannot be erased by selecting latest orphan bank. Test save/load with reproducible failure schedules using the existing storage fixtures; verify both A/B sequence wrap and failed verification. Do not simulate by destructive real-device power cut. Hostmodel before any flash test.
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

- [ ] Use the documented supported device/installer path. Identify FM-1 revision, existing firmware and supported flash type where the existing tools report it. Back up user projects, verify that the backup is readable and restorable with the supported backup/restore tools or their existing validation/restore fixture before the update, and preserve the known-good firmware and recovery instructions. This does not require an actual flash restore or full-flash readback in preflight. Unknown or unsupported target must not be flashed. Do not require a new full-flash dump, external recovery laboratory or extra certification package. No device writes in T21; actual installation remains an operator decision.
- [ ] Reuse `tests/install_test.py`, OTA/loader/package parsing and protected-region checks. Verify wrong identity, unsupported board, corrupt/truncated package and canceled update are rejected before unsafe writes. Keep current firmware/loader protocol and backup/recovery guards. No new parallel installer or custom mandatory package validator solely for certification. Use actual package path and SHA-256. Execute:

```bash
FELUCCA_BLE=0 DRUM_PACKAGE=1 ./build.sh
sh tests/run_tests.sh
python3 tests/grind/test_installer.py
```

Run existing loader, erase-order, target budget and applicable functional checks with their documented arguments; preserve package checksum. Missing historical equivalence is reported separately. Use the actual candidate build; no second byte-identical build requirement. Preview is package-free. No physical installation in T21.

### Task T22: Controlled hardware verification and recovery qualification

**Files:** `docs/grind/compatibility.md`, `docs/grind/recovery.md`, `docs/grind/acceptance.md`, `tests/grind/fixtures/hardware-run-schema.json`; add narrowly scoped instrumentation in `firmware/src/bench.c`, `console.c`, `audio.c`, `grind/runtime.c` only if needed. Existing `tools/fm1_bench.py`, `tools/fm1_serial_log.py`, `tools/stack_depth.py`, `tests/target_budget.py` reviewed before use.

- [ ] Use a successfully built candidate and the applicable installer/loader/host tests. Confirm supported device, backup and documented return path; operator chooses the actual update. Install through the existing supported installer; stop on errors. Do not automate device writes from CI. This is the same hardware-testing approach used by the community, not a separate signed-off certification regime.
- [ ] Hardware function checks: boot/STOP/PLAY, keys/encoders, tracks, audio, USB/TRS MIDI, kit selection, meter/raster and stable supported tempo, test-slot save/load. Run relevant AT cases on the shared simulator/host and native device; portable goldens are host/WASM tests, not a new mandatory device export mechanism.
- [ ] Exercise the existing native audio, target budget, sync, UI and resource tests and a practical FM-1 playing session including dense patterns, FX, LIVE, knob changes and USB/MIDI. Record observed audio dropouts, resets, timing or functional regressions and actual build limits. Use existing bench measurements when available. No added minimum stress duration, percentage audio deadline, percent memory reserve, latency cutoff or fixed clock-test duration.
- [ ] Test regular save/load/reboot and documented return to known-good firmware; host torn-write fixtures cover interrupted saves. Fix observed regressions, including the known linked-lock-bank loss, before enabling affected saves. Record actual supported hardware and known limits. Do not require a fixed number of hardware cycles or destructive power-cut tests. Commit device results only after actual tests.

## 13. P11 / v1.0 RELEASE - reproducible evidence and handover

### Task T23: Traceability, release gates and audited build bundle

**Files:** reuse `.github/workflows/grind.yml`, `tools/grind/check_assets.py`, `tools/grind/resource_report.py`, `docs/grind/acceptance.md`, `docs/grind/release-checklist.md`, `LICENSING.md`, `CHANGELOG.md`, `README.md`; adapt the existing release workflow as needed. Review `tools/version.py`, actual package outputs and fm1-build v1.

- [ ] Record applicable test results, actual commit, package checksum, hardware tested, known limits, corresponding source and applicable license notices in the existing PR/release notes and acceptance documentation. Preserve existing evidence; do not build a separate mandatory 24-row machine-readable certification system. Product cases still guide relevant tests.
- [ ] Follow the shared community workflow for tagged releases. Review the published workflow at `zvenson/fm1-build/.github/workflows/build.yml@v1`; it builds using project commands, optionally tests, calculates hashes, attests packages and publishes on a tag. Reuse it only after confirming the actual release package path and existing Hortator version/identity options. If the existing build requires separate configuration, use the same pattern in our current workflow. Keep distribution restricted to cleared assets. No automatic publication from a PR.

```bash
FELUCCA_BLE=0 DRUM_PACKAGE=1 DRUM_SITE=0 ./build.sh
sh tests/run_tests.sh
bash tools/build_sim.sh --preview
# Run the existing asset check on the actual release candidate inventory.
python3 tools/grind/check_assets.py --manifest "$RELEASE_MANIFEST" --sdk-root "$AC79_SDK"
# Select the actual tagged package produced by the audited release build.
sha256sum "$RELEASE_PACKAGE"



```

The release workflow uses the actual tagged package, not an UNTESTED development artifact; verify source/identity/compatibility and include corresponding source and notices. Use the fm1-build attestation/release pattern. Existing functional tests must pass for shipped features; optional host measurement and unavailable historical comparison do not become extra release gates. No publication until the operator approves it and the advertised hardware is actually tested.

### Task T24: Manuals, acceptance walkthrough and release handoff

**Files:** `README.md`, `CHANGELOG.md`, `docs/manual/grind-quickstart.md`, `docs/manual/grind-reference.md`, `docs/grind/recovery.md`, `docs/grind/compatibility.md`, `docs/grind/acceptance.md`, `docs/grind/release-checklist.md`; actual web manuals linked through existing `tools/build_manual.py` and simulator manual link, not an unlinked file.

- [ ] Document English on-device labels; German quickstart plus concise English reference. Include exact physical key table as tested, 22 styles, 4 controls, 8 roles, six live actions/priority, locks vs native P-LOCK, undo limits, project slots/actual capacity, MIDI/sync, clock loss, max qualified BPM/division, kit asset licenses, offline usage, native vs simulator limitations, compatibility table by tested hardware, backup/restore and safe update. No claims about continuous velocity, Bluetooth, note-out, legacy compatibility or all-device support if not proven. Demo U-01..U-09: fresh boot -> ALG-03 -> PLAY -> knobs -> protected OFF step -> LIVE BREAK -> MUTATE -> UNDO -> STOP -> SAVE -> restart -> restore; separate recovery walkthrough on controlled device. Test every button description against actual behavior; web Guide opens manual and works offline from built site. Update release notes with known limits, test evidence and upstream SHA. Test links and manual build (`python3 tools/build_manual.py` with audited args; actual upstream web tests from T02). `git diff --check`, applicable regressions and community release checks on exact final commit. Docs-only changes after binary build require updated provenance or rebuilding exact final source; do not claim same SHA if source differs.
- [ ] Tag the qualified candidate and publish only after user approval, with package, corresponding source, SHA-256, provenance, license notices, hardware support, documentation and simulator link. Do not publish unqualified/restricted artifacts. No auto-merge or device installation; follow branch completion workflow and hand over actual PRs and test results.

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
| 01 audio stability | T14,T19,T22 | existing audio/soak regressions and device playing test; no unexpected dropout/reset/underrun |
| 02 realtime budget | T03,T09-T10,T15,T22 | existing target budget and audio checks, optional host instruction metrics |
| 03 memory | T03,T09-T10,T17-T18,T22 | actual native linker/app/flash/pool limits and existing resource checks |
| 04 transport | T10,T15,T19,T22 | existing sync checks, no duplicate/lost hits |
| 05 UI | T11-T12,T15,T22 | existing UI/input tests, STOP priority and usable hardware feedback |
| 06 determinism | T05,T08,T12,T22 | existing canonical golden parity host/WASM and native integration test |
| 07 persistence | T17-T18,T22 | existing host storage regressions, injected faults and device save/load |
| 08 safety | T03,T17-T18,T20-T23 | layout audit, blocked invalid package, verified backup/recovery |
| 09 maintainability | T04-T10,T15-T18,T23 | bounded core, properties/sanitizers, regression checks |
| 10 offline/license | T03,T12-T14,T20,T23-T24 | offline run, inventory all distributed assets/code |

### 14.1 Acceptance runbook (AT-01 through AT-24)

| ID | Primary test and expected result | Layer / task |
|---|---|---|
| AT-01 | Fresh boot stopped, GRIND + PLAY, eight named roles/select correct. | WASM T11/T12; device T22 |
| AT-02 | 22 independent reference masks and valid default presets, anchors/rests as specified. | Host rules/golden T06/T08 |
| AT-03 | Fixed PRNG, repeated canonical bytes/hash on host/WASM, native function checks. | Host T05/T08; WASM T12; device T22 |
| AT-04 | 0..100 density monotonic optional hits, aggression no ON change, bounded chaos, Humanize 0. Hear quantized accents. | Host morph T07; audio T14/T22 |
| AT-05 | ON/OFF lock preserves full cell through style/morph/mutate/live; separate P-LOCK. | Host T07/T15/T16; WASM T12/T20 |
| AT-06 | Six meters, three rasters, length/lock conflicts rejected without truncation. | Host grid/locks T04/T07 |
| AT-07 | Native cue 16/12/15 -> longest actual cycle; step 0 exact, mixed divisions. | Adapter cue T09/T10 |
| AT-08 | Latest-wins repeated inputs, stale manual edit, no half commit/OOB. | Host controller T10; device T22 |
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
| AT-20 | Existing target/audio/resource/transport/UI checks and real playing test. | Host/native T22 |
| AT-21 | Save/load with injected faults in host model, regular device save/load. | T18/T22 |
| AT-22 | Existing property cases incl invalid IDs, lock/steps/overflow, sanitizer. | Host T08/T17 |
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

Every PR body: goal, spec FR/AT IDs, base/pinned SHA and current head, files touched, explicit scope exclusions, red/green evidence, baseline regression output, native resource delta or BLOCKED, test artifact IDs, risks and release status. Review highest risk areas first: flash formats, IRQ ownership, device update and data loss; no unrelated refactor. Resolve comments against current code and current head. Merge only after required checks and user/reviewer decision. Do not auto-merge or create a public release without permission. Use actual PR number after creation; don't fabricate links. At device blockers continue independent development, including native branches with applicable tests. Record decisions in docs with rationale and approval.

### Decisions refined in this implementation plan

- Available reference is the already built Hortator commit `44c9ad4`. Keep the historical frozen pin as historical evidence, do not forge its missing object. Adapt checks in T03a.
- Pure core compact `gm_cell`, keyed integer mixer, canonical byte stream and explicit reference patterns are new plan-level choices; not pre-existing upstream interfaces. Golden values and styles receive code review and audible validation.
- Initial kit switch STOP-only (safe option allowed by design). Native accent 96/127, not advertised as continuous velocity. Optional sound smoothing, note-out and additional layers stay conditional.
- Proposed GVR2 storage extension reduces variable capacity rather than borrowing flash sectors. T03/T17 must confirm safe representation and transaction; otherwise new approved design. Native project format discriminator and editor compatibility need audit before changing code.
- Use current native 500ms clock path with explicit computed timeout and safe reset, preserving verified reconnect exception. No duplicate scheduler.
- No native Bluetooth/VIBE port, no dualboot, no new HAL/loader unless separate approved review. Public native release blocked until measured device compatibility and recovery. The user's existing Felucca remains untouched at planning stage.
- This is an implementation blueprint, not a firmware patch. Current native build and Core results are in the actual PRs. Verify source anchors and measure existing constraints for relevant changes, rather than requiring additional certification.

### Final preflight before execution

- [ ] Review the community-policy migration and applicable product contracts.
- [ ] Choose execution mode: separate subagent tasks with review gates (recommended if available) or sequential in-session execution with `executing-plans`. Use worktree isolation for actual implementation; keep original spec commit/history.
- [ ] Continue P01/P02/P03 reviews and P04 development on separate branches. T03a aligns existing CI with the revised policy; fix affected cue/storage regressions before enabling those paths.
- [ ] Keep actual test/build results and known hardware limits; never claim a hardware test that did not run.
- [ ] Do not flash the M-VAVE FM-1 as part of plan creation or ordinary CI.

## 16. Source links

- Approved spec: `docs/superpowers/specs/2026-11-27-grind-machine-design.md`.
- Pinned upstream: <https://github.com/DeadActive/hortator/tree/44c9ad4aa57bebf63f412bd552e57143c30b4065>.
- Branch SHA evidence: <https://api.github.com/repos/DeadActive/hortator/branches/main> (retrieved on 2026-11-27); tree evidence: <https://api.github.com/repos/DeadActive/hortator/git/trees/ff6d99c5c5ec82411016f1de02d4e0b5519d563f?recursive=1>.
- Upstream build docs, licensing, recovery and simulator at pinned commit: `BUILDING.md`, `LICENSING.md`, `docs/RECOVERY.md`, `tools/build_sim.sh`. Verify docs paths in checkout.
- Felucca upstream and existing firmware reference: <https://github.com/hugelton/Felucca>.

**End of plan.** This document authorizes planning/implementation sequencing only; hardware actions and public release are separately gated.

## 17. Selected community-inspired extensions (2026-11-27)

The user selected nine optional extensions from CoralFlower and FRASIL. See [GRIND MACHINE Community Feature Expansion](2026-11-27-grind-community-feature-expansion.md) for the exact scope, dependencies, implementation/test tasks, license and resource decisions. These follow or extend the relevant P05–P09 work without delaying the existing hardware alpha. T01–T24 and the user-approved community quality policy remain unchanged. Unselected features are excluded; no new global gates or resource thresholds.
