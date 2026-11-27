/* SPDX-License-Identifier: GPL-3.0-only */
#include "grind.h"
#include "rule_table.h"

enum { QUARTER_TICKS = 96, REFERENCE_STEPS = 16, SIXTEENTH_TICKS = 24 };

static uint64_t step_bit(unsigned step)
{
    return UINT64_C(1) << step;
}

static int style_known(uint8_t id)
{
    return id >= 1u && id <= GM_ALGORITHM_COUNT;
}

const char *gm_style_name(uint8_t id)
{
    return style_known(id) ? styles[id - 1u].name : NULL;
}

gm_result gm_factory_config(uint8_t id, gm_config *out)
{
    if (!out || !style_known(id))
        return GM_INVALID;
    *out = (gm_config){
        .bpm = styles[id - 1u].bpm, .engine_version = GM_ENGINE_VERSION,
        .algorithm = id, .aggression = 75, .density = 65, .chaos = 20,
        .humanize = id == 18u ? 0 : 25, .grid = {4, 4, 16, id == 11u ? 2 : 1}
    };
    return GM_OK;
}

static int is_segmented(uint8_t id)
{
    return id >= 11u && id <= 13u;
}

static unsigned track_period(uint8_t id, unsigned track)
{
    if (id == 19u && track == GM_SNARE)
        return 12;
    if (id == 19u && track == GM_CHINA)
        return 15;
    return REFERENCE_STEPS;
}

static gm_result add_anchors(const gm_config *c, gm_rule_masks *m, unsigned track,
                             unsigned bar, unsigned bar_steps, unsigned bar_ticks, unsigned unit)
{
    const uint8_t id = c->algorithm;
    uint32_t hit = styles[id - 1u].hits[track];
    const unsigned period = track_period(id, track);
    const unsigned start = bar * bar_steps;
    const int fine = c->grid.division == 32u;
    if (id == 11u)
        hit = (hit >> (bar * REFERENCE_STEPS)) & UINT32_C(0xffff);
    else if (track == GM_CRASH && bar && id != 12u)
        return GM_OK;

    if (id == 6u && track == GM_SNARE && bar_ticks < 4u * QUARTER_TICKS) {
        if (start + bar_steps / 2u < m->length[track])
            m->anchors[track] |= step_bit(start + bar_steps / 2u);
        return GM_OK;
    }
    if (id == 3u && track == GM_KICK && fine) {
        for (unsigned s = start; s < start + bar_steps && s < m->length[track]; ++s)
            m->anchors[track] |= step_bit(s);
        return GM_OK;
    }

    for (unsigned i = 0; i * SIXTEENTH_TICKS < bar_ticks || (is_segmented(id) && i < REFERENCE_STEPS); ++i) {
        if ((is_segmented(id) || track == GM_CRASH) && i == REFERENCE_STEPS)
            break;
        if (!(hit & (UINT32_C(1) << (i % period))))
            continue;
        unsigned tick = i * SIXTEENTH_TICKS;
        unsigned s = start + (is_segmented(id) ? i * bar_steps / REFERENCE_STEPS : tick / unit);
        if (s >= m->length[track])
            continue;
        if ((!is_segmented(id) && tick % unit) || (m->anchors[track] & step_bit(s)))
            return GM_UNSUPPORTED;
        m->anchors[track] |= step_bit(s);
        if (id == 3u && track == GM_KICK)
            m->rat1[track] |= step_bit(s);
        if (id == 4u && track == GM_SNARE) {
            m->accents[track] |= step_bit(s);
            if (fine && s + 1u < m->length[track] && s + 1u < start + bar_steps)
                m->anchors[track] |= step_bit(s + 1u);
            if (!fine)
                m->rat1[track] |= step_bit(s);
        }
        if (id == 5u && track == GM_KICK && i % REFERENCE_STEPS == 15u && !fine)
            m->optional_rat1[track] |= step_bit(s);
    }
    return GM_OK;
}

