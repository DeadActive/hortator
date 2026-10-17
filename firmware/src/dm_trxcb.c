/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* CBTRX: a cowbell built from measurements of a classic digital drum machine's (md-re: renders of an emulator, knob by
 * knob; only the measured laws are here, no code or data of it). Two tones running free (a random phase at each hit,
 * from the hit's seed): B, triangle-like at 1117 Hz, and A, odd harmonics of a 350 Hz base whose 3rd and 5th (1051,
 * 1752 Hz) are strong and its 1st nearly absent: one cycle of each as a table (from the measured line levels). TONE
 * crossfades them (linear in amplitude); DAMP crossfades to a narrow band-pass on B (Q 50; steady from the first
 * sample, as measured: its states start at the tones' steady state); DECAY a hold then an
 * exponential; ENH lowers A's dry level and adds a resonator (Q 21.5, 1079 -> 1047 Hz) after the envelope, fading in
 * over 20 ms, and a low grit in the first ~40 ms; a weak white floor follows the tone, a low pink floor sounds while
 * the hold lasts. BUMP an early pitch rise (+4 % at 127, 8 ms); PTCH the machine's own small pitch knob (B +0.37 %,
 * A +1 % over 0..127). TUNE and the step's note move every frequency by semitones. 1 voice. */
#define CB_NB 3528u                                     /* BUMP's 10 time constants (80 ms): its part ends */
#define CB_NG 4410u                                     /* ENH's grit, 10 time constants (100 ms) */
/* the level: the voice works in units of 1/16 its output, 2.4 M a unit of the machine's (its B ~0.07) */
static const int16_t CB_TB[257] = {
    0, 289, 583, 885, 1198, 1523, 1856, 2196, 2536, 2871, 3195, 3503, 3792, 4061, 4310, 4544,
    4768, 4989, 5213, 5447, 5695, 5961, 6245, 6546, 6859, 7180, 7504, 7826, 8145, 8459, 8770, 9082,
    9403, 9738, 10095, 10481, 10898, 11346, 11821, 12317, 12821, 13320, 13799, 14245, 14647, 14997, 15292, 15538,
    15741, 15917, 16083, 16257, 16456, 16697, 16988, 17333, 17727, 18159, 18610, 19056, 19471, 19828, 20103, 20277,
    20336, 20277, 20103, 19828, 19471, 19056, 18610, 18159, 17727, 17333, 16988, 16697, 16456, 16257, 16083, 15917,
    15741, 15538, 15292, 14997, 14647, 14245, 13799, 13320, 12821, 12317, 11821, 11346, 10898, 10481, 10095, 9738,
    9403, 9082, 8770, 8459, 8145, 7826, 7504, 7180, 6859, 6546, 6245, 5961, 5695, 5447, 5213, 4989,
    4768, 4544, 4310, 4061, 3792, 3503, 3195, 2871, 2536, 2196, 1856, 1523, 1198, 885, 583, 289,
    0, -289, -583, -885, -1198, -1523, -1856, -2196, -2536, -2871, -3195, -3503, -3792, -4061, -4310, -4544,
    -4768, -4989, -5213, -5447, -5695, -5961, -6245, -6546, -6859, -7180, -7504, -7826, -8145, -8459, -8770, -9082,
    -9403, -9738, -10095, -10481, -10898, -11346, -11821, -12317, -12821, -13320, -13799, -14245, -14647, -14997, -15292, -15538,
    -15741, -15917, -16083, -16257, -16456, -16697, -16988, -17333, -17727, -18159, -18610, -19056, -19471, -19828, -20103, -20277,
    -20336, -20277, -20103, -19828, -19471, -19056, -18610, -18159, -17727, -17333, -16988, -16697, -16456, -16257, -16083, -15917,
    -15741, -15538, -15292, -14997, -14647, -14245, -13799, -13320, -12821, -12317, -11821, -11346, -10898, -10481, -10095, -9738,
    -9403, -9082, -8770, -8459, -8145, -7826, -7504, -7180, -6859, -6546, -6245, -5961, -5695, -5447, -5213, -4989,
    -4768, -4544, -4310, -4061, -3792, -3503, -3195, -2871, -2536, -2196, -1856, -1523, -1198, -885, -583, -289,
    0};
