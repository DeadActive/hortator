/* SPDX-License-Identifier: GPL-3.0-only */
#include "grind.h"

uint32_t gm_mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= UINT32_C(0x7feb352d);
    x ^= x >> 15;
    x *= UINT32_C(0x846ca68b);
    return x ^ (x >> 16);
}

uint32_t gm_random(const gm_config *c, uint8_t track, uint8_t step, uint32_t domain)
{
    uint32_t x = gm_mix32(c->seed ^ UINT32_C(0x9e3779b9));
    x = gm_mix32(x ^ c->mutation);
    x = gm_mix32(x ^ ((uint32_t)c->engine_version << 16) ^ c->algorithm);
    x = gm_mix32(x ^ ((uint32_t)track << 8) ^ step);
    return gm_mix32(x ^ domain);
}
