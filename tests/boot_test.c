/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* H1 (spec docs/superpowers/specs/2026-10-05-device-readiness-design.md §3): the drum firmware's boot path,
 * main.c's order, with our real code, against hostile flash and .noinit contents, built with ASan + UBSan.
 * H5 in the boot: the same cases with SEQ held. A sanitizer report aborts the run (nonzero exit). */
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

static uint32_t rs = 1;
static uint32_t rnd(void) { return rs = rs * 1664525u + 1013904223u; }
static void rfill(void *p, uint32_t n)
{
    uint8_t *b = p;
    while (n--)
        *b++ = (uint8_t)(rnd() >> 24);
}

/* a sample slot k with a valid header and random (or plausible) zones */
static void slot_header(uint32_t k, int plausible)
{
    smp_user_hdr_t h;
    uint32_t i;
    rfill(&h, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = (uint8_t)(1u + rnd() % 16u);
    h.data_len = plausible ? 0x8000u : rnd();
    for (i = 0; plausible && i < h.nz; i++) {
        smp_zone_t *z = &h.zone[i];
        z->off = 0;
        z->n = 1000u + rnd() % 30000u;
        z->idx = 0;
        z->rate = 1u << 16;
        z->ls = 0;
        z->le = z->n - 1u;
        z->lo = 0;
        z->hi = 127;
    }
    memcpy(hflash + 0xA0000u + k * 0x14000u, &h, sizeof h);
}

enum { F_ERASED, F_ZERO, F_RANDOM, F_HEADERS, F_FELUCCA, F_KINDS };
static const char *const KIND[F_KINDS] = {"erased", "zeros", "random", "valid headers, garbage fields", "felucca records"};

static void flash_image(int kind, uint32_t seed)
{
    rs = seed * 2654435761u + 7u;
    memset(hflash, kind == F_ZERO ? 0x00 : 0xFF, sizeof hflash);
    if (kind == F_RANDOM)
        rfill(hflash + 0x93000u, sizeof hflash - 0x93000u);   /* the data area (the app area is ours) */
    if (kind == F_HEADERS || kind == F_FELUCCA) {
        persist_t p;
        project_t pr;
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            slot_header(k, kind == F_FELUCCA);
        rfill(&p, sizeof p);
        p.magic = PERSIST_MAGIC;
        if (kind == F_FELUCCA)
            p.panel = PANEL_DEFAULT;
        else
            p.panel.magic = PANEL_MAGIC;              /* a valid panel magic over random ids */
        st_save(OBJ_SETTINGS, &p, sizeof p);
        for (k = 0; k < 4; k++) {
            if (kind == F_HEADERS && k == 1u) {      /* an M1 project ("FDR1"): valid sum over random fields */
                project_v1_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V1;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
            if (kind == F_HEADERS && k == 2u) {      /* an M3 project ("FDR3") */
                project_v3_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V3;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
            if (kind == F_HEADERS && k == 3u) {      /* an M2 project ("FDR2"): valid sum over random fields */
                project_v2_t v;
                rfill(&v, sizeof v);
                v.magic = PROJ_MAGIC_V2;
                v.size = sizeof v;
                v.sum = proj_hash(&v, sizeof v - 4u);
                st_save(OBJ_PROJECT0 + k, &v, sizeof v);
                continue;
            }
            rfill(&pr, sizeof pr);
            pr.magic = kind == F_FELUCCA ? 0x334E5546u : PROJ_MAGIC;   /* Felucca's "FUN3", or ours */
            pr.size = sizeof pr;
            pr.sum = proj_sum(&pr);                   /* ours: valid sum over random tracks */
            st_save(OBJ_PROJECT0 + k, &pr, sizeof pr);
        }
    }
}

static int32_t L[CTL], R[CTL];

/* main.c's order (fm1_main): persist_boot, settings_init, (LCD), input / ADC init, panel_init,
 * felucca_init = drum_boot_init, audio, USB, IRQs; then the main loop (UI + the update service) */
static int boot(int kind, uint32_t seed, int seq)
{
    uint32_t f, i, bounded = 1, peak = 0;
    flash_image(kind, seed);
    rfill(&settings, sizeof settings);               /* .noinit RAM after the stock firmware: anything */
    rfill(proj_slot, sizeof proj_slot);
    host_init();
    memset(&ui, 0, sizeof ui);
    persist_boot();
    if (kind == F_FELUCCA && !usr_nz[0])               /* the glue reads the image: valid slots are found */
        return 0;
    settings_init();
    panel_init();
    fm1_in.buttons = seq ? 1u << panel.btn[B_SEQ] : 0u;
    drum_boot_init();
    fm1_in.buttons = 0;
    for (f = 0; f < 2u * FS; f += CTL) {             /* 2 s of the audio ISR */
        render_mix(L, R, CTL);
        for (i = 0; i < CTL; i++) {
            bounded &= L[i] <= 32767 && L[i] >= -32768 && R[i] <= 32767 && R[i] >= -32768;
            peak |= (uint32_t)abs(L[i]) | (uint32_t)abs(R[i]);
        }
    }
    for (i = 0; i < 30; i++)                         /* the main loop's first UI frames */
        ui_frame();
    for (i = 0; !seq && i < 4; i++) {                /* the first hands-on minutes: load each slot, play */
        project_load(i);
        transport_req = 1;
        render_mix(0, 0, CTL * 64);
        ui_frame();
    }
    return bounded && settings.zoom <= 1u && settings.mutebar <= 1u && (seq ? safe_start && peak == 0 : !safe_start);
}

/* MUTE NEXT BAR (TRACKS) survives a power cycle beside ZOOM, in Felucca's settings format (zoom's bit 1) */
static int mutebar_persists(void)
{
    int ok = 1;
    uint32_t z, m;
    for (z = 0; z < 2u; z++)
        for (m = 0; m < 2u; m++) {
            memset(hflash, 0xFF, sizeof hflash);
            host_init();
            persist_boot();                          /* finds the flash */
            settings.magic = SETTINGS_MAGIC;
            settings.palette = 4;
            settings.lowcut = 0;
            settings.zoom = z;
            settings.mutebar = m;
            settings.accel = (z ^ m) & 1u;
            settings_save();
            rfill(&settings, sizeof settings);       /* power off: .noinit is anything */
            persist_boot();
            settings_init();
            ok &= settings.zoom == z && settings.mutebar == m && settings.accel == ((z ^ m) & 1u);
        }
    return ok;
}

/* USB LEVEL (MENU): FIXED survives a power cycle in bit 3 of the stored zoom word; MASTER when it is clear (a 0.7.0
 * save); the other settings read back unchanged */
static int usb_level_persists(void)
{
    int ok = 1;
    uint32_t u;
    for (u = 0; u < 2u; u++) {
        memset(hflash, 0xFF, sizeof hflash);
        host_init();
        persist_boot();
        settings.magic = SETTINGS_MAGIC;
        settings.palette = 2;
        settings.lowcut = 2;
        settings.zoom = 1;
        settings.mutebar = 1;
        settings.accel = 0;
        settings.usbfix = u;
        settings.perfpage = u;
        settings_save();
        rfill(&settings, sizeof settings);           /* power off: .noinit is anything */
        persist_boot();
        settings_init();
        ok &= settings.usbfix == u && settings.perfpage == u && fx_usb_fixed == u && settings.palette == 2 && settings.lowcut == 2 &&
              settings.zoom == 1 && settings.mutebar == 1 && settings.accel == 0;
    }
    return ok;
}

/* a settings record longer than ours (another firmware's, same marker): the defaults, not a half-read record */
static int settings_oversize_refused(void)
{
    struct {
        persist_t p;
        uint8_t more[16];
    } r;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();                                  /* finds the flash */
    memset(&r, 0, sizeof r);
    r.p.magic = PERSIST_MAGIC;
    r.p.palette = 2;                                 /* not the default (4, MONO) */
    r.p.panel = PANEL_DEFAULT;
    st_save(OBJ_SETTINGS, &r, sizeof r);
    memset(&settings, 0, sizeof settings);           /* power off */
    persist_boot();
    settings_init();
    return settings.palette == 4u;
}

/* an M1 project in flash ("FDR1") loads: its steps and settings; PROB 100 %, 1 hit, SRC STEP, Grids defaults */
static int fdr1_converts(void)
{
    project_v1_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V1;
    v.size = sizeof v;
    for (i = 0; i < G_GMODE; i++)
        v.g[i] = song.g[i];
    v.g[G_BPM] = 133;
    v.sel = 2;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_SRC; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[3].p[P_SLEN] = 23;
    v.t[3].step[5].on = v.t[3].step[5].acc = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 1u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    song.g[G_GLEN2] = 3;                             /* the live state differs: the load replaces it */
    trk[3].p[P_SRC] = 2;
    trk[3].step[5].cond = 9;
    project_load(1);
    return trk[3].p[P_SLEN] == 23 && trk[3].step[5].on && trk[3].step[5].acc && trk[3].step[5].cond == 0 &&
           trk[3].step[5].rat == 0 && !trk[3].step[6].on && trk[3].p[P_SRC] == 0 && song.g[G_BPM] == 133 &&
           song.g[G_GLEN2] == 12 && song.g[G_GMODE] == 0 && song.sel == 2;
}

/* an M2 project in flash ("FDR2") loads: everything it has, COMP off, DUCK off */
static int fdr2_converts(void)
{
    project_v2_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V2;
    v.size = sizeof v;
    for (i = 0; i < G_CSRC; i++)
        v.g[i] = song.g[i];
    v.g[G_GLEN2] = 5;
    v.sel = 3;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_DUCK; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[2].p[P_SRC] = 3;
    v.t[2].step[9].on = 1;
    v.t[2].step[9].cond = 33;
    v.t[2].step[9].rat = 2;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 2u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    song.g[G_CSRC] = 4;                              /* the live state differs: the load replaces it */
    trk[2].p[P_DUCK] = 1;
    project_load(2);
    return song.g[G_GLEN2] == 5 && song.sel == 3 && trk[2].p[P_SRC] == 3 && trk[2].step[9].on &&
           trk[2].step[9].cond == 33 && trk[2].step[9].rat == 2 && song.g[G_CSRC] == 0 && trk[2].p[P_DUCK] == 0 &&
           song.g[G_CRAT] == GP[G_CRAT].def && song.g[G_CMKUP] == 0;
}

/* an M3 project ("FDR3") loads: everything it has, the LFOs off */
static int fdr3_converts(void)
{
    project_v3_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V3;
    v.size = sizeof v;
    for (i = 0; i < G_CGHOST; i++)
        v.g[i] = song.g[i];
    v.g[G_CSRC] = 2;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_LFO1; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[1].p[P_DUCK] = 1;
    v.t[1].step[3].on = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 3u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    trk[1].p[P_LFO1 + LF_DEST] = 4;                  /* the live state differs: the load replaces it */
    project_load(3);
    return song.g[G_CSRC] == 2 && trk[1].p[P_DUCK] == 1 && trk[1].step[3].on && trk[1].p[P_LFO1 + LF_DEST] == 0 &&
           trk[1].p[P_LFO1 + LF_WAVE] == TP[P_LFO1 + LF_WAVE].def && trk[1].p[P_LFO2 + LF_RATE] == 23;
}

/* an FDR4 record (LFOs, no RESON) loads: its parameters; RESON OFF at the defaults */
static int fdr4_converts(void)
{
    project_v4_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V4;
    v.size = sizeof v;
    for (i = 0; i < G_CGHOST; i++)
        v.g[i] = song.g[i];
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_RMODEL; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[2].p[P_LFO1 + LF_DEST] = 9;
    v.t[2].step[5].on = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 3u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    trk[2].p[P_RMODEL] = RS_PIPE;                    /* the live state differs: the load replaces it */
    project_load(3);
    return trk[2].p[P_LFO1 + LF_DEST] == 9 && trk[2].step[5].on && trk[2].p[P_RMODEL] == RS_OFF &&
           trk[2].p[P_RTUNE] == TP[P_RTUNE].def && trk[2].p[P_RDECAY] == TP[P_RDECAY].def;
}
/* an FDR5 record (RESON, no GHOST) loads: its parameters; GHOST KEEP (as it sounded) */
static int fdr5_converts(void)
{
    project_v5_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V5;
    v.size = sizeof v;
    for (i = 0; i < G_CGHOST; i++)
        v.g[i] = song.g[i];
    v.g[G_CSRC] = 1;
    v.g[G_CMKUP] = 77;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_FTYPE; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[2].p[P_RMODEL] = RS_PIPE;
    v.t[2].step[5].on = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 3u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    song.g[G_CGHOST] = CG_HIDE;                      /* the live state differs: the load replaces it */
    project_load(3);
    return song.g[G_CSRC] == 1 && song.g[G_CMKUP] == 77 && song.g[G_CGHOST] == CG_KEEP &&
           trk[2].p[P_RMODEL] == RS_PIPE && trk[2].step[5].on;
}
/* an FDR6 record (GHOST, no reverb TYPE; 3024 B like an FDR5 one) loads: its values; TYPE ROOM */
static int fdr6_converts(void)
{
    project_v6_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V6;
    v.size = sizeof v;
    for (i = 0; i < G_RTYPE; i++)
        v.g[i] = song.g[i];
    v.g[G_CGHOST] = CG_HIDE;
    v.g[G_RSIZE] = 33;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_FTYPE; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[4].step[7].on = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 3u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    song.g[G_RTYPE] = 1;                             /* SPRING running: the load sets ROOM */
    project_load(3);
    return song.g[G_CGHOST] == CG_HIDE && song.g[G_RSIZE] == 33 && song.g[G_RTYPE] == 0 && trk[4].step[7].on;
}
/* FDR7 keeps TYPE through a save and a load */
static int fdr7_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    song.g[G_RTYPE] = 1;
    project_save(2);
    song.g[G_RTYPE] = 0;
    memset(proj_slot, 0, sizeof proj_slot);
    project_load(2);
    return song.g[G_RTYPE] == 1;
}
/* FDR8: the motion goes through a save and a load (flash) */
static int fdr8_motion_round_trip(void)
{
    motion_store_t want;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    motion_add(0, 4, P_E1, 100);
    motion_add(5, 63, P_PAN, -64);
    motion_add(7, 0, P_LFO2 + LF_DEPTH, 127);
    mo.s.on = 0xA1u;
    want = mo.s;
    project_save(2);
    memset(&mo, 0, sizeof mo);
    memset(proj_slot, 0, sizeof proj_slot);
    project_load(2);
    return !memcmp(&mo.s, &want, sizeof want);
}
/* an FDR7 record (3028 B, no motion) loads: its values, no motion */
static int fdr7_converts(void)
{
    project_v7_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V7;
    v.size = sizeof v;
    for (i = 0; i < G_COUNT; i++)
        v.g[i] = song.g[i];
    v.g[G_RTYPE] = 1;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_FTYPE; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[2].step[9].on = 1;
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 1u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    motion_add(0, 1, P_E1, 5);                       /* motion in RAM before: the load replaces it */
    project_load(1);
    return song.g[G_RTYPE] == 1 && trk[2].step[9].on && mo.s.count == 0u && sizeof v == 3028u;
}
/* a broken motion block: the project loads, the motion dropped */
static int fdr8_bad_motion_dropped(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[3].step[5].on = 1;
    motion_add(0, 4, P_E1, 100);
    project_save(0);
    p->motion.ev[0].trk = 9;                         /* out of range */
    p->sum = proj_sum(p);
    memset(&mo, 0, sizeof mo);
    trk[3].step[5].on = 0;
    project_load(0);
    return trk[3].step[5].on && mo.s.count == 0u;
}
/* Review Focus 3: a load while motion plays: the stop after it puts nothing of the old project back */
static int fdr8_load_while_playing(void)
{
    uint32_t b;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[0].p[P_E1] = 11;
    project_save(1);                                 /* the project: E1 11 */
    trk[0].p[P_E1] = 64;
    motion_add(0, 0, P_E1, 100);
    transport_req = 1;
    for (b = 0; b < 8u; b++)
        render_mix(L, R, CTL);                       /* motion plays: E1 100, the base 64 */
    project_load(1);
    for (b = 0; b < 8u; b++)
        render_mix(L, R, CTL);                       /* the stop the load asked for */
    return !song.playing && trk[0].p[P_E1] == 11;
}
/* the song and the name go through a save and a load (flash; FDRA) */
static int fdr9_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    chain.cfg.count = 2;
    chain.cfg.loop = 1;
    chain.cfg.row[0] = (chain_row_t){1, 4};
    chain.cfg.row[1] = (chain_row_t){3, 16};
    name_set(chain.name, "BREAK 2");
    project_save(2);
    memset(&chain, 0, sizeof chain);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(2);
    return chain.cfg.count == 2u && chain.cfg.loop == 1u && chain.cfg.row[0].slot == 1u &&
           chain.cfg.row[0].repeat == 4u && chain.cfg.row[1].slot == 3u && chain.cfg.row[1].repeat == 16u &&
           str_eq(chain.name, "BREAK 2") && chain.from == 3u && sizeof(project_t) == 3672u;
}
/* an FDR9 record (3592 B): its values, steps, motion, song and name; the filter OFF */
static int fdr9_converts(void)
{
    project_v9_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V9;
    v.size = sizeof v;
    for (i = 0; i < G_COUNT; i++)
        v.g[i] = song.g[i];
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_FTYPE; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[1].p[P_E1] = 99;
    v.t[2].step[9].on = 1;
    v.motion.count = 1;
    v.motion.on = 1;
    v.motion.ev[0] = (motion_event_t){0, 4, P_E1, 77};
    v.song.count = 1;
    v.song.row[0] = (chain_row_t){2, 3};
    memcpy(v.name, "OLD ONE", 7);
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 1u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    trk[0].p[P_FTYPE] = FT_LP;                       /* before: the load sets the filter OFF */
    project_load(1);
    return sizeof v == 3592u && trk[1].p[P_E1] == 99 && trk[2].step[9].on && mo.s.count == 1u &&
           chain.cfg.count == 1u && chain.cfg.row[0].slot == 2u && str_eq(chain.name, "OLD ONE") &&
           trk[0].p[P_FTYPE] == FT_OFF && trk[0].p[P_FCUT] == 127 && trk[0].p[P_FDEC] == 40;
}
/* FDRA: the five filter knobs through a save and a load (flash) */
static int fdra_round_trip(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[5].p[P_FTYPE] = FT_NOTCH;
    trk[5].p[P_FCUT] = 33;
    trk[5].p[P_FRESO] = 101;
    trk[5].p[P_FENV] = -17;
    trk[5].p[P_FDEC] = 90;
    project_save(3);
    host_init();
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    project_load(3);
    return trk[5].p[P_FTYPE] == FT_NOTCH && trk[5].p[P_FCUT] == 33 && trk[5].p[P_FRESO] == 101 &&
           trk[5].p[P_FENV] == -17 && trk[5].p[P_FDEC] == 90;
}
/* Review Focus 4: out-of-range filter values in a stored project: clamped on load */
static int fdra_clamps(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    project_save(0);
    p->t[4].p[P_FTYPE] = 9;
    p->t[4].p[P_FCUT] = 300;
    p->t[4].p[P_FENV] = -200;
    p->sum = proj_sum(p);
    project_load(0);
    return trk[4].p[P_FTYPE] == FT_N - 1 && trk[4].p[P_FCUT] == 127 && trk[4].p[P_FENV] == -64;
}
/* an FDR8 record (3544 B): its motion, an empty song, no name */
static int fdr8_converts(void)
{
    project_v8_t v;
    uint32_t i, k;
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    memset(&v, 0, sizeof v);
    v.magic = PROJ_MAGIC_V8;
    v.size = sizeof v;
    for (i = 0; i < G_COUNT; i++)
        v.g[i] = song.g[i];
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_FTYPE; i++)
            v.t[k].p[i] = trk[k].p[i];
    v.t[2].step[9].on = 1;
    v.motion.count = 1;
    v.motion.on = 1;
    v.motion.ev[0] = (motion_event_t){0, 4, P_E1, 77};
    v.sum = proj_hash(&v, sizeof v - 4u);
    st_save(OBJ_PROJECT0 + 1u, &v, sizeof v);
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    chain.cfg.count = 3;                             /* a song and a name in RAM before: the load replaces them */
    name_set(chain.name, "OLD");
    project_load(1);
    return trk[2].step[9].on && mo.s.count == 1u && mo.s.ev[0].value == 77 && chain.cfg.count == 0u &&
           !chain.name[0] && sizeof v == 3544u;
}
/* a broken song or name: the project loads, they are dropped */
static int fdr9_bad_song_name_dropped(void)
{
    project_t *p = &proj_slot[0];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[3].step[5].on = 1;
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    name_set(chain.name, "OK");
    project_save(0);
    p->song.row[0].repeat = 0;                       /* invalid */
    p->name[0] = 'a';                                /* outside the NAME set */
    p->sum = proj_sum(p);
    memset(&chain, 0, sizeof chain);
    trk[3].step[5].on = 0;
    project_load(0);
    return trk[3].step[5].on && chain.cfg.count == 0u && !chain.name[0];
}
/* chain_prepare: rows need used slots; the slots' garbage (cond, rat, timing, motion) is made safe */
static int song_garbage_slot(void)
{
    project_t *p = &proj_slot[1];
    uint32_t b, rc_empty, rc;
    memset(hflash, 0xFF, sizeof hflash);
    memset(proj_slot, 0, sizeof proj_slot);          /* (no slot left in RAM by the tests before) */
    host_init();
    persist_boot();
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){1, 1};
    rc_empty = chain_prepare();
    project_save(1);
    memset(p->t[0].step, 0xFF, sizeof p->t[0].step);  /* cond 255, rat 255, on 255 */
    p->t[0].p[P_SLEN] = 999;
    p->t[0].p[P_SDIV] = -7;
    p->motion.count = 200;                           /* broken */
    p->sum = proj_sum(p);
    rc = chain_prepare();
    for (b = 0; b < 400u; b++)
        render_mix(L, R, CTL);
    transport_req = 2;
    render_mix(L, R, CTL);
    return rc_empty == 4u && rc == 0u && chain.src[1].timing[0][0] == NSTEP && chain.src[1].timing[0][1] == 0 &&
           chain.src[1].m == &MOTION_NONE && !song.playing;
}
/* LOAD while a song plays: refused */
static int song_blocks_load(void)
{
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[0].p[P_E1] = 11;
    project_save(0);
    trk[0].p[P_E1] = 64;
    chain.cfg.count = 1;
    chain.cfg.row[0] = (chain_row_t){0, 1};
    chain_prepare();
    render_mix(L, R, CTL);
    project_load(0);
    transport_req = 2;
    render_mix(L, R, CTL);
    return trk[0].p[P_E1] == 64 && str_eq(ui.msg, "STOP TO LOAD");
}
/* a rename writes only the name (flash), and renames the current project when it is that slot */
static int rename_slot(void)
{
    char b[NAME_LEN + 1u];
    memset(hflash, 0xFF, sizeof hflash);
    host_init();
    persist_boot();
    trk[1].step[2].on = 1;
    name_set(chain.name, "A NAME");
    project_save(3);
    project_rename(3, "INTRO");
    memset(proj_slot, 0, sizeof proj_slot);
    persist_boot();
    return project_name(3, b) && str_eq(b, "INTRO") && proj_slot[3].t[1].step[2].on && str_eq(chain.name, "INTRO") &&
           !project_name(2, b) && !b[0];
}
/* MEMB at every knob extreme (TUNE, DECAY, TONE, HEAD, POS, BEND, STICK) and both velocities: bounded, in memory */
static int memb_extremes(void)
{
    static const int16_t LO[8] = {-24, 0, 0, 0, 0, 0, 0, 0}, HI[8] = {24, 127, 127, 127, 127, 127, 127, 0};
    uint32_t c, k, i;
    int ok = 1;
    for (c = 0; c < 256u; c++) {
        host_init();
        drum_set_model(&trk[0], DM_MEMB);
        for (k = 0; k < 7u; k++)
            trk[0].p[P_E0 + k] = ((c >> k) & 1u) ? HI[k] : LO[k];
        drum_hit(&trk[0], (c & 128u) ? 127u : 96u);
        for (i = 0; i < 40u; i++) {
            render_mix(L, R, CTL);
            ok &= L[0] <= 32767 && L[0] >= -32768;
        }
    }
    return ok;
}
/* review focus 1: RESON at every extreme (pitch with a chord's top note and the fine offset, STRCT, TONE, POS):
 * the lines stay inside rs_buf (ASan) and the output bounded */
