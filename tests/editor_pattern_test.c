/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web editor's PRESETS / PATTERNS commands (firmware/src/editor_pattern.c, web/EDITOR_PROTOCOL.md §2.3 / §2.4)
 * with the firmware's real project / cue / lock code on a host flash (tests/flash_host.h), built with ASan + UBSan.
 * editor.c's framing API is a capture here: every reply and push is checked as a frame (F0 .. F7, 7-bit, <= 400 B)
 * and parsed back. The hooks are called directly, as editor.c's dispatch calls them. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#include "ui_host.h"
#include "editor_proto.h"

static int fails;
static int fail_line;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        fails++;
        if (fail_line)
            printf("     (first failed at line %d)\n", fail_line);
    }
    fail_line = 0;
    return ok;
}
#define OK(e) do { uint32_t ok_ = (e) ? 1u : 0u; if (!ok_ && !fail_line) fail_line = __LINE__; ok &= ok_; } while (0)

/* ---- editor.c's framing API: a capture */
#define NLOG 64u
static uint8_t cur[ED_REPLY_MAX + 64u], flog[NLOG][ED_REPLY_MAX + 64u];
static uint32_t rn, flen[NLOG], nf, bad_frames, seq_now = 5;
static int restoring;
static void put(uint32_t b)
{
    if (rn < sizeof cur)
        cur[rn] = (uint8_t)b;
    rn++;
}
static void ed_b(uint32_t v)
{
    if (v > 127u)
        bad_frames++;
    put(v & 127u);
}
static void ed_reply(uint32_t cmd, uint32_t rc)
{
    rn = 0;
    put(0xF0);
    put(ED_H0);
    put(ED_H1);
    put(ED_H2);
    ed_b(cmd);
    ed_b(seq_now);
    ed_b(rc);
}
static void ed_push(uint32_t cmd)
{
    rn = 0;
    put(0xF0);
    put(ED_H0);
    put(ED_H1);
    put(ED_H2);
    ed_b(cmd);
    ed_b(ED_SEQ_PUSH);
}
static void ed_u14(uint32_t v)
{
    if (v > 0x3FFFu)
        bad_frames++;
    put(v & 127u);
    put(v >> 7 & 127u);
}
static void ed_v14(int32_t v) { ed_u14((uint32_t)(v + 8192)); }
static void ed_u35(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++, v >>= 7)
        put(v & 127u);
}
static void ed_str(const char *s)
{
    for (; *s; s++)
        put(*s >= 32 && *s <= 126 ? (uint32_t)*s : '?');
    put(0);
}
static void ed_pack(const void *d, uint32_t n) { (void)d; (void)n; }
static int32_t ed_ld(void) { return proj_loaded(); }
static void ed_state(void)
{
    uint32_t s, used = 0, dirty = 0;
    int32_t ld = ed_ld();
    ed_b(transport_busy() ? 1u : 0u);
    ed_b(ld < 0 ? ED_NONE : (uint32_t)ld);
    ed_b(var_cur);
    ed_b(cue.q_slot ? cue.q_slot - 1u : ED_NONE);
    ed_b(cue.q_var ? cue.q_var - 1u : ED_NONE);
    ed_b(song.sel);
    ed_b((restoring ? ED_SF_RESTORING : 0u) | (chain_busy() ? ED_SF_SONG : 0u));
    for (s = 0; s < 4u; s++) {
        used |= (uint32_t)proj_ok(&proj_slot[s]) << s;
        dirty |= (uint32_t)slot_dirty(s) << s;
    }
    ed_b(used);
    ed_b(dirty);
    for (s = 0; s < 4u; s++)
        ed_u14((used >> s) & 1u ? slot_var_mask(s) : 1u);
}
static uint32_t ed_room(void) { return rn + 1u >= ED_REPLY_MAX ? 0u : ED_REPLY_MAX - 1u - rn; }
static void ed_send(void)
{
    uint32_t i, ok;
    put(0xF7);
    ok = rn <= ED_REPLY_MAX && rn >= 7u && cur[0] == 0xF0 && cur[rn - 1u] == 0xF7;
    for (i = 1; ok && i + 1u < rn; i++)
        ok = cur[i] < 0x80u;
    if (!ok)
        bad_frames++;
    memcpy(flog[nf % NLOG], cur, rn < sizeof cur ? rn : sizeof cur);
    flen[nf % NLOG] = rn;
    nf++;
}
static void ed_fail(uint32_t cmd, uint32_t rc, uint32_t detail, const char *msg)
{
    ed_reply(cmd, rc);
    ed_b(detail);
    ed_str(msg);
    ed_send();
}
static uint32_t ed_get_u14(const uint8_t *a) { return (uint32_t)a[0] | (uint32_t)a[1] << 7; }
static int32_t ed_get_v14(const uint8_t *a) { return (int32_t)ed_get_u14(a) - 8192; }
static uint32_t ed_get_u35(const uint8_t *a)
{
    return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
}
static uint32_t ed_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)
{
    (void)in, (void)n, (void)out, (void)max;
    return 0;
}
#define ED_HAS_BACKUP 1                               /* (editor_backup.c's lock, as a flag) */
static int ed_restoring(void) { return restoring; }
#include "../firmware/src/editor_pattern.c"

