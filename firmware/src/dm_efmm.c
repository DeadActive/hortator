/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* CBEFM, HHEFM, CYEFM: a cowbell, a hat and a cymbal built from measurements of a classic digital drum machine's FM
 * voices (md-re: renders of an emulator, knob by knob; only the measured laws are here, no code or data of it; the
 * float models md-re/proto/efm-cb.c, efm-hh.c, efm-cy.c). Sine operators on the 1024-point table, phases 2^32.
 *   CBEFM: two tones 379/256 apart (1.4805), each a true-FM pair (the carrier's frequency f (1 + D(t) cos(modulator)),
 *     the modulator at f MFRQ/16, D = MOD / 32.65 falling over MDEC), phase feedback on each carrier (FB), one decay
 *     (DECAY) and SNAP's fast extra decay (4.5 ms). TUNE 0 is the machine's PTCH 64 (409.7 Hz). Fixed start phases
 *     (the machine's: it renders the same hit every time). 1 voice.
 *   HHEFM: one true-FM pair whose carrier phase jumps to a random value every 32 samples (a hat from an FM pair: the
 *     block rate's buzz and the jumps' clicks), FB fed back into the carrier's phase accumulator, then a near
 *     differentiator and a smoother (as measured), DECAY, a tremolo (TREM depth, TFRQ rate, restarting at each hit).
 *     TUNE 0 is the machine's PTCH 64 (577 Hz). The random phases from the hit's seed (the machine's sequence is its
 *     own). 1 voice, choke group 1 (as HATC).
 *   CYEFM: four PM pairs at 1 / 1.4103 / 1.8005 / 2.6997 x the base (580.05 Hz at TUNE 0, the machine's PTCH 64),
 *     each modulated at q x its carrier (q = 1.00055 (MFRQ / 16 + 0.5)) with the index (MOD + 7) / 13.5 falling over
 *     MDEC; FB phase feedback on each carrier (B = FB / 64); faded in over 16 samples; DECAY; HPF two one-pole
 *     high-passes (off at 0). Sidebands past Nyquist alias back, as the machine's do. Fixed start phases. 2 voices.
 * The machine's track high-pass (2-pole 21.3 Hz) is left out: the mixer's, a fraction of a dB at these pitches.
 * TUNE and a step's note move every frequency by semitones (the machines' PTCH: their own pitch laws, replaced). */
#define EFMM_RAD(r) ((uint32_t)(int32_t)((r) * 683565275.58))   /* radians (|r| < pi) -> 2^32 phase; constants only */
#define EFMM_D_END 1947838u                             /* Q32 step of a 50 ms decay: from 5.5 s a voice fades out */
#define EFMM_END 135500u                                /* -84 dB re a Q31 envelope's start: the voice ends */
#define EFMM_COS 0x40000000u                            /* + 90 degrees */

/* an exponential's Q32 step from its time constant (Q4 samples): 1 - e ^ (-1 / tau) ~ 1 / (tau + 1/2) */
static uint32_t efmm_step(uint32_t tq4) { return (0xFFFFFFFFu / (tq4 + 8u)) << 4; }
/* base (Q4 samples) x 2 ^ (d16 / 192) */
static uint32_t efmm_tau(uint32_t base, int32_t d16) { return (uint32_t)(((uint64_t)base * pow2_q16(d16)) >> 16); }
/* the family's DEC law, tau 64.6 ms x 2 ^ ((DEC - 64) / 13.5), as a step */
static uint32_t efmm_dec(int32_t d) { return efmm_step(efmm_tau(45582u, (clamp(d, 0, 127) - 64) * 14222 / 1000)); }
/* EFM-BD's MDEC law (CB, HH), tau 112 ms x 2 ^ ((MDEC - 64) / 11.5) */
static uint32_t efmm_mdec(int32_t d) { return efmm_step(efmm_tau(79027u, (clamp(d, 0, 127) - 64) * 16696 / 1000)); }
/* a Q31 envelope's step, rounded up (it never stalls above its end) */
static inline uint32_t efmm_fall(uint32_t e, uint32_t d) { return e - (uint32_t)(((uint64_t)e * d + 0xFFFFFFFFu) >> 32); }
/* the deviation's floor: below 1e-5 of the carrier's frequency (Q28) the modulators stop (a sideband under -76 dB) */
#define EFMM_EI_MIN 2684u

/* ---- CBEFM */
#define CBEFM_G2 33423                                  /* tone 2's level re tone 1's (1.02), Q15 */
#define CBEFM_LVL 1273                                  /* the output's level, Q12: the defaults level with COWB's */
#define CBEFM_NS 3969u                                  /* SNAP's fast part: 20 time constants (90 ms), then constant */

static void cbefm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    cbefm_t *c = &v->ms.cbe;
    uint32_t i, tt = v->t, pc0 = c->pc[0], pc1 = c->pc[1], pm0 = c->pm[0], pm1 = c->pm[1];
    uint32_t ea = c->ea, es = c->es, ei = c->ei, da;
    const uint32_t i0 = c->inc[0], i1 = c->inc[1], mi0 = c->minc[0], mi1 = c->minc[1], ds = c->ds, di = c->di;
    const int32_t bq = c->bq, sn = c->sn, sk = c->sk, lvl = c->lvl, vg = vel_gain(v);
    int32_t y0 = c->y[0], y1 = c->y[1], z = 0;
    (void)t;
    if (tt >= LIFE_A)                                   /* the 6 s life: DECAY 127 fades out from 5.5 s */
        c->da = EFMM_D_END;
    da = c->da;
    for (i = 0; i < n; i++, tt++) {
        int32_t x, eg;
        y0 = sine_i(pc0 + (uint32_t)(bq * y0));         /* the carriers, their phase feedback */
        y1 = sine_i(pc1 + (uint32_t)(bq * y1));
        if (ei) {                                       /* true FM: f (1 + D(t) m) */
            int32_t e0 = (int32_t)(((int64_t)ei * (2 * sine_i(pm0 + EFMM_COS) + 1)) >> 16);   /* D m, Q28 (m + 1/2:
                                                         * sine_i rounds down, a drift in the carrier's pitch) */
            int32_t e1 = (int32_t)(((int64_t)ei * (2 * sine_i(pm1 + EFMM_COS) + 1)) >> 16);
            pc0 += i0 + (uint32_t)(((int64_t)i0 * e0) >> 28);
            pc1 += i1 + (uint32_t)(((int64_t)i1 * e1) >> 28);
            pm0 += mi0;
            pm1 += mi1;
            ei = efmm_fall(ei, di);
        } else {
            pc0 += i0;
            pc1 += i1;
        }
        x = y0 + ((y1 * CBEFM_G2) >> 15);
        eg = (int32_t)(ea >> 1);                        /* Q30 */
        if (sn) {                                       /* SNAP: x (1 - s + s e ^ (-t / 4.5 ms)) */
            int32_t g = sk;
            if (tt < CBEFM_NS) {
                g += (int32_t)(((int64_t)sn * es) >> 31);
                es = efmm_fall(es, ds);
            }
            eg = (int32_t)(((int64_t)eg * g) >> 30);
        }
        x = (int32_t)(((int64_t)x * eg) >> 30);
        ea = efmm_fall(ea, da);
        z = mulq15(mulq15(clamp((x * lvl) >> 12, -65535, 65535), vg), VOICE_FS);   /* dm_put */
        out[i] += z;
    }
    v->last = z;
    v->t = tt;
    c->pc[0] = pc0, c->pc[1] = pc1, c->pm[0] = pm0, c->pm[1] = pm1;
    c->y[0] = y0, c->y[1] = y1;
    c->ea = ea, c->es = es;
    c->ei = ei < EFMM_EI_MIN ? 0 : ei;
    if (ea < EFMM_END)
        dm_end(v);
}

