/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Host flash for tests/boot_test.c: the firmware's flash glue (felucca.c st_read / st_erase / st_prog, the
 * HAL entry points persist_boot calls) served from the test's 1 MiB image hflash (declared by the test). */
static uint8_t flash_ok;
#define FL_FAR(fn) (fn)
static uint32_t fl_jedec_ram(void) { return 0x856014u; }   /* the FM-1's flash id */
static void fl_plain_window_init(void) {}
static uint32_t irq_save(void) { return 0; }                /* the HAL brackets flash reads with these */
static void irq_restore(uint32_t f) { (void)f; }
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off > sizeof hflash || n > sizeof hflash - off)
        return -1;
    memcpy(dst, hflash + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off > sizeof hflash - 4096u)
        return -8;
    memset(hflash + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off > sizeof hflash || n > sizeof hflash - off)
        return -8;
    for (i = 0; i < n; i++)
        hflash[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"
