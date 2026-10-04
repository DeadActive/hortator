/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* The 8 drum tracks: power-on kit, hits (voice choice, choke groups, sample layer), and the per-track
 * render fx.c mixes (track_render). A cut voice is not dropped: its last sample decays in dtail. */

static uint32_t dvage;                                   /* hit counter: voice ages, noise seeds */

/* power-on kit: model and MIDI note of each track */
static const uint8_t KIT_DEF[NTRK][2] = {
    {DM_K909, 36}, {DM_S808, 38}, {DM_C808, 39}, {DM_HATC, 42},
    {DM_HATO, 46}, {DM_TOM, 45}, {DM_RIM, 37}, {DM_CYMB, 49},
};

static const dmodel_t *trk_model(const track_t *t) { return &DMODELS[(uint32_t)t->p[P_MODEL] % NMODELS]; }

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
    for (i = 0; i < NDV; i++) {
        dv_cut(t, &t->v[i]);
        dv_cut(t, &t->lv[i]);
    }
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

/* the oldest sounding voice of any track (model or layer): *ot its track; returns the sounding weight */
static uint32_t dv_oldest(track_t **ot, dvoice_t **ov)
{
    uint32_t i, k, n = 0;
    *ot = 0;
    *ov = 0;
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < 2u * NDV; k++) {
            dvoice_t *v = k < NDV ? &trk[i].v[k] : &trk[i].lv[k - NDV];
            if (!v->active)
                continue;
            n += dv_weight(&trk[i], v);
            if (!*ov || v->age < (*ov)->age) {
                *ov = v;
                *ot = &trk[i];
            }
        }
    return n;
}

/* a voice of weight w is about to start: the oldest sounding voices stop (declick tail) until it fits the cap */
static void dv_make_room(uint32_t w)
{
    track_t *ot;
    dvoice_t *ov;
    while (dv_oldest(&ot, &ov) + w > DRUM_MAXV && ov)
        dv_cut(ot, ov);
}

/* audio overload (audio.c, > 85 % of a half): the oldest sounding voice stops */
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

/* one hit of track t at velocity vel (1..127) */
static void drum_hit(track_t *t, uint32_t vel)
{
    const dmodel_t *m = trk_model(t);
    uint32_t i, nv = m->voices < NDV ? m->voices : NDV;
    dvoice_t *v;
    if (t->p[P_MUTE])
        return;
    if (t->p[P_CHOKE])
        for (i = 0; i < NTRK; i++)
            if (&trk[i] != t && trk[i].p[P_CHOKE] == t->p[P_CHOKE])
                drum_cut(&trk[i]);
    model_follow(t);
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
            smp_start(v, zi, (uint32_t)t->p[P_LKEY], t->p[P_LTUNE]);
            v->env[0] = ENV1;
            v->k[0] = dk(t->p[P_LDEC]);
        }
    }
}

/* track t's voices and tail into out (cleared first); returns non-zero while anything sounds */
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    const dmodel_t *m = trk_model(t);
    uint32_t i, nr = 0;
    for (i = 0; i < n; i++)
        out[i] = 0;
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

/* model mi with its default sound on t */
static void drum_set_model(track_t *t, uint32_t mi)
{
    const dmodel_t *m = &DMODELS[mi % NMODELS];
    uint32_t i;
    drum_cut(t);
    t->p[P_MODEL] = (int16_t)(mi % NMODELS);
    t->model = (uint8_t)t->p[P_MODEL];
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = m->edit[i].def;
    t->p[P_CHOKE] = m->choke;
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