static const int16_t CB_TA[257] = {
    0, 5601, 10946, 15800, 19974, 23336, 25825, 27450, 28284, 28457, 28134, 27495, 26719, 25959, 25330, 24898,
    24675, 24624, 24667, 24695, 24588, 24229, 23519, 22391, 20819, 18814, 16432, 13757, 10899, 7974, 5096, 2366,
    -139, -2372, -4314, -5973, -7378, -8569, -9594, -10490, -11288, -11999, -12617, -13123, -13484, -13663, -13627, -13352,
    -12827, -12064, -11091, -9956, -8718, -7444, -6203, -5052, -4041, -3197, -2532, -2040, -1701, -1484, -1359, -1296,
    -1278, -1296, -1359, -1484, -1701, -2040, -2532, -3197, -4041, -5052, -6203, -7444, -8718, -9956, -11091, -12064,
    -12827, -13352, -13627, -13663, -13484, -13123, -12617, -11999, -11288, -10490, -9594, -8569, -7378, -5973, -4314, -2372,
    -139, 2366, 5096, 7974, 10899, 13757, 16432, 18814, 20819, 22391, 23519, 24229, 24588, 24695, 24667, 24624,
    24675, 24898, 25330, 25959, 26719, 27495, 28134, 28457, 28284, 27450, 25825, 23336, 19974, 15800, 10946, 5601,
    0, -5601, -10946, -15800, -19974, -23336, -25825, -27450, -28284, -28457, -28134, -27495, -26719, -25959, -25330, -24898,
    -24675, -24624, -24667, -24695, -24588, -24229, -23519, -22391, -20819, -18814, -16432, -13757, -10899, -7974, -5096, -2366,
    139, 2372, 4314, 5973, 7378, 8569, 9594, 10490, 11288, 11999, 12617, 13123, 13484, 13663, 13627, 13352,
    12827, 12064, 11091, 9956, 8718, 7444, 6203, 5052, 4041, 3197, 2532, 2040, 1701, 1484, 1359, 1296,
    1278, 1296, 1359, 1484, 1701, 2040, 2532, 3197, 4041, 5052, 6203, 7444, 8718, 9956, 11091, 12064,
    12827, 13352, 13627, 13663, 13484, 13123, 12617, 11999, 11288, 10490, 9594, 8569, 7378, 5973, 4314, 2372,
    139, -2366, -5096, -7974, -10899, -13757, -16432, -18814, -20819, -22391, -23519, -24229, -24588, -24695, -24667, -24624,
    -24675, -24898, -25330, -25959, -26719, -27495, -28134, -28457, -28284, -27450, -25825, -23336, -19974, -15800, -10946, -5601,
    0};
static const int16_t CB_DAMP[128] = {
    0, 77, 153, 228, 303, 376, 449, 521, 592, 662, 732, 800, 868, 935, 1001, 1067,
    1131, 1195, 1258, 1320, 1381, 1442, 1501, 1560, 1618, 1676, 1732, 1788, 1843, 1897, 1951, 2003,
    2055, 2107, 2157, 2207, 2256, 2304, 2351, 2398, 2444, 2489, 2533, 2577, 2620, 2663, 2704, 2745,
    2785, 2825, 2863, 2902, 2939, 2976, 3012, 3047, 3082, 3115, 3149, 3181, 3213, 3245, 3275, 3305,
    3335, 3363, 3391, 3419, 3445, 3472, 3497, 3522, 3546, 3570, 3593, 3616, 3637, 3659, 3679, 3700,
    3719, 3738, 3756, 3774, 3792, 3808, 3824, 3840, 3855, 3870, 3884, 3897, 3910, 3923, 3935, 3946,
    3957, 3968, 3978, 3987, 3996, 4005, 4013, 4021, 4028, 4035, 4041, 4048, 4053, 4058, 4063, 4068,
    4072, 4075, 4079, 4082, 4084, 4087, 4089, 4091, 4092, 4093, 4094, 4095, 4095, 4096, 4096, 4096};

