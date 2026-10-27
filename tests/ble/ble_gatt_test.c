/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE */
/* The device role's data (firmware/src/ble/ble_gatt.c): the GATT table as the JieLi SDK's att_server_init reads it
 * (records of size, flags, handle, UUID 16/128, value, all little endian, ended by 0x0000; le_trans_data.h), the
 * advertising data and the scan response (AD structures, at most 31 bytes each). Every record walked: the handles in
 * order, the BLE-MIDI service and characteristic (MMA BLE-MIDI 1.0) with the right UUIDs, properties and flags, its
 * CCCD, the GAP device name; the FM-Link service, characteristic and CCCD after them. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../firmware/src/ble/ble_gatt.c"
static int fails;
#define CHECK(c, what) do { if (c) printf("ok    %s\n", what); else { printf("FAIL  %s\n", what); fails++; } } while (0)

/* the UUIDs as written, most significant byte first: the table stores them reversed */
static const uint8_t SVC[16] = {0x03, 0xB8, 0x0E, 0x5A, 0xED, 0xE8, 0x4B, 0x33, 0xA7, 0x51, 0x6C, 0xE3, 0x4E, 0xC4, 0xC7, 0x00};
static const uint8_t CHR[16] = {0x77, 0x72, 0xE5, 0xDB, 0x38, 0x68, 0x41, 0x12, 0xA1, 0xA9, 0xF2, 0x66, 0x9D, 0x10, 0x6B, 0xF3};
static const uint8_t FSVC[16] = {0xF3, 0x11, 0x00, 0x00, 0x6E, 0x4C, 0x4A, 0x5B, 0x9C, 0x2D, 0x1F, 0x3E, 0x5D, 0x7A, 0x9B, 0x0C};
static const uint8_t FCHR[16] = {0xF3, 0x11, 0x00, 0x01, 0x6E, 0x4C, 0x4A, 0x5B, 0x9C, 0x2D, 0x1F, 0x3E, 0x5D, 0x7A, 0x9B, 0x0C};
static int reversed(const uint8_t *p, const uint8_t *uuid)
{
    int i;
    for (i = 0; i < 16; i++)
        if (p[i] != uuid[15 - i])
            return 0;
    return 1;
}
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
typedef struct { uint32_t size, flags, handle; const uint8_t *uuid; uint32_t uuid_len; const uint8_t *val; uint32_t val_len; } rec_t;
static uint32_t walk(rec_t *r, uint32_t max)
{
    const uint8_t *p = BLE_GATT_DB;
    uint32_t n = 0, size;
    while ((size = rd16(p)) != 0u && n < max) {
        rec_t *x = &r[n++];
        x->size = size;
        x->flags = rd16(p + 2);
        x->handle = rd16(p + 4);
        x->uuid = p + 6;
        x->uuid_len = (x->flags & 0x0200u) ? 16u : 2u;
        x->val = p + 6 + x->uuid_len;
        x->val_len = size - 6u - x->uuid_len;
        p += size;
    }
    return (uint32_t)(p - BLE_GATT_DB) + 2u == sizeof BLE_GATT_DB && rd16(p) == 0u ? n : 0u;
}

