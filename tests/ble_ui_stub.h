/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The BLE unit's entry points the core calls (firmware/src/ble/ble_core.h), as host stubs for the FELUCCA_BLE core
 * tests (after ui_host.h): the connected device's name, the Bluetooth page's ble_ui_* (state the test sets in `bs`,
 * the calls counted) and ble_user_start. Each test defines ble_midi_send. */
#define memset felucca_memset                         /* (ble_core.h declares libc.c's, as the firmware's) */
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "../firmware/src/ble/ble_core.h"
#undef memset
#undef memcpy
#undef memcmp
static char conn_name[24];
static int conn_up;
int ble_ui_conn_name(char out[24])                   /* (the BLE unit: SUBSCRIBED / RECEIVING only) */
{
    if (!conn_up)
        return 0;
    memcpy(out, conn_name, 24);
    return 1;
}
static struct {
    uint32_t state, left, ndev, code;                /* ble_ui_state, ble_ui_scan_left, the list's count, the error */
    const char *err;
    ble_ui_dev_t dev[16];                            /* the list in ble_scan_sorted's order (by signal) */
    uint32_t starts, scans, targets, wants, pauses;  /* calls */
    int want, pause;                                 /* the last values */
    uint8_t taddr[6];
    uint32_t ttype;
} bs;
uint32_t ble_ui_state(void) { return bs.state; }
const char *ble_ui_err(uint32_t *code) { *code = bs.code; return bs.err ? bs.err : ""; }
uint32_t ble_ui_scan_left(void) { return bs.left; }
uint32_t ble_ui_devs(void) { return bs.ndev; }
int ble_ui_dev(uint32_t i, ble_ui_dev_t *out)
{
    if (i >= bs.ndev)
        return 0;
    *out = bs.dev[i];
    return 1;
}
void ble_ui_scan(void) { bs.scans++; }
void ble_ui_target(const uint8_t addr[6], uint32_t type)
{
    bs.targets++;
    memcpy(bs.taddr, addr, 6);
    bs.ttype = type;
}
void ble_ui_want(int on) { bs.wants++; bs.want = on; }
void ble_ui_pause(int on) { bs.pauses++; bs.pause = on; }
void ble_user_start(void)
{
    bs.starts++;
    if (bs.state == BLE_ST_OFF)
        bs.state = BLE_ST_STARTING;
}
