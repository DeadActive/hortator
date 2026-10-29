/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Per-track SRR and INSERT (fx.c mix_part; included by fx.c after perform.c). The chain of a track:
 *   RESON -> FILTER -> SRR -> DIST -> INSERT -> SLICER -> the mutes, the COMP duck, LEVEL, PAN, the sends.
 *
 * SRR (P_SRR, INSERT 2/2 KNOB 2): sample rate reduction, as the classic drum machines' (after the filter, before the
 * distortion): the track's signal sampled and held, no smoothing, aliasing on purpose. Each value is held
 * P = 256 + 2 SRR^2 (Q8 samples: SRR 1 1.008, 16 3 (14.7 kHz), 64 33 (1.3 kHz), 127 127 (347 Hz); gentle at first,
 * as DIST's drive); a fractional P (a remainder carried, so a sweep moves smoothly). SRR 0: nothing runs (bit for bit
 * as before). It runs on a block that sounds (trk_inserts: after the FILTER); a silent block sets it at rest, so a hit
 * starts on a fresh sample. Its state: track_t srr_h / srr_a.
 *
 * INSERT (P_ITYPE .. P_IMIX, FX > INSERT): Felucca 1.5's (its Discussions #78 and #177) as Skiff 1.3 ported it (Skiff
 * src/fx.c track_insert), on a drum track. One effect per track, after DIST and before the SLICER, with a dry / wet
 * MIX. TYPE and its three values A B C (what they mean: params.c ins_desc):
 *   SOFT HARD FOLD FUZZ  DRIVE (1x .. ~13x, squared), TONE (two one-pole low-passes at F_CUTOFF), LEVEL (F_DB, as the
 *                        track's LEVEL: 112 is 0 dB). A slow low cut in front (~27 Hz), a DC blocker after (FUZZ is
 *                        lopsided). SOFT tanh, HARD a flat clip at a voice's level, FOLD a triangle folder (the wave
 *                        reflected at +-16384: every fold more harmonics), FUZZ a hard top and a soft, halved bottom;
 *   CRUSH                BITS (1 .. 16, of a voice's full scale), RATE (a held sample, 689 Hz .. 44.1 kHz, INS_HOLD),
 *                        LPF (two one-pole low-passes at F_CUTOFF after them);
 *   PHASR                RATE (F_LFOHZ), DEPTH (the sweep around ~850 Hz: 0 still .. the whole 150 Hz .. 4.8 kHz),
 *                        FDBK (0 .. 0.7): four all-pass stages, per track;
 *   FLANG CHOR           RATE, DEPTH, FDBK: a 256-sample line per track (5.8 ms); FLANG swept 0.25 .. 5 ms (a squared
 *                        triangle), feedback 0 .. 0.85; CHOR a sine around 3.2 ms (+-2.3 ms at DEPTH 127), feedback
 *                        0 .. 0.4. A DC blocker on the swept copy (a clipped loud input is lopsided).
 * The swept ones are the pedals': their wet is (dry + swept) / 2, the notches need both, so MIX 100 % is the whole
 * effect and less a lighter one. MIX blends dry and wet linearly; MIX 0 or TYPE OFF: the track bit for bit as before
 * (nothing runs). MIX glides (2.9 ms, SL_SLOPE); a TYPE change fades the old one out, clears the state, fades the new
 * one in; so does a start from MIX 0 (no stale line or filter). While it runs (TYPE on, MIX above 0) the track's tail
 * after its last voice is 64 blocks (46 ms: a flanger's feedback rings), DIST's 16 otherwise; TYPE OFF or MIX 0 while
 * the track is silent: the INSERT at rest at once (ins_idle).
 * Order: DIST -> INSERT -> SLICER, as Skiff and Felucca 1.5: the insert colours the sound as DIST does; the SLICER's
 * gate (and its STUT recordings) comes after, so its edges stay sharp and a STUT repeats the processed sound.
 * State per track (ins[], 580 B each, 4640 B): the standard build's pool; the Bluetooth build has no pool or .bss room
 * for it: the RAM-code region's spare (app_ble.ld .ram_bss, after .ram_text's PC-guard window, below 0x01C06000, as the
 * variations' banks), which crt0 does not zero: project.c persist_boot does. Loops one per TYPE (no switch a sample);
 * each noinline, out of mix_part (tests/target_budget.py). */
#define IL_LEN 256u                     /* FLANG / CHOR: the line, samples (a power of 2: masked) */
#define IN_ST 4u                        /* PHASR: all-pass stages */
static const uint8_t INS_HOLD[16] = {64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};   /* CRUSH RATE */
/* PHASR: the stages' coefficient a (Q14) for a break of 150 Hz * 2^(5 i / 32): a = (g - 1) / (g + 1), g = tan(pi f / fs) */
static const int16_t IN_PH_A[33] = {
    -16038, -15998, -15955, -15906, -15853, -15793, -15727, -15653, -15572, -15481, -15381, -15270, -15147, -15012,
    -14862, -14696, -14514, -14313, -14092, -13849, -13582, -13290, -12970, -12621, -12240, -11825, -11375, -10886,
    -10356, -9784, -9166, -8500, -7783,
};
typedef struct {
    int32_t w;                          /* the wet share now, Q15 */
    uint8_t type;                       /* the type running */
    uint8_t cnt;                        /* CRUSH: samples left of the held one */
    uint16_t wp;                        /* FLANG / CHOR: the line's next write */
    uint32_t ph;                        /* the sweep's phase */
    int32_t hp, lp1, lp2, dc, hold;     /* low cut (Q8), the two low-passes, DC blocker (Q8), CRUSH's held sample */
    int32_t px[IN_ST], py[IN_ST], fb;   /* PHASR: each stage's last input and output, the fed back */
    int16_t line[IL_LEN];               /* FLANG / CHOR, at a quarter (headroom) */
} ins_t;
#if defined(FELUCCA_BLE) && FELUCCA_BLE
static ins_t ins[NTRK] __attribute__((section(".ram_bss"), aligned(4)));   /* (zeroed by persist_boot) */
#else
static ins_t ins[NTRK] __attribute__((section(".pool")));
#endif

/* ------------------------------------------------------------------------------------------------------- SRR --- */
static int32_t srr_period(int32_t v)                    /* SRR v: each value held this long, Q8 samples */
{
    v = clamp(v, 0, 127);
    return 256 + 2 * v * v;
}
/* track t's block b sampled and held (trk_inserts, after the FILTER); snd 0 (a silent block) or SRR 0: at rest */
static __attribute__((noinline)) void track_srr(track_t *t, int32_t *b, uint32_t n, uint32_t snd)
{
    int32_t p = srr_period(t->p[P_SRR]), h = t->srr_h, a = t->srr_a;
    uint32_t i;
    if (p == 256 || !snd) {
        t->srr_h = t->srr_a = 0;
        return;
    }
    for (i = 0; i < n; i++) {
        if (a < 256) {                                  /* less than a sample left of the held one: a new one */
            h = b[i];
            a += p;
        }
        a -= 256;
        b[i] = h;
    }
    t->srr_h = h;
    t->srr_a = a;
}

/* ---------------------------------------------------------------------------------------------------- INSERT --- */
static inline int32_t ins_mixq(int32_t m) { return m >= 127 ? 32768 : m <= 0 ? 0 : m * 258; }   /* MIX -> Q15 */
static inline int ins_on(const track_t *t) { return (t->p[P_ITYPE] != IT_OFF) & (t->p[P_IMIX] > 0); }
/* a triangle of the sweep's place q, 0 .. 65534 (Q16) */
static inline uint32_t ins_tri(uint32_t q)
{
    uint32_t t = q >> 16;
    return t < 32768u ? t << 1 : (65535u - t) << 1;
}
/* the one-pole low-pass (topology-preserving) coefficient of cutoff index c, Q16: G = g / (1 + g), g = tan(pi fc / FS) */
static int32_t ins_lpk(int32_t c)
{
    uint32_t g = SVF_G[clamp(c, 0, 127)];
    return (int32_t)((g << 16) / (4096u + g));
}
/* the drives' and CRUSH's shape of v (a voice's full scale 32768; v already through DRIVE): the curve the INSERT
 * graph draws too (ui_draw.c graph_insert) */
static inline int32_t ins_shape(uint32_t ty, int32_t v, uint32_t sh)
{
    switch (ty) {
    case IT_SOFT:
        return softclip(v);
    case IT_HARD:
        return clamp(v, -24000, 24000);
    case IT_FOLD: {
        int32_t u = (int32_t)((uint32_t)(v + 16384) & 0xFFFFu) - 32768;   /* a triangle of period 65536: +-16384 */
        u = 16384 - (u < 0 ? -u : u);
        return u + (u >> 1);                            /* x 1.5 (no divide) */
    }
    case IT_FUZZ:
        return v > 0 ? (v > 20000 ? 20000 : v) : softclip(v) >> 1;
    case IT_CRUSH:                                      /* sh: 16 - BITS; to the nearest step */
        return sh ? ((v + (1 << (sh - 1u))) >> sh) << sh : v;
    default:
        return v;
    }
}
static inline uint32_t ins_bits_sh(int32_t a) { return 15u - (uint32_t)(clamp(a, 0, 127) >> 3); }   /* BITS 1..16 */

/* TONE / LPF: two one-pole low-passes on y (x16 inside: the low cutoffs' rounding leaves no offset) */
#define INS_TONE(y)                                                                                                  \
    do {                                                                                                             \
        int32_t v_ = mulq16(((y) << 4) - s->lp1, (uint32_t)k);                                                      \
        (y) = v_ + s->lp1;                                                                                           \
        s->lp1 = (y) + v_;                                                                                           \
        v_ = mulq16((y) - s->lp2, (uint32_t)k);                                                                     \
        (y) = v_ + s->lp2;                                                                                           \
        s->lp2 = (y) + v_;                                                                                           \
        (y) = ((y) + 8) >> 4;                                                                                        \
    } while (0)

/* SOFT .. FUZZ: low cut -> DRIVE -> shape -> TONE -> LEVEL -> DC blocker */
static __attribute__((noinline)) void ins_drive(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, ty = s->type;
    int32_t a = t->p[P_IA], g = 4096 + a * a * 3;       /* DRIVE, Q12: 1x .. 12.8x, gentle at first */
    int32_t k = ins_lpk(t->p[P_IB]), lvl = LEVEL_Q12[t->p[P_IC] & 127];
#define INS_DRIVE_LOOP(TY)                                                                                           \
    for (i = 0; i < n; i++) {                                                                                        \
        int32_t x = b[i], y, v;                                                                                      \
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);                                                              \
        s->hp += ((x << 8) - s->hp) >> 8;               /* ~27 Hz low cut (Q8: no offset left by the rounding) */  \
        v = clamp(x - (s->hp >> 8), -230000, 230000);                                                                \
        y = ins_shape(TY, ((v >> 3) * g) >> 9, 0);      /* (>> 3 first: 32 bits) */                                 \
        INS_TONE(y);                                                                                                 \
        y = (y * lvl) >> 12;                            /* LEVEL */                                                  \
        s->dc += ((y << 8) - s->dc) >> 9;               /* DC blocker, ~14 Hz (FUZZ's lopsided halves) */           \
        y -= s->dc >> 8;                                                                                             \
        b[i] = pf_mix(x, y, s->w);                                                                                   \
    }
    if (ty == IT_SOFT)                                  /* one loop per TYPE: no switch a sample */
        INS_DRIVE_LOOP(IT_SOFT)
    else if (ty == IT_HARD)
        INS_DRIVE_LOOP(IT_HARD)
    else if (ty == IT_FOLD)
        INS_DRIVE_LOOP(IT_FOLD)
    else
        INS_DRIVE_LOOP(IT_FUZZ)
#undef INS_DRIVE_LOOP
}

/* CRUSH: a held sample (RATE) -> BITS -> LPF */
static __attribute__((noinline)) void ins_crush(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, sh = ins_bits_sh(t->p[P_IA]), hold = INS_HOLD[clamp(t->p[P_IB], 0, 127) >> 3];
    int32_t k = ins_lpk(t->p[P_IC]);
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (!s->cnt) {
            s->hold = ins_shape(IT_CRUSH, clamp(x, -262144, 262143), sh);
            s->cnt = (uint8_t)hold;
        }
        s->cnt--;
        y = s->hold;
        INS_TONE(y);
        b[i] = pf_mix(x, y, s->w);
    }
}
#undef INS_TONE

