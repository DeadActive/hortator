/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef GRIND_H
#define GRIND_H

#include <stddef.h>
#include <stdint.h>

#define GM_TRACKS 8u
#define GM_STEPS 64u
#define GM_LOCK_BYTES 64u
#define GM_ENGINE_VERSION 1u
#define GM_ALGORITHM_COUNT 22u

#define GM_ON 1u
#define GM_ACC 2u
#define GM_SLIDE 4u
#define GM_TIE 8u
#define GM_RAT_SHIFT 4u
#define GM_FLAGS_MASK 63u
#define GM_ENCODED_SIZE (25u + GM_TRACKS * 4u + GM_TRACKS * GM_STEPS * 4u + GM_LOCK_BYTES)

typedef enum {
    GM_OK = 0, GM_INVALID, GM_LOCK_CONFLICT, GM_LIMIT, GM_STALE,
    GM_NO_CHANGE, GM_NO_SPACE, GM_IO, GM_UNSUPPORTED
} gm_result;

typedef enum {
    GM_KICK = 0, GM_SNARE, GM_HAT, GM_RIDE, GM_CRASH, GM_CHINA,
    GM_TOM_HIGH, GM_TOM_LOW
} gm_role;

enum {
    GM_DOMAIN_DENSITY = 1,
    GM_DOMAIN_CHAOS = 2,
    GM_DOMAIN_ACCENT = 3,
    GM_DOMAIN_MICRO = 4,
    GM_DOMAIN_ACCENT_HUMANIZE = 5,
    GM_DOMAIN_FILL = 6,
    GM_DOMAIN_CYMBAL = 7
};

typedef struct { uint8_t flags, cond; int8_t note, micro; } gm_cell;
typedef struct { uint8_t num, den, division, bars; } gm_grid;

typedef struct {
    uint32_t seed, mutation;
    uint16_t bpm, engine_version;
    uint8_t algorithm, aggression, density, chaos, humanize;
    gm_grid grid;
} gm_config;

/* Swing and source are adapter-mapped bytes. Division is a musical denominator. */
typedef struct { uint8_t length, division, swing, source; } gm_timing;

typedef struct {
    gm_timing timing[GM_TRACKS];
    gm_cell cell[GM_TRACKS][GM_STEPS];
    uint8_t lock[GM_LOCK_BYTES];
} gm_pattern;

typedef char gm_cell_size_is_four[sizeof(gm_cell) == 4u ? 1 : -1];
typedef char gm_lock_covers_cells[GM_LOCK_BYTES * 8u == GM_TRACKS * GM_STEPS ? 1 : -1];

typedef struct {
    uint64_t anchors[GM_TRACKS], optional[GM_TRACKS], rat1[GM_TRACKS];
    uint64_t optional_rat1[GM_TRACKS], accents[GM_TRACKS], forbidden, paired;
    uint8_t length[GM_TRACKS];
} gm_rule_masks;

const char *gm_style_name(uint8_t id);
gm_result gm_factory_config(uint8_t id, gm_config *out);
gm_result gm_factory_pattern(uint8_t id, gm_pattern *out);
gm_result gm_rules_build(const gm_config *c, gm_rule_masks *out);

/* Length is written only on success. Timing validation covers portable bounds. */
gm_result gm_grid_length(gm_grid grid, uint8_t *length);
gm_result gm_validate_config(const gm_config *c);
gm_result gm_validate_timing(gm_timing timing);

uint32_t gm_mix32(uint32_t x);
/* The key config is required. Random words do not depend on playback or knobs. */
uint32_t gm_random(const gm_config *c, uint8_t track, uint8_t step, uint32_t domain);
int gm_is_locked(const gm_pattern *p, unsigned track, unsigned step);
gm_result gm_set_lock(gm_pattern *p, unsigned track, unsigned step, int locked);
gm_result gm_generate(const gm_config *c, const gm_pattern *base, gm_pattern *out);
gm_result gm_validate(const gm_config *c, const gm_pattern *p);
/* The destination must not overlap the input. A return of 0 leaves it untouched. */
size_t gm_encode(const gm_config *c, const gm_pattern *p, uint8_t *out, size_t cap);
uint32_t gm_hash(const uint8_t *data, size_t length);

#endif
