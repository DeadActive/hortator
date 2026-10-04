# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""ELF32 (little-endian) symbols and section bytes, stdlib only: the data behind the addresses of the target
listing (tools/compare_upstream.py, tools/stack_depth.py)."""
import bisect
import re
import struct

MG = re.compile(r"\.L_MergedGlobals(\.\d+)?")


def _sections(d):
    shoff, = struct.unpack_from("<I", d, 0x20)
    shentsize, shnum, _ = struct.unpack_from("<HHH", d, 0x2E)
    return [struct.unpack_from("<IIIIIIIIII", d, shoff + i * shentsize) for i in range(shnum)]


class Elf:
    """name_of(v): the object containing address v as "name" or "name+off"; cstring(v): a printable C string at v"""

    def __init__(self, path):
        d = open(path, "rb").read()
        secs = _sections(d)
        syms = []
        for s in secs:
            if s[1] != 2:                                # SHT_SYMTAB
                continue
            strtab = secs[s[6]]
            for i in range(s[5] // 16):
                name, value, size, info, _, shndx = struct.unpack_from("<IIIBBH", d, s[4] + i * 16)
                if not name or shndx == 0 or size == 0 or (info & 0xF) not in (0, 1, 2):
                    continue
                end = d.index(b"\0", strtab[4] + name)
                syms.append((value, size, d[strtab[4] + name:end].decode()))
        syms.sort()
        self.syms = syms
        self.addrs = [s[0] for s in syms]
        self.blobs = [(s[3], d[s[4]:s[4] + s[5]]) for s in secs if s[1] == 1 and s[3]]   # PROGBITS with an address

    def name_of(self, v):
        i = bisect.bisect_right(self.addrs, v) - 1
        while i >= 0:
            a, size, name = self.syms[i]
            if a <= v < a + size:
                name = MG.sub("MG", name)
                return name if v == a else f"{name}+{v - a}"
            if a + size <= v and i > 0 and self.syms[i - 1][0] + self.syms[i - 1][1] <= a:
                return None
            i -= 1
        return None

    def cstring(self, v):
        for addr, b in self.blobs:
            if addr <= v < addr + len(b):
                e = b.find(b"\0", v - addr)
                s = b[v - addr:e if e >= 0 else v - addr + 64]
                if 1 <= len(s) <= 64 and all(32 <= c < 127 for c in s):
                    return '"' + s.decode() + '"'
                return None
        return None

    def bytes_at(self, v, n):
        for addr, b in self.blobs:
            if addr <= v and v + n <= addr + len(b):
                return b[v - addr:v - addr + n]
        return None
