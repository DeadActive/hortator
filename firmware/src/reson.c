/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* RESON: a per-track resonator insert, before DIST (spec 2026-10-06-resonator-design.md). The track's sound
 * excites a comb: STRNG (Karplus-Strong: a delay line with a damping low-pass and a stiffness all-pass in the
 * loop; the all-pass' coefficient per note, RS_AP_MAX, so STRCT stretches the overtones alike at every pitch), PIPE (the same with inverted feedback: odd harmonics), CHORD (four STRNG lines tuned to a chord). One
 * fixed line per track (1352 samples), split into four for CHORD. Integer only. */
#define RS_LEN 1352u                                  /* STRNG down to C1 (1348.6 samples) */
#define RS_SEG 338u                                   /* CHORD: 4 lines, the lowest string C3 (337.2) */
#define RS_GMAX 32700                                 /* the loop gain's cap (0.998): never self-oscillating */
#define RS_FLOOR 32u                                  /* the line's level (int16, -60 dB of its full scale) under which,
                                                         * for a whole loop, the ring is over: it fades out (a power of 2:
                                                         * the block's OR of |v| is under it exactly when every |v| is) */
#define RS_FINE 6144                                  /* R.TUN at full DEPTH: 2 octaves, in 1/256 semitone */
#define RS_MAXTRK 4u                                  /* RESON on 4 tracks at most (CPU; 2 of them CHORD), by user decision */
static int16_t rs_buf[NTRK][RS_LEN] __attribute__((section(".pool")));

static const int8_t RS_CHORD_IV[RS_NCHORD][4] = {   /* semitones above TUNE, one note per line (N_RCHORD) */
    {0, 12, 24, 36}, {0, 7, 12, 19}, {0, 5, 12, 17}, {0, 4, 7, 12}, {0, 3, 7, 12}, {0, 2, 7, 12}, {0, 5, 7, 12},
    {0, 3, 6, 12}, {0, 4, 8, 12}, {0, 4, 7, 9}, {0, 3, 7, 9}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 4, 7, 10},
    {0, 3, 6, 10}, {0, 3, 6, 9}, {0, 5, 7, 10}, {0, 4, 7, 14}, {0, 5, 10, 15}, {0, 1, 2, 3},
};

/* p >> sh rounded toward zero: the loop's errors never feed energy one way (a floor's bias, held by a gain just
 * under 1, kept a DC offset or a small cycle alive for ever), so a ring always decays to an all-zero line */
static inline int32_t rs_mz(int32_t p, int32_t sh) { return (p + ((p >> 31) & ((1 << sh) - 1))) >> sh; }

static uint32_t rs_chord(const track_t *t) { return (uint32_t)clamp(t->p[P_RSTRCT], 0, 127) * RS_NCHORD / 128u; }

static uint32_t reson_chords(const track_t *except)   /* tracks with MODEL CHORD, other than except */
{
    uint32_t i, n = 0;
    for (i = 0; i < NTRK; i++)
        n += &trk[i] != except && trk[i].p[P_RMODEL] == RS_CHORD;
    return n;
}

static uint32_t reson_tracks(const track_t *except)   /* tracks with RESON on (any model), other than except */
{
    uint32_t i, n = 0;
    for (i = 0; i < NTRK; i++)
        n += &trk[i] != except && trk[i].p[P_RMODEL] != RS_OFF;
    return n;
}

static int32_t rs_period(int32_t nq)                  /* note in 1/256 semitone -> the loop period, Q8 samples */
{
    int32_t i, f;
    nq = clamp(nq, 0, 128 * 256 - 1);
    i = nq >> 4;
    f = nq & 15;
    return (int32_t)RS_PER[i] - (int32_t)(((RS_PER[i] - RS_PER[i + 1]) * (uint32_t)f) >> 4);
}

static void reson_clear(track_t *t)                   /* silence: the line and the loop filters */
{
    reson_t *r = &t->rs;
    memset(rs_buf[t - trk], 0, sizeof rs_buf[0]);
    memset(r->lp, 0, sizeof r->lp);
    memset(r->lr, 0, sizeof r->lr);
    memset(r->apx, 0, sizeof r->apx);
    memset(r->apy, 0, sizeof r->apy);
    r->quiet = r->peak = r->w = 0;                    /* w: a CHORD line is shorter than STRNG's */
    r->ring = r->kill = 0;
}

/* this block's lines for model m: lengths from TUNE (+ the chord, + the fine offset), the gains from DECAY, the
 * damping from TONE, the stiffness from STRCT, the pickup tap from POS */
static void reson_setup(track_t *t, uint32_t m)
{
    reson_t *r = &t->rs;
    uint32_t s, seg = m == RS_CHORD ? RS_SEG : RS_LEN;
    int32_t base = (clamp(t->p[P_RTUNE], 24, 96) << 8) + t->rfine, dk = (int32_t)RS_DK[clamp(t->p[P_RDECAY], 0, 127)];
    int32_t k = RS_TONE_K[clamp(t->p[P_RTONE], 0, 127)], a, comp;
    if (m == RS_CHORD && base < (48 << 8))
        base = 48 << 8;                               /* CHORD: the lowest string C3 */
    a = m == RS_CHORD ? 0 : -(int32_t)RS_AP_MAX[clamp(base >> 8, 0, 127)] * clamp(t->p[P_RSTRCT], 0, 127) / 127;
    comp = ((32768 - k) << 8) / k + ((32768 - a) << 8) / (32768 + a);   /* the loop filters' delay, Q8 */
    r->ns = (uint8_t)(m == RS_CHORD ? 4u : 1u);
    r->seg = (uint16_t)seg;
    r->k = k;
    r->a = a;
    for (s = 0; s < r->ns; s++) {
        int32_t per = rs_period(base + (m == RS_CHORD ? RS_CHORD_IV[rs_chord(t)][s] << 8 : 0));
        int32_t len = clamp((m == RS_PIPE ? per / 2 : per) - comp, 2 << 8, (int32_t)(seg - 2u) << 8);
        int64_t e = ((int64_t)(len + comp) * dk) >> 16;   /* the loop's decay, log2 units Q16 */
        int32_t g = e >= (15 << 16) ? 0 : (int32_t)(RS_EXP2N[(e >> 8) & 255] >> (e >> 16));
        g = g > RS_GMAX ? RS_GMAX : g;
        r->len[s] = (uint32_t)len;
        r->g[s] = m == RS_PIPE ? -g : g;
        r->tap[s] = (uint16_t)(t->p[P_RPOS] > 0 ? ((uint32_t)len >> 8) * (uint32_t)clamp(t->p[P_RPOS], 0, 127) / 256u + 1u : 0u);
    }
}

/* track t's block b (its rendered sound) through RESON, in place: b = dry + (ring - dry) x MIX. A model change
 * or a cut (kill) fades the old ring out over the block, then clears it. Returns non-zero while it rings. */
static __attribute__((noinline)) uint32_t reson_block(track_t *t, int32_t *b, uint32_t n)   /* (measured on its own:
                                                         * tests/target_budget.py) */
{
    reson_t *r = &t->rs;
    uint32_t m = (uint32_t)clamp(t->p[P_RMODEL], 0, RS_NMODEL - 1), i, s, peak = 0;
    int32_t ring[CTL], mix = t->p[P_RMIX] * 258, fade = 32767, fstep;
    int16_t *buf = rs_buf[t - trk];
    if (m != r->model) {
        if (r->ring && r->model)
            r->kill = 1;                              /* the old ring fades out with its own lines */
        else {
            reson_clear(t);
            r->model = (uint8_t)m;
        }
    }
    if (r->hold && !r->ring)
        return 0;                                     /* after a cut, until the next hit: nothing to excite it */
    if (!r->model) {                                  /* OFF and quiet */
        r->model = (uint8_t)m;
        return 0;
    }
    reson_setup(t, r->model);
    fstep = r->kill ? 32767 / (int32_t)n + 1 : 0;
    for (i = 0; i < n; i++)
        ring[i] = 0;
    for (s = 0; s < r->ns; s++) {
        int16_t *ln = buf + s * r->seg;
        uint32_t li = r->len[s] >> 8, w = r->w, seg = r->seg, tap = r->tap[s];
        uint32_t rp = w >= li ? w - li : w + seg - li, tp = w >= tap ? w - tap : w + seg - tap;   /* read, pickup tap */
        int32_t lp = r->lp[s], lr = r->lr[s], apx = r->apx[s], apy = r->apy[s], g = r->g[s], k = r->k, a = r->a, ink = r->kill || r->hold ? 0 : 1;
        int32_t fr = (int32_t)(r->len[s] & 255u), x1 = ln[rp ? rp - 1u : seg - 1u];   /* the older neighbour */
        for (i = 0; i < n; i++) {                     /* |d - lp| <= 65534; the all-pass output reaches ~2.94 x full scale
                                                         * (|y| < 2^17: (y >> 1) * g fits 32 bits) */
            int32_t x0 = ln[rp], d = x0 + rs_mz((x1 - x0) * fr, 8), y, v;
            x1 = x0;                                  /* (the write position is li >= 2 samples ahead: never read) */
            {                                         /* damping: the step's remainder carried (error feedback):
                                                         * lp follows d exactly, no dead band to hold a value */
                int32_t e = (d - lp) * k + lr, st = e >> 15;   /* |d - lp| <= 65534: e fits 32 bits */
                lr = e - (st << 15);
                lp += st;
            }
            if (a) {                                  /* STRCT: the all-pass (exact product: dropping bits before
                                                         * it fed a small endless cycle); at 0 a plain sample of delay */
                int64_t q = (int64_t)(lp - apy) * a;
                y = (int32_t)((q + ((q >> 63) & 32767)) >> 15) + apx;
                apy = y;
            } else {
                y = apx;
            }
            apx = lp;
            v = clamp(((b[i] >> 2) & -ink) + rs_mz(rs_mz(y, 1) * g, 14), -32767, 32767);
            ring[i] += tap ? v - ln[tp] : v;
            ln[w] = (int16_t)v;
            peak |= (uint32_t)(v ^ (v >> 31));          /* >= the largest |v| (the floor test only) */
            if (++w == seg)
                w = 0;
            if (++rp == seg)
                rp = 0;
            if (++tp == seg)
                tp = 0;
        }
        r->lp[s] = lp;
        r->lr[s] = lr;
        r->apx[s] = apx;
        r->apy[s] = apy;
    }
    r->w = (uint16_t)((r->w + n) % r->seg);
    for (i = 0; i < n; i++) {
        int32_t o = r->ns == 4u ? ring[i] : ring[i] << 2;   /* back to the track's scale (CHORD: 4 lines summed) */
        if (r->kill) {
            o = (int32_t)(((int64_t)o * fade) >> 15);
            fade = fade > fstep ? fade - fstep : 0;
        }
        b[i] += (((o - b[i]) >> 5) * mix) >> 10;     /* |o - b| < 2^20: 32-bit */
    }
    r->peak = (uint16_t)peak;
    r->quiet = (uint16_t)(peak < RS_FLOOR ? r->quiet + 1u : 0u);
    r->ring = (uint8_t)(r->quiet <= r->seg / n + 2u);
    if (!r->ring && !r->kill) {                       /* quiet for a whole loop: fade the rest out next block (a residue
                                                         * of the loop's rounding must not end as a step) */
        r->ring = r->kill = 1;
        return 1;
    }
    if (r->kill) {
        reson_clear(t);
        r->model = (uint8_t)m;
    }
    return r->ring;
}
