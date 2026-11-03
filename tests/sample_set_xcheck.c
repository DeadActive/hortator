/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* A sample set the web editor made (several slots), read and played by the firmware itself (tests/sample_xcheck.mjs
 * drives it): the slot image (header + data, as a .fsmp file holds it) goes into USR1's place, eng_sample.c's
 * smp_user_scan reads it (its header checks) and dm_sample.c's SLOT list is made (smp_slots_sync: the set's samples
 * in key order, as the SAMPLE model's and the LAYER's SLOT knob step through them); then each SLOT 1..N's zone
 * (smp_slot_zone) is decoded by sample_next from its start, as a voice plays it. Writes int32 little endian: N, then per
 * SLOT its zone index in the set, its samples n, its pitch from the root (1/16 semitones) and the n samples; exit 2 when
 * the firmware refuses the slot.
 *   sample_set_xcheck SLOT_FILE > decoded.bin */
#include <stdint.h>
static uint8_t usr_img[3u * 0x14000u];               /* the user slots' flash */
#define SMP_USER_XIP(k) ((const uint8_t *)usr_img + (k) * 0x14000u)
#include "drum_host.h"

static void put(int32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, stdout);
}

int main(int argc, char **argv)
{
    FILE *f;
    long n;
    uint32_t i, j, set = SMP_NSETS, cnt;
    if (argc < 2 || !(f = fopen(argv[1], "rb")))
        return 1;
    memset(usr_img, 0xFF, sizeof usr_img);
    n = (long)fread(usr_img, 1, 0x14000u, f);
    fclose(f);
    if (n < 513)
        return 1;
    smp_user_scan(0);
    smp_slots_sync();
    if (!usr_nz[0]) {
        put(0);
        return 2;
    }
    cnt = smp_slot_count(set);
    put((int32_t)cnt);
    for (i = 0; i < cnt; i++) {
        int32_t o16;
        uint32_t zi = smp_slot_zone(set, SMP_SLOT_V(i), &o16);
        const smp_zone_t *z;
        voice_t v;
        if (zi == 0xFFFFu || zi < 0x8000u) {
            put(-1);
            put(0);
            put(0);
            continue;
        }
        z = smp_zone(zi);
        put((int32_t)(zi & 31u));
        put((int32_t)z->n);
        put(o16);
        memset(&v, 0, sizeof v);
        for (j = 0; j < z->n; j++)
            put(sample_next(z, &v, 0));
    }
    return 0;
}
