/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Projects: four slots in .noinit RAM, so they survive resets and UBOOT entry. With FELUCCA_FLASH
 * every save also goes to flash through storage.c, and an empty RAM slot is filled from flash.
 * Format "FDRF" (the 15th, 2026-10-24 parameter locks): as "FDRE", but its motion block is gone: the motion and the
 * parameter locks are the slot's lock bank (lockbank.c), its own flash object (storage.c OBJ_LOCKS0 + slot), saved
 * just before the record; the record links it (lk_link: a hash of the bank, lk_n: its count), so a save torn by a
 * power cut keeps the last whole pair (the record is the pair's commit). An FDRE or older record's motion converts
 * into the bank as motion events. "FDRE" (the 14th): the globals (with the COMP's GHOST and the reverb TYPE), the selected track, per track every
 * parameter and its 64 steps (with PROB / RATCH, each step's note offset, SLIDE and TIE (ACID), DUCK, the COMP settings,
 * the LFOs and their 4 DEST slots, RESON, the FILTER, PRIO), the motion
 * (motion.c, 128 events), the song and the project's name (song.c), each track's preset id and edited mark (presets.c).
 * M1's "FDR1", M2's "FDR2", M3's "FDR3", the LFOs' "FDR4" RESON's "FDR5", GHOST's "FDR6", the sound pack's
 * "FDR7", motion's "FDR8", the song's "FDR9", the FILTER's "FDRA", ACID's "FDRB", PRIO's "FDRC" and the presets' "FDRD"
 * records (in flash) are converted on load (the FILTER OFF, the steps at NOTE C4 with no SLIDE / TIE, PRIO HIGH, no
 * preset, not edited (FDRD: as stored); CHOKE as stored; an LFO's DEST and DEPTH are DEST slots 1 / 2 with sources LFO 1
 * / LFO 2 (the same ids), slots 3 / 4 off). An older image in a RAM slot (FDRA, FDRB: 3672 B a slot, FDRC: 3688 B,
 * FDRD: 3712 B, FDRE's are 3840 B) is never read as one: the slots are filled from flash at boot. FDRE fills a flash
 * sector's payload (3840 B, ST_PAYLOAD_MAX): a larger record needs another layout. The parameters after its block
 * (P_NSTORED .., RESON's KEYF) ride in a track's steps' spare byte (step_t.rsv: parameter P_NSTORED + j in step j's,
 * 0..255; every earlier firmware wrote 0 there and ignores it on load), so an FDRE from before them loads with them at 0.
 * Felucca's formats ("FUN1".."FUN3") are not read. Settings + the panel table: as in Felucca. */
#define PROJ_MAGIC 0x46524446u                 /* "FDRF" (the 15th): the lock bank (spec 2026-10-24 parameter locks) */
#define PROJ_MAGIC_VE 0x45524446u              /* "FDRE": + the LFO DEST slots (fixes 2026-10-21 item 3); converted on load */
#define PROJ_MAGIC_VD 0x44524446u              /* "FDRD": each track's preset and edited mark; converted on load */
#define PROJ_MAGIC_VC 0x43524446u              /* "FDRC": PRIO per track, no presets; converted on load */
#define PROJ_MAGIC_VB 0x42524446u              /* "FDRB": the step's note, SLIDE and TIE (ACID); converted on load */
#define PROJ_MAGIC_VA 0x41524446u              /* "FDRA": the FILTER, the old steps; converted on load (same size) */
#define PROJ_MAGIC_V9 0x39524446u              /* "FDR9": the song and the name, converted on load */
#define PROJ_MAGIC_V8 0x38524446u              /* "FDR8": the motion, converted on load */
#define PROJ_MAGIC_V7 0x37524446u              /* "FDR7": the reverb TYPE (sound pack), converted on load */
#define PROJ_MAGIC_V6 0x36524446u              /* "FDR6": GHOST projects, converted on load */
#define PROJ_MAGIC_V5 0x35524446u              /* "FDR5": RESON projects, converted on load */
#define PROJ_MAGIC_V4 0x34524446u              /* "FDR4": LFO projects, converted on load */
#define PROJ_MAGIC_V3 0x33524446u              /* "FDR3": M3 projects, converted on load */
#define PROJ_MAGIC_V2 0x32524446u              /* "FDR2": M2 projects, converted on load */
#define PROJ_MAGIC_V1 0x31524446u              /* "FDR1": M1 projects, converted on load */
typedef struct {
    int16_t p[P_NSTORED];                       /* the block; P_NSTORED + j: step[j].rsv (proj_xp_*) */
    step_t step[NSTEP];
} proj_trk_t;
typedef char proj_xp_fit[P_COUNT - P_NSTORED <= NSTEP && P_NSTORED == 69 ? 1 : -1];   /* (FDRE's block: 69) */
/* the parameters past the block: into / out of a stored track's steps' spare bytes (0 = their value 0) */
static void proj_xp_put(proj_trk_t *s, const int16_t *p)
{
    uint32_t i;
    for (i = P_NSTORED; i < P_COUNT; i++)
        s->step[i - P_NSTORED].rsv = (uint8_t)clamp(p[i], 0, 255);
}
static int16_t proj_xp_get(const proj_trk_t *s, uint32_t id) { return (int16_t)s->step[id - P_NSTORED].rsv; }
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    uint16_t lk_n;                              /* FDRF: its lock bank's count (0: no bank needed); FDR8..FDRE: the
                                                 * motion here */
    uint32_t lk_link;                           /* FDRF: .. its proj_link */
    uint32_t var_link;                          /* FDRF (2026-10-26): its variations' object (var_link_of; 0: none) */
    uint16_t var_len;                           /* .. its stored length (0: none, variations 2..8 empty: 0.24.0's) */
    uint8_t rsv_mo[504];                        /* FDRF: reserved, 0 */
    chain_config_t song;                        /* FDR9: the song (song.c) */
    char name[NAME_LEN];                        /* FDR9: the name (NAME_SET, 0-padded; "" = none) */
    uint16_t pre[NTRK];                         /* FDRD: each track's preset: a factory one's uid, 0x8000 | a user
                                                 * slot, PRE_NONE none (presets.c preset_rec_enc: not the id) */
    uint8_t pre_ed[NTRK];                       /* FDRD: .. edited since (0 / 1) */
    uint32_t sum;
} project_t;
typedef char project_size[sizeof(project_t) == 3840u ? 1 : -1];
project_t proj_slot[4] __attribute__((section(".noinit")));
/* FDRE (0.23.x): FDRF's layout with the motion (128 events) where FDRF has its bank link */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_t t[NTRK];
    motion_store_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint16_t pre[NTRK];
    uint8_t pre_ed[NTRK];
    uint32_t sum;
} project_ve_t;
typedef char project_ve_size[sizeof(project_ve_t) == 3840u &&
                             __builtin_offsetof(project_ve_t, motion) == __builtin_offsetof(project_t, lk_n) &&
                             __builtin_offsetof(project_ve_t, song) == __builtin_offsetof(project_t, song) ? 1 : -1];
/* each slot's lock bank (lockbank.c): the one its record links (persist_boot, a LOAD's fetch, a SAVE); .bss (the
 * slots' records are .noinit: a reset finds them, the banks come from flash again). A SONG row plays its slot's */
static lk_bank_t lk_slot[4];
static uint8_t lk_dirty;                            /* bit per slot: its RAM bank is not in flash (an older record converted
                                                     * at boot, or its SAVE failed): a record written alone (RENAME) writes
                                                     * it first, so the record never links a bank flash lacks */
static uint8_t lk_blocked;                          /* this boot's USR3 clamp failed (USR3 still longer than 48 KiB in
                                                     * flash): no lock-bank write (an older Hortator would play them) */
static uint8_t var_blocked;                         /* this boot's USR2 clamp failed (USR2 still longer than 48 KiB in
                                                     * flash): no variation write (spec 2026-10-26 cue / variations) */
/* the variations (varpack.c, spec 2026-10-26 cue / variations): a slot's variations 2..8 are its own flash object
 * (storage.c OBJ_VAR0 + slot), saved before its lock bank and its record, which links it (var_link, var_len: a torn
 * save keeps a whole triple). RAM holds two banks (.bss): the loaded slot's (var_play) and one park buffer; var_who
 * says whose each holds (slot + 1, 0 none). A slot's variations not in RAM are read from flash (XIP) */
static var_bank_t var_buf[2];
static uint8_t var_who[2];
static uint8_t var_play;                            /* var_buf's index of the loaded slot's bank */
static uint8_t var_dirty;                           /* bit per slot: its RAM variations are not flash's (a SAVE that
                                                     * failed, later the edits): a record written alone (RENAME) writes
                                                     * them first */
/* the RAM model (spec 2026-10-26: flash is the truth, edits live in RAM until SAVE): each slot's record (proj_slot),
 * lock bank (lk_slot) and variations are its RAM image; the loaded slot's live state goes back into it (a write-back:
 * a LOAD or a cue of another slot, a paste) so nothing unsaved is lost, and a reset brings flash's back */
static uint8_t proj_dirty;                          /* bit per slot: its RAM record is not flash's (a write-back, a paste) */
static uint32_t proj_fsum[4];                       /* each slot's record as flash has it (its sum; 0 none) */
static uint32_t live_sum0;                          /* live_sum when the loaded slot was last its image (LOAD, SAVE, a
                                                     * switch): differs = the live state has unsaved edits */
static const step_t *var_sp[NTRK];                  /* (a variation's packing / unpacking: static, off the ISR's frame) */
static step_t *var_lp[NTRK];
static int16_t var_tm[NTRK][4];
#ifndef VAR_XIP                                     /* host tests / the sim: their flash image */
#define VAR_XIP(off) ((const void *)fm1_xip_ptr(off))
#endif
static uint32_t proj_hash(const void *p, uint32_t n);
static uint32_t proj_link(const lk_bank_t *b) { return proj_hash(b, sizeof *b) | 1u; }   /* (never 0) */
static uint32_t var_link_of(const var_bank_t *b) { return proj_hash(b, 8u + b->used) | 1u; }   /* (never 0) */
static void lk_empty(uint32_t slot)
{
    memset(&lk_slot[slot], 0, sizeof lk_slot[slot]);
    lk_seal(&lk_slot[slot], slot);
}

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

typedef struct { uint8_t on, acc, cond, rat; } step_v9_t;   /* a step as FDR2..FDRA stored it */
/* FDRA: everything, the steps as FDR2..FDR9 (same size as FDRB: told by the marker) */
typedef struct {
    int16_t p[P_PRIO];
    step_v9_t step[NSTEP];
} proj_trk_va_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_va_t t[NTRK];
    motion_store_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_va_t;
typedef char project_va_size[sizeof(project_va_t) == 3672u ? 1 : -1];
/* FDRB (0.16.0): everything but PRIO, 3672 B (FDRA's size: told by the marker) */
typedef struct {
    int16_t p[P_PRIO];
    step_t step[NSTEP];
} proj_trk_vb_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_vb_t t[NTRK];
    motion_store_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_vb_t;
typedef char project_vb_size[sizeof(project_vb_t) == 3672u ? 1 : -1];
/* a track as FDRC and FDRD stored it: the parameters up to PRIO (no DEST slots 3 / 4, no slot sources) */
typedef struct {
    int16_t p[P_PRIO + 1];
    step_t step[NSTEP];
} proj_trk_vd_t;
/* FDRC (0.17.0 .. 0.20.0): everything but the presets, 3688 B; FDRD's fields up to the name at the same offsets */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_vd_t t[NTRK];
    motion_store_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_vc_t;
/* FDRD (0.21.0 .. 0.22.0): everything but the DEST slots' new parameters, 3712 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_vd_t t[NTRK];
    motion_store_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint16_t pre[NTRK];
    uint8_t pre_ed[NTRK];
    uint32_t sum;
} project_vd_t;
typedef char project_vc_size[sizeof(project_vc_t) == 3688u && sizeof(project_vd_t) == 3712u &&
                             __builtin_offsetof(project_vc_t, name) == __builtin_offsetof(project_vd_t, name) ? 1 : -1];

static step_t step_up(const step_v9_t *o)          /* an FDR2..FDRA step: the flags, NOTE C4, no SLIDE / TIE */
{
    step_t s;
    memset(&s, 0, sizeof s);
    s.on = o->on ? 1u : 0u;
    s.acc = o->acc ? 1u : 0u;
    s.rat = (uint8_t)(o->rat < 3u ? o->rat : 3u);
    s.cond = o->cond;
    return s;
}

/* FDRA and FDRB: the steps at the same offsets (4 bytes each), everything else the same: told by the marker */
typedef char project_va_steps[__builtin_offsetof(project_va_t, t[1].step[1]) ==
                              __builtin_offsetof(project_vb_t, t[1].step[1]) ? 1 : -1];

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
    struct { int16_t p[P_DUCK]; step_v9_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v2_t;
/* M3's format: the globals before G_CGHOST, the parameters before P_LFO1 */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_LFO1]; step_v9_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v3_t;
/* the LFO format: the globals before G_CGHOST, the parameters before P_RMODEL */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    struct { int16_t p[P_RMODEL]; step_v9_t step[NSTEP]; } t[NTRK];
    uint32_t sum;
} project_v4_t;
/* a track as FDR5..FDR9 stored it: the parameters before P_FTYPE (no FILTER) */
typedef struct {
    int16_t p[P_FTYPE];
    step_v9_t step[NSTEP];
} proj_trk_v9_t;
/* RESON's format: the globals before G_CGHOST (the parameters before P_FTYPE) */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_CGHOST];
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[NTRK];
    uint32_t sum;
} project_v5_t;
/* GHOST's format: the globals before G_RTYPE (the parameters as now). 3024 B, as an FDR5 record: the version is
 * told by the marker, not the size */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_RTYPE];
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[NTRK];
    uint32_t sum;
} project_v6_t;
/* the sound pack's format: everything but the motion. 3028 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[NTRK];
    uint32_t sum;
} project_v7_t;
/* motion's format: everything but the song and the name. 3544 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[NTRK];
    motion_store_t motion;
    uint32_t sum;
} project_v8_t;
/* FDR9: everything but the FILTER. 3592 B */
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];
    proj_trk_v9_t t[NTRK];
    motion_store_t motion;
    chain_config_t song;
    char name[NAME_LEN];
    uint32_t sum;
} project_v9_t;
static union {
    project_v1_t v1; project_v2_t v2; project_v3_t v3; project_v4_t v4; project_v5_t v5; project_v6_t v6; project_v7_t v7;
    project_v8_t v8; project_v9_t v9; project_va_t va; project_vb_t vb; project_vc_t vc; project_vd_t vd;
    project_ve_t ve;
} proj_old;

