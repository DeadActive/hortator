/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The reverb's HALL (fx.c rev_hall: REVERB TYPE 3, stored 2), Skiff's, with its tests after Skiff's tests/reverb_test.c:
 *   build/host/reverb_test                  (run_drum_tests.sh; UBSan: an overflow in the loops aborts)
 * 1. TYPE: ROOM, SPRNG, HALL; the stored ids of ROOM (0) and SPRING (1) as before, HALL 2.
 * 2. HALL's decay rises with SIZE (RT60, Schroeder: 0.25 .. 0.9 s at SIZE 0, 1.8 .. 3.5 s at 90, 3.5 .. 8 s at 127).
 * 3. stable at SIZE 127 (DAMP 0 and 127) under 2 s of full-scale noise or square waves: bounded, the last second of
 *    12 near silent; 27 runs (SIZE x DAMP x level) end in exact silence, left and side (no limit cycle).
 * 4. level within 3 dB of ROOM's (the same burst); stereo: the tail's left / right correlation under 0.5.
 * 5. model changes through fx_buses while the bus rings (ROOM / SPRING / HALL both ways): no step larger than the tail's
 *    own, both sides; the new model starts silent; every buffer and state cleared.
 * 6. the master: HALL's left and right differ (its stereo), ROOM's are equal (mono as before); USB 10 CH FX: channels
 *    9 and 10 HALL's left and right.
 * 7. cost: host instructions per sample of rev_hall against rev_room (printed; HALL at most ROOM + 30 %, Skiff's).
 * 8. ROOM after its send stops: its tail ends in exact silence (no offset, no buzz held by its loops' rounding) at
 *    SIZE 127 DAMP 0 and other corners, after a loud noise burst and after a quiet chord. */
#include "drum_host.h"
#include <stdlib.h>
#include <math.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t xs = 0x1234567u;
static int32_t noise(int32_t amp)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return (int32_t)(((int64_t)(int32_t)xs * amp) >> 31);
}

#define IRN (12u * FS / CTL * CTL)
static int32_t ir[IRN], irs[IRN];
enum { M_ROOM, M_SPRING, M_HALL };
static const char *const M_NAME[3] = {"ROOM", "SPRING", "HALL"};

static void model_reset(int m, int32_t size, int32_t damp)
{
    song.g[G_RTYPE] = (int16_t)m;
    song.g[G_RSIZE] = (int16_t)size;
    song.g[G_RDAMP] = (int16_t)damp;
    rev_clear();
    fx.rtype = (uint8_t)m;
    fx.sp_w = 0;
    fx.sp_size = 0;
    fx.sp_ph = 0;
    hl.ph = 0;
    hl.sz = 0;
}
/* one block of model m: mid added to out (the wet bus), its stereo difference into side (HALL's; 0 else) */
static void model_block(int m, const int32_t *in, int32_t *out, int32_t *side, uint32_t n)
{
    uint32_t i;
    hl.side = 0;
    if (m == M_HALL)
        rev_hall(in, out, n);
    else if (m == M_SPRING)
        rev_spring(in, out, n);
    else
        rev_room(in, out, n);
    for (i = 0; i < n; i++)
        side[i] = hl.side ? rev_side[i] : 0;
}
/* model m's response to x[] (len samples, then silence): left (mid + side) into ir, side into irs */
static void model_run(int m, const int32_t *x, uint32_t len, uint32_t n)
{
    static int32_t in[CTL];
    uint32_t t, i;
    for (t = 0; t < n; t += CTL) {
        for (i = 0; i < CTL; i++) {
            in[i] = t + i < len ? x[t + i] : 0;
            ir[t + i] = 0;
        }
        model_block(m, in, ir + t, irs + t, CTL);
        for (i = 0; i < CTL; i++)
            ir[t + i] += irs[t + i];
    }
}
static double rt60(const int32_t *h, uint32_t n)       /* Schroeder: -5 .. -35 dB, x2 */
{
    static double e[IRN];
    double s = 0, t5 = -1, t35 = -1;
    uint32_t i;
    for (i = n; i-- > 0;) {
        s += (double)h[i] * h[i];
        e[i] = s;
    }
    for (i = 0; i < n; i++) {
        double db = 10 * log10(e[i] / e[0] + 1e-30);
        if (t5 < 0 && db <= -5)
            t5 = i;
        if (t35 < 0 && db <= -35) {
            t35 = i;
            break;
        }
    }
    return t35 < 0 ? 99 : (t35 - t5) * 2.0 / FS;
}

static void test_types(void)
{
    check("TYPE: ROOM, SPRNG, HALL (5 characters at most); ROOM and SPRING keep ids 0 and 1, HALL is 2",
          GP[G_RTYPE].max == 2 && !strcmp(N_RTYPE[0], "ROOM") && !strcmp(N_RTYPE[1], "SPRNG") &&
          !strcmp(N_RTYPE[2], "HALL") && strlen(N_RTYPE[2]) <= 5u);
}

static void test_hall_decay(void)
{
    static int32_t burst[64];
    static const int32_t SZ[4] = {0, 64, 90, 127};
    double r[4];
    char what[200];
    uint32_t k, i;
    host_init();
    for (i = 0; i < 64u; i++)                           /* (a click of noise: the send of a drum hit) */
        burst[i] = noise(1 << 17);
    for (k = 0; k < 4u; k++) {
        model_reset(M_HALL, SZ[k], 60);
        model_run(M_HALL, burst, 64, IRN);
        r[k] = rt60(ir, IRN);
    }
    snprintf(what, sizeof what, "HALL decay rises with SIZE: RT60 %.2f s (SIZE 0), %.2f (64), %.2f (90), %.2f (127)",
             r[0], r[1], r[2], r[3]);
    check(what, r[0] < r[1] && r[1] < r[2] && r[2] < r[3] && r[0] >= 0.25 && r[0] <= 0.9 && r[2] >= 1.8 &&
          r[2] <= 3.5 && r[3] >= 3.5 && r[3] <= 8.0);
}

static void test_hall_stable(void)
{
    static int32_t burst[2u * FS];
    char what[200];
    uint32_t k, i;
    host_init();
    for (k = 0; k < 4u; k++) {                          /* the corners: full-scale noise or square waves, then silence */
        int32_t pk = 0, tail = 0, tails = 0;
        for (i = 0; i < 2u * FS; i++)
            burst[i] = k < 2u ? noise(1 << 19) : (i / (k == 2u ? 7u : 53u)) & 1u ? 1 << 19 : -(1 << 19);
        model_reset(M_HALL, 127, k & 1u ? 127 : 0);
        model_run(M_HALL, burst, 2u * FS, IRN);
        for (i = 0; i < IRN; i++) {
            int32_t v = abs(ir[i]);
            pk = v > pk ? v : pk;
            if (i >= IRN - FS) {
                tail = v > tail ? v : tail;
                tails = abs(irs[i]) > tails ? abs(irs[i]) : tails;
            }
        }
        snprintf(what, sizeof what, "HALL at SIZE 127 DAMP %d: 2 s of %s: peak %d, the last second %d (side %d)",
                 k & 1u ? 127 : 0, k < 2u ? "full-scale noise" : k == 2u ? "a full-scale 3.2 kHz square" : "a 416 Hz square",
                 pk, tail, tails);
        check(what, pk <= 12 * 32768 && tail <= 4 && tails <= 4);
    }
    {   /* no limit cycle anywhere: SIZE 0 / 64 / 127 x DAMP 0 / 60 / 127, 1 s of a chord, quiet, middling or loud,
         * then silence: the last second of 12 exactly 0 (left and side) */
        uint32_t s0, d0, lv, worst = 0, held = 0;
        static const int32_t SS[3] = {0, 64, 127}, DD[3] = {0, 60, 127}, LV[3] = {600, 20000, 200000};
        for (s0 = 0; s0 < 3u; s0++)
            for (d0 = 0; d0 < 3u; d0++)
                for (lv = 0; lv < 3u; lv++) {
                    int32_t tail = 0;
                    for (i = 0; i < FS; i++)
                        burst[i] = (int32_t)(LV[lv] * (sin(2 * M_PI * 220 * i / FS) + sin(2 * M_PI * 277 * i / FS) +
                                                       sin(2 * M_PI * 330 * i / FS)) / 3);
                    model_reset(M_HALL, SS[s0], DD[d0]);
                    model_run(M_HALL, burst, FS, IRN);
                    for (i = IRN - FS; i < IRN; i++)
                        tail = abs(ir[i]) + abs(irs[i]) > tail ? abs(ir[i]) + abs(irs[i]) : tail;
                    held += tail != 0;
                    worst = (uint32_t)tail > worst ? (uint32_t)tail : worst;
                }
        snprintf(what, sizeof what, "HALL: silent after its tail at every SIZE x DAMP x level (27 runs): %u not silent, worst %u",
                 held, worst);
        check(what, !held);
    }
}

static void test_hall_level_stereo(void)
{
    static int32_t in[CTL], o[CTL], sd[CTL];
    double er = 0, eh = 0, lr = 0, ll = 0, rr = 0;
    char what[200];
    uint32_t t, i, m;
    host_init();
    for (m = 0; m < 2u; m++) {
        model_reset(m ? M_HALL : M_ROOM, 90, 60);
        xs = 99;
        for (t = 0; t < 2u * FS; t += CTL) {
            for (i = 0; i < CTL; i++) {
                in[i] = t < FS / 4u ? noise(20000) : 0;
                o[i] = 0;
            }
            model_block(m ? M_HALL : M_ROOM, in, o, sd, CTL);
            for (i = 0; i < CTL; i++) {
                double L = o[i] + sd[i], R = o[i] - sd[i];
                *(m ? &eh : &er) += L * L;
                if (m && t >= FS / 2u) {
                    lr += L * R;
                    ll += L * L;
                    rr += R * R;
                }
            }
        }
    }
    snprintf(what, sizeof what, "level: HALL's tail %.1f dB from ROOM's (the same burst, SIZE 90 DAMP 60)", 10 * log10(eh / er));
    check(what, fabs(10 * log10(eh / er)) <= 3.0);
    snprintf(what, sizeof what, "HALL stereo: the tail's left / right correlation %.2f", lr / sqrt(ll * rr));
    check(what, fabs(lr / sqrt(ll * rr)) < 0.5);
}

/* model changes while the bus rings, through fx_buses: the left is wet + dly_side (the master's and USB FX's), the
 * right wet. The old model's tail fades out within a block (no step larger than the tail's own), the new one starts
 * silent; the switch block clears every buffer and state */
static int rev_all_clear(void)
{
    const uint8_t *b = (const uint8_t *)&rev_u;
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof rev_u; i++)
        ok &= b[i] == 0;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        ok &= rev_comb[i] == 0;
    for (i = 0; i < 4u; i++)
        ok &= fx.comb_lp[i] == 0;
    for (i = 0; i < HL_NL; i++)
        ok &= hl.lp[i] == 0 && hl.le[i] == 0;
    return ok && !fx.sp_lp && !fx.sp_hp && !fx.sp_he && !hl.x && !hl.m && !hl.s && !hl.hp && !hl.he;
}
static void test_switch(void)
{
    static const int8_t SEQ[5] = {0, 1, 2, 1, 0}, SEQ2[5] = {0, 2, 1, 2, 0};
    static int32_t c[CTL], d[CTL], r[CTL], w[CTL];
    char what[200];
    uint32_t b, i, k, q;
    for (q = 0; q < 2u; q++)
        for (k = 0; k + 1u < 5u; k++) {
            int from = (q ? SEQ2 : SEQ)[k], to = (q ? SEQ2 : SEQ)[k + 1u];
            int32_t pl = 0, pr = 0, step = 0, own = 0, after = 0, clear = 1;
            if (q && (from != 2 && to != 2))
                continue;
            host_init();
            song.g[G_DMIX] = 0;                         /* (the delay and chorus quiet: only the reverb) */
            song.g[G_RTYPE] = (int16_t)from;
            fx.rtype = (uint8_t)from;
            for (b = 0; b < 4u * FS / CTL; b++) {
                if (b == 2u * FS / CTL)
                    song.g[G_RTYPE] = (int16_t)to;
                for (i = 0; i < CTL; i++) {
                    c[i] = d[i] = 0;
                    r[i] = b < FS / CTL ? (int32_t)(30000 * sin(2 * M_PI * 220 * (b * CTL + i) / FS)) : 0;
                }
                fx_buses(c, d, r, w, CTL);
                if (b == 2u * FS / CTL)
                    clear = rev_all_clear();
                for (i = 0; i < CTL; i++) {
                    int32_t L = w[i] + (dpp.side ? dly_side[i] : 0), R = w[i];
                    int32_t s = abs(L - pl) > abs(R - pr) ? abs(L - pl) : abs(R - pr);
                    if (b >= 2u * FS / CTL - 4u && b <= 2u * FS / CTL + 4u)
                        step = s > step ? s : step;
                    else if (b > FS / CTL + 100u && b < 2u * FS / CTL - 4u)
                        own = s > own ? s : own;
                    if (b > 2u * FS / CTL)
                        after = abs(L) + abs(R) > after ? abs(L) + abs(R) : after;
                    pl = L;
                    pr = R;
                }
            }
            snprintf(what, sizeof what, "%s -> %s while the tail rings: largest step %d (the tail's own %d), silent after: "
                     "%d, cleared: %d", M_NAME[from], M_NAME[to], step, own, after, clear);
            check(what, step <= 2 * own + 64 && after == 0 && fx.rtype == to && clear);
        }
}