/* PHASR, FLANG, CHOR: the sweep at RATE (one LFO per track), DEPTH, FDBK */
static __attribute__((noinline)) void ins_swept(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, inc = LFO_INC[clamp(t->p[P_IA], 0, 127)] / CTL, dep = (uint32_t)clamp(t->p[P_IB], 0, 127);
    int32_t c = clamp(t->p[P_IC], 0, 127);
    if (s->type == IT_PHASER) {                         /* the coefficients a block at a time (at its middle) */
        uint32_t tr = ins_tri(s->ph + (n >> 1) * inc), q = 32768u + (uint32_t)(((int32_t)tr - 32768) * (int32_t)dep / 127);
        int32_t a = IN_PH_A[q >> 11] + (((IN_PH_A[(q >> 11) + 1u] - IN_PH_A[q >> 11]) * (int32_t)(q & 2047u)) >> 11);
        int32_t fbk = c * 180;                          /* 0 .. 0.7, Q15 */
        for (i = 0; i < n; i++) {
            int32_t x = b[i], xs = clamp((x + 4) >> 3, -16383, 16383), u = xs + ((s->fb * fbk + 16384) >> 15), y = u;
            uint32_t st;
            s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
            for (st = 0; st < IN_ST; st++) {            /* y = a (u - y1) + u1 */
                y = clamp(s->px[st] + ((a * (u - s->py[st]) + 8192) >> 14), -65535, 65535);
                s->px[st] = u;
                s->py[st] = y;
                u = y;
            }
            s->fb = clamp(y, -32767, 32767);
            y <<= 2;                                    /* (y: at an eighth) */
            s->dc += ((y << 8) - s->dc) >> 9;           /* DC blocker, ~14 Hz (a clipped loud input is lopsided) */
            b[i] = pf_mix(x, (x >> 1) + y - (s->dc >> 8), s->w);   /* (dry + all-passed) / 2 */
        }
        s->ph += n * inc;
        return;
    }
    {
        int32_t chorus = s->type == IT_CHORUS, fbk = c * (chorus ? 103 : 219);   /* 0 .. 0.4 / 0 .. 0.85, Q15 */
        uint32_t dq = dep * 516u;                       /* DEPTH, Q16 */
        for (i = 0; i < n; i++, s->ph += inc) {
            int32_t x = b[i], r0, r1, v, fr;
            uint32_t d;                                 /* the delay, Q16 samples */
            s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
            if (chorus) {
                d = (uint32_t)((140 << 16) + ((osc_sine(s->ph) * (int32_t)dep * 50) >> 5));   /* 3.2 +- 2.3 ms */
            } else {
                uint32_t tr = ins_tri(s->ph), sw = (tr * tr) >> 16;   /* longer near the short end */
                d = (11u << 16) + 209u * (((sw >> 1) * dq) >> 15);  /* 0.25 .. 5 ms */
            }
            r0 = (int32_t)(s->wp - (d >> 16)) & (int32_t)(IL_LEN - 1u);
            r1 = (r0 - 1) & (int32_t)(IL_LEN - 1u);
            fr = (int32_t)((d >> 1) & 0x7FFFu);
            v = s->line[r0] + (((s->line[r1] - s->line[r0]) * fr) >> 15);
            s->line[s->wp] = (int16_t)clamp((x >> 2) + softclip(mulq15(v, fbk)), -32767, 32767);
            s->wp = (uint16_t)((s->wp + 1u) & (IL_LEN - 1u));
            v <<= 1;                                    /* (v: at a quarter) */
            s->dc += ((v << 8) - s->dc) >> 9;           /* DC blocker (the line's clip on a loud lopsided wave) */
            b[i] = pf_mix(x, (x >> 1) + v - (s->dc >> 8), s->w);   /* (dry + delayed) / 2 */
        }
    }
}

