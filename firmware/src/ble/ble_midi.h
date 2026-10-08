/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE */
/* BLE-MIDI (MMA "MIDI over Bluetooth Low Energy" 1.0) packets -> USB-MIDI event packets, cable 0. */
#ifndef BLE_MIDI_H
#define BLE_MIDI_H
#include <stdint.h>
typedef void (*ble_midi_out_fn)(void *ctx, uint32_t usb_pkt);
typedef struct {
    uint8_t status, need, got, d0, running;
    uint8_t in_sysex, sx_n, sx_buf[3];
    uint32_t sx_len;
    uint32_t packets, msgs, errors, sysex_done;
} ble_midi_t;
void ble_midi_reset(ble_midi_t *m);
void ble_midi_parse(ble_midi_t *m, const uint8_t *p, uint32_t n, ble_midi_out_fn out, void *ctx);
#endif
