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
static uint32_t foot_lit(void);
static uint32_t fb_count(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint16_t c);
static void tap(uint32_t b);
static void hold(uint32_t label);
static void safe_boot(int seq_held);
static void kit_session(void);
static void fx_hold_frames(uint32_t n);
static int led_on(uint32_t id)                       /* the LED of button / key id lit (ui_leds' last frame: fm1_led,
                                                      * or the full layer while the scan's hook composes them) */
{
    uint32_t q = led_pos[id];
    return q != 0xFF && (((led_dim_on ? led_lay[q >> 3] : fm1_led[q >> 3]) >> (q & 7u)) & 1u);
}
static int led_breathes(uint32_t id)                 /* .. breathing (led_dim.c's breath layer; item 5) */
{
    uint32_t q = led_pos[id];
    return q != 0xFF && led_dim_on && ((led_lay[q >> 3] >> (24u + (q & 7u))) & 1u);
}

/* SELECT (spec §2): one detent; the pages, BPM on HOME's TRACKS mode */
static void sel(int32_t d)
{
    turn(EN_SELECT, d);
    ui_frame();
}
/* HOME's TRACKS mode (spec §3): HOME taps until it shows (on HOME a tap toggles TRACKS / HOME, elsewhere it opens
 * HOME in the mode last shown) */
static void open_tracks(void)
{
    uint32_t k;
    for (k = 0; k < 3u && !mix_mode(); k++)
        tap(B_HOME);
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
    check("ENV does nothing (free); SCL toggles KEYS, the page stays", !ui.home && cur_page()->fam == FAM_LFO && ui.keys);
    press(B_SCL);                                    /* KEYS off again */
    ui_frame();
    release_all();
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
    old_set_model(&trk[0], DM_C808);
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
    check("EDIT page 3: CHOKE PRIO, nothing else (no MUTE)", page_id(pg, 0) == P_CHOKE && page_id(pg, 1) == P_PRIO &&
                                                               page_id(pg, 2) == 0xFFu && k == 3u && n == 3u);
    snap_page("engines/zz_c808_edit_3");
    edit_next();
    n = fam_pages(FAM_SND, &k);
    check("EDIT again: back to page 1", k == 1u);
    old_set_model(&trk[0], DM_K909);
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
    sel(1);
    check("SELECT elsewhere (FX page) leaves the model alone", (uint32_t)trk[0].p[P_MODEL] == m0);
}

/* SELECT turns the current section's pages (spec §2), both ways, stopping at the ends (HOME's second mode: HOME 1/3
 * <-> COMP 2/3 <-> 3/3); on HOME's TRACKS mode it is BPM; PRESETS does nothing; it never changes the engine */
