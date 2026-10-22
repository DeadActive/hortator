/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* KTRX: a sine kick built from measurements of a classic digital drum machine's sine kick (md-re: renders of an
 * emulator, knob by knob; only the measured laws are here, no code or data of it). The body is a sine starting
 * downwards, fading in over ~13 ms, whose pitch is TUNE + a built-in drop (two parts, 4.8 ms and 3.6 ms, smaller on
 * higher notes) + SWEEP
 * (cubic in the knob, proportional to the pitch, falling over SDEC); a fixed ~295 Hz transient at the hit (1.35 ms);
 * PUNCH turns the first cycle into a square pulse; NOISE a short low-passed noise burst; SUB odd half-harmonics locked
 * to the body's phase (alternate cycles long and loud / short and quiet), fading on their own (~0.7 s) whatever
 * DECAY is; CLIP above 64 hard-clips the body after its first cycle, before the decay, at a threshold falling from
 * 1 to 0.235 (fitted on the harmonics: each within ~1 dB, the nulls too), with its measured output gain. 1 voice. */
#define KTRX_RAD(r) ((uint32_t)((r) * 683565275.58))   /* radians -> 2^32 phase; constants only */
/* the start, fitted on the spectrogram and the level envelope over six renders (md-re tools/fitproto.py) */
#define KTRX_DROP_HZ 32.2                               /* the drop's slower part at TUNE 0 (61.4 Hz) */
#define KTRX_FAST_HZ 162.1                              /* its fast part */
#define KTRX_K_DROP 65229u                              /* tau 4.83 ms (Q16 per sample) */
#define KTRX_K_FAST 65122u                              /* tau 3.58 ms */
#define KTRX_TR 22135                                   /* the transient's level, Q15 (0.6755) */
#define KTRX_TR_HZ 294.6
#define KTRX_K_TR 64444u                                /* its tau 1.35 ms */
#define KTRX_K_ATK 65424u                               /* the body fades in: tau 13.2 ms */
#define KTRX_AMP 10224                                  /* the body's level, Q15 (0.312) */
#define KTRX_K_PUNCH 65461u                             /* PUNCH's square: tau 20 ms */
#define KTRX_K_SUB 65534u                               /* SUB: tau ~0.7 s (0.74: Q16's step) */
#define KTRX_NCUT 28200                                 /* NOISE: its low-pass, fitted on the emulator's octave bands (1-16 kHz) */
#define KTRX_NRES 25                                    /* (~5.75 kHz Butterworth-like, digital) */
#define KTRX_GAIN(x) (((x) * 13) >> 2)                 /* x 3.25: the defaults level with K909's */
#define KTRX_TAU_MAX 25578u                             /* 0.58 s: 4 s to -60 dB, as DECAY_K's end (the 6 s life) */
/* the measured decay laws as a time constant in samples: tau0 x 2 ^ (d16 / 192) (d16 in 1/16 semitones: the knob x
 * log2 of the law's ratio per 8 steps), floored */
static uint32_t ktrx_tau(uint32_t tau0, int32_t d16, uint32_t floor)
{
    uint64_t ts = ((uint64_t)tau0 * pow2_q16(d16)) >> 16;
    return ts < floor ? floor : ts > KTRX_TAU_MAX ? KTRX_TAU_MAX : (uint32_t)ts;
}
/* a Q31 envelope falling by 1 / tau each sample, the step Q32 (DECAY_K's Q16 is too coarse past ~0.2 s; Q31 keeps
 * the step's truncation below 0.1 % down to -40 dB, and its stall below the voice's end) */
static inline void ktrx_fall(uint32_t *e, uint32_t d32) { *e -= (uint32_t)(((uint64_t)*e * d32) >> 32); }

/* CLIP's output gain, Q12, at 64, 72 .. 120, 127 (measured: the body's level re CLIP 0 over the clip's own) */
static const uint16_t KTRX_CLIPG[9] = {4096, 4710, 4874, 5038, 5202, 5325, 5366, 5120, 3973};

