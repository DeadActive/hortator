/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Presets (spec docs/superpowers/specs/2026-10-19-presets-design.md, plan 2026-10-19-presets.md Task 2) on the host,
 * with the host flash: a factory preset is the voice only (MODEL + the 8 SOUND knobs), a user preset the voice and the
 * track's FX but the SLICER (2026-11-01), the banks' order and the step through them (wrapping, empty user slots and a
 * capped ACID skipped), the 96 user slots (save, load, delete, overwrite, the flash record "UPR2", the deferred save
 * while playing, a power cut, MIDI LEARN's map untouched, an older "UPR1" bank of 128 moved into 96), no default choke
 * (a MODEL change or a load never touches CHOKE), the project's reference (FDRD, FDRC converted) and the edited mark. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#include "ui_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
static void frames(uint32_t n)
{
    while (n--)
        ui_frame();
}
static void fresh(void)                              /* HOME, track 1, a blank flash, stopped */
{
    memset(hflash, 0xFF, sizeof hflash);
    host_jedec = 0x856014u;
    ui_host_init();
    persist_boot();
    settings_init();
    frames(2);
}
static void power_cycle(void)                        /* the RAM gone: what the flash holds comes back */
{
    memset(ml_map, 0xA5, sizeof ml_map);
    host_init();
    memset(&ui, 0, sizeof ui);
    persist_boot();
    settings_init();
    ui.home = ui.hmode = 1;
    ui.force = 1;
    frames(2);
}
static int knobs_are(const track_t *t, const preset_t *p)
{
    uint32_t i;
    if ((uint32_t)t->p[P_MODEL] != p->model)
        return 0;
    for (i = 0; i < 8u; i++)
        if (t->p[P_E0 + i] != p->k[i])
            return 0;
    return 1;
}
static uint32_t user_id(uint32_t slot) { return PRE_NFACT + slot; }
static const char *name_of(uint32_t id)
{
    static char b[9];
    preset_name_of(id, b);
    return b;
}

/* the FX a user preset holds (2026-11-01: every FX of the track but the SLICER), listed here on their own */
static const uint8_t FXI[] = {P_DIST, P_CHOR, P_DLY, P_REV, P_RMODEL, P_RTUNE, P_RDECAY, P_RMIX, P_RTONE, P_RSTRCT,
                              P_RPOS, P_RKEY, P_FTYPE, P_FCUT, P_FRESO, P_FENV, P_FDEC, P_ITYPE, P_IA, P_IB, P_IC,
                              P_IMIX, P_SRR};
#define NFXI (sizeof FXI)
static int is_fx(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < NFXI; i++)
        if (FXI[i] == id)
            return 1;
    return 0;
}
static void fx_fill(track_t *t, uint32_t seed)          /* each FX knob a value in its range, by seed (RESON: STRNG) */
{
    uint32_t i;
    for (i = 0; i < NFXI; i++) {
        const param_desc_t *d = &TP[FXI[i]];
        t->p[FXI[i]] = (int16_t)(d->min + (int32_t)((seed * 13u + i * 29u + 5u) % (uint32_t)(d->max - d->min + 1)));
    }
    t->p[P_RMODEL] = RS_STRNG;
}
static int fx_same(const int16_t *a, const int16_t *b)   /* two tracks' p[]: the same FX */
{
    uint32_t i;
    for (i = 0; i < NFXI; i++)
        if (a[FXI[i]] != b[FXI[i]])
            return 0;
    return 1;
}
static int said(const char *m, uint32_t n)               /* the top bar shows m within n frames */
{
    while (n--) {
        ui_frame();
        if (ui.msg_t && !strcmp(ui.msg, m))
            return 1;
    }
    return 0;
}
static int8_t slot_fx(uint32_t slot, uint32_t id)       /* user slot's stored FX knob id */
{
    uint32_t i;
    for (i = 0; i < UPR_NFX; i++)
        if (UPR_FX[i] == id)
            return upr.s[slot].f[i];
    return -128;
}

/* the banks (plan 2026-10-19-presets Task 5, tools/presets.json -> preset_bank.h): every voice's default = its first
 * preset (MATRX, CLTRX: none, their defaults kept); MACHINEDRUM 55, CLASSIC 56, MODERN 40; names unique, 1..8
 * characters; every knob within its model's range; at most 4 per voice, each in its voice's bank */
static uint32_t bank_of_model(uint32_t m)
{
    return m >= DM_KTRX ? (uint32_t)PB_MD : m <= DM_CLAVE ? (uint32_t)PB_CLASSIC : (uint32_t)PB_MODERN;
}
static void test_defaults_are_first_presets(void)
{
    uint32_t m, i, j, ok = 1, n[NMODELS] = {0};
    preset_t p, q;
    for (m = 0; m < NMODELS; m++) {
        uint32_t id = preset_first_of(m);
        if (m == DM_MATRX || m == DM_CLTRX) {
            ok &= id == PRE_NONE;
            continue;
        }
        ok &= id != PRE_NONE && preset_get(id, &p) && p.model == m;
        for (i = 0; ok && i < 8u; i++)
            ok &= DMODELS[m].edit[i].def == p.k[i];
        if (!ok) {
            printf("      model %s\n", N_MODEL[m]);
            break;
        }
    }
    check("every model's default knobs = its first preset's (MATRX, CLTRX: no preset)", ok);
    ok = PRE_NFACT == 151u && PRE_BANK_START[PB_MD] == 0u && PRE_BANK_START[PB_MODERN] == 55u &&
         PRE_BANK_START[PB_CLASSIC] == 95u && PRE_BANK_START[PB_USER] == PRE_NFACT &&
         PRE_BANK_START[PB_N] == PRE_NFACT + PRE_NUSER;          /* (fix 8: MACHINEDRUM, MODERN, CLASSIC) */
    for (i = 0; i < PRE_NFACT; i++)
        ok &= preset_get(i, &p) && preset_bank_of(i) == (i < 55u ? (uint32_t)PB_MD : i < 95u ? (uint32_t)PB_MODERN
                                                                                              : (uint32_t)PB_CLASSIC);
    ok &= PRE_NUSER == 96u && preset_bank_of(user_id(0)) == PB_USER && preset_bank_of(user_id(95)) == PB_USER &&
          preset_bank_of(user_id(96)) == PB_N && !preset_get(user_id(96), &p) && !preset_get(PRE_NONE, &p);
    check("banks: MACHINEDRUM 55, MODERN 40, CLASSIC 56, then USER 96; a bad id: none", ok);
    for (i = 0, ok = 1; i < PRE_NFACT; i++) {
        uint32_t len = (uint32_t)strnlen(PRE_FACTORY[i].name, sizeof PRE_FACTORY[i].name);
        p = PRE_FACTORY[i];
        ok &= p.model < NMODELS && len >= 1u && len <= 8u && bank_of_model(p.model) == preset_bank_of(i) &&
              ++n[p.model] <= 4u;
        for (j = 0; j < 8u; j++)
            ok &= p.k[j] >= DMODELS[p.model].edit[j].min && p.k[j] <= DMODELS[p.model].edit[j].max;
        for (j = 0; j < i; j++) {
            q = PRE_FACTORY[j];
            ok &= strcmp(p.name, q.name) != 0;
        }
        if (!ok) {
            printf("      preset %u %s\n", i, p.name);
            break;
        }
    }
    check("every preset: a name of 1..8 characters, unique; its knobs in its model's ranges; in its voice's bank; "
          "<= 4 per voice", ok);
    ok = preset_get(0, &p) && p.model == DM_KTRX && !strcmp(p.name, "SUBTHUMP") &&
         preset_get(preset_first_of(DM_CPEFM), &p) && !strcmp(p.name, "BUZZCLAP") && p.k[0] == 24 &&
         preset_get(PRE_BANK_START[PB_CLASSIC], &p) && p.model == DM_K808 && !strcmp(p.name, "ROUNDBD") &&
         preset_get(PRE_BANK_START[PB_MODERN], &p) && p.model == DM_KBOOM && !strcmp(p.name, "BOOMBD") &&
         preset_get(PRE_NFACT - 1u, &p) && p.model == DM_CLAVE && !strcmp(p.name, "BLIP");
    check("SUBTHUMP first, BUZZCLAP CPEFM's default (TUNE +24), BOOMBD, ROUNDBD each bank's first, BLIP last", ok);
    check("bank names, in order", !strcmp(N_PBANK[PB_MD], "MACHINEDRUM") && !strcmp(N_PBANK[1], "MODERN") &&
          !strcmp(N_PBANK[2], "CLASSIC") && !strcmp(N_PBANK[PB_USER], "USER") && PB_MD == 0 && PB_MODERN == 1 &&
          PB_CLASSIC == 2 && PB_USER == 3);
}

