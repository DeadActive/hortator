/* SPDX-License-Identifier: GPL-3.0-only */
/* The GRIND pages through the real UI (panel buttons, encoders, frames): ARP steps to them, a knob turn becomes a request,
 * the pattern changes at the cue boundary (playing) or at once (stopped), and nothing is stored in a project. */
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

static void tap(uint32_t b)
{
    press(b);
    ui_frame();
    release_all();
    ui_frame();
}
static void frames(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ui_frame();
}
static void fresh(void)
{
    ui_host_init();
    gm_runtime_reset();
    gm_v[GU_STYLE] = 0;
    gm_v[GU_AGGR] = 75;
    gm_v[GU_DENS] = 65;
    gm_v[GU_CHAOS] = 20;
    gm_v[GU_HUMAN] = 25;
    gm_v[GU_MUT] = 0;
    gm_v[GU_WHEN] = 1;
    song.g[G_BPM] = 240;
}
static void open_grind(uint32_t page)               /* ARP, then ARP again until the GRIND page `page` (0 / 1) shows */
{
    uint32_t i, seen = 0;
    for (i = 0; i < 8u && !(cur_page()->scope == SC_GRIND && seen == page); i++) {
        tap(B_ARP);
        if (cur_page()->scope == SC_GRIND)
            seen = cur_page()->id[0] == GU_HUMAN ? 1u : 0u;
    }
}
static uint32_t sig(void)
{
    uint32_t h = 2166136261u, k, s;
    for (k = 0; k < NTRK; k++) {
        const uint8_t *b = (const uint8_t *)trk[k].step;
        for (s = 0; s < sizeof trk[k].step; s++)
            h = (h ^ b[s]) * 16777619u;
    }
    return h;
}
/* what the knobs ask for, generated from the live pattern by the core itself */
static int live_is(const gm_config *c, const gm_pattern *base)
{
    gm_pattern want;
    uint32_t k, s;
    if (gm_generate(c, base, &want) != GM_OK)
        return 0;
    for (k = 0; k < NTRK; k++) {
        uint8_t id;
        if (gm_native_division(want.timing[k].division, &id) != GM_OK || trk[k].p[P_SLEN] != want.timing[k].length ||
            trk[k].p[P_SDIV] != id)
            return 0;
        for (s = 0; s < GM_STEPS; s++) {
            gm_cell a = want.cell[k][s], b = gm_from_native(&trk[k].step[s]);
            if (a.flags != b.flags || a.cond != b.cond || a.note != b.note || a.micro != b.micro)
                return 0;
        }
    }
    return 1;
}

static void navigation(void)
{
    uint32_t ok = 1;
    fresh();
    open_grind(0);
    if (getenv("GRIND_SHOT")) {                       /* (a look at the pages: GRIND_SHOT=<prefix>) */
        char path[256];
        turn(EN_K1, 4);
        frames(3);
        snprintf(path, sizeof path, "%s-1.ppm", getenv("GRIND_SHOT"));
        shot(path);
        tap(B_ARP);
        snprintf(path, sizeof path, "%s-2.ppm", getenv("GRIND_SHOT"));
        shot(path);
        fresh();
        open_grind(0);
    }
    ok &= cur_page()->scope == SC_GRIND && cur_page()->title[0] == 'G' && cur_page()->title[4] == 'D' && !cur_page()->title[5] && cur_page()->id[0] == GU_STYLE;
    ok &= cur_page()->fam == FAM_GRIDS;
    tap(B_ARP);
    ok &= cur_page()->scope == SC_GRIND && cur_page()->id[0] == GU_HUMAN;
    check("ui: ARP steps GRIDS, then the two GRIND pages (STYLE AGGR DENS CHAOS / HUMAN MUT WHEN)", ok);
}

