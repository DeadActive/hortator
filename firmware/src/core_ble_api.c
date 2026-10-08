/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* FELUCCA_BLE: the core functions the BLE unit calls (firmware/src/ble/ble_core.h). Thin wrappers over the core's
 * static functions and data; no static data of their own, so the core's other functions compile as without BLE. */
#include "ble/ble_core.h"
#include "fm1_ble_hal.h"                    /* fm1_guard_stack_window, fm1_irq_unmask / _is_masked */
extern uint32_t _pool_start[];
uint32_t core_ms(void) { return fm1_ms; }
void core_wdt_feed(void) { fm1_wdt_feed(); }
void core_reboot(void) { fm1_reboot(); }
void core_audio_stop(void) { fm1_audio_stop(); }
void core_irq_attach(uint32_t n, void (*h)(void), uint32_t prio)   /* IRQs off (the caller) */
{
    fm1_guard_unlock_top();                     /* the vector table is write-protected after boot */
    fm1_irq_attach(n, h, prio);
    fm1_guard_lock_top();
}
void core_ble_stack_window(void)                /* the BLE tasks' stacks are in .pool, below the main stacks */
{
    fm1_guard_stack_window((uint32_t)(uintptr_t)_pool_start, (uint32_t)(uintptr_t)_sstack_top);
}
void core_irq_mask(uint32_t n) { fm1_irq_mask(n); }
void core_irq_unmask(uint32_t n) { fm1_irq_unmask(n); }
uint32_t core_irq_is_masked(uint32_t n) { return fm1_irq_is_masked(n); }
void core_midi_out(uint32_t pkt) { midi_out_event(pkt); }
int core_flash_read(uint32_t off, void *buf, uint32_t n)    /* plain flash only (0x93000..), read only */
{
#if FELUCCA_FLASH
    if (!flash_ok || off < 0x93000u || off > 0x100000u || n > 0x100000u - off)
        return -1;
    return st_read(off, buf, n);
#else
    (void)off;
    (void)buf;
    (void)n;
    return -1;
#endif
}
uint32_t core_audio_late(void) { return felucca_dbg.late; }
void core_con_puts(const char *s)
{
#if FELUCCA_CDC
    con_puts(s);
#else
    (void)s;
#endif
}