static void test_select_pages(void)
{
    uint32_t m0, pos, k;
    int16_t bpm;
    ui_host_init();
    m0 = (uint32_t)TSEL->p[P_MODEL];
    bpm = song.g[G_BPM];
    sel(1);
    check("SELECT on HOME (mode 2): COMP (HOME 2/3), the engine and BPM unchanged",
          !ui.home && cur_page()->fam == FAM_HOME && page_id(cur_page(), 0) == G_CSRC &&
              (uint32_t)TSEL->p[P_MODEL] == m0 && song.g[G_BPM] == bpm);
    sel(1);
    check("SELECT again: COMP (HOME 3/3)", page_id(cur_page(), 0) == G_CATK);
    sel(1);
    check("SELECT at the last page: stays", page_id(cur_page(), 0) == G_CATK);
    sel(-1);
    sel(-1);
    check("SELECT back twice: the HOME screen (mode 2)", ui.home);
    sel(-1);
    check("SELECT back on HOME 1/3: stays, the engine unchanged", ui.home && (uint32_t)TSEL->p[P_MODEL] == m0);
    tap(B_FX);
    check("FX opens on FILTER 1/2", str_eq(cur_page()->title, "FILTER") && page_id(cur_page(), 0) == P_FTYPE);
    sel(1);                                          /* FILTER 2/2 */
    sel(1);                                          /* FX */
    sel(1);
    check("SELECT on FX: SLICER", str_eq(cur_page()->title, "SLICER"));
    sel(1);
    check("SELECT again: RESON 1/2", str_eq(cur_page()->title, "RESON") && page_id(cur_page(), 0) == P_RMODEL);
    sel(-1);
    check("SELECT back: SLICER", str_eq(cur_page()->title, "SLICER"));
    k = ui.page;
    turn(EN_PRESET, 3);
    ui_frame();
    turn(EN_PRESET, -3);
    ui_frame();
    check("PRESETS loads presets only (the page, the BPM, the track stay; 3 right, 3 left: the engine back)",
          ui.page == k && song.g[G_BPM] == bpm && song.sel == 0 && (uint32_t)TSEL->p[P_MODEL] == m0);
    tap(B_SEQ);
    tap(B_FX);
    check("FX again later: the page SELECT left it on (SLICER)", str_eq(cur_page()->title, "SLICER"));
    tap(B_EDIT);                                     /* SOUND 1/3 .. 3/3; page 4 is empty for every engine: skipped */
    for (k = 0; k < 6u; k++)
        sel(1);
    check("SELECT on EDIT: stops on the engine's last SOUND page (empty pages skipped)",
          str_eq(cur_page()->title, "SOUND") && fam_pages(FAM_SND, &pos) == pos && !page_hidden(ui.page));
    tap(B_OCTUP);                                    /* the layer */
    sel(1);
    check("SELECT in the layer: LAYER 2/2", str_eq(cur_page()->title, "LAYER") && page_id(cur_page(), 0) == P_LDEC);
    sel(1);
    check("SELECT at LAYER 2/2: stays", page_id(cur_page(), 0) == P_LDEC);
    tap(B_HOME);                                     /* HOME as last shown (mode 2), then its TRACKS mode */
    tap(B_HOME);
    k = ui.page;
    host_ticks += 300u * 1000u * FM1_TICKS_PER_US;   /* (a new turn: no acceleration) */
    sel(1);
    check("SELECT on HOME's TRACKS mode: BPM +1, the page stays", mix_mode() && ui.page == k && song.g[G_BPM] == bpm + 1);
    host_ticks += 300u * 1000u * FM1_TICKS_PER_US;
    sel(-2);
    check("SELECT left on TRACKS: BPM -2", mix_mode() && song.g[G_BPM] == bpm - 1);
    turn(EN_PRESET, 2);
    ui_frame();
    check("PRESETS on TRACKS: no BPM, the page stays", mix_mode() && song.g[G_BPM] == bpm - 1);
    turn(EN_ALGO, 2);
    ui_frame();
    check("ALGORITHM on TRACKS: the track (1 -> 2 per read)", song.sel == 1);
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

static int led_lit(uint32_t id) { return led_on(id); }   /* the LED of button / key id, as ui_leds left it */

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

/* fixes 2026-10-21 item 1: OCT+ held, a step key pressed, let go, OCT+ let go: the step's accent */
static void grid_acc(uint32_t key)
{
    press(B_OCTUP);
    ui_frame();
    keys(1u << key);
    ui_frame();
    keys(0);
    ui_frame();
    release_all();
    ui_frame();
}

/* a tap turns a step on / off; a hold is a tap (fixes 2026-10-21 item 1: a held key is PROB / RATCH's, no accent);
 * OCT+ held + a step key flips its accent (an off step on and accented); OCT+ let go alone: the next bank, with a
 * step accented: none; while OCT+ is held the keys show the bank's accents */
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
    check("grid: a 0.6 s hold is a tap too (off, no accent)", !s->on && !s->acc);
    grid_hold(WHITE[4], 0);
    grid_acc(WHITE[4]);
    check("grid: OCT+ held + an on step: accented (it stays on), the bank unchanged", s->on && s->acc && ui.bank == 0u);
    grid_acc(WHITE[4]);
    check("grid: OCT+ held + an accented step: the accent off (it stays on)", s->on && !s->acc);
    s->on = 0;
    s->acc = 0;
    grid_acc(WHITE[4]);
    check("grid: OCT+ held + an off step: on with accent", s->on && s->acc);
    grid_hold(WHITE[4], 0);
    check("grid: a tap on an accented step turns it off", !s->on && !s->acc);
    trk[0].p[P_SLEN] = 32;                           /* two banks */
    trk[0].step[4].on = trk[0].step[4].acc = 1;
    trk[0].step[6].on = 1;
    press(B_OCTUP);
    ui_frame();
    check("OCT+ pressed: no bank yet (it waits for the release); the keys show the accents (step 5 lit, step 7 dark)",
          ui.bank == 0u && led_on(14u + WHITE[4]) && !led_on(14u + WHITE[6]));
    release_all();
    ui_frame();
    check("OCT+ let go alone: the next bank, the keys the steps again", ui.bank == 1u && !led_on(14u + WHITE[4]));
    tap(B_OCTDN);
    grid_acc(WHITE[2]);
    check("OCT+ let go after a step accented: the bank stays", ui.bank == 0u && trk[0].step[2].on && trk[0].step[2].acc);
    trk[0].step[2].on = trk[0].step[2].acc = 0;
    {
        uint32_t ms;
        keys(1u << WHITE[6]);                        /* a step key held long: nothing more than its tap */
        ui_frame();
        for (ms = 0; ms < 800u; ms += 50u) {
            host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
            ui_frame();
        }
        check("grid: a key held 0.8 s: its tap only (step 7 off), no accent", !trk[0].step[6].on && !trk[0].step[6].acc);
        keys(0);
        ui_frame();
    }
    trk[0].step[4].on = 1;
    trk[0].step[4].acc = 1;
    trk[0].step[0].on = 1;
    ui.force = 1;
    snap_page("fixes/step_oct_accent_hint");
    trk[0].p[P_SLEN] = 16;
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
    grid_acc(WHITE[4]);                              /* on, accented (OCT+ held) */
    grid_hold_turn(WHITE[4], 0, -5);                 /* 100 % -> 75 % */
    check("hold + KNOB 1: PROB of the held step (100 % -> 75 %); its accent stays, the press undone (on again)",
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
    grid_acc(WHITE[4]);                              /* accent off by OCT+ + the key: PROB / RATCH kept */
    check("OCT+ + the key flips the accent and keeps PROB / RATCH", s->on && !s->acc && s->cond == cond_store(15) && s->rat == 2);
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
    open_tracks();                                   /* HOME's TRACKS mode */
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

static void open_comp(int page2)                    /* HOME's second mode, then SELECT until COMP (2/3, or 3/3) */
{
    uint32_t k;
    for (k = 0; k < 8u && (!comp_mode() || (cur_page()->id[0] == G_CATK) != page2); k++)
        if (ui.home || comp_mode())
            sel(ui.home || page2 ? 1 : -1);
        else
            tap(B_HOME);
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
    tap(B_HOME);
    check("HOME from FX: the HOME screen (1/3, the mode last shown)", ui.home);
    sel(1);
    check("SELECT: COMP (HOME 2/3: SRC THRSH RATIO REL), HOME lit",
          !ui.home && str_eq(cur_page()->title, "COMP") && cur_page()->fam == FAM_HOME && cur_page()->id[0] == G_CSRC &&
              fam_pages(FAM_HOME, &pos) == 2u && pos == 1u && led_lit(panel.btn[B_HOME]));
    sel(1);
    check("SELECT again: COMP (HOME 3/3: ATK KNEE MKUP)", !ui.home && cur_page()->id[0] == G_CATK);
    tap(B_HOME);
    check("a HOME tap on COMP: back to the HOME screen (mode 2)", ui.home);
    sel(1);
    sel(-1);
    check("SELECT left from COMP 2/3: the HOME screen (mode 2)", ui.home);
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

/* ACID on 2 tracks at most (like RESON's cap): a 3rd track's MODEL knob skips ACID with the message; a project
 * with 4 ACID tracks loads with the first 2, the others back to their power-on kit model */
static void test_acid_cap(void)
{
    uint32_t k, n;
    ui_host_init();
    old_set_model(&trk[0], DM_ACID);
    old_set_model(&trk[1], DM_ACID);
    song.sel = 2;
    old_set_model(&trk[2], DM_FM);
    model_step(1);
    check("ACID cap: a 3rd track's MODEL knob goes past ACID (full) to the next model in the list (SMPL)",
          trk[2].p[P_MODEL] == DM_SMPL && str_eq(ui.msg, "ACID: 2 TRACKS MAX"));
    model_step(-1);
    check("ACID cap: back down, past ACID (full) to FM", trk[2].p[P_MODEL] == DM_FM);
    old_set_model(&trk[1], DM_K808);                /* a place frees */
    model_step(1);
    check("ACID cap: once a place frees, the 3rd track gets ACID", trk[2].p[P_MODEL] == DM_ACID);
    ui_host_init();
    for (k = 0; k < 4u; k++)
        old_set_model(&trk[k], DM_ACID);
    project_save(0);
    project_load(0);
    for (k = 0, n = 0; k < NTRK; k++)
        n += trk[k].p[P_MODEL] == DM_ACID;
    check("ACID cap on load: the first two ACID tracks keep it, the others fall back to their kit model",
          n == 2u && trk[0].p[P_MODEL] == DM_ACID && trk[1].p[P_MODEL] == DM_ACID &&
              trk[2].p[P_MODEL] == KIT_DEF[2][0] && trk[3].p[P_MODEL] == KIT_DEF[3][0] &&
              trk[2].model == KIT_DEF[2][0] && trk[2].p[P_E0] == DMODELS[KIT_DEF[2][0]].edit[0].def);
    memset(&proj_slot[0], 0, sizeof proj_slot[0]);
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
    old_set_model(&trk[1], DM_HATO);
    trk[0].step[0].on = trk[0].step[4].on = trk[0].step[8].on = trk[0].step[12].on = 1;
    trk[1].step[2].on = trk[1].step[6].on = trk[1].step[10].on = trk[1].step[14].on = 1;
    transport_req = 1;
    seq_play_to(0, 1);
    snap_page("comp/02_pumping");
    song.g[G_CMKUP] = 127;                           /* MKUP at the end: Streams' limiter */
    seq_play_to(0, 5);
    snap_page("comp/04_limiter");
    tap(B_HOME);                                     /* a HOME tap on COMP: the HOME screen */
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
    old_set_model(&trk[2], DM_CONGA);
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

/* the LFO button (fixes 2026-10-21 item 3): one page set with both LFOs, LFO 1 on KNOB 1-2 and LFO 2 on KNOB 3-4
 * (WAVE RATE, MORPH PHASE, TRIG MODE), then DEST: the track's 4 slots (KNOB 1 the slot, 2 SRC, 3 DEST, 4 DEPTH). OCT-
 * does nothing there any more; OCT+ the RATE MODE of the LFO turned last (DEST: the slot's source) */
static void test_lfo_pages(void)
{
    uint32_t w, k, pos, on = 0;
    char v[12];
    const char *u;
    ui_host_init();
    tap(B_LFO);
    check("LFO opens LFO 1/4: WAVE RATE of LFO 1, WAVE RATE of LFO 2",
          str_eq(cur_page()->title, "LFO") && page_id(cur_page(), 0) == P_LFO1 + LF_WAVE &&
              page_id(cur_page(), 1) == P_LFO1 + LF_RATE && page_id(cur_page(), 2) == P_LFO2 + LF_WAVE &&
              page_id(cur_page(), 3) == P_LFO2 + LF_RATE && fam_pages(FAM_LFO, &pos) == 4u && pos == 1u);
    param_format(&TP[P_LFO1 + LF_RATE], 23, v, &u);
    check("LFO RATE in SYNC: 23 = 1BAR", str_eq(v, "1BAR"));
    tap(B_OCTUP);
    param_format(&TP[P_LFO1 + LF_RATE], 127, v, &u);
    check("OCT+ on an LFO page (nothing turned yet): LFO 1 -> Hz (RATE 127 = 40.0 Hz)",
          TSEL->p[P_LFO1 + LF_MODE] == LM_HZ && TSEL->p[P_LFO2 + LF_MODE] == LM_SYNC && str_eq(v, "40.0") &&
              str_eq(u, "Hz"));
    turn(EN_K3, 1);
    ui_frame();
    tap(B_OCTUP);
    check("KNOB 3 turns LFO 2's WAVE; OCT+ then switches LFO 2's mode only",
          TSEL->p[P_LFO2 + LF_WAVE] == LW_TRI && TSEL->p[P_LFO2 + LF_MODE] == LM_HZ &&
              TSEL->p[P_LFO1 + LF_MODE] == LM_HZ);
    tap(B_OCTDN);
    check("OCT- on an LFO page: nothing (the same page, the same knobs)",
          ui.page == page_first(FAM_LFO) && page_id(cur_page(), 0) == P_LFO1 + LF_WAVE &&
              TSEL->p[P_LFO2 + LF_MODE] == LM_HZ && TSEL->p[P_LFO1 + LF_MODE] == LM_HZ);
    ui_frame();
    check("LFO pages: OCT+ breathes (it switches a RATE MODE), OCT- dark (merge of items 3 and 5)",
          led_breathes(panel.btn[B_OCTUP]) && !led_on(panel.btn[B_OCTUP]) && !led_on(panel.btn[B_OCTDN]) &&
              !led_breathes(panel.btn[B_OCTDN]));
    for (k = 0; k < 40u; k++) {                      /* the LFO LED steady (2 s, 50 ms frames) */
        host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
        ui_frame();
        on += led_lit(panel.btn[B_LFO]);
    }
    check("LFO pages: the LFO LED steady", on == 40u);
    snap_page("lfo/page1_wave_rate");
    tap(B_LFO);
    check("LFO again: 2/4 MORPH PHASE of LFO 1, MORPH PHASE of LFO 2",
          page_id(cur_page(), 0) == P_LFO1 + LF_MORPH && page_id(cur_page(), 1) == P_LFO1 + LF_PHASE &&
              page_id(cur_page(), 2) == P_LFO2 + LF_MORPH && page_id(cur_page(), 3) == P_LFO2 + LF_PHASE &&
              fam_pages(FAM_LFO, &pos) == 4u && pos == 2u);
    snap_page("lfo/page2_morph_phase");
    tap(B_LFO);
    check("3/4: TRIG MODE of LFO 1, TRIG MODE of LFO 2",
          page_id(cur_page(), 0) == P_LFO1 + LF_TRIG && page_id(cur_page(), 1) == P_LFO1 + LF_MODE &&
              page_id(cur_page(), 2) == P_LFO2 + LF_TRIG && page_id(cur_page(), 3) == P_LFO2 + LF_MODE);
    turn(EN_K2, 1);
    ui_frame();
    check("KNOB 2 on 3/4: LFO 1's MODE (Hz -> TIME)", TSEL->p[P_LFO1 + LF_MODE] == LM_TIME);
    snap_page("lfo/page3_trig_mode");
    tap(B_LFO);
    check("4/4 DEST: KNOB 1 the slot (1), 2 SRC 3 DEST 4 DEPTH of slot 1 (LFO 1's own DEST and DEPTH)",
          str_eq(cur_page()->title, "DEST") && fam_pages(FAM_LFO, &pos) == 4u && pos == 4u && song.lslot == 0u &&
              page_id(cur_page(), 0) == 0xFFu && page_id(cur_page(), 1) == P_LSRC1 &&
              page_id(cur_page(), 2) == P_LFO1 + LF_DEST && page_id(cur_page(), 3) == P_LFO1 + LF_DEPTH);
    turn(EN_K3, 3);
    ui_frame();
    param_format(&TP[P_LFO1 + LF_DEST], TSEL->p[P_LFO1 + LF_DEST], v, &u);
    check("slot 1's DEST shows the track's knob by its label (K909: DEST 3 = SWEEP)", str_eq(v, "SWEEP"));
    turn(EN_K1, 2);
    ui_frame();
    check("KNOB 1: slot 3 (its SRC DEST DEPTH on KNOB 2-4)", song.lslot == 2u && page_id(cur_page(), 1) == P_LS3SRC &&
                                                                 page_id(cur_page(), 2) == P_LS3DEST &&
                                                                 page_id(cur_page(), 3) == P_LS3DEPTH);
    turn(EN_K2, 1);
    ui_frame();
    turn(EN_K3, 2);
    ui_frame();
    turn(EN_K4, 8);
    ui_frame();
    check("slot 3: SRC LFO 2, DEST 2, DEPTH 8; slot 1 kept",
          TSEL->p[P_LS3SRC] == 1 && TSEL->p[P_LS3DEST] == 2 && TSEL->p[P_LS3DEPTH] == 8 &&
              TSEL->p[P_LFO1 + LF_DEST] == 3 && TSEL->p[P_LSRC1] == 0);
    tap(B_OCTUP);
    check("OCT+ on DEST: the selected slot's source (LFO 2) Hz -> TIME", TSEL->p[P_LFO2 + LF_MODE] == LM_TIME &&
                                                                         TSEL->p[P_LFO1 + LF_MODE] == LM_TIME);
    turn(EN_K1, 5);
    ui_frame();
    check("KNOB 1 stops at slot 4", song.lslot == 3u && page_id(cur_page(), 3) == P_LS4DEPTH);
    turn(EN_K1, -1);
    ui_frame();
    render_mix(0, 0, CTL * 20);
    snap_page("lfo/dest_page");
    keys(1u << KEY_TRK_KEY[3]);
    ui_frame();
    keys(0);
    ui_frame();
    check("DEST: a white key selects its track (the slot stays)", song.sel == 3 && song.lslot == 2u &&
                                                                   str_eq(cur_page()->title, "DEST"));
    tap(B_LFO);
    check("LFO on DEST: back to 1/4", ui.page == page_first(FAM_LFO));
    for (w = 0; w < LW_COUNT; w++) {                 /* every waveform's page: LFO 1 this wave on TONE, LFO 2 the
                                                      * next one on LVL, playing */
        char name[40];
        uint32_t w2 = (w + 1u) % LW_COUNT;
        ui_host_init();
        TSEL->p[P_LFO1 + LF_WAVE] = (int16_t)w;
        TSEL->p[P_LFO1 + LF_MORPH] = 64;
        TSEL->p[P_LFO1 + LF_DEST] = 3;
        TSEL->p[P_LFO1 + LF_DEPTH] = 40;
        TSEL->p[P_LFO1 + LF_MODE] = LM_HZ;           /* ~8 Hz: the random waves' trail fills in the frames below */
        TSEL->p[P_LFO1 + LF_RATE] = 100;
        TSEL->p[P_LFO2 + LF_WAVE] = (int16_t)w2;
        TSEL->p[P_LFO2 + LF_DEST] = 9;
        TSEL->p[P_LFO2 + LF_DEPTH] = -30;
        TSEL->p[P_LFO2 + LF_MODE] = LM_HZ;
        TSEL->p[P_LFO2 + LF_RATE] = 90;
        TSEL->p[P_LFO2 + LF_TRIG] = LT_FREE;
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

/* S&H / WANDER / RWALK: a scope, the live value as a dot at the right edge of the LFO's half and its trail to the
 * left; each LFO its own */
static void test_lfo_trail(void)
{
    static int16_t seen[4000];
    uint32_t k, n = 0, ok = 1, distinct = 0, y, n2;
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
    TSEL->p[P_LFO2 + LF_WAVE] = LW_RWALK;            /* LFO 2: its own trail */
    TSEL->p[P_LFO2 + LF_DEST] = 10;
    TSEL->p[P_LFO2 + LF_DEPTH] = 64;
    TSEL->p[P_LFO2 + LF_TRIG] = LT_FREE;
    TSEL->p[P_LFO2 + LF_MODE] = LM_HZ;
    TSEL->p[P_LFO2 + LF_RATE] = (int16_t)k;
    tap(B_LFO);
    for (k = 0; k < 600u; k++) {
        ui_frame();
        seen[n++] = (int16_t)(lfo_out(TSEL, 0) >> 1);
    }
    for (k = 0; k < ui.tr_n[0]; k++) {               /* every trail point is a value the LFO had */
        int16_t tv = ui.tr[0][(ui.tr_h[0] + LTR_N - ui.tr_n[0] + k) % LTR_N];
        uint32_t j, f = 0;
        for (j = 0; j < n && !f; j++)
            f = seen[j] == tv;
        ok &= f;
        distinct += tv != prev;
        prev = tv;
    }
    printf("     LFO trail: %u points, %u changes; LFO 2's %u points\n", (unsigned)ui.tr_n[0], (unsigned)distinct,
           (unsigned)ui.tr_n[1]);
    check("LFO S&H trail: full width, made of the LFO's values, several steps", ui.tr_n[0] == LTR_N && ok && distinct >= 3u);
    check("LFO 2 (RWALK) has its own full trail", ui.tr_n[1] == LTR_N);
    y = (uint32_t)(Y_GRAPH + G_OY + LG_CY - lfo_out(TSEL, 0) * LG_A / 32767);
    check("LFO S&H: the dot at the right edge of LFO 1's half at the live value",
          fb[y * 240u + LG_X0 + LTR_N - 1u] == C_INK);
    check("LFO 1/4 shows TRIG and the routing too (the line under the pictures)",
          ui.page == page_first(FAM_LFO) && fb_lit(Y_GRAPH + G_OY + 84u, Y_GRAPH + G_OY + 96u) > 40u);
    snap_page("lfo/trail_SnH");
    n2 = ui.tr_n[1];
    TSEL->p[P_LFO1 + LF_WAVE] = LW_WANDER;
    ui_frame();
    check("LFO trail: LFO 1's wave changed starts its trail over, LFO 2's goes on", ui.tr_n[0] <= 1u && ui.tr_n[1] >= n2);
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    check("LFO trail: another track starts both over", ui.tr_n[0] <= 1u && ui.tr_n[1] <= 1u);
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
    press(B_SEQ);                                    /* PATTERN page: the keys stay the steps (user, 2026-10-08) */
    ui_frame();
    release_all();
    check("PATTERN page: the keys stay the steps", song.seq_mode == 1u);
}

static void test_bank_follows_len(void)
{
    ui_host_init();
    trk[0].p[P_SLEN] = 64;
    open_step_page();
    tap(B_OCTUP);                                    /* (the bank on OCT+'s release: item 1) */
    tap(B_OCTUP);
    tap(B_OCTUP);
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

/* HOME's two modes (spec §3): 1 TRACKS (the mixer: TRACK LEVEL LEN PAN, the keys as on TRACKS), 2 HOME (the scope,
 * the selected track's macros); a HOME tap on HOME toggles them, elsewhere it opens the mode last shown; power-on:
 * TRACKS */
static void test_home_modes(void)
{
    uint32_t k;
    safe_boot(0);                                    /* power-on */
    ui_frame();
    check("power-on: HOME's TRACKS mode (the TRACKS mixer, the footer TRACKS), HOME lit, REC dark",
          mix_mode() && !ui.home && str_eq(cur_page()->title, "TRACKS") && fam_pages(cur_page()->fam, &k) == 1u &&
              led_lit(panel.btn[B_HOME]) && !led_lit(panel.btn[B_REC]) && song.seq_mode == 1u);
    snap_page("fixes/home_tracks");
    turn(EN_K1, 1);
    ui_frame();
    check("TRACKS mode: KNOB 1 selects the track", song.sel == 1);
    turn(EN_K1, -1);
    ui_frame();
    tap(B_HOME);
    check("a HOME tap on TRACKS: HOME's second mode (the HOME screen), HOME lit", ui.home && !mix_mode() &&
          led_lit(panel.btn[B_HOME]) && song.seq_mode == 0u);
    snap_page("fixes/home_mode2");
    tap(B_HOME);
    check("a HOME tap on the HOME screen: TRACKS again", mix_mode());
    tap(B_EDIT);
    tap(B_HOME);
    check("from EDIT a HOME tap opens HOME in the mode last shown (TRACKS)", mix_mode());
    tap(B_HOME);
    tap(B_GLO);
    tap(B_HOME);
    check("from GLO a HOME tap opens HOME in the mode last shown (the HOME screen)", ui.home);
    sel(1);
    sel(1);
    tap(B_SEQ);
    tap(B_HOME);
    check("COMP 3/3, then SEQ, then HOME: the HOME screen (COMP is the second mode's)", ui.home);
    tap(B_HOME);
    hold(B_HOME);                                    /* the menu over TRACKS, and closed again */
    check("HOME held on TRACKS: the menu", ui.menu == 1u);
    hold(B_HOME);
    check("the menu closed: HOME in the mode last shown (TRACKS)", !ui.menu && mix_mode());
    {
        static const uint8_t B[] = {B_EDIT, B_FX, B_SEQ, B_GLO, B_SAVE, B_ARP, B_LFO, B_ENV, B_SCL, B_REC};
        uint32_t b, any = 0;
        for (b = 0; b < sizeof B; b++) {             /* each button tapped on and on: never TRACKS */
            ui_host_init();
            for (k = 0; k < 4u; k++) {
                tap(B[b]);
                any |= (uint32_t)mix_mode();
            }
            transport_req = 2;
            ui_frame();
        }
        check("no page button and no REC opens TRACKS (HOME's)", !any);
    }
    ui_host_init();                                  /* the screens with a pattern: TRACKS, then HOME's second mode */
    kit_session();
    open_tracks();
    snap_page("fixes/home_tracks_kit");
    tap(B_HOME);
    snap_page("fixes/home_mode2_kit");
    transport_req = 2;
    ui_frame();
}

/* REC (spec §4): a tap arms / disarms the selected track on every screen (arming while stopped starts play); never
 * opens TRACKS; the menu, NAME and a confirm keep it */
static void test_rec_everywhere(void)
{
    static const struct { const char *what; uint32_t btn, n; } S[] = {
        {"HOME (mode 2)", B_HOME, 0}, {"HOME's TRACKS mode", B_HOME, 1}, {"EDIT", B_EDIT, 1}, {"FX", B_FX, 1},
        {"GLO", B_GLO, 1}, {"SEQ PATTERN", B_SEQ, 2}, {"LFO", B_LFO, 1}, {"SAVE", B_SAVE, 1},
    };
    uint32_t i, j, pg, home;
    char what[96];
    for (i = 0; i < sizeof S / sizeof S[0]; i++) {
        ui_host_init();
        for (j = 0; j < S[i].n; j++)
            tap(S[i].btn);
        turn(EN_ALGO, 1);                            /* track 2 */
        ui_frame();
        pg = ui.page;
        home = ui.home;
        tap(B_REC);
        snprintf(what, sizeof what, "%s: a REC tap arms track 2, starts play, the screen stays, REC lit", S[i].what);
        check(what, song.rec == 2u && song.playing && ui.page == pg && ui.home == home && led_lit(panel.btn[B_REC]));
        tap(B_REC);
        snprintf(what, sizeof what, "%s: a REC tap again disarms it (still playing), REC dark", S[i].what);
        check(what, song.rec == 0u && song.playing && ui.page == pg && ui.home == home && !led_lit(panel.btn[B_REC]));
        transport_req = 2;
        ui_frame();
    }
    ui_host_init();
    tap(B_EDIT);
    pg = ui.page;
    hold(B_REC);
    check("REC held long off the SEQ pages: no dialog; the release arms (a tap)", !ui.confirm && song.rec == 1u &&
          ui.page == pg);
    transport_req = 2;
    ui_frame();
    ui_host_init();
    hold(B_HOME);                                    /* the menu */
    tap(B_REC);
    check("REC in the menu: nothing", ui.menu == 1u && song.rec == 0u && !song.playing);
    hold(B_HOME);
    ui_host_init();
    trk[0].step[0].on = 1;
    seq_open("STEP");
    hold(B_REC);                                     /* REC held on SEQ: CLEAR TRACK? */
    tap(B_REC);
    check("REC on the CLEAR TRACK dialog: nothing (the dialog stays)", ui.confirm == 1u && song.rec == 0u);
    tap(B_OCTDN);
    check("REC held on SEQ: the CLEAR TRACK dialog still (OCT- keeps the steps)", !ui.confirm && trk[0].step[0].on);
}

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

/* HOME's TRACKS mode: a white key is a step of the selected track (nothing armed); the mutes themselves
 * (track_mute_toggle: a mute cuts what rings; NEXT BAR waits for the bar, at once when stopped) until the MUTE screen
 * drives them; leaving TRACKS the keys play again and the mutes stay */
static void test_quick_mute(void)
{
    uint32_t a, k, ok = 1;
    ui_host_init();
    settings.mutebar = 0;
    open_tracks();
    trk[5].p[P_MUTE] = 1;
    a = hit_age(&trk[2]);
    keys(1u << KEY_TRK_KEY[2]);
    ui_frame();
    keys(0);
    ui_frame();
    check("TRACKS: white key 3 is step 3 of the selected track: no select, no mute, no sound",
          song.sel == 0 && trk[2].p[P_MUTE] == 0 && hit_age(&trk[2]) == a && trk[0].step[2].on);
    for (k = 0; k < NTRK; k++)
        ok &= led_lit(14u + KEY_TRK_KEY[k]) == trk[0].step[k].on;
    check("TRACKS: the keys show the steps, not the mutes", ok);
    drum_hit(&trk[2], 127);                          /* track 3 ringing */
    track_mute_toggle(2);
    for (k = 0; k < 5u; k++)
        ui_frame();
    check("a mute: track 3 muted, its ringing voice cut", trk[2].p[P_MUTE] == 1 && !trk[2].v[0].active &&
          !trk[2].v[1].active);
    track_mute_toggle(2);
    check("again: unmuted", trk[2].p[P_MUTE] == 0);
    settings.mutebar = 1;                            /* MUTE: NEXT BAR */
    transport_req = 1;
    seq_play_to(0, 2);
    track_mute_toggle(0);
    ui_frame();
    check("NEXT BAR while playing: the mute waits (track 1 not muted yet)", trk[0].p[P_MUTE] == 0 && (song.mute_q & 1u));
    seq_play_to(0, 15);
    check("still waiting on the bar's last step", trk[0].p[P_MUTE] == 0);
    seq_play_to(0, 0);
    check("at the bar (track 1's step 1): track 1 muted", trk[0].p[P_MUTE] == 1 && !song.mute_q);
    settings.mutebar = 0;
    track_mute_toggle(0);
    check("NOW while playing: unmuted at once", trk[0].p[P_MUTE] == 0);
    transport_req = 2;
    ui_frame();
    settings.mutebar = 1;
    track_mute_toggle(3);
    ui_frame();
    check("NEXT BAR but stopped: a mute is at once", trk[3].p[P_MUTE] == 1);
    settings.mutebar = 0;
    tap(B_HOME);                                     /* HOME's second mode: keys play again; track 6 stays muted */
    a = hit_age(&trk[0]);
    keys(1u << KEY_TRK_KEY[0]);
    ui_frame();
    keys(0);
    ui_frame();
    check("leaving TRACKS: the keys play again, the mutes stay", ui.home && hit_age(&trk[0]) != a &&
          trk[5].p[P_MUTE] == 1 && trk[0].p[P_MUTE] == 0);
}

/* spec §5: the MUTE screen. GLO tapped (let go within 0.4 s, no key meanwhile): the GLOBAL pages, on the release; GLO
 * held 0.4 s, or a key pressed with it: the MUTE screen while GLO is held (the white keys F3..F4 toggle the tracks'
 * mutes, top C# / D# the mode; the keys play nothing); let go: the screen before */
static uint32_t glo_bit(void) { return 1u << panel.btn[B_GLO]; }
static void glo_hold_frames(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        fm1_in.buttons |= glo_bit();
        ui_frame();
    }
}
static void glo_key(uint32_t key)                    /* a key tapped while GLO is held */
{
    keys(1u << key);
    glo_hold_frames(2);                              /* (the render hands the key over after the UI's pass) */
    keys(0);
    glo_hold_frames(1);
}
static int mute_box_filled(uint32_t k)               /* track k's box: lit inside (audible), not just its outline */
{
    return fb[(MU_BY(k) + MU_BH - 3u) * 240u + MU_BX(k) + 3u] == C_INK;
}
static void test_mute_screen(void)
{
    uint32_t a, k, ok, l1, l2;
    ui_host_init();
    settings.mutebar = 0;
    kit_session();                                   /* track 7 muted */
    song.rec = 0;
    press(B_GLO);
    ui_frame();
    check("GLO pressed: nothing yet (GLOBAL waits for the release)", ui.home && !ui.mute);
    release_all();
    ui_frame();
    check("GLO tapped: GLOBAL 1/3 on the release, no MUTE screen",
          !ui.home && cur_fam() == FAM_GLO && ui.page == page_first(FAM_GLO) && !ui.mute);
    tap(B_GLO);
    tap(B_GLO);
    check("GLO taps go on: SYSTEM 2/3, GLOBAL 3/3", !ui.home && ui.page == page_first(FAM_GLO) + 2u);
    tap(B_HOME);
    check("(HOME again)", ui.home);
    press(B_GLO);
    glo_hold_frames(150);
    check("GLO held 0.15 s: no screen yet", !ui.mute && ui.home);
    glo_hold_frames(300);
    check("GLO held 0.4 s: the MUTE screen over HOME", ui.mute && ui.home);
    snap_page("fixes/mute_screen");
    ok = 1;
    for (k = 0; k < NTRK; k++)
        ok &= led_lit(14u + KEY_TRK_KEY[k]) == !trk[k].p[P_MUTE] && mute_box_filled(k) == !trk[k].p[P_MUTE];
    check("MUTE screen: boxes filled and keys lit for the audible tracks (track 7 muted: outline, dark)", ok);
    check("MUTE screen: top C# lit (NOW), top D# dark", led_lit(14u + 20u) && !led_lit(14u + 22u));
    a = hit_age(&trk[2]);
    glo_key(KEY_TRK_KEY[2]);
    check("MUTE screen: key 3 mutes track 3; no sound, no track select", trk[2].p[P_MUTE] == 1 &&
          hit_age(&trk[2]) == a && song.sel == 0 && !mute_box_filled(2) && !led_lit(14u + KEY_TRK_KEY[2]));
    glo_key(KEY_TRK_KEY[6]);
    check("MUTE screen: key 7 unmutes track 7", trk[6].p[P_MUTE] == 0 && mute_box_filled(6));
    glo_key(22u);                                    /* top D# */
    check("MUTE screen: top D# = NEXT BAR (the setting), its key lit", settings.mutebar == 1u && led_lit(14u + 22u) &&
          !led_lit(14u + 20u));
    transport_req = 1;
    seq_play_to(0, 2);
    glo_key(KEY_TRK_KEY[0]);
    check("NEXT BAR while playing: track 1's mute waits for the bar", trk[0].p[P_MUTE] == 0 && (song.mute_q & 1u));
    host_ticks += 250u * 1000u * FM1_TICKS_PER_US;
    glo_hold_frames(1);
    l1 = led_lit(14u + KEY_TRK_KEY[0]);
    a = mute_box_filled(0);
    host_ticks += 250u * 1000u * FM1_TICKS_PER_US;
    glo_hold_frames(1);
    l2 = led_lit(14u + KEY_TRK_KEY[0]);
    check("the waiting mute blinks (key LED and box, 250 ms)", l1 != l2 && a != (uint32_t)mute_box_filled(0));
    for (k = 0; k < 400u && mute_box_filled(0); k++)  /* the screenshot in the outline phase */
        glo_hold_frames(1);
    shot("build/ui_shots/fixes/mute_waiting.ppm");
    seq_play_to(0, 0);
    check("at the bar: track 1 muted", trk[0].p[P_MUTE] == 1 && !song.mute_q);
    glo_key(20u);                                    /* top C# */
    check("MUTE screen: top C# = NOW", settings.mutebar == 0u);
    turn(EN_K2, 5);
    a = (uint32_t)trk[0].p[P_E1];
    glo_hold_frames(1);
    check("MUTE screen: the knobs edit nothing under it", (uint32_t)trk[0].p[P_E1] == a);
    release_all();
    ui_frame();
    check("GLO let go: the screen gone, HOME as before (no GLOBAL page: not a tap)", !ui.mute && ui.home);
    transport_req = 2;
    ui_frame();
    settings.mutebar = 0;
}

/* Review focus 1: keys with GLO never play or record; GLO let go with a key held leaves nothing stuck */
static void test_mute_screen_keys(void)
{
    uint32_t a, sel, n, k;
    step_t s0;
    ui_host_init();
    settings.mutebar = 0;
    a = hit_age(&trk[4]);
    sel = song.sel;
    press(B_GLO);
    keys(1u << KEY_TRK_KEY[4]);                      /* GLO and a white key in the same frame */
    glo_hold_frames(2);
    check("GLO + a key in the same frame: the MUTE screen, track 5 muted, no sound, no select",
          ui.mute && trk[4].p[P_MUTE] == 1 && hit_age(&trk[4]) == a && song.sel == sel);
    release_all();                                   /* GLO let go, the key still held */
    ui_frame();
    ui_frame();
    check("GLO let go with the key held: the screen gone, no GLOBAL page, still no sound",
          !ui.mute && ui.home && hit_age(&trk[4]) == a && (kb_mute >> KEY_TRK_KEY[4]) & 1u);
    keys(0);
    ui_frame();
    check("the key let go: nothing held, no sound", !kb_mute && hit_age(&trk[4]) == a);
    trk[4].p[P_MUTE] = 0;
    press(B_GLO);
    glo_hold_frames(100);
    release_all();
    ui_frame();
    check("GLO held 0.1 s alone: a tap, GLOBAL", !ui.home && cur_fam() == FAM_GLO && !ui.mute);
    tap(B_HOME);
    press(B_GLO);
    glo_hold_frames(2);
    glo_key(KEY_TRK_KEY[1]);
    release_all();
    ui_frame();
    check("GLO with a key, let go quickly: no GLOBAL page (not a tap)", ui.home && trk[1].p[P_MUTE] == 1);
    trk[1].p[P_MUTE] = 0;
    ui_frame();
    a = hit_age(&trk[0]);                            /* the ISR before the UI's pass sees GLO */
    fm1_in.buttons |= glo_bit();
    keys(1u << KEY_TRK_KEY[0]);
    render_mix(0, 0, CTL);
    check("GLO + a key before the UI's pass: the key is the screen's, no note", (kb_mute & 1u) && hit_age(&trk[0]) == a);
    glo_hold_frames(2);
    check("then the UI: the MUTE screen, track 1 muted", ui.mute && trk[0].p[P_MUTE] == 1);
    keys(0);
    release_all();
    ui_frame();
    trk[0].p[P_MUTE] = 0;
    tap(B_SCL);                                      /* KEYS on: the keys play the selected track */
    a = hit_age(&trk[0]);
    press(B_GLO);
    keys(1u << 3);
    glo_hold_frames(2);
    keys(0);
    glo_hold_frames(1);
    check("KEYS: GLO + a key plays no note", hit_age(&trk[0]) == a && !kz.key && ui.mute);
    release_all();
    ui_frame();
    tap(B_SCL);                                      /* KEYS off */
    open_tracks();
    tap(B_REC);                                      /* track 1 armed, playing: the keys play and record */
    seq_play_to(0, 3);
    for (k = 0, n = 0; k < 16u; k++)
        n += trk[0].step[k].on;
    a = hit_age(&trk[0]);
    press(B_GLO);
    glo_key(KEY_TRK_KEY[0]);
    for (k = 0, sel = 0; k < 16u; k++)
        sel += trk[0].step[k].on;
    check("TRACKS armed: GLO + key 1 records nothing, plays nothing, mutes track 1",
          sel == n && hit_age(&trk[0]) == a && trk[0].p[P_MUTE] == 1);
    release_all();
    ui_frame();
    check("GLO let go: TRACKS again, still armed", mix_mode() && song.rec == 1u && !ui.mute);
    tap(B_REC);
    transport_req = 2;
    ui_frame();
    trk[0].p[P_MUTE] = 0;
    seq_open("STEP");
    s0 = TSEL->step[0];
    press(B_GLO);
    glo_key(0);
    release_all();
    ui_frame();
    check("STEP grid: GLO + key 1 toggles no step (track 1 muted instead)", TSEL->step[0].on == s0.on &&
          trk[0].p[P_MUTE] == 1);
    trk[0].p[P_MUTE] = 0;
}

/* spec §5: not in the menu, NAME, a confirm or the PERFORM layer */
static void test_mute_screen_refused(void)
{
    uint32_t i;
    ui_host_init();
    ui.menu = 1;
    ui.force = 1;
    press(B_GLO);
    glo_hold_frames(450);
    glo_key(KEY_TRK_KEY[0]);
    check("menu: GLO held, no MUTE screen, no mute", !ui.mute && trk[0].p[P_MUTE] == 0);
    menu_close();
    glo_hold_frames(450);
    check("GLO held out of the menu: dead (no screen)", !ui.mute);
    release_all();
    ui_frame();
    check("..and its release is no tap", ui.home);
    ui.confirm = 1;
    ui.force = 1;
    press(B_GLO);
    glo_hold_frames(450);
    check("confirm: no MUTE screen", !ui.mute);
    release_all();
    ui_frame();
    ui.confirm = 0;
    ui.force = 1;
    ui_frame();
    project_save(0);
    name_open(NK_RENAME, 0);
    press(B_GLO);
    glo_hold_frames(450);
    check("NAME: no MUTE screen", !ui.mute && name_on());
    release_all();
    ui_frame();
    name_close();
    ui_frame();
    press(B_FX);
    for (i = 0; i < 500u; i++) {
        fm1_in.buttons |= 1u << panel.btn[B_FX];
        ui_frame();
    }
    press(B_GLO);
    for (i = 0; i < 450u; i++) {
        fm1_in.buttons |= 1u << panel.btn[B_FX] | glo_bit();
        ui_frame();
    }
    check("PERFORM layer: no MUTE screen", ui.layer && !ui.mute);
    release_all();
    ui_frame();
    check("PERFORM layer: GLO a plain button there (its release opens GLOBAL)", !ui.home && cur_fam() == FAM_GLO);
    press(B_GLO);
    glo_hold_frames(450);
    press(B_FX);
    for (i = 0; i < 500u; i++) {
        fm1_in.buttons |= 1u << panel.btn[B_FX] | glo_bit();
        ui_frame();
    }
    check("MUTE screen up: FX opens no layer", ui.mute && !ui.layer && !perf_mask);
    release_all();
    ui_frame();
    ui_frame();
    check("all let go: neither, no page change (not a tap)", !ui.mute && !ui.layer && cur_fam() == FAM_GLO &&
          ui.page == page_first(FAM_GLO));
}

/* HOME's TRACKS mode while a track is armed: the white keys play (and record) their tracks instead of the steps; REC
 * held is no mute key (its release is a REC tap: disarms); disarmed: the steps again */
static void test_tracks_rec_keys(void)
{
    uint32_t a, k, n = 0;
    ui_host_init();
    settings.mutebar = 0;
    for (k = 0; k < 16u; k++)
        trk[1].step[k].on = 0;
    open_tracks();
    turn(EN_K1, 1);                                  /* track 2 */
    ui_frame();
    tap(B_REC);                                      /* arms track 2, starts play */
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
    press(B_REC);                                    /* REC held: no longer the mute key */
    ui_frame();
    a = hit_age(&trk[3]);
    keys(1u << KEY_TRK_KEY[3]);
    press(B_REC);
    ui_frame();
    keys(0);
    press(B_REC);
    ui_frame();
    check("TRACKS, armed, REC held: key 4 plays track 4 (no mute)", trk[3].p[P_MUTE] == 0 && hit_age(&trk[3]) != a &&
          (song.rec & 2u));
    release_all();                                   /* REC let go: a tap, track 2 disarmed */
    ui_frame();
    a = hit_age(&trk[5]);
    keys(1u << KEY_TRK_KEY[5]);
    ui_frame();
    keys(0);
    ui_frame();
    check("TRACKS, disarmed: key 6 is step 6 of track 2, no select, no sound",
          song.rec == 0 && song.sel == 1 && hit_age(&trk[5]) == a && song.seq_mode == 1u);
    transport_req = 2;
    ui_frame();
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
    for (y = 17u, amb = 0; y < 68u; y++)
        for (x = 126; x < 163u; x++)  /* the strip's cell 2: its knob */
            amb += fb[y * 240u + x] == C_INK;
    x = amb;                                         /* (the knob without an LFO dot) */
    check("HOME: no LFO, no marker on the knobs", amb > 0u);
    TSEL->p[P_LFO1 + LF_WAVE] = LW_SQUARE;
    TSEL->p[P_LFO1 + LF_MODE] = LM_HZ;
    TSEL->p[P_LFO1 + LF_RATE] = 60;
    TSEL->p[P_LFO1 + LF_DEST] = 2;                   /* the engine's 2nd knob: HOME's second column */
    TSEL->p[P_LFO1 + LF_DEPTH] = 40;
    for (a = 0; a < 50u; a++)
        ui_frame();
    a = x;
    for (y = 17u, amb = 0; y < 68u; y++)
        for (x = 126; x < 163u; x++)  /* the strip's cell 2: its knob */
            amb += fb[y * 240u + x] == C_INK;
    check("HOME: an LFO on the second knob marks its live value (a hollow dot outside the ring)", amb >= a + 6u);
    snap_page("lfo/91_home_marker");
}

/* GLOBAL 3/3: MUTE NOW / BAR, the device setting the mutes follow (track_mute_toggle) */
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

/* GLOBAL 3/3, KNOB 2: METRO OFF / LOW / MID / HIGH (spec 2026-10-11-fixes §8), the device setting fx_metro (fx.c's
 * click); not in projects */
static void test_global_metro_setting(void)
{
    uint32_t k, ok = 1;
    int16_t *vp;
    const param_desc_t *d;
    char v[12];
    const char *u;
    static const char *const NAMES[4] = {"OFF", "LOW", "MID", "HIGH"};
    ui_host_init();
    fx_metro = 0;
    for (k = 0; k < 6u && (ui.home || cur_page()->id[0] != G_MUTEBAR); k++)
        tap(B_GLO);
    d = page_desc(cur_page(), 1, &vp);
    check("GLOBAL 3/3: KNOB 2 is METRO, OFF", d && str_eq(d->label, "METRO") && vp && *vp == 0);
    for (k = 0; k < 4u; k++) {
        param_format(d, (int32_t)k, v, &u);
        ok &= str_eq(v, NAMES[k]);
    }
    check("METRO values: OFF LOW MID HIGH", ok);
    turn(EN_K2, 2);
    ui_frame();
    check("GLOBAL 3/3: KNOB 2 -> MID (the setting; the knob shows it)", fx_metro == 2u && song.act[3] == 2);
    snap_page("fixes/global_3_metro");
    turn(EN_K2, 5);
    ui_frame();
    check("KNOB 2 up: HIGH at the end", fx_metro == 3u);
    turn(EN_K2, -9);
    ui_frame();
    check("KNOB 2 down: OFF", fx_metro == 0u && song.act[3] == 0);
    fx_metro = 1;
    ui_frame();
    check("the knob follows the setting (as MUTE's)", song.act[3] == 1);
    fx_metro = 0;
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

static uint32_t fb_lit(uint32_t y0, uint32_t y1)    /* drawn pixels in rows y0..y1: neither the theme's background
                                                     * nor black (the screens not redrawn yet), the frame's edge
                                                     * columns x 0 / 239 left out */
{
    uint32_t n = 0, i;
    for (i = y0 * 240u; i < y1 * 240u; i++)
        n += fb[i] != C_BG && fb[i] != 0 && i % 240u != 0 && i % 240u != 239u;
    return n;
}

/* spec 2026-10-11-fixes §12: the key backlight. The level ui_leds gave key / button id: 100 full, 25, 5 or 0 (the
 * dimmed layers in led_lay while BACKLIGHT is ON, else fm1_led) */
static uint32_t led_level(uint32_t id)
{
    uint8_t q = led_pos[id];
    uint32_t lay, bit;
    if (q == 0xFF)
        return 0;
    bit = 1u << (q & 7u);
    if (!led_dim_on)
        return fm1_led[q >> 3] & bit ? 100u : 0u;
    lay = led_lay[q >> 3];
    return lay & bit ? 100u : (lay >> 8) & bit ? 25u : (lay >> 16) & bit ? 5u : (lay >> 24) & bit ? 60u : 0u;
}
static int keys_dim_except(uint32_t mask)          /* every key not in mask at 5 % */
{
    uint32_t k, ok = 1;
    for (k = 0; k < 27u; k++)
        if (!((mask >> k) & 1u))
            ok &= led_level(14u + k) == 5u;
    return (int)ok;
}
static void test_backlight_leds(void)
{
    uint32_t k, ok, full[FM1_NCOL], c;
    ui_host_init();
    backlight = 0;
    for (k = 0; k < NSTEP; k++)
        trk[0].step[k].on = 0;
    trk[0].p[P_SLEN] = 32;
    trk[0].step[1].on = trk[0].step[4].on = 1;      /* steps 2 and 5 (5: a beat) */
    ui_frame();                                      /* (the HOME screen: no OCT LED breathes) */
    check("BACKLIGHT OFF, nothing breathing: ui_leds writes fm1_led, no dimming (the hook idle)",
          !led_dim_on && led_level(14u + KEY_TRK_KEY[0]) == 100u && led_level(14u + 1u) == 0u);
    seq_open("STEP");
    ui_frame();
    ok = 1;
    for (c = 0; c < FM1_NCOL; c++)
        ok &= (led_lay[c] & 0xFFFF00u) == 0u;
    check("BACKLIGHT OFF, STEP (2 banks): the hook on for OCT+'s breath alone (item 5): the keys full or dark, no dim",
          led_dim_on && ok && led_breathes(panel.btn[B_OCTUP]) && led_level(14u + STEP_KEY[1]) == 100u &&
          led_level(14u + STEP_KEY[0]) == 0u && led_level(14u + 1u) == 0u);
    backlight = 1;
    ui_frame();
    ok = led_dim_on && led_level(14u + STEP_KEY[1]) == 100u && led_level(14u + STEP_KEY[4]) == 100u;
    ok &= led_level(14u + STEP_KEY[0]) == 25u && led_level(14u + STEP_KEY[8]) == 25u &&
          led_level(14u + STEP_KEY[12]) == 25u;
    for (k = 0; k < 16u; k++)
        if (k != 0u && k != 1u && k != 4u && k != 8u && k != 12u)
            ok &= led_level(14u + STEP_KEY[k]) == 5u;
    for (k = 0; k < 27u; k++)                        /* the black keys */
        if (k != 0u && k != 2u && k != 4u && k != 6u && k != 7u && k != 9u && k != 11u && k != 12u && k != 14u &&
            k != 16u && k != 18u && k != 19u && k != 21u && k != 23u && k != 24u && k != 26u)
            ok &= led_level(14u + k) == 5u;
    check("BACKLIGHT ON, STEP: on steps full, off beats 1 5 9 13 at 25 %, the other keys at 5 %", ok);
    check("BACKLIGHT ON (DIM LO): the buttons: SEQ lit, the idle ones at 5 % (SAVE, OCT-: no bank before), OCT+ breathing",
          led_level(panel.btn[B_SEQ]) == 100u && led_level(panel.btn[B_SAVE]) == 5u &&
          led_level(panel.btn[B_OCTUP]) == 60u && led_level(panel.btn[B_OCTDN]) == 5u);
    for (c = 0; c < FM1_NCOL; c++)
        full[c] = led_lay[c] & 0xFF0000FFu;          /* (the full and breath layers) */
    tap(B_OCTUP);                                    /* bank 2: steps 17..32, step 21 on */
    trk[0].step[20].on = 1;
    ui_frame();
    check("BACKLIGHT ON, bank 2: the beats are 17 21 25 29 (keys 1 5 9 13); step 21 on: full",
          ui.bank == 1u && led_level(14u + STEP_KEY[0]) == 25u && led_level(14u + STEP_KEY[4]) == 100u &&
          led_level(14u + STEP_KEY[8]) == 25u && led_level(14u + STEP_KEY[12]) == 25u &&
          led_level(14u + STEP_KEY[1]) == 5u);
    tap(B_OCTDN);
    backlight = 0;
    ui_frame();
    ok = led_dim_on;
    for (c = 0; c < FM1_NCOL; c++)
        ok &= led_lay[c] == full[c];
    check("BACKLIGHT OFF again: the full and breath layers as with it ON, nothing dim", ok);
    backlight = 1;
    transport_req = 1;                               /* the playhead: on step 2 (on) it goes out, so it dims; on
                                                      * step 3 (off) it lights full */
    seq_play_to(0, 1);
    ui_leds();                                       /* (the frame lit the LEDs before the step moved) */
    ok = led_level(14u + STEP_KEY[1]) == 5u;
    seq_play_to(0, 2);
    ui_leds();
    ok &= led_level(14u + STEP_KEY[2]) == 100u;
    seq_play_to(0, 4);
    ui_leds();
    ok &= led_level(14u + STEP_KEY[4]) == 25u;
    check("BACKLIGHT ON, playing: the playhead lights an off step full; on an on step it dims (5 %, a beat 25 %)", ok);
    transport_req = 2;
    ui_frame();
    ui_frame();
    ui.home = 1;                                     /* HOME's second mode: the keys play */
    ui.hmode = 1;
    ui.force = 1;
    ui_frame();
    keys(1u << 3);
    ui_frame();
    check("BACKLIGHT ON, HOME (the keys play): a held key and the selected track's (item 2) full, every other key 5 %",
          !step_keys() && led_level(14u + 3u) == 100u && led_level(14u + KEY_TRK_KEY[0]) == 100u &&
          keys_dim_except(1u << 3 | 1u << KEY_TRK_KEY[0]));
    keys(0);
    ui_frame();
    {                                                /* the MUTE screen: audible tracks' keys full, the rest 5 % */
        uint32_t m = 0;
        trk[5].p[P_MUTE] = 1;
        press(B_GLO);
        glo_hold_frames(500);
        for (k = 0; k < NTRK; k++)
            if (!trk[k].p[P_MUTE])
                m |= 1u << KEY_TRK_KEY[k];
        m |= 1u << 20;                               /* top C#: MUTE NOW */
        check("BACKLIGHT ON, MUTE screen: audible tracks and the mode key full, every other key 5 %",
              ui.mute && led_level(14u + KEY_TRK_KEY[5]) == 5u && led_level(14u + KEY_TRK_KEY[0]) == 100u &&
              keys_dim_except(m));
        release_all();
        ui_frame();
        trk[5].p[P_MUTE] = 0;
    }
    {                                                /* PERFORM: lit keys full, dark ones 5 % */
        uint32_t m = 0, dim = 0;
        press(B_FX);
        fx_hold_frames(500);
        for (k = 0; k < 27u; k++) {
            uint32_t l = led_level(14u + k);
            m += l == 100u;
            dim += l == 5u;
        }
        check("BACKLIGHT ON, PERFORM: its lit keys full, every other key 5 %", ui.layer && m > 0u && m + dim == 27u);
        release_all();
        ui_frame();
    }
    safe_start = 1;
    ui_frame();
    ok = !led_dim_on;
    for (c = 0; c < FM1_NCOL; c++)
        ok &= fm1_led[c] == 0u;
    check("BACKLIGHT ON in safe start: all LEDs off, the hook idle", ok);
    safe_start = 0;
    backlight = 0;
    ui_frame();
}

/* the beat marks on every screen where the keys are steps (user, 2026-10-12: "the divisions must be visible on every
 * screen when sequencer on keys available"): every page of the table and the HOME screen, KEYS off / on, the track
 * armed or not. Where step_keys(): the bank's on steps full, its unlit steps 1 5 9 13 at 25 %, the rest 5 %, in bank 1
 * and bank 2 (OCT+, as the keys' own bank); elsewhere no key at 25 %. */
static int beats_shown(uint32_t bank)               /* track 0's pattern below; every other key 5 % */
{
    uint32_t k, ok = 1, steps = 0;
    for (k = 0; k < 16u; k++) {
        uint32_t si = bank * 16u + k, l = led_level(14u + STEP_KEY[k]);
        steps |= 1u << STEP_KEY[k];
        ok &= trk[0].step[si].on ? l == 100u : (k & 3u) == 0u ? l == 25u : l == 5u;
    }
    for (k = 0; k < 27u; k++)
        if (!((steps >> k) & 1u))
            ok &= led_level(14u + k) == 5u;
    return (int)ok;
}
static int no_beat_layer(void)
{
    uint32_t k, ok = 1;
    for (k = 0; k < 27u; k++)
        ok &= led_level(14u + k) != 25u;
    return (int)ok;
}
static void test_backlight_beats_everywhere(void)
{
    uint32_t i, kys, arm, n_steps = 0, n_other = 0, seq_seen = 0, mix_seen = 0, ok_steps = 1, ok_other = 1;
    char bad[160] = "";
    ui_host_init();
    for (i = 0; i < NSTEP; i++)
        trk[0].step[i].on = 0;
    trk[0].p[P_SLEN] = 32;
    trk[0].step[1].on = trk[0].step[4].on = 1;       /* bank 1: steps 2, 5 (a beat) */
    trk[0].step[17].on = trk[0].step[28].on = 1;     /* bank 2: steps 18, 29 (a beat) */
    backlight = 1;
    for (kys = 0; kys < 2u; kys++)
        for (arm = 0; arm < 2u; arm++)
            for (i = 0; i <= NPAGES; i++) {          /* (NPAGES: the HOME screen) */
                ui.home = i == NPAGES;
                ui.hmode = ui.home;
                if (i < NPAGES)
                    ui.page = (uint8_t)i;
                ui.keys = (uint8_t)kys;
                song.rec = (uint8_t)arm;
                ui.bank = 0;
                page_entered();
                ui_frame();
                if (step_keys()) {
                    int ok = ui.bank == 0u && beats_shown(0);
                    tap(B_OCTUP);
                    ok &= ui.bank == 1u && beats_shown(1);
                    tap(B_OCTDN);
                    ok &= ui.bank == 0u && beats_shown(0);
                    if (!ok && str_len(bad) < 120u) {
                        str_cpy(bad + str_len(bad), " ", sizeof bad - str_len(bad));
                        str_cpy(bad + str_len(bad), cur_page()->title, sizeof bad - str_len(bad));
                    }
                    ok_steps &= (uint32_t)ok;
                    n_steps++;
                    seq_seen |= cur_page()->fam == FAM_SEQ ? 1u << (ui.page & 31u) : 0u;
                    mix_seen |= cur_page()->scope == SC_MIX;
                } else {
                    ok_other &= (uint32_t)no_beat_layer();
                    n_other++;
                }
            }
    for (i = 0; i < NPAGES; i++)                     /* every SEQ page among them */
        if (PAGES[i].fam == FAM_SEQ)
            ok_steps &= (seq_seen >> i) & 1u;
    if (!ok_steps)
        printf("      (beats missing on:%s)\n", bad);
    check("BACKLIGHT ON, every page and the HOME screen, KEYS off / on, armed or not: where the keys are steps (every "
          "SEQ page, TRACKS unarmed) on steps full, the unlit beats 1 5 9 13 at 25 % in bank 1 and bank 2, the rest 5 %",
          ok_steps && mix_seen && n_steps == 10u);   /* 4 SEQ pages x armed / not, TRACKS unarmed x KEYS off / on */
    check("BACKLIGHT ON: where the keys are not steps (EDIT FX LFO GLOBAL SAVE GRIDS COMP, the HOME screen, KEYS, "
          "TRACKS armed) no key at 25 %", ok_other && n_other > 0u);
    ui.home = 0;                                     /* a SEQ page under the menu, the MUTE screen, PERFORM: their
                                                      * keys are not steps */
    ui.keys = 0;
    song.rec = 0;
    seq_open("STEP");
    ui_frame();
    i = beats_shown(0);
    ui.menu = 1;
    ui.force = 1;
    ui_frame();
    i &= (uint32_t)no_beat_layer();
    ui.menu = 0;
    ui_frame();
    press(B_GLO);
    glo_hold_frames(500);
    i &= ui.mute && no_beat_layer();
    release_all();
    ui_frame();
    press(B_FX);
    fx_hold_frames(500);
    i &= ui.layer && no_beat_layer();
    release_all();
    ui_frame();
    i &= beats_shown(0);
    check("BACKLIGHT ON, over STEP: the menu, the MUTE screen and PERFORM show no beat marks; back on STEP they do", i);
    backlight = 0;
    ui_frame();
}

/* MENU > BACKLIGHT (fixes 2026-10-21 item 9: upstream Felucca's LEDS levels): KNOB 1 steps OFF DIM LO DIM HI INV INV LO
 * INV HI, stopping at the ends; OCT+ steps and wraps; the keys follow at once; the row drawn (default build: ten rows) */
static void test_backlight_menu(void)
{
    uint32_t ok = 1, i;
    ui_host_init();
    backlight = 0;
    check("menu: BACKLIGHT is a row (after ACCEL), a list of six levels (not ON / OFF)",
          str_eq(MI_NAME[MI_BACKLIGHT], "BACKLIGHT") && MI_BACKLIGHT == MI_ACCEL + 1 && !menu_onoff(MI_BACKLIGHT) &&
          BL_COUNT == 6u && str_eq(BL_NAME[BL_DIM_LO], "DIM LO") && str_eq(BL_NAME[BL_INV_HI], "INV HI"));
    ui.menu = 1;
    ui.menu_sel = MI_BACKLIGHT;
    ui.force = 1;
    for (i = 1; i <= 6u; i++) {
        host_ticks += 300u * 1000u * FM1_TICKS_PER_US;
        turn(EN_K1, 1);
        ui_frame();
        ok &= backlight == (i < 6u ? i : 5u) && led_dim_on;
    }
    for (i = 0; i < 7u; i++) {
        host_ticks += 300u * 1000u * FM1_TICKS_PER_US;
        turn(EN_K1, -1);
        ui_frame();
    }
    ok &= backlight == BL_OFF;
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    ok &= backlight == BL_DIM_LO && ui.menu == 1u;
    check("BACKLIGHT: KNOB 1 steps OFF .. INV HI and stops at the ends, OCT+ steps on; the LEDs follow at once", ok);
    backlight = BL_INV_HI;
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("BACKLIGHT: OCT+ from INV HI wraps to OFF", backlight == BL_OFF);
    backlight = BL_DIM_HI;
    ui.force = 1;
    ui_frame();
    snap_page("fixes/menu_backlight");
    check("menu: ten rows, BACK's drawn above the hints",
          fb_count(12, MENU_Y0 + (MI_COUNT - 1u) * MENU_DY, 214, 10, C_INK) > 50u &&
          fb_count(12, MENU_Y0 + (MI_COUNT - 1u) * MENU_DY + 12u, 214, MENU_HY - (MENU_Y0 + (MI_COUNT - 1u) * MENU_DY + 12u),
                   C_INK) == 0u && fb_count(12, MENU_HY, 214, 14, C_INK) > 50u);
    backlight = 0;
    menu_close();
    ui_frame();
}

/* the start screen (main.c fm1_main, spec 2026-10-19-md-ui §9a): the VU galley mark (the user's pick, C3-4) still on
 * its downbeat frame, HORTATOR under it, the frame; two tones only, in the stored theme */
static void test_boot_title(void)
{
    uint32_t t, i, ok = 1;
    for (t = 0; t < NTHEMES; t++) {
        uint32_t stray = 0;
        theme_set(THEME_TAG | t);
        memset(fb, 0, sizeof fb);
        draw_boot_title();
        if (t == 0)
            shot("build/ui_shots/00_boot.ppm");
        for (i = 0; i < 240u * 240u; i++)
            stray += fb[i] != C_BG && fb[i] != C_INK;
        ok &= stray == 0u;
    }
    check("boot: two tones only, in every theme", ok);
    theme_set(THEME_TAG | 0u);
    memset(fb, 0, sizeof fb);
    draw_boot_title();
    check("boot: the downbeat bar full (52 px over the line, 52 under), solid", fb_count(48, 40, 12, 52, C_INK) == 624u &&
          fb_count(48, 97, 12, 52, C_INK) == 624u);
    check("boot: a dithered bar beside it (+ its peak dot), the dotted centre line between",
          fb_count(66, 60, 12, 32, C_INK) > 30u && fb_count(66, 60, 12, 32, C_INK) <= 12u * 32u / 2u + 24u &&
          fb[94u * 240u + 48u] == C_INK && fb[94u * 240u + 49u] == C_BG);
    check("boot: HORTATOR under the mark, the 1 px frame", fb_count(80, 168, 80, 10, C_INK) > 100u &&
          fb[0] == C_INK && fb[239u * 240u + 120u] == C_INK);
    theme_set(settings.palette);
}

/* spec 2026-10-19-md-ui §2: MENU > COLOR steps the 4 themes (KNOB 1 and OCT+) and wraps */
static void test_theme_menu(void)
{
    uint32_t k, ok;
    ui_host_init();
    ok = settings.palette == (THEME_TAG | 0u);
    ui.menu = 1;
    ui.menu_sel = MI_COLOR;
    ui.force = 1;
    for (k = 1; k <= 4u; k++) {
        press(B_OCTUP);
        ui_frame();
        release_all();
        ui_frame();
        ok &= settings.palette == (THEME_TAG | (k % 4u)) && C_BG == THEMES[k % 4u].bg;
    }
    turn(EN_K1, -1);
    ui_frame();
    ok &= settings.palette == (THEME_TAG | 3u);
    check("COLOR: a new device is MD RED; OCT+ steps MD RED > RED ON BLACK > WHITE ON BLUE > BLACK ON GREEN > MD RED; "
          "KNOB 1 left steps back", ok);
    menu_close();
    ui_frame();
}

/* Review Focus 1: after a theme change every region repaints (nothing keeps the old theme's colours) */
static void test_theme_repaint(void)
{
    uint32_t i, stale = 0;
    ui_host_init();
    tap(B_EDIT);
    ui_frame();
    ui_frame();
    ui.menu = 1;
    ui.menu_sel = MI_COLOR;
    ui.force = 1;
    turn(EN_K1, 1);                                  /* MD RED -> RED ON BLACK -> WHITE ON BLUE (a detent per frame) */
    ui_frame();
    turn(EN_K1, 1);
    ui_frame();
    menu_close();
    ui_frame();
    ui_frame();
    for (i = 0; i < 240u * 240u; i++)
        stale += fb[i] == THEMES[0].bg || fb[i] == THEMES[0].ink;
    check("theme change: the whole screen repaints in the new theme (no MD RED pixel left)",
          settings.palette == (THEME_TAG | 2u) && stale == 0);
    theme_set(THEME_TAG | 0u);
}

/* spec 2026-10-19-md-ui §5: the pixels of colour c in a screen rectangle */
static uint32_t fb_count(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t n = 0, x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            n += fb[y * 240u + x] == c;
    return n;
}

/* §5 top bar: ink bar 0..15, the state glyph in BG at 4,3: ■ stopped, ▶ playing, ● recording, outlined ● armed;
 * the FX tag (BG box) after the BPM while a PERFORM knob effect is on */
static void test_topbar(void)
{
    uint32_t ok;
    ui_host_init();
    tap(B_EDIT);
    song.playing = 0;
    song.rec = 0;
    ui.force = 1;
    ui_frame();
    check("top bar: an ink bar (rows 0..15 >= 80 % ink)", fb_count(0, 0, 240, 16, C_INK) > 240u * 16u * 8u / 10u);
    check("top bar: stopped: a solid 10x10 square", fb_count(4, 3, 10, 10, C_BG) == 100u);
    song.playing = 1;
    ui_frame();
    ok = fb_count(4, 3, 2, 10, C_BG) == 20u && fb[3u * 240u + 12u] == C_INK;
    check("top bar: playing: the play triangle", ok);
    song.rec = 1u << song.sel;
    ui_frame();
    check("top bar: recording (the selected track armed): a solid circle", fb[7u * 240u + 8u] == C_BG &&
          fb[3u * 240u + 4u] == C_INK);
    song.rec = 1u << ((song.sel + 1u) % NTRK);
    ui_frame();
    check("top bar: REC armed elsewhere: an outlined circle", fb[7u * 240u + 8u] == C_INK && fb[5u * 240u + 4u] == C_BG);
    song.rec = 0;
    perf_k[0] = 1;
    ui_frame();
    check("top bar: a PERFORM knob effect on: the FX tag after the BPM", fb_count(45, 1, 19, 14, C_BG) > 150u);
    perf_k[0] = 0;
    song.playing = 0;
    ui_frame();
}

/* §5 breadcrumb: an ink bar 212..238, T<n>>model>page left, the page number right */
static void test_crumb(void)
{
    ui_host_init();
    tap(B_EDIT);
    ui.force = 1;
    ui_frame();
    check("breadcrumb: an ink bar (>= 70 % ink)", fb_count(1, Y_CRUMB, 238, 27, C_INK) > 238u * 27u * 7u / 10u);
    check("breadcrumb: text on the left (T1...) and the page number at the right",
          fb_count(6, Y_CRUMB + 8u, 40, 10, C_BG) > 20u && fb_count(200, Y_CRUMB + 8u, 34, 10, C_BG) > 10u &&
          foot_lit() == 0u);
}

/* §5 trig strip: 10x10 on a 14 px pitch + 2 per beat at y 186: accent solid, a plain trig outline + dither, an empty
 * step an outline */
static void test_trig_strip(void)
{
    track_t *t;
    ui_host_init();
    tap(B_EDIT);
    t = TSEL;
    memset(t->step, 0, sizeof t->step);
    t->step[0].on = 1;
    t->step[0].acc = 1;
    t->step[4].on = 1;
    ui.force = 1;
    ui_frame();
    check("trig strip: an accent solid (box 1 at x 6, y 186)", fb_count(6, 186, 10, 10, C_INK) == 100u);
    check("trig strip: a plain trig outline + dither (box 5 at x 64)",
          fb_count(64, 186, 10, 10, C_INK) > 60u && fb_count(64, 186, 10, 10, C_INK) < 76u);
    check("trig strip: an empty step an outline (box 2 at x 20)", fb_count(20, 186, 10, 10, C_INK) == 36u);
}

/* §7 messages: a small popup over the graph area; the top bar keeps its BPM */
static void test_msg_popup(void)
{
    uint32_t k, edge = 0;
    ui_host_init();
    tap(B_EDIT);
    ui.force = 1;
    ui_frame();
    ui_message("SAVED A1");
    ui_frame();
    for (k = 0; k < 240u; k++)                     /* a 2 px right edge: two ink columns with BG left of them */
        if (k > 2u && fb[120u * 240u + k] == C_INK && fb[120u * 240u + k - 1u] == C_INK &&
            fb[120u * 240u + k - 2u] == C_BG && k < 239u)
            edge = k;
    check("message: a popup window over the graph area, its 2 px shadow edge", edge > 150u && edge < 239u);
    check("message: the top bar stays (ink bar with the BPM in BG)", fb_count(18, 3, 22, 10, C_BG) > 20u &&
          fb_count(0, 0, 240, 16, C_INK) > 240u * 16u * 7u / 10u);
    for (k = 0; k < 45u; k++)
        ui_frame();
    check("message: gone after its time (its shadow edge repainted away)", !ui.msg_t &&
          !(fb[120u * 240u + edge] == C_INK && fb[120u * 240u + edge - 1u] == C_INK &&
            fb[120u * 240u + edge - 2u] == C_BG));
}

/* spec 2026-10-19-md-ui §5 knob cell c: its canvas x c*60+1, the knob at (c*60+17, Y_KNOB+28) r 8, the value right
 * of it (right edge x c*60+55, rows Y_KNOB+24..33); a discrete parameter: its text centred, no knob */
static void open_filter(void)
{
    ui_host_init();
    tap(B_FX);
    ui.force = 1;
    ui_frame();
}

static void test_cell_knob_vs_text(void)
{
    open_filter();
    check("cell: FILTER TYPE (an enum) is text, no knob (no tick / ring at x 17)", str_eq(cur_page()->title, "FILTER") &&
          fb[(Y_KNOB + 18u) * 240u + 17u] == C_BG && fb[(Y_KNOB + 20u) * 240u + 17u] == C_BG);
    check("cell: CUT (continuous) a knob: the tick and the ring's top at x 75",
          fb[(Y_KNOB + 18u) * 240u + 75u] == C_INK && fb[(Y_KNOB + 20u) * 240u + 75u] == C_INK &&
          fb[(Y_KNOB + 19u) * 240u + 75u] == C_BG);
    check("cell: labels in ink on the theme's ground", fb_count(61, Y_KNOB + 2u, 58, 10, C_INK) > 20u &&
          fb_count(61, Y_KNOB, 58, 43, C_BG) > 58u * 43u / 2u);
}

static void test_cell_touched(void)
{
    uint32_t box0, box1, box2, k, same = 1;
    uint16_t crumb0[240 * 10];
    open_filter();
    box0 = fb_count(87, Y_KNOB + 22u, 30, 13, C_INK);
    memcpy(crumb0, fb + (Y_CRUMB + 8u) * 240u, sizeof crumb0);
    turn(EN_K2, 1);
    ui_frame();
    box1 = fb_count(87, Y_KNOB + 22u, 30, 13, C_INK);
    for (k = 0; k < 240u * 10u; k++)
        same &= fb[(Y_CRUMB + 8u) * 240u + k] == crumb0[k];
    check("cell touched: the value inverted (an ink box behind it)", ui.hot_t && ui.hot_col == 1u && box1 > box0 + 80u);
    check("cell touched: the breadcrumb shows the parameter's full value (CUT ... HZ, the page number gone)",
          !same && fb_count(200, Y_CRUMB + 8u, 34, 10, C_BG) == 0u && fb_count(6, Y_CRUMB + 8u, 60, 10, C_BG) > 40u);
    for (k = 0; k < 60u && ui.hot_t; k++)
        ui_frame();
    ui_frame();
    box2 = fb_count(87, Y_KNOB + 22u, 30, 13, C_INK);
    same = 1;
    for (k = 0; k < 240u * 10u; k++)
        same &= fb[(Y_CRUMB + 8u) * 240u + k] == crumb0[k];
    check("cell touched: both return when the touch times out", !ui.hot_t && box2 + 80u < box1 && same);
    turn(EN_K2, 1);
    ui_frame();
    sel(1);                                          /* FILTER 2/2 */
    ui_frame();
    check("cell touched, then another page: nothing inverted there", !ui.hot_t &&
          fb_count(87, Y_KNOB + 22u, 30, 13, C_INK) < box1 - 80u);
}

static void test_cell_value_fits(void)
{
    open_filter();
    TSEL->p[P_FCUT] = (int16_t)TP[P_FCUT].max;      /* the widest cutoff */
    ui.force = 1;
    ui_frame();
    check("cell: a long value fits right of the knob (x 86 clear, ink in x 87..117, nothing at x 118..119)",
          fb_count(86, Y_KNOB + 22u, 1, 13, C_INK) == 0u && fb_count(87, Y_KNOB + 22u, 31, 13, C_INK) > 10u &&
          fb_count(118, Y_KNOB + 22u, 2, 13, C_INK) == 0u);
}

/* spec 2026-10-19-md-ui §8, graphs I (two tones): the model's name in F3 5x; STEP's bars; PROJECT's and SONG's
 * cursor rows inverted */
static void test_graph_model_name(void)
{
    uint32_t x, y, y0 = 999, y1 = 0;
    ui_host_init();
    tap(B_EDIT);
    ui.force = 1;
    ui_frame();
    for (y = Y_GRAPH + G_OY; y < Y_GRAPH + G_OY + 40u; y++)
        for (x = 2; x < 238u; x++)
            if (fb[y * 240u + x] == C_INK) {
                y0 = y < y0 ? y : y0;
                y1 = y > y1 ? y : y1;
            }
    check("SOUND: the model's name in F3 at 5x (25 px tall) on the theme's ground", y1 - y0 + 1u == 25u &&
          fb_count(2, Y_GRAPH + G_OY, 236, 60, C_BG) > 236u * 60u / 2u);
}

static void test_graph_step_bars(void)
{
    uint32_t a, n;
    ui_host_init();
    memset(TSEL->step, 0, sizeof TSEL->step);
    TSEL->step[0].on = 1;
    TSEL->step[0].acc = 1;
    TSEL->step[4].on = 1;
    open_step_page();
    ui.force = 1;
    ui_frame();
    a = fb_count(4, Y_GRAPH + G_OY + 30u, 11, 40, C_INK);
    n = fb_count(62, Y_GRAPH + G_OY + 40u, 11, 30, C_INK);
    check("STEP: an accent a solid bar", a == 11u * 40u);
    check("STEP: a plain trig an outline + dither (40..75 % ink)", n > 330u * 40u / 100u && n < 330u * 75u / 100u);
}

static void test_graph_slots_cursor(void)
{
    uint32_t r;
    ui_host_init();
    tap(B_SAVE);
    ui.force = 1;
    ui_frame();
    r = song.g[G_SLOT] ? (uint32_t)song.g[G_SLOT] - 1u : 0u;
    check("PROJECT: the cursor's slot row an inverted bar", str_eq(cur_page()->title, "PROJECT") &&
          fb_count(2, Y_GRAPH + 20u + r * 16u, 236, 14, C_INK) > 236u * 14u * 7u / 10u &&
          fb_count(2, Y_GRAPH + 20u + ((r + 1u) % 4u) * 16u, 236, 14, C_INK) < 236u * 14u * 3u / 10u);
}

static void test_graph_song_rows(void)
{
    ui_host_init();
    seq_open("SONG");
    chain.cfg.count = 2;
    chain.cfg.row[0].slot = 0;
    chain.cfg.row[0].repeat = 1;
    chain.cfg.row[1].slot = 1;
    chain.cfg.row[1].repeat = 2;
    ui.song_row = 1;
    ui.force = 1;
    ui_frame();
    check("SONG: the cursor's row an inverted bar, the other plain",
          fb_count(1, Y_GRAPH + 6u + 28u - 3u, 238, 16, C_INK) > 238u * 16u * 7u / 10u &&
          fb_count(1, Y_GRAPH + 6u - 3u, 238, 16, C_INK) < 238u * 16u * 3u / 10u);
    chain.cfg.count = 0;
}

/* spec 2026-10-19-md-ui §8, graphs II: OFF between dither bands; COMP's meters with tick dashes; the LFO's area
 * dithered between the wave and the zero line */
static void test_graph_off_word(void)
{
    uint32_t top, bot, word;
    open_filter();
    TSEL->p[P_FTYPE] = 0;                            /* OFF */
    ui.force = 1;
    ui_frame();
    top = fb_count(81, Y_GRAPH + G_OY + 30u, 78, 3, C_INK);
    bot = fb_count(81, Y_GRAPH + G_OY + 67u, 78, 3, C_INK);
    word = fb_count(87, Y_GRAPH + G_OY + 35u, 66, 30, C_INK);
    check("FILTER OFF: OFF in F3 at 6x, a 3-row dither band above and below it",
          top > 78u * 3u * 4u / 10u && top < 78u * 3u * 6u / 10u && bot > 78u * 3u * 4u / 10u && bot < 78u * 3u * 6u / 10u &&
          word > 66u * 30u / 3u);
}

static void test_graph_comp_meter(void)
{
    uint32_t y;
    ui_host_init();                                  /* (on the HOME screen) */
    sel(1);                                          /* COMP 2/3 */
    ui.force = 1;
    ui_frame();
    y = Y_GRAPH + G_OY + 24u;
    check("COMP: the GR meter's tick dashes every 6 px above it (x 36, 42, 48; none between)",
          str_eq(cur_page()->title, "COMP") && fb[y * 240u + 36u] == C_INK && fb[y * 240u + 42u] == C_INK &&
          fb[y * 240u + 48u] == C_INK && fb[y * 240u + 39u] == C_BG);
}

static void test_graph_lfo_fill(void)
{
    uint32_t mid, above;
    ui_host_init();
    TSEL->p[P_LFO1 + LF_WAVE] = LW_SINE;
    TSEL->p[P_LFO1 + LF_MORPH] = 0;
    TSEL->p[P_LFO1 + LF_PHASE] = 0;
    tap(B_LFO);
    ui.force = 1;
    ui_frame();
    mid = fb_count(LG_X0 + LTR_N / 4u, Y_GRAPH + G_OY + LG_CY - LG_A + 2u, 1, LG_A - 2u, C_INK);   /* (LFO 1's half) */
    above = fb_count(LG_X0, Y_GRAPH + G_OY + 12u, LTR_N, 3, C_INK);   /* (under its name, over the crest) */
    check("LFO: the wave's area dithered down to the zero line (half the column under the crest), nothing above it",
          mid >= 11u && mid <= 17u && above == 0u);
}

/* spec 2026-10-19-md-ui §8: TRACKS keeps every item of its rows (number, arm, model, the bank's 16 steps, the level
 * over a dithered box, the live peak); MUTE's boxes; PERFORM's buttons */
static void test_tracks_row_items(void)
{
    uint32_t y, box, inner;
    ui_host_init();
    open_tracks();
    memset(trk[0].step, 0, sizeof trk[0].step);
    trk[0].step[0].on = 1;
    trk[0].p[P_LEVEL] = 64;
    ui.force = 1;
    ui_frame();
    y = MX_ROW_Y(0);
    box = fb_count(MX_LX, y + 1u, MX_LW, 11, C_INK);
    inner = fb_count(MX_LX + (uint32_t)MX_LW / 2u + 4u, y + 3u, 8, 7, C_INK);   /* past the level: the dither */
    check("TRACKS row: the number + model in ink (x 2..60)", fb_count(2, y, 58, MX_RH, C_INK) > 30u);
    check("TRACKS row: step 1 a cell, steps 2..16 baselines", fb_count(MX_CELL_X(0), y + 4u, 6, 7, C_INK) == 42u &&
          fb_count(MX_CELL_X(1), y + 4u, 6, 6, C_INK) == 0u && fb_count(MX_CELL_X(1), y + 10u, 6, 1, C_INK) == 6u);
    check("TRACKS row: the level a solid bar over a dithered box", box > 60u && inner > 20u && inner < 40u);
}

static void test_tracks_peak_tick(void)
{
    uint32_t y, x, r, d = 0;
    uint16_t a[11][MX_LW];
    ui_host_init();
    open_tracks();
    trk[0].p[P_LEVEL] = 30;
    ui.force = 1;
    ui_frame();
    y = MX_ROW_Y(0) + 1u;                            /* the level box's 11 rows */
    for (r = 0; r < 11u; r++)
        for (x = 0; x < (uint32_t)MX_LW; x++)
            a[r][x] = fb[(y + r) * 240u + MX_LX + x];
    trk[0].peak = 20000;
    ui_frame();
    for (r = 0; r < 11u; r++)
        for (x = 0; x < (uint32_t)MX_LW; x++)
            d += fb[(y + r) * 240u + MX_LX + x] != a[r][x];
    check("TRACKS: a loud track's live peak marks its level box", d >= 4u);
}

static void test_mute_boxes(void)
{
    uint32_t k, ok = 1;
    ui_host_init();
    settings.mutebar = 0;
    press(B_GLO);
    glo_hold_frames(500);
    for (k = 0; k < NTRK; k++)
        ok &= mute_box_filled(k);
    trk[2].p[P_MUTE] = 1;
    glo_hold_frames(2);
    check("MUTE: 8 boxes, an audible track's solid; muting track 3 makes its box an outline", ui.mute && ok &&
          !mute_box_filled(2) && mute_box_filled(1) && fb[MU_BY(2u) * 240u + MU_BX(2u) + 10u] == C_INK);
    trk[2].p[P_MUTE] = 0;
    release_all();
    ui_frame();
}

static void test_perform_buttons(void)
{
    uint32_t x = 4u + 47u, y = Y_GRAPH + 4u, on, off;
    ui_host_init();
    press(B_FX);
    fx_hold_frames(500);
    off = fb_count(x + 2u, y + 2u, 40, 30, C_INK);
    perf_held |= 1u << PF_BLACK[1];
    ui.force = 1;
    fx_hold_frames(1);
    on = fb_count(x + 2u, y + 2u, 40, 30, C_INK);
    check("PERFORM: a button an outline, held: solid (ink with its name in BG)", ui.layer && off < 200u && on > 900u);
    perf_held = 0;
    release_all();
    ui_frame();
}

/* spec 2026-10-19-md-ui §9: HOME = the top bar, the MD main-screen strip (bar boxes, the project box, the mode
 * line | LEV | 4 knobs, the values under them), the scope below */
static void test_home_strip(void)
{
    uint32_t nb;
    ui_host_init();
    TSEL->p[P_SLEN] = 32;
    transport_req = 1;
    seq_play_to(song.sel, 20);                       /* the playhead in bank 2 */
    ui.force = 1;
    ui_frame();
    nb = bank_count();
    check("HOME strip: the project box inverted (x 2..60, y 30..53)", ui.home &&
          fb_count(2, 30, 59, 24, C_INK) > 59u * 24u * 6u / 10u && fb_count(2, 30, 59, 24, C_BG) > 40u);
    check("HOME strip: 4 bar boxes (fix 6: not a box per bank), the playing bar (2: step 20) solid", nb == 2u &&
          fb_count(6, 20, 10, 6, C_INK) == 28u && fb_count(19, 20, 10, 6, C_INK) == 60u &&
          fb_count(32, 20, 10, 6, C_INK) == 28u && fb_count(45, 20, 10, 6, C_INK) == 28u);
    check("HOME strip: rules at x 62 / 87 and y 68", fb[40u * 240u + 62u] == C_INK && fb[40u * 240u + 87u] == C_INK &&
          fb_count(1, 68, 238, 1, C_INK) == 238u);
    transport_req = 2;
    ui_frame();
}

static void test_home_knob_values_below(void)
{
    uint32_t c, ok = 1;
    ui_host_init();
    ui.force = 1;
    ui_frame();
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = home_param(c, &vp);
        uint32_t cx = 107u + c * 38u;
        if (!d->label || d->fmt == F_ENUM)
            continue;
        ok &= fb[34u * 240u + cx] == C_INK && fb[35u * 240u + cx] == C_BG;   /* the ring's top, r 7 at y 41 */
        ok &= fb_count(cx - 15u, 54, 30, 10, C_INK) > 8u;                  /* the value under it */
    }
    check("HOME strip: each knob a ring (r 7 at y 41), its value under it (y 54)", ok);
}

static void test_home_scope_height(void)
{
    uint32_t i;
    ui_host_init();
    for (i = 0; i < SCOPE_N; i++)
        scope_buf[i] = (int16_t)((i % 32u) < 16u ? 30000 : -30000);
    scope_w = 0;
    ui.force = 1;
    ui_frame();
    check("HOME: the scope fills rows 69..180 (its wave reaches y 72..95 and 160..178)",
          fb_count(2, 72, 236, 24, C_INK) > 20u && fb_count(2, 160, 236, 19, C_INK) > 20u);
    memset(scope_buf, 0, sizeof scope_buf);
}

/* spec 2026-10-19-md-ui §7: confirms and TEMPO as MD popups over the live page; MENU a popup window (the cursor an
 * inverted bar, COLOR the themes as boxes); NAME's block cursor */
static void test_confirm_popup(void)
{
    uint32_t i;
    ui_host_init();
    trk[0].step[0].on = 1;
    open_step_page();
    fm1_in.buttons |= 1u << panel.btn[B_REC];
    for (i = 0; i < 800; i++)
        ui_input();
    release_all();
    ui.force = 1;
    ui_frame();
    check("confirm: a popup over the page (the knob row and the top bar still drawn)", ui.confirm != 0u &&
          fb_count(1, Y_KNOB, 238, 43, C_INK) > 100u && fb_count(0, 0, 240, 16, C_INK) > 2000u);
    check("confirm: the popup's 2 px shadow edge at x 210..211, its title tab over the top edge",
          fb[120u * 240u + 210u] == C_INK && fb[120u * 240u + 211u] == C_INK && fb[120u * 240u + 209u] == C_BG &&
          fb_count(80, 76, 80, 6, C_INK) > 100u);
    press(B_OCTDN);
    ui_frame();
    release_all();
    ui_frame();
}

static void test_tempo_popup(void)
{
    uint32_t k;
    ui_host_init();
    open_tracks();
    ui.force = 1;
    ui_frame();
    sel(1);                                          /* BPM + 1 */
    ui_frame();
    check("TEMPO: changing BPM opens the TEMPO popup (its shadow edge at x 208..209)", ui.bpm_t &&
          fb[120u * 240u + 208u] == C_INK && fb[120u * 240u + 209u] == C_INK && fb[120u * 240u + 207u] == C_BG);
    for (k = 0; k < 45u; k++)
        ui_frame();
    check("TEMPO: gone when bpm_t runs out", !ui.bpm_t &&
          !(fb[120u * 240u + 208u] == C_INK && fb[120u * 240u + 209u] == C_INK && fb[120u * 240u + 207u] == C_BG));
}

static void test_menu_popup_cursor(void)
{
    uint32_t y;
    ui_host_init();
    ui.menu = 1;
    ui.menu_sel = MI_SPKEQ;
    ui.force = 1;
    ui_frame();
    y = MENU_Y0 + MI_SPKEQ * MENU_DY;
    check("MENU: the cursor's row an inverted bar", fb_count(12, y - 2u, 214, 14, C_INK) > 214u * 14u * 7u / 10u);
    check("MENU: COLOR shows the 4 themes as 10x10 boxes, the current one solid",
          fb_count(170, MENU_Y0, 10, 10, C_INK) == 100u && fb_count(184, MENU_Y0, 10, 10, C_INK) == 36u &&
          fb_count(212, MENU_Y0, 10, 10, C_INK) == 36u);
    menu_close();
    ui_frame();
}

static void test_name_block_cursor(void)
{
    ui_host_init();
    name_open(NK_RENAME, 0);
    ui_frame();
    check("NAME: an ink title bar; the cursor a solid block under its slot (after PROJECT A: slot 10)",
          name_on() && fb_count(0, 0, 240, 16, C_INK) > 240u * 16u * 7u / 10u &&
          fb_count(12u + 18u * 9u, 70, 18, 3, C_INK) == 54u && fb_count(12u + 18u * 8u, 70, 18, 3, C_INK) == 0u);
    nm.kind = NK_NONE;
    ui.force = 1;
    ui_frame();
}

/* spec 2026-10-19-md-ui §10: the review sheet: the key pages in each theme into build/ui_shots/themes/<theme>/, every
 * one in two tones only (the theme's BG and INK) */
static const char *const THEME_DIR[4] = {"md-red", "red-on-black", "white-on-blue", "black-on-green"};
static uint32_t sheet_t;
static uint32_t sheet_stray;
static void sheet_snap(const char *page)
{
    char path[128];
    uint32_t i, k;
    for (k = 0; k < 3u; k++)
        ui_frame();
    snprintf(path, sizeof path, "build/ui_shots/themes/%s/%s.ppm", THEME_DIR[sheet_t], page);
    shot(path);
    for (i = 0; i < 240u * 240u; i++)
        if (fb[i] != C_BG && fb[i] != C_INK) {
            if (!sheet_stray)
                printf("     two tones: %s/%s has 0x%04x at %u,%u\n", THEME_DIR[sheet_t], page, fb[i], i % 240u, i / 240u);
            sheet_stray++;
        }
}
static void sheet_open(void)                          /* a fresh device in theme sheet_t */
{
    ui_host_init();
    settings.palette = THEME_TAG | sheet_t;
    theme_set(settings.palette);
    ui.force = 1;
}
static void test_theme_sheet(void)
{
    uint32_t i;
    sheet_stray = 0;
    for (sheet_t = 0; sheet_t < 4u; sheet_t++) {
        sheet_open();
        draw_boot_title();
        shot("build/ui_shots/themes/boot.ppm");
        sheet_snap("00_home");
        sheet_open();
        tap(B_EDIT);
        sheet_snap("01_sound");
        sheet_open();
        tap(B_FX);
        sheet_snap("02_filter");
        sheet_open();
        trk[0].step[0].on = trk[0].step[0].acc = trk[0].step[4].on = 1;
        open_step_page();
        sheet_snap("03_step");
        sheet_open();
        tap(B_LFO);
        sheet_snap("04_lfo");
        sheet_open();
        sel(1);
        sel(1);
        sheet_snap("05_comp");
        sheet_open();
        tap(B_SAVE);
        sheet_snap("06_project");
        sheet_open();
        open_tracks();
        sheet_snap("07_tracks");
        sheet_open();
        press(B_FX);
        fx_hold_frames(500);
        sheet_snap("08_perform");
        release_all();
        sheet_open();
        ui.menu = 1;
        sheet_snap("09_menu");
        menu_close();
        sheet_open();
        trk[0].step[0].on = 1;
        open_step_page();
        fm1_in.buttons |= 1u << panel.btn[B_REC];
        for (i = 0; i < 800; i++)
            ui_input();
        release_all();
        ui.force = 1;
        sheet_snap("10_confirm");
        press(B_OCTDN);
        ui_frame();
        release_all();
        sheet_open();
        name_open(NK_RENAME, 0);
        sheet_snap("11_name");
        nm.kind = NK_NONE;
        sheet_open();
        press(B_GLO);
        glo_hold_frames(500);
        sheet_snap("12_mute");
        release_all();
        sheet_open();
        safe_start = 1;
        sheet_snap("13_safe_start");
        safe_start = 0;
        sheet_open();
        tap(B_EDIT);
        turn(EN_PRESET, 1);                          /* the PRESETS popup */
        ui_frame();
        sheet_snap("14_preset_popup");
        sheet_open();
        hold(B_EDIT);                                /* the PRESETS list */
        sheet_snap("15_preset_list");
        release_all();
        tap(B_EDIT);
    }
    sheet_t = 0;
    theme_set(THEME_TAG | 0u);
    check("theme sheet: 16 screens in each of the 4 themes, every pixel the theme's BG or INK", sheet_stray == 0u);
}

/* final review fixes (2026-10-19-md-ui): a 4-glyph value whole; ZOOM on HOME; a shorter message repaints; popups do
 * not let the graph under them redraw */
static void test_fix_value_4_glyphs(void)
{
    uint32_t x, y, x0 = 999, x1 = 0;
    ui_host_init();
    open_tracks();
    TSEL->p[P_PAN] = -64;                            /* PAN fully left: -100 */
    ui.force = 1;
    ui_frame();
    for (y = Y_KNOB + 24u; y < Y_KNOB + 34u; y++)
        for (x = 207u; x < 239u; x++)
            if (fb[y * 240u + x] == C_INK) {
                x0 = x < x0 ? x : x0;
                x1 = x > x1 ? x : x1;
            }
    check("cell: PAN fully left shows -100 whole (4 glyphs, 30 px), not -10", x1 - x0 + 1u == 30u);
    TSEL->p[P_PAN] = 0;
}

static void test_fix_zoom_home(void)
{
    ui_host_init();
    settings.zoom = 1;
    ui.force = 1;
    ui_frame();
    turn(EN_K1, 1);
    ui_frame();
    check("ZOOM on HOME: the touched value in a popup over the scope (its shadow edge at x 152..153)", ui.home &&
          fb[110u * 240u + 152u] == C_INK && fb[110u * 240u + 153u] == C_INK && fb[110u * 240u + 151u] == C_BG);
    settings.zoom = 0;
}

static void test_fix_msg_shrink(void)
{
    uint32_t k, e = 0;
    ui_host_init();
    tap(B_EDIT);
    ui.force = 1;
    ui_frame();
    ui_message("A MUCH LONGER MESSAGE");
    ui_frame();
    for (k = 3; k < 239u; k++)
        if (fb[120u * 240u + k] == C_INK && fb[120u * 240u + k - 1u] == C_INK && fb[120u * 240u + k - 2u] == C_BG)
            e = k;
    ui_message("OK");
    ui_frame();
    check("message: a shorter one replacing it leaves no edge of the longer popup", e > 150u &&
          !(fb[120u * 240u + e] == C_INK && fb[120u * 240u + e - 1u] == C_INK && fb[120u * 240u + e - 2u] == C_BG));
}

static void test_fix_popup_holds_graph(void)
{
    uint32_t k, during;
    ui_host_init();
    open_tracks();
    transport_req = 1;
    for (k = 0; k < 5u; k++)
        ui_frame();
    sel(1);                                          /* BPM + 1: TEMPO */
    ui_frame();
    host_graph_blits = 0;
    for (k = 0; k < 6u; k++) {
        trk[k].peak = (int16_t)(8000 + 3000 * (int32_t)k);   /* the meters move */
        ui_frame();
    }
    during = host_graph_blits;
    check("TEMPO over a playing TRACKS: the rows under it are not redrawn (no flicker), the popup stays",
          ui.bpm_t && during == 0u && fb[120u * 240u + 208u] == C_INK && fb[120u * 240u + 209u] == C_INK);
    for (k = 0; k < 45u; k++)
        ui_frame();
    check("TEMPO gone: the rows redrawn again", !ui.bpm_t && host_graph_blits > 0u);
    transport_req = 2;
    ui_frame();
}

/* HOME's LEV (the user, 2026-10-19: "level meter is not working ... on home page"): the selected track's live level as
 * the solid bar, falling back when the sound stops; the level setting a marker wider than the bar */
static void test_home_lev_meter(void)
{
    uint32_t k, hs;
    ui_host_init();
    TSEL->p[P_LEVEL] = 40;
    ui.force = 1;
    ui_frame();
    hs = 40u * 35u / 127u;
    check("HOME LEV: the level setting a 10 px marker (x 70..79), wider than the 6 px bar",
          fb_count(70, 64u - hs, 10, 2, C_INK) == 20u && fb[(65u - hs) * 240u + 69u] == C_BG);
    check("HOME LEV: silent: no bar above the marker", fb[45u * 240u + 74u] == C_BG);
    TSEL->peak = 20000;
    ui_frame();
    check("HOME LEV: a loud hit raises the bar over the setting (live level)", fb[45u * 240u + 74u] == C_INK &&
          fb[45u * 240u + 72u] == C_INK);
    for (k = 0; k < 40u; k++) {
        TSEL->peak = 0;
        ui_frame();
    }
    check("HOME LEV: ... and it falls back when the sound stops", fb[45u * 240u + 74u] == C_BG);
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
    open_tracks();
    for (r = 0; r < NTRK; r++)
        for (k = 0; k < 16; k++)
            ok &= (fb[(MX_ROW_Y(r) + 7u) * 240u + MX_CELL_X(k) + 2u] == C_INK) == trk[r].step[k].on;
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
            if (pg->fam == FAM_MIX) {                /* HOME's TRACKS mode: HOME (toggles) */
                tap(B_HOME);
                continue;
            } else if (pg->fam == FAM_HOME) {        /* COMP: SELECT from HOME's second mode */
                if (ui.home || comp_mode())
                    sel(1);
                else
                    tap(B_HOME);
                continue;
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
    for (k = 0; k < MI_ABOUT; k++) {                 /* SELECT: one item per detent */
        turn(EN_SELECT, 1);
        ui_frame();
    }
    press(B_OCTUP);
    ui_frame();
    release_all();
    snap_page("91_about");
    check("menu > ABOUT draws", ui.menu == 2 && fb_lit(20, 230) > 300);
    {
        uint16_t a[60 * 80];
        uint32_t y, x, d = 0, n0 = fb_count(80, 18, 80, 60, C_INK);
        for (y = 0; y < 60u; y++)
            for (x = 0; x < 80u; x++)
                a[y * 80u + x] = fb[(18u + y) * 240u + 80u + x];
        fm1_ms += 200u;                              /* 3 frames of the meter (15 fps) */
        ui_frame();
        for (y = 0; y < 60u; y++)
            for (x = 0; x < 80u; x++)
                d += fb[(18u + y) * 240u + 80u + x] != a[y * 80u + x];
        check("ABOUT: the VU mark on top (half size), moving", n0 > 300u && d > 20u);
    }
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
    old_set_model(TSEL, DM_KBOOM);
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
    open_tracks();                                   /* HOME's TRACKS mode: the mixer */
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
    old_set_model(&trk[2], DM_CONGA);
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
    ((uint8_t *)&proj_slot[0].t[3].step[0])[0] = 0xC1u;   /* on + both reserved bits */
    proj_slot[0].t[3].step[1].cond = 200;
    ((uint8_t *)&proj_slot[0].t[3].step[1])[0] = 0x30u;   /* rat 3 */
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
    open_tracks();                                   /* HOME's TRACKS mode: the mixer */
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
    turn(EN_K1, 3);
    ui_frame();
    check("CLK USB: GLOBAL's BPM knob keeps the tempo (the clock's) and says so", song.g[G_BPM] == bpm && ui.msg_t &&
          str_eq(ui.msg, "CLK USB"));
    open_tracks();
    ui.msg_t = 0;
    ui.msg[0] = 0;
    sel(3);
    check("CLK USB: SELECT on HOME's TRACKS mode keeps the tempo and says CLK USB", song.g[G_BPM] == bpm && mix_mode() &&
          str_eq(ui.msg, "CLK USB"));
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
    check("FX let go (HOLD): CRUSH stays on, no page change", perf_k[1] > 0 && (uint32_t)(cur_page() - PAGES) == fam);
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
    check("PAGE: another page button closes it, the macros stay on", !ui.pg_open && !ui.layer && perf_k[1] > 0 &&
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
    check("the map: the footer says [FX] HOLD", fb_lit(Y_TRIG, 240) > 0u && ui.layer);
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

/* spec §7: the knob effects (FILTER CRUSH THROW DEPTH) keep their values and keep working after the PERFORM screen
 * closes (FX let go, PAGE closed, the menu, a confirm, NAME); they change only on the PERFORM screen; INIT ALL resets
 * them; the top bar shows FX while any is on. The buffer effects (black keys) still stop as before */
static int head_fx_lit(void)                         /* the top bar's FX tag: a BG box after the BPM (row 1 is
                                                       * all ink without it) */
{
    uint32_t x, n = 0;
    for (x = 30u; x < 90u; x++)
        n += fb[1u * 240u + x] == C_BG;
    return n > 10u;
}
static void test_perform_knobs_stay(void)
{
    uint32_t mode;
    int16_t e1;
    ui_host_init();
    ui_frame();
    check("no knob effect on: no FX mark in the top bar", !head_fx_lit());
    press(B_FX);
    turn(EN_K1, -40);                                 /* FILTER: LPF */
    fx_hold_frames(2);
    release_all();
    ui_frame();
    render_mix(0, 0, CTL);
    check("HOLD: FX let go, FILTER stays on and keeps working", perf_k[0] < 0 && !ui.layer && pf.busy && pf.lc < PF_TOP);
    ui_frame();
    check("..the top bar shows FX", head_fx_lit());
    snap_page("fixes/head_fx");
    ui.keys = 1;                                     /* (the top bar at its fullest: armed, KEYS, a song's row) */
    song.rec = 1u;
    ui.force = 1;
    snap_page("fixes/head_fx_keys");
    ui.keys = 0;
    song.rec = 0;
    ui.force = 1;
    ui_frame();
    e1 = trk[0].p[P_E1];
    turn(EN_K1 + 1, 3);
    ui_frame();
    check("the screen gone: the knobs edit the page again (HOME: DECAY), the macros untouched",
          trk[0].p[P_E1] != e1 && perf_k[1] == 0 && perf_k[0] < 0);
    for (mode = 0; mode < 3u; mode++) {               /* the menu, a confirm, NAME: the macros stay; effects stop */
        press(B_FX);
        keys(1u << 3);                                /* G#3: REPEAT 1/16 */
        turn(EN_K2, 10);                              /* CRUSH */
        fx_hold_frames(2);
        release_all();
        if (mode == 0u) {
            ui.menu = 1;
            ui.force = 1;
        } else if (mode == 1u) {
            ui.confirm = 1;
            ui.force = 1;
        } else {
            project_save(0);
            name_open(NK_RENAME, 0);
        }
        fm1_in.buttons = 0;
        ui_frame();
        render_mix(0, 0, CTL);
        check(mode == 0u ? "the menu: CRUSH and FILTER stay on, the held REPEAT stops"
              : mode == 1u ? "a confirm: CRUSH and FILTER stay on, the held REPEAT stops"
                           : "NAME: CRUSH and FILTER stay on, the held REPEAT stops",
              perf_k[1] > 0 && perf_k[0] < 0 && perf_kill && !perf_act && pf.busy);
        keys(0);
        ui_frame();
        if (mode == 0u)
            menu_close();
        else if (mode == 1u)
            ui.confirm = 0;
        else
            name_close();
        ui.force = 1;
        ui_frame();
        perf_k[1] = 0;
    }
    settings.perfpage = 1;                            /* PAGE: closed by another page */
    press(B_FX);
    turn(EN_K3, 20);                                  /* THROW */
    fx_hold_frames(2);
    release_all();
    ui_frame();
    check("PAGE: the screen stays after FX", ui.pg_open && perf_k[2] > 0);
    tap(B_EDIT);
    check("PAGE closed by another page: THROW stays on", !ui.pg_open && !ui.layer && perf_k[2] > 0);
    settings.perfpage = 0;
    press(B_FX);
    turn(EN_K1, 60);                                  /* FILTER past 0 (-40 + 60): it stops at 0, the off point */
    turn(EN_K3, -40);                                 /* THROW back to 0 */
    fx_hold_frames(2);
    check("FILTER turned across 0 stops at 0 (off), not at the far side", perf_k[0] == 0);
    release_all();
    ui_frame();
    ui_frame();
    check("on the PERFORM screen, turned back to 0: FILTER and THROW off, no FX mark", !perf_k[0] && !perf_k[2] &&
          !head_fx_lit());
    press(B_FX);
    turn(EN_K1, 10);                                  /* from 0 a turn goes on as before */
    fx_hold_frames(2);
    check("FILTER from 0: a turn moves it (HPF side)", perf_k[0] > 0);
    turn(EN_K1, -30);
    fx_hold_frames(2);
    check("FILTER back across 0: stops at 0 again", perf_k[0] == 0);
    turn(EN_K4, 20);                                  /* DEPTH alone: nothing sounds without a black key */
    fx_hold_frames(2);
    release_all();
    ui_frame();
    ui_frame();
    check("DEPTH alone: no FX mark (it acts only with a held black key)", perf_k[3] > 0 && !head_fx_lit());
    perf_k[1] = 30;
    perf_k[3] = 20;
    init_all();
    render_mix(0, 0, CTL);
    check("INIT ALL: the knob effects off", !perf_k[0] && !perf_k[1] && !perf_k[2] && !perf_k[3]);
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
        check(p ? "PAGE: FX held into the menu: the CRUSH macro stays on there" : "HOLD: FX held into the menu: the CRUSH macro stays on there",
              ui.menu && perf_k[1] > 0);
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
    old_set_model(TSEL, DM_MEMB);
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
            press(B_SEQ);                            /* PAGE: another page under it (a page button) closes the PAGE
                                                      * (SELECT turns none under PERFORM: fixes 2026-10-21 item 7) */
            keys(1u << 1);
            ui_frame();
            release_all();
        }
        memcpy(tb, TSEL->p, sizeof tb);
        turn(EN_K1, 3);
        turn(EN_K1 + 2, 3);
        keys(1u << 1);
        ui_frame();
        for (i = 0; i < P_COUNT; i++)
            ch |= TSEL->p[i] != tb[i] && ui.layer;
        keys(0);                                     /* the key let go: the screen goes, the macros stay on */
        ui_frame();
        ui_frame();
        ch |= ui.layer || !perf_k[0] || !perf_k[2];
    }
    check("PERFORM shown (FX or a layer key held, or PAGE): the knobs are its macros, not the page under it", !ch);
}

/* bug 2026-10-10 ("the PERFORM knobs change the page's knobs"): FX and a layer key held into the menu, the menu
 * closed with both still held: FX is dead (no map), the key keeps the screen and the knobs. A knob turn then must not
 * mark the dead FX open: once the key is let go the screen goes with it, and the knobs edit the page again with the
 * page shown (never the page under a PERFORM screen) */
static void test_perform_dead_knob(void)
{
    uint32_t mode, i, bad = 0;
    for (mode = 0; mode < 2u; mode++) {
        int16_t tb[P_COUNT];
        ui_host_init();
        settings.perfpage = mode;
        tap(B_EDIT);                                 /* SOUND under it */
        keys(1u << 1);                               /* FX + a black key: the layer's (REPEAT 1/8) */
        fx_hold_frames(3);
        for (i = 0; i < 1000u && !ui.menu; i++) {    /* HOME held too: the menu */
            fm1_in.buttons |= 1u << panel.btn[B_HOME];
            fx_hold_frames(1);
        }
        fm1_in.buttons &= ~(1u << panel.btn[B_HOME]);
        fx_hold_frames(2);
        menu_close();
        fx_hold_frames(3);                           /* FX still held (dead), the key still held */
        turn(EN_K2, 3);                              /* the key's screen: CRUSH */
        fx_hold_frames(2);
        keys(0);                                     /* the key let go, FX still held */
        fx_hold_frames(3);
        memcpy(tb, TSEL->p, sizeof tb);
        turn(EN_K1, 3);
        fx_hold_frames(2);
        for (i = 0; i < P_COUNT; i++)
            bad |= ui.layer && TSEL->p[i] != tb[i];
        bad |= ui.layer;                             /* a dead FX hold shows no map */
        release_all();
        ui_frame();
    }
    check("FX dead (held through the menu), its layer key let go: no PERFORM screen over the page the knobs edit", !bad);
}

/* user 2026-10-08: on every SEQ page (STEP PATTERN MOTION SONG) and on HOME's TRACKS mode (not armed) the 16 white
 * keys are the selected track's steps in the bank (a tap: on / off, OCT+ held + a key: accent); they play nothing and select
 * nothing; OCT+ / OCT- change the bank; the key LEDs show the steps. The track: ALGO (or TRACKS' KNOB 1) */
static void test_step_keys_everywhere(void)
{
    static const char *const SEQ[4] = {"STEP", "PATTERN", "MOTION", "SONG"};
    uint32_t p, k;
    for (p = 0; p < 5u; p++) {
        uint32_t a2, a3, was;
        const char *name = p < 4u ? SEQ[p] : "TRACKS";
        char what[96];
        ui_host_init();
        for (k = 0; k < NSTEP; k++)
            trk[1].step[k].on = trk[1].step[k].acc = 0;
        trk[1].p[P_SLEN] = 32;
        if (p < 4u) {
            seq_open(SEQ[p]);
        } else {
            open_tracks();                           /* HOME's TRACKS mode */
        }
        turn(EN_ALGO, 1);                            /* track 2, nothing played */
        ui_frame();
        a2 = hit_age(&trk[1]);
        a3 = hit_age(&trk[2]);
        was = trk[1].step[2].on;
        keys(1u << STEP_KEY[2]);                     /* the third white key: step 3 */
        ui_frame();
        keys(0);
        ui_frame();
        snprintf(what, sizeof what, "%s: white key 3 toggles step 3 of the selected track, plays / selects nothing", name);
        check(what, trk[1].step[2].on == !was && song.sel == 1 && hit_age(&trk[1]) == a2 && hit_age(&trk[2]) == a3 &&
              song.seq_mode == 1u && led_lit(14u + STEP_KEY[2]));
        tap(B_OCTUP);                                /* bank 2: steps 17..32 */
        grid_acc(STEP_KEY[0]);                       /* OCT+ held + the key: accent (item 1) */
        snprintf(what, sizeof what, "%s: OCT+ alone is the bank; OCT+ held + a key accents the step", name);
        check(what, ui.bank == 1u && trk[1].step[16].on && trk[1].step[16].acc);
    }
}

/* REC is no mute key (spec §4): on HOME's TRACKS mode REC held + a white key toggles a step (nothing armed), REC
 * + top C# / D# change no MUTE setting; the release is a REC tap (arms); a long REC hold there asks nothing */
static void test_tracks_rec_mutes(void)
{
    uint32_t k;
    ui_host_init();
    settings.mutebar = 0;
    open_tracks();
    press(B_REC);
    ui_frame();
    keys(1u << KEY_TRK_KEY[3]);
    press(B_REC);
    ui_frame();
    keys(0);
    press(B_REC);
    ui_frame();
    keys(1u << 22);                                  /* top D# */
    press(B_REC);
    ui_frame();
    keys(0);
    press(B_REC);
    ui_frame();
    check("TRACKS: REC held + key 4 mutes nothing (step 4 toggled), + top D# sets no MUTE mode",
          trk[3].p[P_MUTE] == 0 && settings.mutebar == 0u && trk[0].step[3].on && !ui.confirm && mix_mode());
    release_all();
    ui_frame();
    check("TRACKS: REC let go: a tap (track 1 armed)", song.rec == 1u);
    transport_req = 2;
    ui_frame();
    for (k = 0; k < 60u; k++) {                      /* REC held 1.2 s alone */
        host_ticks += 20u * 1000u * FM1_TICKS_PER_US;
        press(B_REC);
        ui_frame();
    }
    release_all();
    ui_frame();
    check("TRACKS: a long REC hold asks nothing", !ui.confirm);
    transport_req = 2;
    ui_frame();
}

/* FM: SOUND 1/4 MODEL TUNE DECAY INDEX, 2/4 RATIO MDEC SWEEP FBK, 3/4 VEL LVL PAN NOTE, 4/4 CHOKE; the graph shows the
 * ratio and its sidebands */
static uint32_t fm_graph_lit(void)               /* lit pixels right of the model's name: the sideband bars */
{
    uint32_t n = 0, x, y;
    for (y = Y_GRAPH + 30u; y < Y_GRAPH + 100u; y++)
        for (x = 100u; x < 236u; x++)
            n += fb[y * 240u + x] != 0;
    return n;
}

/* review fix: the FM graph keeps clear of CHOKE / SAMPLE LAYER, and INDEX 0 (a pure sine) shows no sidebands */
static uint32_t fm_graph_area(uint32_t y0, uint32_t y1)   /* lit pixels at x 100..235 of rows y0..y1 (graph rows) */
{
    uint32_t n = 0, x, y;
    for (y = Y_GRAPH + y0; y < Y_GRAPH + y1; y++)
        for (x = 100u; x < 236u; x++)
            n += fb[y * 240u + x] == C_INK;
    return n;
}

static uint32_t fm_sidebands_lit(void)               /* lit pixels above the graph's baseline, outside the carrier */
{
    uint32_t y, x, yb = 0, n = 0;
    for (y = Y_GRAPH; y < 240u && !yb; y++) {         /* the baseline: a row lit across x 100..235 */
        uint32_t c = 0;
        for (x = 100u; x < 236u; x++)
            c += fb[y * 240u + x] == C_INK;
        if (c >= 130u)
            yb = y;
    }
    for (y = yb > 60u ? yb - 60u : 0u; y < yb; y++)
        for (x = 100u; x < 236u; x++)
            if (x != 117u && x != 118u)
                n += fb[y * 240u + x] == C_INK;
    return yb ? n : 0xFFFFFFFFu;
}

static void test_fm_graph_clear(void)
{
    uint32_t plain, labels;
    ui_host_init();
    old_set_model(TSEL, DM_FM);
    for (plain = 0; plain < NPAGES && (ui.home || cur_page()->fam != FAM_SND); plain++)
        tap(B_EDIT);
    ui.force = 1;
    ui_frame();
    plain = fm_graph_area(0u, 110u);
    TSEL->p[P_CHOKE] = 2;
    TSEL->p[P_LLEVEL] = 100;
    ui.force = 1;
    ui_frame();
    labels = fm_graph_area(0u, 110u);
    snap_page("fm/choke_layer");
    check("FM graph: CHOKE and the sample layer's label stay out of the sideband area", labels == plain);
    TSEL->p[P_CHOKE] = 0;
    TSEL->p[P_LLEVEL] = 0;
    ui.force = 1;
    ui_frame();
    labels = fm_sidebands_lit();
    TSEL->p[P_E2] = 0;
    ui.force = 1;
    ui_frame();
    snap_page("fm/index0");
    check("FM graph: INDEX 0 (a pure sine) shows the carrier only; INDEX 60 its sidebands",
          fm_sidebands_lit() == 0u && labels > 40u && labels != 0xFFFFFFFFu);
}

static void test_fm_screens(void)
{
    uint32_t k;
    int16_t *vp;
    ui_host_init();
    old_set_model(TSEL, DM_FM);
    for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_SND); k++)
        tap(B_EDIT);
    ui.force = 1;
    snap_page("fm/sound_1");
    check("FM: SOUND 1/4 holds MODEL TUNE DECAY INDEX; the graph draws the sidebands",
          str_eq(page_desc(cur_page(), 1, &vp)->label, "TUNE") && str_eq(page_desc(cur_page(), 3, &vp)->label, "INDEX") &&
          str_eq(N_FMRATIO[TSEL->p[P_E3]], "1.41") && fm_graph_lit() > 150u);
    page_turn(1);
    ui.force = 1;
    snap_page("fm/sound_2");
    check("FM: SOUND 2/4 holds RATIO MDEC SWEEP FBK",
          str_eq(page_desc(cur_page(), 0, &vp)->label, "RATIO") && str_eq(page_desc(cur_page(), 3, &vp)->label, "FBK"));
}

/* KTRX: SOUND 1/4 holds MODEL TUNE DECAY SWEEP, 2/4 SDEC PUNCH NOISE SUB; the graph draws the pitch and the level,
 * SWEEP raising the pitch curve (more lit rows high in the graph); CHOKE and the layer's label stay out of it */
static uint32_t ktrx_high_lit(void)                  /* lit pixels in the graph's upper half (the pitch above ~2 oct) */
{
    return fm_graph_area(60u, 92u);                   /* (screen rows 134..165: under the title) */
}

static void test_ktrx_screens(void)
{
    uint32_t k, flat, swept, plain, labels;
    int16_t *vp;
    ui_host_init();
    old_set_model(TSEL, DM_KTRX);
    for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_SND); k++)
        tap(B_EDIT);
    TSEL->p[P_E3] = 96;                              /* SDEC 96: a sweep the graph's 300 ms shows */
    TSEL->p[P_E2] = 0;
    ui.force = 1;
    snap_page("ktrx/sweep0");
    flat = ktrx_high_lit();
    {                                                /* review fix: no spike at the level curve's start (its fade-in
                                                      * starts at 0: the first column stays low) */
        uint32_t x, y, n = 0;
        for (y = Y_GRAPH + 66u; y < Y_GRAPH + 80u; y++)
            for (x = 100u; x < 102u; x++)
                n += fb[y * 240u + x] == C_INK;
        check("KTRX graph: SWEEP 0: nothing at the top of its first columns (no spike from the level's start)", n == 0u);
    }
    TSEL->p[P_E2] = 127;
    ui.force = 1;
    snap_page("ktrx/sweep127");
    swept = ktrx_high_lit();
    TSEL->p[P_E2] = 64;
    TSEL->p[P_E3] = 32;
    ui.force = 1;
    snap_page("ktrx/sound_1");
    plain = fm_graph_area(0u, 110u);
    printf("     KTRX graph: upper half lit %u (SWEEP 0) / %u (SWEEP 127, SDEC 96)\n", flat, swept);
    check("KTRX: SOUND 1/4 holds MODEL TUNE DECAY SWEEP; the graph draws the pitch and the level; SWEEP 127 raises "
          "the pitch curve",
          str_eq(page_desc(cur_page(), 1, &vp)->label, "TUNE") && str_eq(page_desc(cur_page(), 3, &vp)->label, "SWEEP") &&
              fm_graph_lit() > 150u && swept > flat + 20u);
    TSEL->p[P_CHOKE] = 2;
    TSEL->p[P_LLEVEL] = 100;
    ui.force = 1;
    ui_frame();
    labels = fm_graph_area(0u, 110u);
    check("KTRX graph: CHOKE and the sample layer's label stay out of it", labels == plain);
    TSEL->p[P_CHOKE] = 0;
    TSEL->p[P_LLEVEL] = 0;
    page_turn(1);
    ui.force = 1;
    snap_page("ktrx/sound_2");
    check("KTRX: SOUND 2/4 holds SDEC PUNCH NOISE SUB",
          str_eq(page_desc(cur_page(), 0, &vp)->label, "SDEC") && str_eq(page_desc(cur_page(), 3, &vp)->label, "SUB"));
}

/* SDTRX, B2TRX: their SOUND pages (MODEL + the first three knobs, then the next four) */
static void test_trx_drum_screens(void)
{
    static const struct { uint32_t m; const char *dir, *l1, *l3, *l4, *l7; } S[] = {
        {DM_SDTRX, "trxdrums/sdtrx", "TUNE", "SNAP", "TONE", "CLIP"},
        {DM_B2TRX, "trxdrums/b2trx", "TUNE", "RAMP", "HOLD", "DIRT"}};
    uint32_t i, k, ok = 1;
    int16_t *vp;
    for (i = 0; i < 2u; i++) {
        char name[48];
        ui_host_init();
        old_set_model(TSEL, S[i].m);
        for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_SND); k++)
            tap(B_EDIT);
        ui.force = 1;
        snprintf(name, sizeof name, "%s_sound_1", S[i].dir);
        snap_page(name);
        ok &= str_eq(page_desc(cur_page(), 1, &vp)->label, S[i].l1) && str_eq(page_desc(cur_page(), 3, &vp)->label, S[i].l3);
        page_turn(1);
        ui.force = 1;
        snprintf(name, sizeof name, "%s_sound_2", S[i].dir);
        snap_page(name);
        ok &= str_eq(page_desc(cur_page(), 0, &vp)->label, S[i].l4) && str_eq(page_desc(cur_page(), 3, &vp)->label, S[i].l7);
    }
    check("SDTRX, B2TRX: SOUND 1/4 holds MODEL TUNE DECAY SNAP / RAMP, 2/4 TONE .. CLIP / HOLD .. DIRT", ok);
}

/* CPTRX / MATRX: their SOUND pages hold the knobs in order (1/4 MODEL TUNE DECAY + the third, 2/4 the next four, 3/4
 * the last); screenshots build/ui_shots/engines/trxn_*.png */
static void test_trxn_screens(void)
{
    static const struct { uint32_t m; const char *k3, *k4, *k7, *s1, *s2, *s3; } M[2] = {
        {DM_CPTRX, "CLPY", "HARD", "RTUN", "engines/trxn_cptrx_1", "engines/trxn_cptrx_2", "engines/trxn_cptrx_3"},
        {DM_MATRX, "ATT", "REV", "HARD", "engines/trxn_matrx_1", "engines/trxn_matrx_2", "engines/trxn_matrx_3"}};
    uint32_t i, k, ok = 1;
    int16_t *vp;
    for (i = 0; i < 2u; i++) {
        ui_host_init();
        old_set_model(TSEL, M[i].m);
        for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_SND); k++)
            tap(B_EDIT);
        ui.force = 1;
        snap_page(M[i].s1);
        ok &= str_eq(page_desc(cur_page(), 1, &vp)->label, "TUNE") && str_eq(page_desc(cur_page(), 2, &vp)->label, "DECAY") &&
              str_eq(page_desc(cur_page(), 3, &vp)->label, M[i].k3);
        page_turn(1);
        ui.force = 1;
        snap_page(M[i].s2);
        ok &= str_eq(page_desc(cur_page(), 0, &vp)->label, M[i].k4);
        page_turn(1);
        ui.force = 1;
        snap_page(M[i].s3);
        ok &= str_eq(page_desc(cur_page(), 0, &vp)->label, M[i].k7);
    }
    check("CPTRX, MATRX: SOUND 1/4 MODEL TUNE DECAY CLPY / ATT, 2/4 from HARD / REV, 3/4 RTUN / HARD", ok);
}

