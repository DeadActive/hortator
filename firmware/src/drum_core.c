/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The 8 drum tracks: power-on kit, hits (voice choice, choke groups, sample layer), and the per-track
 * render fx.c mixes (track_render). A cut voice is not dropped: its last sample decays in dtail. */

static uint32_t dvage;                                   /* hit counter: voice ages, noise seeds */
static uint8_t safe_start;                            /* SEQ held at power-on: no drum audio (drum_boot_init) */

/* power-on kit (a new project, TOOLS INIT ALL too): the TRX classic kit (owner, 2026-10-21; up to 0.22.0 K909 S808 C808
 * HATC HATO TOM RIM CYMB), each voice on its first preset (drum_set_model), the same roles and notes: kick, snare, clap,
 * closed hat, open hat, tom, rim, cymbal; no track in a choke group (CHOKE's default OFF). Model and MIDI note */
static const uint8_t KIT_DEF[NTRK][2] = {
    {DM_KTRX, 36}, {DM_SDTRX, 38}, {DM_CPTRX, 39}, {DM_CHTRX, 42},
    {DM_OHTRX, 46}, {DM_XTTRX, 45}, {DM_RSTRX, 37}, {DM_CYTRX, 49},
};

static const dmodel_t *trk_model(const track_t *t) { return &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS]; }

/* a track's swing: its own + the global one, capped at 100 (upstream #31: 200 made 1.8 / 0.2 steps); seq.c, slicer.c */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}

static void dv_cut(track_t *t, dvoice_t *v)
{
    if (v->active)
        t->dtail += v->last;
    v->active = 0;
    v->last = 0;
}

static void drum_cut(track_t *t)                         /* every voice of t, with the declick tail */
{
    uint32_t i;
    if (t->rs.ring)
        t->rs.kill = 1;                                  /* RESON: the ring fades out with the voices */
    t->rs.hold = 1;                                      /* ... and the cut voices' declick tail starts no new one */
    for (i = 0; i < NDV; i++) {
        dv_cut(t, &t->v[i]);
        dv_cut(t, &t->lv[i]);
    }
    t->gate_n = 0;                                       /* ACID: no gate left to close, nothing slides on */
    t->slide_in = 0;
}

static void model_follow(track_t *t)                     /* the model changed: the old voices stop */
{
    if (t->model != (uint8_t)t->p[P_MODEL]) {
        drum_cut(t);
        t->model = (uint8_t)t->p[P_MODEL];
    }
}

#ifndef DRUM_MAXV
#define DRUM_MAXV 8       /* voices sounding at once over all tracks, model + layer (user decision: realistic use fits the
                           * 1566 reference; the extreme case, ~2388, relies on the device's overload shedding, M1-B;
                           * a heavy model's voice counts 2 (M1-C, user decision) */
#endif

/* what a sounding voice counts toward DRUM_MAXV: a layer voice 1, a model voice its model's weight */
static uint32_t dv_weight(const track_t *t, const dvoice_t *v)
{
    uint32_t w = v >= t->lv && v < t->lv + NDV ? 1u : DMODELS[t->model % NMODELS].weight;
    return w ? w : 1u;
}

/* the voice to go first of any track (model or layer): *ot its track; returns the sounding weight. A gated model's
 * voice (ACID) with its gate open goes last: a TIE or a slide keeps the voice and its age, so a held note would be
 * the oldest and any drum hit would cut it; it is taken only when nothing else sounds. Then a LOW track's voice before
 * a HIGH one's (P_PRIO), then the oldest */
static uint32_t dv_oldest(track_t **ot, dvoice_t **ov)
{
    uint32_t i, k, n = 0, key, best = 0;
    *ot = 0;
    *ov = 0;
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < 2u * NDV; k++) {
            dvoice_t *v = k < NDV ? &trk[i].v[k] : &trk[i].lv[k - NDV];
            if (!v->active)
                continue;
            n += dv_weight(&trk[i], v);
            key = (k < NDV && DMODELS[trk[i].model % NMODELS].gate && (v->x[2] & 1)) * 2u + !trk[i].p[P_PRIO];
            if (!*ov || key < best || (key == best && v->age < (*ov)->age)) {
                *ov = v;
                *ot = &trk[i];
                best = key;
            }
        }
    return n;
}

/* a voice of weight w is about to start: sounding voices stop (declick tail; dv_oldest's order: a held ACID note last,
 * a LOW track's first, the oldest) until it fits the cap */
