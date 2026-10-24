/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Host flash for tests/boot_test.c: the firmware's flash glue (felucca.c st_read / st_erase / st_prog, the
 * HAL entry points persist_boot calls) served from the test's 1 MiB image hflash (declared by the test). */
static uint8_t flash_ok;
#define FL_FAR(fn) (fn)
static uint32_t host_jedec = 0x856014u;                    /* the FM-1's flash id (0: no flash found) */
static uint32_t fl_jedec_ram(void) { return host_jedec; }
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
/* every erase / program the firmware asks for: a log (the first HOST_NFLOG of them since the test cleared host_nflog),
 * a count of the ones outside the flash map the firmware may write (host_fl_stray: never, in any test), and a power
 * loss (host_fail_at: the Nth erase / program from now, 0 the next, fails with -8 and writes nothing; -1 never) */
#define HOST_NFLOG 1024u
static struct { uint8_t erase; uint32_t off, n; } host_flog[HOST_NFLOG];
static uint32_t host_nflog, host_fl_stray;
static int host_fail_at = -1;
/* the map (spec 2026-10-24 parameter locks): projects 0x97000..0x9EFFF, USR3's header sector 0xC8000 (its one-time
 * clamp), the lock banks 0xD4000..0xDBFFF, the user objects 0xDC000..0xDFFFF, the settings 0xFC000..0xFDFFF */
static int host_fl_in_map(uint32_t off, uint32_t n)
{
    static const uint32_t R[5][2] = {{0x97000u, 0x9F000u}, {0xC8000u, 0xC9000u}, {0xD4000u, 0xDC000u},
                                     {0xDC000u, 0xE0000u}, {0xFC000u, 0xFE000u}};
    uint32_t i;
    for (i = 0; i < 5u; i++)
        if (off >= R[i][0] && n <= R[i][1] - off)
            return 1;
    return 0;
}
static int host_fl_op(int erase, uint32_t off, uint32_t n)   /* -8: the injected failure */
{
    if (host_nflog < HOST_NFLOG) {
        host_flog[host_nflog].erase = (uint8_t)erase;
        host_flog[host_nflog].off = off;
        host_flog[host_nflog].n = n;
    }
    host_nflog++;
    if (!host_fl_in_map(off, n))
        host_fl_stray++;
    if (host_fail_at == 0) {
        host_fail_at = -1;
        return -8;
    }
    if (host_fail_at > 0)
        host_fail_at--;
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off > sizeof hflash - 4096u || host_fl_op(1, off, 4096u))
        return -8;
    memset(hflash + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (off > sizeof hflash || n > sizeof hflash - off || host_fl_op(0, off, n))
        return -8;
    for (i = 0; i < n; i++)
        hflash[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"
