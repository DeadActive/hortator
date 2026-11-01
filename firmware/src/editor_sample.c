/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Hortator editor protocol 1.3: the user sample slots USR1..USR3 (web/EDITOR_PROTOCOL.md 2.7 and its Appendix).
 * After upstream Felucca's sample upload (hugelton/Felucca, firmware/src/editor.c SMP_BEGIN .. SMP_INFO: the header
 * sector erased first so the slot reads empty, the data written sector by sector, each sector erased when the data
 * reaches it, the header last after the data's CRC-32 is checked), in this protocol's framing (editor_proto.h).
 * Included by felucca.c after editor_backup.c, before editor.c, which calls the hooks. Main loop only.
 * - The slot limits are law: every erase and program goes through eds_erase / eds_prog, which refuse anything
 *   outside the slot the command names (eng_sample.c smp_user_size: USR1 80 KiB, USR2 / USR3 48 KiB; the variations
 *   at 0xC0000 and the lock banks at 0xD4000 follow them).
 * - A power cut anywhere in an upload leaves the slot empty: BEGIN erases the header sector first, END writes the
 *   header's bytes 4..479 and then its magic.
 * - The update loader's sweep (firmware/loader/ldr_core.c, frozen) erases a sector in 0x93000..0xFBFFF whose bytes
 *   +0xF00.. read as an update record (a CRC-16 at +0 over +2..+79, the magic 0x5441 at +6); the chip's boot ROM looks
 *   for its records at the same place. A piece holding a sector's +0xF00 window whose CRC-16 matches (the magic not
 *   even asked: a superset of the loader's test) is refused and never written (eds_record).
 * - Flash writes only while stopped (they stall the audio: st_erase silences it), never during an update (usb.ota_req,
 *   the editor's update silence drops an upload) or a restore. Scratch RAM: project.c's usr_head (a piece) and
 *   storage.c's st_buf (END's header, RENAME's sector), as project.c's USR2 / USR3 clamp uses them; a queued cue,
 *   staged in st_buf, is cancelled first. */
#include "editor_proto.h"
#define ED_HAS_SAMPLES 1
#ifndef ED_HAS_BACKUP
static int ed_restoring(void) { return 0; }        /* (no restores in this build) */
#endif
#ifndef EDS_MS
#define EDS_MS() fm1_ms
#endif
#ifndef EDS_OTA_REQ
#define EDS_OTA_REQ() (usb.ota_req != 0u)
#endif
#define EDS_NO(cmd, rc, d, m)                                                                                          \
    do {                                                                                                               \
        ed_fail(cmd, rc, d, m);                                                                                        \
        return;                                                                                                        \
    } while (0)
_Static_assert(sizeof(smp_user_hdr_t) == ED_SMP_HDR && ED_SMP_HDR <= SMP_USER_DATA, "the slot header: 480 B before the data");
_Static_assert(ED_PIECE == 256u && sizeof usr_head == ED_PIECE && SMP_USER_DATA % ED_PIECE == 0u,
               "pieces are 256 B on 256-B slot offsets: a sector's +0xF00 window starts a piece");
_Static_assert(ST_PAYLOAD_MAX >= 0x1000u - 256u, "st_buf holds a sector past usr_head's 256 bytes");

static struct {
    uint8_t on, slot;
    uint32_t len, crc, got, last;                   /* data bytes received in order; the last piece's offset + 1 */
    uint32_t t;                                     /* the last editor request (ED_RESTORE_IDLE_MS) */
} eds;

static uint32_t eds_base(uint32_t k) { return SMP_USER_BASE + k * SMP_USER_SIZE; }
static uint32_t eds_max(uint32_t k) { return smp_user_size(k) - SMP_USER_DATA; }
/* [off, off + n) inside slot k: the only flash this file erases or programs */
static int eds_in_slot(uint32_t k, uint32_t off, uint32_t n)
{
    uint32_t b, e;
    if (k >= SMP_USER_SLOTS)
        return 0;
    b = eds_base(k);
    e = b + smp_user_size(k);
    return off >= b && off < e && n && n <= e - off;
}
static int eds_erase(uint32_t k, uint32_t off)
{
    if ((off & 0xFFFu) || !eds_in_slot(k, off, 0x1000u))
        return -9;
    return st_erase(off);
}
static int eds_prog(uint32_t k, uint32_t off, const void *p, uint32_t n)
{
    if (!eds_in_slot(k, off, n))
        return -9;
    return st_prog(off, p, n);
}

/* 80 bytes at a sector's +0xF00 that the loader / boot ROM could take for an update record: their u16 at 0 is not
 * 0 and is the CRC-16 (poly 0x1021, init 0) of bytes 2..79 (ldr_core.c also wants 0x5441 at 6: not asked here) */
static int eds_record(const uint8_t *w)
{
    uint32_t c = 0, i, j, v = (uint32_t)w[0] | (uint32_t)w[1] << 8;
    for (i = 2; i < 80u; i++) {
        c ^= (uint32_t)w[i] << 8;
        for (j = 0; j < 8u; j++)
            c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1;
        c &= 0xFFFFu;
    }
    return v && v == c;
}

/* slot k's voices end (its zones read 0 samples) and nothing reads its flash until it is scanned again */
static void eds_silence(uint32_t k)
{
    uint32_t i;
    fm1_irq_off();
    usr_nz[k] = 0;
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;
    smp_user_gen++;
    fm1_irq_on();
}
static void eds_rescan(uint32_t k)
{
    fm1_irq_off();
    smp_user_scan(k);
    fm1_irq_on();
}

/* a header the device plays (eng_sample.c smp_user_scan's checks), for slot k, data length len, CRC crc */
static int eds_hdr_ok(const smp_user_hdr_t *h, uint32_t k, uint32_t len, uint32_t crc)
{
    uint32_t i, sz = smp_user_size(k);
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u || h->data_len != len ||
        h->data_len > sz - SMP_USER_DATA || h->crc != crc)
        return 0;
    for (i = 0; i < h->nz; i++) {
        const smp_zone_t *z = &h->zone[i];
        if (z->off > h->data_len || z->n > 2u * sz || z->off + (z->n + 1u) / 2u > h->data_len || z->idx > 88u ||
            !z->rate || z->rate > 4u << 16 || z->ls > z->le || z->le >= z->n || z->lo > z->hi)
            return 0;
    }
    return 1;
}