/* a converted record's bank (b: its slot's): the old motion (none: m 0, or a broken one: dropped) as motion events,
 * the record linking it */
static void proj_old_bank(project_t *q, lk_bank_t *b, const motion_store_t *m)
{
    uint32_t slot = (uint32_t)(b - lk_slot);
    if (!m || !lk_from_motion(b, slot, m))
        lk_empty(slot);
    lk_dirty |= (uint8_t)(1u << (slot & 3u));       /* (converted in RAM: not in flash until a save) */
    q->lk_link = proj_link(b);
    q->lk_n = b->count;
}
static void proj_old_done(project_t *q)            /* a converted record: no presets, not edited; its sum */
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        q->pre[k] = (uint16_t)PRE_NONE;
        q->pre_ed[k] = 0;
    }
    q->sum = proj_sum(q);
}

/* an old record (proj_old, version ver) -> q: what it has; the rest at the defaults (v1: PROB 100 %, 1 hit,
 * SRC STEP, Grids; v1 / v2: COMP off, DUCK off; v1..v3: the LFOs off; v1..v4: RESON off; v1..v5: GHOST KEEP;
 * v1..v6: reverb TYPE ROOM; v1..v7: no motion; v1..v8: no song, no name; v1..v9: the FILTER OFF; v1..v10: the steps
 * at NOTE C4, no SLIDE / TIE; v1..v11: PRIO HIGH; v1..v12: no preset, not edited; all: CHOKE as stored, an LFO's DEST
 * and DEPTH DEST slot 1 / 2 (sources LFO 1 / 2: their defaults), slots 3 / 4 off) */
static void proj_from_old(project_t *q, lk_bank_t *b, uint32_t ver)
{
    uint32_t i, k;
    uint32_t ng, np;
    if (ver == 14u) {                                /* FDRE: everything as stored, its motion into the bank */
        memcpy(q, &proj_old.ve, sizeof *q);
        q->magic = PROJ_MAGIC;
        memset(&q->lk_n, 0, sizeof proj_old.ve.motion);
        proj_old_bank(q, b, &proj_old.ve.motion);
        q->sum = proj_sum(q);
        return;
    }
    if (ver >= 12u) {                                /* FDRC / FDRD (FDRC: FDRD's fields up to the name): the
                                                      * parameters up to PRIO, the slots' new ones at their defaults */
        const project_vd_t *o = &proj_old.vd;
        memset(q, 0, sizeof *q);
        q->magic = PROJ_MAGIC;
        q->size = sizeof *q;
        memcpy(q->g, o->g, sizeof q->g);
        q->sel = o->sel;
        for (k = 0; k < NTRK; k++) {
            for (i = 0; i < P_NSTORED; i++)
                q->t[k].p[i] = i <= P_PRIO ? o->t[k].p[i] : TP[i].def;
            memcpy(q->t[k].step, o->t[k].step, sizeof q->t[k].step);
        }
        proj_old_bank(q, b, &o->motion);
        q->song = o->song;
        memcpy(q->name, o->name, NAME_LEN);
        if (ver == 12u) {
            proj_old_done(q);
            return;
        }
        memcpy(q->pre, o->pre, sizeof q->pre);      /* FDRD: its presets as stored */
        memcpy(q->pre_ed, o->pre_ed, sizeof q->pre_ed);
        q->sum = proj_sum(q);
        return;
    }
    if (ver >= 8u) {                                 /* FDR8 / FDR9: the tracks without the FILTER; FDRA / FDRB: without
                                                      * PRIO; FDRB: the steps as now */
        const int16_t *g = ver == 8u ? proj_old.v8.g : ver == 9u ? proj_old.v9.g : ver == 10u ? proj_old.va.g : proj_old.vb.g;
        np = ver < 10u ? (uint32_t)P_FTYPE : (uint32_t)P_PRIO;
        memset(q, 0, sizeof *q);
        q->magic = PROJ_MAGIC;
        q->size = sizeof *q;
        for (i = 0; i < G_COUNT; i++)
            q->g[i] = g[i];
        q->sel = ver == 8u ? proj_old.v8.sel : ver == 9u ? proj_old.v9.sel : ver == 10u ? proj_old.va.sel : proj_old.vb.sel;
        for (k = 0; k < NTRK; k++) {
            const int16_t *op = ver == 8u ? proj_old.v8.t[k].p : ver == 9u ? proj_old.v9.t[k].p
                              : ver == 10u ? proj_old.va.t[k].p : proj_old.vb.t[k].p;
            const step_v9_t *os = ver == 10u ? proj_old.va.t[k].step : ver == 9u ? proj_old.v9.t[k].step : proj_old.v8.t[k].step;
            for (i = 0; i < P_NSTORED; i++)
                q->t[k].p[i] = i < np ? op[i] : TP[i].def;
            if (ver == 11u)
                memcpy(q->t[k].step, proj_old.vb.t[k].step, sizeof q->t[k].step);
            else
                for (i = 0; i < NSTEP; i++)
                    q->t[k].step[i] = step_up(&os[i]);
        }
        proj_old_bank(q, b, ver == 8u ? &proj_old.v8.motion : ver == 9u ? &proj_old.v9.motion
                          : ver == 10u ? &proj_old.va.motion : &proj_old.vb.motion);
        if (ver >= 9u) {
            q->song = ver == 9u ? proj_old.v9.song : ver == 10u ? proj_old.va.song : proj_old.vb.song;
            memcpy(q->name, ver == 9u ? proj_old.v9.name : ver == 10u ? proj_old.va.name : proj_old.vb.name, NAME_LEN);
        }
        proj_old_done(q);
        return;
    }
    ng = ver == 1u ? (uint32_t)G_GMODE : ver == 2u ? (uint32_t)G_CSRC : ver == 6u ? (uint32_t)G_RTYPE
                : ver == 7u ? (uint32_t)G_COUNT : (uint32_t)G_CGHOST;
    np = ver == 1u ? (uint32_t)P_SRC : ver == 2u ? (uint32_t)P_DUCK : ver == 3u ? (uint32_t)P_LFO1
                : ver == 4u ? (uint32_t)P_RMODEL : (uint32_t)P_FTYPE;
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
        for (i = 0; i < P_NSTORED; i++)
            q->t[k].p[i] = i >= np ? TP[i].def
                         : ver == 1u ? proj_old.v1.t[k].p[i] : ver == 2u ? proj_old.v2.t[k].p[i]
                         : ver == 3u ? proj_old.v3.t[k].p[i] : ver == 4u ? proj_old.v4.t[k].p[i] : ver == 5u ? proj_old.v5.t[k].p[i] : ver == 6u ? proj_old.v6.t[k].p[i]
                         : proj_old.v7.t[k].p[i];
        for (i = 0; i < NSTEP; i++) {
            if (ver == 1u) {
                q->t[k].step[i].on = proj_old.v1.t[k].step[i].on ? 1u : 0u;   /* (1-bit fields: 2 is set) */
                q->t[k].step[i].acc = proj_old.v1.t[k].step[i].acc ? 1u : 0u;
            } else {
                q->t[k].step[i] = step_up(ver == 2u ? &proj_old.v2.t[k].step[i]
                                : ver == 3u ? &proj_old.v3.t[k].step[i] : ver == 4u ? &proj_old.v4.t[k].step[i]
                                : ver == 5u ? &proj_old.v5.t[k].step[i] : ver == 6u ? &proj_old.v6.t[k].step[i]
                                : &proj_old.v7.t[k].step[i]);
            }
        }
    }
    proj_old_bank(q, b, 0);                          /* (no motion before FDR8) */
    proj_old_done(q);
}

/* slot's bank from flash, the one its record (proj_slot, valid) links: 1 found; 0 none needed (lk_n 0): empty; -1
 * lost (neither copy a valid bank of this slot with the record's link: an upload over it, a damaged sector, a torn
 * save's leftover): empty, its lk_lost bit set. Either copy may hold it (a save torn after its bank was written leaves
 * the new bank current and the old record's in the other copy) */
static int proj_bank_fetch(uint32_t slot)
{
    const project_t *q = &proj_slot[slot & 3u];
    lk_bank_t *b = &lk_slot[slot & 3u];
    uint32_t c;
    lk_dirty &= (uint8_t)~(1u << (slot & 3u));      /* (the bank flash has, or none) */
    if (!q->lk_n) {
        lk_empty(slot & 3u);
        return 0;
    }
    for (c = 0; c < 2u; c++) {
        st_hdr_t h;
        if (st_head(OBJ_LOCKS0 + (slot & 3u), c, &h) == 0 && h.len == sizeof *b &&
            st_body(OBJ_LOCKS0 + (slot & 3u), c, &h) == 0) {
            memcpy(b, st_buf, sizeof *b);
            if (lk_valid(b, slot & 3u) && b->count == q->lk_n && proj_link(b) == q->lk_link)
                return 1;
        }
    }
    lk_empty(slot & 3u);
    lk_lost |= (uint8_t)(1u << (slot & 3u));
    return -1;
}
/* slot's record (and its bank) from flash: as proj_bank_fetch, -2 no record (an empty slot); an old record converts
 * (its motion into the bank: 0 or 1) */
static int proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), q, sizeof *q);
    lk_dirty &= (uint8_t)~(1u << (slot & 3u));
    if (n > 8 && n <= (int)sizeof proj_old && !proj_ok(q)) {   /* an old record: its marker names it */
        static const uint32_t MAGIC[15] = {0, PROJ_MAGIC_V1, PROJ_MAGIC_V2, PROJ_MAGIC_V3, PROJ_MAGIC_V4, PROJ_MAGIC_V5,
                                           PROJ_MAGIC_V6, PROJ_MAGIC_V7, PROJ_MAGIC_V8, PROJ_MAGIC_V9, PROJ_MAGIC_VA, PROJ_MAGIC_VB,
                                           PROJ_MAGIC_VC, PROJ_MAGIC_VD, PROJ_MAGIC_VE};
        static const uint32_t SIZE[15] = {0, sizeof proj_old.v1, sizeof proj_old.v2, sizeof proj_old.v3,
                                          sizeof proj_old.v4, sizeof proj_old.v5, sizeof proj_old.v6, sizeof proj_old.v7,
                                          sizeof proj_old.v8, sizeof proj_old.v9, sizeof proj_old.va, sizeof proj_old.vb,
                                          sizeof proj_old.vc, sizeof proj_old.vd, sizeof proj_old.ve};
        uint32_t ver, hdr[2], sum;
        memcpy(&proj_old, q, (uint32_t)n);
        memcpy(hdr, &proj_old, sizeof hdr);
        memcpy(&sum, (const uint8_t *)&proj_old + n - 4, 4);
        for (ver = 1; ver < 15u; ver++)
            if (hdr[0] == MAGIC[ver] && hdr[1] == (uint32_t)n && SIZE[ver] == (uint32_t)n &&
                sum == proj_hash(&proj_old, (uint32_t)n - 4u)) {
                proj_from_old(q, &lk_slot[slot & 3u], ver);
                proj_fsum[slot & 3u] = q->sum;          /* (converted: as flash's, until edited) */
                return lk_slot[slot & 3u].count ? 1 : 0;
            }
    }
    if (n != (int)sizeof *q || !proj_ok(q)) {
        q->magic = 0;
        lk_empty(slot & 3u);
        proj_fsum[slot & 3u] = 0;
        return -2;
    }
    proj_fsum[slot & 3u] = q->sum;
    return proj_bank_fetch(slot);
}
#endif

