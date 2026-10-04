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

static void hh_start(hh_t *k, int ring, int32_t note, int32_t tone, int32_t decay, int32_t chr, int32_t vel, uint32_t seed)
{
    static const int32_t RAT[6] = {Q24(1.0), Q24(1.304), Q24(1.466), Q24(1.787), Q24(1.932), Q24(2.536)};
    int32_t n = qknob(chr), d = qknob(decay), t = qknob(tone), acc = qknob(vel);
    int32_t f0 = qnote(note), f = 2 * f0, cut, i;
    k->ring = (uint8_t)ring;
    k->edec = QONE - qm(qratio(Q24(0.003), -qm(d, Q24(84.0))), K44);
    k->cdec = QONE - qm(qratio(Q24(0.0025), -qm(d, Q24(36.0))), K44);
    k->env = qm(Q24(1.5) + (QONE - d) / 2, Q24(0.3) + qm(Q24(0.7), acc));
    cut = qlim(qratio(Q24(150.0 / FS), qm(t, Q24(72.0))), 0, Q24(16000.0 / FS));
    qsvf_set(&k->col, qtan_acc(cut), ring ? QONE : qdiv(QONE, Q24(3.0) + qm(Q24(3.0), t)));
    qsvf_set(&k->hpf, qtan_acc(cut), 2 * QONE);
    k->col.s1 = k->col.s2 = k->hpf.s1 = k->hpf.s2 = 0;
    n = qm(n, n);
    k->noisy = n;
    k->nf = qlim(qm(f0, Q24(16.0) + qm(Q24(16.0), QONE - n)), 0, QONE / 2);
    k->rng = seed;
    k->nclk = qrand(&k->rng);              /* the running module's state at a hit: sources run, the noise holds a value */
    k->nsmp = qrand(&k->rng) - QONE / 2;
    if (!ring) {
        uint64_t f2 = 2ull * qnote_inc(note);                      /* 2 f0, exact: the squares' edges depend on it */
        for (i = 0; i < 6; i++) {
            uint64_t fi = (f2 * (uint32_t)RAT[i]) >> 24;
            k->inc[i] = (uint32_t)(fi < 2143188679u ? fi : 2143188679u);   /* 0.499 * 2^32 */
            k->ph[i] = (uint32_t)qrand(&k->rng) << 8;
        }
    } else {
        int32_t r = qdiv(q48(f), Q24(0.01) + q48(f));
        static const int32_t HZ[6] = {Q24(200.0 / FS), Q24(7530.0 / FS), Q24(510.0 / FS), Q24(8075.0 / FS),
                                      Q24(730.0 / FS), Q24(10500.0 / FS)};
        for (i = 0; i < 6; i++) {
            k->osc[i].f = qlim(qm(HZ[i], r), 1, QONE / 4);
            k->osc[i].ph = qrand(&k->rng);
            k->osc[i].high = k->osc[i].ph >= QONE / 2;
            k->osc[i].next = i & 1 ? k->osc[i].ph : k->osc[i].high ? QONE : 0;   /* square even, saw odd */
            k->osc[i].lp = k->osc[i].hp = 0;
        }
    }
}

/* Plaits Oscillator, pw 0.5: square (sq = 1) or saw, polyBLEP, one sample late */
static int32_t qbosc_tick(qbosc_t *o, int sq)
{
    int32_t th = o->next, t;
    o->next = 0;
    o->ph += o->f;
    if (sq) {
        if (o->high ^ (o->ph >= QONE / 2)) {
            t = qdiv(o->ph - QONE / 2, o->f);
            th += qm(t, t) / 2;
            t = QONE - t;
            o->next -= qm(t, t) / 2;
            o->high = o->ph >= QONE / 2;
        }
        if (o->ph >= QONE) {
            o->ph -= QONE;
            t = qdiv(o->ph, o->f);
            th -= qm(t, t) / 2;
            t = QONE - t;
            o->next += qm(t, t) / 2;
            o->high = 0;
        }
        o->next += o->high ? QONE : 0;
        return 2 * th - QONE;
    }
    if (o->ph >= QONE) {
        o->ph -= QONE;
        t = qdiv(o->ph, o->f);
        th -= qm(t, t) / 2;
        t = QONE - t;
        o->next += qm(t, t) / 2;
    }
    o->next += o->ph;
    return 2 * th - QONE;
}

static int32_t hh_tick(hh_t *k)
{
    int32_t x, lp, bp, i;
    if (!k->ring) {
        int32_t s = 0;
        for (i = 0; i < 6; i++) {
            k->ph[i] += k->inc[i];
            s += (int32_t)(k->ph[i] >> 31);
        }
        x = qm(Q24(0.33), s * QONE) - QONE;
    } else {
        x = 0;
        for (i = 0; i < 6; i += 2)
            x += qm(qbosc_tick(&k->osc[i], 1), qbosc_tick(&k->osc[i + 1], 0));
    }
    qsvf_tick(&k->col, x, &lp, &bp);
    x = bp;
    k->nclk += k->nf;
    if (k->nclk >= QONE) {
        k->nclk -= QONE;
        k->nsmp = qrand(&k->rng) - QONE / 2;
    }
    x += qm(k->noisy, k->nsmp - x);
    k->env = qdecay(k->env, k->env > Q24(0.5) || !k->ring ? k->edec : k->cdec);
    if (!k->ring) {                                           /* SwingVCA */
        x = qm(x, x > 0 ? 4 * QONE : Q24(0.1));
        x = qm(qsat(x) + Q24(0.1), k->env);
    } else {
        x = qm(x, k->env);
    }
    return qsvf_tick(&k->hpf, x, &lp, &bp);
}

/* HMETL: TUNE DECAY TONE NOISE; six square oscillators, a resonant band-pass, a swing VCA (metallic, 808-like).
 * HNOIS: TUNE DECAY TONE NOISE; ring-modulated square x saw pairs, a two-stage envelope (noisy, trashy). */
#define HMETL_NOTE 60                                     /* MIDI note at TUNE 0 (262 Hz) */
#define HNOIS_NOTE 72                                     /* (523 Hz) */
static void hmetl_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    hh_start(&v->ms.hh, 0, HMETL_NOTE + p[0], p[2], p[1], p[3], v->vel, (uint32_t)v->rng);
}

static void hnois_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    hh_start(&v->ms.hh, 1, HNOIS_NOTE + p[0], p[2], p[1], p[3], v->vel, (uint32_t)v->rng);
}

static void hh_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t pk = 0;
    (void)t;
    for (i = 0; i < n; i++) {
        int32_t y = hh_tick(&v->ms.hh);
        dm_putq(v, out, i, y);
        pk = qmax(pk, qabs(y));
    }
    dm_qend(v, n, pk, FS / 50);
}

#define DM_HMETL_DEF {"HMETL", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 40), \
    PD("TONE", F_INT, 0, 127, 80), PD("NOISE", F_INT, 0, 127, 40), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, hmetl_trigger, hh_render}
#define DM_HNOIS_DEF {"HNOIS", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 40), \
    PD("TONE", F_INT, 0, 127, 80), PD("NOISE", F_INT, 0, 127, 40), PD("-", F_INT, 0, 0, 0), \
    PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, hnois_trigger, hh_render}
