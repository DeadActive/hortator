/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The SMC-Mixer DAW layout (firmware/src/smc.c, spec 2026-10-10 §5) on the host, built with FELUCCA_BLE=1 and the UI
 * harness: the decode, each control's action, fader pickup, the LED and position messages (only changes, all of them
 * on a new link, a refused send retried). ble_midi_send is a stub that records what is sent and refuses on demand. */
#include "ui_host.h"
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#include "ble_ui_stub.h"                            /* ble_core.h, the connected name (conn_name / conn_up) */
static uint8_t sent[256][3];                          /* the messages ble_midi_send took */
static uint32_t nsent, tries;
static int32_t accept_left = -1;                      /* this many more taken, then refused (-1: all taken) */
int ble_midi_send(const uint8_t *m, uint32_t n)
{
    tries++;
    if (!accept_left || n != 3u || nsent >= 256u)
        return 0;
    if (accept_left > 0)
        accept_left--;
    memcpy(sent[nsent++], m, 3);
    return 1;
}
#include "../firmware/src/smc.c"
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_page.c"                  /* (ui_menu.c's Bluetooth row needs it) */

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t note(uint32_t n, uint32_t vel) { return 0x09u | 0x90u << 8 | n << 16 | vel << 24; }
static uint32_t note_off(uint32_t n) { return 0x08u | 0x80u << 8 | n << 16 | 64u << 24; }
static uint32_t bend(uint32_t ch, uint32_t v) { return 0x0Eu | (0xE0u | ch) << 8 | 0x5Au << 16 | v << 24; }   /* (the LSB: noise) */
static uint32_t cc(uint32_t n, uint32_t v) { return 0x0Bu | 0xB0u << 8 | n << 16 | v << 24; }
static void in(uint32_t pkt) { bt_midi_in(pkt); }    /* as ble_service hands it (core_ble_midi_in) */
static void clear_sent(void) { nsent = tries = 0; }
static uint32_t count_sent(uint8_t st)
{
    uint32_t i, n = 0;
    for (i = 0; i < nsent; i++)
        n += (sent[i][0] & 0xF0u) == st;
    return n;
}
static int was_sent(uint8_t a, uint8_t b, uint8_t c)
{
    uint32_t i;
    for (i = 0; i < nsent; i++)
        if (sent[i][0] == a && sent[i][1] == b && sent[i][2] == c)
            return 1;
    return 0;
}
static void link_up(void)                            /* (ble_service: core_ble_link(1)) */
{
    memset(conn_name, 0, sizeof conn_name);
    str_cpy(conn_name, "SMC-Mixer", sizeof conn_name);
    conn_up = 1;
    bt_link(1);
    smc_link(1);
}
static void link_down(void)
{
    conn_up = 0;
    bt_link(0);
    smc_link(0);
}
static void fresh(void)                              /* AUTO, linked to an SMC-Mixer, the first frame sent */
{
    ui_host_init();
    btset.btmode = 0;
    accept_left = -1;
    link_up();
    smc_frame();
    clear_sent();
}
static int has_event(uint32_t k, uint32_t id, int v)
{
    uint32_t i;
    for (i = 0; i < mo.s.count; i++)
        if (mo.s.ev[i].trk == k && mo.s.ev[i].param == id && mo.s.ev[i].value == v)
            return 1;
    return 0;
}

static void test_decode(void)
{
    uint32_t i;
    int16_t lv[NTRK];
    fresh();
    trk[2].p[P_LEVEL] = 100;
    smc_frame();
    in(bend(2, 99));
    check("pitch bend ch 3: (d2 << 7 | d1) >> 7 = 99, within 2 of LEVEL 100: picked, LEVEL 99", trk[2].p[P_LEVEL] == 99);
    for (i = 0; i < NTRK; i++)
        lv[i] = trk[i].p[P_LEVEL];
    in(bend(8, 0));
    in(bend(15, 0));
    for (i = 0; i < NTRK && trk[i].p[P_LEVEL] == lv[i]; i++)
        ;
    check("pitch bend ch 9 (master), ch 16: nothing", i == NTRK);
    in(0x0Eu | 0xE2u << 8 | 0x7Fu << 16 | 0x7Fu << 24);
    check("pitch bend 0x3FFF: 127", trk[2].p[P_LEVEL] == 127);
}

