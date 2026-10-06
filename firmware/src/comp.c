/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * The gain computer and detector below are a C port of Mutable Instruments Streams' compressor
 * (streams/compressor.cc, compressor.h, gain.h), under its MIT licence:
 * Copyright 2014 Emilie Gillet.
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 * to permit persons to whom the Software is furnished to do so, subject to the following conditions: The above
 * copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED. */
/* M3 sidechain compressor: Streams' compressor keyed by one track (the SOURCE), its gain applied to the DUCK
 * tracks (fx.c). Configuration as Streams' "globals" path, from our knobs; Streams drove an analog VCA with g
 * (unity 32767, 256 steps = 1.55 dB): comp_lin is that VCA in software. */
#include "comp_tables.h"
#ifndef COMP_LP_TABLE
#define COMP_LP_TABLE COMP_LP_COEF               /* 44.1 kHz (tests/comp_fidelity.c sets Streams' 31,089 Hz table) */
#endif
#define COMP_UNITY 32767                         /* Streams kUnityGain */
#define COMP_GAIN_K 990                          /* Streams kGainConstant: 1 / (1.55 / 6 * 65536 / 256) * 65536 */
#define COMP_MAX_EXP_GAIN 218453                 /* Streams kMaxExponentialGain */

typedef struct { int32_t atk, thr, rat, rel, knee, mkup; } comp_set_t;   /* ATK THRSH RATIO REL MKUP 0..127, KNEE 0/1 */
typedef struct {
    int64_t atk, dec;            /* attack / decay coefficients (Q31); atk -1 = instant (the limiter) */
    int32_t ratio, thr, makeup;  /* reciprocal ratio (8:8), threshold and makeup gain (log2, 65536 / octave) */
    uint8_t soft;                /* soft knee (Streams' "alternate") */
} comp_cfg_t;

static uint32_t comp_k16(int32_t v) { return (uint32_t)clamp(v, 0, 127) * 65535u / 127u; }   /* knob -> 16 bit */

/* Compressor::Configure, globals path, as Streams: attack, threshold, decay, amount (16 bit each). AMOUNT below
 * 32768 is the ratio (32767 = 1:1 .. 0 = the steepest), above it the adaptive makeup up to the limiter. */
static void comp_configure_streams(uint32_t atk16, uint32_t thr16, uint32_t rel16, uint32_t amount, int knee,
                                   comp_cfg_t *c)
{
    uint32_t atk_t = atk16 * (128u + 128u + 99u) >> 16;              /* 0.1 ms .. 0.6 s */
    uint32_t dec_t = 128u + 99u + (rel16 >> 8);                       /* 59 ms .. 5.8 s */
    c->atk = COMP_LP_TABLE[atk_t];
    c->dec = COMP_LP_TABLE[dec_t];
    c->soft = (uint8_t)(knee != 0);
    c->thr = (-1280 + 5 * (int32_t)(thr16 >> 8)) * 256;
    if (amount < 32768u) {                                           /* compression, no makeup */
        c->ratio = COMP_RATIO[(32767u - amount) >> 7];
        c->makeup = 0;
    } else {                                                         /* adaptive compression with makeup */
        int32_t knee_gain;
        amount -= 32768u;
        c->makeup = (int32_t)amount * (COMP_MAX_EXP_GAIN >> 8) >> 7;
        knee_gain = c->thr + c->makeup;
        if (knee_gain >= 0) {
            c->makeup = -c->thr;
            knee_gain = 0;
        }
        if (knee_gain > -4096) {                                     /* brickwall limiter, instant attack */
            c->ratio = 0;
            c->atk = -1;
        } else {
            c->ratio = knee_gain / (c->thr >> 8);
        }
    }
}

/* RATIO knob -> Streams AMOUNT, lower half: 0 = 1:1 .. 127 = the steepest (continuous) */
static uint32_t comp_ratio16(int32_t v) { return 32767u - (uint32_t)clamp(v, 0, 127) * 32767u / 127u; }

/* our knobs: Streams' configuration with the RATIO knob's ratio, then MKUP's makeup on top (Streams' makeup
 * scale, never lifting the knee above 0 dB); MKUP 127 = Streams' limiter */
