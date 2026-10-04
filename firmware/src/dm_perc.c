/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Tuned percussion: tom, conga, claves (body_render, dm_kick.c) and the rimshot. */

/* TOM: TUNE DECAY NOISE DROP */
static void tom_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(120), p[0]);
    v->x[0] = (int32_t)(v->inc[0] / 256u * (uint32_t)p[3]);      /* starts up to ~50 % higher */
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(40 + p[1] * 50 / 127);
    v->k[1] = dk(40);
    v->k[2] = dk(45 + p[1] * 50 / 127);
    v->x[1] = p[2] * 60;
    v->x[2] = 0;
    v->x[3] = 2;
    dsvf_coef(&v->c[0], CUT_600, 0);
}

/* CONGA: TUNE DECAY SLAP DROP */
static void conga_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(310), p[0]);
    v->x[0] = (int32_t)(v->inc[0] / 512u * (uint32_t)p[3]);
    v->inc[1] = v->inc[0] * 2u;
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(30 + p[1] * 45 / 127);
    v->k[1] = dk(30);
    v->k[2] = dk(8);
    v->x[1] = p[2] * 200;
    v->x[2] = 0;
    v->x[3] = 1;
}

/* CLAVE: TUNE DECAY CLICK DROP */
static void clave_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(2500), p[0]);
    v->x[0] = (int32_t)(v->inc[0] / 256u * (uint32_t)p[3]);
    v->inc[1] = v->inc[0] * 2u;
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(31 + p[1] * 25 / 127);
    v->k[1] = dk(5);
    v->k[2] = dk(0);
    v->x[1] = p[2] * 150;
    v->x[2] = 0;
    v->x[3] = 1;
}

/* rimshot: 1667 + 455 Hz under one decay, driven, high-passed at ~600 Hz; x[0], x[1] tone gains, x[2] drive */
static void rim_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t bp, hp, x;
        x = mulq15(sine_i(v->ph[0]), v->x[0]) + mulq15(sine_i(v->ph[1]), v->x[1]);
        v->ph[0] += v->inc[0];
        v->ph[1] += v->inc[1];
        x = mulq15(x, env_q15(v->env[0]));
        x = softclip(x + ((x * v->x[2]) >> 4));
        dsvf_tick(&v->c[0], x, &v->f[0], &v->f[1], &bp, &hp);
        dm_put(v, out, i, hp);
        env_step(v, 0);
    }
    if (v->env[0] < ENV_END)
        dm_end(v);
}

/* RIM: TUNE DECAY TONE DRIVE */
static void rim_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(1667), p[0]);
    v->inc[1] = inc_tune(HZ(455), p[0]);
    v->env[0] = ENV1;
    v->k[0] = dk(13 + p[1] * 20 / 127);
    v->x[0] = 32767 - p[2] * 150;
    v->x[1] = 8000 + p[2] * 150;
    v->x[2] = p[3];
    dsvf_coef(&v->c[0], CUT_600, 0);
}

#define DM_TOM_DEF {"TOM", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("NOISE", F_INT, 0, 127, 30), PD("DROP", F_INT, 0, 127, 50), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, tom_trigger, body_render}
#define DM_CONGA_DEF {"CONGA", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("SLAP", F_INT, 0, 127, 60), PD("DROP", F_INT, 0, 127, 30), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, conga_trigger, body_render}
#define DM_RIM_DEF {"RIM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("DRIVE", F_INT, 0, 127, 40), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, rim_trigger, rim_render}
#define DM_CLAVE_DEF {"CLAVE", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("CLICK", F_INT, 0, 127, 30), PD("DROP", F_INT, 0, 127, 20), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, clave_trigger, body_render}
