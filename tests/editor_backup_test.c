/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web editor's backup / restore (firmware/src/editor_backup.c; web/EDITOR_PROTOCOL.md 2.5 / 2.6): every reply and
 * refusal, backups = flash bytes, restore staging / commit / abort / timeout / update drop, a power cut at every erase /
 * program of a project restore, a project restored into another slot. The firmware's real project.c and storage.c on a
 * host flash (tests/flash_host.h), ASan + UBSan. editor.c is not here: a test-only stand-in for its framing API
 * (editor_proto.h) records the one reply each request gets. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#include "ui_host.h"
static uint32_t host_ms;
#define EDB_MS() host_ms
#include "../firmware/src/editor_proto.h"

/* ---- editor.c's framing API, test-only: one reply buffer */
static uint8_t rp[ED_REPLY_MAX + 16];
static uint32_t rp_n, rp_sends, rp_i, q_seq;
static void ed_put(uint32_t b)
{
    if (rp_n < sizeof rp)
        rp[rp_n++] = (uint8_t)b;
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
    while (*s)
        ed_put((uint8_t)*s++);
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
        ed_put(i == 6u ? (ed_restoring() ? ED_SF_RESTORING : 0u) : 0u);
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
static uint32_t ed_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    uint32_t i = 0, o = 0, j, k, hb;
    while (i < n) {
        hb = in[i++];
        k = n - i < 7u ? n - i : 7u;
        if (!k || hb > 127u)
            return 0;
        for (j = 0; j < k; j++) {
            if (o >= max || in[i + j] > 127u)
                return 0;
            out[o++] = (uint8_t)(in[i + j] | ((hb >> j) & 1u) << 7);
        }
        i += k;
    }
    return o;
}
#include "../firmware/src/editor_backup.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}

/* ---- requests */
static uint8_t ra[ED_REQ_MAX];
static uint32_t ra_n, rc_detail;
static void a_put(uint32_t b) { ra[ra_n++] = (uint8_t)b; }
static void a_u14(uint32_t v) { a_put(v & 127u); a_put((v >> 7) & 127u); }
static void a_u35(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        a_put((v >> (7u * i)) & 127u);
}
/* one request: its rc (0x100: not exactly one well-formed reply with this cmd and seq); rc_detail its detail */
static uint32_t req(uint32_t cmd)
{
    int h;
    q_seq = (q_seq + 1u) % 127u;
    rp_n = rp_sends = 0;
    h = ed_backup_cmd(cmd, ra, ra_n);
    ra_n = 0;
    if (!h || rp_sends != 1u || rp_n < 8u || rp_n > ED_REPLY_MAX || rp[4] != cmd || rp[5] != q_seq ||
        rp[rp_n - 1u] != 0xF7)
        return 0x100;
    rp_i = 7;
    rc_detail = rp[6] ? rp[7] : 0u;
    return rp[6];
}
static uint32_t r_b(void) { return rp[rp_i++]; }
static uint32_t r_u14(void) { rp_i += 2; return ed_get_u14(rp + rp_i - 2); }
static uint32_t r_u35(void) { rp_i += 5; return ed_get_u35(rp + rp_i - 5); }

static struct { uint32_t present, size, crc, seq, flags; } L[ED_OBJ_N];
static uint32_t list(void)
{
    uint32_t i, rc = req(ED_BACKUP_LIST);
    if (rc || r_b() != ED_OBJ_N)
        return rc ? rc : 0x101;
    for (i = 0; i < ED_OBJ_N; i++) {
        if (r_b() != i)
            return 0x102;
        L[i].present = r_b();
        L[i].size = r_u14();
        L[i].crc = r_u35();
        L[i].seq = r_u35();
        L[i].flags = r_b();
    }
    return rp_i + 1u == rp_n ? 0u : 0x103;
}
static int backup(uint32_t obj, uint8_t *out)       /* a whole object through LIST + GET; its size, -1 failed */
{
    uint32_t off, len;
    if (list() || !L[obj].present)
        return -1;
    for (off = 0; off < L[obj].size; off += ED_PIECE) {
        a_put(obj);
        a_u14(off);
        a_u35(L[obj].seq);
        if (req(ED_BACKUP_GET) || r_b() != obj || r_u14() != off)
            return -1;
        len = r_u14();
        if (len != (L[obj].size - off < ED_PIECE ? L[obj].size - off : ED_PIECE) ||
            ed_unpack7(rp + rp_i, rp_n - 1u - rp_i, out + off, ED_PIECE) != len)
            return -1;
    }
    return st_crc32(out, L[obj].size) == L[obj].crc ? (int)L[obj].size : -1;
}

/* a restore: BEGIN, the parts' pieces (up to `pieces` of them in all), COMMIT (commit 0: not sent). The first rc that
 * is not 0, else 0 */
