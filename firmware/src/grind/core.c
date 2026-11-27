/* SPDX-License-Identifier: GPL-3.0-only */
#include "grind.h"

enum { GM_PORTABLE_ONSET_LIMIT = GM_TRACKS * 2u };

static unsigned onsets(gm_cell cell)
{
    return (cell.flags & GM_ON) ? 1u + (cell.flags >> GM_RAT_SHIFT) : 0u;
}

static int valid_cell(gm_cell cell)
{
    return (cell.flags & ~GM_FLAGS_MASK) == 0u && cell.cond <= 58u &&
           cell.note >= -60 && cell.note <= 67 && cell.micro >= -50 && cell.micro <= 50;
}

static int empty_cell(gm_cell cell)
{
    return !cell.flags && !cell.cond && !cell.note && !cell.micro;
}

static int structurally_valid(const gm_pattern *p)
{
    for (unsigned t = 0; t < GM_TRACKS; ++t) {
        gm_timing timing = p->timing[t];
        if (gm_validate_timing(timing) != GM_OK || timing.swing > 100u || timing.source > 3u)
            return 0;
        for (unsigned s = 0; s < GM_STEPS; ++s) {
            gm_cell cell = p->cell[t][s];
            if (!valid_cell(cell) || (s >= timing.length &&
                                      (!empty_cell(cell) || gm_is_locked(p, t, s))))
                return 0;
        }
    }
    return 1;
}

static gm_result validate_pattern(const gm_config *c, const gm_rule_masks *rules, const gm_pattern *p)
{
    unsigned simultaneous[GM_STEPS] = {0}, sum_track_max = 0, polymetric = 0;
    for (unsigned t = 0; t < GM_TRACKS; ++t) {
        gm_timing timing = p->timing[t];
        if (timing.length != rules->length[t] || timing.division != c->grid.division ||
            timing.swing > 100u || timing.source != 0u)
            return GM_INVALID;
        polymetric |= timing.length != p->timing[0].length;
        unsigned track_max = 0;
        for (unsigned s = 0; s < GM_STEPS; ++s) {
            gm_cell cell = p->cell[t][s];
            unsigned locked = gm_is_locked(p, t, s);
            if (!valid_cell(cell) || (s >= timing.length && (!empty_cell(cell) || locked)))
                return GM_INVALID;
            unsigned count = onsets(cell);
            if (count && (rules->forbidden & (UINT64_C(1) << s)) && !locked)
                return GM_INVALID;
            simultaneous[s] += count;
            if (count > track_max)
                track_max = count;
        }
        sum_track_max += track_max;
    }
    /* Portable ceiling only. Sum track maxima if their cycle phases can differ. */
    if (polymetric)
        return sum_track_max > GM_PORTABLE_ONSET_LIMIT ? GM_LIMIT : GM_OK;
    for (unsigned s = 0; s < GM_STEPS; ++s)
        if (simultaneous[s] > GM_PORTABLE_ONSET_LIMIT)
            return GM_LIMIT;
    return GM_OK;
}

gm_result gm_validate(const gm_config *c, const gm_pattern *p)
{
    if (!p)
        return GM_INVALID;
    gm_rule_masks rules;
    gm_result result = gm_rules_build(c, &rules);
    return result == GM_OK ? validate_pattern(c, &rules, p) : result;
}

static int chance(const gm_config *c, unsigned track, unsigned step, unsigned domain, unsigned value)
{
    return gm_random(c, (uint8_t)track, (uint8_t)step, domain) % 100u < value;
}

static gm_cell generated_cell(const gm_config *c, const gm_rule_masks *rules, gm_timing timing,
                              unsigned track, unsigned step)
{
    gm_cell cell = {0};
    uint64_t bit = UINT64_C(1) << step;
    unsigned anchor = (rules->anchors[track] & bit) != 0u;
    unsigned density_track = (rules->paired & bit) && track == GM_SNARE ? GM_KICK : track;
    unsigned optional = (rules->optional[track] & bit) &&
                        chance(c, density_track, step, GM_DOMAIN_DENSITY, c->density);
    if (!anchor && !optional)
        return cell;

    unsigned required_accent = (rules->accents[track] & bit) != 0u;
    unsigned accent_step = c->algorithm == 18u ? 0u : step;
    unsigned accent_score = gm_random(c, (uint8_t)track, (uint8_t)accent_step, GM_DOMAIN_ACCENT) % 100u;
    /* Perturb the score, not the decision, so Aggression remains monotonic. */
    if (optional && chance(c, track, step, GM_DOMAIN_CHAOS, c->chaos))
        accent_score = 99u - accent_score;
    if (optional && chance(c, track, step, GM_DOMAIN_ACCENT_HUMANIZE, c->humanize / 4u))
        accent_score = 99u - accent_score;
    unsigned accent = accent_score < c->aggression;
    if (c->algorithm == 4u && track == GM_SNARE && timing.division == 32u)
        accent = required_accent;
    unsigned rat = ((rules->rat1[track] & bit) != 0u) ||
                   ((rules->optional_rat1[track] & bit) &&
                    chance(c, track, step, GM_DOMAIN_DENSITY, c->density));
    cell.flags = (uint8_t)(GM_ON | ((accent || required_accent) ? GM_ACC : 0u) | (rat << GM_RAT_SHIFT));

    unsigned span = 600u * c->bpm * timing.division / 240000u;
    if (span > 12u)
        span = 12u;
    span = span * c->humanize / 100u;
    span = span * (250u - timing.swing) / 250u;
    if (step == 0u) {
        cell.micro = (int8_t)(gm_random(c, (uint8_t)track, 0, GM_DOMAIN_MICRO) % (span + 1u));
    } else {
        cell.micro = (int8_t)((int)(gm_random(c, (uint8_t)track, (uint8_t)step, GM_DOMAIN_MICRO) %
                                  (2u * span + 1u)) - (int)span);
    }
    return cell;
}

gm_result gm_generate(const gm_config *c, const gm_pattern *base, gm_pattern *out)
{
    if (!out || base == out || (base && !structurally_valid(base)))
        return GM_INVALID;
    gm_rule_masks rules;
    gm_result result = gm_rules_build(c, &rules);
    if (result != GM_OK)
        return result;

    gm_pattern next = {0};
    for (unsigned t = 0; t < GM_TRACKS; ++t) {
        next.timing[t] = (gm_timing){rules.length[t], c->grid.division, 0, 0};
        if (base) {
            for (unsigned s = 0; s < GM_STEPS; ++s) {
                if (!gm_is_locked(base, t, s))
                    continue;
                gm_timing old = base->timing[t];
                if (s >= rules.length[t] || s >= old.length ||
                    old.division != c->grid.division || old.source != 0u)
                    return GM_LOCK_CONFLICT;
                next.timing[t].swing = old.swing;
                next.cell[t][s] = base->cell[t][s];
                (void)gm_set_lock(&next, t, s, 1);
            }
        }
        for (unsigned s = 0; s < rules.length[t]; ++s)
            if (!gm_is_locked(&next, t, s))
                next.cell[t][s] = generated_cell(c, &rules, next.timing[t], t, s);
    }
    result = validate_pattern(c, &rules, &next);
    if (result == GM_OK)
        *out = next;
    return result;
}

gm_result gm_factory_pattern(uint8_t id, gm_pattern *out)
{
    gm_config config;
    gm_result result = gm_factory_config(id, &config);
    return result == GM_OK ? gm_generate(&config, NULL, out) : result;
}
