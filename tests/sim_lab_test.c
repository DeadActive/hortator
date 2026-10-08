/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The landing page's lab (tests/sim_lab.c), headless: what each feature piece computes, on the firmware's own DSP. */
#include "sim_core.c"
#include <stdio.h>
#include <math.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static float rms(const float *a, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)a[i] * a[i];
    return n ? (float)sqrt(s / n) : 0.0f;
}

static uint32_t tail_len(const float *a, uint32_t n)   /* the last sample above -60 dB of the peak */
{
    float pk = 0;
    uint32_t i, last = 0;
    for (i = 0; i < n; i++)
        pk = fabsf(a[i]) > pk ? fabsf(a[i]) : pk;
    for (i = 0; i < n; i++)
        if (fabsf(a[i]) > pk * 0.001f)
            last = i;
    return last;
}

static float buf_a[LAB_MAX_FRAMES], buf_b[LAB_MAX_FRAMES];

static void test_voices(void)
{
    uint32_t m, silent = 0, n = FS / 2u;
    for (m = 0; m < NMODELS; m++) {
        lab_reset();
        lab_model(0, m);
        lab_voice(n, 0);
        if (rms(lab_buf(), n) < 1e-4f) {
            printf("      silent: %s\n", N_MODEL[m]);
            silent++;
        }
    }
    check("voices: every model's hit is heard (sample slots may be empty)", silent <= 1u);
    lab_reset();
    lab_model(0, DM_K808);
    lab_voice(n, 0);
    memcpy(buf_a, lab_buf(), n * sizeof(float));
    lab_model(0, DM_K909);
    lab_voice(n, 0);
    check("voices: the 909 kick is not the 808 kick", memcmp(buf_a, lab_buf(), n * sizeof(float)) != 0);
    lab_param(0, P_E1, (int16_t)track_desc(&trk[0], P_E1)->max);
    lab_voice(n, 0);
    memcpy(buf_b, lab_buf(), n * sizeof(float));
    lab_param(0, P_E1, (int16_t)track_desc(&trk[0], P_E1)->min);
    lab_voice(n, 0);
    check("voices: DECAY at max rings longer than at min", tail_len(buf_b, n) > tail_len(lab_buf(), n));
}

static void test_reson(void)
{
    uint32_t n = FS;
    lab_reset();
    lab_model(0, DM_RIM);
    lab_voice(n, 1);
    memcpy(buf_a, lab_buf(), n * sizeof(float));
    lab_param(0, P_RMODEL, RS_STRNG);
    lab_param(0, P_RDECAY, 110);
    lab_param(0, P_RMIX, 100);
    lab_voice(n, 1);
    check("reson: STRNG rings longer than the dry hit", tail_len(lab_buf(), n) > tail_len(buf_a, n) + FS / 10u);
}

static void test_grids(void)
{
    uint8_t a[96], b[96];
    lab_reset();
    lab_global(G_GX, 0);
    lab_global(G_GY, 0);
    memcpy(a, lab_grids(), sizeof a);
    lab_global(G_GX, 127);
    lab_global(G_GY, 127);
    memcpy(b, lab_grids(), sizeof b);
    check("grids: the lanes differ between two corners of the map", memcmp(a, b, sizeof a) != 0);
}

static void loop_kit(void)                              /* kick 4 on the floor, hats on 8ths */
{
    uint32_t s;
    lab_reset();
    lab_model(0, DM_K909);
    lab_model(1, DM_HATO);
    for (s = 0; s < 16u; s++) {
        lab_step(0, s, s % 4u == 0u, 0, 0, 0);
        lab_step(1, s, s % 2u == 0u, 0, 0, 0);
    }
    lab_param(1, P_E1, 110);
}

static void run_bars(uint32_t bars)
{
    uint32_t f = bars * 16u * (FS * 60u / 120u / 4u);
    while (f) {
        uint32_t n = f < SIM_MAX_FRAMES ? f : SIM_MAX_FRAMES;
        sim_render(n);
        f -= n;
    }
}

static void test_comp(void)
{
    uint32_t i, k = 0;
    int32_t least = 0;
    loop_kit();
    lab_global(G_CSRC, 1);
    lab_global(G_CTHR, 20);
    lab_global(G_CRAT, 80);
    lab_param(1, P_DUCK, 1);
    lab_play(1);
    lab_probe_clear();
    run_bars(1);
    for (i = 0; i < lab_probe_count(); i++, k++)
        least = lab_probes()[i].gr < least ? lab_probes()[i].gr : least;
    check("comp: keyed by the kick, the probes show gain reduction", k > 0u && least < 0);
}

