/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * The backup LIST / GET shape and its result codes after upstream Felucca's editor (hugelton/Felucca 5d9694d,
 * editor.c, GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments). */
/* Hortator editor protocol 1: BACKUP_LIST, BACKUP_GET, RESTORE (web/EDITOR_PROTOCOL.md 2.5 / 2.6; spec
 * docs/superpowers/specs/2026-10-31-web-editor-design.md "Staging"). Included by felucca.c after project.c, before
 * editor.c, which calls the hooks (editor_proto.h). Main loop only.
 * - Backups read flash, never RAM: each object's copy as the device loads it (storage.c's current copy; a lock bank /
 *   variations the copy its flash record links), straight through XIP: st_buf is left alone (a queued cue is staged
 *   there). Unsaved RAM changes are only flagged.
 * - A restore stages into the RAM homes the device has (no new buffers): the record -> proj_slot[s], the lock bank ->
 *   lk_slot[s], the variations -> the park var_buf[var_play ^ 1], the user presets -> upr. COMMIT checks the CRCs and
 *   the boot checks, re-seals a project for its target slot and writes it in SAVE's order (variations, lock bank, then
 *   the record: storage.c's A/B copies keep the old or the new project whole), stopped only. ABORT, the idle timeout
 *   and update traffic (ed_backup_drop) reload the homes from flash.
 * - RESTORING (BEGIN to its end): ui.c ed_lock_on refuses the device's own PLAY, cue keys, LOAD, SAVE, PASTE, NAME
 *   and preset save / delete (main loop only, as the keys are read); editor writes get rc 6 detail 40. */
#include "editor_proto.h"
#define ED_HAS_BACKUP 1
#ifndef EDB_MS
#define EDB_MS() fm1_ms                             /* (host tests: their clock) */
#endif
#ifndef EDB_OTA_REQ
#define EDB_OTA_REQ() (usb.ota_req != 0u)           /* the upgrade command waits: no commit now */
#endif
#define EDB_NO(cmd, rc, d, m)                                                                                          \
    do {                                                                                                               \
        ed_fail(cmd, rc, d, m);                                                                                        \
        return;                                                                                                        \
    } while (0)

static struct {
    uint8_t on, kind, slot, park, ld, ver;          /* restoring; the target; the park staged into; the target is the
                                                     * loaded slot; the record's format (15 FDRF, 1..14 older) */
    uint16_t size[ED_PART_N], got[ED_PART_N], last[ED_PART_N];   /* last: the last piece's offset + 1 (0 none) */
    uint32_t crc[ED_PART_N], magic, t;              /* t: the last request (ED_RESTORE_IDLE_MS) */
} edb;

/* a project record format's size (1..14 FDR1..FDRE, converted at load; 15 FDRF) */
static const uint16_t EDB_RSZ[16] = {0, sizeof proj_old.v1, sizeof proj_old.v2, sizeof proj_old.v3, sizeof proj_old.v4,
                                     sizeof proj_old.v5, sizeof proj_old.v6, sizeof proj_old.v7, sizeof proj_old.v8,
                                     sizeof proj_old.v9, sizeof proj_old.va, sizeof proj_old.vb, sizeof proj_old.vc,
                                     sizeof proj_old.vd, sizeof proj_old.ve, sizeof(project_t)};
static uint32_t edb_fmt(uint32_t m)                 /* a record magic's format: 1..15, 0 not "FDR1".."FDRF" */
{
    uint32_t v = m >> 24;
    if ((m & 0xFFFFFFu) != 0x524446u)
        return 0;
    return v >= '1' && v <= '9' ? v - '0' : v >= 'A' && v <= 'F' ? v - 'A' + 10u : 0u;
}
/* a whole record's format: its magic, its size field = n = the format's size, its FNV-1a sum (proj_fetch's checks);
 * 0 not one */
static uint32_t edb_rec_ver(const uint8_t *p, uint32_t n)
{
    uint32_t h[2], sum, v;
    if (n < 12u)
        return 0;
    memcpy(h, p, 8);
    memcpy(&sum, p + n - 4u, 4);
    v = edb_fmt(h[0]);
    return v && EDB_RSZ[v] == n && h[1] == n && sum == proj_hash(p, n - 4u) ? v : 0u;
}

