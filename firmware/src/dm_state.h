/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Per-voice state of the M1-C models (dvoice_t.ms): one union member per model (Tasks 3-7 add them), and the
 * Q24 filter states of the toolkit (dm_dsp.c). */
typedef struct { int32_t g, r, h, s1, s2; } qsvf_t;     /* stmlib Svf: coefficients, two states */
typedef struct { int32_t g, gi, s; } qpole_t;           /* stmlib OnePole */
typedef struct {
    int32_t f0, q, scale, tone_f, leak, pulse_h, afm, sfm, pre, post;
    int32_t pulse, pulse_lp, fm_lp, retrig, lp_out, tone_lp;
    int32_t rem, fmrem, n;
    qsvf_t res;
} kboom_t;
typedef struct {
    int32_t f0, dirt, fm_amt, fm_dec, body_dec, tone_f, tlevel;
    int32_t phase, pnoise, fm, fm_lp, body, body_lp, trans, trans_lp, tone_lp;
    int32_t c_lp, c_hp, n_lp, n_hp;
    int32_t bpw, fpw;
    uint32_t rng;
    qsvf_t click;
} kpunc_t;
typedef struct {
    int32_t gain[5], snappy, leak, ndec, pulse_h;
    int32_t pulse, pulse_lp, nenv;
    int32_t rem;
    uint32_t rng;
    qsvf_t res[5], nf;
} ssnap_t;
typedef struct {
    int32_t f0, fm_amt, ddec, sdec, dlvl, slvl, rna;
    int32_t ph0, ph1, damp, samp, fm;
    int32_t hold, t;
    uint32_t rng;
    qpole_t dlp, shp;
    qsvf_t slp;
} scrak_t;
typedef struct { int32_t ph, f, next; int32_t lp, hp; uint8_t high; } qbosc_t;   /* Plaits Oscillator, square / saw */
typedef struct {
    int32_t env, edec, cdec, noisy, nf, nclk, nsmp;
    uint32_t ph[6], inc[6];
    qbosc_t osc[6];
    uint8_t ring;                       /* 0 HMETL (squares, swing VCA, resonance), 1 HNOIS (ring mod, linear, 2-stage) */
    uint32_t rng;
    qsvf_t col, hpf;
} hh_t;
typedef union {
    int32_t raw[2];
    scrak_t sc;
    ssnap_t ss;
    kpunc_t kp;
    kboom_t kb;
    hh_t hh;
} dm_state_t;