/* slot's variations in flash: the copy of OBJ_VAR0 + slot (either: as proj_bank_fetch) that is whole and is the one
 * its record (proj_slot, valid) links, as an XIP pointer; 0 none needed (var_len 0) or lost (no copy matches: an
 * upload over it, a damaged sector, a torn save's leftover: the boot's check sets var_lost, said once) */
static const var_bank_t *proj_var_flash(uint32_t slot)
{
#if FELUCCA_FLASH
    const project_t *q = &proj_slot[slot & 3u];
    uint32_t c;
    if (!flash_ok || !q->var_len)
        return 0;
    for (c = 0; c < 2u; c++) {
        st_hdr_t h;
        const var_bank_t *b;
        if (st_head(OBJ_VAR0 + (slot & 3u), c, &h) || h.len != q->var_len || h.len > VAR_BYTES || h.len < 8u)
            continue;
        b = (const var_bank_t *)VAR_XIP(st_sector(OBJ_VAR0 + (slot & 3u), c) + ST_PAYLOAD_OFF);
        if (st_crc32(b, h.len) == h.crc && var_valid(b, slot & 3u, h.len) && var_link_of(b) == q->var_link)
            return b;
    }
#else
    (void)slot;
#endif
    return 0;
}
/* slot's variations -> dst: its flash copy (the rest of dst 0), or empty: 1 found, 0 none, -1 lost */
static int proj_var_load(uint32_t slot, var_bank_t *dst)
{
    const var_bank_t *f = proj_ok(&proj_slot[slot & 3u]) ? proj_var_flash(slot) : 0;
    var_empty(dst, slot & 3u);
    if (!f)
        return proj_ok(&proj_slot[slot & 3u]) && proj_slot[slot & 3u].var_len ? -1 : 0;
    memcpy(dst, f, 8u + f->used);
    return 1;
}

/* the sequencer runs (or starts this block): a flash erase silences the audio and stalls it, so no saving now */
static int transport_busy(void) { return song.playing || transport_req == 1u; }

/* 12 stored name bytes -> d (NAME_LEN + 1): up to the first 0; a byte outside NAME_SET: no name */
static void proj_name_get(char *d, const char *s)
{
    uint32_t i;
    name_set(d, "");
    for (i = 0; i < NAME_LEN && s[i]; i++) {
        if (!name_char_ok(s[i])) {
            name_set(d, "");
            return;
        }
        d[i] = s[i];
    }
}

/* the slot loaded (its variations in var_buf[var_play]), -1 none (the power-on kit). (Its image's sum may be stale a
 * moment: the ISR's cue writes it, cue_poll seals it) */
static int32_t proj_loaded(void)
{
    uint32_t w = var_who[var_play];
    return w && proj_slot[w - 1u].magic == PROJ_MAGIC ? (int32_t)w - 1 : -1;
}
/* slot's record <- the live state with the knobs at rest (what STOP leaves: motion's base, a held lock's value under
 * it, an LFO's resting copy: motion.c motion_rest), its lock bank <- mo.b. Playing a variation other than 1, the
 * steps and the timing (CHAIN_TIMING) stay variation 1's, from the loaded slot's image (p itself, or the loaded slot's
 * for a SAVE to another one). globals 0: the image's globals and selected track kept (a cue keeps the live ones). The
 * variation link and the sum: the caller's. Main loop with the IRQ off, or the ISR */
static __attribute__((noinline)) void proj_image(project_t *p, uint32_t slot, int globals)
{
    int32_t ld = proj_loaded();
    const project_t *v1 = var_cur && ld >= 0 ? &proj_slot[ld] : 0;
    uint32_t i, j, k;
    int ok = proj_ok(p);
    if (globals || !ok) {
        for (i = 0; i < G_COUNT; i++)
            p->g[i] = song.g[i];
        p->sel = song.sel;
    }
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    p->rsv[0] = p->rsv[1] = p->rsv[2] = 0;
    for (k = 0; k < NTRK; k++) {
        int16_t tm[4], mute = p->t[k].p[P_MUTE];
        int keep = !globals && ok;                      /* (a write-back: the mutes are the performance's, kept) */
        for (j = 0; j < 4u; j++)
            tm[j] = v1 ? v1->t[k].p[CHAIN_TIMING[j]] : 0;
        for (i = 0; i < P_NSTORED; i++)
            p->t[k].p[i] = motion_rest(k, i);
        if (keep)
            p->t[k].p[P_MUTE] = mute;
        if (v1) {
            if (p != v1)
                memcpy(p->t[k].step, v1->t[k].step, sizeof p->t[k].step);
            for (j = 0; j < 4u; j++)
                p->t[k].p[CHAIN_TIMING[j]] = tm[j];
        } else {
            memcpy(p->t[k].step, trk[k].step, sizeof p->t[k].step);
        }
        for (i = P_NSTORED; i < P_COUNT; i++)           /* KEYF: the steps' spare bytes */
            p->t[k].step[i - P_NSTORED].rsv = (uint8_t)clamp(motion_rest(k, i), 0, 255);
        p->pre[k] = (uint16_t)preset_rec_enc(trk[k].pre);   /* (stable: a uid / a user slot, presets.c) */
        p->pre_ed[k] = trk[k].pre_ed;
    }
    lk_slot[slot & 3u] = mo.b;                          /* the bank: the motion and the locks (sealed: proj_seal) */
    memset(p->rsv_mo, 0, sizeof p->rsv_mo);
    p->song = chain.cfg;
    memcpy(p->name, chain.name, NAME_LEN);
}
static void proj_var_link(project_t *p, const var_bank_t *b)   /* the record links b (none used: no object) */
{
    p->var_len = var_mask(b) ? (uint16_t)(8u + b->used) : 0u;
    p->var_link = p->var_len ? var_link_of(b) : 0u;
}
/* slot's image whole again after proj_image (main loop): its lock bank sealed and linked, its variations linked (when
 * a RAM bank holds them), its sum, its mark */
static void proj_seal(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
    slot &= 3u;
    lk_seal(&lk_slot[slot], slot);
    p->lk_link = proj_link(&lk_slot[slot]);
    p->lk_n = lk_slot[slot].count;
    if (var_who[var_play] == slot + 1u)
        proj_var_link(p, &var_buf[var_play]);
    else if (var_who[var_play ^ 1u] == slot + 1u)
        proj_var_link(p, &var_buf[var_play ^ 1u]);
    p->sum = proj_sum(p);
    if (p->sum != proj_fsum[slot])
        proj_dirty |= (uint8_t)(1u << slot);
    else
        proj_dirty &= (uint8_t)~(1u << slot);
}
/* the live variation (var_cur 1..7) packed into the loaded slot's bank: 0 (its var_dirty bit set when bytes changed),
 * -1 VARIATIONS FULL (nothing changed) */
static __attribute__((noinline)) int var_pack_live(uint32_t slot)
{
    uint32_t k, j;
    int r;
    if (!var_cur)
        return 0;
    for (k = 0; k < NTRK; k++) {
        var_sp[k] = trk[k].step;
        for (j = 0; j < 4u; j++)
            var_tm[k][j] = trk[k].p[CHAIN_TIMING[j]];
    }
    r = var_put(&var_buf[var_play], var_cur, var_sp, var_tm);
    if (r == 0)
        var_dirty |= (uint8_t)(1u << (slot & 3u));
    return r < 0 ? -1 : 0;
}
/* the loaded slot's live state -> its RAM image (record, lock bank, the playing variation); its marks. 0, -1
 * VARIATIONS FULL (nothing changed). No message, no flash. Main loop: the IRQ off for the copy only (the sums after) */
static int proj_writeback_g(int globals)        /* globals 1: the live globals too (a LOAD replaces them) */
{
    int32_t ld = proj_loaded();
    int r;
    if (ld < 0)
        return 0;
    fm1_irq_off();
    r = var_pack_live((uint32_t)ld);
    if (!r)
        proj_image(&proj_slot[ld], (uint32_t)ld, globals);
    fm1_irq_on();
    if (r)
        return -1;
    proj_seal((uint32_t)ld);
    return 0;
}
static int proj_writeback(void) { return proj_writeback_g(0); }
/* the live globals differ from the loaded slot's record (a LOAD of it would change them: the tempo ..) */
static int live_globals_edited(void)
{
    int32_t ld = proj_loaded();
    uint32_t i;
    if (ld < 0)
        return 0;
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_CLOCK &&
            song.g[i] != (int16_t)clamp(proj_slot[ld].g[i], GP[i].min, GP[i].max))
            return 1;
    return 0;
}
/* the live state's fingerprint: what a write-back would store (the knobs at rest, the steps, the presets' marks, the
 * lock bank, the song, the variation playing; not the globals, not the name: RENAME keeps that in step) */
static uint32_t live_sum(void)
{
    uint32_t h = 2166136261u, k, i;
    const uint8_t *b;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_COUNT; i++)
            h = (h ^ (uint16_t)motion_rest(k, i)) * 16777619u;
        h = (h ^ trk[k].pre ^ (uint32_t)trk[k].pre_ed << 16) * 16777619u;
    }
    for (k = 0; k < NTRK; k++)
        for (b = (const uint8_t *)trk[k].step, i = 0; i < sizeof trk[k].step; i++)
            h = (h ^ b[i]) * 16777619u;
    for (b = (const uint8_t *)&mo.b, i = 0; i < sizeof mo.b; i++)
        h = (h ^ b[i]) * 16777619u;
    for (b = (const uint8_t *)&chain.cfg, i = 0; i < sizeof chain.cfg; i++)
        h = (h ^ b[i]) * 16777619u;
    return (h ^ var_cur) * 16777619u;
}
/* slot s has unsaved edits: its record, lock bank or variations in RAM are not flash's, or (the loaded slot) the live
 * state changed since it was its image. Main loop */
static int slot_dirty(uint32_t s)
{
    s &= 3u;
    if (proj_slot[s].magic != PROJ_MAGIC)               /* (no record; a record's sum is checked when it is read) */
        return 0;
    if (proj_slot[s].sum != proj_fsum[s] || ((proj_dirty | var_dirty) >> s) & 1u)
        return 1;
    return (int32_t)s == proj_loaded() && live_sum() != live_sum0;
}
/* leaving slot out for in (a LOAD, a cue): 0 its variations can be kept (unsaved: they become the park, else they are
 * dropped), or 1 + c: the park holds slot c's unsaved ones (SAVE C FIRST) */
static int var_room(uint32_t out, uint32_t in)
{
    uint32_t c = var_who[var_play ^ 1u];
    if (!((var_dirty >> (out & 3u)) & 1u) || !c || c - 1u == (in & 3u) || c - 1u == (out & 3u))
        return 0;
    return ((var_dirty >> (c - 1u)) & 1u) ? (int)c : 0;
}
static void var_leave(uint32_t out, uint32_t in)   /* (var_room said 0) the banks' roles for in */
{
    uint32_t pk = var_play ^ 1u;
    if (var_who[pk] == (in & 3u) + 1u || ((var_dirty >> (out & 3u)) & 1u))
        var_play = (uint8_t)pk;                     /* out's bank becomes the park; in's (parked, or to load) the other */
}
/* slot s's RAM image back to its flash version (record, lock bank, variations; a park of it dropped), its marks
 * cleared. The live state is not changed (the caller loads) */
static uint8_t paste_undo;                          /* the last PASTE's target + 1, until its SAVE or a paste elsewhere */
static void cue_cancel(void);
static void project_discard(uint32_t s)
{
    s &= 3u;
    cue_cancel();
    if (paste_undo == s + 1u)
        paste_undo = 0;                             /* (its flash version back: nothing to undo) */
#if FELUCCA_FLASH
    if (flash_ok)
        proj_fetch(s);
#endif
    if (var_who[var_play ^ 1u] == s + 1u)
        var_who[var_play ^ 1u] = 0;
    if (var_who[var_play] == s + 1u)
        proj_var_load(s, &var_buf[var_play]);
    proj_dirty &= (uint8_t)~(1u << s);
    var_dirty &= (uint8_t)~(1u << s);
#if FELUCCA_FLASH
    if (!flash_ok)
#endif
        proj_fsum[s] = proj_slot[s].sum;             /* (no flash: RAM's is the version kept) */
}

#if FELUCCA_FLASH
/* slot's variations to flash: not written when its current copy holds these bytes already (a SAVE that changed none:
 * no erase, and the record's link still finds them); 0 ok */
static int var_write(uint32_t slot, const var_bank_t *b)
{
    st_hdr_t h;
    uint32_t len = 8u + b->used;
    if (st_current(OBJ_VAR0 + slot, &h) >= 0 && h.len == len && !memcmp(st_buf, b, len))
        return 0;
    return st_save(OBJ_VAR0 + slot, b, len);
}
#endif

