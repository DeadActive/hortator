/* SPDX-License-Identifier: GPL-3.0-only
 * BLE MIDI test: 2026 DEADACTIVE
 * Drum machine fork: 2026 DEADACTIVE */
/* The core functions the BLE unit (firmware/src/ble/ble.c, its own object) may call: defined in
 * firmware/src/core_ble_api.c inside the core's unit. The BLE unit shares no static data with the core, so the core
 * compiles as it does without BLE (gate 5: tools/check_default_build.sh). And the BLE entry points the core calls:
 * the console's, MIDI out, and the Bluetooth page's (ble_ui_*). */
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
/* the core's UI frame counter (felucca_dbg.ui_frames: once per main-loop pass), for the loop period */
uint32_t core_ui_frames(void);
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
/* BLE MIDI in and the link, from ble_service (the main loop): each received USB-MIDI packet (cable 0) in order, while
 * linked; up = 1 once on entering SUBSCRIBED, 0 once on leaving SUBSCRIBED / RECEIVING */
void core_ble_midi_in(uint32_t pkt);
void core_ble_link(int up);
/* MIDI mode: received packets the core's input ring refused (its last 8 slots are TRS MIDI's), for `ble` */
uint32_t core_ble_in_drops(void);
/* libc.c */
void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);

/* the BLE unit's entry points (ble.c): the core calls ble_service (ed_service) and ble_console (the console's
 * "ble ..." line); ble_user_start is the user's trigger (gate 3: the only way to ble_start) */
void ble_service(void);
void ble_console(const char *args);
void ble_user_start(void);
void ble_list(void);
void ble_status(void);
int ble_started(void);
/* MIDI out to the connected BLE-MIDI device: one message of 1..3 bytes (main loop only, never from an ISR);
 * 1 = queued, 0 = dropped (not connected, or the 64-message queue full: `ble`'s out_drops). A caller that retries
 * sends only what ble_midi_room allows, so out_drops counts real losses only */
int ble_midi_send(const uint8_t *m, uint32_t n);
uint32_t ble_midi_room(void);                          /* the queue's free entries; 0 when not connected */
/* the Bluetooth page (main loop only). None of these starts the stack: the page's BLUETOOTH ON calls ble_user_start
 * (gate 3). States = ble_central.c's. Reconnecting (spec 3): want on + a target -> connect when the target is in the
 * scan list, else an auto scan, 2 s after the last scan end / link drop. */
enum { BLE_ST_OFF, BLE_ST_STARTING, BLE_ST_IDLE, BLE_ST_SCANNING, BLE_ST_CONNECTING, BLE_ST_DISCOVERING,
       BLE_ST_SUBSCRIBED, BLE_ST_RECEIVING, BLE_ST_FAILED };
typedef struct { uint8_t addr[6], type, midi; int8_t rssi; char name[24]; } ble_ui_dev_t;
uint32_t ble_ui_state(void);
const char *ble_ui_err(uint32_t *code);                /* the last error text ("" none) and its code */
uint32_t ble_ui_scan_left(void);                       /* SCANNING: whole seconds left (rounded up), else 0 */
uint32_t ble_ui_devs(void);                            /* the scan list, ble_scan_sorted's order */
int ble_ui_dev(uint32_t i, ble_ui_dev_t *out);         /* 0 = no such entry */
void ble_ui_scan(void);                    /* a 10 s scan: disconnects first (starts on IDLE); pauses reconnecting */
void ble_ui_target(const uint8_t addr[6], uint32_t type);  /* the remembered device (all-zero none): ends the
                                                          * pause; another device than the connected one disconnects */
void ble_ui_want(int on);                  /* reconnecting wanted; 0 also disconnects, cancels a scan / connect */
void ble_ui_pause(int on);                 /* the page's pause of reconnecting (0: the page closed) */
int ble_ui_conn_name(char out[24]);        /* SUBSCRIBED / RECEIVING: 1 and the scanned name ("" none); else 0 */
#endif
