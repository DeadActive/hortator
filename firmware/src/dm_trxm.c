/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* CHTRX, OHTRX, CYTRX: hats and a cymbal built from measurements of a classic digital drum machine's (md-re: renders
 * of an emulator, knob by knob; only the measured laws are here, no code or data of it). One metal bank for the three:
 * seven oscillators with integer periods in samples at 44.1 kHz (50 % squares of 114, 102, 86, 80, 58 and 52 samples,
 * a pulse of 57 samples 15 high, locked to the 114 one), so every partial, aliases included, is an exact harmonic.
 * They run free in the machine (other phases at every hit): here each starts at a random point of its cycle (from the
 * hit's seed); TUNE and the step's note move the whole bank by semitones (TUNE 0: the exact periods).
 *   hats: bank and white noise crossfaded (MTAL: (m / 127) ^ 0.7 each way) -> 2-pole high-pass (HPF: Q 1.82,
 *     3.99 .. 6.63 kHz) -> 2-pole low-pass (LPF: Q 3.24, 4.44 .. 10.25 kHz) -> a linear ramp 1 - t / 540 ms for the
 *     hold (DECAY), then an exponential decay (GAP); OH's longer hold runs the ramp through zero (the hat dips to
 *     silence and comes back inverted, as the machine does). The level rises 1.35 dB over HPF, falls 1 dB at LPF 0.
 *   cymbal: the bank alone, its levels from RICH (two measured tables: the balance, the level) -> high-pass ->
 *     low-pass (SIZE moves both up; PEAK raises the high-pass' Q) -> a bell at ~5.2 kHz (PEAK, its frequency, Q and
 *     gain measured against SIZE), decaying at a rate linear in DECAY; plus the bank through its own high-pass (TTUN)
 *     at TOP's gain with a faster decay (the band above ~6 kHz fades first).
 * The filters are trapezoidal SVFs (the same responses as the measured RBJ biquads), Q30 coefficients. */
#define TRXM_I(p) ((uint32_t)(4294967296.0 / (p)) + 1u)   /* 2^32 / p rounded up: p samples a cycle, exactly (its
                                                         * drift, < 100 per cycle, never reaches an edge in 6 s) */
static const uint32_t TRXM_INC[6] = {TRXM_I(114), TRXM_I(102), TRXM_I(86), TRXM_I(80), TRXM_I(58), TRXM_I(52)};
static const uint8_t TRXM_P[6] = {114, 102, 86, 80, 58, 52};
#define TRXM_PW ((uint32_t)(4294967296.0 * 15 / 57))   /* the pulse: high 15 samples of 57 (twice the 114 one's phase) */
#define TRXM_FQ(hz) ((int32_t)((hz) / FS * QONE))       /* Hz -> fc / FS, Q24 (constants only) */
#define TRXM_D_END 1947838u                             /* Q32 step of a 50 ms decay: from 5.5 s a voice fades out */
#define TRXM_GH 2912000.0                               /* the hats' level: the filters' units a machine unit (2^6 x
                                                         * the output's: the envelope Q30, out = y e / 2^36) */
#define TRXM_GC 4943                                    /* the cymbal's: x = the bank (Q12) x this / 2^8, out = y e / 2^34
                                                         * (the envelope Q28) */

/* (m / 127) ^ 0.7, Q15: MTAL's crossfade, each way */
static const int16_t TRXM_MT[128] = {
    0, 1104, 1793, 2381, 2912, 3404, 3868, 4309, 4731, 5137, 5531, 5912, 6283, 6646, 6999, 7346, 7685, 8018, 8346,
    8668, 8985, 9297, 9604, 9908, 10208, 10503, 10796, 11085, 11371, 11653, 11933, 12210, 12485, 12757, 13026, 13293,
    13558, 13820, 14081, 14339, 14595, 14850, 15102, 15353, 15602, 15850, 16096, 16340, 16582, 16823, 17063, 17301,
    17538, 17773, 18007, 18240, 18472, 18702, 18931, 19159, 19386, 19611, 19836, 20059, 20281, 20503, 20723, 20942,
    21161, 21378, 21594, 21810, 22025, 22238, 22451, 22663, 22874, 23084, 23294, 23502, 23710, 23917, 24124, 24329,
    24534, 24738, 24942, 25144, 25346, 25547, 25748, 25948, 26147, 26346, 26544, 26741, 26938, 27134, 27330, 27524,
    27719, 27913, 28106, 28298, 28490, 28682, 28873, 29063, 29253, 29442, 29631, 29819, 30007, 30195, 30381, 30568,
    30754, 30939, 31124, 31308, 31492, 31676, 31859, 32041, 32223, 32405, 32586, 32767};