typedef struct { const uint8_t *d[3]; uint32_t n[3], crc[3]; } robj_t;
static uint32_t rs_begin(uint32_t kind, uint32_t slot, const robj_t *o)
{
    uint32_t i, m;
    memcpy(&m, o->d[0], 4);
    a_put(ED_RS_BEGIN);
    a_put(kind);
    a_put(slot);
    a_u35(m);
    for (i = 0; i < 3u; i++) {
        a_u14(o->n[i]);
        a_u35(o->crc[i]);
    }
    return req(ED_RESTORE);
}
static uint32_t rs_piece(uint32_t part, uint32_t off, const uint8_t *d, uint32_t len)
{
    a_put(ED_RS_DATA);
    a_put(part);
    a_u14(off);
    pack7(a_put, d, len);
    return req(ED_RESTORE);
}
static uint32_t rs_op(uint32_t op)
{
    a_put(op);
    return req(ED_RESTORE);
}
static uint32_t restore(uint32_t kind, uint32_t slot, const robj_t *o, uint32_t pieces, int commit)
{
    uint32_t p, off, rc = rs_begin(kind, slot, o);
    for (p = 0; !rc && p < 3u; p++)
        for (off = 0; !rc && off < o->n[p] && pieces; off += ED_PIECE, pieces--)
            rc = rs_piece(p, off, o->d[p] + off, o->n[p] - off < ED_PIECE ? o->n[p] - off : ED_PIECE);
    return rc || !commit ? rc : rs_op(ED_RS_COMMIT);
}

/* ---- fixtures */
static step_t VS[NTRK][NSTEP];
static int16_t VTM[NTRK][4];
static const step_t *VSP[NTRK];
static void wipe_ram(void)                            /* a power cycle's RAM */
{
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(lk_slot, 0, sizeof lk_slot);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    memset(&edb, 0, sizeof edb);
    ed_lock_on = 0;
    var_play = 0;
    var_dirty = 0;
    var_lost = 0;
    lk_lost = 0;
    persist_note = 0;
}
static void power_cycle(void)
{
    wipe_ram();
    host_init();
    memset(&ui, 0, sizeof ui);
    transport_req = 0;
    persist_boot();
}
static void fresh(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    power_cycle();
}
/* a project in slot s (SAVE): steps, a knob, two locks, variation 3 */
static void make_project(uint32_t s, uint32_t seed)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < NSTEP; i++) {
            memset(&trk[k].step[i], 0, sizeof trk[k].step[i]);
            trk[k].step[i].on = (i + k * 3u + seed) % 5u == 0u || (k == 0u && i % 4u == 0u);
            VS[k][i] = trk[k].step[i];
            VS[k][i].on = (i * 3u + k + seed) % 7u == 0u;
            VS[k][i].nt = (int8_t)(seed % 12u);
        }
        VTM[k][0] = (int16_t)(16u + (seed + k) % 48u);
        VTM[k][1] = (int16_t)(seed % 10u);
        VTM[k][2] = (int16_t)(seed * 7u % 101u);
        VTM[k][3] = 0;
        VSP[k] = VS[k];
    }
    trk[0].p[P_E1] = (int16_t)(seed % 40u);
    memset(&mo.b, 0, sizeof mo.b);
    lk_put(&mo.b, lk_key(1, 4, P_E1), (int32_t)(seed % 100u), 1);
    lk_put(&mo.b, lk_key(2, 8, P_E1), 20, 0);
    project_save(s);
    var_put(&var_buf[var_play], 2, VSP, VTM);
    project_save(s);
}
static uint8_t B[3][4096];                            /* a backed-up project's objects */
static robj_t backup_project(uint32_t s, uint8_t (*b)[4096])
{
    robj_t o;
    uint32_t p;
    int n;
    memset(&o, 0, sizeof o);
    for (p = 0; p < 3u; p++) {
        n = backup(ED_OBJ_PROJECT + s + 4u * p, b[p]);
        o.d[p] = b[p];
        o.n[p] = n < 0 ? 0u : (uint32_t)n;
        o.crc[p] = n < 0 ? 0u : st_crc32(b[p], o.n[p]);
    }
    return o;
}
static int same_flash(const uint8_t *keep) { return !memcmp(keep, hflash, sizeof hflash); }

