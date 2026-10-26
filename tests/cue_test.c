/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Live cue, variations, FILL, slot copy (spec docs/superpowers/specs/2026-10-26-cue-variations-fill-copy-design.md):
 * the variations' persistence (their flash objects, the record's link, flash as the truth after a reset), the RAM
 * model (write-back, unsaved marks, park), the cue engine and the slot copy, with the firmware's real code on a host
 * flash (tests/flash_host.h), built with ASan + UBSan. */
#define FELUCCA_FLASH 1
#include <stdint.h>
static uint8_t hflash[0x100000];
#define SMP_USER_XIP(k) ((const uint8_t *)hflash + 0xA0000u + (k) * 0x14000u)
#define VAR_XIP(off) ((const void *)(hflash + (off)))
#include "ui_host.h"

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
static uint32_t rs = 1;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static void rfill(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = (uint8_t)(rnd() >> 24);
}

/* ---- helpers */
static step_t VS[NTRK][NSTEP], VR[NTRK][NSTEP];
static int16_t VTM[NTRK][4], VTR[NTRK][4];
static const step_t *VSP[NTRK];
static step_t *VRP[NTRK];
static void vptrs(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        VSP[k] = VS[k];
        VRP[k] = VR[k];
    }
}
static void var_make(uint32_t seed, uint32_t per_mille)   /* VS / VTM: a variation's content */
{
    uint32_t k, i;
    rs = seed;
    memset(VS, 0, sizeof VS);
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < NSTEP; i++)
            if ((rnd() >> 8) % 1000u < per_mille) {
                VS[k][i].on = 1;
                VS[k][i].acc = (rnd() >> 9) & 1u;
                VS[k][i].rat = (rnd() >> 9) & 3u;
                VS[k][i].nt = (int8_t)((rnd() >> 8) % 24u);
            }
        VTM[k][0] = (int16_t)(1u + (seed + k) % 64u);
        VTM[k][1] = (int16_t)((seed + k) % 10u);
        VTM[k][2] = (int16_t)((seed * 3u + k) % 101u);
        VTM[k][3] = 0;
    }
}
static int var_is(const var_bank_t *b, uint32_t v)   /* variation v of b decodes to VS / VTM */
{
    uint32_t k, i;
    if (var_get(b, v, VRP, VTR) != 1 || memcmp(VTM, VTR, sizeof VTM))
        return 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++)
            if (memcmp(&VS[k][i], &VR[k][i], sizeof(step_t)))
                return 0;
    return 1;
}
static void wipe_ram(void)                            /* a power cycle's RAM: .noinit slots gone, .bss zero */
{
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(lk_slot, 0, sizeof lk_slot);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    var_play = 0;
    var_dirty = 0;
    var_lost = 0;
    lk_lost = 0;
    persist_note = 0;
}
static void fresh(void)                               /* nothing in flash, booted */
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&ui, 0, sizeof ui);
    wipe_ram();
    host_nflog = 0;
    persist_boot();
}
static uint32_t power_on(void)                        /* main.c's boot after a power cycle (.noinit random) */
{
    uint32_t irq;
    rfill(&settings, sizeof settings);
    rfill(proj_slot, sizeof proj_slot);
    memset(&song, 0, sizeof song);
    memset(trk, 0, sizeof trk);
    memset(&ui, 0, sizeof ui);
    memset(&mo, 0, sizeof mo);
    memset(&chain, 0, sizeof chain);
    memset(var_buf, 0, sizeof var_buf);
    memset(var_who, 0, sizeof var_who);
    var_play = 0;
    var_dirty = 0;
    var_lost = 0;
    transport_req = 0;
    persist_boot();
    settings_init();
    panel_init();
    fm1_in.buttons = 0;
    irq = host_irq_on;
    drum_boot_init();
    return host_irq_on - irq;
}
static int all_writes_in_map(void)
{
    uint32_t i, ok = host_nflog <= HOST_NFLOG;
    for (i = 0; ok && i < host_nflog; i++)
        ok &= host_fl_in_map(host_flog[i].off, host_flog[i].n);
    return (int)ok;
}
static var_bank_t *live_bank(void) { return &var_buf[var_play]; }
static const var_bank_t *flash_bank(uint32_t slot)   /* the current copy of slot's variation object (or 0) */
{
    st_hdr_t h;
    int c = st_current(OBJ_VAR0 + slot, &h);
    return c < 0 ? 0 : (const var_bank_t *)(hflash + st_sector(OBJ_VAR0 + slot, (uint32_t)c) + ST_PAYLOAD_OFF);
}

