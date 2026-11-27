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
static int grind_page(void)                         /* 0 .. 3 on a GRIND page, else -1 */
{
    if (cur_page()->scope != SC_GRIND)
        return -1;
    return cur_page()->id[0] == GU_HUMAN ? 1 : cur_page()->id[0] == GU_KEEP ? 2 : cur_page()->id[0] == GU_BROWSE ? 3 : cur_page()->id[0] == GU_SINK ? 4 : 0;
}
static void open_grind(uint32_t page)               /* ARP, then ARP again until the GRIND page `page` (0 .. 3) shows */
{
    uint32_t i;
    for (i = 0; i < 10u && grind_page() != (int)page; i++)
        tap(B_ARP);
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
    tap(B_ARP);
    ok &= cur_page()->scope == SC_GRIND && cur_page()->id[0] == GU_KEEP && cur_page()->id[1] == GU_UNDO && cur_page()->id[2] == GU_FREE;
    check("ui: ARP steps GRIDS, then the three GRIND pages (STYLE AGGR DENS CHAOS / HUMAN MUT WHEN KIT / KEEP UNDO FREE)", ok);
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

static void kit_roles(void)
{
    static const uint8_t WANT[GM_TRACKS] = {DM_KTRX, DM_SDTRX, DM_CHTRX, DM_CYTRX, DM_CYTRX, DM_CYTRX, DM_XTTRX, DM_XTTRX};
    uint32_t ok = 1, k;
    int16_t level[NTRK], pan[NTRK], model[NTRK];
    uint32_t pattern;
    fresh();
    open_grind(0);
    tap(B_ARP);                                      /* page 2: HUMAN MUT WHEN KIT */
    ok &= cur_page()->id[3] == GU_KIT;
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_LEVEL] = (int16_t)(40 + k);
        trk[k].p[P_PAN] = (int16_t)(10 + k);
        level[k] = trk[k].p[P_LEVEL];
        pan[k] = trk[k].p[P_PAN];
        model[k] = trk[k].p[P_MODEL];
    }
    pattern = sig();
    turn(EN_K4, 1);                                  /* one detent: it only asks */
    frames(2);
    ok &= gm_v[GU_KIT] == 0 && ui.msg[0] == 'A';     /* AGAIN: ROLE KIT */
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].p[P_MODEL] == model[k];
    turn(EN_K4, 1);                                  /* the second one acts */
    frames(2);
    for (k = 0; k < NTRK; k++) {
        ok &= trk[k].p[P_MODEL] == WANT[k] && trk[k].model == WANT[k];
        ok &= trk[k].p[P_LEVEL] == level[k] && trk[k].p[P_PAN] == pan[k];     /* the mix stays */
    }
    ok &= trk[3].p[P_E0] < trk[5].p[P_E0];           /* ride below china: the shared cymbal model is told apart */
    ok &= trk[6].p[P_E0] > trk[7].p[P_E0];           /* the high tom above the low one */
    ok &= gm_v[GU_KIT] == 0 && sig() == pattern;     /* the button is at rest and no step moved */
    check("ui: KIT asks twice, then gives the eight tracks their role sounds and leaves levels, pans and steps alone", ok);
}

static void kit_refused_playing(void)
{
    uint32_t ok = 1, k;
    int16_t model[NTRK];
    fresh();
    open_grind(0);
    tap(B_ARP);
    for (k = 0; k < NTRK; k++)
        model[k] = trk[k].p[P_MODEL];
    transport_req = 1;
    frames(20);
    turn(EN_K4, 1);
    frames(2);
    turn(EN_K4, 1);
    frames(2);
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].p[P_MODEL] == model[k];
    ok &= ui.msg[0] == 'S';                          /* STOP FIRST */
    check("ui: KIT while playing is refused (STOP FIRST) and changes no sound", ok);
}

static void kit_arm_times_out(void)
{
    uint32_t ok = 1, k;
    int16_t model[NTRK];
    fresh();
    open_grind(0);
    tap(B_ARP);
    for (k = 0; k < NTRK; k++)
        model[k] = trk[k].p[P_MODEL];
    turn(EN_K4, 1);
    frames(120);                                     /* far past the confirm window */
    turn(EN_K4, 1);
    frames(2);
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].p[P_MODEL] == model[k];
    check("ui: a KIT detent after the confirm window only asks again, it does not act", ok);
}

