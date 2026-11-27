/* SPDX-License-Identifier: GPL-3.0-only */
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMPLEMENTED_STYLES GM_ALGORITHM_COUNT
#define BIT(step) (UINT64_C(1) << (step))

static char *column(void)
{
    char *value = strtok(NULL, ",\n");
    assert(value);
    return value;
}

static uint64_t number(const char *value)
{
    char *end;
    uint64_t parsed = strtoull(value, &end, 0);
    assert(*value && !*end);
    return parsed;
}

static gm_config config(uint8_t id, gm_grid grid)
{
    gm_config c;
    assert(gm_factory_config(id, &c) == GM_OK);
    c.grid = grid;
    c.density = c.chaos = c.humanize = 0;
    return c;
}

static void test_fixture(void)
{
    FILE *fixture = fopen("tests/grind/golden/styles-v1.csv", "r");
    assert(fixture);
    char row[640];
    assert(fgets(row, sizeof row, fixture));
    unsigned rows = 0;
    while (fgets(row, sizeof row, fixture)) {
        unsigned id = (unsigned)number(strtok(row, ",\n"));
        const char *name = column();
        unsigned bars = (unsigned)number(column());
        uint8_t length[GM_TRACKS];
        uint64_t on[GM_TRACKS], rat1[GM_TRACKS];
        for (unsigned t = 0; t < GM_TRACKS; t++)
            length[t] = (uint8_t)number(column());
        for (unsigned t = 0; t < GM_TRACKS; t++)
            on[t] = number(column());
        for (unsigned t = 0; t < GM_TRACKS; t++)
            rat1[t] = number(column());
        const uint64_t forbidden = number(column());
        assert(!strtok(NULL, ",\n"));
        assert(id == ++rows && id <= GM_ALGORITHM_COUNT);
        if (id > IMPLEMENTED_STYLES)
            continue;

        assert(!strcmp(gm_style_name((uint8_t)id), name));
        gm_config c;
        assert(gm_factory_config((uint8_t)id, &c) == GM_OK);
        assert(c.algorithm == id && c.engine_version == GM_ENGINE_VERSION);
        assert(c.grid.num == 4u && c.grid.den == 4u && c.grid.division == 16u && c.grid.bars == bars);
        assert(c.aggression == 75u && c.density == 65u && c.chaos == 20u);
        assert(c.humanize == (id == 18u ? 0u : 25u));
        assert(gm_validate_config(&c) == GM_OK);
        gm_rule_masks masks;
        assert(gm_rules_build(&c, &masks) == GM_OK);
        for (unsigned t = 0; t < GM_TRACKS; t++) {
            assert(masks.length[t] == length[t]);
            assert(masks.anchors[t] == on[t]);
            assert(masks.rat1[t] == rat1[t]);
            assert((masks.anchors[t] & masks.optional[t]) == 0);
            assert((masks.rat1[t] & masks.optional_rat1[t]) == 0);
            assert((masks.optional_rat1[t] & ~masks.anchors[t]) == 0);
            assert((masks.accents[t] & ~masks.anchors[t]) == 0);
            assert(((masks.anchors[t] | masks.optional[t]) & forbidden) == 0);
        }
        assert(masks.forbidden == forbidden);
    }
    assert(!ferror(fixture));
    assert(fclose(fixture) == 0);
    assert(rows == GM_ALGORITHM_COUNT);
}

static void test_division(void)
{
    const gm_grid coarse = {4, 4, 8, 1};
    const gm_grid fine = {4, 4, 32, 1};
    gm_rule_masks masks;
    gm_config c = config(1, coarse);
    memset(&masks, 0xA5, sizeof masks);
    gm_rule_masks original;
    memcpy(&original, &masks, sizeof original);
    assert(gm_rules_build(&c, &masks) == GM_UNSUPPORTED);
    assert(memcmp(&masks, &original, sizeof masks) == 0);

    c = config(2, coarse);
    assert(gm_rules_build(&c, &masks) == GM_OK);
    assert(masks.anchors[GM_KICK] == UINT64_C(0xff));
    assert(masks.anchors[GM_SNARE] == masks.anchors[GM_KICK]);
    assert(masks.paired == 0);

    c = config(3, fine);
    assert(gm_rules_build(&c, &masks) == GM_OK);
    assert(masks.anchors[GM_KICK] == UINT64_C(0xffffffff));
    assert(masks.rat1[GM_KICK] == 0);

    c = config(4, fine);
    assert(gm_rules_build(&c, &masks) == GM_OK);
    assert(masks.anchors[GM_SNARE] == UINT64_C(0x33333333));
    assert(masks.accents[GM_SNARE] == UINT64_C(0x11111111));
    assert(masks.rat1[GM_SNARE] == 0);
    c.grid = coarse;
    assert(gm_rules_build(&c, &masks) == GM_UNSUPPORTED);

    c = config(5, (gm_grid){4, 4, 16, 1});
    assert(gm_rules_build(&c, &masks) == GM_OK);
    assert(masks.optional_rat1[GM_KICK] == BIT(15));
}

