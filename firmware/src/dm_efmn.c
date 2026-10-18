/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* SDEFM (snare), CPEFM (clap) and RSEFM (rim shot): the noise-FM voices of a classic digital drum machine's FM family,
 * built from measurements of an emulator's renders (md-re: knob by knob; only the measured laws are here, no code or
 * data of it). Each is a sine whose FREQUENCY is modulated by noise: the machine's FM noise is one fixed sequence,
 * the same on every hit, so here too (a fixed seed per hit); SDEFM's separate NOIS layer is random per hit (v->rng).
 * The output stage is the machine's track filter at its base patch, a 2-pole high-pass (21.3 Hz, Q 0.68).
 *   SDEFM / CPEFM: fc (1 + I(t) (0.35 cos(phi_m) + 0.85 n)), I(t) = MOD/16 e^(-t / tau_i(MDEC)), phi_m at fc MFRQ/16
 *     (MFRQ 0 = 1), n uniform white noise held 2 (SD) / 4 (CP) samples; a one-pole high-pass, pole 1 - HPF/256.
 *   RSEFM: two sines, fc (1 + K (MOD/16)(n + 0.03)) and fs (1 + Ks ((SMOD + 1)/16) n), n one-pole low-passed noise.
 * Every amplitude follows the family's DEC law, tau = 64.6 ms x 2 ^ ((DEC - 64) / 13.5) (2.4 ms .. 1.6 s), kept to
 * the end: past 5.5 s every envelope falls with tau 50 ms (the 6 s lifetime). TUNE (and a step's note) moves every
 * pitch by semitones; TUNE 0 is the machine's PTCH 64. Signals Q23 (1.0 = the prototype's unit), 64-bit products. */
#define EFMN_HP_B0 1071348723                           /* the track filter: RBJ high-pass 21.3 Hz, Q 0.68, Q30 */
#define EFMN_HP_A1 (-2142692512)
#define EFMN_HP_A2 1068960555
#define EFMN_SW 11469                                   /* the modulator: the sine's weight 0.35, Q15 */
#define EFMN_KZ 27853                                   /* the noise's 0.85 */
#define EFMN_END (1u << 17)                             /* a Q31 envelope's end (-84 dB) */
#define EFMN_LIFE ((uint32_t)FS * 11u / 2u)             /* 5.5 s: then every envelope falls ... */
#define EFMN_D_FADE 1947831u                            /* ... with tau 50 ms (Q32 step): ended by 5.9 s */
#define EFMN_SEED 0x12345678                            /* the FM noise: the same sequence on every hit */
#define EFMN_SEED2 0x2545F491                           /* RSEFM's second voice's */

/* the Q32 step of an exponential fall with time constant tau8 (Q8 samples, >= 16 samples): 2^32 (1 - e^(-1/tau)),
 * to the second order (the claps' ~18-sample taus need it); 32-bit divides only */
static uint32_t efmn_step(uint32_t tau8)
{
    uint32_t q = tau8 < (1u << 20) ? (0xFFFFFFFFu / tau8) << 8 : 0xFFFFFFFFu / (tau8 >> 8);
    return q - (uint32_t)(((uint64_t)q * q) >> 33);
}
/* the family's laws as a Q32 step: tau0 (Q8 samples) x 2 ^ (k x step / 192) (step: 1/192 octave per knob step,
 * x 1000) */
static uint32_t efmn_law(uint32_t tau0, int32_t k, int32_t step)
{
    int32_t d = k * step;
    return efmn_step((uint32_t)(((uint64_t)tau0 * pow2_q16((d + (d < 0 ? -500 : 500)) / 1000)) >> 16));
}
#define EFMN_DEC(d) efmn_law(729308u, clamp(d, 0, 127) - 64, 14222)    /* amplitude: 64.6 ms, /13.5 */
/* a Q31 envelope's fall (as ktrx_fall) */
static inline void efmn_fall(uint32_t *e, uint32_t d) { *e -= (uint32_t)(((uint64_t)*e * d) >> 32); }
/* uniform noise in [-1, 1): Q15 (SD, CP: (s >> 8) / 2^23 - 1 in the prototype, 15 bits of it), Q30 (RS: s / 2^31 - 1) */
static inline int32_t efmn_u15(int32_t *s) { return (int32_t)(noise32(s) >> 16) - 32768; }
static inline int32_t efmn_u30(int32_t *s) { return (int32_t)(noise32(s) >> 1) - (1 << 30); }
/* noise FM: the carrier's increment inc + dev4 x 4 x m (m Q30), mod 2^32 (a deviation past Nyquist folds, as the
 * machine's) */