static void keep_track(void)
{
    uint32_t ok = 1, k, s, other_changed = 0;
    step_t t0[NSTEP];
    fresh();
    open_grind(0);
    turn(EN_K1, 2);                                  /* a first style: something on the tracks */
    frames(3);
    memcpy(t0, trk[0].step, sizeof t0);
    open_grind(2);
    ok &= gm_v[GU_KEEP] == 0;
    turn(EN_K1, 1);                                  /* KEEP up: the selected track, whole */
    frames(2);
    ok &= gm_v[GU_KEEP] == trk[0].p[P_SLEN] && gm_keep_count(0) == (uint32_t)trk[0].p[P_SLEN] && gm_keep_count(1) == 0 && ui.msg[0] == 'T';
    open_grind(0);
    for (k = 0; k < 4u; k++) {                       /* four re-rolls of the same style: AGGR, DENS, CHAOS, AGGR */
        static step_t snap[NTRK][NSTEP];
        for (s = 0; s < NTRK; s++)
            memcpy(snap[s], trk[s].step, sizeof snap[s]);
        turn(k == 2u ? EN_K4 : k == 1u ? EN_K3 : EN_K2, k == 0u ? -35 : 30);
        frames(3);
        ok &= memcmp(trk[0].step, t0, sizeof t0) == 0;                 /* the kept track: untouched */
        for (s = 1; s < NTRK; s++)
            other_changed |= memcmp(trk[s].step, snap[s], sizeof snap[s]) != 0;
    }
    ok &= other_changed != 0;
    track_select(1);
    frames(2);
    open_grind(2);
    ok &= gm_v[GU_KEEP] == 0;                        /* the knob shows the selected track's count */
    track_select(0);
    frames(2);
    ok &= gm_v[GU_KEEP] == trk[0].p[P_SLEN];
    turn(EN_K1, -1);                                 /* down: freed */
    frames(2);
    ok &= gm_keep_count(0) == 0 && ui.msg[0] == 'T';
    check("ui: KEEP up keeps the selected track through four re-rolls, down frees it; the knob follows the selection", ok);
}

static void keep_does_not_fit(void)
{
    uint32_t ok = 1, i, refused = 0, fits = 0, n;
    step_t t0[NSTEP];
    fresh();
    open_grind(0);
    turn(EN_K1, 1);
    frames(3);
    memcpy(t0, trk[0].step, sizeof t0);
    n = (uint32_t)trk[0].p[P_SLEN];
    gm_keep_track(0, 1);
    for (i = 0; i < 21u; i++) {                      /* every other style, one detent at a time */
        turn(EN_K1, 1);
        frames(3);
        if (str_eq(ui.msg, "KEEP DOES NOT FIT"))
            refused++;
        else
            fits++;
        ok &= memcmp(trk[0].step, t0, n * sizeof t0[0]) == 0;      /* kept, or the change refused: the steps are as they were */
    }
    printf("     keep through 21 styles: %u fit, %u refused\n", fits, refused);
    trk[0].p[P_SDIV] = (int16_t)(trk[0].p[P_SDIV] == 0 ? 1 : trk[0].p[P_SDIV] - 1);   /* the track is on another grid now */
    gm_touch();
    turn(EN_K2, -7);
    frames(3);
    ok &= str_eq(ui.msg, "KEEP DOES NOT FIT") && memcmp(trk[0].step, t0, n * sizeof t0[0]) == 0;
    check("ui: through every style the kept steps stay as they were (a style they do not fit is refused, not applied)", ok);
}