/* the hats' oscillator levels, Q12: the five squares equal, 114 at -4.9 dB, the pulse at -6.2 dB (52 before it) */
static const int16_t TRXM_WH[7] = {2335, 4096, 4096, 4096, 4096, 4096, 2007};
/* RICH: the cymbal's oscillator levels at RICH 0, 8 .. 120, 127 (Q12 re the 102 square at 64; 114 102 86 80 58 52,
 * the pulse), and the bank's level there (dB x 100): measured */
static const int16_t TRXM_RT[17][7] = {
    {3408, 2302, 471, 1688, 897, 1503, 3109}, {3445, 3072, 856, 262, 672, 1016, 3109},
    {3486, 3695, 1831, 754, 504, 705, 3142},  {3527, 4096, 2527, 1524, 319, 516, 3109},
    {3252, 4096, 2867, 1962, 1053, 397, 2900}, {3109, 4096, 3109, 2277, 1556, 573, 2736},
    {2966, 4096, 3293, 2527, 1937, 1196, 2613}, {2867, 4096, 3408, 2707, 2253, 1667, 2527},
    {2769, 4096, 3527, 2867, 2499, 2077, 2441}, {2470, 4096, 3609, 3035, 2707, 2413, 2175},
    {2224, 4002, 3609, 3072, 2834, 2646, 1937}, {1872, 3486, 3330, 2900, 2736, 2646, 1630},
    {1651, 3072, 3142, 2769, 2646, 2646, 1438}, {1503, 2769, 2966, 2675, 2585, 2646, 1311},
    {1421, 2585, 2736, 2613, 2527, 2646, 1253}, {1405, 2441, 2556, 2499, 2470, 2646, 1225},
    {1421, 2413, 2499, 2441, 2413, 2646, 1253}};
static const int16_t TRXM_RG[17] = {-672, -568, -514, -434, -334, -220, -124, -75, 0, 32, 67, 162, 221, 260, 269, 280,
                                    255};
/* PEAK: the resonance's gain (dB x 100) at PEAK 0, 8 .. 120, 127: measured */
static const int16_t TRXM_PK[17] = {0, 230, 520, 790, 940, 1160, 1350, 1470, 1640, 1790, 1880, 2010, 2130, 2210, 2270,
                                    2350, 2370};

/* 1 / d (Q24, 1/8 <= d < 128): a 32-bit divide, one Newton step (no 64-bit divide) */
static int32_t trxm_inv(int32_t d)
{
    uint32_t s = 0;
    int32_t y;
    while ((d >> s) >= QONE)
        s++;
    d >>= s;
    y = (int32_t)((0xFFFFFFFFu / (uint32_t)(d >> 4)) << 12);
    y = qm(y, 2 * QONE - qm(d, y));
    return y >> s;
}
/* tan(pi f), Q24, f = fc / FS (Q24, < 0.5): the stmlib polynomial where it holds 1e-5 (pi f <= pi / 8), else from
 * tan(2x) = 2 t / (1 - t^2) and tan(pi / 2 - x) = 1 / tan(x) */
