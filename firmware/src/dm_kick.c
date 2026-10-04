/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Pitched-body drums: a sine whose pitch falls from a sweep to its base, a transient (noise, a sine
 * blip or low-passed noise) and a soft-clip drive. 808 / 909 kick here; tom, conga, claves in dm_perc.c. */

/* inc[0] base, x[0] sweep amount (increment), env[0] amp, env[1] pitch, env[2] transient,
 * x[1] transient gain, x[2] drive, x[3] transient mode: 0 noise, 1 sine blip (inc[1]), 2 lp noise (c[0]) */
static void body_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t drv = v->x[2];
    (void)t;
    for (i = 0; i < n; i++) {
        uint32_t inc = v->inc[0] + (uint32_t)(v->x[0] >> 8) * (uint32_t)(v->env[1] >> 16);
        int32_t x = mulq15(sine_i(v->ph[0]), env_q15(v->env[0])), c;
        if (v->x[3] == 1)
            c = sine_i(v->ph[1]);
        else if (v->x[3] == 2) {
            int32_t bp, hp;
            c = dsvf_tick(&v->c[0], dnoise(v), &v->f[0], &v->f[1], &bp, &hp);
        } else
            c = dnoise(v);
        x += mulq15(mulq15(c, env_q15(v->env[2])), v->x[1]);
        v->ph[0] += inc;
        v->ph[1] += v->inc[1];
        if (drv)
            x = softclip(x + ((x * drv) >> 6));
        dm_put(v, out, i, x);
        env_step(v, 0);
        env_step(v, 1);
        env_step(v, 2);
    }
    if (v->env[0] < ENV_END && v->env[2] < ENV_END)
        dm_end(v);
}

/* 909 KICK: TUNE DECAY SWEEP CLICK, SWEEP-TIME DRIVE */
static void k909_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(52), p[0]);
    v->x[0] = (int32_t)inc_tune(HZ(4) * (uint32_t)p[2], p[0]);   /* up to ~500 Hz above */
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(50 + p[1] * 60 / 127);
    v->k[1] = dk(10 + p[4] * 50 / 127);
    v->k[2] = dk(0);
    v->x[1] = p[3] * 200;
    v->x[2] = p[5];
    v->x[3] = 0;
}

/* 808 KICK: TUNE DECAY TONE (blip) DIP (pitch dip), DRIVE */
static void k808_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(49), p[0]);
    v->x[0] = (int32_t)(v->inc[0] / 200u * (uint32_t)p[3]);      /* starts up to ~60 % higher */
    v->inc[1] = v->inc[0] * 24u;                                  /* ~1.2 kHz blip */
    v->env[0] = v->env[1] = v->env[2] = ENV1;
    v->k[0] = dk(60 + p[1] * 67 / 127);
    v->k[1] = dk(14);
    v->k[2] = dk(0);
    v->x[1] = p[2] * 150;
    v->x[2] = p[4];
    v->x[3] = 1;
}

#define DM_K808_DEF {"K808", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 80), \
    PD("TONE", F_INT, 0, 127, 40), PD("DIP", F_INT, 0, 127, 40), PD("DRIVE", F_PCT, 0, 127, 30), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, k808_trigger, body_render}
#define DM_K909_DEF {"K909", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("SWEEP", F_INT, 0, 127, 70), PD("CLICK", F_INT, 0, 127, 50), PD("SWPT", F_INT, 0, 127, 40), \
    PD("DRIVE", F_PCT, 0, 127, 20), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, k909_trigger, body_render}