/* ---- requests */
static const uint8_t *R;                              /* the last frame: [4] cmd [5] seq [6] rc, fields from [7] */
static uint32_t RL;
static int rq(uint32_t cmd, const uint8_t *a, uint32_t n)   /* the reply's rc; -1 not handled */
{
    uint32_t before = nf;
    if (!ed_pattern_cmd(cmd, a, n))
        return nf == before ? -1 : -2;
    if (nf != before + 1u)
        bad_frames++;
    R = flog[(nf - 1u) % NLOG];
    RL = flen[(nf - 1u) % NLOG];
    if (R[4] != cmd || R[5] != seq_now)
        bad_frames++;
    return R[6];
}
#define RQ(cmd, ...) rq(cmd, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))
#define LO(v) ((uint32_t)(v) & 127u)
#define HI(v) ((uint32_t)(v) >> 7 & 127u)
#define V14(v) LO((v) + 8192), HI((v) + 8192)
static int refused(int rc, uint32_t want_rc, uint32_t detail) { return rc == (int)want_rc && R[7] == detail; }
static const char *msg(void) { return (const char *)R + 8; }
static uint32_t u14at(uint32_t o) { return (uint32_t)R[o] | (uint32_t)R[o + 1u] << 7; }
static uint8_t sreq[ED_REQ_MAX];
static int steps_set(uint32_t slot, uint32_t v, uint32_t k, uint32_t first, uint32_t cnt, const uint8_t *st)
{
    sreq[0] = (uint8_t)slot;
    sreq[1] = (uint8_t)v;
    sreq[2] = (uint8_t)k;
    sreq[3] = (uint8_t)first;
    sreq[4] = (uint8_t)cnt;
    memcpy(sreq + 5, st, 3u * cnt);
    return rq(ED_STEPS_SET, sreq, 5u + 3u * cnt);
}

/* ---- the device */
static void wipe_ram(void)
{
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(lk_slot, 0, sizeof lk_slot);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    memset(&cue, 0, sizeof cue);
    memset(&chain, 0, sizeof chain);
    var_play = 0;
    var_cur = 0;
    var_dirty = 0;
    var_lost = 0;
    lk_lost = 0;
    lk_blocked = var_blocked = 0;
    persist_note = 0;
}
static void fresh(void)                               /* nothing in flash, booted, nothing loaded */
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&ui, 0, sizeof ui);
    wipe_ram();
    host_nflog = 0;
    host_fail_at = -1;
    transport_req = 0;
    song.playing = 0;
    persist_boot();
    restoring = 0;
}
static void three_slots(void)                         /* A..C saved (track 0 LEVEL 10 / 20 / 30, track 1 step s on); D
                                                       * empty; A loaded, stopped */
{
    uint32_t s;
    fresh();
    for (s = 0; s < NTRK; s++)
        memset(trk[s].step, 0, sizeof trk[s].step);
    for (s = 0; s < 3u; s++) {
        trk[0].p[P_LEVEL] = (int16_t)(10 + 10 * (int32_t)s);
        memset(trk[1].step, 0, sizeof trk[1].step);
        trk[1].step[s].on = 1;
        project_save(s);
    }
    project_load(0);
    host_nflog = 0;
}
static void run_blk(void)
{
    static int32_t l[CTL], r[CTL];
    render_mix(l, r, CTL);
}
static void play(void)
{
    transport_req = 1;
    run_blk();
}
static void stop(void)
{
    transport_req = 2;
    run_blk();
    cue_poll();
}
static int run_until_var(uint32_t v)                  /* playing: blocks (and the UI's cue_poll) until v plays */
{
    uint32_t f;
    for (f = 0; f < 8u * 44100u / CTL && var_cur != v; f++) {
        run_blk();
        cue_poll();
    }
    return var_cur == v;
}

/* ---- NAMES */
static void names(void)
{
    uint32_t ok = 1, b, first, got, id;
    fresh();
    for (b = 0; b < 3u; b++) {
        uint32_t tot = PRE_BANK_START[b + 1u] - PRE_BANK_START[b], n, i, o;
        id = PRE_BANK_START[b];
        for (first = 0, got = 0; first < tot && got < 200u; first += n) {
            OK(RQ(ED_NAMES, b, first) == ED_OK && R[7] == b && str_eq((const char *)R + 8, N_PBANK[b]));
            o = 8u + str_len(N_PBANK[b]) + 1u;
            OK(R[o] == tot && R[o + 1u] == first && RL <= ED_REPLY_MAX);
            n = R[o + 2u];
            OK(n > 0u);
            if (!n)
                break;
            for (o += 3u, i = 0; i < n; i++, id++) {
                OK(u14at(o) == id && R[o + 2u] == PRE_FACTORY[id].model && str_eq((const char *)R + o + 3u, PRE_FACTORY[id].name));
                o += 3u + str_len(PRE_FACTORY[id].name) + 1u;
            }
            OK(o + 1u == RL);
            got++;
        }
        OK(id == PRE_BANK_START[b + 1u] && got >= (b ? 1u : 2u));   /* (MACHINEDRUM: more than one reply, cut where
                                                                      * it is full) */
    }
    OK(RQ(ED_NAMES, 0, 60) == ED_OK && R[8 + str_len(N_PBANK[0]) + 1 + 2] == 0u);   /* past the end: an empty list */
    OK(refused(RQ(ED_NAMES, 3, 0), ED_E_BAD, ED_D_RANGE));     /* the user bank: backup object 0 */
    OK(refused(RQ(ED_NAMES, 0), ED_E_BAD, ED_D_LENGTH));
    check("NAMES: each factory bank in order, cut to fit a reply, from first; the user bank and bad lengths refused",
          (int)ok);
}

