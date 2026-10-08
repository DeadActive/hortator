/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
/* ble_midi.c: BLE-MIDI packets (MMA / Apple) -> USB-MIDI event packets. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../firmware/src/ble/ble_midi.c"

static uint32_t got[64];
static int ngot;
static void out(void *ctx, uint32_t pkt) { (void)ctx; if (ngot < 64) got[ngot++] = pkt; }
static int fails;
#define CHECK(c, what) do { if (c) printf("ok    %s\n", what); else { printf("FAIL  %s\n", what); fails++; } } while (0)
#define PKT(cin, a, b, c) ((uint32_t)(cin) | (uint32_t)(a) << 8 | (uint32_t)(b) << 16 | (uint32_t)(c) << 24)

static void run(ble_midi_t *m, const uint8_t *p, uint32_t n) { ble_midi_parse(m, p, n, out, 0); }

int main(void)
{
    ble_midi_t m;
    {   /* one note on: header, timestamp, 90 3C 64 */
        static const uint8_t p[] = {0x80, 0x80, 0x90, 0x3C, 0x64};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 1 && got[0] == PKT(0x09, 0x90, 0x3C, 0x64), "single note on");
    }
    {   /* running status, the second message without status and without timestamp */
        static const uint8_t p[] = {0x80, 0x80, 0x90, 0x3C, 0x64, 0x3E, 0x64};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 2 && got[1] == PKT(0x09, 0x90, 0x3E, 0x64), "running status, no timestamp");
    }
    {   /* running status with a timestamp before the second message */
        static const uint8_t p[] = {0x80, 0x80, 0xB0, 0x07, 0x40, 0x81, 0x07, 0x41};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 2 && got[1] == PKT(0x0B, 0xB0, 0x07, 0x41), "running status with timestamp");
    }
    {   /* several full messages, program change (2 bytes), pitch bend */
        static const uint8_t p[] = {0x80, 0x80, 0xC1, 0x05, 0x80, 0xE1, 0x00, 0x48};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 2 && got[0] == PKT(0x0C, 0xC1, 0x05, 0) && got[1] == PKT(0x0E, 0xE1, 0x00, 0x48),
              "program change then pitch bend");
    }
    {   /* timestamp low wraps (0xFF then 0x80): still two messages */
        static const uint8_t p[] = {0x80, 0xFF, 0x90, 0x3C, 0x64, 0x80, 0x80, 0x3C, 0x00};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 2 && got[1] == PKT(0x08, 0x80, 0x3C, 0x00), "timestamp wrap");
    }
    {   /* real-time byte between messages */
        static const uint8_t p[] = {0x80, 0x80, 0xF8, 0x80, 0x90, 0x3C, 0x64};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 2 && got[0] == PKT(0x0F, 0xF8, 0, 0) && got[1] == PKT(0x09, 0x90, 0x3C, 0x64), "real-time");
    }
    {   /* SysEx in one packet: F0 7E 7F 06 01 F7 */
        static const uint8_t p[] = {0x80, 0x80, 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0x81, 0xF7};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 2 && got[0] == PKT(0x04, 0xF0, 0x7E, 0x7F) && got[1] == PKT(0x07, 0x06, 0x01, 0xF7)
              && m.sysex_done == 1 && m.sx_len == 6, "sysex in one packet");
    }
    {   /* SysEx across two packets; the second starts with plain data; a note follows */
        static const uint8_t p1[] = {0x80, 0x80, 0xF0, 0x41, 0x10};
        static const uint8_t p2[] = {0x80, 0x42, 0x12, 0x81, 0xF7, 0x82, 0x90, 0x40, 0x7F};
        ble_midi_reset(&m); ngot = 0; run(&m, p1, sizeof p1); run(&m, p2, sizeof p2);
        CHECK(m.sysex_done == 1 && m.sx_len == 6 && ngot == 3 && got[0] == PKT(0x04, 0xF0, 0x41, 0x10)
              && got[1] == PKT(0x07, 0x42, 0x12, 0xF7) && got[2] == PKT(0x09, 0x90, 0x40, 0x7F),
              "sysex across packets, then a note");
    }
    {   /* malformed: no header bit, data with no status, empty */
        static const uint8_t bad1[] = {0x00, 0x80, 0x90, 0x3C, 0x64};
        static const uint8_t bad2[] = {0x80, 0x80, 0x3C, 0x64};
        ble_midi_reset(&m); ngot = 0;
        run(&m, bad1, sizeof bad1); run(&m, bad2, sizeof bad2); run(&m, bad1, 0); run(&m, bad1, 1);
        CHECK(ngot == 0 && m.errors >= 3, "malformed packets counted, nothing emitted");
    }
    {   /* a truncated message at the packet end is dropped, the next packet parses */
        static const uint8_t p1[] = {0x80, 0x80, 0x90, 0x3C};
        static const uint8_t p2[] = {0x80, 0x80, 0x90, 0x3D, 0x10};
        ble_midi_reset(&m); ngot = 0; run(&m, p1, sizeof p1); run(&m, p2, sizeof p2);
        CHECK(ngot == 1 && got[0] == PKT(0x09, 0x90, 0x3D, 0x10) && m.errors == 1, "truncated message dropped");
    }
    {   /* review #6a: inside SysEx every status byte follows a timestamp; FF here is the timestamp before F7 */
        static const uint8_t p[] = {0x80, 0x80, 0xF0, 0x01, 0x02, 0xFF, 0xF7};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 2 && got[0] == PKT(0x04, 0xF0, 0x01, 0x02) && got[1] == PKT(0x05, 0xF7, 0, 0),
              "SysEx: a timestamp >= F8 before F7 is not a System Reset");
    }
    {   /* #6b: a timestamped real-time byte inside SysEx */
        static const uint8_t p[] = {0x80, 0x80, 0xF0, 0x01, 0x81, 0xF8, 0x02, 0x82, 0xF7};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 3 && got[0] == PKT(0x0F, 0xF8, 0, 0) && got[1] == PKT(0x04, 0xF0, 0x01, 0x02) &&
              got[2] == PKT(0x05, 0xF7, 0, 0), "SysEx: timestamp + real-time inside it");
    }
    {   /* #6c: a timestamped real-time byte inside a running message */
        static const uint8_t p[] = {0x80, 0x80, 0x90, 0x3C, 0x81, 0xF8, 0x64};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(ngot == 2 && got[0] == PKT(0x0F, 0xF8, 0, 0) && got[1] == PKT(0x09, 0x90, 0x3C, 0x64) && !m.errors,
              "real-time with its timestamp inside a note on");
    }
    {   /* #6d: a SysEx with no F7, then a timestamp and a new status: the SysEx ends (error), the note gets through */
        static const uint8_t p[] = {0x80, 0x80, 0xF0, 0x01, 0x81, 0x90, 0x3C, 0x64};
        ble_midi_reset(&m); ngot = 0; run(&m, p, sizeof p);
        CHECK(!m.in_sysex && m.errors == 1 && ngot >= 1 && got[ngot - 1] == PKT(0x09, 0x90, 0x3C, 0x64),
              "an unterminated SysEx ends at the next status");
    }
    printf(fails ? "ble_midi: %d FAILED\n" : "ble_midi: all checks passed\n", fails);
    return fails != 0;
}