static void test_pickup(void)
{
    fresh();
    trk[2].p[P_LEVEL] = 100;
    smc_frame();
    clear_sent();
    in(bend(2, 10));
    check("fader 3 far from LEVEL 100: no change", trk[2].p[P_LEVEL] == 100);
    in(bend(2, 60));
    check("fader 3 moved, still below 100: no change", trk[2].p[P_LEVEL] == 100);
    in(bend(2, 98));
    check("fader 3 within 2 of 100: picked up, LEVEL 98", trk[2].p[P_LEVEL] == 98);
    in(bend(2, 40));
    check("picked: LEVEL follows the fader (40)", trk[2].p[P_LEVEL] == 40);
    smc_frame();
    check("a LEVEL the fader set: its latest position echoed once (E2 40 40), as smc-seq", nsent == 1u &&
          was_sent(0xE2u, 40u, 40u));
    in(bend(2, 20));
    check(".. the fader stays picked (20)", trk[2].p[P_LEVEL] == 20);

    fresh();
    trk[5].p[P_LEVEL] = 100;
    smc_frame();
    in(bend(5, 10));
    in(bend(5, 105));
    check("fader 6 crossed LEVEL 100 on its way up: picked, LEVEL 105", trk[5].p[P_LEVEL] == 105);
    fresh();
    trk[5].p[P_LEVEL] = 100;
    smc_frame();
    in(bend(5, 127));
    in(bend(5, 90));
    check("fader 6 crossed LEVEL 100 from above: picked, LEVEL 90", trk[5].p[P_LEVEL] == 90);
    fresh();
    trk[5].p[P_LEVEL] = 100;
    smc_frame();
    in(bend(5, 127));
    check("the first report after a link, far: no change (no crossing known)", trk[5].p[P_LEVEL] == 100);

    fresh();                                         /* the FM-1 changes LEVEL: unpicked, its position sent */
    in(bend(2, (uint32_t)trk[2].p[P_LEVEL]));
    in(bend(2, 50));
    trk[2].p[P_LEVEL] = 90;                          /* (as TRACKS' KNOB 2) */
    clear_sent();
    smc_frame();
    check("a LEVEL change by the FM-1: E2 90 90 sent", nsent == 1u && was_sent(0xE2u, 90u, 90u));
    in(bend(2, 52));
    check(".. and fader 3 is unpicked again (52: no change)", trk[2].p[P_LEVEL] == 90);
    in(bend(2, 91));
    check(".. until it comes within 2 (91)", trk[2].p[P_LEVEL] == 91);
    clear_sent();
    smc_frame();
    check(".. picked: only the echo (E2 91 91)", nsent == 1u && was_sent(0xE2u, 91u, 91u));

    fresh();                                         /* motion: the selected, armed track while playing */
    song.sel = 2;
    song.rec = 1u << 2;
    transport_req = 1;
    render_mix(0, 0, CTL);
    in(bend(2, (uint32_t)trk[2].p[P_LEVEL]));
    in(bend(2, 70));
    check("fader 3, track 3 selected, armed, playing: LEVEL 70 recorded as motion", trk[2].p[P_LEVEL] == 70 &&
          has_event(2, P_LEVEL, 70));
    in(bend(3, (uint32_t)trk[3].p[P_LEVEL]));
    in(bend(3, 60));
    check("fader 4 (not selected): LEVEL 60, not recorded", trk[3].p[P_LEVEL] == 60 && !has_event(3, P_LEVEL, 60));
}