static void project_save_go(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
    int32_t ld;
    var_bank_t *vb;
    if (transport_busy()) {                            /* (upstream 1.0) only while stopped */
        ui_message("STOP TO SAVE");
        return;
    }
    cue_cancel();                                       /* (a switch made just before the stop: filed first) */
    ld = proj_loaded();
    vb = &var_buf[var_play];
    slot &= 3u;
    if (ld < 0) {                                       /* (the power-on kit: no variations) */
        var_empty(vb, slot);
        var_cur = 0;
    } else if (var_pack_live((uint32_t)ld)) {           /* the live variation first: it must fit */
        ui_message("VARIATIONS FULL");
        return;
    }
    proj_image(p, slot, 1);                             /* (stopped: the base is in the knobs) */
    if (var_who[var_play ^ 1u] == slot + 1u)
        var_who[var_play ^ 1u] = 0;                     /* (a park of this slot: superseded) */
    vb->slot = (uint8_t)slot;                           /* the live variations become this slot's */
    var_who[var_play] = (uint8_t)(slot + 1u);
    proj_seal(slot);
    chain.from = (uint8_t)(slot + 1u);
    proj_dirty |= (uint8_t)(1u << slot);                /* (until written) */
    var_dirty |= (uint8_t)(1u << slot);
    if (ld >= 0 && (uint32_t)ld != slot)                /* "save as": the old slot is its flash version again (its
                                                         * edits are in this one now) */
        project_discard((uint32_t)ld);
    live_sum0 = live_sum();
#if FELUCCA_FLASH
    if (flash_ok) {                                     /* the variations, the bank, then the record (the commit) */
        lk_dirty |= (uint8_t)(1u << slot);              /* (until all are written) */
        if (var_blocked) {                              /* USR2 not shortened: no variation write (RAM keeps it) */
            ui_message("USR2 ERROR: NO SAVE");   /* (this boot: USR2's clamp failed, see the power-on note) */
            return;
        }
        if (lk_blocked) {                               /* USR3 not shortened: no bank write (RAM keeps the save) */
            ui_message("USR3 CLAMP FAILED");
            return;
        }
        if ((p->var_len && var_write(slot, vb)) ||
            st_save(OBJ_LOCKS0 + slot, &lk_slot[slot], sizeof lk_slot[0]) ||
            st_save(OBJ_PROJECT0 + slot, p, sizeof *p)) {
            ui_message("SAVE ERROR");
            return;
        }
        lk_dirty &= (uint8_t)~(1u << slot);
        var_dirty &= (uint8_t)~(1u << slot);
        proj_dirty &= (uint8_t)~(1u << slot);
        proj_fsum[slot] = p->sum;
        if (paste_undo == slot + 1u)
            paste_undo = 0;                             /* (saved: a PASTE on it pastes again) */
        lastp.set = 1;                                  /* the last project: power-on loads it (spec 2026-10-11 §6) */
        lastp.slot = (uint8_t)slot;
        settings_save();                                /* (stopped: now; unchanged: nothing written) */
        ui_message("SAVED");
        return;
    }
#endif
    var_dirty &= (uint8_t)~(1u << slot);
    proj_dirty &= (uint8_t)~(1u << slot);
    proj_fsum[slot] = p->sum;                           /* (no flash: RAM's is the version kept) */
    ui_message("SAVED (RAM)");
}

/* SAVE: onto a slot that is not the loaded one and has unsaved edits (a PASTE, edits kept when it was left), it asks
 * OVERWRITE B*? first (ui_input.c: YES saves) */
static void project_save(uint32_t slot)
{
    int32_t ld = proj_loaded();
    if (!transport_busy() && (int32_t)(slot & 3u) != ld && slot_dirty(slot & 3u)) {
        ui.confirm = CONFIRM_OVERWRITE;
        ui.confirm_trk = (uint8_t)(slot & 3u);
        ui.force = 1;
        return;
    }
    project_save_go(slot);
}

/* a stored preset reference (decoded: presets.c preset_rec_dec) of a track with model m: none, or a factory preset of
 * another voice (a uid retouched by another firmware), is none; a user slot's is kept as it is (its slot may hold
 * another voice by now: shown as none, presets.c preset_of) */
static uint32_t preset_ref(uint32_t id, uint32_t m)
{
    if (id >= PRE_NID || (id < PRE_NFACT && PRE_FACTORY[id].model != m))
        return PRE_NONE;
    return id;
}

/* steps st into d, values in their ranges (garbage cannot index a table or divide by zero) */
static void proj_steps_to(step_t *d, const step_t *st)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        const step_t *q = &st[i];
        step_t *t = &d[i];
        memset(t, 0, sizeof *t);                        /* (its spare bits too: d may be a scratch area) */
        t->on = q->on ? 1u : 0u;
        t->acc = q->acc ? 1u : 0u;
        t->cond = (uint8_t)(q->cond < COND_MAX ? q->cond : COND_MAX);
        t->rat = (uint8_t)(q->rat < 3u ? q->rat : 3u);
        t->slide = q->slide;
        t->tie = q->tie;
        t->nt = (int8_t)clamp(q->nt, NT_MIN, NT_MAX);
        t->rsv = 0;
    }
}
/* every track's sound, presets, model and steps (variation 1) from record p, values back inside their ranges, the
 * caps (RESON on 4 tracks, ACID 2, CHORD 2, MODAL 2). cue_in 1 (a PASTE onto the loaded slot): the mutes kept, an LFO's
 * knobs settled first (lfo.c: the block's restore would put the old ones back) */
static __attribute__((noinline)) void proj_tracks_in(const project_t *p, int cue_in)
{
    uint32_t i, k;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t m = (uint32_t)clamp(s->p[P_MODEL], 0, NMODELS - 1);
        int16_t mute = t->p[P_MUTE];
        if (cue_in && song.lfo_in)
            lfo_restore_track(t);
        drum_cut(t);
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = i >= P_E0 && i <= P_E7 ? &DMODELS[m].edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(i < P_NSTORED ? s->p[i] : proj_xp_get(s, i), d->min, d->max);
        }
        if (cue_in)
            t->p[P_MUTE] = mute;                        /* (a cue: the mutes stay the performance's) */
        t->p[P_MODEL] = (int16_t)m;
        t->model = (uint8_t)m;
        t->pre = (uint16_t)preset_ref(preset_rec_dec(p->pre[k]), m);
        t->pre_ed = p->pre_ed[k] ? 1u : 0u;
        t->key_nt = 0;                                  /* no last KEYS note carried over (spec §2) */
        proj_steps_to(t->step, s->step);
    }
    for (k = 0, i = 0; k < NTRK; k++)                   /* RESON on 4 tracks at most: the first four keep it */
        if (trk[k].p[P_RMODEL] != RS_OFF && ++i > RS_MAXTRK)
            trk[k].p[P_RMODEL] = RS_OFF;
    for (k = 0, i = 0; k < NTRK; k++)                   /* ACID on 2 tracks at most: the first two keep it, the
                                                         * others get their power-on kit model (with its sound) */
        if ((uint32_t)trk[k].p[P_MODEL] == DM_ACID && ++i > ACID_MAXTRK) {
            int16_t note = trk[k].p[P_NOTE];
            drum_set_model(&trk[k], KIT_DEF[k][0]);
            trk[k].p[P_NOTE] = note;
        }
    for (k = 0, i = 0; k < NTRK; k++)                   /* CHORD on 2 of them at most: the first two keep it */
        if (trk[k].p[P_RMODEL] == RS_CHORD && ++i > 2u)
            trk[k].p[P_RMODEL] = RS_STRNG;
    for (k = 0, i = 0; k < NTRK; k++)                   /* MODAL on 2 of them at most: the first two keep it */
        if (trk[k].p[P_RMODEL] == RS_MODAL && ++i > RS_MAXMODAL)
            trk[k].p[P_RMODEL] = RS_STRNG;
}
/* slot -> the live project; 1 loaded. irq: the main loop's load (the audio ISR runs: off while copying); 0 the
 * power-on one (drum_boot_init: before audio_init and fm1_irq_enable_all, so no fm1_irq_on; no EMPTY SLOT message) */
static int proj_load(uint32_t slot, int irq)
{
    project_t *p = &proj_slot[slot & 3u];
    uint32_t i;
    if (chain_busy()) {                                 /* a song reads the slots */
        ui_message("STOP TO LOAD");
        return 0;
    }
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p))
        proj_fetch(slot);
#endif
    if (!proj_ok(p)) {
        if (irq)
            ui_message("EMPTY SLOT");
        return 0;
    }
    transport_req = 2;
    if (irq)
        fm1_irq_off();                                  /* the audio ISR must not see half a project */
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE)
            song.g[i] = (int16_t)(i == G_CLOCK && (p->g[i] < 0 || p->g[i] > GP[i].max)
                                      ? 0 : clamp(p->g[i], GP[i].min, GP[i].max));   /* CLK BLE without BLE: INT */
    proj_tracks_in(p, 0);
    if (lk_valid(&lk_slot[slot & 3u], slot & 3u))       /* the motion and the locks: a broken bank is dropped, the
                                                         * rest loads */
        mo.b = lk_slot[slot & 3u];
    else
        memset(&mo.b, 0, sizeof mo.b);
    motion_forget();                                    /* the new knobs are the base: the stop puts nothing back */
    if (chain_valid(&p->song))                          /* the song and the name: broken ones dropped */
        chain.cfg = p->song;
    else
        memset(&chain.cfg, 0, sizeof chain.cfg);
    proj_name_get(chain.name, p->name);
    chain.from = (uint8_t)((slot & 3u) + 1u);
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    cue.q_slot = cue.q_var = cue.now = 0;               /* (a cue queued: dropped) */
    cue.last[slot & 3u] = 0;                            /* (LOAD plays variation 1) */
    if (irq)
        fm1_irq_on();
    if (var_who[var_play ^ 1u] == (slot & 3u) + 1u)
        var_who[var_play ^ 1u] = 0;                     /* (a park of this slot: the load is flash's) */
    proj_var_load(slot, &var_buf[var_play]);            /* its variations (the ISR plays variation 1: not read) */
    var_who[var_play] = (uint8_t)((slot & 3u) + 1u);
    var_dirty &= (uint8_t)~(1u << (slot & 3u));
    var_cur = 0;                                        /* (LOAD plays variation 1) */
    live_sum0 = live_sum();
    ui.force = 1;
    ui_message(irq && ((lk_lost >> (slot & 3u)) & 1u)    ? "LOCKS LOST"
               : irq && ((var_lost >> (slot & 3u)) & 1u) ? "VARIATIONS LOST"
                                                         : "LOADED");   /* (power-on: the boot's note) */
    if (irq) {
        lk_lost &= (uint8_t)~(1u << (slot & 3u));
        var_lost &= (uint8_t)~(1u << (slot & 3u));
    }
    return 1;
}
/* LOAD (main loop): another slot loaded writes its live edits back first (its unsaved variations to the park: SAVE C
 * FIRST when the park holds another slot's unsaved ones; VARIATIONS FULL when the live one does not fit), so nothing
 * is lost; then slot as flash has it (proj_load) */
static void proj_load_from(uint32_t slot)
{
    int32_t ld;
    slot &= 3u;
    if (chain_busy()) {                                 /* a song reads the slots */
        ui_message("STOP TO LOAD");
        return;
    }
    cue_cancel();                                       /* a queued cue dropped first (no switch under the LOAD) */
    ld = proj_loaded();
    if (ld >= 0 && (uint32_t)ld != slot && proj_ok(&proj_slot[slot])) {
        int r = proj_writeback_g(1);                    /* (its globals too: the LOAD replaces the live ones) */
        fm1_irq_off();
        if (!r)
            r = var_room((uint32_t)ld, slot);
        if (!r)
            var_leave((uint32_t)ld, slot);
        fm1_irq_on();
        if (r < 0) {
            ui_message("VARIATIONS FULL");
            return;
        }
        if (r > 0) {
            char m[] = "SAVE A FIRST";
            m[5] = (char)('A' + r - 1);
            ui_message(m);
            return;
        }
    }
    (void)proj_load(slot, 1);
}
/* LOAD of a slot with unsaved edits asks DISCARD EDITS? (ui_input.c: YES discards them, then loads) */
static void project_load(uint32_t slot)
{
    if (!chain_busy() && (slot_dirty(slot) || ((int32_t)(slot & 3u) == proj_loaded() && live_globals_edited()))) {
        ui.confirm = CONFIRM_DISCARD;
        ui.confirm_trk = (uint8_t)(slot & 3u);
        ui.force = 1;
        return;
    }
    proj_load_from(slot);
}

/* ---- the live cue (cue.c: the ISR's part; spec 2026-10-26) */
/* A queued switch is staged by the main loop (cue_stage, when it is queued; checked every frame while it waits): the
 * incoming steps, timing and (a slot) sounds, clamped, in a staging area — storage.c's st_buf: no flash is read or
 * written through it while the transport runs —, the incoming slot's variations in the bank that is not playing. At
 * the boundary the ISR only swaps the live arrays with the staged ones (the outgoing state lands in the staging area)
 * and the lock bank with the slot's: memory moves, no packing, no hashing (proj_cue_apply). cue_poll then files the
 * outgoing state in the main loop (cue_file_out: its packs, its image). */
