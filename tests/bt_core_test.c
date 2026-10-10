/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The core's side of BLE MIDI in (firmware/src/bt_midi.c, spec 2026-10-10 §4, §7) on the host, built with
 * FELUCCA_BLE=1 and the UI harness: MIDI mode -> the input ring as source 3 with the USB filter, CLK BLE, the room
 * kept for TRS, the mode (MODE, the connected name), SOLO's toggle. The BLE unit's entry points are stubs here. */
#include "ui_host.h"
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#define memset felucca_memset                         /* (ble_core.h declares libc.c's, as the firmware's) */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "../firmware/src/ble/ble_core.h"
#undef memset
#undef memcpy
#undef memcmp
static char conn_name[24];
static int conn_up;
int ble_ui_conn_name(char out[24])                   /* (the BLE unit: SUBSCRIBED / RECEIVING only) */
{
    if (!conn_up)
        return 0;
    memcpy(out, conn_name, 24);
    return 1;
}
#include "../firmware/src/bt_midi.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t note_on(uint32_t ch, uint32_t note, uint32_t vel) { return 0x09u | (0x90u | ch) << 8 | note << 16 | vel << 24; }
static int sounding(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NDV; i++)
        if (t->v[i].active)
            return 1;
    return 0;
}
static void link_to(const char *name)
{
    memset(conn_name, 0, sizeof conn_name);
    str_cpy(conn_name, name, sizeof conn_name);
    conn_up = 1;
    bt_link(1);                                      /* (ble_service: core_ble_link(1) once linked) */
}
static void fresh(uint32_t mode)                     /* MODE (0 AUTO 1 SMC 2 MIDI), nothing connected */
{
    ui_host_init();
    settings.btmode = mode;
    conn_up = 0;
    bt_link(0);
    memset(&bt_in, 0, sizeof bt_in);
}

static void test_midi_mode_notes(void)
{
    uint32_t ch, at;
    fresh(2);
    ch = (uint32_t)song.g[G_DRCH] - 1u;
    bt_midi_in(note_on(ch, (uint32_t)trk[3].p[P_NOTE], 100));
    at = mi_r % MQ;
    check("MIDI mode: a note-on lands in the input ring as source 3",
          mi_w - mi_r == 1u && midi_in_source[at] == 3u && midi_in_q[at] == note_on(ch, (uint32_t)trk[3].p[P_NOTE], 100));
    render_mix(0, 0, CTL);
    check("MIDI mode: a note-on on the drum channel fires the track with that NOTE", sounding(&trk[3]) && !sounding(&trk[2]));
    fresh(2);
    bt_midi_in(note_on((ch + 1u) & 15u, (uint32_t)trk[3].p[P_NOTE], 100));
    render_mix(0, 0, CTL);
    check("MIDI mode: a note on another channel fires nothing", !sounding(&trk[3]));
}

static void test_midi_filter(void)
{
    static const uint32_t BAD[] = {
        0x04u | 0xF0u << 8 | 0x41u << 16 | 0x10u << 24, /* SysEx (CIN 4) */
        0x05u | 0xF7u << 8,                           /* SysEx end (CIN 5) */
        0x0Fu | 0xFEu << 8,                           /* Active Sensing */
        0x0Fu | 0xFFu << 8,                           /* Reset */
        0x08u | 0x90u << 8 | 60u << 16 | 100u << 24, /* CIN 8 with a note-on status */
        0x09u | 0x99u << 8 | 0x80u << 16 | 100u << 24, /* data byte 1 >= 0x80 */
        0x09u | 0x99u << 8 | 60u << 16 | 0x80u << 24,  /* data byte 2 >= 0x80 */
        0x02u | 0xF3u << 8 | 1u << 16,                /* system common (Song Select) */
    };
    uint32_t i, n = 0;
    fresh(2);
    for (i = 0; i < sizeof BAD / sizeof BAD[0]; i++)
        bt_midi_in(BAD[i]);
    check("MIDI mode: the USB filter drops SysEx, active sensing, reset, a mismatched CIN, data >= 0x80, system common",
          mi_w == mi_r && !midi_in_overflow);
    bt_midi_in(0x0Cu | 0xC9u << 8 | 5u << 16 | 0x80u << 24);   /* program change: the third byte is not data */
    bt_midi_in(0x0Bu | 0xB9u << 8 | 7u << 16 | 64u << 24);     /* CC */
    bt_midi_in(0x0Fu | 0xF8u << 8 | 0x1234u << 16);             /* clock: kept as status only */
    n = mi_w - mi_r;
    check("MIDI mode: channel voice and the realtime F8 FA FB FC pass (realtime as its status only)",
          n == 3u && midi_in_q[(mi_r + 2u) % MQ] == (0x0Fu | 0xF8u << 8));
}

