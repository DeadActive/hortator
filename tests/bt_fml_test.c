/* SPDX-License-Identifier: GPL-3.0-only
 * FM-Link: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* (felucca-ble ble-kit's, on Hortator's UI harness: ui_host.h, the Bluetooth store in RAM) FM-Link's glue (firmware/src/bt_fml.c) on the host, built with FELUCCA_BLE=1 and the UI harness: this FM-1's core
 * (its sequencer through seq.c events_block, its main loop's bt_fml_frame, core_fml_rx) against another FM-1 that is
 * the engine alone (fmlink.c), its own crystal and tick count, joined by a link that delivers at 15 ms connection
 * events. The engine's accuracy over a bad link is tests/fmlink_test.c's; here: the roles from CLK (INT leads, LINK
 * follows, CLK USB / TRS / BLE lead with the clock's transport and tempo), PLAY / STOP / BPM through the core's transport, the follower's BPM from the leader, its own
 * PLAY / STOP held, the messages, CLK LINK without a link as INT, a message kept while the BLE unit has no room. */
#include "ui_host.h"
#if !FELUCCA_BLE
#error "build with -DFELUCCA_BLE=1"
#endif
#define BLE_FML_SEND_OWN 1
#include "ble_ui_stub.h"
int ble_midi_send(const uint8_t *m, uint32_t n) { (void)m; (void)n; return 0; }
uint32_t ble_midi_room(void) { return 0; }

/* the link: packets with when they arrive (real time = this FM-1's ticks), to the peer (1) or to this FM-1 (0) */
#define EV (15u * 24000u)                            /* the connection interval, ticks */
typedef struct { uint32_t at; int to_peer; uint8_t len, b[20]; } pk_t;
static pk_t Q[512];
static uint32_t qn, fml_room = 1000000u, fml_sent, link_down;
static void link_put(int to_peer, const uint8_t *b, uint32_t len)
{
    if (link_down || qn >= 512u)
        return;
    Q[qn].at = (host_ticks / EV + 1u) * EV + 12000u;   /* the next connection event, then half a millisecond */
    Q[qn].to_peer = to_peer;
    Q[qn].len = (uint8_t)len;
    memcpy(Q[qn].b, b, len);
    qn++;
}
int ble_fml_send(const uint8_t *p, uint32_t len)
{
    if (!fml_room)
        return 0;
    fml_room--;
    fml_sent++;
    link_put(1, p, len);
    return 1;
}
#define fm1_ticks() (host_ticks)                     /* (the harness' own advances 1 ms a call: here time is the
                                                      * test's, set by step) */
#include "../firmware/src/bt_set.c"
static uint32_t smc_seen;
#define smc_in smc_in_real                            /* (the layout's own entry, renamed: counted here) */
#include "../firmware/src/smc.c"
#undef smc_in
static void smc_in(uint32_t pkt) { (void)pkt; smc_seen++; }
#include "../firmware/src/bt_midi.c"
#include "../firmware/src/bt_fml.c"
#include "../firmware/src/bt_devs.c"
#include "../firmware/src/bt_page.c"

static int fails;
static void ok(const char *what, int good)
{
    printf("%s  %s\n", good ? "ok  " : "FAIL", what);
    fails += !good;
}

