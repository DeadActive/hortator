/* Drum firmware UI checks on the host. */
#include "ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static void test_families(void)
{
    static const struct { uint32_t btn; uint32_t fam; } MAP[] = {
        {B_EDIT, FAM_SND}, {B_ENV, FAM_TRK}, {B_LFO, FAM_LAY}, {B_FX, FAM_FX},
        {B_SEQ, FAM_SEQ}, {B_GLO, FAM_GLO}, {B_SAVE, FAM_SAVE},
    };
    uint32_t i, ok = 1;
    ui_host_init();
    for (i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        press(MAP[i].btn);
        ui_frame();
        release_all();
        ok &= !ui.home && cur_page()->fam == MAP[i].fam;
    }
    check("buttons open their page families (EDIT SOUND, ENV TRACK, LFO LAYER, FX, SEQ, GLO, SAVE)", ok);
    press(B_SCL);
    ui_frame();
    release_all();
    check("SCL does nothing", !ui.home && cur_page()->fam == FAM_SAVE);
    press(B_EDIT);
    ui_frame();
    release_all();
    press(B_EDIT);
    ui_frame();
    release_all();
    check("EDIT again: the second SOUND page", ui.page == page_first(FAM_SND) + 1u && str_eq(cur_page()->title, "SOUND"));
    {
        uint32_t i, ok = 1;
        const char *c;
        for (i = 0; i < NPAGES; i++)                 /* the footer numbers pages ("2/2"): titles carry none */
            for (c = PAGES[i].title; *c; c++)
                ok &= *c < '0' || *c > '9';
        check("page titles have no page number in them", ok);
    }
}

static void test_track_select(void)
{
    ui_host_init();
    turn(EN_ALGO, 3);
    ui_frame();
    check("ALGORITHM selects tracks (1 -> 2 per detent)", song.sel == 1);
    for (int i = 0; i < 20; i++) {
        turn(EN_ALGO, 1);
        ui_frame();
    }
    check("ALGORITHM stops at track 8", song.sel == NTRK - 1);
}

static void test_model_swap(void)
{
    uint32_t m0;
    ui_host_init();
    m0 = (uint32_t)trk[0].p[P_MODEL];
    trk[0].p[P_E1] = 3;                              /* an edited DECAY */
    drum_hit(&trk[0], 127);
    turn(EN_PRESET, 1);
    ui_frame();
    check("PRESET on HOME: next model with its default sound, voices stopped",
          (uint32_t)trk[0].p[P_MODEL] == (m0 + 1u) % NMODELS && trk[0].model == trk[0].p[P_MODEL] &&
              trk[0].p[P_E1] == DMODELS[(m0 + 1u) % NMODELS].edit[1].def && !trk[0].v[0].active);
    press(B_ENV);
    ui_frame();
    release_all();
    turn(EN_K1, -1);
    ui_frame();
    check("MODEL knob on TRACK: back to the first model, defaults loaded",
          (uint32_t)trk[0].p[P_MODEL] == m0 && trk[0].p[P_E1] == DMODELS[m0].edit[1].def);
    press(B_FX);
    ui_frame();
    release_all();
    turn(EN_PRESET, 1);
    ui_frame();
    check("PRESET elsewhere (FX page) leaves the model alone", (uint32_t)trk[0].p[P_MODEL] == m0);
}

static void test_home_macros(void)
{
    ui_host_init();
    trk[0].p[P_E1] = 10;
    turn(EN_K2, 5);
    ui_frame();
    check("HOME: KNOB 2 edits DECAY (P_E1) of the selected track", trk[0].p[P_E1] == 15);
}

static void open_step_page(void)
{
    press(B_SEQ);
    ui_frame();
    release_all();
}

static void test_grid_keys(void)
{
    uint32_t a;
    ui_host_init();
    open_step_page();
    check("SEQ opens the STEP grid (keys belong to it)", str_eq(cur_page()->title, "STEP") && song.seq_mode);
    a = dvage;
    keys(1u << 2);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: key 3 sets step 3 on, no drum hit", trk[0].step[2].on && !trk[0].step[2].acc && dvage == a);
    keys(1u << 2);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: second tap = accent", trk[0].step[2].on && trk[0].step[2].acc);
    keys(1u << 2);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: third tap = off", !trk[0].step[2].on && !trk[0].step[2].acc);
    trk[0].p[P_SLEN] = 64;
    press(B_OCTUP);
    ui_frame();
    release_all();
    keys(1u << 0);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: OCT+ moves to bank 2 (key 1 = step 17)", ui.bank == 1 && trk[0].step[16].on);
    {
        uint32_t i, on0 = 0, on1 = 0;
        for (i = 0; i < NSTEP; i++)
            on0 += trk[0].step[i].on + trk[0].step[i].acc;
        keys(1u << 20);
        ui_frame();
        keys(0);
        ui_frame();
        for (i = 0; i < NSTEP; i++)
            on1 += trk[0].step[i].on + trk[0].step[i].acc;
        check("grid: keys above the lowest 16 change no step", on0 == on1);
    }
    press(B_SEQ);                                    /* PATTERN page: keys play drums again */
    ui_frame();
    release_all();
    check("PATTERN page: the grid lets go of the keys", !song.seq_mode);
}

