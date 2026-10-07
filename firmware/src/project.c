/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Projects: four slots in .noinit RAM, so they survive resets and UBOOT entry. With FELUCCA_FLASH
 * every save also goes to flash through storage.c, and an empty RAM slot is filled from flash.
 * Format "FDR8": the globals (with the COMP's GHOST and the reverb TYPE), the selected track, per track every parameter and its
 * 64 steps (with PROB / RATCH, DUCK, the COMP settings, the LFOs, RESON), and the motion (motion.c, 128 events).
 * M1's "FDR1", M2's "FDR2", M3's "FDR3", the LFOs' "FDR4" RESON's "FDR5", GHOST's "FDR6" and the sound pack's
 * "FDR7" records (in flash) are converted on load.
 * Felucca's formats ("FUN1".."FUN3") are not read. Settings + the panel table: as in Felucca. */
#define PROJ_MAGIC 0x38524446u                 /* "FDR8": + the motion (motion.c) */
#define PROJ_MAGIC_V7 0x37524446u              /* "FDR7": the reverb TYPE (sound pack), converted on load */
#define PROJ_MAGIC_V6 0x36524446u              /* "FDR6": GHOST projects, converted on load */
#define PROJ_MAGIC_V5 0x35524446u              /* "FDR5": RESON projects, converted on load */
#define PROJ_MAGIC_V4 0x34524446u              /* "FDR4": LFO projects, converted on load */
#define PROJ_MAGIC_V3 0x33524446u              /* "FDR3": M3 projects, converted on load */
#define PROJ_MAGIC_V2 0x32524446u              /* "FDR2": M2 projects, converted on load */
#define PROJ_MAGIC_V1 0x31524446u              /* "FDR1": M1 projects, converted on load */
typedef struct {
    int16_t p[P_COUNT];
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    motion_store_t motion;                      /* FDR8: the motion (motion.c) */
    uint32_t sum;
} project_t;
project_t proj_slot[4] __attribute__((section(".noinit")));

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->sum == proj_sum(q); }

#if FELUCCA_FLASH
/* M1's format: the globals before G_GMODE, the parameters before P_SRC, steps of {on, acc} */
typedef struct { uint8_t on, acc; } step_v1_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_GMODE];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_SRC]; step_v1_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v1_t;
/* M2's format: the globals before G_CSRC, the parameters before P_DUCK */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CSRC];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_DUCK]; step_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v2_t;
/* M3's format: the globals before G_CGHOST, the parameters before P_LFO1 */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_LFO1]; step_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v3_t;
/* the LFO format: the globals before G_CGHOST, the parameters before P_RMODEL */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_RMODEL]; step_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v4_t;
/* RESON's format: the globals before G_CGHOST (the parameters as now) */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_v5_t;
/* GHOST's format: the globals before G_RTYPE (the parameters as now). 3024 B, as an FDR5 record: the version is
 * told by the marker, not the size */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_RTYPE];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_v6_t;
/* the sound pack's format: everything but the motion. 3028 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    uint32_t sum;
} project_v7_t;
static union {
    project_v1_t v1; project_v2_t v2; project_v3_t v3; project_v4_t v4; project_v5_t v5; project_v6_t v6; project_v7_t v7;
} proj_old;

/* an old record (proj_old, version ver) -> q: what it has; the rest at the defaults (v1: PROB 100 %, 1 hit,
 * SRC STEP, Grids; v1 / v2: COMP off, DUCK off; v1..v3: the LFOs off; v1..v4: RESON off; v1..v5: GHOST KEEP;
 * v1..v6: reverb TYPE ROOM; all: no motion) */