static void keep_step_and_free(void)
{
    uint32_t ok = 1, h, i;
    fresh();
    open_grind(0);
    turn(EN_K1, 2);
    frames(3);
    h = sig();
    ui.env = 1;                                      /* STEPS: the white keys are the selected track's steps */
    keys(1u << STEP_KEY[2]);
    for (i = 0; i < STEP_HOLD_MS + 60u; i++)
        ui_frame();
    keys(0);
    ui_frame();
    ok &= gm_keep_get(0, 2) == 1 && gm_keep_count(0) == 1 && str_eq(ui.msg, "STEP 3 KEPT");
    ok &= sig() == h;                                /* the hold changed no step, an off one did not turn on */
    keys(1u << STEP_KEY[2]);
    for (i = 0; i < STEP_HOLD_MS + 60u; i++)
        ui_frame();
    keys(0);
    ui_frame();
    ok &= gm_keep_get(0, 2) == 0 && str_eq(ui.msg, "STEP 3 FREE") && sig() == h;   /* the same hold again: free */
    gm_keep_set(3, 9, 1);
    gm_keep_set(5, 63, 1);
    open_grind(2);
    turn(EN_K3, 1);                                  /* FREE: every track */
    frames(2);
    ok &= gm_keep_count(3) == 0 && gm_keep_count(5) == 0 && gm_v[GU_FREE] == 0;
    check("ui: a held step key (STEPS, a GRIND page) toggles that step's KEEP without touching it; FREE frees every track", ok);
}

static void keep_goes_with_the_pattern(void)
{
    uint32_t ok = 1;
    fresh();
    gm_keep_set(2, 5, 1);
    gm_context_isr();
    ok &= gm_keep_count(2) == 0;
    check("ui: a loaded or switched pattern starts with no KEEP", ok);
}

static void undo_redo(void)
{
    uint32_t ok = 1, a, b;
    fresh();
    open_grind(2);
    turn(EN_K2, 1);                                  /* nothing published yet */
    frames(2);
    ok &= str_eq(ui.msg, "NOTHING TO UNDO");
    open_grind(0);
    a = sig();
    turn(EN_K1, 2);
    frames(3);
    b = sig();
    ok &= a != b;
    open_grind(2);
    turn(EN_K2, 1);
    frames(2);
    ok &= sig() == a && str_eq(ui.msg, "UNDONE") && gm_v[GU_UNDO] == 0;
    turn(EN_K2, 1);
    frames(2);
    ok &= sig() == b && str_eq(ui.msg, "REDONE");
    check("ui: UNDO brings back the pattern before the last GRIND change, a second one goes forward again", ok);
}

static void undo_keeps_timing(void)
{
    uint32_t ok = 1, k;
    uint8_t len[NTRK];
    fresh();
    for (k = 0; k < NTRK; k++)
        len[k] = (uint8_t)trk[k].p[P_SLEN];
    open_grind(0);
    turn(EN_K1, 9);                                  /* a style that may set its own lengths */
    frames(3);
    open_grind(2);
    turn(EN_K2, 1);
    frames(2);
    for (k = 0; k < NTRK; k++)
        ok &= (uint8_t)trk[k].p[P_SLEN] == len[k];
    check("ui: UNDO restores the lengths too", ok);
}

static void knobs_stop_at_the_ends(void)
{
    static const struct { uint32_t page, enc, id; int16_t max; } K[] = {
        {0, EN_K2, GU_AGGR, 100}, {0, EN_K3, GU_DENS, 100}, {0, EN_K4, GU_CHAOS, 100},
        {1, EN_K1, GU_HUMAN, 100}, {1, EN_K2, GU_MUT, 99}};
    uint32_t ok = 1, i, j, a;
    for (a = 0; a < 2u; a++) {                       /* knob acceleration on and off */
        fresh();
        settings.accel = a;
        for (i = 0; i < sizeof K / sizeof K[0]; i++) {
            open_grind(K[i].page);
            for (j = 0; j < 12u; j++) {              /* turned on far past the top, in clicks of every size */
                turn(K[i].enc, 1 + (int32_t)j * 3);
                frames(1);
            }
            ok &= gm_v[K[i].id] == K[i].max;         /* stopped at the top: no wrap to 0 */
            turn(K[i].enc, 1);
            frames(1);
            ok &= gm_v[K[i].id] == K[i].max;
            for (j = 0; j < 12u; j++) {              /* and off the bottom */
                turn(K[i].enc, -(1 + (int32_t)j * 3));
                frames(1);
            }
            ok &= gm_v[K[i].id] == 0;                /* stopped at 0: no wrap to the top */
            turn(K[i].enc, -1);
            frames(1);
            ok &= gm_v[K[i].id] == 0;
        }
    }
    settings.accel = 1;
    check("ui: AGGR DENS CHAOS HUMAN MUT stop at 100 (99) and at 0, however far and fast they are turned", ok);
}