typedef struct {
    step_t st[NTRK][NSTEP];
    int16_t tm[NTRK][4];
    int16_t p[NTRK][P_COUNT];
} cue_stage_t;
#if FELUCCA_FLASH
typedef char cue_stage_fits[sizeof(cue_stage_t) <= sizeof st_buf ? 1 : -1];
static cue_stage_t *cue_stg(void) { return (cue_stage_t *)(void *)st_buf; }
#else
static cue_stage_t cue_stage_ram;                   /* (no flash: the host's own) */
static cue_stage_t *cue_stg(void) { return &cue_stage_ram; }
#endif
enum { STG_NONE, STG_VAR, STG_COPY, STG_SLOT };     /* cue.stg / cue.out: a variation's steps, a copy of the live one,
                                                     * a slot */
#define CUE_MARGIN 192u                             /* bytes the outgoing variation may grow between two frames' checks */

static int var_unused(const var_bank_t *b, uint32_t v)   /* variation v + 1 never used (or unreadable) */
{
    uint32_t sz;
    return var_find(b, v, &sz) < 0 || sz == 2u;
}
static uint32_t var_entry(const var_bank_t *b, uint32_t v)   /* its bytes in b (2: unused) */
{
    uint32_t sz;
    return v && var_find(b, v, &sz) >= 0 ? sz : 2u;
}
/* the live variation can be filed into bank b after the switch (and copied into entry copy, 0 none), CUE_MARGIN to
 * spare */
static int cue_out_fits(const var_bank_t *b, uint32_t copy)
{
    uint32_t k, need, used = b->used;
    if (!var_cur && !copy)
        return 1;
    for (k = 0; k < NTRK; k++)
        var_sp[k] = trk[k].step;
    need = var_need(var_sp);
    if (var_cur)
        used = used - var_entry(b, var_cur) + need;
    if (copy)
        used = used - var_entry(b, copy) + need;
    return used <= VAR_CAP && used + CUE_MARGIN <= VAR_CAP;
}
/* record p's tracks' knobs, in their ranges, the caps applied (as proj_tracks_in leaves them) -> o; presets -> pre */
static void cue_stage_knobs(const project_t *p, int16_t o[NTRK][P_COUNT], uint16_t *pre, uint8_t *pre_ed)
{
    uint32_t i, k, nr = 0, na = 0, nc = 0, nm = 0;
    for (k = 0; k < NTRK; k++) {
        const proj_trk_t *s = &p->t[k];
        uint32_t m = (uint32_t)clamp(s->p[P_MODEL], 0, NMODELS - 1);
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = i >= P_E0 && i <= P_E7 ? &DMODELS[m].edit[i - P_E0] : &TP[i];
            o[k][i] = (int16_t)clamp(i < P_NSTORED ? s->p[i] : proj_xp_get(s, i), d->min, d->max);
        }
        o[k][P_MODEL] = (int16_t)m;
        pre[k] = (uint16_t)preset_ref(preset_rec_dec(p->pre[k]), m);
        pre_ed[k] = p->pre_ed[k] ? 1u : 0u;
        if (o[k][P_RMODEL] != RS_OFF && ++nr > RS_MAXTRK)
            o[k][P_RMODEL] = RS_OFF;
        if (m == DM_ACID && ++na > ACID_MAXTRK) {     /* (as drum_set_model without a preset: its edit defaults) */
            m = KIT_DEF[k][0];
            o[k][P_MODEL] = (int16_t)m;
            for (i = 0; i < 8u; i++)
                o[k][P_E0 + i] = DMODELS[m].edit[i].def;
            pre[k] = (uint16_t)PRE_NONE;
            pre_ed[k] = 0;
        }
        if (o[k][P_RMODEL] == RS_CHORD && ++nc > 2u)
            o[k][P_RMODEL] = RS_STRNG;
        if (o[k][P_RMODEL] == RS_MODAL && ++nm > RS_MAXMODAL)
            o[k][P_RMODEL] = RS_STRNG;
    }
}
/* the park would hold another slot's unsaved variations (the incoming slot's go to the bank not playing): 1 + c */
static int cue_park_taken(uint32_t in)
{
    uint32_t c = var_who[var_play ^ 1u];
    return c && c != in + 1u && ((var_dirty >> (c - 1u)) & 1u) ? (int)c : 0;
}
/* main loop: switch target slot s, variation v staged (cue.stg); 0, or CUE_E_*: nothing staged */
static int cue_stage(uint32_t s, uint32_t v)
{
    int32_t ld = proj_loaded();
    cue_stage_t *g = cue_stg();
    uint32_t k, j, pk = var_play ^ 1u;
    cue.stg = STG_NONE;
    if (ld < 0)
        return CUE_E_NOSLOT;
    if (s == (uint32_t)ld) {                          /* a variation switch */
        const var_bank_t *b = &var_buf[var_play];
        if (v == var_cur)
            return CUE_E_SAME;
        if (v && var_unused(b, v)) {                  /* never used: a copy of the live one */
            if (!cue_out_fits(b, v))
                return CUE_E_FULL;
            cue.stg = STG_COPY;
        } else {
            if (!cue_out_fits(b, 0))
                return CUE_E_FULL;
            if (v) {
                for (k = 0; k < NTRK; k++)
                    var_lp[k] = g->st[k];
                (void)var_get(b, v, var_lp, g->tm);
            } else {                                  /* variation 1: the record's */
                for (k = 0; k < NTRK; k++) {
                    proj_steps_to(g->st[k], proj_slot[s].t[k].step);
                    for (j = 0; j < 4u; j++) {
                        uint32_t id = CHAIN_TIMING[j];
                        g->tm[k][j] = (int16_t)clamp(proj_slot[s].t[k].p[id], TP[id].min, TP[id].max);
                    }
                }
            }
            cue.stg = STG_VAR;
        }
    } else {                                          /* a slot switch */
        var_bank_t *nb = &var_buf[pk];
        uint32_t w = v;
        int r = cue_park_taken(s);
        if (r)
            return CUE_E_PARK + r - 1;
        if (!cue_out_fits(&var_buf[var_play], 0))
            return CUE_E_FULL;
        if (var_who[pk] != s + 1u) {                  /* its variations into the bank not playing */
            const var_bank_t *f = proj_var_flash(s);
            var_who[pk] = 0;
            var_empty(nb, s);
            if (f)
                memcpy(nb, f, 8u + f->used);
            var_who[pk] = (uint8_t)(s + 1u);
        }
        if (!lk_valid(&lk_slot[s], s))
            lk_empty(s);
        cue_stage_knobs(&proj_slot[s], g->p, cue.stg_pre, cue.stg_pre_ed);
        if (v && var_unused(nb, v)) {                 /* never used: a copy of its last-played one (or variation 1) */
            uint32_t need;
            w = cue.last[s] != v && cue.last[s] && !var_unused(nb, cue.last[s]) ? cue.last[s] : 0u;
            for (k = 0; k < NTRK; k++)
                var_sp[k] = proj_slot[s].t[k].step;
            need = w ? var_entry(nb, w) : var_need(var_sp);
            if (nb->used > VAR_CAP || nb->used - 2u + need > VAR_CAP)
                return CUE_E_FULL;
        }
        if (w) {
            for (k = 0; k < NTRK; k++)
                var_lp[k] = g->st[k];
            (void)var_get(nb, w, var_lp, g->tm);
            for (k = 0; k < NTRK; k++)
                for (j = 0; j < 4u; j++)
                    g->p[k][CHAIN_TIMING[j]] = g->tm[k][j];
        } else {
            for (k = 0; k < NTRK; k++)
                proj_steps_to(g->st[k], proj_slot[s].t[k].step);
        }
        cue.stg = STG_SLOT;
    }
    cue.stg_slot = (uint8_t)s;
    cue.stg_var = (uint8_t)v;
    return 0;
}
static __attribute__((noinline)) void mem_swap(void *a, void *b, uint32_t n)   /* (words when both are aligned) */
{
    if (!(((uintptr_t)a | (uintptr_t)b | n) & 3u)) {
        uint32_t *x = a, *y = b, t;
        for (n /= 4u; n; n--, x++, y++) {
            t = *x;
            *x = *y;
            *y = t;
        }
    } else if (!(((uintptr_t)a | (uintptr_t)b | n) & 1u)) {
        uint16_t *x = a, *y = b, t;
        for (n /= 2u; n; n--, x++, y++) {
            t = *x;
            *x = *y;
            *y = t;
        }
    } else {
        uint8_t *x = a, *y = b, t;
        for (; n; n--, x++, y++) {
            t = *x;
            *x = *y;
            *y = t;
        }
    }
}
/* the ISR at the boundary (or the main loop, stopped, the IRQ off): the staged state in, the outgoing one into the
 * staging area (cue_poll files it): swaps only. A slot: the knobs (mutes kept) and the steps, the lock bank with the
 * slot's, the presets; its variations' bank (staged in the one not playing) plays */
static __attribute__((noinline)) int proj_cue_apply(void)
{
    cue_stage_t *g = cue_stg();
    int32_t ld = proj_loaded();
    uint32_t k, j, s = cue.stg_slot & 3u;
    if (!cue.stg || ld < 0 || (cue.q_slot ? cue.q_slot - 1u : (uint32_t)ld) != s ||
        (cue.q_var && cue.q_var - 1u != cue.stg_var))
        return CUE_E_SAME;                            /* (nothing staged for it: dropped) */
    cue.out = cue.stg;
    cue.out_var = var_cur;
    if (cue.stg == STG_VAR) {
        for (k = 0; k < NTRK; k++) {
            track_t *t = &trk[k];
            if (song.lfo_in)
                lfo_restore_track(t);
            mem_swap(t->step, g->st[k], sizeof t->step);
            for (j = 0; j < 4u; j++) {
                int16_t x = t->p[CHAIN_TIMING[j]];
                t->p[CHAIN_TIMING[j]] = g->tm[k][j];
                g->tm[k][j] = x;
            }
        }
        cue.done |= CUE_SW_VAR;
    } else if (cue.stg == STG_SLOT) {
        cue.from = (uint8_t)ld;
        for (k = 0; k < NTRK; k++) {
            track_t *t = &trk[k];
            int16_t mute;
            if (song.lfo_in)
                lfo_restore_track(t);
            lock_release(k);                          /* the knobs as set: the outgoing image's (motion.c) */
            motion_restore(k);
            cue.out_pre[k] = t->pre;
            cue.out_pre_ed[k] = t->pre_ed;
            mute = t->p[P_MUTE];
            drum_cut(t);
            mem_swap(t->p, g->p[k], sizeof g->p[k]);
            t->p[P_MUTE] = mute;                      /* (the mutes are the performance's) */
            t->model = (uint8_t)t->p[P_MODEL];
            t->pre = cue.stg_pre[k];
            t->pre_ed = cue.stg_pre_ed[k];
            t->key_nt = 0;
            mem_swap(t->step, g->st[k], sizeof t->step);
        }
        mem_swap(&mo.b, &lk_slot[s], sizeof mo.b);    /* (lk_slot[s] holds the outgoing bank until cue_poll) */
        motion_forget();
        if (song.playing) {                           /* the new knobs are the base: the stop puts them back */
            for (k = 0; k < NTRK; k++)
                memcpy(mo.base[k], trk[k].p, sizeof mo.base[k]);
            mo.base_ok = (uint8_t)((1u << NTRK) - 1u);
        }
        var_play ^= 1u;                               /* its variations (staged there) play; the old ones: the park */
        cue.done |= CUE_SW_SLOT;
    } else {
        cue.done |= CUE_SW_VAR;                       /* a copy: the live steps are the new variation's */
    }
    var_cur = (uint8_t)cue.stg_var;
    cue.last[s] = (uint8_t)cue.stg_var;
    cue.stg = STG_NONE;
    return 0;
}
/* main loop, after a switch: the outgoing state (in the staging area) filed — the variation it played packed into its
 * bank (variation 1: into its record), a slot's knobs, steps, presets, lock bank, song and name into its record */
