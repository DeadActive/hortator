/* SPDX-License-Identifier: GPL-3.0-only */
#include "grind.h"

int gm_is_locked(const gm_pattern *p, unsigned track, unsigned step)
{
    if (!p || track >= GM_TRACKS || step >= GM_STEPS)
        return 0;
    unsigned index = track * GM_STEPS + step;
    return (p->lock[index >> 3] & (1u << (index & 7u))) != 0u;
}

gm_result gm_set_lock(gm_pattern *p, unsigned track, unsigned step, int locked)
{
    if (!p || track >= GM_TRACKS || step >= GM_STEPS)
        return GM_INVALID;
    unsigned index = track * GM_STEPS + step;
    uint8_t bit = (uint8_t)(1u << (index & 7u));
    if (locked)
        p->lock[index >> 3] |= bit;
    else
        p->lock[index >> 3] &= (uint8_t)~bit;
    return GM_OK;
}
