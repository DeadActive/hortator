/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The landing page's lab (docs/superpowers/specs/2026-10-09-websim-features-lab-design.md): the firmware's own DSP
 * driven directly for the feature pieces. A blank project; a track's model, parameters, steps; globals; the transport;
 * a hit, a PERFORM effect held; one track's hit rendered alone (with or without its RESON + FILTER); the Grids lanes;
 * and per audio half (256 frames) a probe of what the pictures draw: the compressor's gain reduction, every track's
 * two LFO outputs, the tracks that fired, the step each track is on. Included by sim_core.c (one translation unit). */
#define LAB_MAX_FRAMES (FS * 4u)                      /* 4 s of one channel */
#define LAB_PROBES 2048u                               /* 11.9 s of halves */

typedef struct {
    int32_t gr;                                        /* comp.gr: <= 0 (Streams' gain reduction) */
    int32_t lfo[NTRK][2];                              /* t->lfo[l].out, Q15 */
    uint32_t fired;                                    /* bit k: track k started a voice in this half */
    uint16_t idx[NTRK];                                /* t->seq_idx */
} lab_probe_t;                                         /* 88 bytes; lab.js reads it by these offsets */

static float lab_out[LAB_MAX_FRAMES];
static lab_probe_t lab_ring[LAB_PROBES];
static uint32_t lab_np, lab_age[NTRK];

static uint32_t lab_newest(const track_t *t)           /* the age of the track's newest voice */
{
    uint32_t i, a = 0;
    for (i = 0; i < NDV; i++)
        a = t->v[i].age > a ? t->v[i].age : a;
    return a;
}

static void lab_half(void)                             /* after each audio half (sim_render) */
{
    lab_probe_t *p;
    uint32_t k;
    if (lab_np >= LAB_PROBES)
        return;
    p = &lab_ring[lab_np++];
    p->gr = comp.gr;
    p->fired = 0;
    for (k = 0; k < NTRK; k++) {
        uint32_t a = lab_newest(&trk[k]);
        p->lfo[k][0] = trk[k].lfo[0].out;
        p->lfo[k][1] = trk[k].lfo[1].out;
        if (a != lab_age[k])
            p->fired |= 1u << k;
        lab_age[k] = a;
        p->idx[k] = trk[k].seq_idx;
    }
}

void lab_probe_clear(void) { lab_np = 0; }
uint32_t lab_probe_count(void) { return lab_np; }
uint32_t lab_probe_size(void) { return sizeof(lab_probe_t); }
lab_probe_t *lab_probes(void) { return lab_ring; }
float *lab_buf(void) { return lab_out; }

void lab_reset(void)                                   /* a blank project at 120 BPM on the internal clock */
{
    uint32_t e, k;
    for (e = 0; e < PF_N; e++)
        perf_press(e, 0);
    perf_mask = 0;
    fm1_in.buttons = fm1_in.notes = 0;
    sim_flash_reset();
    sim_init(0);
    song.g[G_BPM] = 120;
    song.g[G_CLOCK] = 0;
    for (k = 0; k < NTRK; k++)
        lab_age[k] = lab_newest(&trk[k]);
    lab_np = 0;
    sim_half_hook = lab_half;
}

void lab_model(uint32_t t, uint32_t m) { if (t < NTRK && m < NMODELS) drum_set_model(&trk[t], m); }
void lab_param(uint32_t t, uint32_t p, int32_t v) { if (t < NTRK && p < P_COUNT) trk[t].p[p] = (int16_t)v; }
void lab_global(uint32_t g, int32_t v) { if (g < G_COUNT) song.g[g] = (int16_t)v; }
void lab_step(uint32_t t, uint32_t s, uint32_t on, uint32_t acc, uint32_t cond, uint32_t rat)
{
    step_t *st;
    if (t >= NTRK || s >= NSTEP)
        return;
    st = &trk[t].step[s];
    st->on = on ? 1u : 0u;
    st->acc = acc ? 1u : 0u;
    st->cond = (uint8_t)(cond < COND_MAX ? cond : COND_MAX);
    st->rat = (uint8_t)(rat < 3u ? rat : 3u);
}
void lab_play(int on) { transport_req = on ? 1u : 2u; }   /* taken at the next block, as the UI does */
void lab_hit(uint32_t t, uint32_t vel) { if (t < NTRK) drum_hit(&trk[t], vel); }
void lab_perf(uint32_t e, int down) { if (e < PF_N) perf_press(e, down); }

void lab_render_mono(uint32_t frames)                  /* the whole mix, (L + R) / 2 into lab_buf() */
{
    uint32_t done = 0;
    if (frames > LAB_MAX_FRAMES)
        frames = LAB_MAX_FRAMES;
    while (done < frames) {
        uint32_t n = frames - done < SIM_MAX_FRAMES ? frames - done : SIM_MAX_FRAMES, i;
        const float *a;
        sim_render(n);
        a = sim_audio();
        for (i = 0; i < n; i++)
            lab_out[done + i] = 0.5f * (a[2u * i] + a[2u * i + 1u]);
        done += n;
    }
}

/* track 1's hit alone, from silence: its voices (and with inserts, its RESON and FILTER) rendered block by block,
 * no LFOs, no level, no FX; for the waveform and spectrum of a voice or a resonator */
void lab_voice(uint32_t frames, int inserts)
{
    static int32_t blk[CTL];
    track_t *t = &trk[0];
    uint32_t done = 0, guard;
    if (frames > LAB_MAX_FRAMES)
        frames = LAB_MAX_FRAMES;
    drum_cut(t);
    for (guard = 0; guard < FS * 2u / CTL; guard++) {  /* the previous hit's tail and ring, out */
        uint32_t snd;
        drum_block_begin();
        snd = track_render(t, blk, CTL);
        if (trk_inserts(t, blk, CTL, snd) == 0u && snd == 0u && guard > 4u)
            break;
    }
    drum_hit(t, 127);
    while (done < frames) {
        uint32_t i, snd;
        drum_block_begin();
        snd = track_render(t, blk, CTL);
        if (inserts)
            trk_inserts(t, blk, CTL, snd);
        for (i = 0; i < CTL && done < frames; i++, done++)
            lab_out[done] = (float)blk[i] * (1.0f / 32768.0f);
    }
}

uint8_t *lab_grids(void)                               /* 3 lanes x 32 steps: bit 0 a hit, bit 1 an accent */
{
    static uint8_t out[96];
    uint32_t ch, i;
    for (ch = 0; ch < 3u; ch++)
        for (i = 0; i < 32u; i++)
            out[ch * 32u + i] = (uint8_t)grids_preview(ch, i);
    return out;
}