static void comp_configure(const comp_set_t *s, comp_cfg_t *c)
{
    int32_t mk = clamp(s->mkup, 0, 127);
    comp_configure_streams(comp_k16(s->atk), comp_k16(s->thr), comp_k16(s->rel), comp_ratio16(s->rat), s->knee, c);
    if (mk >= 127) {
        c->makeup = -c->thr;
        c->ratio = 0;
        c->atk = -1;
    } else if (mk > 0) {
        c->makeup = (mk * 32767 / 126) * (COMP_MAX_EXP_GAIN >> 8) >> 7;
        if (c->thr + c->makeup >= 0)
            c->makeup = -c->thr;
    }
}

/* Compressor::Log2: Streams' shift loops done as one shift (same value) */
static int32_t comp_log2(int32_t v)
{
    uint32_t u = v > 0 ? (uint32_t)v : 1u, w = u;
    int32_t n = 0;                                                   /* bit length of u, minus 1 */
    if (w >> 16) {
        w >>= 16;
        n += 16;
    }
    if (w >> 8) {
        w >>= 8;
        n += 8;
    }
    if (w >> 4) {
        w >>= 4;
        n += 4;
    }
    if (w >> 2) {
        w >>= 2;
        n += 2;
    }
    if (w >> 1)
        n += 1;
    u = n >= 8 ? u >> (n - 8) : u << (8 - n);                        /* into 256 .. 511 */
    return (n - 8) * 65536 + (int32_t)COMP_LOG2[u - 256u];
}

/* Compressor::Exp2: the octave loops done as a shift (same value) */
static int32_t comp_exp2(int32_t v)
{
    int32_t sh = v >> 16, f = v & 0xFFFF, a = (int32_t)COMP_EXP2[f >> 8], b = (int32_t)COMP_EXP2[(f >> 8) + 1];
    int32_t m = a + ((b - a) * (f & 0xFF) >> 8);
    return sh >= 0 ? m << sh : m >> -sh;
}

/* Compressor::Compress on a level (log2 units, 0 = a 15-bit peak at full scale): -attenuation */
static int32_t comp_atten(const comp_cfg_t *c, int32_t level)
{
    int32_t pos = level - c->thr, att;
    if (pos < 0)
        return 0;
    att = pos - (pos * c->ratio >> 8);
    if (att < 65535 && c->soft) {
        int32_t a = COMP_KNEE[att >> 8], b = COMP_KNEE[(att >> 8) + 1];
        int32_t k = a + ((b - a) * (att & 0xFF) >> 8);
        att += (k - att) * ((65535 - att) >> 1) >> 15;
    }
    return -att;
}

/* Compressor::Process for one source sample x (the EXCITE and AUDIO inputs are both the source here, so
 * Streams' "is there a sidechain signal" detector, which only chooses between them, is left out) */
static uint32_t comp_process(const comp_cfg_t *c, int64_t *det, int32_t *gr, int32_t x)
{
    int32_t s = clamp(x, -32768, 32767), g;
    int64_t err = (int64_t)(s * s) - *det;
    if (err > 0)
        *det += c->atk < 0 ? err : err * c->atk >> 31;
    else
        *det += err * c->dec >> 31;
    g = comp_atten(c, (comp_log2((int32_t)*det) >> 1) - 15 * 65536);
    *gr = g >> 3;
    g = COMP_UNITY + ((g + c->makeup) * COMP_GAIN_K >> 16);
    return (uint32_t)(g > 65535 ? 65535 : g);
}

/* Streams' VCA in software: g -> linear gain, Q16 (990 steps of g = one octave = 6.02 dB) */
static int32_t comp_lin(uint32_t g)
{
    int32_t x = ((int32_t)g - COMP_UNITY) * 8465 >> 7;              /* x 65536 / 990.97: log2 units */
    return comp_exp2(clamp(x, -16 * 65536, 6 * 65536));
}

/* --------------------------------------------------- the sidechain --- */
static struct {
    comp_cfg_t cfg;              /* configured for the knobs in set */
    comp_set_t set;
    uint8_t have;                /* 0 = not configured */
    int64_t det;                 /* the detector (Streams detector_) */
    int32_t gr;                  /* gain reduction for the meter (Streams gain_reduction_) */
    int32_t peak;                /* the source's peak this block (meter) */
    int32_t gain[CTL];           /* this block's gain per sample, Q16, for the DUCK tracks */
} comp;