static void test_load_sets_voice_only(void)
{
    track_t before;
    preset_t p;
    uint32_t id, i, ok;
    fresh();
    trk[2].p[P_FTYPE] = FT_LP;
    trk[2].p[P_FCUT] = 50;
    trk[2].p[P_LEVEL] = 77;
    trk[2].p[P_PAN] = -20;
    trk[2].p[P_CHOKE] = 3;
    trk[2].p[P_NOTE] = 50;
    trk[2].p[P_LFO1 + LF_DEST] = 2;
    trk[2].step[5].on = 1;
    trk[2].step[9].acc = 1;
    fx_fill(&trk[2], 5);                               /* the FX a user preset would carry: a factory one keeps them */
    trk[2].p[P_SLCR] = 1;
    before = trk[2];
    id = preset_first_of(DM_KTRX);
    ok = preset_load(&trk[2], id) == 0;
    preset_get(id, &p);
    ok &= knobs_are(&trk[2], &p) && trk[2].pre == id && !trk[2].pre_ed;
    for (i = 0; i < P_COUNT; i++)
        if (i < P_MODEL || i > P_E7)
            ok &= trk[2].p[i] == before.p[i];
    ok &= !memcmp(trk[2].step, before.step, sizeof before.step);
    check("a factory load: MODEL + 8 knobs = the preset's; FX (FILTER, INSERT, RESON, sends, SRR), SLICER, LEVEL, PAN, "
          "CHOKE 3, NOTE, LFO, steps unchanged", ok);
    check("a bad id or an empty user slot: refused, nothing changes",
          preset_load(&trk[2], user_id(4)) == -1 && preset_load(&trk[2], PRE_NONE) == -1 && trk[2].pre == id &&
              knobs_are(&trk[2], &p));
    drum_set_model(&trk[0], DM_ACID);
    drum_set_model(&trk[1], DM_ACID);
    check("ACID on 2 other tracks: an ACID preset refused (no third ACID)",
          preset_load(&trk[3], preset_first_of(DM_ACID)) == -1 && trk[3].p[P_MODEL] != DM_ACID &&
              preset_load(&trk[1], preset_first_of(DM_ACID)) == 0);
}

static void test_model_change_keeps_choke(void)
{
    fresh();
    trk[0].p[P_CHOKE] = 2;
    trk[0].pre_ed = 1;
    drum_set_model(&trk[0], DM_HATC);
    check("a MODEL change keeps CHOKE (2)", trk[0].p[P_CHOKE] == 2);
    check("a MODEL change: the reference moves to the new voice's first preset, not edited",
          trk[0].pre == preset_first_of(DM_HATC) && !trk[0].pre_ed);
    trk[0].p[P_CHOKE] = 0;
    track_select(0);
    model_step(1);
    check("MODEL turned on the panel: CHOKE stays none, the next voice's first preset",
          trk[0].p[P_CHOKE] == 0 && trk[0].p[P_MODEL] == DM_HATO && trk[0].pre == preset_first_of(DM_HATO));
    trk[0].p[P_CHOKE] = 4;
    preset_load(&trk[0], preset_first_of(DM_CHTRX));
    check("a preset load keeps CHOKE (4)", trk[0].p[P_CHOKE] == 4 && trk[0].p[P_MODEL] == DM_CHTRX);
}

static void test_no_default_choke(void)
{
    uint32_t m, k, ok = 1;
    for (m = 0; m < NMODELS; m++)
        ok &= DMODELS[m].choke == 0;
    check("no voice has a default choke group", ok);
    fresh();
    for (k = 0, ok = 1; k < NTRK; k++)
        ok &= trk[k].p[P_CHOKE] == 0 && trk[k].pre == preset_first_of((uint32_t)trk[k].p[P_MODEL]) && !trk[k].pre_ed;
    check("the power-on kit: no track chokes (the hats too); each track on its voice's first preset", ok);
}

static void test_step_order_and_wrap(void)
{
    uint32_t id, last = PRE_NFACT - 1u, ok = 1, n = 0;
    track_t *t = &trk[0];
    fresh();
    t->pre = PRE_NONE;
    check("no preset yet: the first detent lands on the track's voice's first preset (either way)",
          preset_step(t, PRE_NONE, 1) == preset_first_of((uint32_t)t->p[P_MODEL]) &&
              preset_step(t, PRE_NONE, -1) == preset_first_of((uint32_t)t->p[P_MODEL]));
    for (id = 0; id < last; id++)
        ok &= preset_step(t, id, 1) == id + 1u && preset_step(t, id + 1u, -1) == id;
    check("factory ids step one by one, both ways", ok);
    check("no user preset: the last factory one wraps to the first, and back",
          preset_step(t, last, 1) == 0u && preset_step(t, 0, -1) == last);
    preset_user_save(5, &trk[1]);
    preset_user_save(9, &trk[2]);
    check("user slots 6 and 10 used: the step goes through them (empty slots skipped), then wraps",
          preset_step(t, last, 1) == user_id(5) && preset_step(t, user_id(5), 1) == user_id(9) &&
              preset_step(t, user_id(9), 1) == 0u && preset_step(t, 0, -1) == user_id(9) &&
              preset_step(t, user_id(9), -1) == user_id(5) && preset_step(t, user_id(5), -1) == last);
    check("from an empty (deleted) slot: on to the next used one", preset_step(t, user_id(7), 1) == user_id(9) &&
          preset_step(t, user_id(7), -1) == user_id(5));
    drum_set_model(&trk[1], DM_ACID);
    drum_set_model(&trk[2], DM_ACID);
    id = preset_first_of(DM_ACID);                     /* ACID's 4 presets: id .. id + 3 */
    check("ACID on 2 other tracks: its presets skipped", preset_step(t, id - 1u, 1) == id + 4u &&
          preset_step(t, id + 4u, -1) == id - 1u && PRE_FACTORY[id + 3u].model == DM_ACID &&
          PRE_FACTORY[id + 4u].model != DM_ACID);
    check("... not by one of the two ACID tracks", preset_step(&trk[1], id - 1u, 1) == id);
    for (id = preset_step(t, PRE_NONE, 1); n < 400u; n++)
        id = preset_step(t, id, 1);
    check("400 detents: always a loadable id", id < PRE_NFACT + PRE_NUSER && preset_load(t, id) == 0);
}

