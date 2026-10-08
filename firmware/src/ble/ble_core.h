/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The core functions the BLE unit (firmware/src/ble/ble.c, its own object) may call: defined in
 * firmware/src/core_ble_api.c inside the core's unit. The BLE unit shares no static data with the core, so the core
 * compiles as it does without BLE (gate 5: tools/check_default_build.sh). And the BLE entry points the core calls.
 * Hortator: no screen or keys (the console only). */
#ifndef BLE_CORE_H
#define BLE_CORE_H
#include <stdint.h>
/* time, watchdog, power */
uint32_t core_ms(void);
void core_wdt_feed(void);
void core_reboot(void);
void core_audio_stop(void);
/* interrupts (fm1_irq.h) */
void core_irq_attach(uint32_t n, void (*h)(void), uint32_t prio);
void core_irq_mask(uint32_t n);
void core_irq_unmask(uint32_t n);
uint32_t core_irq_is_masked(uint32_t n);
/* guards (fm1_guard.h): widen the stack-limit window to the BLE task stacks (before the first task runs) */
void core_ble_stack_window(void);
/* USB MIDI out (usb.c midi_out_event: one USB-MIDI event packet, cable 0) */
void core_midi_out(uint32_t pkt);
/* flash: read the plain area (0x93000..0xFFFFF) through the core's driver; 0 = ok (never writes) */
int core_flash_read(uint32_t off, void *buf, uint32_t n);
/* audio: render blocks that missed the DMA since power-on (audio.c felucca_dbg.late), for the console's `ble` */
uint32_t core_audio_late(void);
/* the core's crash record (fm1_irq.h fm1_crash_t, .noinit, same layout): read only, for the console's `ble`
 * (the core's unit has the real declaration) */
#ifndef FM1_CRASH_MAGIC
extern const struct {
    uint32_t magic, count, vec, pc, rets, emu, dbg, sp, psr, icfg, uptime_ms;
    uint32_t etm[4];
    uint32_t early;
} fm1_crash;
#endif
/* console (CDC) */
void core_con_puts(const char *s);
/* libc.c */
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);

/* the BLE unit's entry points (ble.c) */
void ble_service(void);
void ble_status(void);
int ble_started(void);
#endif