static int comp_on(void) { return song.g[G_CSRC] >= 1 && song.g[G_CSRC] <= NTRK; }
static uint32_t comp_src(void) { return comp_on() ? (uint32_t)song.g[G_CSRC] - 1u : NTRK; }   /* NTRK = off */
/* GHOST: the source when it keys the compressor also while muted (KEEP, HIDE), NTRK when not (MUTE, or off) */
static uint32_t comp_ghost_src(void) { return song.g[G_CGHOST] != CG_MUTE ? comp_src() : NTRK; }
static comp_set_t comp_knobs(void)
{
    comp_set_t s;
    s.atk = clamp(song.g[G_CATK], 0, 127);
    s.thr = clamp(song.g[G_CTHR], 0, 127);
    s.rat = clamp(song.g[G_CRAT], 0, 127);
    s.mkup = clamp(song.g[G_CMKUP], 0, 127);
    s.rel = clamp(song.g[G_CREL], 0, 127);
    s.knee = song.g[G_CKNEE] ? 1 : 0;
    return s;
}

static void comp_reset(void)                     /* a fresh detector (PLAY of the module), unity gain */
{
    uint32_t i;
    comp.det = 0;
    comp.gr = 0;
    comp.peak = 0;
    comp.have = 0;
    for (i = 0; i < CTL; i++)
        comp.gain[i] = 65536;
}

/* the source's block x (0: silence): the gain per sample for the DUCK tracks */
static void comp_block(const int32_t *x, uint32_t n)
{
    comp_set_t s = comp_knobs();
    uint32_t i;
    if (!comp.have || memcmp(&s, &comp.set, sizeof s)) {
        comp_configure(&s, &comp.cfg);
        comp.set = s;
        comp.have = 1;
    }
    comp.peak = 0;
    for (i = 0; i < n && i < CTL; i++) {
        int32_t v = x ? x[i] : 0, a = v < 0 ? -v : v;
        if (a > comp.peak)
            comp.peak = a;
        comp.gain[i] = comp_lin(comp_process(&comp.cfg, &comp.det, &comp.gr, v));
    }
}

/* ------------------------------------------------------- display --- */
static void comp_fmt_db10(char *b, int32_t dbx10)  /* 0.1 dB -> "-10.3", "-0.1", "0.0" (fmt_fix drops the 0) */
{
    int32_t a = dbx10 < 0 ? -dbx10 : dbx10;
    char t[12];
    fmt_fix(t, a, 1);
    b[0] = 0;
    if (dbx10 < 0)
        str_cpy(b, "-", 2);
    if (a < 10)
        str_cpy(b + str_len(b), "0", 2);
    str_cpy(b + str_len(b), t, 10);
}

static int32_t comp_thr_dbx10(int32_t v)          /* THRSH in 0.1 dB, rounded (Streams: 256 = 6.02 dB / 256) */
{
    int32_t x = -1280 + 5 * (int32_t)(comp_k16(v) >> 8);
    return (x * 60206 + (x < 0 ? -128000 : 128000)) / 256000;
}

/* RATIO as the column shows it: "3.9" ":1" */
static void comp_ratio_text(int32_t v, char *val, const char **unit)
{
    int32_t r = COMP_RATIO[(32767u - comp_ratio16(v)) >> 7];
    fmt_fix(val, 2560 / (r > 0 ? r : 1), 1);
    *unit = ":1";
}

/* MKUP as the column shows it (at threshold knob thr): "0.0" / "+6.0" "dB", or "LIMIT" */
static void comp_makeup_text(int32_t v, int32_t thr, char *val, const char **unit)
{
    comp_set_t s = {0, thr, 0, 0, 1, v};
    comp_cfg_t c;
    comp_configure(&s, &c);
    *unit = "dB";
    if (c.atk < 0) {
        str_cpy(val, "LIMIT", 6);
        *unit = "";
    } else if (!c.makeup) {
        str_cpy(val, "0.0", 6);
    } else {
        char t[10];
        comp_fmt_db10(t, c.makeup / 1088);         /* log2 units -> 0.1 dB (6.02 dB / 65536) */
        val[0] = '+';
        str_cpy(val + 1, t, 6);
    }
}
