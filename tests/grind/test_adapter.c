/* SPDX-License-Identifier: GPL-3.0-only */
/* T09: the Grind core on the real native step ABI (step_t, track_t) and the real sequencer (seq.c), no copy of either. */
#include "../drum_host.h"
#include "../../firmware/src/grind/amalgam.c"
#include "../../firmware/src/grind/seq_adapter.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static void fresh(void)
{
    host_init();
    song.g[G_BPM] = 120;
}

/* every legal flag value, note and micro survives native -> core -> native field by field */
static void cell_roundtrip(void)
{
    uint32_t f, c, ok = 1, n = 0;
    int nt, mi;
    for (f = 0; f < 64u; f++)
        for (c = 0; c <= COND_MAX; c++)
            for (nt = NT_MIN; nt <= NT_MAX; nt += 17)
                for (mi = -MICRO_MAX; mi <= MICRO_MAX; mi += 25) {
                    gm_cell in = {(uint8_t)f, (uint8_t)c, (int8_t)nt, (int8_t)mi}, out;
                    step_t s;
                    memset(&s, 0xA5, sizeof s);          /* (the padding bits are not the cell's) */
                    gm_to_native(&s, in);
                    out = gm_from_native(&s);
                    ok &= out.flags == in.flags && out.cond == in.cond && out.note == in.note && out.micro == in.micro;
                    ok &= s.on == (f & 1u) && s.acc == ((f >> 1) & 1u) && s.slide == ((f >> 2) & 1u) &&
                          s.tie == ((f >> 3) & 1u) && s.rat == ((f >> 4) & 3u);
                    n++;
                }
    check("adapter: every flag, cond, note and micro round-trips through the real step_t", ok && n > 0u);
}

/* musical denominators <-> the stored DIV ids (1/8 = 1, 1/16 = 2, 1/32 = 3); everything else is not a Grind grid */
static void division_map(void)
{
    static const uint8_t MUS[3] = {8, 16, 32};
    uint32_t i, ok = 1;
    uint8_t id, mus;
    for (i = 0; i < 3u; i++) {
        ok &= gm_native_division(MUS[i], &id) == GM_OK && id == i + 1u;
        ok &= gm_musical_division(id, &mus) == GM_OK && mus == MUS[i];
    }
    for (i = 0; i < 256u; i++)
        if (i != 8u && i != 16u && i != 32u)
            ok &= gm_native_division((uint8_t)i, &id) == GM_UNSUPPORTED;
    for (i = 0; i < 16u; i++)
        if (i < 1u || i > 3u)
            ok &= gm_musical_division((uint8_t)i, &mus) == GM_UNSUPPORTED;
    check("adapter: DIV ids 1..3 <-> 1/8, 1/16, 1/32; 1/4, triplets and slow rates are refused", ok);
}

static void native_limits(void)
{
    gm_pattern p;
    uint32_t ok = 1;
    memset(&p, 0, sizeof p);
    for (ok = 0; ok < GM_TRACKS; ok++)
        p.timing[ok] = (gm_timing){16, 16, 0, 0};
    ok = gm_native_check(&p) == GM_OK;
    p.cell[0][3].cond = COND_MAX + 1u;
    ok &= gm_native_check(&p) == GM_INVALID;
    p.cell[0][3].cond = COND_MAX;
    p.cell[0][3].note = NT_MAX + 1;
    ok &= gm_native_check(&p) == GM_INVALID;
    p.cell[0][3].note = NT_MIN - 1;
    ok &= gm_native_check(&p) == GM_INVALID;
    p.cell[0][3].note = 0;
    p.cell[0][3].micro = MICRO_MAX + 1;
    ok &= gm_native_check(&p) == GM_INVALID;
    p.cell[0][3].micro = -MICRO_MAX - 1;
    ok &= gm_native_check(&p) == GM_INVALID;
    p.cell[0][3].micro = -MICRO_MAX;
    p.cell[0][3].flags = 64u;
    ok &= gm_native_check(&p) == GM_INVALID;
    p.cell[0][3].flags = 0;
    p.timing[1] = (gm_timing){16, 16, 101, 0};
    ok &= gm_native_check(&p) == GM_INVALID;
    p.timing[1] = (gm_timing){16, 16, 0, 4};
    ok &= gm_native_check(&p) == GM_INVALID;
    p.timing[1] = (gm_timing){16, 4, 0, 0};
    ok &= gm_native_check(&p) == GM_UNSUPPORTED;
    check("adapter: values the native sequencer cannot play are refused before anything is written", ok);
}

static uint32_t native_signature(void)
{
    uint32_t h = 2166136261u, k, s;
    const uint8_t *b;
    for (k = 0; k < NTRK; k++) {
        b = (const uint8_t *)trk[k].step;
        for (s = 0; s < sizeof trk[k].step; s++)
            h = (h ^ b[s]) * 16777619u;
        h = (h ^ (uint32_t)trk[k].p[P_SLEN]) * 16777619u;
        h = (h ^ (uint32_t)trk[k].p[P_SDIV]) * 16777619u;
        h = (h ^ (uint32_t)trk[k].p[P_SSWING]) * 16777619u;
        h = (h ^ (uint32_t)trk[k].p[P_SRC]) * 16777619u;
    }
    return h;
}