/* ---- tests */
static void t_list_get(void)
{
    static uint8_t buf[4096];
    static int16_t kv[P_COUNT];
    uint32_t ok = 1, obj, i;
    fresh();
    ok &= list() == 0;
    for (obj = 0; obj < ED_OBJ_N; obj++)
        ok &= !L[obj].present && !L[obj].size && !L[obj].flags;
    check("BACKUP_LIST, nothing saved: 13 objects, none present, one reply under 400 bytes", (int)ok);

    make_project(0, 5);
    kv[P_E0] = 33;
    upr_set(3, 2, kv);
    upr_write();
    ok = list() == 0 && rp_n <= ED_REPLY_MAX;
    ok &= L[0].present && L[0].size == ED_SZ_PRESETS && L[1].present && L[1].size == ED_SZ_PROJECT && L[5].present &&
          L[5].size == ED_SZ_LOCKS && L[9].present && L[9].size == proj_slot[0].var_len && !L[2].present &&
          !L[6].present && !L[10].present;
    for (obj = 0; obj < ED_OBJ_N; obj++)
        ok &= L[obj].flags == 0u;
    for (obj = 0; obj < ED_OBJ_N; obj++) {         /* the copy LIST named (by its seq): its bytes, its CRC */
        static const uint32_t SO[4] = {OBJ_UPRESET0 + 1u, OBJ_PROJECT0, OBJ_LOCKS0, OBJ_VAR0};
        uint32_t so = obj ? SO[1u + (obj - 1u) / 4u] + (obj - 1u) % 4u : SO[0], c, hit = 0;
        st_hdr_t h, hc;
        int n;
        if (!L[obj].present)
            continue;
        n = backup(obj, buf);
        for (c = 0; c < 2u; c++)
            if (!st_head(so, c, &h) && h.seq == L[obj].seq) {
                hit = 1;
                ok &= n == (int)h.len && !memcmp(buf, hflash + st_sector(so, c) + ST_PAYLOAD_OFF, h.len) &&
                      L[obj].crc == h.crc && st_crc32(buf, h.len) == h.crc;
                if (obj < ED_OBJ_LOCKS)                 /* (a record, the user bank: storage.c's current copy) */
                    ok &= st_current(so, &hc) == (int)c;
            }
        ok &= hit;
    }
    check("BACKUP_GET: every object's bytes = its flash copy's, its CRC-32 = the storage header's (and the bytes'), "
          "seq the copy's", (int)ok);

    memset(st_buf, 0xA5, sizeof st_buf);              /* (a queued cue's staging area) */
    ok = list() == 0 && backup(1, buf) == ED_SZ_PROJECT && backup(9, buf) > 0;
    for (i = 0; i < sizeof st_buf; i++)
        ok &= st_buf[i] == 0xA5u;
    check("backups read flash through XIP: st_buf (a staged cue) untouched", (int)ok);

    trk[3].step[9].on ^= 1u;                          /* a live edit of the loaded slot A */
    upr_pending = 1;
    ok = list() == 0 && (L[0].flags & ED_BF_UNSAVED) && (L[1].flags & ED_BF_UNSAVED) && (L[5].flags & ED_BF_UNSAVED) &&
         (L[9].flags & ED_BF_UNSAVED) && !(L[2].flags & ED_BF_UNSAVED);
    {
        st_hdr_t h;
        int n = backup(1, buf);
        st_current(OBJ_PROJECT0, &h);
        ok &= n == ED_SZ_PROJECT && !memcmp(buf, st_buf, h.len);
    }
    upr_pending = 0;
    trk[3].step[9].on ^= 1u;
    check("unsaved changes: flagged UNSAVED (the slot's three objects, the user bank), the backup is flash's", (int)ok);

    ok = list() == 0;
    a_put(13); a_u14(0); a_u35(L[1].seq);
    ok &= req(ED_BACKUP_GET) == ED_E_BAD && rc_detail == ED_D_RANGE;
    a_put(2); a_u14(0); a_u35(0);
    ok &= req(ED_BACKUP_GET) == ED_E_REFUSED && rc_detail == ED_D_NOT_PRESENT;
    a_put(1); a_u14(0); a_u35(L[1].seq + 1u);
    ok &= req(ED_BACKUP_GET) == ED_E_STALE && rc_detail == ED_D_CHANGED;
    a_put(1); a_u14(100); a_u35(L[1].seq);
    ok &= req(ED_BACKUP_GET) == ED_E_BAD && rc_detail == ED_D_RANGE;
    a_put(1); a_u14(ED_SZ_PROJECT + 256u); a_u35(L[1].seq);
    ok &= req(ED_BACKUP_GET) == ED_E_BAD && rc_detail == ED_D_RANGE;
    a_put(1); a_u14(0); a_u35(L[1].seq); ra_n--;
    ok &= req(ED_BACKUP_GET) == ED_E_BAD && rc_detail == ED_D_LENGTH;
    a_put(0);
    ok &= req(ED_BACKUP_LIST) == ED_E_BAD && rc_detail == ED_D_LENGTH;
    flash_ok = 0;
    ok &= req(ED_BACKUP_LIST) == ED_E_REFUSED && rc_detail == ED_D_NO_FLASH;
    flash_ok = 1;
    ok &= ed_backup_cmd(ED_INFO, ra, 0) == 0 && ed_backup_cmd(ED_TEXT, ra, 0) == 0;
    i = L[1].seq;
    trk[1].p[P_E2] = 7;
    project_save(0);                                  /* a SAVE between LIST and GET */
    a_put(1); a_u14(0); a_u35(i);
    ok &= req(ED_BACKUP_GET) == ED_E_STALE && rc_detail == ED_D_CHANGED;
    check("BACKUP_GET / LIST refusals: object out of range, not present, changed since LIST (a SAVE), bad offset, bad "
          "length, no flash; other commands are not this file's", (int)ok);

    {                                                 /* both copies of A's lock bank damaged */
        uint32_t c;
        for (c = 0; c < 2u; c++)
            hflash[st_sector(OBJ_LOCKS0, c) + ST_PAYLOAD_OFF + 9u] ^= 0x55u;
    }
    ok = list() == 0 && L[1].present && (L[1].flags & ED_BF_LOST) && !L[5].present && L[9].present;
    check("BACKUP_LIST: a record whose lock bank no flash copy holds is LOST, the bank not present", (int)ok);
}

