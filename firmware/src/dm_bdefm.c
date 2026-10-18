/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* BDEFM (FM bass drum) and XTEFM (FM tom): one voice, two measured parameter sets, built from measurements of a
 * classic digital drum machine's FM bass drum and FM tom (md-re: renders of an emulator, knob by knob; only the
 * measured laws are here, no code or data of it). Two sine operators in TRUE FM (the carrier's frequency is modulated,
 * not its phase): the carrier's frequency fc (1 + MOD/16 x e^(-t/tau_i) x m), the modulator m = cos(phi_m + B m_prev)
 * at fc x max(MFRQ, 1)/16 with phase feedback B (BDEFM's MFB: pi MFB/64; XTEFM a fixed 2 pi); one pitch envelope on
 * both operators, fc = f0 (1 + (RAMP/16 - 1) e^(-t/tau_r)) (RAMP 16 flat; below it the pitch rises, from 0 Hz at
 * RAMP 0); the amplitude e^(-t/tau_a); then the base patch's 2-pole high-pass (21.3 Hz, Q 0.68). XTEFM: the pitch at
 * half BDEFM's slope (TUNE 0 = 86.1 Hz, BDEFM's 102.6 Hz), CLIC moves the carrier's start phase from pi to 3 pi / 2,
 * and a one-pole 121 Hz high-pass inside the voice; its FM is a stand-in (deviation MOD/18, the chaotic modulator):
 * the machine's own is not understood (md-re docs/EFM.md), MOD 0 is exact. The phases start where the machine's do
 * (fixed), the chaotic feedback (BDEFM's MFB above ~40, XTEFM's FM) has the machine's character, not its trajectory.
 * Past Nyquist (TUNE + notes up to +48, MOD deep) the increments wrap as the machine's would: it aliases. 1 voice. */
#define BDEFM_RAD(r) ((uint32_t)(int32_t)((r) * 683565275.58))   /* radians -> 2^32 phase; constants only */
#define BDEFM_TAU_MAX (25578u << 4)                     /* 0.58 s: 4 s to -60 dB, as DECAY_K's end (the 6 s life) */
#define BDEFM_TAU_NONE 0xFFFFFFFFu                      /* (RDEC / MDEC: no cap, 5 s at most) */
/* the base patch's 2-pole high-pass, RBJ 21.3 Hz Q 0.68 (b0 = -b1 / 2 = b2, a1, a2), Q30 */
#define BDEFM_HB0 1071348723
#define BDEFM_HA1 (-2142692512)
#define BDEFM_HA2 1068960555
#define BDEFM_XHP 1055389614                            /* XTEFM's one-pole high-pass: e^(-2 pi 121 / FS), Q30 */
typedef struct {
    uint32_t f0;                        /* the pitch at TUNE 0 (the machine's PTCH 64), phase increment */
    uint32_t tau_a;                     /* DEC 64's time constant, samples x 16 */
    uint32_t pc;                        /* the carrier's start phase (XTEFM: + CLIC) */
    int32_t i16;                        /* the deviation per MOD step, Q27 (1/16, XTEFM's stand-in 1/18) */
    int32_t g8;                         /* the output gain after the voice level, Q8: the defaults level with K909's /
                                         * TOM's */
} bdefm_mach_t;
static const bdefm_mach_t BDEFM_M[2] = {
    {HZ(102.56), 45610u, 0xA0000000u, 1 << 23, 578},             /* EFM-BD: 25.64 Hz x 2^(64/32), -3 pi / 4 */
    {HZ(86.133), 45581u, 0x80000000u, 7456540, 1060},             /* EFM-XT: its table at 64, pi (+ CLIC) */
};

/* a measured decay law tau0 x 2 ^ ((knob - 64) / per) as an envelope's Q32 step (1 / tau a sample): tau0 in samples x
 * 16 (the short ones need the fraction), k100 = 100 x 192 / per (pow2_q16's 1/192 octaves per knob step), capped */
