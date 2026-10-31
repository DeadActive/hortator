/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web editor's core (firmware/src/editor.c, Hortator editor protocol 1: web/EDITOR_PROTOCOL.md) on the host, with
 * the firmware's real USB SysEx path (usb_app8.c sysex_byte -> sx_frame, ota_wire_send -> the SysEx out ring), project
 * code and a host flash (tests/flash_host.h), built with ASan + UBSan: every core command's reply and refusals, the
 * duplicate rule, the pushes, the update-silence rule (an update frame arriving while an editor frame waits is not
 * lost on its retry; then the editor is silent 10 s), MIDI clock out not paused by editor frames. */
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 1                                 /* (usb_app8.c's SysEx frame and out ring; ui_host.h sets 0 after) */
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#include "ui_host.h"
#undef FELUCCA_OTA
#define FELUCCA_OTA 1

/* the USB host: takes what the SysEx out ring holds (ota_wire_send waits in ota_idle when it is full) */
static uint8_t wire[65536];
static uint32_t nwire;
static void usb_host_take(void)
{
    while (so_r != so_w) {
        uint32_t pkt = sx_out_q[so_r % SXQ], cin = pkt & 15u, k = cin == 4u || cin == 7u ? 3u : cin - 4u, i;
        for (i = 0; i < k && nwire < sizeof wire; i++)
            wire[nwire++] = (uint8_t)(pkt >> (8u * (i + 1u)));
        so_r++;
    }
}
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { usb_host_take(); }
#include "../firmware/src/editor_proto.h"
#include "../firmware/src/editor.c"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

/* ---- frames */
typedef struct { uint8_t b[512]; uint32_t n; } frame_t;
static frame_t out[64];                                /* the frames of the last pass(es), in order */
static uint32_t nout;
static FILE *dump;                                     /* editor_test FILE: every frame out, hex, a line each
                                                        * (tests/editor_wire.mjs decodes them with the page's codec) */
static void frames_take(void)                          /* the wire bytes -> whole frames */
{
    uint32_t i, s = 0;
    usb_host_take();
    nout = 0;
    for (i = 0; i < nwire; i++) {
        if (wire[i] == 0xF0)
            s = i;
        if (wire[i] == 0xF7 && nout < 64u) {
            out[nout].n = i - s + 1u;
            memcpy(out[nout].b, wire + s, out[nout].n);
            if (dump) {
                uint32_t j;
                for (j = 0; j < out[nout].n; j++)
                    fprintf(dump, "%02x", out[nout].b[j]);
                fputc('\n', dump);
            }
            nout++;
        }
    }
    nwire = 0;
}
static void sx_in(const uint8_t *f, uint32_t n)        /* a frame from the host, through the driver's SysEx path */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        sysex_byte(f[i]);
}
static uint8_t rq[700];
static uint32_t nrq;
static void rq_begin(uint32_t cmd, uint32_t seq)
{
    rq[0] = 0xF0;
    rq[1] = 0x7D;
    rq[2] = 0x48;
    rq[3] = 0x54;
    rq[4] = (uint8_t)cmd;
    rq[5] = (uint8_t)seq;
    nrq = 6;
}
static void rq_b(uint32_t v) { rq[nrq++] = (uint8_t)(v & 0x7Fu); }
static void rq_u14(uint32_t v)
{
    rq_b(v);
    rq_b(v >> 7);
}
static void rq_v14(int32_t v) { rq_u14((uint32_t)(v + 8192)); }
static void rq_end(void) { rq[nrq++] = 0xF7; }
static void pass(void)                                 /* one main-loop pass: the editor, then what the host got */
{
    ed_service();
    frames_take();
}
static const frame_t *reply(void)                      /* the pass' reply (seq != 7F), 0 none */
{
    uint32_t i;
    for (i = 0; i < nout; i++)
        if (out[i].n > 6u && out[i].b[5] != 0x7Fu)
            return &out[i];
    return 0;
}
static uint32_t pushes(uint32_t cmd)                   /* pushes of cmd (0: any) in the pass */
{
    uint32_t i, n = 0;
    for (i = 0; i < nout; i++)
        n += out[i].n > 6u && out[i].b[5] == 0x7Fu && (!cmd || out[i].b[4] == cmd);
    return n;
}
static void idle(uint32_t ms)                          /* the main loop running, ms long (a pass every 5 ms) */
{
    uint32_t t;
    for (t = 0; t < ms; t += 5u) {
        fm1_ms += 5u;
        pass();
    }
}
static uint8_t seqn;
static const frame_t *ask(void)                        /* rq as built: sent, one pass, its reply */
{
    rq_end();
    sx_in(rq, nrq);
    pass();
    return reply();
}
static const frame_t *req0(uint32_t cmd)
{
    rq_begin(cmd, seqn++ % 127u);
    return ask();
}
static const frame_t *req1(uint32_t cmd, uint32_t a)
{
    rq_begin(cmd, seqn++ % 127u);
    rq_b(a);
    return ask();
}
static const frame_t *req_knob(uint32_t cmd, uint32_t scope, uint32_t track, uint32_t id, int32_t last)
{
    rq_begin(cmd, seqn++ % 127u);
    rq_b(scope);
    rq_b(track);
    rq_u14(id);
    if (cmd == ED_SET || cmd == ED_TEXT)
        rq_v14(last);
    else
        rq_b((uint32_t)last);
    return ask();
}
/* reading a reply */
typedef struct { const uint8_t *b; uint32_t i, n, bad; } rd_t;
static rd_t rd_of(const frame_t *f, uint32_t at) { rd_t r = {f ? f->b : 0, at, f ? f->n - 1u : 0, !f}; return r; }
static uint32_t rd_b(rd_t *r) { return r->i < r->n ? r->b[r->i++] : (r->bad = 1, 0u); }
static uint32_t rd_u14(rd_t *r) { uint32_t lo = rd_b(r); return lo | rd_b(r) << 7; }
static int32_t rd_v14(rd_t *r) { return (int32_t)rd_u14(r) - 8192; }
static void rd_str(rd_t *r, char *s, uint32_t max)
{
    uint32_t k = 0, c;
    while ((c = rd_b(r)) != 0 && !r->bad)
        if (k + 1u < max)
            s[k++] = (char)c;
    s[k] = 0;
}
static int rc_is(const frame_t *f, uint32_t rc, uint32_t detail)
{
    return f && f->n >= 9u && f->b[6] == rc && (rc == 0u || f->b[7] == detail);
}
static int ends_ok(const frame_t *f, rd_t *r) { return f && !r->bad && r->i == f->n - 1u && f->b[f->n - 1u] == 0xF7; }
static int all_short(void)                             /* every frame out <= ED_REPLY_MAX */
{
    uint32_t i;
    for (i = 0; i < nout; i++)
        if (out[i].n > ED_REPLY_MAX)
            return 0;
    return 1;
}

