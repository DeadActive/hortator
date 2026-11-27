/* SPDX-License-Identifier: GPL-3.0-only */
#include "grind.h"

static void gm_put_u8(uint8_t **out, uint8_t value)
{
    *(*out)++ = value;
}

static void gm_put_u16(uint8_t **out, uint16_t value)
{
    gm_put_u8(out, (uint8_t)value);
    gm_put_u8(out, (uint8_t)(value >> 8));
}

static void gm_put_u32(uint8_t **out, uint32_t value)
{
    gm_put_u16(out, (uint16_t)value);
    gm_put_u16(out, (uint16_t)(value >> 16));
}

size_t gm_encode(const gm_config *c, const gm_pattern *p, uint8_t *out, size_t cap)
{
    if (!p || !out || cap < GM_ENCODED_SIZE || gm_validate_config(c) != GM_OK)
        return 0;
    for (size_t track = 0; track < GM_TRACKS; ++track)
        if (gm_validate_timing(p->timing[track]) != GM_OK)
            return 0;

    gm_put_u8(&out, 0x47u); /* G */
    gm_put_u8(&out, 0x4du); /* M */
    gm_put_u8(&out, 0x43u); /* C */
    gm_put_u8(&out, 0x31u); /* 1 */
    gm_put_u16(&out, c->engine_version);
    gm_put_u8(&out, c->algorithm);
    gm_put_u16(&out, c->bpm);
    gm_put_u32(&out, c->seed);
    gm_put_u32(&out, c->mutation);
    gm_put_u8(&out, c->aggression);
    gm_put_u8(&out, c->density);
    gm_put_u8(&out, c->chaos);
    gm_put_u8(&out, c->humanize);
    gm_put_u8(&out, c->grid.num);
    gm_put_u8(&out, c->grid.den);
    gm_put_u8(&out, c->grid.division);
    gm_put_u8(&out, c->grid.bars);

    for (size_t track = 0; track < GM_TRACKS; ++track) {
        const gm_timing *timing = &p->timing[track];
        gm_put_u8(&out, timing->length);
        gm_put_u8(&out, timing->division);
        gm_put_u8(&out, timing->swing);
        gm_put_u8(&out, timing->source);
    }
    /* Retain all cells, including protected OFF cells outside the active lengths. */
    for (size_t track = 0; track < GM_TRACKS; ++track) {
        for (size_t step = 0; step < GM_STEPS; ++step) {
            const gm_cell *cell = &p->cell[track][step];
            gm_put_u8(&out, cell->flags & GM_FLAGS_MASK);
            gm_put_u8(&out, cell->cond);
            gm_put_u8(&out, (uint8_t)cell->note);
            gm_put_u8(&out, (uint8_t)cell->micro);
        }
    }
    for (size_t i = 0; i < GM_LOCK_BYTES; ++i)
        gm_put_u8(&out, p->lock[i]);

    return GM_ENCODED_SIZE;
}

uint32_t gm_hash(const uint8_t *data, size_t length)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < length; ++i)
        hash = (hash ^ data[i]) * UINT32_C(16777619);
    return hash;
}