/* ---- Task 3: persistence */
static void var_save_load_round_trip(void)
{
    uint32_t ok;
    const var_bank_t *f;
    fresh();
    project_save(0);                                  /* A exists (its variations empty) */
    ok = var_who[var_play] == 1u;
    var_make(31, 300);
    ok &= var_put(live_bank(), 3, VSP, VTM) == 0;
    project_save(0);
    f = flash_bank(0);
    ok &= f && proj_slot[0].var_link == var_link_of(f) && proj_slot[0].var_len == 8u + f->used;
    wipe_ram();
    persist_boot();
    project_load(0);
    ok &= str_eq(ui.msg, "LOADED") && var_who[var_play] == 1u && var_is(live_bank(), 3) &&
          var_mask(live_bank()) == 1u << 3;
    check("variations: saved with SAVE (their object, then the lock bank, then the record linking both); loaded with "
          "LOAD after a power cycle", (int)ok);
}
/* Review Focus 1: a SAVE torn at any erase / program: the record, the lock bank and the variations of one save */
static void var_torn_keeps_triple(void)
{
    uint32_t n, i, ok = 1, olds = 0, news = 0;   /* (both seen: the tear points cover the whole save) */
    fresh();
    trk[0].p[P_E1] = 11;
    lk_put(&mo.b, lk_key(0, 4, P_E1), 100, 1);
    project_save(1);                                  /* the old triple */
    var_make(41, 200);
    var_put(live_bank(), 2, VSP, VTM);
    project_save(1);
    host_nflog = 0;                                   /* count one save's ops: the new triple */
    trk[0].p[P_E1] = 22;
    lk_put(&mo.b, lk_key(0, 4, P_E1), 50, 1);
    var_make(42, 300);
    var_put(live_bank(), 2, VSP, VTM);
    {
        static uint8_t keep[0x100000];
        static var_bank_t lb;
        static lk_bank_t mb;
        static project_t ps[4];
        int16_t e1 = trk[0].p[P_E1];
        memcpy(keep, hflash, sizeof keep);
        lb = *live_bank();
        mb = mo.b;
        memcpy(ps, proj_slot, sizeof ps);
        project_save(1);
        n = host_nflog;
        for (i = 0; i <= n; i++) {                   /* (n: no failure, the new triple) */
            int r;
            memcpy(hflash, keep, sizeof keep);       /* back to the old triple, the same live state */
            memcpy(proj_slot, ps, sizeof ps);
            *live_bank() = lb;
            var_who[var_play] = 2;
            mo.b = mb;
            trk[0].p[P_E1] = e1;
            host_fail_at = (int)i;
            project_save(1);
            host_fail_at = -1;
            wipe_ram();
            host_init();
            persist_boot();
            project_load(1);
            r = lk_find(&mo.b, lk_key(0, 4, P_E1), 1);
            if (trk[0].p[P_E1] == 11 && r >= 0 && mo.b.val[r] == 100) {
                var_make(41, 200);
                ok &= var_is(live_bank(), 2) && !var_lost && str_eq(ui.msg, "LOADED");
                olds++;
            } else if (trk[0].p[P_E1] == 22 && r >= 0 && mo.b.val[r] == 50) {
                var_make(42, 300);
                ok &= var_is(live_bank(), 2) && !var_lost && str_eq(ui.msg, "LOADED");
                news++;
            } else {
                ok = 0;
            }
            if (!ok) {
                printf("     torn at op %u of %u: E1 %d, msg %s\n", i, n, trk[0].p[P_E1], ui.msg);
                break;
            }
        }
    }
    if (!(n > 20u && olds && news))
        printf("     torn: ops %u old %u new %u\n", n, olds, news);
    check("variations: a SAVE torn at any of its erases / programs loads the old or the new record, lock bank and "
          "variations together, never mixed", ok && n > 20u && olds && news && host_fl_stray == 0);
}
static void var_old_record_empty(void)
{
    uint32_t ok;
    project_t p;
    fresh();
    project_save(2);
    p = proj_slot[2];                                 /* as 0.24.0 saved it: the reserved bytes 0, no variations */
    p.var_link = 0;
    p.var_len = 0;
    p.sum = proj_sum(&p);
    st_save(OBJ_PROJECT0 + 2u, &p, sizeof p);
    wipe_ram();
    persist_boot();
    project_load(2);
    ok = str_eq(ui.msg, "LOADED") && var_mask(live_bank()) == 0u && !var_lost && !(persist_note & PN_VARS) &&
         var_who[var_play] == 3u;
    check("variations: a 0.24.0 record (no variation link) loads, variations 2..8 empty, nothing lost", (int)ok);
}
static void usr_image(uint32_t at, uint32_t data_len) /* an upstream-style user sample slot longer than 48 KiB */
{
    smp_user_hdr_t h;
    uint32_t i;
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = 3;
    memcpy(h.name, "BIGLOOP", 7);
    h.data_len = data_len;
    for (i = 0; i < 3u; i++) {
        smp_zone_t *z = &h.zone[i];
        z->off = i == 0 ? 0x100u : i == 1 ? 0xB000u : 0xC000u;
        z->n = i == 0 ? 0x2000u : i == 1 ? 0x4000u : 0x1000u;
        z->le = z->n - 1u;
        z->rate = 1u << 16;
        z->hi = 127;
    }
    rfill(hflash + at + 512u, 0x14000u - 512u);
    memcpy(hflash + at, &h, sizeof h);
}
static void var_upload_over_vars(void)
{
    uint32_t s, f, ok = 1, n2 = 0, nlost = 0, order = 1;
    char last[32] = "";
    fresh();
    for (s = 0; s < 4u; s++) {
        memset(&mo.b, 0, sizeof mo.b);
        lk_put(&mo.b, lk_key(s, 4, P_E1), (int32_t)(10u + s), 1);
        project_save(s);
        var_make(50 + s, 100);
        var_put(live_bank(), 1, VSP, VTM);
        project_save(s);
    }
    rs = 81;
    usr_image(0xB4000u, 0x13E00u);                    /* over 0xB4000..0xC7FFF: the variations too */
    host_nflog = 0;
    power_on();                                       /* (the last project, D, loads) */
    ok &= usr_nz[1] == 2 && (persist_note & PN_VARS) && var_lost == 0x0Fu && var_mask(live_bank()) == 0u &&
          mo.b.count == 1u && all_writes_in_map();
    for (f = 0; f < 200u; f++) {
        ui_frame();
        if (ui.msg_t && !str_eq(ui.msg, last)) {
            snprintf(last, sizeof last, "%s", ui.msg);
            if (str_eq(ui.msg, "USR2 SHORTENED TO 48K"))
                n2++;
            if (str_eq(ui.msg, "VARIATIONS LOST")) {
                nlost++;
                order &= n2 == 1u;
            }
        }
    }
    ok &= n2 == 1u && nlost == 1u && order;
    project_load(1);
    ok &= str_eq(ui.msg, "LOADED") && mo.b.count == 1u && var_mask(live_bank()) == 0u;
    if (!ok)
        printf("     usr2 over variations: nz %u note %u lost %x mask %x count %u n2 %u nlost %u msg %s\n", usr_nz[1],
               persist_note, var_lost, var_mask(live_bank()), mo.b.count, n2, nlost, ui.msg);
    check("variations: an 80 KiB USR2 over them: shortened, the projects and their locks load, variations empty, "
          "VARIATIONS LOST once after the USR2 note", (int)ok);
}
static void flash_is_truth_after_reset(void)
{
    static project_t want;
    uint32_t ok;
    fresh();
    trk[2].step[5].on = 1;
    project_save(1);
    want = proj_slot[1];
    proj_slot[1].t[2].step[5].on = 0;                 /* RAM changed (as a write-back will), its sum redone */
    proj_slot[1].t[2].step[9].on = 1;
    proj_slot[1].sum = proj_sum(&proj_slot[1]);
    host_init();
    persist_boot();                                   /* a reset: .noinit kept */
    ok = !memcmp(&proj_slot[1], &want, sizeof want);
    check("flash is the truth: after a reset every slot is its flash record again (RAM edits gone)", (int)ok);
}
static void var_writes_in_map(void)
{
    uint32_t s, c, i, ok = 1;
    fresh();
    for (s = 0; s < 4u; s++) {
        project_save(s);
        for (i = 1; i <= 2u; i++) {
            var_make(60 + s * 4u + i, 1000);      /* dense: 1634 B each */
            var_put(live_bank(), i, VSP, VTM);
        }
        project_save(s);
        project_save(s);
    }
    project_rename(2, "MAPPED");
    ok &= all_writes_in_map() && host_fl_stray == 0 && host_nflog > 40u;
    for (i = 0; i < host_nflog && i < HOST_NFLOG; i++)
        if (host_flog[i].off >= 0xB5000u && host_flog[i].off < 0xD4000u)
            ok &= host_flog[i].off >= 0xC0000u && host_flog[i].off + host_flog[i].n <= 0xC8000u;
    for (s = 0; s < 4u; s++)
        for (c = 0; c < 2u; c++)
            for (i = 0xF00u; i < 0x1000u; i++)
                ok &= hflash[st_sector(OBJ_VAR0 + s, c) + i] == 0xFFu;
    check("variations: boot, saves with dense variations and a rename write only inside the map (the variations "
          "inside 0xC0000..0xC7FFF); every variation sector's +0xF00.. erased", (int)ok);
}
static void var_blocked_refuses(void)
{
    uint32_t ok, i, f;
    memset(hflash, 0xFF, sizeof hflash);
    rs = 82;
    usr_image(0xB4000u, 0x13E00u);
    host_fail_at = 0;                                 /* the USR2 clamp's erase fails */
    power_on();
    host_fail_at = -1;
    ok = var_blocked == 1u;
    for (f = 0; f < 60u; f++)
        ui_frame();
    host_nflog = 0;
    project_save(0);
    ok &= str_eq(ui.msg, "USR2 CLAMP FAILED") && host_nflog == 0u;
    project_rename(0, "NOPE");
    for (i = 0; i < host_nflog && i < HOST_NFLOG; i++)
        ok &= !(host_flog[i].off >= 0xC0000u && host_flog[i].off < 0xC8000u);
    check("variations: a failed USR2 clamp: SAVE says USR2 CLAMP FAILED and writes nothing that boot", (int)ok);
}