/* ---- the device */
static void ed_reset(void)                            /* power-on: booted, the editor idle, USB configured */
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&ui, 0, sizeof ui);
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(lk_slot, 0, sizeof lk_slot);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    var_play = 0;
    var_dirty = 0;
    persist_boot();
    settings_init();
    memset(&ed, 0, sizeof ed);
    memset(&mout, 0, sizeof mout);
    usb.config = 1;
    usb.ota_req = 0;
    usb.sx_on = 0;
    sx_ready = sx_collect = 0;
    so_r = so_w = 0;
    nwire = nout = 0;
    fm1_ms = 100000u;
}

/* ---- commands */
static void t_info(void)
{
    const frame_t *f;
    rd_t r;
    char magic[16], fw[16];
    uint32_t v[16], i;
    ed_reset();
    f = req0(ED_INFO);
    r = rd_of(f, 7);
    rd_str(&r, magic, sizeof magic);
    rd_str(&r, fw, sizeof fw);
    v[0] = rd_b(&r);                                   /* major, minor, caps */
    v[1] = rd_b(&r);
    v[2] = rd_u14(&r);
    v[3] = rd_b(&r);                                   /* ntrk nstep pcount gcount nmodels */
    v[4] = rd_b(&r);
    v[5] = rd_b(&r);
    v[6] = rd_b(&r);
    v[7] = rd_b(&r);
    v[8] = rd_u14(&r);                                 /* nfact nuser */
    v[9] = rd_u14(&r);
    v[10] = rd_b(&r);                                  /* nbanks nslot nvar maxreq ble */
    v[11] = rd_b(&r);
    v[12] = rd_b(&r);
    v[13] = rd_u14(&r);
    v[14] = rd_b(&r);
    for (i = 0; i < ED_STATE_N; i++)
        rd_b(&r);
    check("INFO: HORTATOR, the firmware version without DRUM-, protocol 1.1, LIVE only (no backup / pattern files), the "
          "sizes, the state block",
          rc_is(f, 0, 0) && ends_ok(f, &r) && !strcmp(magic, "HORTATOR") && !strcmp(fw, "DEV") && v[0] == 1u &&
              v[1] == ED_PROTO_MINOR && ED_PROTO_MINOR == 1u && v[2] == ED_CAP_LIVE && v[3] == 8u && v[4] == 64u && v[5] == P_COUNT && v[6] == G_COUNT &&
              v[7] == NMODELS && v[8] == 151u && v[9] == 128u && v[10] == 3u && v[11] == 4u && v[12] == 8u &&
              v[13] == 640u && v[14] == 0u && f->b[f->n - 1u - ED_STATE_N + 1u] == ED_NONE &&
              f->b[f->n - 1u - ED_STATE_N + 5u] == song.sel);
    rq_begin(ED_INFO, 3);
    rq_b(0);
    f = ask();
    check("INFO with an argument: rc 1 detail 44, the same seq", rc_is(f, 1, ED_D_LENGTH) && f->b[5] == 3u);
}
static void t_pages(void)
{
    const frame_t *f;
    rd_t r;
    uint32_t total, first, n, i, c, seen = 0, ok = 1, dest = 0, sound = 0, grids_len = 0, banned = 0;
    ed_reset();
    song.g[G_GMODE] = 1;                               /* EUCLID: GRIDS shows LEN K / S / H */
    for (first = 0;;) {
        f = req1(ED_PAGES, first);
        r = rd_of(f, 7);
        total = rd_b(&r);
        ok &= rd_b(&r) == first && all_short();
        n = rd_b(&r);
        for (i = 0; i < n; i++) {
            char title[16];
            uint32_t scope, graph, ids[4];
            rd_str(&r, title, sizeof title);
            scope = rd_b(&r);
            graph = rd_b(&r);
            for (c = 0; c < 4u; c++)
                ids[c] = rd_u14(&r);
            if (!strncmp(title, "DEST ", 5)) {
                uint32_t s = (uint32_t)(title[5] - '1');
                dest++;
                ok &= s < 4u && ids[0] == LS_ID[s][0] && ids[1] == LS_ID[s][1] && ids[2] == LS_ID[s][2] &&
                      ids[3] == ED_U14_NONE && graph == GR_LDEST && scope == 0u;
            }
            if (!strcmp(title, "SOUND") && !sound++)
                ok &= ids[0] == P_MODEL && ids[1] == P_E0 && ids[2] == P_E1 && ids[3] == P_E2 && graph == GR_MODEL;
            if (graph == GR_GRIDS && ids[1] == G_GLEN1 && ids[2] == G_GLEN2 && ids[3] == G_GLEN3)
                grids_len = 1;
            banned |= !strcmp(title, "PROJECT") || !strcmp(title, "TOOLS") || !strcmp(title, "MOTION") ||
                      !strcmp(title, "SONG") || !strcmp(title, "STEP") || !strcmp(title, "TRACKS") ||
                      !strcmp(title, "COPY") || graph == GR_SLOTS;
        }
        ok &= ends_ok(f, &r) && rc_is(f, 0, 0);
        seen += n;
        if (!n || first + n >= total)
            break;
        first += n;
    }
    check("PAGES: the device's pages for the selected track, cut to fit 400 B and read on from `first`; SOUND = MODEL "
          "E0.., DEST 1..4 = the LFO slots, GRIDS in EUCLID = LEN K / S / H; no PROJECT / TOOLS / MOTION / SONG / STEP",
          ok && seen == total && total > 20u && dest == 4u && sound >= 2u && grids_len && !banned && first > 0u);
}
static void t_desc(void)
{
    const frame_t *f;
    rd_t r;
    char label[16], unit[16], nm[16];
    uint32_t fmt, flags, nn, cnt, i, ok;
    int32_t mn, mx, df;
    ed_reset();
    f = req_knob(ED_DESC, ED_SC_TRACK, 0x7F, P_MODEL, 0);
    r = rd_of(f, 7);
    ok = rd_b(&r) == 0 && rd_b(&r) == 0x7Fu && rd_u14(&r) == P_MODEL;
    fmt = rd_b(&r);
    flags = rd_b(&r);
    mn = rd_v14(&r);
    mx = rd_v14(&r);
    df = rd_v14(&r);
    rd_str(&r, label, sizeof label);
    rd_str(&r, unit, sizeof unit);
    nn = rd_b(&r);
    ok &= rd_b(&r) == 0u;
    cnt = rd_b(&r);
    for (i = 0; i < cnt; i++) {
        rd_str(&r, nm, sizeof nm);
        ok &= !strcmp(nm, N_MODEL[i]);
    }
    check("DESC MODEL: ENUM, ORDER, min 0 max 44, label MODEL, all 45 names in the model ids' order",
          ok && ends_ok(f, &r) && fmt == F_ENUM && (flags & ED_DF_ORDER) && mn == 0 && mx == NMODELS - 1 && df == 0 &&
              !strcmp(label, "MODEL") && nn == NMODELS && cnt == NMODELS);
    f = req_knob(ED_DESC, ED_SC_TRACK, 0x7F, P_MODEL, ED_DESC_ORDER);
    r = rd_of(f, 7 + 4 + 2 + 6);
    rd_str(&r, label, sizeof label);
    rd_str(&r, unit, sizeof unit);
    rd_b(&r);
    ok = rd_b(&r) == ED_DESC_ORDER;
    cnt = rd_b(&r);
    for (i = 0; i < cnt; i++)
        ok &= rd_b(&r) == MODEL_LIST[i];
    check("DESC MODEL, first 7F: the MODEL knob's shown order (ui.c MODEL_LIST)", ok && cnt == NMODELS && ends_ok(f, &r));
    f = req_knob(ED_DESC, ED_SC_GLOBAL, 0, G_MIDI, 0);
    check("DESC G_MIDI: RO", rc_is(f, 0, 0) && (f->b[12] & ED_DF_RO));
    f = req_knob(ED_DESC, ED_SC_GLOBAL, 0, G_DTIME, 0);
    check("DESC the delay's TIME: ORDER", rc_is(f, 0, 0) && (f->b[12] & ED_DF_ORDER));
    f = req_knob(ED_DESC, ED_SC_TRACK, 2, P_LFO1 + LF_DEST, 0);
    r = rd_of(f, 7 + 4);
    fmt = rd_b(&r);
    flags = rd_b(&r);
    rd_v14(&r), rd_v14(&r), rd_v14(&r);
    rd_str(&r, label, sizeof label);
    rd_str(&r, unit, sizeof unit);
    nn = rd_b(&r);
    rd_b(&r);
    cnt = rd_b(&r);
    rd_str(&r, nm, sizeof nm);
    check("DESC an LFO DEST: LDEST, CONTEXT, 19 names from OFF", fmt == F_LDEST && (flags & ED_DF_CONTEXT) && nn == 19u &&
                                                                  cnt == 19u && !strcmp(nm, "OFF"));
    f = req_knob(ED_DESC, ED_SC_TRACK, 0, P_E0, 0);
    check("DESC a SOUND knob: CONTEXT and LOCK", rc_is(f, 0, 0) && (f->b[12] & ED_DF_CONTEXT) && (f->b[12] & ED_DF_LOCK));
    f = req_knob(ED_DESC, ED_SC_MODEL, DM_ACID, 0, 0);
    r = rd_of(f, 7 + 4 + 2 + 6);
    rd_str(&r, label, sizeof label);
    check("DESC scope MODEL: a model's SOUND knob, independent of the tracks",
          rc_is(f, 0, 0) && !strcmp(label, DMODELS[DM_ACID].edit[0].label));
    f = req_knob(ED_DESC, ED_SC_TRACK, 8, P_LEVEL, 0);
    ok = rc_is(f, 1, ED_D_RANGE);
    f = req_knob(ED_DESC, ED_SC_TRACK, 0, P_COUNT, 0);
    ok &= rc_is(f, 1, ED_D_RANGE);
    f = req_knob(ED_DESC, ED_SC_GLOBAL, 0, G_COUNT, 0);
    ok &= rc_is(f, 1, ED_D_RANGE);
    f = req_knob(ED_DESC, ED_SC_MODEL, NMODELS, 0, 0);
    ok &= rc_is(f, 1, ED_D_RANGE);
    f = req_knob(ED_DESC, 3, 0, 0, 0);
    ok &= rc_is(f, 1, ED_D_RANGE);
    rq_begin(ED_DESC, 9);
    rq_b(0);
    f = ask();
    ok &= rc_is(f, 1, ED_D_LENGTH);
    check("DESC refusals: track 8, id past P_COUNT / G_COUNT, model 45, scope 3 (rc 1 detail 45); short (detail 44)", ok);
}
static void t_get_set(void)
{
    const frame_t *f;
    rd_t r;
    char txt[16], unit[16];
    uint32_t cnt, ok, i;
    int32_t v;
    ed_reset();
    f = req_knob(ED_GET, ED_SC_GLOBAL, 0, G_BPM, 1);
    r = rd_of(f, 7 + 4);
    cnt = rd_b(&r);
    v = rd_v14(&r);
    rd_str(&r, txt, sizeof txt);
    rd_str(&r, unit, sizeof unit);
    check("GET BPM: its value, the device's text and unit", cnt == 1u && v == song.g[G_BPM] && ends_ok(f, &r) &&
                                                             atoi(txt) == song.g[G_BPM] && !strcmp(unit, "BPM"));
    f = req_knob(ED_GET, ED_SC_TRACK, 3, 0, 127);
    r = rd_of(f, 7 + 4);
    cnt = rd_b(&r);
    ok = cnt > 10u && cnt < P_COUNT && f->n <= ED_REPLY_MAX;
    for (i = 0; i < cnt; i++) {
        ok &= rd_v14(&r) == trk[3].p[i];
        rd_str(&r, txt, sizeof txt);
        rd_str(&r, unit, sizeof unit);
    }
    check("GET 127 values from P_MODEL of track 4: cut where the reply is full, each the track's own", ok && ends_ok(f, &r));
    f = req_knob(ED_GET, ED_SC_TRACK, 0, P_COUNT - 2u, 9);
    check("GET stops at P_COUNT", rc_is(f, 0, 0) && f->b[11] == 2u);
    ok = rc_is(req_knob(ED_GET, ED_SC_TRACK, 0, P_LEVEL, 0), 1, ED_D_RANGE);
    ok &= rc_is(req_knob(ED_GET, ED_SC_MODEL, 0, 0, 1), 1, ED_D_RANGE);
    check("GET refusals: count 0, scope MODEL", ok);
    /* another track's LFO RATE: its own RATE MODE's text, not the selected track's */
    trk[5].p[P_LFO1 + LF_MODE] = LM_HZ;
    TSEL->p[P_LFO1 + LF_MODE] = LM_SYNC;
    f = req_knob(ED_GET, ED_SC_TRACK, 5, P_LFO1 + LF_RATE, 1);
    r = rd_of(f, 7 + 5);
    rd_v14(&r);
    rd_str(&r, txt, sizeof txt);
    rd_str(&r, unit, sizeof unit);
    check("GET an LFO RATE of another track: that track's RATE MODE (Hz)", !strcmp(unit, "Hz"));

    f = req_knob(ED_SET, ED_SC_TRACK, 2, P_LEVEL, 50);
    r = rd_of(f, 7 + 4);
    v = rd_v14(&r);
    rd_str(&r, txt, sizeof txt);
    rd_str(&r, unit, sizeof unit);
    check("SET LEVEL of track 3: set, the reply the value kept and its text", rc_is(f, 0, 0) && trk[2].p[P_LEVEL] == 50 &&
                                                                               v == 50 && ends_ok(f, &r) && unit[0]);
    f = req_knob(ED_SET, ED_SC_TRACK, 2, P_PAN, 500);
    r = rd_of(f, 7 + 4);
    check("SET clamps to the knob's range", rd_v14(&r) == TP[P_PAN].max && trk[2].p[P_PAN] == TP[P_PAN].max);
    trk[1].pre_ed = 0;
    req_knob(ED_SET, ED_SC_TRACK, 1, P_E1, trk[1].p[P_E1] == 10 ? 11 : 10);
    check("SET a SOUND knob: the track's preset marked edited", trk[1].pre_ed == 1u);
    {
        int16_t m = TSEL->p[P_MODEL];
        f = req_knob(ED_SET, ED_SC_TRACK, 0x7F, P_MODEL, m == DM_K909 ? DM_K808 : DM_K909);
        check("SET MODEL: the model with its default sound (drum_set_model)",
              rc_is(f, 0, 0) && TSEL->p[P_MODEL] == (m == DM_K909 ? DM_K808 : DM_K909) && TSEL->model == TSEL->p[P_MODEL]);
    }
    trk[0].p[P_MODEL] = DM_ACID;
    trk[1].p[P_MODEL] = DM_ACID;
    trk[2].p[P_MODEL] = DM_K808;
    f = req_knob(ED_SET, ED_SC_TRACK, 2, P_MODEL, DM_ACID);
    check("SET MODEL ACID onto a third track: rc 2 detail 18, unchanged", rc_is(f, 2, ED_D_ACID_CAP) &&
                                                                            trk[2].p[P_MODEL] == DM_K808);
    trk[0].p[P_MODEL] = trk[1].p[P_MODEL] = DM_K808;
    v = song.g[G_MIDI];
    f = req_knob(ED_SET, ED_SC_GLOBAL, 0, G_MIDI, v + 1);
    check("SET a read-only global: rc 1 detail 45", rc_is(f, 1, ED_D_RANGE) && song.g[G_MIDI] == v);
    f = req_knob(ED_SET, ED_SC_GLOBAL, 0, G_SWING, 33);
    check("SET a global", rc_is(f, 0, 0) && song.g[G_SWING] == 33);
    song.g[G_CLOCK] = 1;
    v = song.g[G_BPM];
    f = req_knob(ED_SET, ED_SC_GLOBAL, 0, G_BPM, v + 7);
    r = rd_of(f, 7 + 4);
    check("SET BPM while following a clock: kept as it is (the knob says CLK)", rc_is(f, 0, 0) && song.g[G_BPM] == v &&
                                                                                 rd_v14(&r) == v);
    song.g[G_CLOCK] = 0;
    ok = rc_is(req_knob(ED_SET, ED_SC_MODEL, 0, 0, 1), 1, ED_D_RANGE);
    ok &= rc_is(req_knob(ED_SET, ED_SC_TRACK, 9, P_LEVEL, 1), 1, ED_D_RANGE);
    rq_begin(ED_SET, 1);
    rq_b(0);
    rq_b(0);
    rq_u14(P_LEVEL);
    ok &= rc_is(ask(), 1, ED_D_LENGTH);
    check("SET refusals: scope MODEL, track 9 (detail 45), no value (detail 44)", ok);
    /* motion plays the knob: SET moves its base, GET reads the base (never the modulation) */
    mo.base_ok = 1u << 4;
    memcpy(mo.base[4], trk[4].p, sizeof mo.base[4]);
    motion_mark(4, P_FCUT);
    trk[4].p[P_FCUT] = 99;                             /* motion's value of the moment */
    mo.base[4][P_FCUT] = 40;
    f = req_knob(ED_GET, ED_SC_TRACK, 4, P_FCUT, 1);
    r = rd_of(f, 7 + 5);
    ok = rd_v14(&r) == 40;
    req_knob(ED_SET, ED_SC_TRACK, 4, P_FCUT, 60);
    ok &= mo.base[4][P_FCUT] == 60;
    check("motion playing a knob: GET is the knob as set (its base), SET moves the base", ok);
    mo.base_ok = 0;
    v = trk[6].p[P_DIST];
    f = req_knob(ED_TEXT, ED_SC_TRACK, 6, P_DIST, 127);
    r = rd_of(f, 7 + 4);
    check("TEXT: a value's text, nothing set", rc_is(f, 0, 0) && rd_v14(&r) == 127 && trk[6].p[P_DIST] == v);
}
static void t_dump_select(void)
{
    const frame_t *f;
    rd_t r;
    uint32_t ok, i, n;
    ed_reset();
    trk[3].p[P_LEVEL] = 77;
    trk[3].pre = 5;
    trk[3].pre_ed = 1;
    f = req1(ED_SELECT, 3);
    ok = rc_is(f, 0, 0) && f->b[7] == 3u && song.sel == 3u;
    f = req1(ED_SELECT, 0x7F);
    ok &= rc_is(f, 0, 0) && f->b[7] == 3u && song.sel == 3u;
    check("SELECT: as the track keys (one selection); 7F asks", ok);
    check("SELECT 8: rc 1 detail 45", rc_is(req1(ED_SELECT, 8), 1, ED_D_RANGE) && song.sel == 3u);
    f = req1(ED_DUMP, 0x7F);
    r = rd_of(f, 7);
    ok = rd_b(&r) == 3u && rd_b(&r) == (uint32_t)trk[3].p[P_MODEL] && rd_u14(&r) == 5u && rd_b(&r) == 1u;
    n = rd_b(&r);
    ok &= n == P_COUNT;
    for (i = 0; i < n; i++)
        ok &= rd_v14(&r) == trk[3].p[i];
    n = rd_b(&r);
    ok &= n == G_COUNT;
    for (i = 0; i < n; i++)
        ok &= rd_v14(&r) == song.g[i];
    check("DUMP 7F: the selected track's model, preset, edited, its pcount values, then the gcount globals",
          ok && ends_ok(f, &r));
    trk[2].pre = PRE_NONE;
    f = req1(ED_DUMP, 2);
    r = rd_of(f, 9);
    check("DUMP of a track with no preset: 3FFF", rc_is(f, 0, 0) && rd_u14(&r) == ED_U14_NONE);
    check("DUMP 8: rc 1 detail 45", rc_is(req1(ED_DUMP, 8), 1, ED_D_RANGE));
}
static void t_framing(void)
{
    const frame_t *f;
    uint32_t ok;
    static const uint8_t SHORT[] = {0xF0, 0x7D, 0x48, 0x54, 0x01, 0xF7};
    ed_reset();
    f = req0(0x33);
    ok = rc_is(f, 1, ED_D_UNKNOWN);
    f = req0(0);
    ok &= rc_is(f, 1, ED_D_UNKNOWN);
    check("an unknown command: rc 1 detail 43", ok);
    ok = 1;
    for (uint32_t c = ED_NAMES; c <= ED_RESTORE; c++)
        ok &= rc_is(req0(c), 1, ED_D_NO_CAP);
    check("NAMES .. RESTORE without editor_pattern.c / editor_backup.c: rc 1 detail 46", ok);
    rq_begin(ED_INFO, 0x7F);
    check("a request with the push seq 7F: unanswered", !ask() && !sx_ready);
    sx_in(SHORT, sizeof SHORT);
    pass();
    check("a frame shorter than header + cmd + seq: unanswered, freed", !nout && !sx_ready);
    {
        static const uint8_t UP[] = {0xF0, 0x7D, 0x46, 0x4C, 0x01, 0xF7};   /* upstream Felucca's editor header */
        sx_in(UP, sizeof UP);
        pass();
        check("another 7D frame (upstream Felucca's): not ours, left for ota_take; the editor not silenced",
              !nout && sx_ready && !ed.hush);
        sx_ready = 0;
    }
    /* a long request (over the 64-byte copy) is read in place and freed */
    rq_begin(ED_INFO, 4);
    for (uint32_t i = 0; i < 100u; i++)
        rq_b(0);
    f = ask();
    check("a long request: answered (rc 1 detail 44 here), read in place and freed", rc_is(f, 1, ED_D_LENGTH) && !sx_ready);
    ok = 1;
    rq_begin(ED_GET, 5);
    rq_b(0);
    rq_b(0);
    rq_u14(0);
    rq_b(1);
    rq_end();
    sx_in(rq, nrq);
    ed_service();
    ok &= !sx_ready;                                  /* freed in the same pass */
    frames_take();
    ok &= reply() && reply()->b[5] == 5u;
    check("a request is freed in the pass that answers it; the reply carries its seq", ok);
}
static void t_dup(void)
{
    const frame_t *f;
    frame_t first;
    uint32_t ok;
    ed_reset();
    rq_begin(ED_SET, 20);
    rq_b(0);
    rq_b(1);
    rq_u14(P_LEVEL);
    rq_v14(50);
    f = ask();
    first = *f;
    trk[1].p[P_LEVEL] = 70;                            /* the device moves it meanwhile */
    nrq--;                                            /* the same frame again (a retry: same seq) */
    f = ask();
    ok = f && f->n == first.n && !memcmp(f->b, first.b, f->n) && trk[1].p[P_LEVEL] == 70;
    check("duplicate rule: a write retried with its seq is answered again, not done twice", ok);
    rq_begin(ED_SET, 21);
    rq_b(0);
    rq_b(1);
    rq_u14(P_LEVEL);
    rq_v14(50);
    ask();
    check("a write with the next seq is done", trk[1].p[P_LEVEL] == 50);
    trk[1].p[P_LEVEL] = 70;
    f = req_knob(ED_GET, ED_SC_TRACK, 1, P_LEVEL, 1);
    rq_begin(ED_SET, 21);
    rq_b(0);
    rq_b(1);
    rq_u14(P_LEVEL);
    rq_v14(50);
    ask();
    check("the memory is the last request: after a read, the same write seq is done again", trk[1].p[P_LEVEL] == 50);
    trk[1].p[P_LEVEL] = 70;
    rq_begin(ED_SET, 22);
    rq_b(0);
    rq_b(1);
    rq_u14(P_LEVEL);
    rq_v14(50);
    ask();
    trk[1].p[P_LEVEL] = 70;
    fm1_ms += ED_DUP_MS + 10u;
    pass();
    nrq--;
    ask();
    check("the memory is forgotten after 3 s without requests", trk[1].p[P_LEVEL] == 50);
    /* a read retried with its seq: answered anew */
    rq_begin(ED_GET, 30);
    rq_b(1);
    rq_b(0);
    rq_u14(G_SWING);
    rq_b(1);
    song.g[G_SWING] = 10;
    f = ask();
    ok = rc_is(f, 0, 0) && f->b[12] == 10u + 0u;
    song.g[G_SWING] = 20;
    nrq--;
    f = ask();
    {
        rd_t r = rd_of(f, 12);
        ok &= rd_v14(&r) == 20;
    }
    check("a read retried with its seq: answered anew", ok);
    /* a failed write's retry: the same refusal */
    rq_begin(ED_SET, 40);
    rq_b(1);
    rq_b(0);
    rq_u14(G_MIDI);
    rq_v14(1);
    f = ask();
    first = *f;
    nrq--;
    f = ask();
    check("a refused write retried: the same refusal", rc_is(f, 1, ED_D_RANGE) && f->n == first.n &&
                                                       !memcmp(f->b, first.b, f->n));
}
static void t_watch(void)
{
    const frame_t *f;
    uint32_t ok, i, n;
    ed_reset();
    song.sel = 2;
    f = req1(ED_WATCH, 1);
    ok = rc_is(f, 0, 0) && f->b[7] == 1u && !pushes(0);
    fm1_ms += 20u;
    pass();
    check("WATCH 1: on; nothing pushed while nothing changes", ok && !nout);
    trk[2].p[P_PAN] = 17;                              /* a knob turned on the device */
    fm1_ms += 20u;
    pass();
    {
        rd_t r = rd_of(&out[0], 6);
        ok = pushes(ED_P_CHANGED) == 1u && nout == 1u && rd_b(&r) == 0u && rd_b(&r) == 2u && rd_u14(&r) == P_PAN &&
             rd_v14(&r) == 17;
    }
    check("CHANGED: the selected track's knob turned on the device (scope, track, id, value, text)", ok);
    req_knob(ED_SET, ED_SC_TRACK, 2, P_PAN, -5);
    fm1_ms += 20u;
    pass();
    check("no CHANGED echoes the editor's own SET", !pushes(ED_P_CHANGED) && trk[2].p[P_PAN] == -5);
    trk[5].p[P_PAN] = 33;                              /* another track: not followed */
    fm1_ms += 20u;
    pass();
    check("a knob of another track: no push", !nout);
    song.g[G_DMIX] = 3;
    fm1_ms += 20u;
    pass();
    check("CHANGED: a global (scope 1)", pushes(ED_P_CHANGED) == 1u && out[0].b[6] == 1u && out[0].b[8] == G_DMIX);
    for (i = 0; i < 10u; i++)
        trk[2].p[P_DIST + i] = (int16_t)(trk[2].p[P_DIST + i] ^ 1);
    for (i = n = 0, ok = 1; i < 8u; i++) {             /* (the host drains the ring between passes only) */
        fm1_ms += 20u;
        pass();
        ok &= nout <= ED_PUSH_MAX;
        n += nout;
    }
    check("at most 4 pushes a pass; the rest follow", ok && n == 10u && all_short());
    song.playing = 1;
    fm1_ms += 20u;
    pass();
    ok = pushes(ED_P_STATE) == 1u && out[0].n == 6u + ED_STATE_N + 1u && out[0].b[6] == 1u;
    song.playing = 0;
    fm1_ms += 20u;
    pass();
    ok &= pushes(ED_P_STATE) == 1u && out[0].b[6] == 0u;
    check("STATE: pushed when PLAY / STOP changes it", ok);
    f = req1(ED_SELECT, 6);
    fm1_ms += 20u;
    pass();
    ok = pushes(ED_P_STATE) == 1u && out[0].b[6 + 5] == 6u && !pushes(ED_P_CHANGED);
    check("SELECT: a STATE push follows (the editor's writes included), no CHANGED for the new track's knobs", ok);
    track_select(1);                                   /* the track keys */
    fm1_ms += 20u;
    pass();
    check("a selection on the device: STATE push", pushes(ED_P_STATE) == 1u && out[0].b[6 + 5] == 1u);
    /* the half-empty ring rule: a push waits while the out ring is more than half full */
    so_w = so_r + SXQ / 2u;
    trk[1].p[P_REV] = (int16_t)(trk[1].p[P_REV] ^ 2);
    fm1_ms += 20u;
    ed_service();
    ok = so_w == so_r + SXQ / 2u;
    so_r = so_w;
    fm1_ms += 20u;
    pass();
    ok &= pushes(ED_P_CHANGED) == 1u;
    check("pushes only into a half-empty SysEx out ring (they go out once it drains)", ok);
    fm1_ms += ED_WATCH_MS + 10u;
    trk[1].p[P_REV] = (int16_t)(trk[1].p[P_REV] ^ 2);
    pass();
    fm1_ms += 20u;
    pass();
    check("WATCH ends 3 s after the last request", !ed.on && !nout);
    req1(ED_WATCH, 1);
    for (i = 0; i < 5u; i++) {                         /* the page's keep-alive, once a second */
        fm1_ms += 1000u;
        req1(ED_WATCH, 1);
    }
    trk[1].p[P_REV] = (int16_t)(trk[1].p[P_REV] ^ 2);
    fm1_ms += 20u;
    pass();
    ok = ed.on && pushes(ED_P_CHANGED) == 1u;
    f = req1(ED_WATCH, 0);
    trk[1].p[P_REV] = (int16_t)(trk[1].p[P_REV] ^ 2);
    fm1_ms += 20u;
    pass();
    check("WATCH kept on by requests; WATCH 0 ends the pushes", ok && rc_is(f, 0, 0) && f->b[7] == 0u && !nout);
}
/* an update handshake as the installer sends it (web/fm1ota.js HS_QUERY): pack7(00 59 11 00 00 00 FF) */
static const uint8_t UPD[] = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};
static void t_silence(void)
{
    uint32_t ok, c;
    const frame_t *f;
    ed_reset();
    {   /* other SysEx is not update traffic: a DAW's identity request, another maker's frame, a near miss */
        static const uint8_t OTHER[][10] = {
            {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7},                         /* universal identity request */
            {0xF0, 0x7F, 0x7F, 0x04, 0x01, 0x00, 0x40, 0xF7},             /* universal realtime (master volume) */
            {0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7},       /* another maker's (00 20 29) */
            {0xF0, 0x00, 0x32, 0x44, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7}, /* 00 32, byte 2's low bits 00: decodes to 00 19 */
            {0xF0, 0x00, 0x32, 0xF7},                                     /* too short to be one */
            {0xF0, 0x01, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7}, /* byte 0 decodes to 01 */
        };
        static const uint8_t NOT_OURS[] = {6, 8, 9, 10, 4, 10};
        ok = 1;
        req1(ED_WATCH, 1);
        for (c = 0; c < sizeof NOT_OURS; c++) {
            sx_in(OTHER[c], NOT_OURS[c]);
            pass();
            ok &= !nout && sx_ready && !ed.hush && ed.on;   /* left for ota_take (it frees them), no silence */
            ota_frame_done();
        }
        f = req0(ED_INFO);
        check("other SysEx (F0 7E identity request, F0 7F, another maker's 00 20 29, near misses of the update's header): "
              "not update traffic, left for ota_take, the editor not silenced, WATCH kept, the next request answered",
              ok && rc_is(f, 0, 0));
        ok = 1;
        for (c = 0; c < 32u; c++) {                     /* any update command: 00 32 (01 | cmd << 2) */
            uint8_t u[sizeof UPD];
            memcpy(u, UPD, sizeof u);
            u[3] = (uint8_t)(1u | c << 2);
            ed.hush = 0;
            sx_in(u, sizeof u);
            pass();
            ok &= ed.hush && !nout && sx_ready;
            ota_frame_done();
        }
        ed.hush = 0;
        for (c = 0; c < 1u << 21; c++) {                /* every 3-byte start: ota.c ota_unpack7's first two bytes */
            const uint8_t w[3] = {(uint8_t)(c & 127u), (uint8_t)(c >> 7 & 127u), (uint8_t)(c >> 14)};
            uint32_t acc = w[0] | (uint32_t)w[1] << 7 | (uint32_t)w[2] << 14;
            ok &= ((acc & 0xFFu) == 0x00u && (acc >> 8 & 0xFFu) == 0x59u) == ed_ota_frame(w, 3);
        }
        check("every frame ota_take could accept (wire 00 32 xx, xx & 3 = 1: decoded 00 59 cmd) silences the editor; "
              "of all 2^21 three-byte starts, exactly those that decode to 00 59", ok);
        ed_reset();
    }
    req1(ED_WATCH, 1);
    rq_begin(ED_INFO, 50);                             /* an editor frame waits in the slot (not serviced yet) */
    rq_end();
    sx_in(rq, nrq);
    sx_in(UPD, sizeof UPD);                            /* the update's frame arrives: the slot is full, dropped */
    ok = sx_ready && sx_frame[0] == 0x7Du;
    pass();
    ok &= reply() && reply()->b[5] == 50u && !sx_ready;   /* the editor's answered and freed */
    sx_in(UPD, sizeof UPD);                            /* the update host's retry */
    ok &= sx_ready && sx_frame[0] == 0x00u && sx_frame_len == sizeof UPD - 2u;
    pass();
    ok &= sx_ready && sx_frame[0] == 0x00u && !nout && ed.hush && !ed.on;   /* left for ota_take */
    check("an update frame arriving while an editor frame waits: dropped by the driver, taken on its retry and left "
          "for ota_take; the editor goes silent, WATCH off", ok);
    ota_frame_done();                                  /* (ota_take) */
    trk[song.sel].p[P_PAN] = 9;
    idle(5000u);
    f = req0(ED_INFO);
    ok = !f && !nout && !sx_ready;
    check("silent: an editor frame within 10 s is freed unanswered, no pushes", ok);
    idle(5100u);
    f = req0(ED_INFO);
    check("10 s after the last update frame: answered again", rc_is(f, 0, 0));
    usb.ota_req = 1;                                   /* the upgrade command */
    pass();
    usb.ota_req = 0;
    f = req0(ED_INFO);
    check("the upgrade command (usb.ota_req): silent too", !f && ed.hush);
    fm1_ms += 3000u;                                   /* main.c ran ota_session: no ed_service for 3 s */
    pass();
    idle(9000u);
    f = req0(ED_INFO);
    ok = !f;
    idle(1100u);
    f = req0(ED_INFO);
    check("the main loop held (an update session): silent 10 s from its end", ok && rc_is(f, 0, 0));
}
/* MIDI clock out (midi_out.c mout_paused): editor frames do not pause it, update traffic does */
static void t_clock_pause(void)
{
    uint32_t ok;
    ed_reset();
    rq_begin(ED_INFO, 1);
    rq_end();
    sx_in(rq, 4);                                      /* an editor frame arriving */
    ok = !mout_paused();
    sx_in(rq + 4, nrq - 4u);                           /* .. waiting */
    ok &= !mout_paused();
    ed_service();                                      /* its reply in the out ring */
    ok &= so_w != so_r && !mout_paused();
    frames_take();
    check("editor frames (arriving, waiting, its reply going out) do not pause MIDI clock out", ok);
    sx_in(UPD, 3);
    ok = mout_paused();
    sx_in(UPD + 3, sizeof UPD - 3u);
    ok &= mout_paused();
    sx_ready = 0;
    fm1_ms += 2000u;
    ok &= mout_paused();
    fm1_ms += 1100u;
    ok &= !mout_paused();
    usb.ota_req = 1;
    ok &= mout_paused();
    usb.ota_req = 0;
    {
        static const uint8_t ID[3] = {0xF0, 0x00, 0xF7};
        ok &= ota_wire_send(ID, 3) == 0 && mout_paused();   /* an update's reply (ota.c) going out */
        frames_take();
    }
    check("update traffic (its frame, the upgrade command, an ota reply) still pauses it, 3 s after", ok);
}

int main(int argc, char **argv)
{
    if (argc > 1 && !(dump = fopen(argv[1], "w")))
        return 1;
    t_info();
    t_pages();
    t_desc();
    t_get_set();
    t_dump_select();
    t_framing();
    t_dup();
    t_watch();
    t_silence();
    t_clock_pause();
    if (dump)
        fclose(dump);
    printf(fails ? "editor_test: %d FAILED\n" : "editor_test: all passed\n", fails);
    return fails ? 1 : 0;
}
