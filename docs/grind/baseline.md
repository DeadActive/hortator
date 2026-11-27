# P01 baseline evidence

**Foundation status: BLOCKED.** The pinned repository is available as a complete checkout. The Linux x86-64 baseline produced a native test package and static resource measurements.

| Input | Verified identity |
|---|---|
| Hortator baseline | `44c9ad4aa57bebf63f412bd552e57143c30b4065`, the parent of the Foundation changes in `DaFum/grind-machine` |
| JieLi toolchain | Vendor archive `jieli-linux-toolchains-20260730.1.tar.xz`, SHA-256 `a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd`, pi32v2 Clang 4.0.1 |
| AC79 SDK | Tag `AC79NN_SDK_V1.2.1_2023-12-13`, commit `d179b4484759423312073f5fbb232501aa491047`. The three packaging inputs and Apache-2.0 license text match the recorded SHA-256 digests. |
| Emscripten | `6.0.11`, Linux amd64 manifest `sha256:fca9b3affd2e321ab97ffc657ccc53b9fb88715997712d11d562900e6655f26f` |
| Local baseline | Linux x86-64, Debian GCC 14.2.0, Python 3.12.14, Pillow 13.0.0, Node 24.22.3, Docker 29.9.2 |

## Reproduce Foundation checks

GitHub Actions runs the host, performance, WASM, native and license jobs. The Foundation gate requires `PASS` from each lane. The checks report `FAIL` for failed tests and `BLOCKED` for missing required evidence. `SKIPPED` marks checks outside the lane's scope, including native package checks in the WASM job and Linux instruction measurements in the functional host job.

The CI license job checks versioned source assets. For a release candidate, run `python3 tools/grind/check_assets.py --manifest /path/to/release-assets.json --sdk-root /path/to/ac79-sdk` in the checkout holding the package and simulator outputs. Record output digests in the detached release manifest after the build so the simulator source archive does not contain its own digest. Release approval requires this check to pass on the candidate files.

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

The loader digest matches the upstream pin `cc98eed224299fa42ca22a546073e0f8b98d8362e26b5f325a9aaeab287bdf5d`. The local package was 609,649 B, SHA-256 `0d12d84e10bc9dd918d323aa92469c00c71653871f4eb898c7790681311364a1`. Static target-cost and erase-order checks passed.

## Open Foundation gates

- **Frozen reference:** `frozen-base-11` must resolve to `33ef0f3f2e2461d9092fb2394896aa48352f2294`. Neither the Fork nor Hortator advertises the tag. The Fork rejects fetching the commit by SHA, and the upstream API reports it unavailable. The frozen source guard and H2/H3 comparisons require the exact object and tag.
- **Licensing:** The [manifest](../../assets/grind/manifest.json) identifies the rights-reserved panel and generated drum sounds and records SDK evidence. Final firmware and simulator license evidence stays blocked pending clearance or replacement. The downloaded compiler archive contains usage instructions and no identified standalone license notice.
- **Hardware and integration:** Verify stack high-water and audio timing on the FM-1, confirm the board and recovery path, and finish the integration call-site review before freezing adapter interfaces or enabling native persistence.
