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

static void snap_page(const char *name);
static void seq_play_to(uint32_t ti, uint32_t step);
static void seq_open(const char *title);
static uint32_t fb_lit(uint32_t y0, uint32_t y1);
static void tap(uint32_t b);
static int led_on(uint32_t id)                       /* the LED of button / key id (ui_leds' last frame) */
{
    uint32_t q = led_pos[id];
    return q != 0xFF && ((fm1_led[q >> 3] >> (q & 7u)) & 1u);
}

static void test_families(void)
{
    static const struct { uint32_t btn; uint32_t fam; } MAP[] = {
        {B_EDIT, FAM_SND}, {B_FX, FAM_FX},
        {B_SEQ, FAM_SEQ}, {B_GLO, FAM_GLO}, {B_SAVE, FAM_SAVE}, {B_ARP, FAM_GRIDS}, {B_LFO, FAM_LFO},
    };
    uint32_t i, ok = 1;
    ui_host_init();
    for (i = 0; i < sizeof MAP / sizeof MAP[0]; i++) {
        press(MAP[i].btn);
        ui_frame();
        release_all();
        ui_frame();                                  /* (FX opens its pages on the release) */
        ok &= !ui.home && cur_page()->fam == MAP[i].fam;
    }
    check("buttons open their page families (EDIT SOUND, FX, SEQ, GLO, SAVE, ARP GRIDS, LFO LFO)", ok);
    press(B_ENV);
    ui_frame();
    release_all();
    press(B_SCL);
    ui_frame();
    release_all();
    check("ENV and SCL do nothing (free)", !ui.home && cur_page()->fam == FAM_LFO);
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

/* EDIT: one list per engine, 4 knobs a page: MODEL, its TUNE DECAY TONE and 4th knob, its extras, then
 * LVL PAN NOTE CHOKE (no MUTE: the TRACKS quick mutes) */
static void edit_next(void)
{
    press(B_EDIT);
    ui_frame();
    release_all();
}

static void test_sound_pages(void)
{
    uint32_t n, k;
    const page_t *pg;
    ui_host_init();
    drum_set_model(&trk[0], DM_C808);
    edit_next();
    pg = cur_page();
    n = fam_pages(FAM_SND, &k);
    check("EDIT page 1 (C808): MODEL TUNE DECAY TONE; 3 pages",
          n == 3u && k == 1u && page_id(pg, 0) == P_MODEL && page_id(pg, 1) == P_E0 && page_id(pg, 3) == P_E2);
    snap_page("engines/zz_c808_edit_1");
    edit_next();
    pg = cur_page();
    check("EDIT page 2 (C808): TAIL LVL PAN NOTE",
          page_id(pg, 0) == P_E3 && page_id(pg, 1) == P_LEVEL && page_id(pg, 2) == P_PAN && page_id(pg, 3) == P_NOTE);
    snap_page("engines/zz_c808_edit_2");
    edit_next();
    pg = cur_page();
    n = fam_pages(FAM_SND, &k);
    check("EDIT page 3: CHOKE, nothing else (no MUTE)", page_id(pg, 0) == P_CHOKE && page_id(pg, 1) == 0xFFu && k == 3u && n == 3u);
    snap_page("engines/zz_c808_edit_3");
    edit_next();
    n = fam_pages(FAM_SND, &k);
    check("EDIT again: back to page 1", k == 1u);
    drum_set_model(&trk[0], DM_K909);
    edit_next();
    pg = cur_page();
    check("K909 page 2: CLICK SWPT DRIVE LVL (its extras before LVL)",
          page_id(pg, 0) == P_E3 && page_id(pg, 1) == P_E4 && page_id(pg, 2) == P_E5 && page_id(pg, 3) == P_LEVEL);
    edit_next();
    edit_next();
    turn(EN_K1, 1);
    ui_frame();
    check("EDIT page 1: KNOB 1 is MODEL (next engine, its default sound)", (uint32_t)trk[0].p[P_MODEL] == DM_K909 + 1u);
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
    press(B_EDIT);
    ui_frame();
    release_all();
    turn(EN_K1, 1);
    ui_frame();
    check("MODEL knob on EDIT (page 1): next model with its default sound, voices stopped",
          (uint32_t)trk[0].p[P_MODEL] == (m0 + 1u) % NMODELS && trk[0].model == trk[0].p[P_MODEL] &&
              trk[0].p[P_E1] == DMODELS[(m0 + 1u) % NMODELS].edit[1].def && !trk[0].v[0].active);
    turn(EN_K1, -1);
    ui_frame();
    check("MODEL knob on EDIT (page 1): back to the first model, defaults loaded",
          (uint32_t)trk[0].p[P_MODEL] == m0 && trk[0].p[P_E1] == DMODELS[m0].edit[1].def);
    press(B_FX);
    ui_frame();
    release_all();
    ui_frame();
    turn(EN_PRESET, 1);
    ui_frame();
    check("PRESET elsewhere (FX page) leaves the model alone", (uint32_t)trk[0].p[P_MODEL] == m0);
}

/* PRESET turns the current section's pages, both ways, stopping at the ends (HOME: HOME 1/3 <-> COMP 2/3 <-> 3/3);
 * it no longer changes the engine */
static void preset(int32_t d)
{
    turn(EN_PRESET, d);
    ui_frame();
}

static void test_preset_pages(void)
{
    uint32_t m0, pos, k;
    ui_host_init();
    m0 = (uint32_t)TSEL->p[P_MODEL];
    preset(1);
    check("PRESET on HOME: COMP (HOME 2/3), the engine unchanged",
          !ui.home && cur_page()->fam == FAM_HOME && page_id(cur_page(), 0) == G_CSRC && (uint32_t)TSEL->p[P_MODEL] == m0);
    preset(1);
    check("PRESET again: COMP (HOME 3/3)", page_id(cur_page(), 0) == G_CATK);
    preset(1);
    check("PRESET at the last page: stays", page_id(cur_page(), 0) == G_CATK);
    preset(-1);
    preset(-1);
    check("PRESET back twice: the HOME screen", ui.home);
    preset(-1);
    check("PRESET back on HOME 1/3: stays, the engine unchanged", ui.home && (uint32_t)TSEL->p[P_MODEL] == m0);
    tap(B_FX);
    check("FX opens on FILTER 1/2", str_eq(cur_page()->title, "FILTER") && page_id(cur_page(), 0) == P_FTYPE);
    preset(1);                                       /* FILTER 2/2 */
    preset(1);                                       /* FX */
    preset(1);
    check("PRESET on FX: SLICER", str_eq(cur_page()->title, "SLICER"));
    preset(1);
    check("PRESET again: RESON 1/2", str_eq(cur_page()->title, "RESON") && page_id(cur_page(), 0) == P_RMODEL);
    preset(-1);
    check("PRESET back: SLICER", str_eq(cur_page()->title, "SLICER"));
    tap(B_SEQ);
    tap(B_FX);
    check("FX again later: the page PRESET left it on (SLICER)", str_eq(cur_page()->title, "SLICER"));
    tap(B_EDIT);                                     /* SOUND 1/3 .. 3/3; page 4 is empty for every engine: skipped */
    for (k = 0; k < 6u; k++)
        preset(1);
    check("PRESET on EDIT: stops on the engine's last SOUND page (empty pages skipped)",
          str_eq(cur_page()->title, "SOUND") && fam_pages(FAM_SND, &pos) == pos && !page_hidden(ui.page));
    tap(B_OCTUP);                                    /* the layer */
    preset(1);
    check("PRESET in the layer: LAYER 2/2", str_eq(cur_page()->title, "LAYER") && page_id(cur_page(), 0) == P_LDEC);
    preset(1);
    check("PRESET at LAYER 2/2: stays", page_id(cur_page(), 0) == P_LDEC);
    press(B_REC);                                    /* TRACKS: one page */
    ui_frame();
    release_all();
    ui_frame();
    k = ui.page;
    preset(1);
    preset(-1);
    check("PRESET on TRACKS: nothing to turn", ui.page == k && cur_page()->fam == FAM_MIX);
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

/* the STEP grid's keys: the 16 white keys F3 G3 A3 B3 C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 (key index from F3) */
static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};

static int led_lit(uint32_t id)                     /* the LED of button / key id, as ui_leds left it */
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((fm1_led[q >> 3] >> (q & 7u)) & 1u);
}

/* a key held on the grid for ms (frames advance the host clock), then let go */
static void grid_hold(uint32_t key, uint32_t ms)
{
    uint32_t t;
    keys(1u << key);
    ui_frame();
    for (t = 0; t < ms; t += 50u) {
        host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
        ui_frame();
    }
    keys(0);
    ui_frame();
}

/* a tap turns a step on / off; a hold (0.4 s) flips its accent and leaves it on */
static void test_grid_hold_accent(void)
{
    step_t *s = &trk[0].step[4];
    ui_host_init();
    open_step_page();
    grid_hold(WHITE[4], 0);
    check("grid: a tap turns step 5 on, no accent", s->on && !s->acc);
    grid_hold(WHITE[4], 0);
    check("grid: a tap on an on step turns it off", !s->on && !s->acc);
    grid_hold(WHITE[4], 300);
    check("grid: a 0.3 s press is still a tap (on, no accent)", s->on && !s->acc);
    grid_hold(WHITE[4], 600);
    check("grid: a 0.6 s hold on an on step adds the accent (it stays on)", s->on && s->acc);
    grid_hold(WHITE[4], 600);
    check("grid: a hold on an accented step removes the accent (it stays on)", s->on && !s->acc);
    s->on = 0;
    s->acc = 0;
    grid_hold(WHITE[4], 600);
    check("grid: a hold on an off step makes it on with accent", s->on && s->acc);
    grid_hold(WHITE[4], 0);
    check("grid: a tap on an accented step turns it off", !s->on && !s->acc);
    {
        uint32_t ms;
        keys(1u << WHITE[4]);                        /* the accent appears while still held */
        ui_frame();
        for (ms = 0; ms < 500u; ms += 50u) {
            host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
            ui_frame();
        }
        check("grid: the accent shows while the key is still held", s->on && s->acc);
        keys(0);
        ui_frame();
        check("grid: letting go after a hold changes nothing more", s->on && s->acc);
    }
}


/* a step key held + KNOB n (n = 0 PROB, 1 RATCH) turned `steps` detents, slowly, then let go */
static void grid_hold_turn(uint32_t key, uint32_t knob, int32_t steps)
{
    int32_t i;
    keys(1u << key);
    ui_frame();
    for (i = 0; i < (steps < 0 ? -steps : steps); i++) {
        turn(EN_K1 + knob, steps < 0 ? -1 : 1);
        host_ticks += 100u * 1000u * FM1_TICKS_PER_US;   /* 0.1 s apart: no acceleration */
        ui_frame();
    }
}

static void test_grid_step_edit(void)
{
    step_t *s = &trk[0].step[4];
    ui_host_init();
    open_step_page();
    grid_hold(WHITE[4], 600);                        /* on, accented */
    grid_hold_turn(WHITE[4], 0, -5);                 /* 100 % -> 75 % */
    check("hold + KNOB 1: PROB of the held step (100 % -> 75 %); its accent stays, no accent flip from the hold",
          s->on && s->acc && s->cond == cond_store(15) && ui.bank == 0);
    snap_page("seq/20_step_held_prob");
    keys(0);
    ui_frame();
    grid_hold_turn(WHITE[4], 1, 2);
    keys(0);
    ui_frame();
    check("hold + KNOB 2: RATCH of the held step (1 -> 3 hits)", s->rat == 2 && s->on && s->acc);
    grid_hold_turn(WHITE[6], 0, 3);                  /* an off step: on, 100 % -> 1-SHOT -> 1/2 -> 2/2 */
    keys(0);
    ui_frame();
    check("hold + KNOB 1 on an off step turns it on (no accent): PROB past 100 % gives 1-SHOT, 1/2, 2/2",
          trk[0].step[6].on && !trk[0].step[6].acc && trk[0].step[6].cond == 23);
    grid_hold(WHITE[6], 0);
    check("a tap turning a step off resets its PROB / RATCH", !trk[0].step[6].on && trk[0].step[6].cond == 0);
    grid_hold(WHITE[4], 600);                        /* accent off by hold: PROB / RATCH kept */
    check("a hold flips the accent and keeps PROB / RATCH", s->on && !s->acc && s->cond == cond_store(15) && s->rat == 2);
    trk[0].p[P_SLEN] = 24;                           /* two banks; on bank 2 keys 9..16 are outside LEN */
    ui_frame();
    press(B_OCTUP);
    ui_frame();
    release_all();
    grid_hold_turn(WHITE[12], 0, -2);                /* step 29: outside LEN */
    keys(0);
    ui_frame();
    check("hold + KNOB 1 on a key outside LEN edits nothing and does not turn the bank",
          ui.bank == 1 && trk[0].step[28].cond == 0 && !trk[0].step[28].on);
    press(B_OCTDN);
    ui_frame();
    release_all();
    trk[0].p[P_SLEN] = 16;
    trk[0].step[0].on = 1;
    trk[0].step[0].cond = 33;                        /* 3/5 */
    trk[0].step[8].on = trk[0].step[8].acc = 1;
    trk[0].step[8].rat = 3;
    trk[0].step[12].on = 1;
    trk[0].step[12].cond = (uint8_t)cond_store(10);
    trk[0].step[12].rat = 1;
    ui.force = 1;
    snap_page("seq/21_step_prob_ratch");
    open_step_page();                                /* PATTERN */
    snap_page("seq/22_pattern_prob_ratch");
}

static void test_grids_pages(void)
{
    ui_host_init();
    press(B_ARP);
    ui_frame();
    release_all();
    check("ARP opens GRIDS 1/2 (MODE X Y CHAOS)", !ui.home && str_eq(cur_page()->title, "GRIDS") && cur_page()->id[0] == G_GMODE);
    snap_page("grids/01_map_page1");
    turn(EN_K1 + 1, 3);
    ui_frame();
    check("GRIDS MAP: KNOB 2 turns X", song.g[G_GX] == 67);
    turn(EN_K1, 1);
    ui_frame();
    check("GRIDS: KNOB 1 switches MODE to EUCL", song.g[G_GMODE] == 1);
    turn(EN_K1 + 1, -2);
    ui_frame();
    check("GRIDS EUCLID: KNOB 2 turns LEN K (X kept)", song.g[G_GLEN1] == 14 && song.g[G_GX] == 67);
    snap_page("grids/03_euclid_page1");
    press(B_ARP);
    ui_frame();
    release_all();
    check("ARP again: GRIDS 2/2 (FIL K FIL S FIL H)", cur_page()->id[0] == G_GFILL1);
    turn(EN_K1 + 2, 5);
    ui_frame();
    check("GRIDS 2/2: KNOB 3 turns FIL H", song.g[G_GFILL3] == 69);
    trk[0].p[P_SRC] = 1;
    trk[5].p[P_SRC] = 1;
    trk[1].p[P_SRC] = 2;
    trk[3].p[P_SRC] = 3;
    ui.force = 1;
    snap_page("grids/04_euclid_page2_routing");
    song.g[G_GMODE] = 0;
    transport_req = 1;
    seq_play_to(1, 6);
    snap_page("grids/02_map_page2_playing");
    press(B_ARP);
    ui_frame();
    release_all();
    snap_page("grids/05_map_page1_playing");
}

