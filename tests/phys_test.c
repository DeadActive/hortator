/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* PHYS: MEMB (the drum model: the head's modes, BEND, POS, retrigger, voices, level) and RESON MODAL. */
#include "drum_host.h"
#include <math.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
static int32_t buf[44100 * 3];

static double mode_hz(const px_mode_t *m)           /* g = tan(pi f) (Q28) -> Hz */
{
    return atan((double)m->g / (1 << 28)) / M_PI * 44100.0;
}
static void memb_on(track_t *t)
{
    drum_set_model(t, DM_MEMB);
}
static px_modal_blk_t memb_blk(track_t *t, uint32_t vel)   /* the first block of a hit: its modes */
{
    px_modal_blk_t K;
    drum_hit(t, vel);
    render_track(t, buf, CTL);
    K = memb_body[t - trk].k;
    return K;
}

static void test_memb_modes(void)
{
    px_modal_blk_t K;
    uint32_t i;
    int ok = 1;
    double f0;
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E3] = 0;                              /* HEAD: ideal */
    trk[0].p[P_E5] = 0;                              /* no BEND */
    K = memb_blk(&trk[0], 96);
    f0 = mode_hz(&K.m[0]);
    check("MEMB: TUNE 0 is A2 (110 Hz, within 1 %)", fabs(f0 / 110.0 - 1) < 0.01);
    for (i = 1; i < K.n; i++)
        ok &= fabs(mode_hz(&K.m[i]) / f0 - PX_MEMB_IDEAL[i] / 65536.0) < 0.005 * PX_MEMB_IDEAL[i] / 65536.0 + 0.002;
    check("MEMB HEAD 0: the ideal head's mode ratios", ok && K.n == PX_NMEMB);
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E3] = 127;
    trk[0].p[P_E5] = 0;
    K = memb_blk(&trk[0], 96);
    for (i = 1, ok = 1; i < K.n; i++)
        ok &= fabs(mode_hz(&K.m[i]) / mode_hz(&K.m[0]) - PX_MEMB_LOADED[i] / 65536.0) < 0.01 * PX_MEMB_LOADED[i] / 65536.0;
    check("MEMB HEAD 127: the loaded head's (harmonic) ratios", ok);
}

static void test_memb_bend_pos(void)
{
    px_modal_blk_t K;
    uint32_t i;
    int ok = 1;
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E5] = 127;                            /* BEND 12 semitones */
    K = memb_blk(&trk[0], 96);
    check("MEMB BEND: the strike starts an octave up", fabs(mode_hz(&K.m[0]) / 220.0 - 1) < 0.03);
    render_track(&trk[0], buf, 44100 / 5);           /* 200 ms */
    check("MEMB BEND: back on the note within 200 ms (1 %)", fabs(mode_hz(&memb_body[0].k.m[0]) / 110.0 - 1) < 0.01);
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E4] = 0;                              /* POS: the centre */
    K = memb_blk(&trk[0], 96);
    for (i = 0; i < K.n; i++)
        ok &= PX_MEMB_M[i] ? K.m[i].a == 0 : K.m[i].a != 0;
    check("MEMB POS centre: only the circular modes (m = 0) sound", ok);
}

static void test_memb_voice(void)
{
    uint32_t i, n = 0;
    int32_t pk_t = 0, pk_m = 0;
    host_init();
    check("MEMB: a heavy voice (weight 2), one body per track", DMODELS[DM_MEMB].weight == 2 && DMODELS[DM_MEMB].voices == 1);
    memb_on(&trk[0]);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], buf, 4410);
    drum_hit(&trk[0], 127);                          /* re-struck while ringing */
    check("MEMB retrigger: the head rings on (no declick tail added)", trk[0].dtail == 0 && memb_body[0].m.s[0][0] != 0);
    for (i = 0; i < 5u; i++) {
        memb_on(&trk[i]);
        drum_hit(&trk[i], 127);
    }
    for (i = 0; i < 5u; i++)
        n += trk[i].v[0].active;
    check("MEMB on 5 tracks: at most 4 sound (the cap of 8, weight 2)", n <= 4u);
    host_init();                                     /* level: within -6 .. +3 dB of a default TOM */
    drum_set_model(&trk[0], DM_TOM);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], buf, 22050);
    for (i = 0; i < 22050u; i++)
        pk_t = buf[i] > pk_t ? buf[i] : -buf[i] > pk_t ? -buf[i] : pk_t;
    host_init();
    memb_on(&trk[0]);
    drum_hit(&trk[0], 127);
    render_track(&trk[0], buf, 22050);
    for (i = 0; i < 22050u; i++)
        pk_m = buf[i] > pk_m ? buf[i] : -buf[i] > pk_m ? -buf[i] : pk_m;
    printf("     MEMB peak %d, TOM peak %d\n", pk_m, pk_t);
    check("MEMB level: a default hit within -6 .. +3 dB of a default TOM", pk_m * 2 >= pk_t && pk_m <= pk_t * 141 / 100);
}

