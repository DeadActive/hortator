#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Emit the independent Grind PRNG v1 reference vectors."""

from itertools import product


MASK = 0xFFFFFFFF


def mix32(value):
    value = ((value ^ (value >> 16)) * 0x7FEB352D) & MASK
    value = ((value ^ (value >> 15)) * 0x846CA68B) & MASK
    return value ^ (value >> 16)


def random_cell(seed, mutation, engine, algorithm, track, step, domain):
    state = mix32(seed ^ 0x9E3779B9)
    for key in (mutation, engine * 65536 + algorithm, track * 256 + step, domain):
        state = mix32(state ^ key)
    return state


def main():
    print("# seed mutation algorithm track step domain output (seed, mutation and output in hex; engine v1)")
    cases = product((0, 1, MASK), (0, MASK), (1, 22), (0, 7), (0, 63), range(1, 8))
    for seed, mutation, algorithm, track, step, domain in cases:
        value = random_cell(seed, mutation, 1, algorithm, track, step, domain)
        print(f"{seed:08x} {mutation:08x} {algorithm:2d} {track} {step:2d} {domain} {value:08x}")


if __name__ == "__main__":
    main()