/* KTRX: TUNE DECAY SWEEP SDEC, PUNCH NOISE SUB CLIP */
static void ktrx_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    ktrx_t *k = &v->ms.kt;
    int32_t s = clamp(dm_tune(t), -48, 48), r = clamp(p[2], 0, 127), nz = clamp(p[5], 0, 127);
    uint32_t sc = pow2_q16(-s * 8);                     /* (f0 / 61.4 Hz) ^ -0.5: the drop is smaller up high */
    k->inc0 = inc_tune(HZ(61.4), s);
    k->inc_d = (uint32_t)(((uint64_t)HZ(KTRX_DROP_HZ) * sc) >> 16);
    k->inc_f = (uint32_t)(((uint64_t)HZ(KTRX_FAST_HZ) * sc) >> 16);
    k->inc_r = (uint32_t)(((((uint64_t)k->inc0 * (uint32_t)(r * r * r)) >> 8) * 5935948u) >> 32);   /* 679 / 61.4
                                                         * x f0 x (r / 127)^3 (2^-40: a long SWEEP shows any error) */
    v->env[2] = ENV1;
    k->e_amp = k->e_r = 0x7FFFFFFFu;                    /* Q31 */
    k->d_amp = 0xFFFFFFFFu / ktrx_tau(2293u, (clamp(p[1], 0, 127) - 64) * 1518 / 100, 309u);   /* DECAY: tau 52 ms x
                                                         * 1.55 ^ ((d - 64) / 8), 7 ms floor */
    k->d_ramp = 0xFFFFFFFFu / ktrx_tau(4322u, (clamp(p[3], 0, 127) - 72) * 1427 / 100, 221u);  /* SDEC: tau 98 ms x
                                                         * 1.51 ^ ((d - 72) / 8), 5 ms floor */
    v->k[2] = KTRX_K_FAST;
    k->e_d = k->e_t = k->e_a = k->e_s = k->e_h = ENV1;
    k->ph_h = 0x40000000u;                              /* half the body's phase, + 90 degrees */
    k->first = 1;
    k->punch = clamp(p[4], 0, 127) * 258;               /* Q15 */
    k->sub = clamp(p[6], 0, 127) * 9339 / 127;          /* Q15: 0.285 at 127 (-11 dB re the body) */
    if (p[7] > 64) {                                    /* CLIP: the threshold 1 - 0.01227 (c - 64), Q15 */
        int32_t c = clamp(p[7], 0, 127) - 64, j = c >= 56 ? 7 : c >> 3, f = j == 7 ? (c - 56) * 256 / 7 : (c & 7) << 5;
        k->th = 32768 - 402 * c;
        k->cgain = KTRX_CLIPG[j] + (((KTRX_CLIPG[j + 1] - KTRX_CLIPG[j]) * f) >> 8);
    } else
        k->th = 0;
    dsvf_coef(&v->c[0], KTRX_NCUT, KTRX_NRES);
    k->nlev = nz ? (int32_t)((4538u * pow2_q16((nz - 127) * 3693 / 1000)) >> 16) : 0;   /* 14.6 dB over the knob
                                                         * (its level set on the emulator's octave bands) */
    k->e_n = nz ? ENV1 : 0;
    k->k_n = dk(27 + (nz > 64 ? (nz - 64) * 33 / 80 : 0));   /* tau 3 ms, doubling every 32 above 64 */
}