/* ---- PRESET_LOAD */
static void preset_load_cmd(void)
{
    uint32_t ok = 1, id = PRE_BANK_START[PB_MODERN] + 3u, i, acid = PRE_NONE;
    const preset_t *p = &PRE_FACTORY[id];
    three_slots();
    OK(RQ(ED_PRESET_LOAD, 2, LO(id), HI(id)) == ED_OK && R[7] == 2u && u14at(8) == id && R[10] == p->model);
    OK(trk[2].pre == id && !trk[2].pre_ed && trk[2].p[P_MODEL] == p->model);
    for (i = 0; i < 8u; i++)
        OK(trk[2].p[P_E0 + i] == clamp(p->k[i], DMODELS[p->model].edit[i].min, DMODELS[p->model].edit[i].max));
    song.sel = 5;
    OK(RQ(ED_PRESET_LOAD, ED_NONE, 0, 0) == ED_OK && R[7] == 5u && trk[5].pre == 0u);   /* 7F: the selected track */
    upr.s[0].used = 0;
    OK(refused(RQ(ED_PRESET_LOAD, 1, LO(PRE_NFACT), HI(PRE_NFACT)), ED_E_REFUSED, ED_D_PRESET_EMPTY));
    upr_set(1, DM_K909, trk[3].p);
    OK(RQ(ED_PRESET_LOAD, 1, LO(PRE_NFACT + 1u), HI(PRE_NFACT + 1u)) == ED_OK && trk[1].pre == PRE_NFACT + 1u &&
       trk[1].p[P_MODEL] == DM_K909);
    for (i = 0; i < PRE_NFACT && acid == PRE_NONE; i++)
        if (PRE_FACTORY[i].model == DM_ACID)
            acid = i;
    OK(acid != PRE_NONE);
    OK(RQ(ED_PRESET_LOAD, 6, LO(acid), HI(acid)) == ED_OK && RQ(ED_PRESET_LOAD, 7, LO(acid), HI(acid)) == ED_OK);
    OK(refused(RQ(ED_PRESET_LOAD, 0, LO(acid), HI(acid)), ED_E_REFUSED, ED_D_ACID_CAP) && trk[0].p[P_MODEL] != DM_ACID);
    OK(refused(RQ(ED_PRESET_LOAD, 0, LO(PRE_NID), HI(PRE_NID)), ED_E_BAD, ED_D_RANGE));
    OK(refused(RQ(ED_PRESET_LOAD, 8, 0, 0), ED_E_BAD, ED_D_RANGE));
    check("PRESET_LOAD: a factory / user preset onto a track as the PRESETS list loads it (7F: the selected one); an "
          "empty user slot, a third ACID, bad ids refused", (int)ok);
}

/* ---- STEPS_GET */
static int steps_are(const step_t *st, const uint8_t *w)   /* 64 wire steps = st, as the reply encodes them; then their
                                                           * NUDGE (micro + 64) */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++, w += 3) {
        uint32_t f = (uint32_t)st[i].on | (uint32_t)st[i].acc << 1 | (uint32_t)st[i].slide << 2 | (uint32_t)st[i].tie << 3 |
                     (uint32_t)st[i].rat << 4;
        if (w[0] != f || w[1] != st[i].cond || w[2] != (uint32_t)(st[i].nt + 60) ||
            w[3u * (NSTEP - i) + i] != (uint32_t)(st[i].micro + 64))
            return 0;
    }
    return 1;
}
static void steps_get(void)
{
    static step_t vs[NTRK][NSTEP];
    step_t *vp[NTRK];
    int16_t tm[NTRK][4];
    uint32_t ok = 1, k, i, j;
    three_slots();
    trk[1].step[5].on = 1;
    trk[1].step[5].acc = 1;
    trk[1].step[5].rat = 2;
    trk[1].step[5].cond = 7;
    trk[1].step[5].nt = -3;
    trk[1].step[5].micro = -41;
    trk[1].p[P_SLEN] = 12;
    trk[1].p[P_SDIV] = 3;
    OK(RQ(ED_STEPS_GET, ED_NONE, 0, 1) == ED_OK && R[7] == 0u && R[8] == 0u && R[9] == 1u && R[10] == 1u && R[11] == 1u);
    OK(R[12] == 12u && R[13] == 3u && R[14] == (uint32_t)trk[1].p[P_SSWING] && R[15] == (uint32_t)trk[1].p[P_SRC]);
    OK(R[16 + 15] == 0x23u && R[17 + 15] == 7u && R[18 + 15] == 57u && steps_are(trk[1].step, R + 16) && R[16 + 192 + 5] == 23u &&
       RL == 16u + 192u + 64u + 1u);
    OK(RQ(ED_STEPS_GET, 1, 0, 1) == ED_OK && R[7] == 1u && R[10] == 1u && R[11] == 0u &&   /* B: its record (flash's) */
       steps_are(proj_slot[1].t[1].step, R + 16) && R[16 + 3] == 1u && R[16] == 0u);
    OK(RQ(ED_STEPS_GET, 0, 3, 0) == ED_OK && R[10] == 0u && R[11] == 0u && !R[12] && !R[13] && !R[14] && !R[15]);
    for (i = 0; i < NSTEP; i++)                        /* (a variation never used: no step, timing 0) */
        OK(R[16 + 3 * i] == 0u && R[17 + 3 * i] == 0u && R[18 + 3 * i] == 60u);
    OK(refused(RQ(ED_STEPS_GET, 3, 0, 0), ED_E_REFUSED, ED_D_EMPTY_SLOT) && str_eq(msg(), "SLOT D EMPTY"));
    OK(refused(RQ(ED_STEPS_GET, 0, 8, 0), ED_E_BAD, ED_D_RANGE) && refused(RQ(ED_STEPS_GET, 4, 0, 0), ED_E_BAD, ED_D_RANGE));
    OK(refused(RQ(ED_STEPS_GET, 0, 0), ED_E_BAD, ED_D_LENGTH));
    /* variation 3, stored in the bank: every track read back as var_get decodes it */
    OK(cue_var(2) == 0 && var_cur == 2u);
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < NSTEP; i++)
            if ((k * 7u + i * 13u) % 5u == 0u) {
                trk[k].step[i].on = 1;
                trk[k].step[i].acc = (i >> 1) & 1u;
                trk[k].step[i].rat = i & 3u;
                trk[k].step[i].slide = (i >> 2) & 1u;
                trk[k].step[i].cond = (uint8_t)(i % 59u);
                trk[k].step[i].nt = (int8_t)((int32_t)i - 30);
                trk[k].step[i].micro = (int8_t)(i % 3u ? (int32_t)((i * 7u + k) % 101u) - 50 : 0);   /* (varpack.c: bit 6
                                                                                                * and a byte, or none) */
            } else {
                memset(&trk[k].step[i], 0, sizeof(step_t));
            }
        trk[k].p[P_SLEN] = (int16_t)(1u + k * 7u);
    }
    trk[4].p[P_SSWING] = 66;
    OK(cue_var(0) == 0 && var_cur == 0u);              /* (variation 3 packed into A's bank) */
    for (k = 0; k < NTRK; k++)
        vp[k] = vs[k];
    OK(var_get(edp_bank(0), 2, vp, tm) == 1);
    for (k = 0; k < NTRK; k++) {
        OK(RQ(ED_STEPS_GET, 0, 2, k) == ED_OK && R[10] == 1u && R[11] == 0u && steps_are(vs[k], R + 16));
        for (j = 0; j < 4u; j++)
            OK(R[12 + j] == (uint32_t)tm[k][j]);
    }
    OK(tm[4][2] == 66 && tm[3][0] == 22);
    project_save(0);                                   /* A to flash, then B loaded: A's variations read from there */
    OK(cue_slot(1) == 0 && proj_loaded() == 1);
    proj_var_load(0, &var_buf[var_play ^ 1u]);
    var_who[var_play ^ 1u] = 0;                       /* (the park dropped: flash's copy is the one read) */
    OK(edp_bank(0) && edp_bank(0) != &var_buf[0] && edp_bank(0) != &var_buf[1]);
    for (k = 0; k < NTRK; k++)
        OK(RQ(ED_STEPS_GET, 0, 2, k) == ED_OK && R[10] == 1u && R[11] == 0u && steps_are(vs[k], R + 16) && R[12] == tm[k][0]);
    /* the power-on kit: no slot */
    fresh();
    trk[0].step[2].on = 1;
    OK(RQ(ED_STEPS_GET, ED_NONE, 0, 0) == ED_OK && R[7] == ED_NONE && R[11] == 1u && R[16 + 6] == 1u);
    OK(refused(RQ(ED_STEPS_GET, ED_NONE, 1, 0), ED_E_REFUSED, ED_D_NO_SLOT));
    check("STEPS_GET: the live steps (unsaved edits), another slot's record, a variation never used (timing 0), a "
          "variation from the RAM bank and from flash (= var_get), the power-on kit; empty slot / range refused", (int)ok);
}