/* a track on a Grids channel: STEP / PATTERN / the footer / the LEDs show the channel, read-only */
static void test_src_readonly(void)
{
    uint32_t k, ok = 1;
    ui_host_init();
    trk[0].step[0].on = 1;
    seq_open("PATTERN");
    turn(EN_K1 + 3, 1);
    ui_frame();
    check("PATTERN: KNOB 4 is SRC (STEP -> G-KCK)", trk[0].p[P_SRC] == 1);
    snap_page("grids/07_pattern_src_kick");
    song.g[G_GFILL1] = 127;
    seq_open("STEP");
    check("a Grids track's STEP grid shows the channel: MAP, 32 steps on 2 banks", bank_count() == 2u);
    for (k = 0; k < 16u; k++)
        ok &= led_lit(14u + WHITE[k]) == (int)(grids_preview(0, k) & 1u);
    check("the keys' LEDs show the channel's steps", ok);
    snap_page("grids/06_step_grids_track");
    grid_hold(WHITE[3], 0);
    grid_hold_turn(WHITE[3], 0, 2);
    keys(0);
    ui_frame();
    check("keys and hold + knob do nothing on a Grids track", !trk[0].step[3].on && trk[0].step[3].cond == 0 && trk[0].step[0].on);
    song.g[G_GMODE] = 1;
    song.g[G_GLEN1] = 20;
    ui_frame();
    check("EUCLID: the grid shows LEN K steps (20: 2 banks)", bank_count() == 2u && view_len(&trk[0]) == 20u);
    trk[0].p[P_SRC] = 0;
    ui_frame();
    check("SRC back to STEP: its own steps again", bank_count() == 1u && led_lit(14u + WHITE[0]));
    song.g[G_GMODE] = 0;
    trk[0].p[P_SRC] = 1;
    trk[2].p[P_SRC] = 3;
    press(B_HOME);                                   /* HOME, then a REC tap: TRACKS (on SEQ REC arms) */
    ui_frame();
    release_all();
    ui_frame();
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();
    check("TRACKS open for the Grids rows screen", !ui.home && cur_page()->scope == SC_MIX && !song.rec);
    transport_req = 1;
    seq_play_to(1, 5);
    snap_page("grids/08_tracks_grids_rows");
}

/* review #2: a step key held while the track changes (ALGO, or a page change) writes nothing into the new track */
static void test_grid_hold_track_change(void)
{
    step_t before;
    ui_host_init();
    trk[0].p[P_SLEN] = 64;
    trk[1].p[P_SLEN] = 16;
    open_step_page();
    press(B_OCTUP);                                  /* bank 2 of T1: steps 17..32 */
    ui_frame();
    release_all();
    trk[0].step[20].on = 1;
    trk[0].step[20].cond = 33;
    before = trk[1].step[20];
    keys(1u << WHITE[4]);                            /* hold T1 step 21 */
    ui_frame();
    turn(EN_ALGO, 1);                                /* T2 while holding */
    ui_frame();
    turn(EN_K1, 2);
    host_ticks += 100u * 1000u * FM1_TICKS_PER_US;
    ui_frame();
    host_ticks += 600u * 1000u * FM1_TICKS_PER_US;   /* and held past the accent time */
    ui_frame();
    keys(0);
    ui_frame();
    check("a held step key, then another track (ALGO): the new track's steps are untouched (also outside its LEN)",
          song.sel == 1 && !memcmp(&trk[1].step[20], &before, sizeof before) && trk[0].step[20].cond == 0 &&
              !trk[0].step[20].on);
}

static void open_comp(int page2)                    /* HOME until COMP (HOME 2/3, or 3/3) shows */
{
    uint32_t k;
    for (k = 0; k < 8u && (ui.home || cur_page()->graph != GR_COMP || (cur_page()->id[0] == G_CATK) != page2); k++) {
        press(B_HOME);
        ui_frame();
        release_all();
        ui_frame();
    }
}

static void test_comp_pages(void)
{
    uint32_t k, pos, fx_comp = 0;
    ui_host_init();
    for (k = 0; k < 12u; k++) {                      /* FX steps through its pages: COMP is not among them */
        press(B_FX);
        ui_frame();
        release_all();
        ui_frame();
        fx_comp |= !ui.home && cur_page()->graph == GR_COMP;
    }
    check("FX no longer reaches COMP", !fx_comp);
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    check("HOME from FX: the HOME screen (1/3)", ui.home);
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    check("HOME again: COMP (HOME 2/3: SRC THRSH RATIO REL)",
          !ui.home && str_eq(cur_page()->title, "COMP") && cur_page()->fam == FAM_HOME && cur_page()->id[0] == G_CSRC &&
              fam_pages(FAM_HOME, &pos) == 2u && pos == 1u && led_lit(panel.btn[B_HOME]));
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    check("HOME again: COMP (HOME 3/3: ATK KNEE MKUP)", !ui.home && cur_page()->id[0] == G_CATK);
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    check("HOME again: back to the HOME screen", ui.home);
    open_comp(0);
    snap_page("comp/01_off");
    turn(EN_K1, 1);
    ui_frame();
    turn(EN_K1 + 1, -2);
    ui_frame();
    check("COMP: KNOB 1 picks the source (T1), KNOB 2 the threshold", song.g[G_CSRC] == 1 && song.g[G_CTHR] == 24);
    open_comp(1);
    turn(EN_K1 + 1, -1);
    ui_frame();
    check("FX again: COMP 2/2 (ATK KNEE MKUP); KNOB 2 sets the knee HARD",
          cur_page()->id[0] == G_CATK && cur_page()->id[2] == G_CMKUP && song.g[G_CKNEE] == 0);
    snap_page("comp/03_page2_curve_hard");
}

/* COMP 3/3's KNOB 4: GHOST (MUTE KEEP HIDE, KEEP at start); the picture shows the source HEARD / GHOST / MUTED */
static void test_comp_ghost_knob(void)
{
    char v[12];
    const char *u;
    int ok = 1;
    ui_host_init();
    song.g[G_CSRC] = 1;
    trk[1].p[P_DUCK] = 1;
    open_comp(1);
    check("COMP 3/3: KNOB 4 is GHOST, KEEP at start, the source HEARD",
          cur_page()->id[3] == G_CGHOST && str_eq(GP[G_CGHOST].label, "GHOST") && song.g[G_CGHOST] == CG_KEEP &&
              str_eq(comp_src_state(), "HEARD"));
    param_format(&GP[G_CGHOST], CG_MUTE, v, &u);
    ok &= str_eq(v, "MUTE");
    param_format(&GP[G_CGHOST], CG_HIDE, v, &u);
    ok &= str_eq(v, "HIDE");
    check("GHOST values: MUTE KEEP HIDE", ok);
    trk[0].p[P_MUTE] = 1;
    ui_frame();
    check("GHOST KEEP, the source muted: GHOST", str_eq(comp_src_state(), "GHOST"));
    snap_page("comp/05_ghost_keep_muted");
    turn(EN_K1 + 3, -1);
    ui_frame();
    check("KNOB 4 down: MUTE; the muted source MUTED", song.g[G_CGHOST] == CG_MUTE && str_eq(comp_src_state(), "MUTED"));
    snap_page("comp/06_ghost_mute_muted");
    trk[0].p[P_MUTE] = 0;
    turn(EN_K1 + 3, 2);
    ui_frame();
    check("KNOB 4 up twice: HIDE; the unmuted source GHOST", song.g[G_CGHOST] == CG_HIDE && str_eq(comp_src_state(), "GHOST"));
    snap_page("comp/07_ghost_hide");
    open_comp(0);
    snap_page("comp/08_ghost_hide_page1");
    song.g[G_CSRC] = 0;
    check("SRC OFF: no source state", str_eq(comp_src_state(), ""));
    song.g[G_CSRC] = 1;
    project_save(0);
    song.g[G_CGHOST] = CG_MUTE;
    project_load(0);
    check("GHOST is saved with the project (FDR6)", song.g[G_CGHOST] == CG_HIDE);
    memset(&proj_slot[0], 0, sizeof proj_slot[0]);  /* the slot empty again for the project tests */
}

/* COMP pages: white keys toggle DUCK (lit = ducked), not on the source; with SRC OFF too; elsewhere they play */
static void test_comp_keys(void)
{
    uint32_t a;
    ui_host_init();
    open_comp(0);
    a = hit_age(&trk[1]);
    keys(1u << KEY_TRK_KEY[1]);
    ui_frame();
    keys(0);
    ui_frame();
    check("COMP (SRC OFF): key 2 sets DUCK on T2, no hit, its LED lit",
          trk[1].p[P_DUCK] == 1 && hit_age(&trk[1]) == a && led_lit(14u + KEY_TRK_KEY[1]));
    song.g[G_CSRC] = 1;
    keys(1u << KEY_TRK_KEY[0]);
    ui_frame();
    keys(0);
    ui_frame();
    check("COMP: the source's key (T1) does not toggle DUCK", trk[0].p[P_DUCK] == 0 && !led_lit(14u + KEY_TRK_KEY[0]));
    keys(1u << KEY_TRK_KEY[1]);
    ui_frame();
    keys(0);
    ui_frame();
    check("COMP: key 2 again: DUCK off", trk[1].p[P_DUCK] == 0 && !led_lit(14u + KEY_TRK_KEY[1]));
    trk[1].p[P_DUCK] = trk[3].p[P_DUCK] = trk[4].p[P_DUCK] = 1;
    drum_set_model(&trk[1], DM_HATO);
    trk[0].step[0].on = trk[0].step[4].on = trk[0].step[8].on = trk[0].step[12].on = 1;
    trk[1].step[2].on = trk[1].step[6].on = trk[1].step[10].on = trk[1].step[14].on = 1;
    transport_req = 1;
    seq_play_to(0, 1);
    snap_page("comp/02_pumping");
    song.g[G_CMKUP] = 127;                           /* MKUP at the end: Streams' limiter */
    seq_play_to(0, 5);
    snap_page("comp/04_limiter");
    for (a = 0; a < 3u && !ui.home; a++) {           /* HOME steps on through COMP 3/3 to the HOME screen */
        press(B_HOME);
        ui_frame();
        release_all();
        ui_frame();
    }
    a = hit_age(&trk[2]);
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    check("leaving COMP: the keys play again", hit_age(&trk[2]) != a);
}

/* TOOLS: CLR* clears every pattern and its settings (sounds untouched); INIT* puts everything at power-on;
 * both arm on the first detent and act on the second */
static void test_tools_all(void)
{
    uint32_t k;
    ui_host_init();
    drum_set_model(&trk[2], DM_CONGA);
    trk[2].p[P_E0] = 7;
    trk[2].p[P_LEVEL] = 90;
    trk[2].p[P_DIST] = 50;
    trk[2].step[3].on = 1;
    trk[2].step[3].cond = 33;
    trk[2].p[P_SLEN] = 12;
    trk[2].p[P_SDIV] = 1;
    trk[2].p[P_SSWING] = 40;
    trk[2].p[P_SRC] = 2;
    trk[5].step[0].on = 1;
    song.g[G_BPM] = 140;
    song.g[G_GX] = 99;
    song.g[G_CSRC] = 3;
    for (k = 0; k < NPAGES && (ui.home || !str_eq(cur_page()->title, "TOOLS")); k++) {
        press(B_SAVE);
        ui_frame();
        release_all();
    }
    turn(EN_K1 + 2, 1);
    ui_frame();
    check("TOOLS CLR*: the first detent only arms", trk[2].step[3].on && trk[5].step[0].on);
    turn(EN_K1 + 2, 1);
    ui_frame();
    check("TOOLS CLR*: every pattern cleared, LEN 16 / DIV 1/16 / SWG 0 / SRC STEP",
          !trk[2].step[3].on && trk[2].step[3].cond == 0 && !trk[5].step[0].on && trk[2].p[P_SLEN] == 16 &&
              trk[2].p[P_SDIV] == TP[P_SDIV].def && trk[2].p[P_SSWING] == 0 && trk[2].p[P_SRC] == 0);
    check("TOOLS CLR*: sounds, FX, Grids, COMP and BPM untouched",
          trk[2].p[P_MODEL] == DM_CONGA && trk[2].p[P_E0] == 7 && trk[2].p[P_LEVEL] == 90 && trk[2].p[P_DIST] == 50 &&
              song.g[G_BPM] == 140 && song.g[G_GX] == 99 && song.g[G_CSRC] == 3);
    snap_page("tools_all");
    transport_req = 1;
    ui_frame();
    song.rec = 4u;
    song.master_q12 = 900;
    turn(EN_K1 + 3, 1);
    ui_frame();
    check("TOOLS INIT*: the first detent only arms", trk[2].p[P_MODEL] == DM_CONGA);
    turn(EN_K1 + 3, 1);
    ui_frame();
    ui_frame();
    check("TOOLS INIT*: everything at power-on (kit, sounds, BPM, Grids, COMP), stopped, nothing armed",
          (uint32_t)trk[2].p[P_MODEL] == KIT_DEF[2][0] && trk[2].p[P_LEVEL] == TP[P_LEVEL].def && trk[2].p[P_DIST] == 0 &&
              song.g[G_BPM] == GP[G_BPM].def && song.g[G_GX] == GP[G_GX].def && song.g[G_CSRC] == 0 &&
              !song.playing && song.rec == 0 && song.sel == 0);
    check("TOOLS INIT*: the volume knob's level is kept", song.master_q12 == 900u);
}

/* the sample layer lives in EDIT: OCT+ on a SOUND page opens LAYER 1/2, EDIT steps LAYER 1/2 <-> 2/2 (the EDIT
 * LED blinks), OCT- goes back to the SOUND page it came from; EDIT from elsewhere opens SOUND */
static void tap(uint32_t b)
{
    press(b);
    ui_frame();
    release_all();
    ui_frame();
}

