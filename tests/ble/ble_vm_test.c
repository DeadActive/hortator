/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../firmware/src/ble/ble_vm.c"
static int fails;
#define CHECK(c, what) do { if (c) printf("ok    %s\n", what); else { printf("FAIL  %s\n", what); fails++; } } while (0)
/* the stock firmware's config sector on the test FM-1 (flash 0xE8000, read over the console's flr): the sector
 * magic, the crystal trim (id 106), the PA trim (id 107), the RF trim record (id 187, 68 bytes) */
static const uint8_t SECTOR[] = {
    0x55, 0xAA, 0xAA, 0x55, 0x91, 0x6A, 0x20, 0x00, 0x0B, 0x0B, 0x2C, 0x6B, 0x70, 0x00, 0x01, 0x07,
    0x04, 0x07, 0x0B, 0x01, 0x07, 0x17, 0xBB, 0x40, 0x04, 0xFF, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xE9, 0x03, 0x00, 0x00, 0xD2, 0xFF, 0xFF, 0xFF, 0x17, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x2E, 0x37, 0xFF, 0xF8, 0x00, 0xF6, 0x00, 0x00, 0x92, 0x00, 0x00, 0x00, 0xF6, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x89, 0x3B, 0xFF, 0xFF, 0x89, 0x3B, 0xD0, 0x02, 0x28, 0x00, 0xE0, 0x02, 0x2C,
    0x00, 0xB6, 0x02, 0x1A, 0x00, 0x92, 0x02, 0x0A, 0x00, 0x0F, 0x75, 0x00, 0x00, 0x00, 0x6C, 0x40,
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
int main(void)
{
    uint8_t buf[512], out[68];
    memset(buf, 0xFF, sizeof buf);
    memcpy(buf + 40, SECTOR, sizeof SECTOR);
    CHECK(ble_vm_rf_trim(buf, sizeof buf, out) == 1, "finds the RF trim record in the stock config sector");
    CHECK(!memcmp(out, SECTOR + 25, 68), "returns its 68 bytes (64 trim values + CRC)");
    CHECK(out[64] == 0x0F && out[65] == 0x75, "the record's own CRC16 is part of it (the library checks it again)");
    {   /* a newer version further on wins (the config store appends) */
        uint8_t b2[512];
        memset(b2, 0xFF, sizeof b2);
        memcpy(b2, SECTOR + 21, 72);
        memcpy(b2 + 200, SECTOR + 21, 72);
        b2[200 + 4 + 1] = 0x04;                  /* change a trim value: E9 03 -> ... and fix both CRCs */
        b2[200 + 4 + 64] = 0;
        b2[200 + 4 + 65] = 0;
        {
            uint32_t c = ble_vm_crc16(b2 + 204, 64);
            b2[200 + 4 + 64] = (uint8_t)c;
            b2[200 + 4 + 65] = (uint8_t)(c >> 8);
            b2[200] = (uint8_t)ble_vm_crc16(b2 + 204, 68);
        }
        CHECK(ble_vm_rf_trim(b2, sizeof b2, out) == 1 && out[1] == 0x04, "the last valid version wins");
    }
    {
        uint8_t b3[512];
        memset(b3, 0xFF, sizeof b3);
        memcpy(b3 + 10, SECTOR, sizeof SECTOR);
        b3[10 + 30] ^= 1;                        /* one trim byte flipped: inner CRC fails */
        CHECK(ble_vm_rf_trim(b3, sizeof b3, out) == 0, "a damaged record is refused");
        memcpy(b3 + 10, SECTOR, sizeof SECTOR);
        b3[10 + 22] = 0xBC;                      /* another item id */
        CHECK(ble_vm_rf_trim(b3, sizeof b3, out) == 0, "another item is not the RF trim");
        memset(b3, 0xFF, sizeof b3);
        CHECK(ble_vm_rf_trim(b3, sizeof b3, out) == 0, "an erased sector has none");
        CHECK(ble_vm_rf_trim(SECTOR, 60, out) == 0, "a record cut off by the buffer end is not read");
    }
    printf(fails ? "ble_vm: %d FAILED\n" : "ble_vm: all checks passed\n", fails);
    return fails != 0;
}
