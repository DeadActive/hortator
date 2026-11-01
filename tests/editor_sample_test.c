/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web editor's user sample slots (firmware/src/editor_sample.c; web/EDITOR_PROTOCOL.md 2.7 and the Appendix):
 * INFO, an upload (BEGIN, DATA, END) into each slot up to its limit, READ, CLEAR, RENAME; every refusal; the slot
 * limits as law (no erase / program outside the named slot, hostile lengths, offsets and headers, a fuzz); the header
 * written last, magic last; a power cut at every flash operation of an upload / a rename; the update loader's sweep
 * (no sector's +0xF00 window ever reads as an update record). The firmware's real eng_sample.c, project.c, storage.c
 * and editor_backup.c on a host flash (tests/flash_host.h), ASan + UBSan; editor.c's framing API is a test-only
 * stand-in (as tests/editor_backup_test.c) recording the one reply each request gets. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#define ED_RESTORE_LOCK 1
#include "ui_host.h"
static uint32_t host_ms;
static uint8_t host_ota_req;
#define EDB_MS() host_ms
#define EDB_OTA_REQ() (host_ota_req != 0u)
#define EDS_MS() host_ms
#define EDS_OTA_REQ() (host_ota_req != 0u)
#include "../firmware/src/editor_proto.h"

/* ---- editor.c's framing API, test-only: one reply buffer */
static uint8_t erp[ED_REPLY_MAX + 16];
static uint32_t rp_n, rp_sends, rp_i, q_seq;
static void ed_put(uint32_t b)
{
    if (rp_n < sizeof erp)
        erp[rp_n++] = (uint8_t)b;
}
static void ed_reply(uint32_t cmd, uint32_t rc)
{
    rp_n = 0;
    ed_put(0xF0);
    ed_put(ED_H0);
    ed_put(ED_H1);
    ed_put(ED_H2);
    ed_put(cmd);
    ed_put(q_seq);
    ed_put(rc);
}
static void ed_push(uint32_t cmd) { ed_reply(cmd, 0); rp_n--; }
static void ed_b(uint32_t v) { ed_put(v & 127u); }
static void ed_u14(uint32_t v) { ed_put(v & 127u); ed_put((v >> 7) & 127u); }
static void ed_v14(int32_t v) { ed_u14((uint32_t)(v + 8192)); }
static void ed_u35(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        ed_put((v >> (7u * i)) & 127u);
}
static void ed_str(const char *s)
{
    for (; *s; s++)
        if ((uint8_t)*s >= 32u && (uint8_t)*s <= 126u)
            ed_put((uint8_t)*s);
    ed_put(0);
}
static void pack7(void (*put)(uint32_t), const uint8_t *d, uint32_t n)
{
    uint32_t i, j, k;
    for (i = 0; i < n; i += 7u) {
        uint32_t hb = 0;
        k = n - i < 7u ? n - i : 7u;
        for (j = 0; j < k; j++)
            hb |= (uint32_t)(d[i + j] >> 7) << j;
        put(hb);
        for (j = 0; j < k; j++)
            put(d[i + j] & 127u);
    }
}
static void ed_pack(const void *d, uint32_t n) { pack7(ed_put, d, n); }
static int ed_restoring(void);
static void ed_state(void)
{
    uint32_t i;
    for (i = 0; i < ED_STATE_N; i++)
        ed_put(0);
}
static uint32_t ed_room(void) { return ED_REPLY_MAX - 1u - rp_n; }
static void ed_send(void)
{
    ed_put(0xF7);
    rp_sends++;
}
static void ed_fail(uint32_t cmd, uint32_t rc, uint32_t detail, const char *msg)
{
    ed_reply(cmd, rc);
    ed_b(detail);
    ed_str(msg);
    ed_send();
}
static uint32_t ed_get_u14(const uint8_t *a) { return a[0] | (uint32_t)a[1] << 7; }
static int32_t ed_get_v14(const uint8_t *a) { return (int32_t)ed_get_u14(a) - 8192; }
static uint32_t ed_get_u35(const uint8_t *a)
{
    return a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
}
static uint32_t ed_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)   /* editor.c's */
{
    uint32_t o = 0;
    while (n && o < max) {
        uint32_t m = *in++, j, k;
        n--;
        k = n > 7u ? 7u : n;
        if (!k || o + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, n--)
            out[o++] = (uint8_t)(*in++ | ((m >> j) & 1u) << 7);
    }
    return n ? 0u : o;
}
#include "../firmware/src/editor_backup.c"
#include "../firmware/src/editor_sample.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

/* ---- the law: the slots' flash ranges */
static const uint32_t SLOT_LO[3] = {0xA0000u, 0xB4000u, 0xC8000u}, SLOT_HI[3] = {0xB4000u, 0xC0000u, 0xD4000u};
static const uint32_t SLOT_MAX[3] = {81408u, 48640u, 48640u};
/* every flash operation logged since host_nflog was cleared lies inside slot k (k 3: inside any slot) */
static int flog_in(uint32_t k)
{
    uint32_t i, j, ok = host_nflog <= HOST_NFLOG;
    for (i = 0; i < host_nflog && i < HOST_NFLOG; i++) {
        uint32_t in = 0, lo = host_flog[i].off, n = host_flog[i].n;
        for (j = 0; j < 3u; j++)
            if ((k == 3u || k == j) && lo >= SLOT_LO[j] && lo < SLOT_HI[j] && n <= SLOT_HI[j] - lo)
                in = 1;
        ok &= in;
    }
    return (int)ok && host_fl_bt == 0;
}