/* ---- STEPS_SET */
static void steps_set_live(void)
{
    static const uint8_t two[] = {0x7F, 99, 127, 0x01, 5, 48};
    uint32_t ok = 1;
    three_slots();
    OK(!slot_dirty(0));
    OK(steps_set(ED_NONE, 0, 2, 10, 2, two) == ED_OK && R[7] == ED_NONE && R[8] == 0u && R[9] == 2u && R[10] == 10u &&
       R[11] == 2u);
    OK(trk[2].step[10].on && trk[2].step[10].acc && trk[2].step[10].slide && trk[2].step[10].tie && trk[2].step[10].rat == 3u &&
       trk[2].step[10].cond == COND_MAX && trk[2].step[10].nt == NT_MAX);
    OK(trk[2].step[11].on && !trk[2].step[11].acc && trk[2].step[11].cond == 5u && trk[2].step[11].nt == -12);
    OK(slot_dirty(0) && host_nflog == 0u);             /* (live: the slot's unsaved mark until SAVE) */
    OK(trk[2].step[10].micro == 0 && trk[2].step[11].micro == 0);
    {                                                 /* NUDGE (minor 1): count x micro + 64 after the steps, clamped */
        static const uint8_t nud[] = {ED_NONE, 0, 2, 10, 3, 0x01, 0, 60, 0x01, 0, 60, 0x01, 0, 60, 127, 0, 44};
        static const uint8_t keep[] = {ED_NONE, 0, 2, 10, 3, 0x03, 0, 60, 0x00, 0, 60, 0x01, 0, 60};
        OK(rq(ED_STEPS_SET, nud, sizeof nud) == ED_OK && R[11] == 3u && trk[2].step[10].micro == MICRO_MAX &&
           trk[2].step[11].micro == -MICRO_MAX && trk[2].step[12].micro == -20);
        OK(rq(ED_STEPS_SET, keep, sizeof keep) == ED_OK && trk[2].step[10].micro == MICRO_MAX && trk[2].step[10].acc &&
           trk[2].step[11].micro == 0 && !trk[2].step[11].on && trk[2].step[12].micro == -20);   /* (none sent: kept; a step
                                                                                         * written empty: 0) */
        OK(refused(rq(ED_STEPS_SET, nud, sizeof nud - 1u), ED_E_BAD, ED_D_LENGTH) && trk[2].step[12].micro == -20);
        OK(RQ(ED_STEPS_GET, ED_NONE, 0, 2) == ED_OK && R[16 + 192 + 10] == 114u && R[16 + 192 + 11] == 64u &&
           R[16 + 192 + 12] == 44u && steps_are(trk[2].step, R + 16));
    }
    OK(steps_set(0, 0, 2, 0, 1, two + 3) == ED_OK && trk[2].step[0].on);   /* the loaded slot named */
    OK(refused(steps_set(1, 0, 2, 0, 1, two), ED_E_STALE, ED_D_CUE_FIRST) && !trk[2].step[0].acc);   /* another slot */
    OK(refused(rq(ED_STEPS_SET, (const uint8_t[]){ED_NONE, 0, 2, 0, 2, 1, 0, 60}, 8), ED_E_BAD, ED_D_LENGTH));
    OK(refused(steps_set(ED_NONE, 0, 2, 63, 2, two), ED_E_BAD, ED_D_RANGE));
    OK(refused(steps_set(ED_NONE, 0, 2, 0, 0, two), ED_E_BAD, ED_D_RANGE));
    OK(refused(steps_set(ED_NONE, 0, 8, 0, 1, two), ED_E_BAD, ED_D_RANGE));
    chain.running = 1;
    OK(refused(steps_set(ED_NONE, 0, 2, 20, 1, two), ED_E_REFUSED, ED_D_CUE_SONG) && !trk[2].step[20].on);
    chain.running = 0;
    restoring = 1;
    OK(refused(steps_set(ED_NONE, 0, 2, 20, 1, two), ED_E_BUSY, ED_D_RESTORING) && !trk[2].step[20].on);
    restoring = 0;
    fresh();                                          /* the power-on kit: its live steps */
    OK(steps_set(ED_NONE, 0, 1, 4, 1, two + 3) == ED_OK && trk[1].step[4].on);
    OK(refused(steps_set(ED_NONE, 1, 1, 4, 1, two + 3), ED_E_REFUSED, ED_D_NO_SLOT));
    check("STEPS_SET: the playing variation's live steps, values clamped, the slot marked unsaved, no flash; NUDGE sent "
          "(clamped) or kept (a step written empty: 0); another slot (CUE FIRST), bad lengths / ranges, a song, RESTORING "
          "refused", (int)ok);
}
static void steps_set_switch_stopped(void)
{
    static const uint8_t on[] = {0x01, 0, 60};
    uint32_t ok = 1;
    three_slots();
    OK(var_cur == 0u && !transport_busy());
    OK(steps_set(ED_NONE, 1, 0, 6, 1, on) == ED_OK && var_cur == 1u && !cue.q_var && trk[0].step[6].on);
    OK(proj_loaded() == 0 && !proj_slot[0].t[0].step[6].on);   /* (variation 1, the record, unchanged) */
    OK(steps_set(ED_NONE, 0, 0, 7, 1, on) == ED_OK && var_cur == 0u && trk[0].step[7].on && !trk[0].step[6].on);
    OK(RQ(ED_STEPS_GET, 0, 1, 0) == ED_OK && R[10] == 1u && R[16 + 18] == 1u);   /* variation 2 kept its edit */
    check("STEPS_SET on a variation not playing, stopped: the FM-1 switches to it at once (its key), then the edit "
          "applies (rc 0)", (int)ok);
}
static void steps_set_switch_playing(void)
{
    static const uint8_t on[] = {0x01, 0, 60};
    uint32_t ok = 1, f;
    three_slots();
    play();
    for (f = 0; f < 20u; f++)
        run_blk();
    OK(refused(steps_set(ED_NONE, 2, 0, 3, 1, on), ED_E_STALE, ED_D_CUE_FIRST) && str_eq(msg(), "CUE FIRST"));
    OK(cue.q_var == 3u && !cue.now && var_cur == 0u && !trk[0].step[3].on);   /* queued, as the key queues it */
    OK(refused(steps_set(ED_NONE, 2, 0, 3, 1, on), ED_E_STALE, ED_D_CUE_FIRST) && cue.q_var == 3u && !cue.now);   /* (not
                                                                                       * pressed again: no "at once") */
    OK(run_until_var(2) && !trk[0].step[3].on);
    OK(steps_set(ED_NONE, 2, 0, 3, 1, on) == ED_OK && trk[0].step[3].on);
    stop();
    check("STEPS_SET on a variation not playing, playing: the switch queued like the cue key (CUE FIRST, nothing "
          "written; again: not pressed twice), the edit applies once it plays", (int)ok);
}

