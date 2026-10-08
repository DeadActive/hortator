/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
#include <stdio.h>
#include <stdint.h>
#define BLE_CENTRAL_STATE_ONLY 1
#include <string.h>
#include "../../firmware/src/ble/ble_central.c"
static int fails;
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
    printf(fails ? "ble_central: %d FAILED\n" : "ble_central: all checks passed\n", fails);
    return fails != 0;
}
