/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * Reply shape and the push shadows after upstream Felucca's editor (hugelton/Felucca 5d9694d, editor.c, GPL-3.0-only,
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments); the commands are Hortator's own. */
/* Hortator editor protocol 1, the PRESETS and PATTERNS capabilities (web/EDITOR_PROTOCOL.md §2.3, §2.4): NAMES,
 * PRESET_LOAD, STEPS_GET, STEPS_SET, LOCKS_GET, LOCK_EDIT, CUE, SAVE and the pushes STEPS_CHANGED / LOCKS_CHANGED.
 * Main loop only (editor.c's ed_service): every change goes through the paths the device's own UI takes — presets.c
 * preset_load, motion.c lock_set / lock_clear_*, project.c cue_slot / cue_var / project_save_go —, steps are written
 * with the audio IRQ off. No buffer of its own: a variation is read straight from its bank (RAM, or flash through
 * XIP) into the reply. RAM: the push shadows, 32 B.
 * Switch-to-edit (spec owner decision 3): STEPS_SET of a variation of the loaded slot that is not playing presses that
 * variation's key (cue_var): stopped, the switch happens at once and the edit applies (rc 0); playing, the switch is
 * queued as the key queues it (or already is) and the edit is refused with rc 5 detail 32: the page sends it again
 * once the STATE push shows the variation playing.
 * NUDGE (minor 1, ED_CAP_NUDGE): STEPS_GET's reply ends with the 64 steps' micro (micro + 64); STEPS_SET takes them
 * after its steps (count x 1) or not (minor 0's request): then each written step keeps the device's micro, unless it is
 * written empty (as a tap leaves a step turned off: micro 0 too). */
#define ED_HAS_PATTERN 1

static struct { uint16_t st[NTRK], lk[NTRK]; } edp_sh;   /* each track's steps / locks as the page last saw them */

static uint32_t edp_fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint16_t edp_fold(uint32_t h) { return (uint16_t)(h ^ h >> 16); }
static uint16_t edp_steps_hash(uint32_t k) { return edp_fold(edp_fnv(2166136261u, trk[k].step, sizeof trk[k].step)); }
static uint16_t edp_locks_hash(uint32_t k)       /* track k's events in the live bank: key, value, kind */
{
    uint32_t i, z = lk_seek(&mo.b, k + 1u, 0), h = 2166136261u;
    for (i = lk_seek(&mo.b, k, 0); i < z; i++) {
        uint32_t e = (uint32_t)mo.b.key[i] << 9 | (uint32_t)(uint8_t)mo.b.val[i] << 1 | (uint32_t)lk_is_lock(&mo.b, i);
        h = edp_fnv(h, &e, sizeof e);
    }
    return edp_fold(h);
}
static void ed_pattern_sync(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        edp_sh.st[k] = edp_steps_hash(k);
        edp_sh.lk[k] = edp_locks_hash(k);
    }
}
static uint32_t edp_slot7(void)                  /* the loaded slot on the wire (7F: the power-on kit) */
{
    int32_t ld = proj_loaded();
    return ld < 0 ? ED_NONE : (uint32_t)ld;
}
/* device-side changes since the shadows: one STEPS_CHANGED and one LOCKS_CHANGED at most (track 7F: several); a
 * change not pushed (no budget) stays for the next pass */
static uint32_t ed_pattern_push(uint32_t budget)
{
    uint32_t k, sent = 0, ns = 0, nl = 0, ts = 0, tl = 0;
    uint16_t hs[NTRK], hl[NTRK];
    if (!budget)
        return 0;
    for (k = 0; k < NTRK; k++) {
        hs[k] = edp_steps_hash(k);
        hl[k] = edp_locks_hash(k);
        if (hs[k] != edp_sh.st[k]) {
            ns++;
            ts = k;
        }
        if (hl[k] != edp_sh.lk[k]) {
            nl++;
            tl = k;
        }
    }
    if (ns) {
        ed_push(ED_P_STEPS_CHANGED);
        ed_b(edp_slot7());
        ed_b(var_cur);
        ed_b(ns > 1u ? ED_NONE : ts);
        ed_send();
        for (k = 0; k < NTRK; k++)
            edp_sh.st[k] = hs[k];
        sent++;
    }
    if (nl && sent < budget) {
        ed_push(ED_P_LOCKS_CHANGED);
        ed_b(edp_slot7());
        ed_b(nl > 1u ? ED_NONE : tl);
        ed_send();
        for (k = 0; k < NTRK; k++)
            edp_sh.lk[k] = hl[k];
        sent++;
    }
    return sent;
}

