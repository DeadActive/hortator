#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build Felucca: the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[-suffix]]

Outputs in build/: felucca.bin (app), loader/ota.bin (update loader),
felucca.fwsc (package). See BUILDING.md for the toolchain and the SDK.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
sys.path.insert(0, str(SRC / "tools"))
sys.path.insert(0, str(SRC / "web"))
import fm1pkg_make  # noqa: E402
import lz4blk  # noqa: E402
import make_site
import version as drum_version  # noqa: E402

APP_XIP = 0x02000120                # app.bin offset 0 in the XIP map; the SPL jumps here
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
SDK_MOUNT = None                    # BLE builds: the JieLi BT SDK (tools/ble_libs.py), mounted read-only at /sdk
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

PRODUCT = "FM-1_900"                # package identity; release builds are FM-1_9XY
VERSION = None                      # FELUCCA_VERSION for release builds (default: the drum version, VERSION.txt)


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        sdk = ["-v", f"{SDK_MOUNT}:/sdk:ro"] if SDK_MOUNT else []
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work", *sdk,
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers (fonts, icons, tables, samples)"""
    GEN.mkdir(parents=True, exist_ok=True)
    tools = SRC / "tools"
    cmds = [[tools / "gen_font.py", GEN / "felucca_font.h"],
            [tools / "gen_icons.py", GEN / "felucca_icons.h"],
            [tools / "gen_logo.py", GEN / "felucca_logo.h"],
            [tools / "gen_tables.py", GEN / "felucca_tables.h"],
            [tools / "gen_samples.py", GEN / "felucca_samples.h"]]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f"build: {', '.join(failed)} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, "-Ifirmware/hal", "-Ifirmware/src"]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- app

def build_app():
    # the app without global merging (user decision 2026-10-06): which small statics the compiler merges depends on
    # the whole program, so the same frozen code would address its data differently here and in the frozen
    # baseline's build (H2). The update loader keeps CFLAGS as they are (its binary is pinned).
    flags = [*CFLAGS, "-mllvm", "-enable-global-merge=false", "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
    # FELUCCA_BLE=1: the BLE build (firmware/src/ble/, the JieLi BT libraries; from fm1-lsdj 548ce73).
    # FELUCCA_CORE_REF=1: its core unit (FELUCCA_BLE=1) linked with empty BLE entry points (ble_stub.c), no libraries
    ble = os.environ.get("FELUCCA_BLE") == "1"
    core_ref = os.environ.get("FELUCCA_CORE_REF") == "1"
    if core_ref and not ble:
        flags.append("-DFELUCCA_BLE=1")
    for flag in ("FELUCCA_FLASH", "FELUCCA_OTA", "FELUCCA_OTA_DRYRUN", "FELUCCA_CDC", "FELUCCA_UART",
                 "FELUCCA_ICONS", "FELUCCA_SLICE", "FELUCCA_UAC", "FELUCCA_UAC_TONE", "FELUCCA_BLE"):
        v = os.environ.get(flag)    # unset: the default in firmware/src/felucca.c
        if v in ("0", "1") and not (flag == "FELUCCA_BLE" and core_ref and not ble):
            flags.append(f"-D{flag}={v}")
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    flags.append(f'-DFELUCCA_VERSION="{VERSION or drum_version.firmware_string(drum_version.read())}"')
    asm = [("cc", "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
           ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
           ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o")]
    cfg_objs = []
    if ble:
        global SDK_MOUNT
        import ble_libs
        if not use_docker():
            raise SystemExit("build: FELUCCA_BLE builds run the toolchain in Docker (the SDK is mounted at /sdk)")
        ble_libs.libs()             # first: a missing SDK or a library that is not the pinned one stops here, named
        SDK_MOUNT = str(ble_libs.SDK)
        asm.append(("cc", "-c", FW / "hal" / "fm1_ctx.S", "-o", OUT / "fm1_ctx.o"))
        (OUT / "blecfg").mkdir(exist_ok=True)
        for src in ble_libs.SDK_CONFIG_SOURCES:
            obj = OUT / "blecfg" / (Path(src).stem + ".o")
            asm.append(("cc", *ble_libs.SDK_CFLAGS, "-c", f"/sdk/{src}", "-o", obj))
            cfg_objs.append(obj)
        asm.append(("cc", "-mcpu=r3", "-Os", "-flto", "-Wall", "-c", SRC / ble_libs.LTO_STUBS, "-o",
                    OUT / "blecfg" / "ble_lto_stubs.o"))
        cfg_objs.append(OUT / "blecfg" / "ble_lto_stubs.o")
    tc_all(*asm, ("cc", *flags, "-c", FW / "src" / "felucca.c", "-o", OUT / "felucca.o"))
    elf = OUT / "felucca.elf"
    extra = []
    if core_ref and not ble:        # the reference: empty BLE entry points instead of the BLE unit and the libraries
        tc("cc", *[f for f in flags if f != "-Ibuild/gen"], "-c", FW / "src" / "ble" / "ble_stub.c", "-o",
           OUT / "ble_stub.o")
        extra = [OUT / "ble_stub.o"]
    if ble:                         # (only then: the default link is as without BLE, gate 5)
        libdir = OUT / "blelibs"
        libdir.mkdir(exist_ok=True)
        r = subprocess.run([sys.executable, SRC / "tools" / "ble_libs.py", "libs"], capture_output=True, text=True)
        if r.returncode:            # a missing SDK or a library that is not the pinned one: name it and stop
            raise SystemExit(f"build: tools/ble_libs.py libs failed:\n{r.stdout}{r.stderr}")
        libs = r.stdout.split()
        for p in libs:              # inside SRC, so the Docker mount sees them
            shutil.copy(p, libdir / Path(p).name)
        objs = ble_libs.members(libdir)     # single SDK members (lbuf, circular_buf, wlc, encryption)
        # the BLE module, its own unit: no static data shared with the core's unit
        # long calls, as the libraries (large-program): the BLE unit calls RAM code (__wrap_memcpy, the delays)
        tc("cc", *[f for f in flags if f != "-Ibuild/gen"], "-mllvm", "-pi32v2-large-program=true", "-c",
           FW / "src" / "ble" / "ble.c", "-o", OUT / "ble.o")
        objs.append(OUT / "ble.o")
        extra = [OUT / "fm1_ctx.o", *cfg_objs, *objs, "--start-group", *[libdir / Path(p).name for p in libs],
                 "--end-group"]
    # the BT libraries are LLVM bitcode: link them as the SDK does, through lto-wrapper with its codegen options
    # (apps/demo/demo_ble/board/wl82/Makefile LFLAGS); our objects stay native, so only the libraries go through LTO
    # (lto-wrapper is a python script for `ld --plugin LLVMgold.so ...`; the container has no python3: call ld)
    ld = ("pi32v2/bin/ld",)
    if ble:
        gold = "/opt/jieli/pi32v2/bin/LLVMgold.so"
        ld = (*ld, "--plugin", gold, "--plugin-opt=mcpu=r3", "--plugin-opt=-pi32v2-large-program=true",
              "--plugin-opt=-pi32v2-always-use-itblock=false",
              "--plugin-opt=-dont-used-symbol-list=" + ",".join(ble_libs.LOG_FUNCS),
              "--orphan-handling=error", "-Map", "build/felucca.map")   # every library section placed on purpose
        # the libraries' (and the BLE unit's) memcpy calls go to ble_port.c's RAM copy, __wrap_memcpy: the radio
        # calibration calls memcpy while the flash must not be read. --wrap redirects undefined references only, so
        # the core's unit, which defines memcpy, keeps calling its own
        ld = (*ld, "--wrap=memcpy")
    script = FW / ("app_ble.ld" if ble else "app.ld")    # (app_ble.ld: app.ld + the BT libraries' sections)
    if ble and os.environ.get("BLE_MEASURE") == "1":
        # measuring only (never packaged, main()): the regions widened so an image that does not fit yet still links
        # and can be measured; the XIP to the flash's end, the pool to the noinit area (over the stack symbols)
        lds = FW.joinpath("app_ble.ld").read_text()
        lds, n = re.subn(r"(XIP\s+\(rx\)\s*: ORIGIN = 0x02000120, LENGTH = )0x8DFBC", r"\g<1>0xFFEE0", lds)
        lds, k = re.subn(r"(POOL\s+\(rw\)\s*: ORIGIN = 0x01C20000, LENGTH = )0x54000", r"\g<1>0x5C000", lds)
        if n != 1 or k != 1:
            raise SystemExit("build: BLE_MEASURE: app_ble.ld's XIP / POOL lines not found")
        script = OUT / "app_ble_measure.ld"
        script.write_text(lds)
    tc(*ld, "-T", script, OUT / "crt0.o", OUT / "fm1_vec.o", OUT / "fm1_isr.o",
       OUT / "felucca.o", *extra, "-o", elf)
    if ble:                         # library code meant for RAM would have landed in XIP via *(.*_code): refuse
        # .volatile_ram_code (wl_rf_common's wf_rf_trim) is placed in .ram_text with the trim (app_ble.ld);
        # .bt_updata_ram_code = bredr_frame's BT-over-the-air-update helpers, never run, allowed in XIP (fm1-lsdj
        # docs/ble/LINK_NOTES.md: checked by hand)
        ok = {".volatile_ram_code", ".bt_updata_ram_code"}
        ram_code = sorted(set(re.findall(r"^\s*(\.\S*ram_code\S*)", (OUT / "felucca.map").read_text(), re.M)) - ok)
        if ram_code:
            raise SystemExit(f"build: BT library RAM code sections linked (not placed in RAM): {ram_code}")
    for sect in ("text.bin", "data.bin", "ramtext.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf))
    (OUT / "felucca.dis").write_text(dis)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text and .data follow .text at their load addresses; crt0 copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    (OUT / "felucca.bin").write_bytes(img)
    return bytes(img), syms, dis, rt


def rt_reach(rt):
    """BLE builds (the radio calibration runs from .ram_text and calls within it; fm1-lsdj 548ce73): RAM code may
    call and jump only inside .ram_text, never through a register, and use no XIP address as data"""
    def in_rt(a): return 0x01C00000 <= a < 0x01C06000
    bad, calls = [], 0
    for ln in rt.splitlines():
        if not LINE.match(ln):
            continue
        if re.search(r"\b(call|goto)\s+r\d+\b", ln):
            bad.append(ln)
            continue
        for a in re.findall(r"<[^>]*:\s*([0-9a-f]+)\s*>", ln):
            if 0x02000000 <= int(a, 16) < 0x02100000 or (re.search(r"\b(call|goto)\b", ln) and not in_rt(int(a, 16))):
                bad.append(ln)
        calls += bool(re.search(r"\bcall\b", ln))
    return bad, calls


def check(img, syms, dis, rt, ble=False, measure=False):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        errors.append(f"image starts with {img[:4].hex()}, not the entry stub")
    insns = len([ln for ln in rt.splitlines() if LINE.match(ln)])
    if ble:                         # the trim's RAM code calls inside .ram_text
        rt_bad, n_calls = rt_reach(rt)
        if rt_bad:
            errors.append(f".ram_text reaches outside RAM: {rt_bad[:3]}")
        else:
            notes.append(f".ram_text: {insns} insns, {n_calls} calls, all inside .ram_text, no XIP address")
    else:
        rt_calls = [ln for ln in rt.splitlines() if re.search(r"\bcall\b", ln)]
        if rt_calls:                # RAM code runs with the flash off: no calls into XIP
            errors.append(f".ram_text contains calls: {rt_calls[:3]}")
        else:
            notes.append(f".ram_text: {insns} insns, no calls")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) > APP_SLOT:
        (notes if measure else errors).append(f"image {len(img)} B exceeds the app slot ({APP_SLOT} B) by "
                                              f"{len(img) - APP_SLOT} B" + (" (BLE_MEASURE: measuring only)" if
                                                                            measure else ""))

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    bss = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    notes.append(f"image {len(img)} B; RAM .data+.bss {bss} B of 98304; pool {pool} B of {0x54000}")
    if bss > 96 * 1024:
        errors.append("RAM region overflow")
    if 0x54000 - pool < 8192:                     # keep >= 8 KiB of the pool spare
        (notes if measure else errors).append(f"pool headroom {0x54000 - pool} B < 8192 B" +
                                              (" (BLE_MEASURE: measuring only)" if measure else ""))
    return errors, notes


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    ble = [f for f in (FW / "src" / "ble").glob("*.[ch]") if f.name != "ble_rf_tables.c"]   # (the radio's tables)
    for f in sorted([*(FW / "src").glob("*.[ch]"), *ble, *(FW / "loader").glob("*.c")]):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def drum_label():
    """drum-<version>+<commit>, -dirty when tracked files differ from it: the installer names the exact source
    it ships (docs/VERSIONING.md)"""
    def git(*a):
        return subprocess.run(["git", *a], cwd=SRC, capture_output=True, text=True).stdout.strip()
    rev = git("rev-parse", "--short", "HEAD") or "unknown"
    return f"drum-{drum_version.read()}+{rev}" + ("-dirty" if git("status", "--porcelain", "--untracked-files=no") else "")


def main():
    global PRODUCT, VERSION
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help="release build: identity FM-1_9XY, version string X.Y")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    ap.add_argument("--gen-only", action="store_true", help="only the generated headers (host tests)")
    a = ap.parse_args()
    if a.gen_only:
        generate()
        return 0
    name = "felucca.fwsc"
    if a.release:                   # one digit each: the identity has room for two
        m = re.fullmatch(r"(\d)\.(\d)(-[A-Za-z0-9]+)?", a.release)
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y or X.Y-suffix, one digit each")
        PRODUCT = "FM-1_9" + m[1] + m[2]
        VERSION = a.release.upper() if "BETA" in a.release.upper() else a.release.upper() + " BETA"
        name = f"felucca-{a.release}.fwsc"
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(2) as ex:
        gen, ldr = ex.submit(generate), ex.submit(build_loader)
        gen.result()
        ota = ldr.result()
    ble = os.environ.get("FELUCCA_BLE") == "1"
    measure = ble and os.environ.get("BLE_MEASURE") == "1"
    if measure and os.environ.get("DRUM_PACKAGE") == "1":
        raise SystemExit("build: BLE_MEASURE=1 only measures; no package with it")
    img, syms, dis, rt = build_app()
    errors, notes = check(img, syms, dis, rt, ble, measure)
    hal_err = mmio_check()
    errors += hal_err
    if not hal_err:
        notes.append("register access: hal/ only (src/, src/ble/, loader/ clean)")
    for n in notes:
        print("  ok   ", n)
    for e in errors:
        print("  FAIL ", e)
    if errors:
        raise SystemExit("build: checks failed")
    if os.environ.get("DRUM_PACKAGE") != "1":     # drum fork: no installable file unless asked
        print(f"app      {OUT / 'felucca.bin'}  {len(img)} B ({APP_SLOT - len(img)} B free)")
        print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
        print("package  skipped (DRUM_PACKAGE=1 writes build/felucca-UNTESTED.fwsc; never install it in M1)")
        return 0
    name = name.replace(".fwsc", "-UNTESTED.fwsc")
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, PRODUCT)
    (OUT / name).write_bytes(pkg)
    att = SRC / "assets" / "samples-cc0" / "ATTRIBUTION.txt"
    if att.exists():
        shutil.copy(att, OUT / "ATTRIBUTION.txt")
    print(f"app      {OUT / 'felucca.bin'}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}")
    make_site.main(OUT / name, VERSION or drum_label(), OUT / "site")   # the local web installer: always this package
    return 0


if __name__ == "__main__":
    sys.exit(main())
