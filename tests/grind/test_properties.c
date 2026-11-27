/* SPDX-License-Identifier: GPL-3.0-only */
#include "common.h"
#include "grind.h"

#include <stdio.h>
#include <string.h>

static uint32_t test_state = UINT32_C(0xab1954a1);

static uint32_t sample(void)
{
    test_state = test_state * UINT32_C(1664525) + UINT32_C(1013904223);
    return test_state >> 8;
}

static void check_case(int ok, const gm_config *c, unsigned run, int line)
{
    if (ok)
        return;
    fprintf(stderr, "properties:%d iteration=%u seed=%u style=%u grid=%u/%u div=%u bars=%u\n",
            line, run, (unsigned)c->seed, (unsigned)c->algorithm,
            (unsigned)c->grid.num, (unsigned)c->grid.den,
            (unsigned)c->grid.division, (unsigned)c->grid.bars);
    assert(ok);
}

#define CHECK(expression) check_case((expression), &c, run, __LINE__)

static void factory_defaults(gm_config *configs, gm_pattern *patterns)
{
    gm_config unchanged;
    gm_pattern actual;
    memset(&unchanged, 0xa5, sizeof unchanged);
    gm_config bad = unchanged;
    assert(gm_factory_config(0, &bad) == GM_INVALID);
    assert(gm_factory_config(23, &bad) == GM_INVALID);
    assert(!memcmp(&bad, &unchanged, sizeof bad));
    assert(gm_style_name(0) == NULL && gm_style_name(23) == NULL);

    for (uint8_t id = 1; id <= GM_ALGORITHM_COUNT; ++id) {
        gm_config *c = &configs[id - 1u];
        gm_pattern *p = &patterns[id - 1u];
        assert(gm_factory_config(id, c) == GM_OK && c->algorithm == id);
        assert(gm_style_name(id) && gm_style_name(id)[0]);
        for (uint8_t previous = 1; previous < id; ++previous)
            assert(strcmp(gm_style_name(previous), gm_style_name(id)) != 0);
        assert(gm_factory_pattern(id, p) == GM_OK);
        assert(gm_validate(c, p) == GM_OK);
        for (unsigned i = 0; i < 100u; ++i) {
            memset(&actual, (int)i, sizeof actual);
            assert(gm_generate(c, NULL, &actual) == GM_OK);
            assert(!memcmp(&actual, p, sizeof actual));
        }
    }
    memset(&actual, 0xa5, sizeof actual);
    gm_pattern canary = actual;
    assert(gm_factory_pattern(0, &actual) == GM_INVALID);
    assert(!memcmp(&actual, &canary, sizeof actual));
}

static void perturb(gm_config *c)
{
    static const uint8_t num[] = {3, 4, 5, 6, 7, 9};
    static const uint8_t den[] = {4, 4, 4, 8, 8, 8};
    static const uint8_t divisions[] = {8, 16, 32};
    static const uint8_t knobs[] = {0, 1, 50, 99, 100};
    unsigned meter = sample() % 6u;
    c->seed = sample();
    c->mutation = sample() & 1u ? UINT32_MAX : sample();
    c->grid = (gm_grid){num[meter], den[meter], divisions[sample() % 3u], (uint8_t)(1u + sample() % 4u)};
    c->aggression = knobs[sample() % 5u];
    c->density = knobs[sample() % 5u];
    c->chaos = knobs[sample() % 5u];
    c->humanize = knobs[sample() % 5u];
}