static void test_knobs(void)
{
    const param_desc_t *d;
    char want[24], val[8];
    const char *unit;
    int16_t v0, before[P_COUNT];
    fresh();
    drum_set_model(&trk[0], DM_K808);
    v0 = trk[0].p[P_E0];
    in(cc(16, 3));
    d = track_desc(&trk[0], P_E0);
    check("knob 1 +3 on a K808 track: TUNE 3 steps up", trk[0].p[P_E0] == param_turn(d, v0, 3) && trk[0].p[P_E0] == v0 + 3);
    param_format(d, trk[0].p[P_E0], val, &unit);
    str_cpy(want, "T1 TUNE ", sizeof want);
    str_cpy(want + str_len(want), val, sizeof want - str_len(want));
    str_cpy(want + str_len(want), unit, sizeof want - str_len(want));
    check("knob 1: the top bar says T1 TUNE <value><unit>", str_eq(ui.msg, want));
    v0 = trk[0].p[P_E1];
    in(cc(17, 0x41));
    check("knob 2 counter-clockwise 1 (0x41): DECAY 1 down", trk[0].p[P_E1] == v0 - 1);
    trk[0].p[P_E1] = 127;
    in(cc(17, 10));
    check("knob 2 clamped at the top", trk[0].p[P_E1] == 127);
    drum_set_model(&trk[1], DM_ACID);
    track_select(1);
    v0 = trk[1].p[P_E2];
    in(cc(18, 2));
    check("an ACID track's knob 3 = CUTOF, 2 up", trk[1].p[P_E2] == v0 + 2 && str_eq(ui.msg, "T2 CUTOF 42"));
    v0 = trk[1].p[P_E3];
    in(cc(19, 0x45));
    check("knob 4 = RESO, 5 down", trk[1].p[P_E3] == v0 - 5);
    memcpy(before, trk[1].p, sizeof before);
    ui.msg[0] = 0;
    in(cc(20, 5));
    in(cc(23, 0x41));
    in(cc(16, 0));
    in(cc(16, 0x40));
    in(0x0Bu | 0xB1u << 8 | 16u << 16 | 5u << 24);   /* CC 16 on channel 2 */
    check("knobs 5-8, a zero delta, another channel: nothing", !memcmp(before, trk[1].p, sizeof before) && !ui.msg[0]);
    song.rec = 1u << 1;
    transport_req = 1;
    render_mix(0, 0, CTL);
    in(cc(17, 4));
    check("knob 2 on the selected, armed track while playing: recorded as motion", has_event(1, P_E1, trk[1].p[P_E1]));
}