/* CBEFM: TUNE DECAY SNAP FB, MOD MFRQ MDEC */
static void cbefm_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    cbefm_t *c = &v->ms.cbe;
    int32_t s = clamp(dm_tune(t), -48, 48), mf = clamp(p[5], 1, 127), k;   /* (MFRQ 0 renders as 1) */
    uint32_t f1 = HZ(409.729);                          /* PTCH 64: 172.27 Hz x 2 ^ (64 / 51.2) */
    if (s)
        f1 = inc_tune(f1, s);
    c->inc[0] = f1;
    c->inc[1] = (uint32_t)(((uint64_t)f1 * 379u) >> 8);   /* tone 2: x 379 / 256 */
    for (k = 0; k < 2; k++) {
        c->minc[k] = (uint32_t)(((uint64_t)c->inc[k] * (uint32_t)mf) >> 4);   /* x MFRQ / 16 (past Nyquist: aliased) */
        c->pc[k] = 0x80000000u;                         /* the carriers at pi, */
        c->y[k] = 0;
    }
    c->pm[0] = EFMM_RAD(-1.644);                        /* the modulators (cos) at -1.644 / -1.676 rad */
    c->pm[1] = EFMM_RAD(-1.676);
    c->bq = -(clamp(p[3], 0, 127) * 37549 + 50) / 100;  /* FB: B = -0.0180 FB rad, phase units per Q15 */
    c->ei = (uint32_t)clamp(p[4], 0, 127) * 8221607u;   /* MOD: D = MOD / 32.65, Q28 */
    c->di = efmm_mdec(p[6]);
    c->ea = 0x7FFFFFFFu;
    c->da = efmm_dec(p[1]);
    c->sn = clamp(p[2], 0, 127) * 6839120;              /* SNAP: s = SNAP / 157, Q30 */
    c->sk = (1 << 30) - c->sn;
    c->es = 0x7FFFFFFFu;
    c->ds = efmm_step(3175u);                           /* tau 4.5 ms */
    c->lvl = CBEFM_LVL;
}