static void cue_file_out(void)
{
    cue_stage_t *g = cue_stg();
    int32_t ld = proj_loaded();
    uint32_t k, j, i, ov = cue.out_var, out = cue.out;
    cue.out = STG_NONE;
    if (ld < 0 || out == STG_NONE)
        return;
    if (out == STG_SLOT) {
        uint32_t a = cue.from & 3u;
        project_t *p = &proj_slot[a];
        var_bank_t *ab = &var_buf[var_play ^ 1u];
        for (k = 0; k < NTRK; k++) {
            for (i = 0; i < P_NSTORED; i++) {
                int timing = 0;
                for (j = 0; j < 4u; j++)
                    timing |= i == CHAIN_TIMING[j];
                if (i != P_MUTE && !(timing && ov))   /* (another variation's timing: its, not the record's) */
                    p->t[k].p[i] = g->p[k][i];
            }
            if (!ov)
                memcpy(p->t[k].step, g->st[k], sizeof p->t[k].step);
            for (i = P_NSTORED; i < P_COUNT; i++)      /* KEYF: the steps' spare bytes */
                p->t[k].step[i - P_NSTORED].rsv = (uint8_t)clamp(g->p[k][i], 0, 255);
            p->pre[k] = (uint16_t)preset_rec_enc(cue.out_pre[k]);
            p->pre_ed[k] = cue.out_pre_ed[k];
            var_sp[k] = g->st[k];
            for (j = 0; j < 4u; j++)
                var_tm[k][j] = g->p[k][CHAIN_TIMING[j]];
        }
        if (ov && var_who[var_play ^ 1u] == a + 1u) {
            int r = var_put(ab, ov, var_sp, var_tm);
            if (r == 0)
                var_dirty |= (uint8_t)(1u << a);
            else if (r < 0)
                ui_message("VARIATIONS FULL");
        }
        lk_slot[a] = lk_slot[ld];                     /* (its bank, swapped out at the switch) */
        p->song = chain.cfg;
        memcpy(p->name, chain.name, NAME_LEN);
        proj_seal(a);
        if (chain_valid(&proj_slot[ld].song))         /* the new slot's song and name */
            chain.cfg = proj_slot[ld].song;
        else
            memset(&chain.cfg, 0, sizeof chain.cfg);
        proj_name_get(chain.name, proj_slot[ld].name);
        chain.from = (uint8_t)(ld + 1);
    } else if (out == STG_VAR || out == STG_COPY) {
        project_t *p = &proj_slot[ld];
        for (k = 0; k < NTRK; k++) {
            var_sp[k] = out == STG_VAR ? g->st[k] : trk[k].step;
            for (j = 0; j < 4u; j++)
                var_tm[k][j] = out == STG_VAR ? g->tm[k][j] : trk[k].p[CHAIN_TIMING[j]];
        }
        if (!ov) {                                    /* variation 1 lives in the record */
            for (k = 0; k < NTRK; k++) {
                memcpy(p->t[k].step, var_sp[k], sizeof p->t[k].step);
                for (i = P_NSTORED; i < P_COUNT; i++)
                    p->t[k].step[i - P_NSTORED].rsv = (uint8_t)clamp(motion_rest(k, i), 0, 255);
                for (j = 0; j < 4u; j++)
                    p->t[k].p[CHAIN_TIMING[j]] = var_tm[k][j];
            }
        } else {
            int r = var_put(&var_buf[var_play], ov, var_sp, var_tm);
            if (r == 0)
                var_dirty |= (uint8_t)(1u << ld);
            else if (r < 0)
                ui_message("VARIATIONS FULL");
        }
    }
}
/* main loop: a queued cue dropped (with the IRQ off: the ISR switches no more), a switch it made filed first */
static void cue_cancel(void)
{
    fm1_irq_off();
    cue.q_slot = cue.q_var = cue.now = 0;
    fm1_irq_on();
    cue_poll();
    cue.stg = STG_NONE;
}
static void cue_say(int e)                          /* a refusal's message */
{
    char m[] = "SAVE A FIRST";
    if (e >= CUE_E_PARK) {
        m[5] = (char)('A' + e - CUE_E_PARK);
        ui_message(m);
    } else if (e == CUE_E_FULL) {
        ui_message("VARIATIONS FULL");
    } else if (e == CUE_E_SONG) {
        ui_message("STOP SONG TO CUE");
    } else if (e == CUE_E_NOSLOT) {
        ui_message("SAVE TO A SLOT FIRST");
    }
}
/* main loop, first in ui_input: a switch the ISR made, filed (the outgoing state into its slot, the new slot's
 * image in step with the live state); while a cue waits, the live variation must still fit its bank (else the cue is
 * dropped: VARIATIONS FULL); a refusal said once */
static void cue_poll(void)
{
    uint8_t d = cue.done, e = cue.err;
    if (d) {
        cue.done = 0;
        cue_file_out();
        (void)proj_writeback();                     /* (the new variation packs to the same bytes, a copy is stored) */
        live_sum0 = live_sum();
        ui.force = 1;
    }
    if ((cue.q_slot || cue.q_var) && cue.stg) {
        int32_t ld = proj_loaded();
        if (ld >= 0 && !cue_out_fits(&var_buf[var_play], cue.stg == STG_COPY ? cue.stg_var : 0u)) {
            fm1_irq_off();
            cue.q_slot = cue.q_var = cue.now = 0;
            fm1_irq_on();
            cue.stg = STG_NONE;
            e = CUE_E_FULL;
        }
    }
    if (e) {
        cue.err = 0;
        cue_say(e);
    }
}
/* the variations of slot s used (bit v: variation v + 1; variation 1 always): its RAM bank, else flash's (that one
 * remembered by the record's link: flash changes only with a SAVE, which links anew) */
static struct { uint32_t link[4]; uint8_t mask[4]; } var_fmask;
static uint32_t slot_var_mask(uint32_t s)
{
    const var_bank_t *b = var_who[var_play] == s + 1u ? &var_buf[var_play]
                        : var_who[var_play ^ 1u] == s + 1u ? &var_buf[var_play ^ 1u] : 0;
    uint32_t key;
    if (b)
        return 1u | var_mask(b);
    key = (proj_slot[s & 3u].var_link ^ proj_slot[s & 3u].var_len) | 1u;
    if (var_fmask.link[s & 3u] != key) {
        b = proj_var_flash(s);
        var_fmask.mask[s & 3u] = (uint8_t)(b ? var_mask(b) : 0u);
        var_fmask.link[s & 3u] = key;
    }
    return 1u | var_fmask.mask[s & 3u];
}
/* the cue map's key n (1..12, ui_leds: 1..4 white keys 1..4, 5..12 the black keys F#3 .. A#4): slots A..D lit used,
 * winking playing, blinking queued, breathing with unsaved edits; variations 1..8 of the playing slot (of the queued
 * one, if one is) lit used, winking playing, blinking queued */
static struct { uint8_t mask[4], dirty, used; } cue_map;   /* the map's per-slot state (cue_map_refresh) */
static void cue_map_refresh(void)                   /* ui_leds, once a frame while the map shows */
{
    uint32_t s;
    cue_map.dirty = cue_map.used = 0;
    for (s = 0; s < 4u; s++) {
        int on = proj_slot[s].magic == PROJ_MAGIC;
        cue_map.used |= (uint8_t)(on << s);
        cue_map.dirty |= (uint8_t)(slot_dirty(s) << s);
        cue_map.mask[s] = (uint8_t)(on ? slot_var_mask(s) : 1u);
    }
}
static int cue_led(uint32_t n)
{
    int32_t ld = proj_loaded();
    if (n >= 1u && n <= 4u) {
        uint32_t s = n - 1u;
        if (!((cue_map.used >> s) & 1u))
            return CM_OFF;
        if (cue.q_slot == s + 1u)
            return CM_BLINK;
        if ((cue_map.dirty >> s) & 1u)
            return CM_BREATHE;
        return (int32_t)s == ld ? CM_WINK : CM_ON;
    }
    if (n >= 5u && n <= 12u && (cue.q_slot || ld >= 0)) {
        uint32_t v = n - 5u, s = cue.q_slot ? cue.q_slot - 1u : (uint32_t)ld;
        if (cue.q_var == v + 1u)
            return CM_BLINK;
        if (!cue.q_slot && v == var_cur)
            return CM_WINK;
        return (cue_map.mask[s] >> v) & 1u ? CM_ON : CM_OFF;
    }
    return CM_OFF;
}
/* ---- COPY / PASTE of a slot (spec 2026-10-26; the COPY page) */
/* the live state from slot s's image, the globals kept (a PASTE onto the loaded slot or its undo; stopped) */
static void proj_live_from(uint32_t s)
{
    fm1_irq_off();
    proj_tracks_in(&proj_slot[s], 1);
    if (lk_valid(&lk_slot[s], s))
        mo.b = lk_slot[s];
    else
        memset(&mo.b, 0, sizeof mo.b);
    motion_forget();
    var_cur = 0;
    cue.last[s] = 0;
    fm1_irq_on();
    if (chain_valid(&proj_slot[s].song))
        chain.cfg = proj_slot[s].song;
    else
        memset(&chain.cfg, 0, sizeof chain.cfg);
    proj_name_get(chain.name, proj_slot[s].name);
    live_sum0 = live_sum();
    ui.force = 1;
}
static void project_copy(uint32_t s)              /* COPY: slot s remembered (all slots are in RAM: no copy now) */
{
    char m[] = "COPIED A";
    s &= 3u;
    if (!project_used(s) && proj_loaded() != (int32_t)s) {
        ui_message("EMPTY SLOT");
        return;
    }
    copy_src = (uint8_t)(s + 1u);
    m[7] = (char)('A' + s);
    ui_message(m);
}
/* PASTE onto slot t: its image, lock bank, song and variations become the copied slot's (the loaded source's live
 * edits written back first; the name + " COPY"), in RAM only (flash at SAVE), t marked; t loaded: the live tracks too
 * (the globals kept); t not loaded: its variations in the park (SAVE C FIRST when that holds another slot's unsaved
 * ones). Again on the same target before its SAVE: undone (t's flash version back). Stopped only */
static void project_paste(uint32_t t)
{
    int32_t ld;
    cue_cancel();
    ld = proj_loaded();
    uint32_t src, pk = var_play ^ 1u, dst;
    const var_bank_t *sb;
    t &= 3u;
    if (transport_busy() || chain_busy()) {
        ui_message("STOP TO PASTE");
        return;
    }
    if (paste_undo == t + 1u) {                       /* undo */
        project_discard(t);
        if ((int32_t)t == ld)
            proj_live_from(t);
        ui_message("PASTE UNDONE");
        return;
    }
    if (!copy_src) {
        ui_message("COPY A SLOT FIRST");
        return;
    }
    src = copy_src - 1u;
    if (src == t) {
        ui_message("SAME SLOT");
        return;
    }
    if (ld == (int32_t)src && proj_writeback()) {     /* the source's live edits first */
        ui_message("VARIATIONS FULL");
        return;
    }
    if (!proj_ok(&proj_slot[src])) {
        ui_message("EMPTY SLOT");
        return;
    }
    if ((int32_t)t == ld) {
        dst = var_play;
    } else {                                          /* the park: free, clean, or t's own */
        uint32_t c = var_who[pk];
        if (c && c != t + 1u && ((var_dirty >> (c - 1u)) & 1u)) {
            char m[] = "SAVE A FIRST";
            m[5] = (char)('A' + c - 1u);
            ui_message(m);
            return;
        }
        dst = pk;
    }
    sb = var_who[var_play] == src + 1u ? &var_buf[var_play] : var_who[pk] == src + 1u ? &var_buf[pk] : proj_var_flash(src);
    proj_slot[t] = proj_slot[src];
    {                                                 /* the name: + " COPY" if it fits, "COPY" with none */
        char n[NAME_LEN + 1u];
        uint32_t l;
        proj_name_get(n, proj_slot[src].name);
        l = str_len(n);
        if (!l)
            name_set(n, "COPY");
        else if (l + 5u <= NAME_LEN)
            str_cpy(n + l, " COPY", NAME_LEN + 1u - l);
        memset(proj_slot[t].name, 0, NAME_LEN);
        memcpy(proj_slot[t].name, n, str_len(n));
    }
    lk_slot[t] = lk_slot[src];
    lk_dirty |= (uint8_t)(1u << t);
    if (&var_buf[dst] != sb) {
        var_empty(&var_buf[dst], t);
        if (sb)
            memcpy(&var_buf[dst], sb, 8u + sb->used);
    }
    var_buf[dst].slot = (uint8_t)t;
    var_who[dst] = (uint8_t)(t + 1u);
    var_dirty |= (uint8_t)(1u << t);
    proj_seal(t);
    if ((int32_t)t == ld)
        proj_live_from(t);
    paste_undo = (uint8_t)(t + 1u);
    {
        char m[] = "PASTED A TO B";
        m[7] = (char)('A' + src);
        m[12] = (char)('A' + t);
        ui_message(m);
    }
}

/* the switch at once (stopped: the main loop, the IRQ off) */
static int cue_at_once(void)
{
    int r;
    fm1_irq_off();
    r = cue_switch(0, 0);
    fm1_irq_on();
    if (r == CUE_E_SAME)
        r = 0;
    cue.err = 0;
    cue_poll();
    return r;
}
/* queue slot s + 1 (0: the playing one) / variation v + 1 (0: none named): staged first, queued after (the IRQ off);
 * stopped: at once. 0 or the refusal (said) */
