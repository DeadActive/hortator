/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* One-shot IMA ADPCM playback (eng_sample.c decoder) for the SAMPLE model and the sample layer. */

/* zone index of key in set (built-in sets, then USR1..3), 0xFFFF = none */
static uint32_t smp_find(uint32_t set, uint32_t key)
{
    uint32_t i, zi = 0xFFFFu;
    set %= SMP_NALL;
    if (set < SMP_NSETS) {
        const smp_set_t *s = &SMP_SETS[set];
        for (i = 0; i < s->nz; i++)
            if (key >= SMP_ZONES[s->z0 + i].lo && key <= SMP_ZONES[s->z0 + i].hi)
                zi = s->z0 + i;
    } else {
        uint32_t k = set - SMP_NSETS;
        for (i = 0; i < usr_nz[k]; i++)
            if (key >= usr_zone[k][i].lo && key <= usr_zone[k][i].hi)
                zi = 0x8000u | k << 5 | i;
    }
    return zi;
}

/* from the start of zone zi; key and tune (semitones) set the speed */
static void smp_start(dvoice_t *v, uint32_t zi, uint32_t key, int32_t tune)
{
    const smp_zone_t *z = smp_zone(zi);
    voice_t *s = &v->sv;
    int32_t d16 = clamp((int32_t)key * 16 + tune * 16 - z->root16, -1536, 576);   /* <= 3 octaves up */
    s->ph[0] = s->ph[1] = 0;
    s->s[0] = s->s[1] = s->s[2] = s->s[6] = 0;
    s->s[4] = (int32_t)zi;
    s->s[5] = (int32_t)((pow2_q16(d16) >> 8) * (z->rate >> 8));   /* Q16 source samples per output */
    s->s[3] = z->n ? sample_next(z, s, 0) : 0;
}

/* one block: amplitude env[0] (factor k[0]), gain Q15, one-pole tone lp (Q15 coefficient), drive 0..127.
 * Returns 0 when the sample or its envelope has ended (the voice is then off). */
static int smp_render(dvoice_t *v, int32_t *out, uint32_t n, int32_t gain, int32_t lp, int32_t drv)
{
    voice_t *s = &v->sv;
    const smp_zone_t *z = smp_zone((uint32_t)s->s[4]);
    uint32_t i, frac = s->ph[1], stepq = (uint32_t)s->s[5];
    for (i = 0; i < n; i++) {
        int32_t x;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            s->s[2] = s->s[3];
            if (s->ph[0] >= z->n) {
                dm_end(v);
                return 0;
            }
            s->s[3] = sample_next(z, s, 0);
        }
        x = s->s[2] + (((s->s[3] - s->s[2]) * (int32_t)(frac >> 1)) >> 15);
        s->s[6] += mulq15(x - s->s[6], lp);
        x = s->s[6];
        if (drv)
            x = softclip(x + (((x >> 2) * (drv * 150)) >> 11));
        x = mulq15(mulq15(x, env_q15(v->env[0])), gain);
        x = mulq15(x, VOICE_FS);
        out[i] += x;
        v->last = x;
        env_step(v, 0);
    }
    s->ph[1] = frac;
    if (v->env[0] < ENV_END) {
        dm_end(v);
        return 0;
    }
    return 1;
}

/* SAMPLE model: TUNE DECAY TONE DRIVE, SET KEY */
static int smpl_playable(const track_t *t)          /* SET / KEY name a sample (else a hit takes no voice) */
{
    uint32_t zi = smp_find((uint32_t)t->p[P_E4], (uint32_t)t->p[P_E5]);
    return zi != 0xFFFFu && smp_zone(zi)->n;
}

static void smpl_trigger(track_t *t, dvoice_t *v)
{
    const int16_t *p = &t->p[P_E0];
    uint32_t zi = smp_find((uint32_t)p[4], (uint32_t)p[5]);
    if (zi == 0xFFFFu || !smp_zone(zi)->n) {
        dm_end(v);
        return;
    }
    smp_start(v, zi, (uint32_t)p[5], p[0]);
    v->env[0] = ENV1;
    v->k[0] = dk(p[1]);
    v->x[0] = 4000 + p[2] * 28767 / 127;        /* tone lp coefficient */
}

static void smpl_render(track_t *t, dvoice_t *v, int32_t *out, uint32_t n)
{
    smp_render(v, out, n, vel_gain(v), v->x[0], t->p[P_E3]);
}

#define DM_SMPL_DEF {"SMPL", 2, 0, {PD("TUNE", F_SEMI, -24, 24, 0), PD("DECAY", F_INT, 0, 127, 127), \
    PD("TONE", F_INT, 0, 127, 127), PD("DRIVE", F_PCT, 0, 127, 0), PE("SET", SMP_ALL_NAMES, 0), \
    PD("KEY", F_INT, 0, 127, 36), PD("-", F_INT, 0, 0, 0), PD("-", F_INT, 0, 0, 0)}, smpl_trigger, smpl_render}