static void test_lfo(void)
{
    uint32_t i;
    int32_t lo = 0, hi = 0;
    loop_kit();
    lab_param(1, P_LFO1 + LF_DEST, 3);
    lab_param(1, P_LFO1 + LF_DEPTH, 60);
    lab_param(1, P_LFO1 + LF_WAVE, LW_TRI);
    lab_play(1);
    lab_probe_clear();
    run_bars(1);
    for (i = 0; i < lab_probe_count(); i++) {
        int32_t v = lab_probes()[i].lfo[1][0];
        lo = v < lo ? v : lo;
        hi = v > hi ? v : hi;
    }
    check("lfo: track 2's LFO 1 moves over a bar", hi - lo > 8000);
}

static void test_dice(void)
{
    uint32_t s, bar, some = 0, all = 0, hats[8] = {0};
    loop_kit();
    for (s = 0; s < 16u; s += 2u)
        lab_step(1, s, 1, 0, (uint8_t)cond_store(10), 0);   /* 50 % */
    lab_play(1);
    for (bar = 0; bar < 8u; bar++) {
        uint32_t i;
        lab_probe_clear();
        run_bars(1);
        for (i = 0; i < lab_probe_count(); i++)
            hats[bar] += (lab_probes()[i].fired >> 1) & 1u;
    }
    for (bar = 0; bar < 8u; bar++) {
        some += hats[bar] > 0u;
        all += hats[bar] == 8u;
    }
    check("dice: 50 % hats fire, but not every one every bar", some > 0u && all < 8u);
}

static void test_perform(void)
{
    loop_kit();
    lab_play(1);
    run_bars(1);
    lab_render_mono(FS / 2u);
    memcpy(buf_a, lab_buf(), (FS / 2u) * sizeof(float));
    loop_kit();
    lab_play(1);
    run_bars(1);
    lab_perf(PF_REV, 1);
    lab_render_mono(FS / 2u);
    lab_perf(PF_REV, 0);
    check("perform: REVERSE held changes the output", memcmp(buf_a, lab_buf(), (FS / 2u) * sizeof(float)) != 0);
}

static uint32_t step_now(void) { return lab_probes()[lab_probe_count() - 1u].idx[1]; }

static void test_mute_bar(void)
{
    uint32_t i, early = 0, late = 0, waited = 0;
    loop_kit();
    lab_play(1);
    run_bars(1);
    while (step_now() != 5u) {                        /* mid-bar */
        lab_probe_clear();
        sim_render(256);
    }
    lab_mute(1, 1);                                   /* hats: mute on the next bar */
    lab_probe_clear();
    run_bars(1);
    for (i = 0; i < lab_probe_count(); i++) {
        const lab_probe_t *p = &lab_probes()[i];
        waited |= p->mute_q & 2u;
        if (!(p->muted & 2u))
            early += (p->fired >> 1) & 1u;
        else
            late += (p->fired >> 1) & 1u;
    }
    check("mute at the next bar: queued, the hats play on to the bar line, then not", waited && early > 0u && late == 0u &&
          (lab_probes()[lab_probe_count() - 1u].muted & 2u));
}

static void test_motion(void)
{
    uint32_t i, at4 = 0, off = 0;
    loop_kit();
    lab_watch(1, P_E2);
    lab_motion(1, 1);                                 /* hats armed */
    lab_play(1);
    run_bars(1);
    while (step_now() != 4u) {
        lab_probe_clear();
        sim_render(256);
    }
    lab_knob(1, P_E2, 7);                             /* TONE turned at step 4: recorded */
    lab_motion(1, 0);
    lab_knob(1, P_E2, 100);                           /* not recording: the new base */
    run_bars(1);
    lab_probe_clear();
    run_bars(1);
    for (i = 0; i < lab_probe_count(); i++) {
        const lab_probe_t *p = &lab_probes()[i];
        if (p->idx[1] == 4u && p->watch == 7)
            at4++;
        if (p->idx[1] == 1u && p->watch == 100)
            off++;
    }
    check("motion: the knob recorded at step 4 plays back there; the base elsewhere", at4 > 0u && off > 0u);
}

int main(void)
{
    test_voices();
    test_reson();
    test_grids();
    test_comp();
    test_lfo();
    test_dice();
    test_perform();
    test_mute_bar();
    test_motion();
    printf(fails ? "sim_lab_test: %d FAILED\n" : "sim_lab_test: all ok\n", fails);
    return fails != 0;
}
