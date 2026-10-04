/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The TR-808 metal source: six square waves (205.3 .. 800 Hz), made once per block for every hat,
 * cymbal and cowbell voice; each model shapes it with its own band-passes and envelopes. */
static uint32_t dblock;                              /* blocks rendered (drum_core.c drum_block_begin) */
static const uint32_t METAL_INC[6] = {HZ(205.3), HZ(304.4), HZ(369.6), HZ(522.7), HZ(540.0), HZ(800.0)};
static uint32_t metal_ph[6], metal_blk = 0xFFFFFFFFu;
static int32_t metal_buf[CTL], cow_buf[CTL];

static void metal_make(uint32_t n)
{
    uint32_t i, k;
    if (metal_blk == dblock)
        return;
    metal_blk = dblock;
    for (i = 0; i < n; i++) {
        int32_t s = 0, c;
        for (k = 0; k < 6u; k++) {
            metal_ph[k] += METAL_INC[k];
            s += (int32_t)(metal_ph[k] >> 31);
        }
        c = (int32_t)(metal_ph[4] >> 31) + (int32_t)(metal_ph[5] >> 31);
        metal_buf[i] = (2 * s - 6) * 5461;           /* -32766 .. 32766 */
        cow_buf[i] = (2 * c - 2) * 16383;
    }
}

/* hats: metal -> band-pass c[0] -> high-pass c[1] -> env[0] */
static void hat_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    metal_make(n);
    for (i = 0; i < n; i++) {
        int32_t bp, hp, b2, h2;
        dsvf_tick(&v->c[0], metal_buf[i], &v->f[0], &v->f[1], &bp, &hp);
        dsvf_tick(&v->c[1], bp, &v->f[2], &v->f[3], &b2, &h2);
        dm_put(v, out, i, mulq15(h2, env_q15(v->env[0])));
        env_step(v, 0);
    }
    if (v->env[0] < ENV_END)
        dm_end(v);
}

static void hat_start(track_t *t, dvoice_t *v, int32_t dec)
{
    const int16_t *p = &t->p[P_E0];
    v->env[0] = ENV1;
    v->k[0] = dk(dec);
    dsvf_coef(&v->c[0], CUT_7100 + p[0] * CUT_ST + (p[2] - 64) * 60, 30);
    dsvf_coef(&v->c[1], CUT_6000 + p[0] * CUT_ST + (p[3] - 64) * 60, 0);
}

/* HAT C / HAT O: TUNE (filters) DECAY TONE (band) BRIGHT (high-pass) */
static void hatc_trigger(track_t *t, dvoice_t *v) { hat_start(t, v, 30 + t->p[P_E1] * 30 / 127); }
static void hato_trigger(track_t *t, dvoice_t *v) { hat_start(t, v, 50 + t->p[P_E1] * 50 / 127); }

/* cymbal: band 3.44 kHz with env[0] (DECAY), band 7.1 kHz with env[1]; x[0], x[1] their gains */
static void cymb_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    metal_make(n);
    for (i = 0; i < n; i++) {
        int32_t b1, h1, b2, h2, x;
        dsvf_tick(&v->c[0], metal_buf[i], &v->f[0], &v->f[1], &b1, &h1);
        dsvf_tick(&v->c[1], metal_buf[i], &v->f[2], &v->f[3], &b2, &h2);
        x = mulq15(mulq15(b1, env_q15(v->env[0])), v->x[0]) + mulq15(mulq15(h2, env_q15(v->env[1])), v->x[1]);
        dm_put(v, out, i, x);
        env_step(v, 0);
        env_step(v, 1);
    }
    if (v->env[0] < ENV_END && v->env[1] < ENV_END)
        dm_end(v);
}

/* CYMBAL: TUNE DECAY TONE BAL */
static void cymb_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    int32_t sh = p[0] * CUT_ST + (p[2] - 64) * 60;
    v->env[0] = v->env[1] = ENV1;
    v->k[0] = dk(80 + p[1] * 24 / 127);
    v->k[1] = dk(75);
    v->x[0] = 32767 - p[3] * 200;
    v->x[1] = 7000 + p[3] * 200;
    dsvf_coef(&v->c[0], CUT_3440 + sh, 20);
    dsvf_coef(&v->c[1], CUT_7100 + sh, 20);
}

/* cowbell: 540 + 800 Hz squares -> band-pass ~880 Hz; a fast and a slow decay mixed (x[0], x[1]) */
static void cowb_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    metal_make(n);
    for (i = 0; i < n; i++) {
        int32_t bp, hp, e;
        dsvf_tick(&v->c[0], cow_buf[i], &v->f[0], &v->f[1], &bp, &hp);
        e = mulq15(env_q15(v->env[0]), v->x[0]) + mulq15(env_q15(v->env[1]), v->x[1]);
        dm_put(v, out, i, mulq15(bp, e));
        env_step(v, 0);
        env_step(v, 1);
    }
    if (v->env[0] < ENV_END && v->env[1] < ENV_END)
        dm_end(v);
}

/* COWBELL: TUNE (filter) DECAY TONE TAIL */
static void cowb_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->env[0] = v->env[1] = ENV1;
    v->k[0] = dk(14);
    v->k[1] = dk(40 + p[1] * 50 / 127);
    v->x[0] = 32767 - p[3] * 150;
    v->x[1] = 6000 + p[3] * 150;
    dsvf_coef(&v->c[0], CUT_880 + p[0] * CUT_ST + (p[2] - 64) * 40, 70);
}

#define DM_HATC_DEF {"HATC", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("BRITE", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, hatc_trigger, hat_render}
#define DM_HATO_DEF {"HATO", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("BRITE", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, hato_trigger, hat_render}
#define DM_CYMB_DEF {"CYMB", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("BAL", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, cymb_trigger, cymb_render}
#define DM_COWB_DEF {"COWB", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("TAIL", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, cowb_trigger, cowb_render}
