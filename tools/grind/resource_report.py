#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Report measured plain-build resource evidence as JSON.

Exit 0: requested gates passed, 1: a required check failed, 2: required evidence
is missing. Static analysis does not satisfy the on-device measurements.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import dis_parse  # noqa: E402
from fm1pkg_make import APP_SLOT  # noqa: E402
import stack_depth  # noqa: E402

RAM_LIMIT = 96 * 1024
POOL_LIMIT = 0x54000
POOL_FREE_MINIMUM = 8192
STACK_BLOBS = ("text.bin", "data.bin", "ramtext.bin")
MEMORY_LINE = re.compile(
    r"^\s*ok +image (\d+) B; RAM \.data\+\.bss (\d+) B of (\d+); "
    r"pool (\d+) B of (\d+)\s*$",
    re.MULTILINE,
)


def finding(status, detail, **measurements):
    return {"status": status, "detail": detail, **measurements}


def capacity(used, limit, required_free):
    free = None if used is None else limit - used
    if free is None:
        status, detail = "BLOCKED", "No verified measurement"
    elif free < required_free:
        status = "FAIL"
        detail = "Usage exceeds the applicable limit" if free < 0 else "Below the required reserve"
    else:
        status, detail = "PASS", "Within the applicable budget"
    return finding(status, detail, used_bytes=used, limit_bytes=limit,
                   free_bytes=free, min_free_bytes=required_free)


def memory_measurements(build_log, app_image):
    missing = [str(path) for path in (build_log, app_image) if not path.is_file()]
    if missing:
        return finding("BLOCKED", f"Missing plain build evidence: {', '.join(missing)}"), (None,) * 3
    try:
        log = build_log.read_text(encoding="utf-8")
        lines = MEMORY_LINE.findall(log)
        if len(lines) != 1 or "BLE RAM gate:" in log:
            raise ValueError("Expected one plain-build memory summary")
        image, ram, ram_limit, pool, pool_limit = map(int, lines[0])
        if image != app_image.stat().st_size or image <= 0:
            raise ValueError("Build summary does not match the app image length")
        if (ram_limit, pool_limit) != (RAM_LIMIT, POOL_LIMIT) or ram <= 0 or pool <= 0:
            raise ValueError("Build summary does not match the pinned plain-build layout")
    except (OSError, ValueError) as exc:
        return finding("FAIL", str(exc)), (None,) * 3
    return finding("PASS", "Byte counts from the app binary and native linker-symbol summary"), (image, ram, pool)


def static_stack(build):
    listing = build / "felucca.dis"
    try:
        sources = [listing, *(build / name for name in STACK_BLOBS)]
        missing = [str(path) for path in sources if not path.is_file()]
        if missing:
            blocked = finding("BLOCKED", f"Missing target build input: {', '.join(missing)}")
            return {name: blocked for name in ("stack_main_static", "stack_interrupt_static")}
        functions = dis_parse.functions(listing)
        if any(not functions.get(entry) for entry in ("fm1_cstart", "isr_timer5", "isr_alnk0")):
            raise stack_depth.StackError("The main path or interrupt entry has no instructions")
        main, main_path, interrupts, interrupt_paths = stack_depth.report(build)
    except (OSError, stack_depth.StackError) as exc:
        failed = finding("FAIL", str(exc))
        return {name: failed for name in ("stack_main_static", "stack_interrupt_static")}
    checks = {}
    for name, used, limit, paths in (
        ("stack_main_static", main, stack_depth.USER_STACK, [main_path]),
        ("stack_interrupt_static", interrupts, stack_depth.SYS_STACK, interrupt_paths),
    ):
        checks[name] = capacity(used, limit, limit - int(limit * stack_depth.LIMIT))
        checks[name]["analysis"] = "static call paths"
        checks[name]["paths"] = paths
    return checks


def upstream_check(command, required, positive=None, unbudgeted=None):
    missing = [str(path) for path in required if not path.is_file()]
    if missing:
        return finding("BLOCKED", f"Missing check input: {', '.join(missing)}")
    env = os.environ.copy()
    env.pop("BUDGET_UPDATE", None)
    result = subprocess.run([sys.executable, *map(str, command)], cwd=ROOT, env=env,
                            capture_output=True, text=True, check=False)
    output = result.stdout + result.stderr
    passed = result.returncode == 0
    if positive is not None:
        passed = passed and bool(positive.search(result.stdout))
    if unbudgeted is not None:
        passed = passed and unbudgeted not in result.stdout
    return finding("PASS" if passed else "FAIL", "Upstream check result",
                   tool=str(command[0]), output=output.strip())