static void test_layer_in_edit(void)
{
    uint32_t k, on = 0, off = 0;
    ui_host_init();
    tap(B_EDIT);
    tap(B_EDIT);                                     /* SOUND 2/3 */
    k = ui.page;
    tap(B_OCTUP);
    check("EDIT: OCT+ opens LAYER 1/2", str_eq(cur_page()->title, "LAYER") && cur_page()->id[0] == P_LSET);
    for (k = 0; k < 40u; k++) {                      /* the EDIT LED blinks while in the layer (2 s, 50 ms frames) */
        host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
        ui_frame();
        if (led_lit(panel.btn[B_EDIT]))
            on++;
        else
            off++;
    }
    check("LAYER: the EDIT LED blinks", on > 5u && off > 5u);
    snap_page("layer_in_edit");
    tap(B_EDIT);
    check("LAYER: EDIT steps to LAYER 2/2", str_eq(cur_page()->title, "LAYER") && cur_page()->id[0] == P_LDEC);
    tap(B_EDIT);
    check("LAYER: EDIT again back to LAYER 1/2", cur_page()->id[0] == P_LSET);
    tap(B_OCTDN);
    check("LAYER: OCT- back to the SOUND page it came from (2/3)", str_eq(cur_page()->title, "SOUND") &&
                                                                      ui.page == page_first(FAM_SND) + 1u);
    ui_frame();
    check("SOUND: the EDIT LED is steady again", led_lit(panel.btn[B_EDIT]));
    tap(B_OCTUP);
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    ui_frame();
    check("LAYER: a white key selects its track (as on SOUND)", song.sel == 2 && str_eq(cur_page()->title, "LAYER"));
    tap(B_FX);
    tap(B_EDIT);
    check("EDIT from another section opens SOUND, not the layer", str_eq(cur_page()->title, "SOUND"));
}

/* the LFO button: two pages (WAVE RATE MORPH DEPTH / DEST TRIG PHASE) of the LFO OCT- selects; OCT+ its rate mode */
static void test_lfo_pages(void)
{
    uint32_t w, k, pos, on = 0, off = 0;
    char v[12];
    const char *u;
    ui_host_init();
    tap(B_LFO);
    check("LFO opens LFO 1/2: WAVE RATE MORPH DEPTH of LFO 1",
          str_eq(cur_page()->title, "LFO") && page_id(cur_page(), 0) == P_LFO1 + LF_WAVE &&
              page_id(cur_page(), 3) == P_LFO1 + LF_DEPTH && fam_pages(FAM_LFO, &pos) == 2u && pos == 1u);
    param_format(&TP[P_LFO1 + LF_RATE], 23, v, &u);
    check("LFO RATE in SYNC: 23 = 1BAR", str_eq(v, "1BAR"));
    tap(B_OCTUP);
    param_format(&TP[P_LFO1 + LF_RATE], 127, v, &u);
    check("OCT+ on an LFO page: LFO 1 -> Hz (RATE 127 = 40.0 Hz)",
          TSEL->p[P_LFO1 + LF_MODE] == LM_HZ && str_eq(v, "40.0") && str_eq(u, "Hz"));
    tap(B_OCTUP);
    param_format(&TP[P_LFO1 + LF_RATE], 0, v, &u);
    check("OCT+ again: TIME (RATE 0 = 25 ms)", TSEL->p[P_LFO1 + LF_MODE] == LM_TIME && str_eq(v, "25") && str_eq(u, "ms"));
    tap(B_OCTUP);
    check("OCT+ again: back to SYNC", TSEL->p[P_LFO1 + LF_MODE] == LM_SYNC);
    tap(B_LFO);
    check("LFO again: LFO 2/2 (DEST TRIG PHASE of LFO 1)", page_id(cur_page(), 0) == P_LFO1 + LF_DEST);
    turn(EN_K1, 3);
    ui_frame();
    param_format(&TP[P_LFO1 + LF_DEST], TSEL->p[P_LFO1 + LF_DEST], v, &u);
    check("LFO DEST shows the track's knob by its label (K909: DEST 3 = SWEEP)", str_eq(v, "SWEEP"));
    tap(B_OCTDN);
    check("OCT- on an LFO page: LFO 2, the same page (2/2: DEST of LFO 2)",
          str_eq(cur_page()->title, "LFO") && page_id(cur_page(), 0) == P_LFO2 + LF_DEST && song.lsel == 1u);
    turn(EN_K1, 2);
    ui_frame();
    check("LFO 2's DEST knob edits LFO 2 (LFO 1's DEST kept)",
          TSEL->p[P_LFO2 + LF_DEST] > 0 && TSEL->p[P_LFO1 + LF_DEST] == 3);
    tap(B_LFO);
    tap(B_OCTUP);
    check("LFO 1/2 on LFO 2: OCT+ switches LFO 2's mode only",
          page_id(cur_page(), 0) == P_LFO2 + LF_WAVE && TSEL->p[P_LFO2 + LF_MODE] == LM_HZ &&
              TSEL->p[P_LFO1 + LF_MODE] == LM_SYNC);
    for (k = 0; k < 40u; k++) {                      /* the LFO LED blinks while LFO 2 is shown (2 s, 50 ms frames) */
        host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
        ui_frame();
        if (led_lit(panel.btn[B_LFO]))
            on++;
        else
            off++;
    }
    check("LFO 2: the LFO LED blinks", on > 5u && off > 5u);
    snap_page("lfo/lfo2_page1");
    keys(1u << KEY_TRK_KEY[3]);
    ui_frame();
    keys(0);
    ui_frame();
    check("LFO pages: a white key selects its track, LFO 2 stays shown", song.sel == 3 && song.lsel == 1u);
    tap(B_OCTDN);
    ui_frame();
    check("OCT- again: back to LFO 1, its LED steady", song.lsel == 0u && page_id(cur_page(), 0) == P_LFO1 + LF_WAVE &&
                                                          led_lit(panel.btn[B_LFO]));
    for (w = 0; w < LW_COUNT; w++) {                 /* every waveform's page, LFO 1 on TONE, playing */
        char name[40];
        ui_host_init();
        TSEL->p[P_LFO1 + LF_WAVE] = (int16_t)w;
        TSEL->p[P_LFO1 + LF_MORPH] = 64;
        TSEL->p[P_LFO1 + LF_DEST] = 3;
        TSEL->p[P_LFO1 + LF_DEPTH] = 40;
        TSEL->p[P_LFO1 + LF_MODE] = LM_HZ;           /* ~8 Hz: the random waves' trail fills in the frames below */
        TSEL->p[P_LFO1 + LF_RATE] = 100;
        tap(B_LFO);
        transport_req = 1;
        seq_play_to(0, 3);
        for (k = 0; k < 300u; k++)
            ui_frame();
        {
            uint32_t j;
            snprintf(name, sizeof name, "lfo/%02u_%s", (unsigned)w, N_LWAVE[w]);
            for (j = 4; name[j]; j++)                /* file names: S&H -> SnH, EXP+ -> EXPu, EXP- -> EXPd */
                name[j] = name[j] == '&' ? 'n' : name[j] == '+' ? 'u' : name[j] == '-' ? 'd' : name[j];
        }
        snap_page(name);
    }
}

/* S&H / WANDER / RWALK: a scope, the live value as a dot at the right edge and its trail to the left */
static void test_lfo_trail(void)
{
    static int16_t seen[4000];
    uint32_t k, n = 0, ok = 1, distinct = 0, y;
    int32_t prev = 99999;
    ui_host_init();
    TSEL->p[P_LFO1 + LF_WAVE] = LW_SH;
    TSEL->p[P_LFO1 + LF_DEST] = 9;
    TSEL->p[P_LFO1 + LF_DEPTH] = 64;
    TSEL->p[P_LFO1 + LF_TRIG] = LT_FREE;
    TSEL->p[P_LFO1 + LF_MODE] = LM_HZ;
    for (k = 0; k < 127u && LR_HZ_X100[k] < 1000u; k++)   /* ~10 Hz: ~140 frames (blocks) a cycle */
        ;
    TSEL->p[P_LFO1 + LF_RATE] = (int16_t)k;
    tap(B_LFO);
    for (k = 0; k < 600u; k++) {
        ui_frame();
        seen[n++] = (int16_t)(lfo_out(TSEL, 0) >> 1);
    }
    for (k = 0; k < ui.tr_n; k++) {                  /* every trail point is a value the LFO had */
        int16_t tv = ui.tr[(ui.tr_h + 136u - ui.tr_n + k) % 136u];
        uint32_t j, f = 0;
        for (j = 0; j < n && !f; j++)
            f = seen[j] == tv;
        ok &= f;
        distinct += tv != prev;
        prev = tv;
    }
    printf("     LFO trail: %u points, %u changes\n", (unsigned)ui.tr_n, (unsigned)distinct);
    check("LFO S&H trail: full width, made of the LFO's values, several steps", ui.tr_n == 136u && ok && distinct >= 3u);
    y = (uint32_t)(Y_GRAPH + G_OY + 40 - lfo_out(TSEL, 0) * 34 / 32767);
    check("LFO S&H: the dot at the right edge at the live value", fb[y * 240u + 235u] == C_WHITE);
    check("LFO 1/2 shows the routing and TRIG too (the line under the picture)",
          ui.page == page_first(FAM_LFO) && fb_lit(Y_GRAPH + G_OY + 84u, Y_GRAPH + G_OY + 96u) > 20u);
    snap_page("lfo/trail_SnH");
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    check("LFO trail: another track starts it over", ui.tr_n <= 1u);
    for (k = 0; k < 20u; k++)
        ui_frame();
    tap(B_OCTDN);
    check("LFO trail: switching to LFO 2 starts it over", ui.tr_n <= 1u);
}

/* the EDIT gauge marker: a modulated knob shows its live value */
static void test_lfo_edit_marker(void)
{
    int32_t mv = -1;
    ui_host_init();
    TSEL->p[P_LFO1 + LF_WAVE] = LW_SQUARE;
    TSEL->p[P_LFO1 + LF_MODE] = LM_HZ;
    TSEL->p[P_LFO1 + LF_RATE] = 60;
    TSEL->p[P_LFO1 + LF_DEST] = 2;                   /* DECAY */
    TSEL->p[P_LFO1 + LF_DEPTH] = 40;
    tap(B_EDIT);
    render_mix(0, 0, CTL * 50);
    check("EDIT: a modulated knob reports its live value (for the gauge marker)",
          lfo_live(TSEL, P_E1, &mv) && mv != TSEL->p[P_E1]);
    snap_page("lfo/90_edit_marker");
}

static void test_grid_keys(void)
{
    uint32_t a;
    ui_host_init();
    open_step_page();
    check("SEQ opens the STEP grid (keys belong to it)", str_eq(cur_page()->title, "STEP") && song.seq_mode);
    a = dvage;
    keys(1u << WHITE[2]);                            /* the third white key, A3 */
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: the third white key (A3) sets step 3 on, no drum hit",
          trk[0].step[2].on && !trk[0].step[2].acc && dvage == a);
    {
        uint32_t i, others = 0;
        for (i = 0; i < NSTEP; i++)                  /* only step 3 on: only its key is lit */
            trk[0].step[i].on = i == 2;
        ui_frame();
        for (i = 0; i < 27u; i++)
            others += i != WHITE[2] && led_lit(14u + i);
        check("grid: step 3's LED is the third white key's (A3), no other key lit", led_lit(14u + WHITE[2]) && !others);
    }
    keys(1u << WHITE[2]);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: a second tap turns the step off", !trk[0].step[2].on && !trk[0].step[2].acc);
    trk[0].p[P_SLEN] = 64;
    press(B_OCTUP);
    ui_frame();
    release_all();
    keys(1u << 0);
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: OCT+ moves to bank 2 (white key 1 = step 17)", ui.bank == 1 && trk[0].step[16].on);
    {
        uint32_t i, on0 = 0, on1 = 0;
        for (i = 0; i < NSTEP; i++)
            on0 += trk[0].step[i].on + trk[0].step[i].acc;
        keys(1u << 1 | 1u << 3 | 1u << 20 | 1u << 25);  /* black keys: F#3 G#3 C#5 F#5 */
        ui_frame();
        keys(0);
        ui_frame();
        for (i = 0; i < NSTEP; i++)
            on1 += trk[0].step[i].on + trk[0].step[i].acc;
        check("grid: black keys change no step", on0 == on1);
    }
    keys(1u << WHITE[15]);                           /* G5, the 16th white key: step 32 of bank 2 */
    ui_frame();
    keys(0);
    ui_frame();
    check("grid: the 16th white key (G5) is step 16 of the bank", trk[0].step[31].on);
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
    keys(1u << WHITE[10]);                           /* step 27 > LEN: ignored */
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
    ui_host_init();
    seq_open("PATTERN");
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();
    check("SEQ: REC tap opens TRACKS and arms nothing", !ui.home && cur_page()->fam == FAM_MIX && song.rec == 0u);
}

/* TRACKS: a white key selects its track; with OCT- held the keys show the playing tracks (lit) and mute / unmute
 * them (a mute also cuts what is ringing); OCT- + top C# / D# (MONO / POLY): mutes at once / on the next bar;
 * elsewhere the keys play again and the mutes stay */
/* upstream 1.0 (project.c "STOP TO SAVE"): a flash erase silences the audio and stalls the sequencer, so no project
 * is saved while playing, and a settings save waits until the transport stops */
static void test_no_save_while_playing(void)
{
    uint32_t k;
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    transport_req = 1;
    ui_frame();
    for (k = 0; k < 8u && (ui.home || page_id(cur_page(), 0) != G_SLOT); k++)
        tap(B_SAVE);
    turn(EN_K1 + 3, 1);                              /* SAVE: GO, and GO again */
    ui_frame();
    turn(EN_K1 + 3, 1);
    ui_frame();
    check("SAVE while playing: refused (STOP TO SAVE), the slot stays empty",
          song.playing && str_eq(ui.msg, "STOP TO SAVE") && !project_used(0));
    settings_save();
    check("a settings save while playing waits", ui.persist_pending == 1u);
    transport_req = 2;
    ui_frame();
    ui_frame();
    check("... and is written once stopped", !song.playing && ui.persist_pending == 0u);
    turn(EN_K1 + 3, 1);
    ui_frame();
    turn(EN_K1 + 3, 1);
    ui_frame();
    check("SAVE stopped: saved", project_used(0));
    memset(proj_slot, 0, sizeof proj_slot);
}

/* upstream 1.0 (#23 / #52): knob acceleration only for a turn going on in one direction over a wide range; the
 * first detents, a single quick detent (a bounce) and a reversal are one step each; at most x4; MENU switches it off */
static int32_t acc_at(int32_t s, int32_t range, uint32_t gap_ms)
{
    host_ticks += gap_ms * 1000u * FM1_TICKS_PER_US;
    return accel(EN_K1, s, range);
}

