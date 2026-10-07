/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* FILTER (spec docs/superpowers/specs/2026-10-08-filter-design.md): a filter on each track's sound, after RESON and
 * before DIST (fx.c mix_part): LP / BP / HP / NOTCH of the models' SVF (dm_dsp.c dsvf_coef: Simper's trapezoidal
 * SVF, as upstream's TRIO; NOTCH = LP + HP). Its own tick takes 64-bit products: a track's block reaches +-2^19 and
 * RESO 127 rings ~7x, beyond dsvf_tick's state clamp. The cutoff is set once per 32-sample block: CUT + ENV x the
 * hit's envelope x its velocity (+ the LFOs, which write CUT / RESO). After the last voice it rings out, then the
 * track goes idle (fring 0): a block that moves by <= 2 within +-64 (~ -66 dB) is the end. */
#define FLT_SMAX (1 << 27)                               /* state clamp (64-bit products: no overflow below it) */
#define FLT_OMAX 524287                                  /* the track's range: mix_part's level product fits */
#define FLT_ENV1 (1 << 24)

/* the DECAY knob -> TIME_MS_X10 / ENV_EXP (1 ms .. 10 s over 0..127): 22..105 = 5 ms .. 2 s, ~99 % back to CUT */
static uint32_t flt_dec_idx(int32_t v) { return 22u + (uint32_t)clamp(v, 0, 127) * 83u / 127u; }

static void trk_filter_hit(track_t *t, uint32_t vel)     /* drum_hit: the envelope from the top */
{
    t->fenv = FLT_ENV1;
    t->fvel = (uint8_t)(vel > 127u ? 127u : vel);
}

/* the cutoff this block, 0 .. 127 << 8: CUT + ENV (+-64 units at full) x the envelope x the velocity */
static int32_t flt_cut(const track_t *t)
{
    int32_t ev = mulq15(t->fenv >> 9, t->fvel * 258);  /* Q15 */
    int32_t off = (int32_t)(((int64_t)clamp(t->p[P_FENV], -64, 63) * 256 * ev) >> 15);
    return clamp((clamp(t->p[P_FCUT], 0, 127) << 8) + off, 0, 127 << 8);
}

/* track t's block b through its filter; 1 while it sounds or rings. TYPE OFF: 0, the state cleared (a later
 * TYPE starts from silence) */
static __attribute__((noinline)) uint32_t trk_filter(track_t *t, int32_t *b, uint32_t n)
{
    uint32_t i, ty = (uint32_t)clamp(t->p[P_FTYPE], 0, FT_N - 1), ring;
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    dsvf_t c;
    if (ty != t->fmode) {
        t->fs[0] = t->fs[1] = 0;
        t->fmode = (uint8_t)ty;
    }
    if (ty == FT_OFF) {
        t->fring = 0;
        return 0;
    }
    dsvf_coef(&c, flt_cut(t), clamp(t->p[P_FRESO], 0, 127));
    {
        int32_t s0 = t->fs[0], s1 = t->fs[1];
#define FLT_LOOP(Y)                                                                                      \
        for (i = 0; i < n; i++) {                                                                    \
            int32_t x = b[i], v3 = x - s1, y;                                                        \
            int32_t v1 = (int32_t)(((int64_t)c.a1 * s0 + (int64_t)c.a2 * v3 + 4096) >> 13);           \
            int32_t v2 = s1 + (int32_t)(((int64_t)c.a2 * s0 + (int64_t)c.a3 * v3 + 4096) >> 13);      \
            s0 = clamp(2 * v1 - s0, -FLT_SMAX, FLT_SMAX);                                            \
            s1 = clamp(2 * v2 - s1, -FLT_SMAX, FLT_SMAX);                                            \
            y = clamp((Y), -FLT_OMAX, FLT_OMAX);                                                     \
            b[i] = y;                                                                                \
            lo = y < lo ? y : lo;                                                                    \
            hi = y > hi ? y : hi;                                                                    \
        }
        if (ty == FT_LP)                                 /* one loop per TYPE: the outputs it needs (rounded: */
            FLT_LOOP(v2)                                 /* floors hold an offset) */
        else if (ty == FT_BP)
            FLT_LOOP(v1)
        else if (ty == FT_HP)
            FLT_LOOP(x - (int32_t)(((int64_t)c.k * v1) >> 12) - v2)
        else
            FLT_LOOP(x - (int32_t)(((int64_t)c.k * v1) >> 12))   /* NOTCH = LP + HP */
#undef FLT_LOOP
        t->fs[0] = s0;
        t->fs[1] = s1;
    }
    ring = hi - lo > 2 || hi > 64 || lo < -64;       /* rung out: a whole block still (moving <= 2) and quiet */
    if (!ring)                                       /* (the integer filter can hold a small offset for ever, */
        t->fs[0] = t->fs[1] = 0;                     /* a rounding dead band: cleared here) */
    t->fenv -= (int32_t)(((int64_t)t->fenv * ENV_EXP[flt_dec_idx(t->p[P_FDEC])]) >> 16);
    t->fring = (uint8_t)ring;
    return ring;
}

/* the track's inserts before DIST (fx.c mix_part): RESON (reson.c) while it rings or the track sounds with a MODEL,
 * then the FILTER while the track sounds or the filter rings (TYPE OFF: once more, to clear its state); both keep
 * the track on. Out of line, so mix_part stays straight: a condition there was laid out as a cold block jumping
 * back, which tests/target_budget.py reads as a loop around the whole mix */
static __attribute__((noinline)) uint32_t trk_inserts(track_t *t, int32_t *b, uint32_t n, uint32_t snd)
{
    if (t->rs.ring || (snd && t->p[P_RMODEL]))
        snd |= reson_block(t, b, n);
    if (t->p[P_FTYPE] ? snd || t->fring : t->fmode)
        snd |= trk_filter(t, b, n);
    return snd;
}