static void test_musical_positions(void)
{
    gm_rule_masks masks;
    gm_config c = config(6, (gm_grid){4, 4, 16, 1});
    assert(gm_rules_build(&c, &masks) == GM_OK);
    const uint64_t d_beat = BIT(0) | BIT(3) | BIT(6) | BIT(8) | BIT(11) | BIT(14);
    assert(masks.anchors[GM_KICK] == d_beat);

    c.grid = (gm_grid){5, 4, 16, 1};
    assert(gm_rules_build(&c, &masks) == GM_OK);
    assert(masks.anchors[GM_KICK] == (d_beat | BIT(16) | BIT(19)));
    assert(masks.anchors[GM_CRASH] == BIT(0));

    c.grid = (gm_grid){6, 8, 16, 1};
    assert(gm_rules_build(&c, &masks) == GM_OK);
    assert(masks.anchors[GM_KICK] == (d_beat & (BIT(12) - 1u)));
    assert(masks.anchors[GM_SNARE] == BIT(6));
}

static void test_length_and_pauses(void)
{
    gm_rule_masks masks;
    if (IMPLEMENTED_STYLES >= 16u) {
        gm_config c = config(11, (gm_grid){3, 4, 16, 1});
        assert(gm_rules_build(&c, &masks) == GM_UNSUPPORTED);
        c.grid.bars = 2;
        assert(gm_rules_build(&c, &masks) == GM_UNSUPPORTED);
        c.grid.division = 32;
        assert(gm_rules_build(&c, &masks) == GM_OK);
        assert(masks.length[GM_KICK] == 48);
        assert(masks.anchors[GM_CRASH] == (BIT(0) | BIT(24)));

        c = config(12, (gm_grid){3, 4, 32, 1});
        assert(gm_rules_build(&c, &masks) == GM_OK);
        assert(masks.forbidden == (UINT64_C(0x3f) << 12));
        assert(masks.anchors[GM_CRASH] == (BIT(0) | BIT(18)));

        c = config(13, (gm_grid){3, 4, 16, 1});
        assert(gm_rules_build(&c, &masks) == GM_UNSUPPORTED);
        c.grid.division = 32;
        assert(gm_rules_build(&c, &masks) == GM_OK);
        assert(masks.forbidden & BIT(23));
        for (unsigned t = 0; t < GM_TRACKS; t++)
            assert(!masks.optional[t] && !(masks.anchors[t] & masks.forbidden));
    }
    if (IMPLEMENTED_STYLES == GM_ALGORITHM_COUNT) {
        gm_config c = config(19, (gm_grid){7, 8, 16, 2});
        assert(gm_rules_build(&c, &masks) == GM_OK);
        assert(masks.length[GM_KICK] == 28);
        assert(masks.length[GM_SNARE] == 21);
        assert(masks.length[GM_CHINA] == 27);
        for (unsigned t = 0; t < GM_TRACKS; t++)
            assert((masks.anchors[t] >> masks.length[t]) == 0);
    }
}

static void test_segmented_alternation(void)
{
    gm_config c;
    gm_rule_masks masks, untouched;
    memset(&untouched, 0xa5, sizeof untouched);
    for (uint8_t id = 11; id <= 12; ++id) {
        assert(gm_factory_config(id, &c) == GM_OK);
        c.grid.num = 3;
        masks = untouched;
        assert(gm_rules_build(&c, &masks) == GM_UNSUPPORTED);
        assert(memcmp(&masks, &untouched, sizeof masks) == 0);
        c.grid.num = 4; c.grid.division = 8;
        assert(gm_rules_build(&c, &masks) == GM_UNSUPPORTED);
        assert(memcmp(&masks, &untouched, sizeof masks) == 0);
        c.grid.division = 16;
        assert(gm_rules_build(&c, &masks) == GM_OK);
        assert((masks.anchors[GM_KICK] & masks.anchors[GM_SNARE]) ==
               (id == 12 ? BIT(12) : 0));
        c.grid.num = 3; c.grid.division = 32;
        assert(gm_rules_build(&c, &masks) == GM_OK);
        assert((masks.anchors[GM_KICK] & masks.anchors[GM_SNARE]) ==
               (id == 12 ? BIT(18) : 0));
        if (id == 12) assert(masks.forbidden == (UINT64_C(0x3f) << 12));
    }
}

static void test_invalid(void)
{
    gm_rule_masks masks;
    gm_config c = config(1, (gm_grid){4, 4, 16, 1});
    assert(!gm_style_name(0) && !gm_style_name(GM_ALGORITHM_COUNT + 1u));
    assert(gm_factory_config(0, &c) == GM_INVALID);
    assert(gm_factory_config(1, NULL) == GM_INVALID);
    assert(gm_rules_build(NULL, &masks) == GM_INVALID);
    assert(gm_rules_build(&c, NULL) == GM_INVALID);
}

int main(void)
{
    test_fixture();
    test_division();
    test_musical_positions();
    test_length_and_pauses();
    test_segmented_alternation();
    test_invalid();
    puts("rules: ok");
    return 0;
}
