/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
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
