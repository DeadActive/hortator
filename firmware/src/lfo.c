/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* LFOs: two per track, each modulating one of its knobs (spec docs/superpowers/specs/2026-10-06-lfo-design.md).
 * Waveforms are functions of the phase (2^32 = one cycle) and MORPH; S&H / WANDER / RANDOM WALK keep state.
 * Output Q15, -32767 .. 32767. */
static uint32_t beat_samples(void);               /* fx.c: a quarter note, the clock's when following */
_Static_assert(FS == 44100, "lfo_tables.h is made for 44.1 kHz");

/* SYNC: one cycle in ticks of a quarter note / 96: 8 bars .. 1/64 */
static const uint16_t LFO_SYNC_TICKS[17] = {3072, 1536, 768, 384, 192, 144, 96, 64, 72, 48, 32, 36, 24, 16, 12, 8, 6};

/* DEST -> the knob an LFO writes; R.TUN (11) writes no knob: the resonator's fine pitch (lfo_track) */
static uint32_t lfo_dest_param(uint32_t dest)
{
    return dest >= 1u && dest <= 8u ? P_E0 + dest - 1u : dest == 9u ? (uint32_t)P_LEVEL : dest == 10u ? (uint32_t)P_PAN
         : dest >= 12u && dest <= 16u ? P_RDECAY + dest - 12u : 0xFFu;
}

static uint32_t lfo_inc(const int16_t *q)
{
    uint32_t v = (uint32_t)clamp(q[LF_RATE], 0, 127), mode = (uint32_t)clamp(q[LF_MODE], 0, 2);
    if (mode == LM_HZ)
        return LR_HZ_INC[v];
    if (mode == LM_TIME)
        return LR_TIME_INC[v];
    {
        uint32_t spq = beat_samples();                    /* samples a quarter note (the clock's when following) */
        return 0xFFFFFFFFu / (spq * LFO_SYNC_TICKS[v * 17u / 128u] / 96u);
    }
}

/* sine through a gain k (Q4, 16 = 1.0) and a clip: k 16 = sine .. ~616 = a square with softened edges */
static int32_t lfo_clipsine(uint32_t ph, int32_t k) { return clamp(sine_i(ph) * k >> 4, -32767, 32767); }

/* a ramp 0..32767 bent by t (0 straight .. 127 = the curve c) and mapped to -32767 .. 32767 */
static int32_t lfo_bend(int32_t x, int32_t c, int32_t t) { return 2 * (x + t * (c - x) / 127) - 32767; }

static int32_t lfo_shape(uint32_t wave, int32_t morph, uint32_t ph)
{
    int32_t m = clamp(morph, 0, 127), x = (int32_t)(ph >> 17), x2 = x * x >> 15;   /* x: 0 .. 32767 */
    switch (wave) {
    case LW_SQUARE:
        return lfo_clipsine(ph, 16 + (127 - m) * (127 - m) * 600 / 16129);
    case LW_SINE:
        return lfo_clipsine(ph, 16 + m * m * 600 / 16129);
    case LW_SAW:
        return lfo_bend(x, x2, m);
    case LW_RSAW:
        return -lfo_bend(x, x2, m);
    case LW_EXPUP:
        return lfo_bend(x, x2 * x2 >> 15, m);
    case LW_EXPDN: {
        int32_t r = 32767 - x, r2 = r * r >> 15;
        return lfo_bend(r, r2 * r2 >> 15, m);
    }
    case LW_TRI: {                                   /* tides: the peak at m / 127 of the cycle */
        int32_t p = m * 32767 / 127, y;
        if (p <= 0)
            y = 32767 - x;
        else if (p >= 32767)
            y = x;
        else
            y = x < p ? x * 32767 / p : (32767 - x) * 32767 / (32767 - p);
        return 2 * y - 32767;
    }
    default:
        return 0;                                    /* S&H, WANDER, RWALK: lfo_value (state) */
    }
}

/* ------------------------------------------------------- runtime --- */
/* inside mix_block only: lfo_apply ... lfo_restore (song.lfo_in), a hit in between (drum_hit) re-applies its
 * track. The flag lives in song: a new small global would change the frozen code's global layout (H2). */

static int32_t lfo_rnd(track_t *t)                    /* -32767 .. 32767 */
{
    t->lrng = t->lrng * 1664525u + 1013904223u;
    return clamp((int32_t)(t->lrng >> 16) - 32768, -32767, 32767);
}