static uint32_t bdefm_step(uint32_t tau0, int32_t knob, int32_t k100, uint32_t cap)
{
    int32_t x = (clamp(knob, 0, 127) - 64) * k100;
    uint64_t ts = ((uint64_t)tau0 * pow2_q16((x + (x < 0 ? -50 : 50)) / 100)) >> 16;
    return (0xFFFFFFFFu / (uint32_t)(ts > cap ? cap : ts)) << 4;   /* (tau >= 2.4 ms: no overflow) */
}
/* sin, linearly interpolated as sine_i, Q19 (a sub-audio carrier moves in sine_i's Q15 steps: under the high-passes
 * they would be heard as clicks) */
static inline int32_t bdefm_sin19(uint32_t ph)
{
    uint32_t i = ph >> 22;
    int32_t a = SINE[i], b = SINE[(i + 1u) & 1023u];
    return (a << 4) + (((b - a) * (int32_t)((ph >> 7) & 0x7FFFu)) >> 11);
}
/* a Q31 envelope falling by 1 / tau each sample, the step Q32 (as KTRX's) */
static inline void bdefm_fall(uint32_t *e, uint32_t d32) { *e -= (uint32_t)(((uint64_t)*e * d32) >> 32); }

/* BDEFM: TUNE DECAY RAMP RDEC, MOD MFRQ MDEC MFB; XTEFM: the same with CLIC for MFB */
static void bdefm_trigger_m(track_t *t, dvoice_t *v, uint32_t mc)
{
    const int16_t *p = &t->p[P_E0];
    const bdefm_mach_t *m = &BDEFM_M[mc];
    bdefm_t *b = &v->ms.bdefm;
    int32_t mf = clamp(p[5], 0, 127);
    b->xt = (uint8_t)mc;
    b->inc0 = inc_tune(m->f0, dm_tune(t));
    b->rk = clamp(p[2], 0, 127) - 16;                   /* RAMP: the start pitch x RAMP / 16 */
    b->e_a = b->e_r = b->e_i = 0x7FFFFFFFu;             /* Q31 */
    b->d_a = bdefm_step(m->tau_a, p[1], 1422, BDEFM_TAU_MAX);   /* DEC: tau 64.6 ms x 2 ^ ((d - 64) / 13.5), capped */
    b->d_r = bdefm_step(57718u, p[3], 1515, BDEFM_TAU_NONE);   /* RDEC: tau 81.8 ms x 2 ^ ((d - 64) / 12.67) */
    b->d_i = bdefm_step(79027u, p[6], 1670, BDEFM_TAU_NONE);   /* MDEC: tau 112 ms x 2 ^ ((d - 64) / 11.5) */
    b->i0 = clamp(p[4], 0, 127) * m->i16;               /* MOD: the peak deviation re the carrier, Q27 */
    b->mf = mf < 1 ? 1 : mf;                            /* MFRQ: the modulator at fc x MFRQ / 16 (0 = 1) */
    b->fb = mc ? 128 : clamp(p[7], 0, 127);             /* MFB: B = pi MFB / 64 (XTEFM: 2 pi) */
    v->ph[0] = m->pc + (mc ? (uint32_t)clamp(p[7], 0, 127) * 8454660u : 0u);   /* XTEFM's CLIC: + (pi / 2) CLIC / 127 */
    v->ph[1] = BDEFM_RAD(-0.80);                        /* the modulator (cos) at -0.80 rad */
}
static void bdefm_trigger(track_t *t, dvoice_t *v) { bdefm_trigger_m(t, v, 0); }
static void xtefm_trigger(track_t *t, dvoice_t *v) { bdefm_trigger_m(t, v, 1); }