static void test_buttons(void)
{
    fresh();
    in(note(0x13, 127));
    check("MUTE 4: track 4 muted", trk[3].p[P_MUTE] == 1);
    in(note(0x13, 127));
    check("MUTE 4 again: unmuted", trk[3].p[P_MUTE] == 0);
    settings.mutebar = 1;
    in(note(0x13, 127));
    check("MUTE 4 with MUTE: NEXT BAR: queued (mute_q bit 3), P_MUTE as it was", song.mute_q == 8u && !trk[3].p[P_MUTE]);
    settings.mutebar = 0;
    song.mute_q = 0;
    in(note(0x09, 100));
    check("SOLO 2: song.solo bit 1", song.solo == 2u);
    in(note(0x09, 100));
    check("SOLO 2 again: off", !song.solo);
    in(note(0x04, 1));
    check("REC 5 while stopped: track 5 armed, play requested", song.rec == 16u && transport_req == 1u);
    transport_req = 0;
    in(note(0x04, 1));
    check("REC 5 again: disarmed, no request", !song.rec && !transport_req);
    in(note(0x1E, 127));
    check("SELECT 7: track 7 selected", song.sel == 6u);
    in(note(0x2F, 127));
    check("BANK >: track 8", song.sel == 7u);
    in(note(0x31, 127));
    check("CHANNEL > at track 8: stays", song.sel == 7u);
    track_select(0);
    in(note(0x2E, 127));
    in(note(0x30, 127));
    check("BANK < / CHANNEL < at track 1: stays", song.sel == 0u);
    in(note(0x31, 127));
    in(note(0x31, 127));
    in(note(0x30, 127));
    check("CHANNEL > > <: track 2", song.sel == 1u);
    in(note(0x5F, 127));
    check("REC: the selected track (2) armed, play requested", song.rec == 2u && transport_req == 1u);
    transport_req = 0;
    in(note(0x5F, 127));
    check("REC again: disarmed", !song.rec);
    chain.armed = 1;                                 /* a song armed (PLAY on SONG): REC as on TRACKS */
    ui.msg[0] = 0;
    in(note(0x02, 127));
    check("REC 3 while a song is armed: nothing armed, STOP TO EDIT", !song.rec && str_eq(ui.msg, "STOP TO EDIT"));
    ui.msg[0] = 0;
    in(note(0x5F, 127));
    check("REC while a song is armed: nothing armed, STOP TO EDIT", !song.rec && str_eq(ui.msg, "STOP TO EDIT") &&
          !transport_req);
    chain.running = 1;
    chain.armed = 0;
    ui.msg[0] = 0;
    in(note(0x02, 127));
    in(note(0x5F, 127));
    check("REC 3 / REC while a song runs: nothing armed, STOP TO EDIT", !song.rec && str_eq(ui.msg, "STOP TO EDIT"));
    chain.running = 0;
    in(note(0x5D, 127));
    check("STOP while stopped: nothing", !transport_req);
    in(note(0x5E, 127));
    check("PLAY while stopped: play requested", transport_req == 1u);
    render_mix(0, 0, CTL);
    in(note(0x5E, 127));
    check("PLAY while playing: nothing", song.playing && !transport_req);
    in(note(0x5D, 127));
    check("STOP while playing: stop requested", transport_req == 2u);
    render_mix(0, 0, CTL);
    check("(stopped)", !song.playing);
    in(note_off(0x13));
    in(note(0x13, 0));
    in(note_off(0x5E));
    in(note(0x5E, 0));
    in(note(0x18 + 3, 0));
    check("note-offs and velocity 0: nothing", !trk[3].p[P_MUTE] && !transport_req && song.sel == 1u);
    in(note(0x5B, 127));
    in(note(0x5C, 127));
    in(note(0x60, 127));
    in(note(0x63, 127));
    check("REW, FF, arrows: nothing", !transport_req && song.sel == 1u && !song.rec);
    in(0x09u | 0x91u << 8 | 0x13u << 16 | 127u << 24);
    check("a note on channel 2: nothing", !trk[3].p[P_MUTE]);
}

