/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE (EXPERIMENT, branch usb8) */
/* The frozen usb_app.c's descriptors as the default build compiles them (FELUCCA_CDC=1, FELUCCA_UAC=1), in a unit
 * of their own: tests/usb8_test.c checks that usb_app8.c serves exactly these in SAFE START, and differs from them
 * only where the 10 channels need it otherwise. */
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
