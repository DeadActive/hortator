# M1 baseline — unmodified Felucca (upstream 1e838e1) on this Mac

Date: 2026-10-05. Branch `drum-m1` at 781d5cc (docs only on top of upstream).

## Environment
- macOS 26.1 arm64; Docker Desktop, `linux/amd64` via Rosetta (`uname -m` in container: `x86_64`).
- Python venv `.venv` (Homebrew Python 3.13.1, Pillow 12.3.0). Homebrew 3.14's venv failed in ensurepip.
- JieLi toolchain: `~/.jieli/toolchain -> jieli-linux-toolchains-20250324.1` (tools/get_toolchain.sh).
- JieLi AC79 SDK `AC79NN_SDK_V1.2.1_2023-12-13` at `~/fw-AC79_AIoT_SDK`: gitee reset a full clone; a sparse
  blob-less clone of `cpu/wl82/tools` worked (the build uses only uboot.boot, cfg_tool.bin, cfg/eq_cfg_hw.bin).

## Stock build (`PYTHON=.venv/bin/python ./build.sh`)
    samples: 5 sets, 41 zones, 258554 B ADPCM
      ok    .ram_text: 909 insns, no calls
      ok    image 418600 B; RAM .data+.bss 43052 B of 98304; pool 260384 B of 344064
      ok    register access: hal/ only (src/, loader/ clean)
    app      /Users/evech/Desktop/dev/fm1-drummachine/felucca/build/felucca.bin  418600 B
    loader   /Users/evech/Desktop/dev/fm1-drummachine/felucca/build/loader/ota.bin  6493 B
    package  /Users/evech/Desktop/dev/fm1-drummachine/felucca/build/felucca.fwsc  609649 B, identity FM-1_900

## Stock host tests (`tests/run_tests.sh`)
    ALL HOST TESTS PASSED
(including the regression CPU check against tests/cpu_baseline.txt: this Mac's instruction counts match the
author's within the +25 % budget, so the reference below is valid here)

## Facts for later plans
- Cost reference for M1-A: `cpu/mix/3parts_full_drums 1566` host instructions/sample (tests/cpu_baseline.txt),
  the heaviest stock mix, known to run on the FM-1.
- C++ probe (`pi32v2/bin/clang -x c++`): **CXX_OK** — the JieLi clang compiles C++ (M1-C may port Plaits as C++).
- Float probe: `a*b+1.5f` at -Os compiles to a library **call** (soft-float) with the default flags — no FPU
  instructions are emitted. M1-C must find an FPU flag or treat Plaits' float code as slow on the FM-1.