static void property_cases(const gm_config *configs, const gm_pattern *patterns)
{
    static const uint8_t bad_values[] = {0, 23, 255};
    gm_pattern canary;
    memset(&canary, 0xa5, sizeof canary);
    unsigned success = 0, rejected = 0, preserved = 0;

    for (unsigned run = 0; run < 10000u; ++run) {
        unsigned id = sample() % GM_ALGORITHM_COUNT;
        gm_config c = configs[id];
        gm_pattern base = patterns[id];
        unsigned track = sample() % GM_TRACKS;
        unsigned step = sample() % base.timing[track].length;
        int locked = (run % 3u) != 0u;
        if (locked)
            CHECK(gm_set_lock(&base, track, step, 1) == GM_OK);
        gm_pattern original = base;
        if (run % 4u != 0u)
            perturb(&c);
        int invalid_id = run % 23u == 0u;
        if (invalid_id)
            c.algorithm = bad_values[sample() % 3u];
        gm_pattern output = canary;
        gm_result result = gm_generate(&c, &base, &output);
        CHECK(!memcmp(&base, &original, sizeof base));
        if (invalid_id)
            CHECK(result == GM_INVALID);
        if (result != GM_OK) {
            CHECK(result == GM_INVALID || result == GM_LIMIT || result == GM_UNSUPPORTED ||
                  result == GM_LOCK_CONFLICT);
            CHECK(!memcmp(&output, &canary, sizeof output));
            ++rejected;
            continue;
        }

        ++success;
        CHECK(gm_validate(&c, &output) == GM_OK);
        gm_pattern repeated = canary;
        CHECK(gm_generate(&c, &base, &repeated) == GM_OK);
        CHECK(!memcmp(&repeated, &output, sizeof output));
        if (locked) {
            ++preserved;
            CHECK(gm_is_locked(&output, track, step));
            CHECK(!memcmp(&output.cell[track][step], &base.cell[track][step], sizeof(gm_cell)));
        }
        for (unsigned t = 0; t < GM_TRACKS; ++t) {
            CHECK(output.timing[t].length >= 1u && output.timing[t].length <= GM_STEPS);
            CHECK(output.timing[t].division == c.grid.division);
            for (unsigned s = output.timing[t].length; s < GM_STEPS; ++s) {
                static const gm_cell empty = {0};
                CHECK(!memcmp(&output.cell[t][s], &empty, sizeof empty));
                CHECK(!gm_is_locked(&output, t, s));
            }
        }
        output.cell[0][0].flags |= UINT8_C(0x80);
        CHECK(gm_validate(&c, &output) == GM_INVALID);
    }
    assert(success > 1000u && rejected > 1000u && preserved > 100u);
    printf("properties: 10000 cases, %u generated, %u rejected, %u successful with locks\n",
           success, rejected, preserved);
}

static void test_seven_four_pattern(void)
{
    gm_pattern p;
    gm_config c;
    assert(gm_factory_config(9, &c) == GM_OK);
    c.grid = (gm_grid){7, 4, 16, 1};
    static const unsigned kick[] = {0, 12, 20}, snare[] = {8, 16, 24};
    unsigned i, s;
    c.density = c.chaos = 50;
    assert(gm_generate(&c, NULL, &p) == GM_OK && gm_validate(&c, &p) == GM_OK);
    for (i = 0; i < GM_TRACKS; ++i) {
        assert(p.timing[i].length == 28u && p.timing[i].division == 16u);
        for (s = 28; s < GM_STEPS; ++s)
            assert(!p.cell[i][s].flags && !p.cell[i][s].cond && !p.cell[i][s].note && !p.cell[i][s].micro);
    }
    for (i = 0; i < 3u; ++i) {
        assert((p.cell[GM_KICK][kick[i]].flags & GM_ON) && (p.cell[GM_KICK][kick[i]].flags & GM_ACC));
        assert(p.cell[GM_SNARE][snare[i]].flags & GM_ON);
    }
    assert(p.cell[GM_CRASH][0].flags & GM_ON);
    for (s = 0; s < 28u; s += 2u)
        assert(p.cell[GM_RIDE][s].flags & GM_ON);
}

int main(void)
{
    test_seven_four_pattern();
    static gm_config configs[GM_ALGORITHM_COUNT];
    static gm_pattern patterns[GM_ALGORITHM_COUNT];
    factory_defaults(configs, patterns);
    property_cases(configs, patterns);
    return 0;
}
