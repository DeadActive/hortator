/* SPDX-License-Identifier: GPL-3.0-only */
/* Natural hit variation: velocity-only, deterministic, off at 0. */
#include <stdio.h>
#include <stdint.h>
#include "hit_variation.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
int main(void)
{
    uint32_t k, lo = 127, hi = 0, distinct = 0, prev = 0;
    /* off: the base velocity, whatever the key */
    for (k = 0; k < 5000; k++)
        CHECK(gm_hit_velocity(96, 0, gm_hit_key(7, 1, k, 0, 0)) == 96);
    /* deterministic */
    CHECK(gm_hit_key(1, 2, 3, 4, 5) == gm_hit_key(1, 2, 3, 4, 5));
    CHECK(gm_hit_key(1, 2, 3, 4, 5) != gm_hit_key(1, 2, 3, 5, 5));
    CHECK(gm_hit_key(1, 2, 3, 4, 5) != gm_hit_key(1, 2, 3, 4, 6));
    /* bounded: at 100 the swing stays within +-8 and inside 1..127; accents never wrap */
    for (k = 0; k < 20000; k++) {
        uint32_t v = gm_hit_velocity(96, 100, gm_hit_key(3, 0, k, k >> 4, 0));
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        if (v != prev) distinct++;
        prev = v;
    }
    CHECK(lo >= 88 && hi <= 104);
    CHECK(hi - lo >= 12);                 /* it really spreads */
    CHECK(distinct > 1000);
    CHECK(gm_hit_velocity(127, 100, 0x00FFFF00u) == 127);   /* clamped at the top */
    CHECK(gm_hit_velocity(1, 100, 0) >= 1);
    CHECK(gm_hit_velocity(96, 50, gm_hit_key(9, 9, 9, 9, 9)) >= 92);   /* half the depth, half the swing */
    printf(fails ? "test_hit_variation: %d FAILED\n" : "test_hit_variation: all passed\n", fails);
    return fails != 0;
}