int main(void)
{
    rec_t r[16];
    uint32_t n = walk(r, 16), i, ok;
    const uint8_t *a;
    CHECK(n == 11u, "the table: 11 records, ended by 0x0000 at its last bytes");
    for (i = 0, ok = n != 0u; i < n; i++)
        ok &= r[i].handle == i + 1u && r[i].size >= 8u;
    CHECK(ok, "the handles run 1, 2, 3 ...");
    CHECK(n >= 3u && r[0].flags == 0x0002u && rd16(r[0].uuid) == 0x2800u && rd16(r[0].val) == 0x1800u, "1: the GAP service (0x1800)");
    CHECK(n >= 3u && rd16(r[1].uuid) == 0x2803u && r[1].val[0] == 0x02u && rd16(r[1].val + 1) == BLE_GATT_NAME &&
          rd16(r[1].val + 3) == 0x2A00u, "2: the device name characteristic, READ, its value at BLE_GATT_NAME");
    CHECK(n >= 3u && r[2].handle == BLE_GATT_NAME && r[2].flags == 0x0102u && rd16(r[2].uuid) == 0x2A00u,
          "3: the device name's value: READ | DYNAMIC (att_read_callback gives it)");
    CHECK(n >= 4u && r[3].flags == 0x0002u && rd16(r[3].uuid) == 0x2800u && r[3].val_len == 16u && reversed(r[3].val, SVC),
          "4: the BLE-MIDI service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700");
    CHECK(n >= 5u && rd16(r[4].uuid) == 0x2803u && r[4].val[0] == 0x16u && rd16(r[4].val + 1) == BLE_GATT_MIDI &&
          r[4].val_len == 19u && reversed(r[4].val + 3, CHR),
          "5: the MIDI characteristic 7772E5DB-...: READ | WRITE WITHOUT RESPONSE | NOTIFY, its value at BLE_GATT_MIDI");
    CHECK(n >= 6u && r[5].handle == BLE_GATT_MIDI && r[5].flags == 0x0316u && r[5].uuid_len == 16u &&
          reversed(r[5].uuid, CHR) && r[5].val_len == 0u, "6: the MIDI value: those properties, DYNAMIC, a 128-bit UUID, no stored value");
    CHECK(n >= 7u && r[6].handle == BLE_GATT_MIDI_CCC && BLE_GATT_MIDI_CCC == BLE_GATT_MIDI + 1u && r[6].flags == 0x010Au &&
          rd16(r[6].uuid) == 0x2902u && r[6].val_len == 2u && rd16(r[6].val) == 0u,
          "7: its CCCD right after it (notifications off until the host turns them on)");
    CHECK(n >= 8u && r[7].flags == 0x0002u && rd16(r[7].uuid) == 0x2800u && r[7].val_len == 16u && reversed(r[7].val, FSVC),
          "8: the FM-Link service F3110000-6E4C-4A5B-9C2D-1F3E5D7A9B0C");
    CHECK(n >= 9u && rd16(r[8].uuid) == 0x2803u && r[8].val[0] == 0x14u && rd16(r[8].val + 1) == BLE_GATT_FML &&
          r[8].val_len == 19u && reversed(r[8].val + 3, FCHR),
          "9: the FM-Link characteristic F3110001-...: WRITE WITHOUT RESPONSE | NOTIFY, its value at BLE_GATT_FML");
    CHECK(n >= 10u && r[9].handle == BLE_GATT_FML && r[9].flags == 0x0314u && r[9].uuid_len == 16u &&
          reversed(r[9].uuid, FCHR) && r[9].val_len == 0u, "10: the FM-Link value: those properties, DYNAMIC, a 128-bit UUID");
    CHECK(n >= 11u && r[10].handle == BLE_GATT_FML_CCC && BLE_GATT_FML_CCC == BLE_GATT_FML + 1u && r[10].flags == 0x010Au &&
          rd16(r[10].uuid) == 0x2902u && r[10].val_len == 2u && rd16(r[10].val) == 0u, "11: its CCCD right after it");
    /* the advertising data: flags, the service UUID; the scan response: the name */
    a = BLE_ADV_DATA;
    CHECK(sizeof BLE_ADV_DATA <= 31u && sizeof BLE_RSP_DATA <= 31u, "advertising data and scan response: 31 bytes at most");
    CHECK(a[0] == 2u && a[1] == 0x01u && a[2] == 0x06u, "advertising: flags LE general discoverable, no BR/EDR");
    CHECK(a[3] == 17u && a[4] == 0x07u && reversed(a + 5, SVC) && sizeof BLE_ADV_DATA == 21u,
          "advertising: the complete list of 128-bit services, the BLE-MIDI service");
    CHECK(BLE_RSP_DATA[0] == sizeof BLE_RSP_DATA - 1u && BLE_RSP_DATA[1] == 0x09u &&
          !memcmp(BLE_RSP_DATA + 2, BLE_GATT_NAME_STR, sizeof BLE_RSP_DATA - 2u) &&
          sizeof BLE_RSP_DATA - 2u == sizeof BLE_GATT_NAME_STR - 1u && !strcmp(BLE_GATT_NAME_STR, "Felucca FM-1"),
          "scan response: the complete local name, Felucca FM-1");
    printf(fails ? "ble_gatt: %d FAILED\n" : "ble_gatt: all checks passed\n", fails);
    return fails != 0;
}
