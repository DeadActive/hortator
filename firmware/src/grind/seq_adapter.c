/* SPDX-License-Identifier: GPL-3.0-only */
/* The portable Grind core on the native sequencer's own data: step_t and track_t stay as they are, the sequencer
 * (seq.c) stays authoritative and plays what is written here. Included after core.h and the amalgam, once.
 *
 * Only the main loop calls this. gm_native_publish is the synchronous commit (stopped transport, no song); the
 * boundary-safe commit while playing is the controller's job (controller.c) and is not here. */
#ifndef GRIND_SEQ_ADAPTER_C
#define GRIND_SEQ_ADAPTER_C

typedef char gm_native_tracks_match[GM_TRACKS == NTRK ? 1 : -1];
typedef char gm_native_steps_match[GM_STEPS == NSTEP ? 1 : -1];
typedef char gm_native_step_size[sizeof(step_t) == 4u ? 1 : -1];

/* The stored DIV ids of the grids Grind plays: 1/8 = 1, 1/16 = 2, 1/32 = 3 (params.c N_DIV). */
static gm_result gm_native_division(uint8_t musical, uint8_t *id)
{
    uint8_t v = musical == 8u ? 1u : musical == 16u ? 2u : musical == 32u ? 3u : 0u;
    if (!v)
        return GM_UNSUPPORTED;
    *id = v;
    return GM_OK;
}
static gm_result gm_musical_division(uint8_t id, uint8_t *musical)
{
    if (id < 1u || id > 3u)
        return GM_UNSUPPORTED;
    *musical = (uint8_t)(4u << id);
    return GM_OK;
}

static gm_cell gm_from_native(const step_t *s)
{
    gm_cell c;
    c.flags = (uint8_t)(s->on | (s->acc << 1) | (s->slide << 2) | (s->tie << 3) | (s->rat << GM_RAT_SHIFT));
    c.cond = s->cond;
    c.note = s->nt;
    c.micro = s->micro;
    return c;
}
/* Only after gm_native_check: every field is assigned, nothing is copied as bytes (the padding bits stay zero). */
static void gm_to_native(step_t *s, gm_cell c)
{
    memset(s, 0, sizeof *s);
    s->on = c.flags & 1u;
    s->acc = (c.flags >> 1) & 1u;
    s->slide = (c.flags >> 2) & 1u;
    s->tie = (c.flags >> 3) & 1u;
    s->rat = (c.flags >> GM_RAT_SHIFT) & 3u;
    s->cond = c.cond;
    s->nt = c.note;
    s->micro = c.micro;
}

/* Everything the native sequencer can play, checked before the first byte is written. */
static gm_result gm_native_check(const gm_pattern *p)
{
    uint32_t t, s;
    uint8_t id;
    for (t = 0; t < GM_TRACKS; t++) {
        gm_timing tm = p->timing[t];
        if (tm.length < 1u || tm.length > NSTEP || tm.swing > 100u || tm.source > 3u)
            return GM_INVALID;
        if (gm_native_division(tm.division, &id) != GM_OK)
            return GM_UNSUPPORTED;
        for (s = 0; s < GM_STEPS; s++) {
            gm_cell c = p->cell[t][s];
            if (c.flags > GM_FLAGS_MASK || c.cond > COND_MAX || c.note < NT_MIN || c.note > NT_MAX ||
                c.micro < -MICRO_MAX || c.micro > MICRO_MAX)
                return GM_INVALID;
        }
    }
    return GM_OK;
}

/* The live pattern as the core sees it: what plays. The native steps carry no lock: the caller keeps its own mask. */
static gm_result gm_native_export(gm_pattern *out)
{
    uint32_t t, s;
    memset(out, 0, sizeof *out);
    for (t = 0; t < GM_TRACKS; t++) {
        uint8_t mus;
        if (gm_musical_division((uint8_t)trk[t].p[P_SDIV], &mus) != GM_OK)
            return GM_UNSUPPORTED;
        out->timing[t] = (gm_timing){(uint8_t)trk[t].p[P_SLEN], mus, (uint8_t)trk[t].p[P_SSWING],
                                     (uint8_t)trk[t].p[P_SRC]};
        for (s = 0; s < out->timing[t].length && s < GM_STEPS; s++)   /* (steps past LEN never play: the native bank
                                                                       * keeps them, the core requires them empty) */
            out->cell[t][s] = gm_from_native(&trk[t].step[s]);
    }
    return GM_OK;
}

/* Stopped transport (no PLAY queued either), no song: all or nothing. The song's rows keep their own steps, so a
 * running song refuses. */
static gm_result gm_native_publish(const gm_pattern *p)
{
    uint32_t t, s;
    gm_result r = gm_native_check(p);
    if (r != GM_OK)
        return r;
    if (song.playing || transport_req == 1u || chain.running)   /* (a queued PLAY starts next block) */
        return GM_UNSUPPORTED;
    fm1_irq_off();                                    /* (no ISR reads a half-written track) */
    for (t = 0; t < GM_TRACKS; t++) {
        uint8_t id;
        gm_native_division(p->timing[t].division, &id);
        trk[t].p[P_SLEN] = p->timing[t].length;
        trk[t].p[P_SDIV] = id;
        trk[t].p[P_SSWING] = p->timing[t].swing;
        trk[t].p[P_SRC] = p->timing[t].source;
        for (s = 0; s < GM_STEPS; s++)
            gm_to_native(&trk[t].step[s], p->cell[t][s]);
    }
    fm1_irq_on();
    return GM_OK;
}

#endif