/* ---- replies */
static void edp_step(const step_t *s)           /* a step on the wire: flags, cond, note */
{
    ed_b((uint32_t)s->on | (uint32_t)s->acc << 1 | (uint32_t)s->slide << 2 | (uint32_t)s->tie << 3 |
         (uint32_t)s->rat << ED_STEP_RAT_SHIFT);
    ed_b(s->cond < COND_MAX ? s->cond : COND_MAX);
    ed_b((uint32_t)(clamp(s->nt, NT_MIN, NT_MAX) + (int32_t)ED_NT_OFFSET));
}
static int edp_fail(uint32_t cmd, uint32_t rc, uint32_t detail, const char *msg)
{
    ed_fail(cmd, rc, detail, msg);
    return 1;
}
static int edp_empty(uint32_t cmd, uint32_t s)  /* SLOT c EMPTY (steps / locks) */
{
    char m[] = "SLOT A EMPTY";
    m[5] = (char)('A' + (s & 3u));
    return edp_fail(cmd, ED_E_REFUSED, ED_D_EMPTY_SLOT, m);
}
static int edp_cue_fail(uint32_t cmd, int r)    /* a cue refused: cue.c's code, the device's message */
{
    return edp_fail(cmd, ED_E_REFUSED, (uint32_t)r, ui.msg);
}
static int edp_busy(uint32_t cmd)               /* a write while a restore runs (editor_backup.c) */
{
#ifdef ED_HAS_BACKUP
    if (ed_restoring())
        return edp_fail(cmd, ED_E_BUSY, ED_D_RESTORING, "RESTORING");
#else
    (void)cmd;
#endif
    return 0;
}
/* slot s's variations bank in RAM (the playing one, the park), else flash's (0: none) */
static const var_bank_t *edp_bank(uint32_t s)
{
    if (var_who[var_play] == s + 1u)
        return &var_buf[var_play];
    if (var_who[var_play ^ 1u] == s + 1u)
        return &var_buf[var_play ^ 1u];
    return proj_var_flash(s);
}

/* a variation entry's set steps from q on: n of them (3 B, + 1 with flags bit 6: the micro; varpack.c) -> the byte
 * after them, 0 past end (malformed) */
static uint32_t edp_var_skip(const var_bank_t *b, uint32_t q, uint32_t n, uint32_t end)
{
    for (; n; n--) {
        if (q + 3u > end)
            return 0;
        q += 3u + ((b->d[q] >> 6) & 1u);
    }
    return q <= end ? q : 0u;
}

/* NAMES: bank, first */
static int edp_names(uint32_t cmd, const uint8_t *a)
{
    uint32_t b = a[0], first = a[1], tot, i, n, room;
    if (b >= PB_USER)
        return edp_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    tot = PRE_BANK_START[b + 1u] - PRE_BANK_START[b];
    ed_reply(cmd, ED_OK);
    ed_b(b);
    ed_str(N_PBANK[b]);
    ed_b(tot);
    ed_b(first);
    room = ed_room() - 1u;                       /* (the list's count) */
    for (n = 0, i = first; i < tot; i++, n++) {
        uint32_t need = 2u + 1u + str_len(PRE_FACTORY[PRE_BANK_START[b] + i].name) + 1u;
        if (need > room)
            break;
        room -= need;
    }
    ed_b(n);
    for (i = first; n; i++, n--) {
        const preset_t *p = &PRE_FACTORY[PRE_BANK_START[b] + i];
        ed_u14(PRE_BANK_START[b] + i);
        ed_b(p->model);
        ed_str(p->name);
    }
    ed_send();
    return 1;
}