static void ktrx_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    ktrx_t *k = &v->ms.kt;
    uint32_t i, tt = v->t;
    int32_t hl = 0;
    (void)t;
    for (i = 0; i < n; i++, tt++) {
        uint32_t inc = k->inc0 + (uint32_t)(((uint64_t)k->inc_r * k->e_r) >> 31) +   /* (exact: a long SWEEP drifts
                                                         * on a truncated product) */
                       (k->inc_d >> 12) * (uint32_t)(k->e_d >> 12) + (k->inc_f >> 12) * (uint32_t)(v->env[2] >> 12);
        uint32_t ph = v->ph[0];
        int32_t body = -sine_i(ph), y;
        if (k->sub) {                                   /* SUB: 1/2, 3/2, 5/2 x the pitch, locked to the body */
            hl = mulq15(k->sub, env_q15(k->e_h));
            body += mulq15(sine_i(k->ph_h + KTRX_RAD(1.2)) + (sine_i(3u * k->ph_h + KTRX_RAD(0.75)) >> 2) +
                           mulq15(sine_i(5u * k->ph_h + KTRX_RAD(0.95)), 1638), hl);
            k->ph_h += inc >> 1;
            k->e_h = mulq16(k->e_h, KTRX_K_SUB);
        }
        v->ph[0] = ph + inc;
        if (v->ph[0] < ph)
            k->first = 0;                               /* the first cycle is over */
        y = mulq15(body, KTRX_AMP);
        if (k->th && !k->first)                         /* CLIP: a hard clip, its gain */
            y = (mulq15(clamp(body, -k->th, k->th), KTRX_AMP) * k->cgain) >> 12;
        if (k->punch && tt < 283u) {                    /* PUNCH: the first cycle a square pulse (~156 Hz) */
            int32_t sv = mulq15(tt < 142u ? -9830 : 15401, env_q15(k->e_s));
            y = mulq15(y, 32767 - k->punch) + mulq15(sv, k->punch);
            k->e_s = mulq16(k->e_s, KTRX_K_PUNCH);
        }
        y = mulq15(y, (int32_t)(k->e_amp >> 16));
        if (k->e_a >= ENV_END) {                        /* the start (the fade-in outlasts the rest) */
            y = mulq15(y, 32767 - env_q15(k->e_a));
            if (k->e_t >= ENV_END) {                    /* the transient, starting downwards */
                y -= mulq15(mulq15(sine_i(v->ph[1]), env_q15(k->e_t)), KTRX_TR);
                v->ph[1] += HZ(KTRX_TR_HZ);
                k->e_t = mulq16(k->e_t, KTRX_K_TR);
            }
            k->e_a = mulq16(k->e_a, KTRX_K_ATK);
            k->e_d = mulq16(k->e_d, KTRX_K_DROP);
            env_step(v, 2);
        }
        if (k->e_n >= ENV_END) {                        /* NOISE */
            int32_t bp, hp, nl = dsvf_tick(&v->c[0], dnoise(v), &v->f[0], &v->f[1], &bp, &hp);
            y += mulq15(mulq15(nl, k->nlev), env_q15(k->e_n)) * 2;
            k->e_n = mulq16(k->e_n, k->k_n);
        }
        y = KTRX_GAIN(mulq15(mulq15(y, vel_gain(v)), VOICE_FS));   /* dm_put, the gain after (no clip) */
        out[i] += y;
        v->last = y;
        ktrx_fall(&k->e_amp, k->d_amp);
        ktrx_fall(&k->e_r, k->d_ramp);
    }
    v->t = tt;
    if (k->e_amp < (ENV_END << 7) && k->e_a < ENV_END && k->e_n < ENV_END)
        dm_end(v);
}

#define DM_KTRX_DEF {"BDTRX", 1, 0, {PD("TUNE", F_SEMI, -24, 24, -4), PD("DECAY", F_INT, 0, 127, 68), \
    PD("SWEEP", F_INT, 0, 127, 35), PD("SDEC", F_INT, 0, 127, 35), PD("PUNCH", F_INT, 0, 127, 14), \
    PD("NOISE", F_INT, 0, 127, 14), PD("SUB", F_INT, 0, 127, 35), PD("CLIP", F_INT, 0, 127, 24)}, ktrx_trigger, \
    ktrx_render}