/* ---- CUE = the keys */
typedef struct {
    int32_t ld;
    uint32_t var, qs, qv, now, play, who0, who1, vdirty, pdirty, rc;
    uint8_t last[4];
    char m[24];
    uint32_t steps, level;
} cue_snap_t;
static cue_snap_t snap(int rc)
{
    cue_snap_t s;
    uint32_t k;
    memset(&s, 0, sizeof s);
    s.ld = proj_loaded();
    s.var = var_cur;
    s.qs = cue.q_slot;
    s.qv = cue.q_var;
    s.now = cue.now;
    s.play = var_play;
    s.who0 = var_who[0];
    s.who1 = var_who[1];
    s.vdirty = var_dirty;
    s.pdirty = proj_dirty;
    s.rc = (uint32_t)rc;
    memcpy(s.last, cue.last, 4);
    str_cpy(s.m, ui.msg, sizeof s.m);
    for (k = 0; k < NTRK; k++)
        s.steps = edp_fnv(s.steps ? s.steps : 2166136261u, trk[k].step, sizeof trk[k].step);
    s.level = (uint32_t)trk[0].p[P_LEVEL];
    return s;
}
enum { SC_VAR_STOP, SC_SLOT_STOP, SC_VAR_PLAY, SC_SLOT_CANCEL, SC_SONG, SC_EMPTY, SC_NOSLOT, SC_N };
static int cue_go(int cmd, uint32_t kind, uint32_t n)   /* the editor's CUE (its rc: 0, else the detail), or the key */
{
    if (!cmd)
        return kind ? cue_var(n) : cue_slot(n);
    if (RQ(ED_CUE, kind, n) == ED_OK) {
        if (R[7] != kind || R[8] != n || RL != 9u + ED_STATE_N + 1u || R[11] != var_cur ||
            R[12] != (cue.q_slot ? cue.q_slot - 1u : ED_NONE) || R[13] != (cue.q_var ? cue.q_var - 1u : ED_NONE))
            bad_frames++;
        return 0;
    }
    return R[6] == ED_E_REFUSED ? (int)R[7] : -100;
}
static cue_snap_t cue_case(uint32_t sc, int cmd)
{
    int rc = 0;
    if (sc == SC_NOSLOT)
        fresh();
    else
        three_slots();
    ui.msg[0] = 0;
    switch (sc) {
    case SC_VAR_STOP:
        rc = cue_go(cmd, 1, 2);
        break;
    case SC_SLOT_STOP:
        rc = cue_go(cmd, 0, 1);
        break;
    case SC_VAR_PLAY:                                 /* queued, again: at the next step */
        play();
        run_blk();
        rc = cue_go(cmd, 1, 1);
        rc |= cue_go(cmd, 1, 1) << 8;
        break;
    case SC_SLOT_CANCEL:                              /* B queued, A's key: cancelled */
        play();
        rc = cue_go(cmd, 0, 1);
        rc |= cue_go(cmd, 0, 0) << 8;
        break;
    case SC_SONG:
        chain.running = 1;
        rc = cue_go(cmd, 0, 1);
        rc |= cue_go(cmd, 1, 1) << 8;
        break;
    case SC_EMPTY:
        rc = cue_go(cmd, 0, 3);
        break;
    default:
        rc = cue_go(cmd, 1, 1);
        break;
    }
    {
        cue_snap_t s = snap(rc);
        chain.running = 0;
        if (transport_busy())
            stop();
        return s;
    }
}
static void cue_equals_keys(void)
{
    static const uint32_t WANT[SC_N] = {0, 0, 0, 0, CUE_E_SONG | CUE_E_SONG << 8, CUE_E_EMPTY, CUE_E_NOSLOT};
    uint32_t ok = 1, sc;
    for (sc = 0; sc < SC_N; sc++) {
        cue_snap_t a = cue_case(sc, 1), b = cue_case(sc, 0);
        OK(!memcmp(&a, &b, sizeof a) && a.rc == WANT[sc]);
        if (memcmp(&a, &b, sizeof a) || a.rc != WANT[sc])
            printf("     case %u: cmd rc %x var %u qv %u now %u ld %d '%s' / keys rc %x var %u qv %u now %u ld %d '%s'\n", sc,
                   a.rc, a.var, a.qv, a.now, a.ld, a.m, b.rc, b.var, b.qv, b.now, b.ld, b.m);
    }
    {                                                 /* the states themselves */
        cue_snap_t s = cue_case(SC_VAR_STOP, 1);
        OK(s.var == 2u && !s.qv);
        s = cue_case(SC_SLOT_STOP, 1);
        OK(s.ld == 1 && !s.qs && s.level == 20u);
        s = cue_case(SC_VAR_PLAY, 1);
        OK(s.var == 0u && s.qv == 2u && s.now);
        s = cue_case(SC_SLOT_CANCEL, 1);
        OK(!s.qs && s.ld == 0);
        s = cue_case(SC_EMPTY, 1);
        OK(str_eq(s.m, "SLOT D EMPTY"));
    }
    three_slots();
    OK(refused(RQ(ED_CUE, 2, 0), ED_E_BAD, ED_D_RANGE) && refused(RQ(ED_CUE, 0, 4), ED_E_BAD, ED_D_RANGE) &&
       refused(RQ(ED_CUE, 1, 8), ED_E_BAD, ED_D_RANGE) && refused(RQ(ED_CUE, 1), ED_E_BAD, ED_D_LENGTH));
    OK(refused(RQ(ED_CUE, 0, 3), ED_E_REFUSED, ED_D_CUE_EMPTY) && str_eq(msg(), "SLOT D EMPTY"));
    restoring = 1;
    OK(refused(RQ(ED_CUE, 1, 1), ED_E_BUSY, ED_D_RESTORING) && !cue.q_var && var_cur == 0u);
    restoring = 0;
    check("CUE: exactly the cue keys (stopped at once; playing queued, again at the next step, the playing key "
          "cancels; SONG / empty slot / no slot refused with the device's code and message); the reply's state shows it",
          (int)ok);
}

