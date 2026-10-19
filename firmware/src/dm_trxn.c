/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* CPTRX (a clap) and MATRX (maracas): noise voices built from measurements of a classic digital drum machine's (md-re:
 * renders of an emulator, averaged over repeated hits, knob by knob; only the measured laws are here, no code or data
 * of it). Their noise is the voice's own (v->rng, the prototypes' xorshift), so a hit is reproducible. */

/* ---- 2-pole sections: the bilinear high- / low-pass with a prewarped cutoff (the prototypes' biquads), direct form I
 * with Q30 coefficients and Q23 signals (64-bit products: a 123 Hz high-pass keeps its poles exact). Cascaded
 * sections share their histories: one section's last outputs are the next one's last inputs. */
static int32_t tn_recip(int32_t a0)                     /* 1 / a0, a0 Q24 in [1, 8): Q30 (one Newton step) */
{
    int64_t r = (int64_t)(0xFFFFFFFFu / (uint32_t)(a0 >> 8)) << 14;
    int64_t e = (1LL << 30) - (((int64_t)a0 * r) >> 24);
    return (int32_t)(r + ((r * e) >> 30));
}

static void tn_bq_set(tnbq_t *c, int hp, int32_t fn, int32_t k16)   /* fn = f / FS, Q24 (<= 0.25); k16 = 1 / Q, Q16 */
{
    int32_t g = qtan_acc(fn), g2 = qm(g, g), kg = (int32_t)(((int64_t)k16 * g) >> 16);
    int32_t inv = tn_recip(QONE + kg + g2);
    c->b0 = hp ? inv : (int32_t)(((int64_t)g2 * inv) >> 24);
    c->a1 = (int32_t)((((int64_t)(g2 - QONE) * inv) >> 24) * 2);
    c->a2 = (int32_t)(((int64_t)(QONE - kg + g2) * inv) >> 24);
}

/* one output: xs = x - 2 x1 + x2 (high-pass) or x + 2 x1 + x2 (low-pass), y1 y2 the section's last outputs */
static inline int32_t tn_tick(const tnbq_t *c, int32_t xs, int32_t y1, int32_t y2)
{
    return (int32_t)(((int64_t)c->b0 * xs - (int64_t)c->a1 * y1 - (int64_t)c->a2 * y2 + (1 << 29)) >> 30);
}

/* the prototypes' piecewise-linear knob tables (trigger time) */
static int32_t tn_tab(const uint8_t *x, const int32_t *y, int n, int32_t v)
{
    int i;
    if (v <= x[0])
        return y[0];
    for (i = 1; i < n; i++)
        if (v <= x[i])
            return y[i - 1] + (y[i] - y[i - 1]) * (v - x[i - 1]) / (x[i] - x[i - 1]);
    return y[n - 1];
}
static const uint8_t TN_K5[5] = {0, 32, 64, 96, 127};

static uint32_t tn_db(int32_t cdb)                      /* centi-dB -> Q16 (2^(dB / 6.0206)) */
{
    return pow2_q16(clamp(cdb * 3189 / 10000, -3071, 3071));
}

/* ---- CPTRX: white noise -> 4-pole high-pass (two sections at fh, Q qh) -> resonant 2-pole low-pass (fl, ql) x the
 * burst envelope: four bursts at T x {0, 0.335, 0.72, 1} on one noise, each an instant attack and an exponential
 * decay (one envelope: each onset adds its amplitude); bursts 1-3 at c(CLPY) against the last one's 1 -> a soft clip
 * x / (1 + |x / L|^6)^(1/6) (the output never passes L) + ROOM after the clip: a second noise through a 4-pole
 * resonant low-pass (fr, Q 1.9) on its own exponential decay. The laws:
 *   TUNE: the clap's TONE in semitones: fh = 493 Hz x 2^(TUNE / 12) (TONE 64; the machine's TONE 0..127 is TUNE
 *         -15.4..+15.2, fh = 203 Hz x 2^(TONE / 50)); the low-pass x (0.9 + 0.0016 TONE) and the level's TONE part
 *         follow up to TONE's ends, the high-pass on by semitones
 *   DECAY (the machine's RICH): the bursts' tau 2.3 / 5.6 / 10 / 20 / 43 ms at 0 / 32 / 64 / 96 / 127
 *   CLPY: c = 1.118 + 1.028 CLPY / 127;  RATE: T = 2.27 + 0.468 RATE ms
 *   HARD: qh = 0.49 + 0.0105 HARD; ql 0.56 / 1.04 / 1.66 / 3.0 / 4.62 and fl 4.3 / 5.7 / 6.2 / 6.5 / 6.5 kHz
 *   ROOM: -45 / -35.5 / -32 / -31 dB at 32 / 64 / 96 / 127 (0: off);  RSIZ: tau 19 ms x 2^(RSIZ / 62);
 *   RTUN: fr = 692 Hz x 2^(RTUN / 50)
 * The clap runs until 10 x tau after the last burst (-87 dB), the room 8 x its tau (under -100 dB). */