static void style_stopped(void)
{
    gm_pattern base;
    gm_config c;
    uint32_t ok = 1, before;
    fresh();
    open_grind(0);
    gm_native_export(&base);
    before = sig();
    turn(EN_K1, 3);                                  /* STYLE, three detents on */
    frames(3);
    ok &= gm_v[GU_STYLE] > 0 && sig() != before;
    gm_ui_config(&c);
    ok &= c.algorithm == (uint8_t)(gm_v[GU_STYLE] + 1) && live_is(&c, &base);
    ok &= gm_runtime_commits() == 1u && str_eq(ui.msg, gm_style_name((uint8_t)(gm_v[GU_STYLE] + 1)));   /* the style said */
    check("ui: a STYLE turn while stopped publishes the style's pattern at once, the core's own result", ok);
    if (!ok)
        printf("     style %d, sig changed %d, commits %u, msg '%s'\n", gm_v[GU_STYLE], sig() != before,
               gm_runtime_commits(), ui.msg);
}

static void knobs_morph(void)
{
    gm_pattern base;
    gm_config c;
    uint32_t ok = 1, a, b;
    fresh();
    open_grind(0);
    turn(EN_K1, 2);
    frames(3);
    a = sig();
    gm_native_export(&base);
    turn(EN_K2, -40);                                /* AGGR down */
    frames(3);
    ok &= gm_v[GU_AGGR] < 75 && gm_runtime_commits() == 2u;
    gm_ui_config(&c);
    ok &= live_is(&c, &base);
    b = sig();
    ok &= a != b;
    check("ui: AGGR turned re-generates the same style with the new control (no seed or style change)", ok);
}

static void mutate_and_when(void)
{
    uint32_t ok = 1, a;
    fresh();
    open_grind(0);
    turn(EN_K1, 2);
    frames(3);
    a = sig();
    tap(B_ARP);                                      /* page 2: HUMAN MUT WHEN */
    ok &= cur_page()->id[0] == GU_HUMAN;
    turn(EN_K2, 1);                                  /* MUT */
    frames(3);
    ok &= gm_v[GU_MUT] > 0 && sig() != a;
    if (!ok)
        printf("     MUT %d, page id0 %u, commits %u, sig same %d, msg '%s'\n", gm_v[GU_MUT], cur_page()->id[0],
               gm_runtime_commits(), sig() == a, ui.msg);
    turn(EN_K3, -1);                                 /* WHEN: END -> STEP */
    frames(2);
    ok &= gm_v[GU_WHEN] == 0;
    check("ui: MUT re-rolls the pattern, WHEN chooses the boundary (END / STEP)", ok);
}

static void playing_waits(void)
{
    uint32_t ok = 1, i, old, first = 0, last, changes = 0;
    fresh();
    open_grind(0);
    turn(EN_K1, 1);
    frames(3);
    transport_req = 1;
    frames(40);
    old = sig();
    last = old;
    turn(EN_K1, 2);                                  /* STYLE while playing, WHEN = END */
    for (i = 0; i < 1500u; i++) {
        ui_frame();
        if (sig() != last) {
            if (!first)
                first = i;
            changes++;
            last = sig();
        }
    }
    ok &= changes == 1u && first > 500u;             /* (the pattern's end: about a second at 240 BPM, not at once) */
    check("ui: while playing a STYLE turn waits for the end of the pattern and publishes exactly once", ok);
    if (!ok)
        printf("     changes %u, first change in frame %u\n", changes, first);
}

static void song_refuses(void)
{
    uint32_t ok = 1, before;
    fresh();
    open_grind(0);
    before = sig();
    chain.running = 1;                               /* (no frames while it is faked: the song's rows are not set up) */
    gm_ui_changed(GU_STYLE);                         /* what edit_param calls for a turned knob */
    chain.running = 0;
    frames(3);
    ok &= sig() == before && gm_runtime_commits() == 0u && ui.msg[0] == 'S';     /* STOP THE SONG */
    check("ui: while a song runs a GRIND turn is refused with a message and changes nothing", ok);
}

static void not_stored(void)
{
    uint32_t ok = 1, i;
    int16_t g[G_COUNT];
    fresh();
    open_grind(0);
    memcpy(g, song.g, sizeof g);
    turn(EN_K1, 2);
    turn(EN_K2, 5);
    frames(4);
    for (i = 0; i < G_COUNT; i++)
        if (i != G_BPM)
            ok &= g[i] == song.g[i];                 /* (the globals a project stores are untouched) */
    check("ui: the GRIND knobs are screen state: no project global changes", ok);
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    navigation();
    style_stopped();
    knobs_morph();
    mutate_and_when();
    playing_waits();
    song_refuses();
    not_stored();
    printf("test_ui: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