/* ---- the update loader's test (firmware/loader/ldr_core.c, its flash-record sweep), as it is */
static uint32_t crc16x(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0, i;
    while (n--) {
        c ^= (uint32_t)*p++ << 8;
        for (i = 0; i < 8u; i++)
            c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1;
        c &= 0xFFFFu;
    }
    return c;
}
static int loader_takes(const uint8_t *r)
{
    uint32_t c = r[0] | (uint32_t)r[1] << 8;
    return (r[6] | (uint32_t)r[7] << 8) == 0x5441u && c && c == crc16x(r + 2, 78);
}
static int crc_window(const uint8_t *r)               /* the device's (wider) test: the CRC alone */
{
    uint32_t c = r[0] | (uint32_t)r[1] << 8;
    return c && c == crc16x(r + 2, 78);
}
static int sweep_clean(void)                          /* no sector of 0x93000..0xFBFFF reads as a record */
{
    uint32_t s;
    for (s = 0x94000u; s <= 0xFC000u; s += 0x1000u)
        if (loader_takes(hflash + s - 0x100u))
            return 0;
    return 1;
}

/* ---- requests */
static uint8_t ra[ED_REQ_MAX + 64];
static uint32_t ra_n, rc_detail;
static void a_put(uint32_t b)
{
    if (ra_n < sizeof ra)
        ra[ra_n++] = (uint8_t)b;
}
static void a_u35(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        a_put((v >> (7u * i)) & 127u);
}
static uint32_t req(uint32_t cmd)
{
    int h;
    q_seq = (q_seq + 1u) % 127u;
    rp_n = rp_sends = 0;
    h = ed_sample_cmd(cmd, ra, ra_n);
    ra_n = 0;
    if (!h || rp_sends != 1u || rp_n < 8u || rp_n > ED_REPLY_MAX || erp[4] != cmd || erp[5] != q_seq ||
        erp[rp_n - 1u] != 0xF7)
        return 0x100;
    rp_i = 7;
    rc_detail = erp[6] ? erp[7] : 0u;
    return erp[6];
}
static uint32_t r_b(void) { return erp[rp_i++]; }
static uint32_t r_u14(void) { rp_i += 2; return ed_get_u14(erp + rp_i - 2); }
static uint32_t r_u35(void) { rp_i += 5; return ed_get_u35(erp + rp_i - 5); }
static void r_str(char *s, uint32_t max)
{
    uint32_t n = 0;
    while (rp_i < rp_n && erp[rp_i]) {
        if (n + 1u < max)
            s[n++] = (char)erp[rp_i];
        rp_i++;
    }
    s[n] = 0;
    rp_i++;
}

typedef struct { uint32_t valid, len, crc, max, nz, n, rate; char name[16]; } info_t;
static uint32_t info(uint32_t k, info_t *o)
{
    uint32_t rc;
    a_put(k);
    rc = req(ED_SMP_INFO);
    if (rc)
        return rc;
    if (r_b() != k)
        return 0x101;
    o->valid = r_b();
    r_str(o->name, sizeof o->name);
    o->len = r_u35();
    o->crc = r_u35();
    o->max = r_u35();
    o->nz = r_b();
    o->n = r_u35();
    o->rate = r_u35();
    return rp_i + 1u == rp_n ? 0u : 0x102;
}

/* a sample: len data bytes (deterministic noise, every loader window made harmless as the page does) + its header */
static uint8_t D[0x14000], H[ED_SMP_HDR];
static uint32_t t_rng = 1;
static uint32_t rnd(void) { t_rng = t_rng * 1664525u + 1013904223u; return t_rng >> 8; }
static void window_of(const uint8_t *d, uint32_t len, uint32_t at, uint8_t *w)   /* data offset `at`: 80 bytes, 0xFF past */
{
    uint32_t i;
    for (i = 0; i < 80u; i++)
        w[i] = at + i < len ? d[at + i] : 0xFFu;
}
static void make_sample(uint32_t len, uint32_t seed, const char *name)
{
    smp_user_hdr_t h;
    uint32_t i, at;
    uint8_t w[80];
    t_rng = seed;
    for (i = 0; i < len; i++)
        D[i] = (uint8_t)(rnd() >> 16);
    for (at = 0xF00u - 512u; at < len; at += 0x1000u)   /* (512 + at) % 4096 == 0xF00 */
        for (window_of(D, len, at, w); crc_window(w); window_of(D, len, at, w))
            D[at + 2u] ^= 0x5Au;
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = 1;
    memcpy(h.name, name, strlen(name) < 8u ? strlen(name) : 8u);
    h.data_len = len;
    h.crc = st_crc32(D, len);
    h.zone[0].off = 0;
    h.zone[0].n = 2u * len;
    h.zone[0].ls = 0;
    h.zone[0].le = 2u * len - 1u;
    h.zone[0].rate = 65536u;
    h.zone[0].root16 = 960;
    h.zone[0].lo = 0;
    h.zone[0].hi = 127;
    memcpy(H, &h, sizeof H);
}
static uint32_t begin(uint32_t k, uint32_t len, uint32_t crc)
{
    a_put(k);
    a_u35(len);
    a_u35(crc);
    return req(ED_SMP_BEGIN);
}
static uint32_t piece(uint32_t k, uint32_t off, const uint8_t *d, uint32_t len)
{
    a_put(k);
    a_u35(off);
    pack7(a_put, d, len);
    return req(ED_SMP_DATA);
}
static uint32_t end(uint32_t k, const uint8_t *hdr)
{
    a_put(k);
    pack7(a_put, hdr, ED_SMP_HDR);
    return req(ED_SMP_END);
}
/* a whole upload of D / H (len bytes): the first rc that is not 0 (0x200 + rc at END) */
static uint32_t upload(uint32_t k, uint32_t len)
{
    uint32_t off, rc, n;
    if ((rc = begin(k, len, st_crc32(D, len))) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        n = len - off < 256u ? len - off : 256u;
        if ((rc = piece(k, off, D + off, n)) != 0)
            return rc;
    }
    rc = end(k, H);
    return rc ? 0x200u + rc : 0u;
}
static int slot_holds(uint32_t k, uint32_t len)       /* flash: H at the start, D at +512 */
{
    return !memcmp(hflash + SLOT_LO[k], H, ED_SMP_HDR) && !memcmp(hflash + SLOT_LO[k] + 512u, D, len);
}
static void power_cycle(void)                         /* RAM gone; the boot reads the slots again */
{
    uint32_t k;
    memset(&eds, 0, sizeof eds);
    for (k = 0; k < 3u; k++)
        smp_user_scan(k);
}
static void fresh(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    flash_ok = 1;
    song.playing = 0;
    transport_req = 0;
    var_blocked = lk_blocked = 0;
    host_ota_req = 0;
    memset(&edb, 0, sizeof edb);
    power_cycle();
    host_nflog = host_fl_stray = host_fl_bt = 0;
    host_fail_at = -1;
}