static int cue_queue(uint32_t qs, uint32_t qv)
{
    int32_t ld = proj_loaded();
    uint32_t s = qs ? qs - 1u : (uint32_t)ld, v = qv ? qv - 1u : qs ? cue.last[(qs - 1u) & 3u] : var_cur;
    int r;
    fm1_irq_off();
    cue.q_slot = cue.q_var = cue.now = 0;
    fm1_irq_on();
    r = cue_stage(s, v);
    if (r == CUE_E_SAME)
        return 0;
    if (r) {
        cue_say(r);
        return r;
    }
    fm1_irq_off();
    cue.q_slot = (uint8_t)qs;
    cue.q_var = (uint8_t)qv;
    fm1_irq_on();
    if (transport_busy())
        return 0;
    r = cue_at_once();
    if (r)
        cue_say(r);
    return r;
}
/* SAVE held + a slot key (ui_input.c): queue slot s (its last-played variation), the same key again: at the next
 * step, the playing slot's key: cancel. Stopped: at once */
static int cue_slot(uint32_t s)
{
    int32_t ld = proj_loaded();
    int r = 0;
    s &= 3u;
    if (chain_busy())
        r = CUE_E_SONG;
    else if (ld < 0)
        r = CUE_E_NOSLOT;
    else if (!proj_ok(&proj_slot[s]))
        r = CUE_E_EMPTY;
    if (r) {
        if (r == CUE_E_EMPTY) {
            char m[] = "SLOT A EMPTY";
            m[5] = (char)('A' + s);
            ui_message(m);
        } else {
            cue_say(r);
        }
        return r;
    }
    if (cue.q_slot == s + 1u) {
        cue.now = 1;                                  /* again: at the next step */
        return 0;
    }
    if (s == (uint32_t)ld) {
        fm1_irq_off();
        cue.q_slot = cue.q_var = cue.now = 0;         /* the playing slot: nothing queued */
        fm1_irq_on();
        return 0;
    }
    return cue_queue(s + 1u, 0);
}
/* a variation's black key (ui_input.c CUE_BK: SAVE held, or on HOME / the SEQ pages): queue variation v (0..7) of the
 * queued slot, else of the playing one; again: at the next step; the playing variation's (nothing else queued): cancel.
 * Stopped: at once */
static int cue_var(uint32_t v)
{
    int r = chain_busy() ? CUE_E_SONG : proj_loaded() < 0 ? CUE_E_NOSLOT : 0;
    v &= 7u;
    if (r) {
        cue_say(r);
        return r;
    }
    if (cue.q_var == v + 1u) {
        cue.now = 1;
        return 0;
    }
    if (!cue.q_slot && v == var_cur) {
        fm1_irq_off();
        cue.q_var = 0;
        cue.now = 0;
        fm1_irq_on();
        return 0;
    }
    return cue_queue(cue.q_slot, v + 1u);
}

/* power-on (drum_boot_init, not in safe start): the last project saved to flash loads as a LOAD does, stopped, its
 * slot PROJECT's SLOT. Empty or unreadable: confirm 5, LOAD FAILED / START NEW PROJECT? (ui_input.c) over the power-on
 * kit */
static void project_boot(void)
{
    if (!lastp.set)
        return;
    if (proj_load(lastp.slot, 0)) {
        song.g[G_SLOT] = (int16_t)(lastp.slot + 1u);
        return;
    }
    ui.confirm = 5;
    ui.confirm_trk = lastp.slot;
    ui.force = 1;
}

/* settings + learned panel table: one flash object (format unchanged from Felucca, so a calibrated
 * panel survives the change of firmware). The flash copy wins at boot. */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;          /* palette: bits 0..7 COLOR, bit 8 the BT AUTOSTART (2026-10-12) */
    panel_t panel;
} persist_t;
#define PERSIST_MAGIC 0x50455232u                  /* "PER2" */
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif

/* MIDI LEARN's map (midi_learn.c ml_map), its own flash object OBJ_UPRESET0 (storage.c's user-preset object, unused
 * by Hortator; 0xDC000..0xDDFFF): "HLRN", version 2, ML_N (32) entries {num, trk, code, src} (code: a P_* id + 1 or
 * an action, ML_A_*). Version 1 (16 entries, CCs and parameters only, 72 B) loads into it. Anything else there (none,
 * Felucca's user presets, another version) reads as nothing learned; a broken entry is dropped (spec 2026-10-13 §5,
 * §7) */
#define ML_MAGIC 0x4E524C48u                       /* "HLRN" */
#define ML_N1 16u                                  /* version 1's entries */
typedef struct {
    uint32_t magic;
    uint16_t ver, n;
    ml_ent_t e[ML_N];
} ml_rec_t;
typedef char ml_rec_size[sizeof(ml_rec_t) == 136u && 8u + 4u * ML_N1 == 72u ? 1 : -1];
#if FELUCCA_FLASH
static ml_rec_t ml_saved;                           /* what the flash holds (a save of the same map writes nothing) */
static void ml_rec_make(ml_rec_t *r)
{
    memset(r, 0, sizeof *r);
    r->magic = ML_MAGIC;
    r->ver = 2;
    r->n = ML_N;
    memcpy(r->e, ml_map, sizeof r->e);
}
#endif
/* persist_boot (IRQs off, the map cleared): the map from flash (version 2, or 1), its broken entries dropped: a source
 * or a code this build does not know or cannot learn, a note on a parameter or a macro, a track past 8 (an action:
 * not 0), a source or a code already in an earlier entry */
static void learn_load(void)
{
#if FELUCCA_FLASH
    memset(&ml_saved, 0, sizeof ml_saved);
    {
        ml_rec_t r;
        uint32_t i, j, n = 0, len;
        int got;
        memset(&r, 0, sizeof r);
        got = flash_ok ? st_load(OBJ_UPRESET0, &r, sizeof r) : -1;
        if (got < 0 || r.magic != ML_MAGIC)
            return;
        len = (uint32_t)got;
        if (r.ver == 1u && r.n == ML_N1 && len == 8u + 4u * ML_N1) {
            for (i = 0; i < ML_N1; i++)                 /* (version 1: src was a reserved 0, CC) */
                if (r.e[i].src)
                    r.e[i].code = 0;
            r.ver = 2;                                  /* (as a version 2 of the same map: no write for it) */
            r.n = ML_N;
        } else if (r.ver != 2u || r.n != ML_N || len != sizeof r) {
            return;
        }
        ml_saved = r;
        for (i = 0; i < ML_N; i++) {
            const ml_ent_t *e = &r.e[i];
            uint32_t s = (uint32_t)(e->num | e->src << 8);
            int ok = e->code && e->src <= ML_NOTE && ml_src_ok(s) && ml_code_ok(e->trk, e->code) &&
                     (e->src != ML_NOTE || ml_btn(e->code));
            for (j = 0; ok && j < n; j++)
                ok = (uint32_t)(ml_map[j].num | ml_map[j].src << 8) != s &&
                     !(ml_map[j].code == e->code && ml_map[j].trk == e->trk);
            if (ok)
                ml_map[n++] = *e;
        }
    }
#endif
}
/* after a learn / clear (main loop): the map saved when it changed; while playing (or PLAY pending) once stopped
 * (settings_poll): a write erases a sector with the audio silenced */
static void learn_save(void)
{
    if (transport_busy()) {
        ui.ml.save = 1;
        return;
    }
    ui.ml.save = 0;
#if FELUCCA_FLASH
    {
        static ml_rec_t r;                          /* (static: 136 B off the main path's deepest stack) */
        if (!flash_ok)
            return;
        ml_rec_make(&r);
        if (!memcmp(&r, &ml_saved, sizeof r))
            return;
        if (st_save(OBJ_UPRESET0, &r, sizeof r) == 0)
            ml_saved = r;
    }
#endif
}

/* the user presets (presets.c upr): their own flash object OBJ_UPRESET0 + 1 (storage.c's second user-preset object,
 * unused before; 0xDE000..0xDFFFF; MIDI LEARN's map keeps OBJ_UPRESET0): "UPR1", 128 slots {used, model, k[8]} (1284 B).
 * Another record there (none, Felucca's presets, another version) reads as no user preset; a slot that is not used 1
 * or names no model is empty, its knobs are put inside the model's ranges. A storage.c save is two copies and a commit
 * record: a power cut keeps the last record whole */
static void upr_write(void)                         /* the bank to flash: now when stopped, else at the next STOP */
{
    if (transport_busy()) {
        upr_pending = 1;
        return;
    }
    upr_pending = 0;
#if FELUCCA_FLASH
    if (!flash_ok)
        return;
    upr.magic = UPR_MAGIC;
    if (st_save(OBJ_UPRESET0 + 1u, &upr, sizeof upr))
        ui_message("SAVE ERROR");
#endif
}
/* t's voice into user slot (0..127, over what it holds): in the list at once, in flash as upr_write. 0, -1 a bad slot.
 * The knobs as set: while MOTION plays the track, its base (motion.c), not the value motion moved a knob to. The track
 * is not changed (the caller points its reference at PRE_NFACT + slot) */
static int preset_user_save(uint32_t slot, const track_t *t)
{
    static int16_t kv[P_COUNT];                     /* (static: off the main path's deepest stack) */
    uint32_t k = (uint32_t)(t - trk), id;
    if (slot >= PRE_NUSER)
        return -1;
    for (id = 0; id < P_COUNT; id++)                /* motion's base, a lock's value under it (motion.c) */
        kv[id] = k < NTRK ? motion_rest(k, id) : t->p[id];
    upr_set(slot, (uint32_t)t->p[P_MODEL], kv);
    upr_write();
    return 0;
}
static void preset_user_delete(uint32_t slot)       /* user slot emptied (tracks loaded from it keep their knobs) */
{
    if (slot >= PRE_NUSER || upr.s[slot].used != 1u)
        return;
    memset(&upr.s[slot], 0, sizeof upr.s[slot]);
    upr_write();
}
static void preset_user_boot(void)                  /* persist_boot (after flash_ok): the bank from flash */
{
    memset(&upr, 0, sizeof upr);
    upr_pending = 0;
#if FELUCCA_FLASH
    {
        uint32_t i, k;
        if (!flash_ok || st_load(OBJ_UPRESET0 + 1u, &upr, sizeof upr) != (int)sizeof upr || upr.magic != UPR_MAGIC) {
            memset(&upr, 0, sizeof upr);
            return;
        }
        for (i = 0; i < PRE_NUSER; i++) {
            upr_slot_t *u = &upr.s[i];
            if (u->used != 1u || u->model >= NMODELS) {
                memset(u, 0, sizeof *u);
                continue;
            }
            for (k = 0; k < 8u; k++)
                u->k[k] = (int8_t)clamp(u->k[k], DMODELS[u->model].edit[k].min, DMODELS[u->model].edit[k].max);
        }
    }
#endif
}
static void preset_user_tick(void)                  /* the main loop (settings_poll): a change left for the STOP */
{
    if (upr_pending && !transport_busy())
        upr_write();
}

/* the stored lowcut word's bits 0..7 (bits 8..31: btset.bt_hi): 0..1 SPEAKER EQ, 2..4 the last project (2 set, 3..4
 * its slot), 5..6 METRO (spec 2026-10-11 §9), 7 BACKLIGHT not OFF (§12; its level: the palette word's bits 9..11,
 * fixes 2026-10-21 item 9, settings_save). An older firmware reads SPEAKER EQ FLAT from a record with any of 2..7 set */
static uint32_t lc_pack(void)
{
    return (settings.lowcut & 3u) |
           (lastp.set ? 1u << 2 | (uint32_t)(lastp.slot & 3u) << 3 : 0u) | (uint32_t)(fx_metro & 3u) << 5 |
           (uint32_t)(backlight != BL_OFF) << 7;
}
static void lc_unpack(uint32_t lc)
{
    settings.lowcut = lc & 3u;                       /* (3: settings_init makes it FLAT) */
    lastp.set = (uint8_t)((lc >> 2) & 1u);
    lastp.slot = (uint8_t)((lc >> 3) & 3u);
    fx_metro = (uint8_t)((lc >> 5) & 3u);
    backlight = (lc >> 7) & 1u;                      /* (ON: DIM LO; the level from the palette word, persist_boot) */
}

#if FELUCCA_FLASH
/* USR3 and USR2 at 48 KiB (specs 2026-10-24 parameter locks, 2026-10-26 cue / variations): an upstream Felucca upload
 * longer than that (its slot was 80 KiB) is shortened in flash once, before anything writes the lock banks (USR3's
 * last 32 KiB) or the variations (USR2's) behind it, so no Hortator (this one or an older one) plays them as sound:
 * zones starting at or past the cut are dropped (the later ones move down), one across it is cut there (its loop end
 * inside it, the loop off when its start was cut), the length and the CRC (over the kept data) new. Its header sector
 * is rewritten (NOR cannot raise bits): the payload part first, the header bytes 0..255 last (a power cut before them:
 * the slot empty, nothing else touched). k: 1 USR2, 2 USR3. 1 rewritten, 0 nothing to do, < 0 a flash error (the slot
 * then scans as unusable: its length is past 48 KiB) */
