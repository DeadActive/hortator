/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* MIDI LEARN (spec docs/superpowers/specs/2026-10-13-midi-learn-design.md) on the host, built with FELUCCA_BLE=1 (the
 * Bluetooth MIDI input in MIDI and SMC DAW mode) and the host flash (the map's own object): GLO + G4, a knob picks, a
 * CC binds, OCT- / OCT+, the rules, a learned CC in the audio interrupt (range, enum, bipolar, its track, the LFOs'
 * base, motion's base, a song's lock), every input, the flash record. Screens: build/ui_shots/learn/. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#include "ble_ui_stub.h"
uint32_t ble_midi_room(void) { return 64u; }
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 1; }
#include "../firmware/src/bt_set.c"
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_fml.c"
#include "../firmware/src/bt_devs.c"
#include "../firmware/src/bt_page.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
static int same(const char *a, const char *b) { return !strcmp(a, b); }

/* ---- the panel */
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
    glo_hold_frames(2);
    keys(0);
    glo_hold_frames(1);
}
static void glo_up(void)
{
    release_all();
    ui_frame();
}
#define KEY_G4 14u
static void learn_toggle(void)                       /* GLO held, G4, GLO let go */
{
    press(B_GLO);
    glo_hold_frames(450);                            /* the MUTE screen (0.4 s) */
    glo_key(KEY_G4);
    glo_up();
}
static void tap(uint32_t b)
{
    press(b);
    ui_frame();
    release_all();
    ui_frame();
}
static void knob(uint32_t k, int32_t s)
{
    turn(EN_K1 + k, s);
    ui_frame();
}
static int led_lit(uint32_t id)
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((fm1_led[q >> 3] >> (q & 7u)) & 1u);
}
static void snap(const char *name)
{
    char path[96];
    uint32_t i;
    for (i = 0; i < 3; i++)
        ui_frame();
    snprintf(path, sizeof path, "build/ui_shots/learn/%s.ppm", name);
    shot(path);
}

/* ---- MIDI in: a CC packet on channel ch, from each input; the audio block reads the ring, the UI frame after it
 * takes a learn */
static uint32_t cc_pkt(uint32_t ch, uint32_t n, uint32_t v) { return 0x0Bu | (0xB0u | ch) << 8 | n << 16 | v << 24; }
static void usb_cc(uint32_t ch, uint32_t n, uint32_t v) { midi_in_event(cc_pkt(ch, n, v)); }
static void trs_cc(uint32_t ch, uint32_t n, uint32_t v)
{
    um_byte(0xB0u | ch);
    um_byte(n);
    um_byte(v);
}
static void ble_cc(uint32_t ch, uint32_t n, uint32_t v) { bt_midi_in(cc_pkt(ch, n, v)); }
static void frames(uint32_t n)
{
    while (n--)
        ui_frame();
}
static void send_cc(uint32_t n, uint32_t v)          /* USB, channel 1; then two frames */
{
    usb_cc(0, n, v);
    frames(2);
}

static const char *label(uint32_t k, uint32_t id) { return track_desc(&trk[k], id)->label; }
static uint32_t count(void) { return ml_count(); }
static uint32_t cc_of(uint32_t k, uint32_t id) { return ml_cc_of(k, id); }   /* CC + 1, 0 none */

static void fresh(void)                              /* HOME (the HOME screen), track 1, nothing learned, a blank flash */
{
    memset(hflash, 0xFF, sizeof hflash);
    host_jedec = 0x856014u;
    ui_host_init();
    persist_boot();
    settings_init();
    btset.btmode = 2;                                /* Bluetooth: MIDI */
    frames(2);
}
/* learn cc to knob k's parameter of the selected track on HOME */
static void learn_home(uint32_t k, uint32_t n)
{
    knob(k, 1);
    send_cc(n, 64);
}

/* ---- the flow */
static void test_flow(void)
{
    char ln[32], want[32];
    uint32_t a, v;
    fresh();
    press(B_GLO);
    glo_hold_frames(450);
    check("GLO held: the MUTE screen", ui.mute);
    snap("mute_learn_off");
    a = hit_age(&trk[0]);
    glo_key(KEY_G4);
    check("MUTE screen + G4: LEARN on, the top bar says MIDI LEARN ON", ui.ml.on && same(ui.msg, "MIDI LEARN ON"));
    check("G4 played nothing (the MUTE screen's key), its LED lit while LEARN is on", hit_age(&trk[0]) == a &&
          led_lit(14u + KEY_G4));
    ui.msg_t = 0;
    snap("mute_learn_on");
    glo_up();
    check("GLO let go: HOME again, LEARN still on", !ui.mute && ui.home && ui.ml.on);
    ml_line(ln);
    check("the line: LEARN: TURN OR PRESS", same(ln, "LEARN: TURN OR PRESS"));
    snap("line_turn_a_knob");
    v = (uint32_t)trk[0].p[P_E1];
    knob(1, 1);
    check("a knob turned on HOME edits as usual", (uint32_t)trk[0].p[P_E1] == v + 1u);
    check(".. and picks its parameter on the selected track", ui.ml.pick && ui.ml.trk == 0u && ui.ml.id == P_E1);
    ml_line(ln);
    snprintf(want, sizeof want, "T1 %s: SEND A CC", label(0, P_E1));
    check("the line: T1 <knob>: SEND A CC", same(ln, want));
    snap("line_send_a_cc");
    usb_cc(3, 74, 0);
    frames(2);
    snprintf(want, sizeof want, "CC74 -> T1 %s", label(0, P_E1));
    check("the next CC (any channel) is bound: CC74 -> T1 <knob>; it set nothing while picked",
          cc_of(0, P_E1) == 75u && same(ui.msg, want) && (uint32_t)trk[0].p[P_E1] == v + 1u);
    check("the pick ends: TURN OR PRESS again", !ui.ml.pick && !ml_arm);
    usb_cc(5, 74, 0);
    frames(2);
    check("CC74 value 0 (another channel): the knob's minimum", trk[0].p[P_E1] == track_desc(&trk[0], P_E1)->min);
    usb_cc(0, 74, 127);
    frames(2);
    check("CC74 value 127: the knob's maximum", trk[0].p[P_E1] == track_desc(&trk[0], P_E1)->max);
    knob(1, -1);
    ml_line(ln);
    snprintf(want, sizeof want, "T1 %s = CC74", label(0, P_E1));
    check("the knob again: picked, the line T1 <knob> = CC74", ui.ml.pick && same(ln, want));
    ui.msg_t = 0;
    snap("line_learned");
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("OCT+: done (LEARN DONE), nothing cleared", !ui.ml.on && same(ui.msg, "LEARN DONE") && count() == 1u);
    usb_cc(0, 74, 0);
    frames(2);
    check("LEARN off: CC74 still sets its knob", trk[0].p[P_E1] == track_desc(&trk[0], P_E1)->min);
    learn_toggle();
    check("GLO + G4 again: on", ui.ml.on);
    learn_toggle();
    check("GLO + G4 once more: off (LEARN DONE)", !ui.ml.on && same(ui.msg, "LEARN DONE"));
}