/* ---- Task 4: the RAM model (write-back, unsaved marks, park, LOAD / SAVE-as / RENAME) */
static void four_slots(void)                          /* A..D saved, each its own LEVEL and a step; A loaded */
{
    uint32_t s;
    fresh();
    for (s = 0; s < 4u; s++) {
        trk[0].p[P_LEVEL] = (int16_t)(10 + 10 * (int32_t)s);
        memset(trk[1].step, 0, sizeof trk[1].step);
        trk[1].step[s].on = 1;
        chain.name[0] = (char)('A' + s);
        chain.name[1] = 0;
        project_save(s);
    }
    project_load(0);
    host_nflog = 0;
}
static int image_is_flash(uint32_t s)                 /* RAM image = the flash record, byte for byte */
{
    static project_t f;
    return st_load(OBJ_PROJECT0 + s, &f, sizeof f) == (int)sizeof f && !memcmp(&f, &proj_slot[s], sizeof f);
}
static void writeback_keeps_edits(void)
{
    uint32_t ok;
    four_slots();
    trk[3].step[7].on = 1;
    trk[2].p[P_LEVEL] = 77;
    ok = slot_dirty(0) && !(proj_dirty & 1u);
    ok &= proj_writeback() == 0;
    ok &= proj_slot[0].t[3].step[7].on == 1u && proj_slot[0].t[2].p[P_LEVEL] == 77 && proj_ok(&proj_slot[0]);
    ok &= (proj_dirty & 1u) && slot_dirty(0) && host_nflog == 0u && !image_is_flash(0);
    check("RAM model: a write-back puts the live edits (a step, a knob) into the loaded slot's image, marks it, no "
          "flash op", (int)ok);
}
static void writeback_rest_values(void)
{
    static track_t tk[NTRK];
    int16_t e0, e1;
    uint32_t ok, i;
    four_slots();
    trk[0].step[0].on = 1;
    trk[0].p[P_E0] = 40;
    trk[0].p[P_E1] = 41;
    motion_add(0, 0, P_E0, 100);                      /* motion moves E0 at step 0 */
    motion_set_play(0, 1);
    lock_set(0, 0, P_E1, 50);                         /* a lock on step 0 */
    transport_req = 1;
    for (i = 0; i < 4u; i++)
        render_mix(0, 0, CTL);
    ok = song.playing && trk[0].p[P_E0] != 40 && trk[0].p[P_E1] == 50;   /* both held now */
    memcpy(tk, trk, sizeof tk);
    {                                                 /* what STOP leaves, on a copy */
        static typeof(mo) mk;
        mk = mo;
        motion_end();
        e0 = trk[0].p[P_E0];
        e1 = trk[0].p[P_E1];
        mo = mk;
        memcpy(trk, tk, sizeof tk);
    }
    ok &= proj_writeback() == 0 && proj_slot[0].t[0].p[P_E0] == e0 && proj_slot[0].t[0].p[P_E1] == e1 && e0 == 40 &&
          e1 == 41 && trk[0].p[P_E0] != 40;
    if (!ok)
        printf("     rest: playing %u E0 %d E1 %d stop-left %d %d image %d %d\n", song.playing, trk[0].p[P_E0],
               trk[0].p[P_E1], e0, e1, proj_slot[0].t[0].p[P_E0], proj_slot[0].t[0].p[P_E1]);
    transport_req = 2;
    render_mix(0, 0, CTL);
    check("RAM model: a write-back while playing stores the knobs at rest (motion's base, a held lock's value under "
          "it), not what they play now", (int)ok);
}
static void writeback_var_not_record(void)
{
    static step_t want1[NTRK][NSTEP];
    uint32_t ok, k;
    four_slots();
    for (k = 0; k < NTRK; k++)
        memcpy(want1[k], proj_slot[0].t[k].step, sizeof want1[k]);
    var_cur = 2;                                      /* playing variation 3: the live steps are its own */
    memset(trk[4].step, 0, sizeof trk[4].step);
    trk[4].step[9].on = 1;
    trk[4].p[P_SLEN] = 12;
    ok = proj_writeback() == 0;
    for (k = 0; k < NTRK; k++)
        ok &= !memcmp(want1[k], proj_slot[0].t[k].step, sizeof want1[k]);
    ok &= proj_slot[0].t[4].p[P_SLEN] == 16 && var_get(live_bank(), 2, VRP, VTR) == 1 && VR[4][9].on == 1u &&
          VTR[4][0] == 12 && (var_dirty & 1u);
    var_cur = 0;
    check("RAM model: playing variation 3, a write-back packs the live steps and LEN into variation 3, the record "
          "keeps variation 1's", (int)ok);
}
static void load_dirty_asks(void)
{
    static project_t want;
    uint32_t ok;
    four_slots();
    project_load(1);
    trk[5].step[3].on = 1;                            /* B edited */
    project_load(0);                                  /* (B written back: dirty) */
    ok = slot_dirty(1) && !ui.confirm && trk[0].p[P_LEVEL] == 10;
    ui.msg[0] = 0;
    project_load(1);
    ok &= ui.confirm == CONFIRM_DISCARD && ui.confirm_trk == 1u && trk[0].p[P_LEVEL] == 10 && !str_eq(ui.msg, "LOADED");
    release_all();
    press(B_OCTDN);
    ui_frame();
    release_all();
    ui_frame();
    ok &= !ui.confirm && trk[0].p[P_LEVEL] == 10 && slot_dirty(1);
    project_load(1);
    ok &= ui.confirm == CONFIRM_DISCARD;
    press(B_OCTUP);
    ui_frame();
    release_all();
    st_load(OBJ_PROJECT0 + 1u, &want, sizeof want);
    ok &= !ui.confirm && !memcmp(&want, &proj_slot[1], sizeof want) && trk[0].p[P_LEVEL] == 20 &&
          !trk[5].step[3].on && str_eq(ui.msg, "LOADED") && !slot_dirty(1);
    if (!ok)
        printf("     discard: confirm %u level %d step %u msg %s dirty %d same %d\n", ui.confirm, trk[0].p[P_LEVEL],
               trk[5].step[3].on, ui.msg, slot_dirty(1), !memcmp(&want, &proj_slot[1], sizeof want));
    check("RAM model: LOAD of an edited slot asks DISCARD EDITS? (NO: nothing; YES: its flash version loads)", (int)ok);
}
static void load_other_writes_back(void)
{
    uint32_t ok;
    four_slots();
    trk[6].step[11].on = 1;
    project_load(1);
    ok = !ui.confirm && str_eq(ui.msg, "LOADED") && trk[0].p[P_LEVEL] == 20 && proj_slot[0].t[6].step[11].on &&
         slot_dirty(0) && !slot_dirty(1) && host_nflog == 0u;
    project_load(0);
    ok &= ui.confirm == CONFIRM_DISCARD;
    ui.confirm = 0;
    check("RAM model: LOAD of another slot writes the loaded one's edits back first (no question); LOAD of it then asks",
          (int)ok);
}
static void load_park_full_refused(void)
{
    uint32_t ok;
    four_slots();
    project_load(2);                                  /* C: an edit in variation 2 */
    var_cur = 1;
    trk[2].step[2].on = 1;
    project_load(0);                                  /* (C's variations parked, unsaved) */
    ok = str_eq(ui.msg, "LOADED") && var_cur == 0u && (var_dirty & 4u) && var_who[var_play ^ 1u] == 3u;
    var_cur = 1;                                      /* A: an edit in variation 2 */
    trk[3].step[3].on = 1;
    project_load(1);
    ok &= str_eq(ui.msg, "SAVE C FIRST") && trk[0].p[P_LEVEL] == 10 && var_cur == 1u && trk[3].step[3].on &&
          var_who[var_play] == 1u && var_who[var_play ^ 1u] == 3u;
    var_cur = 0;
    check("RAM model: leaving a slot with unsaved variations while the park holds another's: SAVE C FIRST, nothing "
          "loads", (int)ok);
}
static void save_as_moves_edits(void)
{
    static project_t f;
    const var_bank_t *fb;
    uint32_t ok;
    four_slots();
    trk[2].p[P_LEVEL] = 99;                           /* A: a knob, and variation 2 */
    var_cur = 1;
    memset(trk[7].step, 0, sizeof trk[7].step);
    trk[7].step[15].on = 1;
    project_save(1);                                  /* SAVE to B: "save as" */
    st_load(OBJ_PROJECT0 + 1u, &f, sizeof f);
    fb = flash_bank(1);
    ok = str_eq(ui.msg, "SAVED") && f.t[2].p[P_LEVEL] == 99 && f.t[0].p[P_LEVEL] == 10 && f.t[1].step[0].on &&
         !f.t[7].step[15].on && fb && var_get(fb, 1, VRP, VTR) == 1 && VR[7][15].on == 1u && f.var_len;
    ok &= image_is_flash(0) && !slot_dirty(0) && !slot_dirty(1) && chain.from == 2u && var_who[var_play] == 2u;
    var_cur = 0;
    check("RAM model: SAVE to another slot stores the live state and variations there; the old slot is its flash "
          "version again", (int)ok);
}
static void rename_dirty_ram_only(void)
{
    char b[NAME_LEN + 1u];
    uint32_t ok;
    four_slots();
    project_load(1);
    trk[5].step[5].on = 1;
    project_load(0);                                  /* B dirty (written back) */
    host_nflog = 0;
    project_rename(1, "NEWB");
    ok = host_nflog == 0u && str_eq(ui.msg, "RENAMED (SAVE TO KEEP)") && project_name(1, b) && str_eq(b, "NEWB");
    project_rename(2, "NEWC");
    ok &= host_nflog > 0u && str_eq(ui.msg, "RENAMED") && image_is_flash(2);
    check("RAM model: RENAME of an edited slot renames it in RAM only (RENAMED (SAVE TO KEEP)); a clean one is written",
          (int)ok);
}
static void slot_dirty_marks(void)
{
    uint32_t ok;
    four_slots();
    ok = !slot_dirty(0) && !slot_dirty(1);
    trk[0].p[P_PAN] = 33;
    ok &= slot_dirty(0);                              /* a live edit */
    project_load(1);
    ok &= slot_dirty(0) && !slot_dirty(1);            /* written back */
    var_cur = 1;
    trk[1].step[40].on = 1;
    project_load(3);
    ok &= slot_dirty(1) && var_who[var_play ^ 1u] == 2u;   /* parked */
    project_load(1);                                  /* (asks) */
    ui.confirm = 0;
    project_discard(1);
    ok &= !slot_dirty(1) && image_is_flash(1) && var_who[var_play ^ 1u] != 2u;
    trk[2].p[P_PAN] = 5;
    ok &= slot_dirty(3);
    project_save(3);
    ok &= !slot_dirty(3) && slot_dirty(0);
    project_discard(0);
    ok &= !slot_dirty(0) && image_is_flash(0);
    var_cur = 0;
    check("RAM model: the unsaved mark: a live edit, a written-back slot, a parked one; gone after SAVE and DISCARD",
          (int)ok);
}

