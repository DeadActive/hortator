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
    check("EDIT again: SOUND 2", str_eq(cur_page()->title, "SOUND 2"));
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

int main(void)
{
    test_families();
    test_track_select();
    test_model_swap();
    test_home_macros();
    test_grid_keys();
    test_bank_follows_len();
    test_mixer_and_rec();
    test_clear_confirm();
    test_oct_both_reaches_main();
    printf(fails ? "ui_test: %d FAILED\n" : "ui_test: all passed\n", fails);
    return fails ? 1 : 0;
}