static inline int32_t cb_tab(const int16_t *t, uint32_t ph)   /* one cycle, linear between its 256 points */
{
    uint32_t i = ph >> 24;
    int32_t a = t[i];
    return a + (((t[i + 1] - a) * (int32_t)((ph >> 9) & 0x7FFFu)) >> 15);
}
static inline int32_t cb_mul31(int32_t x, uint32_t k) { return (int32_t)(((int64_t)x * k) >> 31); }
static inline uint32_t cb_fall(uint32_t e, uint32_t d) { return e - (uint32_t)(((uint64_t)e * d) >> 32); }
static int32_t cb_enh(const int16_t *t, int32_t e)      /* ENH's tables at 0, 32, 64, 96, 127 */
{
    int32_t k = e >= 96 ? 3 : e >> 5, x0 = k << 5;
    return t[k] + (t[k + 1] - t[k]) * (e - x0) / (k == 3 ? 31 : 32);
}
static const int16_t CB_ER[5] = {0, 3542, 6327, 6766, 4910};       /* the resonator's gain x 1000 */
static const int16_t CB_EA[5] = {1000, 668, 426, 234, 269};        /* A's dry level x 1000 */
static const int16_t CB_EF[5] = {10791, 10791, 10723, 10539, 10465}; /* the resonator's frequency, 0.1 Hz */
/* Paul Kellet's pink filter: poles (Q31), gains (x 0.11, the 150 Hz high-pass' scale, x the hold floor's level:
 * the pink states are in the voice's units; Q2 a noise unit), the last two w's own and the delayed one */
static const uint32_t CB_PP[6] = {2145035517u, 2133138457u, 2080911655u, 1860794581u, 1181116006u, 1635523546u};
static const int16_t CB_PG[8] = {1865, 2522, 5169, 10431, 17905, 568, 18015, 3895};

/* the pink noise from r's low 16 bits, high-passed (~150 Hz): the hold floor's level (the voice's units) */
static inline int32_t cb_pink(trxcbp_t *c, uint32_t r)
{
    int32_t w = (int32_t)(r << 16) >> 16, s = c->pn[6] + (w * CB_PG[6] >> 17);   /* w x 0.5362 + p6 */
    s += c->pn[0] = cb_mul31(c->pn[0], CB_PP[0]) + (w * CB_PG[0] >> 17);   /* (unrolled: no loop in the loop) */
    s += c->pn[1] = cb_mul31(c->pn[1], CB_PP[1]) + (w * CB_PG[1] >> 17);
    s += c->pn[2] = cb_mul31(c->pn[2], CB_PP[2]) + (w * CB_PG[2] >> 17);
    s += c->pn[3] = cb_mul31(c->pn[3], CB_PP[3]) + (w * CB_PG[3] >> 17);
    s += c->pn[4] = cb_mul31(c->pn[4], CB_PP[4]) + (w * CB_PG[4] >> 17);
    c->pn[5] = -cb_mul31(c->pn[5], CB_PP[5]) - (w * CB_PG[5] >> 17);
    s += c->pn[5];
    c->pn[6] = w * CB_PG[7] >> 17;                      /* w x 0.115926 */
    c->hy = cb_mul31(c->hy + s - c->hx, 2102171743u);   /* x 0.9789 */
    c->hx = s;
    return c->hy;
}
/* ENH's grit: the pink through a 900 Hz low-pass (0.1201), fading (tau 10 ms) */
static inline int32_t cb_grit(trxcbp_t *p, trxcb_t *c)
{
    int32_t g;
    p->el += cb_mul31(p->hy - p->el, 258444167u);
    g = cb_mul31((int32_t)(((int64_t)p->el * c->glvl) >> 12), c->eg);
    c->eg = cb_fall(c->eg, 9728121u);
    return g;
}

