/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2012 Emilie Gillet (Mutable Instruments Grids: grids/pattern_generator.cc, avrlib/random.h)
 * C port for the FM-1 drum firmware: 2026 DEADACTIVE (GPL-3.0-or-later, built into this GPL-3.0-only firmware) */
/* Grids: the topographic drum pattern generator (map mode) and its Euclidean mode, one engine for the song.
 * grids_step() is one Grids step (a 1/32 note: 24 ppqn, 3 pulses a step) exactly as the original evaluates it:
 * three clock ticks, each advancing the random generator, the pattern evaluated on the first. The settings are
 * read from song.g (G_GMODE ..) at every step. Output: bits 0..2 triggers (kick, snare, hats), 3..5 accents. */
#include "grids_tables.h"

static const uint8_t GRIDS_MAP[5][5] = {         /* drum_map[x][y]: node numbers */
    {10, 8, 0, 9, 11}, {15, 7, 13, 12, 6}, {18, 14, 4, 5, 3}, {23, 16, 21, 1, 2}, {24, 19, 17, 20, 22},
};

static struct {
    uint16_t rng;                /* avrlib Random: 16-bit Galois LFSR, seed 0x21 */
    uint8_t step;                /* the step evaluated next, 0..31 */
    uint8_t last;                /* the step evaluated last (the playhead) */
    uint8_t estep[3];            /* Euclidean positions (count 1/16s, wrap at the length when evaluated) */
    uint8_t elast[3];            /* the positions evaluated last */
    uint8_t perturb[3];          /* chaos per channel, drawn at step 0 */
} grids;

static uint32_t grids_byte(int32_t v) { return (uint32_t)clamp(v, 0, 127) * 2u + ((uint32_t)clamp(v, 0, 127) >> 6); }
static uint32_t grids_mix(uint32_t a, uint32_t b, uint32_t bal) { return (a * (255u - bal) + b * bal) >> 8; }   /* U8Mix */
static void grids_rng(void) { grids.rng = (uint16_t)((grids.rng >> 1) ^ (-(uint32_t)(grids.rng & 1u) & 0xB400u)); }
static uint32_t grids_elen(uint32_t ch) { return (uint32_t)clamp(song.g[G_GLEN1 + ch], 1, 32); }
static uint32_t grids_len(uint32_t ch) { return song.g[G_GMODE] ? grids_elen(ch) : 32u; }
/* the playhead: the step / Euclidean position evaluated last (inside the length even right after a LEN change) */
static uint32_t grids_pos(uint32_t ch) { return song.g[G_GMODE] ? grids.elast[ch % 3u] % grids_elen(ch % 3u) : grids.last; }

static void grids_start(void)
{
    memset(&grids, 0, sizeof grids);
    grids.rng = 0x21;
}

/* ReadDrumMap: the level (0..255) of channel ch at step, between the four nodes around x, y (bytes) */
static uint32_t grids_level(uint32_t step, uint32_t ch, uint32_t x, uint32_t y)
{
    uint32_t i = x >> 6, j = y >> 6, off = ch * 32u + (step & 31u), xb = (x << 2) & 255u, yb = (y << 2) & 255u;
    const uint8_t *a = GRIDS_NODE[GRIDS_MAP[i][j]], *b = GRIDS_NODE[GRIDS_MAP[i + 1u][j]];
    const uint8_t *c = GRIDS_NODE[GRIDS_MAP[i][j + 1u]], *d = GRIDS_NODE[GRIDS_MAP[i + 1u][j + 1u]];
    return grids_mix(grids_mix(a[off], b[off], xb), grids_mix(c[off], d[off], xb), yb);
}

/* MAP: hit when the level (plus the step's chaos) is above ~fill; accent above 192 */
static uint32_t grids_map_bits(uint32_t step, int chaos)
{
    uint32_t ch, bits = 0, x = grids_byte(song.g[G_GX]), y = grids_byte(song.g[G_GY]);
    for (ch = 0; ch < 3u; ch++) {
        uint32_t level = grids_level(step, ch, x, y), p = chaos ? grids.perturb[ch] : 0u;
        level = level < 255u - p ? level + p : 255u;
        if (level > (~grids_byte(song.g[G_GFILL1 + ch]) & 255u)) {
            bits |= 1u << ch;
            if (level > 192u)
                bits |= 8u << ch;
        }
    }
    return bits;
}

static uint32_t grids_euclid_hit(uint32_t ch, uint32_t pos)   /* the table's bit for position pos */
{
    uint32_t len = grids_elen(ch), dens = grids_byte(song.g[G_GFILL1 + ch]) >> 3;
    return pos < 32u && (GRIDS_EUCLID[(len - 1u) * 32u + dens] >> pos) & 1u;
}

/* EUCLID (on even steps): each channel's position wraps at its length; accent (reset bit) at position 0 */
static uint32_t grids_euclid_bits(void)
{
    uint32_t ch, bits = 0;
    for (ch = 0; ch < 3u; ch++) {
        uint32_t len = grids_elen(ch);
        while (grids.estep[ch] >= len)
            grids.estep[ch] = (uint8_t)(grids.estep[ch] - len);
        if (grids_euclid_hit(ch, grids.estep[ch]))
            bits |= 1u << ch;
        if (!grids.estep[ch])
            bits |= 8u << ch;
        grids.elast[ch] = grids.estep[ch];
    }
    return bits;
}

static uint32_t grids_step(void)
{
    uint32_t bits, ch;
    grids_rng();                                     /* tick 1: Evaluate() */
    if (song.g[G_GMODE]) {
        bits = (grids.step & 1u) ? 0u : grids_euclid_bits();
    } else {
        if (!grids.step)                             /* a new bar: chaos per channel (Random::GetByte) */
            for (ch = 0; ch < 3u; ch++) {
                grids_rng();
                grids.perturb[ch] = (uint8_t)(((uint32_t)(grids.rng >> 8) * (grids_byte(song.g[G_GCHAOS]) >> 2)) >> 8);
            }
        bits = grids_map_bits(grids.step, 1);
    }
    grids.last = grids.step;
    grids_rng();                                     /* ticks 2 and 3 */
    grids_rng();
    if (!(grids.step & 1u))
        for (ch = 0; ch < 3u; ch++)
            grids.estep[ch]++;
    grids.step = (uint8_t)((grids.step + 1u) & 31u);
    return bits;
}

/* the pattern as drawn (no chaos): bit 0 hit, bit 1 accent. MAP: step i of 32; EUCLID: position i of LEN */
static uint32_t grids_preview(uint32_t ch, uint32_t i)
{
    ch %= 3u;
    if (song.g[G_GMODE]) {
        uint32_t hit = i < grids_elen(ch) && grids_euclid_hit(ch, i);
        return hit | (hit && !i) << 1;
    }
    {
        uint32_t level = grids_level(i, ch, grids_byte(song.g[G_GX]), grids_byte(song.g[G_GY]));
        uint32_t hit = level > (~grids_byte(song.g[G_GFILL1 + ch]) & 255u);
        return hit | (hit && level > 192u) << 1;
    }
}