static void dv_make_room(uint32_t w)
{
    track_t *ot;
    dvoice_t *ov;
    while (dv_oldest(&ot, &ov) + w > DRUM_MAXV && ov)
        dv_cut(ot, ov);
}

/* audio overload (audio.c, > 95 % of a half): one voice stops (dv_oldest's order: a held ACID note last, a LOW
 * track's first, the oldest) */
static void drum_shed(void)
{
    track_t *ot;
    dvoice_t *ov;
    if (dv_oldest(&ot, &ov) && ov)
        dv_cut(ot, ov);
}

static dvoice_t *dv_alloc(track_t *t, dvoice_t *pool, uint32_t nv, uint32_t w)   /* free, else the oldest */
{
    uint32_t i;
    dvoice_t *v = &pool[0];
    for (i = 0; i < nv; i++) {
        if (!pool[i].active) {
            dv_make_room(w);
            return &pool[i];
        }
        if (pool[i].age < v->age)
            v = &pool[i];
    }
    dv_cut(t, v);
    return v;
}

static void dv_init(dvoice_t *v, uint32_t vel)
{
    memset(v, 0, sizeof *v);
    v->active = 1;
    v->vel = (uint8_t)(vel > 127u ? 127u : vel < 1u ? 1u : vel);
    v->age = ++dvage;
    v->rng = (int32_t)(dvage * 2654435761u) | 1;
}

/* silent: muted, or another track soloed and not this one (SOLO: the SMC-Mixer; P_MUTE itself unchanged) */
static int trk_silent(const track_t *t)
{
    return t->p[P_MUTE] || (song.solo && !((song.solo >> (uint32_t)(t - trk)) & 1u));
}

/* one hit of track t at velocity vel (1..127), playing note nt (an offset from C4) */
static void drum_play(track_t *t, uint32_t vel, int32_t nt)
{
    const dmodel_t *m = trk_model(t);
    uint32_t i, nv = m->voices < NDV ? m->voices : NDV;
    dvoice_t *v;
    t->nt = (int8_t)clamp(nt, NT_MIN, NT_MAX);
    if (safe_start)
        return;
    lfo_hit(t);                                     /* HIT LFOs restart, the trigger reads their values */
    if (trk_silent(t) && (uint32_t)(t - trk) != comp_ghost_src())   /* a muted COMP source still plays (GHOST KEEP /
                                                                      * HIDE, fx.c) */
        return;
    trk_filter_hit(t, vel);                             /* FILTER: the hit's sweep (filter.c) */
    if (t->p[P_CHOKE])
        for (i = 0; i < NTRK; i++)
            if (&trk[i] != t && trk[i].p[P_CHOKE] == t->p[P_CHOKE])
                drum_cut(&trk[i]);
    model_follow(t);
    t->rs.hold = 0;                                     /* RESON hears this hit (a model change's cut came first) */
    if ((uint32_t)t->p[P_MODEL] % NMODELS != DM_SMPL || smpl_playable(t)) {   /* nothing to play: no voice */
        v = dv_alloc(t, t->v, nv, m->weight ? m->weight : 1u);
        dv_init(v, vel);
        m->trigger(t, v);
    }
    if (t->p[P_LLEVEL]) {
        uint32_t zi = smp_find((uint32_t)t->p[P_LSET], (uint32_t)t->p[P_LKEY]);
        if (zi != 0xFFFFu && smp_zone(zi)->n) {
            v = dv_alloc(t, t->lv, nv, 1u);
            dv_init(v, vel);
            smp_start(v, zi, (uint32_t)t->p[P_LKEY], t->p[P_LTUNE] + clamp(t->nt, -24, 24));   /* as dm_tune */
            v->env[0] = ENV1;
            v->k[0] = dk(t->p[P_LDEC]);
        }
    }
}

static uint32_t hit_gate_len(const track_t *t);          /* seq.c */

/* MIDI, the keys, Grids: C4. A gated model's (ACID) note no step drives ends as a step's: its gate closes after
 * GATE of the track's step (seq.c seq_tick counts it, stopped too) */
static void drum_hit(track_t *t, uint32_t vel)
{
    drum_play(t, vel, 0);
    if (trk_model(t)->gate) {
        t->gate_n = hit_gate_len(t);
        t->slide_in = 0;
    }
}