/* ---------------------------------------------------------------- tests --- */
static void t_info(void)
{
    info_t o;
    uint32_t k, ok = 1;
    fresh();
    for (k = 0; k < 3u; k++)
        ok &= info(k, &o) == 0 && !o.valid && !o.len && !o.crc && o.max == SLOT_MAX[k] && !o.nz && !o.name[0];
    check("SMP_INFO: three empty slots, max 81,408 / 48,640 / 48,640 data bytes", (int)ok);
    a_put(3);
    check("SMP_INFO slot 3: rc 1 detail 45", req(ED_SMP_INFO) == ED_E_BAD && rc_detail == ED_D_RANGE);
    a_put(0);
    a_put(0);
    check("SMP_INFO with 2 bytes: rc 1 detail 44", req(ED_SMP_INFO) == ED_E_BAD && rc_detail == ED_D_LENGTH);
    check("a command not ours is left (0)", ed_sample_cmd(ED_INFO, ra, 0) == 0);
    flash_ok = 0;
    a_put(0);
    check("no flash this boot: rc 2 detail 28", req(ED_SMP_INFO) == ED_E_REFUSED && rc_detail == ED_D_NO_FLASH);
}

static void t_law(void)                               /* eds_erase / eds_prog: nothing outside the named slot */
{
    uint32_t k, ok = 1;
    uint8_t b[4] = {0, 0, 0, 0};
    fresh();
    for (k = 0; k < 3u; k++) {
        ok &= eds_in_slot(k, SLOT_LO[k], SLOT_HI[k] - SLOT_LO[k]) && eds_in_slot(k, SLOT_HI[k] - 1u, 1);
        ok &= !eds_in_slot(k, SLOT_HI[k], 1) && !eds_in_slot(k, SLOT_HI[k] - 0x1000u, 0x1001u);
        ok &= !eds_in_slot(k, SLOT_LO[k] - 1u, 2) && !eds_in_slot(k, SLOT_LO[k], 0xFFFFFFFFu) && !eds_in_slot(k, SLOT_LO[k], 0);
        ok &= eds_erase(k, SLOT_HI[k]) == -9 && eds_erase(k, SLOT_LO[k] - 0x1000u) == -9 && eds_erase(k, SLOT_LO[k] + 1u) == -9;
        ok &= eds_prog(k, SLOT_HI[k] - 2u, b, 4) == -9 && eds_prog(k, SLOT_LO[(k + 1u) % 3u], b, 4) == -9;
    }
    ok &= !eds_in_slot(3, 0xD4000u, 1) && eds_erase(3, 0xD4000u) == -9 && eds_prog(3, 0xD4000u, b, 4) == -9;
    ok &= host_nflog == 0;
    check("the law: erase / program only inside the named slot (its last byte yes; the next slot, the variations at "
          "0xC0000, the lock banks at 0xD4000, a wrapping length: no flash operation)",
          (int)ok);
}

static void t_upload(void)
{
    info_t o;
    uint32_t k, ok = 1, off;
    for (k = 0; k < 3u; k++) {
        fresh();
        make_sample(SLOT_MAX[k], 7u + k, "KICK");
        ok &= upload(k, SLOT_MAX[k]) == 0;
        ok &= erp[7] == k && erp[8] == 1u;                   /* END's reply: slot, valid 1 */
        ok &= slot_holds(k, SLOT_MAX[k]) && usr_nz[k] == 1u && usr_zone[k][0].n == 2u * SLOT_MAX[k];
        ok &= info(k, &o) == 0 && o.valid && o.len == SLOT_MAX[k] && o.crc == st_crc32(D, SLOT_MAX[k]) &&
              !strcmp(o.name, "KICK") && o.nz == 1u && o.n == 2u * SLOT_MAX[k] && o.rate == 65536u;
        ok &= flog_in(k) && sweep_clean();
        /* the end of the slot reached exactly, the neighbours untouched */
        for (off = 0; off < 0x1000u; off++)
            ok &= hflash[SLOT_HI[k] + off] == 0xFFu && hflash[SLOT_LO[k] - 0x1000u + off] == 0xFFu;
        power_cycle();
        ok &= usr_nz[k] == 1u;
    }
    check("an upload at each slot's limit: the header, the data, INFO, the voices' zones; only that slot written; "
          "a power cycle keeps it",
          (int)ok);
}