/* ---- HHEFM */
#define HHEFM_A1 239981298                              /* the differentiator's pole 0.2235, Q30 */
#define HHEFM_A2 905164358                              /* the smoother: 1 - 0.157, Q30 */
#define HHEFM_LVL 151                                  /* the output's level, Q12 (x the measured 0.91): the defaults
                                                         * level with HATC's */

/* the tremolo's gain at phase ph: 1 - depth (1 - cos) / 2, Q30 */
static inline int32_t hhefm_trem(int32_t dep, uint32_t ph)
{
    return (1 << 30) - (int32_t)(((int64_t)dep * (32768 - sine_i(ph + EFMM_COS))) >> 16);
}

static void hhefm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    hhefm_t *c = &v->ms.hhe;
    uint32_t i, tt = v->t, pc = c->pc, pm = c->pm, ea = c->ea, ei = c->ei, tp = c->tp, da;
    const uint32_t inc = c->inc, minc = c->minc, di = c->di, tinc = c->tinc;
    const int32_t bq = c->bq, dep = c->dep, lvl = c->lvl, vg = vel_gain(v);
    int32_t h = c->h, up = c->up, l = c->l, g = c->g, gs = c->gs, rng = v->rng, z = 0;
    (void)t;
    if (tt >= LIFE_A)
        c->da = EFMM_D_END;
    da = c->da;
    for (i = 0; i < n; i++, tt++) {
        int32_t u, x, eg;
        if (!(tt & 31u)) {                              /* every 32-sample block: the carrier's phase at random */
            pc = noise32(&rng);
            if (dep) {                                  /* the tremolo: its block's line (its fastest period 750
                                                         * samples) */
                g = hhefm_trem(dep, tp);
                gs = (hhefm_trem(dep, tp + (tinc << 5)) - g) >> 5;
                tp += tinc << 5;
            }
        }
        u = sine_i(pc);
        pc += inc + (uint32_t)bq * (uint32_t)u;         /* FB: beta sin(phase) into the accumulator (mod 2 pi) */
        if (ei) {                                       /* true FM */
            int32_t e = (int32_t)(((int64_t)ei * (2 * sine_i(pm + EFMM_COS) + 1)) >> 16);   /* I m, Q27 (m + 1/2) */
            pc += (uint32_t)(((int64_t)inc * e) >> 27);
            pm += minc;
            ei = efmm_fall(ei, di);
        }
        h = (int32_t)(((int64_t)(h + (u - up) * 256) * HHEFM_A1) >> 30);   /* h = a (h + u - u'), Q23 */
        up = u;
        l += (int32_t)(((int64_t)(h - l) * HHEFM_A2) >> 30);
        eg = (int32_t)(ea >> 1);                        /* Q30 */
        if (dep) {
            eg = (int32_t)(((int64_t)eg * g) >> 30);
            g += gs;
        }
        x = (int32_t)(((int64_t)l * eg) >> 34);         /* Q19 */
        ea = efmm_fall(ea, da);
        z = mulq15(mulq15(clamp((x * lvl) >> 12, -65535, 65535), vg), VOICE_FS);   /* dm_put */
        out[i] += z;
    }
    v->last = z;
    v->t = tt;
    v->rng = rng;
    c->pc = pc, c->pm = pm, c->ea = ea, c->tp = tp;
    c->h = h, c->up = up, c->l = l, c->g = g, c->gs = gs;
    c->ei = ei < EFMM_EI_MIN / 2u ? 0 : ei;
    if (ea < EFMM_END)
        dm_end(v);
}