/* ---- the rules */
static void test_rules(void)
{
    uint32_t n;
    fresh();
    learn_toggle();
    learn_home(1, 74);
    learn_home(2, 74);                               /* the same CC to another knob */
    check("a CC learned again: it moves (one CC, one parameter)", cc_of(0, P_E2) == 75u && !cc_of(0, P_E1) &&
          count() == 1u);
    learn_home(2, 75);                               /* another CC to the same knob */
    check("a parameter learned again: its old CC goes (one parameter, one CC)", cc_of(0, P_E2) == 76u && count() == 1u);
    usb_cc(0, 74, 0);
    frames(2);
    check("(the old CC no longer sets it)", trk[0].p[P_E2] != track_desc(&trk[0], P_E2)->min);
    knob(0, 1);
    for (n = 0; n < 4u; n++) {
        static const uint8_t BAD[4] = {0, 32, 120, 127};
        ui.msg_t = 0;
        send_cc(BAD[n], 10);
        if (!check(n == 0u ? "CC0 (bank select): CC NOT LEARNABLE, the pick stays" : n == 1u ? "CC32: CC NOT LEARNABLE"
                   : n == 2u ? "CC120 (channel mode): CC NOT LEARNABLE" : "CC127: CC NOT LEARNABLE",
                   same(ui.msg, "CC NOT LEARNABLE") && ui.ml.pick && !cc_of(0, P_E0) && count() == 1u))
            printf("    msg \"%s\"\n", ui.msg);
    }
    usb_cc(0, 0, 10);                                /* bank select, then a CC, in one block: the CC is taken */
    usb_cc(0, 1, 10);
    frames(2);
    check("a bank select and a CC in one block: the CC is bound", cc_of(0, P_E0) == 2u && count() == 2u);
    check("CC1 and CC119 are learnable, 0 32 120..127 never", ml_free_cc(1) && ml_free_cc(119) && ml_free_cc(31) &&
          ml_free_cc(33) && !ml_free_cc(0) && !ml_free_cc(32) && !ml_free_cc(120) && !ml_free_cc(127));
}

static void test_full(void)
{
    uint32_t k, i;
    fresh();
    learn_toggle();
    for (k = 0; k < NTRK; k++) {                     /* 8 tracks x 4 HOME knobs = 32 */
        track_select(k);
        for (i = 0; i < 4u; i++)
            learn_home(i, 40u + k * 4u + i);   /* (CC40..71) */
    }
    if (!check("32 learned", count() == 32u))
        printf("    %u learned\n", (unsigned)count());
    tap(B_PLAY);                                     /* (an action: PLAY picked) */
    ui.msg_t = 0;
    send_cc(80, 127);
    check("a 33rd: LEARN FULL, nothing replaced, the pick stays", same(ui.msg, "LEARN FULL") && count() == 32u &&
          !ml_src_of(0, ML_A_PLAY) && ui.ml.pick && ui.ml.act == ML_A_PLAY);
    send_cc(40, 127);                                /* a learned CC: it moves here */
    check("full, a learned CC to an action: it moves", ml_src_of(0, ML_A_PLAY) == 41u && !cc_of(0, P_E0) &&
          count() == 32u);
}

/* OCT- clears the picked parameter's CC; with nothing picked asks CLEAR ALL LEARNED? */
static void oct(uint32_t b)
{
    press(b);
    ui_frame();
    release_all();
    ui_frame();
}
static void test_clear(void)
{
    char want[32], ln[32];
    int16_t v;
    fresh();
    learn_toggle();
    learn_home(1, 74);
    learn_home(2, 75);
    knob(1, 1);
    oct(B_OCTDN);
    snprintf(want, sizeof want, "T1 %s CC CLEARED", label(0, P_E1));
    check("OCT- with a learned parameter picked: its CC cleared", !cc_of(0, P_E1) && cc_of(0, P_E2) == 76u &&
          same(ui.msg, want) && ui.ml.on);
    ml_line(want);
    snprintf(ln, sizeof ln, "T1 %s: SEND A CC", label(0, P_E1));
    check("(the parameter stays picked, waiting for a CC)", ui.ml.pick && same(want, ln));
    ui.msg_t = 0;
    oct(B_OCTDN);
    check("OCT- again: NO CC TO CLEAR", same(ui.msg, "NO CC TO CLEAR"));
    v = trk[0].p[P_E1];
    send_cc(74, 0);
    check("a CC binds it again (setting nothing)", cc_of(0, P_E1) == 75u && trk[0].p[P_E1] == v && !ui.ml.pick);
    oct(B_OCTDN);
    check("OCT- with nothing picked: CLEAR ALL LEARNED? (LEARN stays on)", ui.confirm == 6u && ui.ml.on);
    snap("clear_all_confirm");
    oct(B_OCTDN);
    check("OCT- (NO): kept", !ui.confirm && count() == 2u && ui.ml.on);
    oct(B_OCTDN);
    oct(B_OCTUP);
    check("OCT+ (YES): all cleared (LEARN CLEARED), LEARN still on", !ui.confirm && !count() && ui.ml.on &&
          same(ui.msg, "LEARN CLEARED"));
    oct(B_OCTDN);
    check("OCT- with nothing learned: NOTHING LEARNED, no confirm", !ui.confirm && same(ui.msg, "NOTHING LEARNED"));
    learn_home(1, 74);
    press(B_OCTDN);                                  /* OCT- and OCT+ in one frame (or held: the UPDATE combo) */
    press(B_OCTUP);
    ui_frame();
    release_all();
    ui_frame();
    check("OCT- + OCT+ in one frame: LEARN done, no confirm left up, nothing cleared", !ui.ml.on && !ui.confirm &&
          count() == 1u);
    oct(B_OCTUP);
    check("(a later OCT+ clears nothing)", count() == 1u);
}

/* LEARN ends when the menu, NAME or another confirm opens; MODEL picks nothing */
static void hold(uint32_t label)
{
    uint32_t k;
    press(label);
    for (k = 0; k < 800; k++)
        ui_input();
    release_all();
    ui_frame();
}
static void test_ends(void)
{
    uint32_t k;
    fresh();
    learn_toggle();
    knob(1, 1);
    hold(B_HOME);
    check("the menu opens: LEARN off", ui.menu && !ui.ml.on && !ml_arm);
    hold(B_HOME);
    learn_toggle();
    tap(B_SEQ);                                      /* STEP */
    hold(B_REC);
    check("REC held on SEQ while learning: REC picked (no CLEAR TRACK?)", !ui.confirm && ui.ml.on &&
          ui.ml.act == ML_A_REC);
    lk_put(&mo.b, lk_key(0, 4, P_PAN), 40, 0);       /* (a motion event: MOTION's CLEAR asks) */
    for (k = 0; k < 6u && cur_page()->graph != GR_MOTION; k++)
        tap(B_SEQ);
    knob(3, 1);
    frames(1);
    check("another confirm (MOTION's CLEAR MOTION T1?): LEARN off", ui.confirm == 2u && !ui.ml.on);
    oct(B_OCTDN);
    project_save(0);                                 /* (slot A used: NAME renames it) */
    learn_toggle();
    for (k = 0; k < 4u && cur_page()->graph != GR_SLOTS; k++)
        tap(B_SAVE);
    knob(1, 1);                                      /* PROJECT NAME: renames */
    frames(1);
    check("NAME opens: LEARN off", name_on() && !ui.ml.on);
    name_close();
    fresh();
    tap(B_EDIT);                                     /* SOUND 1: MODEL */
    learn_toggle();
    knob(0, 1);
    check("MODEL: NOT LEARNABLE, no pick", same(ui.msg, "NOT LEARNABLE") && !ui.ml.pick);
    knob(1, 1);
    check("the next SOUND knob picks", ui.ml.pick && ui.ml.id == page_id(cur_page(), 1));
    fresh();
    learn_toggle();
    press(B_GLO);
    glo_hold_frames(450);
    turn(EN_K2, 3);
    glo_hold_frames(1);
    check("the MUTE screen's knobs pick nothing", !ui.ml.pick);
    glo_up();
}

