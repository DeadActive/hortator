/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Snares (two decaying tones + filtered noise) and claps (band-passed noise in sawtooth bursts + tail). */

/* ph/inc[0..1] tones, env[0..1] their decays, env[2] noise; x[0], x[1] tone gains, x[2] noise gain;
 * c[0] noise filter (808: band-pass; 909: low-pass, then c[1] high-pass); x[3] 1 = 909 noise path */
static void snare_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t bp, hp, lp, nz, x;
        x = mulq15(mulq15(sine_i(v->ph[0]), env_q15(v->env[0])), v->x[0]) +
            mulq15(mulq15(sine_i(v->ph[1]), env_q15(v->env[1])), v->x[1]);
        v->ph[0] += v->inc[0];
        v->ph[1] += v->inc[1];
        lp = dsvf_tick(&v->c[0], dnoise(v), &v->f[0], &v->f[1], &bp, &hp);
        if (v->x[3]) {
            dsvf_tick(&v->c[1], lp, &v->f[2], &v->f[3], &bp, &hp);
            nz = hp;
        } else {
            nz = bp;
        }
        x += mulq15(mulq15(nz, env_q15(v->env[2])), v->x[2]);
        dm_put(v, out, i, x);
        env_step(v, 0);
        env_step(v, 1);
        env_step(v, 2);
    }
    if (v->env[0] < ENV_END && v->env[1] < ENV_END && v->env[2] < ENV_END)
        dm_end(v);
}

/* 808 SNARE: TUNE DECAY TONE SNAPPY, NOISE-TONE */
static void s808_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(173), p[0]);
    v->inc[1] = inc_tune(HZ(336), p[0]);
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = v->k[1] = dk(33);                      /* ~29 ms, as measured on a TR-808 */
    v->k[2] = dk(40 + p[1] * 50 / 127);
    v->x[0] = 32767 - p[2] * 200;
    v->x[1] = 8000 + p[2] * 190;
    v->x[2] = p[3] * 258;
    v->x[3] = 0;
    dsvf_coef(&v->c[0], CUT_4000 + (p[4] - 64) * 80, 40);
}

/* 909 SNARE: TUNE DECAY BODY SNAPPY, NOISE-TONE */
static void s909_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(185), p[0]);
    v->inc[1] = inc_tune(HZ(330), p[0]);
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(38);
    v->k[1] = dk(30);
    v->k[2] = dk(35 + p[1] * 55 / 127);
    v->x[0] = p[2] * 200;
    v->x[1] = p[2] * 120;
    v->x[2] = p[3] * 258;
    v->x[3] = 1;
    dsvf_coef(&v->c[0], CUT_7000, 0);
    dsvf_coef(&v->c[1], CUT_2000 + (p[4] - 64) * 80, 0);
}

/* claps: t samples since the hit; ph[0] position in the burst, inc[0] burst length, ph[1] bursts done,
 * x[0] number of bursts, x[1] jitter (samples), x[2] tail gain, x[3] burst slope (Q15 per sample);
 * env[0] the last burst's decay (from the end of the bursts), env[1] the tail */
static void clap_next_burst(dvoice_t *v, uint32_t base)
{
    uint32_t j = (uint32_t)v->x[1];
    v->inc[0] = j ? base - j / 2u + (uint32_t)noise32(&v->rng) % (j + 1u) : base;
    v->x[3] = 32767 / (int32_t)v->inc[0];
    v->ph[0] = 0;
}

static void clap_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t bp, hp, e, x;
        dsvf_tick(&v->c[0], dnoise(v), &v->f[0], &v->f[1], &bp, &hp);
        if (v->ph[1] < (uint32_t)v->x[0]) {          /* sawtooth bursts */
            e = ((int32_t)v->inc[0] - (int32_t)v->ph[0]) * v->x[3];
            if (++v->ph[0] >= v->inc[0]) {
                v->ph[1]++;
                clap_next_burst(v, v->inc[2]);
            }
        } else {
            e = env_q15(v->env[0]);
            env_step(v, 0);
        }
        x = mulq15(bp, e) + mulq15(mulq15(bp, env_q15(v->env[1])), v->x[2]);
        dm_put(v, out, i, x);
        env_step(v, 1);
    }
    if (v->ph[1] >= (uint32_t)v->x[0] && v->env[0] < ENV_END && v->env[1] < ENV_END)
        dm_end(v);
}

static void clap_start(dvoice_t *v, uint32_t burst, uint32_t nb, uint32_t jitter)
{
    v->inc[2] = burst;
    v->x[0] = (int32_t)nb;
    v->x[1] = (int32_t)jitter;
    v->env[0] = v->env[1] = ENV1;
    v->k[0] = dk(26);                                /* the last burst: ~20 ms */
    clap_next_burst(v, burst);
}

/* 808 CLAP: - DECAY TONE TAIL */
static void c808_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    clap_start(v, 441, 3, 0);                        /* 3 x 10 ms */
    v->k[1] = dk(40 + p[1] * 40 / 127);
    v->x[2] = p[3] * 200;
    dsvf_coef(&v->c[0], CUT_1000 + (p[2] - 64) * 80 + p[0] * CUT_ST, 60);
}

/* 909 CLAP: TUNE DECAY TONE SPREAD */
static void c909_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    clap_start(v, 353, 3, (uint32_t)p[3] * 2u);      /* 3 x ~8 ms, randomised */
    v->k[1] = dk(35 + p[1] * 40 / 127);
    v->x[2] = 9000;
    dsvf_coef(&v->c[0], CUT_1200 + (p[2] - 64) * 80 + p[0] * CUT_ST, 70);
}

#define DM_S808_DEF {"S808", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("SNAP", F_INT, 0, 127, 90), PD("NTONE", F_INT, 0, 127, 64), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, s808_trigger, snare_render}
#define DM_S909_DEF {"S909", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("BODY", F_INT, 0, 127, 80), PD("SNAP", F_INT, 0, 127, 80), PD("NTONE", F_INT, 0, 127, 64), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, s909_trigger, snare_render}
#define DM_C808_DEF {"C808", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("TAIL", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, c808_trigger, clap_render}
#define DM_C909_DEF {"C909", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("SPRD", F_INT, 0, 127, 40), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, c909_trigger, clap_render}