/* LFO l's value at its phase (+ PHASE); a new cycle (wrapped) draws the random waves' next point */
static int32_t lfo_value(track_t *t, uint32_t l, int wrapped)
{
    const int16_t *q = &t->p[l ? P_LFO2 : P_LFO1];
    lfo_state_t *s = &t->lfo[l];
    uint32_t wave = (uint32_t)clamp(q[LF_WAVE], 0, LW_COUNT - 1), ph = s->ph + (uint32_t)clamp(q[LF_PHASE], 0, 127) * (1u << 25);
    int32_t m = clamp(q[LF_MORPH], 0, 127), f = (int32_t)(ph >> 17);
    if (wave == LW_SH) {
        if (wrapped) {
            s->from = s->out;
            s->to = lfo_rnd(t);
        }
        return m ? s->from + (int32_t)((int64_t)(s->to - s->from) * clamp(f * 127 / m, 0, 32767) >> 15) : s->to;
    }
    if (wave == LW_WANDER) {                          /* 1 .. 8 smooth segments a cycle (MORPH: speed of change) */
        uint32_t seg = 1u + (uint32_t)m / 16u, sp = ph * seg;
        int32_t sf;
        if (sp < s->sub || wrapped) {
            s->from = s->to;
            s->to = lfo_rnd(t);
        }
        s->sub = sp;
        sf = sine_i((sp >> 17) << 15);                /* sin(pi/2 * frac) */
        return s->from + (int32_t)((int64_t)(s->to - s->from) * (sf * sf >> 15) >> 15);
    }
    if (wave == LW_RWALK) {                           /* a step a cycle, glided; MORPH: step size */
        if (wrapped) {
            int32_t step = 512 + m * 16000 / 127, to = s->to + (int32_t)((int64_t)lfo_rnd(t) * step >> 15);
            s->from = s->to;
            s->to = to > 32767 ? 65534 - to : to < -32767 ? -65534 - to : to;
        }
        return s->from + (int32_t)((int64_t)(s->to - s->from) * f >> 15);
    }
    return lfo_shape(wave, m, ph);
}

/* the LFOs of t in mask (bit l) turned on (DEST and DEPTH set): a PLAY LFO joins the cycle it would be in since PLAY
 * (rare: kept out of the per-block loop) */
static __attribute__((noinline)) void lfo_join(track_t *t, uint32_t mask)
{
    uint32_t l;
    for (l = 0; l < 2u; l++) {
        const int16_t *q = &t->p[l ? P_LFO2 : P_LFO1];
        if ((mask >> l & 1u) && q[LF_TRIG] == LT_PLAY && song.playing) {
            t->lfo[l].ph = song.tick * (uint32_t)CTL * lfo_inc(q);
            t->lfo[l].fresh = 1;
        }
    }
}

/* track t: advance its LFOs by n samples (0: none) and write the modulated knobs, saving their set values */
static void lfo_track(track_t *t, uint32_t n)
{
    uint32_t l, k, on = (t->p[P_LFO1 + LF_DEST] && t->p[P_LFO1 + LF_DEPTH]) |
                        (uint32_t)(t->p[P_LFO2 + LF_DEST] && t->p[P_LFO2 + LF_DEPTH]) << 1;
    int32_t acc[2] = {0, 0};
    t->rfine = 0;
    if (on & ~(uint32_t)t->lon)
        lfo_join(t, on & ~(uint32_t)t->lon);
    t->lon = (uint8_t)on;
    for (l = 0; l < 2u; l++) {
        const int16_t *q = &t->p[l ? P_LFO2 : P_LFO1];
        lfo_state_t *s = &t->lfo[l];
        uint32_t old, pid, off = (uint32_t)clamp(q[LF_PHASE], 0, 127) << 25;
        const param_desc_t *d;
        if (!q[LF_DEST] || !q[LF_DEPTH])
            continue;                                 /* off: costs nothing (its phase waits) */
        old = s->ph;
        s->ph += lfo_inc(q) * n;
        s->out = lfo_value(t, l, (n && s->ph + off < old + off) | s->fresh);   /* a new cycle where PHASE puts it, or a
                                                                                 * restart */
        s->fresh = 0;
        pid = lfo_dest_param((uint32_t)clamp(q[LF_DEST], 0, 16));
        if (pid == 0xFFu)
            continue;
        d = track_desc(t, pid);
        if (!d->label || d->label[0] == '-' || d->max <= d->min)
            continue;                                 /* the engine has no such knob */
        for (k = 0; k < t->lnum && t->lpid[k] != pid; k++)
            ;
        if (k == t->lnum) {
            t->lpid[k] = (uint8_t)pid;
            t->lsave[k] = t->p[pid];
            t->lnum++;
        }
        acc[k] += s->out * clamp(q[LF_DEPTH], -64, 64) / 64 * (d->max - d->min) / 32767;
    }
    for (k = 0; k < t->lnum; k++) {
        const param_desc_t *d = track_desc(t, t->lpid[k]);
        t->lval[k] = (int16_t)clamp(t->lsave[k] + acc[k], d->min, d->max);
        t->p[t->lpid[k]] = t->lval[k];
    }
    for (l = 0; l < 2u; l++) {                        /* R.TUN: the resonator's fine pitch, smooth (TUNE untouched) */
        const int16_t *q = &t->p[l ? P_LFO2 : P_LFO1];
        if (q[LF_DEST] == 11 && q[LF_DEPTH])
            t->rfine += t->lfo[l].out * clamp(q[LF_DEPTH], -64, 64) / 64 * RS_FINE / 32767;
    }
}