static int32_t trxm_tan(int32_t f)
{
    int32_t t, d, h = f <= QONE / 4 ? f : QONE / 2 - f;
    if (h > QONE / 8) {
        t = qtan_acc(h / 2);
        d = QONE - qm(t, t);
        t = qm(2 * t, trxm_inv(d));                     /* tan(pi h) */
    } else
        t = qtan_acc(h);
    return f <= QONE / 4 ? t : trxm_inv(t);
}
/* an SVF at f (fc / FS, Q24; up to 20 kHz), damping k = 1 / Q (Q24, <= 1.9) */
static void trxm_svf(trxmf_t *c, int32_t f, int32_t k)
{
    int32_t g = trxm_tan(clamp(f, TRXM_FQ(20.0), TRXM_FQ(20000.0))), a1 = trxm_inv(QONE + qm(g, g + k));
    c->a1 = a1 << 6;
    c->a2 = (int32_t)(((int64_t)g * a1) >> 18);
    c->g = g << 4;
    c->k = k << 6;
    c->s1 = c->s2 = 0;
}
/* one sample: the low-pass out, the band-pass and high-pass in *bp, *hp (v2 = s2 + a2 s1 + a3 v3 = s2 + g v1) */
static inline int32_t trxm_tick(trxmf_t *c, int32_t in, int32_t *bp, int32_t *hp)
{
    int32_t v3 = in - c->s2;
    int32_t v1 = (int32_t)(((int64_t)c->a1 * c->s1 + (int64_t)c->a2 * v3) >> 30);
    int32_t v2 = c->s2 + (int32_t)(((int64_t)c->g * v1) >> 28);
    c->s1 = 2 * v1 - c->s1;
    c->s2 = 2 * v2 - c->s2;
    *bp = v1;
    *hp = in - (int32_t)(((int64_t)c->k * v1) >> 30) - v2;
    return v2;
}
/* base x 2 ^ (d / 192 / 256): pow2_q16's 1/192 octave steps (0.36 %: too coarse for a resonant corner), the rest
 * linear (ln 2 / 192 / 256 a step) */
static int32_t trxm_q24(int32_t base, int32_t d)
{
    uint32_t r = pow2_q16(d >> 8);
    r = (uint32_t)(((uint64_t)r * (65536u + (((uint32_t)d & 255u) * 237u >> 8))) >> 16);
    return (int32_t)(((int64_t)base * r) >> 16);
}

/* an exponential decay's step, e x d / 2^32 (d: Q32), rounded up (a positive envelope never stalls above its end;
 * a negative one stalls under tau, ~-90 dB) */
static inline int32_t trxm_fall(int32_t e, uint32_t d) { return (int32_t)(((int64_t)e * d + 0xFFFFFFFFLL) >> 32); }

/* the bank: each oscillator at a random point of its cycle, TUNE's semitones; levels w (Q12) */
static void trxm_bank(track_t *t, dvoice_t *v, const int16_t *w)
{
    trxm_t *m = &v->ms.tm;
    int32_t s = clamp(dm_tune(t), -48, 48);
    uint32_t k;
    for (k = 0; k < 6u; k++) {
        m->ph[k] = (uint32_t)(((uint64_t)noise32(&v->rng) * TRXM_P[k]) >> 32) * TRXM_INC[k];
        m->inc[k] = s ? inc_tune(TRXM_INC[k], s) : TRXM_INC[k];   /* (inc_tune(x, 0) is 2e-4 sharp) */
    }
    m->wsum = 0;
    for (k = 0; k < 7u; k++) {
        m->w[k] = 2 * w[k];
        m->wsum += w[k];
    }
}

/* the bank for a block: sum w - 2 x the levels of those low (a square low its second half, the pulse after its 15
 * samples, at twice the 114 one's phase) */
static void trxm_bank_block(trxm_t *m, int32_t *x, uint32_t n)
{
    uint32_t i, p0 = m->ph[0], p1 = m->ph[1], p2 = m->ph[2], p3 = m->ph[3], p4 = m->ph[4], p5 = m->ph[5];
    const uint32_t i0 = m->inc[0], i1 = m->inc[1], i2 = m->inc[2], i3 = m->inc[3], i4 = m->inc[4], i5 = m->inc[5];
    const int32_t *w = m->w, ws = m->wsum, gb = m->gb, sh = m->ish;
    for (i = 0; i < n; i++) {
        int32_t b = ws - (w[6] & -(int32_t)((p0 << 1) >= TRXM_PW)) - (w[0] & ((int32_t)p0 >> 31)) -
                    (w[1] & ((int32_t)p1 >> 31)) - (w[2] & ((int32_t)p2 >> 31)) - (w[3] & ((int32_t)p3 >> 31)) -
                    (w[4] & ((int32_t)p4 >> 31)) - (w[5] & ((int32_t)p5 >> 31));
        x[i] = (b * gb) >> sh;
        p0 += i0, p1 += i1, p2 += i2, p3 += i3, p4 += i4, p5 += i5;
    }
    m->ph[0] = p0, m->ph[1] = p1, m->ph[2] = p2, m->ph[3] = p3, m->ph[4] = p4, m->ph[5] = p5;
}

