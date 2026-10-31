/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE
 * Parts ported from upstream Felucca's editor (hugelton/Felucca 5d9694d, firmware/src/editor.c: the framing helpers,
 * ed_unpack7, the ed_service frame peek, the per-command length check, the live-sync shadows with ED_PUSH_MAX and the
 * half-empty ring rule, the WATCH keep-alive; GPL-3.0-only, Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton
 * Instruments). */
/* Hortator editor protocol 1, the core (web/EDITOR_PROTOCOL.md; the numbers: editor_proto.h). Included last of the
 * editor's files (felucca.c, FELUCCA_OTA): framing, the 7-bit fields, the duplicate rule, dispatch; INFO, PAGES, DESC,
 * GET, SET, TEXT, DUMP, SELECT, WATCH and the CHANGED / STATE pushes; the update-silence rule. editor_backup.c and
 * editor_pattern.c, when included before it, define ED_HAS_BACKUP / ED_HAS_PATTERN and their hooks (editor_proto.h).
 *
 * Frames come through usb_app.c's one received frame (sx_frame, ota_frame_get) and leave through its SysEx out ring
 * (ota_wire_send). main.c (frozen) calls ed_service() before ota_service() every pass and again in its 15 ms wait:
 * a 7D 48 54 frame is taken and freed in the same pass (a short one copied first, so the slot is free while the reply
 * goes out; a long one read in place); anything not starting 7D is update traffic, left for ota_take(), and silences
 * the editor for ED_SILENCE_MS (no replies, no pushes, WATCH off, an uncommitted restore dropped). Main loop only:
 * knob writes go as a learned MIDI CC's (midi_learn.c ml_set: motion's base, a lock's value under it), a MODEL as the
 * MODEL knob's (drum_set_model), with the audio IRQ off for the moment of the write. */
#define ED_NV (P_COUNT + G_COUNT)          /* push shadows: the selected track's knobs, then the globals */
#define ED_REQ_COPY 64u                    /* a request this short is copied out of sx_frame before it is answered */
#define ED_DUP_MAX 48u                     /* a write's reply kept for a retry (every write's reply is shorter) */
#define ED_SCAN_MS 10u                     /* pushes: the device's state compared at most this often */
#define ED_STALL_MS 1000u                  /* ed_service not called this long while silent (an update session ran):
                                            * the silence starts again from now */
_Static_assert(ED_NV <= 255u, "ed.pos is a byte");

static uint8_t ed_out[ED_REPLY_MAX] __attribute__((section(".pool")));   /* the reply / push being built */
static struct {
    uint32_t n;                            /* bytes in ed_out */
    uint32_t rx_ms;                        /* the last request (WATCH keep-alive, the duplicate memory) */
    uint32_t hush_ms, call_ms, scan_ms;    /* the last update traffic; the last ed_service; the last push scan */
    uint8_t hush, on, seq, write;          /* silent; WATCH on; the request's seq; it is a write (its reply kept) */
    uint8_t last, last_cmd, last_seq;      /* the last (cmd, seq) handled (the duplicate rule) */
    uint8_t dup_n, pos;                    /* bytes of its kept reply (0: none); the CHANGED scan's round robin */
    uint8_t st[ED_STATE_N];                /* the state block as last pushed */
    int16_t v[ED_NV];                      /* the knobs as the editor knows them */
    uint8_t dup[ED_DUP_MAX];
    uint8_t req[ED_REQ_COPY];
} ed;