static inline uint32_t efmn_inc(uint32_t inc, uint32_t dev4, int32_t m30)
{
    return inc + (uint32_t)(((int64_t)dev4 * m30) >> 28);
}
/* the track filter (direct form I, Q23 in and out) */
static inline int32_t efmn_hp2(efmn_hp2_t *h, int32_t x)
{
    int32_t y = (int32_t)(((int64_t)EFMN_HP_B0 * (x - 2 * h->x1 + h->x2) - (int64_t)EFMN_HP_A1 * h->y1 -
                           (int64_t)EFMN_HP_A2 * h->y2 + (1 << 29)) >> 30);
    h->x2 = h->x1;
    h->x1 = x;
    h->y2 = h->y1;
    h->y1 = y;
    return y;
}
/* the voice's output: Q23 -> Q15, velocity, voice level, the model's gain (Q8) */
static inline void efmn_put(dvoice_t *v, int32_t *out, uint32_t i, int32_t y, int32_t gain)
{
    int32_t x = clamp(y >> 8, -65535, 65535);
    x = (mulq15(mulq15(x, vel_gain(v)), VOICE_FS) * gain) >> 8;
    out[i] += x;
    v->last = x;
}

/* ---- SDEFM and CPEFM: the noise-FM carrier (shared) */
#define EFMN_I0 1264435u                                /* MDEC: tau_i 112 ms x 2 ^ ((m - 64) / 11.5), Q8 samples */
/* the carrier at tuned increment inc: MOD, MFRQ, MDEC, the noise's hold, the start phases */
static void efmn_fm_set(efmn_fm_t *f, uint32_t inc, int32_t mod, int32_t mfrq, int32_t mdec, uint32_t hold,
                        uint32_t pc0)
{
    f->pc = pc0;
    f->pm = 3748115076u;                                /* the sine modulator from -0.80 rad */
    f->inc = inc;
    f->incm = (uint32_t)(((uint64_t)inc * (uint32_t)(mfrq < 1 ? 1 : mfrq)) >> 4);   /* fc x max(MFRQ, 1) / 16 */
    f->dev = (uint32_t)(((uint64_t)inc * (uint32_t)mod) >> 6);   /* fc x MOD / 16, / 4 (fits 32 bits at +48) */
    f->ei = mod ? 0x7FFFFFFFu : 0;
    f->di = efmn_law(EFMN_I0, mdec - 64, 16696);
    f->fz = EFMN_SEED;
    f->hold = (uint8_t)hold;
}
/* one sample: the carrier's sine, then its phase moves on (the deviation decaying; nothing while it is gone) */
static inline int32_t efmn_fm_tick(efmn_fm_t *f)
{
    int32_t s = sine_i(f->pc);
    uint32_t inc = f->inc;
    if (f->ei >= EFMN_END) {
        if (!f->hc) {                                   /* the noise, held */
            f->nz = efmn_u15(&f->fz);
            f->hc = f->hold;
        }
        f->hc--;
        inc = efmn_inc(inc, (uint32_t)(((uint64_t)f->dev * f->ei) >> 31),
                       EFMN_SW * sine_i(f->pm + 0x40000000u) + EFMN_KZ * f->nz);
        f->pm += f->incm;
        efmn_fall(&f->ei, f->di);
    }
    f->pc += inc;
    return s;
}
/* the HPF knob: a one-pole high-pass, h = a h + (1 + a) / 2 (x - x'), a = 1 - HPF / 256 (unity at Nyquist) */
static inline int32_t efmn_hpf(efmn_t *k, int32_t y)
{
    k->hh = (int32_t)(((int64_t)k->hh * (256 - k->hk) >> 8) + ((int64_t)(y - k->hx) * (512 - k->hk) >> 9));
    k->hx = y;
    return k->hh;
}