static void test_knob_accel(void)
{
    int32_t r[6];
    uint32_t k, ok = 1;
    ui_host_init();
    settings.accel = 1;
    for (k = 0; k < 4u; k++)
        ok &= acc_at(1, 127, 100) == 1;
    check("accel: a slow turn (100 ms a detent): one step per detent", ok);
    for (k = 0; k < 6u; k++)                         /* a new turn (after a pause), fast */
        r[k] = acc_at(1, 127, k ? 8u : 300u);
    printf("     accel, a new fast turn (8 ms a detent): %d %d %d %d %d %d\n", r[0], r[1], r[2], r[3], r[4], r[5]);
    check("accel: a fast turn speeds up after its first detents, at most x4",
          r[0] == 1 && r[1] == 1 && r[5] > 1 && r[5] <= 4);
    check("accel: a reversal is one step", acc_at(-1, 127, 8) == -1);
    check("accel: a single quick detent after a pause (a bounce) is one step", acc_at(1, 127, 300) == 1 && acc_at(1, 127, 8) == 1);
    for (k = 0, ok = 1; k < 6u; k++)
        ok &= acc_at(1, 32, 8) == 1;
    check("accel: a small range (32 or less) is never accelerated", ok);
    settings.accel = 0;
    for (k = 0, ok = 1; k < 6u; k++)
        ok &= acc_at(1, 127, 8) == 1;
    check("accel: KNOB ACCEL OFF: one step per detent", ok);
    settings.accel = 1;
    {
        const panel_t keep = panel;
        panel.btn[1] = panel.btn[0];                 /* a calibration with one button learned twice */
        panel_init();
        check("calibration: a button learned twice falls back to the default table", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
        panel = keep;
        panel.enc[2] = panel.enc[1];
        panel_init();
        check("calibration: a knob learned twice falls back to the default table", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
        panel = keep;
    }
}

static void test_quick_mute(void)
{
    uint32_t a, k, ok = 1, on = 0, off = 0;
    ui_host_init();
    settings.mutebar = 0;
    press(B_REC);                                    /* HOME: a REC tap opens TRACKS */
    ui_frame();
    release_all();
    ui_frame();
    trk[5].p[P_MUTE] = 1;
    a = hit_age(&trk[2]);
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    ui_frame();
    check("TRACKS: white key 3 (no OCT-) selects track 3, does not mute or play it",
          song.sel == 2 && trk[2].p[P_MUTE] == 0 && hit_age(&trk[2]) == a);
    for (k = 0; k < NTRK; k++)
        ok &= !led_lit(14u + KEY_TRK_KEY[k]);
    check("TRACKS without OCT-: the keys show no mutes", ok);
    press(B_OCTDN);                                  /* OCT- held from here */
    ui_frame();
    for (k = 0, ok = 1; k < NTRK; k++)
        ok &= led_lit(14u + KEY_TRK_KEY[k]) == (k != 5u);
    check("TRACKS, OCT- held: the keys light for the playing tracks, dark for the muted one", ok);
    snap_page("seq/23_tracks_mute_hold");
    drum_hit(&trk[2], 127);                          /* track 3 ringing */
    a = hit_age(&trk[2]);
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    for (k = 0; k < 5u; k++)
        ui_frame();
    check("OCT- + key 3 mutes track 3 (LED dark), does not play it, cuts its ringing voice",
          trk[2].p[P_MUTE] == 1 && hit_age(&trk[2]) == a && !trk[2].v[0].active && !trk[2].v[1].active &&
              !led_lit(14u + KEY_TRK_KEY[2]));
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    ui_frame();
    check("OCT- + key 3 again unmutes it (LED lit)", trk[2].p[P_MUTE] == 0 && led_lit(14u + KEY_TRK_KEY[2]));
    keys(1u << 22);                                  /* top D# (POLY) */
    ui_frame();
    keys(0);
    ui_frame();
    check("OCT- + top D#: mutes on the next bar", settings.mutebar == 1u && str_eq(ui.msg, "MUTE: NEXT BAR"));
    transport_req = 1;
    seq_play_to(0, 2);
    keys(1u << KEY_TRK_KEY[0]);
    ui_frame();
    keys(0);
    ui_frame();
    check("next bar: OCT- + key 1 while playing waits (track 1 not muted yet)", trk[0].p[P_MUTE] == 0);
    for (k = 0; k < 40u; k++) {                      /* 2 s, 50 ms frames */
        host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
        ui_frame();
        if (led_lit(14u + KEY_TRK_KEY[0]))
            on++;
        else
            off++;
    }
    check("the waiting mute's key blinks", on > 5u && off > 5u && trk[0].p[P_MUTE] == 0);
    seq_play_to(0, 15);
    check("still waiting on the bar's last step", trk[0].p[P_MUTE] == 0);
    seq_play_to(0, 0);
    check("at the bar (track 1's step 1): track 1 muted", trk[0].p[P_MUTE] == 1);
    keys(1u << 20);                                  /* top C# (MONO) */
    ui_frame();
    keys(0);
    ui_frame();
    check("OCT- + top C#: mutes at once", settings.mutebar == 0u && str_eq(ui.msg, "MUTE: NOW"));
    keys(1u << KEY_TRK_KEY[0]);
    ui_frame();
    keys(0);
    ui_frame();
    check("NOW: OCT- + key 1 while playing unmutes at once", trk[0].p[P_MUTE] == 0);
    transport_req = 2;
    ui_frame();
    settings.mutebar = 1;
    keys(1u << KEY_TRK_KEY[3]);
    ui_frame();
    keys(0);
    ui_frame();
    check("NEXT BAR but stopped: a mute is at once", trk[3].p[P_MUTE] == 1);
    settings.mutebar = 0;
    release_all();
    ui_frame();
    press(B_HOME);                                   /* HOME: keys play again; track 6 stays muted */
    ui_frame();
    release_all();
    ui_frame();
    a = hit_age(&trk[0]);
    keys(1u << KEY_TRK_KEY[0]);
    ui_frame();
    keys(0);
    ui_frame();
    check("leaving TRACKS: the keys play again, the mutes stay", hit_age(&trk[0]) != a && trk[5].p[P_MUTE] == 1 &&
                                                                 trk[0].p[P_MUTE] == 0);
}

/* TRACKS while a track is armed: the white keys play (and record) their tracks instead of selecting; OCT- held:
 * they still mute; disarmed: they select again */
static void test_tracks_rec_keys(void)
{
    uint32_t a, k, n = 0;
    ui_host_init();
    settings.mutebar = 0;
    for (k = 0; k < 16u; k++)
        trk[1].step[k].on = 0;
    press(B_REC);                                    /* HOME: REC tap opens TRACKS */
    ui_frame();
    release_all();
    ui_frame();
    turn(EN_K1, 1);                                  /* track 2 */
    ui_frame();
    press(B_REC);                                    /* arms track 2, starts play */
    ui_frame();
    release_all();
    ui_frame();
    seq_play_to(1, 3);
    a = hit_age(&trk[1]);
    keys(1u << KEY_TRK_KEY[1]);
    ui_frame();
    keys(0);
    ui_frame();
    for (k = 0; k < 16u; k++)
        n += trk[1].step[k].on;
    check("TRACKS, armed: key 2 plays track 2 and records it", (song.rec & 2u) && hit_age(&trk[1]) != a && n == 1u);
    a = hit_age(&trk[3]);
    keys(1u << KEY_TRK_KEY[3]);
    ui_frame();
    keys(0);
    ui_frame();
    check("TRACKS, armed: key 4 plays track 4, the selection stays", song.sel == 1 && hit_age(&trk[3]) != a);
    press(B_OCTDN);                                  /* OCT- held */
    ui_frame();
    a = hit_age(&trk[3]);
    keys(1u << KEY_TRK_KEY[3]);
    ui_frame();
    keys(0);
    ui_frame();
    check("TRACKS, armed, OCT- held: key 4 mutes track 4, no sound", trk[3].p[P_MUTE] == 1 && hit_age(&trk[3]) == a);
    release_all();
    ui_frame();
    press(B_REC);                                    /* disarm */
    ui_frame();
    release_all();
    ui_frame();
    a = hit_age(&trk[5]);
    keys(1u << KEY_TRK_KEY[5]);
    ui_frame();
    keys(0);
    ui_frame();
    check("TRACKS, disarmed: key 6 selects track 6, no sound", song.rec == 0 && song.sel == 5 && hit_age(&trk[5]) == a);
}

/* FX: RESON 1/2 and 2/2 after SLICER; STRCT is CHORD on CHORD; CHORD on 2 tracks at most (knob and load) */
static void test_reson_pages(void)
{
    uint32_t k, n;
    ui_host_init();
    for (k = 0; k < 8u && (ui.home || page_id(cur_page(), 0) != P_RMODEL); k++)
        tap(B_FX);
    check("FX: RESON 1/2 after SLICER (MODEL TUNE DECAY MIX)",
          str_eq(cur_page()->title, "RESON") && page_id(cur_page(), 0) == P_RMODEL && page_id(cur_page(), 3) == P_RMIX &&
              str_eq(PAGES[ui.page - 1u].title, "SLICER"));
    turn(EN_K1, 1);
    ui_frame();
    check("RESON: KNOB 1 picks STRNG", TSEL->p[P_RMODEL] == RS_STRNG);
    keys(1u << KEY_TRK_KEY[0]);                      /* play it: the ring for the picture */
    ui_frame();
    keys(0);
    for (k = 0; k < 10u; k++)
        ui_frame();
    snap_page("reson/01_strng");
    tap(B_FX);
    check("FX again: RESON 2/2 (TONE STRCT POS)", page_id(cur_page(), 0) == P_RTONE && page_id(cur_page(), 1) == P_RSTRCT);
    TSEL->p[P_RMODEL] = RS_CHORD;
    ui_frame();
    {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), 1, &vp);
        check("RESON 2/2 on CHORD: the second knob is CHORD", d && str_eq(d->label, "CHORD"));
    }
    snap_page("reson/02_chord_page2");
    trk[1].p[P_RMODEL] = RS_CHORD;                   /* tracks 1 and 2 CHORD: track 3 cannot */
    trk[2].p[P_RMODEL] = RS_PIPE;
    for (k = 0; k < 8u && (ui.home || page_id(cur_page(), 0) != P_RMODEL); k++)
        tap(B_FX);                                   /* round the FX pages back to RESON 1/2 */
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    ui_frame();
    turn(EN_K1, 1);
    ui_frame();
    check("RESON CHORD cap: a 3rd track's MODEL knob goes past CHORD (full) to MODAL",
          song.sel == 2 && trk[2].p[P_RMODEL] == RS_MODAL);
    snap_page("reson/03_chord_cap");
    turn(EN_K1, -1);
    ui_frame();
    check("RESON CHORD cap: back down, past CHORD (full) to PIPE", trk[2].p[P_RMODEL] == RS_PIPE);
    trk[1].p[P_RMODEL] = RS_STRNG;                   /* a place frees */
    turn(EN_K1, 1);
    ui_frame();
    check("RESON CHORD cap: once a place frees, the 3rd track gets CHORD", trk[2].p[P_RMODEL] == RS_CHORD);
    for (k = 0; k < NTRK; k++)                       /* RESON on 4 tracks at most: a 5th track's knob stays OFF */
        trk[k].p[P_RMODEL] = (int16_t)(k < 4u ? RS_STRNG : RS_OFF);
    keys(1u << KEY_TRK_KEY[4]);
    ui_frame();
    keys(0);
    ui_frame();
    turn(EN_K1, 1);
    ui_frame();
    check("RESON on 4 tracks at most: a 5th track's MODEL knob stays OFF, with the message",
          song.sel == 4 && trk[4].p[P_RMODEL] == RS_OFF && str_eq(ui.msg, "RESON: 4 TRACKS MAX"));
    trk[0].p[P_RMODEL] = RS_OFF;                     /* a place frees */
    turn(EN_K1, 1);
    ui_frame();
    check("RESON on 4 tracks at most: once a place frees, the 5th gets it", trk[4].p[P_RMODEL] == RS_STRNG);
    for (k = 0; k < NTRK; k++)                       /* a project with 8 CHORD tracks loads with 2 CHORD + 2 STRNG */
        trk[k].p[P_RMODEL] = RS_CHORD;
    project_save(0);
    project_load(0);
    for (k = 0, n = 0; k < NTRK; k++)
        n += trk[k].p[P_RMODEL] == RS_CHORD;
    check("RESON caps on load: the first two keep CHORD, the next two play STRNG, the rest OFF (4 tracks at most)",
          n == 2u && trk[0].p[P_RMODEL] == RS_CHORD && trk[1].p[P_RMODEL] == RS_CHORD && trk[2].p[P_RMODEL] == RS_STRNG &&
              trk[3].p[P_RMODEL] == RS_STRNG && trk[4].p[P_RMODEL] == RS_OFF && trk[7].p[P_RMODEL] == RS_OFF);
    memset(&proj_slot[0], 0, sizeof proj_slot[0]);  /* the slot empty again for the project tests */
}

/* HOME: a white key selects its track and plays it; an LFO on a HOME knob shows its live value on the gauge */
static void test_home_keys_and_lfo(void)
{
    uint32_t a, x, y, amb;
    ui_host_init();
    a = hit_age(&trk[3]);
    keys(1u << KEY_TRK_KEY[3]);
    ui_frame();
    keys(0);
    ui_frame();
    check("HOME: white key 4 selects track 4 and plays it", ui.home && song.sel == 3 && hit_age(&trk[3]) != a);
    for (y = Y_GAUGE - 6u, amb = 0; y < Y_SEP_END; y++)
        for (x = 64; x < 120u; x++)   /* column 2 */
            amb += fb[y * 240u + x] == C_AMB;
    check("HOME: no LFO, no marker on the gauges", amb == 0u);
    TSEL->p[P_LFO1 + LF_WAVE] = LW_SQUARE;
    TSEL->p[P_LFO1 + LF_MODE] = LM_HZ;
    TSEL->p[P_LFO1 + LF_RATE] = 60;
    TSEL->p[P_LFO1 + LF_DEST] = 2;                   /* the engine's 2nd knob: HOME's second column */
    TSEL->p[P_LFO1 + LF_DEPTH] = 40;
    for (a = 0; a < 50u; a++)
        ui_frame();
    for (y = Y_GAUGE - 6u, amb = 0; y < Y_SEP_END; y++)
        for (x = 64; x < 120u; x++)   /* column 2 */
            amb += fb[y * 240u + x] == C_AMB;
    check("HOME: an LFO on the second knob marks its live value on that gauge", amb > 0u);
    snap_page("lfo/91_home_marker");
}