/* a MODEL turn to a model without the picked knob: the pick goes (no "T1 -: SEND A CC") */
static void test_model_drop(void)
{
    char ln[32];
    uint32_t i;
    fresh();
    tap(B_EDIT);                                     /* SOUND 1: MODEL */
    learn_toggle();
    for (i = 0; i < 64u && label(0, P_E5)[0] == '-'; i++)
        model_step(1);
    ml_knob(0, P_E5);
    check("(a model with E5: picked)", ui.ml.pick && ui.ml.id == P_E5);
    for (i = 0; i < 64u && label(0, P_E5)[0] != '-'; i++)
        knob(0, 1);                                  /* MODEL */
    ml_line(ln);
    check("MODEL to one without the picked knob: the pick dropped, the footer TURN OR PRESS",
          label(0, P_E5)[0] == '-' && !ui.ml.pick && !ml_arm && same(ln, "LEARN: TURN OR PRESS"));
    send_cc(74, 64);
    check("(a CC then learns nothing)", !count() && ui.ml.on);
}

/* the pages: each picks the parameter it edits on the selected track */
static void test_pages(void)
{
    uint32_t ok = 1, k;
    fresh();
    track_select(2);
    tap(B_LFO);
    learn_toggle();
    knob(3, 1);
    ok &= ui.ml.pick && ui.ml.trk == 2u && ui.ml.id == P_LFO2 + LF_RATE;
    check("LFO 1/4: KNOB 4 is LFO 2's RATE, of the selected track (T3)", ok);
    oct(B_OCTUP);
    tap(B_LFO);
    tap(B_LFO);
    tap(B_LFO);                                      /* DEST */
    learn_toggle();
    knob(3, 1);
    check("DEST, slot 1: its DEPTH (LFO 1's own DEPTH id)", ui.ml.pick && ui.ml.id == P_LFO1 + LF_DEPTH);
    knob(0, 2);                                      /* KNOB 1: slot 3 (no pick: not a parameter) */
    knob(3, 1);
    {
        char b[16];
        ml_name(b);
        check("DEST, slot 3: its DEPTH, named T3 D3.DEPTH", ui.ml.id == P_LS3DEPTH && same(b, "T3 D3.DEPTH"));
    }
    knob(1, 1);
    check("DEST, slot 3: its SRC", ui.ml.id == P_LS3SRC);
    tap(B_FX);
    for (k = 0; k < 6u && cur_page()->graph != GR_FX; k++)
        tap(B_FX);
    knob(2, 1);
    check("FX: DLY (the track's send)", ui.ml.id == P_DLY && ui.ml.trk == 2u);
    for (k = 0; k < 8u && page_id(cur_page(), 3) != P_RKEY; k++)
        tap(B_FX);
    knob(3, 1);
    {
        char b[16];
        ml_name(b);
        check("RESON 2/2: KNOB 4 is KEYF, named T3 R.KEYF", ui.ml.id == P_RKEY && ui.ml.trk == 2u && same(b, "T3 R.KEYF"));
    }
    tap(B_HOME);
    tap(B_HOME);
    if (!mix_mode())
        tap(B_HOME);
    knob(1, 1);
    check("HOME's TRACKS mode: LEVEL", mix_mode() && ui.ml.pick && ui.ml.id == P_LEVEL && ui.ml.trk == 2u);
    knob(3, 1);
    check("TRACKS: PAN", ui.ml.id == P_PAN);
    knob(2, 1);
    check("TRACKS: LEN", ui.ml.id == P_SLEN);
    ui.ml.pick = 0;
    knob(0, 1);
    check("TRACKS KNOB 1 (TRACK): no pick", !ui.ml.pick && song.sel == 3u);
}

/* ---- a learned CC in the audio interrupt */
static void test_ranges(void)
{
    const uint32_t div = P_SDIV;
    fresh();
    learn_toggle();
    ml_learn(80, 0, P_PAN);
    ml_learn(81, 0, P_LFO1 + LF_DEPTH);
    ml_learn(82, 0, P_FTYPE);
    ml_learn(83, 0, div);
    ml_learn(84, 0, P_SLEN);
    send_cc(80, 0);
    check("PAN (bipolar): 0 -> -64", trk[0].p[P_PAN] == -64);
    send_cc(80, 64);
    check("PAN: 64 -> 0 (the middle)", trk[0].p[P_PAN] == 0);
    send_cc(80, 127);
    check("PAN: 127 -> 63", trk[0].p[P_PAN] == 63);
    send_cc(81, 64);
    check("LFO DEPTH (-64..64): 64 -> 0", trk[0].p[P_LFO1 + LF_DEPTH] == 0);
    send_cc(81, 63);
    check("LFO DEPTH: 63 -> -1", trk[0].p[P_LFO1 + LF_DEPTH] == -1);
    send_cc(81, 127);
    check("LFO DEPTH: 127 -> 64", trk[0].p[P_LFO1 + LF_DEPTH] == 64);
    send_cc(81, 0);
    check("LFO DEPTH: 0 -> -64", trk[0].p[P_LFO1 + LF_DEPTH] == -64);
    {
        uint32_t v, ok = 1, seen = 0;
        for (v = 0; v < 128u; v++) {
            send_cc(82, v);
            ok &= trk[0].p[P_FTYPE] >= 0 && trk[0].p[P_FTYPE] < FT_N && trk[0].p[P_FTYPE] >= (int16_t)(v * 4u / 127u) - 1;
            seen |= 1u << trk[0].p[P_FTYPE];
        }
        check("FILTER TYPE (an enum): every type reached, in order, by step", ok && seen == (1u << FT_N) - 1u &&
              trk[0].p[P_FTYPE] == FT_NOTCH);
    }
    send_cc(83, 0);
    check("DIV: 0 -> its first in the shown order (4BAR)", trk[0].p[div] == 9);
    send_cc(83, 127);
    check("DIV: 127 -> its last (1/32)", trk[0].p[div] == 3);
    send_cc(83, 64);
    check("DIV: 64 -> the middle of the shown order (1/8)", trk[0].p[div] == 1);
    send_cc(84, 127);
    check("LEN: 127 -> 64", trk[0].p[P_SLEN] == 64);
    send_cc(84, 0);
    check("LEN: 0 -> 1", trk[0].p[P_SLEN] == 1);
    chain.armed = 1;                                 /* (a song armed: chain_busy, as while it plays) */
    send_cc(84, 127);
    check("a song plays: LEN (PATTERN) stays", trk[0].p[P_SLEN] == 1);
    send_cc(80, 0);
    check("a song plays: PAN still moves", trk[0].p[P_PAN] == -64);
    chain.armed = 0;
}

static void test_bound_track(void)
{
    int16_t v5;
    fresh();
    track_select(1);
    learn_toggle();
    learn_home(2, 74);
    track_select(4);
    v5 = trk[4].p[P_E2];
    send_cc(74, 127);
    check("the CC moves the track it was learned on (T2), not the selected one (T5)",
          trk[1].p[P_E2] == track_desc(&trk[1], P_E2)->max && trk[4].p[P_E2] == v5 && cc_of(1, P_E2) == 75u &&
          !cc_of(4, P_E2));
}