static void test_bank_follows_len(void)
{
    ui_host_init();
    trk[0].p[P_SLEN] = 64;
    open_step_page();
    press(B_OCTUP);
    ui_frame();
    release_all();
    press(B_OCTUP);
    ui_frame();
    release_all();
    press(B_OCTUP);
    ui_frame();
    release_all();
    check("bank 4 of 64 steps", ui.bank == 3);
    trk[0].p[P_SLEN] = 20;                           /* LEN shortened elsewhere (knob, project) */
    ui_frame();
    check("shorter LEN: the bank follows (20 steps = banks 1..2)", ui.bank == 1);
    keys(1u << 10);                                  /* step 27 > LEN: ignored */
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: a key past LEN changes nothing", !trk[0].step[26].on);
}

static void test_mixer_and_rec(void)
{
    ui_host_init();
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();                                      /* REC acts on release (tap) */
    check("REC tap elsewhere opens the TRACKS mixer", !ui.home && cur_page()->fam == FAM_MIX);
    turn(EN_K1, 1);
    ui_frame();
    check("TRACKS: KNOB 1 selects the track", song.sel == 1);
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();
    check("TRACKS: REC tap arms the selected track and starts play", ((song.rec >> 1) & 1u) && song.playing);
}

static void test_clear_confirm(void)
{
    uint32_t i;
    ui_host_init();
    trk[0].step[0].on = trk[0].step[5].on = 1;
    open_step_page();
    fm1_in.buttons |= 1u << panel.btn[B_REC];
    for (i = 0; i < 800; i++)                        /* REC held 0.8 s (1 ms per tick) */
        ui_input();
    release_all();
    check("REC held on SEQ: the clear dialog", ui.confirm != 0);
    press(B_OCTUP);
    ui_frame();
    release_all();
    check("OCT+ clears the track's steps", !trk[0].step[0].on && !trk[0].step[5].on && !ui.confirm);
}

static void test_oct_both_reaches_main(void)
{
    ui_host_init();
    open_step_page();
    fm1_in.buttons = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    ui_input();
    check("OCT- + OCT+ held: buttons stay visible to main.c (UBOOT countdown)",
          (fm1_in.buttons & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) ==
              ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP])));
    release_all();
}

static uint32_t fb_lit(uint32_t y0, uint32_t y1)    /* non-black pixels in rows y0..y1 */
{
    uint32_t n = 0, i;
    for (i = y0 * 240u; i < y1 * 240u; i++)
        n += fb[i] != 0;
    return n;
}

static void snap_page(const char *name)
{
    char path[96];
    uint32_t i;
    for (i = 0; i < 3; i++)
        ui_frame();
    snprintf(path, sizeof path, "build/ui_shots/%s.ppm", name);
    shot(path);
}

/* a drum session for the screens: four on the floor, snare and clap on 2 / 4, hats, a tom fill,
 * a muted rim, a crash; track 3 armed */
static void kit_session(void)
{
    static const char *const PAT[NTRK] = {
        "X...x...x...x...",   /* K909: four on the floor, accent on 1 */
        "....x.......X...",   /* S808 */
        "............x...",   /* C808 */
        "x.x.x...x.x.x...",   /* HATC */
        "......x.......x.",   /* HATO */
        ".............xxX",   /* TOM: fill */
        "...x.......x....",   /* RIM (muted) */
        "X...............",   /* CYMB */
    };
    uint32_t i, k;
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < 16; k++) {
            trk[i].step[k].on = PAT[i][k] != '.';
            trk[i].step[k].acc = PAT[i][k] == 'X';
        }
    trk[6].p[P_MUTE] = 1;
    song.rec = 1u << 2;
}