static void t_restore_refusals(void)
{
    static uint8_t keep[sizeof hflash], bad[4096];
    robj_t o, x;
    uint32_t ok = 1, i;
    fresh();
    make_project(0, 5);
    o = backup_project(0, B);
    memcpy(keep, hflash, sizeof keep);
    ok &= o.n[0] == ED_SZ_PROJECT && o.n[1] == ED_SZ_LOCKS && o.n[2] >= ED_SZ_VARS_MIN;
    for (i = ED_RS_DATA; i <= ED_RS_ABORT; i++)
        ok &= rs_op(i) == ED_E_BAD && rc_detail == ED_D_NOT_RESTORING;
    ok &= rs_op(4) == ED_E_BAD && rc_detail == ED_D_RANGE;
    ok &= req(ED_RESTORE) == ED_E_BAD && rc_detail == ED_D_LENGTH;
    check("RESTORE DATA / COMMIT / ABORT without BEGIN: rc 1 detail 41; an unknown op, no op: rc 1", (int)ok);

    transport_req = 1;                                /* PLAY */
    ok = rs_begin(ED_RK_PROJECT, 1, &o) == ED_E_STOP && rc_detail == ED_D_STOP && !ed_restoring() &&
         same_flash(keep);
    transport_req = 0;
    song.playing = 1;
    ok &= rs_begin(ED_RK_PRESETS, 0, &o) == ED_E_STOP && !ed_restoring();
    song.playing = 0;
    check("RESTORE BEGIN while playing: rc 3 STOP FIRST, nothing staged, flash untouched", (int)ok);

    ok = 1;
    a_put(ED_RS_BEGIN);
    for (i = 1; i < ED_LEN_RESTORE_BEGIN - 1u; i++)
        a_put(0);
    ok &= req(ED_RESTORE) == ED_E_BAD && rc_detail == ED_D_LENGTH;
    ok &= rs_begin(2, 0, &o) == ED_E_BAD && rc_detail == ED_D_RANGE;
    ok &= rs_begin(ED_RK_PROJECT, 4, &o) == ED_E_BAD && rc_detail == ED_D_RANGE;
    ok &= rs_begin(ED_RK_PRESETS, 1, &o) == ED_E_BAD && rc_detail == ED_D_RANGE;
    ok &= rs_begin(ED_RK_PRESETS, 0, &o) == ED_E_REFUSED && rc_detail == ED_D_BAD_OBJECT;   /* (an FDRF magic) */
    memcpy(bad, B[0], sizeof bad);
    x = o;
    x.d[0] = bad;
    memcpy(bad, "FDRG", 4);
    ok &= rs_begin(ED_RK_PROJECT, 1, &x) == ED_E_REFUSED && rc_detail == ED_D_NEWER;
    memcpy(bad, "FUN3", 4);
    ok &= rs_begin(ED_RK_PROJECT, 1, &x) == ED_E_REFUSED && rc_detail == ED_D_BAD_OBJECT;
    memcpy(bad, "FDRE", 4);                           /* an older record: alone */
    ok &= rs_begin(ED_RK_PROJECT, 1, &x) == ED_E_REFUSED && rc_detail == ED_D_BAD_OBJECT;
    x = o;
    x.n[0]--;
    ok &= rs_begin(ED_RK_PROJECT, 1, &x) == ED_E_REFUSED && rc_detail == ED_D_BAD_OBJECT;
    x = o;
    x.n[1] = 3000;
    ok &= rs_begin(ED_RK_PROJECT, 1, &x) == ED_E_REFUSED && rc_detail == ED_D_BAD_OBJECT;
    x = o;
    x.n[2] = ED_SZ_VARS_MIN - 1u;
    ok &= rs_begin(ED_RK_PROJECT, 1, &x) == ED_E_REFUSED && rc_detail == ED_D_BAD_OBJECT;
    x.n[2] = ED_SZ_VARS_MAX + 1u;
    ok &= rs_begin(ED_RK_PROJECT, 1, &x) == ED_E_REFUSED && rc_detail == ED_D_BAD_OBJECT;
    lk_blocked = 1;
    ok &= rs_begin(ED_RK_PROJECT, 1, &o) == ED_E_REFUSED && rc_detail == ED_D_BLOCKED;
    lk_blocked = 0;
    var_who[var_play ^ 1u] = 3;                       /* the park: C's unsaved variations */
    var_dirty |= 4u;
    ok &= rs_begin(ED_RK_PROJECT, 1, &o) == ED_E_REFUSED && rc_detail == ED_D_CUE_PARK + 2u;
    var_who[var_play ^ 1u] = 0;
    var_dirty &= (uint8_t)~4u;
    ok &= !ed_restoring() && same_flash(keep);
    check("RESTORE BEGIN refusals: length, kind / slot range, wrong magic or sizes (an older record with parts), a newer "
          "record (UPDATE THE FM-1 FIRST), USR3 guard, the park holding another slot's unsaved variations (SAVE C "
          "FIRST); nothing staged, flash untouched", (int)ok);

    ok = rs_begin(ED_RK_PROJECT, 1, &o) == ED_OK && r_b() == ED_RS_BEGIN && r_b() == ED_RK_PROJECT && r_b() == 1u &&
         ed_restoring() && str_eq(ui.msg, "RESTORING");
    ok &= rs_piece(ED_PART_LOCKS, 256, B[1] + 256, 256) == ED_E_STALE && rc_detail == ED_D_ORDER;
    ok &= rs_piece(3, 0, B[1], 256) == ED_E_BAD && rc_detail == ED_D_RANGE;
    ok &= rs_piece(ED_PART_LOCKS, 0, B[1], 255) == ED_E_BAD && rc_detail == ED_D_LENGTH;
    ok &= rs_piece(ED_PART_RECORD, 0, B[0], 256) == ED_OK && r_b() == ED_RS_DATA && r_b() == 0u && r_u14() == 0u &&
          r_u14() == 256u;
    ok &= rs_piece(ED_PART_RECORD, 0, B[0], 256) == ED_OK;   /* a retry of the last piece */
    ok &= rs_piece(ED_PART_RECORD, 512, B[0] + 512, 256) == ED_E_STALE && rc_detail == ED_D_ORDER;
    ok &= rs_op(ED_RS_COMMIT) == ED_E_REFUSED && rc_detail == ED_D_INCOMPLETE && ed_restoring();
    ok &= restore(ED_RK_PROJECT, 1, &o, 999, 0) == ED_OK;   /* (a BEGIN while one runs: that one dropped first) */
    transport_req = 1;
    ok &= rs_op(ED_RS_COMMIT) == ED_E_STOP && rc_detail == ED_D_STOP && ed_restoring() && same_flash(keep);
    transport_req = 0;
    ok &= rs_op(ED_RS_COMMIT) == ED_OK && r_b() == ED_RS_COMMIT && r_b() == ED_RK_PROJECT && r_b() == 1u &&
          rp[rp_i + 6u] == 0u && !ed_restoring();
    check("RESTORE BEGIN / DATA / COMMIT: replies; DATA out of order (rc 5 34), unknown part, wrong length, a retry "
          "accepted; COMMIT incomplete (rc 2 27, still restoring), playing (rc 3, flash untouched, still restoring), "
          "then stopped: written", (int)ok);

    memcpy(keep, hflash, sizeof keep);
    x = o;
    x.crc[1] ^= 1u;
    ok = restore(ED_RK_PROJECT, 2, &x, 999, 1) == ED_E_REFUSED && rc_detail == ED_D_CRC && !ed_restoring() &&
         same_flash(keep);
    memcpy(bad, B[0], ED_SZ_PROJECT);                 /* a record whose sum is wrong (the file's CRC right) */
    bad[100] ^= 1u;
    x = o;
    x.d[0] = bad;
    x.crc[0] = st_crc32(bad, ED_SZ_PROJECT);
    ok &= restore(ED_RK_PROJECT, 2, &x, 999, 1) == ED_E_REFUSED && rc_detail == ED_D_INVALID && !ed_restoring() &&
          same_flash(keep) && !proj_ok(&proj_slot[2]);
    x = o;                                            /* a record without its lock bank */
    x.n[1] = 0;
    ok &= restore(ED_RK_PROJECT, 2, &x, 999, 1) == ED_E_REFUSED && rc_detail == ED_D_INVALID && same_flash(keep);
    check("RESTORE COMMIT: a CRC mismatch (rc 2 25), a record failing the boot checks or missing its linked bank (rc 2 "
          "26): dropped, flash untouched", (int)ok);
}

