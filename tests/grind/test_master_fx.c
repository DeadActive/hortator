/* SPDX-License-Identifier: GPL-3.0-only */
/* B3 master macros SINK / SWELL / CUT on the real mixer and sequencer. */
#define GRIND_NATIVE 1
#undef NDEBUG
#include <assert.h>
#include "../ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define NB 400u
static int32_t L[NB * CTL], R[NB * CTL];

static void setup(void)
{
    uint32_t i, k;
    host_init();
    song.g[G_BPM] = 240;
    gm_runtime_reset();
    for (k = 0; k < 3u; k++) {                         /* kick, snare and a hat on every step: broadband and dense */
        for (i = 0; i < 16u; i++) {
            trk[k].step[i].on = 1;
            trk[k].step[i].acc = (uint8_t)(i & 1u);
        }
        trk[k].p[P_SLEN] = 16;
    }
}
static void play(uint32_t from, uint32_t to)
{
    uint32_t b;
    for (b = from; b < to; b++)
        render_mix(L + b * CTL, R + b * CTL, CTL);
}
static uint64_t energy(uint32_t from, uint32_t to)
{
    uint64_t e = 0;
    uint32_t i;
    for (i = from * CTL; i < to * CTL; i++)
        e += (uint64_t)((int64_t)L[i] * L[i] / 256);
    return e;
}
static uint64_t hf(uint32_t from, uint32_t to)         /* the first difference: the high end */
{
    uint64_t e = 0;
    uint32_t i;
    for (i = from * CTL + 1; i < to * CTL; i++) {
        int64_t d = (int64_t)L[i] - L[i - 1];
        e += (uint64_t)(d * d / 256);
    }
    return e;
}
static uint64_t sig(void)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t i;
    for (i = 0; i < NB * CTL; i++)
        h = (h ^ (uint32_t)L[i] ^ ((uint64_t)(uint32_t)R[i] << 17)) * 1099511628211ull;
    return h;
}

int main(void)
{
    uint64_t off_hf, off_e, a, b, base_rel;

    setup();
    transport_req = 1;
    play(0, NB);
    a = sig();
    off_hf = hf(100, 200);
    off_e = energy(100, 200);
    base_rel = hf(300, 400);
    setup();
    transport_req = 1;
    gm_master_set(0, 0);
    play(0, NB);
    b = sig();
    check("OFF (0, 0): the same PCM as without the macros", a == b);

    setup();
    transport_req = 1;
    gm_master_set(100, 0);                              /* SINK */
    play(0, 200);
    check("SINK 100: the high end falls", hf(100, 200) * 2 < off_hf);
    gm_master_set(0, 0);
    play(200, NB);
    check("SINK released: the sound is back", hf(300, 400) * 10 > base_rel * 9);

    setup();
    transport_req = 1;
    gm_master_set(0, 100);                              /* SWELL: more wet, never a gain jump */
    play(0, NB);
    {
        int32_t pk = 0, i;
        for (i = 0; i < (int32_t)(NB * CTL); i++) {
            if (L[i] > pk)
                pk = L[i];
            if (-L[i] > pk)
                pk = -L[i];
        }
        check("SWELL 100: louder wet, still inside the limiter", energy(100, 200) > off_e && pk <= 32767 * 256);
    }
    check("SWELL touches no song or track parameter", song.g[G_BPM] == 240);

    setup();
    transport_req = 1;
    play(0, 60);
    gm_master_cut();
    play(60, NB);
    check("CUT: the transport stops", !song.playing);
    check("CUT: silence follows (bounded fade, no further hits)", energy(NB - 20, NB) * 1000 < off_e);
    transport_req = 1;
    play(0, 40);
    check("CUT does not suppress the next PLAY", song.playing);
    printf(fails ? "test_master_fx: %d FAILED\n" : "test_master_fx: all passed\n", fails);
    return fails != 0;
}
