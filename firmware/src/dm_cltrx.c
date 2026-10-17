/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* CLTRX: claves built from measurements of a classic digital drum machine's (md-re: renders of an emulator, knob by
 * knob; only the measured laws are here, no code or data of it). Two pure sines on one exponential decay: Y (TUNE2)
 * from the hit, X (TUNE) one 32-sample block later plus DUAL's delay, each fading in over a block; both pitches
 * linear in Hz in their knob (943 .. 3130 Hz, 2045 Hz at 64) and TUNE / the step's note moving both by semitones.
 * Their levels as measured: X's slightly pitch-dependent, Y's re X's from a 5 x 5 table of both pitches (Y far under
 * X when it is well below it); ENH a gain on X (+3.5 .. +8.2 dB by pitch); CLICK a short ~3.4 kHz click at the hit.
 * 1 voice. */
static const int16_t CL_G[5] = {0, 32, 64, 96, 127};
/* X's level (0.1 dB) at PTCH 0, 32 .. 127, ENH 0 and 127; Y's re X's [PTCH][TUNE2] at ENH 0 and 127 */
static const int16_t CL_LX[2][5] = {{-231, -249, -241, -223, -228}, {-159, -214, -202, -156, -147}};
static const int16_t CL_LY[2][5][5] = {{{-204, -204, -197, -183, -169},
                                        {-269, -208, -147, -145, -137},
                                        {-443, -154, -89, -112, -116},
                                        {-601, -341, -143, -112, -80},
                                        {-716, -476, -300, -151, -151}},
                                       {{-270, -270, -268, -255, -242},
                                        {-269, -223, -176, -183, -172},
                                        {-477, -125, -89, -145, -160},
                                        {-670, -398, -151, -139, -126},
                                        {-807, -566, -380, -209, -209}}};
#define CL_OFF 40                                       /* the table's window (5-45 ms) to the onset: +4 dB */
#define CL_HZ0 HZ(942.7)                                /* the pitch law: 942.7 Hz + 17.223 Hz x the knob */
#define CL_HZK HZ(17.223)
#define CL_K_CLIC 59853u                                /* CLICK: tau 0.25 ms */
#define CL_CLIC_HZ 3400.0
#define CL_CLIC_END 128u
#define CL_GAIN 365                                     /* the output gain, Q8: the defaults level with CLAVE's */

static int32_t cl_t1(const int16_t *t, int32_t p8)     /* t[] at CL_G, p Q8 -> 0.1 dB, Q8 */
{
    int32_t k = p8 >= 96 * 256 ? 3 : p8 / (32 * 256), f = (p8 - CL_G[k] * 256) / (CL_G[k + 1] - CL_G[k]);
    return t[k] * 256 + (t[k + 1] - t[k]) * f;
}
static int32_t cl_t2(const int16_t t[5][5], int32_t p8, int32_t q8)   /* bilinear */
{
    int32_t i = p8 >= 96 * 256 ? 3 : p8 / (32 * 256), fp = (p8 - CL_G[i] * 256) / (CL_G[i + 1] - CL_G[i]);
    int32_t a = cl_t1(t[i], q8), b = cl_t1(t[i + 1], q8);
    return a + (int32_t)(((int64_t)(b - a) * fp) >> 8);
}
static int32_t cl_knob(uint32_t inc)                   /* the knob value of a pitch, Q8, 0..127 */
{
    if (inc > 0x7FFFFFFFu)                             /* (a pitch past Nyquist: TUNE + a note up to +48) */
        inc = 0x7FFFFFFFu;
    return clamp(((int32_t)inc - (int32_t)CL_HZ0) / (int32_t)(CL_HZK >> 8), 0, 127 * 256);
}
static uint32_t cl_amp(int32_t db8)                    /* 0.1 dB Q8 -> a level, Q16 (1.0 = the measured unit) */
{
    int32_t d16 = (int32_t)(((int64_t)db8 * 13062) >> 20);   /* x 192 / 60.206 / 256: 1/192 octave */
    return pow2_q16(d16 < -3072 ? -3072 : d16);
}