static void test_user_save_load_delete(void)
{
    preset_t p;
    track_t *t = &trk[0];
    fresh();
    check("a blank flash: every user slot free, the first free is 0", preset_user_free() == 0 &&
          !preset_get(user_id(0), &p));
    t->p[P_E1] = 99;
    t->p[P_E2] = 5;
    t->p[P_E0] = -7;
    check("SAVE to slot 1 (stopped)", preset_user_save(0, t) == 0 && preset_user_free() == 1);
    check("it is the track's voice, named \"K909 01\"", preset_get(user_id(0), &p) && knobs_are(t, &p) &&
          !strcmp(p.name, "K909 01"));
    check("SAVE leaves the track as it is (the UI points its reference at the slot)", t->pre == preset_first_of(DM_K909));
    power_cycle();
    check("power cycle: back from flash", preset_get(user_id(0), &p) && p.model == DM_K909 && p.k[1] == 99 &&
          p.k[2] == 5 && p.k[0] == -7);
    check("loads onto another track", preset_load(&trk[4], user_id(0)) == 0 && knobs_are(&trk[4], &p) &&
          trk[4].pre == user_id(0));
    drum_set_model(&trk[1], DM_SDTRX);
    preset_user_save(93, &trk[1]);
    preset_user_save(94, &trk[0]);
    preset_user_save(95, &trk[1]);
    check("names: \"SDTRX 94\", \"K909 95\", \"SDTRX 96\" (8 characters at most)",
          !strcmp(name_of(user_id(93)), "SDTRX 94") && !strcmp(name_of(user_id(94)), "K909 95") &&
              !strcmp(name_of(user_id(95)), "SDTRX 96"));
    drum_set_model(&trk[2], DM_KBOOM);                 /* the renamed models (owner, fixes 9): 4 characters, the space */
    drum_set_model(&trk[3], DM_XCTRX);
    preset_user_save(6, &trk[2]);
    preset_user_save(90, &trk[2]);
    preset_user_save(91, &trk[3]);
    check("renamed models' slots: \"BOOM 07\", \"BOOM 91\", \"CGTRX 92\"",
          !strcmp(name_of(user_id(6)), "BOOM 07") && !strcmp(name_of(user_id(90)), "BOOM 91") &&
              !strcmp(name_of(user_id(91)), "CGTRX 92"));
    preset_user_delete(6);
    preset_user_delete(90);
    preset_user_delete(91);
    check("a slot out of range: refused", preset_user_save(96, t) == -1 && preset_user_save(128, t) == -1);
    preset_user_delete(0);
    check("DELETE: the slot empty, free again", !preset_get(user_id(0), &p) && preset_user_free() == 0);
    power_cycle();
    check("power cycle: still deleted, the others kept", !preset_get(user_id(0), &p) &&
          preset_get(user_id(93), &p) && p.model == DM_SDTRX && preset_get(user_id(95), &p));
    {
        uint32_t i;
        for (i = 0; i < PRE_NUSER; i++)
            preset_user_save(i, &trk[i & 7u]);
        check("all 96 slots used: no free slot", preset_user_free() == -1);
        power_cycle();
        check("96 slots: all back after a power cycle", preset_user_free() == -1 && preset_get(user_id(95), &p) &&
              p.model == (uint32_t)trk[7].p[P_MODEL]);
    }
}

static int flash_has(uint32_t slot)                   /* the user record in flash ("UPR2") holds slot */
{
    static upr_rec_t r;
    memset(&r, 0, sizeof r);
    if (st_load(OBJ_UPRESET0 + 1u, &r, sizeof r) != (int)sizeof r || r.magic != 0x32525055u)
        return 0;
    return r.s[slot].used == 1u || r.s[slot].used == 3u;   /* (3: the voice only, moved from UPR1) */
}
static void learn_sectors(uint8_t *d) { memcpy(d, hflash + 0xDC000u, 0x2000u); }

static void test_user_save_deferred_and_cut(void)
{
    static uint8_t ml0[0x2000], ml1[0x2000];
    preset_t p;
    ml_rec_t r;
    fresh();
    ml_map[0] = (ml_ent_t){74, 0, P_E1 + 1u, ML_CC};
    learn_save();
    learn_sectors(ml0);
    preset_user_save(0, &trk[0]);
    check("saved while stopped: in flash at once (\"UPR2\" in its own object)", flash_has(0));
    transport_req = 1;
    render_mix(0, 0, CTL * 4);
    check("(playing)", song.playing);
    preset_user_save(1, &trk[3]);
    check("saved while playing: in the list at once, not in flash yet", preset_get(user_id(1), &p) &&
          p.model == DM_HATC && !flash_has(1));
    frames(3);
    check("... still not while playing", !flash_has(1));
    power_cycle();
    check("a power cut before STOP: only it is lost, the earlier save kept", !preset_get(user_id(1), &p) &&
          preset_get(user_id(0), &p) && p.model == DM_K909);
    learn_sectors(ml1);
    check("MIDI LEARN's map byte-identical across the user saves", !memcmp(ml0, ml1, sizeof ml0) && ml_count() == 1u);
    transport_req = 1;
    render_mix(0, 0, CTL * 4);
    preset_user_save(1, &trk[3]);
    preset_user_delete(0);
    transport_req = 2;
    render_mix(0, 0, CTL * 4);
    frames(3);
    check("STOP: written (the save and the delete)", flash_has(1) && !flash_has(0));
    power_cycle();
    check("power cycle: as written", preset_get(user_id(1), &p) && p.model == DM_HATC && !preset_get(user_id(0), &p));
    /* a learn while playing, then a save: both wait for the stop, both intact after it */
    transport_req = 1;
    render_mix(0, 0, CTL * 4);
    ml_map[1] = (ml_ent_t){75, 2, P_E2 + 1u, ML_CC};
    learn_save();
    preset_user_save(2, &trk[5]);
    transport_req = 2;
    render_mix(0, 0, CTL * 4);
    frames(3);
    memset(&r, 0, sizeof r);
    check("a SAVE right after a MIDI LEARN change (playing), then STOP: both written",
          st_load(OBJ_UPRESET0, &r, sizeof r) == (int)sizeof r && r.e[1].num == 75u && flash_has(2) && flash_has(1));
    power_cycle();
    check("power cycle: the learn map (2 entries) and the user bank (slots 2, 3) intact",
          ml_count() == 2u && preset_get(user_id(1), &p) && preset_get(user_id(2), &p) && p.model == DM_TOM);
    {
        static uint8_t junk[sizeof(upr_rec_t)];
        uint32_t i;
        for (i = 0; i < sizeof junk; i++)
            junk[i] = (uint8_t)(i * 29u + 3u);
        st_save(OBJ_UPRESET0 + 1u, junk, 1284u);
        power_cycle();
        check("another record in the object (UPR1's size): no user preset", preset_user_free() == 0 &&
              !preset_get(user_id(1), &p) && ml_count() == 2u);
        preset_user_save(1, &trk[3]);
        st_save(OBJ_UPRESET0 + 1u, junk, sizeof junk);
        power_cycle();
        check("another record in the object (UPR2's size): no user preset", preset_user_free() == 0 &&
              !preset_get(user_id(1), &p) && ml_count() == 2u);
    }
}