static void lfo_apply(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        lfo_track(&trk[i], n);
    song.lfo_in = 1;
}

static void lfo_restore_track(track_t *t)
{
    uint32_t k;
    for (k = 0; k < t->lnum; k++)
        t->p[t->lpid[k]] = t->lsave[k];
    t->lnum = 0;
}

static void lfo_restore(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        lfo_restore_track(&trk[i]);
    song.lfo_in = 0;
}

/* a hit of t: its HIT LFOs restart; inside the block the knobs are modulated again from there (the trigger reads
 * them next) */
static void lfo_hit(track_t *t)
{
    uint32_t l, any = 0;
    for (l = 0; l < 2u; l++)
        if (t->p[(l ? P_LFO2 : P_LFO1) + LF_TRIG] == LT_HIT) {
            t->lfo[l].ph = 0;
            t->lfo[l].sub = 0;
            t->lfo[l].fresh = 1;
            any = 1;
        }
    if (any && song.lfo_in) {
        lfo_restore_track(t);
        lfo_track(t, 0);
    }
}

/* PLAY: PLAY LFOs restart, the random sequences too (HIT LFOs' as well); inside the block the knobs are modulated
 * again from there (this block's hits read them next) */
static void lfo_start(void)
{
    uint32_t i, l;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        t->lrng = 0x2545F491u * (i + 1u);
        for (l = 0; l < 2u; l++)
            if (t->p[(l ? P_LFO2 : P_LFO1) + LF_TRIG] != LT_FREE) {
                memset(&t->lfo[l], 0, sizeof t->lfo[l]);
                t->lfo[l].fresh = 1;
                t->lon |= (uint8_t)(1u << l);         /* restarted here: no joining */
            }
        if (song.lfo_in) {
            lfo_restore_track(t);
            lfo_track(t, 0);
        }
    }
}

/* the UI: the knob pid of t is modulated now, at *val (the last block's value) */
static int lfo_live(const track_t *t, uint32_t pid, int32_t *val)
{
    uint32_t l;
    if (pid == P_RTUNE) {                             /* R.TUN: TUNE + the fine offset of the last block */
        if ((t->p[P_LFO1 + LF_DEST] == 11 && t->p[P_LFO1 + LF_DEPTH]) || (t->p[P_LFO2 + LF_DEST] == 11 && t->p[P_LFO2 + LF_DEPTH])) {
            *val = clamp(t->p[P_RTUNE] + t->rfine / 256, 24, 96);
            return 1;
        }
        return 0;
    }
    for (l = 0; l < 2u; l++) {
        const int16_t *q = &t->p[l ? P_LFO2 : P_LFO1];
        if (q[LF_DEST] && q[LF_DEPTH] && lfo_dest_param((uint32_t)q[LF_DEST]) == pid) {
            const param_desc_t *d = track_desc(t, pid);
            *val = clamp(t->p[pid] + t->lfo[l].out * clamp(q[LF_DEPTH], -64, 64) / 64 * (d->max - d->min) / 32767,
                         d->min, d->max);
            return 1;
        }
    }
    return 0;
}

static int32_t lfo_out(const track_t *t, uint32_t l) { return t->lfo[l & 1u].out; }