/* ---------------------------------------------------------------- framing --- */
static void ed_head(uint32_t cmd, uint32_t seq)
{
    ed_out[0] = 0xF0;
    ed_out[1] = ED_H0;
    ed_out[2] = ED_H1;
    ed_out[3] = ED_H2;
    ed_out[4] = (uint8_t)(cmd & 0x7Fu);
    ed_out[5] = (uint8_t)(seq & 0x7Fu);
    ed.n = 6;
}
static void ed_b(uint32_t v)
{
    if (ed.n < ED_REPLY_MAX - 1u)
        ed_out[ed.n++] = (uint8_t)(v & 0x7Fu);
}
static void ed_reply(uint32_t cmd, uint32_t rc)
{
    ed_head(cmd, ed.seq);
    ed_b(rc);
}
static void ed_push(uint32_t cmd) { ed_head(cmd, ED_SEQ_PUSH); }
static void ed_u14(uint32_t v)
{
    ed_b(v);
    ed_b(v >> 7);
}
static void ed_v14(int32_t v) { ed_u14((uint32_t)(clamp(v, -8192, 8191) + 8192)); }
static void ed_u35(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 5u; i++, v >>= 7)
        ed_b(v);
}
static void ed_str(const char *s)          /* ASCII 32..126 (the font's degree sign: "deg"), then 0 */
{
    for (; s && *s; s++) {
        uint32_t c = (uint8_t)*s;
        if (c == 0xB0u) {
            ed_b('d');
            ed_b('e');
            ed_b('g');
        } else if (c >= 32u && c <= 126u) {
            ed_b(c);
        }
    }
    ed_b(0);
}
static void ed_pack(const void *d, uint32_t n)   /* groups of up to 7 bytes after a byte of their bit 7s */
{
    const uint8_t *p = d;
    uint32_t i, j, k;
    for (i = 0; i < n; i += k) {
        uint32_t hi = 0;
        k = n - i < 7u ? n - i : 7u;
        for (j = 0; j < k; j++)
            hi |= (uint32_t)(p[i + j] >> 7) << j;
        ed_b(hi);
        for (j = 0; j < k; j++)
            ed_b(p[i + j]);
    }
}
static uint32_t ed_room(void) { return ED_REPLY_MAX - 1u - ed.n; }
static void ed_wire(const uint8_t *p, uint32_t n)   /* into the SysEx out ring; midi_out.c: not update traffic */
{
    mout.ed_tx = 1;
    RING_PUBLISH();
    ota_wire_send(p, n);
    mout.ed_end = so_w;
    RING_PUBLISH();
    mout.ed_tx = 0;
}
static void ed_send(void)
{
    ed_out[ed.n++] = 0xF7;
    if (ed.hush)
        return;
    if (ed.write && ed_out[5] != ED_SEQ_PUSH && ed.n <= sizeof ed.dup) {   /* a write's reply: kept for a retry */
        memcpy(ed.dup, ed_out, ed.n);
        ed.dup_n = (uint8_t)ed.n;
    }
    ed_wire(ed_out, ed.n);
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
static uint32_t ed_unpack7(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t max)   /* (upstream's) */
{
    uint32_t o = 0;
    while (n && o < max) {
        uint32_t m = *in++, j, k;
        n--;
        k = n > 7u ? 7u : n;
        if (!k || o + k > max || (m >> k))
            return 0;                      /* a group without data, too much, or mask bits past its bytes */
        for (j = 0; j < k; j++, n--)
            out[o++] = (uint8_t)(*in++ | ((m >> j) & 1u) << 7);
    }
    return n ? 0u : o;
}

/* ------------------------------------------------------------ the state --- */
static void ed_state_get(uint8_t *s)       /* web/EDITOR_PROTOCOL.md section 3 */
{
    int32_t ld = proj_loaded();
    uint32_t i, used = 0, dirty = 0, m;
    s[0] = song.playing ? 1u : 0u;
    s[1] = ld < 0 ? ED_NONE : (uint8_t)ld;
    s[2] = var_cur & 7u;
    s[3] = cue.q_slot ? (uint8_t)(cue.q_slot - 1u) : ED_NONE;
    s[4] = cue.q_var ? (uint8_t)(cue.q_var - 1u) : ED_NONE;
    s[5] = song.sel;
    s[6] = (chain_busy() ? ED_SF_SONG : 0u) | (flash_ok ? 0u : ED_SF_NOFLASH);
#ifdef ED_HAS_BACKUP
    s[6] |= ed_restoring() ? ED_SF_RESTORING : 0u;
#endif
    for (i = 0; i < 4u; i++) {
        int on = proj_slot[i].magic == PROJ_MAGIC;
        used |= (uint32_t)on << i;
        dirty |= (uint32_t)(slot_dirty(i) != 0) << i;
        m = on ? slot_var_mask(i) : 1u;
        if ((int32_t)i == ld)
            m |= 1u << (var_cur & 7u);
        s[9u + 2u * i] = (uint8_t)(m & 0x7Fu);
        s[10u + 2u * i] = (uint8_t)((m >> 7) & 0x7Fu);
    }
    s[7] = (uint8_t)used;
    s[8] = (uint8_t)dirty;
}
static void ed_state(void)
{
    uint8_t s[ED_STATE_N];
    uint32_t i;
    ed_state_get(s);
    for (i = 0; i < ED_STATE_N; i++)
        ed_b(s[i]);
}

/* ------------------------------------------------------------ the knobs --- */
#define ED_RO_G ((1u << G_MIDI) | (1u << G_INFO) | (1u << G_SLOT) | (1u << G_NAME) | (1u << G_LOAD) | (1u << G_SAVE) | \
                 (1u << G_CLRSEQ) | (1u << G_INITSND))   /* globals shown, not settable */
