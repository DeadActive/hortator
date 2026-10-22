/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
#include <stdio.h>
#include <stdint.h>
#define BLE_CENTRAL_STATE_ONLY 1
#define BLE_MONITOR 1                                /* (ble_midi_text: host only) */
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
    /* the device role (a host connects to the FM-1) */
    CHECK(ble_next(BLE_ADVERTISING, EV_CONNECTED) == BLE_SUBSCRIBED, "device: a host connects while advertising -> linked");
    CHECK(ble_next(BLE_ADVERTISING, EV_DISCONNECTED) == BLE_ADVERTISING, "device: no link while advertising: a disconnect changes nothing");
    CHECK(ble_next(BLE_SUBSCRIBED, EV_NOTIFY) == BLE_RECEIVING, "device: MIDI written -> receiving");
    CHECK(ble_next(BLE_RECEIVING, EV_DISCONNECTED) == BLE_IDLE, "device: the host leaves -> idle (then advertising again)");
    CHECK(ble_dev_step(1, BLE_IDLE) == BV_ADV_ON, "device step: wanted and idle -> advertise");
    CHECK(ble_dev_step(1, BLE_ADVERTISING) == BV_NONE && ble_dev_step(1, BLE_RECEIVING) == BV_NONE,
          "device step: wanted and advertising or linked -> nothing");
    CHECK(ble_dev_step(0, BLE_ADVERTISING) == BV_ADV_OFF, "device step: not wanted while advertising -> stop advertising");
    CHECK(ble_dev_step(0, BLE_SUBSCRIBED) == BV_DROP && ble_dev_step(0, BLE_RECEIVING) == BV_DROP,
          "device step: not wanted while linked -> disconnect");
    CHECK(ble_dev_step(0, BLE_IDLE) == BV_NONE && ble_dev_step(1, BLE_OFF) == BV_NONE &&
          ble_dev_step(1, BLE_STARTING) == BV_NONE && ble_dev_step(1, BLE_FAILED) == BV_NONE,
          "device step: not started, failed or idle and unwanted -> nothing");
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
        {   /* the FM-Link characteristic F3110001-6E4C-4A5B-9C2D-1F3E5D7A9B0C, both orders; not the MIDI one */
            static const uint8_t fbe[16] = {0xF3, 0x11, 0x00, 0x01, 0x6E, 0x4C, 0x4A, 0x5B, 0x9C, 0x2D, 0x1F, 0x3E, 0x5D, 0x7A, 0x9B, 0x0C};
            uint8_t fle[16];
            int k;
            for (k = 0; k < 16; k++)
                fle[k] = fbe[15 - k];
            CHECK(ble_is_fml_char(fbe) && ble_is_fml_char(fle), "FM-Link characteristic UUID, both orders");
            CHECK(!ble_is_fml_char(be) && !ble_is_midi_char(fbe), "FM-Link's and MIDI's UUIDs are told apart");
        }
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
        CHECK(ble_cmd_parse("") == BLE_CMD_STATUS && ble_cmd_parse("  ") == BLE_CMD_STATUS, "ble: status");
        CHECK(ble_cmd_parse("start") == BLE_CMD_START && ble_cmd_parse(" scan ") == BLE_CMD_SCAN &&
              ble_cmd_parse("list") == BLE_CMD_LIST && ble_cmd_parse("stop") == BLE_CMD_STOP, "ble: words");
        CHECK(ble_cmd_parse("connect 3") == BLE_CMD_BAD && ble_cmd_parse("connect") == BLE_CMD_BAD,
              "ble: no connect word (the saved devices pick each link's device; the core overrode a console pick)");
        CHECK(ble_cmd_parse("starts") == BLE_CMD_BAD && ble_cmd_parse("start now") == BLE_CMD_BAD &&
              ble_cmd_parse("blah") == BLE_CMD_BAD, "ble: unknown words");
    }
    {   /* which state each command needs (Review Focus 1) */
        CHECK(!ble_cmd_refuse(BLE_CMD_START, BLE_OFF), "start from OFF");
        CHECK(ble_cmd_refuse(BLE_CMD_START, BLE_IDLE) && ble_cmd_refuse(BLE_CMD_START, BLE_FAILED),
              "start twice / after a failure refused");
        CHECK(ble_cmd_refuse(BLE_CMD_SCAN, BLE_OFF) && ble_cmd_refuse(BLE_CMD_SCAN, BLE_STARTING) &&
              ble_cmd_refuse(BLE_CMD_SCAN, BLE_RECEIVING) && !ble_cmd_refuse(BLE_CMD_SCAN, BLE_IDLE),
              "scan only from IDLE");
        CHECK(!ble_cmd_refuse(BLE_CMD_STOP, BLE_CONNECTING) && !ble_cmd_refuse(BLE_CMD_STOP, BLE_RECEIVING)
              && ble_cmd_refuse(BLE_CMD_STOP, BLE_OFF) && ble_cmd_refuse(BLE_CMD_STOP, BLE_IDLE),
              "stop: only while connecting or connected");
        CHECK(!ble_cmd_refuse(BLE_CMD_LIST, BLE_OFF) && !ble_cmd_refuse(BLE_CMD_STATUS, BLE_OFF),
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
    {   /* the host's two link slots (CTRL, FM1) and its scanner: one decision per pass (ble_host_step) */
        ble_host_t h;
        uint32_t sl, k, on, off, now;
        const uint32_t T = 100000u;
        #define HCLR() (memset(&h, 0, sizeof h), h.want = 1)
        HCLR();
        h.s[0].target = h.s[0].listed = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_CONNECT && sl == 0u, "connect_listed: IDLE, its target listed, rested -> connect it");
        HCLR();
        h.s[1].target = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_SCAN_ON, "scan_for_unlisted: a target not listed -> scan");
        HCLR();
        h.s[1].target = 1;
        h.scan_on = 1;
        h.scan_until = T;
        CHECK(ble_host_step(&h, T - 1u, &sl) == BH_NONE && ble_host_step(&h, T, &sl) == BH_SCAN_OFF,
              "scan_window: a background scan ends after its window");
        h.scan_on = 0;
        h.scan_rest = T + 2000u;
        CHECK(ble_host_step(&h, T + 1999u, &sl) == BH_NONE && ble_host_step(&h, T + 2000u, &sl) == BH_SCAN_ON,
              "scan_window: then 2 s of rest, then the next window");
        HCLR();
        h.s[0].st = h.s[1].st = HS_UP;
        h.s[0].target = h.s[1].target = h.s[0].listed = h.s[1].listed = 1;
        h.scan_want = 1;
        h.scan_rest = T + 5000u;                    /* (the page's scan: no rest) */
        CHECK(ble_host_step(&h, T, &sl) == BH_SCAN_ON, "scan_beside_links: both links up, the page wants a scan -> scan");
        h.scan_on = 1;
        h.scan_until = T;
        CHECK(ble_host_step(&h, T + 60000u, &sl) == BH_NONE, "scan_beside_links: the page's scan runs on, no link dropped");
        HCLR();
        h.s[0].target = h.s[1].target = 1;          /* nothing listed: windows of 10 s with 2 s rests */
        for (on = off = 0, now = T; now < T + 12000u; now += 10u) {
            k = ble_host_step(&h, now, &sl);
            if (k == BH_SCAN_ON) {
                on++;
                h.scan_on = 1;
                h.scan_until = now + 10000u;
            } else if (k == BH_SCAN_OFF) {
                off++;
                h.scan_on = 0;
                h.scan_rest = now + 2000u;
            }
        }
        CHECK(on == 1u && off == 1u, "scan_rest: nothing around -> one 10 s window and one rest in 12 s");
        HCLR();
        h.s[0].st = HS_CONNECTING;
        h.s[0].deadline_ms = T + 5000u;
        h.s[1].target = h.s[1].listed = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_NONE, "one_at_a_time: a connect running -> the other slot waits");
        h.scan_on = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_SCAN_OFF, "one_at_a_time: .. and a scan stops");
        h.scan_on = 0;
        CHECK(ble_host_step(&h, T + 5000u, &sl) == BH_CANCEL && sl == 0u, "connect_deadline: 5 s -> cancel");
        HCLR();
        h.s[1].st = HS_DISCOVERING;
        h.s[1].target = 1;
        h.s[1].deadline_ms = T;
        CHECK(ble_host_step(&h, T, &sl) == BH_DROP && sl == 1u, "discovery_deadline: -> drop");
        HCLR();
        h.s[0].st = HS_UP;
        h.s[0].target = h.s[0].mismatch = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_DROP && sl == 0u, "mismatch_drops: linked to another than its target -> drop");
        h.s[0].dropping = 1;
        CHECK(ble_host_step(&h, T, &sl) != BH_DROP, "mismatch_drops: asked once");
        HCLR();
        h.want = 0;
        h.s[0].st = HS_UP;
        h.s[0].target = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_DROP && sl == 0u, "bt_off: a link drops");
        h.s[0].st = HS_CONNECTING;
        CHECK(ble_host_step(&h, T, &sl) == BH_CANCEL, "bt_off: a connect is cancelled");
        h.s[0].st = HS_IDLE;
        h.scan_on = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_SCAN_OFF, "bt_off: a scan stops");
        h.scan_on = 0;
        CHECK(ble_host_step(&h, T, &sl) == BH_NONE, "bt_off: then nothing");
        HCLR();
        h.s[0].target = h.s[0].listed = h.s[0].tried = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_SCAN_ON, "tried_waits: a failed connect -> not again until it is seen again (a scan)");
        HCLR();
        h.s[0].target = h.s[0].listed = 1;
        h.s[0].rest_ms = T + 2000u;
        CHECK(ble_host_step(&h, T, &sl) != BH_CONNECT && ble_host_step(&h, T + 2000u, &sl) == BH_CONNECT,
              "rest: 2 s after a failure or a drop before the next connect");
        HCLR();
        h.s[0].target = h.s[0].listed = 1;
        h.scan_on = 1;
        h.scan_until = T + 9000u;
        CHECK(ble_host_step(&h, T, &sl) == BH_SCAN_OFF, "connect_listed: scanning -> the scan stops first");
        HCLR();                                     /* final review I4: the background search is windowed */
        h.scan_bg = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_SCAN_ON, "scan_bg: a background search -> a scan");
        h.scan_on = 1;
        h.scan_until = T + 10000u;
        CHECK(ble_host_step(&h, T + 9999u, &sl) == BH_NONE && ble_host_step(&h, T + 10000u, &sl) == BH_SCAN_OFF,
              "scan_bg: in windows (not continuous)");
        HCLR();                                     /* final review I5: a cancel is busy until its completion */
        h.s[0].st = HS_CANCELLING;
        h.s[1].target = h.s[1].listed = 1;
        CHECK(ble_host_step(&h, T, &sl) == BH_NONE, "cancelling_busy: the other slot waits for the cancel's completion");
        CHECK(ble_fail_rest(0, 0) == 2000u && ble_fail_rest(1, 0) == 4000u && ble_fail_rest(2, 0) == 8000u &&
              ble_fail_rest(3, 0) == 16000u && ble_fail_rest(9, 0) == 16000u,
              "fail_rest: stopped: 2 s after a drop or a failed connect, doubling to 16 s");
        CHECK(ble_fail_rest(0, 1) == 8000u && ble_fail_rest(1, 1) == 16000u && ble_fail_rest(5, 1) == 16000u,
              "fail_rest: slow (RECONNECT SLOW, AUTO playing): 8 s, then 16 s (a refused try holds the audio off ~12 ms)");
        #undef HCLR
    }
    printf(fails ? "ble_central: %d FAILED\n" : "ble_central: all checks passed\n", fails);
    return fails != 0;
}