static void test_lfo(void)
{
    track_t *t;
    int32_t mod;
    uint32_t i;
    fresh();
    t = &trk[0];
    ml_learn(80, 0, P_PAN);
    t->p[P_LFO1 + LF_DEST] = 10;                     /* PAN */
    t->p[P_LFO1 + LF_DEPTH] = 32;
    t->p[P_LFO1 + LF_WAVE] = LW_SQUARE;
    t->p[P_LFO1 + LF_MODE] = LM_HZ;
    t->p[P_LFO1 + LF_RATE] = 40;
    t->p[P_PAN] = 0;
    render_mix(0, 0, CTL * 8);
    check("(the LFO moves PAN: the block's copy off the base)", t->lval[0] != 0 && t->p[P_PAN] == 0);
    usb_cc(0, 80, 127);
    render_mix(0, 0, CTL);
    mod = t->lfo[0].out * 32 / 64 * 127 / 32767;
    check("a CC under an LFO: the base moves (63), the block's end does not undo it", t->p[P_PAN] == 63);
    check("the LFO modulates around the new base in that block", t->lval[0] == clamp(63 + mod, -64, 63) &&
          t->lsave[0] == 63);
    usb_cc(0, 80, 0);
    render_mix(0, 0, CTL);
    mod = t->lfo[0].out * 32 / 64 * 127 / 32767;
    check("again to 0 (-64): base -64, modulated from there", t->p[P_PAN] == -64 && t->lval[0] == clamp(-64 + mod, -64, 63));
    ml_learn(81, 0, P_LFO1 + LF_DEPTH);
    usb_cc(0, 81, 64);                               /* DEPTH 0: the LFO stops at once, PAN back to its base */
    render_mix(0, 0, CTL);
    check("a CC on the LFO's own DEPTH: in effect in that block, PAN at its base", t->p[P_LFO1 + LF_DEPTH] == 0 &&
          t->p[P_PAN] == -64);
    for (i = 0; i < 4u; i++)
        render_mix(0, 0, CTL);
    check("(and stays)", t->p[P_PAN] == -64);
    t->p[P_LFO1 + LF_DEPTH] = 32;                    /* (in a block: the LFO holds PAN) */
    render_mix(0, 0, CTL);
    lfo_apply(0);
    {
        int16_t mv = t->p[P_PAN], sv = mv == 5 ? 6 : 5;
        t->p[P_PAN] = sv;                            /* (what a restore and a re-apply would overwrite) */
        learn_cc(80, 0);                             /* -64: the base as it is */
        check("a CC to the value the base has: nothing done (no restore, no re-apply)", t->p[P_PAN] == sv &&
              t->lnum == 1u && t->lsave[0] == -64);
        t->p[P_PAN] = mv;
    }
    lfo_restore();
    t = &trk[1];                                     /* no LFO on */
    ml_learn(82, 1, P_PAN);
    lfo_apply(0);
    t->rfine = 77;                                   /* (lfo_track zeroes it) */
    learn_cc(82, 127);
    check("a CC on a track with no LFO on: set, the LFOs not re-applied", t->p[P_PAN] == 63 && t->rfine == 77);
    lfo_restore();
    t = &trk[2];                                     /* an LFO on PAN at DEPTH 0: a CC on its DEPTH turns it on */
    t->p[P_LFO1 + LF_DEST] = 10;
    t->p[P_LFO1 + LF_DEPTH] = 0;
    t->p[P_LFO1 + LF_WAVE] = LW_SQUARE;
    ml_learn(83, 2, P_LFO1 + LF_DEPTH);
    lfo_apply(0);
    learn_cc(83, 127);
    check("a CC on an off LFO's DEPTH: it holds PAN in that block", t->p[P_LFO1 + LF_DEPTH] == 64 && t->lnum == 1u &&
          t->lpid[0] == P_PAN);
    lfo_restore();
}

static void test_motion_base(void)
{
    track_t *t = &trk[0];
    fresh();
    ml_learn(80, 0, P_PAN);
    lk_put(&mo.b, lk_key(0, 4, P_PAN), 40, 0);
    mo.b.on = 1;
    transport_req = 1;
    render_mix(0, 0, CTL * 4);
    check("(motion plays: its base taken)", (mo.base_ok & 1u) && song.playing);
    usb_cc(0, 80, 0);
    render_mix(0, 0, CTL);
    check("a CC while motion plays: the knob and motion's base (-64)", t->p[P_PAN] == -64 && mo.base[0][P_PAN] == -64);
    transport_req = 2;
    render_mix(0, 0, CTL);
    check("STOP: the knob stays at the CC's value (the base), no motion event recorded", t->p[P_PAN] == -64 &&
          mo.b.count == 1u);
}

/* ---- the inputs */
static void test_inputs(void)
{
    int16_t m;
    fresh();
    ml_learn(80, 0, P_PAN);
    usb_cc(9, 80, 0);
    render_mix(0, 0, CTL);
    check("USB (channel 10, the drum channel): sets", trk[0].p[P_PAN] == -64);
    trs_cc(15, 80, 127);
    render_mix(0, 0, CTL);
    check("TRS (channel 16): sets", trk[0].p[P_PAN] == 63);
    btset.btmode = 2;
    ble_cc(0, 80, 0);
    render_mix(0, 0, CTL);
    check("Bluetooth, MIDI mode: sets", trk[0].p[P_PAN] == -64);
    btset.btmode = 1;
    m = trk[0].p[P_PAN];
    ble_cc(0, 80, 127);
    render_mix(0, 0, CTL);
    check("Bluetooth, SMC DAW mode: the SMC layout's, not learned", trk[0].p[P_PAN] == m);
    learn_toggle();
    knob(1, 1);
    ble_cc(0, 90, 10);
    frames(2);
    check("SMC DAW mode: binds nothing while learning", ui.ml.pick && !cc_of(0, P_E1));
    btset.btmode = 2;
    ble_cc(0, 90, 10);
    frames(2);
    check("Bluetooth MIDI mode: binds", cc_of(0, P_E1) == 91u);
    trs_cc(0, 91, 10);
    frames(1);
    check("(nothing picked: a CC not learned is ignored)", count() == 2u);
    knob(2, 1);
    trs_cc(4, 92, 10);
    frames(2);
    check("TRS: binds", cc_of(0, P_E2) == 93u);
}