/* ---- SDEFM: TUNE DECAY NOIS NDEC, MOD MFRQ MDEC HPF. The carrier at 290.1 Hz (PTCH 64), 0.2047 x DEC's decay, from
 * -3.04 rad; NOIS a separate white noise (random per hit), 1-pole low-pass 12 kHz and high-pass 400 Hz, rms 0.0562 x
 * NOIS / 127 at the hit, decaying with tau 25.4 ms x 2 ^ ((NDEC - 64) / 18.9) */
#define SDEFM_A 439589903u                              /* the body's level, Q31 (0.2047) */
#define SDEFM_NL 271750380                              /* NOIS 127: 0.0562 x sqrt(3) (uniform) x 1.30 (the filters' loss), Q31 */
#define SDEFM_CL 879482387                              /* its low-pass, 12 kHz: 1 - e^(-2 pi f / FS), Q30 */
#define SDEFM_AN 1014259954                             /* its high-pass, 400 Hz: the pole, Q30 */
#define SDEFM_AN2 1044000889                            /* (1 + pole) / 2 */
#define SDEFM_GAIN 256                                  /* the output gain, Q8: the defaults level with S909's */
static void sdefm_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    efmn_t *k = &v->ms.en;
    int32_t s = clamp(dm_tune(t), -48, 48), nz = clamp(p[2], 0, 127);
    efmn_fm_set(&k->fm, inc_tune(HZ(290.0835), s), clamp(p[4], 0, 127), clamp(p[5], 0, 127), clamp(p[6], 0, 127), 2u,
                2216928858u);                           /* -3.04 rad */
    k->ea = SDEFM_A;
    k->da = EFMN_DEC(p[1]);
    k->en = (uint32_t)(SDEFM_NL / 127 * nz);
    k->dn = efmn_law(286756u, clamp(p[3], 0, 127) - 64, 10159);   /* NDEC: 25.4 ms, /18.9 */
    k->hk = clamp(p[7], 0, 127);
}

static void sdefm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    efmn_t *k = &v->ms.en;
    uint32_t i;
    (void)t;
    if (v->t >= EFMN_LIFE) {                            /* the 6 s lifetime */
        k->da = k->da > EFMN_D_FADE ? k->da : EFMN_D_FADE;
        k->dn = k->dn > EFMN_D_FADE ? k->dn : EFMN_D_FADE;
    }
    for (i = 0; i < n; i++) {
        int32_t y = (int32_t)(((int64_t)efmn_fm_tick(&k->fm) * k->ea) >> 23);   /* Q15 x Q31 -> Q23 */
        if (k->en >= EFMN_END) {                        /* NOIS */
            int32_t w = (int32_t)(((int64_t)dnoise(v) * k->en) >> 23);
            k->nlp += (int32_t)(((int64_t)(w - k->nlp) * SDEFM_CL) >> 30);
            k->nhp = (int32_t)(((int64_t)k->nhp * SDEFM_AN + (int64_t)(k->nlp - k->nxp) * SDEFM_AN2) >> 30);
            k->nxp = k->nlp;
            y += k->nhp;
            efmn_fall(&k->en, k->dn);
        }
        if (k->hk)
            y = efmn_hpf(k, y);
        efmn_put(v, out, i, efmn_hp2(&k->hp, y), SDEFM_GAIN);
        efmn_fall(&k->ea, k->da);
    }
    v->t += n;
    if (k->ea < EFMN_END && k->en < EFMN_END)
        dm_end(v);
}