/* GLOBAL 3/3: MUTE NOW / BAR, the same device setting as OCT- + C# / D# on TRACKS */
static void test_global_mute_setting(void)
{
    uint32_t k;
    ui_host_init();
    settings.mutebar = 0;
    for (k = 0; k < 6u && (ui.home || cur_page()->id[0] != G_MUTEBAR); k++)
        tap(B_GLO);
    check("GLO reaches GLOBAL 3/3 with MUTE", !ui.home && str_eq(cur_page()->title, "GLOBAL") &&
                                                  cur_page()->id[0] == G_MUTEBAR && ui.page == page_first(FAM_GLO) + 2u);
    turn(EN_K1, 1);
    ui_frame();
    check("GLOBAL 3/3: KNOB 1 -> BAR (the setting)", settings.mutebar == 1u);
    snap_page("global_3_mute");
    turn(EN_K1, -1);
    ui_frame();
    check("GLOBAL 3/3: KNOB 1 back -> NOW", settings.mutebar == 0u);
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

/* the start screen (main.c fm1_main): the HortatoR logo and DRUM MACHINE, nothing above or below */
static void test_boot_title(void)
{
    memset(fb, 0, sizeof fb);
    draw_boot_title();
    shot("build/ui_shots/00_boot.ppm");
    check("boot: the logo", fb_lit(BOOT_Y, BOOT_Y + LOGO_H) > 3000);
    check("boot: DRUM MACHINE under it", fb_lit(BOOT_Y + LOGO_H, BOOT_Y + LOGO_H + 24) > 100);
    check("boot: nothing else", fb_lit(0, BOOT_Y) + fb_lit(BOOT_Y + LOGO_H + 24, 240) == 0);
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
        if (page_hidden(i))                          /* EDIT page 4: no engine has that many knobs yet */
            continue;
        char *d;
        const char *c;
        for (k = 0; k < NPAGES && (cur_page() != pg || ui.home); k++) {   /* its button until it shows */
            if (pg->fam == FAM_MIX || pg->fam == FAM_HOME) {
                press(pg->fam == FAM_MIX ? B_REC : B_HOME);   /* REC opens TRACKS, HOME steps to COMP, on release */
                ui_frame();
                release_all();
            } else if (pg->fam == FAM_LAY) {         /* the layer: OCT+ from EDIT, then EDIT steps */
                press(ui.home || (cur_page()->fam != FAM_SND && cur_page()->fam != FAM_LAY) ? B_EDIT
                      : cur_page()->fam == FAM_SND ? B_OCTUP : B_EDIT);
            } else {
                press(FAM_BTN[pg->fam]);
            }
            ui_frame();
            release_all();
            ui_frame();                              /* (FX opens its pages on the release) */
        }
        reach &= cur_page() == pg && !ui.home;
        snprintf(name, sizeof name, "%02u_", (unsigned)++n);
        for (d = name + 3, c = pg->title; *c && j < 20u; c++, j++)
            *d++ = *c == '/' ? '-' : (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
        *d = 0;
        if (nth && str_eq(PAGES[i - 1u].title, pg->title)) {   /* SOUND 2/3, LAYER 2/2, COMP 2/2: the n-th of its title */
            uint32_t same = 1, q;
            for (q = page_first(pg->fam); q < i; q++)
                same += str_eq(PAGES[q].title, pg->title);
            snprintf(d, sizeof name - (size_t)(d - name), "_%u", (unsigned)same);
        }
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
    check("ABOUT: the HortatoR logo on top", fb_lit(H_HEAD + 1, H_HEAD + 1 + LOGO_H) > 3000);
    press(B_OCTDN);
    ui_frame();
    release_all();
    press(B_OCTDN);
    ui_frame();
    release_all();
    seq_open("STEP");                                /* (SEQ comes back to SONG, the last SEQ page: STEP) */
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
    for (k = 0; k < 4; k++)                          /* (not MODEL: it would change the engine) */
        if (page_id(cur_page(), k) != P_MODEL)
            turn(EN_K1 + k, -999);
    ui_frame();
    snprintf(name, sizeof name, "engines/%02u_%s_%u_min", (unsigned)mi, low, (unsigned)pg + 1u);
    snap_page(name);
    for (k = 0; k < 4; k++)
        if (page_id(cur_page(), k) != P_MODEL)
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
        for (k = 0; k < 2u * NMODELS && (uint32_t)TSEL->p[P_MODEL] != mi; k++)   /* the next engine */
            model_step(1);
        for (k = 0; k < 4u; k++)                     /* every EDIT page this engine shows */
            if (!page_hidden(page_first(FAM_SND) + k))
                engine_page_shots(mi, page_first(FAM_SND) + k);
        n++;
    }
    printf("     engines: %u engines, SOUND pages at default / min / max in build/ui_shots/engines\n", (unsigned)n);
}

/* ---- the sequencer at work, for looking at: build/ui_shots/seq/ (no checks: the user looks) */
static void seq_play_to(uint32_t ti, uint32_t step)  /* play until track ti's playhead is on step */
{
    uint32_t k;
    for (k = 0; k < 4000u && !(song.playing && trk[ti].seq_idx == step); k++)
        ui_frame();
}

static void seq_open(const char *title)              /* SEQ until the page shows (STEP, PATTERN) */
{
    uint32_t k;
    for (k = 0; k < NPAGES && (!str_eq(cur_page()->title, title) || ui.home); k++) {
        press(B_SEQ);
        ui_frame();
        release_all();
        ui_frame();
    }
}

static void seq_select(uint32_t ti)                  /* the track encoder, one detent at a time */
{
    uint32_t k;
    for (k = 0; k < NTRK && song.sel != ti; k++) {
        turn(EN_ALGO, ti > song.sel ? 1 : -1);
        ui_frame();
    }
}

static void seq_snap_at(uint32_t ti, uint32_t step, const char *name)
{
    char path[64];
    seq_play_to(ti, step);
    snprintf(path, sizeof path, "seq/%s", name);
    snap_page(path);
}

static void test_seq_screens(void)
{
    static const uint32_t S[4] = {0, 4, 8, 12};
    char name[48];
    uint32_t i, k;
    ui_host_init();
    kit_session();
    song.rec = 0;
    trk[5].p[P_SLEN] = 32;                           /* TOM: two banks, a second-bar fill */
    for (k = 16; k < 32; k++)
        trk[5].step[k].on = k == 20 || k == 26 || k >= 29;
    trk[5].step[31].acc = 1;
    trk[4].p[P_SLEN] = 12;                           /* HATO: a 12-step loop, swung */
    trk[4].p[P_SSWING] = 60;
    seq_open("STEP");
    snap_page("seq/01_step_t1_stopped");
    transport_req = 1;
    for (i = 0; i < 4; i++) {                        /* the playhead walks the kick's bar */
        snprintf(name, sizeof name, "%02u_step_t1_play_step%02u", (unsigned)(2 + i), (unsigned)S[i] + 1u);
        seq_snap_at(0, S[i], name);
    }
    seq_select(3);                                   /* HATC: 8ths with gaps */
    seq_snap_at(3, 6, "06_step_t4_hats_step07");
    seq_select(5);                                   /* TOM, 32 steps: bank 2 while its playhead is there */
    press(B_OCTUP);
    ui_frame();
    release_all();
    seq_snap_at(5, 26, "07_step_t6_bank2_step27");
    press(B_OCTDN);
    ui_frame();
    release_all();
    seq_open("PATTERN");
    seq_snap_at(5, 29, "08_pattern_t6_len32_step30");
    seq_select(4);                                   /* HATO: LEN 12, swing 60 */
    seq_snap_at(4, 9, "09_pattern_t5_len12_swing_step10");
    seq_select(0);
    seq_snap_at(0, 4, "10_pattern_t1_step05");
    press(B_HOME);                                   /* REC tap off the SEQ pages: the TRACKS mixer */
    ui_frame();
    release_all();
    ui_frame();
    press(B_REC);
    ui_frame();
    release_all();
    ui_frame();
    seq_snap_at(0, 2, "11_tracks_step03");
    seq_snap_at(0, 13, "12_tracks_step14");
    press(B_HOME);
    ui_frame();
    release_all();
    ui_frame();
    seq_snap_at(0, 8, "13_home_play_step09");
    for (k = 0; k < 16; k++)                         /* live recording: the snare's steps cleared, track 2 armed */
        trk[1].step[k].on = trk[1].step[k].acc = 0;
    seq_select(1);
    seq_open("STEP");
    seq_snap_at(1, 0, "14_rec_t2_before");
    seq_open("PATTERN");                             /* off the grid: the keys play (and record) the drums */
    song.rec = 1u << 1;
    for (i = 0; i < 4; i++) {                        /* a player hits the snare key on steps 3, 7, 11, 15 */
        seq_play_to(1, 2 + 4 * i);
        keys(1u << KEY_TRK_KEY[1]);
        ui_frame();
        keys(0);
        ui_frame();
    }
    seq_snap_at(1, 0, "15_rec_t2_pattern_after");
    seq_open("STEP");
    seq_snap_at(1, 1, "16_rec_t2_step_after");
    transport_req = 2;
    for (k = 0; k < 8; k++)
        ui_frame();
    snap_page("seq/17_step_t2_stopped_after");
    printf("     sequencer: screens in build/ui_shots/seq\n");
}

static void test_project_roundtrip(void)
{
    ui_host_init();
    drum_set_model(&trk[2], DM_CONGA);
    trk[2].p[P_E0] = -5;
    trk[2].p[P_LLEVEL] = 77;
    trk[2].step[7].on = trk[2].step[7].acc = 1;
    trk[2].p[P_SLEN] = 23;
    trk[2].step[7].cond = 33;                        /* 3/5 */
    trk[2].step[7].rat = 2;                          /* 3 hits */
    trk[2].p[P_SRC] = 2;                             /* G-SNR */
    song.g[G_GMODE] = 1;
    song.g[G_GX] = 99;
    song.g[G_GLEN3] = 5;
    trk[3].p[P_DUCK] = 1;
    song.g[G_CSRC] = 1;
    song.g[G_CRAT] = 99;
    song.g[G_CMKUP] = 50;
    song.g[G_CKNEE] = 0;
    trk[4].p[P_LFO2 + LF_WAVE] = LW_RWALK;
    trk[4].p[P_LFO2 + LF_DEST] = 9;
    trk[4].p[P_LFO2 + LF_DEPTH] = -20;
    trk[4].p[P_LFO2 + LF_MODE] = LM_TIME;
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
    check("project: load restores PROB / RATCH, SRC and the Grids settings",
          trk[2].step[7].cond == 33 && trk[2].step[7].rat == 2 && trk[2].p[P_SRC] == 2 && song.g[G_GMODE] == 1 &&
              song.g[G_GX] == 99 && song.g[G_GLEN3] == 5);
    check("project: load restores DUCK and the COMP settings",
          trk[3].p[P_DUCK] == 1 && song.g[G_CSRC] == 1 && song.g[G_CRAT] == 99 && song.g[G_CMKUP] == 50 &&
              song.g[G_CKNEE] == 0);
    check("project: load restores the LFOs",
          trk[4].p[P_LFO2 + LF_WAVE] == LW_RWALK && trk[4].p[P_LFO2 + LF_DEST] == 9 &&
              trk[4].p[P_LFO2 + LF_DEPTH] == -20 && trk[4].p[P_LFO2 + LF_MODE] == LM_TIME);
}

static void test_project_rejects(void)
{
    ui_host_init();
    project_save(0);
    proj_slot[0].t[3].p[P_MODEL] = 99;               /* corrupt the stored data, fix the checksum */
    proj_slot[0].t[3].p[P_E1] = 30000;
    proj_slot[0].t[3].step[0].on = 7;
    proj_slot[0].t[3].step[1].cond = 200;
    proj_slot[0].t[3].step[1].rat = 9;
    proj_slot[0].t[3].p[P_SRC] = 40;
    proj_slot[0].g[G_GLEN1] = -5;
    proj_slot[0].g[G_CSRC] = 77;
    proj_slot[0].t[3].p[P_DUCK] = 5;
    proj_slot[0].t[3].p[P_LFO1 + LF_DEST] = 99;
    proj_slot[0].t[3].p[P_LFO1 + LF_WAVE] = -4;
    proj_slot[0].t[3].p[P_RMODEL] = 99;
    proj_slot[0].t[3].p[P_RTUNE] = 500;
    proj_slot[0].t[3].p[P_RDECAY] = -9;
    proj_slot[0].sum = proj_sum(&proj_slot[0]);
    project_load(0);
    check("project: out-of-range values are clamped on load",
          trk[3].p[P_MODEL] < NMODELS && trk[3].p[P_E1] <= DMODELS[trk[3].p[P_MODEL]].edit[1].max &&
              trk[3].step[0].on == 1 && trk[3].step[1].cond == COND_MAX && trk[3].step[1].rat == 3 &&
              trk[3].p[P_SRC] == 3 && song.g[G_GLEN1] == 1 &&
              song.g[G_CSRC] == 8 && trk[3].p[P_DUCK] == 1 &&
              trk[3].p[P_LFO1 + LF_DEST] == TP[P_LFO1 + LF_DEST].max && trk[3].p[P_LFO1 + LF_WAVE] == 0 &&
              trk[3].p[P_RMODEL] == RS_NMODEL - 1 && trk[3].p[P_RTUNE] == 96 && trk[3].p[P_RDECAY] == 0);
    proj_slot[1].magic = 0x46554E33u;                /* an old Felucca project ("FUN3") */
    check("project: Felucca projects are not used", !project_used(1));
    project_save(2);
    proj_slot[2].sum ^= 1;
    check("project: a bad checksum is not used", !project_used(2));
}

/* SEQ held at power-on: a silent start with USB intact (spec §4) */
static void safe_boot(int seq_held)
{
    ui_host_init();
    fm1_in.buttons = seq_held ? 1u << panel.btn[B_SEQ] : 0u;
    host_scans = 0;
    drum_boot_init();
    fm1_in.buttons = 0;
}

static void test_safe_start(void)
{
    uint32_t i, peak = 0, a;
    safe_boot(1);
    check("safe start: SEQ held through the boot's polled scan (FM1_DEB_RELEASE + 4 scans)",
          safe_start && host_scans == FM1_DEB_RELEASE + 4u);
    a = dvage;
    keys(1u << KEY_TRK_KEY[0]);                      /* a white key */
    ui_frame();
    keys(0);
    midi_in_q[mi_w % MQ] = 0x09u | (0x90u | 9u) << 8 | (uint32_t)trk[1].p[P_NOTE] << 16 | 100u << 24;
    mi_w++;                                          /* a USB-MIDI note on the drum channel (as drum_test's midi_in) */
    drum_hit(&trk[2], 127);                          /* the editor's audition path */
    for (i = 0; i < 40; i++) {
        ui_frame();
        peak |= (uint32_t)abs(mixo[0]) | (uint32_t)abs(mixo[1]);
    }
    check("safe start: keys, MIDI and direct hits start no voice; the mix is silent", dvage == a && peak == 0);
    snap_page("safe_start");
    check("safe start: the SAFE START screen draws", fb_lit(90, 160) > 200);
    for (i = 0; i < SMP_USER_SLOTS; i++)
        peak |= usr_nz[i];
    check("safe start: user sample slots unusable", peak == 0);
    safe_boot(0);
    check("normal start: SEQ not held: no safe mode, the same scans", !safe_start && host_scans == FM1_DEB_RELEASE + 4u);
    {
        panel_t keep = panel;                        /* a recalibrated panel: SEQ on another matrix id */
        uint8_t t = panel.btn[B_SEQ];
        panel.btn[B_SEQ] = panel.btn[B_PLAY];
        panel.btn[B_PLAY] = t;
        safe_boot(1);
        check("safe start: SEQ found through a learned panel map", safe_start);
        panel = keep;
    }
    safe_boot(1);                                    /* an update cancelled in safe start: main.c clears the */
    ui_frame();                                      /* screen and sets ui.force; the safe screen must come back */
    memset(fb, 0, sizeof fb);
    ui.force = 1;
    ui_frame();
    check("safe start: the screen is redrawn after an update session (ui.force)", fb_lit(90, 160) > 200 && !ui.force);
    safe_boot(0);                                    /* leave the other tests a normal start */
}

/* H4: the heaviest page's frame (TRACKS while playing, 8 rows) is far inside the 8 s watchdog */
static void test_ui_frame_cost(void)
{
    uint64_t i0, worst = 0, c;
    uint32_t i;
    ui_host_init();
    kit_session();
    transport_req = 1;
    press(B_REC);                                    /* REC tap on HOME: the TRACKS mixer */
    ui_frame();
    release_all();
    for (i = 0; i < 60; i++) {
        i0 = instr_now();
        ui_input();
        ui_leds();
        ui_draw();
        c = instr_now() - i0;
        worst = c > worst ? c : worst;
        render_mix(0, 0, CTL);
    }
    printf("     ui: heaviest frame %llu host instructions (TRACKS, playing)\n", (unsigned long long)worst);
    check("ui frame cost: under 4,000,000 host instructions (8 s watchdog, 100x margin)", !i0 || worst < 4000000u);
}

/* a white key on a per-track page (SOUND, TRACK, MIDI, LAYER, FX, SLICER) selects its track and still plays it;
 * HOME, the STEP grid, PATTERN and the global pages keep the selection */
static int key_selects(const char *title, uint32_t fam_btn, uint32_t presses)
{
    uint32_t a, i;
    ui_host_init();
    for (i = 0; i < presses; i++) {
        press(fam_btn);
        ui_frame();
        release_all();
        ui_frame();
    }
    if (!str_eq(cur_page()->title, title)) {
        printf("     (could not open %s, on %s)\n", title, cur_page()->title);
        return -1;
    }
    a = hit_age(&trk[2]);
    keys(1u << KEY_TRK_KEY[2]);                      /* the third white key: track 3 */
    ui_frame();
    keys(0);
    ui_frame();
    return song.sel == 2 && hit_age(&trk[2]) != a ? 1 : song.sel == 0 ? 0 : -1;
}

static void test_key_selects_track(void)
{
    static const struct { const char *title; uint32_t btn, presses; int want; } C[] = {
        {"SOUND", B_EDIT, 1, 1},
        {"FILTER", B_FX, 1, 1}, {"FX", B_FX, 3, 1}, {"SLICER", B_FX, 4, 1}, {"RESON", B_FX, 5, 1}, {"DLY", B_FX, 7, 0},
        {"PATTERN", B_SEQ, 2, 0},
    };
    uint32_t i, ok = 1;
    for (i = 0; i < sizeof C / sizeof C[0]; i++) {
        int got = key_selects(C[i].title, C[i].btn, C[i].presses);
        if (got != C[i].want) {
            printf("     %s: key 3 %s\n", C[i].title, got == 1 ? "selected T3" : got == 0 ? "kept T1" : "unexpected");
            ok = 0;
        }
    }
    check("a white key on a per-track page selects its track (and plays it); global / sequencer pages keep it", ok);
    ui_host_init();                                  /* HOME: keys select (and play), as SOUND / FX */
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    check("HOME: a white key selects its track", song.sel == 2);
}

/* MENU > SPEAKER EQ (sound pack): KNOB 1 steps FLAT LOWCUT BASS+ and stops at the ends, OCT+ steps and wraps;
 * the master follows (fx_lowcut); a stored value past BASS+ loads as FLAT */
static void test_speaker_eq(void)
{
    static const int8_t TURN[6] = {1, 1, 1, -1, -1, -1};
    static const uint8_t WANT[6] = {1, 2, 2, 1, 0, 0};
    uint32_t ok = 1, i;
    ui_host_init();
    settings.lowcut = 0;
    fx_lowcut = 0;
    ui.menu = 1;
    ui.menu_sel = MI_SPKEQ;
    ui.force = 1;
    for (i = 0; i < 6u; i++) {
        turn(EN_K1, TURN[i]);
        ui_frame();
        ok &= settings.lowcut == WANT[i] && fx_lowcut == WANT[i];
    }
    check("SPEAKER EQ: KNOB 1 steps FLAT LOWCUT BASS+, stops at the ends; the master follows", ok);
    ok = 1;
    for (i = 0; i < 4u; i++) {
        press(B_OCTUP);
        ui_frame();
        release_all();
        ui_frame();
        ok &= settings.lowcut == (i + 1u) % 3u && fx_lowcut == (i + 1u) % 3u;
    }
    check("SPEAKER EQ: OCT+ steps and wraps", ok);
    settings.lowcut = 2;
    fx_lowcut = 2;
    ui.force = 1;
    snap_page("menu_speaker_eq");
    settings.lowcut = 3;
    settings_init();
    check("SPEAKER EQ: a stored value past BASS+ loads as FLAT", settings.lowcut == 0 && fx_lowcut == 0);
    ui.menu = 0;
    ui.force = 1;
}


/* sound pack: REV/CHO split into REVERB (TYPE SIZE DAMP) and CHORUS (RATE DEPTH); FX reaches both, TYPE turns
 * ROOM / SPRING; screens for the user */
static void fx_open(const char *title)              /* FX until the page shows */
{
    uint32_t k;
    for (k = 0; k < NPAGES && (!str_eq(cur_page()->title, title) || ui.home); k++) {
        press(B_FX);
        ui_frame();
        release_all();
        ui_frame();
    }
}
static void test_reverb_pages(void)
{
    ui_host_init();
    fx_open("REVERB");
    check("FX reaches REVERB: TYPE SIZE DAMP", str_eq(cur_page()->title, "REVERB") && cur_page()->id[0] == G_RTYPE &&
          cur_page()->id[1] == G_RSIZE && cur_page()->id[2] == G_RDAMP && cur_page()->id[3] == 0xFF);
    turn(EN_K1, 1);
    ui_frame();
    check("REVERB: KNOB 1 turns TYPE to SPRING", song.g[G_RTYPE] == 1);
    check("REVERB: the TYPE names fit the 5-character value (ROOM, SPRNG)",
          strlen(N_RTYPE[0]) <= 5u && strlen(N_RTYPE[1]) <= 5u && str_eq(N_RTYPE[1], "SPRNG"));
    snap_page("fx_reverb_spring");
    fx_open("CHORUS");
    check("FX reaches CHORUS: RATE DEPTH", str_eq(cur_page()->title, "CHORUS") && cur_page()->id[0] == G_CRATE &&
          cur_page()->id[1] == G_CDEPTH && cur_page()->id[2] == 0xFF && cur_page()->id[3] == 0xFF);
    snap_page("fx_chorus");
}

/* sound pack: division knobs run by length (4BAR .. 1/32), the stored value stays the id; the gauge follows */
static void test_div_order(void)
{
    static const int16_t WANT[10] = {9, 8, 7, 6, 0, 1, 4, 2, 5, 3};
    const param_desc_t *d = &TP[P_SDIV];
    uint32_t i, ok = 1;
    for (i = 0; i + 1u < 10u; i++)
        ok &= param_turn(d, WANT[i], 1) == WANT[i + 1] && param_turn(d, WANT[i + 1], -1) == WANT[i];
    ok &= param_turn(d, 3, 1) == 3 && param_turn(d, 9, -1) == 9 && param_turn(d, 9, 20) == 3;
    check("DIV: the knob steps 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32, stopping at the ends", ok);
    check("DLY TIME: the same order", param_turn(&GP[G_DTIME], 6, 1) == 0 && param_turn(&GP[G_DTIME], 0, -1) == 6);
    check("DIV gauge: by length (4BAR empty, 1/32 full, 1/4 at 4/9)",
          RATIO(d, 9) == 0 && RATIO(d, 3) == 1000 && RATIO(d, 0) == 444);
    check("other knobs keep their order (SLICER RATE, ids as shown)", param_turn(&TP[P_SLRATE], 1, 1) == 2);
    ui_host_init();
    settings.accel = 0;
    seq_open("PATTERN");
    trk[song.sel].p[P_SDIV] = 9;
    ok = 1;
    for (i = 1; i < 10u; i++) {
        turn(EN_K2, 1);
        ui_frame();
        ok &= trk[song.sel].p[P_SDIV] == WANT[i];
    }
    check("PATTERN DIV: turning right walks the length order", ok);
    trk[song.sel].p[P_SDIV] = 8;
    snap_page("seq/pattern_div_2bar");
    settings.accel = 1;
}

/* MENU > USB LEVEL (USB audio): KNOB 1 right FIXED / left MASTER, OCT+ toggles; the mix follows (fx_usb_fixed); the
 * menu's eight rows for the user's eye */
static void test_usb_level(void)
{
    uint32_t ok = 1;
    ui_host_init();
    settings.usbfix = 0;
    fx_usb_fixed = 0;
    ui.menu = 1;
    ui.menu_sel = MI_USB;
    ui.force = 1;
    turn(EN_K1, 1);
    ui_frame();
    ok &= settings.usbfix == 1 && fx_usb_fixed == 1;
    turn(EN_K1, -1);
    ui_frame();
    ok &= settings.usbfix == 0 && fx_usb_fixed == 0;
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    ok &= settings.usbfix == 1 && fx_usb_fixed == 1;
    check("USB LEVEL: KNOB 1 right FIXED / left MASTER, OCT+ toggles; the mix follows", ok);
    ui.force = 1;
    snap_page("menu_usb_level");
    settings.usbfix = 0;
    fx_usb_fixed = 0;
    ui.menu = 0;
    ui.force = 1;
}

/* MIDI clock: GLOBAL with CLK USB, and the BPM knob while following (a message, the tempo kept) */
static void test_clock_screens(void)
{
    int16_t bpm;
    ui_host_init();
    while (!str_eq(cur_page()->title, "GLOBAL") || ui.home) {
        press(B_GLO);
        ui_frame();
        release_all();
        ui_frame();
    }
    song.g[G_CLOCK] = 1;
    ui.force = 1;
    snap_page("global_clk_usb");
    bpm = song.g[G_BPM];
    turn(EN_SELECT, 3);
    ui_frame();
    check("CLK USB: the BPM knob keeps the tempo (the clock's) and says so", song.g[G_BPM] == bpm && ui.msg_t);
    song.g[G_CLOCK] = 0;
    render_mix(0, 0, CTL);
}

/* PERFORM: FX tapped opens its pages on the release; held, the layer (keys, knobs, LEDs) */
static uint32_t fx_bit(void) { return 1u << panel.btn[B_FX]; }
static void fx_hold_frames(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        fm1_in.buttons |= fx_bit();
        ui_frame();
    }
}
static void test_perform_gesture(void)
{
    uint32_t fam;
    ui_host_init();
    press(B_FX);
    ui_frame();
    check("FX pressed: nothing yet (the page waits for the release)", ui.home);
    release_all();
    ui_frame();
    check("FX tapped: the FX pages on the release", !ui.home && cur_fam() == FAM_FX && !ui.layer);
    fam = cur_page() - PAGES;
    press(B_FX);
    fx_hold_frames(500);                              /* well past 0.4 s (1 ms a frame) */
    check("FX held alone: the layer's map", ui.layer == 1u);
    release_all();
    ui_frame();
    check("FX let go after the hold: no page change, the map gone", (uint32_t)(cur_page() - PAGES) == fam && !ui.layer);
    press(B_FX);
    ui_frame();
    turn(EN_K2, 5);
    fx_hold_frames(1);
    check("FX + KNOB 2: CRUSH, the layer open at once", perf_k[1] > 0 && ui.layer == 1u);
    release_all();
    ui_frame();
    check("FX let go: the macros back to off, no page change", !perf_k[1] && (uint32_t)(cur_page() - PAGES) == fam);
}