/* storage object so's copy c: its commit record valid and its payload's CRC right, read through XIP -> the payload */
static const uint8_t *edb_copy(uint32_t so, uint32_t c, st_hdr_t *h)
{
    const uint8_t *p;
    if (st_head(so, c, h))
        return 0;
    p = (const uint8_t *)VAR_XIP(st_sector(so, c) + ST_PAYLOAD_OFF);
    return st_crc32(p, h->len) == h->crc ? p : 0;
}
static const uint8_t *edb_current(uint32_t so, st_hdr_t *h)   /* st_current's choice, st_buf untouched */
{
    st_hdr_t a, b;
    int va = st_head(so, 0, &a) == 0, vb = st_head(so, 1, &b) == 0;
    uint32_t first = vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u)), k;
    const uint8_t *p;
    for (k = 0; k < 2u; k++)
        if ((first ^ k ? vb : va) && (p = edb_copy(so, first ^ k, h)) != 0)
            return p;
    return 0;
}

typedef struct { const uint8_t *p; uint32_t len, crc, seq, flags; } edb_obj_t;   /* p 0: not present */
/* backup object obj as a power cycle would load it (BACKUP_LIST's fields) */
static void edb_obj(uint32_t obj, edb_obj_t *o)
{
    st_hdr_t h;
    const uint8_t *p, *b = 0;
    const project_t *q;
    uint32_t s = (obj - 1u) & 3u, c, v;
    memset(o, 0, sizeof *o);
    if (!obj) {                                     /* the user presets (preset_user_boot's record) */
        o->flags = upr_pending ? ED_BF_UNSAVED : 0u;
        b = edb_current(OBJ_UPRESET0 + 1u, &h);
        if (b && (h.len != sizeof upr || memcmp(b, "UPR1", 4)))
            b = 0;
    } else {
        p = edb_current(OBJ_PROJECT0 + s, &h);
        v = p ? edb_rec_ver(p, h.len) : 0u;
        if (!v)
            return;
        o->flags = slot_dirty(s) ? ED_BF_UNSAVED : 0u;
        q = (const project_t *)p;
        if (obj < ED_OBJ_LOCKS) {
            b = p;
            o->flags |= v < 15u ? ED_BF_OLD : 0u;
            for (c = ED_OBJ_LOCKS + s; v == 15u && c < ED_OBJ_N; c += 4u) {   /* LOST: a linked object missing */
                edb_obj_t x;
                edb_obj(c, &x);
                if (!x.p && (c < ED_OBJ_VARS ? q->lk_n : q->var_len))
                    o->flags |= ED_BF_LOST;
            }
        } else if (v == 15u && (obj < ED_OBJ_VARS ? q->lk_n : q->var_len)) {
            for (c = 0; c < 2u && !b; c++) {        /* the copy the record links (proj_bank_fetch, proj_var_flash) */
                b = edb_copy(obj < ED_OBJ_VARS ? OBJ_LOCKS0 + s : OBJ_VAR0 + s, c, &h);
                if (b && !(obj < ED_OBJ_VARS ? h.len == sizeof(lk_bank_t) && lk_valid((const lk_bank_t *)b, s) &&
                                                   ((const lk_bank_t *)b)->count == q->lk_n &&
                                                   proj_link((const lk_bank_t *)b) == q->lk_link
                                             : h.len == q->var_len && h.len >= 8u && h.len <= VAR_BYTES &&
                                                   var_valid((const var_bank_t *)b, s, h.len) &&
                                                   var_link_of((const var_bank_t *)b) == q->var_link))
                    b = 0;
            }
        }
    }
    if (!b)
        return;
    o->p = b;
    o->len = h.len;
    o->crc = h.crc;
    o->seq = h.seq;
}

/* the RAM home of a restore's part, and its size */
static uint8_t *edb_home(uint32_t part)
{
    if (edb.kind == ED_RK_PRESETS)
        return (uint8_t *)&upr;
    return part == ED_PART_RECORD ? (uint8_t *)&proj_slot[edb.slot]
           : part == ED_PART_LOCKS ? (uint8_t *)&lk_slot[edb.slot]
                                   : (uint8_t *)&var_buf[edb.park];
}
static uint32_t edb_home_n(uint32_t part)
{
    if (edb.kind == ED_RK_PRESETS)
        return sizeof upr;
    return part == ED_PART_RECORD ? sizeof(project_t) : part == ED_PART_LOCKS ? sizeof(lk_bank_t) : sizeof(var_bank_t);
}

