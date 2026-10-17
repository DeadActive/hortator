/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* XTTRX (tom) and XCTRX (conga): one voice, two measured parameter sets, built from measurements of a classic digital
 * drum machine's tom and conga (md-re: renders of an emulator, knob by knob; only the measured laws are here, no code
 * or data of it). A fixed click at the hit (a fast fall, a linear return, smoothed; DAMP raises it), then a sine
 * (phase 0, ~1.3 / 0.7 ms after the click) whose pitch is TUNE + RAMP's extra Hz (the same Hz at any pitch) falling
 * over RDEC, on an exponential decay with a truncation floor (the tail cut to zero as measured); DAMP lowers the
 * body's start; DIST hard-clips the body after its decay at a falling ceiling, and while clipped the output sags
 * back towards and past zero at DTYP's rate; two one-pole high-passes (~17 Hz) on everything. 1 voice. */
typedef struct {
    uint32_t f64;                       /* the pitch at TUNE 0 (the machine's PTCH 64) */
    uint32_t tau0;                      /* DEC 64's time constant, samples */
    int32_t dstep;                      /* DEC's law per step, 1/100 of 1/16 semitone (24 x log2 of the ratio per 8) */
    int32_t amp;                        /* the body's level, Q16 (1.0 = the measured unit) */
    int32_t th0, ths;                   /* DIST's ceiling: th0 - ths x DIST / 100, Q16 */
    uint32_t sag;                       /* DTYP's sag at 127, per sample, Q16 x 65536 */
    int32_t sagp;                       /* its exponent, Q8 */
    uint32_t nb, frac;                  /* the body starts at sample nb, frac (Q8 of a sample) after the measured start */
    uint32_t n0, n1, n2, end;           /* the click's breakpoints (first sample of each segment) and its end */
    int32_t s0, r1, s1, r2, s2;         /* its segments: r = s0 t, r1 + s1 t, r2 + s2 t (Q24, t in samples) */
    int32_t cls;                        /* its one-pole smoothing, Q16 */
    uint32_t hs1, hs2;                  /* the output high-passes, Q16 */
    int32_t gain;                       /* the output gain, Q8: the defaults level with TOM's / CONGA's */
} xt_mach_t;
#define XT_Q24(x) ((int32_t)((x) * 16777216.0))       /* constants only */
/* the click's segments from its breakpoints (cta, -ap) (t1, -a1) (t2, 0), t in samples */
#define XT_CLICK(cta, ap, t1, a1, t2) XT_Q24(-(ap) / (cta)), \
    XT_Q24(-(ap) - ((ap) - (a1)) / ((t1) - (cta)) * (cta)), XT_Q24(((ap) - (a1)) / ((t1) - (cta))), \
    XT_Q24(-(a1) * (t2) / ((t2) - (t1))), XT_Q24((a1) / ((t2) - (t1)))
static const xt_mach_t XT_M[2] = {
    {HZ(144.70), 4864u, 1103, 21647, 22308, 13533, 5025404u, 591, 60u, 241u, 3u, 34u, 53u, 117u,   /* TRX-XT */
     XT_CLICK(2.98116, 0.1544, 33.98346, 0.0687, 52.15266), 11747, 65362u, 65395u, 175},
    {HZ(172.91), 2412u, 808, 21555, 21404, 12504, 5580536u, 538, 31u, 189u, 2u, 31u, 32u, 96u,     /* TRX-XC */
     XT_CLICK(1.9404, 0.2143, 30.33198, -0.0227, 31.89312), 17377, 65381u, 65381u, 182},
};
#define XT_TAU_MAX 37485u                               /* 0.85 s: the measured floor then ends the voice by 5.4 s */
#define XT_FLOOR 97u                                    /* the tail's truncation floor, 4.5e-8 a sample (Q31) */
#define XT_RAMP_STEP 264568u                            /* RAMP: 345 Hz / 127 a step (phase increment) */

/* log2(x), Q16, x >= 1 (bit by bit; trigger time only) */
static int32_t xt_log2(uint32_t x)
{
    int32_t r = 0, b;
    uint64_t m;
    while (x >= 2u << (r >> 16))
        r += 65536;
    m = ((uint64_t)x << 30) >> (r >> 16);              /* the mantissa, [1, 2) Q30 */
    for (b = 32768; b; b >>= 1) {
        m = (m * m) >> 30;
        if (m >= 2ull << 30) {
            m >>= 1;
            r += b;
        }
    }
    return r;
}
/* (x / 127) ^ p, Q16 (x 0..127, p Q8; trigger time only) */
static uint32_t xt_powk(int32_t x, int32_t p8)
{
    int32_t d16;
    if (x <= 0)
        return 0;
    if (x >= 127)
        return 65536u;
    d16 = (int32_t)(((int64_t)(xt_log2((uint32_t)x) - xt_log2(127u)) * p8 * 192) >> 24);   /* 1/192 octave */
    return pow2_q16(d16 < -3072 ? -3072 : d16);
}

