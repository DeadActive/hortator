/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* ACID (spec docs/superpowers/specs/2026-10-09-acid-design.md §4): a 303-style mono bass voice. A band-limited saw or
 * square (polyBLEP) into a 24 dB 4-pole ladder (zero-delay feedback, Q24, a cubic soft clip on its input x - k y)
 * whose cutoff follows CUTOF + ENVMD x the filter envelope + the accent; the cutoff is computed per block at the
 * block's end and the ladder's coefficients ramp to it sample by sample (no zipper). The amp opens fast, holds while
 * the gate is open, releases in ~8 ms. ACC: more level and filter envelope (ACCNT), a shorter filter decay, and a
 * slow smoother per track (track_t.wow) that consecutive accents charge (the 303's "wow"). Slide (glide hook): the
 * pitch moves in the log domain over ~60 ms, nothing restarts. The sequencer drives the gate (seq.c acid_step, the
 * countdown in seq_tick); a hit no step drives (MIDI, the keys, Grids: drum_core.c drum_hit) closes after GATE of the
 * track's step; ACID_LIFE closes any gate still open, a backstop as the M1-C voices' lifetime: no note drones on.
 * One voice per track. Voice fields: ph[0] phase; x[0] pitch now, x[1] its target (1/16 semitone); x[2] flags
 * (bit 0 gate open, 1 attack done, 2 SQR, 3 the ramps below started); x[3] accent (Q15); env[0] amp, env[1] filter
 * envelope (Q24), k[1] its DECAY; f[0..3] the ladder; c[0].a1 / a2 / a3: G, 1 / (1 + k G^4) and the output gain the
 * last block ended on. */
#define ACID_ATT (QONE / 132)                           /* the amp's attack: ~3 ms linear to full */
#define ACID_REL 64268u                                 /* the amp's release per sample, Q16: ~8 ms to -60 dB */
#define ACID_GLIDE Q24(0.03)                            /* per block: a 12-semitone slide within 1/16 in ~60 ms */
#define ACID_WOW 65300u                                 /* track_t.wow's decay per block, Q16: ~200 ms */
#define ACID_LIFE LIFE_A                                /* the gate closes 5.5 s after the note (or its slide) */
#define ACID_KMAX Q24(4.4)                              /* RESO 127: the ladder's feedback past 4: it oscillates at
                                                         * every CUTOF (but the lowest, under a far higher note), the
                                                         * soft clip holds the amplitude */
#define ACID_GAIN 870                                   /* the output gain, Q8 (x3.4): the defaults level with TOM's */

static inline int32_t acid_clip(int32_t x)              /* x - 4/27 x^3, x held to +-1.5 (Q24): out +-1, smooth */
{
    x = qlim(x, -Q24(1.5), Q24(1.5));
    return x - qm(qm(qm(x, x), x), Q24(4.0 / 27.0));
}
static inline int32_t acid_blep(uint32_t ph, uint32_t inc)   /* polyBLEP residual of a -2 step at the wrap (Q24),
                                                              * ph / inc Q32 */
{
    int32_t t;
    if (ph < inc) {                                     /* just after the wrap: t = ph / inc in 0..1 */
        t = (int32_t)(((uint64_t)ph << 24) / inc);
        return t + t - qm(t, t) - QONE;
    }
    if (ph > 0xFFFFFFFFu - inc) {                       /* just before it: t in -1..0 */
        t = (int32_t)(((uint64_t)(ph - (0xFFFFFFFFu - inc)) << 24) / inc) - QONE;
        return qm(t, t) + t + t + QONE;
    }
    return 0;
}
static uint32_t acid_inc(int32_t p16)                   /* pitch in 1/16 semitone (0 = MIDI 0) -> increment */
{
    static const uint32_t FINE[16] = {65536, 65773, 66011, 66250, 66489, 66730, 66971, 67213,   /* 2^(i / 192), Q16 */
                                      67456, 67700, 67945, 68191, 68438, 68685, 68933, 69183};
    p16 = qlim(p16, 0, 127 * 16);
    return (uint32_t)(((uint64_t)qnote_inc(p16 >> 4) * FINE[p16 & 15]) >> 16);
}
static int32_t acid_pitch(const track_t *t)             /* the target: C4 + the note + TUNE, 1/16 semitone */
{
    return (60 + t->nt + t->p[P_E0]) * 16;
}
static uint32_t acid_decay(int32_t p)                   /* DECAY 0..127 -> DECAY_K 34..121: 30 ms .. 3 s to -60 dB */
{
    return dk(34 + clamp(p, 0, 127) * 87 / 127);
}

/* ACID: TUNE DECAY CUTOF RESO, ENVMD ACCNT WAVE GATE */
static void acid_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    int32_t acc = v->vel > 110u ? clamp(p[5], 0, 127) * 258 : 0;   /* ACCNT (Q15) on an accented note */
    v->x[0] = v->x[1] = acid_pitch(t);
    v->x[2] = 1 | (p[6] ? 4 : 0);                         /* the gate open, the attack running; WAVE */
    v->x[3] = acc;
    v->ph[0] = 0;
    v->env[0] = 0;
    v->env[1] = ENV1;
    if (acc)
        t->wow = qmin(t->wow + (acc << 9) / 2, ENV1);      /* the wow charges on accents, across notes */
    v->k[1] = acid_decay(acc ? clamp(p[1], 0, 127) / 3 : p[1]);   /* DECAY; an accent: shorter */
    v->f[0] = v->f[1] = v->f[2] = v->f[3] = 0;
}