/* ---- CPEFM: TUNE DECAY CLPS CDEC, MOD MFRQ MDEC HPF. The carrier at 820.5 Hz (PTCH 64), from -3.06 rad, running
 * through floor(CLPS / 8) claps: each restarts the level at 0.2074 and decays with tau 0.393 x the period (the
 * measured table over CDEC: 45 .. 1809 samples, log-interpolated; the first ~2 samples short), then DEC's decay from
 * 0.2074. The FM as SDEFM's, the noise held 4 samples, the deviation timed from the hit */
#define CPEFM_A 445388109u                              /* 0.2074, Q31 */
#define CPEFM_GAIN 152                                  /* the output gain, Q8: the defaults level with C909's */
/* the clap period at CDEC 0, 16 .. 112, 127: 192 x log2(P / 45 samples) (measured P: 45 71 114 182 290 464 741 1185
 * 1809) */
static const int16_t CPEFM_PL[9] = {0, 126, 257, 387, 516, 646, 776, 906, 1023};
/* the period, Q16 samples */
static uint32_t cpefm_period(int32_t cd)
{
    int32_t j = cd >= 112 ? 7 : cd >> 4, l;
    l = CPEFM_PL[j] + (CPEFM_PL[j + 1] - CPEFM_PL[j]) * (j == 7 ? cd - 112 : cd & 15) / (j == 7 ? 15 : 16);
    return 45u * pow2_q16(l);
}
static void cpefm_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    efmn_t *k = &v->ms.en;
    int32_t s = clamp(dm_tune(t), -48, 48);
    efmn_fm_set(&k->fm, inc_tune(HZ(820.48), s), clamp(p[4], 0, 127), clamp(p[5], 0, 127), clamp(p[6], 0, 127), 4u,
                2203257553u);                           /* -3.06 rad */
    k->ea = k->ec = CPEFM_A;
    k->da = EFMN_DEC(p[1]);
    k->nb = (uint8_t)(clamp(p[2], 0, 127) >> 3);
    k->per = cpefm_period(clamp(p[3], 0, 127));
    k->tc = 65536u;                                     /* (the first clap 1 sample short of the period) */
    k->dc = efmn_step((uint32_t)(((uint64_t)k->per * 25756u) >> 24));   /* tau 0.393 x the period */
    k->hk = clamp(p[7], 0, 127);
}

static void cpefm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    efmn_t *k = &v->ms.en;
    uint32_t i;
    (void)t;
    if (v->t >= EFMN_LIFE)
        k->da = k->da > EFMN_D_FADE ? k->da : EFMN_D_FADE;
    for (i = 0; i < n; i++) {
        uint32_t e;
        int32_t y;
        if (k->nb) {                                    /* the claps */
            e = k->ec;
            efmn_fall(&k->ec, k->dc);
            if ((k->tc += 65536u) >= k->per) {
                k->tc -= k->per;
                k->ec = CPEFM_A;
                k->nb--;
            }
        } else {                                        /* then the main decay */
            e = k->ea;
            efmn_fall(&k->ea, k->da);
        }
        y = (int32_t)(((int64_t)efmn_fm_tick(&k->fm) * e) >> 23);
        if (k->hk)
            y = efmn_hpf(k, y);
        efmn_put(v, out, i, efmn_hp2(&k->hp, y), CPEFM_GAIN);
    }
    v->t += n;
    if (!k->nb && k->ea < EFMN_END)
        dm_end(v);
}

/* ---- RSEFM: TUNE DECAY MOD HPF, SNAR SPTC SDEC SMOD. The rim: 975.7 Hz (PTCH 64), from -1.61 rad, 0.2077 x DEC's
 * decay, its frequency x (1 + Kr (MOD / 16)(n + 0.03)), n white noise through a 1-pole 8 kHz low-pass, Kr 0.40 ..
 * 0.20 over its pitch. The second voice (SNAR's level, 0.2045 at 127; 0 = off): 121.9 Hz x 2 ^ (SPTC / 51.1)
 * (290.4 at 64), from -1.56 rad, SDEC's decay, x (1 + Ks ((SMOD + 1) / 16) n), its noise low-passed at 5 kHz, Ks 1.8 ..
 * 1.25. TUNE moves both (the machine's PTCH only the rim). HPF: a bilinear one-pole high-pass at the measured corners
 * (449 Hz at 16 .. 4583 Hz at 127; 0 = off). The very first sample 0.75 of the sines'. */