_Static_assert(G_INITSND < 32, "ED_RO_G is a word");
static int ed_ro_global(uint32_t id) { return id < 32u && ((ED_RO_G >> id) & 1u); }
typedef struct {
    const param_desc_t *d;
    const track_t *t;                      /* scope track: the track (else 0) */
    uint32_t k, flags, nnames;             /* the track's index; DESC flags; value names */
} ed_knob_t;
static uint32_t ed_names_n(const param_desc_t *d)
{
    uint32_t n = 0;
    if (d->fmt == F_LDEST)
        return 19u;
    if (!d->names)
        return 0;
    if (d->fmt == F_ENUM)
        return (uint32_t)(d->max - d->min + 1);
    while (d->names[n])
        n++;
    return n;
}
static const char *ed_name(const ed_knob_t *x, uint32_t i)
{
    if (x->d->fmt == F_LDEST)
        return lfo_dest_label(x->t ? x->t : TSEL, (int32_t)i);
    return x->d->fmt == F_ENUM ? x->d->names[x->d->min + (int32_t)i] : x->d->names[i];
}
/* (scope, track, id) -> its knob; 0, or the rc 1 detail 45 reply sent */
static int ed_knob(uint32_t cmd, uint32_t scope, uint32_t track, uint32_t id, ed_knob_t *x)
{
    x->t = 0;
    x->k = 0;
    x->flags = 0;
    if (scope == ED_SC_TRACK && (track < NTRK || track == ED_NONE) && id < P_COUNT) {
        x->k = track == ED_NONE ? song.sel : track;
        x->t = &trk[x->k];
        x->d = track_desc(x->t, id);
        x->flags = (motion_param(id) ? ED_DF_LOCK : 0u) | (id == P_MODEL || enum_order(x->d) ? ED_DF_ORDER : 0u) |
                   ((id >= P_E0 && id <= P_E7) || (id >= P_IA && id <= P_IC) || id == P_RSTRCT || x->d->fmt == F_LDEST
                        ? ED_DF_CONTEXT : 0u);
    } else if (scope == ED_SC_GLOBAL && id < G_COUNT) {
        x->d = &GP[id];
        x->flags = (enum_order(x->d) ? ED_DF_ORDER : 0u) | (ed_ro_global(id) ? ED_DF_RO : 0u);
    } else if (scope == ED_SC_MODEL && track < NMODELS && id < 8u) {
        x->d = &DMODELS[track].edit[id];
    } else {
        ed_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
        return 0;
    }
    if (x->d->names && x->d->fmt != F_ENUM)
        x->flags |= ED_DF_SPLIT;
    x->nnames = ed_names_n(x->d);
    return 1;
}
static int32_t ed_value(const ed_knob_t *x, uint32_t scope, uint32_t id)   /* the knob as set (never modulated) */
{
    return scope == ED_SC_GLOBAL ? song.g[id] : motion_rest(x->k, id);
}
/* the device's text of v on knob d of track t (0: a global): param_format, which reads the selected track's RATE
 * MODE and DEST labels; another track's its own */