static void eds_drop(void) { eds.on = 0; }         /* the slot stays empty (its header sector erased at BEGIN) */
static void ed_sample_drop(void) { eds_drop(); }
static void ed_sample_poll(void)
{
    if (!eds.on)
        return;
    if (EDS_MS() - eds.t > ED_RESTORE_IDLE_MS)
        eds_drop();
    else if (ui.msg_t < 2u) {
        char m[] = "UPLOAD USR1";
        m[10] = (char)('1' + eds.slot);
        ui_message(m);
    }
}

/* flash may be written now for slot k (BEGIN, END, CLEAR, RENAME): 1, else refused (replied) */
static int eds_can_write(uint32_t cmd, uint32_t k, const char *stop)   /* k 3: no slot's clamp asked */
{
    if (transport_busy() || chain_busy())
        ed_fail(cmd, ED_E_STOP, ED_D_STOP, stop);
    else if (ed_restoring())
        ed_fail(cmd, ED_E_BUSY, ED_D_RESTORING, "RESTORING");
    else if (EDS_OTA_REQ())
        ed_fail(cmd, ED_E_BUSY, ED_D_NONE, "UPDATE");
    else if ((k == 1u && var_blocked) || (k == 2u && lk_blocked))
        ed_fail(cmd, ED_E_REFUSED, ED_D_BLOCKED, k == 1u ? "USR2 ERROR: NO SAVE" : "USR3 CLAMP FAILED");
    else
        return 1;
    return 0;
}

static void eds_info(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    uint32_t v = usr_nz[k] != 0u && !(eds.on && eds.slot == k), i;
    char nm[9];
    for (i = 0; i < 8u; i++)
        nm[i] = v ? h->name[i] : 0;
    nm[8] = 0;
    ed_reply(ED_SMP_INFO, ED_OK);
    ed_b(k);
    ed_b(v);
    ed_str(nm);
    ed_u35(v ? h->data_len : 0u);
    ed_u35(v ? h->crc : 0u);
    ed_u35(eds_max(k));
    ed_b(v ? h->nz : 0u);
    ed_u35(v ? h->zone[0].n : 0u);
    ed_u35(v ? h->zone[0].rate : 0u);
    ed_send();
}

