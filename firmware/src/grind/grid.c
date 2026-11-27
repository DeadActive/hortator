/* SPDX-License-Identifier: GPL-3.0-only */
#include "grind.h"

static int gm_supported_division(uint8_t division)
{
    return division == 8u || division == 16u || division == 32u;
}

static int gm_supported_meter(gm_grid grid)
{
    return (grid.den == 4u && grid.num >= 3u && grid.num <= 5u) ||
           (grid.den == 8u && (grid.num == 6u || grid.num == 7u || grid.num == 9u));
}

gm_result gm_grid_length(gm_grid grid, uint8_t *length)
{
    if (!length || !gm_supported_meter(grid) || !gm_supported_division(grid.division) ||
        grid.bars < 1u || grid.bars > 4u)
        return GM_INVALID;

    uint32_t product = (uint32_t)grid.num * grid.division * grid.bars;
    if (product % grid.den != 0u)
        return GM_INVALID;
    uint32_t steps = product / grid.den;
    if (steps == 0u)
        return GM_INVALID;
    if (steps > GM_STEPS)
        return GM_LIMIT;

    *length = (uint8_t)steps;
    return GM_OK;
}

gm_result gm_validate_config(const gm_config *c)
{
    uint8_t length;
    if (!c || c->engine_version != GM_ENGINE_VERSION ||
        c->algorithm < 1u || c->algorithm > GM_ALGORITHM_COUNT ||
        c->bpm < 40u || c->bpm > 350u ||
        c->aggression > 100u || c->density > 100u || c->chaos > 100u || c->humanize > 100u)
        return GM_INVALID;
    return gm_grid_length(c->grid, &length);
}

gm_result gm_validate_timing(gm_timing timing)
{
    if (!gm_supported_division(timing.division) || timing.length == 0u)
        return GM_INVALID;
    return timing.length > GM_STEPS ? GM_LIMIT : GM_OK;
}
