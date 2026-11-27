/* SPDX-License-Identifier: GPL-3.0-only */
#include "common.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    FILE *reference = fopen("tests/grind/golden/patterns-v1.bin", "rb");
    assert(reference);
    static const uint32_t seeds[] = {0, 1, 42, UINT32_MAX};
    for (uint8_t id = 1; id <= GM_ALGORITHM_COUNT; ++id)
        for (unsigned i = 0; i < sizeof seeds / sizeof *seeds; ++i) {
            uint8_t actual[GM_ENCODED_SIZE], expected[GM_ENCODED_SIZE];
            gm_config c;
            gm_pattern pattern;
            assert(gm_factory_config(id, &c) == GM_OK);
            c.seed = seeds[i];
            assert(gm_generate(&c, NULL, &pattern) == GM_OK);
            assert(gm_encode(&c, &pattern, actual, sizeof actual) == sizeof actual);
            assert(fread(expected, 1, sizeof expected, reference) == sizeof expected);
            if (memcmp(actual, expected, sizeof actual)) {
                unsigned at = 0;
                while (actual[at] == expected[at])
                    ++at;
                fprintf(stderr, "golden: style %u seed %u offset %u: got %u expected %u\n",
                        id, (unsigned)c.seed, at, actual[at], expected[at]);
                return 1;
            }
        }
    assert(fgetc(reference) == EOF && !ferror(reference));
    assert(fclose(reference) == 0);
    puts("golden: 88 portable patterns match byte-for-byte");
    return 0;
}