static void test_leds(void)
{
    uint32_t i;
    ui_host_init();
    btset.btmode = 0;
    accept_left = -1;
    clear_sent();
    smc_frame();
    check("not linked: nothing sent", !tries);
    link_up();
    check("link up: BT CONNECTED", str_eq(ui.msg, "BT CONNECTED"));
    smc_frame();
    check("the first frame after a link: 32 + 3 LEDs and 8 positions", count_sent(0x90u) == 35u && count_sent(0xE0u) == 8u &&
          nsent == 43u);
    for (i = 0; i < 8u; i++)
        if (!was_sent((uint8_t)(0xE0u + i), (uint8_t)trk[i].p[P_LEVEL], (uint8_t)trk[i].p[P_LEVEL]))
            break;
    check(".. each position its track's LEVEL (E0+n v v)", i == 8u);
    check(".. SELECT 1 lit, SELECT 2 dark, STOP lit, PLAY dark, REC dark, MUTE 1 dark",
          was_sent(0x90, 0x18, 127) && was_sent(0x90, 0x19, 0) && was_sent(0x90, 0x5D, 127) && was_sent(0x90, 0x5E, 0) &&
          was_sent(0x90, 0x5F, 0) && was_sent(0x90, 0x10, 0));
    clear_sent();
    smc_frame();
    smc_frame();
    check("nothing changed: nothing sent", !tries);
    in(note(0x13, 127));
    clear_sent();
    smc_frame();
    check("a mute: exactly one LED change (MUTE 4 lit)", nsent == 1u && was_sent(0x90, 0x13, 127));
    settings.mutebar = 1;
    in(note(0x14, 127));
    in(note(0x13, 127));
    clear_sent();
    smc_frame();
    check("NEXT BAR: MUTE 5 lit while its mute waits, MUTE 4 dark while its unmute waits",
          nsent == 2u && was_sent(0x90, 0x14, 127) && was_sent(0x90, 0x13, 0));
    render_mix(0, 0, CTL);                           /* stopped: the queue is applied at once */
    clear_sent();
    smc_frame();
    check(".. applied: nothing changes on the mixer", !tries && trk[4].p[P_MUTE] && !trk[3].p[P_MUTE]);
    settings.mutebar = 0;
    in(note(0x0A, 127));
    in(note(0x1A, 127));
    in(note(0x5F, 127));
    clear_sent();
    smc_frame();
    check("SOLO 3, SELECT 3, REC: SOLO 3, SELECT 3 lit, SELECT 1 dark, REC 3 lit, REC lit",
          nsent == 5u && was_sent(0x90, 0x0A, 127) && was_sent(0x90, 0x1A, 127) && was_sent(0x90, 0x18, 0) &&
          was_sent(0x90, 0x02, 127) && was_sent(0x90, 0x5F, 127));
    render_mix(0, 0, CTL);                           /* (REC started play) */
    clear_sent();
    smc_frame();
    check("playing: PLAY lit, STOP dark", nsent == 2u && was_sent(0x90, 0x5E, 127) && was_sent(0x90, 0x5D, 0));
    transport_req = 2;
    render_mix(0, 0, CTL);
    smc_frame();

    in(note(0x15, 127));                             /* refused: retried */
    clear_sent();
    accept_left = 0;
    smc_frame();
    check("a refused send: tried, nothing taken", tries >= 1u && !nsent);
    accept_left = -1;
    smc_frame();
    check(".. retried on the next frame: MUTE 6 lit", nsent == 1u && was_sent(0x90, 0x15, 127));
    link_down();
    clear_sent();
    smc_frame();
    check("link down: BT LOST, nothing sent", str_eq(ui.msg, "BT LOST") && !tries);
    trk[1].p[P_LEVEL] = 33;
    link_up();
    accept_left = 20;
    smc_frame();
    check("a new link with room for 20: 20 sent", nsent == 20u);
    accept_left = -1;
    smc_frame();
    check(".. the rest on the next frame: 35 LEDs and 8 positions in all", count_sent(0x90u) == 35u && count_sent(0xE0u) == 8u &&
          nsent == 43u && was_sent(0xE1, 33, 33) && was_sent(0x90, 0x15, 127));
    in(bend(1, 120));
    check("a new link: the faders unpicked (fader 2 at 120 leaves LEVEL 33)", trk[1].p[P_LEVEL] == 33);
}

static void test_midi_mode(void)
{
    fresh();
    btset.btmode = 2;                                /* MODE MIDI with an SMC linked */
    in(note(0x13, 127));
    in(bend(2, (uint32_t)trk[2].p[P_LEVEL]));
    in(bend(2, 3));
    check("MIDI mode: smc_in is not called (MUTE 4, fader 3 do nothing)", !trk[3].p[P_MUTE] && trk[2].p[P_LEVEL] != 3);
    trk[2].p[P_LEVEL] = 11;
    clear_sent();
    smc_frame();
    check("MIDI mode: no LEDs, no positions", !tries);
    btset.btmode = 1;
    smc_frame();
    check("back to SMC: everything sent again", nsent == 43u && was_sent(0xE2, 11, 11));
}

int main(void)
{
    test_decode();
    test_pickup();
    test_knobs();
    test_buttons();
    test_leds();
    test_midi_mode();
    printf(fails ? "smc_test: %d FAILED\n" : "smc_test: all passed\n", fails);
    return fails ? 1 : 0;
}
