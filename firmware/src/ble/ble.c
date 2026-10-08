/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The BLE module: its own compilation unit (tools/build.py, FELUCCA_BLE=1), so the core's unit compiles exactly as
 * without BLE (gate 5: tools/check_default_build.sh). It reaches the core only through ble_core.h
 * (firmware/src/core_ble_api.c). No screen of its own: the console's `ble` reports (ble_status, ble_central.c). */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_ble_hal.h"
#include "ble/ble_core.h"
#include "ble/ble_sdk_abi.h"
#include "ble/ble_sdk.h"
uint32_t ble_os_now_ms(void) { return core_ms(); }
#define BLE_DIAG_MAGIC 0xB1E0D1A6u
static struct {
    uint32_t magic, stage, task, starts, trim, delays, last_us, t4_stalls, irq_leaks, fatal;
    uint32_t heap_high, alloc_fails, irq_lowered;
} ble_diag __attribute__((section(".noinit"), unused));
static __attribute__((unused)) void ble_stage(uint32_t s)
{
    if (ble_diag.magic != BLE_DIAG_MAGIC) {
        ble_diag.magic = BLE_DIAG_MAGIC;
        ble_diag.starts = 0;
    }
    if (s == 1u) {
        ble_diag.starts++;
        ble_diag.trim = ble_diag.delays = ble_diag.last_us = ble_diag.t4_stalls = ble_diag.irq_leaks = ble_diag.fatal = 0;
        ble_diag.heap_high = ble_diag.alloc_fails = ble_diag.irq_lowered = 0;
    }
    ble_diag.stage = s;
}
#include "ble/ble_os.c"
#include "ble/ble_midi.c"
#include "ble/ble_scan.c"
#include "ble/ble_vm.c"
#include "ble/ble_port.c"
/* the BT libraries' heap, in .pool: the largest round size that keeps the pool's 8 KB headroom with 2 KB task stacks
 * (27296 B at 0.14.0, docs/ble/FIT.md); 1b sizes it from the measured high-water (`ble`) + 25 % (budget) */
#define BLE_HEAP_BYTES (26u * 1024u)
static uint8_t ble_heap[BLE_HEAP_BYTES] __attribute__((section(".pool")));
static uint8_t ble_on;
int ble_started(void) { return ble_on; }
/* how far the last run got, kept over a crash reboot (.noinit) for the console's `ble`: stage 1 OS ready, 2 stack
 * window, 3 MAC set, 4 in btstack_init, 5 btstack_init returned, 6 profile init, 7 BT_STATUS_INIT_OK; trim = 1 in
 * the radio calibration, 2 after it (ble_port.c's RAM hooks); task = the BT task running (+1), 0 = the core;
 * delays / last_us = delay_us calls and the last one's length; t4_stalls = delays during which TIMER4 did not move;
 * irq_leaks = times a BT task handed back with interrupts still off (ble_port.c re-enables them); fatal = 1 a
 * library assert, 2 a library reset request, 3 a task's stack overflow (`task` names it) (ble_port.c ble_fatal);
 * heap_high / alloc_fails = the heap's most in use and its refused requests; irq_lowered = BT IRQ requests above
 * priority 2, attached at 2 (ble_port.c request_irq) */

#include "ble/ble_central.c"
void ble_service(void)                             /* the main loop (ed_service): a 5 ms slice for the BT tasks */
{
    uint32_t pkt;
    ble_loop_note(&loop, core_ui_frames(), core_ms());   /* BT off too: the period to compare with */
    if (!ble_on)
        return;
    ble_os_service(5000);
    ble_diag.heap_high = ble_os_heap_high();       /* (.noinit: kept over a crash reboot, for the heap's size) */
    ble_diag.alloc_fails = ble_os_alloc_fails();
    ble_tick();                                    /* deadlines: scan end, start / connect / discovery timeouts */
    while (ble_midi_take(&pkt))                    /* counted and parsed (`msgs`), not played yet: part 2 */
        ;
}