static void publish_export(void)
{
    gm_config c;
    gm_pattern p, q;
    uint32_t ok = 1, k, s;
    fresh();
    ok &= gm_factory_config(2, &c) == GM_OK && gm_factory_pattern(2, &p) == GM_OK;
    p.timing[3].swing = 40;
    p.cell[3][1] = (gm_cell){GM_ON | GM_ACC | (2u << GM_RAT_SHIFT), COND_1SHOT, -7, -33};
    trk[5].p[P_SRC] = 2;                              /* a manual Grids source on a generated track: replaced */
    p.timing[5].source = 0;
    ok &= gm_native_publish(&p) == GM_OK;
    for (k = 0; k < GM_TRACKS; k++) {
        ok &= trk[k].p[P_SLEN] == p.timing[k].length && trk[k].p[P_SSWING] == p.timing[k].swing;
        ok &= trk[k].p[P_SRC] == 0;
        for (s = 0; s < GM_STEPS; s++) {
            gm_cell a = p.cell[k][s], b = gm_from_native(&trk[k].step[s]);
            ok &= a.flags == b.flags && a.cond == b.cond && a.note == b.note && a.micro == b.micro;
        }
    }
    ok &= trk[0].p[P_SDIV] == 2 && trk[3].step[1].rat == 2 && trk[3].step[1].micro == -33;
    memset(&q, 0, sizeof q);
    ok &= gm_native_export(&q) == GM_OK;
    ok &= !memcmp(q.cell, p.cell, sizeof q.cell);
    for (k = 0; k < GM_TRACKS; k++)
        ok &= q.timing[k].length == p.timing[k].length && q.timing[k].division == p.timing[k].division &&
              q.timing[k].swing == p.timing[k].swing && q.timing[k].source == 0;
    check("adapter: publish writes every native field; export reads the same pattern back", ok);
}

static void publish_refusals(void)
{
    gm_pattern p;
    uint32_t sig, ok = 1;
    fresh();
    gm_factory_pattern(2, &p);
    trk[2].step[4].on = 1;
    trk[2].step[4].nt = 9;
    sig = native_signature();
    p.cell[7][63].cond = COND_MAX + 1u;               /* (the last cell written: nothing before it may land) */
    ok &= gm_native_publish(&p) == GM_INVALID && native_signature() == sig;
    p.cell[7][63].cond = 0;
    chain.running = 1;
    ok &= gm_native_publish(&p) == GM_UNSUPPORTED && native_signature() == sig;
    chain.running = 0;
    song.playing = 1;
    ok &= gm_native_publish(&p) == GM_UNSUPPORTED && native_signature() == sig;
    song.playing = 0;
    ok &= gm_native_publish(&p) == GM_OK && native_signature() != sig;
    check("adapter: invalid, song-running and playing publishes leave the live pattern untouched", ok);
}

/* Bomb at 240 BPM on the 16th grid: the real seq_tick fires exactly the cells the core made, each on its step's start */
static uint32_t hits_at(uint32_t ti, uint32_t frames, uint32_t *at, uint32_t max)
{
    uint32_t f, n = 0, a = hit_age(&trk[ti]);
    for (f = 0; f < frames; f += CTL) {
        render_mix(0, 0, CTL);
        if (hit_age(&trk[ti]) != a) {
            a = hit_age(&trk[ti]);
            if (n < max)
                at[n] = f / CTL;
            n++;
        }
    }
    return n;
}
static void bomb_playback(void)
{
    gm_config c;
    gm_pattern p;
    uint32_t ok = 1, k, s, c0, want[64], nw, at[64], n, shown = 0;
    uint32_t beat = FS * 60u / 240u, rem = 0, start[40], pos = 0;
    fresh();
    song.g[G_BPM] = 240;
    ok &= gm_factory_config(2, &c) == GM_OK && gm_factory_pattern(2, &p) == GM_OK && c.bpm == 240u;
    ok &= gm_native_publish(&p) == GM_OK;
    for (s = 0; s < 40u; s++) {                       /* (an independent model of the steps' starts, 1/16 at 240) */
        uint32_t per = (beat + rem) / 4u;
        start[s] = pos;
        rem = (beat + rem) % 4u;
        pos += per;
    }
    transport_req = 1;
    for (k = 0; k < 2u; k++) {                        /* kick and snare: the cells' own hits, 2 bars of 16 steps */
        for (nw = 0, c0 = 0; c0 < 32u; c0++) {
            gm_cell cell = p.cell[k][c0 % 16u];
            if (cell.flags & GM_ON) {                 /* (late: inside its step; early: in the step before; step 0 early: at once) */
                int32_t m = cell.micro;
                uint32_t due = m >= 0 ? start[c0] + (start[c0 + 1u] - start[c0]) * (uint32_t)m / 100u
                               : !c0 ? 0u
                                     : start[c0 - 1u] + (start[c0] - start[c0 - 1u]) * (uint32_t)(100 + m) / 100u;
                ok &= !(cell.flags >> GM_RAT_SHIFT);  /* (no ratchets in the factory Bomb: one hit a cell) */
                want[nw++] = (due + CTL - 1u) / CTL;
            }
        }
        n = hits_at(k, start[32] - CTL, at, 64);
        ok &= n == nw;
        for (s = 0; s < n && s < nw; s++)
            if (at[s] != want[s]) {
                if (shown++ < 6u)
                    printf("     track %u hit %u at block %u, want %u\n", k, s, at[s], want[s]);
                ok = 0;
            }
        fresh();                                      /* (rewind for the next track: the same publish and PLAY) */
        song.g[G_BPM] = 240;
        gm_native_publish(&p);
        transport_req = 1;
    }
    check("adapter: Bomb at 240 BPM plays exactly the generated hits on the native step starts (real seq_tick)", ok);
}

int main(void)
{
    cell_roundtrip();
    division_map();
    native_limits();
    publish_export();
    publish_refusals();
    bomb_playback();
    printf("test_adapter: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