/* ---- Task 5: the cue engine (queue, boundary, switch, declick) */
static uint32_t now_s;                                /* samples rendered since play() */
static int32_t OL[CTL], OR_[CTL];
static void run_blk(void)
{
    render_mix(OL, OR_, CTL);
    now_s += CTL;
}
static void play(void)
{
    transport_req = 1;
    now_s = 0;
    run_blk();
}
static void stop(void)
{
    transport_req = 2;
    run_blk();
}
static void pattern_slot(uint32_t len, uint32_t div)  /* A: track 0 LEN len, step 0 on; track 1 one step shorter, step 0
                                                       * on; the rest off; saved; variation 2: track 0 step 1, track 1
                                                       * step 0; A loaded */
{
    uint32_t k;
    four_slots();
    for (k = 0; k < NTRK; k++) {
        memset(trk[k].step, 0, sizeof trk[k].step);
        trk[k].p[P_SLEN] = (int16_t)len;
        trk[k].p[P_SDIV] = (int16_t)div;
        trk[k].p[P_SSWING] = 0;
        trk[k].p[P_SRC] = 0;
    }
    trk[1].p[P_SLEN] = (int16_t)(len > 1u ? len - 1u : 1u);
    trk[0].step[0].on = 1;
    trk[1].step[0].on = 1;
    project_save(0);
    memset(VS, 0, sizeof VS);
    for (k = 0; k < NTRK; k++) {
        VTM[k][0] = trk[k].p[P_SLEN];
        VTM[k][1] = (int16_t)div;
        VTM[k][2] = 0;
        VTM[k][3] = 0;
    }
    VS[0][1].on = 1;
    VS[1][0].on = 1;
    var_put(live_bank(), 1, VSP, VTM);
    project_save(0);
    host_nflog = 0;
}
typedef struct { uint32_t pos, idx, cnt, rem, par; } seqst_t;
static seqst_t seqst(uint32_t k)
{
    seqst_t q;
    q.pos = trk[k].seq_pos;
    q.idx = trk[k].seq_idx;
    q.cnt = trk[k].seq_cnt % (uint32_t)trk[k].p[P_SLEN];
    q.rem = trk[k].seq_rem;
    q.par = (trk[k].seq_cnt ^ trk[k].seq_par) & 1u;
    return q;
}
static int seq_same(seqst_t a, seqst_t b) { return a.pos == b.pos && a.idx == b.idx && a.cnt == b.cnt && a.rem == b.rem && a.par == b.par; }
static int cue_boundary_case(uint32_t len, uint32_t div)
{
    uint32_t wrap_at = 0, sw_at = 0, t_end, a0, hits0 = 0, hit_bound = 0, hit_next = 0, ok = 1;
    seqst_t r0, r1;
    pattern_slot(len, div);
    play();                                           /* the plain loop: where its second pattern starts */
    while (!wrap_at && now_s < 40u * FS) {
        run_blk();
        if (trk[0].seq_cnt == len)
            wrap_at = now_s;                          /* (the block ending at now_s holds the boundary) */
    }
    t_end = wrap_at + 3u * div_samples(div) + 777u;
    while (now_s < t_end)
        run_blk();
    r0 = seqst(0);
    r1 = seqst(1);
    stop();
    play();
    a0 = hit_age(&trk[0]);
    while (now_s < wrap_at / 2u)
        run_blk();
    OK(cue_var(1) == 0 && cue.q_var == 2u && var_cur == 0u);   /* mid pattern */
    while (now_s < t_end) {
        uint32_t was = var_cur;
        run_blk();
        cue_poll();
        if (hit_age(&trk[0]) != a0) {
            a0 = hit_age(&trk[0]);
            if (!was && var_cur)
                hit_bound++;                          /* a hit in the switch block */
            else if (var_cur)
                hit_next++;
            else
                hits0++;
        }
        if (!was && var_cur)
            sw_at = now_s;
    }
    OK(sw_at == wrap_at && var_cur == 1u && seq_same(seqst(0), r0) && trk[1].seq_cnt == trk[0].seq_cnt &&
          trk[1].seq_rem == trk[0].seq_rem);           /* (every track from its step 0 at the boundary, together) */
    OK(hits0 == 0u && hit_bound == 0u && hit_next == 1u);   /* (variation 1's step 0 was PLAY's block) none on the
                                                       * boundary (variation 2's step 0 is off), its step 1 after */
    (void)r1;
    stop();
    if (!ok)
        printf("     LEN %u DIV %u: switch at %u (boundary block %u), var %u, hits %u / %u / %u, same %d %d\n", len, div,
               sw_at, wrap_at, var_cur, hits0, hit_bound, hit_next, seq_same(seqst(0), r0), seq_same(seqst(1), r1));
    return (int)ok;
}
static void cue_boundary_lens(void)
{
    uint32_t ok = 1;
    OK((uint32_t)cue_boundary_case(16, 2));
    OK((uint32_t)cue_boundary_case(12, 2));
    OK((uint32_t)cue_boundary_case(7, 4));
    OK((uint32_t)cue_boundary_case(64, 3));
    OK((uint32_t)cue_boundary_case(3, 0));
    check("cue: a queued variation switches exactly at the end of the longest track's pattern (LEN / DIV 16 1/16, 12 "
          "1/16, 7 8T, 64 1/32, 3 1/4; a shorter second track), on the plain loop's grid", (int)ok);
}
static void cue_grid_kept(void)
{
    uint32_t ok = 1, t_end, k;
    seqst_t r[NTRK];
    pattern_slot(16, 2);
    trk[0].p[P_SSWING] = 60;
    trk[1].p[P_SLEN] = 16;
    song.g[G_SWING] = 20;
    project_save(0);
    VTM[0][2] = 60;                                   /* variation 2: the same timing */
    VTM[1][0] = 16;
    t_end = 5u * 4u * 22050u + 999u;                 /* 4 bars after the first boundary (120 BPM) */
    play();
    while (now_s < t_end)
        run_blk();
    for (k = 0; k < 2u; k++)
        r[k] = seqst(k);
    stop();
    var_put(live_bank(), 1, VSP, VTM);                /* (variation 2: the same timing, SWG 0 on track 0) */
    play();
    while (now_s < 4u * 22050u)
        run_blk();
    cue_var(1);
    while (now_s < t_end) {
        run_blk();
        cue_poll();
    }
    for (k = 0; k < 2u; k++)
        OK(seq_same(seqst(k), r[k]));
    OK(var_cur == 1u);
    stop();
    song.g[G_SWING] = 0;
    check("cue: 4 bars after a switch (SWING 60 / 20, 1/16) every step sits where a never-switched loop's does", (int)ok);
}
static void cue_again_now(void)
{
    uint32_t ok = 1, step = div_samples(2), at, sw_at = 0, nb;
    pattern_slot(16, 2);
    play();
    while (now_s < 5u * step + step / 2u)            /* mid step 6 */
        run_blk();
    OK(cue_slot(1) == 0 && cue.q_slot == 2u);
    OK(cue_slot(1) == 0 && cue.now);                /* B again: at the next step */
    nb = 6u * step;                                   /* the next step of any track (all 1/16) */
    at = now_s;
    while (now_s < at + 2u * step) {
        uint32_t was = cue.q_slot;
        run_blk();
        cue_poll();
        if (was && !cue.q_slot && !sw_at)
            sw_at = now_s;
    }
    OK(sw_at && sw_at - CTL < nb + CTL && sw_at >= nb && trk[0].p[P_LEVEL] == 20 && proj_loaded() == 1);
    OK(trk[0].seq_cnt < 3u);                        /* a new pattern from its step 0 */
    stop();
    if (!ok)
        printf("     now: switch block end %u, next step %u, level %d, loaded %d\n", sw_at, nb, trk[0].p[P_LEVEL],
               proj_loaded());
    check("cue: the same slot key again while queued switches at the next step", (int)ok);
}
static void cue_stopped_immediate(void)
{
    uint32_t ok;
    four_slots();
    cue.last[1] = 0;
    ok = cue_slot(1) == 0 && proj_loaded() == 1 && trk[0].p[P_LEVEL] == 20 && !cue.q_slot && var_cur == 0u;
    OK(cue_slot(0) == 0 && proj_loaded() == 0 && trk[0].p[P_LEVEL] == 10);
    check("cue: stopped, a cue switches at once (the slot's last variation)", (int)ok);
}
static void cue_globals_kept(void)
{
    static int16_t g[G_COUNT];
    uint32_t ok = 1, k, f;
    int16_t mute[NTRK];
    four_slots();
    song.g[G_BPM] = 133;
    song.g[G_SWING] = 7;
    song.g[G_DTIME] = 5;
    song.g[G_RSIZE] = 99;
    song.g[G_GX] = 17;
    trk[3].p[P_MUTE] = 1;
    song.sel = 5;
    play();
    for (f = 0; f < 50u; f++)
        run_blk();
    memcpy(g, song.g, sizeof g);
    for (k = 0; k < NTRK; k++)
        mute[k] = trk[k].p[P_MUTE];
    cue_slot(2);
    for (f = 0; f < 4u * 22050u / CTL; f++) {
        run_blk();
        cue_poll();
    }
    OK(proj_loaded() == 2 && trk[0].p[P_LEVEL] == 30 && !memcmp(g, song.g, sizeof g) && song.sel == 5u);
    for (k = 0; k < NTRK; k++)
        OK(trk[k].p[P_MUTE] == mute[k]);
    stop();
    check("cue: a slot switch keeps the globals (BPM, SWING, delay, reverb, Grids), the mutes and the selected track",
          (int)ok);
}
static void cue_round_trip_edits(void)
{
    uint32_t ok = 1;
    pattern_slot(16, 2);
    OK(cue_var(2) == 0 && var_cur == 2u);           /* stopped: at once; variation 3 (never used: a copy of 1) */
    trk[6].step[13].on = 1;                           /* an edit of variation 3 */
    trk[2].p[P_PAN] = 21;
    OK(cue_slot(1) == 0 && proj_loaded() == 1 && !trk[6].step[13].on);
    OK(cue_slot(0) == 0 && proj_loaded() == 0 && var_cur == 2u && trk[6].step[13].on && trk[2].p[P_PAN] == 21);
    OK(slot_dirty(0) && !slot_dirty(1));
    check("cue: A on variation 3 with an edit, cue B, cue A: back on variation 3 with the edit (and the knob)", (int)ok);
}
static void cue_empty_copies(void)                    /* owner 2026-10-26: a never-used variation starts as a copy */
{
    uint32_t ok = 1, k;
    pattern_slot(16, 2);
    OK(!(var_mask(live_bank()) & (1u << 4)));
    trk[5].step[9].on = 1;                            /* variation 1, edited */
    trk[5].p[P_SLEN] = 9;
    OK(cue_var(4) == 0 && var_cur == 4u && trk[5].step[9].on && trk[5].p[P_SLEN] == 9 && trk[0].step[0].on);
    OK((var_mask(live_bank()) >> 4) & 1u);          /* used now */
    OK(cue_var(1) == 0 && var_cur == 1u && !trk[0].step[0].on && trk[0].step[1].on);   /* variation 2 as stored */
    OK(cue_var(4) == 0 && trk[5].step[9].on && trk[0].step[0].on);   /* variation 5: the copy kept */
    for (k = 2; k <= 3u; k++) {                       /* fill the bank: dense variations 3, 4 */
        var_make(70 + k, 1000);
        var_put(live_bank(), k, VSP, VTM);
    }
    {                                                 /* variation 7 (never used) does not fit: VARIATIONS FULL */
        static step_t keep[NTRK][NSTEP];
        uint32_t used = live_bank()->used;
        for (k = 0; k < NTRK; k++)
            memcpy(keep[k], trk[k].step, sizeof keep[k]);
        var_make(80, 1000);                           /* (the live variation 5 made dense: its copy is 1634 B) */
        for (k = 0; k < NTRK; k++)
            memcpy(trk[k].step, VS[k], sizeof VS[k]);
        OK(live_bank()->used + 1634u - (used - 0u) > 0u);
        OK(cue_var(6) == CUE_E_FULL && var_cur == 4u && !((var_mask(live_bank()) >> 6) & 1u));
        for (k = 0; k < NTRK; k++)
            OK(!memcmp(trk[k].step, VS[k], sizeof VS[k]));
    }
    check("cue: a never-used variation starts as a copy of the playing one (steps, timing), then counts as used; a "
          "copy that does not fit: VARIATIONS FULL, the playing one plays on", (int)ok);
}
static void cue_var_edits_while_queued(void)      /* Review Focus 5 */
{
    uint32_t ok = 1, f;
    pattern_slot(16, 2);
    cue_var(1);                                       /* stopped: variation 2 plays */
    play();
    for (f = 0; f < 100u; f++)
        run_blk();
    OK(cue_var(2) == 0 && cue.q_var == 3u);
    trk[7].step[3].on = 1;                            /* an edit of variation 2 before the boundary */
    for (f = 0; f < 4u * 44100u / CTL && cue.q_var; f++) {
        run_blk();
        cue_poll();
    }
    OK(var_cur == 2u && trk[7].step[3].on && var_get(live_bank(), 1, VRP, VTR) == 1 && VR[7][3].on == 1u &&
       ((var_mask(live_bank()) >> 2) & 1u));         /* (variation 3, never used: a copy of 2 with the edit) */
    trk[7].step[3].on = 0;                            /* variation 3 edited apart */
    cue_var(1);
    for (f = 0; f < 4u * 44100u / CTL && cue.q_var; f++) {
        run_blk();
        cue_poll();
    }
    OK(var_cur == 1u && trk[7].step[3].on);
    stop();
    check("cue: variation 2 edited while variation 3 is queued: the edit packed at the switch (3, never used, starts "
          "as its copy), there when 2 comes back", (int)ok);
}
static void cue_var_full(void)
{
    uint32_t ok = 1, f, k;
    pattern_slot(16, 2);
    cue_var(1);                                       /* variation 2 plays */
    for (k = 2; k <= 3u; k++) {                       /* variations 3, 4 dense: the bank nearly full */
        var_make(90 + k, 1000);
        var_put(live_bank(), k, VSP, VTM);
    }
    play();
    for (f = 0; f < 50u; f++)
        run_blk();
    OK(cue_var(2) == 0);                            /* variation 3 queued */
    var_make(99, 1000);                               /* live variation 2 made dense: it no longer fits */
    for (k = 0; k < NTRK; k++)
        memcpy(trk[k].step, VS[k], sizeof VS[k]);
    ui.msg[0] = 0;
    for (f = 0; f < 4u * 44100u / CTL; f++) {
        run_blk();
        cue_poll();
    }
    OK(var_cur == 1u && !cue.q_var && str_eq(ui.msg, "VARIATIONS FULL"));
    for (k = 0; k < NTRK; k++)
        OK(!memcmp(trk[k].step, VS[k], sizeof VS[k]));
    stop();
    check("cue: the playing variation's edits do not fit: at the boundary no switch, it plays on with its edits, "
          "VARIATIONS FULL", (int)ok);
}
static void cue_park_refused(void)                    /* Review Focus 5 */
{
    uint32_t ok = 1, f;
    pattern_slot(16, 2);
    OK(cue_slot(2) == 0 && proj_loaded() == 2);     /* (stopped) C, its variation 2 edited */
    cue_var(1);
    trk[4].step[4].on = 1;
    OK(cue_slot(0) == 0 && proj_loaded() == 0 && var_who[var_play ^ 1u] == 3u && (var_dirty & 4u));   /* C parked */
    cue_var(1);                                       /* A's variation 2 edited too */
    trk[4].step[6].on = 1;
    OK(cue_slot(1) == CUE_E_PARK + 2 && proj_loaded() == 0 && !cue.q_slot && str_eq(ui.msg, "SAVE C FIRST"));
    cue_var(0);                                       /* back to variation 1: A's variations still unsaved */
    project_discard(0);
    proj_load_from(0);                                /* A clean */
    OK(!slot_dirty(0));
    play();
    for (f = 0; f < 50u; f++)
        run_blk();
    OK(cue_slot(1) == 0 && cue.q_slot == 2u);       /* accepted */
    cue_var(1);                                       /* (B's variation 2 queued too) */
    OK(cue.q_var == 2u);
    ui.msg[0] = 0;
    var_cur = 0;
    for (f = 0; f < 4u * 22050u / CTL && (cue.q_slot || cue.q_var); f++) {
        if (f == 10u) {                               /* A's variations edited after the cue was accepted */
            var_cur = 1;
            trk[2].step[2].on = 1;
        }
        run_blk();
        cue_poll();
    }
    OK(proj_loaded() == 0 && str_eq(ui.msg, "SAVE C FIRST") && trk[2].step[2].on && var_cur == 1u);
    stop();
    var_cur = 0;
    check("cue: the park holds C's unsaved variations, A's are unsaved: cue B refused (SAVE C FIRST); A clean: accepted; "
          "A's variations edited before the boundary: refused there, A's edit kept", (int)ok);
}
static void cue_song_refused(void)
{
    uint32_t ok;
    four_slots();
    chain.running = 1;
    ok = cue_slot(1) == CUE_E_SONG && cue_var(1) == CUE_E_SONG && !cue.q_slot && !cue.q_var &&
         str_eq(ui.msg, "STOP SONG TO CUE");
    chain.running = 0;
    OK(cue_slot(1) == 0);
    check("cue: SONG playing: STOP SONG TO CUE, nothing queued", (int)ok);
}
static void cue_never_writes_flash(void)           /* Review Focus 1 */
{
    uint32_t ok = 1, i, f;
    pattern_slot(16, 2);
    host_nflog = 0;
    for (i = 0; i < 20u; i++) {
        if (i == 10u)
            play();
        if (i & 1u)
            cue_slot(i % 4u);
        else
            cue_var(i % 8u);
        if (i % 5u == 4u)
            cue_var(i % 8u);                         /* again = now */
        for (f = 0; f < (song.playing ? 3000u : 1u); f++) {
            run_blk();
            cue_poll();
        }
    }
    stop();
    ok = host_nflog == 0u;
    check("cue: 20 cues (slots and variations, playing and stopped, again = now) write no flash", (int)ok);
}
static int32_t peak(const int32_t *b, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        p = b[i] > p ? b[i] : -b[i] > p ? -b[i] : p;
    return p;
}
static int32_t REC[8u * 22050u + CTL];
static void cue_fade(void)
{
    uint32_t ok = 1, wrap = 4u * 22050u, i, n;
    int32_t pk20, pkend;
    four_slots();
    for (i = 0; i < NTRK; i++)
        memset(trk[i].step, 0, sizeof trk[i].step);
    trk[0].step[15].on = 1;                           /* A: an ACID note held through its step, into the boundary */
    old_set_model(&trk[0], DM_ACID);
    trk[0].p[P_E0 + 7] = 127;                         /* (GATE 100 %) */
    trk[0].p[P_LEVEL] = 100;
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_REV] = trk[i].p[P_DLY] = trk[i].p[P_CHOR] = 0;
    project_save(0);
    play();
    n = 0;
    while (now_s < 3u * 22050u) {
        run_blk();
        memcpy(REC + n, OL, sizeof OL);
        n += CTL;
    }
    cue_slot(1);
    while (now_s < wrap + 4u * CTL) {
        run_blk();
        cue_poll();
        memcpy(REC + n, OL, sizeof OL);
        n += CTL;
    }
    pk20 = peak(REC + wrap - 882u - 64u, 64u);       /* 20 ms before the boundary */
    pkend = peak(REC + wrap - 64u, 64u);              /* the last 64 samples before it */
    OK(proj_loaded() == 1 && pk20 > 0 && pkend * 8 < pk20 && mix_fade == 32767);
    stop();
    if (!ok)
        printf("     fade: peak 20 ms before %d, last 64 %d, loaded %d\n", pk20, pkend, proj_loaded());
    {                                                 /* a variation switch: up to the boundary the same as no switch */
        static int32_t a[4u * 22050u + CTL];
        pattern_slot(16, 2);
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_REV] = trk[i].p[P_DLY] = 0;
        play();
        for (n = 0; n < 4u * 22050u; n += CTL) {
            run_blk();
            memcpy(a + n, OL, sizeof OL);
        }
        stop();
        pattern_slot(16, 2);                          /* the same start */
        for (i = 0; i < NTRK; i++)
            trk[i].p[P_REV] = trk[i].p[P_DLY] = 0;
        play();
        cue_var(1);
        for (n = 0, i = 1; n < 4u * 22050u - CTL; n += CTL) {
            run_blk();
            i &= !memcmp(a + n, OL, sizeof OL);
        }
        OK(i);
        stop();
    }
    check("cue: a slot switch fades the old voices out over 5 ms before the boundary (the last 64 samples under 1/8 of "
          "20 ms before); a variation switch changes nothing up to the boundary", (int)ok);
}
static void song_plays_var1(void)
{
    uint32_t ok = 1, f, a;
    pattern_slot(16, 2);
    cue_var(1);                                       /* A on variation 2 (track 0 step 1) */
    proj_writeback();
    chain.cfg.count = 1;
    chain.cfg.loop = 1;
    chain.cfg.row[0].slot = 0;
    chain.cfg.row[0].repeat = 1;
    OK(chain_prepare() == 0);
    now_s = 0;
    run_blk();
    a = hit_age(&trk[0]);
    OK(chain.running && chain.src[0].step[0][0].on && !chain.src[0].step[0][1].on && a != 0u);   /* variation 1 */
    for (f = 0; f < 10u; f++)
        run_blk();
    stop();
    OK(!chain.running && var_cur == 1u && trk[0].step[1].on && !trk[0].step[0].on);
    check("cue: a SONG row plays its slot's variation 1; STOP returns to the live variation", (int)ok);
}