/* ---- locks */
static void locks(void)
{
    uint32_t ok = 1, i, n, first, prev, tot;
    int16_t v;
    three_slots();
    OK(RQ(ED_LOCK_EDIT, ED_LK_SET, 0, 3, P_LEVEL, V14(200)) == ED_OK && R[7] == ED_LK_SET && R[8] == 0u && R[9] == 3u &&
       R[10] == P_LEVEL && u14at(11) == 127u + 8192u && u14at(13) == 1u);
    OK(lock_get(0, 3, P_LEVEL, &v) && v == 127);
    OK(RQ(ED_LOCK_EDIT, ED_LK_SET, 0, 3, P_PAN, V14(-100)) == ED_OK && u14at(11) == 8192u - 64u && lock_get(0, 3, P_PAN, &v) &&
       v == -64 && u14at(13) == 2u);
    OK(refused(RQ(ED_LOCK_EDIT, ED_LK_SET, 0, 3, P_MODEL, V14(1)), ED_E_REFUSED, ED_D_NOT_LOCKABLE));
    fm1_irq_off();
    motion_add(0, 3, P_LEVEL, 50);                    /* a MOTION event on the same place */
    fm1_irq_on();
    OK(RQ(ED_LOCKS_GET, ED_NONE, 0, 0, 0) == ED_OK && R[7] == 0u && R[8] == 0u && u14at(9) == 3u && u14at(11) == 0u &&
       R[13] == 3u);
    OK(R[14] == 3u && R[15] == P_LEVEL && R[16] == ED_LK_KIND_MOTION && u14at(17) == 50u + 8192u);   /* MOTION first */
    OK(R[19] == 3u && R[20] == P_LEVEL && R[21] == ED_LK_KIND_LOCK && u14at(22) == 127u + 8192u);
    OK(R[24] == 3u && R[25] == P_PAN && R[26] == ED_LK_KIND_LOCK && u14at(27) == 8192u - 64u && RL == 30u);
    OK(RQ(ED_LOCK_EDIT, ED_LK_CLEAR, 0, 3, P_LEVEL, 0, 0) == ED_OK && u14at(13) == 1u && !lock_get(0, 3, P_LEVEL, &v) &&
       lock_get(0, 3, P_PAN, &v) && lk_count(&mo.b, 0, LK_MOTION) == 1u);
    OK(RQ(ED_LOCK_EDIT, ED_LK_SET, 0, 5, P_LEVEL, V14(9)) == ED_OK && RQ(ED_LOCK_EDIT, ED_LK_SET, 0, 5, P_E0, V14(9)) == ED_OK);
    OK(RQ(ED_LOCK_EDIT, ED_LK_CLEAR_STEP, 0, 5, 0, 0, 0) == ED_OK && u14at(13) == 1u && !lock_get(0, 5, P_E0, &v));
    OK(RQ(ED_LOCK_EDIT, ED_LK_CLEAR_TRACK, 0, 0, 0, 0, 0) == ED_OK && u14at(13) == 0u && lock_count(0) == 0u &&
       lk_count(&mo.b, 0, LK_MOTION) == 1u);          /* (MOTION never touched) */
    OK(slot_dirty(0) && host_nflog == 0u);
    OK(refused(RQ(ED_LOCK_EDIT, 4, 0, 0, 0, 0, 0), ED_E_BAD, ED_D_RANGE) &&
       refused(RQ(ED_LOCK_EDIT, 0, 0, 64, P_LEVEL, 0, 0), ED_E_BAD, ED_D_RANGE) &&
       refused(RQ(ED_LOCK_EDIT, 0, 0, 0, P_COUNT, 0, 0), ED_E_BAD, ED_D_RANGE) &&
       refused(RQ(ED_LOCK_EDIT, 0, 0, 0, 0, 0), ED_E_BAD, ED_D_LENGTH));
    restoring = 1;
    OK(refused(RQ(ED_LOCK_EDIT, ED_LK_SET, 1, 0, P_LEVEL, V14(5)), ED_E_BUSY, ED_D_RESTORING) && lock_count(1) == 0u);
    restoring = 0;
    /* a full bank: track 7 holds the rest as locks; a new one refused, an existing one replaced; read in pieces */
    for (i = 0; mo.b.count < LK_MAX; i++)
        lk_put(&mo.b, lk_key(7, i / 16u, P_E0 + i % 16u), (int32_t)(i % 100u), 1);
    OK(refused(RQ(ED_LOCK_EDIT, ED_LK_SET, 1, 0, P_LEVEL, V14(5)), ED_E_REFUSED, ED_D_LOCKS_FULL) && !mo.lfull);
    OK(RQ(ED_LOCK_EDIT, ED_LK_SET, 7, 0, P_E0 + 1u, V14(3)) == ED_OK && lock_get(7, 0, P_E0 + 1u, &v) && v == 3);
    tot = lk_count(&mo.b, 7, LK_BOTH);
    for (first = 0, n = 0, prev = 0; first < tot && n < 1000u; n++) {
        uint32_t c, j;
        OK(RQ(ED_LOCKS_GET, ED_NONE, 7, LO(first), HI(first)) == ED_OK && u14at(9) == tot && u14at(11) == first);
        c = R[13];
        OK(c > 0u && RL == 15u + 5u * c);
        if (!c)
            break;
        for (j = 0; j < c; j++) {
            uint32_t key = (uint32_t)R[14 + 5 * j] << 7 | R[15 + 5 * j];
            OK(!first && !j ? 1 : key >= prev);
            prev = key;
        }
        first += c;
    }
    OK(first == tot && n >= 10u);
    /* another slot's bank (RAM), an empty slot */
    lk_put(&lk_slot[1], lk_key(2, 9, P_LEVEL), 33, 1);
    lk_seal(&lk_slot[1], 1);
    OK(RQ(ED_LOCKS_GET, 1, 2, 0, 0) == ED_OK && R[7] == 1u && u14at(9) == 1u && R[14] == 9u && R[16] == ED_LK_KIND_LOCK &&
       u14at(17) == 33u + 8192u);
    OK(refused(RQ(ED_LOCKS_GET, 3, 0, 0, 0), ED_E_REFUSED, ED_D_EMPTY_SLOT));
    check("LOCK_EDIT / LOCKS_GET: SET clamped to the knob (and -64..127), CLEAR one / a step / a track, MOTION "
          "untouched, NOT LOCKABLE, LOCKS FULL (an existing lock still replaced), the list in the bank's order in "
          "pieces, another slot's bank, RESTORING", (int)ok);
}