/* the track's INSERT on its block b (mix_part, after DIST): nothing while TYPE is OFF or MIX is 0 and faded */
static __attribute__((noinline)) void track_insert(track_t *t, int32_t *b, uint32_t n)
{
    ins_t *s = &ins[(uint32_t)(t - trk) % NTRK];
    int32_t ty = clamp(t->p[P_ITYPE], 0, IT_N - 1), tw;
    if (!ty && !s->w)
        return;                                         /* OFF: bit for bit */
    if (s->type != ty && !s->w)                         /* faded out: the new type from rest */
        s->type = (uint8_t)ty;
    tw = s->type == ty && ty ? ins_mixq(t->p[P_IMIX]) : 0;
    if (!s->w) {
        if (!tw)
            return;                                     /* dry: bit for bit */
        memset(s, 0, sizeof *s);                        /* starting: no stale line or filter */
        s->type = (uint8_t)ty;
    }
    if (s->type >= IT_PHASER)
        ins_swept(s, t, b, n, tw);
    else if (s->type == IT_CRUSH)
        ins_crush(s, t, b, n, tw);
    else
        ins_drive(s, t, b, n, tw);
}

/* the track silent (mix_part, its tail run out): TYPE OFF or MIX 0 meanwhile, the INSERT at rest at once (its fade
 * never ran: no stale wet on the next hit) */
static __attribute__((noinline)) void ins_idle(track_t *t)
{
    ins_t *s = &ins[(uint32_t)(t - trk) % NTRK];
    if (s->w && !ins_on(t))
        memset(s, 0, sizeof *s);
}
