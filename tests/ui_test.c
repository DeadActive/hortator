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
    turn(EN_PRESET, 1);
    ui_frame();
    check("PRESET on HOME: next model with its default sound, voices stopped",
          (uint32_t)trk[0].p[P_MODEL] == (m0 + 1u) % NMODELS && trk[0].model == trk[0].p[P_MODEL] &&
              trk[0].p[P_E1] == DMODELS[(m0 + 1u) % NMODELS].edit[1].def && !trk[0].v[0].active);
    press(B_EDIT);
    ui_frame();
    release_all();
    turn(EN_K1, -1);
    ui_frame();
    check("MODEL knob on EDIT (page 1): back to the first model, defaults loaded",
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
    check("RESON CHORD cap: a 3rd track's MODEL knob stays on PIPE, with the message",
          song.sel == 2 && trk[2].p[P_RMODEL] == RS_PIPE && str_eq(ui.msg, "CHORD: 2 TRACKS MAX"));
    snap_page("reson/03_chord_cap");
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
        for (k = 0; k < 2u * NMODELS && (uint32_t)TSEL->p[P_MODEL] != mi; k++) {   /* PRESET on HOME: next engine */
            turn(EN_PRESET, 1);
            ui_frame();
        }
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
              trk[3].p[P_RMODEL] == RS_CHORD && trk[3].p[P_RTUNE] == 96 && trk[3].p[P_RDECAY] == 0);
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
    check("safe start: SEQ held through the boot's polled scan (FM1_DEBOUNCE + 4 scans)",
          safe_start && host_scans == FM1_DEBOUNCE + 4u);
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
    check("normal start: SEQ not held: no safe mode, the same scans", !safe_start && host_scans == FM1_DEBOUNCE + 4u);
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
        {"FX", B_FX, 1, 1}, {"SLICER", B_FX, 2, 1}, {"RESON", B_FX, 3, 1}, {"DLY", B_FX, 5, 0}, {"PATTERN", B_SEQ, 2, 0},
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
    test_home_keys_and_lfo();
    test_tracks_rec_keys();
    test_reson_pages();
    test_global_mute_setting();
    test_comp_pages();
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
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: all passed\n", fails);
    return fails ? 1 : 0;
}