/* Review Focus 2: cueing while following an external clock (CLK USB / TRS): the switch comes from the clock's real
 * advance, in the block whose pulse ends the pattern (where a plain loop's next step 0 is), never late, never missed;
 * a slot switch's fade starts on the boundary's last pulse. (Here, not in midi_clock_test.c: the cue needs project.c) */
static uint64_t ck_now;
static double ck_next, ck_arr = -1.0;
static uint32_t ck_pulses, ck_rng = 7;
static void ck_send(uint32_t status, uint32_t src, double at)
{
    fm1_ms = (uint32_t)(at * 1000.0 / FS);
    if (src == 2u)
        um_byte(status);
    else
        midi_enqueue(0x0Fu | status << 8, src);
}
static double ck_late(void)
{
    ck_rng = ck_rng * 1664525u + 1013904223u;
    return (double)((ck_rng >> 29) % 5u) * FS / 1000.0;
}
static void ck_block(double bpm, uint32_t src)       /* one block, the pulses that arrive in it first */
{
    for (;;) {
        if (ck_arr < 0.0)
            ck_arr = ck_next + ck_late();
        if (ck_arr >= (double)(ck_now + CTL))
            break;
        ck_send(0xF8u, src, ck_arr);
        ck_pulses++;
        ck_next += FS * 60.0 / bpm / 24.0;
        ck_arr = -1.0;
    }
    fm1_ms = (uint32_t)((ck_now + CTL) * 1000u / FS);
    render_mix(OL, OR_, CTL);
    ck_now += CTL;
}
static void ck_setup(uint32_t src)
{
    pattern_slot(16, 2);
    song.g[G_CLOCK] = (int16_t)src;
    render_mix(OL, OR_, CTL);                        /* (the mode change settles) */
    ck_now = 0;
    ck_pulses = 0;
    ck_rng = 7;
    ck_next = 100.0;
    ck_arr = -1.0;
    mi_r = mi_w;
}
static int cue_follow(double bpm, uint32_t src, int slot)
{
    uint32_t b, wrap_blk = 0, sw_blk = 0, fade_blk = 0, fade_pulse = 0, ok = 1, end;
    seqst_t r0;
    ck_setup(src);
    ck_send(0xFAu, src, (double)ck_now);
    for (b = 0; b < 200000u && !wrap_blk; b++) {      /* the plain loop: the block its second pattern starts in */
        ck_block(bpm, src);
        if (song.playing && trk[0].seq_cnt == 16u)
            wrap_blk = b;
    }
    end = wrap_blk + 400u;
    for (; b <= end; b++)
        ck_block(bpm, src);
    r0 = seqst(0);
    ck_send(0xFCu, src, (double)ck_now);
    ck_block(bpm, src);
    ck_setup(src);
    ck_send(0xFAu, src, (double)ck_now);
    for (b = 0; b <= end; b++) {
        uint32_t was = slot ? cue.q_slot : cue.q_var;
        if (b == wrap_blk / 2u)
            OK((slot ? cue_slot(1) : cue_var(1)) == 0);
        ck_block(bpm, src);
        cue_poll();
        if (slot && !fade_blk && cue.fade == 1u) {
            fade_blk = b;
            fade_pulse = ck_pulses;
        }
        if (was && !(slot ? cue.q_slot : cue.q_var))
            sw_blk = b;
    }
    OK(sw_blk == wrap_blk && (slot ? proj_loaded() == 1 : var_cur == 1u));
    if (!slot) {                                      /* (the step remainder from the grid's time: a measured
                                                       * tempo's fraction may put it one sample off the plain loop's,
                                                       * as a SONG row's; the next pulse re-anchors it) */
        seqst_t q = seqst(0);
        OK(q.idx == r0.idx && q.cnt == r0.cnt && q.par == r0.par && q.pos + 1u >= r0.pos && q.pos <= r0.pos + 1u);
    }
    else                                              /* the fade from the boundary's last pulse (96 = 16 x 6) */
        OK(fade_blk && fade_blk < sw_blk && fade_pulse >= 95u && fade_pulse <= 96u);
    ck_send(0xFCu, src, (double)ck_now);
    ck_block(bpm, src);
    song.g[G_CLOCK] = 0;
    render_mix(OL, OR_, CTL);
    if (!ok)
        printf("     follow %.1f BPM src %u %s: switch block %u, plain wrap %u, fade at block %u pulse %u\n", bpm, src,
               slot ? "slot" : "variation", sw_blk, wrap_blk, fade_blk, fade_pulse);
    return (int)ok;
}
static void cue_follow_all(void)
{
    static const double BPMS[3] = {90.0, 120.0, 174.6};
    uint32_t b, s, ok = 1;
    for (s = 1; s <= 2u; s++)
        for (b = 0; b < 3u; b++) {
            ok &= (uint32_t)cue_follow(BPMS[b], s, 0);
            ok &= (uint32_t)cue_follow(BPMS[b], s, 1);
        }
    check("cue following CLK USB / TRS at 90, 120, 174.6 BPM: a variation's or a slot's switch in the block whose pulse "
          "ends the pattern (a plain loop's next step 0), on its grid; a slot's fade from the boundary's last pulse",
          (int)ok);
}

int main(void)
{
    vptrs();
    var_save_load_round_trip();
    var_torn_keeps_triple();
    var_old_record_empty();
    var_upload_over_vars();
    flash_is_truth_after_reset();
    var_writes_in_map();
    var_blocked_refuses();
    writeback_keeps_edits();
    writeback_rest_values();
    writeback_var_not_record();
    load_dirty_asks();
    load_other_writes_back();
    load_park_full_refused();
    save_as_moves_edits();
    rename_dirty_ram_only();
    slot_dirty_marks();
    cue_boundary_lens();
    cue_grid_kept();
    cue_again_now();
    cue_stopped_immediate();
    cue_globals_kept();
    cue_round_trip_edits();
    cue_empty_copies();
    cue_var_edits_while_queued();
    cue_var_full();
    cue_park_refused();
    cue_song_refused();
    cue_never_writes_flash();
    cue_fade();
    song_plays_var1();
    cue_follow_all();
    check("flash: no erase or program in any test outside the map", host_fl_stray == 0);
    printf(fails ? "cue_test: %d FAILED\n" : "cue_test: all passed\n", fails);
    return fails ? 1 : 0;
}
