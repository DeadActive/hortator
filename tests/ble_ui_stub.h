/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* (felucca-ble ble-kit's, with Hortator's clean-boot hook) */
/* The BLE unit's entry points the core calls (firmware/src/ble/ble_core.h), as host stubs for the FELUCCA_BLE core
 * tests (after ui_host.h): the connected device's name, the Bluetooth page's ble_ui_* (state the test sets in `bs`,
 * the calls counted) and ble_user_start. Each test defines ble_midi_send; ble_fml_send (FM-Link out) sends nothing
 * unless the test defines BLE_FML_SEND_OWN and its own. */
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
uint32_t ble_ui_devs(void) { return bs.ndev; }
int ble_ui_dev(uint32_t i, ble_ui_dev_t *out)
{
    if (i >= bs.ndev)
        return 0;
    *out = bs.dev[i];
    return 1;
}
static struct {                                     /* the host's slots as the test sets them; the targets as set */
    ble_ui_slot_t slot[2];
    uint8_t tgt[2][6], tgt_type[2];
    uint32_t targets[2];                             /* ble_ui_slot_target calls per slot */
    int scan_want;
} bh;
void ble_ui_slot_target(uint32_t slot, const uint8_t addr[6], uint32_t type)
{
    if (slot < 2u) {
        bh.targets[slot]++;
        memcpy(bh.tgt[slot], addr, 6);
        bh.tgt_type[slot] = (uint8_t)type;
    }
}
int ble_ui_slot(uint32_t slot, ble_ui_slot_t *out)
{
    if (slot >= 2u)
        return 0;
    *out = bh.slot[slot];
    return 1;
}
void ble_ui_scan_want(int on) { bs.scans += on && !bh.scan_want; bh.scan_want = on; }
void ble_ui_want(int on) { bs.wants++; bs.want = on; }
static int bs_role = -1;                             /* the last ble_ui_role (-1 none) */
void ble_ui_role(int device) { bs_role = device; }
uint32_t ble_ui_role_now(void) { return bs_role > 0; }
int ble_fml_ready(void) { return conn_up; }          /* (a link up: the test's) */
void ble_user_start(void)
{
    bs.starts++;
    if (bs.state == BLE_ST_OFF)
        bs.state = BLE_ST_STARTING;
}
static int host_boot_clean = 1;                      /* bt_page.c's clean boot (the firmware: bootguard, the WDT) */
#define BT_BOOT_CLEAN() host_boot_clean
#ifndef BLE_FML_SEND_OWN
int ble_fml_send(const uint8_t *p, uint32_t len) { (void)p; (void)len; return 0; }
#endif
