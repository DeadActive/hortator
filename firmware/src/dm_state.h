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
typedef struct { int32_t a1, a2, g, k, s1, s2; } trxmf_t;   /* the TRX metal ports' SVF (dm_trxm.c): a1 a2 k Q30, g Q28 */
typedef struct {                        /* CHTRX OHTRX CYTRX (dm_trxm.c) */
    uint32_t ph[6], inc[6];
    int32_t w[7], wsum, gb, gn;
    trxmf_t hp, lp, pk, tp;
    int32_t pkg, e, et, ek, ds, eend;
    uint32_t d, dt, nh;
    uint8_t ish, osh, top;
} trxm_t;
typedef struct { int32_t pn[7], hx, hy, hl, el; } trxcbp_t;   /* CBTRX: its pink noise, the floor, the grit */
typedef struct {                        /* CBTRX (dm_trxcb.c) */
    uint32_t pb, pa, ib, ia, e, e0, ea, eg, eb, d, nh;
    int32_t gb, ga, gae, m, erk, bump, glvl;
    trxcbp_t pk;
    trxmf_t db, da, enh;
} trxcb_t;
typedef union {
    int32_t raw[2];
    scrak_t sc;
    ssnap_t ss;
    kpunc_t kp;
    kboom_t kb;
    hh_t hh;
    ktrx_t kt;
    trxm_t tm;
    trxcb_t cb;
} dm_state_t;