static void ed_format(const track_t *t, const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    if (t && t != TSEL && d->fmt == F_LDEST) {
        str_cpy(val, lfo_dest_label(t, v), 6);
        *unit = "";
        return;
    }
    if (t && t != TSEL && (d->fmt == F_LRATE1 || d->fmt == F_LRATE2)) {   /* params.c's F_LRATE text by t's MODE */
        uint32_t mode = (uint32_t)clamp(t->p[(d->fmt == F_LRATE1 ? P_LFO1 : P_LFO2) + LF_MODE], 0, 2);
        uint32_t k = (uint32_t)clamp(v, 0, 127);
        *unit = "";
        if (mode == LM_SYNC) {
            str_cpy(val, N_LSYNC[k * 17u / 128u], 6);
        } else if (mode == LM_HZ) {
            uint32_t h = LR_HZ_X100[k], n;
            fmt_int(val, (int32_t)(h / 100u));
            n = str_len(val);
            val[n++] = '.';
            val[n++] = (char)('0' + h / 10u % 10u);
            if (h < 1000u)
                val[n++] = (char)('0' + h % 10u);
            val[n] = 0;
            *unit = "Hz";
        } else {
            fmt_ms10(val, unit, LR_TIME_MS[k] * 10u);
        }
        return;
    }
    param_format(d, v, val, unit);
}
static void ed_value_text(const ed_knob_t *x, int32_t v)   /* value v14, text str, unit str */
{
    char val[16];
    const char *unit = "";
    val[0] = 0;
    ed_format(x->t, x->d, v, val, &unit);
    ed_v14(v);
    ed_str(val);
    ed_str(unit);
}
static uint32_t ed_text_len(const ed_knob_t *x, int32_t v)   /* bytes ed_value_text needs */
{
    char val[16];
    const char *unit = "";
    val[0] = 0;
    ed_format(x->t, x->d, v, val, &unit);
    return 2u + str_len(val) + 1u + str_len(unit) + 3u;   /* (a degree sign: 3 bytes) */
}
static void ed_shadow_track(void)          /* the editor knows the selected track's knobs as they are */
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        ed.v[i] = motion_rest(song.sel, i);
}
static void ed_shadow_all(void)
{
    uint32_t i;
    ed_shadow_track();
    for (i = 0; i < G_COUNT; i++)
        ed.v[P_COUNT + i] = song.g[i];
    ed_state_get(ed.st);
}

/* a knob set as the device would (midi_learn.c ml_set, the MODEL knob's drum_set_model): 0, or the refusal sent */
static int ed_set(uint32_t cmd, const ed_knob_t *x, uint32_t scope, uint32_t id, int32_t v)
{
    track_t *t;
    if (scope == ED_SC_GLOBAL) {
        if (id == G_BPM && song.g[G_CLOCK])
            return 0;                      /* following a clock: its tempo (the knob says CLK) */
        song.g[id] = (int16_t)v;
        return 0;
    }
    t = &trk[x->k];
    if (id == P_MODEL) {
        if (v == t->p[P_MODEL])
            return 0;
        if (v == DM_ACID && acid_tracks(t) >= ACID_MAXTRK) {
            ed_fail(cmd, ED_E_REFUSED, ED_D_ACID_CAP, "ACID ON 2 TRACKS");
            return 1;
        }
        fm1_irq_off();
        drum_set_model(t, (uint32_t)v);
        motion_rebase(x->k);
        fm1_irq_on();
        ui.force = 1;
        return 0;
    }
    if (id == P_RMODEL && v != t->p[P_RMODEL]) {
        const char *why = reson_refused(t, t->p[P_RMODEL], v);
        if (why) {
            ed_fail(cmd, ED_E_REFUSED, ED_D_NONE, why);
            return 1;
        }
    }
    if ((id >= P_SLEN && id <= P_SSWING) || id == P_SRC) {
        if (chain_busy())
            return 0;                      /* PATTERN while a song plays: its rows' */
    }
    fm1_irq_off();
    if (PRE_SOUND(id) && motion_rest(x->k, id) != v)
        preset_touch(t);                   /* presets.c: the voice edited */
    if ((mo.base_ok >> x->k) & 1u)
        mo.base[x->k][id] = (int16_t)v;    /* while motion plays: the new base */
    if (lock_held(x->k, id))
        mo.under[x->k][id] = (int16_t)v;   /* a locked step: what the knob goes back to */
    t->p[id] = (int16_t)v;
    fm1_irq_on();
    return 0;
}

