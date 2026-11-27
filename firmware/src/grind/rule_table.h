/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef GRIND_RULE_TABLE_H
#define GRIND_RULE_TABLE_H

/* Reference positions are sixteenth notes in role order: K S H R C X TH TL. */
typedef struct {
    const char *name;
    uint32_t hits[GM_TRACKS];
    uint16_t silence, bpm;
} gm_style;

static const gm_style styles[] = {
    {"Traditional", {0x5555, 0xaaaa, 0x5555, 0, 1}, 0, 240},
    {"Hammer", {0x5555, 0x5555, 0, 0x5555, 1}, 0, 240},
    {"Bomb", {0xffff, 0x5555, 0, 0x5555, 1}, 0, 240},
    {"Gravity", {0x1111, 0x5555, 0, 0x1111, 1}, 0, 240},
    {"Grind Blast", {0xd555, 0xaaaa, 0x5555, 0, 1, 0x1000}, 0, 240},
    {"D-Beat", {0x4949, 0x1010, 0x5555, 0, 1}, 0, 220},
    {"Skank", {0x1111, 0x4444, 0x5555, 0, 1}, 0, 220},
    {"Doublebass", {0xffff, 0x1010, 0, 0x1111, 1}, 0, 200},
    {"Technical", {0x4849, 0x1010, 0, 0x9249, 1, 0, 0x400, 0x8000}, 0, 220},
    {"Brutal", {0x4cc7, 0x1110, 0x1111, 0, 1, 0x101}, 0, 220},
    {"Fast-Slow", {0x10495555, 0x0100aaaa, 0x11115555, 0, 0x10001}, 0, 220},
    {"Abrupt Break", {0x5055, 0x90aa, 0x5055, 0, 0x1001}, 0xf00, 220},
    {"Stop-and-Go", {0x4713, 0x4111, 0, 0, 1, 0x4111}, 0xb8ec, 200},
    {"Breakdown", {0x9449, 0x100, 0x1111, 0, 1, 0x101}, 0, 150},
    {"Two-Step", {0x4949, 0x1410, 0x5555, 0, 1}, 0, 190},
    {"Half-Time", {0x1045, 0x100, 0, 0x5555, 1}, 0, 160},
    {"Chaos", {0x4221, 0x810, 0, 0x1111, 1, 0, 0x80, 0x2000}, 0, 200},
    {"Industrial", {0x1111, 0x1010, 0x5555, 0, 1, 0x404}, 0, 160},
    {"Polymetric", {0x1111, 0x410, 0x5555, 0, 1, 0x421}, 0, 180},
    {"Sludge", {0x4109, 0x1010, 0, 0x4141, 1, 0, 0, 0x8000}, 0, 90},
    {"Funeral", {0x401, 0x100, 0, 0x101, 1, 0, 0, 0x8000}, 0x00f0, 55},
    {"Stoner", {0x989, 0x1010, 0x5555, 0, 1}, 0, 110},
};
typedef char gm_style_count[sizeof styles / sizeof *styles == GM_ALGORITHM_COUNT ? 1 : -1];

#endif
