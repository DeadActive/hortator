/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* SRR and INSERT (insert.c), on the real sources. The INSERT's tests are Skiff's (tests/insert_test.c of Skiff 1.3,
 * from Felucca 1.5's INSERT), on a drum track:
 * 1. bit for bit: a phrase (four tracks, DIST, the sends, FILTER, RESON, the tails) renders as drum 0.26.0 rendered it
 *    with SRR 0, with TYPE OFF and any A B C MIX, with MIX 0 of every TYPE; SRR and MIX 100 % of each TYPE change it.
 * 2. SRR: the hold (each value held 256 + 2 SRR^2 / 256 samples), its place (after FILTER, before DIST), a hit starts
 *    on a fresh sample, nothing runs at 0.
 * 3. each TYPE on 3 and 8 saws, A B C at their corners: no DC, bounded, MIX 0 = the input bit for bit, MIX 50 %
 *    halfway to MIX 100 %; CRUSH's steps odd.
 * 4. MIX glides (2.9 ms), a TYPE change fades out, switches, fades in; TYPE OFF while silent: at rest.
 * 5. MOTION: SRR, A B C and MIX recorded and lockable, not TYPE (as FILTER's TYPE, RESON's MODEL); a lock plays.
 * 6. the knobs' names (A B C by the TYPE), SRR's rate on the screen; the cost per sample (printed). */
#include "drum_host.h"
#include <sys/wait.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

/* ------------------------------------------------------------------ 1 --- */
#define PHRASE_0260 0x3c7f2bcfa8133b84ull         /* drum 0.26.0's hash of the phrase (before SRR and the INSERT) */
static int32_t ph_srr = -1, ph_ty = -1, ph_a, ph_b, ph_c, ph_mix, ph_trk = 4;   /* ph_trk: the tracks set (4: 0..3) */
static void set_ins(track_t *t, int32_t ty, int32_t a, int32_t b, int32_t c, int32_t mix)
{
    t->p[P_ITYPE] = (int16_t)ty;
    t->p[P_IA] = (int16_t)a;
    t->p[P_IB] = (int16_t)b;
    t->p[P_IC] = (int16_t)c;
    t->p[P_IMIX] = (int16_t)mix;
}
/* the phrase: 4 tracks, DIST / sends / FILTER / RESON on, 2 s playing and 1 s of tails, the mix hashed; SRR and
 * the INSERT as ph_* say (-1: the power-on values) on the tracks ph_trk says */
static uint64_t ph_hash(void)
{
    static int32_t l[CTL], r[CTL];
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t b, i, k;
    host_init();
    song.g[G_BPM] = 120;
    for (k = 0; k < 16u; k++) {
        trk[0].step[k].on = (k % 4u) == 0u;
        trk[1].step[k].on = (k % 8u) == 4u;
        trk[2].step[k].on = (k % 2u) == 0u;
        trk[3].step[k].on = (k % 4u) == 2u;
        trk[2].step[k].acc = (k % 4u) == 2u;
    }
    trk[0].p[P_REV] = 40;
    trk[1].p[P_DIST] = 60;
    trk[1].p[P_DLY] = 50;
    trk[2].p[P_CHOR] = 30;
    trk[3].p[P_FTYPE] = FT_LP;
    trk[3].p[P_FCUT] = 60;
    trk[3].p[P_FRESO] = 90;
    trk[3].p[P_RMODEL] = RS_STRNG;
    trk[3].p[P_RMIX] = 80;
    for (k = 0; k < 4u; k++) {
        if (ph_trk < 4 && (int32_t)k != ph_trk)
            continue;
        if (ph_srr >= 0)
            trk[k].p[P_SRR] = (int16_t)ph_srr;
        if (ph_ty >= 0)
            set_ins(&trk[k], ph_ty, ph_a, ph_b, ph_c, ph_mix);
    }
    transport_req = 1;
    for (b = 0; b < 3u * FS / CTL; b++) {
        if (b == 2u * FS / CTL)
            transport_req = 2;
        render_mix(l, r, CTL);
        for (i = 0; i < CTL; i++)
            for (k = 0; k < 4u; k++) {
                h ^= (((uint32_t)l[i] >> (8u * k)) & 0xFFu) ^ ((((uint32_t)r[i] >> (8u * k)) & 0xFFu) << 3);
                h *= 0x100000001B3ull;
            }
    }
    return h;
}

static void test_identity(void)
{
    uint64_t h;
    int32_t ty;
    int ok0 = 1, okw = 1;
    ph_srr = ph_ty = -1;
    ph_trk = 4;
    check("power-on values (SRR 0, TYPE OFF): the phrase as drum 0.26.0 rendered it, bit for bit",
          ph_hash() == PHRASE_0260);
    check("SRR 0 / TYPE OFF are the power-on values; MIX 100 %, A B C as Skiff's",
          TP[P_SRR].def == 0 && TP[P_ITYPE].def == IT_OFF && TP[P_IMIX].def == 127 && TP[P_IA].def == 64 &&
              TP[P_IB].def == 96 && TP[P_IC].def == 96);
    ph_srr = 0;
    ph_ty = IT_OFF; ph_a = 1; ph_b = 2; ph_c = 3; ph_mix = 50;
    check("SRR 0 and TYPE OFF with A B C MIX set on every track: bit for bit as 0.26.0", ph_hash() == PHRASE_0260);
    for (ty = 1; ty < IT_N; ty++) {
        ph_ty = ty; ph_a = 127; ph_b = 127; ph_c = 127; ph_mix = 0;
        ok0 &= ph_hash() == PHRASE_0260;
        ph_mix = 127;
        ph_trk = 1;                                  /* (the DIST track: the INSERT after it) */
        h = ph_hash();
        ph_trk = 4;
        if (h == PHRASE_0260)
            printf("     %s: the phrase as 0.26.0's\n", N_ITYPE[ty]);
        okw &= h != PHRASE_0260;
    }
    check("MIX 0 of every TYPE on every track (the DIST track's tail too): bit for bit as 0.26.0", ok0);
    check("MIX 100 % of every TYPE: the phrase changes", okw);
    ph_ty = -1;
    ph_srr = 20;
    ph_trk = 1;
    check("SRR 20: the phrase changes", ph_hash() != PHRASE_0260);
    ph_srr = -1;
    ph_trk = 4;
}

/* ------------------------------------------------------------------ 2 --- */
#define NS (2u * FS / CTL * CTL)                      /* two seconds, whole blocks */
static int32_t sig_in[NS], sig_a[NS], sig_b[NS];

static void test_srr(void)
{
    track_t *t = &trk[0];
    uint32_t i, ok = 1, runs = 0, run = 0, bad_run = 0;
    int32_t v;
    host_init();
    for (i = 0; i < NS; i++)
        sig_in[i] = (int32_t)(i * 7u) - 300000;      /* a ramp: every sample its own value */
    check("SRR: each value held 256 + 2 SRR^2 / 256 samples (SRR 1: 1.008, 16: 3, 64: 33, 127: 127.0)",
          srr_period(0) == 256 && srr_period(1) == 258 && srr_period(16) == 768 && srr_period(64) == 8448 &&
              srr_period(127) == 32514);
    t->p[P_SRR] = 16;                                /* exactly 3 */
    memcpy(sig_a, sig_in, sizeof sig_in);
    for (i = 0; i < NS; i += CTL)
        track_srr(t, sig_a + i, CTL, 1);
    for (i = 0; i < NS; i++)
        ok &= sig_a[i] == sig_in[i / 3u * 3u];
    check("SRR 16: every third sample held for three (14.7 kHz), across the blocks, the first one at once", ok);
    for (v = 1; v < 128; v += 9) {                   /* how many new samples: N * 256 / P (+-1) */
        uint32_t n = 0, want;
        t->srr_a = t->srr_h = 0;
        t->p[P_SRR] = (int16_t)v;
        memcpy(sig_a, sig_in, sizeof sig_in);
        for (i = 0; i < NS; i += CTL)
            track_srr(t, sig_a + i, CTL, 1);
        for (i = 0; i < NS; i++)
            n += !i || sig_a[i] != sig_a[i - 1];
        want = (uint32_t)(((uint64_t)NS * 256u + (uint32_t)srr_period(v) - 1u) / (uint32_t)srr_period(v));
        ok &= n + 1u >= want && n <= want + 1u;
        if (!(n + 1u >= want && n <= want + 1u))
            printf("     SRR %d: %u new samples in %u, want %u\n", v, n, NS, want);
    }
    t->srr_a = t->srr_h = 0;
    t->p[P_SRR] = 127;
    memcpy(sig_a, sig_in, sizeof sig_in);
    for (i = 0; i < NS; i += CTL)
        track_srr(t, sig_a + i, CTL, 1);
    for (i = 1; i < NS; i++) {
        run++;
        if (sig_a[i] != sig_a[i - 1]) {
            bad_run |= run != 127u && run != 128u;
            runs++;
            run = 0;
        }
    }
    check("SRR at every 9th value: N x 256 / P new samples (+-1); SRR 127: runs of 127 / 128 (347 Hz)",
          ok && !bad_run && runs > NS / 129u);
    t->srr_a = t->srr_h = 0;
    t->p[P_SRR] = 0;
    memcpy(sig_a, sig_in, sizeof sig_in);
    for (i = 0; i < NS; i += CTL)
        track_srr(t, sig_a + i, CTL, 1);
    check("SRR 0: the block untouched", !memcmp(sig_a, sig_in, sizeof sig_in) && !t->srr_a && !t->srr_h);
    {   /* its place: after the FILTER (trk_inserts), on a sounding block only; silence resets it */
        int32_t b[CTL];
        t->srr_a = t->srr_h = 0;
        t->p[P_SRR] = 64;
        for (i = 0; i < CTL; i++)
            b[i] = sig_in[i + 5u];
        trk_inserts(t, b, CTL, 1);
        ok = 1;
        for (i = 0; i < CTL; i++)
            ok &= b[i] == sig_in[5];
        check("SRR in the track's inserts (after RESON and FILTER, before DIST): the block held", ok && t->srr_a);
        for (i = 0; i < CTL; i++)
            b[i] = 0;
        trk_inserts(t, b, CTL, 0);
        check("  a silent block: SRR at rest (the next hit starts on a fresh sample)", !t->srr_a && !t->srr_h);
    }
}

/* ------------------------------------------------------------------ 3 --- */
static void make_sig(uint32_t voices)              /* saws, 24000 each (a voice's level), a chord's pitches */
{
    static const double HZ[8] = {110.0, 139.0, 165.0, 220.0, 277.0, 330.0, 440.0, 554.0};
    uint32_t i, v;
    for (i = 0; i < NS; i++) {
        double s = 0;
        for (v = 0; v < voices; v++) {
            double ph = fmod(i * HZ[v] / FS + v * 0.37, 1.0);
            s += (ph * 2.0 - 1.0) * 24000.0;
        }
        sig_in[i] = (int32_t)s;
    }
}
/* the input through track 1's INSERT from rest into out */
static void run_ins(int32_t ty, int32_t a, int32_t b, int32_t c, int32_t mix, int32_t *out)
{
    track_t *t = &trk[0];
    uint32_t i;
    memset(ins, 0, sizeof ins);
    set_ins(t, ty, a, b, c, mix);
    memcpy(out, sig_in, sizeof sig_in);
    for (i = 0; i < NS; i += CTL)
        track_insert(t, out + i, CTL);
}
static void test_types(void)
{
    static const int16_t COR[3] = {0, 64, 127};
    uint32_t nv, ty, ia, ib, ic, i;
    host_init();
    printf("     the types on 3 and 8 saws (peak in 72000 / 192000), A B C at 0 64 127 (27 settings), MIX 100 %%:\n");
    for (ty = 1; ty < IT_N; ty++) {
        int32_t pk[2] = {0, 0};
        double dc[2] = {0, 0};
        int same0 = 1, lin = 1, diff = 0;
        for (nv = 3; nv <= 8; nv += 5) {
            uint32_t s = nv == 8;
            make_sig(nv);
            for (ia = 0; ia < 3u; ia++)
                for (ib = 0; ib < 3u; ib++)
                    for (ic = 0; ic < 3u; ic++) {
                        double sy = 0, sx = 0;
                        /* (CRUSH: against the input held at its RATE, as it samples it) */
                        uint32_t hold = ty == IT_CRUSH ? INS_HOLD[COR[ib] >> 3] : 1u;
                        run_ins((int32_t)ty, COR[ia], COR[ib], COR[ic], 127, sig_a);
                        for (i = 0; i < NS; i++) {
                            int32_t m = sig_a[i] < 0 ? -sig_a[i] : sig_a[i];
                            pk[s] = m > pk[s] ? m : pk[s];
                            diff |= sig_a[i] != sig_in[i];
                            if (i >= NS - FS) {
                                sy += sig_a[i];
                                sx += sig_in[i / hold * hold];
                            }
                        }
                        sy = fabs((sy - sx) / FS);
                        if (ty != IT_CRUSH || ia || ib)     /* (1 bit at 689 Hz: its coarse errors do not average
                                                             * out in 1 s; the odd check below) */
                            dc[s] = sy > dc[s] ? sy : dc[s];
                        if (ia == 1 && ic != 1) {           /* MIX 50 % halfway, MIX 0 the input */
                            run_ins((int32_t)ty, COR[ia], COR[ib], COR[ic], 64, sig_b);
                            for (i = SL_RAMP; i < NS; i++) {
                                int32_t want = sig_in[i] + mulq16(sig_a[i] - sig_in[i], 64u * 258u * 2u);
                                lin &= abs(sig_b[i] - want) <= 1;
                            }
                            run_ins((int32_t)ty, COR[ia], COR[ib], COR[ic], 0, sig_b);
                            same0 &= !memcmp(sig_b, sig_in, sizeof sig_in) && !ins[0].w;
                        }
                    }
        }
        printf("       %-5s peak 3 saws %6d, 8 saws %6d; worst DC (mean against the input's) %5.1f / %5.1f\n",
               N_ITYPE[ty], pk[0], pk[1], dc[0], dc[1]);
        {
            char w[112];
            snprintf(w, sizeof w, "%s: no DC (< 0.5 %% of a voice's full scale), bounded (< 1.5 x the input, LEVEL +7.5 dB)",
                     N_ITYPE[ty]);
            check(w, dc[0] < 164.0 && dc[1] < 164.0 && pk[0] < 120000 && pk[1] < 288000);
            snprintf(w, sizeof w, "%s: MIX 0 the input bit for bit, MIX 50 %% halfway to MIX 100 %% (the wet), it acts",
                     N_ITYPE[ty]);
            check(w, same0 && lin && diff);
        }
    }
    {   /* CRUSH's steps are odd: -x gives minus what x gives (but at an exact half step, which rounds up), every BITS */
        int ok = 1;
        int32_t v, a;
        for (a = 0; a < 128; a += 8)
            for (v = -140000; v <= 140000; v += 7) {
                uint32_t sh = ins_bits_sh(a);
                int32_t half = sh && !((v + (1 << (sh - 1u))) & ((1 << sh) - 1));
                ok &= half || ins_shape(IT_CRUSH, -v, sh) == -ins_shape(IT_CRUSH, v, sh);
            }
        check("CRUSH: its steps are odd (no offset) at every BITS", ok);
    }
    {   /* what each TYPE is: HARD flat at its clip, FOLD folds back, CRUSH holds its RATE and keeps BITS levels */
        int32_t pk = 0, lv = 0;
        uint32_t n = 0, runs_ok = 1, run = 1;
        make_sig(3);
        run_ins(IT_HARD, 127, 127, 112, 127, sig_a);   /* (TONE open, LEVEL 0 dB) */
        for (i = FS; i < NS; i++)
            pk = abs(sig_a[i]) > pk ? abs(sig_a[i]) : pk;
        if (pk <= 22000 || pk >= 36000)
            printf("     HARD: peak %d\n", pk);
        check("HARD at full DRIVE (12.8x), LEVEL 0 dB: held at its clip (24000; under 1.5x it with the DC blocker's "
              "tilt and TONE)", pk > 22000 && pk < 36000);
        run_ins(IT_CRUSH, 0, 0, 127, 127, sig_a);       /* 1 bit, 689 Hz (a hold of 64), LPF open */
        memset(ins, 0, sizeof ins);
        set_ins(&trk[0], IT_CRUSH, 0, 0, 127, 127);
        for (i = 0; i < NS; i++)
            sig_b[i] = ins_shape(IT_CRUSH, clamp(sig_in[i / 64u * 64u], -262144, 262143), ins_bits_sh(0));
        for (i = 0; i < NS; i++)
            lv |= 1 << (((sig_b[i] >> 15) + 8) & 31);
        for (i = 1; i < NS; i++)
            if (sig_b[i] != sig_b[i - 1]) {
                runs_ok &= run % 64u == 0u;
                run = 1;
                n++;
            } else
                run++;
        check("CRUSH BITS 1, RATE 689 Hz: whole steps of 32768, each held a multiple of 64 samples",
              runs_ok && n > 10u && __builtin_popcount((unsigned)lv) <= 6);
    }
}

/* ------------------------------------------------------------------ 4 --- */
static void test_glide(void)
{
    track_t *t = &trk[0];
    int32_t w1, w4, wm;
    uint32_t i;
    int ok;
    host_init();
    make_sig(3);
    memset(ins, 0, sizeof ins);
    set_ins(t, IT_SOFT, 64, 96, 96, 127);
    memcpy(sig_a, sig_in, sizeof sig_in);
    track_insert(t, sig_a, CTL);
    w1 = ins[0].w;
    for (i = 1; i < 4u; i++)
        track_insert(t, sig_a + i * CTL, CTL);
    w4 = ins[0].w;
    check("MIX 0 -> 100 %: the wet share glides in over 2.9 ms (128 samples)", w1 == CTL * SL_SLOPE && w4 == 32768);
    t->p[P_IMIX] = 0;
    track_insert(t, sig_a + 4u * CTL, CTL);
    wm = ins[0].w;
    for (i = 5; i < 9u; i++)
        track_insert(t, sig_a + i * CTL, CTL);
    check("  and out again; then nothing runs (the input as it is)", wm == 32768 - CTL * SL_SLOPE && !ins[0].w &&
          !memcmp(sig_a + 8u * CTL, sig_in + 8u * CTL, CTL * sizeof(int32_t)));
    t->p[P_IMIX] = 127;                              /* a TYPE change while it plays: SOFT out, FLANG in from rest */
    for (i = 9; i < 16u; i++)
        track_insert(t, sig_a + i * CTL, CTL);
    t->p[P_ITYPE] = IT_FLANGER;
    ok = ins[0].type == IT_SOFT;
    for (i = 16; i < 20u; i++) {
        track_insert(t, sig_a + i * CTL, CTL);
        ok &= i < 19u ? ins[0].type == IT_SOFT && ins[0].w > 0 : 1;
    }
    track_insert(t, sig_a + 20u * CTL, CTL);
    ok &= ins[0].type == IT_FLANGER && ins[0].w == CTL * SL_SLOPE;
    for (i = 21; i < 24u; i++)
        track_insert(t, sig_a + i * CTL, CTL);
    {   /* no click: the largest step of the output over the change against the input's own */
        int32_t jmp = 0, jin = 0;
        for (i = 16u * CTL + 1u; i < 24u * CTL; i++) {
            int32_t a = abs(sig_a[i] - sig_a[i - 1]), b = abs(sig_in[i] - sig_in[i - 1]);
            jmp = a > jmp ? a : jmp;
            jin = b > jin ? b : jin;
        }
        check("a TYPE change: the old one fades out, the new one starts from rest and fades in, no jump past the saws'",
              ok && jmp <= jin + jin / 4);
    }
    {   /* TYPE OFF while the track is silent (its tail run out) leaves no INSERT state for the next hit */
        int stale;
        host_init();
        old_set_model(t, DM_K909);
        set_ins(t, IT_FLANGER, 64, 64, 64, 127);
        drum_hit(t, 127);
        render_mix(0, 0, FS / 4u / CTL * CTL);
        render_mix(0, 0, 4u * FS / CTL * CTL);       /* silent, the tail run out */
        stale = ins[0].w != 0;
        t->p[P_ITYPE] = IT_OFF;
        render_mix(0, 0, CTL);
        check("TYPE OFF while silent: the INSERT at rest at once (no stale fade on the next hit)", stale && !ins[0].w);
    }
    {   /* the INSERT's tail: a FLANG with feedback rings on after the hit (as DIST's tail), then the track idles */
        uint32_t blk, after = 0, rang = 0;
        int32_t l[CTL];
        host_init();
        old_set_model(t, DM_RIM);
        set_ins(t, IT_FLANGER, 64, 64, 127, 127);
        drum_hit(t, 127);
        for (blk = 0; blk < FS / CTL; blk++) {
            render_mix(l, 0, CTL);
            if (!t->v[0].active && !t->dtail) {
                after++;
                for (i = 0; i < CTL; i++)
                    rang |= after > 1u && l[i] != 0;
            }
        }
        check("FLANG's tail runs on after the voice (the feedback's ring), then the track goes idle",
              rang && !t->tail);
    }
}

/* ------------------------------------------------------------------ 5 --- */
#define STEP16 (22050u / 4u)                         /* a 1/16 at 120 BPM: 5512 samples */
static void play(void) { transport_req = 1; render_mix(0, 0, CTL); }
static void stop(void) { transport_req = 2; render_mix(0, 0, CTL); }
static void test_motion(void)
{
    track_t *t = &trk[0];
    uint32_t k, idx;
    int ok_l = 1, seen_l = 0, ok_a = 1, seen_w = 0;
    check("MOTION records SRR, the INSERT's A B C and MIX, not its TYPE (as FILTER's TYPE and RESON's MODEL)",
          motion_param(P_SRR) && motion_param(P_IA) && motion_param(P_IB) && motion_param(P_IC) &&
              motion_param(P_IMIX) && !motion_param(P_ITYPE));
    check("MIDI LEARN takes SRR and every INSERT knob", ml_param_ok(P_SRR) && ml_param_ok(P_ITYPE) &&
          ml_param_ok(P_IA) && ml_param_ok(P_IC) && ml_param_ok(P_IMIX));
    host_init();
    song.g[G_BPM] = 120;
    for (k = 0; k < 16u; k++)
        t->step[k].on = 1;
    set_ins(t, IT_FOLD, 90, 96, 96, 0);
    lock_set(0, 4, P_SRR, 40);                      /* SRR on step 5 only */
    motion_add(0, 0, P_IMIX, 0);                    /* MIX 0 on step 1, 100 % from step 9 */
    motion_add(0, 8, P_IMIX, 127);
    motion_set_play(0, 1);
    play();
    for (k = 0; k < 16u * STEP16 / CTL; k++) {
        render_mix(0, 0, CTL);
        idx = t->seq_idx;
        ok_l &= idx == 4u ? t->p[P_SRR] == 40 : t->p[P_SRR] == 0;
        seen_l |= idx == 4u && t->srr_a != 0;
        if (idx < 8u)
            ok_a &= t->p[P_IMIX] == 0 && !ins[0].w;
        else if (t->seq_pos > 4u * CTL)
            seen_w |= ins[0].w == 32768;
    }
    stop();
    check("SRR as a step's lock: on step 5 only (it runs there), 0 before and after", ok_l && seen_l);
    check("MIX as motion: 0 on steps 1..8 (nothing runs), 100 % from step 9, the wet share there", ok_a && seen_w);
}

/* ------------------------------------------------------------------ 6 --- */
static int same(const char *a, const char *b) { return !strcmp(a, b); }
static void test_names(void)
{
    track_t *t = &trk[0];
    char v[16];
    const char *u = 0;
    int ok;
    host_init();
    ok = same(track_desc(t, P_IA)->label, "A");     /* (OFF: TP's own) */
    t->p[P_ITYPE] = IT_FUZZ;
    ok &= same(track_desc(t, P_IA)->label, "DRIVE") && same(track_desc(t, P_IB)->label, "TONE") &&
          same(track_desc(t, P_IC)->label, "LEVEL");
    t->p[P_ITYPE] = IT_CRUSH;
    ok &= same(track_desc(t, P_IA)->label, "BITS") && same(track_desc(t, P_IB)->label, "RATE") &&
          same(track_desc(t, P_IC)->label, "LPF");
    t->p[P_ITYPE] = IT_CHORUS;
    ok &= same(track_desc(t, P_IA)->label, "RATE") && same(track_desc(t, P_IB)->label, "DEPTH") &&
          same(track_desc(t, P_IC)->label, "FDBK");
    check("the INSERT's A B C named by its TYPE (DRIVE TONE LEVEL, BITS RATE LPF, RATE DEPTH FDBK)", ok);
    param_format(&TP[P_SRR], 0, v, &u);
    ok = same(v, "OFF");
    param_format(&TP[P_SRR], 16, v, &u);
    ok &= same(v, "14.7") && same(u, "kHz");
    param_format(&TP[P_SRR], 127, v, &u);
    ok &= same(v, "347") && same(u, "Hz");
    param_format(&TP[P_ITYPE], IT_PHASER, v, &u);
    check("SRR shows its rate (OFF, 14.7 kHz, 347 Hz); TYPE its name (PHASR)", ok && same(v, "PHASR"));
}

/* the cost per sample of one track's SRR and INSERT (host instructions; the target's: tests/target_budget.py) */
static void test_cost(void)
{
    track_t *t = &trk[0];
    uint32_t i, ty, reps = 200;
    uint64_t c0, c;
    host_init();
    make_sig(3);
    t->p[P_SRR] = 40;
    c0 = instr_now();
    for (i = 0; i < reps; i++)
        track_srr(t, sig_a + (i % 64u) * CTL, CTL, 1);
    c = instr_now() - c0;
    printf("     cost: SRR %.1f host instructions a sample\n", (double)c / (reps * CTL));
    for (ty = 1; ty < IT_N; ty++) {
        memset(ins, 0, sizeof ins);
        set_ins(t, (int32_t)ty, 64, 96, 96, 100);
        memcpy(sig_a, sig_in, sizeof sig_in);
        for (i = 0; i < 8u; i++)
            track_insert(t, sig_a + i * CTL, CTL);  /* (the glide done) */
        c0 = instr_now();
        for (i = 0; i < reps; i++)
            track_insert(t, sig_a + (8u + i % 64u) * CTL, CTL);
        c = instr_now() - c0;
        printf("     cost: INSERT %-5s %.1f host instructions a sample\n", N_ITYPE[ty], (double)c / (reps * CTL));
    }
    check("cost measured (printed above)", 1);
}

int main(void)
{
    test_identity();
    test_srr();
    test_types();
    test_glide();
    test_motion();
    test_names();
    test_cost();
    printf(fails ? "insert_test: %d FAILED\n" : "insert_test: all passed\n", fails);
    return fails ? 1 : 0;
}