/* through the master: a snare into the reverb; HALL's tail differs left and right, ROOM's is the same on both */
static void test_master_stereo(void)
{
    static int32_t ML[2u * FS], MR[2u * FS];
    uint32_t m, k, i;
    for (m = 0; m < 2u; m++) {
        double dd = 0, e = 0;
        host_init();
        for (k = 0; k < NTRK; k++)
            trk[k].p[P_REV] = trk[k].p[P_CHOR] = trk[k].p[P_DLY] = 0;
        trk[1].step[0].on = 1;
        trk[1].p[P_REV] = 127;
        trk[1].p[P_SLEN] = 64;
        song.g[G_RTYPE] = (int16_t)(m ? M_HALL : M_ROOM);
        transport_req = 1;
        render_mix(ML, MR, 2u * FS);
        for (i = FS / 4u; i < 2u * FS; i++) {
            dd += fabs((double)ML[i] - MR[i]);
            e += fabs((double)ML[i]) + fabs((double)MR[i]);
        }
        if (m)
            check("HALL through the master: its left and right differ (stereo), the tail heard", dd > 0.2 * e && e > 0);
        else
            check("ROOM through the master: left = right, as before", dd == 0 && e > 0);
    }
}

/* MENU > USB AUDIO 10 CH FX: channels 9-10 carry HALL's left and right */
static void test_usb_fx(void)
{
    static int32_t UL[2u * FS], UR[2u * FS];
    uint32_t f, i, k;
    double dd = 0, e = 0;
    host_init();
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_REV] = trk[k].p[P_CHOR] = trk[k].p[P_DLY] = 0;
    trk[1].step[0].on = 1;
    trk[1].p[P_REV] = 127;
    trk[1].p[P_SLEN] = 64;
    song.g[G_RTYPE] = M_HALL;
    usb10 = 1;
    usb_fxpair = 1;
    usb8_g = 32768;
    transport_req = 1;
    for (f = 0; f < 2u * FS; f += CTL) {
        const int32_t *p;
        mix_block(mixo, CTL);
        p = usb8_main(mixo, CTL);
        for (i = 0; i < CTL && f + i < 2u * FS; i++) {
            UL[f + i] = p[2u * i];
            UR[f + i] = p[2u * i + 1u];
        }
    }
    usb10 = 0;
    usb_fxpair = 0;
    usb8_g = 0;
    for (i = FS / 4u; i < 2u * FS; i++) {
        dd += fabs((double)UL[i] - UR[i]);
        e += fabs((double)UL[i]) + fabs((double)UR[i]);
    }
    check("HALL on USB 10 CH FX: channels 9 and 10 its left and right (they differ)", dd > 0.2 * e && e > 0);
}