static void test_perform_keys(void)
{
    uint32_t sel;
    ui_host_init();
    sel = song.sel;
    press(B_FX);
    ui_frame();
    keys(1u << 4);                                    /* A3: track 3's key */
    fx_hold_frames(2);                                /* (the render takes the key after the UI's pass) */
    check("FX + a track key: no track select, the layer open, track 3 muted",
          song.sel == sel && ui.layer == 1u && ((perf_held >> (PF_M1 + 2u)) & 1u));
    check("LEDs: the muted track's key dark", !led_on(14u + 4u));
    keys(0);
    fx_hold_frames(1);
    release_all();
    ui_frame();
    check("all let go: nothing held, the map gone", !perf_held && !ui.layer);
    seq_open("STEP");                                 /* the STEP grid: FX + a white key toggles no step */
    {
        step_t s0 = TSEL->step[0];
        press(B_FX);
        ui_frame();
        keys(1u << 0);
        fx_hold_frames(1);
        keys(0);
        fx_hold_frames(1);
        release_all();
        ui_frame();
        check("STEP grid: FX + a white key toggles no step", TSEL->step[0].on == s0.on);
    }
}

/* Review Focus 3 */
static void test_perform_menu_kills(void)
{
    uint32_t i;
    ui_host_init();
    press(B_FX);
    ui_frame();
    keys(1u << 3);                                    /* G#3: REPEAT 1/16 */
    fx_hold_frames(1);
    check("REPEAT held in the layer", (perf_held >> PF_R16) & 1u);
    release_all();
    for (i = 0; i < 1000u && !ui.menu; i++) {         /* HOME held: the menu */
        fm1_in.buttons |= 1u << panel.btn[B_HOME];
        ui_frame();
    }
    release_all();
    ui_frame();                                       /* (the next pass sees the menu: 1 ms) */
    check("the menu opened with an effect key held: effects off, the key still the layer's",
          ui.menu && perf_kill && !perf_act && (kb_layer >> 3) & 1u);
    keys(0);
    ui_frame();
    check("the key let go: nothing held", !perf_held && !kb_layer);
    release_all();
    ui_frame();
    menu_close();
    ui_frame();
    ui_frame();
    check("menu closed, no key held: effects allowed again", !perf_kill);
}

