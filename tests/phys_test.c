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

/* final review: MODAL's modes keep a positive damping (r = rpg - g > 0) over the whole knob range with the fine pitch
 * at its ends (a wrapped rpg fed itself: an endless near-Nyquist tone) */
static void test_modal_stable(void)
{
    static const int16_t TUNE[7] = {24, 48, 60, 69, 72, 84, 96}, ST[7] = {0, 34, 70, 94, 116, 124, 127};
    static const int16_t DEC[5] = {0, 32, 64, 100, 127}, TON[4] = {0, 32, 64, 127};
    static const int32_t FINE[3] = {-RS_FINE, 0, RS_FINE};
    int32_t in[CTL] = {0}, ring[CTL];
    uint32_t a, b, c, d, e, k, bad = 0, runs = 0;
    for (a = 0; a < 7u; a++)
        for (b = 0; b < 3u; b++)
            for (c = 0; c < 7u; c++)
                for (d = 0; d < 5u; d++)
                    for (e = 0; e < 4u; e++) {
                        host_init();
                        modal_kick(&trk[0]);
                        trk[0].p[P_RTUNE] = TUNE[a];
                        trk[0].rfine = FINE[b];
                        trk[0].p[P_RSTRCT] = ST[c];
                        trk[0].p[P_RDECAY] = DEC[d];
                        trk[0].p[P_RTONE] = TON[e];
                        reson_modal(&trk[0], in, ring, CTL);
                        for (k = 0; k < trk_modal_blk.n; k++)
                            bad += trk_modal_blk.m[k].rpg <= trk_modal_blk.m[k].g || trk_modal_blk.m[k].rpg <= 0;
                        runs++;
                    }
    printf("     MODAL stability: %u unstable modes over %u settings\n", bad, runs);
    check("MODAL: every mode damped (rpg > g) at every TUNE / R.TUN / STRCT / DECAY / TONE", bad == 0);
}
static void test_modal_decay0_ends(void)
{
    uint32_t b;
    host_init();
    drum_set_model(&trk[0], DM_S909);
    trk[0].p[P_RMODEL] = RS_MODAL;
    trk[0].p[P_RTUNE] = 69;
    trk[0].p[P_RSTRCT] = 124;
    trk[0].p[P_RDECAY] = 0;
    trk[0].p[P_RTONE] = 0;
    trk[0].p[P_RMIX] = 127;
    drum_hit(&trk[0], 127);
    for (b = 0; b < 44100u / CTL; b++)
        render_mix(0, 0, CTL);
    check("MODAL DECAY 0 (STRCT 124, TONE 0, TUNE A4): one hit rings out within 1 s", !trk[0].rs.ring);
}
static double mode_t60(const px_mode_t *m)           /* the mode's ring time (s): Q = 1 / r, T60 = Q ln 1000 / (pi f) */
{
    double r = (double)((int64_t)m->rpg - m->g) / (1 << 28), f = mode_hz(m);
    return r > 0 && f > 0 ? log(1000.0) / (r * M_PI * f) : 1e9;
}
static void test_modal_upper_modes(void)
{
    int32_t in[CTL] = {0}, ring[CTL];
    double t1, t2;
    host_init();
    modal_kick(&trk[0]);
    trk[0].p[P_RTUNE] = 60;
    trk[0].p[P_RDECAY] = 100;                        /* 2.30 s */
    trk[0].p[P_RTONE] = 127;
    trk[0].p[P_RSTRCT] = 34;
    reson_modal(&trk[0], in, ring, CTL);
    t1 = mode_t60(&trk_modal_blk.m[0]);
    t2 = mode_t60(&trk_modal_blk.m[1]);
    printf("     MODAL DECAY 100 TONE 127: mode 1 %.2f s, mode 2 %.2f s\n", t1, t2);
    check("MODAL: the first mode rings DECAY's time (2.30 s, 15 %)", fabs(t1 / 2.30 - 1) < 0.15);
    check("MODAL TONE 127: the 2nd mode rings nearly as long (upstream's Q per cycle)", t2 >= 0.8 * t1);
}
/* final review: a ringing MEMB re-struck at another velocity: no jump in the ringing body's level */
static void test_memb_retrigger_vel(void)
{
    static const uint8_t V[2][2] = {{127, 96}, {96, 127}};
    uint32_t c, i;
    for (c = 0; c < 2u; c++) {
        int32_t nat = 0, step;
        host_init();
        memb_on(&trk[0]);
        trk[0].p[P_E1] = 100;
        trk[0].p[P_E2] = 0;
        trk[0].p[P_E6] = 0;
        drum_hit(&trk[0], V[c][0]);
        render_track(&trk[0], buf, 13216);           /* 0.3 s (whole blocks) */
        for (i = 13216u - 64u; i < 13216u; i++)
            nat = abs(buf[i] - buf[i - 1]) > nat ? abs(buf[i] - buf[i - 1]) : nat;
        drum_hit(&trk[0], V[c][1]);
        render_track(&trk[0], buf + 13216, CTL);
        step = abs(buf[13216] - buf[13215]);
        printf("     MEMB re-struck %u -> %u: step %d (natural %d)\n", V[c][0], V[c][1], step, nat);
        check(c ? "MEMB re-struck 96 -> 127: no level jump" : "MEMB re-struck 127 -> 96: no level jump", step <= 2 * nat + 64);
    }
}

/* final review: MEMB's modes damped at every knob extreme, and a hit at DECAY 127 ends by the 6 s lifetime */
static void test_memb_stable(void)
{
    static const int16_t LO[7] = {-24, 0, 0, 0, 0, 0, 0}, HI[7] = {24, 127, 127, 127, 127, 127, 127};
    uint32_t c, k, b, bad = 0;
    for (c = 0; c < 128u; c++) {
        host_init();
        memb_on(&trk[0]);
        for (k = 0; k < 7u; k++)
            trk[0].p[P_E0 + k] = ((c >> k) & 1u) ? HI[k] : LO[k];
        drum_hit(&trk[0], 127);
        render_track(&trk[0], buf, CTL);
        for (k = 0; k < memb_body[0].k.n; k++)
            bad += memb_body[0].k.m[k].rpg <= memb_body[0].k.m[k].g;
    }
    check("MEMB: every mode damped at every knob extreme", bad == 0);
    host_init();
    memb_on(&trk[0]);
    trk[0].p[P_E1] = 127;
    drum_hit(&trk[0], 127);
    for (b = 0; b < 44100u * 7u / CTL && trk[0].v[0].active; b++)
        render_track(&trk[0], buf, CTL);
    check("MEMB DECAY 127: the voice ends by its 6 s lifetime", !trk[0].v[0].active);
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
    test_modal_stable();
    test_modal_decay0_ends();
    test_modal_upper_modes();
    test_memb_retrigger_vel();
    test_memb_stable();
    printf(fails ? "phys_test: %d FAILED\n" : "phys_test: all passed\n", fails);
    return fails ? 1 : 0;
}