#define RSEFM_AR 446032354u                             /* the rim's level, Q31 (0.2077) */
#define RSEFM_AS 439160406u                             /* the second voice's at SNAR 127 (0.2045) */
#define RSEFM_LR 730272432                              /* the rim's noise low-pass, 8 kHz, Q30 */
#define RSEFM_LS 547097416                              /* the second voice's, 5 kHz */
#define RSEFM_BIAS 32212255                             /* the rim's noise bias, 0.03, Q30 */
#define RSEFM_GAIN 256                                  /* the output gain, Q8: the defaults level with RIM's */
/* Kr over the rim's pitch (1/16 semitones re 975.7 Hz: 244, 488, 976, 3819 Hz), Ks over the second voice's (re 121.9
 * Hz: 122, 291, 682 Hz); Q15 */
static const int16_t RSEFM_KRX[4] = {-384, -192, 0, 378}, RSEFM_KRY[4] = {13107, 11469, 10813, 6554};
static const int16_t RSEFM_KSX[3] = {0, 241, 477}, RSEFM_KSY[3] = {29491, 24576, 20480};   /* (Ks / 2: dev[1]'s
                                                         * shift one less) */
static int32_t rsefm_k(const int16_t *x, const int16_t *y, int n, int32_t d)   /* linear over log f (trigger time) */
{
    int i;
    if (d <= x[0])
        return y[0];
    for (i = 1; i < n; i++)
        if (d <= x[i])
            return y[i - 1] + (y[i] - y[i - 1]) * (d - x[i - 1]) / (x[i] - x[i - 1]);
    return y[n - 1];
}
/* the HPF's corner at 0, 16 .. 112, 127, Hz */
static const int16_t RSEFM_HF[9] = {0, 449, 932, 1456, 1991, 2603, 3206, 3891, 4583};
/* the second voice's pitch, 1/16 semitones re 121.91 Hz */
static inline int32_t rsefm_d16s(int32_t sptc, int32_t s) { return sptc * 37573 / 10000 + s * 16; }
static void rsefm_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    efmrs_t *k = &v->ms.er;
    int32_t s = clamp(dm_tune(t), -48, 48), sn = clamp(p[4], 0, 127), h = clamp(p[3], 0, 127), d16;
    k->pc[0] = 3194427202u;                             /* -1.61 rad */
    k->pc[1] = 3228605466u;                             /* -1.56 rad */
    k->inc[0] = inc_tune(HZ(975.72), s);
    k->dev[0] = (uint32_t)(((uint64_t)k->inc[0] * (uint32_t)(rsefm_k(RSEFM_KRX, RSEFM_KRY, 4, s * 16) *
                                                             clamp(p[2], 0, 127))) >> 21);   /* fc Kr MOD / 16, / 4 */
    d16 = rsefm_d16s(clamp(p[5], 0, 127), s);
    k->inc[1] = (uint32_t)(((uint64_t)HZ(121.91) * pow2_q16(d16)) >> 16);
    k->dev[1] = (uint32_t)(((uint64_t)k->inc[1] * (uint32_t)(rsefm_k(RSEFM_KSX, RSEFM_KSY, 3, d16) *
                                                             (clamp(p[7], 0, 127) + 1))) >> 20);
    k->e[0] = RSEFM_AR;
    k->d[0] = EFMN_DEC(p[1]);
    k->e[1] = (uint32_t)(RSEFM_AS / 127 * sn);
    k->d[1] = EFMN_DEC(p[6]);
    k->z[0] = EFMN_SEED;
    k->z[1] = EFMN_SEED2;
    k->g = 0;
    if (h) {                                            /* G = g / (1 + g), g = tan(pi fc / FS) (bilinear), Q30 */
        int32_t j = h >= 112 ? 7 : h >> 4, fc, g;
        fc = RSEFM_HF[j] + (RSEFM_HF[j + 1] - RSEFM_HF[j]) * (j == 7 ? h - 112 : h & 15) / (j == 7 ? 15 : 16);
        g = qtan_acc((int32_t)(((uint32_t)fc * 389567u) >> 10));   /* fc / FS, Q24 */
        k->g = (int32_t)(((int64_t)g * tn_recip(QONE + g)) >> 24);
    }
}