static void test_clock(void)
{
    fresh(2);
    song.g[G_CLOCK] = 3;
    render_mix(0, 0, CTL);                           /* (a CLK change resets the clock state first) */
    bt_midi_in(0x0Fu | 0xFAu << 8);
    bt_midi_in(0x0Fu | 0xF8u << 8);
    render_mix(0, 0, CTL);
    check("CLK BLE: a BLE Start and Clock drive the sequencer", song.playing && midi_clock.tempo_valid);
    fresh(2);
    song.g[G_CLOCK] = 1;
    render_mix(0, 0, CTL);
    bt_midi_in(0x0Fu | 0xFAu << 8);
    bt_midi_in(0x0Fu | 0xF8u << 8);
    render_mix(0, 0, CTL);
    check("CLK USB: a BLE Start and Clock do nothing", !song.playing && !midi_clock.tempo_valid);
    check("CLK: BLE is value 3 in this build", GP[G_CLOCK].max == 3 && str_eq(N_CLOCK[3], "BLE"));
    fresh(2);
    song.g[G_CLOCK] = 3;
    turn(EN_SELECT, 1);
    ui_input();
    check("CLK BLE: the BPM knob says CLK BLE", str_eq(ui.msg, "CLK BLE") && song.g[G_BPM] == GP[G_BPM].def);
    song.g[G_CLOCK] = 2;
    ui.msg[0] = 0;
    turn(EN_SELECT, 1);
    ui_input();
    check("CLK TRS: the BPM knob says CLK TRS", str_eq(ui.msg, "CLK TRS"));
    fresh(2);
    song.g[G_CLOCK] = 3;
    project_save(0);
    song.g[G_CLOCK] = 0;
    project_load(0);
    check("CLK BLE survives a project save and load in this build", song.g[G_CLOCK] == 3);
}

static void test_ring_room(void)
{
    uint32_t ch;
    fresh(2);
    ch = (uint32_t)song.g[G_DRCH] - 1u;
    mi_r = 1000u;
    mi_w = mi_r + MQ - 9u;                           /* 9 slots free: one goes to BLE */
    bt_midi_in(note_on(ch, 36, 100));
    check("ring: with 9 slots free a BLE packet goes in", mi_w - mi_r == MQ - 8u && !bt_in.drops);
    bt_midi_in(note_on(ch, 36, 100));                /* 8 free: TRS's */
    check("ring: with only 8 slots free the BLE packet is refused, counted, the overflow flag not set",
          mi_w - mi_r == MQ - 8u && !midi_in_overflow && bt_in.drops == 1u);
    mi_r = mi_w = 0;
}

static void test_mode(void)
{
    uint32_t ch;
    fresh(0);
    ch = (uint32_t)song.g[G_DRCH] - 1u;
    check("MODE AUTO, not connected: MIDI", bt_mode_now() == 0u);
    link_to("SMC-Mixer");
    check("MODE AUTO, connected to SMC-Mixer: SMC DAW", bt_mode_now() == 1u);
    bt_midi_in(note_on(ch, (uint32_t)trk[0].p[P_NOTE], 100));
    check("SMC DAW mode: nothing goes into the ring", mi_w == mi_r);
    link_to("nanoKEY Studio");
    check("MODE AUTO, connected to another name: MIDI", bt_mode_now() == 0u);
    link_to("SM");
    check("MODE AUTO, a name shorter than SMC: MIDI", bt_mode_now() == 0u);
    settings.btmode = 1;
    check("MODE SMC DAW: SMC whatever the name", bt_mode_now() == 1u);
    settings.btmode = 2;
    link_to("SMC-Mixer");
    check("MODE MIDI: MIDI with an SMC connected", bt_mode_now() == 0u);
    settings.btmode = 0;
    conn_up = 0;
    bt_link(0);
    check("MODE AUTO: the link down -> MIDI (the name's decision cleared)", bt_mode_now() == 0u);
    conn_up = 1;                                     /* linked again, before core_ble_link(1): not SMC yet */
    check("MODE AUTO: the name is read once, on core_ble_link(1)", bt_mode_now() == 0u);
    bt_link(1);
    check("MODE AUTO: .. then SMC DAW", bt_mode_now() == 1u);
}

static void test_solo_toggle(void)
{
    ui_host_init();
    trk[3].p[P_MUTE] = 1;
    panic_req = 0;
    track_solo_toggle(1);
    check("SOLO 2: song.solo bit 1; the tracks it silences (not the muted one) are cut",
          song.solo == 2u && panic_req == (0xFFu & ~(1u << 1) & ~(1u << 3)));
    panic_req = 0;
    track_solo_toggle(5);
    check("SOLO 6 too: nothing newly silent, nothing cut", song.solo == (2u | 32u) && !panic_req);
    track_solo_toggle(1);
    check("unsolo 2 with 6 soloed: track 2 now silent, cut", song.solo == 32u && panic_req == 2u);
    panic_req = 0;
    track_solo_toggle(5);
    check("unsolo 6: nothing cut, the mask clear", !song.solo && !panic_req);
    track_solo_toggle(NTRK);
    check("SOLO beyond the tracks: nothing", !song.solo);
    track_solo_toggle(2);
    init_all();
    check("INIT ALL clears SOLO", !song.solo);
}

int main(void)
{
    test_midi_mode_notes();
    test_midi_filter();
    test_clock();
    test_ring_room();
    test_mode();
    test_solo_toggle();
    printf(fails ? "bt_core_test: %d FAILED\n" : "bt_core_test: all passed\n", fails);
    return fails ? 1 : 0;
}
