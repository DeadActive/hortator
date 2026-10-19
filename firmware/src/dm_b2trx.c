/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* B2TRX: a sine bass drum built from measurements of a classic digital drum machine's second bass drum (md-re:
 * renders of an emulator, knob by knob; only the measured laws are here, no code or data of it). A sine (50.3 Hz at
 * TUNE 0; the machine's runs free, here it starts at 0 upwards on every hit) with RAMP's added pitch (up to 420 Hz,
 * the same in Hz on every note, tau 35 .. 43 ms), a 1.3 ms fade-in, HOLD at full level (up to seconds) before DECAY's
 * exponential; DIRT quantises the sine to 3 .. 257 levels and DIST drives it into a hard clip, both before the
 * envelope; TICK a negative one-sided pulse (tau 0.24 ms), NOISE a white burst (tau 9.1 ms); all of it through the
 * output's saturation (pinned at 0.322), a high-pass y - k LP(y) (LP 18.8 Hz; k 1, or 1.87 with DIRT / DIST on) and a
 * 3-tap smoothing. 1 voice. */
#define B2TRX_AMP 5571                                  /* the sine's level, Q15 (0.170) */
#define B2TRX_ATK 36581u                                /* the fade-in, Q15 per sample x 64 (1.3 ms) */
#define B2TRX_TICK 77175                                /* TICK's pulse per knob step, Q23 (0.0092) */
#define B2TRX_K_TICK 59310u                             /* x 0.905 per sample, Q16 */
#define B2TRX_D_N 10702368u                             /* NOISE: tau 9.1 ms (Q32 per sample) */
#define B2TRX_LPC 11488874                              /* the high-pass' one-pole LP, 18.8 Hz (Q32) */
#define B2TRX_HK 7660                                   /* its k with the shaper on, Q12 (1.87) */
#define B2TRX_LIFE 255780u                              /* HOLD + the decay to its end within 5.8 s */
#define B2TRX_GAIN(x) ((x) * 6)                         /* the defaults level with K909's */
/* RAMP's depth 420 Hz (r / 127) ^ 0.9 at 0, 8 .. 120, 127, Hz x 256 */
static const int32_t B2TRX_RAMP[17] = {0, 8930, 16664, 24002, 31096, 38012, 44790, 51456, 58027, 64516, 70933, 77286,
                                       83581, 89825, 96020, 102171, 107520};
/* NOISE's level at 0, 32, 64, 96, 127 (x 0.16), Q15 */
static const int32_t B2TRX_NL[5] = {0, 1363, 2779, 4404, 5243};
/* the output saturation above 0.285 (measured on TICK's clipped pulse), every 1/64 from 0.285, Q15; 0.322 above */
static const int16_t B2TRX_SAT[36] = {9339, 9732, 9934, 10083, 10139, 10163, 10185, 10206, 10227, 10244, 10261, 10278,
                                      10295, 10312, 10329, 10343, 10356, 10370, 10384, 10398, 10412, 10425, 10435,
                                      10445, 10455, 10465, 10475, 10485, 10495, 10504, 10514, 10521, 10528, 10535,
                                      10542, 10548};
static inline int32_t b2trx_sat(int32_t y)
{
    int32_t a = y < 0 ? -y : y, i;
    if (a <= 9339)
        return y;
    i = (a - 9339) >> 9;
    a = i >= 35 ? 10548 : B2TRX_SAT[i] + (((B2TRX_SAT[i + 1] - B2TRX_SAT[i]) * ((a - 9339) & 511)) >> 9);
    return y < 0 ? -a : a;
}

/* B2TRX: TUNE DECAY RAMP HOLD, TICK NOISE DIRT DIST */
static void b2trx_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    b2trx_t *k = &v->ms.b2;
    int32_t r = clamp(p[2], 0, 127), h = clamp(p[3], 0, 127), nz = clamp(p[5], 0, 127), di = clamp(p[6], 0, 127);
    int32_t ds = clamp(p[7], 0, 127), j;
    uint32_t tau = ktrx_tau(3175u, (clamp(p[1], 0, 127) - 64) * 19277 / 1000, 132u);   /* DECAY: tau 72 ms x 1.745 ^
                                                         * ((d - 64) / 8), 3 ms floor */
    v->inc[0] = inc_tune(HZ(50.32), dm_tune(t));
    j = r >= 120 ? 15 : r >> 3;
    v->inc[1] = (uint32_t)(((uint64_t)(B2TRX_RAMP[j] + (((B2TRX_RAMP[j + 1] - B2TRX_RAMP[j]) *
                                                         (r >= 120 ? (r - 120) * 256 / 7 : (r & 7) << 5) + 128) >> 8)) *
                            24932236u) >> 16);         /* RAMP, its phase increment (2^32 / 44100 / 256, Q16) */
    k->e_r = r ? 0x7FFFFFFFu : 0;
    k->d_r = (0xFFFFFFFFu / (3088u + ((uint32_t)r * 7056u + 635u) / 1270u)) << 1;   /* tau 35 + 8 r / 127 ms (1 / (tau +
                                                         * 0.5): e ^ (-1 / tau) to 2nd order; a ramp of ~14 cycles
                                                         * shows any error) */
    k->e_amp = 0x7FFFFFFFu;
    k->d_amp = 0xFFFFFFFFu / tau;
    k->hold = 0;
    if (h) {                                            /* HOLD: 190 ms x 1.647 ^ ((h - 64) / 8) - 14 ms */
        uint32_t hs = (8379u * pow2_q16((h - 64) * 17276 / 1000)) >> 16, hmax = B2TRX_LIFE - tau * 97u / 10u;
        hs = hs > 617u ? hs - 617u : 0;                 /* (the decay to -84 dB takes 9.7 tau: the voice still ends
                                                         * within its 6 s, HOLD shortened if need be) */
        k->hold = hs > hmax ? hmax : hs;
    }
    k->tick = -B2TRX_TICK * clamp(p[4], 0, 127);        /* TICK: Q23 */
    j = nz >= 96 ? 3 : nz >> 5;
    k->nl = B2TRX_NL[j] + (B2TRX_NL[j + 1] - B2TRX_NL[j]) * (nz - 32 * j) / (j == 3 ? 31 : 32);
    k->e_n = nz ? 0x7FFFFFFFu : 0;
    k->qb = di ? (di >= 104 ? 15 : 15 - (221 - 2 * di) / 26) : 0;   /* DIRT: steps of 2 ^ -b, b = round((104 - v) / 13)
                                                         * (as a shift: 15 - b; 0 off) */
    k->g8 = ds ? (int32_t)(pow2_q16(ds * 16) >> 8) : 0; /* DIST: x 2 ^ (v / 12), at most 100 */
    if (k->g8 > 25600)
        k->g8 = 25600;
    k->hk = di >= 40 || ds ? B2TRX_HK : 4096;           /* (the shaped path's high-pass over-subtracts) */
}