static void rsefm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    efmrs_t *k = &v->ms.er;
    uint32_t i;
    (void)t;
    if (v->t >= EFMN_LIFE) {
        k->d[0] = k->d[0] > EFMN_D_FADE ? k->d[0] : EFMN_D_FADE;
        k->d[1] = k->d[1] > EFMN_D_FADE ? k->d[1] : EFMN_D_FADE;
    }
    for (i = 0; i < n; i++) {
        int32_t y = 0;
        if (k->e[0] >= EFMN_END) {                      /* the rim */
            uint32_t inc = k->inc[0];
            y = (int32_t)(((int64_t)sine_i(k->pc[0]) * k->e[0]) >> 23);
            if (k->dev[0]) {
                k->n[0] += (int32_t)((((int64_t)efmn_u30(&k->z[0]) - k->n[0]) * RSEFM_LR) >> 30);
                inc = efmn_inc(inc, k->dev[0], k->n[0] + RSEFM_BIAS);
            }
            k->pc[0] += inc;
            efmn_fall(&k->e[0], k->d[0]);
        }
        if (k->e[1] >= EFMN_END) {                      /* the second voice */
            y += (int32_t)(((int64_t)sine_i(k->pc[1]) * k->e[1]) >> 23);
            k->n[1] += (int32_t)((((int64_t)efmn_u30(&k->z[1]) - k->n[1]) * RSEFM_LS) >> 30);
            k->pc[1] = efmn_inc(k->pc[1] + k->inc[1], k->dev[1], k->n[1]);
            efmn_fall(&k->e[1], k->d[1]);
        }
        if (!v->t && !i)
            y = y * 3 / 4;                              /* the first sample */
        if (k->g) {                                     /* HPF (TPT one-pole) */
            int32_t u = (int32_t)(((int64_t)(y - k->hs) * k->g) >> 30), lp = u + k->hs;
            k->hs = lp + u;
            y -= lp;
        }
        efmn_put(v, out, i, efmn_hp2(&k->hp, y), RSEFM_GAIN);
    }
    v->t += n;
    if (k->e[0] < EFMN_END && k->e[1] < EFMN_END)
        dm_end(v);
}

#define DM_SDEFM_DEF {"SDEFM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("NOIS", F_INT, 0, 127, 64), PD("NDEC", F_INT, 0, 127, 64), PD("MOD", F_INT, 0, 127, 64), \
    PD("MFRQ", F_INT, 0, 127, 64), PD("MDEC", F_INT, 0, 127, 64), PD("HPF", F_INT, 0, 127, 0)}, sdefm_trigger, \
    sdefm_render}
#define DM_CPEFM_DEF {"CPEFM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("CLPS", F_INT, 0, 127, 64), PD("CDEC", F_INT, 0, 127, 64), PD("MOD", F_INT, 0, 127, 64), \
    PD("MFRQ", F_INT, 0, 127, 64), PD("MDEC", F_INT, 0, 127, 64), PD("HPF", F_INT, 0, 127, 0)}, cpefm_trigger, \
    cpefm_render}
#define DM_RSEFM_DEF {"RSEFM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("MOD", F_INT, 0, 127, 64), PD("HPF", F_INT, 0, 127, 0), PD("SNAR", F_INT, 0, 127, 64), \
    PD("SPTC", F_INT, 0, 127, 64), PD("SDEC", F_INT, 0, 127, 64), PD("SMOD", F_INT, 0, 127, 64)}, rsefm_trigger, \
    rsefm_render}