/* MENU > PERFORM: HOLD / PAGE; PAGE: the PERFORM screen stays after FX is let go, until the screen changes */
static void test_perform_page(void)
{
    ui_host_init();
    ui.menu = 1;
    ui.menu_sel = MI_PERF;
    ui.force = 1;
    turn(EN_K1, 1);
    ui_frame();
    check("MENU PERFORM: KNOB 1 right = PAGE", settings.perfpage == 1u);
    snap_page("perform/menu_page");
    turn(EN_K1, -1);
    ui_frame();
    check("MENU PERFORM: KNOB 1 left = HOLD", settings.perfpage == 0u);
    menu_close();
    ui_frame();
    settings.perfpage = 1;
    tap(B_SEQ);                                       /* a page under it */
    press(B_FX);
    fx_hold_frames(500);
    release_all();
    ui_frame();
    check("PAGE: FX held then let go, the PERFORM screen stays", ui.layer && ui.pg_open);
    keys(1u << 3);                                    /* G#3 without FX */
    ui_frame();
    check("PAGE: a black key plays its effect with no button held", ((perf_held >> PF_R16) & 1u) && (kb_layer >> 3) & 1u);
    keys(0);
    ui_frame();
    turn(EN_K2, 10);
    ui_frame();
    check("PAGE: the knobs are the macros without FX, and keep their values", perf_k[1] > 0);
    press(B_PLAY);
    ui_frame();
    release_all();
    ui_frame();
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("PAGE: PLAY and OCT+ leave it open", ui.pg_open && perf_k[1] > 0);
    tap(B_GLO);
    check("PAGE: another page button closes it, the macros off", !ui.pg_open && !ui.layer && !perf_k[1] &&
          cur_fam() == FAM_GLO);
    press(B_FX);
    fx_hold_frames(500);
    release_all();
    ui_frame();
    tap(B_FX);
    ui_frame();
    check("PAGE: an FX tap closes it and opens the FX pages", !ui.pg_open && cur_fam() == FAM_FX);
    press(B_FX);
    fx_hold_frames(500);
    release_all();
    ui_frame();
    tap(B_HOME);
    ui_frame();
    check("PAGE: HOME closes it", !ui.pg_open && !ui.layer);
    settings.perfpage = 0;
    press(B_FX);
    fx_hold_frames(500);
    release_all();
    ui_frame();
    check("HOLD: the map goes with FX", !ui.layer && !ui.pg_open);
    transport_req = 2;                                /* (PLAY above started the transport: stopped for the next test) */
    render_mix(0, 0, CTL);
}

/* PERFORM screens: the map held alone, with keys held (REPEAT 1/16 running, track 2 muted), an unavailable REPEAT
 * (40 BPM), OCT UP with the SHIMMER knob */
static void test_perform_screens(void)
{
    ui_host_init();
    press(B_FX);
    fx_hold_frames(500);
    snap_page("perform/map");
    check("the map: the footer says [FX] HOLD", fb_lit(Y_FOOT, 240) > 0u && ui.layer);
    keys(1u << 3 | 1u << 2);                          /* G#3 REPEAT 1/16, G3 track 2 */
    fx_hold_frames(2);
    snap_page("perform/keys");
    keys(0);
    fx_hold_frames(2);
    song.g[G_BPM] = 40;
    ui.force = 1;
    snap_page("perform/bpm40");
    song.g[G_BPM] = 120;
    keys(1u << 20);                                   /* C#5 OCT UP */
    turn(EN_K4, 40);
    fx_hold_frames(2);
    snap_page("perform/oct_shimmer");
    check("OCT UP playing: KNOB 4 is SHIMMER", perf_harm_on() && perf_k[3] > 0);
    keys(0);
    release_all();
    ui_frame();
}

/* review: FX pressed and a key at once, before the UI's pass has seen FX: still the layer's (no note) */
static void test_perform_fast_key(void)
{
    uint32_t age;
    ui_host_init();
    ui_frame();
    age = hit_age(&trk[0]);
    fm1_in.buttons |= fx_bit();                       /* FX down, the UI has not run since */
    keys(1u << 0);                                    /* F3 in the same moment */
    render_mix(0, 0, CTL);                            /* the audio render first */
    check("FX + a key before the UI's pass: the key is the layer's, no note",
          (kb_layer & 1u) && hit_age(&trk[0]) == age);
    keys(0);
    release_all();
    ui_frame();
    ui_frame();
}

/* review: FX held into the menu and out again: the press is dead (no map, no layer, no PAGE), the macros off */
static void test_perform_dead_hold(void)
{
    uint32_t p, i;
    for (p = 0; p < 2u; p++) {
        ui_host_init();
        settings.perfpage = p;
        press(B_FX);
        turn(EN_K2, 10);                              /* CRUSH: the layer open at once */
        fx_hold_frames(2);
        for (i = 0; i < 1000u && !ui.menu; i++) {     /* HOME held too: the menu */
            fm1_in.buttons |= 1u << panel.btn[B_HOME];
            fx_hold_frames(1);
        }
        fm1_in.buttons &= ~(1u << panel.btn[B_HOME]);
        fx_hold_frames(2);
        check(p ? "PAGE: FX held into the menu: the CRUSH macro off there" : "HOLD: FX held into the menu: the CRUSH macro off there",
              ui.menu && !perf_k[1]);
        menu_close();
        fx_hold_frames(3);                            /* FX still held, the menu gone */
        check(p ? "PAGE: FX held out of the menu: dead (no PERFORM page, keys not the layer's)"
                : "HOLD: FX held out of the menu: dead (no map, keys not the layer's)",
              !ui.layer && !ui.pg_open && !perf_mask);
        release_all();
        ui_frame();
        settings.perfpage = 0;
    }
}

/* MOTION: knob turns of the armed, selected track record while playing (SOUND, HOME, TRACKS); clears */
static void motion_armed_play(void)
{
    ui_host_init();
    song.rec = 1u;                                    /* track 1 armed */
    transport_req = 1;
    ui_frame();
    ui_frame();
}
static void test_motion_recording(void)
{
    uint32_t i;
    motion_armed_play();
    tap(B_EDIT);                                      /* SOUND 1: MODEL, the model's 1st, 2nd, 3rd knob */
    turn(EN_K3, 4);
    ui_frame();
    check("MOTION: a SOUND knob of the armed track records while playing", motion_count(0) == 1u);
    ui.home = 1;
    ui.force = 1;
    ui_frame();
    turn(EN_K1, 3);
    ui_frame();
    check("MOTION: a HOME knob records", motion_count(0) == 2u);
    open_family(FAM_MIX);
    ui_frame();
    turn(EN_K2, 3);                                   /* TRACKS: LEVEL */
    ui_frame();
    check("MOTION: TRACKS' LEVEL records", motion_count(0) >= 3u);
    for (i = 0; mo.s.count < MOTION_MAX; i++)
        motion_add(1, i % 64u, P_E0 + i / 64u, 1);    /* the store full with track 2's */
    tap(B_EDIT);
    turn(EN_K4, -4);                                  /* the model's 3rd knob: a new place (E1's is taken) */
    ui_frame();
    check("MOTION FULL: said when a turn finds no free place", str_eq(ui.msg, "MOTION FULL") && !mo.full);
    track_clear(&trk[1]);
    check("CLEAR TRACK (and CLR SEQ / CLR ALL): its motion cleared too", motion_count(1) == 0u && motion_count(0) >= 1u);
    transport_req = 2;
    ui_frame();
}

/* Review Focus 4: a model change while motion plays re-takes the base */
static void test_motion_model_change(void)
{
    int16_t def;
    uint32_t i;
    ui_host_init();
    trk[0].p[P_E1] = 7;                               /* the old sound's DECAY, not a default */
    trk[0].p[P_LEVEL] = 104;                          /* a knob the model change leaves alone */
    motion_add(0, 0, P_E1, 120);                      /* plays at step 0: E1 moved off the base 7 */
    motion_add(0, 0, P_LEVEL, 20);                    /* .. and LEVEL off 104 */
    transport_req = 1;
    for (i = 0; i < 4u; i++)
        ui_frame();
    tap(B_EDIT);
    turn(EN_K1, 1);                                   /* SOUND 1 KNOB 1: the next model, its default sound */
    ui_frame();
    def = trk[0].p[P_E1];
    check("MOTION: a model change while playing is the new base", mo.base[0][P_E1] == def && def != 7);
    transport_req = 2;
    ui_frame();
    check("MOTION: STOP after a model change keeps the new model's values (not the old 7)", trk[0].p[P_E1] == def);
    check("MOTION: STOP after a model change puts the other knobs' patch values back (LEVEL 104, not 20)",
          trk[0].p[P_LEVEL] == 104);
    init_all();
    check("INIT ALL: no motion left", mo.s.count == 0u);
}

/* the MOTION page: PLAY, EVENTS, CLEAR with its dialog; screens */
static void test_motion_page(void)
{
    ui_host_init();
    seq_open("MOTION");
    check("SEQ reaches the MOTION page", !ui.home && str_eq(cur_page()->title, "MOTION"));
    snap_page("motion/empty");
    motion_add(0, 0, P_E1, 20);
    motion_add(0, 4, P_E1, 60);
    motion_add(0, 9, P_PAN, -30);
    transport_req = 1;
    ui_frame();
    ui.force = 1;
    snap_page("motion/events");
    turn(EN_K1, -1);
    ui_frame();
    check("MOTION KNOB 1 left: PLAY OFF", !motion_on(0));
    turn(EN_K1, 1);
    ui_frame();
    check("MOTION KNOB 1 right: PLAY ON", motion_on(0));
    turn(EN_K4, 1);
    ui_frame();
    check("MOTION KNOB 4: asks before clearing", ui.confirm == 2u && motion_count(0) == 3u);
    snap_page("motion/confirm");
    press(B_OCTDN);
    ui_frame();
    release_all();
    ui_frame();
    check("the dialog: OCT- keeps the motion", !ui.confirm && motion_count(0) == 3u);
    turn(EN_K4, 1);
    ui_frame();
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("the dialog: OCT+ clears the track's motion", !ui.confirm && motion_count(0) == 0u);
    transport_req = 2;
    ui_frame();
}

static void rec_hold(void)                           /* REC held 0.8 s (1 ms per tick), then let go */
{
    uint32_t i;
    fm1_in.buttons |= 1u << panel.btn[B_REC];
    for (i = 0; i < 800; i++)
        ui_input();
    release_all();
    ui_frame();
}
static void song_slot_saved(uint32_t s, const char *pat)   /* slot s: track 1 playing pat */
{
    uint32_t i;
    memset(trk[0].step, 0, sizeof trk[0].step);
    for (i = 0; pat[i]; i++)
        trk[0].step[i].on = pat[i] != '.';
    project_save(s);
}
static void test_song_page(void)
{
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    seq_open("SONG");
    check("SEQ reaches the SONG page", !ui.home && str_eq(cur_page()->title, "SONG"));
    snap_page("song/empty");
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("SONG: KNOB 2 on the + row adds row 1 (A x1)", chain.cfg.count == 1u && chain.cfg.row[0].slot == 0u &&
          chain.cfg.row[0].repeat == 1u);
    turn(EN_K1 + 1, 1);
    ui_frame();
    turn(EN_K1 + 2, 3);
    ui_frame();
    check("SONG: KNOB 2 SLOT, KNOB 3 REPEAT", chain.cfg.row[0].slot == 1u && chain.cfg.row[0].repeat == 4u);
    turn(EN_K1, 1);
    ui_frame();
    turn(EN_K1 + 2, 1);
    ui_frame();
    check("SONG: the next + row: a row from the previous slot (B x1)", chain.cfg.count == 2u &&
          chain.cfg.row[1].slot == 1u && chain.cfg.row[1].repeat == 1u);
    turn(EN_K1, 5);
    ui_frame();
    check("SONG: KNOB 1 stops at the + row", ui.song_row == 2u);
    turn(EN_K1 + 3, 1);
    ui_frame();
    check("SONG: KNOB 4 LOOP ON", chain.cfg.loop == 1u);
    snap_page("song/rows");
}
static void test_song_play_ui(void)
{
    uint32_t b;
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    song_slot_saved(0, "x...x...x...x...");
    trk[0].p[P_SLEN] = 7;
    seq_open("SONG");
    tap(B_PLAY);
    check("SONG: PLAY with no rows: ADD A SONG ROW", !song.playing && str_eq(ui.msg, "ADD A SONG ROW"));
    chain.cfg.count = 2;
    chain.cfg.row[0] = (chain_row_t){0, 2};
    chain.cfg.row[1] = (chain_row_t){1, 1};
    tap(B_PLAY);
    check("SONG: PLAY with an empty slot: PATTERN B EMPTY", !song.playing && str_eq(ui.msg, "PATTERN B EMPTY"));
    chain.cfg.row[1].slot = 0;
    tap(B_PLAY);
    for (b = 0; b < 4u; b++)
        ui_frame();
    check("SONG: PLAY starts the song", song.playing && chain.running && trk[0].p[P_SLEN] == 16);
    ui.msg_t = 0;                                    /* (the header: the row playing) */
    ui.force = 1;
    snap_page("song/playing");
    turn(EN_K1 + 2, 1);
    ui_frame();
    check("SONG: an edit while it plays: STOP TO EDIT", chain.cfg.row[0].repeat == 2u && str_eq(ui.msg, "STOP TO EDIT"));
    seq_open("PATTERN");
    turn(EN_K1, 1);
    ui_frame();
    check("PATTERN while a song plays: STOP TO EDIT", trk[0].p[P_SLEN] == 16 && str_eq(ui.msg, "STOP TO EDIT"));
    seq_open("STEP");
    keys(1u << 2);
    ui_frame();
    keys(0);
    ui_frame();
    check("STEP grid while a song plays: STOP TO EDIT, the step unchanged", !trk[0].step[1].on &&
          str_eq(ui.msg, "STOP TO EDIT"));
    tap(B_PLAY);
    ui_frame();
    check("PLAY again: the song stops, LEN back", !song.playing && !chain.running && trk[0].p[P_SLEN] == 7);
}
static void test_song_play_twice(void)
{
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    song_slot_saved(0, "x...............");
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    seq_open("SONG");
    press(B_PLAY);
    ui_input();                                      /* armed, the ISR has not started it yet */
    release_all();
    press(B_PLAY);
    ui_input();                                      /* stopped again before it began */
    release_all();
    render_mix(0, 0, CTL);
    render_mix(0, 0, CTL);
    check("SONG: PLAY twice before it began: nothing playing, nothing armed", !song.playing && !chain_busy());
}
static void test_song_delete(void)
{
    ui_host_init();
    seq_open("SONG");
    chain.cfg.count = 3;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    chain.cfg.row[1] = (chain_row_t){1, 2};
    chain.cfg.row[2] = (chain_row_t){2, 3};
    ui.song_row = 1;
    rec_hold();
    check("SONG: REC held on a row asks DELETE ROW", ui.confirm == 3u);
    snap_page("song/delete");
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("SONG: OCT+ deletes it, the later rows move up", chain.cfg.count == 2u && chain.cfg.row[1].slot == 2u &&
          chain.cfg.row[1].repeat == 3u);
    ui.song_row = 2;
    rec_hold();
    check("SONG: REC held on the + row asks CLEAR SONG", ui.confirm == 4u);
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("SONG: OCT+ clears the song", chain.cfg.count == 0u && ui.song_row == 0u);
}

