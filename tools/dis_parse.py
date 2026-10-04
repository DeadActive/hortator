# SPDX-License-Identifier: GPL-3.0-only
# Drum machine fork: 2026 DEADACTIVE
"""Parser of the JieLi objdump -d listing (build/felucca.dis): functions and their instructions.
A function starts at a line "name:" in column 0; an instruction line is " ADDR:    BYTES \tTEXT";
"}" lines of the pretty-printer belong to the instruction before them and are kept as text."""
import re

FUNC_RE = re.compile(r"^([A-Za-z_.$][\w.$]*):$")
INSN_RE = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*\t(.*)$")


def functions(path):
    funcs, cur = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        m = FUNC_RE.match(line)
        if m:
            cur = funcs.setdefault(m.group(1), [])
            continue
        m = INSN_RE.match(line)
        if m and cur is not None:
            cur.append((int(m.group(1), 16), m.group(2).replace("\t", " ").strip()))
        elif cur is not None and line.strip() == "}":
            cur.append((cur[-1][0] if cur else 0, "}"))
    return funcs