#define CP_GABS 27671                                   /* the gain before the clip over L, Q16 (0.0361 / 0.0855) */
#define CP_RNL 536553u                                  /* the room filter's gain 2.8 over L, Q14 */
#define CP_RK16 34493                                   /* the room's 1 / Q (1.9), Q16 */
#define CP_OUT 2                                        /* the output (L = 32767) x 2 / 4: the defaults level with C909's */
static const int16_t CP_CLIP[193] = {                   /* L x u / (1 + u^6)^(1/6) over u = 0 .. 3 in 1/64, Q15 */
    0, 512, 1024, 1536, 2048, 2560, 3072, 3584, 4096, 4608, 5120, 5632, 6144, 6656, 7168, 7680, 8191, 8703, 9215,
    9727, 10238, 10749, 11261, 11771, 12282, 12792, 13302, 13811, 14319, 14826, 15333, 15838, 16341, 16843, 17343,
    17841, 18336, 18828, 19317, 19802, 20283, 20759, 21230, 21695, 22154, 22605, 23050, 23486, 23913, 24332, 24740,
    25138, 25525, 25900, 26263, 26615, 26953, 27279, 27592, 27891, 28177, 28451, 28711, 28958, 29192, 29414, 29624,
    29822, 30008, 30184, 30349, 30504, 30649, 30785, 30912, 31032, 31143, 31247, 31344, 31435, 31520, 31599, 31673,
    31742, 31806, 31866, 31921, 31973, 32022, 32067, 32110, 32149, 32186, 32220, 32253, 32283, 32311, 32337, 32362,
    32385, 32406, 32426, 32445, 32463, 32479, 32495, 32510, 32523, 32536, 32548, 32560, 32571, 32581, 32590, 32599,
    32608, 32615, 32623, 32630, 32637, 32643, 32649, 32655, 32660, 32665, 32670, 32674, 32678, 32682, 32686, 32690,
    32693, 32697, 32700, 32703, 32705, 32708, 32711, 32713, 32715, 32717, 32719, 32721, 32723, 32725, 32727, 32728,
    32730, 32731, 32733, 32734, 32735, 32737, 32738, 32739, 32740, 32741, 32742, 32743, 32744, 32745, 32746, 32746,
    32747, 32748, 32748, 32749, 32750, 32750, 32751, 32751, 32752, 32753, 32753, 32753, 32754, 32754, 32755, 32755,
    32756, 32756, 32756, 32757, 32757, 32757, 32758, 32758, 32758, 32759, 32759, 32759, 32759, 32760};

static inline int32_t cp_clip(int32_t u)                /* u Q23 (L = 1 << 23) -> Q15 (L = 32767) */
{
    uint32_t a = u < 0 ? 0u - (uint32_t)u : (uint32_t)u, i = a >> 17;
    int32_t y = i >= 192u ? CP_CLIP[192]
                          : CP_CLIP[i] + (((CP_CLIP[i + 1] - CP_CLIP[i]) * (int32_t)((a >> 2) & 0x7FFFu)) >> 15);
    return u < 0 ? -y : y;
}