/* ---- the flash record */
typedef struct {                                     /* version 1's record */
    uint32_t magic;
    uint16_t ver, n;
    ml_ent_t e[16];
} ml_rec1_t;
static ml_rec_t rec_read(int *ok)
{
    ml_rec_t r;
    memset(&r, 0, sizeof r);
    *ok = st_load(OBJ_UPRESET0, &r, sizeof r) == (int)sizeof r;
    return r;
}
static void power_cycle(void)
{
    memset(ml_map, 0xA5, sizeof ml_map);             /* RAM: anything */
    host_init();
    memset(&ui, 0, sizeof ui);
    persist_boot();
    settings_init();
    ui.home = ui.hmode = 1;                          /* (the HOME screen, as fresh) */
    ui.force = 1;
    frames(2);
}
static void test_storage(void)
{
    int ok;
    ml_rec_t r;
    fresh();
    learn_toggle();
    learn_home(1, 74);
    r = rec_read(&ok);
    check("a learn is saved: its own object (HLRN, version 2, 32 entries)", ok && r.magic == ML_MAGIC && r.ver == 2u &&
          r.n == 32u && r.e[0].num == 74u && r.e[0].trk == 0u && r.e[0].code == P_E1 + 1u && r.e[0].src == ML_CC);
    track_select(5);
    knob(3, 1);
    send_cc(75, 1);
    power_cycle();
    check("power cycle: the map back (CC74 T1, CC75 T6), LEARN off", !ui.ml.on && count() == 2u &&
          cc_of(0, P_E1) == 75u && cc_of(5, P_E3) == 76u);
    send_cc(75, 127);
    check("(and works)", trk[5].p[P_E3] == track_desc(&trk[5], P_E3)->max);
    track_select(5);
    transport_req = 1;
    render_mix(0, 0, CTL * 4);
    learn_toggle();
    knob(2, 1);
    send_cc(76, 1);
    r = rec_read(&ok);
    check("a learn while playing: in RAM at once, the flash waits", cc_of(5, P_E2) == 77u && ok && r.e[2].code == 0u);
    transport_req = 2;
    frames(3);
    r = rec_read(&ok);
    check("STOP: saved", ok && r.e[2].num == 76u && r.e[2].trk == 5u && r.e[2].code == P_E2 + 1u);
    {
        uint32_t seq0;
        st_hdr_t h;
        st_current(OBJ_UPRESET0, &h);
        seq0 = h.seq;
        learn_home(2, 76);                           /* the same again: no change */
        st_current(OBJ_UPRESET0, &h);
        check("a learn that changes nothing writes nothing", h.seq == seq0);
    }
    oct(B_OCTUP);
    learn_toggle();
    oct(B_OCTDN);
    oct(B_OCTUP);
    r = rec_read(&ok);
    check("clear all: saved empty", ok && r.magic == ML_MAGIC && !r.e[0].code && !r.e[1].code && !r.e[2].code);
    power_cycle();
    check("power cycle: nothing learned", !count());
    /* a blank flash, Felucca's presets in that object, a wrong version, broken entries */
    memset(hflash, 0xFF, sizeof hflash);
    power_cycle();
    check("a blank flash: nothing learned", !count());
    {
        uint8_t junk[2048];
        uint32_t i;
        for (i = 0; i < sizeof junk; i++)
            junk[i] = (uint8_t)(i * 37u + 11u);
        st_save(OBJ_UPRESET0, junk, sizeof junk);
        power_cycle();
        check("another record in the object (Felucca's presets): nothing learned", !count());
        memset(&r, 0, sizeof r);
        r.magic = ML_MAGIC;
        r.ver = 3;
        r.n = ML_N;
        r.e[0] = (ml_ent_t){74, 0, P_E1 + 1u, 0};
        st_save(OBJ_UPRESET0, &r, sizeof r);
        power_cycle();
        check("a later version: nothing learned", !count());
        {
            ml_rec1_t r1;                            /* version 1 (16 entries, 72 B) with broken entries */
            memset(&r1, 0, sizeof r1);
            r1.magic = ML_MAGIC;
            r1.ver = 1;
            r1.n = 16;
            r1.e[0] = (ml_ent_t){74, 0, P_E1 + 1u, 0};
            r1.e[1] = (ml_ent_t){0, 0, P_E2 + 1u, 0};    /* bank select */
            r1.e[2] = (ml_ent_t){75, 8, P_E2 + 1u, 0};   /* track 9 */
            r1.e[3] = (ml_ent_t){76, 0, P_COUNT + 1u, 0}; /* a parameter this build does not know */
            r1.e[4] = (ml_ent_t){77, 0, P_MODEL + 1u, 0}; /* MODEL */
            r1.e[5] = (ml_ent_t){74, 1, P_E3 + 1u, 0};   /* CC74 twice */
            r1.e[6] = (ml_ent_t){78, 0, P_E1 + 1u, 0};   /* T1's knob twice */
            r1.e[7] = (ml_ent_t){79, 2, P_PAN + 1u, 0};  /* good */
            r1.e[8] = (ml_ent_t){80, 0, P_E4 + 1u, 1};   /* (version 1: the reserved byte not 0) */
            st_save(OBJ_UPRESET0, &r1, sizeof r1);
            power_cycle();
            check("version 1: broken entries dropped, the good ones kept", count() == 2u && cc_of(0, P_E1) == 75u &&
                  cc_of(2, P_PAN) == 80u);
            r1.n = 17;
            st_save(OBJ_UPRESET0, &r1, sizeof r1);
            power_cycle();
            check("a wrong count: nothing learned", !count());
        }
        memset(&r, 0, sizeof r);                     /* version 2's broken entries */
        r.magic = ML_MAGIC;
        r.ver = 2;
        r.n = ML_N;
        r.e[0] = (ml_ent_t){36, 0, ML_A_PLAY, ML_NOTE};       /* good */
        r.e[1] = (ml_ent_t){37, 0, P_E1 + 1u, ML_NOTE};       /* a note on a parameter */
        r.e[2] = (ml_ent_t){38, 0, ML_A_MAC, ML_NOTE};        /* .. on a macro */
        r.e[3] = (ml_ent_t){39, 0, 0x84u, ML_NOTE};           /* an action this build does not know */
        r.e[4] = (ml_ent_t){40, 0, ML_A_REC, 2};              /* a source this build does not know */
        r.e[5] = (ml_ent_t){41, 3, ML_A_NEXT, ML_NOTE};       /* an action with a track */
        r.e[6] = (ml_ent_t){36, 0, ML_A_REC, ML_NOTE};        /* NOTE36 twice */
        r.e[7] = (ml_ent_t){20, 0, ML_A_PLAY, ML_CC};         /* PLAY twice */
        r.e[8] = (ml_ent_t){0, 0, ML_A_REC, ML_CC};           /* CC0 */
        r.e[9] = (ml_ent_t){36, 0, ML_A_MAC + 1u, ML_CC};     /* good: CC36 (not NOTE36) -> CRUSH */
        r.e[31] = (ml_ent_t){37, 0, ML_A_FX + PF_M1 + 7u, ML_NOTE};   /* good: the last entry, T8's mute */
        st_save(OBJ_UPRESET0, &r, sizeof r);
        power_cycle();
        check("version 2: broken entries dropped, the good ones kept (the 32nd too)", count() == 3u &&
              ml_src_of(0, ML_A_PLAY) == (ML_NOTE << 8 | 36u) + 1u && ml_src_of(0, ML_A_MAC + 1u) == 37u &&
              ml_src_of(0, ML_A_FX + PF_M1 + 7u) == (ML_NOTE << 8 | 37u) + 1u);
    }
    memset(hflash, 0xFF, sizeof hflash);
    learn_toggle();
    learn_home(1, 74);
    host_jedec = 0;                                  /* no flash found */
    power_cycle();
    check("no flash at boot: nothing learned", !count() && !flash_ok);
    learn_toggle();
    learn_home(1, 74);
    check("no flash: learning works (RAM), nothing written", count() == 1u && cc_of(0, P_E1) == 75u);
    usb_cc(0, 74, 0);
    frames(2);
    check("(and sets)", trk[0].p[P_E1] == track_desc(&trk[0], P_E1)->min);
    host_jedec = 0x856014u;
}