static void t_limits(void)
{
    uint32_t k, ok = 1, rc;
    uint8_t keep[0x40000];
    for (k = 0; k < 3u; k++) {
        fresh();
        make_sample(SLOT_MAX[k] + 1u, 3, "BIG");
        memcpy(keep, hflash + 0xA0000u, sizeof keep);
        ok &= begin(k, SLOT_MAX[k] + 1u, 0) == ED_E_REFUSED && rc_detail == ED_D_TOO_BIG && host_nflog == 0;
        ok &= begin(k, 0xFFFFFFFFu, 0) == ED_E_REFUSED && rc_detail == ED_D_TOO_BIG && host_nflog == 0;
        ok &= begin(k, 0, 0) == ED_E_BAD && rc_detail == ED_D_RANGE && host_nflog == 0;
        ok &= !memcmp(keep, hflash + 0xA0000u, sizeof keep);
    }
    check("BEGIN longer than the slot (by 1, by 4 GiB) or empty: refused, nothing erased", (int)ok);
    ok = 1;
    fresh();
    ok &= begin(3, 100, 0) == ED_E_BAD && rc_detail == ED_D_RANGE;
    ok &= begin(127, 100, 0) == ED_E_BAD && rc_detail == ED_D_RANGE;
    a_put(0);
    ok &= req(ED_SMP_BEGIN) == ED_E_BAD && rc_detail == ED_D_LENGTH && host_nflog == 0;
    check("BEGIN: slot 3 / 127, a short request: rc 1, nothing erased", (int)ok);

    ok = 1;
    fresh();
    make_sample(1000, 5, "S");
    ok &= begin(1, 1000, st_crc32(D, 1000)) == 0;
    ok &= piece(1, 1024, D, 256) == ED_E_BAD && rc_detail == ED_D_RANGE;          /* past the length */
    ok &= piece(1, 0xFFFFFF00u, D, 256) == ED_E_BAD && rc_detail == ED_D_RANGE;   /* far past it */
    ok &= piece(1, 100, D, 256) == ED_E_BAD && rc_detail == ED_D_RANGE;           /* not a piece's offset */
    ok &= piece(1, 256, D, 256) == ED_E_STALE && rc_detail == ED_D_ORDER;          /* not the next one */
    ok &= piece(1, 0, D, 255) == ED_E_BAD && rc_detail == ED_D_LENGTH;             /* short */
    ok &= piece(1, 768, D, 256) == ED_E_STALE;                                    /* (the last one, too early) */
    ok &= piece(0, 0, D, 256) == ED_E_BAD && rc_detail == ED_D_NOT_RESTORING;     /* another slot: none running */
    ok &= flog_in(1);
    check("DATA: past the length, a far offset, a misaligned or skipped offset, a wrong length, another slot: "
          "refused; only USR2 written",
          (int)ok);
    ok = 1;
    for (k = 0; k < 3u; k++) {
        smp_user_hdr_t h;
        uint32_t z;
        static const char *what[] = {"magic", "version", "nz 0", "nz 17", "length", "crc", "off", "n", "idx",
                                     "rate 0", "rate > 4x", "loop", "le", "lo > hi"};
        for (z = 0; z < 14u; z++) {
            fresh();
            make_sample(2000, 9, "Z");
            memcpy(&h, H, sizeof h);
            switch (z) {
            case 0: h.magic ^= 1u; break;
            case 1: h.version = 2; break;
            case 2: h.nz = 0; break;
            case 3: h.nz = 17; break;
            case 4: h.data_len = 1999; break;
            case 5: h.crc ^= 1u; break;
            case 6: h.zone[0].off = 2001; break;
            case 7: h.zone[0].n = 4001; h.zone[0].le = 4000; break;
            case 8: h.zone[0].idx = 89; break;
            case 9: h.zone[0].rate = 0; break;
            case 10: h.zone[0].rate = (4u << 16) + 1u; break;
            case 11: h.zone[0].ls = 3000; h.zone[0].le = 2000; break;
            case 12: h.zone[0].le = 4000; break;
            default: h.zone[0].lo = 100; h.zone[0].hi = 99; break;
            }
            memcpy(H, &h, sizeof H);
            rc = upload(k, 2000);
            ok &= rc == 0x200u + ED_E_REFUSED && rc_detail == ED_D_INVALID;
            ok &= hflash[SLOT_LO[k]] == 0xFFu && usr_nz[k] == 0u && flog_in(k);
            power_cycle();
            ok &= usr_nz[k] == 0u;
            if (!ok) {
                printf("  header %s, slot %u: rc %x\n", what[z], k, rc);
                break;
            }
        }
    }
    check("END: a header the device would not play (magic, version, zones, length, CRC field, a zone outside the "
          "data or malformed): refused, nothing written at the header, the slot empty",
          (int)ok);
}