/* an FDRC record (the 12th format): the hats with CHOKE 1, an edited knob */
static void test_fdrc_converts(void)
{
    static project_vc_t old;
    uint32_t k, ok = 1;
    fresh();
    trk[3].p[P_CHOKE] = trk[4].p[P_CHOKE] = 1;
    trk[3].p[P_E1] = 33;
    trk[4].p[P_E2] = 101;
    memset(&old, 0, sizeof old);
    old.magic = PROJ_MAGIC_VC;
    old.size = sizeof old;
    for (k = 0; k < G_NSTORED; k++)
        old.g[k] = song.g[k];
    for (k = 0; k < NTRK; k++) {
        memcpy(old.t[k].p, trk[k].p, sizeof old.t[k].p);
        memcpy(old.t[k].step, trk[k].step, sizeof old.t[k].step);
    }
    old.sum = proj_hash(&old, sizeof old - 4u);
    check("(the FDRC record: 3688 B)", sizeof old == 3688u);
    st_save(OBJ_PROJECT0 + 1u, &old, sizeof old);
    proj_slot[1].magic = 0;
    host_init();                                     /* a fresh kit: no choke, every track on a preset */
    check("loads", proj_load(1, 1) == 1);
    check("the hats keep CHOKE 1", trk[3].p[P_CHOKE] == 1 && trk[4].p[P_CHOKE] == 1);
    for (k = 0; k < NTRK; k++)
        ok &= trk[k].pre == PRE_NONE && !trk[k].pre_ed && !memcmp(trk[k].p, old.t[k].p, sizeof old.t[k].p);
    check("every track: no preset, not edited, every parameter as stored (sound unchanged)", ok);
    check("the converted slot is an FDRE one now", proj_slot[1].magic == PROJ_MAGIC && proj_ok(&proj_slot[1]));
}

static void test_fdrd_round_trip(void)
{
    uint32_t k, ok = 1;
    fresh();
    check("(FDRE: 3840 B, one flash sector)", sizeof(project_t) == 3840u && sizeof(project_t) <= ST_PAYLOAD_MAX);
    preset_load(&trk[2], preset_first_of(DM_SDEFM));
    preset_load(&trk[5], preset_first_of(DM_MEMB));
    trk[5].pre_ed = 1;
    preset_user_save(3, &trk[6]);
    preset_load(&trk[6], user_id(3));
    trk[1].pre = PRE_NONE;
    project_save(2);
    for (k = 0; k < NTRK; k++) {
        trk[k].pre = 7;
        trk[k].pre_ed = 1;
    }
    proj_slot[2].magic = 0;                          /* from flash */
    check("loads", proj_load(2, 1) == 1);
    ok = trk[2].pre == preset_first_of(DM_SDEFM) && !trk[2].pre_ed && trk[5].pre == preset_first_of(DM_MEMB) &&
         trk[5].pre_ed && trk[6].pre == user_id(3) && !trk[6].pre_ed && trk[1].pre == PRE_NONE && !trk[1].pre_ed;
    check("each track's preset and edited mark saved and loaded (factory, edited, user, none)", ok);
    proj_slot[2].t[0].p[P_MODEL] = DM_K808;          /* a stored factory reference that is not the track's voice */
    proj_slot[2].pre[0] = (uint16_t)preset_rec_enc(preset_first_of(DM_KTRX));
    proj_slot[2].pre[1] = 0x7FFF;                    /* a uid no preset has */
    proj_slot[2].pre_ed[1] = 9;
    proj_slot[2].sum = proj_sum(&proj_slot[2]);
    proj_load(2, 1);
    check("a factory reference of another voice, an unknown uid: no preset; a stray mark: 0/1",
          trk[0].pre == PRE_NONE && trk[1].pre == PRE_NONE && trk[1].pre_ed == 1);
}

/* review fix 1: a project stores a stable reference (FDRD): a factory preset by its uid (PRE_UID, tools/presets.json,
 * append-only), a user slot as 0x8000 | slot; the ids in RAM stay positional. A later firmware whose banks grew and
 * were reordered (simulated: two new presets first, the rest reversed) finds the same factory preset and user slot */
static void test_preset_ref_stable(void)
{
    static uint16_t auid[PRE_NFACT + 2u];
    static preset_t apre[PRE_NFACT + 2u];
    uint32_t fa = preset_first_of(DM_SDEFM), na = PRE_NFACT + 2u, i, a, u, ok = 1;
    fresh();
    preset_load(&trk[2], fa);
    preset_user_save(3, &trk[6]);
    preset_load(&trk[6], user_id(3));
    trk[1].pre = PRE_NONE;
    project_save(2);
    check("the record: a factory preset by its uid, a user slot as 0x8000 | slot, none as none",
          proj_slot[2].pre[2] == PRE_UID[fa] && proj_slot[2].pre[6] == (0x8000u | 3u) &&
              proj_slot[2].pre[1] == (uint16_t)PRE_NONE);
    auid[0] = 900;                                   /* the later firmware: two new presets first .. */
    auid[1] = 901;
    apre[0] = apre[1] = PRE_FACTORY[0];
    for (i = 0; i < PRE_NFACT; i++) {                /* .. the old ones reversed */
        auid[2u + i] = PRE_UID[PRE_NFACT - 1u - i];
        apre[2u + i] = PRE_FACTORY[PRE_NFACT - 1u - i];
    }
    a = preset_rec_dec_in(proj_slot[2].pre[2], auid, na);
    u = preset_rec_dec_in(proj_slot[2].pre[6], auid, na);
    check("(the simulated banks move it: its old id names another preset there)",
          strcmp(apre[fa].name, PRE_FACTORY[fa].name) != 0);
    check("other banks: the factory reference finds the same preset", a < na &&
          !strcmp(apre[a].name, PRE_FACTORY[fa].name) && apre[a].model == PRE_FACTORY[fa].model &&
          !memcmp(apre[a].k, PRE_FACTORY[fa].k, sizeof apre[a].k));
    check("other banks: the user reference finds the same slot", u == na + 3u);
    check("a removed preset (its uid gone): no preset", preset_rec_dec_in(PRE_UID[fa], auid + 2u, fa) == PRE_NONE);
    check("an unknown uid, uid 0, a user slot out of range, none: no preset",
          preset_rec_dec(0x7FFFu) == PRE_NONE && preset_rec_dec(0) == PRE_NONE &&
              preset_rec_dec(0x8000u | PRE_NUSER) == PRE_NONE && preset_rec_dec(PRE_NONE) == PRE_NONE);
    for (i = 0; i < PRE_NID; i++)
        ok &= preset_rec_dec(preset_rec_enc(i)) == i;
    check("every id: encoded and decoded back; none stays none", ok && preset_rec_enc(PRE_NONE) == PRE_NONE);
    proj_slot[2].magic = 0;
    for (i = 0; i < NTRK; i++)
        trk[i].pre = 7;
    check("loads (from flash)", proj_load(2, 1) == 1);
    check("loaded: the same factory preset, user slot and none", trk[2].pre == fa && trk[6].pre == user_id(3) &&
          trk[1].pre == PRE_NONE);
}