/* the restore ends: its homes from flash again (after a COMMIT: what it wrote) */
static void edb_end(void)
{
    uint32_t s = edb.slot, bit = 1u << s;
    edb.on = 0;
    ed_lock_on = 0;                                 /* (ui.c: the device's keys again) */
    if (edb.kind == ED_RK_PRESETS) {
        preset_user_boot();
        return;
    }
    proj_fetch(s);                                  /* the record and its lock bank */
    proj_dirty &= (uint8_t)~bit;
    if (!edb.ld)
        var_dirty &= (uint8_t)~bit;                 /* (no RAM variations of it: its park was dropped at BEGIN) */
    if (paste_undo == s + 1u)
        paste_undo = 0;
}
static void ed_backup_drop(void)
{
    if (edb.on)
        edb_end();
}
static int ed_restoring(void) { return edb.on; }

static void ed_backup_poll(void)
{
    if (!edb.on)
        return;
    if (EDB_MS() - edb.t > ED_RESTORE_IDLE_MS)
        edb_end();
    else if (ui.msg_t < 2u)
        ui_message("RESTORING");
}

/* COMMIT's checks passed: re-seal (a project) and write, in SAVE's order; 0 ok */
static int edb_write(void)
{
    uint32_t s = edb.slot;
    project_t *q = &proj_slot[s];
    var_bank_t *vb = &var_buf[edb.park];
    if (edb.kind == ED_RK_PRESETS)
        return st_save(OBJ_UPRESET0 + 1u, &upr, sizeof upr);
    if (edb.ver != 15u)                             /* an older record: as it is, converted at load */
        return st_save(OBJ_PROJECT0 + s, q, edb.size[ED_PART_RECORD]);
    if (q->lk_n)
        lk_seal(&lk_slot[s], s);
    else
        lk_empty(s);
    q->lk_link = proj_link(&lk_slot[s]);
    q->lk_n = lk_slot[s].count;
    if (q->var_len) {
        vb->slot = (uint8_t)s;
        q->var_link = var_link_of(vb);
    }
    q->sum = proj_sum(q);
    return (q->var_len && var_write(s, vb)) || st_save(OBJ_LOCKS0 + s, &lk_slot[s], sizeof lk_slot[0]) ||
           st_save(OBJ_PROJECT0 + s, q, sizeof *q);
}

/* the staged objects pass the device's boot checks (their CRCs right) */
static int edb_valid(void)
{
    const project_t *q = &proj_slot[edb.slot];
    const lk_bank_t *b = &lk_slot[edb.slot];
    const var_bank_t *v = &var_buf[edb.park];
    uint32_t i;
    if (memcmp(edb_home(ED_PART_RECORD), &edb.magic, 4))
        return 0;
    if (edb.kind == ED_RK_PRESETS) {
        for (i = 0; i < PRE_NUSER; i++)
            if (upr.s[i].used > 1u || (upr.s[i].used && upr.s[i].model >= NMODELS))
                return 0;
        return 1;
    }
    if (edb_rec_ver(edb_home(ED_PART_RECORD), edb.size[ED_PART_RECORD]) != edb.ver)
        return 0;
    if (edb.ver != 15u)
        return 1;
    return !q->lk_n == !edb.size[ED_PART_LOCKS] && !q->var_len == !edb.size[ED_PART_VARS] &&
           (!q->lk_n || (b->slot < 4u && lk_valid(b, b->slot) && b->count == q->lk_n && proj_link(b) == q->lk_link)) &&
           (!q->var_len || (v->slot < 4u && q->var_len == edb.size[ED_PART_VARS] && var_valid(v, v->slot, q->var_len) &&
                            var_link_of(v) == q->var_link));
}

/* flash may be written now (BEGIN, COMMIT): 1, else refused (replied) */
static int edb_can_write(uint32_t kind)
{
    if (transport_busy() || chain_busy())
        ed_fail(ED_RESTORE, ED_E_STOP, ED_D_STOP, "STOP FIRST");
    else if (kind == ED_RK_PROJECT && (var_blocked || lk_blocked))
        ed_fail(ED_RESTORE, ED_E_REFUSED, ED_D_BLOCKED, var_blocked ? "USR2 ERROR: NO SAVE" : "USR3 CLAMP FAILED");
    else
        return 1;
    return 0;
}