static void t_fuzz(void)
{
    uint32_t i, j, ok = 1, cmds = 0, inside = 1, wrote = 0;
    fresh();
    t_rng = 12345;
    for (i = 0; i < 60000u; i++) {
        uint32_t cmd = ED_SMP_INFO + rnd() % 7u, n = rnd() % 600u, r = rnd() % 8u;
        if (r == 0) {                                  /* a valid BEGIN now and then, so DATA / END run */
            a_put(rnd() % 3u);
            a_u35(1u + rnd() % 90000u);
            a_u35(rnd());
            cmd = ED_SMP_BEGIN;
        } else if (r == 1 && eds.on) {                 /* a DATA at the next offset with random bytes */
            uint32_t len = eds.len - eds.got < 256u ? eds.len - eds.got : 256u;
            uint8_t d[256];
            for (j = 0; j < 256u; j++)
                d[j] = (uint8_t)rnd();
            a_put(eds.slot);
            a_u35(eds.got);
            pack7(a_put, d, len);
            cmd = ED_SMP_DATA;
        } else {
            for (j = 0; j < n; j++)
                a_put(r == 2 ? rnd() : rnd() & 127u);
            if (r == 3 && ra_n)
                ra[0] = (uint8_t)(rnd() % 4u);
        }
        song.playing = rnd() % 16u == 0;
        host_ms += rnd() % 500u;
        host_nflog = 0;
        j = req(cmd);
        ok &= j != 0x100u;
        wrote += !j && (cmd == ED_SMP_DATA || cmd == ED_SMP_BEGIN);
        inside &= flog_in(3);
        cmds++;
    }
    song.playing = 0;
    check("fuzz: 60,000 random sample requests (valid BEGINs and pieces among them): one well-formed reply each", (int)ok);
    printf("      (fuzz: %u BEGINs / pieces written)\n", wrote);
    check("fuzz: every erase / program inside a sample slot, nothing else touched", (int)inside && wrote > 1000u);
    check("fuzz: no sector reads as an update record", sweep_clean());
    (void)cmds;
}

static void t_header_last(void)
{
    uint32_t ok = 1, off, n, last, i;
    info_t o;
    fresh();
    make_sample(5000, 21, "OLD");
    ok &= upload(0, 5000) == 0 && usr_nz[0] == 1u;
    make_sample(9000, 22, "NEW");
    host_nflog = 0;
    ok &= begin(0, 9000, st_crc32(D, 9000)) == 0;
    ok &= host_nflog == 1u && host_flog[0].erase && host_flog[0].off == 0xA0000u;   /* the header sector, first */
    ok &= usr_nz[0] == 0u && info(0, &o) == 0 && !o.valid;
    for (off = 0; off < 9000u; off += 256u) {
        n = 9000u - off < 256u ? 9000u - off : 256u;
        ok &= piece(0, off, D + off, n) == 0;
        ok &= info(0, &o) == 0 && !o.valid && hflash[0xA0000u] == 0xFFu;
    }
    power_cycle();
    ok &= usr_nz[0] == 0u;                              /* (a power cycle before END: empty) */
    fresh();
    make_sample(9000, 22, "NEW");
    ok &= upload(0, 9000) == 0;
    for (i = 0, last = 0; i < host_nflog; i++)
        if (!host_flog[i].erase && host_flog[i].off < 0xA0000u + 512u)
            last = i;
    ok &= !host_flog[last].erase && host_flog[last].off == 0xA0000u && host_flog[last].n == 4u && last + 1u == host_nflog;
    check("upload: BEGIN erases the header sector first (the slot reads empty), the data never makes it valid, END "
          "writes the header with its magic last",
          (int)ok);
}

static void t_power_cut(void)
{
    uint32_t ok = 1, ops, at, k = 2;
    fresh();
    make_sample(30000, 31, "A");
    ok &= upload(k, 30000) == 0;
    ops = host_nflog;
    for (at = 0; at < ops; at++) {
        fresh();
        make_sample(12000, 41, "OLD");                  /* an older sample in the slot */
        ok &= upload(k, 12000) == 0;
        make_sample(30000, 31, "A");
        host_nflog = 0;
        host_fail_at = (int)at;
        (void)upload(k, 30000);
        host_fail_at = -1;
        power_cycle();
        if (at == 0)                                     /* (cut before BEGIN's erase: nothing changed) */
            ok &= usr_nz[k] == 1u && !memcmp(hflash + SLOT_LO[k] + 8u, "OLD", 3);
        else
            ok &= usr_nz[k] == 0u;
        ok &= flog_in(k) && sweep_clean();
        if (!ok) {
            printf("  power cut at op %u of %u\n", at, ops);
            break;
        }
    }
    check("a power cut at every erase / program of an upload (over an older sample): the slot empty after (the old "
          "one only when the cut came before the first erase), never a mix",
          (int)ok);
    ok = 1;
    for (at = 0; at < 64u; at++) {
        uint32_t ops2;
        fresh();
        make_sample(6000, 51, "NAME");
        ok &= upload(1, 6000) == 0;
        host_nflog = 0;
        host_fail_at = (int)at;
        a_put(1);
        {
            const char *nm = "SNARE";
            uint32_t j;
            for (j = 0; nm[j]; j++)
                a_put((uint8_t)nm[j]);
            a_put(0);
        }
        (void)req(ED_SMP_RENAME);
        ops2 = host_nflog;
        host_fail_at = -1;
        power_cycle();
        if (usr_nz[1]) {                                 /* whole: the old name or the new one, the data intact */
            const smp_user_hdr_t *h = (const smp_user_hdr_t *)(hflash + SLOT_LO[1]);
            ok &= (!memcmp(h->name, "NAME\0\0\0\0", 8) || !memcmp(h->name, "SNARE\0\0\0", 8)) &&
                  st_crc32(hflash + SLOT_LO[1] + 512u, 6000) == h->crc && !memcmp(hflash + SLOT_LO[1] + 512u, D, 6000);
        }
        ok &= flog_in(1);
        if (!ok) {
            printf("  rename: power cut at op %u\n", at);
            break;
        }
        if (at >= ops2)
            break;
    }
    check("a power cut at every erase / program of a rename: the slot empty or whole (old or new name), never garbage",
          (int)ok);
}