def historical_comparison(build, upstream):
    if upstream is None:
        return finding("SKIPPED", "No verified historical build was provided")
    reference = [upstream / name for name in ("felucca.elf", "felucca.dis")]
    if any(not path.is_file() or path.stat().st_size == 0 for path in reference):
        return finding("BLOCKED", "The supplied historical build is incomplete")
    return upstream_check(
        ["tools/compare_upstream.py", build, upstream],
        [build / "felucca.elf", build / "felucca.dis", *reference],
        positive=re.compile(r"(?:[1-9]\d* frozen functions identical|[1-9]\d* identical in effect)"),
    )


def outcome(checks):
    if any(check["status"] == "FAIL" for check in checks.values()):
        return "FAIL"
    if any(check["status"] != "PASS" for check in checks.values()):
        return "BLOCKED"
    return "PASS"


def report(build, log, upstream, require_hardware):
    image = build / "felucca.bin"
    elf = build / "felucca.elf"
    listing = build / "felucca.dis"
    loader = build / "loader/ota.bin"
    target_budget = ROOT / "tests/target_budget.txt"
    memory, (flash, ram, pool) = memory_measurements(log, image)
    static = {
        "memory_evidence": memory,
        "flash_app": capacity(flash, APP_SLOT, 0),
        "ram_app": capacity(ram, RAM_LIMIT, 0),
        "pool": capacity(pool, POOL_LIMIT, POOL_FREE_MINIMUM),
        "target_cost": upstream_check(
            ["tests/target_budget.py", listing, target_budget], [listing, target_budget],
            positive=re.compile(r"cost\s+\d+ \(budget \d+\)"),
            unbudgeted="no budget",
        ),
        "loader_pin": upstream_check(["tools/check_loader.py", build], [loader]),
        "erase_order": upstream_check(["tools/check_erase_order.py", listing], [listing]),
    }
    historical = historical_comparison(build, upstream)
    if historical["status"] != "SKIPPED":
        static["upstream_comparison"] = historical
    stack = {name: dict(check, diagnostic_only=True) for name, check in static_stack(build).items()}
    hardware = {
        "stack_main_high_water": finding("BLOCKED", "Requires measurements on the FM-1", used_bytes=None),
        "stack_interrupt_high_water": finding("BLOCKED", "Requires measurements on the FM-1", used_bytes=None),
        "audio_deadline": finding("BLOCKED", "Requires timing and dropout measurements on the FM-1",
                                  used_percent=None),
    }
    paths = {
        "app_image": image, "app_elf": elf, "target_listing": listing, "native_build_log": log,
        "loader": loader,
        **{name: build / name for name in STACK_BLOBS},
    }
    if upstream is not None:
        paths.update(reference_elf=upstream / "felucca.elf", reference_listing=upstream / "felucca.dis")
    artifacts = {
        name: {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
        for name, path in paths.items() if path.is_file()
    }
    checks = {**static, "upstream_comparison": historical, **stack, **hardware}
    return {
        "status": outcome({**static, **hardware} if require_hardware else static),
        "static_status": outcome(static),
        "hardware_status": outcome(hardware),
        "checks": checks,
        "artifacts": artifacts,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--build-log", type=Path, required=True, help="stdout/stderr from one successful plain build")
    parser.add_argument("--upstream-build-dir", type=Path, help="verified frozen build from tools/upstream_build.sh")
    parser.add_argument("--require-hardware", action="store_true", help="make missing hardware evidence a blocker")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    upstream = args.upstream_build_dir.resolve() if args.upstream_build_dir else None
    data = report(build, args.build_log.resolve(), upstream, args.require_hardware)
    print(json.dumps(data, indent=2))
    return {"PASS": 0, "FAIL": 1, "BLOCKED": 2}[data["status"]]


if __name__ == "__main__":
    sys.exit(main())