/* ---- the actions (spec §7) */
static void usb_note(uint32_t ch, uint32_t n, uint32_t v)   /* a note on (v > 0) / note on velocity 0 */
{
    midi_in_event(0x09u | (0x90u | ch) << 8 | n << 16 | v << 24);
}
static void usb_noff(uint32_t ch, uint32_t n) { midi_in_event(0x08u | (0x80u | ch) << 8 | n << 16 | 64u << 24); }
static void send_note(uint32_t n, uint32_t v)        /* USB, channel 1; then two frames */
{
    usb_note(0, n, v);
    frames(2);
}
#define SRC_NOTE(n) ((ML_NOTE << 8 | (n)) + 1u)          /* ml_src_of of a note */
static uint32_t fx_bit(void) { return 1u << panel.btn[B_FX]; }
static void fx_key(uint32_t key)                     /* FX held, a key tapped, FX let go (the PERFORM layer) */
{
    uint32_t i;
    press(B_FX);
    ui_frame();
    keys(1u << key);
    for (i = 0; i < 2u; i++) {
        fm1_in.buttons |= fx_bit();
        ui_frame();
    }
    keys(0);
    fm1_in.buttons |= fx_bit();
    ui_frame();
    release_all();
    ui_frame();
}
#define KEY_GS3 3u                                   /* G#3: the second black key, REPEAT 1/16 */
#define KEY_A3 4u                                    /* A3: the third white key, track 3's mute */
#define KEY_D4 10u                                   /* D#4: TAPE */
static void test_act_learn(void)
{
    char ln[32];
    uint32_t a;
    fresh();
    learn_toggle();
    ml_line(ln);
    check("LEARN on: the line LEARN: TURN OR PRESS", same(ln, "LEARN: TURN OR PRESS"));
    tap(B_PLAY);
    check("PLAY while learning: PLAY picked, nothing plays", ui.ml.pick && ui.ml.act == ML_A_PLAY && !song.playing &&
          !transport_req);
    ml_line(ln);
    check("the line: PLAY: SEND CC/NOTE", same(ln, "PLAY: SEND CC/NOTE"));
    ui.msg_t = 0;
    snap("act_play_send");
    a = hit_age(&trk[0]);
    usb_note(9, (uint32_t)trk[0].p[P_NOTE], 100);    /* (track 1's note, on the drum channel) */
    frames(2);
    {
        char want[24];
        snprintf(want, sizeof want, "NOTE%d -> PLAY", trk[0].p[P_NOTE]);
        check("a note binds it: NOTE<n> -> PLAY; the note played no drum, PLAY did nothing",
              ml_src_of(0, ML_A_PLAY) == SRC_NOTE((uint32_t)trk[0].p[P_NOTE]) && same(ui.msg, want) &&
              hit_age(&trk[0]) == a && !song.playing && !ui.ml.pick);
    }
    snap("act_play_learned");
    usb_noff(9, (uint32_t)trk[0].p[P_NOTE]);
    frames(2);
    check("(its note off after: nothing)", !song.playing && !ui.ml.pick);
    tap(B_REC);
    frames(1);
    check("REC while learning: REC picked, nothing armed", ui.ml.act == ML_A_REC && !song.rec && !song.playing);
    send_cc(20, 127);
    check("a CC binds it: CC20 -> REC", ml_src_of(0, ML_A_REC) == 21u && same(ui.msg, "CC20 -> REC") && !song.rec);
    send_cc(20, 0);
    turn(EN_ALGO, 1);
    ui_frame();
    check("ALGORITHM right while learning: NEXT TRACK picked, the track stays", ui.ml.act == ML_A_NEXT &&
          song.sel == 0u);
    send_cc(21, 127);
    check("CC21 -> NEXT TRACK", ml_src_of(0, ML_A_NEXT) == 22u && same(ui.msg, "CC21 -> NEXT TRACK") && !song.sel);
    send_cc(21, 0);
    turn(EN_ALGO, -1);
    ui_frame();
    check("ALGORITHM left: PREV TRACK picked", ui.ml.act == ML_A_PREV && song.sel == 0u);
    send_note(40, 90);
    check("NOTE40 -> PREV TRACK", ml_src_of(0, ML_A_PREV) == SRC_NOTE(40u) && same(ui.msg, "NOTE40 -> PREV TRACK"));
    usb_noff(0, 40);
    fx_key(KEY_GS3);
    ml_line(ln);
    check("FX held + G#3 while learning: FX R1/16 picked, the effect not played", ui.ml.act == ML_A_FX + PF_R16 &&
          !perf_held && !perf_act && same(ln, "FX R1/16: SEND CC/NOTE"));
    ui.msg_t = 0;
    snap("act_fx_send");
    send_cc(22, 127);
    check("CC22 -> FX R1/16 (bound: the effect not held by the learning press)", ml_src_of(0, ML_A_FX + PF_R16) == 23u &&
          same(ui.msg, "CC22 -> FX R1/16") && !perf_held);
    send_cc(22, 0);
    check("(its release after: nothing held)", !perf_held);
    fx_key(KEY_A3);
    check("FX held + A3: FX MUTE T3 picked, T3 not muted", ui.ml.act == ML_A_FX + PF_M1 + 2u && !perf_held);
    send_note(41, 100);
    check("NOTE41 -> FX MUTE T3", ml_src_of(0, ML_A_FX + PF_M1 + 2u) == SRC_NOTE(41u) &&
          same(ui.msg, "NOTE41 -> FX MUTE T3"));
    usb_noff(0, 41);
    frames(1);
    press(B_FX);
    ui_frame();
    fm1_in.buttons |= fx_bit();
    turn(EN_K1, 3);
    ui_frame();
    ml_line(ln);
    check("the PERFORM screen (FX held), KNOB 1: FX FILTER picked (and turned, as a knob is)", ui.ml.act == ML_A_MAC &&
          perf_k[0] > 0 && same(ln, "FX FILTER: SEND A CC"));
    ui.msg_t = 0;
    snap("act_macro_send");
    release_all();
    ui_frame();
    send_note(42, 100);
    check("a note does not bind a macro (a CC does)", ui.ml.pick && !ml_src_of(0, ML_A_MAC));
    usb_noff(0, 42);
    send_cc(23, 64);
    check("CC23 -> FX FILTER", ml_src_of(0, ML_A_MAC) == 24u && same(ui.msg, "CC23 -> FX FILTER"));
    perf_k[0] = 0;
    press(B_FX);
    ui_frame();
    fm1_in.buttons |= fx_bit();
    turn(EN_K3, 2);
    ui_frame();
    release_all();
    ui_frame();
    send_cc(24, 0);
    check("KNOB 3 on it: CC24 -> FX THROW", ml_src_of(0, ML_A_MAC + 2u) == 25u && same(ui.msg, "CC24 -> FX THROW"));
    perf_k[2] = 0;
    tap(B_PLAY);
    ml_line(ln);
    {
        char want[32];
        snprintf(want, sizeof want, "PLAY = NOTE%d", trk[0].p[P_NOTE]);
        check("PLAY again: the line PLAY = NOTE<n>", same(ln, want));
    }
    oct(B_OCTDN);
    check("OCT- with PLAY picked: its note cleared (PLAY CLEARED)", !ml_src_of(0, ML_A_PLAY) &&
          same(ui.msg, "PLAY CLEARED") && ui.ml.pick);
    ui.msg_t = 0;
    oct(B_OCTDN);
    check("OCT- again: NOTHING TO CLEAR", same(ui.msg, "NOTHING TO CLEAR"));
    send_note(36, 100);
    check("(a note binds it again)", ml_src_of(0, ML_A_PLAY) == SRC_NOTE(36u));
    usb_noff(0, 36);
    oct(B_OCTUP);
    check("OCT+: LEARN done", !ui.ml.on && !ml_keys);
    tap(B_PLAY);
    frames(1);
    check("OCT+ done: PLAY plays again", song.playing);
    tap(B_PLAY);
    frames(1);
    tap(B_REC);
    frames(1);
    check(".. REC arms again (HOME: REC ALL)", song.rec == 0xFFu && ui.rec_all);
    tap(B_REC);
    song.rec = 0;
    transport_req = 2;
    frames(1);
    turn(EN_ALGO, 1);
    ui_frame();
    check(".. ALGORITHM selects again", song.sel == 1u);
    fx_key(KEY_GS3);
    press(B_FX);
    ui_frame();
    keys(1u << KEY_GS3);
    fm1_in.buttons |= fx_bit();
    ui_frame();
    check(".. FX + G#3 holds REPEAT 1/16 again", (perf_held >> PF_R16) & 1u);
    keys(0);
    release_all();
    ui_frame();
    check("(let go: nothing held)", !perf_held);
}

/* bindings made directly: NOTE36 PLAY, CC20 REC, CC21 NEXT, NOTE40 PREV, CC22 R1/16, NOTE41 T3's mute, CC23 FILTER,
 * CC24 THROW, CC25 PLAY (a second test) */