/* ---- SAVE */
static int image_is_flash(uint32_t s)
{
    static project_t f;
    return st_load(OBJ_PROJECT0 + s, &f, sizeof f) == (int)sizeof f && !memcmp(&f, &proj_slot[s], sizeof f);
}
static void save_cmd(void)
{
    uint32_t ok = 1;
    three_slots();
    trk[4].step[8].on = 1;
    OK(slot_dirty(0));
    OK(RQ(ED_SAVE, ED_NONE) == ED_OK && R[7] == 0u && RL == 8u + ED_STATE_N + 1u && !slot_dirty(0) && image_is_flash(0) &&
       proj_slot[0].t[4].step[8].on && R[8 + 8] == 0u);   /* (the state: dirty 0) */
    OK(RQ(ED_SAVE, 0) == ED_OK);
    OK(refused(RQ(ED_SAVE, 1), ED_E_BAD, ED_D_RANGE));
    trk[4].step[9].on = 1;
    play();
    OK(refused(RQ(ED_SAVE, ED_NONE), ED_E_STOP, ED_D_STOP) && slot_dirty(0));
    stop();
    host_fail_at = 0;
    OK(refused(RQ(ED_SAVE, ED_NONE), ED_E_FLASH, ED_D_NONE) && str_eq(msg(), "SAVE ERROR"));
    host_fail_at = -1;
    lk_blocked = 1;
    OK(refused(RQ(ED_SAVE, ED_NONE), ED_E_REFUSED, ED_D_BLOCKED) && str_eq(msg(), "USR3 CLAMP FAILED"));
    lk_blocked = 0;
    flash_ok = 0;
    OK(refused(RQ(ED_SAVE, ED_NONE), ED_E_REFUSED, ED_D_NO_FLASH));
    flash_ok = 1;
    restoring = 1;
    OK(refused(RQ(ED_SAVE, ED_NONE), ED_E_BUSY, ED_D_RESTORING));
    restoring = 0;
    OK(RQ(ED_SAVE, ED_NONE) == ED_OK && !slot_dirty(0) && image_is_flash(0));
    fresh();
    OK(refused(RQ(ED_SAVE, ED_NONE), ED_E_REFUSED, ED_D_NO_SLOT));
    check("SAVE: the device's SAVE of the loaded slot (flash = RAM after, unsaved mark gone); playing (STOP TO SAVE), "
          "another slot, a flash error, the USR3 guard, no flash, RESTORING, nothing loaded refused", (int)ok);
}

