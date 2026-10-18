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
typedef struct {                        /* XTTRX, XCTRX (dm_xttrx.c) */
    uint32_t inc0, inc_r, e, d_e, e_r, d_r, sag, sl;
    int32_t th, cl, cz, px, hp1, hp1p, hp2;
    uint8_t mc;
} xttrx_t;
typedef struct {                        /* CLTRX (dm_cltrx.c) */
    uint32_t e, d_e, sx;
    int32_t ax, ay, ce;
} cltrx_t;
typedef struct {                        /* RSTRX (dm_rstrx.c) */
    uint32_t e, d_e, tj, d0, kk, mm;
    int32_t amp, g;
} rstrx_t;
typedef struct { int32_t b0, a1, a2; } tnbq_t;          /* CPTRX, MATRX (dm_trxn.c): a 2-pole section, Q30 */
typedef struct {                        /* CPTRX (dm_trxn.c) */
    tnbq_t hp, lp, rq;
    int32_t s[8], r[6];
    uint32_t e, d, ac, a4, re, rd;
    uint32_t on[5], cend, rend;
    uint8_t nb;
} cptrx_t;
typedef struct {                        /* MATRX (dm_trxn.c) */
    tnbq_t hp;
    int32_t s[6], lp, g, rk, gq, et, gv;
    uint32_t poff, ia, idv, dv;
    uint32_t t1, t2, t2v, t3, tend;
} matrx_t;
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
typedef struct {                        /* BDEFM, XTEFM (dm_bdefm.c) */
    uint32_t inc0, e_a, d_a, e_r, d_r, e_i, d_i;
    int32_t rk, i0, mf, fb, m, x1, x2, y1, y2, q1, q2, hx, hy;
    uint8_t xt;
} bdefm_t;
typedef struct { int32_t x1, x2, y1, y2, q1, q2; } efmn_hp2_t;   /* the EFM noise voices' track filter (dm_efmn.c) */
typedef struct {                        /* SDEFM, CPEFM's noise-FM carrier (dm_efmn.c) */
    uint32_t pc, pm, inc, incm, dev, ei, di;
    int32_t fz, nz;
    uint8_t hc, hold;
} efmn_fm_t;
typedef struct {                        /* SDEFM, CPEFM (dm_efmn.c) */
    efmn_fm_t fm;
    efmn_hp2_t hp;
    uint32_t ea, da, en, dn, ec, dc, tc, per;
    int32_t nlp, nhp, nxp, hh, hx, hk;
    uint8_t nb;
} efmn_t;
typedef struct {                        /* RSEFM (dm_efmn.c) */
    efmn_hp2_t hp;
    uint32_t pc[2], inc[2], dev[2], e[2], d[2];
    int32_t n[2], z[2], g, hs;
} efmrs_t;
typedef struct {                        /* CBEFM (dm_efmm.c) */
    uint32_t pc[2], pm[2], inc[2], minc[2], ea, es, ei, da, ds, di;
    int32_t y[2], bq, sn, sk, lvl;
} cbefm_t;
typedef struct {                        /* HHEFM (dm_efmm.c) */
    uint32_t pc, pm, inc, minc, ea, ei, da, di, tp, tinc;
    int32_t bq, dep, h, up, l, g, gs, lvl;
} hhefm_t;
typedef struct {                        /* CYEFM (dm_efmm.c) */
    uint32_t pc[4], pm[4], wc[4], wm[4], ea, da, ih, di;
    int32_t yp[4], bq, gk, s1, s2, lvl;
} cyefm_t;
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
    xttrx_t xt;
    cltrx_t cl;
    rstrx_t rs;
    cptrx_t cp;
    matrx_t ma;
    trxm_t tm;
    trxcb_t cb;
    bdefm_t bdefm;
    efmn_t en;
    efmrs_t er;
    cbefm_t cbe;
    hhefm_t hhe;
    cyefm_t cye;
} dm_state_t;
