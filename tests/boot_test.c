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
    return bounded && (seq ? safe_start && peak == 0 : !safe_start);
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
           song.g[G_CAMT] == GP[G_CAMT].def;
}

int main(void)
{
    char what[96];
    int k, s, seq;
    check("project: an M1 record (FDR1) loads with PROB 100 %, 1 hit, SRC STEP, Grids defaults", fdr1_converts());
    check("project: an M2 record (FDR2) loads with COMP off and DUCK off", fdr2_converts());
    for (k = 0; k < F_KINDS; k++)
        for (s = 0; s < (k == F_RANDOM || k == F_HEADERS ? 8 : 1); s++)
            for (seq = 0; seq < 2; seq++) {
                snprintf(what, sizeof what, "boot: %s #%d (%s)", KIND[k], s, seq ? "safe" : "normal");
                check(what, boot(k, (uint32_t)s + 1u, seq));
            }
    printf(fails ? "boot_test: %d FAILED\n" : "boot_test: all passed\n", fails);
    return fails ? 1 : 0;
}
