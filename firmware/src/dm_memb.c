/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE (MEMB as a drum model: one body per track, the fork's knobs) */
/* MEMB: a struck drum head, upstream Felucca 1.0's PHYS MEMB on the modal core (phys_dsp.c): the modes of a circular
 * membrane, HEAD blending the ideal head (inharmonic: toms, timpani) into a loaded one (harmonic: tabla), POS from the
 * centre (only the circular modes) to the rim, BEND a pitch drop after the strike (~60 ms), STICK the strike itself in
 * the output. Accent: upstream's ACC (90) at full velocity, scaled by the hit's velocity. One body per track
 * (memb_body): a hit while it rings strikes it again (the cut voice's declick taken back, as the head goes on). */
#define MEMB_NOTE 45                                  /* A2, 110 Hz at TUNE 0 */
#define MEMB_ACC 90
#define MEMB_GAIN 90000                               /* upstream PHYS_GAIN[PM_MEMB]: model 1.0 -> this (VOICE_FS scale) */
typedef struct {
    px_modal_t m;                                     /* the head */
    px_modal_blk_t k;                                 /* the last block's modes (the tests read them) */
    int32_t last;                                     /* the last sample out (taken back from a retrigger's declick) */
    uint32_t blk;                                     /* dblock of the last block rendered */
} memb_body_t;
static memb_body_t memb_body[NTRK] __attribute__((section(".pool")));

static void memb_trigger(track_t *t, dvoice_t *v)
{
    memb_body_t *B = &memb_body[t - trk];
    if (B->blk == dblock && B->last) {
        t->dtail -= B->last;                          /* the head rings on: no declick copy of it */
    } else {
        memset(&B->m, 0, sizeof B->m);
        B->m.rng = 0x9E3779B9u ^ v->age;
    }
    B->m.trig = 1;
}

static void memb_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    memb_body_t *B = &memb_body[t - trk];
    const int16_t *p = &t->p[P_E0];
    int32_t y[CTL], ax[CTL], pk = 0, exc = p[6] * 258;
    int32_t acc = clamp(MEMB_ACC * v->vel * 4, 0, 65536);
    uint32_t i;
    if (n > CTL)
        n = CTL;
    px_memb_block(&B->k, &B->m, qnote_inc(MEMB_NOTE + p[0]), p[3] * 516, p[2] * 516, p[1] * 516, acc, p[4] * 516,
                  p[5] * 6192);
    px_modal_run(&B->k, &B->m, y, ax, n);
    for (i = 0; i < n; i++) {
        int32_t s = px_m(y[i] + px_m(ax[i], exc, 15), MEMB_GAIN, 20), a = s < 0 ? -s : s;
        uint32_t tt = v->t + i;
        if (a > 24000) {                              /* soft knee: only peaks saturate (upstream) */
            a = 24000 + (softclip(clamp(a - 24000, 0, 1 << 20) * 2) >> 1);
            s = s < 0 ? -a : a;
        }
        if (tt >= LIFE_A)
            s = tt >= LIFE_B ? 0 : (int32_t)((int64_t)s * (int32_t)(LIFE_B - tt) / (int32_t)(LIFE_B - LIFE_A));
        s = mulq15(s, vel_gain(v));
        out[i] += s;
        v->last = s;
        pk = (s < 0 ? -s : s) > pk ? (s < 0 ? -s : s) : pk;
    }
    B->last = v->last;
    B->blk = dblock;
    v->t += n;
    v->x[0] = pk < 2 ? v->x[0] + 1 : 0;              /* quiet blocks (as dm_qend) */
    if (v->t >= LIFE_B || (v->t > FS / 10u && v->x[0] >= DM_QQUIET)) {
        dm_end(v);
        B->last = 0;
    }
}

#define DM_MEMB_DEF {"MEMB", 1, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 60), \
    PD("TONE", F_INT, 0, 127, 64), PD("HEAD", F_INT, 0, 127, 20), PD("POS", F_INT, 0, 127, 40), \
    PD("BEND", F_INT, 0, 127, 30), PD("STICK", F_INT, 0, 127, 24), PD("-", F_INT, 0, 0, 0)}, memb_trigger, memb_render, 2}