/* SDEFM / CPEFM / RSEFM: their SOUND pages hold the knobs in order (1/4 MODEL TUNE DECAY + the third, 2/4 the next
 * four, 3/4 the last); screenshots build/ui_shots/engines/efmn_*.png */
static void test_efmn_screens(void)
{
    static const struct { uint32_t m; const char *k3, *k4, *k8, *s1, *s2, *s3; } M[3] = {
        {DM_SDEFM, "NOIS", "NDEC", "HPF", "engines/efmn_sdefm_1", "engines/efmn_sdefm_2", "engines/efmn_sdefm_3"},
        {DM_CPEFM, "CLPS", "CDEC", "HPF", "engines/efmn_cpefm_1", "engines/efmn_cpefm_2", "engines/efmn_cpefm_3"},
        {DM_RSEFM, "MOD", "HPF", "SMOD", "engines/efmn_rsefm_1", "engines/efmn_rsefm_2", "engines/efmn_rsefm_3"}};
    uint32_t i, k, ok = 1;
    int16_t *vp;
    for (i = 0; i < 3u; i++) {
        ui_host_init();
        old_set_model(TSEL, M[i].m);
        for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_SND); k++)
            tap(B_EDIT);
        ui.force = 1;
        snap_page(M[i].s1);
        ok &= str_eq(page_desc(cur_page(), 1, &vp)->label, "TUNE") && str_eq(page_desc(cur_page(), 2, &vp)->label, "DECAY") &&
              str_eq(page_desc(cur_page(), 3, &vp)->label, M[i].k3);
        page_turn(1);
        ui.force = 1;
        snap_page(M[i].s2);
        ok &= str_eq(page_desc(cur_page(), 0, &vp)->label, M[i].k4);
        page_turn(1);
        ui.force = 1;
        snap_page(M[i].s3);
        ok &= str_eq(page_desc(cur_page(), 0, &vp)->label, M[i].k8);
    }
    check("SDEFM, CPEFM, RSEFM: SOUND 1/4 MODEL TUNE DECAY NOIS / CLPS / MOD, 2/4 from NDEC / CDEC / HPF, 3/4 HPF / HPF / "
          "SMOD", ok);
}