static void t_abort_timeout_drop(void)
{
    static uint8_t keep[sizeof hflash];
    static project_t p1;
    static lk_bank_t l1;
    static upr_rec_t u;
    static int16_t kv[P_COUNT];
    robj_t o, pr;
    uint32_t ok;
    fresh();
    make_project(0, 5);
    o = backup_project(0, B);
    make_project(1, 8);                               /* B: the target, loaded */
    kv[P_E0] = 10;
    upr_set(0, 1, kv);
    upr_write();
    preset_user_boot();                               /* (its knobs as the boot clamps them) */
    memcpy(keep, hflash, sizeof keep);
    p1 = proj_slot[1];
    l1 = lk_slot[1];
    u = upr;
    memset(&pr, 0, sizeof pr);
    pr.d[0] = (const uint8_t *)&u;
    pr.n[0] = sizeof u;
    pr.crc[0] = st_crc32(&u, sizeof u) ^ 1u;

    ok = restore(ED_RK_PROJECT, 1, &o, 20, 0) == ED_OK && memcmp(&proj_slot[1], &p1, sizeof p1) &&
         rs_op(ED_RS_ABORT) == ED_OK && r_b() == ED_RS_ABORT && !ed_restoring() &&
         !memcmp(&proj_slot[1], &p1, sizeof p1) && !memcmp(&lk_slot[1], &l1, sizeof l1) && same_flash(keep);
    ok &= restore(ED_RK_PRESETS, 0, &pr, 2, 0) == ED_OK && rs_op(ED_RS_ABORT) == ED_OK &&
          !memcmp(&upr, &u, sizeof u) && same_flash(keep);
    check("RESTORE ABORT: the RAM homes (record, lock bank; the user bank) reloaded from flash, nothing written", (int)ok);

    ok = restore(ED_RK_PROJECT, 1, &o, 5, 0) == ED_OK;
    host_ms += ED_RESTORE_IDLE_MS;
    ui.msg_t = 0;
    ed_backup_poll();
    ok &= ed_restoring() && str_eq(ui.msg, "RESTORING");
    host_ms += 1u;
    ed_backup_poll();
    ok &= !ed_restoring() && !memcmp(&proj_slot[1], &p1, sizeof p1) && !memcmp(&lk_slot[1], &l1, sizeof l1) &&
          same_flash(keep) && rs_op(ED_RS_COMMIT) == ED_E_BAD && rc_detail == ED_D_NOT_RESTORING;
    ok &= restore(ED_RK_PRESETS, 0, &pr, 9, 0) == ED_OK;
    host_ms += ED_RESTORE_IDLE_MS + 1u;
    ed_backup_poll();
    ok &= !ed_restoring() && !memcmp(&upr, &u, sizeof u);
    check("RESTORE idle 3 s: aborted (RESTORING shown until then), homes reloaded from flash, nothing written", (int)ok);

    ok = restore(ED_RK_PROJECT, 1, &o, 999, 0) == ED_OK;
    ed_backup_drop();                                 /* update traffic (editor.c's silence rule) mid-restore */
    ok &= !ed_restoring() && rs_op(ED_RS_COMMIT) == ED_E_BAD && rc_detail == ED_D_NOT_RESTORING && same_flash(keep) &&
          !memcmp(&proj_slot[1], &p1, sizeof p1);
    ok &= restore(ED_RK_PROJECT, 1, &o, 999, 0) == ED_OK;
    usb.ota_req = 1;                                  /* the upgrade command waits at COMMIT */
    ok &= rs_op(ED_RS_COMMIT) == ED_E_BUSY && !ed_restoring() && same_flash(keep) &&
          !memcmp(&proj_slot[1], &p1, sizeof p1);
    usb.ota_req = 0;
    check("update abort mid-restore (ed_backup_drop, or the upgrade command at COMMIT): nothing committed, homes "
          "reloaded", (int)ok);
}