/* ---- pushes */
static int pushed(uint32_t i, uint32_t cmd, uint32_t a, uint32_t b, uint32_t c)
{
    const uint8_t *f = flog[i % NLOG];
    if (f[4] != cmd || f[5] != ED_SEQ_PUSH || f[6] != a || f[7] != b)
        return 0;
    return cmd == ED_P_STEPS_CHANGED ? f[8] == c && flen[i % NLOG] == 10u : flen[i % NLOG] == 9u;
}
static void pushes(void)
{
    static const uint8_t on[] = {0x01, 0, 60};
    uint32_t ok = 1, n0;
    three_slots();
    ed_pattern_sync();
    n0 = nf;
    OK(ed_pattern_push(ED_PUSH_MAX) == 0u && nf == n0);
    trk[3].step[2].on = 1;                            /* a step key on the device */
    OK(ed_pattern_push(ED_PUSH_MAX) == 1u && pushed(n0, ED_P_STEPS_CHANGED, 0, 0, 3) && nf == n0 + 1u);
    trk[3].step[3].on = 1;
    trk[5].step[3].on = 1;
    OK(ed_pattern_push(ED_PUSH_MAX) == 1u && pushed(n0 + 1u, ED_P_STEPS_CHANGED, 0, 0, ED_NONE));   /* several */
    lock_set(5, 1, P_LEVEL, 10);                      /* a lock set on the device */
    OK(ed_pattern_push(ED_PUSH_MAX) == 1u && pushed(n0 + 2u, ED_P_LOCKS_CHANGED, 0, 5, 0));
    trk[6].step[0].on = 1;
    lock_clear_track(5);
    OK(ed_pattern_push(0) == 0u && nf == n0 + 3u);    /* no room: kept for the next pass */
    OK(ed_pattern_push(1) == 1u && pushed(n0 + 3u, ED_P_STEPS_CHANGED, 0, 0, 6));
    OK(ed_pattern_push(1) == 1u && pushed(n0 + 4u, ED_P_LOCKS_CHANGED, 0, 5, 0));
    OK(ed_pattern_push(ED_PUSH_MAX) == 0u);
    OK(cue_var(1) == 0);                              /* a switch (stopped) on the device */
    trk[0].step[0].on = 1;
    n0 = nf;
    OK(ed_pattern_push(ED_PUSH_MAX) == 1u && pushed(n0, ED_P_STEPS_CHANGED, 0, 1, 0));   /* (variation 2 now) */
    OK(steps_set(ED_NONE, 1, 2, 0, 1, on) == ED_OK && RQ(ED_LOCK_EDIT, ED_LK_SET, 2, 0, P_LEVEL, V14(3)) == ED_OK);
    n0 = nf;
    OK(ed_pattern_push(ED_PUSH_MAX) == 0u && nf == n0);   /* the editor's own writes: never pushed */
    fresh();                                          /* the power-on kit: slot 7F */
    ed_pattern_sync();
    trk[1].step[1].on = 1;
    n0 = nf;
    OK(ed_pattern_push(ED_PUSH_MAX) == 1u && pushed(n0, ED_P_STEPS_CHANGED, ED_NONE, 0, 1));
    check("pushes: STEPS_CHANGED / LOCKS_CHANGED for device-side edits (one track or 7F: several), kept while there "
          "is no room, never for the editor's own writes", (int)ok);
}

static void dispatch(void)
{
    uint32_t ok = 1, c;
    static const uint8_t z[8] = {0};
    three_slots();
    OK(rq(ED_INFO, z, 0) == -1 && rq(ED_BACKUP_LIST, z, 0) == -1 && rq(ED_SET, z, 6) == -1 && rq(ED_TEXT, z, 6) == -1);
    for (c = ED_NAMES; c <= ED_SAVE; c++)             /* one byte short (STEPS_SET: under its minimum) */
        OK(refused(rq(c, z, c == ED_STEPS_SET ? 4u : (uint32_t)(c == ED_NAMES ? 1 : c == ED_PRESET_LOAD ? 2 : c == ED_STEPS_GET ? 2 : c == ED_LOCKS_GET ? 3 : c == ED_LOCK_EDIT ? 5 : c == ED_CUE ? 1 : 0)),
                   ED_E_BAD, ED_D_LENGTH));
    OK(refused(rq(ED_SAVE, z, 2), ED_E_BAD, ED_D_LENGTH));
    restoring = 1;                                    /* reads go on while a restore runs */
    OK(RQ(ED_STEPS_GET, ED_NONE, 0, 0) == ED_OK && RQ(ED_LOCKS_GET, ED_NONE, 0, 0, 0) == ED_OK && RQ(ED_NAMES, 1, 0) == ED_OK);
    OK(refused(RQ(ED_PRESET_LOAD, 0, 0, 0), ED_E_BUSY, ED_D_RESTORING));
    restoring = 0;
    check("dispatch: other commands left to editor.c; every pattern command's length checked; reads answered while "
          "RESTORING, writes refused", (int)ok);
}

int main(void)
{
    names();
    preset_load_cmd();
    steps_get();
    steps_set_live();
    steps_set_switch_stopped();
    steps_set_switch_playing();
    cue_equals_keys();
    locks();
    save_cmd();
    pushes();
    dispatch();
    check("every reply and push a whole frame: F0 .. F7, 7-bit data, <= 400 bytes, one per request, its cmd and seq",
          bad_frames == 0u);
    printf(fails ? "editor_pattern_test: %d FAILED\n" : "editor_pattern_test: all passed\n", fails);
    return fails ? 1 : 0;
}
