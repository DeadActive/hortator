/* SPDX-License-Identifier: GPL-3.0-only */
#include "common.h"
#include <string.h>

int main(void)
{
    gm_config c;
    gm_pattern base, before, previous, next;
    assert(gm_factory_config(1, &c) == GM_OK);
    c.seed = UINT32_C(123456789);
    c.density = c.humanize = c.aggression = c.chaos = 0;
    assert(gm_generate(&c, NULL, &base) == GM_OK);
    base.cell[GM_KICK][0] = (gm_cell){GM_ON | GM_TIE, 58, -10, -2};
    base.cell[GM_HAT][1] = (gm_cell){GM_ACC, 17, 11, 4};
    assert(gm_set_lock(&base, GM_KICK, 0, 1) == GM_OK);
    assert(gm_set_lock(&base, GM_HAT, 1, 1) == GM_OK);
    before = base;

    assert(gm_generate(&c, &base, &previous) == GM_OK);
    for (unsigned density = 1; density <= 100; ++density) {
        c.density = (uint8_t)density;
        assert(gm_generate(&c, &base, &next) == GM_OK);
        for (unsigned t = 0; t < GM_TRACKS; ++t)
            for (unsigned s = 0; s < GM_STEPS; ++s) {
                assert(!(previous.cell[t][s].flags & GM_ON) || (next.cell[t][s].flags & GM_ON));
                if (!gm_is_locked(&base, t, s))
                    assert(next.cell[t][s].micro == 0);
            }
        assert(!memcmp(&next.cell[GM_KICK][0], &base.cell[GM_KICK][0], sizeof(gm_cell)));
        assert(!memcmp(&next.cell[GM_HAT][1], &base.cell[GM_HAT][1], sizeof(gm_cell)));
        assert(!memcmp(next.lock, base.lock, sizeof base.lock));
        previous = next;
    }
    assert(!memcmp(&base, &before, sizeof base));

    c.aggression = c.chaos = c.humanize = 100;
    assert(gm_generate(&c, &base, &next) == GM_OK);
    unsigned varied = 0;
    for (unsigned t = 0; t < GM_TRACKS; ++t) {
        for (unsigned s = 0; s < GM_STEPS; ++s) {
            assert(!((next.cell[t][s].flags ^ previous.cell[t][s].flags) & GM_ON));
            if (!gm_is_locked(&next, t, s)) {
                int micro = next.cell[t][s].micro;
                assert(micro >= (s == 0 ? 0 : -9) && micro <= 9);
                varied |= micro != 0;
            }
        }
    }
    assert(varied);
    previous = next;
    c.grid.division = 32;
    assert(gm_generate(&c, &base, &next) == GM_LOCK_CONFLICT);
    assert(!memcmp(&previous, &next, sizeof next));
    assert(!memcmp(&base, &before, sizeof base));

    assert(gm_factory_config(2, &c) == GM_OK);
    c.aggression = c.humanize = 0;
    c.chaos = 0;
    assert(gm_generate(&c, NULL, &base) == GM_OK);
    c.chaos = 100;
    assert(gm_generate(&c, NULL, &next) == GM_OK);
    for (unsigned s = 0; s < 16; ++s) {
        assert(!((next.cell[GM_KICK][s].flags ^ next.cell[GM_SNARE][s].flags) & GM_ON));
        if (!(s & 1u))
            for (unsigned t = 0; t < GM_TRACKS; ++t)
                assert(!memcmp(&next.cell[t][s], &base.cell[t][s], sizeof(gm_cell)));
    }

    before = base;
    previous = next;
    for (unsigned t = 0; t < GM_TRACKS; ++t) {
        base.cell[t][0].flags = GM_ON | (3u << GM_RAT_SHIFT);
        assert(gm_set_lock(&base, t, 0, 1) == GM_OK);
    }
    assert(gm_generate(&c, &base, &next) == GM_LIMIT);
    assert(!memcmp(&previous, &next, sizeof next));
    assert(gm_validate(&c, &before) == GM_OK);


    assert(gm_factory_config(13, &c) == GM_OK);
    c.aggression = c.chaos = c.density = c.humanize = 100;
    assert(gm_generate(&c, NULL, &next) == GM_OK);
    for (unsigned t = 0; t < GM_TRACKS; ++t)
        for (unsigned s = 0; s < 16; ++s)
            if (UINT32_C(0xb8ec) & (1u << s))
                assert(!(next.cell[t][s].flags & GM_ON));
    return 0;
}
