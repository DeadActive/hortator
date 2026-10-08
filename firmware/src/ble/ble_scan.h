/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
#ifndef BLE_SCAN_H
#define BLE_SCAN_H
#include <stdint.h>
#define BLE_SCAN_MAX 16
typedef struct {
    uint8_t addr[6], addr_type, midi, used;
    int8_t rssi;
    char name[24];
} ble_dev_t;
typedef struct { ble_dev_t dev[BLE_SCAN_MAX]; uint32_t reports; } ble_scan_t;
void ble_scan_clear(ble_scan_t *s);
void ble_scan_report(ble_scan_t *s, const uint8_t addr[6], uint8_t addr_type, int8_t rssi,
                     const uint8_t *ad, uint32_t ad_len);
uint32_t ble_scan_sorted(const ble_scan_t *s, uint8_t idx[BLE_SCAN_MAX]);
#endif