static void b2trx_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    b2trx_t *k = &v->ms.b2;
    uint32_t i, tt = v->t;
    (void)t;
    for (i = 0; i < n; i++, tt++) {
        int32_t s = sine_i(v->ph[0]), y, hp;
        v->ph[0] += v->inc[0];
        if (k->e_r) {                                   /* RAMP */
            v->ph[0] += (uint32_t)(((uint64_t)v->inc[1] * k->e_r) >> 31);
            ktrx_fall(&k->e_r, k->d_r);
            if (k->e_r < (1u << 12))
                k->e_r = 0;                             /* (above the fall's stall: tau) */
        }
        if (k->qb)                                      /* DIRT: quantised before the envelope */
            s = ((s + (1 << (k->qb - 1))) >> k->qb) * (1 << k->qb);
        if (k->g8)                                      /* DIST: a hard clip at the sine's own level */
            s = clamp((s * k->g8) >> 8, -32768, 32768);
        y = mulq15(mulq15(s, B2TRX_AMP), (int32_t)(k->e_amp >> 16));
        if (tt < 58u)                                   /* the fade-in */
            y = mulq15(y, (int32_t)((tt * B2TRX_ATK) >> 6));
        if (k->tick) {                                  /* TICK */
            y += k->tick >> 8;
            k->tick = mulq16(k->tick, B2TRX_K_TICK);
            if (k->tick > -256)
                k->tick = 0;
        }
        if (k->e_n >= (1u << 17)) {                     /* NOISE */
            y += mulq15(mulq15(dnoise(v), k->nl), (int32_t)(k->e_n >> 16));
            ktrx_fall(&k->e_n, B2TRX_D_N);
        }
        y = b2trx_sat(y);                               /* the output: saturation, y - k LP(y), smoothing */
        k->lp += (int32_t)(((int64_t)(y * 4096 - k->lp) * B2TRX_LPC) >> 32);   /* (12 more bits) */
        hp = y - (int32_t)(((int64_t)k->lp * k->hk) >> 24);
        y = (hp * 24576 + k->z1 * 6881 + k->z2 * 1311) >> 15;
        k->z2 = k->z1;
        k->z1 = hp;
        y = B2TRX_GAIN(mulq15(mulq15(y, vel_gain(v)), VOICE_FS));   /* dm_put, the gain after */
        out[i] += y;
        v->last = y;
        if (tt >= k->hold)
            ktrx_fall(&k->e_amp, k->d_amp);
    }
    v->t = tt;
    if (k->e_amp < (1u << 17) && !k->tick && k->e_n < (1u << 17) && k->lp > -4096 && k->lp < 4096)
        dm_end(v);                                      /* (the high-pass' tail too: 1 LSB) */
}

#define DM_B2TRX_DEF {"B2TRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, -1), PD("DECAY", F_INT, 0, 127, 74), \
    PD("RAMP", F_INT, 0, 127, 33), PD("HOLD", F_INT, 0, 127, 22), PD("TICK", F_INT, 0, 127, 7), \
    PD("NOISE", F_INT, 0, 127, 4), PD("DIRT", F_INT, 0, 127, 31), PD("DIST", F_INT, 0, 127, 6)}, b2trx_trigger, \
    b2trx_render}
