/* SPDX-License-Identifier: GPL-3.0-only */
#include <inttypes.h>
#include <stdio.h>
#include "common.h"

static void test_reference_vectors(void)
{
    char header[140];
    FILE *fixture = fopen("tests/grind/golden/rng-v1.txt", "r");
    assert(fixture && fgets(header, sizeof header, fixture) && header[0] == '#');

    gm_config c = {.engine_version = 1};
    unsigned algorithm, track, step, count = 0;
    uint32_t domain, expected;
    int values;
    while ((values = fscanf(fixture, "%" SCNx32 " %" SCNx32 " %u %u %u %" SCNu32 " %" SCNx32,
                            &c.seed, &c.mutation, &algorithm, &track, &step, &domain, &expected)) == 7) {
        c.algorithm = (uint8_t)algorithm;
        uint32_t actual = gm_random(&c, (uint8_t)track, (uint8_t)step, domain);
        if (actual != expected) {
            fprintf(stderr, "keyed reference row %u: %08" PRIx32 " != %08" PRIx32 "\n",
                    count + 1u, actual, expected);
            assert(actual == expected);
        }
        ++count;
    }
    assert(values == EOF && feof(fixture) && !ferror(fixture));
    assert(fclose(fixture) == 0);
    assert(count == 336u);
}

static void test_order_and_controls(void)
{
    gm_config c = {.seed = 1, .mutation = UINT32_MAX, .engine_version = 1, .algorithm = 22};
    assert(gm_random(&c, 7, 63, GM_DOMAIN_CYMBAL) == UINT32_C(0x8f3a5796));

    uint32_t forward[GM_TRACKS][GM_STEPS];
    for (uint8_t track = 0; track < GM_TRACKS; ++track)
        for (uint8_t step = 0; step < GM_STEPS; ++step)
            forward[track][step] = gm_random(&c, track, step, GM_DOMAIN_DENSITY);

    for (unsigned repeat = 0; repeat < 100u; ++repeat) {
        c.aggression = c.density = c.chaos = c.humanize = (uint8_t)repeat;
        c.bpm = (uint16_t)(40u + repeat);
        c.grid = (gm_grid){7, 8, 16, 1};
        for (unsigned track = GM_TRACKS; track > 0; --track)
            for (unsigned step = GM_STEPS; step > 0; --step)
                assert(gm_random(&c, (uint8_t)(track - 1u), (uint8_t)(step - 1u), GM_DOMAIN_DENSITY) ==
                       forward[track - 1u][step - 1u]);
    }
}

int main(void)
{
    assert(gm_mix32(0) == 0);
    assert(gm_mix32(1) == UINT32_C(0x688990c0));
    test_reference_vectors();
    test_order_and_controls();
    puts("rng: 336 independent keyed vectors and 100 reverse traversals passed");
    return 0;
}