/* ------------------------------------------------------------- commands --- */
static int ed_len(uint32_t cmd, uint32_t n, uint32_t want)   /* upstream's ed_args_ok: 0, or rc 1 detail 44 sent */
{
    if (n == want)
        return 1;
    ed_fail(cmd, ED_E_BAD, ED_D_LENGTH, "BAD LENGTH");
    return 0;
}
static void ed_info(void)
{
    const char *fw = FELUCCA_VERSION;
    uint32_t caps = ED_CAP_LIVE;
#ifdef ED_HAS_PATTERN
    caps |= ED_CAP_PRESETS | ED_CAP_PATTERNS;
#endif
#ifdef ED_HAS_BACKUP
    caps |= ED_CAP_BACKUP | ED_CAP_RESTORE;
#endif
    if (fw[0] == 'D' && fw[1] == 'R' && fw[2] == 'U' && fw[3] == 'M' && fw[4] == '-')
        fw += 5;                           /* as VERSION.txt holds it */
    ed_reply(ED_INFO, ED_OK);
    ed_str(ED_MAGIC);
    ed_str(fw);
    ed_b(ED_PROTO_MAJOR);
    ed_b(ED_PROTO_MINOR);
    ed_u14(caps);
    ed_b(NTRK);
    ed_b(NSTEP);
    ed_b(P_COUNT);
    ed_b(G_COUNT);
    ed_b(NMODELS);
    ed_u14(PRE_NFACT);
    ed_u14(PRE_NUSER);
    ed_b(PB_USER);
    ed_b(4);
    ed_b(VAR_N);
    ed_u14(ED_REQ_MAX);
#if FELUCCA_BLE
    ed_b(1);                               /* the Bluetooth build (the editor's link stays USB) */
#else
    ed_b(0);
#endif
    ed_state();
    ed_send();
}
/* the device's pages the editor shows (EDITOR_PROTOCOL.md 2.2), resolved for the selected track now: page i, its
 * sub-page s (the LFO DEST page: its 4 slots) -> its 4 columns; 0: not shown */