/* CLTRX: TUNE DECAY DUAL ENH, TUNE2 CLICK */
static void cltrx_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    cltrx_t *c = &v->ms.cl;
    int32_t s = dm_tune(t), u = clamp(p[2], 0, 127), px, py, w, lx, ly;
    v->inc[1] = inc_tune(CL_HZ0 + 64u * CL_HZK, s);     /* X: TUNE 0 is the machine's PTCH 64 (2045 Hz) */
    v->inc[0] = inc_tune(CL_HZ0 + (uint32_t)clamp(p[4], 0, 127) * CL_HZK, s);   /* Y: TUNE2 (the machine's TUNE) */
    px = cl_knob(v->inc[1]);
    py = cl_knob(v->inc[0]);
    w = (int32_t)xt_powk(clamp(p[3], 0, 127), 192);    /* ENH: (v / 127) ^ 0.75, Q16 */
    lx = cl_t1(CL_LX[0], px);
    lx += (int32_t)(((int64_t)(cl_t1(CL_LX[1], px) - lx) * w) >> 16);
    ly = cl_t2(CL_LY[0], px, py);
    ly = lx + ly + (int32_t)(((int64_t)(cl_t2(CL_LY[1], px, py) - ly) * w) >> 16);
    c->ax = (int32_t)cl_amp(lx + CL_OFF * 256) * 2;     /* Q17 */
    c->ay = (int32_t)cl_amp(ly + CL_OFF * 256) * 2;
    c->sx = 32u + (uint32_t)((u * 33959 + 4096) >> 13);   /* DUAL: X 0.094 ms x v later */
    c->e = 0x7FFFFFFFu;
    c->d_e = 0xFFFFFFFFu / ktrx_tau(2417u, (clamp(p[1], 0, 127) - 64) * 8116 / 1000, 1u);   /* DEC: tau 54.8 ms x
                                                         * e ^ (0.0293 (v - 64)) */
    c->ce = clamp(p[5], 0, 127) * 6791;                 /* CLICK: 0.26 x X's level at 64 at 127, Q17 << 8 */
    v->inc[2] = HZ(CL_CLIC_HZ);
    v->ph[2] = 0x40000000u;                             /* a cosine */
}

static void cltrx_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    cltrx_t *c = &v->ms.cl;
    uint32_t i, tt = v->t;
    (void)t;
    for (i = 0; i < n; i++, tt++) {
        int32_t y = mulq15(sine_i(v->ph[0]), c->ay);
        if (tt < 32u)                                   /* Y fades in over a block */
            y = (y * (int32_t)tt) >> 5;
        v->ph[0] += v->inc[0];
        if (tt >= c->sx) {                              /* X joins */
            int32_t xx = mulq15(sine_i(v->ph[1]), c->ax);
            if (tt < c->sx + 32u)
                xx = (xx * (int32_t)(tt - c->sx)) >> 5;
            y += xx;
            v->ph[1] += v->inc[1];
        }
        y = mulq15(y, (int32_t)(c->e >> 16));
        if (c->ce && tt < CL_CLIC_END) {                /* CLICK */
            y += mulq15(sine_i(v->ph[2]), c->ce >> 8);
            v->ph[2] += v->inc[2];
            c->ce = mulq16(c->ce, CL_K_CLIC);
        }
        y = (mulq15(mulq15(y, vel_gain(v)), VOICE_FS) * CL_GAIN) >> 8;
        out[i] += y;
        v->last = y;
        ktrx_fall(&c->e, c->d_e);
    }
    v->t = tt;
    if (c->e < (ENV_END << 7) && tt >= CL_CLIC_END)
        dm_end(v);
}

#define DM_CLTRX_DEF {"CLTRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 64), \
    PD("DUAL", F_INT, 0, 127, 0), PD("ENH", F_INT, 0, 127, 0), PD("TUNE2", F_INT, 0, 127, 64), \
    PD("CLICK", F_INT, 0, 127, 0), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, cltrx_trigger, cltrx_render}