/* the other FM-1: the engine, its ticks = this one's + an offset, 40 ppm faster */
static struct { fml_t f; uint32_t off, bpm, playing, next, req, spb; } P;
static uint32_t peer_clk(void) { return host_ticks + P.off + (uint32_t)((uint64_t)host_ticks * 40u / 1000000u); }
static int said_lead, said_follow;
static void step(void)                               /* one block of 32 samples on each, the main loops, the link */
{
    uint32_t i, k = 0, len, seq_n;
    uint8_t b[20];
    host_ticks += 32u * 544u;
    fm1_ms = host_ticks / 24000u;
    for (i = 0; i < qn; i++) {
        if ((int32_t)(host_ticks - Q[i].at) >= 0) {
            if (Q[i].to_peer)
                fml_rx(&P.f, Q[i].b, Q[i].len, peer_clk());
            else
                bt_fml_rx(Q[i].b, Q[i].len, host_ticks);   /* (core_fml_rx) */
        } else {
            Q[k++] = Q[i];
        }
    }
    qn = k;
    bt_fml_heard = host_ticks + 256u * 544u;         /* (audio.c kb_out_tick: rendered 256 samples ahead) */
    events_block(32);
    if ((int32_t)(peer_clk() + 256u * 544u - P.next) >= 0) {
        uint32_t bpm = P.f.role == FML_FOLLOWER && P.f.l_valid ? P.f.l_bpm : P.bpm;
        P.spb = (uint32_t)FS * 60u / bpm;
        i = fml_block(&P.f, P.next, 32, bpm, P.spb, (int)P.playing, P.req, &seq_n);
        P.req = 0;
        if (i == 1u || i == 3u)
            P.playing = 1;
        else if (i == 2u)
            P.playing = 0;
        P.next += 32u * 544u;
    }
    bt_fml_frame();                                  /* (bt_service: the main loop) */
    while ((len = fml_poll(&P.f, peer_clk(), b)) != 0u)
        link_put(0, b, len);
    said_lead |= str_eq(ui.msg, "FM-LINK: LEADING");
    said_follow |= str_eq(ui.msg, "FM-LINK: FOLLOWING");
}
static uint32_t pulse_ms, pulse_next;              /* an external MIDI clock on USB: a pulse every pulse_ms (0 none) */
static void usb_rt(uint32_t status)                  /* a realtime message from USB into the input ring */
{
    midi_in_q[mi_w % MQ] = 0x0Fu | status << 8;
    midi_in_source[mi_w % MQ] = 1u;
    midi_in_ms[mi_w % MQ] = fm1_ms;
    mi_w++;
}
static void run_ms(uint32_t ms)
{
    uint32_t end = host_ticks + ms * 24000u;
    while ((int32_t)(host_ticks - end) < 0) {
        if (pulse_ms && (int32_t)(fm1_ms - pulse_next) >= 0) {
            usb_rt(0xF8u);
            pulse_next += pulse_ms;
        }
        step();
    }
}
/* |this FM-1's beats - the peer's| at this moment, in ms (each from its last block) */
static double apart_ms(void)
{
    double a = bt_fml.ext ?                          /* (a clock's leader: from its last pulse, the clock's grid) */
               (double)bt_fml.snap_beats / 65536.0 + (double)(int32_t)(host_ticks - bt_fml.snap_t) / (544.0 * beat_samples()) :
               (double)bt_fml.last_beats / 65536.0 +
               (double)(int32_t)(host_ticks - bt_fml.last_t) / (544.0 * (double)((uint32_t)FS * 60u / (uint32_t)song.g[G_BPM]));
    double b = (double)P.f.last_beats / 65536.0 + (double)(int32_t)(peer_clk() - P.f.last_t) / (544.0 * P.spb);
    double d = (a - b) * 60000.0 / song.g[G_BPM];
    return d < 0 ? -d : d;
}
static double worst_ms(uint32_t ms)
{
    double w = 0, d;
    uint32_t i;
    for (i = 0; i < ms / 10u; i++) {
        run_ms(10);
        d = apart_ms();
        w = d > w ? d : w;
    }
    return w;
}
static void fresh(uint32_t clock, uint32_t peer_role)  /* power-on, MIDI mode, linked to the other FM-1 */
{
    bt_set_t s;
    ui_host_init();
    memset(&midi_clock, 0, sizeof midi_clock);
    mi_r = mi_w = 0;
    memset(&bt_st, 0, sizeof bt_st);
    memset(&s, 0, sizeof s);
    s.mode = BT_MODE_MIDI;
    bt_set_put(&s);
    memset(&bt_in, 0, sizeof bt_in);
    memset(&bt_fml, 0, sizeof bt_fml);
    memset(&bt_fo, 0, sizeof bt_fo);
    qn = 0;
    link_down = 0;
    fml_room = 1000000u;
    host_ticks = 123456789u;
    fm1_ms = host_ticks / 24000u;
    song.g[G_CLOCK] = (int16_t)clock;
    song.g[G_BPM] = 97;
    memset(&P, 0, sizeof P);
    fml_init(&P.f, peer_role);
    P.off = 0x9E3779B9u;
    P.bpm = 120;
    P.next = peer_clk() + 256u * 544u;
    str_cpy(conn_name, "Felucca FM-1", sizeof conn_name);
    conn_up = 1;
    bt_link(1);
    said_lead = said_follow = 0;
}