static void name_type(uint32_t key)                  /* one key tap (key index from F3) */
{
    keys(1u << key);
    ui_frame();
    keys(0);
    ui_frame();
}
static void project_page(void)
{
    uint32_t k;
    for (k = 0; k < 8u && (ui.home || page_id(cur_page(), 0) != G_SLOT); k++)
        tap(B_SAVE);
}
static void save_knob(void)                          /* SAVE: GO, and GO again */
{
    turn(EN_K1 + 3, 1);
    ui_frame();
    turn(EN_K1 + 3, 1);
    ui_frame();
}
static void test_name_save(void)
{
    char b[NAME_LEN + 1u];
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    project_page();
    save_knob();
    check("SAVE saves at once (no NAME screen), the project unnamed", !name_on() && project_used(0) &&
          project_name(0, b) && !b[0]);
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("NAME knob on an unnamed slot: NAME opens, prefilled PROJECT A", name_on() && str_eq(nm.s, "PROJECT A"));
    snap_page("name/abc");
    while (nm.len) {                                 /* C# (key 8): DELETE */
        nm.cur = nm.len;
        name_type(8);
    }
    name_type(0);                                    /* F3: AB -> A */
    name_type(0);                                    /* again within 0.8 s: B */
    host_ticks += 900u * 1000u * FM1_TICKS_PER_US;   /* 0.9 s: kept */
    ui_frame();
    name_type(2);                                    /* G3: CD -> C */
    host_ticks += 900u * 1000u * FM1_TICKS_PER_US;
    ui_frame();
    name_type(3);                                    /* G#3: SPACE */
    name_type(10);                                   /* D#4: 123 */
    snap_page("name/123");
    name_type(0);                                    /* 1 */
    name_type(2);                                    /* 2 */
    check("NAME: multi-tap, the timeout, SPACE, 123", str_eq(nm.s, "BC 12") && nm.cur == 5u);
    name_type(1);                                    /* F#3: left */
    name_type(8);                                    /* DELETE: the 1 */
    check("NAME: cursor left, DELETE", str_eq(nm.s, "BC 2") && nm.cur == 3u);
    turn(EN_K1, -3);
    ui_frame();
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("NAME: KNOB 1 the cursor, KNOB 2 the character", nm.cur == 0u && nm.s[0] == 'C');
    turn(EN_K1, 10);
    ui_frame();
    name_type(3);                                    /* a trailing space: dropped when written */
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("NAME OCT+: the slot renamed (ends trimmed), the current project's name too", !name_on() &&
          project_name(0, b) && str_eq(b, "CC 2") && str_eq(chain.name, "CC 2"));
    project_page();
    ui.force = 1;
    snap_page("name/project");
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("NAME knob: renames the selected slot, prefilled with its name", name_on() && str_eq(nm.s, "CC 2"));
    press(B_OCTDN);
    ui_frame();
    release_all();
    ui_frame();
    check("NAME OCT-: cancelled, nothing written", !name_on() && str_eq(chain.name, "CC 2"));
    turn(EN_K1, 1);                                  /* slot B: empty */
    ui_frame();
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("NAME knob on an empty slot: EMPTY SLOT", !name_on() && str_eq(ui.msg, "EMPTY SLOT"));
}
static void test_name_keys_silent(void)
{
    uint32_t a;
    ui_host_init();
    project_page();
    project_save(0);
    transport_req = 1;
    ui_frame();
    transport_req = 0;
    name_open(NK_RENAME, 0);
    song.rec = 1u;
    a = hit_age(&trk[0]);
    name_type(0);                                    /* F3: track 1's key */
    press(B_FX);
    ui_frame();
    name_type(1);                                    /* F#3 with FX held: not PERFORM */
    release_all();
    ui_frame();
    check("NAME: the keys never sound, record or start PERFORM", hit_age(&trk[0]) == a && !perf_held &&
          !trk[0].step[0].on && !trk[0].step[1].on && str_eq(nm.s, "PROJECT AA"));
}
static void test_name_stop_to_save(void)
{
    char b[NAME_LEN + 1u];
    ui_host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    project_page();
    project_save(0);
    name_open(NK_RENAME, 0);
    transport_req = 1;
    ui_frame();
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("NAME OCT+ while playing: STOP TO SAVE, the screen stays", name_on() && project_name(0, b) && !b[0] &&
          str_eq(ui.msg, "STOP TO SAVE"));
    tap(B_PLAY);
    ui_frame();
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("... stopped (PLAY works in NAME): renamed", !name_on() && project_name(0, b) && str_eq(b, "PROJECT A"));
    name_open(NK_RENAME, 0);
    name_type(0);
    tap(B_HOME);
    ui_frame();
    check("NAME: HOME cancels", !name_on() && project_name(0, b) && str_eq(b, "PROJECT A"));
}

static void test_song_row_after_load(void)
{
    uint32_t i;
    ui_host_init();
    seq_open("SONG");
    chain.cfg.count = 10;
    for (i = 0; i < 10u; i++)
        chain.cfg.row[i] = (chain_row_t){1, 2};
    ui.song_row = 10;                                /* the + row of a 10-row song */
    chain.cfg.count = 1;                             /* a LOAD brings a 1-row song */
    turn(EN_K1 + 1, 1);
    ui_frame();
    check("SONG after a LOAD of a shorter song: a turn on + adds row 2, the song valid",
          chain.cfg.count == 2u && chain_valid(&chain.cfg) && chain.cfg.row[1].repeat == 1u && ui.song_row <= 2u);
}
static void test_name_message_ends(void)
{
    uint32_t i;
    ui_host_init();
    project_page();
    project_save(0);
    name_open(NK_RENAME, 0);
    ui_message("STOP TO SAVE");
    for (i = 0; i < 60u; i++)
        ui_frame();
    check("NAME: a message in its title goes after a while", ui.msg_t == 0u);
    name_close();
}

static void test_reson_model_skips_full(void)
{
    uint32_t k;
    ui_host_init();
    trk[1].p[P_RMODEL] = RS_CHORD;
    trk[2].p[P_RMODEL] = RS_CHORD;                    /* CHORD full (2) */
    for (k = 0; k < NPAGES && (ui.home || !str_eq(cur_page()->title, "RESON")); k++)
        tap(B_FX);
    TSEL->p[P_RMODEL] = RS_PIPE;
    turn(EN_K1, 1);
    ui_frame();
    check("RESON MODEL: past a full CHORD the knob reaches MODAL", TSEL->p[P_RMODEL] == RS_MODAL);
    trk[3].p[P_RMODEL] = RS_MODAL;                    /* MODAL full too (tracks 1 and 4) */
    song.sel = 4;
    TSEL->p[P_RMODEL] = RS_PIPE;
    turn(EN_K1, 1);
    ui_frame();
    check("RESON MODEL: CHORD and MODAL full: stays, says why", TSEL->p[P_RMODEL] == RS_PIPE &&
          (str_eq(ui.msg, "MODAL: 2 TRACKS MAX") || str_eq(ui.msg, "RESON: 4 TRACKS MAX")));
}
static void test_modal_load_clamp(void)
{
    uint32_t k, n = 0;
    ui_host_init();
    for (k = 0; k < 3u; k++)
        trk[k].p[P_RMODEL] = RS_MODAL;
    project_save(0);
    project_load(0);
    for (k = 0; k < NTRK; k++)
        n += trk[k].p[P_RMODEL] == RS_MODAL;
    check("a project with 3 MODAL tracks loads with 2 (the 3rd STRNG)", n == 2u && trk[2].p[P_RMODEL] == RS_STRNG);
}

static void test_phys_screens(void)
{
    uint32_t k;
    ui_host_init();
    drum_set_model(TSEL, DM_MEMB);
    for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_SND); k++)
        tap(B_EDIT);
    ui.force = 1;
    snap_page("phys/memb_1");
    page_turn(1);
    ui.force = 1;
    snap_page("phys/memb_2");
    {
        int16_t *vp;
        const param_desc_t *h = page_desc(cur_page(), 0, &vp), *b = page_desc(cur_page(), 2, &vp);
        check("MEMB: SOUND 2/3 holds HEAD .. BEND", h && b && str_eq(h->label, "HEAD") && str_eq(b->label, "BEND"));
    }
    TSEL->p[P_RMODEL] = RS_MODAL;
    TSEL->p[P_RSTRCT] = 110;
    for (k = 0; k < NPAGES && (ui.home || !str_eq(cur_page()->title, "RESON")); k++)
        tap(B_FX);
    ui.force = 1;
    snap_page("phys/reson_modal");
    check("RESON with MODAL draws", !ui.home && str_eq(cur_page()->title, "RESON"));
}

/* FILTER: two pages first in FX (TYPE CUT RESO ENV, DECAY), the graph per TYPE */
static void test_filter_screens(void)
{
    static const char *const SHOT[FT_N] = {"filter/off", "filter/lp", "filter/bp", "filter/hp", "filter/notch"};
    uint32_t k, ty;
    int16_t *vp;
    ui_host_init();
    for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_FX); k++)
        tap(B_FX);
    check("FX: the first page is FILTER 1/2 (TYPE CUT RESO ENV)",
          str_eq(cur_page()->title, "FILTER") && str_eq(page_desc(cur_page(), 0, &vp)->label, "TYPE") &&
          str_eq(page_desc(cur_page(), 3, &vp)->label, "ENV"));
    TSEL->p[P_FCUT] = 70;
    TSEL->p[P_FRESO] = 90;
    TSEL->p[P_FENV] = 30;
    for (ty = 0; ty < FT_N; ty++) {
        TSEL->p[P_FTYPE] = (int16_t)ty;
        ui.force = 1;
        snap_page(SHOT[ty]);
    }
    page_turn(1);
    ui.force = 1;
    snap_page("filter/decay");
    check("FILTER 2/2 holds DECAY", str_eq(cur_page()->title, "FILTER") && str_eq(page_desc(cur_page(), 0, &vp)->label, "DECAY"));
    TSEL->p[P_LFO1 + LF_DEST] = 17;
    for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_LFO); k++)
        tap(B_LFO);
    page_turn(1);
    ui.force = 1;
    snap_page("filter/lfo_fcut");
    {
        char v[8];
        const char *u;
        param_format(&TP[P_LFO1 + LF_DEST], 17, v, &u);
        check("LFO DEST 17 reads F.CUT", str_eq(v, "F.CUT"));
    }
}

/* INIT SOUND resets the FILTER (spec §3); a model change keeps it (as DIST and RESON) */
static void test_initsnd_filter(void)
{
    uint32_t k;
    ui_host_init();
    TSEL->p[P_FTYPE] = FT_HP;
    TSEL->p[P_FCUT] = 40;
    TSEL->p[P_FENV] = -20;
    model_step(1);
    check("a model change keeps the FILTER", TSEL->p[P_FTYPE] == FT_HP && TSEL->p[P_FCUT] == 40);
    for (k = 0; k < NPAGES && (ui.home || !str_eq(cur_page()->title, "TOOLS")); k++) {
        press(B_SAVE);
        ui_frame();
        release_all();
    }
    turn(EN_K1 + 1, 1);                              /* INIT SOUND: one detent arms, */
    ui_frame();
    turn(EN_K1 + 1, 1);                              /* a second acts */
    ui_frame();
    check("INIT SOUND resets the FILTER (TYPE OFF, CUT 127, ENV 0)",
          TSEL->p[P_FTYPE] == FT_OFF && TSEL->p[P_FCUT] == 127 && TSEL->p[P_FENV] == 0 && TSEL->p[P_FDEC] == 40);
}

/* the knobs follow the screen: while the PERFORM screen shows (FX held, a layer key held, or the PAGE), they are
 * its macros, never the page under it; when it goes, the macros snap back (user report 2026-10-08, PAGE mode) */
static void test_perform_knobs_follow_screen(void)
{
    uint32_t f, i, ch = 0, mode;
    for (mode = 0; mode < 2u; mode++) {              /* 0: HOLD, 1: PAGE */
        int16_t tb[P_COUNT];
        ui_host_init();
        settings.perfpage = (uint32_t)mode;
        tap(B_EDIT);                                 /* SOUND under it */
        for (f = 0; f < 600u; f++) {                 /* FX held past the hold time: the PERFORM screen */
            press(B_FX);
            ui_frame();
        }
        keys(1u << 1);                               /* a black key: the layer's (REPEAT 1/8) */
        press(B_FX);
        ui_frame();
        release_all();                               /* FX let go, the key still held */
        keys(1u << 1);
        ui_frame();
        if (mode) {
            turn(EN_PRESET, 1);                      /* PAGE: another page under it closes the PAGE */
            keys(1u << 1);
            ui_frame();
        }
        memcpy(tb, TSEL->p, sizeof tb);
        turn(EN_K1, 3);
        turn(EN_K1 + 2, 3);
        keys(1u << 1);
        ui_frame();
        for (i = 0; i < P_COUNT; i++)
            ch |= TSEL->p[i] != tb[i] && ui.layer;
        keys(0);                                     /* the key let go: the screen goes, the macros snap back */
        ui_frame();
        ui_frame();
        ch |= !ui.layer && (perf_k[0] || perf_k[2]);
    }
    check("PERFORM shown (FX or a layer key held, or PAGE): the knobs are its macros, not the page under it", !ch);
}

int main(void)
{
    test_safe_start();
    test_key_selects_track();
    test_grid_hold_accent();
    test_grid_step_edit();
    test_grid_hold_track_change();
    test_grids_pages();
    test_src_readonly();
    test_engine_screens();
    test_seq_screens();
    test_families();
    test_sound_pages();
    test_track_select();
    test_model_swap();
    test_home_macros();
    test_grid_keys();
    test_bank_follows_len();
    test_mixer_and_rec();
    test_quick_mute();
    test_no_save_while_playing();
    test_knob_accel();
    test_home_keys_and_lfo();
    test_tracks_rec_keys();
    test_reson_pages();
    test_preset_pages();
    test_global_mute_setting();
    test_comp_pages();
    test_comp_ghost_knob();
    test_comp_keys();
    test_tools_all();
    test_layer_in_edit();
    test_lfo_pages();
    test_lfo_trail();
    test_lfo_edit_marker();
    test_clear_confirm();
    test_oct_both_reaches_main();
    test_tracks_rows();
    test_screens();
    test_project_roundtrip();
    test_project_rejects();
    test_ui_frame_cost();
    test_speaker_eq();
    test_reverb_pages();
    test_div_order();
    test_usb_level();
    test_clock_screens();
    test_perform_gesture();
    test_perform_keys();
    test_perform_menu_kills();
    test_perform_page();
    test_perform_screens();
    test_perform_fast_key();
    test_perform_dead_hold();
    test_motion_recording();
    test_motion_model_change();
    test_motion_page();
    test_song_page();
    test_song_play_ui();
    test_song_play_twice();
    test_song_delete();
    test_name_save();
    test_name_keys_silent();
    test_name_stop_to_save();
    test_song_row_after_load();
    test_name_message_ends();
    test_reson_model_skips_full();
    test_modal_load_clamp();
    test_phys_screens();
    test_filter_screens();
    test_initsnd_filter();
    test_perform_knobs_follow_screen();
    test_boot_title();
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: all passed\n", fails);
    return fails ? 1 : 0;
}