static void add_optional(const gm_config *c, gm_rule_masks *m)
{
    switch (c->algorithm) {
    case 1: case 5: case 6: case 7: case 15: case 22:
        for (unsigned s = 0; s < m->length[GM_HAT]; ++s) {
            uint64_t bit = step_bit(s);
            if (!(m->anchors[GM_HAT] & bit))
                m->optional[GM_HAT] |= bit;
        }
        break;
    case 2:
        if (c->grid.division != 8u) {
            for (unsigned s = 0; s < m->length[GM_KICK]; ++s) {
                uint64_t bit = step_bit(s);
                if (!(m->anchors[GM_KICK] & bit))
                    m->paired |= bit;
            }
            m->optional[GM_KICK] = m->optional[GM_SNARE] = m->paired;
        }
        break;
    case 8:
        if (c->grid.division == 32u)
            for (unsigned s = 1; s < m->length[GM_KICK]; s += 2)
                m->optional[GM_KICK] |= step_bit(s) & ~m->anchors[GM_KICK];
        break;
    case 16:
        for (unsigned s = 2; s < m->length[GM_KICK]; s += 4)
            m->optional[GM_KICK] |= step_bit(s) & ~m->anchors[GM_KICK];
        break;
    case 17: {
        /* Ghost candidates between the fixed snare accents. Keep the ride pulse. */
        const uint32_t zone = UINT32_C(0x8484);
        const unsigned bar_steps = m->length[GM_SNARE] / c->grid.bars;
        for (unsigned s = 0; s < m->length[GM_SNARE]; ++s) {
            unsigned numerator = (s % bar_steps) * 16u;
            unsigned ref = numerator / c->grid.division % REFERENCE_STEPS;
            if (numerator % c->grid.division == 0 && (zone & (UINT32_C(1) << ref)))
                m->optional[GM_SNARE] |= step_bit(s) & ~m->anchors[GM_SNARE];
        }
        break;
    }
    default:
        break;
    }
}

gm_result gm_rules_build(const gm_config *c, gm_rule_masks *out)
{
    if (!out)
        return GM_INVALID;
    gm_result result = gm_validate_config(c);
    if (result != GM_OK)
        return result;
    if (c->algorithm == 11u && c->grid.bars != 2u)
        return GM_UNSUPPORTED;
    if (c->grid.division == 8u && (c->algorithm == 3u || c->algorithm == 4u))
        return GM_UNSUPPORTED;
    uint8_t length;
    (void)gm_grid_length(c->grid, &length);
    const unsigned bar_steps = length / c->grid.bars;
    /* These alternating figures need an injective mapping of reference positions. */
    if ((c->algorithm == 11u || c->algorithm == 12u) && bar_steps < REFERENCE_STEPS)
        return GM_UNSUPPORTED;
    const unsigned bar_ticks = c->grid.num * 4u * QUARTER_TICKS / c->grid.den;
    const unsigned unit = 4u * QUARTER_TICKS / c->grid.division;
    gm_rule_masks masks = {0};
    for (unsigned t = 0; t < GM_TRACKS; ++t) {
        unsigned n = length;
        if (c->algorithm == 19u && t == GM_SNARE)
            n = 3u * n / 4u;
        if (c->algorithm == 19u && t == GM_CHINA && n > 1u)
            --n;
        masks.length[t] = (uint8_t)(n ? n : 1u);
        for (unsigned bar = 0; bar < c->grid.bars; ++bar) {
            result = add_anchors(c, &masks, t, bar, bar_steps, bar_ticks, unit);
            if (result != GM_OK)
                return result;
        }
    }
    const uint16_t rests = styles[c->algorithm - 1u].silence;
    for (unsigned bar = 0; bar < c->grid.bars; ++bar) {
        for (unsigned s = 0; s < bar_steps; ++s) {
            unsigned reference = is_segmented(c->algorithm)
                                 ? ((s + 1u) * REFERENCE_STEPS - 1u) / bar_steps
                                 : (s * unit / SIXTEENTH_TICKS) % REFERENCE_STEPS;
            if (rests & (UINT32_C(1) << reference))
                masks.forbidden |= step_bit(bar * bar_steps + s);
        }
    }
    for (unsigned t = 0; t < GM_TRACKS; ++t) {
        if (masks.anchors[t] & masks.forbidden)
            return GM_UNSUPPORTED;
    }
    add_optional(c, &masks);
    *out = masks;
    return GM_OK;
}