/* CPTRX: TUNE DECAY CLPY HARD, RATE ROOM RSIZ RTUN */
static void cptrx_trigger(track_t *t, dvoice_t *v)
{
    static const int32_t TAU16[5] = {1623, 3951, 7056, 14112, 30341};      /* RICH's tau, samples x 16 */
    static const int32_t FL[5] = {4327, 5708, 6157, 6482, 6508}, QL[5] = {560, 1040, 1660, 3000, 4620};
    static const int32_t GH[5] = {-60, -80, 0, -20, 0}, GT[5] = {-30, -30, 0, 30, 100};   /* the level, centi-dB */
    static const int32_t RL[5] = {-20000, -4500, -3550, -3200, -3100};
    const int16_t *p = &t->p[P_E0];
    cptrx_t *c = &v->ms.cp;
    int32_t s = clamp(dm_tune(t), -48, 48), ha = clamp(p[3], 0, 127), ra = clamp(p[4], 0, 127);
    int32_t ro = clamp(p[5], 0, 127), rs = clamp(p[6], 0, 127), rt = clamp(p[7], 0, 127);
    int32_t to12 = clamp(768 + s * 50, 0, 1524);        /* the TONE TUNE stands for, x 12 */
    uint32_t fh = (uint32_t)(((uint64_t)(uint32_t)Q24(492.97 / 44100.0) * pow2_q16(s * 16)) >> 16);
    uint32_t fl = (uint32_t)tn_tab(TN_K5, FL, 5, ha) * (58982u + (uint32_t)to12 * 8738u / 1000u);   /* Hz, Q16 */
    uint32_t tau16 = (uint32_t)tn_tab(TN_K5, TAU16, 5, clamp(p[1], 0, 127));
    uint32_t gl = (uint32_t)(((uint64_t)CP_GABS * tn_db(tn_tab(TN_K5, GH, 5, ha) + tn_tab(TN_K5, GT, 5, to12 / 12))) >> 16);
    uint32_t cq = 73269u + (uint32_t)clamp(p[2], 0, 127) * 67371u / 127u;   /* c, Q16 */
    uint32_t tq = 25627u + 5284u * (uint32_t)ra;        /* T, samples Q8 */
    tn_bq_set(&c->hp, 1, (int32_t)(fh < 4194304u ? fh : 4194304u), 131072000 / (980 + 21 * ha));
    tn_bq_set(&c->lp, 0, (int32_t)(((uint64_t)fl * 24348u) >> 22), 65536000 / tn_tab(TN_K5, QL, 5, ha));   /* (2^30 /
                                                         * 44100: Hz Q16 -> Q24) */
    c->on[0] = 0;
    c->on[1] = (uint32_t)(((uint64_t)tq * 21955u + (1u << 23)) >> 24);        /* T x 0.335, 0.72 (Q16), 1 */
    c->on[2] = (uint32_t)(((uint64_t)tq * 47186u + (1u << 23)) >> 24);
    c->on[3] = (tq + 128u) >> 8;
    c->on[4] = 0xFFFFFFFFu;
    c->nb = 0;
    c->d = (0xFFFFFFFFu / tau16) << 4;                  /* the decay's step, Q32 */
    c->ac = (uint32_t)(((uint64_t)cq * gl) >> 5);       /* bursts 1-3 and 4, x the gain over L, Q27 */
    c->a4 = gl << 11;
    c->e = 0;
    c->cend = c->on[3] + 10u * (tau16 >> 4);
    if (ro) {
        uint32_t rtau16 = (13406u * pow2_q16((rs * 3097 + 500) / 1000)) >> 16;   /* 19 ms x 2^(RSIZ / 62), x 16 */
        uint32_t fr = (uint32_t)(((uint64_t)(uint32_t)Q24(692.0 / 44100.0) * pow2_q16((rt * 384 + 50) / 100)) >> 16);
        tn_bq_set(&c->rq, 0, (int32_t)fr, CP_RK16);
        c->re = (uint32_t)(((uint64_t)tn_db(tn_tab(TN_K5, RL, 5, ro)) * CP_RNL)); /* rl x 2.8 / L, Q30 */
        c->rd = (0xFFFFFFFFu / rtau16) << 4;
        c->rend = 8u * (rtau16 >> 4);
    } else
        c->rend = c->re = 0;
}