static void act_map(void)
{
    ml_bind(ML_NOTE << 8 | 36u, 0, ML_A_PLAY);
    ml_bind(20, 0, ML_A_REC);
    ml_bind(21, 0, ML_A_NEXT);
    ml_bind(ML_NOTE << 8 | 40u, 0, ML_A_PREV);
    ml_bind(22, 0, ML_A_FX + PF_R16);
    ml_bind(ML_NOTE << 8 | 41u, 0, ML_A_FX + PF_M1 + 2u);
    ml_bind(23, 0, ML_A_MAC);
    ml_bind(24, 0, ML_A_MAC + 2u);
}
static void test_act_trigger(void)
{
    uint32_t a;
    fresh();
    act_map();
    check("(8 bound)", count() == 8u);
    send_note(36, 100);
    check("a learned note on: PLAY (plays)", song.playing);
    usb_noff(0, 36);
    frames(2);
    check("its note off: nothing", song.playing);
    usb_note(0, 36, 100);
    usb_note(0, 36, 0);                              /* (velocity 0: a note off) */
    frames(2);
    check("on again: PLAY (stops); velocity 0 is a note off", !song.playing);
    ml_bind(25, 0, ML_A_PLAY);
    check("PLAY learned to CC25: NOTE36 freed", ml_src_of(0, ML_A_PLAY) == 26u && count() == 8u);
    send_cc(25, 127);
    check("a learned CC >= 64: PLAY", song.playing);
    send_cc(25, 100);
    check("still >= 64: no edge, nothing", song.playing);
    send_cc(25, 10);
    check("< 64: let go, nothing", song.playing);
    send_cc(25, 64);
    check("64 again: PLAY (stops)", !song.playing);
    send_cc(25, 0);
    send_cc(20, 127);
    check("CC20 (REC) on HOME: REC ALL, as REC's tap there (every track armed, no play)", song.rec == 0xFFu &&
          ui.rec_all && same(ui.msg, "REC ALL") && !song.playing);
    send_cc(20, 0);
    send_cc(20, 127);
    check("again: REC ALL off, nothing armed (as before it)", !song.rec && !ui.rec_all);
    send_cc(20, 0);
    ui.home = 0;                                     /* TRACKS */
    ui.page = (uint8_t)page_first(FAM_MIX);
    page_entered();
    send_cc(20, 127);
    check("CC20 on TRACKS: the selected track armed (T1 REC ARMED)", song.rec == 1u && !ui.rec_all &&
          same(ui.msg, "T1 REC ARMED"));
    send_cc(20, 0);
    send_cc(20, 127);
    send_cc(20, 0);
    ui.home = 1;
    page_entered();
    transport_req = 2;
    frames(1);
    send_cc(21, 127);
    send_cc(21, 0);
    check("CC21 (NEXT TRACK): T2 selected", song.sel == 1u);
    send_cc(21, 127);
    check("NEXT again: T3", song.sel == 2u);
    send_cc(21, 0);
    send_note(40, 1);
    usb_noff(0, 40);
    check("NOTE40 (PREV TRACK): T2", song.sel == 1u);
    track_select(7);
    send_cc(21, 127);
    send_cc(21, 0);
    check("NEXT at T8: stays (clamped)", song.sel == 7u);
    track_select(0);
    send_note(40, 100);
    usb_noff(0, 40);
    check("PREV at T1: stays", song.sel == 0u);
    frames(1);
    send_cc(22, 127);
    check("CC22 >= 64: REPEAT 1/16 held (as its key in the layer, FX not held)", perf_held == PF_BIT(PF_R16) &&
          ((perf_act >> PF_R16) & 1u));
    send_cc(22, 0);
    check("CC22 < 64: let go", !perf_held);
    send_note(41, 100);
    check("NOTE41: T3 muted while held", perf_held == PF_BIT(PF_M1 + 2u));
    usb_noff(0, 41);
    frames(2);
    check("its note off: open again", !perf_held);
    send_cc(23, 0);
    check("CC23 0: FILTER -100 (the LPF end)", perf_k[0] == -100);
    send_cc(23, 64);
    check("CC23 64: FILTER 0 (off)", perf_k[0] == 0);
    send_cc(23, 63);
    check("CC23 63: just under (LPF)", perf_k[0] < 0 && perf_k[0] > -5);
    send_cc(23, 127);
    check("CC23 127: FILTER 100 (HPF)", perf_k[0] == 100);
    send_cc(24, 127);
    check("CC24 127: THROW 100", perf_k[2] == 100);
    send_cc(24, 0);
    check("CC24 0: THROW 0", perf_k[2] == 0);
    send_cc(23, 64);
    a = hit_age(&trk[0]);
    ml_bind(ML_NOTE << 8 | (uint32_t)trk[0].p[P_NOTE], 0, ML_A_REC);
    usb_note(9, (uint32_t)trk[0].p[P_NOTE], 100);
    frames(2);
    check("a learned note on the drum channel: its action (HOME: REC ALL), its drum does not play",
          hit_age(&trk[0]) == a && song.rec == 0xFFu);
    usb_noff(9, (uint32_t)trk[0].p[P_NOTE]);
    song.rec = 0;
    ui.rec_all = 0;
    transport_req = 2;
    frames(1);
    usb_note(9, (uint32_t)trk[1].p[P_NOTE], 100);
    frames(2);
    check("(a note not learned still plays its drum)", hit_age(&trk[1]) != 0u);
    ml_bind(ML_NOTE << 8 | 36u, 0, ML_A_REC);
    a = count();
    ml_bind(ML_NOTE << 8 | 36u, 0, ML_A_PLAY);
    check("a note is bound once: NOTE36 from REC to PLAY, REC left with none, PLAY's CC25 gone",
          ml_src_of(0, ML_A_PLAY) == SRC_NOTE(36u) && !ml_src_of(0, ML_A_REC) && count() == a - 1u);
    check("a note never binds a parameter", ml_bind(ML_NOTE << 8 | 50u, 0, P_E1 + 1u) == 2u &&
          ml_bind(ML_NOTE << 8 | 50u, 0, ML_A_MAC) == 2u && ml_bind(0, 0, ML_A_PLAY) == 2u &&
          ml_bind(120, 0, ML_A_REC) == 2u);
    learn_toggle();
    knob(1, 1);
    a = hit_age(&trk[0]);
    usb_note(9, (uint32_t)trk[0].p[P_NOTE] + 0u, 100);   /* (now REC's: test with another track's note) */
    usb_note(9, (uint32_t)trk[2].p[P_NOTE], 100);
    frames(2);
    check("a parameter picked: a note binds nothing and plays as usual", ui.ml.pick && !cc_of(0, P_E1) &&
          hit_age(&trk[2]) != 0u);
}

/* PERFORM held by MIDI renders as its key (perform_test style: the whole mix, bit for bit) */
#define ACT_B 512u                                  /* blocks rendered; the effect held from ACT_ON to ACT_OFF */
#define ACT_ON 64u
#define ACT_OFF 384u
static int32_t AL[CTL * ACT_B], AR[CTL * ACT_B], BL2[CTL * ACT_B], BR2[CTL * ACT_B];
static void act_kit(void)
{
    uint32_t k;
    for (k = 0; k < 16u; k++) {
        trk[0].step[k].on = (k & 3u) == 0u;
        trk[1].step[k].on = (k & 7u) == 4u;
        trk[2].step[k].on = (k & 1u) == 0u;
    }
    song.g[G_BPM] = 120;
}
static void act_render(int midi, uint32_t key, uint32_t src, int32_t *l, int32_t *r)
{
    uint32_t b;
    fresh();
    act_kit();
    if (midi)
        ml_bind(src, 0, ML_A_FX + perf_key(key));
    transport_req = 1;
    for (b = 0; b < ACT_B; b++) {
        if (b == ACT_ON || b == ACT_OFF) {
            int on = b == ACT_ON;
            if (!midi) {
                perf_mask = fx_bit();
                fm1_in.buttons = on ? fx_bit() : 0u;
                fm1_in.notes = on ? 1u << key : 0u;
            } else if (src >> 8) {
                if (on)
                    usb_note(0, src & 127u, 100);
                else
                    usb_noff(0, src & 127u);
            } else {
                usb_cc(0, src, on ? 127u : 0u);
            }
        }
        render_mix(l + b * CTL, r + b * CTL, CTL);
    }
}
static void test_act_render(void)
{
    static const uint8_t KEY[3] = {KEY_GS3, KEY_A3, KEY_D4};
    static const char *const WHAT[3] = {"REPEAT 1/16 held by a CC renders bit for bit as its key",
                                        "T3's mute held by a note: as its key", "TAPE held by a CC: as its key"};
    uint32_t i;
    for (i = 0; i < 3u; i++) {
        act_render(0, KEY[i], 0, AL, AR);
        act_render(1, KEY[i], i == 1u ? ML_NOTE << 8 | 41u : 22u, BL2, BR2);
        check(WHAT[i], !memcmp(AL, BL2, sizeof AL) && !memcmp(AR, BR2, sizeof AR));
    }
    {
        double e = 0;
        uint32_t j;
        act_render(0, KEY[0], 0, AL, AR);
        fresh();
        act_kit();
        transport_req = 1;
        render_mix(BL2, BR2, CTL * ACT_B);
        for (j = ACT_ON * CTL; j < ACT_OFF * CTL; j++)
            e += fabs((double)AL[j] - BL2[j]);
        check("(and the effect changes the mix)", e > 0);
    }
}

