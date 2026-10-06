/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web simulator's core, headless (tools/build_sim.sh): the same C the browser runs as wasm, through its API. */
#include "sim_core.c"
#include <stdio.h>

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

int main(void)
{
    test_boot_and_input();
    printf(fails ? "sim_test: %d FAILED\n" : "sim_test: all ok\n", fails);
    return fails != 0;
}