/* ---- ACID plan B: KEYS (spec §5) */
/* an SCL tap turns KEYS on (its LED, the top bar KEYS F3) and off; the 27 keys play the selected track chromatically
 * (a drum transposed: nt = NOTE - 60, F3 = 53); OCT- / OCT+ shift by an octave, -2..+2; KEYS selects no track */
static void test_keys_map_oct(void)
{
    char b[16];
    uint32_t k, a, ok = 1;
    ui_host_init();
    tap(B_SCL);
    check("KEYS: an SCL tap turns it on: the SCL LED lit, the top bar KEYS F3, the keys KEYS' (seq_mode 3)",
          ui.keys && led_on(panel.btn[B_SCL]) && head_mode(b) && str_eq(b, "KEYS F3") && song.seq_mode == 3u);
    for (k = 0; k < 27u; k++) {
        a = hit_age(&trk[0]);
        keys(1u << k);
        ui_frame();
        keys(0);
        ui_frame();
        ok &= hit_age(&trk[0]) != a && trk[0].nt == (int32_t)k - 7;
    }
    check("KEYS: key k (0 = F3) hits the selected drum transposed to NOTE 53 + k (nt k - 7), every key", ok);
    check("KEYS: the keys select no track (HOME)", song.sel == 0);
    tap(B_OCTUP);
    tap(B_OCTUP);
    tap(B_OCTUP);
    keys(1u);
    ui_frame();
    keys(0);
    ui_frame();
    check("KEYS: OCT+ three times: +2 octaves at most (F3's key plays F5, nt +17), KEYS F5, OCT+ lit",
          song.octave == 2 && trk[0].nt == 17 && head_mode(b) && str_eq(b, "KEYS F5") && led_on(panel.btn[B_OCTUP]));
    for (k = 0; k < 5u; k++)
        tap(B_OCTDN);
    keys(1u);
    ui_frame();
    keys(0);
    ui_frame();
    check("KEYS: OCT- five times: -2 octaves at most (F1, nt -31), KEYS F1",
          song.octave == -2 && trk[0].nt == -31 && head_mode(b) && str_eq(b, "KEYS F1"));
    tap(B_SCL);
    keys(1u << KEY_TRK_KEY[3]);
    ui_frame();
    keys(0);
    ui_frame();
    check("KEYS off: the LED dark, no KEYS in the top bar, the keys select and hit tracks again",
          !ui.keys && !led_on(panel.btn[B_SCL]) && !head_mode(b) && song.sel == 3 && song.seq_mode == 0u);
}

