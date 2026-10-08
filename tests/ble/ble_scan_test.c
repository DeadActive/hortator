/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../firmware/src/ble/ble_scan.c"

static int fails;
#define CHECK(c, what) do { if (c) printf("ok    %s\n", what); else { printf("FAIL  %s\n", what); fails++; } } while (0)
static const uint8_t T_MIDI_UUID_LE[16] = {0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
                                         0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03};

int main(void)
{
    static ble_scan_t s;
    uint8_t a[6] = {1, 2, 3, 4, 5, 6}, idx[BLE_SCAN_MAX];
    uint8_t ad[40];
    uint32_t n = 0, k, cnt;
    /* flags + complete name "SMC-Mixer" + complete 128-bit list with the MIDI service */
    ad[n++] = 2; ad[n++] = 0x01; ad[n++] = 0x06;
    ad[n++] = 10; ad[n++] = 0x09; memcpy(ad + n, "SMC-Mixer", 9); n += 9;
    ad[n++] = 17; ad[n++] = 0x07; memcpy(ad + n, T_MIDI_UUID_LE, 16); n += 16;
    ble_scan_clear(&s);
    ble_scan_report(&s, a, 0, -60, ad, n);
    cnt = ble_scan_sorted(&s, idx);
    CHECK(cnt == 1 && !strcmp(s.dev[idx[0]].name, "SMC-Mixer") && s.dev[idx[0]].midi && s.dev[idx[0]].rssi == -60,
          "name, MIDI tag, rssi from one report");
    /* the same address again, name-less scan response: no duplicate, name and tag kept, rssi updated */
    ble_scan_report(&s, a, 0, -50, ad, 0);
    cnt = ble_scan_sorted(&s, idx);
    CHECK(cnt == 1 && !strcmp(s.dev[idx[0]].name, "SMC-Mixer") && s.dev[idx[0]].midi && s.dev[idx[0]].rssi == -50,
          "same address: merged, not duplicated");
    /* a scan response that only carries a shortened name merges into an entry first seen without a name */
    {
        uint8_t b[6] = {9, 9, 9, 9, 9, 9}, sr[8] = {5, 0x08, 'K', 'E', 'Y', 'S'};
        ble_scan_report(&s, b, 1, -70, ad, 3);    /* flags only */
        ble_scan_report(&s, b, 1, -70, sr, 6);
        cnt = ble_scan_sorted(&s, idx);
        CHECK(cnt == 2 && !strcmp(s.dev[idx[1]].name, "KEYS") && !s.dev[idx[1]].midi, "scan response name merged");
    }
    /* fill to 16 with weak devices, then a strong new one replaces the weakest */
    for (k = 0; k < 20; k++) {
        uint8_t c[6] = {0xAA, (uint8_t)k, 0, 0, 0, 0};
        ble_scan_report(&s, c, 0, (int8_t)(-90 - (int)(k % 5)), ad, 3);
    }
    cnt = ble_scan_sorted(&s, idx);
    CHECK(cnt == BLE_SCAN_MAX, "capped at 16");
    {
        uint8_t c[6] = {0xBB, 0, 0, 0, 0, 0};
        ble_scan_report(&s, c, 0, -40, ad, 3);
        cnt = ble_scan_sorted(&s, idx);
        CHECK(cnt == BLE_SCAN_MAX && s.dev[idx[0]].addr[0] == 0xBB, "a stronger device replaces the weakest");
        for (k = 1; k < cnt; k++)
            if (s.dev[idx[k - 1]].rssi < s.dev[idx[k]].rssi)
                break;
        CHECK(k == cnt, "sorted strongest first");
    }
    /* a malformed AD (length past the end) does not crash or read past */
    {
        uint8_t bad[4] = {30, 0x09, 'X', 'Y'}, d[6] = {7, 7, 7, 7, 7, 7};
        ble_scan_report(&s, d, 0, -30, bad, 4);
        cnt = ble_scan_sorted(&s, idx);
        CHECK(s.dev[idx[0]].addr[0] == 7 && s.dev[idx[0]].name[0] == 0, "malformed AD ignored safely");
    }
    CHECK(s.reports == 26, "reports counted");   /* 1 + 1 + 2 + 20 + 1 + 1 calls */
    printf(fails ? "ble_scan: %d FAILED\n" : "ble_scan: all checks passed\n", fails);
    return fails != 0;
}