static void proj_from_old(project_t *q, uint32_t ver)
{
    uint32_t i, k;
    uint32_t ng = ver == 1u ? (uint32_t)G_GMODE : ver == 2u ? (uint32_t)G_CSRC : ver == 6u ? (uint32_t)G_RTYPE
                : ver == 7u ? (uint32_t)G_COUNT : (uint32_t)G_CGHOST;
    uint32_t np = ver == 1u ? (uint32_t)P_SRC : ver == 2u ? (uint32_t)P_DUCK : ver == 3u ? (uint32_t)P_LFO1
                : ver == 4u ? (uint32_t)P_RMODEL : (uint32_t)P_COUNT;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    for (i = 0; i < G_COUNT; i++)
        q->g[i] = i >= ng ? GP[i].def
                : ver == 1u ? proj_old.v1.g[i] : ver == 2u ? proj_old.v2.g[i] : ver == 3u ? proj_old.v3.g[i]
                : ver == 4u ? proj_old.v4.g[i] : ver == 5u ? proj_old.v5.g[i] : ver == 6u ? proj_old.v6.g[i] : proj_old.v7.g[i];
    q->sel = ver == 1u ? proj_old.v1.sel : ver == 2u ? proj_old.v2.sel : ver == 3u ? proj_old.v3.sel
           : ver == 4u ? proj_old.v4.sel : ver == 5u ? proj_old.v5.sel : ver == 6u ? proj_old.v6.sel : proj_old.v7.sel;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++)
            q->t[k].p[i] = i >= np ? TP[i].def
                         : ver == 1u ? proj_old.v1.t[k].p[i] : ver == 2u ? proj_old.v2.t[k].p[i]
                         : ver == 3u ? proj_old.v3.t[k].p[i] : ver == 4u ? proj_old.v4.t[k].p[i] : ver == 5u ? proj_old.v5.t[k].p[i] : ver == 6u ? proj_old.v6.t[k].p[i]
                         : proj_old.v7.t[k].p[i];
        for (i = 0; i < NSTEP; i++) {
            if (ver == 1u) {
                q->t[k].step[i].on = proj_old.v1.t[k].step[i].on;
                q->t[k].step[i].acc = proj_old.v1.t[k].step[i].acc;
            } else {
                q->t[k].step[i] = ver == 2u ? proj_old.v2.t[k].step[i]
                                : ver == 3u ? proj_old.v3.t[k].step[i] : ver == 4u ? proj_old.v4.t[k].step[i]
                                : ver == 5u ? proj_old.v5.t[k].step[i] : ver == 6u ? proj_old.v6.t[k].step[i]
                                : proj_old.v7.t[k].step[i];
            }
        }
    }
    q->sum = proj_sum(q);
}

static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), q, sizeof *q);
    if (n > 8 && n <= (int)sizeof proj_old && n != (int)sizeof *q) {   /* an old record: its marker names it */
        static const uint32_t MAGIC[8] = {0, PROJ_MAGIC_V1, PROJ_MAGIC_V2, PROJ_MAGIC_V3, PROJ_MAGIC_V4,
                                          PROJ_MAGIC_V5, PROJ_MAGIC_V6, PROJ_MAGIC_V7};
        static const uint32_t SIZE[8] = {0, sizeof proj_old.v1, sizeof proj_old.v2, sizeof proj_old.v3,
                                         sizeof proj_old.v4, sizeof proj_old.v5, sizeof proj_old.v6, sizeof proj_old.v7};
        uint32_t ver, hdr[2], sum;
        memcpy(&proj_old, q, (uint32_t)n);
        memcpy(hdr, &proj_old, sizeof hdr);
        memcpy(&sum, (const uint8_t *)&proj_old + n - 4, 4);
        for (ver = 1; ver < 8u; ver++)
            if (hdr[0] == MAGIC[ver] && hdr[1] == (uint32_t)n && SIZE[ver] == (uint32_t)n &&
                sum == proj_hash(&proj_old, (uint32_t)n - 4u)) {
                proj_from_old(q, ver);
                return;
            }
    }
    if (n != (int)sizeof *q || !proj_ok(q))
        q->magic = 0;
}
#endif

/* the sequencer runs (or starts this block): a flash erase silences the audio and stalls it, so no saving now */
static int transport_busy(void) { return song.playing || transport_req == 1u; }

static void project_save(uint32_t slot)
{
    if (transport_busy()) {                            /* (upstream 1.0) only while stopped */
        ui_message("STOP TO SAVE");
        return;
    }
    project_t *p = &proj_slot[slot & 3u];
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    for (i = 0; i < NTRK; i++) {
        memcpy(p->t[i].p, trk[i].p, sizeof trk[i].p);
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
    }
    p->motion = mo.s;                                  /* (stopped: the base is in the knobs) */
    p->sum = proj_sum(p);
#if FELUCCA_FLASH
    if (flash_ok) {
        ui_message(st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p) ? "SAVE ERROR" : "SAVED");
        return;
    }