/* ACID under KEYS: mono, the last key wins; held: the gate stays open past GATE; a key over the sounding one slides
 * (same voice); the sounding key's release closes the gate, an earlier key's does nothing */
static void test_keys_acid_mono(void)
{
    track_t *t = &trk[0];
    uint32_t age, k;
    ui_host_init();
    old_set_model(t, DM_ACID);
    tap(B_SCL);
    keys(1u << 7);                                   /* C4 */
    ui_frame();
    age = t->v[0].age;
    check("KEYS, ACID: a key starts its note (C4), the gate open",
          t->v[0].active && (t->v[0].x[2] & 1) && t->v[0].x[1] == 60 * 16);
    for (k = 0; k < 400u; k++)                       /* 400 blocks, 290 ms: well past GATE (half a 1/16) */
        ui_frame();
    check("KEYS, ACID: held past GATE the gate stays open (the key holds it)", (t->v[0].x[2] & 1) != 0);
    keys(1u << 7 | 1u << 12);                        /* F4 over it */
    ui_frame();
    check("KEYS, ACID: a key over the sounding one slides to it (same voice, no restart)",
          t->v[0].age == age && t->v[0].x[1] == 65 * 16 && (t->v[0].x[2] & 1));
    keys(1u << 12);                                  /* C4 (no longer sounding) let go */
    ui_frame();
    check("KEYS, ACID: an earlier key let go: the note goes on", (t->v[0].x[2] & 1) != 0);
    keys(0);
    ui_frame();
    check("KEYS, ACID: the sounding key let go: the gate closes", !(t->v[0].x[2] & 1));
    keys(1u << 9);                                   /* D4 after all were let go */
    ui_frame();
    check("KEYS, ACID: a key after all were let go starts a new note", t->v[0].age != age && t->v[0].x[1] == 62 * 16);
    keys(0);
    ui_frame();
}

/* Review Focus 1: a key's ACID note ends on its release wherever the keys went meanwhile */
static void test_keys_release_anywhere(void)
{
    ui_host_init();
    old_set_model(&trk[0], DM_ACID);
    old_set_model(&trk[1], DM_ACID);
    tap(B_SCL);
    keys(1u << 7);                                   /* C4 on track 1 */
    ui_frame();
    tap(B_SCL);                                      /* KEYS off, C4 still held */
    keys(0);
    ui_frame();
    check("KEYS turned off while a key holds an ACID note: its release still ends it",
          !(trk[0].v[0].x[2] & 1) && song.seq_mode == 0u);
    tap(B_SCL);
    keys(1u << 7);
    ui_frame();
    turn(EN_ALGO, 1);                                /* track 2 selected while C4 holds track 1's note */
    ui_frame();
    keys(0);
    ui_frame();
    check("KEYS: the track changed while held: the release ends track 1's note", song.sel == 1 && !(trk[0].v[0].x[2] & 1));
    keys(1u << 7);                                   /* C4 on track 2 */
    ui_frame();
    tap(B_HOME);                                     /* a HOME tap: HOME's TRACKS mode (its own keys) */
    keys(0);
    ui_frame();
    check("KEYS: TRACKS opened while held: the release ends the note", mix_mode() && song.seq_mode == 1u &&
          !(trk[1].v[0].x[2] & 1));
    tap(B_HOME);
    keys(1u << 7);                                   /* C4 on track 2, held */
    ui_frame();
    turn(EN_ALGO, -1);                               /* track 1 */
    ui_frame();
    keys(1u << 7 | 1u << 9);                         /* D4 on track 1 while C4 still holds track 2's note */
    ui_frame();
    check("KEYS: a key on another track ends the held note (mono over the keys)",
          !(trk[1].v[0].x[2] & 1) && (trk[0].v[0].x[2] & 1) && trk[0].v[0].x[1] == 62 * 16);
    keys(0);
    ui_frame();
    check("KEYS: ... and its own release ends the new one", !(trk[0].v[0].x[2] & 1));
}

/* Review Focus 3: with KEYS on, FX held (PERFORM), TRACKS, COMP, the menu and NAME keep their keys */
static void test_keys_owners(void)
{
    uint32_t a;
    int16_t duck;
    ui_host_init();
    old_set_model(&trk[0], DM_ACID);
    tap(B_SCL);
    press(B_FX);
    ui_frame();
    keys(1u << 4);                                   /* A3 with FX held */
    fx_hold_frames(2);
    check("KEYS + FX held: the key is PERFORM's (track 3 muted while held), no note",
          ((perf_held >> (PF_M1 + 2u)) & 1u) && !trk[0].v[0].active);
    keys(0);
    fx_hold_frames(1);
    release_all();
    ui_frame();
    open_tracks();                                   /* HOME's TRACKS mode, nothing armed: its step grid */
    keys(1u << STEP_KEY[2]);
    ui_frame();
    keys(0);
    ui_frame();
    check("KEYS + TRACKS: a white key is the track's step (seq_mode 1), no note",
          mix_mode() && song.seq_mode == 1u && trk[0].step[2].on && !trk[0].v[0].active);
    open_comp(0);
    duck = trk[3].p[P_DUCK];
    keys(1u << KEY_TRK_KEY[3]);
    ui_frame();
    keys(0);
    ui_frame();
    check("KEYS + COMP: a white key ducks its track, no note", comp_mode() && trk[3].p[P_DUCK] != duck &&
          !trk[0].v[0].active);
    hold(B_HOME);                                    /* the menu */
    check("KEYS + the menu: the keys are not KEYS' (as without it)", ui.menu && !keys_mode() && song.seq_mode == 0u);
    hold(B_HOME);
    project_page();
    project_save(0);
    name_open(NK_RENAME, 0);
    a = trk[0].v[0].age;
    name_type(7);                                    /* C4 */
    check("KEYS + NAME: the keys type, no note", name_on() && song.seq_mode == 1u && trk[0].v[0].age == a);
    name_close();
}

/* main.c's UBOOT (OCT- + OCT+ held 5 s): under KEYS both buttons stay visible; pressed together the octave is back */
static void test_keys_oct_uboot(void)
{
    uint32_t both;
    ui_host_init();
    tap(B_SCL);
    seq_open("STEP");
    both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
    press(B_OCTDN);
    press(B_OCTUP);
    ui_input();
    check("KEYS: OCT- + OCT+ together: the octave back where it was, both buttons visible to main.c (UBOOT), no bank",
          song.octave == 0 && (fm1_in.buttons & both) == both && ui.bank == 0);
    release_all();
}

/* ---- ACID plan B: live record (spec §6) */
/* armed and playing, KEYS on ACID: a key writes the nearest step (ON + NOTE), TIE on each later step it is held past
 * the middle of, a legato key SLIDE on the held note's last step */
static void test_keys_live_rec(void)
{
    track_t *t = &trk[0];
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    tap(B_SCL);                                      /* KEYS, HOME */
    song.rec = 1u;                                   /* track 1 armed (as TRACKS' REC tap) */
    transport_req = 1;
    seq_play_to(0, 2);                               /* step 3 just started */
    keys(1u << 7);                                   /* C4 */
    ui_frame();
    seq_play_to(0, 5);                               /* held through steps 4 and 5's middles */
    keys(1u << 7 | 1u << 11);                        /* E4 over it: legato */
    ui_frame();
    keys(1u << 11);                                  /* C4 let go */
    ui_frame();
    keys(0);                                         /* E4 let go at once */
    ui_frame();
    seq_play_to(0, 7);
    check("KEYS live record: the key's note on the nearest step (ON + C4)", t->step[2].on && !t->step[2].tie &&
          t->step[2].nt == 0);
    check("KEYS live record: held into the later steps: TIE on them", t->step[3].tie && !t->step[3].on &&
          t->step[4].tie && !t->step[4].on);
    check("KEYS live record: a legato key: SLIDE on the held note's last step, its own note (E4) next",
          t->step[4].slide && !t->step[2].slide && !t->step[3].slide && t->step[5].on && t->step[5].nt == 4 &&
          !t->step[5].slide);
    check("KEYS live record: let go before the next step's middle: nothing there", !t->step[6].tie && !t->step[6].on);
    transport_req = 2;
    ui_frame();
}

/* Review Focus 4: pressed past the middle of the last step: the next one across the wrap (LEN 4), which then does not
 * hit a second time; the TIE follows across it */
static void test_keys_live_rec_wrap(void)
{
    track_t *t = &trk[0];
    uint32_t k, age;
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    t->p[P_SLEN] = 4;
    tap(B_SCL);
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 3);
    for (k = 0; k < 120u; k++)                       /* 70 % into step 4 (172 blocks a 1/16 at 120 BPM) */
        ui_frame();
    keys(1u << 9);                                   /* D4 */
    ui_frame();
    age = t->v[0].age;
    check("KEYS live record past mid-step: the next step across the wrap (step 1: ON + D4), step 4 untouched",
          t->step[0].on && t->step[0].nt == 2 && !t->step[3].on && !t->step[3].tie && t->rskip && t->rskip_idx == 0u);
    seq_play_to(0, 2);                               /* held through step 1 and step 2's middle */
    check("KEYS live record: the written step does not hit again, the note holds (same voice); TIE on step 2",
          t->v[0].age == age && (t->v[0].x[2] & 1) && t->step[1].tie && t->step[0].on && !t->step[0].tie);
    keys(0);
    ui_frame();
    seq_play_to(0, 3);
    check("KEYS live record: let go at step 3's start: no TIE there", !t->step[2].tie && !t->step[2].on);
    transport_req = 2;
    ui_frame();
}

/* Review Focus 4, the block a held note passes a step's middle in: a legato key pressed in that very block still finds
 * the TIE there (keys_tie before the presses), so the SLIDE goes on it and no rest is left between the two notes */
static void test_keys_live_rec_mid_block(void)
{
    track_t *t = &trk[0];
    uint32_t k, half;
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    tap(B_SCL);
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 2);
    keys(1u << 7);                                   /* C4 on step 3 */
    ui_frame();
    seq_play_to(0, 3);
    half = step_samples(t, div_period((uint32_t)t->p[P_SDIV], t->seq_rem), t->seq_cnt) / 2u;
    for (k = 0; k < 400u && t->seq_pos <= half; k++)  /* the block that passes step 4's middle has just run */
        ui_frame();
    keys(1u << 7 | 1u << 11);                        /* E4 over it in the next block */
    ui_frame();
    keys(0);
    ui_frame();
    seq_play_to(0, 6);
    check("KEYS live record: a legato key in the block that passes the middle: TIE + SLIDE on step 4, E4 on step 5",
          t->step[2].on && t->step[2].nt == 0 && t->step[3].tie && t->step[3].slide && t->step[4].on &&
          t->step[4].nt == 4);
    transport_req = 2;
    ui_frame();
}

/* Review fix: a note held longer than LEN stops recording at the phrase's start: its TIEs never reach the step the key
 * wrote (it stays ON with its note); a later legato note does not move that start */
static void test_keys_live_rec_long_hold(void)
{
    track_t *t = &trk[0];
    uint32_t k, ok = 1;
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    t->p[P_SLEN] = 4;
    tap(B_SCL);
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 1);                               /* step 2 just started */
    keys(1u << 7);                                   /* C4 on step 2 */
    ui_frame();
    seq_play_to(0, 3);
    seq_play_to(0, 1);                               /* across the wrap */
    seq_play_to(0, 3);                               /* held through 5 steps' middles */
    keys(0);
    ui_frame();
    for (k = 0; k < 4u; k++)
        ok &= k == 1u || (t->step[k].tie && !t->step[k].on && t->step[k].nt == 0);
    check("KEYS live record, held longer than LEN: the start step stays ON with its note (C4), TIEs on the others",
          t->step[1].on && !t->step[1].tie && t->step[1].nt == 0 && ok);
    transport_req = 2;
    ui_frame();
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    t->p[P_SLEN] = 4;
    tap(B_SCL);
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 1);
    keys(1u << 7);                                   /* C4 on step 2 */
    ui_frame();
    seq_play_to(0, 3);                               /* TIE on step 3 */
    keys(1u << 7 | 1u << 11);                        /* E4 over it on step 4: legato */
    ui_frame();
    keys(1u << 11);
    ui_frame();
    seq_play_to(0, 1);
    seq_play_to(0, 3);                               /* E4 held round to the phrase's start and past it */
    keys(0);
    ui_frame();
    check("KEYS live record, a legato note held round the loop: the phrase's start (C4) kept, step 3 TIE + SLIDE kept",
          t->step[1].on && !t->step[1].tie && t->step[1].nt == 0 && t->step[2].tie && t->step[2].slide &&
          t->step[2].nt == 0 && t->step[3].on && t->step[3].nt == 4 && t->step[0].tie && t->step[0].nt == 4);
    transport_req = 2;
    ui_frame();
}

/* Review fix: a step held through becomes a plain TIE: its PROB, RATCH, ACC (and SLIDE) cleared, as rec_hit's step */
static void test_keys_live_rec_tie_clean(void)
{
    track_t *t = &trk[0];
    step_t *s = &t->step[3];
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    s->on = 1;
    s->acc = 1;
    s->slide = 1;
    s->cond = 5;
    s->rat = 2;
    s->nt = -7;
    tap(B_SCL);
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 2);
    keys(1u << 7);                                   /* C4 on step 3, held through step 4 */
    ui_frame();
    seq_play_to(0, 4);
    keys(0);
    ui_frame();
    check("KEYS live record: a step held through is a plain TIE (no ACC, PROB, RATCH, SLIDE; the held note)",
          s->tie && !s->on && !s->acc && !s->slide && s->cond == 0 && s->rat == 0 && s->nt == 0);
    transport_req = 2;
    ui_frame();
}

/* a drum track records ON + NOTE only: holding its key writes no TIE */
static void test_keys_live_rec_drum(void)
{
    track_t *t = &trk[0];
    ui_host_init();
    memset(t->step, 0, sizeof t->step);
    tap(B_SCL);
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 2);
    keys(1u << 9);                                   /* D4, held into step 6 */
    ui_frame();
    seq_play_to(0, 5);
    keys(0);
    ui_frame();
    check("KEYS live record, a drum: ON + NOTE (D4: nt +2) on the nearest step, no TIE on the steps held through",
          t->step[2].on && t->step[2].nt == 2 && !t->step[3].tie && !t->step[3].on && !t->step[4].tie &&
          !t->step[4].on);
    transport_req = 2;
    ui_frame();
}

/* spec §2: a step the grid keys turn on gets the track's last played / recorded note (C4 at first); a TIE step turned
 * on plays (its TIE cleared) */
static void test_step_key_note(void)
{
    track_t *t;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    open_step_page();
    grid_hold(WHITE[0], 0);
    check("grid: a new step, nothing played yet: C4", t->step[0].on && t->step[0].nt == 0);
    tap(B_SCL);                                      /* KEYS: F4 played */
    keys(1u << 12);
    ui_frame();
    keys(0);
    ui_frame();
    tap(B_SCL);
    check("KEYS: the track's last key note kept (F4, nt +5)", t->key_nt == 5);
    grid_hold(WHITE[1], 0);
    check("grid: a new step gets the track's last key note (F4)", t->step[1].on && t->step[1].nt == 5);
    t->step[2].tie = 1;
    t->step[2].nt = -3;
    grid_hold(WHITE[2], 0);
    check("grid: a TIE step turned on plays: ON, TIE cleared, the last key note", t->step[2].on && !t->step[2].tie &&
          t->step[2].nt == 5);
    t->step[3].tie = 1;
    grid_acc(WHITE[3]);
    check("grid: a TIE step accented (OCT+ held): ON + ACC, TIE cleared", t->step[3].on && t->step[3].acc &&
          !t->step[3].tie && t->step[3].nt == 5);
}

/* bug 2026-10-10 (user: "ACID only"): the last KEYS note goes to the steps the grid keys turn on only on a gated
 * (ACID) track; on a drum the new step is C4 (tap, hold, hold + knob) */
static void test_step_key_note_drum(void)
{
    track_t *t;
    uint32_t pass;
    for (pass = 0; pass < 2u; pass++) {              /* 0: a drum (the kick), 1: ACID */
        int8_t want;
        ui_host_init();
        t = TSEL;
        if (pass)
            old_set_model(t, DM_ACID);
        memset(t->step, 0, sizeof t->step);
        tap(B_SCL);                                  /* KEYS: D4 played */
        keys(1u << 9);
        ui_frame();
        keys(0);
        ui_frame();
        tap(B_SCL);
        open_step_page();
        grid_hold(WHITE[0], 0);                      /* tap */
        grid_acc(WHITE[1]);                          /* OCT+ held: an accent */
        grid_hold_turn(WHITE[2], 0, -2);             /* hold + KNOB 1 */
        keys(0);
        ui_frame();
        want = pass ? 2 : 0;
        check(pass ? "ACID: a step the grid keys turn on (tap, hold, hold + knob) gets the last KEYS note (D4)"
                   : "drum: a step the grid keys turn on (tap, hold, hold + knob) is C4 after KEYS played D4",
              t->step[0].on && t->step[1].on && t->step[2].on && t->step[0].nt == want && t->step[1].nt == want &&
                  t->step[2].nt == want);
    }
}

/* bug 2026-10-10: no last KEYS note carries over a LOAD or a cleared pattern */
static void test_key_note_reset(void)
{
    uint32_t k, ok = 1;
    ui_host_init();
    project_save(0);
    for (k = 0; k < NTRK; k++)
        trk[k].key_nt = 5;
    project_load(0);
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].key_nt == 0;
    check("LOAD: every track's last KEYS note back to C4", ok);
    trk[2].key_nt = 5;
    track_clear(&trk[2]);
    check("a track's pattern cleared (CLEAR TRACK, CLR PATTERN): its last KEYS note back to C4", trk[2].key_nt == 0);
    for (k = 0; k < NTRK; k++)
        trk[k].key_nt = 5;
    seq_clear_all();
    for (ok = 1, k = 0; k < NTRK; k++)
        ok &= trk[k].key_nt == 0;
    check("CLR*: every track's last KEYS note back to C4", ok);
}

/* bug 2026-10-10: live record of a drum key (or MIDI) writes the step's NOTE as C4 (the hit sounded at C4): a NOTE
 * left on an off step (a step-record REST, TIME -> REST) does not come back */
static void test_rec_hit_drum_note(void)
{
    track_t *t = &trk[0];
    ui_host_init();
    memset(t->step, 0, sizeof t->step);
    t->step[3].nt = 7;                               /* off, a NOTE left on it */
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 3);
    keys(1u << 0);                                   /* F3: track 1's key (HOME, KEYS off) */
    ui_frame();
    keys(0);
    ui_frame();
    check("drum live record onto a step holding a NOTE: ON at C4", t->step[3].on && t->step[3].nt == 0);
    transport_req = 2;
    ui_frame();
}

/* ---- ACID plan B: step record (spec §6) */
static void srec_open(void)                          /* ACID on track 1, KEYS, STEP, stopped; a REC tap: step record */
{
    ui_host_init();
    old_set_model(TSEL, DM_ACID);
    memset(TSEL->step, 0, sizeof TSEL->step);
    tap(B_SCL);
    seq_open("STEP");
    tap(B_REC);
}
static void srec_turn(uint32_t knob, int32_t d)      /* one detent, 0.1 s after the last (no acceleration) */
{
    turn(EN_K1 + knob, d);
    host_ticks += 100u * 1000u * FM1_TICKS_PER_US;
    ui_frame();
}

static void test_srec_keys(void)
{
    char b[16];
    track_t *t;
    srec_open();
    t = TSEL;
    check("step record: a REC tap on STEP (KEYS, stopped) starts it at step 1: STEP REC 01, REC lit, still STEP, nothing armed",
          ui.srec && ui.sr_cur == 0u && head_mode(b) && str_eq(b, "STEP REC 01") && led_on(panel.btn[B_REC]) &&
          !ui.home && cur_page()->scope == SC_GRID && song.rec == 0u);
    keys(1u << 7);                                   /* C4 */
    ui_frame();
    check("step record: a key writes its note to the cursor step (ON, C4), the cursor waits for its release",
          t->step[0].on && !t->step[0].tie && t->step[0].nt == 0 && ui.sr_cur == 0u);
    keys(0);
    ui_frame();
    check("step record: let go: the cursor moves on (STEP REC 02)", ui.sr_cur == 1u && head_mode(b) &&
          str_eq(b, "STEP REC 02"));
    keys(1u << 9);                                   /* D4 */
    ui_frame();
    keys(1u << 9 | 1u << 11);                        /* E4 before D4 is let go */
    ui_frame();
    check("step record: a key before the last one is let go: SLIDE on that step, the new note on the next",
          t->step[1].nt == 2 && t->step[1].slide && t->step[2].on && t->step[2].nt == 4 && !t->step[2].slide &&
          ui.sr_cur == 2u);
    keys(1u << 9);                                   /* E4 (the cursor's) let go */
    ui_frame();
    keys(0);                                         /* D4 let go */
    ui_frame();
    check("step record: the cursor's key let go moves it on, the earlier one does nothing", ui.sr_cur == 3u);
    tap(B_OCTUP);
    keys(1u << 7);
    ui_frame();
    keys(0);
    ui_frame();
    check("step record: OCT+ (KEYS): C5 on step 4, the track's last note C5", t->step[3].on && t->step[3].nt == 12 &&
          ui.sr_cur == 4u && song.octave == 1 && t->key_nt == 12);
}

static void test_srec_knobs(void)
{
    track_t *t;
    srec_open();
    t = TSEL;
    t->step[2].on = 1;
    srec_turn(0, 1);
    srec_turn(0, 1);
    check("step record: KNOB 1 moves the cursor (step 3)", ui.sr_cur == 2u);
    srec_turn(1, 1);
    srec_turn(1, 1);
    srec_turn(1, 1);
    check("step record: KNOB 2 the cursor step's NOTE (+3: D#4)", t->step[2].nt == 3);
    srec_turn(2, 1);
    check("step record: KNOB 3 TIME: NOTE -> TIE", !t->step[2].on && t->step[2].tie);
    srec_turn(2, 1);
    check("step record: TIME: TIE -> REST", !t->step[2].on && !t->step[2].tie);
    srec_turn(2, 1);
    check("step record: TIME: REST is the end", !t->step[2].on && !t->step[2].tie);
    srec_turn(2, -1);
    srec_turn(2, -1);
    check("step record: TIME: back to NOTE", t->step[2].on && !t->step[2].tie);
    srec_turn(3, 1);
    check("step record: KNOB 4 FLAG: - -> ACC", t->step[2].acc && !t->step[2].slide);
    srec_turn(3, 1);
    check("step record: FLAG: ACC -> SLD", !t->step[2].acc && t->step[2].slide);
    srec_turn(3, 1);
    check("step record: FLAG: SLD -> A+S", t->step[2].acc && t->step[2].slide);
    srec_turn(3, -1);
    srec_turn(3, -1);
    srec_turn(3, -1);
    check("step record: FLAG: back to -", !t->step[2].acc && !t->step[2].slide);
    tap(B_EDIT);
    check("step record: an EDIT tap writes a rest and moves on (no SOUND page)", !t->step[2].on && !t->step[2].tie &&
          ui.sr_cur == 3u && ui.srec && cur_page()->scope == SC_GRID);
    keys(1u << 7);                                   /* C4 on step 4, held ... */
    ui_frame();
    srec_turn(0, -1);                                /* ... while KNOB 1 moves the cursor back */
    keys(0);
    ui_frame();
    check("step record: a key let go after KNOB 1 moved the cursor moves it no further", ui.sr_cur == 2u &&
          t->step[3].on && t->step[3].nt == 0);
}

/* Review Focus 5: every way out ends step record; nothing is written after it */
static void test_srec_exits(void)
{
    track_t *t;
    uint32_t i;
    srec_open();
    tap(B_REC);
    check("step record ends: a REC tap (STEP stays, nothing armed)", !ui.srec &&
          cur_page()->scope == SC_GRID && song.rec == 0u);
    srec_open();
    press(B_PLAY);
    ui_frame();
    release_all();
    ui_frame();
    check("step record ends: PLAY (the transport starts)", !ui.srec && song.playing);
    srec_open();
    press(B_SEQ);
    ui_frame();
    release_all();
    ui_frame();
    check("step record ends: leaving the page (SEQ: PATTERN)", !ui.srec && cur_page()->graph == GR_STEPS);
    srec_open();
    tap(B_HOME);
    check("step record ends: HOME", !ui.srec && ui.home);
    srec_open();
    tap(B_SCL);
    check("step record ends: KEYS off", !ui.srec && !ui.keys);
    srec_open();
    turn(EN_ALGO, 1);
    ui_frame();
    check("step record ends: another track", !ui.srec && song.sel == 1);
    srec_open();
    transport_req = 1;                               /* as a MIDI Start or a song would */
    ui_frame();
    ui_frame();
    check("step record ends: the transport started elsewhere", !ui.srec && song.playing);
    srec_open();
    t = TSEL;
    keys(1u << 7);                                   /* C4 held ... */
    ui_frame();
    tap(B_REC);                                      /* ... over the end */
    keys(0);
    ui_frame();
    keys(1u << 9);                                   /* a key after the end */
    ui_frame();
    keys(0);
    ui_frame();
    check("step record: after its end no key writes a step (only C4 on step 1)", !ui.srec && t->step[0].on &&
          t->step[0].nt == 0 && !t->step[1].on && !t->step[1].tie);
    srec_open();
    for (i = 0; i < 1000u && !ui.menu; i++) {        /* HOME held: the menu */
        fm1_in.buttons |= 1u << panel.btn[B_HOME];
        ui_frame();
    }
    release_all();
    ui_frame();
    check("step record ends: the menu opened (REC dark under it)", ui.menu && !ui.srec && !led_on(panel.btn[B_REC]));
    menu_close();
    ui_frame();
    srec_open();
    press(B_FX);
    fx_hold_frames(500);                             /* PERFORM's layer: its keys, its knobs */
    check("step record ends: FX held (PERFORM's layer)", ui.layer && !ui.srec);
    release_all();
    ui_frame();
    check("step record: FX let go: still off, still STEP", !ui.srec && cur_page()->scope == SC_GRID);
    srec_open();
    name_open(NK_RENAME, 0);
    ui_frame();
    check("step record ends: NAME", !ui.srec);
    name_close();
    ui_frame();
    ui_host_init();
    seq_open("STEP");
    tap(B_REC);
    check("KEYS off: a REC tap on STEP arms the track and starts play (STEP stays)", !ui.srec && song.rec == 1u &&
          song.playing && cur_page()->scope == SC_GRID);
    transport_req = 2;
    ui_frame();
    ui_host_init();
    tap(B_SCL);
    seq_open("STEP");
    transport_req = 1;
    ui_frame();
    tap(B_REC);
    check("KEYS on, playing: a REC tap on STEP arms the track (STEP stays)", !ui.srec && song.rec == 1u &&
          cur_page()->scope == SC_GRID);
    transport_req = 2;
    ui_frame();
}