int main(void)
{
    double e;
    uint32_t i;
    /* this FM-1 follows (CLK LINK), the other leads */
    fresh(CLK_LINK, FML_LEADER);
    run_ms(3000);
    ok("CLK LINK: the follower's role", bt_fml.role == FML_FOLLOWER);
    ok("the follower hears the leader and knows the offset", fml_linked(&bt_fml, host_ticks) && bt_fml.off_ok);
    ok("it says FM-LINK: FOLLOWING", said_follow && !said_lead);
    P.req = 1;                                       /* PLAY on the leader */
    run_ms(5);
    ok("the leader announces its start (both 80 ms on)", !P.playing && !song.playing);
    run_ms(300);
    ok("both play", P.playing && song.playing);
    ok("the follower's BPM is the leader's (from 97)", song.g[G_BPM] == 120);
    e = worst_ms(20000);
    printf("      the core's sequencer against the leader: %.3f ms at most over 20 s\n", e);
    ok("within 1.5 ms over 20 s, still playing (CLK LINK has no external clock's 500 ms timeout)", e <= 1.5 && song.playing);
    transport_req = 2;                               /* its own STOP */
    run_ms(50);
    ok("a linked follower's own STOP does nothing and says FOLLOWING LINK", song.playing && str_eq(ui.msg, "FOLLOWING LINK"));
    P.bpm = 133;                                     /* the leader's BPM changes */
    run_ms(1000);
    ok("the follower takes the leader's new BPM", song.g[G_BPM] == 133);
    e = worst_ms(5000);
    ok("in time at the new BPM", e <= 1.5);
    link_down = 1;                                   /* the link lost: it plays on */
    run_ms(3000);
    ok("the link lost: the follower plays on at the leader's last BPM", song.playing && song.g[G_BPM] == 133);
    link_down = 0;
    run_ms(3000);
    P.req = 2;                                       /* STOP on the leader */
    run_ms(200);
    ok("STOP on the leader stops the follower", !P.playing && !song.playing);

    /* this FM-1 leads (CLK INT), the other follows */
    fresh(0, FML_FOLLOWER);
    song.g[G_BPM] = 110;
    run_ms(3000);
    ok("CLK INT: the leader's role, FM-LINK: LEADING", bt_fml.role == FML_LEADER && said_lead);
    transport_req = 1;                               /* PLAY */
    run_ms(5);
    ok("PLAY with a follower linked: not at once", !song.playing);
    run_ms(300);
    ok("both play, the follower at the leader's BPM", song.playing && P.playing && P.f.l_bpm == 110u);
    e = worst_ms(10000);
    printf("      the follower against the core's sequencer: %.3f ms at most over 10 s\n", e);
    ok("within 1.5 ms", e <= 1.5);
    transport_req = 2;                               /* STOP */
    run_ms(200);
    ok("STOP stops both", !song.playing && !P.playing);
    fml_room = 0;                                    /* the BLE unit has no room: a message is kept, not lost */
    for (i = 0; i < 600u && !bt_fo.len; i++)         /* (until one is due: TIME every 100..150 ms) */
        run_ms(1);
    ok("no room: one message waits", bt_fo.len != 0u);
    fml_sent = 0;
    fml_room = 1000000u;
    run_ms(2);
    ok("room again: it goes first", fml_sent != 0u && bt_fo.len == 0u);
    link_down = 1;                                   /* the follower gone: PLAY at once again */
    run_ms(2500);
    transport_req = 1;
    run_ms(2);
    ok("no follower: PLAY starts at once", song.playing);

    /* CLK LINK without a link: as INT */
    fresh(CLK_LINK, FML_LEADER);
    link_down = 1;
    run_ms(100);
    transport_req = 1;
    run_ms(2);
    ok("CLK LINK, no other FM-1: PLAY starts at once, at its own BPM", song.playing && song.g[G_BPM] == 97);
    run_ms(2000);
    ok("... and keeps playing", song.playing);

    /* this FM-1 follows a MIDI clock on USB and leads the other FM-1 */
    fresh(1, FML_FOLLOWER);
    song.g[G_BPM] = 125;
    pulse_ms = 20u;                                  /* 125 BPM */
    pulse_next = fm1_ms;
    run_ms(3000);
    ok("CLK USB: the leader's role, FM-LINK: LEADING", bt_fml.role == FML_LEADER && bt_fml.ext && said_lead);
    ok("the clock running, no Start: neither plays", !song.playing && !P.playing);
    usb_rt(0xFAu);                                   /* the DAW's Start */
    run_ms(300);
    ok("the clock's Start: both play, the follower from beat 1", song.playing && P.playing && P.f.joined_beats == 0u);
    run_ms(2000);
    ok("the follower at the clock's BPM", P.f.l_bpm == 125u);
    e = worst_ms(10000);
    printf("      the follower against the clock's sequencer: %.3f ms at most over 10 s\n", e);
    ok("within 2 ms", e <= 2.0);
    usb_rt(0xFAu);                                   /* the DAW's Start again while playing: from beat 1, both */
    run_ms(400);
    ok("a Start while playing: the follower starts again from beat 1 with it", song.playing && P.playing &&
       P.f.joined_beats == 0u && P.f.beats < (2u << 16));
    e = worst_ms(3000);
    ok("... within 2 ms", e <= 2.0);
    usb_rt(0xFCu);                                   /* the DAW's Stop */
    run_ms(200);
    ok("the clock's Stop stops both", !song.playing && !P.playing);
    pulse_ms = 0;
    /* a clock's leader alone: the clock's PLAY as before */
    fresh(2, FML_FOLLOWER);
    link_down = 1;
    run_ms(2500);
    ok("CLK TRS, no other FM-1: the leader's role, nothing linked", bt_fml.role == FML_LEADER && !fml_linked(&bt_fml, host_ticks));
    printf(fails ? "bt_fml: %d FAILED\n" : "bt_fml: all checks passed\n", fails);
    return fails != 0;
}