/* RESTORING on the device (ui.c ed_lock_on): its own PLAY, cue keys, LOAD, SAVE, PASTE, NAME and preset save / delete
 * are refused with "RESTORING" and touch nothing (the staged homes, flash, the transport); COMMIT, ABORT, the idle
 * timeout and update traffic release them */
static int keys_refused(const uint8_t *keep, const project_t *staged)
{
    uint32_t ok = 1, ld = (uint32_t)proj_loaded(), vc = var_cur;
    transport_req = 0;
    ui.msg[0] = 0;
    play_press();
    ok &= transport_req == 0u && str_eq(ui.msg, "RESTORING");
    ui.msg[0] = 0;
    cue_keys(1u << STEP_KEY[0] | 1u << CUE_BK[3]);
    ok &= !cue.q_slot && !cue.q_var && proj_loaded() == (int32_t)ld && var_cur == vc && str_eq(ui.msg, "RESTORING");
    project_load(0);
    proj_load_from(0);
    project_save(ld);
    project_save_go(ld);
    project_paste(2);
    project_rename(ld, "X");
    ok &= preset_user_save(0, &trk[0]) < 0;
    preset_user_delete(0);
    ok &= !ui.confirm && proj_loaded() == (int32_t)ld && same_flash(keep) && transport_req == 0u &&
          !memcmp(&proj_slot[1], staged, sizeof *staged) && str_eq(ui.msg, "RESTORING");
    return (int)ok;
}
static int keys_free(void)
{
    transport_req = 0;
    ui.msg[0] = 0;
    play_press();
    return !ed_lock_on && transport_req == 1u && !str_eq(ui.msg, "RESTORING");
}
static void t_restoring_lock(void)
{
    static uint8_t keep[sizeof hflash];
    static project_t staged;
    static int16_t kv[P_COUNT];
    robj_t o;
    uint32_t ok;
    fresh();
    make_project(0, 5);
    o = backup_project(0, B);
    make_project(1, 8);                               /* B: the target, loaded */
    kv[P_E0] = 10;
    upr_set(0, 1, kv);
    upr_write();
    ok = keys_free();
    transport_req = 0;
    memcpy(keep, hflash, sizeof keep);
    ok &= restore(ED_RK_PROJECT, 1, &o, 3, 0) == ED_OK && ed_lock_on;
    staged = proj_slot[1];
    ok &= keys_refused(keep, &staged) && ed_restoring();
    ok &= rs_op(ED_RS_ABORT) == ED_OK && keys_free();
    check("RESTORING: the device's PLAY, cue keys, LOAD, SAVE, PASTE, NAME, preset save / delete refused with its "
          "message, nothing touched; ABORT releases them", (int)ok);

    transport_req = 0;
    ok = restore(ED_RK_PROJECT, 1, &o, 3, 0) == ED_OK;
    staged = proj_slot[1];
    ok &= keys_refused(keep, &staged);
    host_ms += ED_RESTORE_IDLE_MS + 1u;
    ed_backup_poll();
    ok &= keys_free();
    transport_req = 0;
    ok &= restore(ED_RK_PROJECT, 1, &o, 3, 0) == ED_OK && ed_lock_on;
    ed_backup_drop();                                 /* update traffic */
    ok &= keys_free();
    transport_req = 0;
    ok &= restore(ED_RK_PROJECT, 1, &o, 999, 1) == ED_OK && keys_free();
    transport_req = 0;
    check("RESTORING released by the idle timeout, update traffic and COMMIT", (int)ok);
}