static void acid_glide(track_t *t, dvoice_t *v)          /* slide: a new target, nothing restarts */
{
    const int16_t *p = &t->p[P_E0];
    v->x[1] = acid_pitch(t);
    v->t = 0;                                            /* a new note: its lifetime starts */
    v->x[3] = v->vel > 110u ? clamp(p[5], 0, 127) * 258 : 0;   /* the slid-to step's accent, or none */
    if (v->x[3])
        t->wow = qmin(t->wow + (v->x[3] << 9) / 2, ENV1);
}

static void acid_gate(track_t *t, dvoice_t *v, int open)  /* open: the attack resumes from where the amp is */
{
    (void)t;
    v->x[2] = open ? ((v->x[2] | 1) & ~2) : (v->x[2] & ~1);
}

/* the ladder's coefficients at filter envelope e: G = g / (1 + g), g = tan(pi fc / fs); *inv = 1 / (1 + k G^4) */
static int32_t acid_coef(const track_t *t, const dvoice_t *v, int32_t e, int32_t k, int32_t *inv)
{
    const int16_t *p = &t->p[P_E0];
    int32_t cut, g, G, G2;
    /* cutoff in 1/16 semitones over MIDI 16 (~20 Hz): CUTOF (0..103 semitones: ~20 Hz .. ~8 kHz) + ENVMD x env (up
     * to 48) + the accent (env x ACCNT, up to 36; the track's wow, up to 12) */
    cut = clamp(p[2], 0, 127) * 103 * 16 / 127 +
          (int32_t)(((int64_t)clamp(p[4], 0, 127) * 16 * 48 / 127 * env_q15(e)) >> 15) +
          (int32_t)(((int64_t)v->x[3] * 16 * 36 / 32767 * env_q15(e)) >> 15) +
          (env_q15(t->wow) * 16 * 12 >> 15);
    cut = qlim(cut, 0, 111 * 16);                          /* ~20 Hz .. ~12.5 kHz (MIDI 127) */
    g = qtan_acc((int32_t)(acid_inc(cut + 16 * 16) >> 8)); /* tan(pi fc / fs): fc / fs is inc / 2^32, Q24 */
    G = qdiv(g, QONE + g);
    G2 = qm(G, G);
    *inv = qdiv(QONE, QONE + qm(k, qm(G2, G2)));
    return G;
}