static void cbtrx_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    trxcb_t *c = &v->ms.cb;
    uint32_t i, tt = v->t, e = c->e, pb = c->pb, pa = c->pa;   /* (the hot state in locals: out[] cannot alias */
    const uint32_t nh = c->nh;                                /* them) */
    const int32_t gb = c->gb, gae = c->gae, ga = c->ga, dm = c->m, erk = c->erk;
    int32_t rng = v->rng, vg = vel_gain(v), z = 0;
    trxcbp_t pk = c->pk;
    (void)t;
    for (i = 0; i < n; i++, tt++) {
        uint32_t ib = c->ib, ia = c->ia, r = noise32(&rng);   /* the floor: its high 16 bits, the pink: its low */
        int32_t b, a, x, y, full = 0, bp, hp;
        if (c->bump && tt < CB_NB) {                    /* BUMP: the pitch x (1 + bump e ^ (-t / 8 ms)) */
            int32_t kb = cb_mul31(c->bump, c->eb);
            ib += (uint32_t)(((uint64_t)ib * (uint32_t)kb) >> 30);
            ia += (uint32_t)(((uint64_t)ia * (uint32_t)kb) >> 30);
            c->eb = cb_fall(c->eb, 12156707u);          /* tau 8 ms */
        }
        b = cb_tab(CB_TB, pb);
        a = cb_tab(CB_TA, pa);
        x = (b * gb + a * gae) >> 11;
        if (erk)
            full = (b * gb + a * ga) >> 11;
        if (dm) {                                     /* DAMP: B and A through the band-pass on B (k v1: unity) */
            int32_t bd, ad;
            trxm_tick(&c->db, b, &bd, &hp);
            trxm_tick(&c->da, a, &ad, &hp);
            bd = (int32_t)(((int64_t)bd * c->db.k) >> 30);
            ad = (int32_t)(((int64_t)ad * c->da.k) >> 30);
            x += (((bd * gb + ad * gae) >> 11) - x) * dm >> 12;
            if (erk)
                full += (((bd * gb + ad * ga) >> 11) - full) * dm >> 12;
        }
        pb += ib;
        pa += ia;
        x += ((int32_t)r >> 16) * 2036 >> 15;           /* the white floor: 0.012 x B's level, with the tone */
        if (tt < nh) {                                  /* the hold: its pink floor */
            y = x + (pk.hl += cb_mul31(cb_pink(&pk, r) - pk.hl, 1094194833u));   /* a 5 kHz low-pass (0.5096) */
            if (c->glvl && tt < CB_NG)
                y += cb_grit(&pk, c);
        } else {
            e = tt == nh ? c->e0 : cb_fall(e, c->d);   /* the decay */
            y = cb_mul31(x, e);
            if (c->glvl && tt < CB_NG) {                /* (the same pink after the hold, for the grit only) */
                cb_pink(&pk, r);
                y += cb_grit(&pk, c);
            }
        }
        if (erk) {                                      /* ENH: the resonator on the enveloped tone, fading in */
            int32_t in = cb_mul31(cb_mul31(full, e), 0x7FFFFFFFu - c->ea);
            trxm_tick(&c->enh, in, &bp, &hp);
            y += (int32_t)(((int64_t)bp * erk) >> 24);
            c->ea = cb_fall(c->ea, 4866818u);           /* tau 20 ms */
        }
        z = mulq15(mulq15(clamp(y >> 4, -65535, 65535), vg), VOICE_FS);   /* dm_put */
        out[i] += z;
    }
    v->last = z;
    v->rng = rng;
    v->t = tt;
    c->e = e;
    c->pb = pb;
    c->pa = pa;
    c->pk = pk;
    if (tt > c->nh && e < (1u << 17) && (!c->glvl || tt >= CB_NG))   /* -84 dB */
        dm_end(v);
}

/* DAMP's band-pass (Q 50 at B) on B and on A, each at its steady state at the hit (the machine's DAMP acts from the
 * first sample): the states before the first sample for B's fundamental (at the centre: s1 = (sin - g cos) / k,
 * s2 = -(cos + g sin) / k) and A's 3rd and 5th (their phasors, measured on the filter at PTCH 0 and 127, Q12 and the
 * phase); the rest of either tone passes at -40 dB or less (a start too small to hear) */
static void cb_damp(trxcb_t *c, int32_t pt)
{
    static const int32_t AM[2][4] = {{31537, 33631, 4436, 2829}, {35065, 37147, 4367, 2767}};   /* |s1|, |s2|: A3, A5 */
    static const uint32_t AP[2][2] = {{917420414u, 4138645886u}, {3151024955u, 2077283131u}};   /* s1's, s2's phase */
    static const int32_t AD[2] = {-97803, -8712};       /* their phases per PTCH step */
    static const int32_t AH[2] = {16384, 15892};        /* A3, A5 in the table (Q14) */
    int32_t g = trxm_tan((int32_t)(c->ib >> 8)), sb = sine_i(c->pb), cb = sine_i(c->pb + 0x40000000u), j;
    trxm_svf(&c->db, (int32_t)(c->ib >> 8), Q24(1 / 50.0));
    c->da = c->db;
    c->db.s1 = 25 * (sb - (int32_t)(((int64_t)g * cb) >> 24));   /* x 16384 (the table) x 50 (1 / k) / 32768 */
    c->db.s2 = -25 * (cb + (int32_t)(((int64_t)g * sb) >> 24));
    for (j = 0; j < 2; j++) {
        uint32_t th = (3u + 2u * (uint32_t)j) * c->pa, d = (uint32_t)(AD[j] * pt);
        int32_t m1 = AM[0][2 * j] + (AM[1][2 * j] - AM[0][2 * j]) * pt / 127;
        int32_t m2 = AM[0][2 * j + 1] + (AM[1][2 * j + 1] - AM[0][2 * j + 1]) * pt / 127;
        c->da.s1 += (int32_t)(((int64_t)m1 * sine_i(th + AP[j][0] + d) * AH[j]) >> 27);
        c->da.s2 += (int32_t)(((int64_t)m2 * sine_i(th + AP[j][1] + d) * AH[j]) >> 27);
    }
}