/* PRESET_LOAD: track (7F: selected), id u14 — as the PRESETS list's load */
static int edp_preset_load(uint32_t cmd, const uint8_t *a)
{
    uint32_t k = a[0] == ED_NONE ? song.sel : a[0], id = ed_get_u14(a + 1);
    preset_t p;
    if (edp_busy(cmd))
        return 1;
    if (k >= NTRK || id >= PRE_NID)
        return edp_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (!preset_get(id, &p))
        return edp_fail(cmd, ED_E_REFUSED, ED_D_PRESET_EMPTY, "EMPTY SLOT");
    if (preset_load(&trk[k], id) < 0)
        return edp_fail(cmd, ED_E_REFUSED, ED_D_ACID_CAP, "ACID ON 2 TRACKS");
    ui.force = 1;
    ed_reply(cmd, ED_OK);
    ed_b(k);
    ed_u14(id);
    ed_b((uint32_t)trk[k].p[P_MODEL]);
    ed_send();
    return 1;
}

/* STEPS_GET: slot (7F: loaded), var, track — what the device would play; then the steps' NUDGE (minor 1) */
static int edp_steps_get(uint32_t cmd, const uint8_t *a)
{
    uint32_t v = a[1], k = a[2], i, j, used = 1;
    int32_t ld = proj_loaded(), s = a[0] == ED_NONE ? ld : (int32_t)a[0];
    int8_t mic[NSTEP];
    int live;
    if (v >= VAR_N || k >= NTRK || (a[0] != ED_NONE && a[0] > 3u))
        return edp_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (s < 0 && v != var_cur)
        return edp_fail(cmd, ED_E_REFUSED, ED_D_NO_SLOT, "SAVE TO A SLOT FIRST");
    if (s >= 0 && s != ld && !proj_ok(&proj_slot[s]))
        return edp_empty(cmd, (uint32_t)s);
    live = s == ld && v == var_cur;
    ed_reply(cmd, ED_OK);
    ed_b(s < 0 ? ED_NONE : (uint32_t)s);
    ed_b(v);
    ed_b(k);
    if (live || !v) {                            /* the live steps, or variation 1: the slot's record (FDRG: rsv the micro) */
        const step_t *st = live ? trk[k].step : proj_slot[s].t[k].step;
        ed_b(1);
        ed_b(live ? 1u : 0u);
        for (j = 0; j < 4u; j++) {
            uint32_t id = CHAIN_TIMING[j];
            ed_b((uint32_t)clamp(live ? trk[k].p[id] : proj_slot[s].t[k].p[id], TP[id].min, TP[id].max));
        }
        for (i = 0; i < NSTEP; i++) {
            edp_step(&st[i]);
            mic[i] = st[i].micro;
        }
    } else {                                     /* variations 2..8: track k's part of the entry (varpack.c layout) */
        const var_bank_t *b = edp_bank((uint32_t)s);
        uint32_t sz = 0, p = 0, q = 0, end = 0, map[2] = {0, 0}, n;
        int32_t o = b ? var_find(b, v, &sz) : -1;
        static const uint8_t TM_MIN[4] = {1, 0, 0, 0}, TM_MAX[4] = {NSTEP, 9, 100, 3};
        used = o >= 0 && sz > 2u;
        if (used) {
            p = (uint32_t)o + 2u;
            end = (uint32_t)o + sz;
            for (j = 0, n = 0; j < k; j++)
                for (i = 0; i < 8u; i++)
                    n += var_pop8(b->d[p + j * VAR_TRK + 4u + i]);
            for (i = 0; i < 8u; i++)
                map[i >> 2] |= (uint32_t)b->d[p + k * VAR_TRK + 4u + i] << 8u * (i & 3u);
            q = sz < 2u + NTRK * VAR_TRK ? 0u : edp_var_skip(b, p + NTRK * VAR_TRK, n, end);
            if (!q || !edp_var_skip(b, q, var_pop8(map[0]) + var_pop8(map[1]), end))
                map[0] = map[1] = 0;            /* (malformed: as var_get, no steps) */
        }
        ed_b(used);
        ed_b(0);
        for (j = 0; j < 4u; j++)
            ed_b(used ? (uint32_t)clamp(b->d[p + k * VAR_TRK + j], TM_MIN[j], TM_MAX[j]) : 0u);
        for (i = 0; i < NSTEP; i++) {
            step_t x;
            memset(&x, 0, sizeof x);
            if ((map[i >> 5] >> (i & 31u)) & 1u) {
                uint32_t f = b->d[q];
                x.on = f & 1u;
                x.acc = (f >> 1) & 1u;
                x.slide = (f >> 2) & 1u;
                x.tie = (f >> 3) & 1u;
                x.rat = (f >> 4) & 3u;
                x.cond = b->d[q + 1u];
                x.nt = (int8_t)b->d[q + 2u];
                if ((f >> 6) & 1u)
                    x.micro = (int8_t)b->d[q + 3u];
                q += 3u + ((f >> 6) & 1u);
            }
            edp_step(&x);
            mic[i] = x.micro;
        }
    }
    for (i = 0; i < NSTEP; i++)                  /* NUDGE: micro + 64 */
        ed_b((uint32_t)(clamp(mic[i], -MICRO_MAX, MICRO_MAX) + (int32_t)ED_MICRO_OFFSET));
    ed_send();
    return 1;
}