static void trxm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    trxm_t *m = &v->ms.tm;
    uint32_t i, tt = v->t;
    int32_t e = m->e, et = m->et, xb[CTL];
    if (n > CTL) {                                      /* (the mix's blocks are CTL) */
        trxm_render(t, v, out, CTL);
        if (v->active)
            trxm_render(t, v, out + CTL, n - CTL);
        return;
    }
    if (tt >= LIFE_A)                                   /* the 6 s life: CY's longest decay fades out from 5.5 s */
        m->d = m->dt = TRXM_D_END;
    if (m->gb)                                          /* the bank */
        trxm_bank_block(m, xb, n);
    else
        for (i = 0; i < n; i++)
            xb[i] = 0;
    if (m->gn)                                          /* the noise */
        for (i = 0; i < n; i++)
            xb[i] += (dnoise(v) * m->gn) >> m->ish;
    {
        trxmf_t hpf = m->hp, lpf = m->lp, pkf = m->pk, tpf = m->tp;   /* (locals: out[] cannot alias them) */
        const int32_t pkg = m->pkg, ds = m->ds, ek = m->ek, osh = m->osh, vg = vel_gain(v), top = m->top;
        const uint32_t nh = m->nh, d = m->d, dt = m->dt;
        int32_t z = 0;
        for (i = 0; i < n; i++, tt++) {
            int32_t x = xb[i], y, bp, hp;
            trxm_tick(&hpf, x, &bp, &hp);
            y = trxm_tick(&lpf, hp, &bp, &z);
            if (pkg) {                                  /* CY: PEAK's bell */
                trxm_tick(&pkf, y, &bp, &z);
                y += (int32_t)(((int64_t)bp * pkg) >> 29);
            }
            if (tt < nh) {                              /* the hold: a ramp */
                if (tt)
                    e -= ds;
            } else if (tt == nh)
                e = ek;                                 /* the knee */
            else
                e -= trxm_fall(e, d);
            z = (int32_t)(((int64_t)y * e) >> osh);
            if (top) {                                  /* CY: the top */
                trxm_tick(&tpf, x, &bp, &hp);
                if (tt)
                    et -= trxm_fall(et, dt);
                z += (int32_t)(((int64_t)hp * et) >> osh);
            }
            z = mulq15(mulq15(clamp(z, -65535, 65535), vg), VOICE_FS);   /* dm_put */
            out[i] += z;
        }
        v->last = z;
        m->hp = hpf, m->lp = lpf, m->pk = pkf, m->tp = tpf;
    }
    v->t = tt;
    m->e = e;
    m->et = et;
    if (m->top && et < m->eend && et > -m->eend)
        m->top = 0;
    if (tt > m->nh && e < m->eend && e > -m->eend && !m->top)
        dm_end(v);
}

