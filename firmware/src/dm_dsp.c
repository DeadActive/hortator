/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum model building blocks, all fixed point. Envelopes are Q24 (ENV1 = full) and fall by a
 * per-sample factor from DECAY_K (Q16; index 0..127 = 5 ms .. 4 s to -60 dB, exponential). */
#define ENV1 (1 << 24)
#define ENV_END (1 << 10)                    /* -84 dB: the voice ends */
#define HZ(f) ((uint32_t)((f) * 97391.548))  /* phase increment of f Hz (2^32 / 44100); constants only */
#define CUT_ST 299                           /* dsvf cutoff units (0..127 << 8 = 30 Hz..16 kHz) per semitone */
/* cutoffs of the frequencies the models use: 127 * ln(f / 30) / ln(16000 / 30) << 8 */
#define CUT_600 15511
#define CUT_880 17494
#define CUT_1000 18156
#define CUT_1200 19100
#define CUT_2000 21745
#define CUT_3440 24553
#define CUT_4000 25334
#define CUT_6000 27433
#define CUT_7000 28232
#define CUT_7100 28305

static inline uint32_t dk(int32_t idx) { return DECAY_K[clamp(idx, 0, 127)]; }
static inline int32_t env_q15(int32_t e) { return e >> 9; }
static inline int32_t vel_gain(const dvoice_t *v) { return v->vel * 258; }       /* 127 -> 32766 */
static inline int32_t dnoise(dvoice_t *v) { return (int32_t)noise32(&v->rng) >> 16; }
static inline void env_step(dvoice_t *v, uint32_t i) { v->env[i] = mulq16(v->env[i], v->k[i]); }

/* inc * 2^(semis / 12); at trigger time only (a divide in pow2_q16, a 64-bit product) */
static uint32_t inc_tune(uint32_t inc, int32_t semis)
{
    return (uint32_t)(((uint64_t)inc * pow2_q16(clamp(semis, -48, 48) * 16)) >> 16);
}

/* Simper SVF as tsvf (dsp.c), with the band-pass and high-pass outputs */
static void dsvf_coef(dsvf_t *c, int32_t cut, int32_t reso)       /* cut 0..127 << 8, reso 0..127 */
{
    tsvf_t t;
    tsvf_coef(&t, cut, reso);
    c->a1 = t.a1;
    c->a2 = t.a2;
    c->a3 = t.a3;
    c->k = 8192 - reso * 7600 / 127;          /* damping, Q12, as tsvf_coef */
}

static inline int32_t dsvf_tick(const dsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2, int32_t *bp, int32_t *hp)
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    *bp = v1;
    *hp = in - ((c->k * v1) >> 12) - v2;
    return v2;
}

/* add one model sample to the output: velocity, voice level; remembers it for the declick */
static inline void dm_put(dvoice_t *v, int32_t *out, uint32_t i, int32_t x)
{
    x = mulq15(mulq15(x, vel_gain(v)), VOICE_FS);
    out[i] += x;
    v->last = x;
}

static inline void dm_end(dvoice_t *v)
{
    v->active = 0;
    v->last = 0;
}

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
/* ---- Q24 toolkit (the M1-C models). 1.0 = 1 << 24, range +-128; products and quotients in 64 bits (the
 * pi32v2 multiplies, divides and shifts 64-bit natively, no helper calls). Q24(x) is for constants only: the
 * compiler folds it, no float reaches the firmware. */
#define QONE (1 << 24)
#define Q24(x) ((int32_t)((x) * 16777216.0 + ((x) < 0 ? -0.5 : 0.5)))
#ifdef DM_QCHECK                                    /* host test builds: count Q24 results that do not fit (tests assert 0) */
static uint32_t dm_qover;
static inline int32_t qfit(int64_t p)
{
    if (p > INT32_MAX || p < INT32_MIN)
        dm_qover++;
    return (int32_t)p;
}
#else
#define qfit(p) ((int32_t)(p))
#endif
static inline int32_t qm(int32_t a, int32_t b) { return qfit(((int64_t)a * b + (1 << 23)) >> 24); }
static inline int32_t qdiv(int32_t a, int32_t b) { return qfit(((int64_t)a << 24) / b); }
/* x * k for a decay (k < 1), rounded; where rounding stops the decay (below 0.5 / (1 - k) LSB, a tail that never
 * ends) it steps one LSB toward 0 instead */