/* XTTRX / XCTRX: TUNE DECAY RAMP RDEC, DAMP DIST DTYP */
static void xt_trigger_m(track_t *t, dvoice_t *v, uint32_t mc)
{
    const int16_t *p = &t->p[P_E0];
    const xt_mach_t *m = &XT_M[mc];
    xttrx_t *x = &v->ms.xt;
    int32_t r = clamp(p[2], 0, 127), rd = clamp(p[3], 0, 127), dm = clamp(p[4], 0, 127), ds = clamp(p[5], 0, 127);
    uint64_t ts = ((uint64_t)m->tau0 * pow2_q16((clamp(p[1], 0, 127) - 64) * m->dstep / 100)) >> 16;
    x->mc = (uint8_t)mc;
    x->inc0 = inc_tune(m->f64, dm_tune(t));
    x->inc_r = XT_RAMP_STEP * (uint32_t)r;
    x->d_r = 0xFFFFFFFFu / (441000000u / (uint32_t)(420000 - 3175 * rd));   /* RDEC: 1 / tau = 42.0 - 0.3175 x RDEC
                                                         * per second (23.8 ms .. 0.6 s) */
    x->e_r = 0x7FFFFFFFu - (uint32_t)(((uint64_t)x->d_r * m->frac) >> 9);   /* Q31, as at the fractional start */
    x->e = 0x7FFFFFFFu - (uint32_t)dm * 8454660u;      /* DAMP: the body starts at 1 - DAMP / 254 */
    x->d_e = 0xFFFFFFFFu / (uint32_t)(ts > XT_TAU_MAX ? XT_TAU_MAX : ts);   /* DEC: tau0 x ratio ^ ((d - 64) / 8) */
    x->cl = 16384 + dm * 129;                           /* the click x (1 + DAMP / 127), Q14 */
    x->th = m->th0 - ds * m->ths / 100;                 /* DIST: the ceiling (no clip at 0) */
    x->sl = (uint32_t)(((uint64_t)m->sag * xt_powk(clamp(p[6], 0, 127), m->sagp)) >> 16);   /* DTYP: the sag */
    v->ph[0] = ((x->inc0 + x->inc_r) >> 8) * m->frac;   /* phase 0 at the measured (fractional) start */
}
static void xttrx_trigger(track_t *t, dvoice_t *v) { xt_trigger_m(t, v, 0); }
static void xctrx_trigger(track_t *t, dvoice_t *v) { xt_trigger_m(t, v, 1); }

static void xt_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    xttrx_t *x = &v->ms.xt;
    const xt_mach_t *m = &XT_M[x->mc];
    uint32_t i, tt = v->t;
    (void)t;
    for (i = 0; i < n; i++, tt++) {
        int32_t y = 0, d;
        if (tt < m->end) {                              /* the click */
            int32_t r = tt < m->n0 ? m->s0 * (int32_t)tt : tt < m->n1 ? m->r1 + m->s1 * (int32_t)tt :
                        tt < m->n2 ? m->r2 + m->s2 * (int32_t)tt : 0;
            x->cz += mulq16(r - x->cz, (uint32_t)m->cls);
            y = ((x->cz >> 8) * x->cl) >> 14;
        }
        if (tt >= m->nb && x->e) {                      /* the body */
            uint32_t inc = x->inc0 + (x->inc_r ? (uint32_t)(((uint64_t)x->inc_r * x->e_r) >> 31) : 0u);
            int32_t b = (mulq15(sine_i(v->ph[0]), (int32_t)(x->e >> 16)) * m->amp) >> 15;
            uint32_t f;
            if (b > x->th || b < -x->th) {              /* DIST: the ceiling, sagging by DTYP */
                int32_t c = x->th - (int32_t)(x->sag >> 16);
                b = b > 0 ? c : -c;
                x->sag += x->sl;
            } else
                x->sag = 0;
            y += b;
            v->ph[0] += inc;
            f = (uint32_t)(((uint64_t)x->e * x->d_e) >> 32) + XT_FLOOR;
            x->e = x->e > f ? x->e - f : 0;
            x->e_r -= (uint32_t)(((uint64_t)x->e_r * x->d_r) >> 32);
        }
        d = (y - x->px) << 8;                           /* the two high-passes (Q24) */
        x->px = y;
        x->hp1 = mulq16(x->hp1 + d, m->hs1);
        d = x->hp1 - x->hp1p;
        x->hp1p = x->hp1;
        x->hp2 = mulq16(x->hp2 + d, m->hs2);
        y = (mulq15(mulq15(x->hp2 >> 8, vel_gain(v)), VOICE_FS) * m->gain) >> 8;
        out[i] += y;
        v->last = y;
    }
    v->t = tt;
    if (tt >= m->end && tt >= m->nb && x->e < (ENV_END << 7))
        dm_end(v);
}

#define DM_XTTRX_DEF {"XTTRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("RAMP", F_INT, 0, 127, 0), PD("RDEC", F_INT, 0, 127, 64), PD("DAMP", F_INT, 0, 127, 0), \
    PD("DIST", F_INT, 0, 127, 0), PD("DTYP", F_INT, 0, 127, 0), PD("-", F_INT, 0, 0, 0)}, xttrx_trigger, xt_render}
#define DM_XCTRX_DEF {"XCTRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("RAMP", F_INT, 0, 127, 0), PD("RDEC", F_INT, 0, 127, 64), PD("DAMP", F_INT, 0, 127, 0), \
    PD("DIST", F_INT, 0, 127, 0), PD("DTYP", F_INT, 0, 127, 0), PD("-", F_INT, 0, 0, 0)}, xctrx_trigger, xt_render}
