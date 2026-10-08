/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
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
    printf(fails ? "ble_central: %d FAILED\n" : "ble_central: all checks passed\n", fails);
    return fails != 0;
}
