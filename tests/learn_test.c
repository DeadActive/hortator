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
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
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
    check("the line: LEARN: TURN A KNOB", same(ln, "LEARN: TURN A KNOB"));
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
    check("the pick ends: TURN A KNOB again", !ui.ml.pick && !ml_arm);
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
    for (k = 0; k < 4u; k++) {                       /* 4 tracks x 4 HOME knobs = 16 */
        track_select(k);
        for (i = 0; i < 4u; i++)
            learn_home(i, 40u + k * 4u + i);   /* (CC40..55) */
    }
    if (!check("16 learned", count() == 16u))
        printf("    %u learned\n", (unsigned)count());
    track_select(4);
    knob(0, 1);
    ui.msg_t = 0;
    send_cc(60, 1);
    check("a 17th: LEARN FULL, nothing replaced, the pick stays", same(ui.msg, "LEARN FULL") && count() == 16u &&
          !cc_of(4, P_E0) && ui.ml.pick);
    send_cc(40, 1);                                  /* a learned CC: it moves here */
    check("full, a learned CC to another parameter: it moves", cc_of(4, P_E0) == 41u && !cc_of(0, P_E0) && count() == 16u);
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
    check("REC held on SEQ (CLEAR TRACK?): LEARN off", ui.confirm == 1u && !ui.ml.on);
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

/* the pages: each picks the parameter it edits on the selected track */
static void test_pages(void)
{
    uint32_t ok = 1, k;
    fresh();
    track_select(2);
    tap(B_LFO);
    learn_toggle();
    knob(3, 1);
    ok &= ui.ml.pick && ui.ml.trk == 2u && ui.ml.id == P_LFO1 + LF_DEPTH;
    check("LFO 1: DEPTH of the selected track (T3)", ok);
    oct(B_OCTUP);
    oct(B_OCTDN);                                    /* (LEARN off: OCT- is the page's) */
    learn_toggle();
    knob(3, 1);
    check("LFO 2 (OCT- with LEARN off): its DEPTH", ui.ml.id == P_LFO2 + LF_DEPTH);
    tap(B_FX);
    for (k = 0; k < 6u && cur_page()->graph != GR_FX; k++)
        tap(B_FX);
    knob(2, 1);
    check("FX: DLY (the track's send)", ui.ml.id == P_DLY && ui.ml.trk == 2u);
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
}

static void test_motion_base(void)
{
    track_t *t = &trk[0];
    fresh();
    ml_learn(80, 0, P_PAN);
    mo.s.ev[0] = (motion_event_t){0, 4, P_PAN, 40};
    mo.s.count = 1;
    mo.s.on = 1;
    transport_req = 1;
    render_mix(0, 0, CTL * 4);
    check("(motion plays: its base taken)", (mo.base_ok & 1u) && song.playing);
    usb_cc(0, 80, 0);
    render_mix(0, 0, CTL);
    check("a CC while motion plays: the knob and motion's base (-64)", t->p[P_PAN] == -64 && mo.base[0][P_PAN] == -64);
    transport_req = 2;
    render_mix(0, 0, CTL);
    check("STOP: the knob stays at the CC's value (the base), no motion event recorded", t->p[P_PAN] == -64 &&
          mo.s.count == 1u);
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
    check("a learn is saved: its own object (HLRN, version 1, 16 entries)", ok && r.magic == ML_MAGIC && r.ver == 1u &&
          r.n == ML_N && r.e[0].cc == 74u && r.e[0].trk == 0u && r.e[0].code == P_E1 + 1u);
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
    check("STOP: saved", ok && r.e[2].cc == 76u && r.e[2].trk == 5u && r.e[2].code == P_E2 + 1u);
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
        r.ver = 2;
        r.n = ML_N;
        r.e[0] = (ml_ent_t){74, 0, P_E1 + 1u, 0};
        st_save(OBJ_UPRESET0, &r, sizeof r);
        power_cycle();
        check("a later version: nothing learned", !count());
        r.ver = 1;
        r.e[1] = (ml_ent_t){0, 0, P_E2 + 1u, 0};    /* bank select */
        r.e[2] = (ml_ent_t){75, 8, P_E2 + 1u, 0};   /* track 9 */
        r.e[3] = (ml_ent_t){76, 0, P_COUNT + 1u, 0}; /* a parameter this build does not know */
        r.e[4] = (ml_ent_t){77, 0, P_MODEL + 1u, 0}; /* MODEL */
        r.e[5] = (ml_ent_t){74, 1, P_E3 + 1u, 0};   /* CC74 twice */
        r.e[6] = (ml_ent_t){78, 0, P_E1 + 1u, 0};   /* T1's knob twice */
        r.e[7] = (ml_ent_t){79, 2, P_PAN + 1u, 0};  /* good */
        st_save(OBJ_UPRESET0, &r, sizeof r);
        power_cycle();
        check("broken entries dropped, the good ones kept", count() == 2u && cc_of(0, P_E1) == 75u &&
              cc_of(2, P_PAN) == 80u);
        r.n = 17;
        st_save(OBJ_UPRESET0, &r, sizeof r);
        power_cycle();
        check("a wrong count: nothing learned", !count());
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
    tap(B_LFO);                                      /* LFO 2/2: DEST TRIG PHASE */
    learn_toggle();
    knob(2, 1);
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
    test_pages();
    test_ranges();
    test_bound_track();
    test_lfo();
    test_motion_base();
    test_inputs();
    test_storage();
    test_screens();
    printf(fails ? "learn_test: %d FAILED\n" : "learn_test: all passed\n", fails);
    return fails ? 1 : 0;
}
