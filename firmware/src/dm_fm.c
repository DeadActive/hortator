/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* FM (spec docs/superpowers/specs/2026-10-08-fm-design.md): 2-operator FM percussion. A carrier sine phase-modulated
 * by a sine at RATIO x the pitch, the depth INDEX x the FM envelope (MDEC); the same envelope raises the pitch by
 * SWEEP (set once per block); the modulator fed back into itself (FBK: the mean of its last two outputs, as the DX7,
 * so it grits up to noise without a whine; the knob spans 0 .. ~2.4 rad, where the grit grows: deeper only repeats the
 * same noise); the amplitude falls over
 * DECAY; VEL: how much
 * the hit's velocity adds to INDEX. 2 voices (a ringing bell is not cut by the next hit). */
#define FM_PM 1300u                                     /* phase modulation: x Q15 x 0..127 -> up to ~8 radians */
#define FM_GAIN(x) (((x) * 3) >> 1)                     /* x 1.5: the defaults level with TOM's */
static const uint16_t FM_RATIO_Q8[16] = {128, 256, 361, 384, 512, 707, 768, 896, 1024, 1382, 1536, 1792, 2048,
                                         2355, 2816, 3584};
static const char *const N_FMRATIO[16] = {"0.5", "1", "1.41", "1.5", "2", "2.76", "3", "3.5", "4", "5.4", "6", "7",
                                          "8", "9.2", "11", "14"};

/* FM: TUNE DECAY INDEX RATIO, MDEC SWEEP FBK VEL */
static void fm_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    v->inc[0] = inc_tune(HZ(220), p[0]);
    v->inc[1] = (uint32_t)(((uint64_t)v->inc[0] * FM_RATIO_Q8[(uint32_t)clamp(p[3], 0, 15)]) >> 8);
    v->ph[0] = v->ph[1] = 0;
    v->env[0] = v->env[1] = ENV1;
    v->k[0] = dk(p[1]);                                 /* DECAY: 5 ms .. 4 s */
    v->k[1] = dk(clamp(p[4], 0, 127) * 114 / 127);      /* MDEC: 5 ms .. 2 s */
    v->x[0] = clamp(p[2] + p[7] * ((int32_t)v->vel - 96) / 127, 0, 127);   /* INDEX + VEL x the velocity over a plain
                                                         * step (96): a plain step plays INDEX (user) */
    v->x[1] = clamp(p[5], 0, 127) * 768 / 127;          /* SWEEP: up to 48 semitones, in 1/16 semitone */
    v->x[2] = clamp(p[6], 0, 127) * 38 / 127;           /* FBK: a depth up to ~2.4 rad (measured: the grit grows to
                                                         * ~2.2 rad, past it only the same noise) */
    v->x[3] = 0;                                        /* the modulator's last two outputs (int16 each) */
}

static void fm_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i, mult, ic, im;
    int32_t idx = v->x[0], fbk = v->x[2], f1 = (int16_t)(v->x[3] & 0xFFFF), f2 = (int16_t)((uint32_t)v->x[3] >> 16);
    (void)t;
    mult = pow2_q16((v->x[1] * env_q15(v->env[1])) >> 15);   /* SWEEP: the pitch over TUNE this block (Q16) */
    ic = (uint32_t)(((uint64_t)v->inc[0] * mult) >> 16);
    im = (uint32_t)(((uint64_t)v->inc[1] * mult) >> 16);
    for (i = 0; i < n; i++) {
        int32_t fe = env_q15(v->env[1]);
        int32_t m = sine_i(v->ph[1] + (uint32_t)(((f1 + f2) >> 1) * fbk) * FM_PM);
        int32_t c = sine_i(v->ph[0] + (uint32_t)(m * ((idx * fe) >> 15)) * FM_PM);
        f2 = f1;
        f1 = m;
        v->ph[0] += ic;
        v->ph[1] += im;
        dm_put(v, out, i, FM_GAIN(mulq15(c, env_q15(v->env[0]))));
        env_step(v, 0);
        env_step(v, 1);
    }
    v->x[3] = (int32_t)(((uint32_t)(uint16_t)f2 << 16) | (uint16_t)f1);
    if (v->env[0] < ENV_END)
        dm_end(v);
}

#define DM_FM_DEF {"FM", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 50), \
    PD("INDEX", F_INT, 0, 127, 60), PE("RATIO", N_FMRATIO, 2), PD("MDEC", F_INT, 0, 127, 40), \
    PD("SWEEP", F_INT, 0, 127, 0), PD("FBK", F_INT, 0, 127, 0), PD("VEL", F_INT, 0, 127, 64)}, fm_trigger, fm_render}