static void reset_starts_from_a_valid_config(void)
{
    gm_config f;
    uint32_t ok = 1;
    ui_host_init();
    memset(&gmrt, 0xA5, sizeof gmrt);                /* whatever was in RAM */
    gm_runtime_reset();
    gm_factory_config(1, &f);
    ok &= gmrt.ctl.active_config.algorithm == 1u && !memcmp(&gmrt.ctl.active_config, &f, sizeof f);
    ok &= gm_validate_config(&gmrt.ctl.active_config) == GM_OK && gmrt.next_rev == 0u && !gmrt.undo_ok;
    check("runtime: a reset holds style 1's factory config (the id 0 it used to ask for is not a style)", ok);
}

static void seven_four_style(void)
{
    uint32_t ok = 1, k, s;
    static const uint32_t kick[] = {0, 12, 20};
    fresh();
    open_grind(0);
    ok &= GM_UI_STYLES == GM_ALGORITHM_COUNT + 1u && N_GSTYLE[GM_UI_STYLES - 1u][0] == 'M';
    turn(EN_K1, 40);                                 /* to the last entry: the list stops there */
    frames(3);
    ok &= gm_v[GU_STYLE] == (int16_t)(GM_UI_STYLES - 1u);
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].p[P_SLEN] == 28 && trk[k].p[P_SDIV] == 2;       /* LEN 28, 1/16 */
    for (s = 0; s < 3u; s++)
        ok &= trk[0].step[kick[s]].on && trk[0].step[kick[s]].acc;
    for (s = 28; s < NSTEP; s++)
        ok &= !trk[0].step[s].on;
    ok &= str_eq(ui.msg, "Mathcore 7/4");
    turn(EN_K1, -1);                                 /* back to Stoner: 4/4 and 16 steps again */
    frames(3);
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].p[P_SLEN] == 16;
    check("ui: the last STYLE entry is Technical in 7/4: LEN 28, the 3+2+2 hits; the one before is 4/4 again", ok);
}

/* the browser's preview against the core's own result for that style on the live pattern */
static int preview_equals_core(uint32_t idx)
{
    gm_config c;
    gm_pattern base, want;
    uint32_t k, s;
    if (gm_ui_style_config(idx, &c) != GM_OK)
        return 0;
    c.bpm = (uint16_t)clamp(song.g[G_BPM], 40, 350);
    gm_native_export(&base);
    memcpy(base.lock, gmrt.lock, sizeof base.lock);
    if (gm_generate(&c, &base, &want) != GM_OK || gm_pv.state != PV_READY || gm_pv.idx != idx)
        return 0;
    if (gm_pv.len != want.timing[0].length || gm_pv.div != want.timing[0].division)
        return 0;
    for (k = 0; k < NTRK; k++)
        for (s = 0; s < gm_pv.len; s++) {
            gm_cell a = want.cell[k][s];
            if (!!(gm_pv.on[k][s >> 3] >> (s & 7u) & 1u) != !!(a.flags & GM_ON) ||
                !!(gm_pv.acc[k][s >> 3] >> (s & 7u) & 1u) != !!(a.flags & GM_ACC) ||
                !!(gm_pv.rat[k][s >> 3] >> (s & 7u) & 1u) != !!(a.flags >> GM_RAT_SHIFT))
                return 0;
        }
    return 1;
}

static void browser_previews_and_cancels(void)
{
    uint32_t ok = 1, before, commits;
    int16_t bpm;
    fresh();
    open_grind(0);
    turn(EN_K1, 1);                                  /* style 2 live */
    frames(3);
    before = sig();
    commits = gm_runtime_commits();
    bpm = song.g[G_BPM];
    open_grind(3);
    ok &= cur_page()->id[0] == GU_BROWSE && gm_v[GU_BROWSE] == gm_v[GU_STYLE];
    turn(EN_K1, 3);                                  /* browse three on */
    frames(3);
    ok &= gm_v[GU_BROWSE] == gm_v[GU_STYLE] + 3 && preview_equals_core((uint32_t)gm_v[GU_BROWSE]);
    ok &= sig() == before && gm_runtime_commits() == commits && song.g[G_BPM] == bpm;   /* nothing live moved */
    ok &= gm_v[GU_STYLE] == 1 && gm_ui_style_config(4, &(gm_config){0}) == GM_OK;
    tap(B_ARP);                                      /* leaving the page cancels */
    frames(3);
    ok &= gm_v[GU_BROWSE] == gm_v[GU_STYLE] && sig() == before && gm_runtime_commits() == commits;
    check("ui: STYLE BROWSER shows what the core would make for the browsed style and changes nothing live; leaving cancels", ok);
}