static void bdefm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    bdefm_t *b = &v->ms.bdefm;
    const int32_t g8 = BDEFM_M[b->xt].g8, vg = vel_gain(v);
    uint32_t i;
    (void)t;
    for (i = 0; i < n; i++) {
        /* the pitch: f0 (1 + (RAMP / 16 - 1) e_r), the multiplier Q27 (0 .. 7.94) */
        uint32_t fc = (uint32_t)(((uint64_t)b->inc0 * (uint32_t)((1 << 27) + b->rk * (int32_t)(b->e_r >> 8))) >> 27);
        uint32_t inc = fc;
        int32_t y, s = bdefm_sin19(v->ph[0]);
        if (b->i0) {                                    /* the FM, while its deviation is heard */
            int32_t ie = (int32_t)(((uint64_t)(uint32_t)b->i0 * b->e_i) >> 31);   /* MOD e_i, Q27 */
            int32_t md = bdefm_sin19(v->ph[1] + 0x40000000u + ((uint32_t)(b->fb * b->m) << 6));   /* cos, Q19, its
                                                         * feedback pi fb / 64 x the last */
            int32_t d = (int32_t)(((int64_t)ie * md) >> 19);   /* the deviation re fc, Q27 */
            b->m = md;
            inc += (uint32_t)(((int64_t)fc * d) >> 27); /* (past Nyquist the increment wraps: aliasing) */
            v->ph[1] += (uint32_t)(((uint64_t)fc * (uint32_t)b->mf) >> 4);
            bdefm_fall(&b->e_i, b->d_i);
            if (b->e_i < (0x7FFFFFFFu >> 12))           /* -72 dB: the FM is over */
                b->i0 = 0;
        }
        v->ph[0] += inc;
        y = (int32_t)(((int64_t)s * b->e_a) >> 23);     /* Q27 (the high-passes' states need the bits) */
        if (b->xt) {                                    /* XTEFM's 121 Hz one-pole high-pass */
            b->hy = (int32_t)(((int64_t)BDEFM_XHP * (b->hy + y - b->hx) + (1 << 29)) >> 30);
            b->hx = y;
            y = b->hy;
        }
        {                                               /* the base patch's 21.3 Hz 2-pole high-pass */
            int64_t acc = (int64_t)BDEFM_HB0 * (y - 2 * b->x1 + b->x2) - (int64_t)BDEFM_HA1 * b->y1 -
                          (int64_t)BDEFM_HA2 * b->y2 + 2 * (int64_t)b->q1 - b->q2;
            int32_t o = (int32_t)(acc >> 30);           /* (the last two remainders fed back, 2 q1 - q2: the poles
                                                         * sit at z ~ 1, a plain rounding's error comes out x 5e4 at
                                                         * DC and holds a dead band; this way it is shaped away) */
            b->q2 = b->q1;
            b->q1 = (int32_t)(acc - ((int64_t)o << 30));
            b->x2 = b->x1;
            b->x1 = y;
            b->y2 = b->y1;
            b->y1 = o;
            o = (mulq15(mulq15(clamp(o >> 13, -65535, 65535), vg), VOICE_FS) * g8) >> 8;   /* (dm_put, the gain
                                                         * after: Q14 in) */
            out[i] += o;
            v->last = o;
        }
        bdefm_fall(&b->e_r, b->d_r);
        bdefm_fall(&b->e_a, b->d_a);
    }
    v->t += n;
    if (b->e_a < (ENV_END << 7) && b->y1 < 8192 && b->y1 > -8192)   /* -84 dB, the high-pass' tail too */
        dm_end(v);
}

#define DM_BDEFM_DEF {"BDEFM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("RAMP", F_INT, 0, 127, 0), PD("RDEC", F_INT, 0, 127, 64), PD("MOD", F_INT, 0, 127, 64), \
    PD("MFRQ", F_INT, 0, 127, 64), PD("MDEC", F_INT, 0, 127, 64), PD("MFB", F_INT, 0, 127, 0)}, bdefm_trigger, \
    bdefm_render}
#define DM_XTEFM_DEF {"XTEFM", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("RAMP", F_INT, 0, 127, 0), PD("RDEC", F_INT, 0, 127, 64), PD("MOD", F_INT, 0, 127, 64), \
    PD("MFRQ", F_INT, 0, 127, 64), PD("MDEC", F_INT, 0, 127, 64), PD("CLIC", F_INT, 0, 127, 0)}, xtefm_trigger, \
    bdefm_render}