static void acid_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    uint32_t i, j, inc, kn = v->k[1];
    int32_t d, G, inv, G1, inv1, dG, dinv, k, gain, gain1, dgain, s0 = v->f[0], s1 = v->f[1], s2 = v->f[2],
                                                                        s3 = v->f[3];
    int32_t sq = v->x[2] & 4, amp = v->env[0];
    /* pitch: toward the target in the log domain, at least 1/16 semitone a block (no stall short of it) */
    d = v->x[1] - v->x[0];
    if (d) {
        int32_t s = (int32_t)(((int64_t)d * ACID_GLIDE + (1 << 23)) >> 24);
        v->x[0] += s ? s : d > 0 ? 1 : -1;
    }
    inc = acid_inc(v->x[0]);
    /* the ladder's coefficients at the block's end (the filter envelope n samples on: DECAY^n, n a power of two as
     * CTL) and the output gain (velocity x voice level x gain x the accent, <= 122332); each ramps from where the
     * last block ended (a new voice: from its value now) */
    for (j = n; j > 1u; j >>= 1)
        kn = (uint32_t)(((uint64_t)kn * kn) >> 16);
    k = clamp(t->p[P_E0 + 3], 0, 127) * (ACID_KMAX / 127);  /* RESO: 0 .. 4.4 (self-oscillation from ~4, 116) */
    G1 = acid_coef(t, v, mulq16(v->env[1], kn), k, &inv1);
    gain1 = (int32_t)(((int64_t)mulq15(vel_gain(v), VOICE_FS) * ACID_GAIN * (ENV1 + (v->x[3] << 9) / 2)) >> 32);
    if (!(v->x[2] & 8)) {
        v->x[2] |= 8;
        v->c[0].a1 = acid_coef(t, v, v->env[1], k, &v->c[0].a2);
        v->c[0].a3 = gain1;
    }
    G = v->c[0].a1;
    inv = v->c[0].a2;
    gain = v->c[0].a3;
    dG = (G1 - G) / (int32_t)n;                            /* per block: three 32-bit divides */
    dinv = (inv1 - inv) / (int32_t)n;
    dgain = (gain1 - gain) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t x, S, y, u, a, G2, G3;
        uint32_t ph = v->ph[0];
        if (sq) {                                        /* square +-1: two saws half a cycle apart */
            x = (int32_t)(ph >> 7) - (int32_t)((ph + 0x80000000u) >> 7);
            x -= acid_blep(ph, inc) - acid_blep(ph + 0x80000000u, inc);
        } else {
            x = (int32_t)(ph >> 7) - QONE;                /* saw -1 .. +1 */
            x -= acid_blep(ph, inc);
        }
        v->ph[0] = ph + inc;
        x >>= 1;                                         /* into the ladder at half: the soft clip stays near
                                                          * linear for the oscillator, the feedback can saturate */
        G += dG;
        inv += dinv;
        gain += dgain;
        G2 = qm(G, G);
        G3 = qm(G2, G);
        /* the ladder (TPT): y4 = (G^4 x + S) / (1 + k G^4), S = (1 - G)(G^3 s0 + G^2 s1 + G s2 + s3); the soft
         * clip on the ladder's input (x - k y, the feedback subtracted); each one-pole: v = (u - s) G, y = v + s,
         * s = y + v */
        S = qm(QONE - G, qm(G3, s0) + qm(G2, s1) + qm(G, s2) + s3);
        y = qm(qm(qm(G3, G), x) + S, inv);
        u = acid_clip(x - qm(k, y));
        a = qm(u - s0, G); u = a + s0; s0 = u + a;
        a = qm(u - s1, G); u = a + s1; s1 = u + a;
        a = qm(u - s2, G); u = a + s2; s2 = u + a;
        a = qm(u - s3, G); u = a + s3; s3 = u + a;
        /* the amp: attack, hold while the gate is open, release */
        if (v->x[2] & 1) {
            if (!(v->x[2] & 2)) {
                amp += ACID_ATT;
                if (amp >= ENV1) {
                    amp = ENV1;
                    v->x[2] |= 2;
                }
            }
        } else {
            amp = mulq16(amp, ACID_REL);
        }
        a = (int32_t)(((int64_t)qm(u, amp) * gain) >> 24);   /* as dm_put, the gain folded in */
        out[i] += a;
        v->last = a;
        env_step(v, 1);                                  /* the filter envelope: DECAY */
    }
    v->f[0] = s0; v->f[1] = s1; v->f[2] = s2; v->f[3] = s3;
    v->c[0].a1 = G1;                                     /* the next block ramps from these */
    v->c[0].a2 = inv1;
    v->c[0].a3 = gain1;
    v->env[0] = amp;
    v->t += n;
    if (v->t >= ACID_LIFE)
        v->x[2] &= ~1;
    if (!(v->x[2] & 1) && amp < ENV_END)
        dm_end(v);
}

static const char *const N_ACIDWAVE[2] = {"SAW", "SQR"};
#define DM_ACID_DEF {"ACID", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 50), \
    PD("CUTOF", F_INT, 0, 127, 40), PD("RESO", F_INT, 0, 127, 64), PD("ENVMD", F_INT, 0, 127, 64), \
    PD("ACCNT", F_INT, 0, 127, 64), PE("WAVE", N_ACIDWAVE, 0), PD("GATE", F_INT, 0, 127, 56)}, acid_trigger, \
    acid_render, 1, acid_glide, acid_gate}