static void browser_every_style(void)
{
    uint32_t ok = 1, i;
    fresh();
    open_grind(3);
    for (i = 0; i < GM_UI_STYLES; i++) {
        gm_v[GU_BROWSE] = (int16_t)i;                /* (the knob's value: a turn per style would only slow the test) */
        frames(2);
        ok &= preview_equals_core(i);
        ok &= gm_pv.len == (i == GM_UI_STYLES - 1u ? 28u : gm_pv.len) && gm_pv.bpm > 0 && gm_pv.num >= 3u;
    }
    ok &= gm_pv.len == 28u && gm_pv.num == 7u && gm_pv.den == 4u;   /* the last one: 7/4, two banks */
    gm_v[GU_PVBANK] = 4;                             /* more banks than it has: held at the last one */
    frames(2);
    ok &= gm_v[GU_PVBANK] == 2;
    if (getenv("GRIND_SHOT")) {                      /* (a look at the page: GRIND_SHOT=<prefix>) */
        char path[256];
        ui.force = 1;
        frames(2);
        snprintf(path, sizeof path, "%s-browser-bank2.ppm", getenv("GRIND_SHOT"));
        shot(path);
        gm_v[GU_PVBANK] = 1;
        gm_v[GU_BROWSE] = 8;
        ui.force = 1;
        frames(3);
        snprintf(path, sizeof path, "%s-browser-technical.ppm", getenv("GRIND_SHOT"));
        shot(path);
    }
    check("ui: STYLE BROWSER previews all 23 entries, the last one 7/4 with LEN 28", ok);
}

static void browser_confirm_stopped(void)
{
    uint32_t ok = 1;
    gm_pattern base;
    gm_config c;
    fresh();
    open_grind(3);
    turn(EN_K1, 6);
    frames(3);
    gm_native_export(&base);
    turn(EN_K3, 1);                                  /* APPLY */
    frames(3);
    ok &= gm_runtime_commits() == 1u && gm_v[GU_STYLE] == 6 && gm_v[GU_OK] == 0;
    gm_ui_config(&c);
    ok &= c.algorithm == 7u && live_is(&c, &base);   /* the preview's pattern, now live */
    ok &= preview_equals_core(6) || gm_pv.state == PV_READY;
    check("ui: APPLY while stopped publishes the browsed style once and makes it the committed one", ok);
}

static void browser_confirm_playing(void)
{
    uint32_t ok = 1, i, old, first = 0, last, changes = 0;
    fresh();
    open_grind(3);
    transport_req = 1;
    frames(40);
    old = sig();
    last = old;
    turn(EN_K1, 4);
    frames(3);
    turn(EN_K3, 1);                                  /* APPLY while playing, WHEN = END */
    ok &= gm_runtime_commits() == 0u && sig() == old;
    for (i = 0; i < 1500u; i++) {
        ui_frame();
        if (sig() != last) {
            if (!first)
                first = i;
            changes++;
            last = sig();
        }
    }
    ok &= changes == 1u && first > 500u && gm_runtime_commits() == 1u;
    check("ui: APPLY while playing is pending, then one native commit at the pattern's end", ok);
}

static void browser_follows_live_edits_and_keep(void)
{
    uint32_t ok = 1, s;
    fresh();
    open_grind(0);
    turn(EN_K1, 1);
    frames(3);
    open_grind(3);
    turn(EN_K1, 2);
    frames(3);
    ok &= preview_equals_core((uint32_t)gm_v[GU_BROWSE]);
    for (s = 0; s < 16u; s++)                        /* the player changes the live pattern: native edit */
        if (!trk[2].step[s].on) {
            gm_touch();
            trk[2].step[s].on = 1;
            break;
        }
    frames(3);
    ok &= preview_equals_core((uint32_t)gm_v[GU_BROWSE]);                    /* regenerated from the new base */
    gm_keep_track(0, 1);                              /* KEEP the kick track: the preview keeps it too */
    frames(3);
    ok &= preview_equals_core((uint32_t)gm_v[GU_BROWSE]);
    for (s = 0; s < 16u; s++)
        ok &= !!(gm_pv.on[0][s >> 3] >> (s & 7u) & 1u) == !!trk[0].step[s].on;
    check("ui: the preview is made again from a changed live pattern and honours KEEP (the base the publish will use)", ok);
}