static void test_user_slot_reused(void)
{
    preset_t p;
    fresh();
    drum_set_model(&trk[5], DM_TOM);
    preset_user_save(3, &trk[0]);                    /* K909 */
    preset_load(&trk[1], user_id(3));
    preset_load(&trk[2], user_id(3));
    trk[2].p[P_E1] = 12;
    preset_touch(&trk[2]);
    preset_user_save(3, &trk[5]);                    /* overwritten with a TOM */
    preset_get(user_id(3), &p);
    check("overwritten with another voice: the tracks keep their voice, knobs and id; shown as no preset (\"-\", "
          "never the TOM's name)",
          trk[1].p[P_MODEL] == DM_K909 && trk[1].pre == user_id(3) && preset_of(&trk[1]) == PRE_NONE &&
              !strcmp(name_of(preset_of(&trk[1])), "-") && trk[2].p[P_E1] == 12 && trk[2].pre == user_id(3) &&
              trk[2].pre_ed && preset_of(&trk[2]) == PRE_NONE && p.model == DM_TOM);
    preset_load(&trk[5], user_id(3));
    check("a TOM track loaded from it: its name (\"TOM 04\")", preset_of(&trk[5]) == user_id(3) &&
          !strcmp(name_of(preset_of(&trk[5])), "TOM 04"));
    preset_user_save(3, &trk[0]);                    /* the K909 back: the K909 tracks' name again */
    check("overwritten with the same voice: the slot's name", preset_of(&trk[1]) == user_id(3) &&
          !strcmp(name_of(preset_of(&trk[1])), "K909 04") && preset_of(&trk[5]) == PRE_NONE);
    preset_user_delete(3);
    check("deleted: the tracks keep their knobs and id; the name shown is \"-\"",
          trk[1].p[P_MODEL] == DM_K909 && trk[1].pre == user_id(3) && preset_of(&trk[1]) == PRE_NONE &&
              !strcmp(name_of(trk[1].pre), "-") && !strcmp(name_of(PRE_NONE), "-") && preset_of(&trk[5]) == PRE_NONE);
    check("stepping on from the deleted slot works", preset_step(&trk[1], trk[1].pre, 1) == 0u);
}

/* review fix 3: SAVE while MOTION plays saves the knob as set (motion's base), not the value motion moved it to */
static void test_save_while_motion_plays(void)
{
    track_t *t = &trk[0];
    preset_t p;
    fresh();
    t->p[P_E1] = 40;
    t->p[P_E2] = 50;
    motion_add(0, 0, P_E1, 100);
    mo.b.on = 1u;
    transport_req = 1;
    render_mix(0, 0, CTL * 8);
    check("(playing, motion moved DECAY off its base)", song.playing && (mo.base_ok & 1u) && t->p[P_E1] == 100 &&
          mo.base[0][P_E1] == 40);
    preset_user_save(5, t);
    check("SAVE while motion plays: the knobs as set (the base), not motion's value",
          preset_get(user_id(5), &p) && p.k[1] == 40 && p.k[2] == 50 && p.model == (uint32_t)t->p[P_MODEL]);
    transport_req = 2;
    render_mix(0, 0, CTL * 4);
    memset(&mo.b, 0, sizeof mo.b);
}

/* parameter locks (2026-10-24): SAVE during a locked step saves the knob's own value, not the lock's */
static void test_save_during_lock(void)
{
    track_t *t = &trk[0];
    preset_t p;
    fresh();
    t->p[P_E2] = 50;
    t->step[0].on = 1;
    lock_set(0, 0, P_E2, 99);
    transport_req = 1;
    render_mix(0, 0, CTL * 8);
    check("(playing, the lock on step 0 holds E2 at 99)", song.playing && t->seq_idx == 0u && t->p[P_E2] == 99);
    preset_user_save(6, t);
    check("SAVE during a locked step: the knob's own value (50), not the lock's",
          preset_get(user_id(6), &p) && p.k[2] == 50);
    transport_req = 2;
    render_mix(0, 0, CTL * 4);
    memset(&mo.b, 0, sizeof mo.b);
}

static void test_edit_marks(void)
{
    track_t *t = &trk[0];
    uint32_t id = preset_first_of(DM_SDTRX);
    fresh();
    track_select(0);
    preset_load(t, id);
    t->p[P_LFO1 + LF_DEST] = 2;                      /* DECAY */
    t->p[P_LFO1 + LF_DEPTH] = 64;
    t->p[P_LFO1 + LF_RATE] = 127;
    t->p[P_LFO1 + LF_MODE] = LM_HZ;
    transport_req = 1;
    render_mix(0, 0, CTL * 64);
    transport_req = 2;
    render_mix(0, 0, CTL * 4);
    check("an LFO on DECAY: not edited", !t->pre_ed && t->pre == id);
    turn(EN_K1 + 1, 3);                              /* HOME: knob 2 = DECAY */
    frames(1);
    check("a SOUND knob turned on HOME: edited", t->pre_ed && t->pre == id);
    preset_load(t, id);
    t->p[P_E1] = (int16_t)track_desc(t, P_E1)->max;
    turn(EN_K1 + 1, 3);                              /* at its limit: nothing changes */
    frames(1);
    check("HOME: a knob turned on at its limit (nothing changed): not edited", !t->pre_ed &&
          t->p[P_E1] == track_desc(t, P_E1)->max);
    preset_load(t, id);
    check("loaded again: not edited", !t->pre_ed);
    ml_set(t, P_LEVEL, 10);
    check("a learned CC on LEVEL: not edited", !t->pre_ed);
    ml_set(t, P_E2, 3);
    check("a learned CC on a SOUND knob: edited", t->pre_ed);
    preset_load(t, id);
    open_family(FAM_SND);                            /* EDIT 1: MODEL TUNE DECAY TONE */
    frames(1);
    turn(EN_K1 + 3, -2);                             /* (HARDSNAP's TONE is at its top: down) */
    frames(1);
    check("a SOUND page knob (TONE): edited", t->pre_ed && t->p[P_MODEL] == DM_SDTRX);
    preset_load(t, id);
    t->p[P_E2] = (int16_t)track_desc(t, P_E2)->max;
    turn(EN_K1 + 3, 2);                              /* TONE at its limit */
    frames(1);
    check("a SOUND page knob turned on at its limit (nothing changed): not edited", !t->pre_ed &&
          t->p[P_E2] == track_desc(t, P_E2)->max);
    preset_load(t, id);
    turn(EN_K1 + 0, 1);                              /* MODEL */
    frames(1);
    check("MODEL turned on the EDIT page: the next voice's first preset, not edited", !t->pre_ed &&
          t->p[P_MODEL] == DM_SDTRX + 1 && t->pre == preset_first_of(DM_SDTRX + 1u));
    preset_load(t, id);
    model_step(1);
    check("MODEL turned: the new voice's first preset, not edited", !t->pre_ed &&
          t->pre == preset_first_of((uint32_t)t->p[P_MODEL]));
}