static int reson_extremes(void)
{
    static const int16_t TUNE[2] = {24, 96}, END[2] = {0, 127};
    static const int32_t FINE[3] = {-RS_FINE, 0, RS_FINE};
    uint32_t m, a, b, c, d, f, i;
    int ok = 1;
    for (m = RS_STRNG; m < RS_NMODEL; m++)
        for (a = 0; a < 2u; a++)
            for (b = 0; b < 2u; b++)
                for (c = 0; c < 2u; c++)
                    for (d = 0; d < 2u; d++)
                        for (f = 0; f < 3u; f++) {
                            host_init();
                            trk[0].p[P_RMODEL] = (int16_t)m;
                            trk[0].p[P_RTUNE] = TUNE[a];
                            trk[0].p[P_RSTRCT] = END[b];
                            trk[0].p[P_RTONE] = END[c];
                            trk[0].p[P_RPOS] = END[d];
                            trk[0].p[P_RDECAY] = 127;
                            trk[0].rfine = FINE[f];
                            drum_hit(&trk[0], 127);
                            for (i = 0; i < 64u; i++) {
                                render_mix(L, R, CTL);
                                trk[0].rfine = FINE[f];   /* (the LFOs rewrite it every block) */
                                ok &= L[0] <= 32767 && L[0] >= -32768;
                            }
                        }
    return ok;
}
int main(void)
{
    char what[96];
    int k, s, seq;
    check("project: an M1 record (FDR1) loads with PROB 100 %, 1 hit, SRC STEP, Grids defaults", fdr1_converts());
    check("project: an M2 record (FDR2) loads with COMP off and DUCK off", fdr2_converts());
    check("project: an M3 record (FDR3) loads with the LFOs off", fdr3_converts());
    check("project: an FDR4 record loads with RESON off", fdr4_converts());
    check("project: an FDR5 record loads with GHOST KEEP", fdr5_converts());
    check("project: an FDR6 record (same size as FDR5) loads with reverb TYPE ROOM", fdr6_converts());
    check("project: the reverb TYPE survives a save and a load", fdr7_round_trip());
    check("project: FDR8 keeps the motion through a save and a load", fdr8_motion_round_trip());
    check("project: an FDR7 record (3028 B) loads with no motion", fdr7_converts());
    check("project: a broken motion block is dropped, the project loads", fdr8_bad_motion_dropped());
    check("project: a load while motion plays: the old base is not put back", fdr8_load_while_playing());
    check("project: FDRA keeps the song and the name through a save and a load", fdr9_round_trip());
    check("project: an FDR9 record loads, the filter OFF", fdr9_converts());
    check("project: FDRA keeps the five filter knobs through a save and a load", fdra_round_trip());
    check("project: out-of-range filter values are clamped on load", fdra_clamps());
    check("project: an FDR8 record (3544 B) loads with its motion, no song, no name", fdr8_converts());
    check("project: a broken song or name is dropped, the project loads", fdr9_bad_song_name_dropped());
    check("song: an empty slot refused; a slot's garbage made safe (ASan)", song_garbage_slot());
    check("song: LOAD while a song plays: STOP TO LOAD", song_blocks_load());
    check("project: a rename writes the name only, the current project's too", rename_slot());
    check("MEMB at every extreme: bounded (ASan)", memb_extremes());
    check("RESON at every extreme: inside its lines (ASan), bounded", reson_extremes());
    check("settings: MUTE NEXT BAR, ZOOM and KNOB ACCEL survive a power cycle (Felucca's settings format)", mutebar_persists());
    check("settings: USB LEVEL FIXED survives a power cycle; a save without it reads MASTER", usb_level_persists());
    check("settings: a record longer than ours loads the defaults (not truncated)", settings_oversize_refused());
    for (k = 0; k < F_KINDS; k++)
        for (s = 0; s < (k == F_RANDOM || k == F_HEADERS ? 8 : 1); s++)
            for (seq = 0; seq < 2; seq++) {
                snprintf(what, sizeof what, "boot: %s #%d (%s)", KIND[k], s, seq ? "safe" : "normal");
                check(what, boot(k, (uint32_t)s + 1u, seq));
            }
    printf(fails ? "boot_test: %d FAILED\n" : "boot_test: all passed\n", fails);
    return fails ? 1 : 0;
}