/* the engine view's rendered hit (ui_draw.c vg_step), on the UI thread: t's model, one voice (a two-voice model's one
 * hit; no sample layer, no FX, no RESON, no LFO), struck on the scratch track vg_trk (dm_dsp.c) with t's knobs at C4,
 * velocity vel. Nothing live is touched: no voice from the pool, no voice cap, no choke, no age or seed from dvage (age
 * 1 and its seed, as the first hit after power-on: the same knobs, the same sound), the metal bank and SKIN's body the
 * scratch's own. A SAMPLE with no sample: no voice */
static void vg_hit(const track_t *t, uint32_t vel)
{
    dvoice_t *v = &vg_trk.v[0];
    memset(&vg_trk, 0, sizeof vg_trk);
    memcpy(vg_trk.p, t->p, sizeof vg_trk.p);
    vg_trk.model = (uint8_t)((uint32_t)t->p[P_MODEL] % NMODELS);
    memset(vg_metal_ph, 0, sizeof vg_metal_ph);
    memset(&vg_memb, 0, sizeof vg_memb);
    if (vg_trk.model == DM_SMPL && !smpl_playable(&vg_trk))
        return;
    v->active = 1;
    v->vel = (uint8_t)vel;
    v->age = 1;
    v->rng = (int32_t)2654435761u | 1;
    DMODELS[vg_trk.model].trigger(&vg_trk, v);
}

/* the next n (<= CTL) samples of the rendered hit into out (cleared first); 0 once its voice has ended */
static uint32_t vg_render(int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (!vg_trk.v[0].active)
        return 0;
    DMODELS[vg_trk.model].render(&vg_trk, &vg_trk.v[0], out, n);
    return 1;
}

/* track t's voices and tail into out (cleared first); returns non-zero while anything sounds */
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    const dmodel_t *m = trk_model(t);
    uint32_t i, nr = 0;
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (t->wow)
        t->wow = mulq16(t->wow, ACID_WOW);              /* ACID's accent smoother falls every block, sounding or not */
    model_follow(t);
    for (i = 0; i < NDV; i++)
        if (t->v[i].active) {
            m->render(t, &t->v[i], out, n);
            nr++;
        }
    for (i = 0; i < NDV; i++)
        if (t->lv[i].active) {
            smp_render(&t->lv[i], out, n, mulq15(t->p[P_LLEVEL] * 258, vel_gain(&t->lv[i])), 32767, 0);
            nr++;
        }
    if (t->dtail) {
        for (i = 0; i < n && t->dtail; i++) {
            int32_t d = t->dtail >> 4;
            out[i] += t->dtail;
            t->dtail -= d ? d : (t->dtail > 0 ? 1 : -1);
        }
        nr++;
    }
    return nr;
}

static void drum_block_begin(void) { dblock++; }

/* model mi with its default sound on t: the voice's first preset (presets.c; else the knobs' defaults), the track's
 * reference on it, not edited. CHOKE is the track's own: a MODEL change never touches it (spec 2026-10-19) */
static void drum_set_model(track_t *t, uint32_t mi)
{
    const dmodel_t *m = &DMODELS[mi % NMODELS];
    uint32_t i, id = preset_first_of(mi % NMODELS);
    preset_t pr;
    drum_cut(t);
    t->p[P_MODEL] = (int16_t)(mi % NMODELS);
    t->model = (uint8_t)t->p[P_MODEL];
    if (id != PRE_NONE && preset_get(id, &pr)) {
        preset_apply(t, &pr, id);
        return;
    }
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = m->edit[i].def;
    t->pre = (uint16_t)PRE_NONE;
    t->pre_ed = 0;
}

/* (ACID_MAXTRK: presets.c) */

static uint32_t acid_tracks(const track_t *except)   /* tracks with MODEL ACID, other than except */
{
    uint32_t i, n = 0;
    for (i = 0; i < NTRK; i++)
        n += &trk[i] != except && (uint32_t)trk[i].p[P_MODEL] % NMODELS == DM_ACID;
    return n;
}

static void drum_tracks_init(void)
{
    uint32_t i, k;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        memset(t, 0, sizeof *t);
        for (i = 0; i < P_COUNT; i++)
            if (i < P_E0 || i > P_E7)
                t->p[i] = TP[i].def;
        drum_set_model(t, KIT_DEF[k][0]);
        t->p[P_NOTE] = KIT_DEF[k][1];
        if (KIT_DEF[k][0] == DM_SMPL)
            t->p[P_E5] = KIT_DEF[k][1];             /* the GM kit's sound for that note */
    }
    song.sel = 0;
    song.master_q12 = 2048;
}