static void cptrx_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    cptrx_t *c = &v->ms.cp;
    int32_t *w = c->s, *r = c->r;
    uint32_t i, tt = v->t;
    (void)t;
    for (i = 0; i < n; i++, tt++) {
        int32_t y = 0;
        if (tt < c->cend) {                             /* the clap */
            int32_t x = (int32_t)noise32(&v->rng) >> 8, h1, h2, l;
            if (tt == c->on[c->nb])
                c->e += c->nb++ < 3u ? c->ac : c->a4;
            h1 = tn_tick(&c->hp, x - 2 * w[0] + w[1], w[2], w[3]);
            h2 = tn_tick(&c->hp, h1 - 2 * w[2] + w[3], w[4], w[5]);
            l = tn_tick(&c->lp, h2 + 2 * w[4] + w[5], w[6], w[7]);
            w[1] = w[0], w[0] = x, w[3] = w[2], w[2] = h1, w[5] = w[4], w[4] = h2, w[7] = w[6], w[6] = l;
            y = cp_clip((int32_t)(((int64_t)l * c->e) >> 27));
            c->e -= (uint32_t)(((uint64_t)c->e * c->d) >> 32);
        }
        if (tt < c->rend) {                             /* the room, after the clip */
            int32_t x = (int32_t)noise32(&v->rng) >> 8, r1, r2;
            r1 = tn_tick(&c->rq, x + 2 * r[0] + r[1], r[2], r[3]);
            r2 = tn_tick(&c->rq, r1 + 2 * r[2] + r[3], r[4], r[5]);
            r[1] = r[0], r[0] = x, r[3] = r[2], r[2] = r1, r[5] = r[4], r[4] = r2;
            y += (int32_t)(((int64_t)r2 * c->re) >> 38);
            c->re -= (uint32_t)(((uint64_t)c->re * c->rd) >> 32);
        }
        dm_put(v, out, i, clamp((y * CP_OUT) >> 2, -65536, 65536));
    }
    v->t = tt;
    if (tt >= c->cend && tt >= c->rend)
        dm_end(v);
}

#define DM_CPTRX_DEF {"CPTRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 10), PD("DECAY", F_INT, 0, 127, 127), \
    PD("CLPY", F_INT, 0, 127, 56), PD("HARD", F_INT, 0, 127, 7), PD("RATE", F_INT, 0, 127, 43), \
    PD("ROOM", F_INT, 0, 127, 56), PD("RSIZ", F_INT, 0, 127, 0), PD("RTUN", F_INT, 0, 127, 93)}, cptrx_trigger, \
    cptrx_render}

/* ---- MATRX: white noise gated per 32-sample block (each block silent with p_off(DAMP), its level jittered ~2.3 dB:
 * the grains; gated before the filter, so the edges carry no lows) -> 4-pole high-pass (two sections at fh, Q q) ->
 * a one-pole 16 kHz low-pass; + the rattle (a second, flat noise, RATL, rising through the hit as t^0.7); all x the
 * stepped envelope: an attack (t / Ta)^1.3, a hold, REV's V-shaped dip and second hold, a cut to -48 dB in 0.7 ms and
 * a faint 11 ms tail. The laws:
 *   TUNE: fh x 2^(TUNE / 12) (on top of TONE; at most 11 kHz)
 *   DECAY (the machine's SUS): the hold 14 ms + 1.451 ms x DECAY (two 32-sample blocks a step)
 *   ATT: Ta = 1.451 ms x ATT;  REV: + 1.451 ms x REV after the hold, its dip 0.6 ms x REV
 *   DAMP: p_off 0.30 / 0.28 / 0.35 / 0.42 / 0.52 / 0.62 / 0.68 / 0.74 / 0.78 at 0, 16 .. 112, 127
 *   RATL: the rattle 0.59 x RATL / 127 of the main noise's amplitude at 190 ms (the machine's RTYP, a level only, at 0)
 *   TONE: fh = 3620 Hz x 2^(TONE / 124), Q x (1.26 - 0.0043 TONE), level -3.1 / -2.3 / 0 / 2.8 / 6.3 dB
 *   HARD: Q 0.47 + 0.0068 HARD, fh x 0.787 / 0.92 / 1 / 1.014 / 1.054 at 0 / 32 / 64 / 80 / 127, level 1.5 .. -1.3 dB
 * The voice ends 6 tail time constants after the cut (-100 dB). */
#define MA_G30 104484                                   /* the main noise's level 0.0131 (Q23 -> the output, Q30): the
                                                         * defaults level with HATC's */
#define MA_RK 181239                                    /* the rattle's 1.47 x 0.59 x 0.0131 at RATL 127 (as MA_G30), Q8 */
#define MA_LA 58830u                                    /* the 16 kHz one-pole, Q16 */
#define MA_TAIL 65401u                                  /* the tail: tau 11 ms, Q16 */
static const int16_t MA_ATT[65] = {                     /* (t / Ta)^1.3 at t / Ta = 0 .. 1 in 1/64, Q15 */
    0, 147, 362, 613, 891, 1191, 1510, 1845, 2195, 2558, 2934, 3321, 3718, 4126, 4543, 4970, 5405, 5848, 6299,
    6757, 7223, 7696, 8176, 8663, 9155, 9654, 10159, 10670, 11187, 11709, 12237, 12769, 13308, 13851, 14399, 14952,
    15509, 16072, 16639, 17210, 17786, 18366, 18951, 19539, 20132, 20729, 21330, 21935, 22543, 23156, 23772, 24392,
    25015, 25643, 26273, 26908, 27545, 28186, 28831, 29479, 30130, 30784, 31442, 32103, 32767};