static void browser_busy_while_a_request_waits(void)
{
    uint32_t ok = 1;
    fresh();
    open_grind(0);
    transport_req = 1;
    frames(40);
    turn(EN_K1, 3);                                  /* a style request waits for the end */
    frames(2);
    open_grind(3);
    turn(EN_K1, 1);
    frames(3);
    ok &= gm_pv.state == PV_BUSY;                    /* not made while the stage is in use: BUSY, no corruption */
    {
        uint32_t i;
        for (i = 0; i < 1600u && gm_pv.state != PV_READY; i++)
            ui_frame();
    }
    ok &= gm_pv.state == PV_READY && preview_equals_core((uint32_t)gm_v[GU_BROWSE]);   /* made once the stage was free */
    check("ui: the preview waits (BUSY) while a request is staged, then appears", ok);
}

static void vary_knob(void)
{
    uint32_t ok = 1, before;
    fresh();
    open_grind(2);
    ok &= gm_vary == 0;
    before = sig();
    turn(EN_K4, 5);
    frames(4);
    ok &= gm_vary > 0 && gm_vary == (uint8_t)gm_v[GU_VARY];     /* VARY reaches the sequencer */
    ok &= sig() == before && !gmrt.ctl.has_pending;             /* a performance feel: no pattern, no request */
    turn(EN_K4, -5);
    frames(4);
    ok &= gm_vary == 0;
    if (!ok)
        printf("v=%d vary=%d sig=%d pend=%d\n", gm_v[GU_VARY], gm_vary, sig() == before, gmrt.ctl.has_pending);
    check("ui: VARY is a live knob on the KEEP page, it makes no request", ok);
}

static void live_page(void)
{
    uint32_t ok = 1, before;
    fresh();
    open_grind(4);
    ok &= grind_page() == 4;
    before = sig();
    turn(EN_K1, 7);
    turn(EN_K2, 3);
    frames(4);
    ok &= gm_sink == 7 && gm_swell == 3;                        /* the macros reach the mixer */
    ok &= sig() == before && !gmrt.ctl.has_pending;             /* no pattern, no request */
    turn(EN_K1, -7);
    turn(EN_K2, -3);
    frames(4);
    ok &= gm_sink == 0 && gm_swell == 0;
    song.playing = 1;
    turn(EN_K3, 1);                                             /* CUT */
    frames(4);
    ok &= !song.playing && gm_v[GU_CUT] == 0;
    check("ui: the LIVE page: SINK and SWELL are held levels, CUT stops", ok);
}

static void init_clears_macros(void)
{
    uint32_t ok = 1;
    fresh();
    open_grind(4);
    turn(EN_K1, 30);
    turn(EN_K2, 20);
    frames(4);
    gm_vary = 9;
    ok &= gm_sink == 30 && gm_swell == 20;
    init_all();
    ok &= gm_sink == 0 && gm_swell == 0 && gm_vary == 0;
    ok &= gm_v[GU_SINK] == 0 && gm_v[GU_SWELL] == 0 && gm_v[GU_VARY] == 0;
    check("ui: TOOLS INIT clears SINK, SWELL and VARY", ok);
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
    kit_roles();
    kit_refused_playing();
    kit_arm_times_out();
    keep_track();
    keep_does_not_fit();
    keep_step_and_free();
    keep_goes_with_the_pattern();
    undo_redo();
    undo_keeps_timing();
    knobs_stop_at_the_ends();
    reset_starts_from_a_valid_config();
    seven_four_style();
    browser_previews_and_cancels();
    browser_every_style();
    browser_confirm_stopped();
    browser_confirm_playing();
    browser_follows_live_edits_and_keep();
    browser_busy_while_a_request_waits();
    vary_knob();
    live_page();
    init_clears_macros();
    not_stored();
    printf("test_ui: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