static int ed_page(uint32_t i, uint32_t s, uint16_t *ids)
{
    const page_t *pg = &PAGES[i];
    uint32_t c, any = 0;
    if ((pg->scope != SC_TRACK && pg->scope != SC_GLOBAL) || pg->graph == GR_SLOTS || pg->graph == GR_MOTION ||
        pg->graph == GR_SONG || s >= (pg->graph == GR_LDEST ? LS_N : 1u))
        return 0;
    for (c = 0; c < 4u; c++)
        if (pg->scope == SC_GLOBAL && pg->id[c] != 0xFFu && pg->id[c] >= G_COUNT)
            return 0;                      /* TOOLS, GLOBAL 3/3: past the globals */
    for (c = 0; c < 4u; c++) {
        uint32_t id = pg->graph == GR_LDEST ? (c < 3u ? LS_ID[s][c] : 0xFFu) : page_id(pg, c);
        ids[c] = (uint16_t)(id == 0xFFu ? ED_U14_NONE : id);
        any |= id != 0xFFu;
    }
    return (int)any;
}
static void ed_pages(uint32_t first)
{
    uint32_t i, s, at, idx = 0, cnt = 0, full = 0, c;
    uint16_t ids[4];
    ed_reply(ED_PAGES, ED_OK);
    at = ed.n;
    ed_b(0);                               /* total, patched */
    ed_b(first);
    ed_b(0);                               /* count, patched */
    for (i = 0; i < NPAGES; i++)
        for (s = 0; s < LS_N; s++) {
            char title[12];
            if (!ed_page(i, s, ids))
                continue;
            if (idx++ < first || full)
                continue;
            str_cpy(title, PAGES[i].title, 8);
            if (PAGES[i].graph == GR_LDEST) {
                str_cpy(title, "DEST 1", 8);
                title[5] = (char)('1' + s);
            }
            if (ed_room() < str_len(title) + 1u + 2u + 8u) {
                full = 1;
                continue;
            }
            ed_str(title);
            ed_b(PAGES[i].scope == SC_TRACK ? ED_SC_TRACK : ED_SC_GLOBAL);
            ed_b(PAGES[i].graph);
            for (c = 0; c < 4u; c++)
                ed_u14(ids[c]);
            cnt++;
        }
    ed_out[at] = (uint8_t)(idx & 0x7Fu);
    ed_out[at + 2u] = (uint8_t)cnt;
    ed_send();
}
static void ed_desc(const uint8_t *a)
{
    uint32_t scope = a[0], track = a[1], id = ed_get_u14(a + 2), first = a[4], at, cnt = 0, i;
    ed_knob_t x;
    if (!ed_knob(ED_DESC, scope, track, id, &x))
        return;
    ed_reply(ED_DESC, ED_OK);
    ed_b(scope);
    ed_b(track);
    ed_u14(id);
    ed_b(x.d->fmt);
    ed_b(x.flags);
    ed_v14(x.d->min);
    ed_v14(x.d->max);
    ed_v14(x.d->def);
    ed_str(x.d->label ? x.d->label : "");
    ed_str(x.d->unit ? x.d->unit : "");
    ed_b(x.nnames);
    ed_b(first);
    at = ed.n;
    ed_b(0);                               /* count, patched */
    if (first == ED_DESC_ORDER) {          /* the shown order: value - min by place */
        const uint8_t *o = scope == ED_SC_TRACK && id == P_MODEL ? MODEL_LIST : enum_order(x.d);
        for (i = 0; o && i <= (uint32_t)(x.d->max - x.d->min) && ed_room() > 0u; i++, cnt++)
            ed_b(o[i]);
    } else {
        for (i = first; i < x.nnames; i++, cnt++) {
            const char *nm = ed_name(&x, i);
            if (ed_room() < str_len(nm) + 4u)
                break;
            ed_str(nm);
        }
    }
    ed_out[at] = (uint8_t)cnt;
    ed_send();
}
static void ed_get(const uint8_t *a)
{
    uint32_t scope = a[0], track = a[1], id = ed_get_u14(a + 2), count = a[4], n, at, cnt = 0;
    ed_knob_t x;
    if (scope > ED_SC_GLOBAL || !count) {
        ed_fail(ED_GET, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
        return;
    }
    if (!ed_knob(ED_GET, scope, track, id, &x))
        return;
    n = scope == ED_SC_GLOBAL ? G_COUNT : P_COUNT;
    ed_reply(ED_GET, ED_OK);
    ed_b(scope);
    ed_b(track);
    ed_u14(id);
    at = ed.n;
    ed_b(0);                               /* count, patched */
    for (; id < n && cnt < count; id++, cnt++) {
        int32_t v;
        x.d = scope == ED_SC_GLOBAL ? &GP[id] : track_desc(x.t, id);
        v = ed_value(&x, scope, id);
        if (ed_room() < ed_text_len(&x, v))
            break;
        ed_value_text(&x, v);
    }
    ed_out[at] = (uint8_t)cnt;
    ed_send();
}
static void ed_set_cmd(uint32_t cmd, const uint8_t *a)   /* SET, TEXT */
{
    uint32_t scope = a[0], track = a[1], id = ed_get_u14(a + 2);
    int32_t v = ed_get_v14(a + 4);
    ed_knob_t x;
    if (scope > ED_SC_GLOBAL) {
        ed_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
        return;
    }
    if (!ed_knob(cmd, scope, track, id, &x))
        return;
    v = clamp(v, x.d->min, x.d->max);
    if (cmd == ED_SET) {
        if (x.flags & ED_DF_RO) {
            ed_fail(cmd, ED_E_BAD, ED_D_RANGE, "READ ONLY");
            return;
        }
#ifdef ED_HAS_BACKUP
        if (ed_restoring()) {
            ed_fail(cmd, ED_E_BUSY, ED_D_RESTORING, "RESTORING");
            return;
        }
#endif
        if (ed_set(cmd, &x, scope, id, v))
            return;
        if (scope == ED_SC_TRACK)
            x.d = track_desc(x.t, id);     /* (a MODEL change: the knob's new context) */
        v = ed_value(&x, scope, id);
        if (scope == ED_SC_GLOBAL)         /* no CHANGED echoes the editor's own write */
            ed.v[P_COUNT + id] = (int16_t)v;
        else if (x.k == song.sel)
            ed_shadow_track();             /* (a MODEL moves the 8 SOUND knobs too) */
    }
    ed_reply(cmd, ED_OK);
    ed_b(scope);
    ed_b(track);
    ed_u14(id);
    ed_value_text(&x, v);
    ed_send();
}
static void ed_dump(uint32_t track)
{
    uint32_t k = track == ED_NONE ? song.sel : track, i;
    const track_t *t;
    if (k >= NTRK) {
        ed_fail(ED_DUMP, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
        return;
    }
    t = &trk[k];
    ed_reply(ED_DUMP, ED_OK);
    ed_b(k);
    ed_b((uint32_t)t->p[P_MODEL]);
    ed_u14(t->pre == PRE_NONE ? ED_U14_NONE : t->pre);
    ed_b(t->pre_ed ? 1u : 0u);
    ed_b(P_COUNT);
    for (i = 0; i < P_COUNT; i++)
        ed_v14(motion_rest(k, i));
    ed_b(G_COUNT);
    for (i = 0; i < G_COUNT; i++)
        ed_v14(song.g[i]);
    ed_send();
}

static int ed_is_write(uint32_t cmd)
{
    return cmd == ED_SET || cmd == ED_SELECT || cmd == ED_PRESET_LOAD || cmd == ED_STEPS_SET || cmd == ED_LOCK_EDIT ||
           cmd == ED_CUE || cmd == ED_SAVE || cmd == ED_RESTORE;
}
static void ed_dispatch(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    switch (cmd) {
    case ED_INFO:
        if (ed_len(cmd, n, ED_LEN_INFO))
            ed_info();
        return;
    case ED_PAGES:
        if (ed_len(cmd, n, ED_LEN_PAGES))
            ed_pages(a[0]);
        return;
    case ED_DESC:
        if (ed_len(cmd, n, ED_LEN_DESC))
            ed_desc(a);
        return;
    case ED_GET:
        if (ed_len(cmd, n, ED_LEN_GET))
            ed_get(a);
        return;
    case ED_SET:
    case ED_TEXT:
        if (ed_len(cmd, n, ED_LEN_SET))
            ed_set_cmd(cmd, a);
        return;
    case ED_DUMP:
        if (ed_len(cmd, n, ED_LEN_DUMP))
            ed_dump(a[0]);
        return;
    case ED_SELECT:
        if (!ed_len(cmd, n, ED_LEN_SELECT))
            return;
        if (a[0] != ED_NONE) {
            if (a[0] >= NTRK) {
                ed_fail(cmd, ED_E_BAD, ED_D_RANGE, "OUT OF RANGE");
                return;
            }
            track_select(a[0]);            /* as the track keys: one selection */
            ed_shadow_track();             /* the editor reads it (a STATE push follows) */
        }
        ed_reply(cmd, ED_OK);
        ed_b(song.sel);
        ed_send();
        return;
    case ED_WATCH:
        if (!ed_len(cmd, n, ED_LEN_WATCH))
            return;
        if (a[0] && !ed.on) {
            ed_shadow_all();               /* from what the device holds now */
#ifdef ED_HAS_PATTERN
            ed_pattern_sync();
#endif
        }
        ed.on = a[0] ? 1u : 0u;
        ed.scan_ms = fm1_ms;
        ed_reply(cmd, ED_OK);
        ed_b(ed.on);
        ed_send();
        return;
    default:
        break;
    }
#ifdef ED_HAS_PATTERN
    if (ed_pattern_cmd(cmd, a, n))
        return;
#endif
#ifdef ED_HAS_BACKUP
    if (ed_backup_cmd(cmd, a, n))
        return;
#endif
    if (cmd >= ED_NAMES && cmd < ED_CMD_N)
        ed_fail(cmd, ED_E_BAD, ED_D_NO_CAP, "NOT IN THIS FIRMWARE");
    else
        ed_fail(cmd, ED_E_BAD, ED_D_UNKNOWN, "UNKNOWN COMMAND");
}
/* f: the bytes between F0 and F7 (header, cmd, seq, arguments), n >= ED_MIN_FRAME */
static void ed_handle(const uint8_t *f, uint32_t n, uint32_t now)
{
    uint32_t cmd = f[3], seq = f[4];
    if (seq == ED_SEQ_PUSH)
        return;                            /* (a push's seq: answered by nothing) */
    ed.rx_ms = now;                        /* any request keeps WATCH on */
    ed.seq = (uint8_t)seq;
    if (ed_is_write(cmd) && ed.last && ed.last_cmd == cmd && ed.last_seq == seq && ed.dup_n) {
        ed_wire(ed.dup, ed.dup_n);         /* a retry of the write just done: its reply again, not done twice */
        return;
    }
    ed.last = 1;                           /* (INFO: it replaces, so forgets, the last write) */
    ed.last_cmd = (uint8_t)cmd;
    ed.last_seq = (uint8_t)seq;
    ed.dup_n = 0;
    ed.write = (uint8_t)ed_is_write(cmd);
    ed_dispatch(cmd, f + ED_MIN_FRAME, n - ED_MIN_FRAME);
    ed.write = 0;
}

/* ------------------------------------------------------------------ pushes --- */
static int ed_ring_ok(void) { return so_w - so_r + 8u <= SXQ / 2u; }   /* a push never makes a reply wait */
static void ed_sync(uint32_t now)
{
    uint8_t st[ED_STATE_N];
    uint32_t i, sent = 0;
    if (!ed.on)
        return;
    if (now - ed.rx_ms > ED_WATCH_MS || !usb.config) {
        ed.on = 0;                         /* 3 s without a request, or no host */
        return;
    }
    if (now - ed.scan_ms < ED_SCAN_MS || !ed_ring_ok())
        return;
    ed.scan_ms = now;
    ed_state_get(st);
    if (memcmp(st, ed.st, sizeof st)) {
        if (st[5] != ed.st[5])
            ed_shadow_track();             /* another selection: the page reads its knobs (DUMP) */
        memcpy(ed.st, st, sizeof st);
        ed_push(ED_P_STATE);
        for (i = 0; i < ED_STATE_N; i++)
            ed_b(st[i]);
        ed_send();
        sent++;
    }
    for (i = 0; i < ED_NV && sent < ED_PUSH_MAX; i++) {   /* round robin: nothing starves */
        uint32_t j = (ed.pos + i) % ED_NV, g = j >= P_COUNT, id = g ? j - P_COUNT : j;
        ed_knob_t x;
        int32_t v = g ? song.g[id] : motion_rest(song.sel, id);
        if (v == ed.v[j])
            continue;
        if (!ed_ring_ok())
            return;
        ed.v[j] = (int16_t)v;
        x.t = g ? 0 : TSEL;
        x.d = g ? &GP[id] : track_desc(TSEL, id);
        ed_push(ED_P_CHANGED);
        ed_b(g ? ED_SC_GLOBAL : ED_SC_TRACK);
        ed_b(g ? 0u : song.sel);
        ed_u14(id);
        ed_value_text(&x, v);
        ed_send();
        sent++;
        ed.pos = (uint8_t)((j + 1u) % ED_NV);
    }
#ifdef ED_HAS_PATTERN
    if (sent < ED_PUSH_MAX && ed_ring_ok())
        ed_pattern_push(ED_PUSH_MAX - sent);
#endif
}

/* ------------------------------------------------------- the main loop's --- */
static void ed_hush(uint32_t now)          /* update traffic: silent ED_SILENCE_MS from now */
{
    if (!ed.hush) {
        ed.on = 0;
#ifdef ED_HAS_BACKUP
        ed_backup_drop();                  /* an uncommitted restore: dropped, its RAM homes reloaded */
#endif
    }
    ed.hush = 1;
    ed.hush_ms = now;
}
/* main.c: every main-loop pass before ota_service(), and in its 15 ms wait. FELUCCA_BLE: the BT stack's slice first */
static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n, now;
#if FELUCCA_BLE
    bt_service();                          /* bt_page.c: the BT stack's slice, then the core's Bluetooth work */
#endif
    now = fm1_ms;
    if (ed.hush && now - ed.call_ms > ED_STALL_MS)
        ed.hush_ms = now;                  /* the main loop was held (an update session): silent from now */
    ed.call_ms = now;
    if (ed.hush && now - ed.hush_ms >= ED_SILENCE_MS)
        ed.hush = 0;
    if (usb.ota_req)
        ed_hush(now);                      /* the upgrade command (main.c takes it after this) */
    if (ota_frame_get(&p, &n)) {
        if (p[0] != ED_H0) {
            ed_hush(now);                  /* update traffic: left for ota_take() */
        } else if (n >= ED_HDR_N && p[1] == ED_H1 && p[2] == ED_H2) {
            if (ed.hush || n < ED_MIN_FRAME) {
                ota_frame_done();          /* silent, or too short: freed unanswered */
            } else if (n <= sizeof ed.req) {
                memcpy(ed.req, p, n);
                ota_frame_done();          /* the slot free while the reply goes out */
                ed_handle(ed.req, n, now);
            } else {
                ed_handle(p, n, now);      /* a long one read in place */
                ota_frame_done();
            }
        }                                  /* (another 7D frame: not ours, ota_take frees it) */
    }
    if (ed.hush)
        return;
    if (ed.last && now - ed.rx_ms > ED_DUP_MS)
        ed.last = 0;                       /* the duplicate memory forgets */
#ifdef ED_HAS_BACKUP
    ed_backup_poll();
#endif
    ed_sync(now);
}