static const int16_t MA_RAT[65] = {                     /* (t / 190 ms)^0.7 at t = 0 .. 300 ms in 1/64, Q14 */
    0, 1227, 1994, 2648, 3239, 3786, 4302, 4792, 5262, 5714, 6151, 6575, 6988, 7391, 7785, 8170, 8547, 8918, 9282,
    9640, 9992, 10340, 10682, 11019, 11353, 11682, 12007, 12328, 12646, 12961, 13272, 13580, 13885, 14188, 14487,
    14784, 15079, 15371, 15660, 15948, 16233, 16516, 16797, 17076, 17353, 17628, 17901, 18173, 18443, 18711, 18977,
    19242, 19505, 19767, 20028, 20286, 20544, 20800, 21055, 21308, 21560, 21811, 22061, 22309, 22557};

static inline int32_t ma_lerp(const int16_t *tb, uint32_t q)   /* q: index Q16 */
{
    uint32_t i = q >> 16;
    return tb[i] + (((tb[i + 1] - tb[i]) * (int32_t)((q >> 1) & 0x7FFFu)) >> 15);
}

/* MATRX: TUNE DECAY ATT REV, DAMP RATL TONE HARD */
static void matrx_trigger(track_t *t, dvoice_t *v)
{
    static const uint8_t H5[5] = {0, 32, 64, 80, 127}, D9[9] = {0, 16, 32, 48, 64, 80, 96, 112, 127};
    static const int32_t HS[5] = {51577, 60293, 65536, 66453, 69075};      /* fh x, Q16 */
    static const int32_t GT[5] = {-310, -230, 0, 280, 630}, GH[5] = {150, 70, 0, -90, -130};   /* centi-dB */
    static const int32_t PO[9] = {19661, 18350, 22938, 27525, 34079, 40632, 44564, 48497, 51118};   /* p_off, Q16 */
    const int16_t *p = &t->p[P_E0];
    matrx_t *m = &v->ms.ma;
    int32_t s = clamp(dm_tune(t), -48, 48), su = clamp(p[1], 0, 127), at = clamp(p[2], 0, 127);
    int32_t re = clamp(p[3], 0, 127), ha = clamp(p[7], 0, 127), to = clamp(p[6], 0, 127);
    uint32_t fh = (uint32_t)(((uint64_t)(uint32_t)Q24(3620.0 / 44100.0) * pow2_q16((to * 1548 + 500) / 1000 + s * 16)) >> 16);
    uint32_t k1 = 655360000u / (uint32_t)(4700 + 68 * ha), g;   /* 1 / (0.47 + 0.0068 HARD), Q16 */
    fh = (uint32_t)(((uint64_t)fh * (uint32_t)tn_tab(H5, HS, 5, ha)) >> 16);
    tn_bq_set(&m->hp, 1, (int32_t)(fh < 4181000u ? fh : 4181000u), (int32_t)(k1 * 9850u / (uint32_t)(12600 - 43 * to)));
    g = tn_db(tn_tab(TN_K5, GT, 5, to) + tn_tab(TN_K5, GH, 5, ha));
    m->g = (int32_t)(((uint64_t)MA_G30 * g) >> 16);
    m->rk = (int32_t)(((uint64_t)(MA_RK * (uint32_t)clamp(p[5], 0, 127) / 127u) * g) >> 16);
    m->poff = (uint32_t)tn_tab(D9, PO, 9, clamp(p[4], 0, 127)) << 16;
    m->t1 = 64u * (uint32_t)at;                          /* the envelope's corners, samples */
    m->t2 = m->t1 + 617u + 64u * (uint32_t)su;
    m->dv = (uint32_t)(re * 2646 + 50) / 100u;
    m->t2v = m->t2 + m->dv;
    m->t3 = m->t2 + 64u * (uint32_t)re;
    m->tend = m->t3 + 31u + 6u * 485u;
    m->ia = at ? 0xFFFFFFFFu / m->t1 >> 4 : 0;           /* the attack: index (Q16, 0..64) = t x ia >> 6 */
    m->idv = re ? 0x3FFF8000u / m->dv : 0;
    m->et = 67089;                                       /* the tail's start, Q24 (0.004) */
    m->gq = 0;
    m->lp = 0;
    m->gv = mulq15(vel_gain(v), VOICE_FS);              /* as dm_put, in one rounded product: a quiet voice's last
                                                         * bits (truncated, a bias following the grains) stay out */
}