static void test_cost(void)
{
    static int32_t in[CTL], o[CTL], sd[CTL];
    uint64_t c[2];
    uint32_t m, b, i;
    char what[160];
    for (m = 0; m < 2u; m++) {
        uint64_t i0;
        model_reset(m ? M_HALL : M_ROOM, 127, 0);
        i0 = instr_now();
        for (b = 0; b < 4u * FS / CTL; b++) {
            for (i = 0; i < CTL; i++) {
                in[i] = noise(1 << 17);
                o[i] = 0;
            }
            model_block(m ? M_HALL : M_ROOM, in, o, sd, CTL);
        }
        c[m] = instr_now() - i0;
    }
    if (!c[0]) {
        printf("     cost: no instruction counter here\n");
        return;
    }
    snprintf(what, sizeof what, "cost: HALL %.0f host instructions / sample, ROOM %.0f (with the noise send): HALL at most "
             "ROOM + 30 %%", c[1] / (4.0 * FS), c[0] / (4.0 * FS));
    check(what, c[1] <= c[0] * 13u / 10u);
}

/* ROOM: 2 s of a send (loud noise, or a quiet chord near the loops' rounding), then 18 s of silence: the last 2 s
 * exactly 0. (Its loops once rounded down: a DC offset and, at DAMP 0, a buzz held for ever, ~-55 dBFS on USB FX) */