/* no effect stuck: let go, the menu, a confirm, NAME, the binding cleared while held; key and MIDI together */
static void test_act_stuck(void)
{
    fresh();
    act_map();
    send_cc(22, 127);
    check("(R1/16 held by CC22)", perf_held == PF_BIT(PF_R16));
    hold(B_HOME);
    check("the menu opens: the MIDI-held effect ends", ui.menu && !perf_mh && !perf_held);
    hold(B_HOME);
    frames(2);
    check("the menu closed: it does not come back (the controller still down)", !ui.menu && !perf_held);
    send_cc(22, 0);
    check("(its release: nothing)", !perf_held);
    send_cc(22, 127);
    check("pressed again: held again", perf_held == PF_BIT(PF_R16));
    send_note(41, 100);
    tap(B_SEQ);
    hold(B_REC);
    check("a confirm (CLEAR TRACK?): both end", ui.confirm == 1u && !perf_held);
    oct(B_OCTDN);
    frames(2);
    send_cc(22, 0);
    usb_noff(0, 41);
    frames(2);
    check("(the confirm gone, released: nothing)", !perf_held);
    send_cc(22, 127);
    press(B_FX);
    ui_frame();
    keys(1u << KEY_GS3);
    fm1_in.buttons |= fx_bit();
    ui_frame();
    keys(0);
    fm1_in.buttons |= fx_bit();
    ui_frame();
    release_all();
    ui_frame();
    check("held by CC22 and its key: the key let go, the CC still holds it", perf_held == PF_BIT(PF_R16));
    send_cc(22, 0);
    check("the CC let go: ended", !perf_held);
    learn_toggle();                                  /* (GLO held: the MUTE screen ends PERFORM's effects) */
    send_cc(22, 0);
    send_cc(22, 127);
    fx_key(KEY_GS3);
    check("(learning: FX R1/16 picked, still held by CC22)", ui.ml.act == ML_A_FX + PF_R16 &&
          perf_held == PF_BIT(PF_R16));
    oct(B_OCTDN);
    check("its binding cleared while held: the effect ends", !ml_src_of(0, ML_A_FX + PF_R16) && !perf_held);
    oct(B_OCTUP);
    send_cc(22, 0);
    send_cc(22, 127);
    check("(CC22 now nothing)", !perf_held);
    ml_bind(22, 0, ML_A_FX + PF_R16);
    send_cc(22, 127);
    ml_bind(22, 0, ML_A_FX + PF_TAPE);
    check("CC22 learned again (to TAPE) while R1/16 held: R1/16 ends, TAPE waits for a press", !perf_held);
    send_cc(22, 0);
    send_cc(22, 127);
    check("(pressed: TAPE)", perf_held == PF_BIT(PF_TAPE));
    send_cc(22, 0);
    send_cc(22, 127);
    project_save(0);
    for (perf_kill = 0; cur_page()->graph != GR_SLOTS;)
        tap(B_SAVE);
    knob(1, 1);
    frames(1);
    check("NAME opens: ended", name_on() && !perf_held);
    name_close();
    frames(2);
    send_cc(22, 0);
    check("(released after: nothing)", !perf_held);
}

/* a version 1 record (the first MIDI LEARN) loads into version 2, and the next save writes version 2 */
static void test_v1_load(void)
{
    ml_rec1_t r1;
    ml_rec_t r;
    int ok;
    fresh();
    memset(&r1, 0, sizeof r1);
    r1.magic = ML_MAGIC;
    r1.ver = 1;
    r1.n = 16;
    r1.e[0] = (ml_ent_t){74, 0, P_E1 + 1u, 0};
    r1.e[15] = (ml_ent_t){75, 5, P_PAN + 1u, 0};
    st_save(OBJ_UPRESET0, &r1, sizeof r1);
    power_cycle();
    check("version 1 (72 B, 16 entries): its CCs loaded", count() == 2u && cc_of(0, P_E1) == 75u &&
          cc_of(5, P_PAN) == 76u);
    send_cc(75, 0);
    check("(and set)", trk[5].p[P_PAN] == -64);
    learn_toggle();
    tap(B_PLAY);
    send_note(36, 100);
    r = rec_read(&ok);
    check("a learn after: saved as version 2 (32 entries), the old CCs kept", ok && r.ver == 2u && r.n == 32u &&
          r.e[0].num == 74u && r.e[1].num == 75u && r.e[1].trk == 5u && r.e[2].code == ML_A_PLAY &&
          r.e[2].src == ML_NOTE && r.e[2].num == 36u);
    power_cycle();
    check("power cycle: all three back", count() == 3u && ml_src_of(0, ML_A_PLAY) == SRC_NOTE(36u));
}

static void test_screens(void)
{
    fresh();
    tap(B_EDIT);
    learn_toggle();
    ui.msg_t = 0;
    snap("sound_turn_a_knob");
    knob(2, 1);
    ui.msg_t = 0;
    snap("sound_send_a_cc");
    send_cc(74, 30);
    snap("sound_learned_msg");
    track_select(7);
    tap(B_SEQ);
    tap(B_SEQ);                                      /* PATTERN */
    knob(1, 1);
    ui.msg_t = 0;
    snap("pattern_send_a_cc");
    {
        uint32_t k;
        for (k = 0; k < 3u && !mix_mode(); k++)
            tap(B_HOME);
    }
    knob(3, 1);
    send_cc(75, 64);
    knob(3, 1);
    ui.msg_t = 0;
    snap("tracks_learned_line");
    oct(B_OCTUP);
    tap(B_LFO);
    tap(B_LFO);                                      /* LFO 2/4: MORPH PHASE of LFO 1, of LFO 2 */
    learn_toggle();
    knob(1, 1);
    {
        char ln[32];
        ml_line(ln);
        check("an LFO's knob is named with its LFO: T8 L1.PHASE: SEND A CC", same(ln, "T8 L1.PHASE: SEND A CC"));
    }
    ui.msg_t = 0;
    snap("lfo_long_line");
    send_cc(76, 0);
    check("(the top bar: CC76 -> T8 L1.PHASE)", same(ui.msg, "CC76 -> T8 L1.PHASE"));
    snap("lfo_learned_msg");
}

int main(void)
{
    test_flow();
    test_rules();
    test_full();
    test_clear();
    test_ends();
    test_model_drop();
    test_pages();
    test_ranges();
    test_bound_track();
    test_lfo();
    test_motion_base();
    test_inputs();
    test_storage();
    test_act_learn();
    test_act_trigger();
    test_act_render();
    test_act_stuck();
    test_v1_load();
    test_screens();
    printf(fails ? "learn_test: %d FAILED\n" : "learn_test: all passed\n", fails);
    return fails ? 1 : 0;
}
