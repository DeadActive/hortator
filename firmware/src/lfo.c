/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* LFOs: two per track, each modulating one of its knobs (spec docs/superpowers/specs/2026-10-06-lfo-design.md).
 * Waveforms are functions of the phase (2^32 = one cycle) and MORPH; S&H / WANDER / RANDOM WALK keep state.
 * Output Q15, -32767 .. 32767. */
_Static_assert(FS == 44100, "lfo_tables.h is made for 44.1 kHz");

/* SYNC: one cycle in ticks of a quarter note / 96: 8 bars .. 1/64 */
static const uint16_t LFO_SYNC_TICKS[17] = {3072, 1536, 768, 384, 192, 144, 96, 64, 72, 48, 32, 36, 24, 16, 12, 8, 6};

static uint32_t lfo_dest_param(uint32_t dest)
{
    return dest >= 1u && dest <= 8u ? P_E0 + dest - 1u : dest == 9u ? (uint32_t)P_LEVEL : dest == 10u ? (uint32_t)P_PAN : 0xFFu;
}

static uint32_t lfo_inc(const int16_t *q)
{
    uint32_t v = (uint32_t)clamp(q[LF_RATE], 0, 127), mode = (uint32_t)clamp(q[LF_MODE], 0, 2);
    if (mode == LM_HZ)
        return LR_HZ_INC[v];
    if (mode == LM_TIME)
        return LR_TIME_INC[v];
    {
        uint32_t spq = (uint32_t)FS * 60u / (uint32_t)clamp(song.g[G_BPM], 40, 240);   /* samples a quarter note */
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