/* HHEFM: TUNE DECAY TREM TFRQ, MOD MFRQ MDEC FB */
static void hhefm_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    hhefm_t *c = &v->ms.hhe;
    int32_t s = clamp(dm_tune(t), -48, 48), mf = clamp(p[5], 1, 127);
    uint32_t f = HZ(576.979);                           /* PTCH 64: 243.0 Hz x 2 ^ (64 / 51.3) */
    if (s)
        f = inc_tune(f, s);
    c->inc = f;
    c->minc = (uint32_t)(((uint64_t)f * (uint32_t)mf) >> 4);   /* x MFRQ / 16 */
    c->pm = EFMM_RAD(-0.8);
    c->ei = (uint32_t)clamp(p[4], 0, 127) << 23;        /* MOD: I = MOD / 16, Q27 */
    c->di = efmm_mdec(p[6]);
    c->bq = -(clamp(p[7], 0, 127) * 10222 + 5) / 10;    /* FB: beta = -0.049 FB rad, phase units per Q15 */
    c->ea = 0x7FFFFFFFu;
    c->da = efmm_dec(p[1]);
    c->dep = clamp(p[2], 0, 127) * 8454660;             /* TREM: depth TREM / 127, Q30 */
    c->tinc = (uint32_t)(((uint64_t)749166u * pow2_q16((clamp(p[3], 0, 127) - 64) * 8972 / 1000)) >> 16);   /* TFRQ:
                                                         * the period 130 ms x 2 ^ (-(TFRQ - 64) / 21.4) */
    c->tp = 0;
    c->g = 1 << 30;
    c->gs = 0;
    c->h = c->up = c->l = 0;
    c->lvl = HHEFM_LVL;
}

/* ---- CYEFM */
static const uint32_t CYEFM_R[4] = {16777216u, 23660908u, 30207377u, 45293450u};   /* the carriers' ratios, Q24 */
static const int16_t CYEFM_HPV[9] = {0, 16, 32, 48, 64, 80, 96, 112, 127};          /* HPF: its cutoff (Hz) there */
static const int16_t CYEFM_HPF[9] = {80, 431, 780, 1142, 1522, 1920, 2339, 2780, 3216};
#define CYEFM_LVL 170                                  /* the output's level, Q12: the defaults level with CYMB's */

