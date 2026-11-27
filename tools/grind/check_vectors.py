#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independently encode the 4/4 reference patterns and compare checked-in bytes."""
import argparse
import csv
import io
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
GOLDEN = ROOT / "tests/grind/golden"
SEEDS = (0, 1, 42, 0xFFFFFFFF)
FACTORY_TEMPO = (240, 240, 240, 240, 240, 220, 220, 200, 220, 220, 220, 220, 200,
                 150, 190, 160, 200, 160, 180, 90, 55, 110)
ROLES = ("kick", "snare", "hat", "ride", "crash", "china", "tom_high", "tom_low")


def mix(x):
    x = ((x ^ (x >> 16)) * 0x7FEB352D) % (1 << 32)
    x = ((x ^ (x >> 15)) * 0x846CA68B) % (1 << 32)
    return x ^ (x >> 16)


def pattern(row, seed):
    style = int(row["id"])
    bpm = FACTORY_TEMPO[style - 1]
    human = 0 if style == 18 else 25
    data = bytearray(struct.pack("<4sHBHIIBBBBBBBB", b"GMC1", 1, style, bpm, seed, 0,
                                 75, 65, 20, human, 4, 4, 16, int(row["bars"])))
    base_key = mix(mix(mix(seed ^ 0x9E3779B9)) ^ ((1 << 16) + style))
    for role in ROLES:
        data.extend((int(row[role + "_length"]), 16, 0, 0))
    for track, role in enumerate(ROLES):
        length = int(row[role + "_length"])
        anchors = int(row[role + "_on"], 0)
        rats = int(row[role + "_rat1"], 0)
        optional = 0
        if (style in (1, 5, 6, 7, 15, 22) and role == "hat") or (
                style == 2 and role in ("kick", "snare")):
            optional = ((1 << length) - 1) ^ anchors
        elif style == 16 and role == "kick":
            optional = sum(1 << s for s in range(2, length, 4)) & ~anchors
        for step in range(64):
            bit = 1 << step
            key = mix(base_key ^ (track * 256 + step))
            kick_key = mix(base_key ^ step) if style == 2 and role == "snare" else key
            optional_hit = optional & bit and mix(kick_key ^ 1) % 100 < 65
            if not (anchors & bit or optional_hit):
                data.extend(bytes(4))
                continue
            accent = mix(key ^ 3) % 100 < 75
            if optional_hit:
                accent ^= mix(key ^ 2) % 100 < 20
                accent ^= mix(key ^ 5) % 100 < human // 4
            accent = accent or (style == 4 and role == "snare")
            twice = bool(rats & bit or (style == 5 and role == "kick" and step == 15
                                      and mix(key ^ 1) % 100 < 65))
            span = min(12, bpm // 25) * human // 100
            micro = mix(key ^ 4) % (span + 1 if not step else 2 * span + 1)
            data.extend(struct.pack("<BBBb", 1 + 2 * accent + 16 * twice, 0, 0,
                                    micro - (span if step else 0)))
    return data + bytes(64)


def vectors():
    metadata = io.StringIO(newline="")
    writer = csv.writer(metadata, lineterminator="\n")
    writer.writerow(("style", "seed", "offset", "length"))
    binary = bytearray()
    with (GOLDEN / "styles-v1.csv").open(encoding="utf-8", newline="") as source:
        for row in csv.DictReader(source):
            for seed in SEEDS:
                encoded = pattern(row, seed)
                assert len(encoded) == 2169
                writer.writerow((int(row["id"]), seed, len(binary), len(encoded)))
                binary.extend(encoded)
    return metadata.getvalue().encode(), binary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true", help="write the independent reference vectors")
    args = parser.parse_args()
    for suffix, content in zip(("csv", "bin"), vectors()):
        path = GOLDEN / f"patterns-v1.{suffix}"
        if args.write:
            path.write_bytes(content)
        elif path.read_bytes() != content:
            raise SystemExit(f"{path}: differs from the independent reference")
    print("vectors: independent reference matches")


if __name__ == "__main__":
    main()