static void test_memb_accent(void)
{
    px_modal_blk_t a, b;
    host_init();
    memb_on(&trk[0]);
    a = memb_blk(&trk[0], 96);
    host_init();
    memb_on(&trk[0]);
    b = memb_blk(&trk[0], 127);
    check("MEMB accent: a harder, brighter strike", b.ex.g > a.ex.g);
}

static void modal_kick(track_t *t)                    /* a kick through RESON MODAL at full MIX */
{
    drum_set_model(t, DM_K909);
    t->p[P_RMODEL] = RS_MODAL;
    t->p[P_RTUNE] = 60;
    t->p[P_RDECAY] = 64;
    t->p[P_RMIX] = 127;
    t->p[P_RSTRCT] = 70;
    t->p[P_RTONE] = 80;
}
static void test_modal_ring(void)
{
    uint32_t b, i, z = 1;
    int32_t pk = 0;
    host_init();
    modal_kick(&trk[0]);
    trk[0].p[P_RDECAY] = 100;                         /* (RESON's DECAY: a ring time, here ~2.3 s) */
    drum_hit(&trk[0], 127);
    for (b = 0; b < 44100u / 2u / CTL; b++)
        render_mix(0, 0, CTL);
    check("MODAL: the ring is on half a second after a hit (DECAY 100)", trk[0].rs.ring);
    trk[0].p[P_RDECAY] = 64;
    for (b = 0; b < 44100u * 20u / CTL && trk[0].rs.ring; b++)
        render_mix(0, 0, CTL);
    for (i = 0; i < sizeof rs_buf[0] / 2u; i++)
        z &= rs_buf[0][i] == 0;
    check("MODAL: rings out to off, its states all zero (DECAY 64)", !trk[0].rs.ring && z);
    host_init();
    modal_kick(&trk[0]);
    drum_hit(&trk[0], 127);
    render_mix(0, 0, 44100 / 2);
    drum_cut(&trk[0]);
    render_mix(0, 0, 2 * CTL);
    for (i = 0, z = 1; i < sizeof rs_buf[0] / 2u; i++)
        z &= rs_buf[0][i] == 0;
    (void)pk;
    check("MODAL: a cut ends the ring (off, its states cleared)", !trk[0].rs.ring && z);
}
static void test_modal_tune(void)
{
    px_modal_blk_t K0, K1;
    int32_t in[CTL] = {0}, ring[CTL];
    host_init();
    modal_kick(&trk[0]);
    reson_modal(&trk[0], in, ring, CTL);              /* (called directly: the LFOs rewrite rfine every block) */
    K0 = trk_modal_blk;                               /* reson_modal's last block (a test hook, see Step 3) */
    trk[0].rfine = 12 * 256;                          /* R.TUN: an octave up */
    reson_modal(&trk[0], in, ring, CTL);
    K1 = trk_modal_blk;
    check("MODAL: R.TUN moves the modes (an octave: twice the frequency, 1 %)",
          fabs(mode_hz(&K1.m[0]) / mode_hz(&K0.m[0]) - 2.0) < 0.02);
}
static void test_modal_sustained(void)
{
    uint32_t i, k;
    int32_t pk = 0;
    host_init();
    drum_set_model(&trk[0], DM_S909);                 /* (broadband: body and noise ring the modes) */
    trk[0].p[P_RMODEL] = RS_MODAL;
    trk[0].p[P_RDECAY] = 127;
    trk[0].p[P_RMIX] = 127;
    for (k = 0; k < 16u; k++)
        trk[0].step[k].on = 1;
    trk[0].p[P_SDIV] = 3;                            /* 1/32 */
    song.g[G_BPM] = 240;
    transport_req = 1;
    for (k = 0; k < 44100u * 10u / CTL; k++) {        /* 10 s of hats into a ringing body */
        render_mix(0, 0, CTL);
        for (i = 0; i < CTL; i++)
            pk = mixo[2 * i] > pk ? mixo[2 * i] : -mixo[2 * i] > pk ? -mixo[2 * i] : pk;
    }
    printf("     MODAL sustained by 1/32 snares, DECAY 127: master peak %d\n", pk);
    check("MODAL fed for 10 s at DECAY 127: bounded (the master never clips past full scale)", pk > 1000 && pk <= 32767);
}

int main(void)
{
    test_memb_modes();
    test_memb_bend_pos();
    test_memb_voice();
    test_memb_accent();
    test_modal_ring();
    test_modal_tune();
    test_modal_sustained();
    printf(fails ? "phys_test: %d FAILED\n" : "phys_test: all passed\n", fails);
    return fails ? 1 : 0;
}