static void cyefm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    cyefm_t *c = &v->ms.cye;
    uint32_t i, tt = v->t, ea = c->ea, da;
    uint32_t pc0 = c->pc[0], pc1 = c->pc[1], pc2 = c->pc[2], pc3 = c->pc[3];
    uint32_t pm0 = c->pm[0], pm1 = c->pm[1], pm2 = c->pm[2], pm3 = c->pm[3];
    const uint32_t w0 = c->wc[0], w1 = c->wc[1], w2 = c->wc[2], w3 = c->wc[3];
    const uint32_t m0 = c->wm[0], m1 = c->wm[1], m2 = c->wm[2], m3 = c->wm[3], di = c->di;
    const int32_t bq = c->bq, gk = c->gk, lvl = c->lvl, vg = vel_gain(v);
    int32_t y0 = c->yp[0], y1 = c->yp[1], y2 = c->yp[2], y3 = c->yp[3], s1 = c->s1, s2 = c->s2, z = 0;
    uint32_t ih = c->ih;
    (void)t;
    if (tt >= LIFE_A)
        c->da = EFMM_D_END;
    da = c->da;
    for (i = 0; i < n; i++, tt++) {
        uint32_t ix = ih >> 12;                         /* the index, phase units per Q15 */
        int32_t y, ye;
        y0 = sine_i(pc0 + ix * (uint32_t)sine_i(pm0 + EFMM_COS) + (uint32_t)(bq * y0));   /* sin(phase + I cos(mod)
                                                         * + B y'), all mod 2 pi */
        y1 = sine_i(pc1 + ix * (uint32_t)sine_i(pm1 + EFMM_COS) + (uint32_t)(bq * y1));
        y2 = sine_i(pc2 + ix * (uint32_t)sine_i(pm2 + EFMM_COS) + (uint32_t)(bq * y2));
        y3 = sine_i(pc3 + ix * (uint32_t)sine_i(pm3 + EFMM_COS) + (uint32_t)(bq * y3));
        pc0 += w0, pc1 += w1, pc2 += w2, pc3 += w3;
        pm0 += m0, pm1 += m1, pm2 += m2, pm3 += m3;
        y = y0 + y1 + y2 + y3;
        if (tt < 16u)                                   /* faded in over 16 samples: (t + 1) / 17 */
            y = (int32_t)(((int64_t)y * (int32_t)((tt + 1u) * 3855u)) >> 16);
        ye = (int32_t)(((int64_t)y * ea) >> 25);        /* Q21 (4 x Q15 in) */
        ih = efmm_fall(ih, di);
        ea = efmm_fall(ea, da);
        if (gk) {                                       /* HPF: two one-pole (TPT) high-passes */
            int32_t w = (int32_t)(((int64_t)(ye - s1) * gk) >> 30), lp = w + s1;
            s1 = lp + w;
            ye -= lp;
            w = (int32_t)(((int64_t)(ye - s2) * gk) >> 30), lp = w + s2;
            s2 = lp + w;
            ye -= lp;
        }
        z = mulq15(mulq15(clamp((ye >> 6) * lvl >> 12, -65535, 65535), vg), VOICE_FS);   /* dm_put */
        out[i] += z;
    }
    v->last = z;
    v->t = tt;
    c->pc[0] = pc0, c->pc[1] = pc1, c->pc[2] = pc2, c->pc[3] = pc3;
    c->pm[0] = pm0, c->pm[1] = pm1, c->pm[2] = pm2, c->pm[3] = pm3;
    c->yp[0] = y0, c->yp[1] = y1, c->yp[2] = y2, c->yp[3] = y3;
    c->s1 = s1, c->s2 = s2, c->ea = ea, c->ih = ih;
    if (ea < EFMM_END)
        dm_end(v);
}

