# P01 baseline evidence

The Linux x86-64 baseline produced a native test package and static resource measurements. The [pre-migration CI run](https://github.com/DaFum/grind-machine/actions/runs/45935159017) recorded blockers under the original policy. T03a applies the [community policy in plan v1.1](../superpowers/plans/2026-11-27-grind-machine-implementation.md) to new runs.

| Input | Verified identity |
|---|---|
| Hortator baseline | `44c9ad4aa57bebf63f412bd552e57143c30b4065`, the parent of the Foundation changes in `DaFum/grind-machine` |
| JieLi toolchain | Vendor archive `jieli-linux-toolchains-20260730.1.tar.xz`, SHA-256 `a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd`, pi32v2 Clang 4.0.1 |
| AC79 SDK | Tag `AC79NN_SDK_V1.2.1_2023-12-13`, commit `d179b4484759423312073f5fbb232501aa491047`. The three packaging inputs and Apache-2.0 license text match the recorded SHA-256 digests. |
| Emscripten | `6.0.11`, Linux amd64 manifest `sha256:fca9b3affd2e321ab97ffc657ccc53b9fb88715997712d11d562900e6655f26f` |
| Local baseline | Linux x86-64, Debian GCC 14.2.0, Python 3.12.14, Pillow 13.0.0, Node 24.22.3, Docker 29.9.2 |

## Reproduce Foundation checks

The Foundation gate requires passing Host Core, Host Upstream, WASM and web, and native jobs. Available historical comparisons are checked in the applicable jobs. Without the pinned object those comparisons report `SKIPPED`. Host instruction counts depend on a supported counter. An unavailable counter skips those measurements and a failed measurement still fails the gate. Source asset and T03 checks report their results for follow-up. An applicable functional regression or missing required evidence still fails the gate.

The CI license job checks versioned source assets. Distribution requires clearance or replacement of the affected assets and corresponding source and notices. Use `python3 tools/grind/check_assets.py --manifest /path/to/release-assets.json --sdk-root /path/to/ac79-sdk` to verify a detached release inventory against the candidate package and simulator outputs. Record output digests after the build so the simulator source archive does not contain its own digest.

```bash
bash tools/grind/run_ci.sh host-core
bash tools/grind/run_ci.sh host-upstream
# Install the browser driver. The WASM lane uses an installed Chromium or Chrome.
npm install --prefix build/grind-browser --no-save --ignore-scripts --no-audit --no-fund playwright-core@1.58.2
bash tools/grind/run_ci.sh wasm
bash tools/grind/run_ci.sh license
# macOS with an available instruction counter
bash tools/grind/run_ci.sh performance
```

For the native lane, use Linux x86-64 with Git history, C/C++ compilers, rsync, Node and Python 3.12 or newer. Create a project-local virtual environment with Pillow. The frozen build uses `.venv/bin/python`.

```bash
python3 -m venv .venv
.venv/bin/pip install Pillow==13.0.0
git fetch --no-tags https://github.com/DeadActive/hortator.git 'refs/tags/drum-v*:refs/tags/drum-v*'

vendor_dir=$(mktemp -d)
curl -fL -o "$vendor_dir/jieli-linux-toolchains-20260730.1.tar.xz" \
  https://jl-update.oss-cn-shenzhen.aliyuncs.com/jieli-linux-toolchains-20260730.1.tar.xz
git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
  https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git "$vendor_dir/ac79-sdk"

GRIND_JIELI_ARCHIVE="$vendor_dir/jieli-linux-toolchains-20260730.1.tar.xz" \
AC79_SDK="$vendor_dir/ac79-sdk" bash tools/grind/run_ci.sh native
```

The native lane verifies the archive and SDK before running the toolchain. It builds with `FELUCCA_BLE=0 DRUM_PACKAGE=1 DRUM_SITE=0` and runs the frozen build when its exact ref is available. The native suite consumes the test package locally. Logs, artifact hashes and the JSON resource report go under `build/grind-evidence/`. CI neither publishes packages or simulator output nor connects to devices.

The Foundation gate assembles `build/grind-evidence/T03/audit.json` from the current run's native layout probe, resource report and host/WASM results. It validates the record with `python3 tools/grind/audit.py --complete build/grind-evidence/T03/audit.json`. The native probe compiles the firmware translation unit with the app's flags and verifies the measured project record size against the linked ELF. CI retains allowlisted text logs, hashes and JSON reports even when a check is blocked.

The [call-site evidence](callsite-audit.json) records the reviewed `firmware_tree`, integration points and remaining `unknowns`. The validator's `CALLSITE_AREAS` lists the required coverage. A changed firmware tree requires refreshed evidence. Review adapter or persistence questions before enabling the affected paths.

## Native Linux measurements

An isolated checkout of PR commit `139af92` built the package with exit code 0. The resource and stack checks below come from the same build's binary, linker symbols and disassembly. The built ELF's `proj_slot` contains four 3,840-byte project records.

| Check | Used | Limit | Available | Status |
|---|---:|---:|---:|---|
| App flash | 411,360 B | 581,564 B | 170,204 B | PASS |
| Static app RAM | 88,312 B | 98,304 B | 9,992 B | PASS |
| Pool | 323,340 B | 344,064 B | 20,724 B | PASS |
| Main stack, static analysis | 2,880 B | 24,320 B | 21,440 B | PASS |
| Interrupt stack, static analysis | 1,544 B | 7,936 B | 6,392 B | PASS |
| Runtime stack high-water and audio deadline | | | | BLOCKED pending FM-1 measurements |

The earlier 161 B figure was the space available under the former 90% static app RAM cap. The measured native region has 9,992 B free. A 2,144 B pattern would leave an estimated 7,848 B before measuring the integrated core. The pool retains its inherited 8,192 B minimum free space, leaving 12,532 B above that floor in this build. Static stack estimates are diagnostic. Verify runtime use on the device before release.

The loader digest matches the upstream pin `cc98eed224299fa42ca22a546073e0f8b98d8362e26b5f325a9aaeab287bdf5d`. The local package was 609,649 B, SHA-256 `0d12d84e10bc9dd918d323aa92469c00c71653871f4eb898c7790681311364a1`. Static target-cost and erase-order checks passed.

## Open evidence

- **Frozen reference:** `frozen-base-11` is pinned to `33ef0f3f2e2461d9092fb2394896aa48352f2294`. The upstream change describes it as a local tag. The public refs and checked local Git objects lack the exact commit. The frozen source guard and H2/H3 run when the exact commit is available. The required source guard checks HAL and loader paths against `44c9ad4`. [Upstream pin change](https://github.com/DeadActive/hortator/commit/efe311f3f5ab011c335249b0dd8ae487de67d78f)
- **Licensing:** The [manifest](../../assets/grind/manifest.json) identifies the rights-reserved panel and generated drum sounds and records SDK evidence. Final firmware and simulator license evidence stays blocked pending clearance or replacement. The downloaded compiler archive contains usage instructions and no identified standalone license notice.
- **Hardware and integration:** Verify stack high-water and audio timing on the FM-1, confirm the board and recovery path, and finish the integration call-site review before freezing adapter interfaces or enabling native persistence. The optional exact-stock recovery fixture needs M-VAVE's V15 package. The current vendor download is V16.