static void test_room_silence(void)
{
    static const int16_t SZ[5] = {127, 127, 90, 64, 0}, DP[5] = {0, 60, 30, 0, 127};
    static int32_t in[CTL], o[CTL];
    char what[200];
    uint32_t k, lv, b, i, held = 0;
    int32_t worst = 0;
    host_init();
    for (k = 0; k < 5u; k++)
        for (lv = 0; lv < 2u; lv++) {
            int32_t tail = 0;
            model_reset(M_ROOM, SZ[k], DP[k]);
            for (b = 0; b < 20u * FS / CTL; b++) {
                for (i = 0; i < CTL; i++) {
                    uint32_t t = b * CTL + i;
                    in[i] = b >= 2u * FS / CTL ? 0 : lv ? noise(1 << 19)
                          : (int32_t)(900 * (sin(2 * M_PI * 220 * t / FS) + sin(2 * M_PI * 277 * t / FS)));
                    o[i] = 0;
                }
                rev_room(in, o, CTL);
                if (b >= 18u * FS / CTL)
                    for (i = 0; i < CTL; i++)
                        tail = abs(o[i]) > tail ? abs(o[i]) : tail;
            }
            held += tail != 0;
            worst = tail > worst ? tail : worst;
        }
    snprintf(what, sizeof what, "ROOM: silent (exactly 0) once its tail has died, 10 runs (SIZE x DAMP x loud / quiet): "
             "%u not silent, worst %d", held, worst);
    check(what, !held);
}

int main(void)
{
    test_room_silence();
    test_types();
    test_hall_decay();
    test_hall_stable();
    test_hall_level_stereo();
    test_switch();
    test_master_stereo();
    test_usb_fx();
    test_cost();
    printf(fails ? "reverb_test: %d FAILED\n" : "reverb_test: all passed\n", fails);
    return fails ? 1 : 0;
}