#define SMP_USR_CUT 0xBE00u                                 /* data bytes kept: 48 KiB - the header's 512 */
typedef char smp_usr_cut_ok[SMP_USR_CUT == SMP_USR2_SIZE - SMP_USER_DATA && SMP_USR_CUT == SMP_USR3_SIZE - SMP_USER_DATA
                            ? 1 : -1];
static uint8_t usr_head[256] __attribute__((aligned(4)));
static uint8_t *usr_at(uint32_t o) { return o < 256u ? &usr_head[o] : &st_buf[o - 256u]; }   /* sector byte o */
static uint32_t usr_rd(uint32_t o)
{
    const uint8_t *b = usr_at(o);
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}
static void usr_wr(uint32_t o, uint32_t v)
{
    uint8_t *b = usr_at(o);
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}
static int smp_usr_clamp(uint32_t k)
{
    uint32_t nz, i, j, o, at = SMP_USER_BASE + k * SMP_USER_SIZE;
    if (st_read(at, usr_head, sizeof usr_head) || st_read(at + 256u, st_buf, ST_PAYLOAD_MAX))
        return -1;
    nz = *usr_at(6);
    if (usr_rd(0) != SMP_USER_MAGIC || (usr_rd(4) & 0xFFFFu) != 1u || nz < 1u || nz > 16u ||
        usr_rd(16) <= SMP_USR_CUT)
        return 0;
    for (i = 0, j = 0; i < nz; i++) {
        uint32_t zi = 32u + 28u * i, zo = 32u + 28u * j, off = usr_rd(zi), n = usr_rd(zi + 4u);
        if (off >= SMP_USR_CUT)
            continue;                                   /* past the cut: dropped */
        if (zi != zo)
            for (o = 0; o < 28u; o++)
                *usr_at(zo + o) = *usr_at(zi + o);
        if (n > 2u * (SMP_USR_CUT - off)) {             /* across the cut (its last byte past it): cut there */
            uint32_t ls = usr_rd(zo + 8u), le = usr_rd(zo + 12u);
            n = 2u * (SMP_USR_CUT - off);
            usr_wr(zo + 4u, n);
            if (le > n - 1u)
                le = n - 1u;
            if (ls > le) {
                ls = le;
                *usr_at(zo + 27u) = 0;                  /* the loop's start cut: no loop */
            }
            usr_wr(zo + 8u, ls);
            usr_wr(zo + 12u, le);
        }
        j++;
    }
    *usr_at(6) = (uint8_t)j;
    usr_wr(16, SMP_USR_CUT);
    usr_wr(20, st_crc32(smp_user_xip(k) + SMP_USER_DATA, SMP_USR_CUT));   /* (the data, before the erase) */
    if (st_erase(at))
        return -2;
    for (o = 0; o < ST_PAYLOAD_MAX; o += 256u)
        if (st_prog(at + 256u + o, st_buf + o, 256u))
            return -3;
    if (st_prog(at, usr_head, sizeof usr_head))     /* the header, last */
        return -4;
    return 1;
}
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
    memset(&btset, 0, sizeof btset);                  /* no device, MODE AUTO, AUTOSTART OFF unless the flash record
                                                       * has them */
    memset(&lastp, 0, sizeof lastp);                  /* no last project, unless the flash record has one */
    fx_metro = 0;                                     /* METRO OFF, as well */
    backlight = 0;                                    /* BACKLIGHT OFF, as well */
#if USB10_BUILD
    usbaud = 0;                                       /* USB AUDIO 2 CH, as well */
#endif
    memset(ml_map, 0, sizeof ml_map);                 /* MIDI LEARN: nothing learned, as well */
    memset(&upr, 0, sizeof upr);                      /* no user preset, as well (presets.c) */
    upr_pending = 0;
#if !FELUCCA_FLASH
    {
        uint32_t i;
        for (i = 0; i < 4u; i++)
            proj_fsum[i] = proj_slot[i].sum;          /* (no flash: the RAM slots are the versions kept) */
    }
#else
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;
    irq_restore(f);
    if (!flash_ok) {
        uint32_t i;
        for (i = 0; i < 4u; i++)
            proj_fsum[i] = proj_slot[i].sum;          /* (no flash: the RAM slots are the versions kept) */
        return;
    }
    fl_plain_window_init();
    lk_dirty = 0;
    lk_blocked = 0;
    var_blocked = 0;
    {
        int c = smp_usr_clamp(1);                     /* USR2 at 48 KiB: before any variation write */
        if (c > 0)
            persist_note |= PN_USR2;
        else if (c < 0) {                             /* failed: USR2 still past 48 KiB in flash, no variation write now */
            var_blocked = 1;
            persist_note |= PN_USR2F;
        }
        c = smp_usr_clamp(2);                         /* USR3 at 48 KiB: before any lock-bank write */
        if (c > 0)
            persist_note |= PN_USR3;
        else if (c < 0) {                             /* failed: USR3 still past 48 KiB in flash, no bank write now */
            lk_blocked = 1;
            persist_note |= PN_USR3F;
        }
    }
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    if (st_load(OBJ_SETTINGS, &p, sizeof p) == (int)sizeof p && p.magic == PERSIST_MAGIC) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = p.palette & 0xFFu;       /* bits 0..7 the palette; bit 8 AUTOSTART (BLE: bt_page.c) */
        btset.bt_auto = (p.palette >> 8) & 1u;
#if USB10_BUILD
        usbaud = (p.palette >> 12) & 3u;            /* bits 12..13 = USB AUDIO: 0 2 CH (older records), 1 10 CH MIX
                                                     * (e586073's 10 CH), 2 10 CH FX, 3 none: 2 CH (the Bluetooth
                                                     * build ignores them, and writes them clear) */
        if (usbaud >= UA_COUNT)
            usbaud = UA_2CH;
#endif
        lc_unpack(p.lowcut);
        if (((p.palette >> 9) & 7u) - 1u < BL_COUNT - 1u && backlight)   /* BACKLIGHT's level, bits 9..11 (item 9; 0:
                                                     * an older record, its ON = DIM LO) */
            backlight = (p.palette >> 9) & 7u;
        settings.zoom = p.zoom & 1u;
        settings.mutebar = (p.zoom >> 1) & 1u;     /* kept in zoom's bit 1: the flash format stays Felucca's */
        settings.accel = !((p.zoom >> 2) & 1u);    /* bit 2 = KNOB ACCEL OFF: older saves read as on */
        settings.usbfix = (p.zoom >> 3) & 1u;      /* bit 3 = USB LEVEL FIXED: older saves read MASTER */
        settings.perfpage = (p.zoom >> 4) & 1u;    /* bit 4 = PERFORM PAGE: older saves read HOLD */
        btset.btmode = (p.zoom >> 5) & 3u;         /* bits 5..6 = BT MODE, 7 its address type, 8..31 its address */
        if (btset.btmode > 2u)                     /* bytes 0..2; lowcut bits 8..31 bytes 3..5 (all 0 = none) */
            btset.btmode = 0;
        btset.bt_type = (p.zoom >> 7) & 1u;
        btset.bt_lo = p.zoom >> 8;
        btset.bt_hi = p.lowcut >> 8;
        if (p.panel.magic == PANEL_MAGIC)
            panel = p.panel;
        persist_saved = p;
    }
    learn_load();                                     /* MIDI LEARN's map (its own object) */
    preset_user_boot();                               /* the user presets (theirs) */
    {   /* the slots, their banks and variations (after the clamps): flash is the truth (spec 2026-10-26: edits live in
         * RAM until SAVE), so every slot is its flash record again, whatever .noinit kept */
        uint32_t i;
        lk_lost = 0;
        var_lost = 0;
        var_dirty = 0;
        proj_dirty = 0;
        var_cur = 0;
        var_who[0] = var_who[1] = 0;
        for (i = 0; i < 4u; i++) {
            if (proj_fetch(i) == -1)
                persist_note |= PN_LOCKS;
            if (proj_ok(&proj_slot[i]) && proj_slot[i].var_len && !proj_var_flash(i)) {
                var_lost |= (uint8_t)(1u << i);
                persist_note |= PN_VARS;
            }
        }
    }
#endif
}

static int project_used(uint32_t slot) { return proj_ok(&proj_slot[slot & 3u]); }
static int project_name(uint32_t slot, char *b)   /* slot's name -> b (NAME_LEN + 1, "" none); 0 = an empty slot */
{
    const project_t *p = &proj_slot[slot & 3u];
    name_set(b, "");
    if (!proj_ok(p))
        return 0;
    proj_name_get(b, p->name);
    return 1;
}
static void project_rename(uint32_t slot, const char *name)   /* only the name; the current one too if it is that slot */
{
    project_t *p = &proj_slot[slot & 3u];
    char n[NAME_LEN + 1u];
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return;
    }
    if (!proj_ok(p)) {
        ui_message("EMPTY SLOT");
        return;
    }
    {
        int dirty = (proj_slot[slot & 3u].sum != proj_fsum[slot & 3u]) || ((proj_dirty >> (slot & 3u)) & 1u) ||
                    ((var_dirty >> (slot & 3u)) & 1u);
        name_set(n, name);
        memcpy(p->name, n, NAME_LEN);
        p->sum = proj_sum(p);
        if (chain.from == (slot & 3u) + 1u)
            name_set(chain.name, n);
        if (dirty) {                                    /* unsaved edits: the name with them, in RAM (flash keeps its
                                                         * version whole) */
            ui_message("RENAMED (SAVE TO KEEP)");
            return;
        }
    }
#if FELUCCA_FLASH
    if (flash_ok) {
        if (lk_blocked) {                               /* (USR3 not shortened: the pair is not written) */
            ui_message("USR3 CLAMP FAILED");
            return;
        }
        /* (its variations: flash's, a slot with unsaved ones renamed in RAM above) */
        if ((lk_dirty >> (slot & 3u)) & 1u) {           /* its bank only in RAM: written first (the record links it) */
            if (st_save(OBJ_LOCKS0 + (slot & 3u), &lk_slot[slot & 3u], sizeof lk_slot[0])) {
                ui_message("SAVE ERROR");
                return;
            }
            lk_dirty &= (uint8_t)~(1u << (slot & 3u));
        }
        if (st_save(OBJ_PROJECT0 + (slot & 3u), p, sizeof *p)) {
            ui_message("SAVE ERROR");
            return;
        }
        proj_fsum[slot & 3u] = p->sum;
        ui_message("RENAMED");
        return;
    }
#endif
    proj_fsum[slot & 3u] = p->sum;
    ui_message("RENAMED (RAM)");
}
/* PLAY on SONG (main loop): the rows' slots -> chain.src, armed; the ISR starts it (seq_start). 0 armed, 1 no rows,
 * 2 busy, 3 + s slot s empty. A slot's steps are safe as stored (step_fire bounds PROB and RATCH); its timing is put
 * inside the ranges, a broken motion plays none */
static uint32_t chain_prepare(void)
{
    uint32_t i, k, j, used = 0;
    if (transport_busy() || chain_busy())
        return 2;
    if (!chain.cfg.count || !chain_valid(&chain.cfg))
        return 1;
    for (i = 0; i < chain.cfg.count; i++) {
        uint32_t s = chain.cfg.row[i].slot;
        if (!project_used(s))
            return 3u + s;
        used |= 1u << s;
    }
    fm1_irq_off();
    for (i = 0; i < 4u; i++)
        if ((used >> i) & 1u) {
            const project_t *p = &proj_slot[i];
            chain_src_t *d = &chain.src[i];
            d->m = lk_valid(&lk_slot[i], i) ? &lk_slot[i] : &LK_NONE;
            for (k = 0; k < NTRK; k++) {
                d->step[k] = p->t[k].step;
                d->model[k] = (uint8_t)p->t[k].p[P_MODEL];
                for (j = 0; j < 4u; j++) {
                    uint32_t id = CHAIN_TIMING[j];
                    d->timing[k][j] = (int16_t)clamp(p->t[k].p[id], TP[id].min, TP[id].max);
                }
            }
        }
    chain.armed = 1;
    fm1_irq_on();
    transport_req = 1;
    return 0;
}

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
    p.palette = (settings.palette & 0xFFu) | (btset.bt_auto & 1u) << 8 | (backlight & 7u) << 9;
#if USB10_BUILD
    p.palette |= (usbaud & 3u) << 12;
#endif
    /* an older Hortator reads a palette out of range from bit 8: its display settings reset; 9..11 BACKLIGHT's level
     * (item 9: an older one reads lowcut's bit 7, ON); 12 USB AUDIO 10 CH (an older one ignores it) */
    p.lowcut = lc_pack() | btset.bt_hi << 8;
    p.zoom = (settings.zoom & 1u) | (settings.mutebar & 1u) << 1 | (uint32_t)!settings.accel << 2 | (settings.usbfix & 1u) << 3 |
             (settings.perfpage & 1u) << 4 | (btset.btmode & 3u) << 5 | (btset.bt_type & 1u) << 7 | btset.bt_lo << 8;
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
    if (ui.ml.save && !transport_busy())             /* .. and MIDI LEARN's map */
        learn_save();
    preset_user_tick();                              /* .. and the user presets */
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
#endif