/* ---- ACID plan B: the display (spec §7) */
static uint16_t gpx(int32_t x, int32_t gy)          /* the screen pixel at x, graph row gy */
{
    return fb[(uint32_t)(Y_GRAPH + G_OY + gy) * 240u + (uint32_t)x];
}
static void acid_line(track_t *t)                    /* C4 ACC, D4 SLIDE, E4, TIE, G4 RATCH 2, a rest */
{
    memset(t->step, 0, sizeof t->step);
    t->step[0].on = 1;
    t->step[0].acc = 1;
    t->step[1].on = 1;
    t->step[1].nt = 2;
    t->step[1].slide = 1;
    t->step[2].on = 1;
    t->step[2].nt = 4;
    t->step[3].tie = 1;
    t->step[4].on = 1;
    t->step[4].nt = 7;
    t->step[4].rat = 1;
}

static void test_roll_draw(void)
{
    int32_t x, k, yc, yd, ye, yg, xx, yy, ok = 1, amb = 0, rest = 0, lab = 0;
    ui_host_init();
    old_set_model(TSEL, DM_ACID);
    acid_line(TSEL);
    seq_open("STEP");
    ui.force = 1;
    ui_frame();
    yc = pr_row_y(60);
    yd = pr_row_y(62);
    ye = pr_row_y(64);
    yg = pr_row_y(67);
    check("roll: an ACID track's STEP page: the view holds the bank's notes (C4..G4)", proll.lo <= 60 &&
          proll.lo + PR_ROWS > 67);
    for (k = 0; k < PR_RH; k++)
        ok &= gpx(PR_X0 + 4, yc + k) == C_INK;
    check("roll: an accented note (C4) ink, its row's full height", ok);
    x = PR_X0 + 2 * PR_CW + 4;
    check("roll: a plain note (E4) a 2 px bar inside its row", gpx(x, ye + 1) == C_INK && gpx(x, ye + 2) == C_INK &&
          gpx(x, ye) != C_INK && gpx(x, ye + 3) != C_INK);
    check("roll: a TIE carries the note before through its column, across the step line",
          gpx(PR_X0 + 3 * PR_CW, ye + 1) == C_INK && gpx(PR_X0 + 3 * PR_CW + 6, ye + 1) == C_INK);
    x = PR_X0 + 4 * PR_CW + 2;
    check("roll: RATCH 2 splits the bar in two, 1 px apart", gpx(x + 3, yg + 1) == C_INK && gpx(x + 4, yg + 1) != C_INK &&
          gpx(x + 5, yg + 1) == C_INK);
    for (xx = PR_X0 + PR_CW + 11; xx <= PR_X0 + PR_CW + 13; xx++)
        for (yy = ye; yy <= yd + 2; yy++)
            amb += gpx(xx, yy) == C_INK;
    check("roll: SLIDE a dotted line from D4's bar into E4", amb > 0);
    for (yy = PR_Y0; yy < PR_Y0 + PR_ROWS * PR_RH; yy++)
        rest += gpx(PR_X0 + 5 * PR_CW + 4, yy) == C_INK;
    check("roll: a rest is empty", rest == 0);
    for (yy = yc - 6; yy < yc + 10; yy++)
        for (xx = 1; xx < PR_KX; xx++)
            lab += gpx(xx, yy) == C_INK;
    check("roll: the C row named on the left (C4)", lab > 10);
}

static void test_roll_follow(void)
{
    track_t *t;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    t->step[0].on = t->step[1].on = 1;
    t->step[0].nt = 30;                              /* F#6, A#6 */
    t->step[1].nt = 34;
    seq_open("STEP");
    ui_frame();
    check("roll: the view follows the notes up (F#6, A#6 in view)", proll.lo <= 90 && proll.lo + PR_ROWS > 94);
    t->step[0].nt = -30;                             /* F#1, A#1 */
    t->step[1].nt = -26;
    ui_frame();
    check("roll: ... and down (F#1, A#1)", proll.lo <= 30 && proll.lo + PR_ROWS > 34);
    t->step[0].nt = -40;                             /* G#0 .. E7: wider than the view */
    t->step[1].nt = 40;
    ui_frame();
    check("roll: notes wider than the view, one each: the lowest window (G#0 on the bottom row), the other a mark on "
          "the top edge", proll.lo == 20 && gpx(PR_X0 + PR_CW + 4, PR_Y0) == C_INK);
}

/* fix round 1: the roll's view (the coordinator's ruling): the 24-row window holding the most of the bank's sounding
 * notes (a TIE counts its source's), the lowest of equals, kept while it holds as many as the best; step record
 * shifts it only as far as the cursor's sounding note needs (a rest: nothing); neither the playhead nor a KEYS key
 * moves it (a key outside: the strip's edge row lit) */
static void roll_notes(track_t *t, const int8_t *nt, uint32_t n)   /* steps 1..n ON at C4 + nt; 99: a TIE, 100: a rest */
{
    uint32_t i;
    memset(t->step, 0, sizeof t->step);
    for (i = 0; i < n; i++) {
        t->step[i].tie = nt[i] == 99;
        t->step[i].on = nt[i] < 99;
        t->step[i].nt = (int8_t)(nt[i] < 99 ? nt[i] : 0);
    }
}
static void roll_open(track_t *t)                    /* STEP on an ACID track, the view chosen afresh */
{
    proll.trk = 0;
    seq_open("STEP");
    ui.force = 1;
    ui_frame();
    (void)t;
}

static void test_roll_view_most(void)
{
    static const int8_t L[11] = {-24, 0, 2, 3, 5, 7, 10, -24, 28, 99, 100};   /* C2 x2, C4..A#4 (6), E6 + its TIE */
    track_t *t;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    roll_notes(t, L, 11);
    roll_open(t);
    check("roll view: a line wider than 24 rows: the window holding the most notes (C4 up: 6; C2 up 2, D4 up 5)",
          proll.lo == 60);
    t->step[11].on = t->step[12].on = 1;             /* C#6, C6: D4 up and F4 up now hold 7, C4 up 6 */
    t->step[11].nt = 25;
    t->step[12].nt = 24;
    ui_frame();
    check("roll view: ... notes added: the window moves to the lowest holding the most (D4 up)", proll.lo == 62);
    t->step[11].on = 0;                              /* C#6 gone: C4 up, D4 up, F4 up hold 6 each */
    t->step[11].nt = 0;
    ui_frame();
    check("roll view: ... one removed: C4 up holds as many as D4 up: the view stays (no jump)", proll.lo == 62);
    roll_open(t);
    check("roll view: ... opened afresh: the lowest of the equal windows (C4 up)", proll.lo == 60);
}

static void test_roll_view_low_note(void)
{
    static const int8_t L[8] = {12, 14, 15, 17, 19, -24, 15, 12};   /* a riff at C5..G5, one C2 under it */
    track_t *t;
    int32_t yy, edge = 0;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    roll_notes(t, L, 8);
    roll_open(t);
    for (yy = PR_Y0; yy < PR_Y0 + PR_ROWS * PR_RH; yy++)
        edge += gpx(PR_X0 + 5 * PR_CW + 4, yy) == C_INK;
    check("roll view: one low note under a high riff: the riff stays in view (C5 on the bottom row)", proll.lo == 72);
    check("roll view: ... the low note a 1 px mark on the bottom edge",
          edge == 1 && gpx(PR_X0 + 5 * PR_CW + 4, PR_Y0 + PR_ROWS * PR_RH - 1) == C_INK);
}

static void test_roll_view_srec(void)
{
    static const int8_t L[8] = {0, 2, 4, 5, 7, 36, 100, 7};   /* C4 D4 E4 F4 G4, C7, a rest, G4 */
    track_t *t;
    uint32_t lo0;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    roll_notes(t, L, 8);
    tap(B_SCL);
    roll_open(t);
    lo0 = proll.lo;
    tap(B_REC);
    ui_frame();
    check("roll view, step record: the cursor's note (C4) in the view: no move", ui.srec && ui.sr_cur == 0u &&
          proll.lo == lo0 && lo0 == 60u);
    turn(EN_K1, 4);
    ui_frame();
    check("roll view, step record: ... G4 still in it: no move", ui.sr_cur == 4u && proll.lo == lo0);
    turn(EN_K1, 1);
    ui_frame();
    check("roll view, step record: ... C7 out of it: the view moves only as far as C7 needs (C7 the top row)",
          ui.sr_cur == 5u && proll.lo == 96u - PR_ROWS + 1u);
    turn(EN_K1, 1);
    ui_frame();
    check("roll view, step record: ... a rest moves nothing", ui.sr_cur == 6u && proll.lo == 96u - PR_ROWS + 1u);
    turn(EN_K1, 1);
    ui_frame();
    check("roll view, step record: ... G4 out of it again: back to the best window, G4 in it", ui.sr_cur == 7u &&
          proll.lo == lo0);
    tap(B_REC);
    tap(B_SCL);
    ui_frame();
}

static void test_roll_view_key_out(void)
{
    track_t *t;
    uint32_t lo0;
    int32_t ybot = PR_Y0 + (PR_ROWS - 1) * PR_RH;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    acid_line(t);
    tap(B_SCL);
    roll_open(t);
    lo0 = proll.lo;
    keys(1u << 0);                                   /* F3: under the view (C4 up) */
    ui_frame();
    ui_frame();                                      /* (the key sounds in the frame's audio, the graph shows it next) */
    check("roll view: a KEYS key under the view: the view stays, the strip's bottom row lit",
          kz.key && proll.lo == lo0 && lo0 == 60u && gpx(PR_KX + 1, ybot) == C_INK && gpx(PR_KX + 1, PR_Y0) != C_INK);
    keys(0);
    ui_frame();
    ui_frame();
    check("roll view: ... let go: the bottom row back to its key", gpx(PR_KX + 1, ybot) != C_INK);
    tap(B_OCTUP);
    tap(B_OCTUP);
    keys(1u << 26);                                  /* G7 (two octaves up): over the view */
    ui_frame();
    ui_frame();
    check("roll view: a KEYS key over the view: the strip's top row lit, the view stays",
          kz.key && proll.lo == lo0 && gpx(PR_KX + 1, PR_Y0) == C_INK && gpx(PR_KX + 1, ybot) != C_INK);
    keys(0);
    tap(B_OCTDN);
    tap(B_OCTDN);
    tap(B_SCL);
    ui_frame();
}

static void test_roll_view_idle(void)
{
    ui_host_init();
    old_set_model(TSEL, DM_ACID);
    acid_line(TSEL);
    roll_open(TSEL);
    ui_frame();
    proll.lo = 3;                                    /* a mark: an idle frame does not choose the view again */
    ui_frame();
    ui_frame();
    check("roll view: chosen only when the graph changes, not every idle frame", proll.lo == 3);
    TSEL->step[5].on = 1;
    ui_frame();
    check("roll view: ... a step changed: chosen again", proll.lo == 60);
}

static void test_roll_label_slide_edges(void)
{
    static const int8_t L[16] = {-60, -58, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, -58};
    track_t *t;
    int32_t xx, yy, lit0 = 0, whole = 0, amb = 0, yc;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    roll_notes(t, L, 16);
    t->step[15].slide = 1;                           /* step 16 slides into step 1 (the wrap) */
    roll_open(t);
    yc = pr_row_y(0);
    for (yy = yc - 6; yy < H_GRAPH - G_OY; yy++)    /* the label: from its row's y - 6 down */
        for (xx = 1; xx < PR_KX; xx++)
            lit0 += gpx(xx, yy) == C_INK;
    cv_begin(240, 40, C_BG);                         /* the label whole, for reference */
    md_text(0, 0, &MD_F3, 2, "C-1", C_INK);
    for (yy = 0; yy < 40 * 240; yy++)
        whole += cv_px[yy] == swap16(C_INK);
    ui.force = 1;
    ui_frame();
    check("roll: C-1 (NOTE 0) on the bottom row, its label whole (from x 0, not clipped)", proll.lo == 0u &&
          PR_KX - 2 - md_text_w(&MD_F3, 2, "C-1") >= 2 && whole > 20 && lit0 == whole);
    for (yy = PR_Y0 - 2; yy < PR_Y0 + PR_ROWS * PR_RH + 2; yy++)
        for (xx = PR_X0 + 16 * PR_CW + 1; xx < 239; xx++)
            amb += gpx(xx, yy) == C_INK;
    check("roll: a slide out of the bank's last column stays inside the grid", amb == 0);
}

/* the footer reads the steps the roll and the grid show: a song's row while a song runs */
static void test_foot_note_song(void)
{
    static step_t row[NSTEP];
    const step_t *keep;
    char b[16];
    track_t *t;
    ui_host_init();
    t = TSEL;
    memset(t->step, 0, sizeof t->step);
    t->step[3].on = 1;                               /* the track's own step 4: C4 */
    memset(row, 0, sizeof row);
    row[3].on = 1;                                   /* the song row's step 4: F4 SLD */
    row[3].nt = 5;
    row[3].slide = 1;
    keep = chain.src[0].step[0];
    chain.src[0].step[0] = row;
    chain.slot = 0;
    chain.running = 1;
    ui.srec = 1;
    ui.sr_cur = 3;
    check("footer: in a song, the row's step (as the roll / grid show it): F4 SLD", foot_note(b) && str_eq(b, "F4 SLD"));
    ui.srec = 0;
    chain.running = 0;
    chain.src[0].step[0] = keep;
    check("footer: ... no song: the track's own (no note shown without step record or a held step)", !foot_note(b));
}

static void test_roll_cursor_playhead(void)
{
    int32_t x;
    ui_host_init();
    old_set_model(TSEL, DM_ACID);
    acid_line(TSEL);
    tap(B_SCL);
    seq_open("STEP");
    tap(B_REC);
    turn(EN_K1, 2);
    ui_frame();
    x = PR_X0 + 2 * PR_CW;
    check("roll: step record's cursor framed (step 3)", ui.srec && gpx(x + 5, PR_Y0 - 2) == C_INK &&
          gpx(x, 50) == C_INK && gpx(x + PR_CW, 50) == C_INK);
    tap(B_REC);
    transport_req = 1;
    seq_play_to(0, 4);
    ui_frame();
    x = PR_X0 + 4 * PR_CW;
    check("roll: the playhead a 1 px line through the playing step (5)", gpx(x + 6, PR_Y0 + 1) == C_INK &&
          gpx(x + 5, PR_Y0 + 1) != C_INK);
    transport_req = 2;
    ui_frame();
}

static uint32_t foot_lit(void)                       /* the breadcrumb's text where the note goes (x 120..199) */
{
    uint32_t n = 0, x, y;
    for (y = Y_CRUMB + 8u; y < Y_CRUMB + 18u; y++)
        for (x = 120u; x < 200u; x++)
            n += fb[y * 240u + x] == C_BG;
    return n;
}

/* the footer: the step held on the grid, or step record's cursor step: its note, + ACC SLD, or TIE; a drum's grid
 * shows step record's cursor framed */
static void test_foot_note(void)
{
    char b[16];
    uint32_t ms;
    track_t *t;
    ui_host_init();
    t = TSEL;                                        /* track 1, a drum */
    t->key_nt = 1;
    open_step_page();
    keys(1u << WHITE[1]);
    ui_frame();
    check("footer: a step held on the grid shows its note (a drum's new step: C4, not the last key note)",
          foot_note(b) && str_eq(b, "C4") && foot_lit() > 0u);
    keys(0);
    ui_frame();
    press(B_OCTUP);                                  /* OCT+ held + the key: its accent (item 1), held */
    ui_frame();
    keys(1u << WHITE[1]);
    for (ms = 0; ms < 100u; ms += 50u) {
        host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
        ui_frame();
    }
    check("footer: accented with OCT+, held: C4 ACC", foot_note(b) && str_eq(b, "C4 ACC"));
    keys(0);
    ui_frame();
    release_all();
    ui_frame();
    check("footer: nothing held: no note", !foot_note(b) && !b[0] && foot_lit() == 0u);
    t->step[1].nt = 1;                               /* C#4 (as KEYS record writes it) */
    t->step[2].tie = 1;
    t->step[2].nt = -11;
    t->step[3].on = 1;
    t->step[3].slide = 1;
    tap(B_SCL);
    tap(B_REC);
    turn(EN_K1, 2);
    ui_frame();
    check("footer: step record's cursor step, a TIE: the note it holds (step 2's C#4, not its own unused C#3)",
          foot_note(b) && str_eq(b, "C#4 TIE"));
    check("drum grid: step record's cursor framed (step 3)", gpx(4 + 2 * 14 - 2, 40) == C_INK);
    turn(EN_K1, 1);
    ui_frame();
    check("footer: ... a slide: C4 SLD", foot_note(b) && str_eq(b, "C4 SLD"));
    tap(B_REC);
}

static void test_grid_transposed_mark(void)
{
    ui_host_init();
    memset(TSEL->step, 0, sizeof TSEL->step);
    TSEL->step[1].on = 1;
    TSEL->step[1].nt = 3;
    TSEL->step[2].on = 1;
    open_step_page();
    ui.force = 1;
    ui_frame();
    check("drum grid: a transposed step (NOTE != C4) a small mark under its bar; a C4 step none",
          gpx(4 + 14 + 4, 30 + 42) == C_INK && gpx(4 + 2 * 14 + 4, 30 + 42) != C_INK);
}

static int32_t acid_peak_x(void)                    /* the leftmost lit x (100..235) above the 0 dB line; -1 none */
{
    uint32_t x, y;
    for (x = 100u; x < 236u; x++)
        for (y = Y_GRAPH + G_OY + 2u; y < Y_GRAPH + G_OY + ACID_GY; y++)
            if (fb[y * 240u + x] == C_INK)
                return (int32_t)x;
    return -1;
}
static uint32_t acid_hi(uint32_t y0, uint32_t y1)   /* the curve's pixels at x 110..235 (past the first 10 px) of
                                                     * graph rows y0..y1: ink with an ink neighbour (dotted lines'
                                                     * pixels have none) */
{
    uint32_t n = 0, x, y;
    for (y = Y_GRAPH + G_OY + y0; y < Y_GRAPH + G_OY + y1; y++)
        for (x = 110u; x < 236u; x++)
            n += fb[y * 240u + x] == C_INK && (fb[y * 240u + x - 1u] == C_INK || fb[y * 240u + x + 1u] == C_INK ||
                                              fb[(y - 1u) * 240u + x] == C_INK || fb[(y + 1u) * 240u + x] == C_INK);
    return n;
}
static void test_acid_edit_graph(void)
{
    uint32_t k, flat, peak, shortd, longd;
    int32_t x40, x100;
    ui_host_init();
    old_set_model(TSEL, DM_ACID);
    for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_SND); k++)
        tap(B_EDIT);
    TSEL->p[P_E0 + 4] = 0;                           /* ENVMD 0: no reach line */
    TSEL->p[P_E0 + 3] = 0;
    ui.force = 1;
    ui_frame();
    flat = fm_graph_area(G_OY + 2u, G_OY + ACID_GY);
    TSEL->p[P_E0 + 3] = 127;
    ui_frame();
    peak = fm_graph_area(G_OY + 2u, G_OY + ACID_GY);
    check("ACID graph: RESO 0 nothing over 0 dB, RESO 127 a peak at the cutoff", flat == 0u && peak > 0u);
    TSEL->p[P_E0 + 2] = 40;
    ui_frame();
    x40 = acid_peak_x();
    TSEL->p[P_E0 + 2] = 100;
    ui_frame();
    x100 = acid_peak_x();
    check("ACID graph: the peak follows CUTOF (40 -> 100: right)", x40 > 0 && x100 > x40 + 40);
    TSEL->p[P_E0 + 1] = 0;
    ui_frame();
    shortd = acid_hi(ACID_EY, ACID_EY + 18u);
    TSEL->p[P_E0 + 1] = 127;
    ui_frame();
    longd = acid_hi(ACID_EY, ACID_EY + 18u);
    check("ACID graph: the filter envelope falls over DECAY (30 ms: down within 10 px; 3 s: still in the upper half)",
          shortd == 0u && longd > 20u);
}

static void test_roll_frame_cost(void)
{
    uint64_t i0 = 0, worst = 0, c;
    uint32_t i;
    ui_host_init();
    old_set_model(TSEL, DM_ACID);
    acid_line(TSEL);
    seq_open("STEP");
    transport_req = 1;
    for (i = 0; i < 400u; i++) {
        ui.force = i % 50u == 0u;
        i0 = instr_now();
        ui_input();
        ui_leds();
        ui_draw();
        c = instr_now() - i0;
        worst = c > worst ? c : worst;
        render_mix(0, 0, CTL);
    }
    printf("     ui: heaviest frame %llu host instructions (STEP, the ACID piano roll, playing)\n",
           (unsigned long long)worst);
    check("ui frame cost, the piano roll: under 4,000,000 host instructions (as every page)", !i0 || worst < 4000000u);
    transport_req = 2;
    ui_frame();
}

/* ---- ACID plan B: the final review's fixes */
static uint32_t pr_bar_lit(int32_t x, int32_t y)    /* a plain bar's pixels lit (of its 9 x 2: graph_roll's pr_bar) */
{
    uint32_t n = 0;
    int32_t xx, yy;
    for (yy = y + 1; yy < y + PR_RH - 1; yy++)
        for (xx = x; xx < x + 9; xx++)
            n += gpx(xx, yy) == C_INK;
    return n;
}

/* final review 1: an ACID track's STEP page (the roll), KEYS off: a held step's line (STEP n  PROB  RATCH n) in the band
 * above the roll (screen y 74..97), gone when let go; a PROB bar hatched */
static void test_roll_held_prob(void)
{
    track_t *t = TSEL;
    uint32_t k, band0, band1, band2, lit0, lit1;
    int32_t y;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    for (k = 0; k < 16u; k += 2u) {
        t->step[k].on = 1;
        t->step[k].nt = (int8_t)(k % 5u);
    }
    seq_open("STEP");
    ui.force = 1;
    ui_frame();
    y = pr_row_y(62);                                /* step 3: D4 */
    band0 = fb_lit(Y_GRAPH, Y_GRAPH + G_OY);
    lit0 = pr_bar_lit(PR_X0 + 2 * PR_CW + 2, y);
    keys(1u << STEP_KEY[2]);                         /* step 3 held ... */
    ui_frame();
    band1 = fb_lit(Y_GRAPH, Y_GRAPH + G_OY);
    for (k = 0; k < 3u; k++)                         /* ... KNOB 1: its PROB */
        srec_turn(0, -1);
    lit1 = pr_bar_lit(PR_X0 + 2 * PR_CW + 2, y);
    check("roll, KEYS off: KNOB 1 with step 3 held sets its PROB", t->step[2].cond != 0u);
    check("roll: nothing held: the band above the roll empty", band0 == 0u);
    check("roll: a step held: its line (STEP 3  100%  RATCH 1) in the band above the roll", band1 > 40u);
    check("roll: a plain bar whole (9 x 2 px)", lit0 == 18u);
    check("roll: a PROB bar hatched (some of its pixels dark)", lit1 > 4u && lit1 < 18u);
    keys(0);
    ui_frame();
    band2 = fb_lit(Y_GRAPH, Y_GRAPH + G_OY);
    check("roll: the step let go: the band cleared", band2 == 0u);
    t->step[2].cond = 0;
}

/* final review 2: KEYS off, armed: a hit recorded onto a TIE step plays (on, no TIE: acid_step reads TIE first) */
static void test_rec_hit_on_tie(void)
{
    track_t *t = &trk[0];
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    t->step[3].tie = 1;
    t->step[3].nt = 7;
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 3);
    keys(1u << 0);                                   /* F3: track 1's key (HOME, KEYS off) */
    ui_frame();
    keys(0);
    ui_frame();
    check("live record onto a TIE step: ON, the TIE cleared", t->step[3].on && !t->step[3].tie);
    transport_req = 2;
    ui_frame();
}

/* final review 3: LEN cut under a recording key's phrase start (8 -> 4, the key from step 7): the recording ends, the
 * loop's notes stay (no TIEs round and round) */
static void test_keys_live_rec_len_cut(void)
{
    track_t *t = &trk[0];
    uint32_t k, ok = 1;
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    t->p[P_SLEN] = 8;
    for (k = 0; k < 4u; k++) {
        t->step[k].on = 1;
        t->step[k].nt = (int8_t)k;
    }
    tap(B_SCL);
    song.rec = 1u;
    transport_req = 1;
    seq_play_to(0, 6);
    keys(1u << 7);                                   /* C4 on step 7, held */
    ui_frame();
    t->p[P_SLEN] = 4;
    for (k = 0; k < 1200u; k++)                      /* held over the 4 steps twice */
        ui_frame();
    keys(0);
    ui_frame();
    for (k = 0; k < 4u; k++)
        ok &= t->step[k].on && !t->step[k].tie && t->step[k].nt == (int8_t)k;
    check("KEYS live record: LEN cut under the held note's start: the loop's notes stay", ok);
    check("KEYS live record: ... the key's own note on step 7", t->step[6].on && t->step[6].nt == 0);
    t->p[P_SLEN] = 16;
    transport_req = 2;
    ui_frame();
}

/* final review 4: step record's EDIT rest clears the step's PROB RATCH ACC SLIDE (a later key: a plain note) */
static void test_srec_rest_clears(void)
{
    track_t *t;
    srec_open();
    t = TSEL;
    t->step[0].on = 1;
    t->step[0].cond = 5;
    t->step[0].rat = 2;
    t->step[0].acc = 1;
    t->step[0].slide = 1;
    tap(B_EDIT);
    check("step record: EDIT's rest clears PROB, RATCH, ACC, SLIDE", !t->step[0].on && !t->step[0].tie &&
          !t->step[0].cond && !t->step[0].rat && !t->step[0].acc && !t->step[0].slide && ui.sr_cur == 1u);
    srec_turn(0, -1);
    keys(1u << 7);
    ui_frame();
    keys(0);
    ui_frame();
    check("step record: a key on that rest: a plain note (no old PROB / RATCH)", t->step[0].on && !t->step[0].cond &&
          !t->step[0].rat);
    tap(B_REC);
}

/* final review 6: KEYS, STEP, a drum track: "REC: STEP RECORD" only where a REC tap starts it (stopped); playing (a REC
 * tap arms the track): KEYS alone */
static uint32_t hint_lit(int32_t x0)               /* the STEP grid's hint row (graph y 84..95) lit from x0 */
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = 84; y < 96; y++)
        for (x = x0; x < 239; x++)                   /* (x 239: the frame's edge) */
            n += gpx(x, y) == C_INK;
    return n;
}
static void test_keys_hint_playing(void)
{
    uint32_t stopped, playing;
    int32_t x0;
    ui_host_init();
    tap(B_SCL);
    seq_open("STEP");
    ui.force = 1;
    ui_frame();
    x0 = md_text_w(&MD_F3, 2, "KEYS") + 6;
    stopped = hint_lit(x0);
    transport_req = 1;
    ui_frame();
    ui_frame();
    playing = hint_lit(x0);
    check("KEYS, STEP, stopped: the hint names REC: STEP RECORD", stopped > 40u);
    check("KEYS, STEP, playing: no REC: STEP RECORD (a REC tap arms)", playing == 0u);
    transport_req = 2;
    ui_frame();
    ui_frame();
}

/* final review 7: step record, two keys in one frame: one note, the highest (as the ISR sounds), no SLIDE */
static void test_srec_chord(void)
{
    track_t *t;
    srec_open();
    t = TSEL;
    keys(1u << 7 | 1u << 11);                        /* C4 + E4 at once */
    ui_frame();
    check("step record: a chord in one frame writes one step: the highest key (E4), no SLIDE",
          t->step[0].on && t->step[0].nt == 4 && !t->step[0].slide && !t->step[1].on && ui.sr_cur == 0u);
    keys(1u << 7);                                   /* E4 (the cursor's) let go */
    ui_frame();
    check("step record: ... its release moves the cursor on", ui.sr_cur == 1u);
    keys(0);
    ui_frame();
    check("step record: ... the other key's release moves nothing", ui.sr_cur == 1u && !t->step[1].on);
    tap(B_REC);
}

/* final review 8 (plan rulings: the sequencer and a key on one ACID voice): rests leave a key's gate open; an ON step
 * takes the voice over (its gate counts); the sequencer's gate closes while the key is still held; the release then
 * does nothing */
static void test_keys_seq_takeover(void)
{
    track_t *t = &trk[0];
    uint32_t age;
    ui_host_init();
    old_set_model(t, DM_ACID);
    memset(t->step, 0, sizeof t->step);
    t->step[4].on = 1;                               /* one ON step (5: G4), rests elsewhere */
    t->step[4].nt = 7;
    tap(B_SCL);
    transport_req = 1;
    seq_play_to(0, 1);
    keys(1u << 7);                                   /* C4 held */
    ui_frame();
    seq_play_to(0, 3);
    check("KEYS + sequencer: rests leave the key's gate open", (t->v[0].x[2] & 1) && t->gate_n == 0u);
    age = t->v[0].age;
    seq_play_to(0, 4);
    ui_frame();
    check("KEYS + sequencer: an ON step takes the voice over: restarted (no glide from the key's note), G4, its gate counting",
          t->v[0].age != age && t->v[0].x[1] == 67 * 16 && t->gate_n != 0u);
    seq_play_to(0, 6);
    check("KEYS + sequencer: the step's gate closes, the key still held", !(t->v[0].x[2] & 1));
    keys(0);
    ui_frame();
    check("KEYS + sequencer: the key let go then: nothing (gate closed, nothing counting)",
          !(t->v[0].x[2] & 1) && t->gate_n == 0u);
    transport_req = 2;
    ui_frame();
}