/* CHTRX / OHTRX: TUNE DECAY GAP HPF, LPF MTAL */
static void trxh_trigger(track_t *t, dvoice_t *v, int open)
{
    const int16_t *p = &t->p[P_E0];
    trxm_t *m = &v->ms.tm;
    int32_t dec = clamp(p[1], 0, 127), gap = clamp(p[2], 0, 127), hpf = clamp(p[3], 0, 127), lpf = clamp(p[4], 0, 127);
    int32_t mt = clamp(p[5], 0, 127), amp, ek;
    uint32_t th = open ? 1014300u + 277830u * (uint32_t)dec : 110250u + 64254u * (uint32_t)dec;   /* the hold,
                                                         * 1/1000 samples: 23 ms + 6.3 ms x DECAY (OH), 2.5 + 1.457 (CH) */
    uint32_t tau = open ? (uint32_t)trxm_q24(11572, gap * 161684 / 100)    /* GAP: 16.4 ms x 2 ^ (GAP / 30.4) (OH), */
                        : (uint32_t)trxm_q24(11642, gap * 80577 / 100);   /* 16.5 ms x 2 ^ (GAP / 61) (CH); Q4 samples */
    trxm_bank(t, v, TRXM_WH);
    m->gb = TRXM_MT[mt] * (int32_t)(0.01041 * 2 * TRXM_GH) >> 15;    /* bank 0.0104 (Q12 levels), noise 0.0184 */
    m->gn = TRXM_MT[127 - mt] * (int32_t)(0.01841 / 4 * TRXM_GH) >> 15;   /* (+-32768) a machine unit; >> 13 */
    m->ish = 13;
    m->osh = 36;
    trxm_svf(&m->hp, trxm_q24(TRXM_FQ(3993.0), hpf * 28369 / 100), Q24(1 / 1.82));   /* x 2 ^ (0.733 HPF / 127) */
    trxm_svf(&m->lp, trxm_q24(TRXM_FQ(4439.0), lpf * 46714 / 100), Q24(1 / 3.24));   /* x 2 ^ (1.207 LPF / 127) */
    m->pkg = 0;
    m->top = 0;
    amp = trxm_q24(1 << 30, (135 * hpf - 100 * (127 - lpf)) * 6428 / 10000);   /* + 1.35 dB x HPF / 127,
                                                         * - 1 dB x (1 - LPF / 127) */
    ek = (int32_t)(((int64_t)th * 2954951) >> 16);      /* th / 540 ms, Q30 */
    m->nh = (th + 999u) / 1000u;
    m->ek = amp - (int32_t)(((int64_t)amp * ek) >> 30);
    m->e = amp;
    m->ds = amp / 23814;                                /* 1 / 540 ms a sample */
    m->d = (0xFFFFFFFFu / (tau + 8u)) << 4;             /* 1 - e ^ (-1 / tau) ~ 1 / (tau + 1/2), Q32 */
    m->dt = 0;
    m->et = 0;
    m->eend = amp >> 14;                                /* -84 dB re the start: the end */
}
static void chtrx_trigger(track_t *t, dvoice_t *v) { trxh_trigger(t, v, 0); }
static void ohtrx_trigger(track_t *t, dvoice_t *v) { trxh_trigger(t, v, 1); }