static void t_refusals(void)
{
    uint32_t ok = 1;
    info_t o;
    fresh();
    make_sample(3000, 61, "R");
    song.playing = 1;
    ok &= begin(0, 3000, st_crc32(D, 3000)) == ED_E_STOP && rc_detail == ED_D_STOP && host_nflog == 0;
    ok &= !strcmp((const char *)erp + 8, "STOP TO UPLOAD");
    song.playing = 0;
    ok &= begin(0, 3000, st_crc32(D, 3000)) == 0;
    ok &= piece(0, 0, D, 256) == 0;
    song.playing = 1;
    host_nflog = 0;
    ok &= piece(0, 256, D + 256, 256) == ED_E_STOP && host_nflog == 0 && eds.on;
    ok &= end(0, H) == ED_E_STOP && host_nflog == 0 && eds.on;
    song.playing = 0;
    ok &= piece(0, 256, D + 256, 256) == 0;
    ok &= end(0, H) == ED_E_REFUSED && rc_detail == ED_D_INCOMPLETE && eds.on;   /* not all there: it stays */
    ok &= piece(0, 256, D + 256, 256) == 0;                                     /* a retry of the last piece */
    ok &= piece(0, 0, D, 256) == ED_E_STALE && rc_detail == ED_D_ORDER;
    check("playing: BEGIN rc 3 'STOP TO UPLOAD'; DATA / END rc 3 and the upload stays; END early rc 2 detail 27; a "
          "retry of the last piece answered; an older one rc 5 detail 34",
          (int)ok);

    ok = 1;
    fresh();
    make_sample(3000, 62, "R");
    edb.on = 1;
    ok &= begin(0, 3000, st_crc32(D, 3000)) == ED_E_BUSY && rc_detail == ED_D_RESTORING && host_nflog == 0;
    edb.on = 0;
    host_ota_req = 1;
    ok &= begin(0, 3000, st_crc32(D, 3000)) == ED_E_BUSY && rc_detail == 0u && host_nflog == 0;
    host_ota_req = 0;
    var_blocked = 1;
    ok &= begin(1, 3000, st_crc32(D, 3000)) == ED_E_REFUSED && rc_detail == ED_D_BLOCKED && host_nflog == 0;
    ok &= begin(2, 3000, st_crc32(D, 3000)) == 0;
    var_blocked = 0;
    lk_blocked = 1;
    ok &= begin(2, 3000, st_crc32(D, 3000)) == ED_E_REFUSED && rc_detail == ED_D_BLOCKED;
    lk_blocked = 0;
    check("restoring rc 6 detail 40; the upgrade command pending rc 6 'UPDATE'; USR2 / USR3 while its clamp failed rc 2 "
          "detail 29: nothing erased",
          (int)ok);

    ok = 1;
    fresh();
    make_sample(3000, 63, "R");
    ok &= upload(0, 3000) == 0;                                                  /* a sample there */
    make_sample(3000, 64, "R2");
    ok &= begin(0, 3000, st_crc32(D, 3000) ^ 1u) == 0;                          /* a wrong CRC announced */
    ok &= piece(0, 0, D, 256) == 0 && piece(0, 256, D + 256, 256) == 0;
    {
        uint32_t off;
        for (off = 512; off < 3000u; off += 256u)
            ok &= piece(0, off, D + off, 3000u - off < 256u ? 3000u - off : 256u) == 0;
    }
    {
        smp_user_hdr_t h;                                                        /* the header says what BEGIN said */
        memcpy(&h, H, sizeof h);
        h.crc ^= 1u;
        memcpy(H, &h, sizeof H);
    }
    ok &= end(0, H) == ED_E_REFUSED && rc_detail == ED_D_CRC && !eds.on && usr_nz[0] == 0u && hflash[0xA0000u] == 0xFFu;
    ok &= info(0, &o) == 0 && !o.valid;
    check("END: the data's CRC-32 is not the announced one: rc 2 detail 25, dropped, the slot empty", (int)ok);

    ok = 1;
    fresh();
    make_sample(3000, 65, "R");
    ok &= begin(0, 3000, st_crc32(D, 3000)) == 0 && piece(0, 0, D, 256) == 0;
    host_ota_req = 1;
    {
        uint32_t off;
        host_ota_req = 0;
        for (off = 256; off < 3000u; off += 256u)
            ok &= piece(0, off, D + off, 3000u - off < 256u ? 3000u - off : 256u) == 0;
        host_ota_req = 1;
    }
    ok &= end(0, H) == ED_E_BUSY && !eds.on && hflash[0xA0000u] == 0xFFu;
    host_ota_req = 0;
    check("END with the upgrade command pending: rc 6, dropped, nothing written", (int)ok);

    ok = 1;
    fresh();
    make_sample(3000, 66, "R");
    ok &= begin(0, 3000, st_crc32(D, 3000)) == 0 && piece(0, 0, D, 256) == 0;
    host_ms += ED_RESTORE_IDLE_MS + 1u;
    ed_sample_poll();
    ok &= !eds.on && piece(0, 256, D + 256, 256) == ED_E_BAD && rc_detail == ED_D_NOT_RESTORING;
    ok &= begin(1, 3000, st_crc32(D, 3000)) == 0 && piece(1, 0, D, 256) == 0;
    ed_sample_drop();
    ok &= !eds.on && end(1, H) == ED_E_BAD && rc_detail == ED_D_NOT_RESTORING && usr_nz[1] == 0u;
    host_ms += 1000u;
    ok &= begin(2, 3000, st_crc32(D, 3000)) == 0;
    host_ms += ED_RESTORE_IDLE_MS - 1u;
    a_put(2);
    ok &= req(ED_SMP_INFO) == 0;                         /* any request keeps it alive */
    host_ms += ED_RESTORE_IDLE_MS - 1u;
    ed_sample_poll();
    ok &= eds.on;
    check("an idle upload (3 s) and update traffic drop it (the slot stays empty); any request keeps it alive", (int)ok);
}