static void edb_begin(const uint8_t *a, uint32_t n)
{
    uint32_t kind, s, m, v, c, i;
    if (n != ED_LEN_RESTORE_BEGIN)
        EDB_NO(ED_RESTORE, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    if (!edb_can_write(ED_RK_PRESETS))              /* (playing: nothing changes, a restore running neither) */
        return;
    ed_backup_drop();                               /* a restore running: dropped first */
    kind = a[1];
    s = a[2];
    m = ed_get_u35(a + 3);
    v = edb_fmt(m);
    if (kind > ED_RK_PROJECT || s > 3u || (kind == ED_RK_PRESETS && s))
        EDB_NO(ED_RESTORE, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (!edb_can_write(kind))
        return;
    for (i = 0; i < ED_PART_N; i++) {
        edb.size[i] = (uint16_t)ed_get_u14(a + 8u + 7u * i);
        edb.crc[i] = ed_get_u35(a + 10u + 7u * i);
        edb.got[i] = edb.last[i] = 0;
    }
    if (kind == ED_RK_PROJECT && !v && (m & 0xFFFFFFu) == 0x524446u && (m >> 24) > 'F')
        EDB_NO(ED_RESTORE, ED_E_REFUSED, ED_D_NEWER, "UPDATE THE FM-1 FIRST");
    if (kind == ED_RK_PRESETS ? m != UPR_MAGIC || edb.size[0] != sizeof upr || edb.size[1] || edb.size[2]
                              : !v || edb.size[0] != EDB_RSZ[v] || (v < 15u && (edb.size[1] || edb.size[2])) ||
                                    (edb.size[1] && edb.size[1] != sizeof(lk_bank_t)) ||
                                    (edb.size[2] && (edb.size[2] < ED_SZ_VARS_MIN || edb.size[2] > VAR_BYTES)))
        EDB_NO(ED_RESTORE, ED_E_REFUSED, ED_D_BAD_OBJECT, "NOT A HORTATOR OBJECT");
    cue_cancel();
    c = var_who[var_play ^ 1u];                     /* the park: another slot's unsaved variations are kept */
    if (kind == ED_RK_PROJECT && edb.size[ED_PART_VARS] && c && c - 1u != s && ((var_dirty >> (c - 1u)) & 1u)) {
        char msg[] = "SAVE A FIRST";
        msg[5] = (char)('A' + c - 1u);
        EDB_NO(ED_RESTORE, ED_E_REFUSED, ED_D_CUE_PARK + c - 1u, msg);
    }
    edb.on = 1;
    ed_lock_on = 1;                                 /* ui.c: PLAY, cue, LOAD, SAVE, preset save refused on the device */
    edb.kind = (uint8_t)kind;
    edb.slot = (uint8_t)s;
    edb.magic = m;
    edb.ver = (uint8_t)v;
    edb.park = (uint8_t)(var_play ^ 1u);
    edb.ld = kind == ED_RK_PROJECT && var_who[var_play] == s + 1u;
    edb.t = EDB_MS();
    if (kind == ED_RK_PRESETS)
        upr_pending = 0;                            /* (a change left for the STOP: replaced) */
    else if (c && (c - 1u == s || edb.size[ED_PART_VARS]))
        var_who[edb.park] = 0;                      /* the park: the target's dropped, or this restore's now */
    for (i = 0; i < ED_PART_N; i++)
        if (edb.size[i])
            memset(edb_home(i), 0, edb_home_n(i));
    ui_message("RESTORING");
    ed_reply(ED_RESTORE, ED_OK);
    ed_b(ED_RS_BEGIN);
    ed_b(kind);
    ed_b(s);
    ed_send();
}

static void edb_data(const uint8_t *a, uint32_t n)
{
    uint32_t part = a[1], off = ed_get_u14(a + 2), len;
    if (part >= ED_PART_N || !edb.size[part])
        EDB_NO(ED_RESTORE, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if ((off != edb.got[part] && off + 1u != edb.last[part]) || off >= edb.size[part])
        EDB_NO(ED_RESTORE, ED_E_STALE, ED_D_ORDER, "OUT OF ORDER");
    len = edb.size[part] - off < ED_PIECE ? edb.size[part] - off : ED_PIECE;
    if (off % ED_PIECE || n - ED_LEN_RESTORE_DATA_MIN != len + (len + 6u) / 7u ||
        ed_unpack7(a + ED_LEN_RESTORE_DATA_MIN, n - ED_LEN_RESTORE_DATA_MIN, edb_home(part) + off, len) != len)
        EDB_NO(ED_RESTORE, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    edb.last[part] = (uint16_t)(off + 1u);
    if (off == edb.got[part])
        edb.got[part] = (uint16_t)(off + len);
    ed_reply(ED_RESTORE, ED_OK);
    ed_b(ED_RS_DATA);
    ed_b(part);
    ed_u14(off);
    ed_u14(len);
    ed_send();
}

static void edb_commit(void)
{
    uint32_t i;
    if (!edb_can_write(edb.kind))                   /* (the restore stays: STOP, then COMMIT again) */
        return;
    for (i = 0; i < ED_PART_N; i++)
        if (edb.got[i] != edb.size[i])
            EDB_NO(ED_RESTORE, ED_E_REFUSED, ED_D_INCOMPLETE, "INCOMPLETE");
    for (i = 0; i < ED_PART_N; i++)
        if (edb.size[i] && st_crc32(edb_home(i), edb.size[i]) != edb.crc[i]) {
            edb_end();
            EDB_NO(ED_RESTORE, ED_E_REFUSED, ED_D_CRC, "CRC MISMATCH");
        }
    if (!edb_valid()) {
        edb_end();
        EDB_NO(ED_RESTORE, ED_E_REFUSED, ED_D_INVALID, "INVALID DATA");
    }
    if (EDB_OTA_REQ()) {                            /* the update check, right before the writes */
        edb_end();
        EDB_NO(ED_RESTORE, ED_E_BUSY, ED_D_NONE, "UPDATE");
    }
    cue_cancel();
    i = (uint32_t)edb_write();
    edb_end();                                      /* the homes from flash: what it holds now */
    if (i)
        EDB_NO(ED_RESTORE, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
    if (edb.ld)
        proj_load(edb.slot, 1);                     /* the loaded slot: loaded again, as LOAD (its live edits replaced) */
    ed_reply(ED_RESTORE, ED_OK);
    ed_b(ED_RS_COMMIT);
    ed_b(edb.kind);
    ed_b(edb.slot);
    ed_state();
    ed_send();
}

static void edb_list(void)
{
    uint32_t obj;
    edb_obj_t o;
    ed_reply(ED_BACKUP_LIST, ED_OK);
    ed_b(ED_OBJ_N);
    for (obj = 0; obj < ED_OBJ_N; obj++) {
        edb_obj(obj, &o);
        ed_b(obj);
        ed_b(o.p != 0);
        ed_u14(o.len);
        ed_u35(o.crc);
        ed_u35(o.seq);
        ed_b(o.flags);
    }
    ed_send();
}

static void edb_get(const uint8_t *a)
{
    uint32_t obj = a[0], off = ed_get_u14(a + 1), len;
    edb_obj_t o;
    if (obj >= ED_OBJ_N)
        EDB_NO(ED_BACKUP_GET, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    edb_obj(obj, &o);
    if (!o.p)
        EDB_NO(ED_BACKUP_GET, ED_E_REFUSED, ED_D_NOT_PRESENT, "NO DATA");
    if (o.seq != ed_get_u35(a + 3))
        EDB_NO(ED_BACKUP_GET, ED_E_STALE, ED_D_CHANGED, "CHANGED: READ AGAIN");
    if (off % ED_PIECE || off >= o.len)
        EDB_NO(ED_BACKUP_GET, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    len = o.len - off < ED_PIECE ? o.len - off : ED_PIECE;
    ed_reply(ED_BACKUP_GET, ED_OK);
    ed_b(obj);
    ed_u14(off);
    ed_u14(len);
    ed_pack(o.p + off, len);
    ed_send();
}

/* editor.c's dispatch: 1 handled (replied). Every request routed here keeps a restore alive */
static int ed_backup_cmd(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t op;
    edb.t = EDB_MS();
    if (cmd != ED_BACKUP_LIST && cmd != ED_BACKUP_GET && cmd != ED_RESTORE)
        return 0;
    op = n ? a[0] : 0u;
    if (cmd == ED_BACKUP_LIST ? n != ED_LEN_BACKUP_LIST : cmd == ED_BACKUP_GET ? n != ED_LEN_BACKUP_GET : n < 1u)
        ed_fail(cmd, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    else if (!flash_ok)
        ed_fail(cmd, ED_E_REFUSED, ED_D_NO_FLASH, "NO FLASH");
    else if (cmd == ED_BACKUP_LIST)
        edb_list();
    else if (cmd == ED_BACKUP_GET)
        edb_get(a);
    else if (op == ED_RS_BEGIN)
        edb_begin(a, n);
    else if (op > ED_RS_ABORT)
        ed_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    else if (!edb.on)
        ed_fail(cmd, ED_E_BAD, ED_D_NOT_RESTORING, "NO RESTORE");
    else if (op == ED_RS_DATA ? n < ED_LEN_RESTORE_DATA_MIN : n != ED_LEN_RESTORE_OP)
        ed_fail(cmd, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    else if (op == ED_RS_DATA)
        edb_data(a, n);
    else if (op == ED_RS_COMMIT)
        edb_commit();
    else {
        edb_end();
        ed_reply(cmd, ED_OK);
        ed_b(ED_RS_ABORT);
        ed_send();
    }
    return 1;
}
