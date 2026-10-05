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

typedef struct { int32_t atk, thr, amt, rel, knee; } comp_set_t;   /* ATK THRSH AMNT REL 0..127, KNEE 0 / 1 */
typedef struct {
    int64_t atk, dec;            /* attack / decay coefficients (Q31); atk -1 = instant (the limiter) */
    int32_t ratio, thr, makeup;  /* reciprocal ratio (8:8), threshold and makeup gain (log2, 65536 / octave) */
    uint8_t soft;                /* soft knee (Streams' "alternate") */
} comp_cfg_t;

static uint32_t comp_k16(int32_t v) { return (uint32_t)clamp(v, 0, 127) * 65535u / 127u; }   /* knob -> 16 bit */

/* AMNT: 0..63 the ratio from 1:1 to Streams' steepest, no makeup; 64..127 makeup gain up to the limiter */
static uint32_t comp_amount16(int32_t v)
{
    v = clamp(v, 0, 127);
    return v < 64 ? 32767u - (uint32_t)v * 32767u / 63u : 32768u + (uint32_t)(v - 64) * 32767u / 63u;
}

/* Compressor::Configure (globals path) */
static void comp_configure(const comp_set_t *s, comp_cfg_t *c)
{
    uint32_t atk_t = comp_k16(s->atk) * (128u + 128u + 99u) >> 16;   /* 0.1 ms .. 0.6 s */
    uint32_t dec_t = 128u + 99u + (comp_k16(s->rel) >> 8);           /* 59 ms .. 5.8 s */
    uint32_t amount = comp_amount16(s->amt);
    c->atk = COMP_LP_TABLE[atk_t];
    c->dec = COMP_LP_TABLE[dec_t];
    c->soft = (uint8_t)(s->knee != 0);
    c->thr = (-1280 + 5 * (int32_t)(comp_k16(s->thr) >> 8)) * 256;
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

/* ------------------------------------------------------- display --- */
static int32_t comp_thr_dbx10(int32_t v)          /* THRSH in 0.1 dB, rounded (Streams: 256 = 6.02 dB / 256) */
{
    int32_t x = -1280 + 5 * (int32_t)(comp_k16(v) >> 8);
    return (x * 60206 + (x < 0 ? -128000 : 128000)) / 256000;
}

/* AMNT as the column shows it: the ratio ("3.9" ":1"), the makeup ("+6.0" "dB") or "LIMIT" (at threshold thr) */
static void comp_amount_text(int32_t amt, int32_t thr, char *val, const char **unit)
{
    comp_set_t s = {0, thr, amt, 0, 1};
    comp_cfg_t c;
    comp_configure(&s, &c);
    *unit = "";
    if (clamp(amt, 0, 127) < 64) {
        fmt_fix(val, 2560 / (c.ratio > 0 ? c.ratio : 1), 1);
        *unit = ":1";
    } else if (c.atk < 0) {
        str_cpy(val, "LIMIT", 6);
    } else {
        char t[8];
        fmt_fix(t, c.makeup / 1088, 1);            /* log2 units -> 0.1 dB (6.02 dB / 65536) */
        val[0] = '+';
        str_cpy(val + 1, t, 6);
        *unit = "dB";
    }
}
