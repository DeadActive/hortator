/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
#include <stdio.h>
#include <stdint.h>
#define BLE_CENTRAL_STATE_ONLY 1
#include <string.h>
static int link_up, link_down;                      /* core_ble_link's calls (the core's side, counted) */
void core_ble_link(int up) { if (up) link_up++; else link_down++; }
#include "../../firmware/src/ble/ble_midi.c"
#include "../../firmware/src/ble/ble_central.c"
static int fails;
static uint32_t tx_n, tx_busy, tx_drop;            /* ble_out_drain's sender: packets, refuse, a disconnect inside */
static int tx(const uint8_t *p, uint32_t n)
{
    (void)p;
    (void)n;
    if (tx_busy)
        return 1;
    tx_n++;
    if (tx_drop)
        ble_ev(EV_DISCONNECTED);                    /* a handler run synchronously by the send */
    return 0;
}
#define CHECK(c, what) do { if (c) printf("ok    %s\n", what); else { printf("FAIL  %s\n", what); fails++; } } while (0)
int main(void)
{
    static const uint32_t live[] = {BLE_CONNECTING, BLE_DISCOVERING, BLE_SUBSCRIBED, BLE_RECEIVING};
    uint32_t i;
    CHECK(ble_next(BLE_STARTING, EV_INIT_OK) == BLE_IDLE, "start ok -> idle");
    CHECK(ble_next(BLE_STARTING, EV_TIMEOUT) == BLE_FAILED, "start timeout -> failed");
    CHECK(ble_next(BLE_STARTING, EV_INIT_FAIL) == BLE_FAILED, "start failure -> failed");
    CHECK(ble_next(BLE_SCANNING, EV_SCAN_DONE) == BLE_IDLE, "scan done -> idle");
    CHECK(ble_next(BLE_CONNECTING, EV_CONNECTED) == BLE_DISCOVERING, "connected -> discovering");
    CHECK(ble_next(BLE_CONNECTING, EV_TIMEOUT) == BLE_IDLE, "connect timeout -> idle");
    CHECK(ble_next(BLE_CONNECTING, EV_CONN_FAIL) == BLE_IDLE, "connect failure -> idle");
    CHECK(ble_next(BLE_DISCOVERING, EV_FOUND_MIDI) == BLE_SUBSCRIBED, "MIDI found -> subscribed");
    CHECK(ble_next(BLE_DISCOVERING, EV_NO_MIDI) == BLE_IDLE, "no MIDI service -> idle");
    CHECK(ble_next(BLE_DISCOVERING, EV_TIMEOUT) == BLE_IDLE, "discovery timeout -> idle");
    CHECK(ble_next(BLE_SUBSCRIBED, EV_NOTIFY) == BLE_RECEIVING, "first notification -> receiving");
    CHECK(ble_next(BLE_RECEIVING, EV_NOTIFY) == BLE_RECEIVING, "more notifications -> receiving");
    for (i = 0; i < 4; i++)
        CHECK(ble_next(live[i], EV_DISCONNECTED) == BLE_IDLE, "disconnect in a live state -> idle");
    CHECK(ble_next(BLE_IDLE, EV_NOTIFY) == BLE_IDLE, "stray event keeps the state");
    CHECK(ble_next(BLE_SCANNING, EV_DISCONNECTED) == BLE_SCANNING, "a disconnect while scanning keeps scanning");
    CHECK(ble_next(BLE_FAILED, EV_INIT_OK) == BLE_FAILED, "failed stays failed");
    {   /* the MIDI characteristic 7772E5DB-3868-4112-A1A9-F2669D106BF3, in either byte order */
        static const uint8_t be[16] = {0x77, 0x72, 0xE5, 0xDB, 0x38, 0x68, 0x41, 0x12, 0xA1, 0xA9, 0xF2, 0x66,
                                       0x9D, 0x10, 0x6B, 0xF3};
        uint8_t le[16], other[16];
        for (i = 0; i < 16; i++)
            le[i] = be[15 - i];
        memcpy(other, be, 16);
        other[7] ^= 1;
        CHECK(ble_is_midi_char(be), "MIDI characteristic UUID, string order");
        CHECK(ble_is_midi_char(le), "MIDI characteristic UUID, little-endian order");
        CHECK(!ble_is_midi_char(other), "another UUID is not the MIDI characteristic");
    }
    {   /* monitor lines from USB-MIDI packets (cin | status << 8 | d1 << 16 | d2 << 24) */
        char t[32];
        uint32_t sx = 0;
        ble_midi_text(0x9u | 0x90u << 8 | 60u << 16 | 100u << 24, &sx, t);
        CHECK(!strcmp(t, "NOTE ON  1 C4 v100"), "note on, channel 1, middle C");
        ble_midi_text(0x9u | 0x9Fu << 8 | 61u << 16, &sx, t);
        CHECK(!strcmp(t, "NOTE OFF 16 C#4"), "note on with velocity 0 is a note off");
        ble_midi_text(0x8u | 0x80u << 8 | 0u << 16, &sx, t);
        CHECK(!strcmp(t, "NOTE OFF 1 C-1"), "note off, lowest note");
        ble_midi_text(0xBu | 0xB2u << 8 | 7u << 16 | 127u << 24, &sx, t);
        CHECK(!strcmp(t, "CC 3 #7 = 127"), "control change");
        ble_midi_text(0xCu | 0xC0u << 8 | 5u << 16, &sx, t);
        CHECK(!strcmp(t, "PC 1 5"), "program change");
        ble_midi_text(0xEu | 0xE0u << 8 | 0u << 16 | 0x40u << 24, &sx, t);
        CHECK(!strcmp(t, "PB 1 +0"), "pitch bend centre");
        ble_midi_text(0xEu | 0xE0u << 8 | 0u << 16 | 0u << 24, &sx, t);
        CHECK(!strcmp(t, "PB 1 -8192"), "pitch bend bottom");
        CHECK(ble_midi_text(0x4u | 0xF0u << 8 | 1u << 16 | 2u << 24, &sx, t) == 0, "SysEx start: no line yet");
        ble_midi_text(0x6u | 0x03u << 8 | 0xF7u << 16, &sx, t);
        CHECK(!strcmp(t, "SYSEX 5 BYTES"), "SysEx end: one line with the byte count");
        ble_midi_text(0xFu | 0xF8u << 8, &sx, t);
        CHECK(!strcmp(t, "RT F8"), "real-time");
        ble_midi_text(0xAu | 0xA1u << 8 | 2u << 16 | 3u << 24, &sx, t);
        CHECK(!strcmp(t, "MIDI A1 02 03"), "anything else as hex");
    }
    {   /* review #7: a named device's text ends after the name, not in what the buffer held before */
        char b[64];
        ble_dev_t d;
        memset(&d, 0, sizeof d);
        strcpy(d.name, "SMC-Mixer");
        memset(b, 'X', sizeof b);
        put_dev(b, &d);
        CHECK(!strcmp(b, "SMC-Mixer"), "a device name is a whole string");
        memset(&d, 0, sizeof d);
        d.addr[5] = 0x5E; d.addr[0] = 0x10;
        memset(b, 'X', sizeof b);
        put_dev(b, &d);
        CHECK(!strcmp(b, "5E:00:00:00:00:10"), "no name: the address, most significant byte first");
    }
    {   /* the console's words (part 1b) */
        uint32_t n = 0;
        CHECK(ble_cmd_parse("", &n) == BLE_CMD_STATUS && ble_cmd_parse("  ", &n) == BLE_CMD_STATUS, "ble: status");
        CHECK(ble_cmd_parse("start", &n) == BLE_CMD_START && ble_cmd_parse(" scan ", &n) == BLE_CMD_SCAN &&
              ble_cmd_parse("list", &n) == BLE_CMD_LIST && ble_cmd_parse("stop", &n) == BLE_CMD_STOP, "ble: words");
        CHECK(ble_cmd_parse("connect 3", &n) == BLE_CMD_CONNECT && n == 3u, "ble: connect N");
        CHECK(ble_cmd_parse("connect 16", &n) == BLE_CMD_CONNECT && n == 16u, "ble: connect the last slot");
        CHECK(ble_cmd_parse("connect", &n) == BLE_CMD_BAD && ble_cmd_parse("connect 0", &n) == BLE_CMD_BAD &&
              ble_cmd_parse("connect 17", &n) == BLE_CMD_BAD && ble_cmd_parse("connect x", &n) == BLE_CMD_BAD &&
              ble_cmd_parse("connect 1x", &n) == BLE_CMD_BAD, "ble: bad connect numbers");
        CHECK(ble_cmd_parse("starts", &n) == BLE_CMD_BAD && ble_cmd_parse("start now", &n) == BLE_CMD_BAD &&
              ble_cmd_parse("blah", &n) == BLE_CMD_BAD, "ble: unknown words");
    }
    {   /* which state each command needs (Review Focus 1) */
        CHECK(!ble_cmd_refuse(BLE_CMD_START, BLE_OFF, 0, 0), "start from OFF");
        CHECK(ble_cmd_refuse(BLE_CMD_START, BLE_IDLE, 0, 0) && ble_cmd_refuse(BLE_CMD_START, BLE_FAILED, 0, 0),
              "start twice / after a failure refused");
        CHECK(ble_cmd_refuse(BLE_CMD_SCAN, BLE_OFF, 0, 0) && ble_cmd_refuse(BLE_CMD_SCAN, BLE_STARTING, 0, 0) &&
              ble_cmd_refuse(BLE_CMD_SCAN, BLE_RECEIVING, 0, 0) && !ble_cmd_refuse(BLE_CMD_SCAN, BLE_IDLE, 0, 0),
              "scan only from IDLE");
        CHECK(!ble_cmd_refuse(BLE_CMD_CONNECT, BLE_IDLE, 3, 3) && ble_cmd_refuse(BLE_CMD_CONNECT, BLE_IDLE, 3, 4) &&
              ble_cmd_refuse(BLE_CMD_CONNECT, BLE_OFF, 3, 1) && ble_cmd_refuse(BLE_CMD_CONNECT, BLE_SCANNING, 3, 1),
              "connect: IDLE and a listed device");
        CHECK(!ble_cmd_refuse(BLE_CMD_STOP, BLE_CONNECTING, 0, 0) && !ble_cmd_refuse(BLE_CMD_STOP, BLE_RECEIVING, 0, 0)
              && ble_cmd_refuse(BLE_CMD_STOP, BLE_OFF, 0, 0) && ble_cmd_refuse(BLE_CMD_STOP, BLE_IDLE, 0, 0),
              "stop: only while connecting or connected");
        CHECK(!ble_cmd_refuse(BLE_CMD_LIST, BLE_OFF, 0, 0) && !ble_cmd_refuse(BLE_CMD_STATUS, BLE_OFF, 0, 0),
              "list / status in any state");
    }
    {   /* the main loop's period, from the UI frame counter (BT off and on) */
        ble_loop_t l = {0};
        ble_loop_note(&l, 5, 100);
        CHECK(l.last_ms == 0 && l.max_ms == 0, "loop: the first note is the start");
        ble_loop_note(&l, 5, 110);
        ble_loop_note(&l, 6, 116);
        CHECK(l.last_ms == 16u && l.max_ms == 16u, "loop: one pass");
        ble_loop_note(&l, 7, 136);
        ble_loop_note(&l, 8, 151);
        CHECK(l.last_ms == 15u && l.max_ms == 20u, "loop: last and max");
    }
    {   /* MIDI out: queued only while connected, 64 deep, emptied on a disconnect */
        static const uint8_t on[3] = {0x90, 0x3C, 0x7F};
        uint8_t m[3] = {0xB0, 0, 0};
        int all = 1;
        st = BLE_IDLE;
        CHECK(!ble_midi_send(on, 3), "out: refused while not connected (IDLE)");
        st = BLE_DISCOVERING;
        CHECK(!ble_midi_send(on, 3) && mo.h == mo.t && !mo.drops, "out: refused while discovering, not a drop");
        st = BLE_SUBSCRIBED;
        CHECK(!ble_midi_send(on, 0) && !ble_midi_send(on, 4) && mo.h == mo.t, "out: 1..3 bytes only");
        CHECK(ble_midi_send(on, 3) && mo.len[mo.t & 63u] == 3u && !memcmp(mo.msg[mo.t & 63u], on, 3),
              "out: queued while subscribed");
        st = BLE_RECEIVING;
        for (i = 1; i < 64; i++) {
            m[1] = (uint8_t)i;
            all &= ble_midi_send(m, 3);
        }
        CHECK(all && mo.h - mo.t == 64u && mo.msg[(mo.t + 63u) & 63u][1] == 63u, "out: 64 queued in order");
        CHECK(!ble_midi_send(on, 2) && mo.drops == 1u && mo.h - mo.t == 64u, "out: the 65th dropped and counted");
        ble_ev(EV_DISCONNECTED);
        CHECK(st == BLE_IDLE && mo.h == mo.t, "out: a disconnect empties the queue");
        CHECK(!ble_midi_send(on, 3) && mo.drops == 1u, "out: refused after the disconnect");
        CHECK(!ble_midi_room(), "room: 0 while not connected");
        st = BLE_SUBSCRIBED;
        CHECK(ble_midi_room() == 64u, "room: 64 when linked and empty");
        ble_midi_send(on, 3);
        ble_midi_send(on, 3);
        CHECK(ble_midi_room() == 62u, "room: 62 after two queued");
        while (ble_midi_room())
            ble_midi_send(on, 3);
        CHECK(mo.h - mo.t == 64u && mo.drops == 1u, "room: a caller that sends only what fits never counts a drop");
        ble_ev(EV_DISCONNECTED);
    }
    {   /* the out flush: the tail moves only over messages still queued (a clear inside the send) */
        static const uint8_t on[3] = {0x90, 0x3C, 0x7F};
        uint32_t sent;
        st = BLE_RECEIVING;
        ble_midi_send(on, 3);
        ble_midi_send(on, 3);
        tx_busy = 1;
        ble_out_drain(0, tx);
        CHECK(mo.h - mo.t == 2u && !tx_n, "flush: a busy stack keeps the queue");
        tx_busy = 0;
        sent = mo.sent;
        ble_out_drain(0, tx);
        CHECK(mo.h == mo.t && tx_n == 1u && mo.sent == sent + 2u, "flush: both in one packet");
        ble_midi_send(on, 3);
        tx_drop = 1;
        sent = mo.sent;
        ble_out_drain(0, tx);
        CHECK(st == BLE_IDLE && mo.h == mo.t && mo.sent == sent, "flush: a disconnect inside the send: queue empty");
        tx_drop = 0;
        st = BLE_SUBSCRIBED;
        CHECK(ble_midi_send(on, 3) && mo.h - mo.t == 1u, "flush: the queue works after the next connect");
        ble_ev(EV_DISCONNECTED);
    }
    {   /* how a disconnect is asked: CONNECTING cancels (a stale handle from a failed link is not used) */
        CHECK(ble_disc_how(BLE_CONNECTING, 0) == BD_CANCEL && ble_disc_how(BLE_CONNECTING, 0x41) == BD_CANCEL,
              "disconnect: connecting -> cancel, even with a stale handle");
        CHECK(ble_disc_how(BLE_DISCOVERING, 0x41) == BD_DISC && ble_disc_how(BLE_SUBSCRIBED, 0x41) == BD_DISC &&
              ble_disc_how(BLE_RECEIVING, 0x41) == BD_DISC, "disconnect: a live link with a handle -> disconnect");
        CHECK(ble_disc_how(BLE_RECEIVING, 0) == BD_NONE && ble_disc_how(BLE_IDLE, 0x41) == BD_NONE &&
              ble_disc_how(BLE_SCANNING, 0x41) == BD_NONE, "disconnect: no link or no handle -> nothing");
    }
    {   /* the auto-connect step (spec 3) */
        ble_auto_t a;
        memset(&a, 0, sizeof a);
        a.want = 1;
        a.have = 1;
        a.rest_ms = 1000;
        CHECK(ble_auto_step(&a, BLE_IDLE, 1, 1000) == BA_CONNECT, "auto: IDLE, target listed -> connect");
        CHECK(ble_auto_step(&a, BLE_IDLE, 0, 2900) == BA_NONE, "auto: not listed, 1.9 s after the rest -> nothing");
        CHECK(ble_auto_step(&a, BLE_IDLE, 0, 3000) == BA_SCAN, "auto: not listed, 2.0 s after the rest -> scan");
        CHECK(ble_auto_step(&a, BLE_SCANNING, 2, 3000) == BA_STOP_SCAN, "auto: auto scan, target seen with its name -> stop the scan");
        CHECK(ble_auto_step(&a, BLE_SCANNING, 0, 9000) == BA_NONE, "auto: auto scan, not seen -> keep scanning");
        CHECK(ble_auto_step(&a, BLE_SCANNING, 1, 3000) == BA_NONE, "auto: auto scan, target listed without a name, not sighted -> keep scanning");
        a.sighted = 1;
        a.sighted_ms = 3000;
        CHECK(ble_auto_step(&a, BLE_SCANNING, 1, 3000) == BA_NONE && ble_auto_step(&a, BLE_SCANNING, 1, 3499) == BA_NONE,
              "auto: listed without a name at t0, t0 + 499 ms -> keep scanning (the scan response may follow)");
        CHECK(ble_auto_step(&a, BLE_SCANNING, 2, 3001) == BA_STOP_SCAN, "auto: .. its name arrives -> stop the scan");
        CHECK(ble_auto_step(&a, BLE_SCANNING, 1, 3500) == BA_STOP_SCAN, "auto: still no name at t0 + 500 ms -> stop the scan");
        a.sighted = 0;
        a.manual = 1;
        CHECK(ble_auto_step(&a, BLE_SCANNING, 2, 3000) == BA_NONE, "auto: a manual scan is never stopped early");
        a.manual = 0;
        a.tried = 1;
        CHECK(ble_auto_step(&a, BLE_IDLE, 1, 2900) == BA_NONE && ble_auto_step(&a, BLE_IDLE, 1, 3000) == BA_SCAN,
              "auto: one connect per sighting: after a failed try, a scan after the rest");
        a.tried = 0;
        CHECK(ble_auto_step(&a, BLE_RECEIVING, 1, 3000) == BA_NONE && ble_auto_step(&a, BLE_CONNECTING, 1, 3000) ==
              BA_NONE, "auto: connected / connecting -> nothing");
        a.paused = 1;
        CHECK(ble_auto_step(&a, BLE_IDLE, 1, 9000) == BA_NONE && ble_auto_step(&a, BLE_IDLE, 0, 9000) == BA_NONE,
              "auto: paused -> no auto connect, no auto scan");
        a.scan_req = 1;
        CHECK(ble_auto_step(&a, BLE_IDLE, 0, 1000) == BA_SCAN, "auto: a requested scan starts on IDLE (no rest)");
        CHECK(ble_auto_step(&a, BLE_RECEIVING, 0, 1000) == BA_NONE, "auto: a requested scan waits for IDLE");
        a.scan_req = 0;
        a.paused = 0;
        a.have = 0;
        CHECK(ble_auto_step(&a, BLE_IDLE, 0, 9000) == BA_NONE && ble_auto_step(&a, BLE_RECEIVING, 0, 9000) == BA_NONE,
              "auto: no target -> nothing");
        a.have = 1;
        for (i = 0; i < 3; i++) {
            static const uint32_t S[3] = {BLE_OFF, BLE_STARTING, BLE_FAILED};
            CHECK(ble_auto_step(&a, S[i], 1, 9000) == BA_NONE && ble_auto_step(&a, S[i], 0, 9000) == BA_NONE,
                  "auto: OFF / STARTING / FAILED -> nothing");
        }
        a.want = 0;
        CHECK(ble_auto_step(&a, BLE_RECEIVING, 1, 9000) == BA_DISCONNECT, "auto: want off while receiving -> disconnect");
        CHECK(ble_auto_step(&a, BLE_DISCOVERING, 1, 9000) == BA_DISCONNECT, "auto: want off while discovering -> disconnect");
        CHECK(ble_auto_step(&a, BLE_IDLE, 1, 9000) == BA_NONE && ble_auto_step(&a, BLE_IDLE, 0, 9000) == BA_NONE,
              "auto: want off, IDLE -> nothing");
        a.dropping = 1;
        CHECK(ble_auto_step(&a, BLE_RECEIVING, 1, 9000) == BA_NONE, "auto: want off: the disconnect asked once");
        a.dropping = 0;
        a.console = 1;
        CHECK(ble_auto_step(&a, BLE_RECEIVING, 1, 9000) == BA_NONE, "auto: want off: the console's link is left alone");
        a.console = 0;
        a.scan_req = 1;
        CHECK(ble_auto_step(&a, BLE_IDLE, 0, 1000) == BA_SCAN, "auto: a requested scan runs without want");
    }
    {   /* the service's watch: link up / down to the core once each, the rest times, the scan flags */
        ble_auto_t a;
        memset(&a, 0, sizeof a);
        st = BLE_IDLE;
        ble_watch(&a, 100);
        a.tried = 1;
        st = BLE_SCANNING;
        ble_watch(&a, 200);
        CHECK(!a.tried, "watch: a scan start is a new sighting");
        a.sighted = 1;
        ble_sight(&a, 1, 250);
        CHECK(a.sighted_ms == 0u, "sight: an old sighting is kept");
        a.sighted = 0;
        ble_sight(&a, 0, 250);
        CHECK(!a.sighted, "sight: not listed -> not sighted");
        ble_sight(&a, 1, 260);
        ble_sight(&a, 1, 270);
        CHECK(a.sighted && a.sighted_ms == 260u, "sight: the first listing in the scan sets the time");
        st = BLE_IDLE;
        ble_watch(&a, 280);
        st = BLE_SCANNING;
        ble_watch(&a, 290);
        CHECK(!a.sighted, "watch: a scan start clears the sighting");
        a.manual = 1;
        ble_ev(EV_SCAN_DONE);
        ble_watch(&a, 300);
        CHECK(a.rest_ms == 300u && !a.manual, "watch: a scan end: the rest starts, the scan is no longer manual");
        st = BLE_CONNECTING;
        a.console = 1;
        ble_watch(&a, 400);
        ble_ev(EV_CONNECTED);
        ble_watch(&a, 500);
        CHECK(!link_up && !link_down, "watch: connecting / discovering: no link yet");
        ble_ev(EV_FOUND_MIDI);
        ble_watch(&a, 600);
        CHECK(link_up == 1 && !link_down, "watch: SUBSCRIBED -> core_ble_link(1)");
        ble_ev(EV_NOTIFY);
        ble_watch(&a, 700);
        ble_watch(&a, 800);
        CHECK(link_up == 1 && !link_down, "watch: RECEIVING after it: no second call");
        a.dropping = 1;
        ble_ev(EV_DISCONNECTED);
        ble_watch(&a, 900);
        ble_watch(&a, 1000);
        CHECK(link_up == 1 && link_down == 1, "watch: the disconnect -> core_ble_link(0) once");
        CHECK(a.rest_ms == 900u && !a.dropping && !a.console, "watch: a drop: the rest starts, the link's flags clear");
        st = BLE_CONNECTING;
        ble_watch(&a, 1100);
        ble_ev(EV_CONN_FAIL);
        ble_watch(&a, 1200);
        CHECK(link_up == 1 && link_down == 1 && a.rest_ms == 1200u, "watch: a failed connect: rest, no link calls");
    }
    printf(fails ? "ble_central: %d FAILED\n" : "ble_central: all checks passed\n", fails);
    return fails != 0;
}
