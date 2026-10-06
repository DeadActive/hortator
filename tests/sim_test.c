/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web simulator's core, headless (tools/build_sim.sh): the same C the browser runs as wasm, through its API. */
#include "sim_core.c"
#include <stdio.h>
#include <math.h>
#include <time.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t fb_lit(void)                         /* pixels that are not black */
{
    const uint16_t *f = sim_fb();
    uint32_t i, n = 0;
    for (i = 0; i < 240u * 240u; i++)
        n += f[i] != 0;
    return n;
}

static uint32_t fb_hash(void)                        /* FNV-1a over the framebuffer */
{
    const uint8_t *p = (const uint8_t *)sim_fb();
    uint32_t i, h = 2166136261u;
    for (i = 0; i < 240u * 240u * 2u; i++)
        h = (h ^ p[i]) * 16777619u;
    return h;
}

static void run(uint32_t frames)                     /* frames of audio (the UI frames due run inside) */
{
    while (frames) {
        uint32_t n = frames < SIM_MAX_FRAMES ? frames : SIM_MAX_FRAMES;
        sim_render(n);
        frames -= n;
    }
}

static void test_boot_and_input(void)
{
    uint32_t h;
    sim_init(1);
    run(4096);
    check("boot: the screen draws", fb_lit() > 500u);
    h = fb_hash();
    sim_btn(B_EDIT, 1);
    run(1024);
    sim_btn(B_EDIT, 0);
    run(1024);
    check("a button (EDIT) changes the screen", fb_hash() != h);
    h = fb_hash();
    sim_enc(EN_K1, 3);
    run(2048);
    check("an encoder turn (KNOB 1) changes the screen", fb_hash() != h);
    h = fb_hash();
    sim_key(2, 1);                                   /* the second white key: track 2 */
    run(2048);
    sim_key(2, 0);
    run(2048);
    check("a note key (track 2's white key) selects track 2 and changes the screen", song.sel == 1u && fb_hash() != h);
}

static float peak(uint32_t frames)                  /* the loudest sample over the next frames */
{
    float m = 0;
    while (frames) {
        uint32_t n = frames < SIM_MAX_FRAMES ? frames : SIM_MAX_FRAMES, i;
        const float *a;
        sim_render(n);
        a = sim_audio();
        for (i = 0; i < 2u * n; i++)
            m = fabsf(a[i]) > m ? fabsf(a[i]) : m;
        frames -= n;
    }
    return m;
}

static void tap(uint32_t label)
{
    sim_btn(label, 1);
    run(1024);
    sim_btn(label, 0);
    run(1024);
}

static void four_on_the_floor(void)                  /* the current project's track 1 (until the demos) */
{
    uint32_t k;
    for (k = 0; k < 16u; k += 4u)
        trk[0].step[k].on = 1;
}

static void test_audio(void)
{
    float loud, quiet;
    uint32_t on = 0, off = 0, i;
    sim_init(1);
    four_on_the_floor();
    run(4096);
    check("before PLAY: silent", peak(FS / 2u) < 4.0f / 32768.0f);
    tap(B_PLAY);
    check("PLAY: the transport runs", sim_playing());
    loud = peak(FS);
    check("PLAY: audio within 1 s", loud > 0.01f);
    for (i = 0; i < 40u; i++) {                      /* PLAY's LED blinks while playing */
        run(FS / 40u / CTL * CTL);
        if ((sim_leds() >> B_PLAY) & 1u)
            on++;
        else
            off++;
    }
    check("PLAY's LED blinks while playing", on && off);
    sim_master(0);
    run(FS / 2u);
    quiet = peak(FS);
    check("MASTER at 0: much quieter", quiet < loud * 0.1f);
    sim_master(800);
    tap(B_PLAY);
    check("PLAY again: stopped", !sim_playing());
    run(FS * 3u);
    check("stopped: silent once the tails decay", peak(FS / 2u) < 4.0f / 32768.0f);
}

static void test_leds(void)
{
    sim_init(1);
    run(4096);
    sim_key(5, 1);
    run(2048);
    check("a held note key lights its LED", (sim_leds() >> (14u + 5u)) & 1u);
    sim_key(5, 0);
    run(2048);
    check("released: its LED goes off", !((sim_leds() >> (14u + 5u)) & 1u));
}

static void test_frame_cost(void)                    /* informational: a UI frame runs in the audio thread */
{
    clock_t t0;
    uint32_t i;
    sim_init(1);
    tap(B_EDIT);
    t0 = clock();
    for (i = 0; i < 200u; i++) {
        ui.force = 1;
        sim_frame();
    }
    printf("info  UI frame (full redraw): %.0f us\n", (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / 200.0);
}

int main(void)
{
    test_boot_and_input();
    test_audio();
    test_leds();
    test_frame_cost();
    printf(fails ? "sim_test: %d FAILED\n" : "sim_test: all ok\n", fails);
    return fails != 0;
}