/* CYTRX: TUNE DECAY RICH TOP, TTUN SIZE PEAK */
static void cytrx_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    trxm_t *m = &v->ms.tm;
    int32_t dec = clamp(p[1], 0, 127), rich = clamp(p[2], 0, 127), top = clamp(p[3], 0, 127), ttun = clamp(p[4], 0, 127);
    int32_t sz = clamp(p[5], 0, 127), pk = clamp(p[6], 0, 127), w[7] = {0}, j, fr, gl, db, pf, qs, gs, pq, a, k;
    int16_t w16[7];
    uint32_t x;
    j = rich >= 120 ? 15 : rich >> 3;                   /* RICH: the tables, linear between their points (fr Q8) */
    fr = j == 15 ? (rich - 120) * 256 / 7 : (rich & 7) << 5;
    gl = (int32_t)pow2_q16((TRXM_RG[j] + (((TRXM_RG[j + 1] - TRXM_RG[j]) * fr) >> 8)) * 3189 / 10000);   /* dB -> Q16 */
    for (x = 0; x < 7u; x++) {
        w[x] = TRXM_RT[j][x] + (((TRXM_RT[j + 1][x] - TRXM_RT[j][x]) * fr) >> 8);
        w16[x] = (int16_t)((w[x] * gl) >> 16);
    }
    trxm_bank(t, v, w16);
    m->gb = TRXM_GC;
    m->gn = 0;
    m->ish = 8;
    m->osh = 34;
    j = pk >= 120 ? 15 : pk >> 3;
    fr = j == 15 ? (pk - 120) * 256 / 7 : (pk & 7) << 5;
    db = TRXM_PK[j] + (((TRXM_PK[j + 1] - TRXM_PK[j]) * fr) >> 8);   /* PEAK's gain, dB x 100 */
    if (sz < 64) {                                      /* the bell against SIZE: fc (Hz), Q at PEAK 127 and the gain's
                                                         * scale (x 1000), linear between 0, 64 and 127 */
        pf = 5558 - 328 * sz / 64;
        qs = 1840 + 450 * sz / 64;
        gs = 760 + 240 * sz / 64;
    } else {
        pf = 5230 + 169 * (sz - 64) / 63;
        qs = 2290 - 1370 * (sz - 64) / 63;
        gs = 1000 - 460 * (sz - 64) / 63;
    }
    pq = (1550 + 58 * pk / 10) * qs / 2290;             /* the bell's Q x 1000: (1.55 + 0.0058 PEAK) x qs / 2.29 */
    a = (int32_t)pow2_q16(db * gs / 1000 * 1595 / 10000);   /* its A = 10 ^ (dB x gs / 40), Q16 */
    k = trxm_inv((int32_t)(((int64_t)pq * 16777 * a) >> 16));   /* 1 / (Q A), Q24 */
    m->pkg = pk ? (int32_t)(((int64_t)k * ((((int64_t)a * a) >> 8) - QONE)) >> 19) : 0;   /* k (A^2 - 1), Q29 */
    trxm_svf(&m->pk, (int32_t)(((int64_t)pf * 1558281) >> 12), k);
    trxm_svf(&m->hp, trxm_q24(TRXM_FQ(767.0), sz * 61498 / 100),          /* x 2 ^ (1.589 SIZE / 127) */
             trxm_q24(Q24(1 / 2.058), -(db * 21348) / 1000));             /* Q 2.058 x the peak's 0.2615 power */
    trxm_svf(&m->lp, trxm_q24(TRXM_FQ(4497.0), sz * 39592 / 100), Q24(1 / 1.813));   /* x 2 ^ (1.023 SIZE / 127) */
    trxm_svf(&m->tp, trxm_q24(TRXM_FQ(7438.0), ttun * 46172 / 100), Q24(1.41421356));   /* x 2 ^ (1.193 TTUN / 127) */
    m->nh = 0;
    m->e = m->ek = 1 << 28;
    m->eend = 1 << 14;
    m->ds = 0;
    m->et = (int32_t)(((int64_t)(1 << 28) * (-315 + 4565 * top / 127) * pow2_q16((ttun - 64) * 4309 / 1000)) >> 28);   /*
                                                         * TOP: -0.077 + 1.1145 TOP / 127, x 17.2 dB per 127 TTUN steps */
    m->top = m->et != 0;
    m->d = (uint32_t)clamp(6617 - 4162 * dec / 100, 200, 6617) * 97392u / 1000u;   /* 1 / tau = 6.62 - 0.0416 DECAY
                                                         * per second (Q32 a sample) */
    m->dt = (uint32_t)clamp(14720 - 734 * dec / 10, 500, 14720) * 97392u / 1000u;  /* the top: 14.7 - 0.0734 DECAY */
}

#define DM_CHTRX_DEF {"CHTRX", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("GAP", F_INT, 0, 127, 64), PD("HPF", F_INT, 0, 127, 0), PD("LPF", F_INT, 0, 127, 127), \
    PD("MTAL", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, chtrx_trigger, trxm_render}
#define DM_OHTRX_DEF {"OHTRX", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("GAP", F_INT, 0, 127, 64), PD("HPF", F_INT, 0, 127, 0), PD("LPF", F_INT, 0, 127, 127), \
    PD("MTAL", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, ohtrx_trigger, trxm_render}
#define DM_CYTRX_DEF {"CYTRX", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("RICH", F_INT, 0, 127, 64), PD("TOP", F_INT, 0, 127, 64), PD("TTUN", F_INT, 0, 127, 64), \
    PD("SIZE", F_INT, 0, 127, 64), PD("PEAK", F_INT, 0, 127, 0), PD("-", F_INT, 0, 0, 0)}, cytrx_trigger, \
    trxm_render}