/* CBTRX: TUNE DECAY ENH DAMP, TONE BUMP PTCH */
static void cbtrx_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    trxcb_t *c = &v->ms.cb;
    int32_t s = clamp(dm_tune(t), -48, 48), dec = clamp(p[1], 0, 127), enh = clamp(p[2], 0, 127);
    int32_t tone = clamp(p[4], 0, 127), pt = clamp(p[6], 0, 127), ea;
    uint32_t hq8, tau, fr;
    c->ib = HZ(1115.2) + (uint32_t)((pt * 201328) >> 6);   /* PTCH: B 1115.2 + 0.0323 PTCH Hz, */
    c->ia = HZ(348.67) + (uint32_t)((pt * 176707) >> 6);   /* A's base 348.67 + 0.02835 PTCH Hz */
    if (s) {
        c->ib = inc_tune(c->ib, s);
        c->ia = inc_tune(c->ia, s);
    }
    c->pb = noise32(&v->rng);                           /* running free: a random phase */
    c->pa = noise32(&v->rng);
    ea = cb_enh(CB_EA, enh);
    c->gb = (618 + 10 * tone) * 16860 / 1000;           /* TONE: B ~ 61.8 + TONE, A ~ 188 - TONE (A -14 dB at 64); */
    c->ga = (188 - tone) * 34129 / 1000;                /* the tables Q14, >> 11 */
    c->gae = c->ga * ea / 1000;
    c->m = CB_DAMP[clamp(p[3], 0, 127)];                /* DAMP: 1 - (1 - DAMP / 127) ^ 2.4, Q12 */
    if (c->m)
        cb_damp(c, pt);
    c->erk = enh ? cb_enh(CB_ER, enh) * 7803 / 10 : 0;  /* ENH: the resonator's gain x its k (1 / 21.5), Q24 */
    fr = (uint32_t)(((int64_t)cb_enh(CB_EF, enh) * 155828) >> 12);   /* (fr: the resonator's fc / FS, Q24) */
    trxm_svf(&c->enh, s ? trxm_q24((int32_t)fr, s * 4096) : (int32_t)fr, Q24(1 / 21.5));
    c->ea = 0x7FFFFFFFu;
    c->glvl = enh ? 31858 * (enh < 24 ? enh : 24) / 24 : 0;   /* the grit: 3.5 x B's level from ENH 24, re the hold
                                                         * floor's (0.45), Q12 */
    c->eg = 0x7FFFFFFFu;
    c->bump = clamp(p[5], 0, 127) * 338186;             /* BUMP: + 4 % at 127, Q30 */
    c->eb = 0x7FFFFFFFu;
    hq8 = (uint32_t)(((uint64_t)90317u * pow2_q16((dec - 64) * 64 / 5)) >> 16);   /* the hold: 8 ms x 2 ^ ((DECAY - 64)
                                                         * / 15), Q8 samples */
    tau = (uint32_t)(((uint64_t)16511u * pow2_q16((dec - 64) * 12)) >> 16);   /* tau 23.4 ms x 2 ^ ((DECAY - 64) / 16),
                                                         * Q4 samples */
    c->d = (0xFFFFFFFFu / (tau + 8u)) << 4;
    c->nh = (hq8 + 255u) >> 8;
    fr = (c->nh << 8) - hq8;                            /* the decay's first sample: e ^ (-fr / tau) */
    c->e0 = 0x7FFFFFFFu - (uint32_t)(((uint64_t)(c->d >> 1) * fr) >> 8);
    c->e = 0x7FFFFFFFu;
    memset(&c->pk, 0, sizeof c->pk);
}

#define DM_CBTRX_DEF {"CBTRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("ENH", F_INT, 0, 127, 0), PD("DAMP", F_INT, 0, 127, 0), PD("TONE", F_INT, 0, 127, 64), \
    PD("BUMP", F_INT, 0, 127, 0), PD("PTCH", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0)}, cbtrx_trigger, \
    cbtrx_render}