/* STEPS_SET: slot (7F: loaded), var, track, first, count, count x step[, count x NUDGE] — the playing variation of
 * the loaded slot; another variation of it: its key pressed first (switch-to-edit, above) */
static int edp_steps_set(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t v = a[1], k = a[2], first = a[3], cnt = a[4], i, done = 0, mic = n == ED_LEN_STEPS_SET_MIN + 4u * cnt;
    int32_t ld = proj_loaded();
    if (n != ED_LEN_STEPS_SET_MIN + 3u * cnt && !mic)
        return edp_fail(cmd, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    if (edp_busy(cmd))
        return 1;
    if (v >= VAR_N || k >= NTRK || !cnt || first + cnt > NSTEP || (a[0] != ED_NONE && a[0] > 3u))
        return edp_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (chain_busy())                            /* (ui.c song_lock: a song's patterns stay as they are) */
        return edp_fail(cmd, ED_E_REFUSED, ED_D_CUE_SONG, "STOP TO EDIT");
    if (ld < 0 && v != var_cur)
        return edp_fail(cmd, ED_E_REFUSED, ED_D_NO_SLOT, "SAVE TO A SLOT FIRST");
    if (a[0] != ED_NONE && (int32_t)a[0] != ld)
        return edp_fail(cmd, ED_E_STALE, ED_D_CUE_FIRST, "CUE FIRST");
    if (v != var_cur) {                          /* switch-to-edit: the variation's key */
        int r = 0;
        if (cue.q_slot)
            return edp_fail(cmd, ED_E_STALE, ED_D_CUE_FIRST, "CUE FIRST");   /* (a slot queued: its variations) */
        if (cue.q_var != v + 1u)
            r = cue_var(v);
        if (r)
            return edp_cue_fail(cmd, r);
    }
    fm1_irq_off();                               /* (no switch in between: the check and the write together) */
    if (v == var_cur && proj_loaded() == ld) {
        for (i = 0; i < cnt; i++) {
            const uint8_t *w = a + ED_LEN_STEPS_SET_MIN + 3u * i;
            step_t x;
            memset(&x, 0, sizeof x);
            x.on = w[0] & ED_STEP_ON ? 1u : 0u;
            x.acc = w[0] & ED_STEP_ACC ? 1u : 0u;
            x.slide = w[0] & ED_STEP_SLIDE ? 1u : 0u;
            x.tie = w[0] & ED_STEP_TIE ? 1u : 0u;
            x.rat = (w[0] >> ED_STEP_RAT_SHIFT) & 3u;
            x.cond = (uint8_t)(w[1] < COND_MAX ? w[1] : COND_MAX);
            x.nt = (int8_t)clamp((int32_t)w[2] - (int32_t)ED_NT_OFFSET, NT_MIN, NT_MAX);
            if (mic)                             /* NUDGE sent; else the device's kept (an empty step: 0) */
                x.micro = (int8_t)clamp((int32_t)a[ED_LEN_STEPS_SET_MIN + 3u * cnt + i] - (int32_t)ED_MICRO_OFFSET,
                                        -MICRO_MAX, MICRO_MAX);
            else if (x.on | x.acc | x.slide | x.tie | x.rat | x.cond | x.nt)
                x.micro = trk[k].step[first + i].micro;
            trk[k].step[first + i] = x;
        }
        done = 1;
    }
    fm1_irq_on();
    if (!done)                                   /* playing: the switch queued, the page sends it again after it */
        return edp_fail(cmd, ED_E_STALE, ED_D_CUE_FIRST, "CUE FIRST");
    edp_sh.st[k] = edp_steps_hash(k);            /* (no push echoes the editor's own write) */
    ui.force = 1;
    ed_reply(cmd, ED_OK);
    ed_b(a[0]);
    ed_b(v);
    ed_b(k);
    ed_b(first);
    ed_b(cnt);
    ed_send();
    return 1;
}

/* LOCKS_GET: slot (7F: loaded), track, first u14 — the bank's events of the track in its order */
static int edp_locks_get(uint32_t cmd, const uint8_t *a)
{
    uint32_t k = a[1], first = ed_get_u14(a + 2), i, z, tot, n;
    int32_t ld = proj_loaded(), s = a[0] == ED_NONE ? ld : (int32_t)a[0];
    const lk_bank_t *b = &mo.b;
    if (k >= NTRK || (a[0] != ED_NONE && a[0] > 3u))
        return edp_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (s >= 0 && s != ld) {
        if (!proj_ok(&proj_slot[s]))
            return edp_empty(cmd, (uint32_t)s);
        b = &lk_slot[s];
        if (!lk_valid(b, (uint32_t)s))
            b = 0;                               /* (a broken bank loads empty) */
    }
    i = b ? lk_seek(b, k, 0) : 0;
    z = b ? lk_seek(b, k + 1u, 0) : 0;
    tot = z - i;
    ed_reply(cmd, ED_OK);
    ed_b(s < 0 ? ED_NONE : (uint32_t)s);
    ed_b(k);
    ed_u14(tot);
    ed_u14(first);
    n = first < tot ? tot - first : 0u;
    if (n > (ed_room() - 1u) / 5u)
        n = (ed_room() - 1u) / 5u;
    ed_b(n);
    for (i += first; n; i++, n--) {
        ed_b((uint32_t)(b->key[i] >> 7) & 63u);
        ed_b(b->key[i] & 127u);
        ed_b(lk_is_lock(b, i) ? ED_LK_KIND_LOCK : ED_LK_KIND_MOTION);
        ed_v14(b->val[i]);
    }
    ed_send();
    return 1;
}

/* LOCK_EDIT: op, track, step, param, value v14 — the live bank, through motion.c's lock calls */
static int edp_lock_edit(uint32_t cmd, const uint8_t *a)
{
    uint32_t op = a[0], k = a[1], st = a[2], id = a[3];
    int32_t v = ed_get_v14(a + 4);
    if (edp_busy(cmd))
        return 1;
    if (op > ED_LK_CLEAR_TRACK || k >= NTRK || st >= NSTEP || id >= P_COUNT)
        return edp_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (op == ED_LK_SET) {
        const param_desc_t *d = track_desc(&trk[k], id);
        if (!motion_param(id))
            return edp_fail(cmd, ED_E_REFUSED, ED_D_NOT_LOCKABLE, "NOT LOCKABLE");
        v = clamp(v, d->min > -64 ? d->min : -64, d->max < 127 ? d->max : 127);
        if (lk_find(&mo.b, lk_key(k, st, id), 1) < 0 && mo.b.count >= LK_MAX)
            return edp_fail(cmd, ED_E_REFUSED, ED_D_LOCKS_FULL, "LOCKS FULL");
        lock_set(k, st, id, v);
    } else if (op == ED_LK_CLEAR) {
        uint32_t ids[MO_W] = {0};
        ids[id >> 5] = 1u << (id & 31u);
        (void)lock_clear_step(k, st, ids);
    } else if (op == ED_LK_CLEAR_STEP) {
        (void)lock_clear_step(k, st, 0);
    } else {
        lock_clear_track(k);
    }
    edp_sh.lk[k] = edp_locks_hash(k);
    ui.force = 1;
    ed_reply(cmd, ED_OK);
    ed_b(op);
    ed_b(k);
    ed_b(st);
    ed_b(id);
    ed_v14(v);
    ed_u14(lock_count(k));
    ed_send();
    return 1;
}

/* CUE: kind (0 slot, 1 variation), n — the cue keys */
static int edp_cue(uint32_t cmd, const uint8_t *a)
{
    uint32_t kind = a[0], n = a[1];
    int r;
    if (edp_busy(cmd))
        return 1;
    if (kind > ED_CUE_VAR || n > (kind == ED_CUE_SLOT ? 3u : 7u))
        return edp_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    r = kind == ED_CUE_SLOT ? cue_slot(n) : cue_var(n);
    if (r)
        return edp_cue_fail(cmd, r);
    ed_reply(cmd, ED_OK);
    ed_b(kind);
    ed_b(n);
    ed_state();
    ed_send();
    return 1;
}

/* SAVE: slot (7F: loaded) — the device's SAVE of the loaded slot */
static int edp_save(uint32_t cmd, const uint8_t *a)
{
    int32_t ld = proj_loaded();
    if (edp_busy(cmd))
        return 1;
    if (a[0] != ED_NONE && (a[0] > 3u || (int32_t)a[0] != ld))
        return edp_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
    if (transport_busy())
        return edp_fail(cmd, ED_E_STOP, ED_D_STOP, "STOP TO SAVE");
    if (ld < 0)
        return edp_fail(cmd, ED_E_REFUSED, ED_D_NO_SLOT, "SAVE TO A SLOT FIRST");
    if (!flash_ok)
        return edp_fail(cmd, ED_E_REFUSED, ED_D_NO_FLASH, "NO FLASH");
    project_save_go((uint32_t)ld);
    if (str_eq(ui.msg, "VARIATIONS FULL"))
        return edp_fail(cmd, ED_E_REFUSED, ED_D_VARS_FULL, ui.msg);
    if (var_blocked || lk_blocked)
        return edp_fail(cmd, ED_E_REFUSED, ED_D_BLOCKED, ui.msg);
    if (!str_eq(ui.msg, "SAVED"))
        return edp_fail(cmd, ED_E_FLASH, ED_D_NONE, "SAVE ERROR");
    ed_reply(cmd, ED_OK);
    ed_b((uint32_t)ld);
    ed_state();
    ed_send();
    return 1;
}

/* editor.c's dispatch: 1 handled (replied), 0 not this file's command */
static int ed_pattern_cmd(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    static const uint8_t LEN[ED_SAVE - ED_NAMES + 1] = {ED_LEN_NAMES, ED_LEN_PRESET_LOAD, ED_LEN_STEPS_GET,
                                                        ED_LEN_STEPS_SET_MIN, ED_LEN_LOCKS_GET, ED_LEN_LOCK_EDIT,
                                                        ED_LEN_CUE, ED_LEN_SAVE};
    if (cmd < ED_NAMES || cmd > ED_SAVE)
        return 0;
    if (cmd == ED_STEPS_SET ? n < ED_LEN_STEPS_SET_MIN : n != LEN[cmd - ED_NAMES])
        return edp_fail(cmd, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    switch (cmd) {
    case ED_NAMES:
        return edp_names(cmd, a);
    case ED_PRESET_LOAD:
        return edp_preset_load(cmd, a);
    case ED_STEPS_GET:
        return edp_steps_get(cmd, a);
    case ED_STEPS_SET:
        return edp_steps_set(cmd, a, n);
    case ED_LOCKS_GET:
        return edp_locks_get(cmd, a);
    case ED_LOCK_EDIT:
        return edp_lock_edit(cmd, a);
    case ED_CUE:
        return edp_cue(cmd, a);
    default:
        return edp_save(cmd, a);
    }
}