#endif
    ui_message("SAVED (RAM)");
}

static void project_load(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
    uint32_t i, k;
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p))
        proj_fetch(slot);
#endif
    if (!proj_ok(p)) {
        ui_message("EMPTY SLOT");
        return;
    }
    transport_req = 2;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t m = (uint32_t)clamp(s->p[P_MODEL], 0, NMODELS - 1);
        drum_cut(t);
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = i >= P_E0 && i <= P_E7 ? &DMODELS[m].edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->p[P_MODEL] = (int16_t)m;
        t->model = (uint8_t)m;
        for (i = 0; i < NSTEP; i++) {                   /* garbage cannot index a table or divide by zero */
            const step_t *q = &s->step[i];
            t->step[i].on = q->on ? 1u : 0u;
            t->step[i].acc = q->acc ? 1u : 0u;
            t->step[i].cond = (uint8_t)(q->cond < COND_MAX ? q->cond : COND_MAX);
            t->step[i].rat = (uint8_t)(q->rat < 3u ? q->rat : 3u);
        }
    }
    for (k = 0, i = 0; k < NTRK; k++)                   /* RESON on 4 tracks at most: the first four keep it */
        if (trk[k].p[P_RMODEL] != RS_OFF && ++i > RS_MAXTRK)
            trk[k].p[P_RMODEL] = RS_OFF;
    for (k = 0, i = 0; k < NTRK; k++)                   /* CHORD on 2 of them at most: the first two keep it */
        if (trk[k].p[P_RMODEL] == RS_CHORD && ++i > 2u)
            trk[k].p[P_RMODEL] = RS_STRNG;
    if (motion_valid(&p->motion))                       /* the motion: a broken block is dropped, the rest loads */
        mo.s = p->motion;
    else
        memset(&mo.s, 0, sizeof mo.s);
    motion_forget();                                    /* the new knobs are the base: the stop puts nothing back */
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    ui.force = 1;
    ui_message("LOADED");
}

/* settings + learned panel table: one flash object (format unchanged from Felucca, so a calibrated
 * panel survives the change of firmware). The flash copy wins at boot. */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
} persist_t;
#define PERSIST_MAGIC 0x50455232u                  /* "PER2" */
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    if (st_load(OBJ_SETTINGS, &p, sizeof p) == (int)sizeof p && p.magic == PERSIST_MAGIC) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = p.palette;
        settings.lowcut = p.lowcut;
        settings.zoom = p.zoom & 1u;
        settings.mutebar = (p.zoom >> 1) & 1u;     /* kept in zoom's bit 1: the flash format stays Felucca's */
        settings.accel = !((p.zoom >> 2) & 1u);    /* bit 2 = KNOB ACCEL OFF: older saves read as on */
        settings.usbfix = (p.zoom >> 3) & 1u;      /* bit 3 = USB LEVEL FIXED: older saves read MASTER */
        settings.perfpage = (p.zoom >> 4) & 1u;    /* bit 4 = PERFORM PAGE: older saves read HOLD */
        if (p.panel.magic == PANEL_MAGIC)
            panel = p.panel;
        persist_saved = p;
    }
    {
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_ok(&proj_slot[i]))
                proj_fetch(i);
    }
#endif
}

static int project_used(uint32_t slot) { return proj_ok(&proj_slot[slot & 3u]); }

static void settings_save(void)                    /* asked for while playing: written once stopped (settings_poll) */
{
    if (transport_busy()) {
        ui.persist_pending = 1;
        return;
    }
    ui.persist_pending = 0;
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = settings.palette;
    p.lowcut = settings.lowcut;
    p.zoom = (settings.zoom & 1u) | (settings.mutebar & 1u) << 1 | (uint32_t)!settings.accel << 2 | (settings.usbfix & 1u) << 3 |
             (settings.perfpage & 1u) << 4;
    p.panel = panel;
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0)
        persist_saved = p;
#endif
}

static void settings_poll(void)                    /* every UI frame: a settings save left for the stop */
{
    if (ui.persist_pending && !transport_busy())
        settings_save();
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
#endif
