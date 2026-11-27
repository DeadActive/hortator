/* SPDX-License-Identifier: GPL-3.0-only */
/* Natural hit variation: a small, deterministic velocity change per hit, so a repeated step is not machine-identical.
 * Pure (no state): the key comes from where the hit is (seed, track, step count, loop, roll ordinal), so every PLAY gives
 * the same performance. VARY 0 returns the base velocity unchanged. The swing is +-8 velocity at 100. */
#ifndef GM_HIT_VARIATION_H
#define GM_HIT_VARIATION_H
#include <stdint.h>

static inline uint32_t gm_hit_key(uint32_t seed, uint32_t track, uint32_t step, uint32_t loop, uint32_t ordinal)
{
    uint32_t h = seed * 0x9E3779B1u + track;
    h = (h ^ (h >> 15)) * 0x85EBCA6Bu + step;
    h = (h ^ (h >> 13)) * 0xC2B2AE35u + loop;
    h = (h ^ (h >> 16)) * 0x27D4EB2Fu + ordinal;
    h ^= h >> 15;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    return h;
}

static inline uint32_t gm_hit_velocity(uint32_t base, uint32_t vary, uint32_t key)
{
    int32_t r, v;
    if (!vary)
        return base;
    if (vary > 100u)
        vary = 100u;
    r = (int32_t)((key >> 8) & 0xFFFFu) - 32768;           /* the middle bits: the best mixed */
    v = (int32_t)base + (int32_t)(((int64_t)r * (int32_t)vary * 8) / (32768 * 100));
    return (uint32_t)(v < 1 ? 1 : v > 127 ? 127 : v);
}
#endif
