/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include "common.h"

static void test_supported_grids(void)
{
    static const uint8_t divisions[] = {8, 16, 32};
    static const struct {
        uint8_t num, den, steps[3];
    } meters[] = {
        {3, 4, {6, 12, 24}}, {4, 4, {8, 16, 32}}, {5, 4, {10, 20, 40}},
        {6, 8, {6, 12, 24}}, {7, 8, {7, 14, 28}}, {9, 8, {9, 18, 36}},
        {7, 4, {14, 28, 56}}                        /* seven quarters: 28 sixteenths, 56 thirty-seconds */
    };

    for (size_t m = 0; m < sizeof meters / sizeof meters[0]; ++m) {
        for (size_t d = 0; d < sizeof divisions / sizeof divisions[0]; ++d) {
            for (uint8_t bars = 1; bars <= 4; ++bars) {
                gm_grid g = {meters[m].num, meters[m].den, divisions[d], bars};
                unsigned expected = meters[m].steps[d] * bars;
                uint8_t actual = 99;
                if (expected > 64) {
                    assert(gm_grid_length(g, &actual) == GM_LIMIT);
                    assert(actual == 99);
                } else {
                    assert(gm_grid_length(g, &actual) == GM_OK);
                    assert(actual == expected);
                }
            }
        }
    }
}

static void test_invalid_grids(void)
{
    static const gm_grid invalid[] = {
        {3, 8, 16, 1}, {2, 4, 16, 1}, {4, 8, 16, 1}, {6, 4, 16, 1},
        {0, 4, 16, 1}, {4, 0, 16, 1}, {4, 4, 0, 1}, {4, 4, 7, 1},
        {4, 4, 24, 1}, {4, 4, 16, 0}, {4, 4, 16, 5}, {4, 4, 16, 255},
        {255, 255, 255, 255}
    };

    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        uint8_t actual = 99;
        assert(gm_grid_length(invalid[i], &actual) == GM_INVALID);
        assert(actual == 99);
    }
    assert(gm_grid_length((gm_grid){4, 4, 16, 1}, NULL) == GM_INVALID);
}

static void test_config_and_timing(void)
{
    gm_config c = {.engine_version = 1, .algorithm = 1, .bpm = 40,
                   .grid = {4, 4, 16, 4}};
    assert(gm_validate_config(NULL) == GM_INVALID);
    assert(gm_validate_config(&c) == GM_OK);
    c.algorithm = 22;
    c.bpm = 350;
    c.aggression = c.density = c.chaos = c.humanize = 100;
    assert(gm_validate_config(&c) == GM_OK);

    c.bpm = 351; assert(gm_validate_config(&c) == GM_INVALID);
    c.bpm = 39; assert(gm_validate_config(&c) == GM_INVALID);
    c.bpm = 350;
    c.algorithm = 0; assert(gm_validate_config(&c) == GM_INVALID);
    c.algorithm = 23; assert(gm_validate_config(&c) == GM_INVALID);
    c.algorithm = 22;
    c.engine_version = 0; assert(gm_validate_config(&c) == GM_INVALID);
    c.engine_version = 2; assert(gm_validate_config(&c) == GM_INVALID);
    c.engine_version = 1;
    c.aggression = 101; assert(gm_validate_config(&c) == GM_INVALID); c.aggression = 100;
    c.density = 101; assert(gm_validate_config(&c) == GM_INVALID); c.density = 100;
    c.chaos = 101; assert(gm_validate_config(&c) == GM_INVALID); c.chaos = 100;
    c.humanize = 101; assert(gm_validate_config(&c) == GM_INVALID); c.humanize = 100;
    c.grid = (gm_grid){9, 8, 32, 2}; assert(gm_validate_config(&c) == GM_LIMIT);
    c.grid = (gm_grid){4, 4, 16, 0}; assert(gm_validate_config(&c) == GM_INVALID);

    assert(gm_validate_timing((gm_timing){1, 8, 0, 0}) == GM_OK);
    assert(gm_validate_timing((gm_timing){64, 32, 0, 0}) == GM_OK);
    assert(gm_validate_timing((gm_timing){0, 16, 0, 0}) == GM_INVALID);
    assert(gm_validate_timing((gm_timing){65, 16, 0, 0}) == GM_LIMIT);
    assert(gm_validate_timing((gm_timing){16, 4, 0, 0}) == GM_INVALID);
}

int main(void)
{
    test_supported_grids();
    test_invalid_grids();
    test_config_and_timing();
    puts("grid: 72 grid combinations, bounds and portable validation passed");
    return 0;
}
