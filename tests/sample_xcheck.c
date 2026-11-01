/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The firmware's own sample decoder on a slot the web editor made (tests/sample_xcheck.mjs drives it): the slot image
 * (header + data, as SMP_READ / a .fsmp file hold it) goes into USR1's place, eng_sample.c's smp_user_scan reads it
 * (its header checks), then sample_next decodes zone 1 as a voice plays it. Writes "nz n" and the n samples (int16,
 * little endian) to stdout; exit 2 when the firmware refuses the slot.
 *   sample_xcheck SLOT_FILE > decoded.bin */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "felucca_tables.h"
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
typedef struct {
    uint32_t ph[3];
    int32_t s[8];
} voice_t;                                           /* (core.h's: the fields sample_next uses) */
static uint8_t usr_img[3u * 0x14000u];               /* the user slots' flash (BSS: after SMP_DATA, as the firmware's
                                                      * XIP window: zone offsets are rebased onto SMP_DATA) */
#define SMP_USER_XIP(k) ((const uint8_t *)usr_img + (k) * 0x14000u)
#include "../firmware/src/eng_sample.c"

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    uint32_t i;
    voice_t v;
    const smp_zone_t *z;
    if (argc < 2 || !(f = fopen(argv[1], "rb")))
        return 1;
    memset(usr_img, 0xFF, sizeof usr_img);
    n = (long)fread(usr_img, 1, 0x14000u, f);
    fclose(f);
    if (n < 513 || (uintptr_t)usr_img < (uintptr_t)SMP_DATA || (uintptr_t)usr_img - (uintptr_t)SMP_DATA > 0xF0000000u) {
        fprintf(stderr, "sample_xcheck: bad image or layout\n");
        return 1;
    }
    smp_user_scan(0);
    if (!usr_nz[0]) {
        printf("0 0\n");
        return 2;
    }
    z = &usr_zone[0][0];
    printf("%u %u\n", (unsigned)usr_nz[0], (unsigned)z->n);
    memset(&v, 0, sizeof v);
    for (i = 0; i < z->n; i++) {
        int32_t s = sample_next(z, &v, 0);
        uint8_t b[2] = {(uint8_t)(s & 0xFF), (uint8_t)((s >> 8) & 0xFF)};
        fwrite(b, 1, 2, stdout);
    }
    return 0;
}
