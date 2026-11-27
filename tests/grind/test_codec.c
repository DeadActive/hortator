/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdio.h>
#include <string.h>
#include "common.h"

static const gm_config config = {
    .seed = UINT32_C(0x01234567), .mutation = UINT32_C(0x89abcdef),
    .bpm = 350, .engine_version = 1, .algorithm = 22,
    .aggression = 6, .density = 22, .chaos = 100, .humanize = 99, .grid = {7, 8, 16, 4}
};

static const gm_pattern pattern = {
    .timing = {
        {56, 16, 57, 0}, {42, 16, 60, 1}, {63, 8, 0, 0}, {17, 32, 1, 0},
        {1, 8, 0, 0}, {24, 16, 0, 1}, {64, 32, 75, 0}, {16, 16, 50, 0}
    },
    .cell = {
        [0][0] = {0xeb, 5, 67, -23},
        [0][63] = {GM_ACC, 9, -60, 50}, /* retained locked OFF beyond the active length */
        [7][63] = {0x3d, 0, 2, -50}
    },
    .lock = {[0] = 0x01, [7] = 0x80, [63] = 0x80}
};

/* Literal stream positions keep this fixture independent of the encoder's size and offsets. */
static const uint8_t expected[2169] = {
    0x47, 0x4d, 0x43, 0x31, 1, 0, 22, 0x5e, 1,
    0x67, 0x45, 0x23, 0x01, 0xef, 0xcd, 0xab, 0x89,
    6, 22, 100, 99, 7, 8, 16, 4,
    [25] = 56, 16, 57, 0, 42, 16, 60, 1, 63, 8, 0, 0, 17, 32, 1, 0,
    1, 8, 0, 0, 24, 16, 0, 1, 64, 32, 75, 0, 16, 16, 50, 0,
    [57] = 0x2b, 5, 0x43, 0xe9,
    [309] = 2, 9, 0xc4, 0x32,
    [2101] = 0x3d, 0, 2, 0xce,
    [2105] = 0x01, [2112] = 0x80, [2168] = 0x80
};

static void assert_unchanged(const uint8_t *buffer, size_t size)
{
    for (size_t i = 0; i < size; ++i)
        assert(buffer[i] == 0xa5);
}

static void test_capacity_and_bytes(void)
{
    uint8_t buffer[sizeof expected + 3u];
    memset(buffer, 0xa5, sizeof buffer);
    assert(GM_ENCODED_SIZE == sizeof expected);
    assert(gm_encode(&config, &pattern, buffer + 1, 0) == 0);
    assert(gm_encode(&config, &pattern, buffer + 1, sizeof expected - 1u) == 0);
    assert_unchanged(buffer, sizeof buffer);

    assert(gm_encode(&config, &pattern, buffer + 1, sizeof expected) == sizeof expected);
    assert(memcmp(buffer + 1, expected, sizeof expected) == 0);
    assert(buffer[0] == 0xa5 && buffer[sizeof buffer - 2u] == 0xa5 &&
           buffer[sizeof buffer - 1u] == 0xa5);
}

static void test_invalid_input(void)
{
    uint8_t buffer[sizeof expected];
    memset(buffer, 0xa5, sizeof buffer);
    assert(gm_encode(NULL, &pattern, buffer, sizeof buffer) == 0);
    assert(gm_encode(&config, NULL, buffer, sizeof buffer) == 0);
    assert(gm_encode(&config, &pattern, NULL, sizeof buffer) == 0);

    gm_config invalid = config;
    invalid.algorithm = 0;
    assert(gm_encode(&invalid, &pattern, buffer, sizeof buffer) == 0);
    invalid = config;
    invalid.bpm = 351;
    assert(gm_encode(&invalid, &pattern, buffer, sizeof buffer) == 0);
    invalid = config;
    invalid.engine_version = 2;
    assert(gm_encode(&invalid, &pattern, buffer, sizeof buffer) == 0);
    invalid = config;
    invalid.grid = (gm_grid){9, 8, 32, 2};
    assert(gm_encode(&invalid, &pattern, buffer, sizeof buffer) == 0);
    gm_pattern bad_timing = pattern;
    bad_timing.timing[7].length = 65;
    assert(gm_encode(&config, &bad_timing, buffer, sizeof buffer) == 0);
    bad_timing = pattern;
    bad_timing.timing[7].division = 7;
    assert(gm_encode(&config, &bad_timing, buffer, sizeof buffer) == 0);
    assert_unchanged(buffer, sizeof buffer);
}

int main(void)
{
    static const uint8_t hello[] = {0x68, 0x65, 0x6c, 0x6c, 0x6f};
    assert(gm_hash(NULL, 0) == UINT32_C(2166136261));
    assert(gm_hash(hello, sizeof hello) == UINT32_C(0x4f9f2cab));
    test_capacity_and_bytes();
    test_invalid_input();
    puts("codec: 2169 exact bytes, canaries, invalid input and FNV-1a passed");
    return 0;
}