/* fix 8 (owner, 2026-10-21): the factory banks now run MACHINEDRUM, MODERN, CLASSIC, so MODERN's and CLASSIC's ids moved;
 * a project stores uids, never ids (presets.c preset_rec_enc): every 0.22.0 factory preset's uid (the frozen fixture,
 * tests/preset_old_022.h) finds the same preset here, and a 0.22.0 FDRD record with presets of every bank loads onto
 * the same presets and sound, unchanged */
#include "preset_old_022.h"
static void test_bank_order_old_refs(void)
{
    static const uint32_t PICK[NTRK] = {0, 54, 55, 110, 111, 150, 60, 120};   /* 0.22.0 ids: each bank's ends */
    uint32_t i, k, moved = 0, ok = 1;
    preset_t p;
    for (i = 0; i < PRE022_N; i++) {
        uint32_t id = preset_rec_dec(PRE022[i].uid);
        ok &= id < PRE_NFACT && preset_get(id, &p) && !strcmp(p.name, PRE022[i].name) && p.model == PRE022[i].model &&
              preset_rec_enc(id) == PRE022[i].uid;
        moved += id != i;
        if (!ok) {
            printf("      0.22.0 id %u %s\n", i, PRE022[i].name);
            break;
        }
    }
    check("every 0.22.0 factory preset's uid: the same preset (name, voice) in the new banks", ok && PRE022_N == PRE_NFACT);
    check("(the reorder moved MODERN's and CLASSIC's ids: 96 of them; MACHINEDRUM's 55 stay)", moved == 96u);
    fresh();
    for (k = 0; k < NTRK; k++)                       /* a 0.22.0 project: the presets of every bank, one edited */
        preset_load(&trk[k], preset_rec_dec(PRE022[PICK[k]].uid));
    trk[6].p[P_E1] = 3;
    trk[6].pre_ed = 1;
    project_save(1);
    for (k = 0; k < NTRK; k++)                       /* the record holds the 0.22.0 uids (what 0.22.0 wrote) */
        ok &= proj_slot[1].pre[k] == PRE022[PICK[k]].uid;
    check("the record: each track's preset by its 0.22.0 uid", ok);
    proj_slot[1].magic = 0;
    host_init();
    check("loads (from flash)", proj_load(1, 1) == 1);
    for (k = 0, ok = 1; k < NTRK; k++) {
        preset_name_of(trk[k].pre, (char *)&p.name);
        ok &= !strcmp(p.name, PRE022[PICK[k]].name) && (uint32_t)trk[k].p[P_MODEL] == PRE022[PICK[k]].model &&
              trk[k].pre_ed == (k == 6u);
    }
    check("loaded: every track on its 0.22.0 preset (name, voice), the edited mark and knob kept",
          ok && trk[6].p[P_E1] == 3);
}


/* 2026-11-01: a user preset holds the voice and the track's FX but the SLICER: the record "UPR2", 96 slots */
static void test_record_layout(void)
{
    uint32_t i, j, ok = UPR_NFX == NFXI;
    for (i = 0; ok && i < NFXI; i++) {
        for (j = 0; j < UPR_NFX && UPR_FX[j] != FXI[i]; j++)
            ;
        ok &= j < UPR_NFX && TP[FXI[i]].min >= -128 && TP[FXI[i]].max <= 127;
    }
    check("the FX stored: DIST CHO DLY REV, RESON (MODEL TUNE DECAY MIX TONE STRCT POS KEYF), FILTER (TYPE CUT RESO ENV "
          "DECAY), INSERT (TYPE A B C MIX), SRR; each fits a byte", ok);
    check("not stored: SLICER, LAYER, LFOs and their DEST slots, LEVEL, PAN, NOTE, CHOKE, PRIO",
          !is_fx(P_SLCR) && !is_fx(P_SLPAT) && !is_fx(P_LSET) && !is_fx(P_LFO1 + LF_DEST) && !is_fx(P_LS3DEST) &&
              !is_fx(P_LEVEL) && !is_fx(P_PAN) && !is_fx(P_NOTE) && !is_fx(P_CHOKE) && !is_fx(P_PRIO));
    check("the record: 4 + 96 x 33 + 32 = 3204 B, <= 3584 (one sector, below the loader's scan)",
          sizeof(upr_rec_t) == 3204u && sizeof(upr_rec_t) <= 3584u && sizeof(upr_rec_t) <= ST_PAYLOAD_MAX &&
              PRE_NUSER == 96u);
}

static void test_user_fx_round_trip(void)
{
    static int16_t want[P_COUNT];
    track_t before, *u = &trk[2];
    preset_t p;
    uint32_t i, ok;
    fresh();
    fx_fill(&trk[1], 1);
    trk[1].p[P_SLCR] = 2;
    trk[1].p[P_LEVEL] = 50;
    trk[1].p[P_E1] = 33;
    memcpy(want, trk[1].p, sizeof want);
    check("SAVE: the voice and the FX into the slot", preset_user_save(4, &trk[1]) == 0 && upr.s[4].used == 1u &&
          slot_fx(4, P_FCUT) == trk[1].p[P_FCUT] && slot_fx(4, P_ITYPE) == trk[1].p[P_ITYPE] && flash_has(4));
    fx_fill(u, 2);
    u->p[P_SLCR] = 1;
    u->p[P_LEVEL] = 77;
    u->p[P_PAN] = -20;
    u->p[P_NOTE] = 50;
    u->p[P_CHOKE] = 3;
    u->p[P_PRIO] = 1;
    u->p[P_LFO1 + LF_DEST] = 2;
    u->p[P_LS3DEST] = 4;
    u->p[P_LS4DEST] = 27;                            /* an appended DEST (the INSERT's C) is kept too */
    u->p[P_LSET] = 1;
    before = *u;
    ok = preset_load(u, user_id(4)) == 0 && preset_get(user_id(4), &p) && knobs_are(u, &p) && fx_same(u->p, want);
    for (i = 0; i < P_COUNT; i++)
        if ((i < P_MODEL || i > P_E7) && !is_fx(i))
            ok &= u->p[i] == before.p[i];
    check("a user load: the voice and the FX = the slot's; SLICER, LAYER, LFOs, slots, LEVEL, PAN, NOTE, CHOKE, PRIO "
          "unchanged", ok && u->pre == user_id(4) && !u->pre_ed);
    power_cycle();
    fx_fill(&trk[3], 7);
    check("power cycle: the FX back from flash, loaded onto another track", preset_load(&trk[3], user_id(4)) == 0 &&
          fx_same(trk[3].p, want) && trk[3].p[P_E1] == 33 && trk[3].p[P_SLCR] == 0 && trk[3].p[P_LEVEL] != 50);
}