/* CYEFM: TUNE DECAY FB HPF, MOD MFRQ MDEC */
static void cyefm_trigger(track_t *t, dvoice_t *v)
{
    static const uint32_t PC[4] = {2421998441u, 2370047480u, 2343388434u, 2363895392u};   /* the start: -2.740 rad
                                                         * (+ -0.076 / -0.115 / -0.085), */
    static const uint32_t PM[4] = {3496563054u, 3553298972u, 3661985851u, 3809635950u};   /* the modulators (cos)
                                                         * -1.168 (+ 0.083 / 0.242 / 0.458) */
    const int16_t *p = &t->p[P_E0];
    cyefm_t *c = &v->ms.cye;
    int32_t s = clamp(dm_tune(t), -48, 48), hp = clamp(p[3], 0, 127), k;
    uint32_t f = HZ(580.05), q = (uint32_t)(((uint64_t)16786443u * (uint32_t)(clamp(p[5], 0, 127) + 8)) >> 4);   /* q:
                                                         * 1.00055 (MFRQ / 16 + 0.5), Q24 */
    if (s)
        f = inc_tune(f, s);
    for (k = 0; k < 4; k++) {
        c->wc[k] = (uint32_t)(((uint64_t)f * CYEFM_R[k]) >> 24);
        c->wm[k] = (uint32_t)(((uint64_t)c->wc[k] * q) >> 24);   /* (past Nyquist: aliased, as the machine's) */
        c->pc[k] = PC[k];
        c->pm[k] = PM[k];
        c->yp[k] = 0;
    }
    c->ih = (uint32_t)(clamp(p[4], 0, 127) + 7) * 6329309u;   /* MOD: I = (MOD + 7) / 13.5 rad, phase units per Q15
                                                         * << 12 */
    c->di = efmm_step(efmm_tau(194040u, clamp(p[6], 0, 127) * 8));   /* MDEC: tau 275 ms x 2 ^ (MDEC / 24) */
    c->bq = (clamp(p[2], 0, 127) * 32595 + 50) / 100;   /* FB: B = FB / 64 rad, phase units per Q15 */
    c->ea = 0x7FFFFFFFu;
    c->da = efmm_dec(p[1]);
    c->gk = 0;
    if (hp) {                                           /* HPF: the measured cutoffs, linear between them */
        int32_t j = 0, hz, g;
        while (j < 7 && hp > CYEFM_HPV[j + 1])
            j++;
        hz = CYEFM_HPF[j] + (CYEFM_HPF[j + 1] - CYEFM_HPF[j]) * (hp - CYEFM_HPV[j]) / (CYEFM_HPV[j + 1] - CYEFM_HPV[j]);
        g = trxm_tan((hz * 389565) >> 10);            /* tan(pi fc / FS), Q24 (2^24 / 44100 = 380.43) */
        c->gk = qm(g, trxm_inv(QONE + g)) << 6;         /* g / (1 + g), Q30 */
    }
    c->s1 = c->s2 = 0;
    c->lvl = CYEFM_LVL;
}

#define DM_CBEFM_DEF {"CBEFM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("SNAP", F_INT, 0, 127, 0), PD("FB", F_INT, 0, 127, 0), PD("MOD", F_INT, 0, 127, 64), \
    PD("MFRQ", F_INT, 0, 127, 64), PD("MDEC", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0)}, cbefm_trigger, \
    cbefm_render}
#define DM_HHEFM_DEF {"HHEFM", 1, 1, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("TREM", F_INT, 0, 127, 0), PD("TFRQ", F_INT, 0, 127, 64), PD("MOD", F_INT, 0, 127, 64), \
    PD("MFRQ", F_INT, 0, 127, 64), PD("MDEC", F_INT, 0, 127, 64), PD("FB", F_INT, 0, 127, 0)}, hhefm_trigger, \
    hhefm_render}
#define DM_CYEFM_DEF {"CYEFM", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("FB", F_INT, 0, 127, 0), PD("HPF", F_INT, 0, 127, 0), PD("MOD", F_INT, 0, 127, 64), \
    PD("MFRQ", F_INT, 0, 127, 64), PD("MDEC", F_INT, 0, 127, 64), PD("-", F_INT, 0, 0, 0)}, cyefm_trigger, \
    cyefm_render}