static void matrx_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    matrx_t *m = &v->ms.ma;
    int32_t *w = m->s;
    uint32_t i, tt = v->t;
    (void)t;
    for (i = 0; i < n && tt < m->tend; i++, tt++) {
        int32_t x, h1, h2, y, e;
        if (!(tt & 31u)) {                               /* a grain: on or off, its level jittered */
            int32_t u = ((int32_t)noise32(&v->rng) >> 4) + ((int32_t)noise32(&v->rng) >> 4) +
                        ((int32_t)noise32(&v->rng) >> 4) + ((int32_t)noise32(&v->rng) >> 4) +
                        ((int32_t)noise32(&v->rng) >> 4) + ((int32_t)noise32(&v->rng) >> 4);
            if ((noise32(&v->rng) ^ 0x80000000u) < m->poff)
                m->gq = 0;
            else {                                      /* 10 ^ (2.3 dB x u / 0.707 / 20) = 2 ^ x, x Q16 */
                int32_t xq = ((u >> 14) * 35412) >> 14, k = xq >> 16;
                uint32_t f = (uint32_t)xq & 0xFFFFu;
                uint32_t pw = 65536u + ((f * (45600u + ((f * (14738u + ((f * 5198u) >> 16))) >> 16))) >> 16);
                m->gq = (int32_t)(k >= 0 ? (pw << k) >> 2 : pw >> (2 - k));   /* Q14 */
            }
        }
        x = (((int32_t)noise32(&v->rng) >> 16) * m->gq) >> 6;   /* Q23 */
        h1 = tn_tick(&m->hp, x - 2 * w[0] + w[1], w[2], w[3]);
        h2 = tn_tick(&m->hp, h1 - 2 * w[2] + w[3], w[4], w[5]);
        w[1] = w[0], w[0] = x, w[3] = w[2], w[2] = h1, w[5] = w[4], w[4] = h2;
        m->lp += mulq16(h2 - m->lp, MA_LA);
        y = (int32_t)(((int64_t)m->lp * m->g + (1 << 25)) >> 26);   /* Q4 */
        if (m->rk)                                      /* the rattle */
            y += (int32_t)(((int64_t)(((int32_t)noise32(&v->rng) >> 16) *
                                      (tt < 13230u ? ma_lerp(MA_RAT, tt * 317u) : MA_RAT[64])) * m->rk) >> 33);
        if (tt < m->t1)
            e = ma_lerp(MA_ATT, (tt * m->ia) >> 6);
        else if (tt < m->t2)
            e = 32767;
        else if (tt < m->t2v) {                         /* REV: down to 0 and back */
            int32_t d = (int32_t)m->dv - 2 * (int32_t)(tt - m->t2);
            e = (int32_t)(((uint32_t)(d < 0 ? -d : d) * m->idv) >> 15);
        } else if (tt < m->t3)
            e = 32767;
        else if (tt < m->t3 + 31u)                      /* the cut: to -48 dB in 0.7 ms */
            e = 32767 - 1057 * (int32_t)(tt - m->t3);
        else {                                          /* the faint tail */
            e = env_q15(m->et);
            m->et = mulq16(m->et, MA_TAIL);
        }
        y = (int32_t)(((int64_t)clamp(y, -1048560, 1048560) * e * m->gv + (1LL << 33)) >> 34);
        out[i] += y;
        v->last = y;
    }
    v->t = tt;
    if (tt >= m->tend)
        dm_end(v);
}

#define DM_MATRX_DEF {"MATRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("ATT", F_INT, 0, 127, 0), PD("REV", F_INT, 0, 127, 0), PD("DAMP", F_INT, 0, 127, 0), \
    PD("RATL", F_INT, 0, 127, 0), PD("TONE", F_INT, 0, 127, 64), PD("HARD", F_INT, 0, 127, 64)}, matrx_trigger, \
    matrx_render}
