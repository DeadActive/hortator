/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Projects: four slots in .noinit RAM, so they survive resets and UBOOT entry. With FELUCCA_FLASH
 * every save also goes to flash through storage.c, and an empty RAM slot is filled from flash.
 * Format "FDR1": the globals, the selected track, and per track every parameter and its 64 steps.
 * Felucca's formats ("FUN1".."FUN3") are not read. Settings + the panel table: as in Felucca. */
#define PROJ_MAGIC 0x31524446u                 /* "FDR1": 8 drum tracks */
typedef struct {
    int16_t p[P_COUNT];
    step_t step[NSTEP];
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
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
static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    if (st_load(OBJ_PROJECT0 + (slot & 3u), q, sizeof *q) != (int)sizeof *q || !proj_ok(q))
        q->magic = 0;
}
#endif

static void project_save(uint32_t slot)
{
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
        for (i = 0; i < NSTEP; i++) {
            t->step[i].on = s->step[i].on ? 1u : 0u;
            t->step[i].acc = s->step[i].acc ? 1u : 0u;
        }
    }
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
        settings.zoom = p.zoom;
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

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = settings.palette;
    p.lowcut = settings.lowcut;
    p.zoom = settings.zoom;
    p.panel = panel;
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
#endif