static void t_other_slot_plays(void)
{
    static uint8_t C[3][4096];
    static project_t pa;
    static lk_bank_t la;
    static var_bank_t va;
    robj_t a, c;
    uint32_t ok, k, age, f;
    fresh();
    make_project(0, 5);
    a = backup_project(0, B);
    pa = proj_slot[0];
    la = lk_slot[0];
    va = var_buf[var_play];
    make_project(2, 11);
    c = backup_project(2, C);
    project_load(0);                                  /* (C not loaded) */
    ok = restore(ED_RK_PROJECT, 2, &a, 999, 1) == ED_OK && list() == 0 && L[3].present && L[7].present &&
         L[11].present && !L[3].flags;
    {
        static uint8_t b[4096];
        ok &= backup(7, b) == ED_SZ_LOCKS && b[4] == 2u && backup(11, b) == (int)a.n[2] && b[4] == 2u;
    }
    power_cycle();
    project_load(2);
    ok &= str_eq(ui.msg, "LOADED") && var_who[var_play] == 3u && mo.b.count == la.count &&
          !memcmp(mo.b.key, la.key, sizeof la.key) && !memcmp(mo.b.val, la.val, sizeof la.val) &&
          var_buf[var_play].used == va.used && !memcmp(var_buf[var_play].d, va.d, va.used) &&
          trk[0].p[P_E1] == pa.t[0].p[P_E1];
    for (k = 0; k < NTRK; k++)
        ok &= !memcmp(proj_slot[2].t[k].step, pa.t[k].step, sizeof pa.t[k].step);
    age = hit_age(&trk[0]);
    transport_req = 1;
    for (f = 0; f < 400u; f++)
        render_mix(0, 0, CTL);
    ok &= song.playing && hit_age(&trk[0]) != age;
    transport_req = 2;
    render_mix(0, 0, CTL);
    check("a project restored into another slot (A's backup into C): re-sealed for C, loads after a power cycle "
          "(steps, knobs, locks, variations as A's) and plays", (int)ok);

    trk[5].step[3].on ^= 1u;                          /* C loaded, with a live edit */
    ok = restore(ED_RK_PROJECT, 2, &c, 999, 1) == ED_OK && var_who[var_play] == 3u && !slot_dirty(2) &&
         str_eq(ui.msg, "LOADED");
    for (k = 0; k < NTRK; k++)
        ok &= !memcmp(trk[k].step, ((const project_t *)C[0])->t[k].step, sizeof trk[k].step);
    check("a restore into the loaded slot: loaded again as LOAD does (its live edits replaced)", (int)ok);
}

