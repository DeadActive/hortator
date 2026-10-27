/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The frozen usb_app.c as the app compiles it (FELUCCA_CDC=1, FELUCCA_UAC=1), in a unit
 * of its own: tests/usb8_test.c checks that usb_app8.c in 2 CH (and SAFE START) answers and streams exactly as it does,
 * and in 10 CH differs only where the 10 channels need it. */
#include <stdint.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC 1
#define FELUCCA_UAC 1
#define HALF_FRAMES 256
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/src/usb_app.c"
int ref_get_desc(uint32_t wvalue, const uint8_t **d, uint16_t *l) { return get_desc(wvalue, d, l); }
/* the 2-channel stream as the frozen driver makes it (tests/usb8_test.c: 2 CH's packets = these) */
void ref_stream(uint32_t alt) { usb.config = 1; uac_stream(alt); }
void ref_flowing(void) { uac.flowing = 1; }
void ref_render_start(void) { uac_render_start(); }
void ref_tap(const int32_t *out, uint32_t n) { uac_tap(out, n); }
uint32_t ref_packet(uint32_t *d) { return uac_packet(d); }