static void t_loader_window(void)
{
    uint32_t ok = 1, at = 0xF00u - 512u, len = 8000, i, rc;
    uint8_t rec[80];
    fresh();
    make_sample(len, 71, "EVIL");
    /* an update record (ota.c's UPDATA_PARM shape) at the first sector's +0xF00 */
    memset(rec, 0, sizeof rec);
    rec[2] = 0x0D, rec[3] = 0x5A, rec[4] = 0x01, rec[5] = 0x5A, rec[6] = 0x41, rec[7] = 0x54;
    rec[72] = 0x00, rec[73] = 0x00, rec[74] = 0x0E;
    i = crc16x(rec + 2, 78);
    rec[0] = (uint8_t)i, rec[1] = (uint8_t)(i >> 8);
    ok &= loader_takes(rec);
    memcpy(D + at, rec, 80);
    {
        smp_user_hdr_t h;
        memcpy(&h, H, sizeof h);
        h.crc = st_crc32(D, len);
        memcpy(H, &h, sizeof H);
    }
    rc = upload(0, len);
    ok &= rc == ED_E_REFUSED && rc_detail == ED_D_REFUSED_DATA && !eds.on;
    for (i = 0; i < 80u; i++)
        ok &= hflash[0xA0F00u + i] == 0xFFu;
    ok &= sweep_clean() && usr_nz[0] == 0u;
    check("DATA: a piece planting an update record at a sector's +0xF00: rc 2 detail 35, dropped, never written", (int)ok);

    ok = 1;
    fresh();
    make_sample(len, 72, "CRC");                         /* the CRC alone (no 0x5441): refused too (the wider test) */
    memset(rec, 0x33, sizeof rec);
    i = crc16x(rec + 2, 78);
    rec[0] = (uint8_t)i, rec[1] = (uint8_t)(i >> 8);
    memcpy(D + at + 0x1000u, rec, 80);
    rc = begin(0, len, st_crc32(D, len));
    for (i = 0; i < len && !rc; i += 256u)
        rc = piece(0, i, D + i, len - i < 256u ? len - i : 256u);
    ok &= rc == ED_E_REFUSED && rc_detail == ED_D_REFUSED_DATA;
    check("DATA: a window whose CRC-16 matches without the magic: refused as well", (int)ok);

    ok = 1;
    fresh();                                             /* the last piece short: the window's tail is 0xFF */
    make_sample(at + 40u, 73, "TAIL");
    memset(rec, 0xFF, sizeof rec);
    for (i = 0; i < 40u; i++)
        rec[i] = (uint8_t)(i * 7u + 1u);
    rec[6] = 0x41, rec[7] = 0x54;
    i = crc16x(rec + 2, 78);
    rec[0] = (uint8_t)i, rec[1] = (uint8_t)(i >> 8);
    memcpy(D + at, rec, 40);
    {
        smp_user_hdr_t h;
        memcpy(&h, H, sizeof h);
        h.data_len = at + 40u;
        h.crc = st_crc32(D, at + 40u);
        h.zone[0].n = 2u * (at + 40u);
        h.zone[0].le = h.zone[0].n - 1u;
        memcpy(H, &h, sizeof H);
    }
    ok &= loader_takes(rec);
    rc = upload(0, at + 40u);
    ok &= rc == ED_E_REFUSED && rc_detail == ED_D_REFUSED_DATA && sweep_clean();
    check("DATA: a last piece whose bytes and the erased ones after it would read as a record: refused", (int)ok);
}