/* the screens, for the user's eye: build/ui_shots/acid/ */
static void test_acid_screens(void)
{
    track_t *t;
    uint32_t k;
    ui_host_init();
    t = TSEL;
    old_set_model(t, DM_ACID);
    acid_line(t);
    t->step[6].on = 1;
    t->step[6].nt = -5;
    t->step[7].on = 1;
    t->step[7].nt = 7;
    t->step[7].acc = 1;
    t->step[7].slide = 1;
    t->step[8].on = 1;
    t->step[8].nt = 12;
    t->step[9].tie = 1;
    t->step[10].tie = 1;
    t->step[12].on = 1;
    t->step[12].nt = -12;
    t->step[12].rat = 2;
    t->step[14].on = 1;
    t->step[14].nt = 3;
    t->step[14].acc = 1;
    tap(B_SCL);
    snap_page("acid/01_home_keys_f3");
    tap(B_OCTUP);
    snap_page("acid/02_home_keys_f4");
    tap(B_OCTDN);
    seq_open("STEP");
    snap_page("acid/03_roll");
    keys(1u << 11);                                  /* E4 held: its row lit on the keyboard strip */
    snap_page("acid/04_roll_key_held");
    keys(0);
    ui_frame();
    tap(B_REC);                                      /* step record, the cursor on step 5 */
    turn(EN_K1, 4);
    snap_page("acid/05_step_rec_05");
    tap(B_REC);
    transport_req = 1;
    seq_play_to(0, 7);
    snap_page("acid/06_roll_play_step08");
    seq_play_to(0, 12);
    snap_page("acid/07_roll_play_step13");
    transport_req = 2;
    ui_frame();
    tap(B_SCL);                                      /* KEYS off */
    seq_select(1);                                   /* track 2, a drum: its transposed steps marked */
    for (k = 0; k < 16u; k++) {
        trk[1].step[k].on = k % 4u == 0u || k == 6u || k == 14u;
        trk[1].step[k].nt = (int8_t)(k == 4u ? 5 : k == 6u ? -7 : k == 14u ? 12 : 0);
    }
    snap_page("acid/08_drum_grid_transposed");
    trk[1].key_nt = 3;
    press(B_OCTUP);                                  /* step 2 accented (OCT+ held) and held: the footer's note */
    ui_frame();
    keys(1u << WHITE[1]);
    for (k = 0; k < 10u; k++) {
        host_ticks += 50u * 1000u * FM1_TICKS_PER_US;
        ui_frame();
    }
    snap_page("acid/09_footer_held_step");
    keys(0);
    ui_frame();
    release_all();
    ui_frame();
    seq_select(0);
    for (k = 0; k < NPAGES && (ui.home || cur_page()->fam != FAM_SND); k++)
        tap(B_EDIT);
    snap_page("acid/10_edit_acid");
    t->p[P_E0 + 3] = 127;
    t->p[P_E0 + 2] = 70;
    snap_page("acid/11_edit_acid_reso127");
    t->p[P_E0 + 1] = 127;
    t->p[P_E0 + 7] = 127;
    snap_page("acid/12_edit_acid_decay_gate");
    seq_select(1);                                   /* track 2, a drum, on STEP: KEYS on, then step record */
    seq_open("STEP");
    tap(B_SCL);
    snap_page("acid/13_drum_grid_keys");
    tap(B_REC);                                      /* the cursor on step 5 (F4, transposed): the footer's note */
    turn(EN_K1, 4);
    snap_page("acid/14_drum_grid_step_rec");
    tap(B_REC);
    seq_select(0);                                   /* the ACID roll: a KEYS key over the view */
    seq_open("STEP");
    tap(B_OCTUP);
    tap(B_OCTUP);
    keys(1u << 26);                                  /* G7 */
    snap_page("acid/15_roll_key_over_view");
    keys(0);
    tap(B_OCTDN);
    tap(B_OCTDN);
    tap(B_SCL);
    ui_frame();
    t->step[8].cond = 3;                             /* KEYS off: C5 (tied) at PROB 10 %; step 5 (G4, RATCH 2) held, */
    keys(1u << STEP_KEY[4]);                         /* KNOB 1: its PROB (85 %) */
    ui_frame();
    for (k = 0; k < 3u; k++)
        srec_turn(0, -1);
    snap_page("acid/16_roll_held_prob");
    keys(0);
    ui_frame();
    t->step[8].cond = t->step[4].cond = 0;
    printf("     acid: screens in build/ui_shots/acid\n");
}

/* ---- CPU (spec 2026-10-11 §10): PRIO on the SOUND list after CHOKE; the CPU icon after an overload shed */

static void test_prio_sound_list(void)
{
    uint8_t ids[16];
    uint32_t m, n, ok = 1;
    ui_host_init();
    for (m = 0; m < NMODELS; m++) {
        old_set_model(TSEL, m);
        n = snd_list(TSEL, ids);
        ok &= n <= 16u && ids[n - 1u] == P_PRIO && ids[n - 2u] == P_CHOKE;
    }
    check("PRIO: every model's SOUND list ends CHOKE, PRIO", ok);
    old_set_model(TSEL, DM_CYMB);
    n = snd_list(TSEL, ids);
    ui.home = 0;                                     /* the SOUND page with PRIO */
    ui.page = (uint8_t)(page_first(FAM_SND) + (n - 1u) / 4u);
    ui.force = 1;
    ui_frame();
    turn(EN_K1 + (n - 1u) % 4u, 1);
    ui_frame();
    check("PRIO: its knob sets LOW (the selected track), HIGH back", TSEL->p[P_PRIO] == 1 && trk[1].p[P_PRIO] == 0);
    snap_page("fixes/sound_prio");
    turn(EN_K1 + (n - 1u) % 4u, -1);
    ui_frame();
    check("PRIO: ... and HIGH again", TSEL->p[P_PRIO] == 0);
}

static uint32_t head_alert(int32_t x0, int32_t x1)   /* the CPU chip's pixels (BG on the ink bar), rows 3..11 */
{
    uint32_t x, y, n = 0;
    for (y = 3; y < 12u; y++)
        for (x = (uint32_t)x0; x <= (uint32_t)x1; x++)
            n += fb[y * 240u + x] == C_BG;
    return n;
}

/* spec 2026-10-19-md-ui §5: the CPU chip right of T<n> (x 166..190), steady for 2 s after a shed */
static void test_cpu_icon(void)
{
    uint32_t a, b, ball, c, d;
    ui_host_init();
    fm1_ms = 5000;
    ui_frame();
    a = head_alert(166, 215);
    song.sheds++;                                    /* audio.c shed a voice */
    ui_frame();
    b = head_alert(166, 190);
    ball = head_alert(166, 215);
    snap_page("fixes/cpu_icon");
    fm1_ms = 6999;
    ui_frame();
    c = head_alert(166, 190);
    fm1_ms = 7000;
    ui_frame();
    d = head_alert(166, 215);
    check("CPU icon: none before a shed; the chip right of T<n> (x 166..190) after one", a == 0u && b > 10u && b == ball);
    check("CPU icon: shown for 2 s, then gone", c == b && d == 0u);
    fm1_ms = 8000;
    song.sheds += 3u;                                /* more sheds: shown again, 2 s from the last */
    ui_frame();
    fm1_ms = 9500;
    song.sheds++;
    ui_frame();
    fm1_ms = 11000;
    ui_frame();
    check("CPU icon: each shed shows it 2 s from then", head_alert(166, 190) == b);
    fm1_ms = 11500;
    ui_frame();
    check("CPU icon: ... and it goes", head_alert(166, 215) == 0u);
    srec_open();                                     /* STEP REC 01 before T1: the chip after T1 */
    fm1_ms = 20000;
    ui_frame();
    song.sheds++;
    ui_frame();
    check("CPU icon: in step record it keeps clear of STEP REC nn and T<n>", ui.srec && head_alert(166, 190) > 10u &&
                                                                           head_alert(137, 149) == 0u);
    snap_page("fixes/cpu_icon_step_rec");
    tap(B_REC);
    fm1_ms = 0;
    ui_frame();
}

/* ---- PRESETS (plan 2026-10-19-presets Task 3): the encoder's popup, the list on a long EDIT, the preset's name and
 * the edited mark; the screens in build/ui_shots/presets/ */
static int voice_is(const track_t *t, uint32_t id)    /* t's MODEL and 8 SOUND knobs = preset id's */
{
    preset_t p;
    uint32_t i;
    if (!preset_get(id, &p) || (uint32_t)t->p[P_MODEL] != p.model || t->model != p.model)
        return 0;
    for (i = 0; i < 8u; i++)
        if (t->p[P_E0 + i] != p.k[i])
            return 0;
    return 1;
}
static void pre_turn(int32_t d)                       /* PRESETS, d detents, one frame */
{
    turn(EN_PRESET, d);
    ui_frame();
}
static void pre_wait(uint32_t ms)                     /* ms later, one frame */
{
    host_ticks += ms * 1000u * FM1_TICKS_PER_US;
    ui_frame();
}
static uint32_t lit_box(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)   /* ink pixels in the box (the frame's
                                                                                  * edge columns left out) */
{
    uint32_t n = 0, x, y;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += fb[y * 240u + x] == C_INK && x != 0u && x != 239u;
    return n;
}
static void users_clear(void)                          /* no user preset, nothing left to write */
{
    memset(&upr, 0, sizeof upr);
    upr_pending = 0;
}

static void test_popup_shows_and_loads(void)
{
    uint32_t id0, id1, m;
    ui_host_init();
    users_clear();
    ui_frame();
    id0 = TSEL->pre;
    id1 = preset_step(TSEL, id0, 1);
    check("PRESETS: the power-on track's voice is its first preset", id0 == preset_first_of(DM_K909) && !TSEL->pre_ed);
    pre_turn(1);
    check("PRESETS one detent right: the next preset loaded at once (MODEL and the 8 knobs), not edited; the popup up",
          TSEL->pre == id1 && voice_is(TSEL, id1) && !TSEL->pre_ed && pp_on());
    check("PRESETS popup: the preset in the middle, the two loadable ones before and after around it",
          ui.pr.shown[2] == id1 && ui.pr.shown[1] == id0 && ui.pr.shown[0] == preset_step(TSEL, id0, -1) &&
              ui.pr.shown[3] == preset_step(TSEL, id1, 1) && ui.pr.shown[4] == preset_step(TSEL, ui.pr.shown[3], 1));
    check("PRESETS popup: drawn over the graph (its preset row lit)", lit_box(8, Y_GRAPH + 56, 140, Y_GRAPH + 86) > 0);
    snap_page("presets/01_popup");
    pre_wait(1000);
    check("PRESETS popup: still up 1 s after the detent", pp_on());
    pre_wait(600);
    check("PRESETS popup: gone ~1.5 s after the last detent (the graph back)", !pp_on());
    snap_page("presets/02_popup_closed");
    pre_turn(-1);
    check("PRESETS one detent left: the one before (the first again)", TSEL->pre == id0 && voice_is(TSEL, id0));
    TSEL->pre = (uint16_t)PRE_NONE;                    /* (a converted project: no preset) */
    m = (uint32_t)TSEL->p[P_MODEL];
    pre_turn(-1);
    check("PRESETS with no preset: the first detent (either way) loads the voice's first preset",
          TSEL->pre == preset_first_of(m) && voice_is(TSEL, preset_first_of(m)));
    while (TSEL->pre != PRE_BANK_START[PB_MODERN] - 1u)    /* the MACHINEDRUM bank's last (fix 8: MODERN next) */
        pre_turn(TSEL->pre < PRE_BANK_START[PB_MODERN] ? 1 : -1);
    pre_turn(1);
    check("PRESETS across banks: MACHINEDRUM's last -> MODERN's first", TSEL->pre == PRE_BANK_START[PB_MODERN] &&
                                                                          preset_bank_of(ui.pr.shown[1]) == PB_MD);
    snap_page("presets/03_popup_bank_edge");
    pre_wait(1600);
    open_tracks();
    pre_turn(1);
    check("PRESETS on TRACKS: loads, the popup over the rows, TRACKS stays", pp_on() && mix_mode() &&
                                                                         TSEL->pre == PRE_BANK_START[PB_MODERN] + 1u);
    snap_page("presets/04_popup_tracks");
    pre_wait(1600);
    check("PRESETS on TRACKS: the rows back", !pp_on() && mix_mode());
}

/* Review Focus 1: turning PRESETS fast while a dense pattern plays: every detent's preset loads, the output stays
 * bounded, no voice is left stuck */
static void test_popup_fast_while_playing(void)
{
    static int32_t l[2205], r[2205];                    /* 50 ms: 40 detents in 2 s */
    uint32_t k, i, ok = 1, stuck = 0;
    int32_t peak = 0;
    ui_host_init();
    users_clear();
    kit_session();
    trk[6].p[P_MUTE] = 0;
    song.rec = 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < 16u; i++)
            trk[k].step[i].on = 1;                      /* every track on every step */
    transport_req = 1;
    ui_frame();
    for (k = 0; k < 40u; k++) {
        uint32_t want = preset_step(TSEL, TSEL->pre, 1);
        pre_turn(1);
        ok &= TSEL->pre == want && voice_is(TSEL, want) && pp_on();
        render_mix(l, r, 2205);
        for (i = 0; i < 2205u; i++) {
            int32_t a = l[i] < 0 ? -l[i] : l[i], b = r[i] < 0 ? -r[i] : r[i];
            peak = a > peak ? a : peak;
            peak = b > peak ? b : peak;
        }
    }
    check("PRESETS: 40 detents in 2 s while 8 tracks play every step: each detent's preset loaded at once",
          ok && song.playing);
    check("PRESETS fast while playing: the output sounds and stays inside 16 bits", peak > 1000 && peak <= 32767);
    transport_req = 2;
    ui_frame();
    for (i = 0; i < 160u; i++)                          /* 8 s */
        render_mix(0, 0, 2205);
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NDV; i++)
            stuck += trk[k].v[i].active;
    check("PRESETS fast while playing: stopped, every voice ends (none stuck)", !song.playing && !stuck);
}

/* Review Focus 2: ACID on 2 tracks: the popup skips ACID's presets, the list refuses them; never a third ACID track */
static void test_popup_skips_capped_acid(void)
{
    uint32_t a = preset_first_of(DM_ACID), z = a, k, n;
    while (z < PRE_NFACT && PRE_FACTORY[z].model == DM_ACID)
        z++;                                            /* z: the preset after ACID's (a .. z - 1) */
    ui_host_init();
    users_clear();
    drum_set_model(&trk[0], DM_ACID);
    drum_set_model(&trk[1], DM_ACID);
    song.sel = 2;
    preset_load(&trk[2], a - 1u);                       /* the preset before ACID's */
    ui_frame();
    pre_turn(1);
    for (k = 0, n = 0; k < NTRK; k++)
        n += trk[k].p[P_MODEL] == DM_ACID;
    check("PRESETS with ACID on 2 tracks: the detent past ACID's presets to the one after them",
          z > a + 1u && trk[2].pre == z && trk[2].p[P_MODEL] != DM_ACID && n == 2u);
    for (k = 0, n = 0; k < 5u; k++)
        n += ui.pr.shown[k] >= a && ui.pr.shown[k] < z;
    check("PRESETS with ACID on 2 tracks: the popup leaves them out", ui.pr.shown[1] == a - 1u && n == 0u);
    snap_page("presets/05_popup_acid_capped");
    pre_turn(-1);
    check("PRESETS with ACID on 2 tracks: back past it too", trk[2].pre == a - 1u);
    pre_wait(1600);
    preset_list_open();
    pre_turn(1);                                        /* the cursor onto ACID's first: no audition */
    check("list with ACID on 2 tracks: the cursor on ACID's preset auditions nothing (ACID: 2 TRACKS MAX)",
          ui.pr.cur == a && trk[2].pre == a - 1u && trk[2].p[P_MODEL] != DM_ACID &&
              str_eq(ui.msg, "ACID: 2 TRACKS MAX"));
    ui.msg[0] = 0;
    tap(B_OCTUP);
    check("list with ACID on 2 tracks: LOAD on ACID's preset refused (ACID: 2 TRACKS MAX), the voice kept, the list too",
          trk[2].pre == a - 1u && trk[2].p[P_MODEL] != DM_ACID && str_eq(ui.msg, "ACID: 2 TRACKS MAX") && pl_on());
    drum_set_model(&trk[1], DM_K808);                   /* a place frees */
    tap(B_OCTUP);
    check("list: once a place frees, LOAD gives the 3rd track ACID (the list closed)",
          trk[2].pre == a && trk[2].p[P_MODEL] == DM_ACID && !pl_on());
}

/* the list: SAVE into the first free user slot or the one under the cursor (a used one: SAVE twice), DELETE (OCT-
 * twice) on a used user slot, LOAD (OCT+), the banks (KNOB 2), the cursor (PRESETS, KNOB 1); a SAVE while playing is
 * in the list at once and in flash at STOP */
static void test_list_save_delete_overwrite(void)
{
    uint32_t u0 = PRE_NFACT, kb = PRE_BANK_START[PB_CLASSIC];   /* (fix 8: CLASSIC the last factory bank) */
    char nb[9];
    ui_host_init();
    users_clear();
    ui_frame();
    TSEL->p[P_E1] = 10;                                 /* an edited DECAY */
    preset_touch(TSEL);
    hold(B_EDIT);
    check("EDIT held: the PRESETS list, its cursor on the track's preset", pl_on() && ui.pr.cur == TSEL->pre);
    snap_page("presets/06_list");
    tap(B_SAVE);
    check("list SAVE on a factory preset: the voice into the first free user slot, now the track's (not edited)",
          upr.s[0].used == 1u && upr.s[0].model == DM_K909 && upr.s[0].k[1] == 10 && TSEL->pre == u0 &&
              !TSEL->pre_ed && ui.pr.cur == u0 && pl_on());
    preset_name_of(u0, nb);
    check("list SAVE: auto-named (K909 01), said in the title", str_eq(nb, "K909 01") && str_eq(ui.msg, "SAVED K909 01"));
    snap_page("presets/07_list_saved");
    TSEL->p[P_E1] = 20;
    tap(B_SAVE);
    check("list SAVE on a used user slot: armed only (SAVE AGAIN), the slot as it was", upr.s[0].k[1] == 10 && ui.pr.arm == 1u);
    snap_page("presets/08_list_save_over_armed");
    tap(B_SAVE);
    check("list SAVE again: saved over it", upr.s[0].k[1] == 20 && !ui.pr.arm);
    pre_turn(3);
    check("PRESETS in the list: the cursor, one per detent (an empty slot: nothing auditioned)",
          ui.pr.cur == u0 + 3u && TSEL->pre == u0);
    tap(B_SAVE);
    check("list SAVE on an empty user slot: saved there at once", upr.s[3].used == 1u && TSEL->pre == u0 + 3u && !ui.pr.arm);
    tap(B_OCTDN);
    check("list OCT- on a used user slot: armed only (DELETE AGAIN)", upr.s[3].used == 1u && ui.pr.arm == 2u);
    snap_page("presets/09_list_delete_armed");
    tap(B_OCTDN);
    preset_name_of(TSEL->pre, nb);
    check("list OCT- again: deleted; the track keeps its knobs and its reference (shown as -)",
          upr.s[3].used != 1u && TSEL->pre == u0 + 3u && TSEL->p[P_E1] == 20 && str_eq(nb, "-"));
    turn(EN_K2, -1);
    ui_frame();
    check("list KNOB 2 left: the bank before, on its first (CLASSIC)", ui.pr.cur == kb);
    tap(B_SAVE);
    check("list SAVE off the user bank: the first free slot (2: 1 used, 4 deleted)", upr.s[1].used == 1u && TSEL->pre == u0 + 1u);
    turn(EN_K2, -1);
    ui_frame();
    tap(B_OCTUP);
    check("list OCT+ (LOAD): the preset under the cursor on the track, the list closed",
          TSEL->pre == kb && voice_is(TSEL, kb) && !pl_on());
    hold(B_EDIT);
    check("(the list again, on it)", pl_on() && ui.pr.cur == kb);
    tap(B_OCTDN);
    check("list OCT- on a factory preset: nothing deleted", !ui.pr.arm && TSEL->pre == kb);
    host_ticks += 300u * 1000u * FM1_TICKS_PER_US;      /* (a new turn: no acceleration) */
    turn(EN_K1, 1);
    ui_frame();
    check("list KNOB 1: the cursor", ui.pr.cur == kb + 1u);
    transport_req = 1;
    ui_frame();
    ui_frame();
    tap(B_SAVE);
    check("list SAVE while playing: in the list at once (slot 3), flash at the next STOP; the sound saved the track's, "
          "not the auditioned one",
          song.playing && upr.s[2].used == 1u && TSEL->pre == u0 + 2u && upr_pending &&
              upr.s[2].model == PRE_FACTORY[kb].model && upr.s[2].k[0] == PRE_FACTORY[kb].k[0] && voice_is(TSEL, u0 + 2u));
    snap_page("presets/10_list_saved_playing");
    transport_req = 2;
    ui_frame();
    ui_frame();
    check("list: STOP writes it (nothing left pending)", !song.playing && !upr_pending && pl_on());
    tap(B_EDIT);
    check("list EDIT: closed, back where the hold began (HOME)", !pl_on() && ui.home);
    hold(B_EDIT);
    tap(B_HOME);
    check("list HOME: closed", !pl_on());
}

/* EDIT: a tap as always (SOUND, its next page), held GLO_HOLD_MS the list over the page the press left; step record's
 * EDIT a rest, held too */
static void test_long_edit_opens_list(void)
{
    uint32_t k, i, cur;
    ui_host_init();
    users_clear();
    tap(B_EDIT);
    check("EDIT tap: SOUND at once, no list", !pl_on() && !ui.home && cur_page()->fam == FAM_SND);
    k = ui.page;
    press(B_EDIT);
    for (i = 0; i < 300u; i++)                          /* 0.3 s */
        ui_input();
    release_all();
    ui_frame();
    check("EDIT held 0.3 s: a tap (the next SOUND page), no list", !pl_on() && ui.page == k + 1u);
    k = ui.page;
    hold(B_EDIT);
    check("EDIT held 0.8 s: the list, the page under it the one before the press", pl_on() && ui.page == k);
    tap(B_EDIT);
    check("EDIT in the list: back to that page", !pl_on() && ui.page == k && cur_page()->fam == FAM_SND && !ui.home);
    tap(B_SCL);                                         /* KEYS */
    seq_open("STEP");
    tap(B_REC);
    cur = ui.sr_cur;
    check("step record on", ui.srec);
    hold(B_EDIT);
    check("step record: EDIT held = one rest, the cursor on; no list", ui.srec && !pl_on() && ui.sr_cur == cur + 1u);
    tap(B_EDIT);
    check("step record: an EDIT tap still a rest", ui.srec && !pl_on() && ui.sr_cur == cur + 2u);
}

/* review fix 5: MIDI LEARN on, EDIT held does nothing (no list: LEARN stays on); the tap as always */
static void test_edit_hold_in_learn(void)
{
    ui_host_init();
    users_clear();
    ml_toggle();
    check("(MIDI LEARN on)", ui.ml.on);
    tap(B_EDIT);
    check("LEARN: an EDIT tap opens SOUND as always, LEARN stays on", ui.ml.on && !pl_on() && !ui.home &&
          cur_page()->fam == FAM_SND);
    hold(B_EDIT);
    check("LEARN: EDIT held opens no list (the press a tap), LEARN stays on", ui.ml.on && !pl_on() &&
          cur_page()->fam == FAM_SND);
    ml_toggle();
    hold(B_EDIT);
    check("LEARN off: EDIT held opens the list again", !ui.ml.on && pl_on());
    tap(B_EDIT);
}

/* the track's preset on its SOUND page ("PRESET K909", "*" once a SOUND knob moved) and in TRACKS' footer */
static void test_name_and_edited_mark(void)
{
    uint32_t a, b;
    ui_host_init();
    users_clear();
    tap(B_EDIT);
    snap_page("presets/11_sound_preset");
    a = lit_box(0, Y_GRAPH + 2, 240, Y_GRAPH + 22);
    check("SOUND: the track's preset named above the voice", a > 0 && !TSEL->pre_ed);
    host_ticks += 300u * 1000u * FM1_TICKS_PER_US;
    turn(EN_K2, 1);                                     /* TUNE */
    ui_frame();
    host_ticks += 2000u * 1000u * FM1_TICKS_PER_US;
    for (b = 0; b < 50u; b++)                           /* (the knob's readout gone) */
        ui_frame();
    snap_page("presets/12_sound_edited");
    b = lit_box(0, Y_GRAPH + 2, 240, Y_GRAPH + 22);
    check("SOUND: a SOUND knob turned marks the preset edited (*)", TSEL->pre_ed && b > a);
    open_tracks();
    snap_page("presets/13_tracks_preset");
    check("TRACKS: the selected track's preset in the breadcrumb (right)", fb_count(120, Y_CRUMB + 8u, 114, 10, C_BG) > 0u);
    TSEL->pre = (uint16_t)PRE_NONE;
    tap(B_EDIT);
    snap_page("presets/14_sound_no_preset");
    check("SOUND with no preset: PRESET -", lit_box(0, Y_GRAPH + 2, 240, Y_GRAPH + 22) > 0);
}

/* ---- the list auditions (owner, 2026-10-19): every cursor move puts the highlighted preset on the track for now (its
 * MODEL and 8 knobs) and hits it once; the track's key plays it; LOAD keeps it and closes the list; any other way out
 * (EDIT, HOME) puts the track's sound from before back, as another track picked in the list does; SAVE stores that
 * sound, not the auditioned one; seven rows, no K1 / K2 hint */
static uint32_t voices_on(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NDV; i++)
        n += t->v[i].active;
    return n;
}
typedef struct { int16_t p[P_COUNT]; uint16_t pre; uint8_t ed, model; } snd_t;   /* a track's whole sound */
static void snd_take(const track_t *t, snd_t *s)
{
    memcpy(s->p, t->p, sizeof s->p);
    s->pre = t->pre;
    s->ed = t->pre_ed;
    s->model = t->model;
}
static int snd_same(const track_t *t, const snd_t *s)
{
    return !memcmp(s->p, t->p, sizeof s->p) && s->pre == t->pre && s->ed == t->pre_ed && s->model == t->model;
}
static int voice_kept(const track_t *t, const snd_t *s)   /* everything but MODEL and the 8 SOUND knobs as in s */
{
    uint32_t id;
    for (id = 0; id < P_COUNT; id++)
        if (id != P_MODEL && !PRE_SOUND(id) && t->p[id] != s->p[id])
            return 0;
    return 1;
}

static void test_list_preview_scrub(void)
{
    uint32_t id0, want;
    snd_t s;
    ui_host_init();
    users_clear();
    ui_frame();
    TSEL->p[P_CHOKE] = 2;                               /* (a CHOKE, a LEVEL, a FILTER the audition leaves alone) */
    TSEL->p[P_LEVEL] = 77;
    TSEL->p[P_FCUT] = 50;
    snd_take(TSEL, &s);
    hold(B_EDIT);
    id0 = ui.pr.cur;
    drum_cut(TSEL);
    ui_frame();
    check("list open: nothing auditioned yet (the track as it was, nothing hit)", pl_on() && snd_same(TSEL, &s) &&
                                                                                   voices_on(TSEL) == 0u);
    pre_turn(1);
    check("list PRESETS one detent: the highlighted preset on the track at once (MODEL, 8 knobs), not edited, hit once",
          ui.pr.cur == id0 + 1u && voice_is(TSEL, id0 + 1u) && !TSEL->pre_ed && voices_on(TSEL) == 1u && pl_on());
    check("list audition: CHOKE, LEVEL, FILTER and the rest untouched", voice_kept(TSEL, &s));
    snap_page("presets/15_list_preview");
    drum_cut(TSEL);
    host_ticks += 300u * 1000u * FM1_TICKS_PER_US;      /* (a new turn: no acceleration) */
    turn(EN_K1, 1);
    ui_frame();
    check("list KNOB 1: the cursor, auditioned (one hit)", ui.pr.cur == id0 + 2u && voice_is(TSEL, id0 + 2u) &&
                                                          voices_on(TSEL) == 1u);
    drum_cut(TSEL);
    want = PRE_BANK_START[(preset_bank_of(ui.pr.cur) + PB_N - 1u) % PB_N];   /* (fix 8: CLASSIC is the last factory
                                                                              * bank, USER after it: back to MODERN) */
    turn(EN_SELECT, -1);
    ui_frame();
    check("list SELECT: the bank before's first, auditioned (another voice: one hit)", ui.pr.cur == want &&
          voice_is(TSEL, want) && voices_on(TSEL) == 1u && voice_kept(TSEL, &s));
    drum_cut(TSEL);
    turn(EN_K2, 1);
    ui_frame();
    want = PRE_BANK_START[(preset_bank_of(want) + 1u) % PB_N];
    check("list KNOB 2: the next bank's first, auditioned", ui.pr.cur == want && voice_is(TSEL, want) &&
                                                              voices_on(TSEL) == 1u);
    drum_cut(TSEL);
    keys(1u << KEY_TRK_KEY[0]);
    ui_frame();
    keys(0);
    ui_frame();
    check("list: the track's own key plays the auditioned sound (the track stays)", song.sel == 0u &&
          voice_is(TSEL, want) && voices_on(TSEL) == 1u && pl_on());
    while (preset_bank_of(ui.pr.cur) != PB_CLASSIC) {         /* the last factory bank (fix 8) */
        turn(EN_SELECT, 1);
        ui_frame();
    }
    want = ui.pr.cur;
    drum_cut(TSEL);
    turn(EN_SELECT, 1);                                 /* USER: its first slot empty */
    ui_frame();
    check("list on an empty user slot: no audition (the last one kept, nothing hit), EMPTY SLOT said",
          ui.pr.cur == PRE_NFACT && voice_is(TSEL, want) && voices_on(TSEL) == 0u && str_eq(ui.msg, "EMPTY SLOT"));
    tap(B_EDIT);
    check("list EDIT: the track's sound back (CHOKE, LEVEL, FILTER too)", !pl_on() && snd_same(TSEL, &s));
}

static void test_list_cancel_restores(void)
{
    uint32_t c;
    snd_t s, s1;
    ui_host_init();
    users_clear();
    ui_frame();
    TSEL->p[P_E1] = 10;                                 /* an edited sound */
    preset_touch(TSEL);
    snd_take(TSEL, &s);
    hold(B_EDIT);
    pre_turn(5);
    turn(EN_SELECT, -1);                                /* another voice (MODERN: CLASSIC's next is USER, fix 8) */
    ui_frame();
    check("(list: another voice auditioned)", (uint32_t)TSEL->p[P_MODEL] != s.model && pl_on());
    drum_cut(TSEL);
    tap(B_EDIT);
    check("list EDIT without LOAD: closed, MODEL, knobs, preset and * exactly as before, nothing hit",
          !pl_on() && snd_same(TSEL, &s) && voices_on(TSEL) == 0u);
    hold(B_EDIT);
    pre_turn(-3);
    check("(list: the one 3 before auditioned)", !snd_same(TSEL, &s));
    tap(B_HOME);
    check("list HOME without LOAD: closed, the sound back exactly", !pl_on() && snd_same(TSEL, &s));
    snd_take(&trk[1], &s1);
    hold(B_EDIT);
    pre_turn(2);
    c = ui.pr.cur;
    drum_cut(&trk[1]);
    turn(EN_ALGO, 1);
    ui_frame();
    check("list ALGORITHM: track 1's sound back, track 2 auditions the highlighted preset (one hit)",
          song.sel == 1u && snd_same(&trk[0], &s) && voice_is(&trk[1], c) && voices_on(&trk[1]) == 1u && pl_on());
    drum_cut(&trk[0]);
    keys(1u << KEY_TRK_KEY[0]);
    ui_frame();
    keys(0);
    ui_frame();
    check("list: track 1's white key: track 2's sound back, track 1 auditions it (the key's one hit)",
          song.sel == 0u && snd_same(&trk[1], &s1) && voice_is(&trk[0], c) && voices_on(&trk[0]) == 1u && pl_on());
    tap(B_EDIT);
    check("list EDIT: both tracks as before", !pl_on() && snd_same(&trk[0], &s) && snd_same(&trk[1], &s1));
}