static inline int32_t qdecay(int32_t x, int32_t k)
{
    int32_t y = qm(x, k);
    return y != x ? y : x > 0 ? x - 1 : x < 0 ? x + 1 : 0;
}
static inline int32_t qabs(int32_t x) { return x < 0 ? -x : x; }
static inline int32_t qmin(int32_t a, int32_t b) { return a < b ? a : b; }
static inline int32_t qmax(int32_t a, int32_t b) { return a > b ? a : b; }
static inline int32_t qlim(int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline int32_t qsat(int32_t x) { return qdiv(x, QONE + qabs(x)); }          /* x / (1 + |x|) */
static inline int32_t qsoftclip(int32_t x)                                          /* stmlib SoftClip */
{
    int32_t x2;
    if (x <= -3 * QONE)
        return -QONE;
    if (x >= 3 * QONE)
        return QONE;
    x2 = qm(x, x);
    return qdiv(qm(x, 27 * QONE + x2), 27 * QONE + 9 * x2);
}
/* p (0..127) as 0..1 */
static inline int32_t qknob(int32_t p) { return (int32_t)(((int64_t)qlim(p, 0, 127) << 24) / 127); }
/* v * 2^(st / 12), st in Q24 semitones; saturates at +-2^31 */
static int32_t qratio(int32_t v, int32_t st)
{
    int64_t oct = ((int64_t)st * 1398101) >> 24;          /* st / 12 in Q24 (1398101 = 2^24 / 12) */
    int32_t n = (int32_t)(oct >> 24), f = (int32_t)(oct & 0xFFFFFF);   /* floor octave, fraction Q24 */
    int64_t m = 22370;                                     /* 2^f = sum (f ln 2)^k / k!, k <= 5 (0.3 cent) */
    int64_t y;
    m = 161365 + ((m * f) >> 24);
    m = 931204 + ((m * f) >> 24);
    m = 4030332 + ((m * f) >> 24);
    m = 11629080 + ((m * f) >> 24);
    m = QONE + ((m * f) >> 24);
    y = ((int64_t)v * m) >> 24;
    if (n >= 0)
        y = n > 30 ? (y ? (y > 0 ? INT32_MAX : INT32_MIN) : 0) : y << n;
    else
        y = n < -40 ? 0 : y >> -n;
    return y > INT32_MAX ? INT32_MAX : y < INT32_MIN ? INT32_MIN : (int32_t)y;
}
/* stmlib tan approximations of pi f (f: Q24 normalized frequency) */
static inline int32_t qtan_dirty(int32_t f)
{
    int32_t x = qm(f, Q24(3.14159265358979)), x2 = qm(x, x);
    return qm(x, QONE + qm(Q24(0.3736), x2));
}
static inline int32_t qtan_fast(int32_t f)
{
    int32_t x = qm(f, Q24(3.14159265358979)), x2 = qm(x, x);
    return qm(x, QONE + qm(x2, Q24(0.3260) + qm(Q24(0.1823), x2)));
}
static inline int32_t qtan_acc(int32_t f)
{
    int32_t x = qm(f, Q24(3.14159265358979)), x2 = qm(x, x), p;
    p = Q24(9.5168091e-03);
    p = Q24(2.900525e-03) + qm(p, x2);
    p = Q24(5.33740603e-02) + qm(p, x2);
    p = Q24(1.333923995e-01) + qm(p, x2);
    p = Q24(3.333314036e-01) + qm(p, x2);
    return qm(x, QONE + qm(p, x2));
}
/* atan(x) in radians (Q24), x >= 0 (Abramowitz & Stegun 4.4.49 on [0, 1]; above 1: pi / 2 - atan(1 / x)) */
static int32_t qatan(int32_t x)
{
    int32_t inv = x > QONE, t = inv ? qdiv(QONE, x) : x, t2 = qm(t, t), p;
    p = Q24(0.0028662257);
    p = Q24(-0.0161657367) + qm(p, t2);
    p = Q24(0.0429096138) + qm(p, t2);
    p = Q24(-0.0752896400) + qm(p, t2);
    p = Q24(0.1065626393) + qm(p, t2);
    p = Q24(-0.1420889944) + qm(p, t2);
    p = Q24(0.1999355085) + qm(p, t2);
    p = Q24(-0.3333314528) + qm(p, t2);
    p = qm(t, QONE + qm(p, t2));
    return inv ? Q24(1.57079632679) - p : p;
}
/* the 44.1 kHz coefficient g of a stmlib filter Plaits sets with FREQUENCY_FAST at f48 (normalized at 48 kHz,
 * <= 0.5): the same analog cutoff in Hz, tan(atan(g48) * 48000 / 44100). Near Nyquist the 48 kHz normalized
 * value (or its clamp at 0.5) would land elsewhere in Hz at 44.1 kHz. Above pi / 4, tan(x) = 1 / tan(pi / 2 - x). */
static int32_t qtan48(int32_t f48)
{
    int32_t a = qm(qatan(qtan_fast(f48)), Q24(48000.0 / 44100.0 / 3.14159265358979)), b, t;   /* the 44.1 kHz f */
    b = a <= QONE / 4 ? a : QONE / 2 - a;
    t = qtan_acc(b / 2);                                   /* tan(pi b) = 2 t / (1 - t^2): the polynomial below pi / 8 */
    t = qdiv(2 * t, QONE - qm(t, t));
    return a <= QONE / 4 ? t : qdiv(QONE, t);
}
/* 1 / q with q in Q12 (q up to ~5e5): the damping of a resonator */
static inline int32_t qinv12(int32_t q12) { return (int32_t)((1LL << 36) / q12); }
/* stmlib Svf (trapezoidal, Simper); state type qsvf_t in dm_state.h */
static inline void qsvf_set(qsvf_t *f, int32_t g, int32_t r)
{
    f->g = g;
    f->r = r;
    /* 1 / (1 + r g + g^2); near Nyquist (g ~ 10) the sum passes 128: then a 4-bit shorter 32-bit divisor (native) */
    int64_t d = (int64_t)QONE + qm(r, g) + qm(g, g);
    f->h = d <= INT32_MAX ? (int32_t)((1LL << 48) / (int32_t)d) : (int32_t)((1LL << 44) / (int32_t)(d >> 4));
}
/* one sample: returns hp, *lp, *bp */
static inline int32_t qsvf_tick(qsvf_t *f, int32_t in, int32_t *lp, int32_t *bp)
{
    int32_t hp = qm(in - qm(f->r + f->g, f->s1) - f->s2, f->h), b, l;
    b = qm(f->g, hp) + f->s1;
    f->s1 = qm(f->g, hp) + b;
    l = qm(f->g, b) + f->s2;
    f->s2 = qm(f->g, b) + l;
    *lp = l;
    *bp = b;
    return hp;
}
/* stmlib OnePole (trapezoidal); state type qpole_t in dm_state.h */
static inline void qpole_set(qpole_t *p, int32_t g) { p->g = g; p->gi = qdiv(QONE, QONE + g); }
static inline int32_t qpole_lp(qpole_t *p, int32_t in)
{
    int32_t lp = qm(qm(p->g, in) + p->s, p->gi);
    p->s = qm(p->g, in - lp) + lp;
    return lp;
}
/* stmlib Random: the same LCG, per voice; Q24 in [0, 1) */
static inline int32_t qrand(uint32_t *st)
{
    *st = *st * 1664525u + 1013904223u;
    return (int32_t)(*st >> 8);
}
/* sqrt of x (Q24, >= 0) */
static int32_t qsqrt(int32_t x)
{
    uint64_t v = (uint64_t)(x < 0 ? 0 : x) << 24, r = 0, b = 1ULL << 62;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return (int32_t)r;
}
/* MIDI note -> normalized frequency at 44.1 kHz (Q24); its 48 kHz equivalent (f * 44100 / 48000) */
static inline int32_t qnote(int32_t note) { return qratio(Q24(8.17579891564 / 44100.0 * 64.0), note << 24) >> 6; }
static inline int32_t q48(int32_t f) { return qm(f, Q24(44100.0 / 48000.0)); }
/* per-sample coefficient c of Plaits (48 kHz) at 44.1 kHz: 1 - (1 - c)^(48000 / 44100) (literals) */
#define C44_75 Q24(0.7788451)
#define C44_50 Q24(0.5297289)
#define C44_10 Q24(0.1083469)
#define C44_05 Q24(0.0542996)
#define C44_04 Q24(0.0434595)
#define C44_005 Q24(0.005441)
#define C44_002 Q24(0.0021767)
#define K44 Q24(48000.0 / 44100.0)          /* small per-sample rates scale by this */
static inline int32_t qsine(int32_t ph) { return sine_i((uint32_t)ph << 8) * 512; }   /* sin(2 pi ph), any phase */

/* ---- output and lifetime of the M1-C models */
#define DM_FLOAT1 24576                    /* Q15 level of a reference sample of 1.0 */
#define DM_QEND 1057                       /* -84 dB re 1.0 (Q24): quiet */
#define DM_QQUIET 64                       /* quiet blocks in a row that end a voice (46 ms: > half a period of 11 Hz) */
#define LIFE_A ((uint32_t)FS * 11u / 2u)   /* M1-C voices fade out from 5.5 s ... */
#define LIFE_B ((uint32_t)FS * 6u)         /* ... and end at 6 s (their longest DECAY rings 9-15 s; the M1 limit is 6.5 s) */

/* add one M1-C sample y (Q24, 1.0 = a reference sample of 1.0): no velocity gain (the model applies its
 * accent itself), the lifetime fade, the declick memory */
static inline void dm_putq(dvoice_t *v, int32_t *out, uint32_t i, int32_t y)
{
    uint32_t t = v->t + i;
    int32_t x = clamp((int32_t)(((int64_t)y * DM_FLOAT1 + (1 << 23)) >> 24), -4 * 32768, 4 * 32768);   /* rounded */
    if (t >= LIFE_A)
        x = t >= LIFE_B ? 0 : (int32_t)((int64_t)x * (int32_t)(LIFE_B - t) / (int32_t)(LIFE_B - LIFE_A));
    x = (int32_t)(((int64_t)x * VOICE_FS + (1 << 14)) >> 15);
    out[i] += x;
    v->last = x;
}

/* after a block of n (peak pk): the voice ends at 6 s, or after guard samples once DM_QQUIET blocks in a row
 * were quiet (a low note spends whole blocks near its zero crossings; v->x[0] counts, unused by these models) */
static inline void dm_qend(dvoice_t *v, uint32_t n, int32_t pk, uint32_t guard)
{
    v->t += n;
    v->x[0] = pk < DM_QEND ? v->x[0] + 1 : 0;
    if (v->t >= LIFE_B || (v->t > guard && v->x[0] >= DM_QQUIET))
        dm_end(v);
}
