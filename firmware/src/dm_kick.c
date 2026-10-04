/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
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

/* ---- M1-C: integer ports of Mutable Instruments' drum algorithms (Plaits, stmlib), MIT licence:
 * Copyright 2012-2016 Emilie Gillet (emilie.o.gillet@gmail.com).
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without
 * limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
 * Software, and to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions: The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE. */

static void kboom_start(kboom_t *k, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel)
{
    int32_t h = qknob(chr), morph = qknob(decay), timbre = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), f48 = q48(f0), drive, d, d2, pga, pgb, pre, sq;
    k->f0 = f0;
    k->q = qratio(Q24(1500.0 / 65536.0), qm(morph, Q24(80.0)));          /* q / 2^16 */
    k->scale = qdiv(Q24(0.001), f48);
    k->tone_f = qmin(qratio(4 * f0, qm(timbre, Q24(108.0))), QONE);
    k->leak = qm(Q24(0.08), timbre + Q24(0.25));
    k->pulse_h = Q24(3.0) + qm(Q24(7.0), acc);
    k->afm = qm(qmin(4 * h, QONE), Q24(1.7));
    k->sfm = qm(qlim(4 * h - QONE, 0, QONE), Q24(0.08));
    drive = qm(qmax(2 * h - QONE, 0), qmax(QONE - 16 * f48, 0));
    d = Q24(0.5) + drive / 2;
    d2 = qm(d, d);
    pga = d / 2;
    pgb = qm(qm(qm(d2, d2), d), Q24(24.0));
    pre = pga + qm(pgb - pga, d2);
    sq = qm(d, 2 * QONE - d);
    k->pre = pre;
    k->post = qdiv(QONE, qsoftclip(Q24(0.33) + qm(sq, pre - Q24(0.33))));
    k->pulse = k->pulse_lp = k->fm_lp = k->retrig = k->lp_out = k->tone_lp = 0;
    k->res.s1 = k->res.s2 = 0;
    k->n = 0;
    k->rem = FS / 1000;                                                    /* 1 ms */
    k->fmrem = 6 * FS / 1000;                                              /* 6 ms */
}

static int32_t kboom_tick(kboom_t *k)
{
    static const int32_t PDEC = Q24(1.0 - 1.0 / (0.2e-3 * FS)), PFILT = Q24(1.0 / (0.1e-3 * FS));
    static const int32_t RDEC = Q24(1.0 - 1.0 / (0.05 * FS));
    int32_t pulse = 0, fm_pulse = 0, punch, f, ro, lp, d;
    if (k->rem) {
        k->rem--;
        pulse = k->rem ? k->pulse_h : k->pulse_h - QONE;
        k->pulse = pulse;
    } else if (k->pulse) {
        k->pulse = qdecay(k->pulse, PDEC);
        pulse = k->pulse;
    }
    if (pulse || k->pulse_lp) {
        k->pulse_lp = pulse ? k->pulse_lp + qm(PFILT, pulse - k->pulse_lp) : 0;
        d = (pulse - k->pulse_lp) + qm(pulse, Q24(0.044));
        pulse = d >= 0 ? d : qm(Q24(0.7), qsat(2 * d));                     /* Diode */
    }
    if (k->fmrem) {
        k->fmrem--;
        fm_pulse = QONE;
        k->retrig = k->fmrem ? 0 : Q24(-0.8);
    } else {
        k->retrig = qdecay(k->retrig, RDEC);
    }
    k->fm_lp += qm(PFILT, fm_pulse - k->fm_lp);
    if (!(k->n++ & 1)) {                       /* the resonator follows the pitch every 2 samples (22 kHz): cost */
        d = 10 * k->lp_out - QONE;
        punch = Q24(0.7) + (d >= 0 ? d : qm(Q24(0.7), qsat(2 * d)));
        f = k->f0 + qm(k->f0, qm(k->fm_lp, k->afm) + qm(punch, k->sfm));
        f = qlim(f, 0, Q24(0.4));
        qsvf_set(&k->res, qtan_dirty(f), qinv12(4096 + (int32_t)(((int64_t)k->q * q48(f)) >> 20)));
    }
    qsvf_tick(&k->res, qm(pulse - qm(k->retrig, Q24(0.2)), k->scale), &lp, &ro);
    k->lp_out = lp;
    k->tone_lp += qm(k->tone_f, qm(pulse, k->leak) + ro - k->tone_lp);
    return qm(qsoftclip(qm(k->pre, k->tone_lp)), k->post);
}

/* KBOOM: TUNE DECAY TONE PUNCH (attack FM -> self FM -> drive); analog bass drum: a pulse into a resonator, overdrive */
#define KBOOM_NOTE 31                                     /* MIDI note at TUNE 0 (49 Hz) */
static void kboom_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    kboom_start(&v->ms.kb, KBOOM_NOTE + p[0], p[2], p[1], p[3], v->vel);
}

static void kboom_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t y = kboom_tick(&v->ms.kb);
        dm_putq(v, out, i, y);
        pk = qmax(pk, qabs(y));
    }
    dm_qend(v, n, pk, FS * 3 / 10);
}

#define DM_KBOOM_DEF {"KBOOM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TONE", F_INT, 0, 127, 64), PD("PUNCH", F_INT, 0, 127, 32), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, kboom_trigger, kboom_render, 2}
