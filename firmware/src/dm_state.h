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
typedef struct {                        /* KTRX (dm_ktrx.c) */
    uint32_t inc0, inc_d, inc_f, inc_r, ph_h;
    int32_t e_d, e_t, e_a, e_s, e_h, e_n;
    int32_t punch, sub, th, cgain, nlev;
    uint32_t k_n, d_amp, d_ramp, e_amp, e_r;
    uint8_t first;
} ktrx_t;
typedef struct {                        /* SDTRX (dm_sdtrx.c) */
    int32_t ga, gb, bk, g, nl, xl, x1, x2, hy, hx;
    uint32_t e_amp, d_amp, bu, du, be, dbe, drive, e_n, e_x;
} sdtrx_t;
typedef struct {                        /* B2TRX (dm_b2trx.c) */
    uint32_t e_r, d_r, e_amp, d_amp, hold, e_n;
    int32_t tick, nl, qb, g8, hk, lp, z1, z2;
} b2trx_t;
typedef union {
    int32_t raw[2];
    scrak_t sc;
    ssnap_t ss;
    kpunc_t kp;
    kboom_t kb;
    hh_t hh;
    ktrx_t kt;
    sdtrx_t sd;
    b2trx_t b2;
} dm_state_t;