/* RESON's caps on a user load (as a project load: CHORD / MODAL over their cap STRNG, RESON past 4 tracks OFF) */
static void test_user_fx_caps(void)
{
    uint32_t k;
    fresh();
    fx_fill(&trk[6], 3);
    trk[6].p[P_RMODEL] = RS_CHORD;
    preset_user_save(0, &trk[6]);
    trk[6].p[P_RMODEL] = RS_OFF;
    for (k = 0; k < 4u; k++)
        trk[k].p[P_RMODEL] = RS_STRNG;
    preset_load(&trk[7], user_id(0));
    check("RESON on 4 other tracks: a user load leaves RESON OFF (the rest of its FX loaded)",
          trk[7].p[P_RMODEL] == RS_OFF && trk[7].p[P_FCUT] == trk[6].p[P_FCUT]);
    trk[0].p[P_RMODEL] = trk[1].p[P_RMODEL] = RS_CHORD;
    trk[2].p[P_RMODEL] = trk[3].p[P_RMODEL] = RS_OFF;
    preset_load(&trk[7], user_id(0));
    check("CHORD on 2 other tracks: the preset's CHORD loads as STRNG", trk[7].p[P_RMODEL] == RS_STRNG);
    trk[0].p[P_RMODEL] = trk[1].p[P_RMODEL] = RS_OFF;
    preset_load(&trk[7], user_id(0));
    check("room again: CHORD", trk[7].p[P_RMODEL] == RS_CHORD);
}

/* MOTION playing an FX knob: a user load makes the preset's value the knob's base (STOP keeps it); SAVE stores the
 * base, not motion's value */
static void test_user_fx_motion(void)
{
    track_t *t = &trk[0];
    fresh();
    trk[1].p[P_FCUT] = 30;
    preset_user_save(2, &trk[1]);
    t->p[P_FCUT] = 100;
    motion_add(0, 0, P_FCUT, 10);
    mo.b.on = 1u;
    transport_req = 1;
    render_mix(0, 0, CTL * 8);
    check("(playing, motion moved CUT off its base)", song.playing && t->p[P_FCUT] == 10 && mo.base[0][P_FCUT] == 100);
    preset_user_save(3, t);
    check("SAVE while motion plays: CUT as set (100), not motion's 10", slot_fx(3, P_FCUT) == 100);
    preset_load(t, user_id(2));
    check("a user load while motion plays: CUT the preset's, and its base", t->p[P_FCUT] == 30 &&
          mo.base[0][P_FCUT] == 30);
    transport_req = 2;
    render_mix(0, 0, CTL * 4);
    check("STOP: CUT back to the base, the preset's", !song.playing && t->p[P_FCUT] == 30);
    memset(&mo.b, 0, sizeof mo.b);
}

/* the PRESETS list: the audition of a user preset plays its FX too; a factory one after it the track's own; leaving the
 * list puts the track's FX back; LOAD keeps them */
static void test_user_fx_audition(void)
{
    static int16_t own[P_COUNT], up[P_COUNT];
    track_t *t = &trk[0];
    fresh();
    track_select(0);
    fx_fill(&trk[1], 1);
    memcpy(up, trk[1].p, sizeof up);
    preset_user_save(0, &trk[1]);
    fx_fill(t, 9);
    memcpy(own, t->p, sizeof own);
    preset_list_open();
    pl_move((int32_t)user_id(0));
    check("the list's audition of a user preset: its voice and FX", ui.pr.pv && fx_same(t->p, up) &&
          t->p[P_MODEL] == up[P_MODEL]);
    pl_move(0);
    check("then a factory preset: its voice, the track's own FX", fx_same(t->p, own) && t->pre == 0u);
    pl_move((int32_t)user_id(0));
    pl_close();
    check("the list left (no LOAD): the track's own voice and FX back", !pl_on() && fx_same(t->p, own) &&
          !memcmp(t->p, own, sizeof own));
    preset_list_open();
    pl_move((int32_t)user_id(0));
    pl_load();
    check("LOAD: the user preset's FX kept", !pl_on() && fx_same(t->p, up) && t->pre == user_id(0) && !t->pre_ed);
}

/* the edited mark: a track loaded from a user preset (it holds the FX) is edited by an FX knob too; from a factory
 * preset (or an older user preset that holds no FX) by the SOUND knobs only */
static void fx_page(uint32_t first)
{
    uint32_t n;
    for (n = 0; n < 12u && (ui.home || cur_page()->fam != FAM_FX || page_id(cur_page(), 0) != first); n++)
        open_family(FAM_FX);
    frames(1);
}
static void test_edit_marks_fx(void)
{
    track_t *t = &trk[0];
    fresh();
    track_select(0);
    t->p[P_FCUT] = 60;
    t->p[P_SLCR] = 0;
    preset_user_save(0, t);
    preset_load(t, user_id(0));
    fx_page(P_FTYPE);
    turn(EN_K1 + 1, -3);                             /* FILTER CUT */
    frames(1);
    check("loaded from a user preset: FILTER CUT turned: edited", t->pre_ed && t->p[P_FCUT] != 60);
    preset_load(t, user_id(0));
    check("loaded again: not edited, CUT the preset's", !t->pre_ed && t->p[P_FCUT] == 60);
    fx_page(P_SLCR);
    turn(EN_K1 + 0, 1);                              /* SLICER: not in a preset */
    frames(1);
    check("SLICER turned: not edited", !t->pre_ed && t->p[P_SLCR] != 0);
    fx_page(P_RMODEL);
    turn(EN_K1 + 3, 2);                              /* RESON MIX */
    frames(1);
    check("RESON MIX turned: edited", t->pre_ed);
    preset_load(t, user_id(0));
    ml_set(t, P_DLY, 90);
    check("a learned CC on DLY: edited", t->pre_ed);
    preset_load(t, user_id(0));
    ml_set(t, P_LEVEL, 10);
    check("a learned CC on LEVEL: not edited", !t->pre_ed);
    preset_load(t, preset_first_of((uint32_t)t->p[P_MODEL]));
    fx_page(P_FTYPE);
    turn(EN_K1 + 1, -3);
    frames(1);
    check("loaded from a factory preset: FILTER CUT turned: not edited", !t->pre_ed);
    ml_set(t, P_DLY, 20);
    check(".. a learned CC on DLY: not edited", !t->pre_ed);
}

/* an older bank ("UPR1": 128 slots, the voice only) at the first boot: slots 1..96 stay, 97..128's used ones move into
 * the lowest empty slots (named by their new slot), what cannot fit is dropped and said once; they all load as the
 * voice only (the track's FX kept); projects' references follow the moves */