static void t_power_cut(void)
{
    static uint8_t keep[sizeof hflash];
    robj_t o;
    uint32_t n, i, ok = 1, olds = 0, news = 0, old_sum, new_sum;
    fresh();
    make_project(0, 5);
    o = backup_project(0, B);
    make_project(1, 8);                               /* the old project in B */
    old_sum = proj_slot[1].sum;
    memcpy(keep, hflash, sizeof keep);
    host_nflog = 0;
    ok &= restore(ED_RK_PROJECT, 1, &o, 999, 1) == ED_OK;
    n = host_nflog;
    new_sum = proj_slot[1].sum;
    for (i = 0; i <= n && ok; i++) {
        uint32_t rc;
        memcpy(hflash, keep, sizeof keep);
        power_cycle();
        host_fail_at = (int)i;                        /* the power goes at erase / program i of the commit */
        rc = restore(ED_RK_PROJECT, 1, &o, 999, 1);
        host_fail_at = -1;
        ok &= i < n ? rc == ED_E_FLASH && !ed_restoring() : rc == ED_OK;
        power_cycle();
        ok &= proj_ok(&proj_slot[1]) && !lk_lost && !var_lost && proj_var_flash(1) != 0 &&
              lk_slot[1].count == proj_slot[1].lk_n;
        if (proj_slot[1].sum == old_sum)
            olds++;
        else if (proj_slot[1].sum == new_sum)
            news++;
        else
            ok = 0;
        project_load(1);
        ok &= str_eq(ui.msg, "LOADED");
        if (!ok)
            printf("     cut at op %u of %u: rc %u msg %s lost %x %x\n", i, n, rc, ui.msg, lk_lost, var_lost);
    }
    check("a power cut at every erase / program of a project restore: the old or the new project loads whole (record, "
          "lock bank, variations)", ok && n > 20u && olds && news && host_fl_stray == 0);
}

static void t_presets(void)
{
    static upr_rec_t u, b;
    robj_t o;
    preset_t p;
    uint32_t ok;
    st_hdr_t h;
    fresh();
    memset(&u, 0, sizeof u);
    u.magic = UPR_MAGIC;
    u.s[0].used = 1;
    u.s[0].model = 1;
    u.s[0].k[2] = 17;
    u.s[127].used = 1;
    u.s[127].model = NMODELS - 1u;
    memset(&o, 0, sizeof o);
    o.d[0] = (const uint8_t *)&u;
    o.n[0] = sizeof u;
    o.crc[0] = st_crc32(&u, sizeof u);
    ok = restore(ED_RK_PRESETS, 0, &o, 999, 1) == ED_OK && r_b() == ED_RS_COMMIT && r_b() == ED_RK_PRESETS &&
         st_current(OBJ_UPRESET0 + 1u, &h) >= 0 && h.len == sizeof u && !memcmp(st_buf, &u, sizeof u) &&
         preset_get(PRE_NFACT + 0u, &p) && p.model == 1u && preset_get(PRE_NFACT + 127u, &p) &&
         !preset_get(PRE_NFACT + 1u, &p) && backup(0, (uint8_t *)&b) == (int)sizeof b && !memcmp(&b, &u, sizeof u);
    check("RESTORE user presets: the UPR1 record written as sent, the user bank reloaded, its backup = the bytes", (int)ok);
    u.s[4].used = 2;
    o.crc[0] = st_crc32(&u, sizeof u);
    ok = restore(ED_RK_PRESETS, 0, &o, 999, 1) == ED_E_REFUSED && rc_detail == ED_D_INVALID;
    u.s[4].used = 1;
    u.s[4].model = NMODELS;
    o.crc[0] = st_crc32(&u, sizeof u);
    ok &= restore(ED_RK_PRESETS, 0, &o, 999, 1) == ED_E_REFUSED && rc_detail == ED_D_INVALID && !ed_restoring() &&
          upr.s[0].used == 1u && !upr.s[4].used;
    check("RESTORE user presets refused: a slot neither used nor empty, a model past the table (rc 2 26)", (int)ok);
}

static void t_old_record(void)
{
    static project_t ve;
    robj_t o;
    uint32_t ok, k;
    fresh();
    make_project(0, 5);
    ve = proj_slot[0];                                /* as 0.23.x saved it: FDRE, no motion */
    ve.magic = PROJ_MAGIC_VE;
    memset((uint8_t *)&ve + __builtin_offsetof(project_t, lk_n), 0,
           __builtin_offsetof(project_t, song) - __builtin_offsetof(project_t, lk_n));
    ve.sum = proj_sum(&ve);
    memset(&o, 0, sizeof o);
    o.d[0] = (const uint8_t *)&ve;
    o.n[0] = sizeof ve;
    o.crc[0] = st_crc32(&ve, sizeof ve);
    ok = restore(ED_RK_PROJECT, 3, &o, 999, 1) == ED_OK && list() == 0 && L[4].present && (L[4].flags & ED_BF_OLD) &&
         !L[8].present && !L[12].present;
    power_cycle();
    project_load(3);
    ok &= str_eq(ui.msg, "LOADED") && mo.b.count == 0u;
    for (k = 0; k < NTRK; k++)
        ok &= !memcmp(trk[k].step, ve.t[k].step, sizeof ve.t[k].step);
    check("RESTORE an older record (FDRE): written as it is, listed OLD, converted at load as always", (int)ok);
}

int main(void)
{
    t_list_get();
    t_restore_refusals();
    t_abort_timeout_drop();
    t_restoring_lock();
    t_other_slot_plays();
    t_power_cut();
    t_presets();
    t_old_record();
    check("flash: no erase or program outside the map", host_fl_stray == 0);
    printf(fails ? "editor_backup_test: %d FAILED\n" : "editor_backup_test: all passed\n", fails);
    return fails ? 1 : 0;
}
