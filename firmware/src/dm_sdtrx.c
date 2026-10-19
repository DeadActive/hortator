/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* SDTRX: a snare built from measurements of a classic digital drum machine's snare (md-re: renders of an emulator,
 * knob by knob; only the measured laws are here, no code or data of it). Two sines, both starting at their trough and
 * frozen for the first 32-sample block (the machine's DSP block): A at 341.8 Hz (+ BUMP: an alpha-shaped rise and
 * fall, up to 42 Hz, peaking at BENV's time 5.1 .. 10.6 ms), and B, whose phase restarts every block from a phase
 * advancing at F1 and runs at F2 inside the block (a comb of lines F1 + k x 1378 Hz under F2; F1, F2 set by BENV from
 * a measured table, erratic: 249 Hz .. 13.8 kHz); TONE crossfades A to B; DECAY's exponential; CLIP a peak-preserving
 * tanh(d x) / tanh(d) whose drive d falls on its own clock (tau 28.3 ms), whatever DECAY is. SNAP: the same noise on
 * every hit (as the machine's), high- and low-passed (1.55 / 6.9 kHz), tau 19.7 ms, and above 64 a 1.44 kHz
 * resonance (tau 51.7 ms). A 40 Hz one-pole high-pass at the output. TUNE moves both sines (and BUMP) by semitones;
 * the noise stays. 1 voice. */
#define SDTRX_AMP 6796                                  /* the body's level, Q15 (0.2074) */
#define SDTRX_NL 12911                                  /* SNAP's band at 127, Q15 (0.394) */
#define SDTRX_XL 8765                                   /* the resonance at 127, Q15 (0.2675) */
#define SDTRX_SEED 0x2545F491                           /* the noise: the same sequence on every hit */
#define SDTRX_D_N 4943655u                              /* SNAP's band: tau 19.7 ms (Q32 per sample) */
#define SDTRX_D_X 1883817u                              /* the resonance: tau 51.7 ms */
#define SDTRX_D_C 3437319u                              /* CLIP's drive: tau 28.3 ms */
#define SDTRX_HPK 1067639936                            /* the output high-pass, 40 Hz: its pole, Q30 */
#define SDTRX_INC4 1595663133ull                        /* phase increment of 1/4 Hz, Q16 */
#define SDTRX_GAIN(x) (((x) * 9) >> 3)                  /* x 1.125: the defaults level with S909's */
/* SNAP's filters (Simper SVF, Q13; damping Q12): high-pass 1554 Hz Q 1.12, low-pass 6932 Hz Q 0.91, band-pass
 * 1441 Hz Q 2.19 (fitted on the emulator's third-octave bands) */
static const dsvf_t SDTRX_HP = {7370, 819, 91, 3657}, SDTRX_LP = {4354, 2344, 1262, 4501},
                    SDTRX_BP = {7745, 798, 82, 1870};
/* osc B's F1 and F2 per BENV, Hz x 4 (measured on the emulator, md-re tools/sd_btable.py) */
static const uint16_t SDTRX_F1[128] = {
    27928, 27928, 26588, 26588, 28434, 33436, 33436, 41543, 47206, 47206, 44875, 50388, 50113, 50113, 50113, 43616,
    17727, 17727, 10976, 10976, 42458, 49106, 54619, 51804, 17765, 17765, 24386, 24386, 47056, 54538, 49026, 4813,
    47346, 47346, 46926, 52439, 12048, 47195, 47195, 51141, 1594, 1594, 51023, 51023, 35202, 35051, 35051, 52471,
    10890, 10890, 50528, 50528, 5232, 53031, 53031, 13593, 53952, 53952, 14486, 14486, 51174, 8193, 8193, 49941,
    5028, 5028, 54759, 54759, 24903, 48191, 48191, 51174, 18809, 18809, 50528, 50528, 16608, 48945, 48945, 52304,
    4920, 4920, 49758, 49758, 44552, 14180, 14180, 55087, 43072, 43072, 11892, 11892, 54624, 52665, 52665, 1249,
    49042, 49042, 50614, 50614, 24569, 22459, 22459, 51916, 51949, 51949, 13485, 13485, 29517, 55023, 55023, 52702,
    18023, 18023, 21275, 21275, 48670, 51578, 51578, 36902, 996, 996, 34103, 34103, 51879, 52390, 52390, 36138};
static const uint16_t SDTRX_F2[128] = {
    28836, 28836, 27462, 27462, 29360, 34512, 34512, 42879, 45133, 45133, 42309, 47900, 47626, 47626, 47626, 44928,
    18234, 18234, 11365, 11365, 43818, 46477, 52053, 49716, 18271, 18271, 25187, 25187, 44490, 53432, 46953, 4918,
    46299, 48864, 44360, 49936, 12382, 48714, 46148, 48779, 1692, 1692, 48536, 48536, 36248, 36188, 36188, 50124,
    11279, 11279, 47866, 47866, 5352, 50449, 50449, 13973, 51464, 51464, 14896, 14896, 48608, 8404, 8404, 47280,
    5234, 5234, 52272, 52272, 25727, 47173, 47173, 50077, 19348, 19348, 47866, 47866, 17170, 47917, 50426, 50231,
    5030, 5030, 47271, 47271, 45984, 14577, 14577, 52600, 44455, 44455, 12219, 12219, 52262, 51548, 51548, 1337,
    50523, 50523, 48048, 48048, 25377, 23114, 23114, 49429, 49861, 49861, 13956, 13956, 30382, 52536, 52536, 50615,
    18627, 18627, 21885, 21885, 46308, 49012, 49012, 38093, 1077, 1077, 35112, 35112, 49392, 49903, 49903, 37296};
/* tanh(i / 16), Q24, i = 0..96 (above 6: 1) */
static const int32_t SDTRX_TANH[97] = {
    0, 1047213, 2086297, 3109375, 4109053, 5078627, 6012239, 6905000, 7753039, 8553528, 9304639, 10005488,
    10656031, 11256960, 11809576, 12315676, 12777430, 13197274, 13577819, 13921766, 14231838, 14510725, 14761043,
    14985298, 15185868, 15364986, 15524733, 15667035, 15793661, 15906232, 16006223, 16094975, 16173699, 16243486,
    16305319, 16360079, 16408555, 16451454, 16489403, 16522966, 16552641, 16578873, 16602058, 16622544, 16640645,
    16656635, 16670758, 16683232, 16694249, 16703976, 16712566, 16720149, 16726845, 16732756, 16737974, 16742580,
    16746646, 16750235, 16753403, 16756200, 16758668, 16760846, 16762769, 16764466, 16765964, 16767285, 16768452,
    16769481, 16770390, 16771192, 16771900, 16772524, 16773076, 16773562, 16773991, 16774370, 16774705, 16775000,
    16775260, 16775490, 16775693, 16775872, 16776030, 16776169, 16776292, 16776401, 16776496, 16776581, 16776656,
    16776721, 16776780, 16776831, 16776876, 16776916, 16776951, 16776982, 16777010};
/* BUMP's depth 42 Hz (b / 127) ^ 2.2 at 0, 8 .. 120, 127, Hz x 256 */
static const int32_t SDTRX_BUMPK[17] = {0, 25, 113, 275, 518, 847, 1264, 1775, 2381, 3085, 3890, 4797, 5809, 6928,
                                        8155, 9491, 10752};
/* CLIP's drive at 48, 56 .. 120, 127, log2 in 1/192 octaves (fitted on the 3rd harmonic over time: 0.5 .. 370) */
static const int32_t SDTRX_CLIPL[11] = {-192, 0, 66, 140, 392, 557, 680, 762, 947, 1286, 1638};
/* the resonance's level ((s - 64) / 63) ^ 1.59 at 64, 72 .. 120, 127, Q15 */
static const int32_t SDTRX_XS[9] = {0, 1231, 3707, 7064, 11161, 15914, 21265, 27172, 32768};

/* a knob c (c0..127) through a table at c0, c0 + 8 .. 120, 127 (c0 a multiple of 8), linear between */
static int32_t sdtrx_tab(const int32_t *t, int32_t c, int32_t c0)
{
    int32_t j = c >= 120 ? (120 - c0) >> 3 : (c - c0) >> 3, f = c >= 120 ? (c - 120) * 256 / 7 : (c & 7) << 5;
    return t[j] + (((t[j + 1] - t[j]) * f) >> 8);
}
/* tanh(u), u Q20 (signed), Q24 */
static inline int32_t sdtrx_tanh(int32_t u)
{
    uint32_t a = (uint32_t)(u < 0 ? -u : u), i = a >> 16;
    int32_t y = i >= 96u ? 1 << 24 : SDTRX_TANH[i] + (int32_t)(((int64_t)(SDTRX_TANH[i + 1] - SDTRX_TANH[i]) *
                                                                (int32_t)(a & 0xFFFFu)) >> 16);
    return u < 0 ? -y : y;
}

/* SDTRX: TUNE DECAY SNAP TONE, BUMP BENV CLIP */
static void sdtrx_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    sdtrx_t *k = &v->ms.sd;
    int32_t s = clamp(dm_tune(t), -48, 48), sn = clamp(p[2], 0, 127), to = clamp(p[3], 0, 127);
    int32_t bu = clamp(p[4], 0, 127), be = clamp(p[5], 0, 127), c = clamp(p[6], 0, 127);
    uint32_t tb8;
    v->ph[0] = v->ph[1] = v->ph[2] = 0xC0000000u;       /* both sines start at their trough */
    v->inc[0] = inc_tune(HZ(341.8), s);
    v->inc[1] = inc_tune((uint32_t)((SDTRX_F1[be] * SDTRX_INC4) >> 16), s) << 5;   /* B's start: a block's worth of
                                                         * F1 (mod 2^32: aliased, as the machine's) */
    v->inc[2] = inc_tune((uint32_t)((SDTRX_F2[be] * SDTRX_INC4) >> 16), s);
    k->ga = 32768 - to * 129;                           /* TONE: A (1 - T / 254) + B (T / 254) */
    k->gb = to * 129;
    k->e_amp = 0x7FFFFFFFu;                             /* DECAY: tau 59.5 ms x 1.455 ^ ((d - 64) / 16) */
    k->d_amp = 0xFFFFFFFFu / ktrx_tau(2624u, (clamp(p[1], 0, 127) - 64) * 6492 / 1000, 100u);
    k->bk = bu ? (int32_t)(((uint64_t)(sdtrx_tab(SDTRX_BUMPK, bu, 0) * 1034u) * pow2_q16(s * 16)) >> 16) : 0;   /* the
                                                         * bump's K x e, phase increment (1034: e x 2^32 / 44100 / 256) */
    tb8 = (uint32_t)((57577ull * pow2_q16(be * 8 / 5)) >> 16);        /* its time: 5.1 ms x 2 ^ (BENV / 120), Q8 samples */
    k->bu = 0;
    k->du = 0xFFFFFFFFu / tb8;                          /* t / tb, Q24 */
    k->be = 0x7FFFFFFFu;                                /* e ^ (-t / tb), Q31 */
    k->dbe = (0xFFFFFFFFu / tb8) << 8;
    k->drive = 0;
    if (c > 40) {                                       /* CLIP: the drive, Q16 */
        if (c < 48)
            k->drive = (uint32_t)(c - 40) * 4096u;      /* 0.5 (c - 40) / 8 */
        else
            k->drive = pow2_q16(sdtrx_tab(SDTRX_CLIPL, c, 48));   /* (log2: the fit's interpolation) */
        k->g = (int32_t)((0xFFFFFFFFu / (uint32_t)(sdtrx_tanh((int32_t)(k->drive << 4)) >> 12)) << 4);   /* 1 / tanh(d),
                                                         * Q24 (refined every sample) */
    }
    k->nl = sn * SDTRX_NL / 127;
    k->xl = sn > 64 ? sdtrx_tab(SDTRX_XS, sn, 64) * SDTRX_XL >> 15 : 0;
    k->e_n = sn ? 0x7FFFFFFFu : 0;
    k->e_x = k->xl ? 0x7FFFFFFFu : 0;
    v->rng = SDTRX_SEED;
}

static void sdtrx_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    sdtrx_t *k = &v->ms.sd;
    uint32_t i, tt = v->t;
    (void)t;
    for (i = 0; i < n; i++, tt++) {
        int32_t osc, y;
        if (!(tt & 31u) && tt >= 32u) {                 /* osc B: a block starts from its start phase */
            if (tt >= 64u)
                v->ph[2] += v->inc[1];
            v->ph[1] = v->ph[2];
        }
        osc = mulq15(sine_i(v->ph[0]), k->ga);
        if (k->gb)
            osc += mulq15(sine_i(v->ph[1]), k->gb);
        if (tt >= 32u) {                                /* (the first block frozen) */
            uint32_t inc = v->inc[0];
            if (k->bk)                                  /* BUMP: K (t / tb) e ^ (1 - t / tb) */
                inc += (uint32_t)(((uint64_t)(uint32_t)(((uint64_t)k->be * k->bu) >> 24) * (uint32_t)k->bk) >> 31);
            v->ph[0] += inc;
            v->ph[1] += v->inc[2];
        }
        if (k->bk) {
            if (k->bu > 0xC000000u)
                k->bk = 0;                              /* (gone past 12 x tb: < 0.01 Hz) */
            k->bu += k->du;
            ktrx_fall(&k->be, k->dbe);
        }
        if (k->drive) {                                 /* CLIP: tanh(d x) / tanh(d), d falling on its own */
            int32_t td = sdtrx_tanh((int32_t)(k->drive << 4));
            int32_t ty = (int32_t)(((int64_t)td * k->g) >> 24);
            k->g = (int32_t)(((int64_t)k->g * ((2 << 24) - ty)) >> 24);   /* Newton: g -> 1 / tanh(d) */
            osc = (int32_t)(((int64_t)sdtrx_tanh((int32_t)(((int64_t)k->drive * osc) >> 11)) * k->g) >> 33);
            ktrx_fall(&k->drive, SDTRX_D_C);
            if (k->drive < 4096u)
                k->drive = 0;                           /* (d < 1 / 16: clean, the 3rd harmonic < -66 dB) */
        }
        y = mulq15(mulq15(osc, SDTRX_AMP), (int32_t)(k->e_amp >> 16));
        if (k->e_n >= (1u << 17) || k->e_x >= (1u << 17)) {   /* SNAP: the band, the resonance (the same noise) */
            int32_t w = dnoise(v), bp, hp, lp;
            dsvf_tick(&SDTRX_HP, w, &v->f[0], &v->f[1], &bp, &hp);
            lp = dsvf_tick(&SDTRX_LP, hp, &v->f[2], &v->f[3], &bp, &hp);
            y += mulq15(mulq15(lp, k->nl), (int32_t)(k->e_n >> 16));
            ktrx_fall(&k->e_n, SDTRX_D_N);
            if (k->xl) {
                dsvf_tick(&SDTRX_BP, w, &k->x1, &k->x2, &bp, &hp);
                y += mulq15(mulq15((SDTRX_BP.k * bp) >> 12, k->xl), (int32_t)(k->e_x >> 16));
                ktrx_fall(&k->e_x, SDTRX_D_X);
            }
        }
        k->hy = (int32_t)(((int64_t)SDTRX_HPK * (k->hy + (y - k->hx) * 256)) >> 30);   /* the 40 Hz high-pass (8 more
                                                                                          * bits) */
        k->hx = y;
        y = SDTRX_GAIN(mulq15(mulq15(k->hy >> 8, vel_gain(v)), VOICE_FS));   /* dm_put, the gain after */
        out[i] += y;
        v->last = y;
        ktrx_fall(&k->e_amp, k->d_amp);
    }
    v->t = tt;
    if (k->e_amp < (1u << 17) && k->e_n < (1u << 17) && k->e_x < (1u << 17))
        dm_end(v);
}

#define DM_SDTRX_DEF {"SDTRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 7), \
    PD("SNAP", F_INT, 0, 127, 127), PD("TONE", F_INT, 0, 127, 0), PD("BUMP", F_INT, 0, 127, 0), \
    PD("BENV", F_INT, 0, 127, 64), PD("CLIP", F_INT, 0, 127, 92), PD("-", F_INT, 0, 0, 0)}, sdtrx_trigger, \
    sdtrx_render}