/* TRACKS: one row per track; a step's cell is lit exactly when the step is on */
static void test_tracks_rows(void)
{
    uint32_t r, k, ok = 1;
    ui_host_init();
    kit_session();
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();
    for (r = 0; r < NTRK; r++)
        for (k = 0; k < 16; k++)
            ok &= (fb[(MX_ROW_Y(r) + 7u) * 240u + MX_CELL_X(k) + 2u] != 0) == trk[r].step[k].on;
    check("TRACKS: 8 rows, each shows its track's 16 steps", ok);
}

static void hold(uint32_t label)                    /* a button held 0.8 s (1 ms per tick), then let go */
{
    uint32_t k;
    press(label);
    for (k = 0; k < 800; k++)
        ui_input();
    release_all();
    ui_frame();
}

/* a screenshot of HOME, every page of PAGES[] (reached with its own button), the menu, ABOUT and
 * the clear dialog: build/ui_shots/NN_title[_n].png */
static void test_screens(void)
{
    uint32_t i, k, n = 0, ok = 1, reach = 1;
    char name[40];
    ui_host_init();
    kit_session();                                   /* a pattern to show */
    transport_req = 1;
    snap_page("00_home");
    ok &= fb_lit(0, 20) > 50 && fb_lit(26, 70) > 200 && fb_lit(202, 240) > 100;
    for (i = 0; i < NPAGES; i++) {
        const page_t *pg = &PAGES[i];
        uint32_t nth = i - page_first(pg->fam), j = 0;
        char *d;
        const char *c;
        for (k = 0; k < NPAGES && (cur_page() != pg || ui.home); k++) {   /* its button until it shows */
            if (pg->fam == FAM_MIX) {
                press(B_REC);                        /* REC opens TRACKS on release */
                ui_frame();
                release_all();
            } else {
                press(FAM_BTN[pg->fam]);
            }
            ui_frame();
            release_all();
        }
        reach &= cur_page() == pg && !ui.home;
        snprintf(name, sizeof name, "%02u_", (unsigned)++n);
        for (d = name + 3, c = pg->title; *c && j < 20u; c++, j++)
            *d++ = *c == '/' ? '-' : (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
        *d = 0;
        if (nth && str_eq(PAGES[i - 1u].title, pg->title))   /* SOUND 2/2, LAYER 2/2 */
            snprintf(d, sizeof name - (size_t)(d - name), "_%u", (unsigned)nth + 1u);
        snap_page(name);
        ok &= fb_lit(26, 70) > 100 && fb_lit(202, 240) > 100;
        if (pg->graph == GR_MIX)
            ok &= fb_lit(74, 198) > 300;             /* 8 rows */
    }
    check("every page of PAGES[] is reached with its button", reach);
    check("every page draws header, columns and footer", ok);
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    hold(B_HOME);                                    /* HOME held: the menu */
    snap_page("90_menu");
    check("HOME held: the menu draws", ui.menu == 1 && fb_lit(20, 230) > 300);
    for (k = 0; k < MI_ABOUT; k++) {                 /* PRESETS: one item per detent */
        turn(EN_PRESET, 1);
        ui_frame();
    }
    press(B_OCTUP);
    ui_frame();
    release_all();
    snap_page("91_about");
    check("menu > ABOUT draws", ui.menu == 2 && fb_lit(20, 230) > 300);
    press(B_OCTDN);
    ui_frame();
    release_all();
    press(B_OCTDN);
    ui_frame();
    release_all();
    press(B_SEQ);
    ui_frame();
    release_all();
    hold(B_REC);                                     /* REC held on SEQ: the clear dialog */
    snap_page("92_clear_track");
    check("REC held on SEQ: the clear dialog draws", ui.confirm && fb_lit(80, 150) > 100);
    press(B_OCTDN);
    ui_frame();
    release_all();
    check("OCT- cancels the clear dialog (screens in build/ui_shots)", !ui.confirm);
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    fm1_irq_off();
    drum_set_model(TSEL, DM_KBOOM);
    fm1_irq_on();
    for (k = 0; k < NPAGES && (cur_page() != &PAGES[0] || ui.home); k++) {   /* SOUND 1/2: its knobs */
        press(B_EDIT);
        ui_frame();
        release_all();
        ui_frame();
    }
    snap_page("93_sound_kboom");
    check("an M1-C model's SOUND page draws (KBOOM: TUNE DECAY TONE PUNCH)", TSEL->model == DM_KBOOM &&
          cur_page() == &PAGES[0] && str_eq(PAGES[0].title, "SOUND") && fb_lit(26, 70) > 100 && fb_lit(202, 240) > 100);
}

/* every engine's SOUND pages, chosen and edited through the UI, for looking at: build/ui_shots/engines/
 * NN_name_P_default / _min / _max (page P = 1 or 2; every knob of the page turned to its end) */
static void engine_page_shots(uint32_t mi, uint32_t pg)
{
    char name[64], low[8];
    uint32_t k, j;
    for (j = 0; N_MODEL[mi][j] && j < sizeof low - 1; j++)
        low[j] = (char)(N_MODEL[mi][j] >= 'A' && N_MODEL[mi][j] <= 'Z' ? N_MODEL[mi][j] + 32 : N_MODEL[mi][j]);
    low[j] = 0;
    for (k = 0; k < NPAGES && (cur_page() != &PAGES[pg] || ui.home); k++) {
        press(B_EDIT);
        ui_frame();
        release_all();
        ui_frame();
    }
    snprintf(name, sizeof name, "engines/%02u_%s_%u_default", (unsigned)mi, low, (unsigned)pg + 1u);
    snap_page(name);
    for (k = 0; k < 4; k++)
        turn(EN_K1 + k, -999);
    ui_frame();
    snprintf(name, sizeof name, "engines/%02u_%s_%u_min", (unsigned)mi, low, (unsigned)pg + 1u);
    snap_page(name);
    for (k = 0; k < 4; k++)
        turn(EN_K1 + k, 999);
    ui_frame();
    snprintf(name, sizeof name, "engines/%02u_%s_%u_max", (unsigned)mi, low, (unsigned)pg + 1u);
    snap_page(name);
}

static void test_engine_screens(void)
{
    uint32_t mi, k, n = 0;
    for (mi = 0; mi < NMODELS; mi++) {
        ui_host_init();
        for (k = 0; k < 2u * NMODELS && (uint32_t)TSEL->p[P_MODEL] != mi; k++) {   /* PRESET on HOME: next engine */
            turn(EN_PRESET, 1);
            ui_frame();
        }
        engine_page_shots(mi, 0);
        n++;
        for (k = 4; k < 8; k++)
            if (DMODELS[mi].edit[k].max != DMODELS[mi].edit[k].min) {   /* SOUND 2/2 has knobs */
                engine_page_shots(mi, 1);
                break;
            }
    }
    printf("     engines: %u engines, SOUND pages at default / min / max in build/ui_shots/engines\n", (unsigned)n);
}

static void test_project_roundtrip(void)
{
    ui_host_init();
    drum_set_model(&trk[2], DM_CONGA);
    trk[2].p[P_E0] = -5;
    trk[2].p[P_LLEVEL] = 77;
    trk[2].step[7].on = trk[2].step[7].acc = 1;
    trk[2].p[P_SLEN] = 23;
    song.g[G_BPM] = 133;
    song.sel = 2;
    project_save(1);
    ui_host_init();
    check("project: a saved slot reads as used", project_used(1) && !project_used(0));
    project_load(1);
    check("project: load restores models, params, steps, globals, selection",
          trk[2].p[P_MODEL] == DM_CONGA && trk[2].model == DM_CONGA && trk[2].p[P_E0] == -5 &&
              trk[2].p[P_LLEVEL] == 77 && trk[2].step[7].on && trk[2].step[7].acc && trk[2].p[P_SLEN] == 23 &&
              song.g[G_BPM] == 133 && song.sel == 2);
}

static void test_project_rejects(void)
{
    ui_host_init();
    project_save(0);
    proj_slot[0].t[3].p[P_MODEL] = 99;               /* corrupt the stored data, fix the checksum */
    proj_slot[0].t[3].p[P_E1] = 30000;
    proj_slot[0].t[3].step[0].on = 7;
    proj_slot[0].sum = proj_sum(&proj_slot[0]);
    project_load(0);
    check("project: out-of-range values are clamped on load",
          trk[3].p[P_MODEL] < NMODELS && trk[3].p[P_E1] <= DMODELS[trk[3].p[P_MODEL]].edit[1].max &&
              trk[3].step[0].on == 1);
    proj_slot[1].magic = 0x46554E33u;                /* an old Felucca project ("FUN3") */
    check("project: Felucca projects are not used", !project_used(1));
    project_save(2);
    proj_slot[2].sum ^= 1;
    check("project: a bad checksum is not used", !project_used(2));
}

int main(void)
{
    test_engine_screens();
    test_families();
    test_track_select();
    test_model_swap();
    test_home_macros();
    test_grid_keys();
    test_bank_follows_len();
    test_mixer_and_rec();
    test_clear_confirm();
    test_oct_both_reaches_main();
    test_tracks_rows();
    test_screens();
    test_project_roundtrip();
    test_project_rejects();
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: all passed\n", fails);
    return fails ? 1 : 0;
}