static void test_list_load_closes(void)
{
    uint32_t c;
    ui_host_init();
    users_clear();
    ui_frame();
    TSEL->p[P_E1] = 10;
    preset_touch(TSEL);
    hold(B_EDIT);
    turn(EN_SELECT, -1);                                /* (MODERN: CLASSIC's next is USER, fix 8) */
    ui_frame();
    c = ui.pr.cur;
    tap(B_OCTUP);
    check("list LOAD (OCT+): the highlighted preset stays on the track (its preset, not edited), the list closed",
          !pl_on() && TSEL->pre == c && voice_is(TSEL, c) && !TSEL->pre_ed && ui.home);
    snap_page("presets/16_list_loaded");
}

static void test_list_save_saves_real_sound(void)
{
    uint32_t m;
    ui_host_init();
    users_clear();
    ui_frame();
    m = (uint32_t)TSEL->p[P_MODEL];
    TSEL->p[P_E1] = 10;
    preset_touch(TSEL);
    hold(B_EDIT);
    turn(EN_SELECT, -1);                                /* (MODERN: CLASSIC's next is USER, fix 8) */
    ui_frame();
    check("(list: another voice auditioned)", (uint32_t)TSEL->p[P_MODEL] != m);
    tap(B_SAVE);
    check("list SAVE while auditioning: the sound from before saved (its voice, DECAY 10), the track back to it, now "
          "its preset (not edited)",
          upr.s[0].used == 1u && upr.s[0].model == m && upr.s[0].k[1] == 10 && (uint32_t)TSEL->p[P_MODEL] == m &&
              TSEL->p[P_E1] == 10 && TSEL->pre == PRE_NFACT && !TSEL->pre_ed && ui.pr.cur == PRE_NFACT && pl_on());
    tap(B_EDIT);
    check("list EDIT after SAVE: the saved sound stays", !pl_on() && (uint32_t)TSEL->p[P_MODEL] == m &&
                                                         TSEL->p[P_E1] == 10 && TSEL->pre == PRE_NFACT);
}

static void test_list_seven_rows(void)
{
    uint32_t i, rows = 0, bands = 0, prev = 0, y;
    ui_host_init();
    users_clear();
    ui_frame();
    hold(B_EDIT);
    ui.force = 1;
    ui_frame();
    for (i = 0; i < 7u; i++)
        rows += lit_box(16, 48u + i * 20u + 2u, 120, 48u + i * 20u + 18u) > 0u;
    check("list: 7 preset rows", rows == 7u);
    check("list: the controls start under the 7th row (their dotted rule)", lit_box(0, 188, 240, 189) >= 119u);
    for (y = 189; y < 239u; y++) {                      /* (row 239: the frame) */
        uint32_t on = lit_box(0, y, 240, y + 1u) > 0u;
        bands += on && !prev;
        prev = on;
    }
    check("list: two hint lines (OCT+ / SAVE, OCT- / EDIT), no K1 / K2 line", bands == 2u);
}

/* the md-ui final review's deferred minors (2026-10-19) */
static uint32_t fb_diff(const uint16_t *a, uint32_t x0, uint32_t y0, uint32_t w, uint32_t h)   /* pixels changed */
{
    uint32_t x, y, n = 0;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            n += fb[y * 240u + x] != a[y * 240u + x];
    return n;
}

static void test_popups_keep_their_margins(void)
{
    static uint16_t a[240u * 240u];
    uint32_t i;
    ui_host_init();                                  /* the clear confirm: box x 28..211, y 82..156; its tab x 95..144 */
    trk[0].step[0].on = 1;
    open_step_page();
    ui.force = 1;
    ui_frame();
    memcpy(a, fb, sizeof a);
    fm1_in.buttons |= 1u << panel.btn[B_REC];
    for (i = 0; i < 800; i++)
        ui_input();
    release_all();
    ui_frame();
    check("confirm: nothing beside its tab, left of it or under its shadow is wiped",
          ui.confirm && fb_diff(a, 28, 76, 66, 6) + fb_diff(a, 146, 76, 66, 6) + fb_diff(a, 27, 76, 1, 84) +
          fb_diff(a, 27, 157, 186, 3) + fb_diff(a, 212, 76, 1, 84) == 0u);
    press(B_OCTDN);
    ui_frame();
    release_all();
    ui_frame();
    ui_host_init();                                  /* TEMPO: box x 30..209, y 76..166; its tab x 95..144 */
    open_tracks();
    ui.force = 1;
    ui_frame();
    memcpy(a, fb, sizeof a);
    sel(1);
    ui_frame();
    check("TEMPO: nothing beside its tab, left of it or under its shadow is wiped",
          ui.bpm_t && fb_diff(a, 30, 70, 64, 6) + fb_diff(a, 146, 70, 64, 6) + fb_diff(a, 29, 70, 1, 98) +
          fb_diff(a, 29, 167, 182, 1) + fb_diff(a, 210, 70, 1, 98) == 0u);
    for (i = 0; i < 45u; i++)
        ui_frame();
}

static void test_crumb_unassigned_knob(void)
{
    ui_host_init();                                  /* RSTRX: HOME knob 4 has no parameter */
    TSEL->p[P_MODEL] = DM_RSTRX;
    ui.hot_col = 0;
    ui.hot_t = 40;
    ui.force = 1;
    ui_frame();
    ui.hot_col = 3;
    ui.hot_t = 40;
    ui_frame();
    check("an unassigned knob touched: the breadcrumb does not keep the last knob's value", !ui.focus_l[0]);
}

static void test_song_rules_under_the_bar(void)
{
    ui_host_init();
    seq_open("SONG");
    ui.song_row = 0;
    ui.force = 1;
    ui_frame();
    check("SONG: the column rules start under the first row's bar (no stubs over it), and run down",
          fb_count(40, Y_GRAPH, 1, 3, C_INK) + fb_count(64, Y_GRAPH, 1, 3, C_INK) +
          fb_count(100, Y_GRAPH, 1, 3, C_INK) == 0u && fb_count(40, 100, 1, 60, C_INK) > 20u);
}

static void test_about_moves_only_the_logo(void)
{
    uint32_t k, px;
    ui_host_init();
    hold(B_HOME);
    for (k = 0; k < MI_ABOUT; k++) {
        turn(EN_SELECT, 1);
        ui_frame();
    }
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    host_blit_px = 0;
    fm1_ms += 70u;                                   /* the logo's next frame */
    ui_frame();
    px = host_blit_px;
    check("ABOUT: a logo frame sends the logo, not the whole screen", ui.menu == 2 && px > 0u && px < 240u * 70u);
    check("ABOUT: the credits stay", fb_lit(80, 230) > 300);
    press(B_OCTDN);
    ui_frame();
    release_all();
    press(B_OCTDN);
    ui_frame();
    release_all();
}

/* fix 6 (owner, 2026-10-21): HOME's 4 boxes above PROJ are the bar position of a 4-bar cycle: always 4, the current bar
 * solid (1/16s since PLAY / 16, mod 4: the selected track's LEN or bank does not matter), advancing with the playhead,
 * box 1 again at every PLAY; stopped: box 1 (where PLAY starts) */
static int hbar_lit(uint32_t c)                       /* box c (x 6 + 13c, y 20, 10 x 6): 1 solid, 0 an outline, -1 else */
{
    uint32_t n = fb_count(6u + c * 13u, 20, 10, 6, C_INK);
    return n == 60u ? 1 : n == 28u ? 0 : -1;
}
static int hbar_only(uint32_t b)                      /* box b solid, the 3 others outlines */
{
    uint32_t c;
    int ok = 1;
    for (c = 0; c < 4u; c++)
        ok &= hbar_lit(c) == (c == b);
    return ok;
}
static void test_home_bar_cycle(void)
{
    uint32_t b, k, ok = 1;
    ui_host_init();
    TSEL->p[P_SLEN] = 16;                            /* one bank: the old boxes showed one */
    ui.force = 1;
    ui_frame();
    check("HOME bars: stopped, 4 boxes, bar 1 solid", ui.home && hbar_only(0));
    transport_req = 1;
    for (b = 0; b < 6u; b++) {                       /* 6 bars: 1 2 3 4 1 2, redrawn as they change (no force) */
        for (k = 0; k < 20000u && (gclk.cnt == 0xFFFFFFFFu || gclk.cnt < b * 16u + 3u); k++)
            ui_frame();
        ui_frame();
        ok &= hbar_only(b % 4u);
        if (!hbar_only(b % 4u))
            printf("      bar %u (1/16 %u): boxes %d %d %d %d\n", b, gclk.cnt, hbar_lit(0), hbar_lit(1), hbar_lit(2),
                   hbar_lit(3));
        if (b == 2u)
            shot("build/ui_shots/fixes/home_bar3.ppm");
    }
    check("HOME bars: playing, the current bar of 4 solid (1 2 3 4 1 2), the others outlines", ok);
    transport_req = 2;
    ui_frame();
    ui_frame();
    check("HOME bars: STOP, bar 1 again", hbar_only(0));
    TSEL->p[P_SLEN] = 64;                            /* 4 banks: still the bar, not the bank */
    transport_req = 1;
    ui_frame();                                      /* (PLAY taken: the count from 0) */
    for (k = 0; k < 20000u && (gclk.cnt == 0xFFFFFFFFu || gclk.cnt < 16u + 3u); k++)
        ui_frame();
    ui_frame();
    ok = hbar_only(1);
    if (!ok)
        printf("      LEN 64 (1/16 %u): boxes %d %d %d %d\n", gclk.cnt, hbar_lit(0), hbar_lit(1), hbar_lit(2), hbar_lit(3));
    transport_req = 1;                               /* PLAY again while playing: from bar 1 */
    ui_frame();
    ui_frame();
    if (!hbar_only(0))
        printf("      again (1/16 %u): boxes %d %d %d %d\n", gclk.cnt, hbar_lit(0), hbar_lit(1), hbar_lit(2), hbar_lit(3));
    check("HOME bars: LEN 64, bar 2 solid; PLAY again: bar 1", ok && hbar_only(0));
    transport_req = 2;
    ui_frame();
}

/* fix 8 (owner, 2026-10-21): the MODEL knob's list runs MD -> MODERN -> CLASSIC (the models keep their numbers: projects
 * store them); each voice with its first preset, wrapping both ways */
static void test_model_list_order(void)
{
    static const char *const WANT[NMODELS] = {
        "KTRX", "SDTRX", "XTTRX", "XCTRX", "CPTRX", "RSTRX", "CBTRX", "CHTRX", "OHTRX", "CYTRX", "MATRX", "CLTRX",
        "B2TRX", "BDEFM", "SDEFM", "XTEFM", "CPEFM", "RSEFM", "CBEFM", "HHEFM", "CYEFM",
        "KBOOM", "KPUNC", "SSNAP", "SCRAK", "HMETL", "HNOIS", "MEMB", "FM", "ACID", "SMPL",
        "K808", "K909", "S808", "S909", "C808", "C909", "HATC", "HATO", "CYMB", "COWB", "TOM", "CONGA", "RIM", "CLAVE"};
    uint32_t i, ok = 1, okb = 1, okp = 1;
    ui_host_init();
    song.sel = 0;
    old_set_model(TSEL, DM_KTRX);
    for (i = 1; i <= NMODELS; i++) {
        model_step(1);
        ok &= str_eq(N_MODEL[TSEL->p[P_MODEL]], WANT[i % NMODELS]);
        okp &= TSEL->pre == preset_first_of((uint32_t)TSEL->p[P_MODEL]) && !TSEL->pre_ed;
        if (!str_eq(N_MODEL[TSEL->p[P_MODEL]], WANT[i % NMODELS]))
            printf("      step %u: %s, want %s\n", i, N_MODEL[TSEL->p[P_MODEL]], WANT[i % NMODELS]);
    }
    for (i = NMODELS; i-- > 0;) {
        model_step(-1);
        okb &= str_eq(N_MODEL[TSEL->p[P_MODEL]], WANT[i]);
    }
    check("MODEL list: MD (KTRX .. CYEFM), MODERN (KBOOM .. SMPL), CLASSIC (K808 .. CLAVE), CLAVE wraps to KTRX", ok);
    check("MODEL list: the same order backwards (KTRX back to CLAVE)", okb);
    check("MODEL list: each voice on its first preset, not edited", okp);
    check("models keep their numbers (projects store them): K808 0, ACID 23, KTRX 24, CYEFM 44",
          DM_K808 == 0 && DM_ACID == 23 && DM_KTRX == 24 && DM_CYEFM == 44 && NMODELS == 45);
}

/* fix 4: TOOLS INIT ALL (a new project) gives the TRX classic kit, each on its first preset, no choke; TRACKS and HOME
 * shown with it (build/ui_shots/fixes/trx_kit_*) */
static void test_init_all_trx_kit(void)
{
    static const uint32_t M[NTRK] = {DM_KTRX, DM_SDTRX, DM_CPTRX, DM_CHTRX, DM_OHTRX, DM_XTTRX, DM_RSTRX, DM_CYTRX};
    uint32_t k, ok = 1;
    ui_host_init();
    init_all();
    for (k = 0; k < NTRK; k++)
        ok &= (uint32_t)trk[k].p[P_MODEL] == M[k] && trk[k].p[P_CHOKE] == 0 &&
              trk[k].pre == preset_first_of(M[k]) && !trk[k].pre_ed;
    check("TOOLS INIT ALL: the TRX classic kit, first presets, no choke", ok);
    ui.force = 1;
    snap_page("fixes/trx_kit_home");
    open_tracks();
    ui.force = 1;
    snap_page("fixes/trx_kit_tracks");
    check("(TRACKS: the kit's models listed)", mix_mode());
}

/* ------------------------------------------------- fixes 2026-10-21 (fix-keys: items 1 2 5 7 9) --- */
/* item 7: PERFORM has its own knobs: while it has them (FX held, the PAGE screen, a layer key held) a turn of KNOB 1..4
 * is a macro's, never the page's under it, even when the detents come in between the pass's two reads (the scan ISR
 * runs while ui_input does: before, the page's loop took the late ones); SELECT turns no page under it */
static void test_fk_perform_own_knobs(void)
{
    int16_t e1, e0;
    uint32_t pg, r;
    ui_host_init();
    e1 = trk[0].p[P_E1];
    press(B_FX);
    ui_frame();
    host_ticks += 300u * 1000u * FM1_TICKS_PER_US;
    turn(EN_K2, 2);
    host_enc_late[panel.enc[EN_K2]] = 3 * panel.dir[EN_K2];   /* 3 more detents after fx_layer's read */
    fx_hold_frames(1);
    check("PERFORM (FX held, HOME under it): late KNOB 2 detents in the same pass leave DECAY alone",
          trk[0].p[P_E1] == e1 && perf_k[1] > 0);
    fx_hold_frames(1);
    check("..and are CRUSH's on the next pass (none lost)", perf_k[1] == 5);
    release_all();
    ui_frame();
    perf_k[1] = 0;
    tap(B_FX);                                       /* FILTER 1/2: KNOB 1 TYPE */
    e0 = TSEL->p[P_FTYPE];
    pg = ui.page;
    settings.perfpage = 1;                           /* PAGE: the screen stays after FX */
    press(B_FX);
    fx_hold_frames(500);
    release_all();
    ui_frame();
    for (r = 0; r < 4u; r++) {
        host_ticks += 300u * 1000u * FM1_TICKS_PER_US;   /* (slow turns: no acceleration) */
        turn(EN_K1, 1);
        host_enc_late[panel.enc[EN_K1]] = 2 * panel.dir[EN_K1];
        ui_frame();
    }
    sel(1);
    check("PERFORM PAGE over FILTER: KNOB 1 is FILTER's macro only (TYPE untouched), SELECT turns no page",
          ui.pg_open && ui.layer && TSEL->p[P_FTYPE] == e0 && ui.page == pg && perf_k[0] == 12);
    tap(B_HOME);
    settings.perfpage = 0;
    perf_k[0] = 0;
    ui_frame();
}

/* item 2: the HOME screen and the EDIT (SOUND, LAYER), FX and LFO pages show the selected track on the keys: its
 * white key (KEY_TRK_KEY) lit, the other keys dark unless held; it follows ALGORITHM and a key's select; KEYS (the
 * keyboard), COMP (DUCK), the step pages and GLOBAL / SAVE / GRIDS keep their own; the menu none */
static int fk_keys_show_only(uint32_t mask)          /* the 27 key LEDs: lit exactly where mask says */
{
    uint32_t k, ok = 1;
    for (k = 0; k < 27u; k++)
        ok &= led_on(14u + k) == (int)((mask >> k) & 1u);
    return (int)ok;
}
static void test_fk_track_keys(void)
{
    uint32_t i, n = 0, ok = 1, other = 1, fams = 0;
    ui_host_init();
    for (i = 0; i <= NPAGES; i++) {                  /* (NPAGES: the HOME screen) */
        uint32_t fam;
        ui.home = i == NPAGES;
        ui.hmode = ui.home;
        if (i < NPAGES)
            ui.page = (uint8_t)i;
        page_entered();
        song.sel = (uint8_t)(i % NTRK);
        ui_frame();
        fam = cur_fam();
        if (ui.home || fam == FAM_SND || fam == FAM_LAY || fam == FAM_FX || fam == FAM_LFO) {
            ok &= fk_keys_show_only(1u << KEY_TRK_KEY[song.sel]);
            fams |= 1u << fam;
            n++;
        } else if (!step_keys() && !comp_mode()) {
            other &= fk_keys_show_only(0);           /* GLOBAL SAVE GRIDS: as before, the keys held only */
        }
    }
    check("item 2: HOME, EDIT (SOUND, LAYER), FX, LFO: the selected track's key lit, every other key dark",
          ok && n > 10u && fams == (1u << FAM_HOME | 1u << FAM_SND | 1u << FAM_LAY | 1u << FAM_FX | 1u << FAM_LFO));
    check("item 2: GLOBAL, SAVE, GRIDS: no track key (the keys held only, as before)", other);
    tap(B_FX);
    song.sel = 0;
    turn(EN_ALGO, 2);
    ui_frame();
    check("item 2: FX, ALGORITHM to track 2: its key (G3) lit, F3 dark", song.sel == 1u &&
          fk_keys_show_only(1u << KEY_TRK_KEY[1]));
    tap(B_EDIT);
    keys(1u << KEY_TRK_KEY[4]);                      /* C4: track 5, selected by its key */
    ui_frame();
    check("item 2: EDIT, track 5's key pressed: selected, lit (held)", song.sel == 4u &&
          fk_keys_show_only(1u << KEY_TRK_KEY[4]));
    keys(1u << KEY_TRK_KEY[4] | 1u << 1);            /* a black key held too: lit while held */
    ui_frame();
    check("item 2: a key held is lit beside the track's", fk_keys_show_only(1u << KEY_TRK_KEY[4] | 1u << 1));
    keys(0);
    ui_frame();
    check("item 2: let go: track 5's key stays lit", fk_keys_show_only(1u << KEY_TRK_KEY[4]));
    tap(B_SCL);                                      /* KEYS: the keyboard's picture (held keys only) */
    check("item 2: KEYS on: no track key (the keyboard)", fk_keys_show_only(0));
    tap(B_SCL);
    tap(B_LFO);
    check("item 2: LFO: track 5's key", fk_keys_show_only(1u << KEY_TRK_KEY[4]));
    ui.menu = 1;
    ui.force = 1;
    ui_frame();
    check("item 2: the menu over it: no track key", fk_keys_show_only(0));
    ui.menu = 0;
    ui_frame();
}

/* item 5: OCT- / OCT+ breathe (upstream Felucca's #119, led_dim.c) where pressing them would do something, a way back
 * lit: the steps (a bank that way; OCT+ lit while held, its accent), SOUND (OCT+: LAYER), LAYER (OCT- back), a dialog
 * (NO lit, YES breathing), the menu (BACK lit, OK breathing; ABOUT: back), NAME, MIDI LEARN, the PRESETS list (LOAD;
 * DELETE on a used user slot); KEYS: lit off octave 0 as before; HOME, GLOBAL: dark */
static uint32_t fk_oct(void)                         /* 1 OCT- lit, 2 OCT+ lit, 4 OCT+ breathing, 8 OCT- breathing */
{
    return (uint32_t)led_on(panel.btn[B_OCTDN]) | (uint32_t)led_on(panel.btn[B_OCTUP]) << 1 |
           (uint32_t)led_breathes(panel.btn[B_OCTUP]) << 2 | (uint32_t)led_breathes(panel.btn[B_OCTDN]) << 3;
}
static void test_fk_oct_breath(void)
{
    uint32_t k;
    ui_host_init();
    ui_frame();
    check("item 5: the HOME screen: OCT- / OCT+ dark, the hook idle (BACKLIGHT OFF)", fk_oct() == 0u && !led_dim_on);
    trk[0].p[P_SLEN] = 48;
    seq_open("STEP");
    ui_frame();
    check("item 5: STEP, bank 1 of 3: OCT+ breathes (a bank after), OCT- dark", fk_oct() == 4u && led_br_on);
    tap(B_OCTUP);
    check("item 5: bank 2 of 3: both breathe", ui.bank == 1u && fk_oct() == 12u);
    tap(B_OCTUP);
    check("item 5: bank 3 of 3: OCT- breathes, OCT+ dark", ui.bank == 2u && fk_oct() == 8u);
    press(B_OCTUP);
    ui_frame();
    check("item 5: OCT+ held (the accent): lit", fk_oct() == (2u | 8u));
    release_all();
    ui_frame();
    tap(B_OCTDN);
    tap(B_OCTDN);
    trk[0].p[P_SLEN] = 16;
    tap(B_SCL);                                      /* KEYS: the octave, lit off 0 */
    check("item 5: KEYS at octave 0: dark", fk_oct() == 0u);
    tap(B_OCTUP);
    check("item 5: KEYS at octave +1: OCT+ lit (not breathing)", song.octave == 1 && fk_oct() == 2u);
    tap(B_OCTDN);
    tap(B_SCL);
    tap(B_EDIT);
    check("item 5: SOUND: OCT+ breathes (the LAYER pages)", fk_oct() == 4u);
    tap(B_OCTUP);
    check("item 5: LAYER: OCT- lit (back to SOUND)", cur_fam() == FAM_LAY && fk_oct() == 1u);
    tap(B_GLO);
    check("item 5: GLOBAL: dark", fk_oct() == 0u);
    ui.confirm = 1;
    ui.confirm_trk = 0;
    ui.force = 1;
    ui_frame();
    check("item 5: a dialog: OCT- (NO) lit, OCT+ (YES) breathes", fk_oct() == 5u);
    tap(B_OCTDN);
    for (k = 0; k < 1000u && !ui.menu; k++) {         /* HOME held: the menu */
        fm1_in.buttons |= 1u << panel.btn[B_HOME];
        ui_frame();
    }
    release_all();
    ui_frame();
    check("item 5: the menu: OCT- (BACK) lit, OCT+ (OK) breathes", ui.menu == 1u && fk_oct() == 5u);
    ui.menu_sel = MI_ABOUT;
    tap(B_OCTUP);
    check("item 5: ABOUT: OCT- (back) lit alone", ui.menu == 2u && fk_oct() == 1u);
    tap(B_OCTDN);
    menu_close();
    ui_frame();
    ui.ml.on = 1;
    ui_frame();
    check("item 5: MIDI LEARN: OCT- (CLEAR) lit, OCT+ (DONE) breathes", fk_oct() == 5u);
    ui.ml.on = 0;
    ui_frame();
    project_save(0);
    name_open(NK_RENAME, 0);
    ui_frame();
    check("item 5: NAME: OCT- (CANCEL) lit, OCT+ (RENAME) breathes", name_on() && fk_oct() == 5u);
    name_close();
    ui_frame();
    users_clear();
    tap(B_EDIT);
    hold(B_EDIT);
    check("item 5: the PRESETS list on a factory preset: OCT+ (LOAD) breathes, OCT- (DELETE) dark", pl_on() &&
          fk_oct() == 4u);
    upr.s[0].used = 1u;
    upr.s[0].model = 0;
    ui.pr.cur = PRE_NFACT;                           /* a used user slot under the cursor */
    ui_frame();
    check("item 5: .. on a used user slot: OCT- (DELETE) breathes too", fk_oct() == 12u);
    tap(B_EDIT);
    users_clear();
    ui_frame();
}

/* item 9: BACKLIGHT's levels (upstream Felucca's LEDS: OFF DIM LO DIM HI INV, 1.5.6's INV LO INV HI) on STEP with
 * steps 2 and 5 on: the layers each mode gives the keys and the buttons, and the frames of 20 the hook lights them in */
static uint32_t fk_layer(uint32_t id)                /* 0 none, 1 full, 2 mid, 3 glow, 4 breath */
{
    uint8_t q = led_pos[id];
    uint32_t lay, bit;
    if (q == 0xFF || !led_dim_on)
        return q != 0xFF && ((fm1_led[q >> 3] >> (q & 7u)) & 1u);
    bit = 1u << (q & 7u);
    lay = led_lay[q >> 3];
    return lay & bit ? 1u : (lay >> 8) & bit ? 2u : (lay >> 16) & bit ? 3u : (lay >> 24) & bit ? 4u : 0u;
}
static uint32_t fk_frames(uint32_t ph)               /* frames of 20 in a phase mask */
{
    uint32_t f, n = 0;
    for (f = 0; f < 20u; f++)
        n += (ph >> f) & 1u;
    return n;
}
static void test_fk_led_levels(void)
{
    static const char *const WHAT[BL_COUNT] = {
        "OFF: on steps full, the rest dark",
        "DIM LO (the old ON): on steps full, the unlit beats mid (5 of 20 frames), the other keys and the idle buttons "
        "glow (1 of 20)",
        "DIM HI: as DIM LO, brighter: the beats 6 of 20, the glow 2 of 20",
        "INV: the active ones dark, the idle keys and buttons lit",
        "INV LO: the active ones dark, the idle ones mid (2 of 20)",
        "INV HI: the active ones dark, the idle ones mid (5 of 20)"};
    static const uint32_t MID_N[BL_COUNT] = {0, 5, 6, 0, 2, 5}, GLOW_N[BL_COUNT] = {0, 1, 2, 0, 0, 0};
    uint32_t m;
    ui_host_init();
    trk[0].p[P_SLEN] = 32;
    trk[0].step[1].on = trk[0].step[4].on = 1;       /* step 2; step 5 a beat */
    seq_open("STEP");
    for (m = 0; m < BL_COUNT; m++) {
        uint32_t on = 1u, idle = m == BL_OFF ? 0u : m == BL_INV ? 1u : m >= BL_INV_LO ? 2u : 3u, ok;
        backlight = m;
        ui_frame();
        if (m == BL_INV || m >= BL_INV_LO)
            on = 0u;
        ok = fk_layer(14u + STEP_KEY[1]) == on && fk_layer(14u + STEP_KEY[4]) == on &&
             fk_layer(14u + STEP_KEY[2]) == idle && fk_layer(14u + 1u) == idle &&   /* an off step, a black key */
             fk_layer(14u + STEP_KEY[0]) == (m == BL_DIM_LO || m == BL_DIM_HI ? 2u : idle) &&   /* an unlit beat */
             fk_layer(panel.btn[B_SEQ]) == on && fk_layer(panel.btn[B_SAVE]) == idle &&
             fk_layer(panel.btn[B_OCTUP]) == 4u &&   /* breathing in every mode */
             fk_frames(led_mid_ph) == MID_N[m] && fk_frames(led_glow_ph) == GLOW_N[m];
        check(WHAT[m], ok);
    }
    backlight = BL_OFF;
    trk[0].p[P_SLEN] = 16;
    ui_frame();
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
    test_home_modes();
    test_rec_everywhere();
    test_quick_mute();
    test_mute_screen();
    test_mute_screen_keys();
    test_mute_screen_refused();
    test_no_save_while_playing();
    test_knob_accel();
    test_home_keys_and_lfo();
    test_tracks_rec_keys();
    test_reson_pages();
    test_acid_cap();
    test_select_pages();
    test_global_mute_setting();
    test_global_metro_setting();
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
    test_perform_knobs_stay();
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
    test_perform_dead_knob();
    test_step_keys_everywhere();
    test_tracks_rec_mutes();
    test_fm_screens();
    test_fm_graph_clear();
    test_ktrx_screens();
    test_trxn_screens();
    test_efmn_screens();
    test_trx_drum_screens();
    test_keys_map_oct();
    test_keys_acid_mono();
    test_keys_release_anywhere();
    test_keys_owners();
    test_keys_oct_uboot();
    test_keys_live_rec();
    test_keys_live_rec_wrap();
    test_keys_live_rec_mid_block();
    test_keys_live_rec_long_hold();
    test_keys_live_rec_tie_clean();
    test_keys_live_rec_drum();
    test_step_key_note();
    test_step_key_note_drum();
    test_key_note_reset();
    test_rec_hit_drum_note();
    test_srec_keys();
    test_srec_knobs();
    test_srec_exits();
    test_roll_draw();
    test_roll_follow();
    test_roll_view_most();
    test_roll_view_low_note();
    test_roll_view_srec();
    test_roll_view_key_out();
    test_roll_view_idle();
    test_roll_label_slide_edges();
    test_foot_note_song();
    test_roll_cursor_playhead();
    test_foot_note();
    test_grid_transposed_mark();
    test_acid_edit_graph();
    test_roll_frame_cost();
    test_roll_held_prob();
    test_rec_hit_on_tie();
    test_keys_live_rec_len_cut();
    test_srec_rest_clears();
    test_keys_hint_playing();
    test_srec_chord();
    test_keys_seq_takeover();
    test_acid_screens();
    test_prio_sound_list();
    test_cpu_icon();
    test_backlight_leds();
    test_backlight_beats_everywhere();
    test_backlight_menu();
    test_boot_title();
    test_theme_menu();
    test_theme_repaint();
    test_topbar();
    test_crumb();
    test_trig_strip();
    test_msg_popup();
    test_cell_knob_vs_text();
    test_cell_touched();
    test_cell_value_fits();
    test_graph_model_name();
    test_graph_step_bars();
    test_graph_slots_cursor();
    test_graph_song_rows();
    test_graph_off_word();
    test_graph_comp_meter();
    test_graph_lfo_fill();
    test_tracks_row_items();
    test_tracks_peak_tick();
    test_mute_boxes();
    test_perform_buttons();
    test_home_strip();
    test_home_knob_values_below();
    test_home_scope_height();
    test_confirm_popup();
    test_tempo_popup();
    test_menu_popup_cursor();
    test_name_block_cursor();
    test_theme_sheet();
    test_fix_value_4_glyphs();
    test_fix_zoom_home();
    test_fix_msg_shrink();
    test_fix_popup_holds_graph();
    test_home_lev_meter();
    test_popup_shows_and_loads();
    test_popup_fast_while_playing();
    test_popup_skips_capped_acid();
    test_list_save_delete_overwrite();
    test_long_edit_opens_list();
    test_edit_hold_in_learn();
    test_name_and_edited_mark();
    test_list_preview_scrub();
    test_list_cancel_restores();
    test_list_load_closes();
    test_list_save_saves_real_sound();
    test_list_seven_rows();
    test_popups_keep_their_margins();
    test_crumb_unassigned_knob();
    test_song_rules_under_the_bar();
    test_about_moves_only_the_logo();
    test_home_bar_cycle();
    test_model_list_order();
    test_init_all_trx_kit();
    test_fk_perform_own_knobs();                     /* fixes 2026-10-21 (fix-keys) */
    test_fk_track_keys();
    test_fk_oct_breath();
    test_fk_led_levels();
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: all passed\n", fails);
    return fails ? 1 : 0;
}
