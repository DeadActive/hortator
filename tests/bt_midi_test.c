/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* Bluetooth MIDI out, clock out, the release on a lost link and MODE FM-LINK (firmware/src/bt_midi.c, after
 * felucca-ble's tests/bt_midi_test.c) on the host, built with FELUCCA_BLE=1 and the UI harness: the keys' hits go to
 * the device in MIDI mode only (not SMC DAW, not FM-LINK, not unlinked), the clock out on CLK INT only (Start, 24
 * pulses a beat from the samples, Start again on a PLAY while playing, Stop), the device's held channels released when
 * the link drops, FM-LINK ignores what the device sends. (MIDI in: tests/bt_core_test.c.) */
#include "ui_host.h"
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#include "ble_ui_stub.h"
static uint8_t sent[512][3];
static uint32_t nsent, sent_len[512];
uint32_t ble_midi_room(void) { return 64u; }
int ble_midi_send(const uint8_t *m, uint32_t n)
{
    if (nsent >= 512u)
        return 0;
    memset(sent[nsent], 0, 3);
    memcpy(sent[nsent], m, n);
    sent_len[nsent++] = n;
    return 1;
}
#include "../firmware/src/bt_set.c"
static uint32_t smc_seen;
#define smc_in smc_in_real                            /* (the layout's own entry, renamed: this test counts the calls) */
#include "../firmware/src/smc.c"
#undef smc_in
static void smc_in(uint32_t pkt) { (void)pkt; smc_seen++; }
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_fml.c"
#include "../firmware/src/bt_devs.c"
#include "../firmware/src/bt_page.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
static void link_to(const char *name)
{
    memset(conn_name, 0, sizeof conn_name);
    str_cpy(conn_name, name, sizeof conn_name);
    conn_up = 1;
    bt_link(1);
}
static void fresh(uint32_t mode)                     /* MODE (0 AUTO 1 SMC 2 MIDI 3 FM-LINK), nothing linked, sent */
{
    bt_set_t s;
    ui_host_init();
    memset(&bt_st, 0, sizeof bt_st);
    bt_set_get(&s);
    s.mode = (uint8_t)mode;
    bt_set_put(&s);
    conn_up = 0;
    bt_link(0);
    memset(&bt_in, 0, sizeof bt_in);
    memset(&bt_clk, 0, sizeof bt_clk);
    bt_out_r = bt_out_w = 0;
    mi_r = mi_w = 0;
    nsent = 0;
    smc_seen = 0;
    song.g[G_CLOCK] = 0;
}
static uint32_t count(uint8_t st)
{
    uint32_t i, n = 0;
    for (i = 0; i < nsent; i++)
        n += sent[i][0] == st;
    return n;
}
static void hit_key0(void)                           /* track 1's key (F3) down, then up, a block each */
{
    keys(1u);
    render_mix(0, 0, CTL);
    keys(0);
    render_mix(0, 0, CTL);
}

static void test_out(void)
{
    uint32_t ch, note;
    fresh(2);
    link_to("nanoKEY");
    ch = drum_ch();
    note = (uint32_t)trk[0].p[P_NOTE] & 127u;
    hit_key0();
    bt_midi_frame();
    check("MIDI mode: a key's hit and its release go to the device (note on / off, the drum channel)", nsent == 2u &&
          sent[0][0] == (0x90u | ch) && sent[0][1] == note && sent[0][2] == 100u && sent_len[0] == 3u &&
          sent[1][0] == (0x80u | ch) && sent[1][1] == note);
    fresh(1);
    link_to("nanoKEY");
    hit_key0();
    bt_midi_frame();
    check("SMC DAW mode: nothing sent for the keys", nsent == 0u);
    fresh(3);
    link_to("nanoKEY");
    hit_key0();
    bt_midi_frame();
    check("FM-LINK mode: nothing sent", nsent == 0u);
    fresh(2);
    hit_key0();
    bt_midi_frame();
    check("not linked: nothing queued or sent", nsent == 0u && bt_out_w == 0u);
    fresh(2);
    link_to("nanoKEY");
    {
        uint32_t i;
        for (i = 0; i < 40u; i++)
            bt_midi_out_isr(0x09u | 0x90u << 8 | 60u << 16 | 1u << 24);
    }
    check("the out queue full: the rest dropped and counted", bt_out_w == BT_OUT_Q && bt_in.out_drops == 40u - BT_OUT_Q);
    bt_midi_frame();
    check("the queue drained to the device", nsent == BT_OUT_Q && bt_out_r == bt_out_w);
    link_to("nanoKEY");
    bt_midi_out_isr(0x0Fu | 0xF8u << 8);
    check("realtime from the keys' path: not queued (the clock has its own)", bt_out_w == bt_out_r);
}

static void test_clock(void)
{
    uint32_t i, beat, blocks, pulses;
    fresh(2);
    link_to("MacBook");
    song.g[G_BPM] = 120;
    transport_req = 1;
    render_mix(0, 0, CTL);
    bt_midi_frame();
    check("CLK INT, PLAY: Start, then the first pulse", nsent == 2u && sent[0][0] == 0xFAu && sent[1][0] == 0xF8u &&
          sent_len[0] == 1u);
    beat = beat_samples();
    blocks = beat * 4u / CTL;                        /* four beats */
    nsent = 0;
    for (i = 0; i < blocks; i++) {
        render_mix(0, 0, CTL);
        bt_midi_frame();
    }
    pulses = count(0xF8u);
    check("24 pulses a beat, counted from the samples", pulses >= 95u && pulses <= 96u && !count(0xFAu));
    nsent = 0;
    transport_req = 1;                               /* PLAY while playing: from step 0 again */
    render_mix(0, 0, CTL);
    bt_midi_frame();
    check("PLAY while playing: Start again", count(0xFAu) == 1u);
    nsent = 0;
    transport_req = 2;
    render_mix(0, 0, CTL);
    render_mix(0, 0, CTL);
    bt_midi_frame();
    check("STOP: Stop, once, then nothing", count(0xFCu) == 1u && nsent == 1u);
    fresh(2);
    link_to("MacBook");
    song.g[G_CLOCK] = 1;                             /* CLK USB: following a clock itself */
    render_mix(0, 0, CTL);
    transport_req = 1;
    for (i = 0; i < 200u; i++)
        render_mix(0, 0, CTL);
    bt_midi_frame();
    check("CLK USB: no clock out (no loop)", !count(0xFAu) && !count(0xF8u));
    fresh(1);
    link_to("SMC-Mixer");
    transport_req = 1;
    for (i = 0; i < 50u; i++)
        render_mix(0, 0, CTL);
    bt_midi_frame();
    check("SMC DAW: no clock out", !count(0xF8u) && !count(0xFAu));
}

static void test_release(void)
{
    uint32_t a;
    fresh(2);
    link_to("nanoKEY");
    bt_midi_in(0x09u | 0x99u << 8 | 36u << 16 | 100u << 24);   /* a note on channel 10 */
    bt_midi_in(0x0Bu | 0xB2u << 8 | 64u << 16 | 127u << 24);   /* the pedal down on channel 3 */
    mi_r = mi_w;
    check("held: the channels the device played", bt_in.held == ((1u << 9) | (1u << 2)));
    conn_up = 0;
    bt_link(0);
    bt_midi_frame();
    a = mi_r % MQ;
    check("the link down: CC 64 = 0 and CC 123 on each held channel into the ring (source 3)", mi_w - mi_r == 4u &&
          midi_in_q[a] == (0x0Bu | 0xB2u << 8 | 64u << 16) && midi_in_q[(a + 1u) % MQ] == (0x0Bu | 0xB2u << 8 | 123u << 16) &&
          midi_in_source[a] == 3u && !bt_in.release);
    mi_r = mi_w;
    bt_midi_frame();
    check("released once", mi_w == mi_r);
}

static void test_fmlink_mode(void)
{
    fresh(3);
    link_to("SMC-Mixer");
    bt_midi_in(0x09u | 0x99u << 8 | 36u << 16 | 100u << 24);
    bt_midi_in(0x0Fu | 0xFAu << 8);
    check("FM-LINK: what the device sends is ignored (no ring, no SMC)", mi_w == mi_r && !smc_seen && !bt_in.held);
    check("FM-LINK: neither MIDI nor SMC mode", bt_link_only() && !bt_in.out_on);
    fresh(0);
    link_to("SMC-Mixer");
    bt_midi_in(0x09u | 0x90u << 8 | 0x10u << 16 | 127u << 24);
    check("AUTO with an SMC: to the layout", smc_seen == 1u && mi_w == mi_r);
}

int main(void)
{
    test_out();
    test_clock();
    test_release();
    test_fmlink_mode();
    printf(fails ? "bt_midi_test: %d FAILED\n" : "bt_midi_test: all passed\n", fails);
    return fails ? 1 : 0;
}
