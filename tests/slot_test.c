/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The SAMPLE model's and the LAYER's sample knobs (dm_sample.c): their labels, SLOT (a set's samples 1..N, in key order,
 * each played at its own root; a stored note from before SLOT converted where it loads: projects, presets, the user
 * bank; LOCK / MOTION events keep their note and play it as before), START / END (0..127 over the sample, exact ADPCM
 * decoder states: generated for the built-in set, worked out in the main loop for a user slot; the defaults
 * bit-identical). The firmware's real project.c / presets.c / motion.c / ui on a host flash (tests/flash_host.h). */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#include "ui_host.h"

static int fails;
static void check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    fails += !ok;
}
#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int32_t wa[SECS(2)], wb[SECS(2)];
static int32_t peak(const int32_t *x, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        if (abs(x[i]) > p)
            p = abs(x[i]);
    return p;
}
static uint32_t last_sound(const int32_t *x, uint32_t n)
{
    while (n && !x[n - 1u])
        n--;
    return n;
}

static void power_cycle(void)
{
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(lk_slot, 0, sizeof lk_slot);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    var_play = var_dirty = var_lost = lk_lost = 0;
    persist_note = 0;
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

/* one hit of track 0 as it is now, rendered */
static void hit(int32_t *w, uint32_t n)
{
    drum_play(&trk[0], 127, 0);
    render_track(&trk[0], w, n);
}
/* a SAMPLE track 0 with knobs k (E0..E7 as stored, no conversion), LAYER off */
static void smpl_raw(const int16_t *k)
{
    uint32_t i;
    host_init();
    drum_set_model(&trk[0], DM_SMPL);
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = k[i];
    trk[0].p[P_LLEVEL] = 0;
}

/* ---- a user slot in flash (eng_sample.c's layout): zones of a ramp-ish signal, ADPCM by the firmware's decoder rule */
typedef struct { uint32_t n, lo, hi, root; } uz_t;
static void user_slot(uint32_t k, const uz_t *z, uint32_t nz)
{
    uint8_t *s = hflash + 0xA0000u + k * 0x14000u;
    uint32_t i, j, off = 0;
    memset(s, 0xFF, 0x4000u * 3u);
    memset(s, 0, 512);
    memcpy(s, "FSMP", 4);
    s[4] = 1;
    s[6] = (uint8_t)nz;
    memcpy(s + 8, "TEST", 4);
    for (i = 0; i < nz; i++) {
        uint8_t *e = s + 32u + 28u * i;
        uint32_t w[5] = {off, z[i].n, 0, z[i].n - 1u, 65536u};
        int16_t r16 = (int16_t)(z[i].root * 16u);
        memcpy(e, w, 20);
        memcpy(e + 20, &r16, 2);
        e[25] = (uint8_t)z[i].lo;
        e[26] = (uint8_t)z[i].hi;
        for (j = 0; j < (z[i].n + 1u) / 2u; j++)          /* codes that wander: every nibble used */
            s[512u + off + j] = (uint8_t)((j * 37u + i * 11u + (j >> 5)) ^ (j * j >> 3));
        off += (z[i].n + 1u) / 2u;
    }
    memcpy(s + 16, &off, 4);
    smp_user_scan(k);
    smp_slots_sync();
}

/* ---- the reference decoder (eng_sample.c sample_next, written out): the state after `pos` nibbles of zone z */
static void ref_state(const smp_zone_t *z, uint32_t pos, int32_t *pred, int32_t *idx)
{
    uint32_t i;
    int32_t p = 0, x = 0;
    for (i = 0; i < pos; i++) {
        uint32_t b = ((const uint8_t *)((uintptr_t)SMP_DATA + z->off))[i >> 1], c = (i & 1u) ? b >> 4 : b & 15u;
        int32_t st = IMA_STEP[x], vd = st >> 3;
        if (c & 4u)
            vd += st;
        if (c & 2u)
            vd += st >> 1;
        if (c & 1u)
            vd += st >> 2;
        p = clamp(p + ((c & 8u) ? -vd : vd), -32768, 32767);
        x = clamp(x + IMA_IDX[c & 7u], 0, 88);
    }
    *pred = p;
    *idx = x;
}

/* ---------------------------------------------------------------- labels */
static void t_labels(void)
{
    static const char *const L[5] = {"SET", "SLOT", "LEVEL", "TUNE", "DECAY"};
    static const char *const S[8] = {"TUNE", "DECAY", "TONE", "DRIVE", "SET", "SLOT", "START", "END"};
    uint32_t i, ok = 1, w = 1;
    for (i = 0; i < 5u; i++)
        ok &= !strcmp(TP[P_LSET + i].label, L[i]);
    check("LAYER: SET SLOT LEVEL TUNE DECAY", ok);
    for (ok = 1, i = 0; i < 8u; i++) {
        ok &= !strcmp(DMODELS[DM_SMPL].edit[i].label, S[i]);
        w &= str_len(DMODELS[DM_SMPL].edit[i].label) <= 5u;
    }
    for (i = 0; i < 5u; i++)
        w &= str_len(TP[P_LSET + i].label) <= 5u;
    check("SAMPLE: TUNE DECAY TONE DRIVE / SET SLOT START END", ok);
    check("labels fit the 5-character columns", w);
    {
        char b[16];
        ui.ml.act = 0;
        ui.ml.trk = 2;
        ui.ml.id = P_LKEY;
        ml_name(b);
        ok = !strcmp(b, "T3 L.SLOT");
        ui.ml.id = P_LLEVEL;
        ml_name(b);
        ok &= !strcmp(b, "T3 L.LEVEL");
        old_set_model(&trk[2], DM_SMPL);
        ui.ml.id = P_E5;
        ml_name(b);
        ok &= !strcmp(b, "T3 SLOT");
        ui.ml.id = P_E6;
        ml_name(b);
        ok &= !strcmp(b, "T3 START");
        check("MIDI LEARN names: T3 L.SLOT, T3 L.LEVEL (the LAYER's), T3 SLOT, T3 START", ok);
    }
    check("START and END are learnable SOUND knobs (no longer '-')",
          ml_knob_real(&DMODELS[DM_SMPL].edit[6]) && ml_knob_real(&DMODELS[DM_SMPL].edit[7]) && motion_param(P_E6) &&
              motion_param(P_E7));
}

/* ---------------------------------------------------------------- SLOT */
static void t_slot_list(void)
{
    uint32_t i, n = smp_slot_count(0), ok = 1, lo_prev = 0, d = 1;
    check("PERC: 16 samples (24 zones, some sharing a sample)", n == 16u);
    for (i = 0; i < n; i++) {
        int32_t o16;
        uint32_t zi = smp_slot_zone(0, SMP_SLOT_V(i), &o16), j;
        const smp_zone_t *z = smp_zone(zi);
        uint32_t lo = 127;
        for (j = 0; j < SMP_SETS[0].nz; j++)        /* the sample's lowest key */
            if (SMP_ZONES[j].off == z->off && SMP_ZONES[j].lo < lo)
                lo = SMP_ZONES[j].lo;
        ok &= zi != 0xFFFFu && o16 == 0 && lo >= lo_prev;
        lo_prev = lo;
        for (j = 0; j < i; j++) {
            int32_t q;
            d &= smp_zone(smp_slot_zone(0, SMP_SLOT_V(j), &q))->off != z->off;
        }
    }
    check("SLOT 1..16: each a sample of its own, in key order, played at its root", ok && d);
    check("SLOT 1 is the kick (key 36's sample)", smp_zone(smp_slot_zone(0, SMP_SLOT_V(0), &(int32_t){0}))->off ==
                                                     smp_zone(smp_find(0, 36))->off);
    {
        int32_t o;
        ok = smp_slot_zone(0, -100, &o) == smp_slot_zone(0, SMP_SLOT_V(0), &o);
        check("SLOT below 1 (a stray value) plays the first sample", ok);
    }
}
static void t_slot_text(void)
{
    char v[16];
    const char *u;
    ui_host_init();
    old_set_model(TSEL, DM_SMPL);
    TSEL->p[P_E4] = 0;
    param_format(&DMODELS[DM_SMPL].edit[5], SMP_SLOT_V(2), v, &u);
    check("SLOT text: 3/16", !strcmp(v, "3/16"));
    param_format(&DMODELS[DM_SMPL].edit[5], 36, v, &u);
    check("SLOT text of a note from before SLOT: its sample (36: 1/16)", !strcmp(v, "1/16"));
    TSEL->p[P_E4] = SMP_NSETS;                      /* USR1, empty */
    param_format(&DMODELS[DM_SMPL].edit[5], SMP_SLOT_V(0), v, &u);
    check("SLOT text of an empty user slot: --", !strcmp(v, "--"));
    TSEL->p[P_LSET] = 0;
    param_format(&TP[P_LKEY], SMP_SLOT_V(15), v, &u);
    check("the LAYER's SLOT reads the LAYER's SET: 16/16", !strcmp(v, "16/16"));
}
static void t_slot_knob(void)
{
    uint32_t i, ok = 1;
    char v[16];
    const char *u;
    ui_host_init();
    old_set_model(TSEL, DM_K909);
    TSEL->p[P_LSET] = 0;
    TSEL->p[P_LKEY] = SMP_SLOT_V(0);
    ui.home = 0;
    ui.page = (uint8_t)page_first(FAM_LAY);
    ui.force = 1;
    ui_frame();
    for (i = 1; i < 16u; i++) {
        turn(EN_K2, 1);
        ui_frame();
        ok &= TSEL->p[P_LKEY] == SMP_SLOT_V(i);
    }
    turn(EN_K2, 1);
    ui_frame();
    check("LAYER SLOT: a detent a sample, 1 .. 16, stops at 16", ok && TSEL->p[P_LKEY] == SMP_SLOT_V(15));
    TSEL->p[P_LKEY] = 50;                           /* a note from before SLOT (tom 47..50) */
    turn(EN_K2, 1);
    ui_frame();
    param_format(&TP[P_LKEY], TSEL->p[P_LKEY], v, &u);
    check("a detent on a note from before SLOT: from its sample on", TSEL->p[P_LKEY] < 0 &&
                                                                          TSEL->p[P_LKEY] == smp_slot_of(0, 50) + 1 - 16);
    user_slot(0, &(uz_t){4000, 0, 127, 60}, 1);
    TSEL->p[P_LKEY] = SMP_SLOT_V(9);
    turn(EN_K1, 1);                                 /* SET: PERC -> USR1 */
    ui_frame();
    check("SET changed: SLOT clamped to the new set's samples (1/1)",
          TSEL->p[P_LSET] == SMP_NSETS && TSEL->p[P_LKEY] == SMP_SLOT_V(0));
    old_set_model(TSEL, DM_SMPL);
    TSEL->p[P_E4] = 0;
    TSEL->p[P_E5] = SMP_SLOT_V(12);
    ml_set(TSEL, P_E4, 127);                        /* a learned CC: SET -> USR3 (empty) */
    check("SET by MIDI LEARN: SLOT kept inside the new set (an empty set: 1)", TSEL->p[P_E4] == SMP_NALL - 1 &&
                                                                                  TSEL->p[P_E5] == SMP_SLOT_V(0));
}
static void t_slot_play(void)
{
    uint32_t i, ok = 1;
    for (i = 0; i < smp_slot_count(0); i++) {       /* a sample whose root key plays it: SLOT = that note, exactly */
        int32_t o;
        const smp_zone_t *z = smp_zone(smp_slot_zone(0, SMP_SLOT_V(i), &o));
        uint32_t key = (uint32_t)z->root16 / 16u, zk = smp_find(0, key);
        int16_t a[8] = {0, 127, 127, 0, 0, (int16_t)key, 0, 0}, b[8] = {0, 127, 127, 0, 0, SMP_SLOT_V(i), 0, 127};
        if (zk == 0xFFFFu || smp_zone(zk)->off != z->off || smp_zone(zk)->root16 != (int16_t)(key * 16u))
            continue;
        smpl_raw(a);
        hit(wa, SECS(0.5));
        smpl_raw(b);
        hit(wb, SECS(0.5));
        ok &= !memcmp(wa, wb, sizeof(int32_t) * SECS(0.5)) && peak(wa, SECS(0.5)) > 0;
    }
    check("SLOT n = the old KEY at that sample's root, bit for bit", ok);
    {
        int16_t a[8] = {0, 127, 127, 0, 0, 42, 0, 127}, b[8] = {0, 127, 127, 0, 0, 0, 0, 127};
        int32_t o;
        b[5] = (int16_t)smp_slot_of(0, 0);          /* (placeholder) */
        for (i = 0; i < smp_slot_count(0); i++)
            if (smp_zone(smp_slot_zone(0, SMP_SLOT_V(i), &o))->off == SMP_ZONES[0].off)
                b[5] = SMP_SLOT_V(i);
        smpl_raw(a);
        hit(wa, SECS(0.3));
        smpl_raw(b);
        hit(wb, SECS(0.3));
        check("the closed hat (keys 42 / 44, under the low tom's 41..45) has a SLOT of its own",
              b[5] < 0 && peak(wb, SECS(0.3)) > 0 && memcmp(wa, wb, sizeof(int32_t) * SECS(0.3)));
    }
    {
        int16_t a[8] = {5, 127, 127, 0, 0, 36, 0, 127}, b[8] = {5, 127, 127, 0, 0, SMP_SLOT_V(0), 0, 127};
        smpl_raw(a);
        hit(wa, SECS(0.5));
        smpl_raw(b);
        hit(wb, SECS(0.5));
        check("TUNE pitches a SLOT as it pitched a KEY (+5)", !memcmp(wa, wb, sizeof(int32_t) * SECS(0.5)));
    }
    {
        static int32_t la[SECS(0.5)];
        host_init();
        old_set_model(&trk[0], DM_K909);
        trk[0].p[P_LLEVEL] = 100;
        trk[0].p[P_LSET] = 0;
        trk[0].p[P_LKEY] = 38;
        trk[0].p[P_LTUNE] = 3;
        hit(la, SECS(0.5));
        host_init();
        old_set_model(&trk[0], DM_K909);
        trk[0].p[P_LLEVEL] = 100;
        trk[0].p[P_LSET] = 0;
        trk[0].p[P_LKEY] = (int16_t)SMP_SLOT_V(smp_slot_of(0, 38));
        trk[0].p[P_LTUNE] = 3;
        hit(wb, SECS(0.5));
        check("LAYER: SLOT of the snare = the old LKEY 38, bit for bit", !memcmp(la, wb, sizeof la));
    }
    user_slot(1, (const uz_t[]){{3000, 0, 59, 48}, {2000, 60, 127, 72}}, 2);
    {
        int32_t o;
        check("a user slot with 2 samples: SLOT 1/2, 2/2 in key order",
              smp_slot_count(SMP_NSETS + 1u) == 2u && smp_zone(smp_slot_zone(SMP_NSETS + 1u, SMP_SLOT_V(0), &o))->n == 3000u &&
                  smp_zone(smp_slot_zone(SMP_NSETS + 1u, SMP_SLOT_V(1), &o))->n == 2000u);
        check("SLOT past the set's N (a value set for a bigger set) plays its last sample",
              smp_slot_zone(SMP_NSETS + 1u, SMP_SLOT_V(9), &o) == smp_slot_zone(SMP_NSETS + 1u, SMP_SLOT_V(1), &o));
    }
}

/* ---------------------------------------------------------------- conversion */
static void t_conv(void)
{
    int32_t tu;
    check("conv: a root note -> its sample, TUNE kept",
          (tu = 2, smp_slot_conv(0, 38, &tu)) == SMP_SLOT_V(smp_slot_of(0, 38)) && tu == 2);
    tu = 1;
    check("conv: 50 (low tom, root 48) -> its sample, TUNE +2",
          smp_slot_conv(0, 50, &tu) == SMP_SLOT_V(smp_slot_of(0, 50)) && tu == 3);
    tu = 23;
    check("conv: 50 with TUNE 23: no room for +2: the note kept (plays as before)", smp_slot_conv(0, 50, &tu) == 50 && tu == 23);
    tu = 0;
    check("conv: 20 (no sample there) -> the nearest (the kick, 35..36), TUNE kept",
          smp_slot_conv(0, 20, &tu) == SMP_SLOT_V(0) && tu == 0);
    tu = 0;
    check("conv: a SLOT value stays", smp_slot_conv(0, SMP_SLOT_V(7), &tu) == SMP_SLOT_V(7) && tu == 0);
    tu = 0;
    check("conv: an empty user slot: the note kept", smp_slot_conv(SMP_NSETS + 2u, 40, &tu) == 40 && tu == 0);
    user_slot(0, &(uz_t){4000, 0, 127, 60}, 1);
    tu = 0;
    check("conv: a one-sample user slot, KEY 36 (root 60) -> 1/1, TUNE -24",
          smp_slot_conv(SMP_NSETS, 36, &tu) == SMP_SLOT_V(0) && tu == -24);
    tu = -3;
    check("conv: .. with TUNE -3: -27 does not fit: the note kept", smp_slot_conv(SMP_NSETS, 36, &tu) == 36 && tu == -3);
}

/* a project from before SLOT (KEY notes, START / END 0): saved, loaded after a power cycle, converted, the same sound */
static void t_project(void)
{
    static int32_t a[2][SECS(0.5)];
    static const int16_t K[2][8] = {{1, 127, 127, 0, 0, 50, 0, 0}, {23, 127, 127, 0, 0, 50, 0, 0}};
    uint32_t k;
    fresh();
    for (k = 0; k < 2u; k++) {                      /* the sound before: the notes as KEY (the stored form), track k */
        uint32_t i;
        track_t keep;
        host_init();
        drum_set_model(&trk[k], DM_SMPL);
        for (i = 0; i < 8u; i++)
            trk[k].p[P_E0 + i] = K[k][i];
        keep = trk[k];
        host_init();
        trk[0] = keep;
        trk[0].p[P_LLEVEL] = 0;
        hit(a[k], SECS(0.5));
    }
    fresh();
    for (k = 0; k < 2u; k++) {
        uint32_t i;
        drum_set_model(&trk[k], DM_SMPL);
        for (i = 0; i < 8u; i++)
            trk[k].p[P_E0 + i] = K[k][i];
    }
    trk[2].p[P_LSET] = 0;
    trk[2].p[P_LKEY] = 38;
    trk[2].p[P_LTUNE] = 0;
    trk[3].p[P_LSET] = 0;
    trk[3].p[P_LKEY] = 20;
    project_save(0);
    check("the old project saved as it was (KEY 50, END 0 in flash)", proj_slot[0].t[0].p[P_E5] == 50 &&
                                                                          proj_slot[0].t[0].p[P_E7] == 0);
    power_cycle();
    project_load(0);
    check("loaded: KEY 50 -> its sample, TUNE 1 -> 3, END 0 -> 127",
          trk[0].p[P_E5] == SMP_SLOT_V(smp_slot_of(0, 50)) && trk[0].p[P_E0] == 3 && trk[0].p[P_E7] == 127 &&
              trk[0].p[P_E6] == 0);
    check("loaded: KEY 50 with TUNE 23 kept as the note (no room for +2)", trk[1].p[P_E5] == 50 && trk[1].p[P_E0] == 23);
    check("loaded: LAYER SLOT 38 -> the snare, 20 -> the nearest (the kick)",
          trk[2].p[P_LKEY] == SMP_SLOT_V(smp_slot_of(0, 38)) && trk[3].p[P_LKEY] == SMP_SLOT_V(0));
    check("loaded: not marked as edited", !slot_dirty(0));
    {                                               /* a cue into the slot stages the same knobs (project.c) */
        static int16_t o[NTRK][P_COUNT];
        static uint16_t pre[NTRK];
        static uint8_t pe[NTRK];
        cue_stage_knobs(&proj_slot[0], o, pre, pe);
        check("a cue's staged knobs: converted as a LOAD's", !memcmp(o[0], trk[0].p, sizeof o[0]) &&
                                                               !memcmp(o[1], trk[1].p, sizeof o[1]) &&
                                                               o[2][P_LKEY] == trk[2].p[P_LKEY]);
    }
    {
        static track_t loaded[2];
        loaded[0] = trk[0];
        loaded[1] = trk[1];
    for (k = 0; k < 2u; k++) {
        static int32_t b[SECS(0.5)];
        track_t keep = loaded[k];
        uint32_t i;
        host_init();
        trk[0] = keep;
        trk[0].p[P_LLEVEL] = 0;
        for (i = 0; i < NDV; i++)
            trk[0].v[i].active = trk[0].lv[i].active = 0;
        drum_play(&trk[0], 127, 0);
        render_track(&trk[0], b, SECS(0.5));
        check(k ? "loaded track 2 plays as before, bit for bit" : "loaded track 1 plays as before, bit for bit",
              !memcmp(a[k], b, sizeof b));
    }
    }
}

/* factory presets: the SAMPLE ones stored as SLOT / END now (tools/presets.json), each the hit its old KEY played;
 * the user bank's old KEY / END converted when loaded */
static void t_presets(void)
{
    static const int16_t OLD[4][8] = {{0, 127, 127, 0, 0, 36, 0, 0}, {0, 127, 127, 0, 0, 54, 0, 0},
                                      {0, 127, 110, 0, 0, 82, 0, 0}, {-5, 127, 100, 60, 0, 63, 0, 0}};   /* 0.28.0's */
    uint32_t id, n = 0, ok = 1;
    for (id = 0; id < PRE_NFACT; id++) {
        preset_t pr;
        int32_t tu;
        if (!preset_get(id, &pr) || pr.model != DM_SMPL)
            continue;
        if (n >= 4u) {
            ok = 0;
            break;
        }
        tu = OLD[n][0];
        ok &= pr.k[5] == smp_slot_conv(0, OLD[n][5], &tu) && tu == pr.k[0] && pr.k[6] == 0 && pr.k[7] == 127;
        smpl_raw(OLD[n]);
        hit(wa, SECS(0.5));
        host_init();
        preset_apply(&trk[0], &pr, id);
        trk[0].p[P_LLEVEL] = 0;
        hit(wb, SECS(0.5));
        ok &= !memcmp(wa, wb, sizeof(int32_t) * SECS(0.5)) && peak(wa, SECS(0.5)) > 0;
        n++;
    }
    check("factory SAMPLE presets: SLOT / END (their old KEY converted), the same hits bit for bit", n == 4u && ok);
    fresh();
    upr.magic = UPR_MAGIC;
    upr.s[5].used = UPR_USED;
    upr.s[5].model = DM_SMPL;
    {
        static const int8_t K[8] = {-2, 100, 127, 0, 0, 63, 0, 0};   /* conga 62..64, root 63 */
        memcpy(upr.s[5].k, K, 8);
    }
    st_save(OBJ_UPRESET0 + 1u, &upr, sizeof upr);
    power_cycle();
    check("the user bank's old KEY kept through the boot's clamps (63), END 0 read as 127", upr.s[5].k[5] == 63 &&
                                                                                                upr.s[5].k[7] == 127);
    {
        preset_t pr;
        host_init();
        preset_get(PRE_NFACT + 5u, &pr);
        preset_apply(&trk[0], &pr, PRE_NFACT + 5u);
        check("a user preset from before SLOT loads as SLOT (conga), TUNE -2, END 127",
              trk[0].p[P_E5] == SMP_SLOT_V(smp_slot_of(0, 63)) && trk[0].p[P_E0] == -2 && trk[0].p[P_E7] == 127);
    }
}

/* a LOCK / MOTION event holding an old KEY: played as it was (the note, not clamped into SLOT's range) */
static void t_locks(void)
{
    host_init();
    old_set_model(&trk[0], DM_SMPL);
    trk[0].p[P_E5] = SMP_SLOT_V(0);
    memset(&mo.b, 0, sizeof mo.b);
    trk[0].step[3].on = 1;
    lk_put(&mo.b, lk_key(0, 3, P_E5), 50, 1);
    lk_put(&mo.b, lk_key(0, 3, P_E6), 64, 1);
    motion_step(&trk[0], 3, MS_ALL);
    check("a LOCK of the old KEY (50) plays the note (pitched as before), a START lock its value",
          trk[0].p[P_E5] == 50 && trk[0].p[P_E6] == 64);
    motion_step(&trk[0], 4, MS_ALL);
    check(".. and lets go after its step", trk[0].p[P_E5] == SMP_SLOT_V(0) && trk[0].p[P_E6] == 0);
    memset(&mo.b, 0, sizeof mo.b);
    lk_put(&mo.b, lk_key(0, 3, P_E5), SMP_SLOT_V(4), 1);
    motion_step(&trk[0], 3, MS_ALL);
    check("a LOCK of SLOT 5 plays SLOT 5", trk[0].p[P_E5] == SMP_SLOT_V(4));
    memset(&mo.b, 0, sizeof mo.b);
}

/* ---------------------------------------------------------------- START / END */
static void t_seek_table(void)
{
    uint32_t zi, s, ok = 1, rows = 0;
    for (zi = 0; zi < sizeof SMP_ZONES / sizeof SMP_ZONES[0]; zi++) {
        const smp_zone_t *z = &SMP_ZONES[zi];
        for (s = 1; s < 128u; s++) {
            int32_t p, x, rp, rx;
            if (!smp_seek_at(zi, s, &p, &x)) {
                ok = 0;
                continue;
            }
            ref_state(z, (z->n * s) >> 7, &rp, &rx);
            ok &= p == rp && x == rx;
        }
        rows += SMP_SEEK_ROW[zi] != 0xFFu;
    }
    check("built-in START states (generated) = the decoder's, every zone, every START", ok && rows == 24u);
    check("built-in START states: one table per sample (16), 6 KB of flash", SMP_SEEK_NROW == 16u);
}
static void t_start_end(void)
{
    static int16_t K[8] = {0, 127, 127, 0, 0, 0, 0, 127};
    uint32_t n0, n1, ok;
    int32_t p, x;
    const smp_zone_t *z;
    K[5] = SMP_SLOT_V(0);
    smpl_raw(K);
    hit(wa, SECS(1));
    n0 = last_sound(wa, SECS(1));
    {
        static int16_t D[8] = {0, 127, 127, 0, 0, 36, 0, 0};   /* the defaults as stored before: KEY 36, END 0 */
        smpl_raw(D);
        hit(wb, SECS(1));
        check("START 0 / END 127 = START / END as stored before (0 / 0), bit for bit", !memcmp(wa, wb, sizeof(int32_t) * SECS(1)));
    }
    K[6] = 64;
    smpl_raw(K);
    drum_play(&trk[0], 127, 0);
    z = smp_zone((uint32_t)trk[0].v[0].sv.s[4]);
    ref_state(z, (z->n * 64u) >> 7, &p, &x);
    ok = trk[0].v[0].sv.ph[0] == ((z->n * 64u) >> 7) + 1u;
    {
        int32_t q = p, y = x;                           /* the next sample decoded from the state at START */
        uint32_t pos = (z->n * 64u) >> 7, b = ((const uint8_t *)((uintptr_t)SMP_DATA + z->off))[pos >> 1], c = (pos & 1u) ? b >> 4 : b & 15u;
        int32_t st = IMA_STEP[y], vd = st >> 3;
        vd += (c & 4u) ? st : 0;
        vd += (c & 2u) ? st >> 1 : 0;
        vd += (c & 1u) ? st >> 2 : 0;
        q = clamp(q + ((c & 8u) ? -vd : vd), -32768, 32767);
        ok &= trk[0].v[0].sv.s[3] == q;
    }
    render_track(&trk[0], wb, SECS(1));
    n1 = last_sound(wb, SECS(1));
    check("START 64: from the middle, the decoder's state there exactly", ok);
    check("START 64: about half as long", n1 > n0 * 4u / 10u && n1 < n0 * 6u / 10u);
    K[6] = 0;
    K[7] = 31;
    smpl_raw(K);
    hit(wb, SECS(1));
    n1 = last_sound(wb, SECS(1));
    check("END 31: the first quarter, the same start", n1 > n0 * 2u / 10u && n1 < n0 * 3u / 10u &&
                                                         !memcmp(wa, wb, sizeof(int32_t) * (n1 / 2u)));
    K[6] = 100;
    K[7] = 20;
    smpl_raw(K);
    hit(wb, SECS(1));
    n1 = last_sound(wb, SECS(1));
    check("END below START plays as END = START (1/128 of the sample)", n1 > 0 && n1 < n0 / 40u);
    K[6] = 127;
    K[7] = 127;
    smpl_raw(K);
    hit(wb, SECS(1));
    check("START 127: the last 1/128, sound", last_sound(wb, SECS(1)) > 0);
}
static void t_user_start(void)
{
    static int16_t K[8] = {0, 127, 127, 0, SMP_NSETS, 0, 96, 127};
    const smp_zone_t *z;
    int32_t p, x;
    uint32_t pos;
    user_slot(0, &(uz_t){9000, 0, 127, 60}, 1);
    K[5] = SMP_SLOT_V(0);
    smpl_raw(K);
    drum_play(&trk[0], 127, 0);
    z = smp_zone((uint32_t)trk[0].v[0].sv.s[4]);
    pos = (z->n * 96u) >> 7;
    {                                               /* the reference: decoded from the slot's own data */
        uint32_t i;
        int32_t q = 0, y = 0;
        for (i = 0; i < pos; i++) {
            uint32_t b = ((const uint8_t *)((uintptr_t)SMP_DATA + z->off))[i >> 1], c = (i & 1u) ? b >> 4 : b & 15u;
            int32_t st = IMA_STEP[y], vd = st >> 3;
            vd += (c & 4u) ? st : 0;
            vd += (c & 2u) ? st >> 1 : 0;
            vd += (c & 1u) ? st >> 2 : 0;
            q = clamp(q + ((c & 8u) ? -vd : vd), -32768, 32767);
            y = clamp(y + IMA_IDX[c & 7u], 0, 88);
        }
        p = q;
        x = y;
    }
    check("a user slot: START 96 from the main loop's states, exactly",
          trk[0].v[0].sv.ph[0] == pos + 1u && smp_seek_at((uint32_t)trk[0].v[0].sv.s[4], 96, &(int32_t){0}, &(int32_t){0}) &&
              (smp_seek_at((uint32_t)trk[0].v[0].sv.s[4], 96, &p, &x), 1));
    {
        int32_t sp, sx;
        smp_seek_at((uint32_t)trk[0].v[0].sv.s[4], 96, &sp, &sx);
        check("a user slot's START state = the decoder's", sp == p && sx == x);
    }
    smp_user_gen++;                                 /* an upload changed it: the states stale until the main loop */
    check("stale states (a slot just changed) are not used", !smp_seek_at((uint32_t)trk[0].v[0].sv.s[4], 96, &p, &x));
    smp_slots_sync();
    check(".. the main loop works them out again", smp_seek_at((uint32_t)trk[0].v[0].sv.s[4], 96, &p, &x));
    user_slot(1, (const uz_t[]){{3000, 0, 59, 48}, {2000, 60, 127, 72}}, 2);
    K[4] = SMP_NSETS + 1;
    K[5] = SMP_SLOT_V(1);
    smpl_raw(K);
    drum_play(&trk[0], 127, 0);
    check("a user slot's second sample: START from its beginning (states for the first sample only), END kept",
          trk[0].v[0].sv.ph[0] == 1u && trk[0].v[0].sv.ph[2] == 2000u);
}

int main(void)
{
    fresh();
    t_labels();
    t_slot_list();
    t_slot_text();
    t_slot_knob();
    fresh();
    t_slot_play();
    fresh();
    t_conv();
    t_project();
    t_presets();
    t_locks();
    t_seek_table();
    t_start_end();
    fresh();
    t_user_start();
    printf("slot_test: %s (%d failed)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}
