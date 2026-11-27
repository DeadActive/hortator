/* SPDX-License-Identifier: GPL-3.0-only */
/* A3 natural hit variation on the real sequencer: velocity of the sounding voices. */
#define GRIND_NATIVE 1
#undef NDEBUG
#include <assert.h>
#include "../drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

/* play a track full of steps, return the XOR-folded hash of every voice velocity seen at each block, and the spread */
static uint32_t run(uint32_t vary, uint32_t acc, uint32_t rat, uint32_t *lo, uint32_t *hi)
{
    uint32_t b, i, h = 2166136261u;
    host_init();
    song.g[G_BPM] = 240;
    gm_runtime_reset();
    for (i = 0; i < 16u; i++) {
        trk[0].step[i].on = 1;
        trk[0].step[i].acc = (uint8_t)acc;
        trk[0].step[i].rat = (uint8_t)rat;
    }
    trk[0].p[P_SLEN] = 16;
    gm_vary = (uint8_t)vary;
    transport_req = 1;
    *lo = 127;
    *hi = 0;
    for (b = 0; b < 700u; b++) {
        render_mix(0, 0, CTL);
        for (i = 0; i < NDV; i++)
            if (trk[0].v[i].active && trk[0].v[i].age > 0) {
                uint32_t v = trk[0].v[i].vel;
                h = (h ^ v) * 16777619u;
                if (v < *lo)
                    *lo = v;
                if (v > *hi)
                    *hi = v;
            }
    }
    return h;
}

int main(void)
{
    uint32_t lo, hi, a, b, c, lo2, hi2;
    a = run(0, 0, 0, &lo, &hi);
    check("vary 0: every hit at the base velocity", lo == 96u && hi == 96u);
    b = run(100, 0, 0, &lo, &hi);
    check("vary 100: the hits differ, within +-8", lo < hi && lo >= 88u && hi <= 104u);
    c = run(100, 0, 0, &lo2, &hi2);
    check("the same performance after every PLAY", b == c && lo == lo2 && hi == hi2);
    check("vary changes the sound", a != b);
    run(100, 1, 0, &lo, &hi);
    check("accents stay accents (<=127, near the top)", hi <= 127u && lo >= 119u);
    run(100, 0, 3, &lo, &hi);
    check("ratchet hits vary too", lo < hi);
    run(0, 0, 3, &lo, &hi);
    check("ratchet at vary 0 stays flat", lo == hi);
    host_init();
    song.g[G_BPM] = 240;
    gm_runtime_reset();
    {
        uint32_t i, b, l = 127, h = 0;
        drum_set_model(&trk[0], DM_ACID);              /* a gated model: not varied, its rolls neither */
        for (i = 0; i < 16u; i++) {
            trk[0].step[i].on = 1;
            trk[0].step[i].rat = 3;
        }
        trk[0].p[P_SLEN] = 16;
        gm_vary = 100;
        transport_req = 1;
        for (b = 0; b < 400u; b++) {
            render_mix(0, 0, CTL);
            for (i = 0; i < NDV; i++)
                if (trk[0].v[i].active) {
                    uint32_t v = trk[0].v[i].vel;
                    l = v < l ? v : l;
                    h = v > h ? v : h;
                }
        }
        check("a gated track (ACID) and its ratchets are not varied", l == h);
    }
    printf(fails ? "test_vary: %d FAILED\n" : "test_vary: all passed\n", fails);
    return fails != 0;
}