static void eds_begin(const uint8_t *a)
{
    uint32_t k = a[0], len = ed_get_u35(a + 1), crc = ed_get_u35(a + 6);
    if (k >= SMP_USER_SLOTS || !len)
        EDS_NO(ED_SMP_BEGIN, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (len > eds_max(k))
        EDS_NO(ED_SMP_BEGIN, ED_E_REFUSED, ED_D_TOO_BIG, "TOO BIG");
    if (!eds_can_write(ED_SMP_BEGIN, k, "STOP TO UPLOAD"))
        return;
    eds_drop();                                     /* an upload running: dropped (its slot stays empty) */
    cue_cancel();                                   /* (st_buf: END's scratch) */
    eds_silence(k);
    if (eds_erase(k, eds_base(k))) {                /* the header sector: the slot reads empty from now */
        eds_rescan(k);
        EDS_NO(ED_SMP_BEGIN, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
    }
    eds.on = 1;
    eds.slot = (uint8_t)k;
    eds.len = len;
    eds.crc = crc;
    eds.got = eds.last = 0;
    ed_reply(ED_SMP_BEGIN, ED_OK);
    ed_b(k);
    ed_u35(eds_max(k));
    ed_send();
}

static void eds_data(const uint8_t *a, uint32_t n)
{
    uint32_t k = a[0], off = ed_get_u35(a + 1), len, so, i;
    uint8_t *p = usr_head;
    if (!eds.on || k != eds.slot)
        EDS_NO(ED_SMP_DATA, ED_E_BAD, ED_D_NOT_RESTORING, "NO UPLOAD");
    if (transport_busy() || chain_busy())
        EDS_NO(ED_SMP_DATA, ED_E_STOP, ED_D_STOP, "STOP TO UPLOAD");
    if (off != eds.got && off + 1u != eds.last)     /* (only the next piece or the last one again: below the length,
                                                     * on a piece's offset) */
        EDS_NO(ED_SMP_DATA, ED_E_STALE, ED_D_ORDER, "OUT OF ORDER");
    if (off % ED_PIECE || off >= eds.len)
        EDS_NO(ED_SMP_DATA, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    len = eds.len - off < ED_PIECE ? eds.len - off : ED_PIECE;
    if (n - ED_LEN_SMP_DATA_MIN != len + (len + 6u) / 7u ||
        ed_unpack7(a + ED_LEN_SMP_DATA_MIN, n - ED_LEN_SMP_DATA_MIN, p, len) != len)
        EDS_NO(ED_SMP_DATA, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    so = SMP_USER_DATA + off;                       /* the piece's place in the slot (256-aligned) */
    if (off != eds.got) {                           /* a retry of the last piece: answered when flash holds it */
        if (memcmp(smp_user_xip(k) + so, p, len))
            EDS_NO(ED_SMP_DATA, ED_E_STALE, ED_D_ORDER, "OUT OF ORDER");
    } else {
        if ((so & 0xFFFu) == 0xF00u) {              /* a sector's loader window: as flash will hold it */
            for (i = len; i < 80u; i++)
                p[i] = 0xFFu;
            if (eds_record(p)) {
                eds_drop();
                EDS_NO(ED_SMP_DATA, ED_E_REFUSED, ED_D_REFUSED_DATA, "DATA REFUSED");
            }
        }
        if ((!(so & 0xFFFu) && eds_erase(k, eds_base(k) + so)) || eds_prog(k, eds_base(k) + so, p, len) ||
            memcmp(smp_user_xip(k) + so, p, len)) {
            eds_drop();
            EDS_NO(ED_SMP_DATA, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
        }
        eds.got = off + len;
        eds.last = off + 1u;
    }
    ed_reply(ED_SMP_DATA, ED_OK);
    ed_b(k);
    ed_u35(off);
    ed_u14(len);
    ed_send();
}

static void eds_end(const uint8_t *a, uint32_t n)
{
    uint32_t k = a[0];
    smp_user_hdr_t *h = (smp_user_hdr_t *)(void *)st_buf;
    if (!eds.on || k != eds.slot)
        EDS_NO(ED_SMP_END, ED_E_BAD, ED_D_NOT_RESTORING, "NO UPLOAD");
    if (transport_busy() || chain_busy())
        EDS_NO(ED_SMP_END, ED_E_STOP, ED_D_STOP, "STOP TO UPLOAD");
    cue_cancel();
    if (ed_unpack7(a + 1, n - 1u, st_buf, ED_SMP_HDR) != ED_SMP_HDR)
        EDS_NO(ED_SMP_END, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    if (eds.got != eds.len)
        EDS_NO(ED_SMP_END, ED_E_REFUSED, ED_D_INCOMPLETE, "INCOMPLETE");
    if (!eds_hdr_ok(h, k, eds.len, eds.crc)) {
        eds_drop();
        EDS_NO(ED_SMP_END, ED_E_REFUSED, ED_D_INVALID, "INVALID DATA");
    }
    if (st_crc32(smp_user_xip(k) + SMP_USER_DATA, eds.len) != eds.crc) {
        eds_drop();
        EDS_NO(ED_SMP_END, ED_E_REFUSED, ED_D_CRC, "CRC MISMATCH");
    }
    if (EDS_OTA_REQ() || ed_restoring()) {          /* the update check, right before the header */
        eds_drop();
        EDS_NO(ED_SMP_END, ED_E_BUSY, EDS_OTA_REQ() ? ED_D_NONE : ED_D_RESTORING, EDS_OTA_REQ() ? "UPDATE" : "RESTORING");
    }
    eds_drop();
    if (eds_prog(k, eds_base(k) + 4u, st_buf + 4, ED_SMP_HDR - 4u) || eds_prog(k, eds_base(k), st_buf, 4u))
        EDS_NO(ED_SMP_END, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");   /* (the magic, last) */
    eds_rescan(k);
    ed_reply(ED_SMP_END, ED_OK);
    ed_b(k);
    ed_b(usr_nz[k] != 0u);
    ed_send();
}

static void eds_read(const uint8_t *a)
{
    uint32_t k = a[0], off = ed_get_u35(a + 1), crc = ed_get_u35(a + 6), total, len;
    const smp_user_hdr_t *h;
    if (k >= SMP_USER_SLOTS)
        EDS_NO(ED_SMP_READ, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    h = (const smp_user_hdr_t *)smp_user_xip(k);
    if (!usr_nz[k])
        EDS_NO(ED_SMP_READ, ED_E_REFUSED, ED_D_NOT_PRESENT, "NO DATA");
    if (h->crc != crc)
        EDS_NO(ED_SMP_READ, ED_E_STALE, ED_D_CHANGED, "CHANGED: READ AGAIN");
    total = SMP_USER_DATA + h->data_len;            /* (a scanned header: inside the slot) */
    if (off % ED_PIECE || off >= total)
        EDS_NO(ED_SMP_READ, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    len = total - off < ED_PIECE ? total - off : ED_PIECE;
    ed_reply(ED_SMP_READ, ED_OK);
    ed_b(k);
    ed_u35(off);
    ed_u14(len);
    ed_pack(smp_user_xip(k) + off, len);
    ed_send();
}

static void eds_clear(uint32_t k)
{
    if (k >= SMP_USER_SLOTS)
        EDS_NO(ED_SMP_CLEAR, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (!eds_can_write(ED_SMP_CLEAR, SMP_USER_SLOTS, "STOP FIRST"))   /* (a slot whose clamp failed: emptied) */
        return;
    if (eds.on && eds.slot == k)
        eds_drop();
    eds_silence(k);
    if (eds_erase(k, eds_base(k))) {
        eds_rescan(k);
        EDS_NO(ED_SMP_CLEAR, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
    }
    eds_rescan(k);
    ed_reply(ED_SMP_CLEAR, ED_OK);
    ed_b(k);
    ed_send();
}

/* the header sector rewritten with another name: its bytes 0..255 in usr_head, 256..4095 in st_buf (as project.c's
 * clamp), erased, the data part programmed, then the header (its magic last) */
static void eds_rename(const uint8_t *a, uint32_t n)
{
    uint32_t k = a[0], len = n - 2u, i, b;
    char nm[9];
    if (k >= SMP_USER_SLOTS || a[n - 1u] || len < 1u || len > 8u)
        EDS_NO(ED_SMP_RENAME, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    for (i = 0; i < 8u; i++) {
        nm[i] = (char)(i < len ? a[1u + i] : 0u);
        if (i < len && (a[1u + i] < 32u || a[1u + i] > 126u))
            EDS_NO(ED_SMP_RENAME, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    }
    nm[8] = 0;
    if (!eds_can_write(ED_SMP_RENAME, SMP_USER_SLOTS, "STOP FIRST"))
        return;
    if (!usr_nz[k] || (eds.on && eds.slot == k))
        EDS_NO(ED_SMP_RENAME, ED_E_REFUSED, ED_D_NOT_PRESENT, "NO DATA");
    cue_cancel();
    b = eds_base(k);
    if (st_read(b, usr_head, sizeof usr_head) || st_read(b + 256u, st_buf, 0x1000u - 256u))
        EDS_NO(ED_SMP_RENAME, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
    for (i = 0; i < 8u; i++)
        usr_head[8u + i] = (uint8_t)nm[i];
    eds_silence(k);                                 /* (its data in this sector is rewritten too) */
    if (eds_erase(k, b)) {
        eds_rescan(k);
        EDS_NO(ED_SMP_RENAME, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
    }
    for (i = 0; i < 0x1000u - 256u; i += 256u) {    /* the data part (pages all 0xFF: nothing to program) */
        uint32_t j, any = 0;
        for (j = 0; j < 256u; j++)
            any |= st_buf[i + j] ^ 0xFFu;
        if (any && eds_prog(k, b + 256u + i, st_buf + i, 256u)) {
            eds_rescan(k);
            EDS_NO(ED_SMP_RENAME, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
        }
    }
    i = (uint32_t)(eds_prog(k, b + 4u, usr_head + 4, 256u - 4u) || eds_prog(k, b, usr_head, 4u));
    eds_rescan(k);
    if (i)
        EDS_NO(ED_SMP_RENAME, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
    ed_reply(ED_SMP_RENAME, ED_OK);
    ed_b(k);
    ed_str(nm);
    ed_send();
}

/* editor.c's dispatch: 1 handled (replied). Every request keeps an upload alive */
static int ed_sample_cmd(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    eds.t = EDS_MS();
    if (cmd < ED_SMP_INFO || cmd > ED_SMP_RENAME)
        return 0;
    if (cmd == ED_SMP_INFO ? n != ED_LEN_SMP_INFO : cmd == ED_SMP_BEGIN ? n != ED_LEN_SMP_BEGIN
        : cmd == ED_SMP_DATA ? n < ED_LEN_SMP_DATA_MIN : cmd == ED_SMP_END ? n != ED_LEN_SMP_END
        : cmd == ED_SMP_READ ? n != ED_LEN_SMP_READ : cmd == ED_SMP_CLEAR ? n != ED_LEN_SMP_CLEAR
        : n < ED_LEN_SMP_RENAME_MIN || n > ED_LEN_SMP_RENAME_MIN + 7u)
        ed_fail(cmd, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    else if (!flash_ok)
        ed_fail(cmd, ED_E_REFUSED, ED_D_NO_FLASH, "NO FLASH");
    else if (cmd == ED_SMP_INFO) {
        if (a[0] >= SMP_USER_SLOTS)
            ed_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
        else
            eds_info(a[0]);
    } else if (cmd == ED_SMP_BEGIN)
        eds_begin(a);
    else if (cmd == ED_SMP_DATA)
        eds_data(a, n);
    else if (cmd == ED_SMP_END)
        eds_end(a, n);
    else if (cmd == ED_SMP_READ)
        eds_read(a);
    else if (cmd == ED_SMP_CLEAR)
        eds_clear(a[0]);
    else
        eds_rename(a, n);
    return 1;
}