typedef struct {
    uint32_t magic;
    struct { uint8_t used, model; int8_t k[8]; } s[128];
} upr1_t;
static upr1_t u1;
static void u1_use(uint32_t i)                       /* old slot i: a K909, DECAY = i (its mark) */
{
    uint32_t k;
    u1.s[i].used = 1;
    u1.s[i].model = DM_K909;
    for (k = 0; k < 8u; k++)
        u1.s[i].k[k] = (int8_t)DMODELS[DM_K909].edit[k].def;
    u1.s[i].k[1] = (int8_t)i;
}
static void u1_put(void)
{
    u1.magic = 0x31525055u;
    check("(the UPR1 record: 1284 B)", sizeof u1 == 1284u && st_save(OBJ_UPRESET0 + 1u, &u1, sizeof u1) == 0);
}
static int slot_is_old(uint32_t slot, uint32_t old)  /* user slot holds old slot `old`, the voice only */
{
    preset_t p;
    return preset_get(user_id(slot), &p) && p.model == DM_K909 && p.k[1] == (int8_t)old && upr.s[slot].used == 3u;
}
static void test_migrate_fits(void)
{
    static const uint32_t OLD[6] = {0, 2, 5, 96, 100, 127};
    static int16_t own[P_COUNT];
    uint32_t i, ok;
    preset_t p;
    fresh();
    memset(&u1, 0, sizeof u1);
    for (i = 0; i < 6u; i++)
        u1_use(OLD[i]);
    u1.s[50].used = 1;                               /* a broken slot (no such model): empty */
    u1.s[50].model = 200;
    u1_put();
    power_cycle();
    ok = slot_is_old(0, 0) && slot_is_old(2, 2) && slot_is_old(5, 5) && slot_is_old(1, 96) && slot_is_old(3, 100) &&
         slot_is_old(4, 127) && !preset_get(user_id(50), &p) && preset_user_free() == 6;
    check("UPR1 migrated: 1..96 kept; 97, 101, 128 into the lowest empty slots (2, 4, 5); a broken slot empty", ok);
    check("their names follow the new slot (\"K909 02\", \"K909 05\")", !strcmp(name_of(user_id(1)), "K909 02") &&
          !strcmp(name_of(user_id(4)), "K909 05"));
    check("nothing dropped: no PRESETS LOST", !said("1 PRESET LOST", 120) && !(persist_note & PN_UPR));
    check("written as UPR2 at once", flash_has(1) && flash_has(4) && !flash_has(6));
    check("project references follow: old 101 -> 4, 97 -> 2, 128 -> 5, 6 stays, an empty old 98 -> none, 129 -> none",
          preset_rec_dec(0x8000u | 100u) == user_id(3) && preset_rec_dec(0x8000u | 96u) == user_id(1) &&
              preset_rec_dec(0x8000u | 127u) == user_id(4) && preset_rec_dec(0x8000u | 5u) == user_id(5) &&
              preset_rec_dec(0x8000u | 97u) == PRE_NONE && preset_rec_dec(0x8000u | 128u) == PRE_NONE);
    fx_fill(&trk[2], 4);
    memcpy(own, trk[2].p, sizeof own);
    check("a migrated preset loads as the voice only: the track's FX kept", preset_load(&trk[2], user_id(3)) == 0 &&
          trk[2].p[P_E1] == 100 && fx_same(trk[2].p, own));
    track_select(2);
    fx_page(P_FTYPE);
    turn(EN_K1 + 1, -3);
    frames(1);
    check(".. its FX turned: not edited (it holds none)", !trk[2].pre_ed);
    for (i = 0; i < NTRK; i++)                       /* a project saved before: old slots 101 and 98, 6 */
        drum_set_model(&trk[i], DM_K909);
    project_save(1);
    proj_slot[1].pre[3] = 0x8000u | 100u;
    proj_slot[1].pre[4] = 0x8000u | 97u;
    proj_slot[1].pre[5] = 0x8000u | 5u;
    proj_slot[1].sum = proj_sum(&proj_slot[1]);
    check("a project of the older bank loads (from RAM)", proj_load(1, 1) == 1);
    check(".. its tracks on the moved slot (old 101: \"K909 04\"), none (old 98, empty), 6 as it was",
          trk[3].pre == user_id(3) && !strcmp(name_of(preset_of(&trk[3])), "K909 04") && trk[4].pre == PRE_NONE &&
              trk[5].pre == user_id(5));
    power_cycle();
    check("power cycle: the moves kept (no second migration), references still follow",
          slot_is_old(1, 96) && slot_is_old(4, 127) && preset_rec_dec(0x8000u | 100u) == user_id(3) &&
              preset_user_free() == 6);
    fx_fill(&trk[1], 2);
    preset_user_save(1, &trk[1]);
    check("a new SAVE over a migrated slot holds the FX", upr.s[1].used == 1u && slot_fx(1, P_FCUT) == trk[1].p[P_FCUT]);
}
static void test_migrate_overflow(void)
{
    uint32_t i;
    preset_t p;
    fresh();
    memset(&u1, 0, sizeof u1);
    for (i = 0; i < 101u; i++)                       /* 1..96 used but 11 and 21; 97..101 used */
        if (i != 10u && i != 20u)
            u1_use(i);
    u1_put();
    power_cycle();
    check("a full older bank: 97 and 98 into 11 and 21, 99..101 dropped, every slot used",
          slot_is_old(10, 96) && slot_is_old(20, 97) && preset_user_free() == -1 && slot_is_old(95, 95));
    check("said once: 3 PRESETS LOST", said("3 PRESETS LOST", 200));
    check("references: old 98 -> 21, a dropped one (99) -> none", preset_rec_dec(0x8000u | 97u) == user_id(20) &&
          preset_rec_dec(0x8000u | 98u) == PRE_NONE && preset_rec_dec(0x8000u | 100u) == PRE_NONE);
    power_cycle();
    check("power cycle: not said again, the bank as migrated", !said("3 PRESETS LOST", 200) &&
          slot_is_old(20, 97) && preset_get(user_id(95), &p));
    memset(&u1, 0, sizeof u1);
    u1_use(97);
    u1_use(110);
    u1_put();
    power_cycle();
    check("an older bank with only 98 and 111: into slots 1 and 2", slot_is_old(0, 97) && slot_is_old(1, 110));
    memset(&u1, 0, sizeof u1);
    for (i = 0; i < 97u; i++)
        u1_use(i);
    u1_put();
    power_cycle();
    check("one dropped: \"1 PRESET LOST\"", said("1 PRESET LOST", 200));
}

int main(void)
{
    test_defaults_are_first_presets();
    test_load_sets_voice_only();
    test_model_change_keeps_choke();
    test_no_default_choke();
    test_step_order_and_wrap();
    test_user_save_load_delete();
    test_user_save_deferred_and_cut();
    test_fdrc_converts();
    test_fdrd_round_trip();
    test_preset_ref_stable();
    test_user_slot_reused();
    test_save_while_motion_plays();
    test_save_during_lock();
    test_edit_marks();
    test_bank_order_old_refs();
    test_record_layout();
    test_user_fx_round_trip();
    test_user_fx_caps();
    test_user_fx_motion();
    test_user_fx_audition();
    test_edit_marks_fx();
    test_migrate_fits();
    test_migrate_overflow();
    printf(fails ? "preset_test: %d FAILED\n" : "preset_test: all passed\n", fails);
    return fails ? 1 : 0;
}