static void t_read(void)
{
    uint32_t ok = 1, off, crc, total, len;
    static uint8_t back[0x14000];
    info_t o;
    fresh();
    a_put(0);
    a_u35(0);
    a_u35(0);
    ok &= req(ED_SMP_READ) == ED_E_REFUSED && rc_detail == ED_D_NOT_PRESENT;
    make_sample(20000, 81, "LOOP");
    ok &= upload(1, 20000) == 0 && info(1, &o) == 0;
    crc = o.crc;
    total = 512u + 20000u;
    for (off = 0; off < total && ok; off += 256u) {
        a_put(1);
        a_u35(off);
        a_u35(crc);
        ok &= req(ED_SMP_READ) == 0 && r_b() == 1u && r_u35() == off;
        len = r_u14();
        ok &= len == (total - off < 256u ? total - off : 256u);
        ok &= ed_unpack7(erp + rp_i, rp_n - 1u - rp_i, back + off, 256) == len;
    }
    ok &= !memcmp(back, hflash + 0xB4000u, total) && !memcmp(back, H, ED_SMP_HDR) && !memcmp(back + 512, D, 20000);
    check("SMP_READ: the raw slot (header block + data), 256 bytes a piece, equals flash", (int)ok);
    ok = 1;
    a_put(1);
    a_u35(total);
    a_u35(crc);
    ok &= req(ED_SMP_READ) == ED_E_BAD && rc_detail == ED_D_RANGE;
    a_put(1);
    a_u35(100);
    a_u35(crc);
    ok &= req(ED_SMP_READ) == ED_E_BAD && rc_detail == ED_D_RANGE;
    a_put(1);
    a_u35(0);
    a_u35(crc ^ 1u);
    ok &= req(ED_SMP_READ) == ED_E_STALE && rc_detail == ED_D_CHANGED;
    check("SMP_READ: past the end / misaligned rc 1 detail 45, another CRC rc 5 detail 33, an empty slot rc 2 detail 30",
          (int)ok);
}

static void t_clear_rename(void)
{
    uint32_t ok = 1;
    info_t o;
    dvoice_t v;
    int32_t out[32];
    fresh();
    make_sample(20000, 91, "HAT");
    ok &= upload(2, 20000) == 0;
    memset(&v, 0, sizeof v);                          /* a voice in USR3's sample (its data is read only on the
                                                       * device: SMP_DATA's index past its bounds reads the slot) */
    v.sv.s[4] = (int32_t)(0x8000u | 2u << 5);
    v.sv.s[5] = 65536;
    v.sv.ph[0] = 1;
    v.env[0] = ENV1;
    v.k[0] = dk(127);
    memset(out, 0, sizeof out);
    ok &= usr_zone[2][0].n == 40000u;
    song.playing = 1;
    a_put(2);
    ok &= req(ED_SMP_CLEAR) == ED_E_STOP && usr_nz[2] == 1u;
    song.playing = 0;
    host_nflog = 0;
    a_put(2);
    ok &= req(ED_SMP_CLEAR) == 0 && usr_nz[2] == 0u && info(2, &o) == 0 && !o.valid;
    ok &= host_nflog == 1u && host_flog[0].erase && host_flog[0].off == 0xC8000u;
    ok &= smp_render(&v, out, 32, 32767, 32767, 0) == 0;   /* the voice playing it ended */
    power_cycle();
    ok &= usr_nz[2] == 0u;
    check("SMP_CLEAR: stopped only; the header sector erased, the slot empty, its voice ended", (int)ok);

    ok = 1;
    fresh();
    make_sample(6000, 92, "OLDNAME");
    ok &= upload(0, 6000) == 0;
    {
        static const char *bad[] = {"", "NINECHARS", "A\x7F"};
        uint32_t b, j;
        for (b = 0; b < 3u; b++) {
            a_put(0);
            for (j = 0; bad[b][j]; j++)
                a_put((uint8_t)bad[b][j]);
            a_put(0);
            ok &= req(ED_SMP_RENAME) == ED_E_BAD;
        }
        a_put(0);
        a_put('A');                                       /* no terminating 0 */
        ok &= req(ED_SMP_RENAME) == ED_E_BAD;
    }
    a_put(0);
    a_put('K'), a_put('I'), a_put('C'), a_put('K'), a_put(' '), a_put('2'), a_put(0);
    ok &= req(ED_SMP_RENAME) == 0 && r_b() == 0u;
    ok &= info(0, &o) == 0 && o.valid && !strcmp(o.name, "KICK 2") && o.len == 6000u && o.crc == st_crc32(D, 6000);
    ok &= !memcmp(hflash + 0xA0000u + 512u, D, 6000) && flog_in(0) && usr_nz[0] == 1u;
    a_put(1);
    a_put('X'), a_put(0);
    ok &= req(ED_SMP_RENAME) == ED_E_REFUSED && rc_detail == ED_D_NOT_PRESENT;
    song.playing = 1;
    a_put(0);
    a_put('X'), a_put(0);
    ok &= req(ED_SMP_RENAME) == ED_E_STOP;
    song.playing = 0;
    check("SMP_RENAME: the name changes, the data and its CRC kept; bad names rc 1; an empty slot rc 2 detail 30; "
          "playing rc 3",
          (int)ok);
}

int main(void)
{
    t_info();
    t_law();
    t_upload();
    t_limits();
    t_header_last();
    t_power_cut();
    t_refusals();
    t_loader_window();
    t_read();
    t_clear_rename();
    t_fuzz();
    check("flash: the Bluetooth store never touched", host_fl_bt == 0);
    printf(fails ? "editor_sample_test: %d FAILED\n" : "editor_sample_test: all passed\n", fails);
    return fails ? 1 : 0;
}
