/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* ACID (dm_acid.c, spec docs/superpowers/specs/2026-10-09-acid-design.md §4): the pitch, the filter's stability and
 * self-oscillation, the band-limited oscillator, gate, slide, accent, the level, the extremes. One line per test;
 * any failure exits 1. Built with UBSan: a signed overflow aborts. */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}
#define N (FS * 2)
static int32_t buf[N];

static double goertzel(const int32_t *x, uint32_t n, double f)
{
    double w = 2 * M_PI * f / FS, c = 2 * cos(w), s0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        s0 = x[i] * (0.5 - 0.5 * cos(2 * M_PI * i / n)) + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) / n;
}
static double rms(const int32_t *x, uint32_t n)
{
    double s = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (n ? n : 1));
}
static int32_t peak(const int32_t *x, uint32_t n)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        m = abs(x[i]) > m ? abs(x[i]) : m;
    return m;
}

static track_t *acid(void)                          /* track 0 as ACID at its defaults, note C4 */
{
    host_init();
    drum_set_model(&trk[0], DM_ACID);
    return &trk[0];
}
static void play(track_t *t, uint32_t vel, int32_t nt, int32_t *out, uint32_t n)   /* a note, the gate open */
{
    memset(out, 0, n * sizeof *out);
    drum_play(t, vel, nt);
    dm_render_n(t, out, n);
}

static void test_pitch(void)                         /* A2 (nt -15): 110 Hz is the strongest partial near it */
{
    track_t *t = acid();
    double f;
    t->p[P_E0 + 2] = 127;                            /* CUTOF open: the fundamental clear */
    play(t, 96, -15, buf, N / 2);
    f = goertzel(buf + FS / 10, N / 2 - FS / 10, 110.0);
    check("acid: A2 sounds at 110 Hz (the step's note + TUNE)",
          f > 4 * goertzel(buf + FS / 10, N / 2 - FS / 10, 104.0) && f > 4 * goertzel(buf + FS / 10, N / 2 - FS / 10, 116.0));
}

static void test_gate(void)                          /* gate closed: silent within ~15 ms; open: still sounding */
{
    track_t *t = acid();
    play(t, 96, 0, buf, FS / 4);
    check("acid: the gate open holds the note (amp not decayed at 250 ms)", rms(buf + FS / 5, FS / 20) > 200.0);
    DMODELS[DM_ACID].gate(t, &t->v[0], 0);
    memset(buf, 0, FS / 4 * sizeof *buf);
    dm_render_n(t, buf, FS / 4);
    check("acid: the gate closed ends the note within 15 ms", rms(buf + FS * 15 / 1000, FS / 20) < 20.0 &&
          !t->v[0].active);
}

static void test_slide(void)                         /* a glide from C3 to C4 reaches C4 in 40..90 ms, no restart */
{
    track_t *t = acid();
    int32_t fe;
    uint32_t i, at = 0;
    play(t, 96, -12, buf, FS / 10);
    fe = t->v[0].env[1];
    t->nt = 0;
    DMODELS[DM_ACID].glide(t, &t->v[0]);
    check("acid: a slide does not restart the filter envelope", t->v[0].env[1] <= fe);
    for (i = 0; i < FS / 5 / CTL; i++) {
        dm_render_n(t, buf, CTL);
        if (!at && abs(t->v[0].x[0] - t->v[0].x[1]) < 16)   /* within 1 semitone (16 sixteenths) of C4 */
            at = (i + 1u) * CTL;
    }
    check("acid: a slide reaches the next note in 40..90 ms", at >= FS * 40 / 1000 && at <= FS * 90 / 1000);
}

static void test_accent(void)                        /* ACC: louder and brighter than a plain note */
{
    track_t *t = acid();
    double plain, acc, bp, ba;
    play(t, 96, -24, buf, FS / 5);
    plain = rms(buf, FS / 5);
    bp = goertzel(buf, FS / 5, 65.4 * 8);
    t = acid();
    play(t, 127, -24, buf, FS / 5);
    acc = rms(buf, FS / 5);
    ba = goertzel(buf, FS / 5, 65.4 * 8);
    check("acid: accent is louder (> +2 dB)", acc > plain * 1.26);
    check("acid: accent opens the filter (the 8th harmonic > +3 dB)", ba > bp * 1.41);
}

static void test_self_osc(void)                      /* RESO 127, CUTOF at ~1 kHz: a ringing sine near the cutoff */
{
    track_t *t = acid();
    t->p[P_E0 + 3] = 127;
    t->p[P_E0 + 4] = 0;                              /* ENVMD 0: the cutoff stays */
    play(t, 96, -36, buf, N / 2);
    check("acid: RESO 127 self-oscillates (energy near the cutoff, the output bounded)",
          rms(buf + FS / 2, FS / 2) > 500.0 && peak(buf, N / 2) < 32767 * 4);
}

/* C7, SAW and SQR (spec §8): the 21st harmonic folds back to ~147 Hz, in band (the ladder at CUTOF 127 passes it):
 * under -30 dB of the fundamental. A naive (no polyBLEP) wave has it at 1 / 21, -26 dB: the measurement tells them
 * apart (checked on a naive saw made here) */
static void test_alias(void)
{
    double f = (double)acid_inc(96 * 16) * FS / 4294967296.0, fa = fabs(FS - 21.0 * f), f0, al;
    uint32_t w, i, ph = 0, inc = acid_inc(96 * 16);
    for (i = 0; i < FS; i++, ph += inc)                /* a naive saw at the voice's own C7 */
        buf[i] = (int32_t)(ph >> 17) - 16384;
    check("acid: the alias measurement tells a naive saw (its folded 21st > -30 dB)",
          goertzel(buf + FS / 10, FS - FS / 10, fa) > goertzel(buf + FS / 10, FS - FS / 10, f) * 0.0316);
    for (w = 0; w < 2u; w++) {
        track_t *t = acid();
        t->p[P_E0 + 2] = 127;
        t->p[P_E0 + 3] = 0;
        t->p[P_E0 + 6] = (int16_t)w;                   /* WAVE: SAW, SQR */
        play(t, 96, 36, buf, FS);
        f0 = goertzel(buf + FS / 10, FS - FS / 10, f);
        al = goertzel(buf + FS / 10, FS - FS / 10, fa);
        check(w ? "acid: a high SQR is band-limited (C7: the folded 21st, ~147 Hz, < -30 dB of the fundamental)"
                : "acid: a high SAW is band-limited (C7: the folded 21st, ~147 Hz, < -30 dB of the fundamental)",
              al < f0 * 0.0316);
    }
}

static void test_extremes(void)                      /* Review Focus 3: no overflow (UBSan), bounded */
{
    static const int16_t V[4] = {0, 1, 64, 127};
    uint32_t a, b, c, d, ok = 1;
    for (a = 0; a < 4u; a++)
        for (b = 0; b < 4u; b++)
            for (c = 0; c < 4u; c++)
                for (d = 0; d < 4u; d++) {
                    track_t *t = acid();
                    t->p[P_E0 + 2] = V[a];
                    t->p[P_E0 + 3] = V[b];
                    t->p[P_E0 + 4] = V[c];
                    t->p[P_E0 + 5] = V[d];
                    t->p[P_E0] = (int16_t)(d & 1u ? 24 : -24);
                    t->p[P_E0 + 6] = (int16_t)(c & 1u);
                    play(t, 127, (a & 1u) ? NT_MAX : NT_MIN, buf, FS / 10);
                    drum_play(t, 127, 0);                 /* an accent chain */
                    dm_render_n(t, buf, FS / 10);
                    if (peak(buf, FS / 10) >= 32767 * 4)
                        ok = 0;
                }
    check("acid: every extreme of CUTOF x RESO x ENVMD x ACCNT x note x TUNE stays bounded", ok);
}

static void test_level(void)                         /* the defaults' peak within +-3 dB of TOM's */
{
    track_t *t = acid();
    int32_t pa, pt;
    play(t, 96, -24, buf, FS / 4);
    pa = peak(buf, FS / 4);
    host_init();
    drum_set_model(&trk[0], DM_TOM);
    play(&trk[0], 96, 0, buf, FS / 4);
    pt = peak(buf, FS / 4);
    check("acid: the defaults' level is within +-3 dB of TOM's", pa > pt * 0.708 && pa < pt * 1.413);
}

/* review round 1 (spec §4): the slope, the wow, a slide out of an accent, self-oscillation, the cutoff's and the
 * level's smoothness. Thresholds from the measured voice, with margins (task-3-report.md, fix round 1) */
static double fc_of(int32_t cutof)                   /* CUTOF's cutoff (ENVMD 0, no accent): MIDI 16 + 0..103 st */
{
    return 440.0 * pow(2, ((cutof * 103 * 16 / 127) / 16.0 + 16 - 69) / 12);
}

static void test_slope(void)                         /* 24 dB/oct: the saw's partials from 2x to 8x the cutoff */
{
    track_t *t = acid();
    double f0 = 32.703, fc = fc_of(40), a2, a8;
    int32_t h;
    t->p[P_E0 + 3] = 0;
    t->p[P_E0 + 4] = 0;
    play(t, 96, -36, buf, FS);                       /* C1 */
    h = (int32_t)(2 * fc / f0 + 0.5);
    a2 = goertzel(buf + FS / 4, FS * 3 / 4, h * f0) * h;   /* x h: the saw's 1 / n undone */
    h = (int32_t)(8 * fc / f0 + 0.5);
    a8 = goertzel(buf + FS / 4, FS * 3 / 4, h * f0) * h;
    check("acid: the ladder falls 24 dB/oct (-20 dB/oct or more from 2x to 8x the cutoff)",
          20 * log10(a8 / a2) / 2 <= -20.0);
}

static void test_wow(void)                           /* four accented 1/16s at 120 BPM: the 4th opens further */
{
    track_t *t = acid();
    uint32_t k, L = FS / 8 / CTL * CTL;
    double m[4];
    for (k = 0; k < 4u; k++) {
        drum_play(t, 127, -24);
        render_track(t, buf, L);                     /* track_render: the wow falls every block */
        m[k] = goertzel(buf, L, 65.406 * 8);
    }
    check("acid: consecutive accents charge the wow (the 4th note's 8th harmonic > +2 dB over the 1st's)",
          m[3] > m[0] * 1.259);
}

static void test_slide_unaccent(void)                /* a slide from an accented step into a plain one */
{
    track_t *t = acid();
    double r1, r2;
    play(t, 127, -24, buf, FS / 4);
    r1 = rms(buf + FS / 8, FS / 8);
    t->v[0].vel = 96;                                /* as acid_step: the slid-to step's velocity, then glide */
    DMODELS[DM_ACID].glide(t, &t->v[0]);
    memset(buf, 0, FS / 4 * sizeof *buf);
    dm_render_n(t, buf, FS / 4);
    r2 = rms(buf + FS / 8, FS / 8);
    check("acid: a slide into a plain step drops the accent (x[3] 0, the level down > 3.5 dB)",
          t->v[0].x[3] == 0 && r2 < r1 * 0.668);
}

static void test_self_osc_far(void)                  /* RESO 127, the note 7 octaves under the cutoff: a sine there */
{
    track_t *t = acid();
    double fc = fc_of(100), best = 0, g;
    t->p[P_E0 + 2] = 100;
    t->p[P_E0 + 3] = 127;
    t->p[P_E0 + 4] = 0;
    play(t, 96, -60, buf, N / 2);                    /* C-1 (8 Hz) under ~2.2 kHz */
    for (g = fc * 0.85; g < fc * 1.15; g += fc / 2000)
        best = fmax(best, goertzel(buf + FS / 2, FS / 2, g));
    check("acid: RESO 127 oscillates by itself far from the note (a sine near the cutoff, magnitude > 1000)",
          best > 1000.0);
}

static void test_cutoff_smooth(void)                 /* blocks vs the cutoff computed every sample: no zipper */
{
    static int32_t ref[8192];
    track_t *t;
    uint32_t i, mode;
    double e = 0, s = 0;
    for (mode = 0; mode < 2u; mode++) {
        int32_t *o = mode ? ref : buf;
        t = acid();
        t->p[P_E0 + 1] = 0;                          /* DECAY 0, ENVMD 127, RESO 100: the fastest sweep, ringing */
        t->p[P_E0 + 3] = 100;
        t->p[P_E0 + 4] = 127;
        memset(o, 0, 8192 * sizeof *o);
        drum_play(t, 96, -24);
        if (mode)
            for (i = 0; i < 8192u; i++)
                DMODELS[DM_ACID].render(t, &t->v[0], o + i, 1);
        else
            dm_render_n(t, o, 8192);
    }
    for (i = 0; i < 8192u; i++) {
        e += (double)(buf[i] - ref[i]) * (buf[i] - ref[i]);
        s += (double)ref[i] * ref[i];
    }
    check("acid: the cutoff moves smoothly (CTL blocks within -30 dB of a per-sample cutoff)", 10 * log10(e / s) < -30.0);
}

static void test_level_ramp(void)                    /* a slide into an accent: the level ramps, no step */
{
    track_t *t = acid();
    int32_t prev, mx, pk;
    uint32_t i;
    t->p[P_E0 + 3] = 0;
    play(t, 96, -60, buf, FS / 10);                  /* C-1: the output moves slowly, a level step shows */
    prev = t->v[0].last;
    t->v[0].vel = 127;
    DMODELS[DM_ACID].glide(t, &t->v[0]);
    memset(buf, 0, 8 * CTL * sizeof *buf);
    dm_render_n(t, buf, 8 * CTL);
    pk = peak(buf, 8 * CTL);
    mx = abs(buf[0] - prev);
    for (i = 1; i < 8u * CTL; i++)
        mx = abs(buf[i] - buf[i - 1]) > mx ? abs(buf[i] - buf[i - 1]) : mx;
    check("acid: a slide into an accent ramps the level (no sample step > 5 % of the peak)", mx < pk / 20);
}

/* the sequencer on track 0 (ACID), 16th steps at 120 BPM: run n steps, render the track's voices */
static void seq_run(track_t *t, uint32_t steps, int32_t *out)
{
    uint32_t i, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0) * steps;
    memset(out, 0, len * sizeof *out);
    for (i = 0; i + CTL <= len; i += CTL) {
        seq_tick(t, CTL);
        dm_render_n(t, out + i, CTL);
    }
}
static uint32_t seq_at;                              /* seq_to: samples since acid_seq */
static track_t *acid_seq(void)
{
    track_t *t = acid();
    memset(buf, 0, sizeof buf);                       /* (the checks read a little past seq_run's steps) */
    memset(t->step, 0, sizeof t->step);
    t->p[P_SLEN] = 16;
    t->p[P_SRC] = 0;
    song.playing = 1;
    t->seq_pos = 0x7FFFFFFFu;                         /* PLAY: step 0 next block */
    t->seq_cnt = (uint32_t)-1;
    t->gate_n = 0;
    t->slide_in = 0;
    seq_at = 0;
    return t;
}

static void seq_to(track_t *t, uint32_t until)      /* the sequencer and the track's voices up to sample `until` */
{
    static int32_t scratch[CTL];
    for (; seq_at + CTL <= until; seq_at += CTL) {
        seq_tick(t, CTL);
        dm_render_n(t, scratch, CTL);
    }
}

static void test_seq_gate(void)                      /* a plain note: the gate closes at GATE (50 %) of the step */
{
    track_t *t = acid_seq();
    uint32_t len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    t->step[0].on = 1;
    seq_run(t, 1, buf);
    check("acid seq: the gate closes at GATE of the step, the note ends in that step",
          rms(buf + len * 3 / 4, len / 8) < 20.0 && rms(buf + len / 8, len / 4) > 200.0);
}

static void test_seq_slide(void)                     /* SLIDE on step 0: step 1 glides, the envelopes not restarted */
{
    track_t *t = acid_seq();
    int32_t fe;
    uint32_t age, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    t->step[0].on = 1;
    t->step[0].slide = 1;
    t->step[0].nt = -12;
    t->step[1].on = 1;
    t->step[1].nt = 0;
    seq_run(t, 1, buf);
    fe = t->v[0].env[1];
    age = t->v[0].age;
    seq_run(t, 1, buf);
    check("acid seq: a slide keeps the note sounding into the next step (no restart, the gate held)",
          rms(buf + len / 8, len / 4) > 200.0 && t->v[0].age == age && t->v[0].env[1] < fe &&
          t->v[0].x[1] == 60 * 16);   /* (step 1 itself does not slide: its gate closes at GATE, the note ends in it) */
}

static void test_seq_tie(void)                       /* TIE: no restart, the same pitch, the gate held through it */
{
    track_t *t = acid_seq();
    int32_t fe, p;
    uint32_t len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    t->step[0].on = 1;
    t->step[1].tie = 1;
    t->step[1].nt = 12;                                /* a TIE's own note is ignored */
    seq_run(t, 1, buf);
    fe = t->v[0].env[1];
    p = t->v[0].x[1];
    seq_run(t, 1, buf);
    check("acid seq: a TIE holds the note (no restart, the same pitch, sounding at its end)",
          t->v[0].env[1] < fe && t->v[0].x[1] == p && rms(buf + len * 3 / 4, len / 8) > 200.0);
}

static void test_seq_rest(void)                      /* a rest after a held gate closes it */
{
    track_t *t = acid_seq();
    t->step[0].on = 1;
    t->step[0].slide = 1;                              /* held into step 1 */
    seq_run(t, 2, buf);
    check("acid seq: a rest after a slide closes the gate", !(t->v[0].x[2] & 1) && t->gate_n == 0u);
}

static void test_seq_prob_tie(void)                  /* Review Focus 4 */
{
    track_t *t = acid_seq();
    t->step[0].on = 1;
    t->step[0].cond = 1;                               /* PROB 0 %: never plays */
    t->step[1].tie = 1;
    seq_run(t, 2, buf);
    check("acid seq: a TIE after a skipped step plays nothing", !t->v[0].active && peak(buf, FS / 4) == 0);
    t = acid_seq();
    t->step[0].on = 1;
    t->step[0].slide = 1;
    t->step[1].on = 1;
    t->step[1].cond = 1;                               /* the slid-to step skipped */
    seq_run(t, 2, buf);
    check("acid seq: a slide into a skipped step closes the gate", !(t->v[0].x[2] & 1));
    t = acid_seq();
    t->step[0].on = 1;
    t->step[0].rat = 1;                                /* 2 hits */
    seq_to(t, CTL);                                    /* hit 1 */
    {
        uint32_t age = t->v[0].age, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
        int32_t fe;
        seq_to(t, len / 2u - CTL);                     /* just before hit 2 (at half the step) */
        fe = t->v[0].env[1];
        seq_to(t, len / 2u + 3u * CTL);                /* just after it (hit 2 lands in the block holding len / 2) */
        check("acid seq: a ratcheted step restarts the note (hit 2: a new voice, the filter envelope from the top)",
              t->rat_k == 2u && t->v[0].active && t->v[0].age != age && t->v[0].env[1] > fe);
    }
}

static void test_seq_stop_closes(void)              /* Review Focus 1: STOP / a model change: no hanging note */
{
    track_t *t = acid_seq();
    t->step[0].on = 1;
    t->step[0].slide = 1;
    seq_run(t, 1, buf);
    seq_stop();                                        /* the transport stops (seq.c: every stop path calls it) */
    memset(buf, 0, FS / 10 * sizeof *buf);
    dm_render_n(t, buf, FS / 10);
    check("acid seq: STOP closes an open gate (the note ends)", rms(buf + FS / 20, FS / 20) < 20.0);
    t = acid_seq();
    t->step[0].on = 1;
    t->step[0].slide = 1;
    seq_run(t, 1, buf);
    t->p[P_MODEL] = DM_K808;
    model_follow(t);                                   /* a model change cuts the voices */
    check("acid seq: a model change cuts the note and clears the gate", !t->v[0].active && t->gate_n == 0u &&
          !t->slide_in);
}

static void test_seq_drum_tie(void)                  /* a drum track: a TIE step plays nothing */
{
    track_t *t = acid_seq();
    t->p[P_MODEL] = DM_K808;
    t->model = DM_K808;
    t->step[0].tie = 1;
    seq_run(t, 1, buf);
    check("seq: a TIE step on a drum track plays nothing", !t->v[0].active && !t->v[1].active);
}

/* a hit no step drives (MIDI, the keys, Grids: drum_hit) closes its own gate after GATE of the track's step, the
 * transport stopped: the gate counts in seq_tick, which runs every block */
/* ruling 1 with the transport running: a live hit (drum_hit) late in a step keeps its own GATE through the next
 * step's start (a rest closes only a gate the sequencer holds), and a TIE does not extend it */
static void test_live_hit_playing(void)
{
    track_t *t = acid_seq();
    uint32_t len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0), at = len * 4u / 5u / CTL * CTL,
             g;
    seq_to(t, at);                                     /* an empty pattern, playing: 80 % into step 0 */
    drum_hit(t, 100);
    g = t->gate_n;
    seq_to(t, len + 8u * CTL);                         /* just past step 1's start (a rest) */
    check("acid seq: a rest leaves a live hit's gate open (it closes by its own GATE)",
          (t->v[0].x[2] & 1) && t->gate_n != 0u && t->gate_n != GATE_HELD);
    seq_to(t, at + g + 2u * CTL);
    check("acid seq: ... and the live hit's gate closes at its GATE", !(t->v[0].x[2] & 1) && t->gate_n == 0u);
    t = acid_seq();
    t->step[1].tie = 1;                                /* a TIE after an empty step: it plays nothing */
    seq_to(t, at);
    drum_hit(t, 100);
    seq_to(t, at + g + 2u * CTL);
    check("acid seq: a TIE does not extend a live hit (its gate closes at GATE, inside the TIE step)",
          !(t->v[0].x[2] & 1) && t->gate_n == 0u);
}

static void test_hit_gate(void)
{
    track_t *t = acid();
    uint32_t i, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    song.playing = 0;
    drum_hit(t, 100);                                  /* as midi_block / keyboard_block (input_hit) */
    memset(buf, 0, len * sizeof *buf);
    for (i = 0; i + CTL <= len; i += CTL) {
        seq_tick(t, CTL);
        dm_render_n(t, buf + i, CTL);
    }
    check("acid: a MIDI / key hit ends within one step, no sequencer step (its gate closes at GATE)",
          rms(buf + len / 8, len / 4) > 200.0 && rms(buf + len * 3 / 4, len / 8) < 20.0 && !t->v[0].active &&
          t->gate_n == 0u);
}

/* Review Focus 1: a song's row change restarts every track: an ACID gate held into it closes (a TIE at the new row's
 * step 0 holds nothing) */
static step_t ROW[2][NSTEP], NONE[NSTEP];
static void test_row_change(void)
{
    track_t *t = acid();
    uint32_t s, k, i;
    memset(ROW, 0, sizeof ROW);
    memset(NONE, 0, sizeof NONE);
    ROW[0][15].on = 1;
    ROW[0][15].slide = 1;                              /* row 1's last step slides into row 2 */
    ROW[1][0].tie = 1;
    song.g[G_BPM] = 120;
    for (s = 0; s < 2u; s++)
        for (k = 0; k < NTRK; k++) {
            chain.src[s].step[k] = k ? NONE : ROW[s];   /* track 0 only */
            chain.src[s].m = &MOTION_NONE;
            chain.src[s].timing[k][0] = 16;
            chain.src[s].timing[k][1] = 2;
            chain.src[s].model[k] = (uint8_t)trk[k].p[P_MODEL];
        }
    chain.cfg.count = 2;
    chain.cfg.row[0].slot = 0;
    chain.cfg.row[0].repeat = 1;
    chain.cfg.row[1].slot = 1;
    chain.cfg.row[1].repeat = 1;
    chain.armed = 1;
    transport_req = 1;
    for (i = 0; i < 4u * 22050u - 2000u; i += CTL) /* just before row 2 */
        render_mix(0, 0, CTL);
    check("acid: row 1's last step slides (its gate held)", t->v[0].active && (t->v[0].x[2] & 1) &&
          t->gate_n == GATE_HELD);
    for (; i < 4u * 22050u + 2000u; i += CTL)          /* into row 2's step 0 (a TIE) */
        render_mix(0, 0, CTL);
    check("acid: a song's row change closes the held gate (the next row's TIE holds nothing)",
          chain.row == 1u && !(t->v[0].x[2] & 1) && t->gate_n == 0u);
    seq_stop();
}

/* the voice cap (DRUM_MAXV) steals the oldest voice; a TIE keeps the ACID voice and its age, so a held note would
 * be the oldest: it goes last. The heavy kit (BENCH heavy+2acid) with 8-step ties on 7 and 8 over 4 bars: every held
 * note sounds through its tie (the voice active, the same age, until the next note) */
static void test_steal_held(void)
{
    uint32_t i, k, s, cuts = 0, notes = 0, open = 0, blocks = 0, age[2], po[2];
    host_init();
    bench_setup(BENCH_MORE + 5u);
    for (k = 6; k < 8u; k++)
        for (s = 0; s < 16u; s++) {
            memset(&trk[k].step[s], 0, sizeof(step_t));
            trk[k].step[s].on = s % 8u == 0u;
            trk[k].step[s].tie = s % 8u != 0u;
            trk[k].step[s].nt = -24;
        }
    transport_req = 1;
    for (i = 0; i < 4u * 2u * FS; i += CTL) {         /* 4 bars at 120 BPM */
        for (k = 0; k < 2u; k++) {
            dvoice_t *v = &trk[6 + k].v[0];
            po[k] = v->active && (v->x[2] & 1);
            age[k] = v->age;
        }
        render_mix(0, 0, CTL);
        for (k = 0; k < 2u; k++) {
            dvoice_t *v = &trk[6 + k].v[0];
            if (po[k] && !v->active)
                cuts++;
            if (v->active && v->age != age[k])
                notes++;
            open += v->active && (v->x[2] & 1);
            blocks++;
        }
    }
    check("acid: the voice cap does not cut a held ACID note (heavy kit, 8-step ties: no cut, the gate open)",
          cuts == 0u && notes == 2u * 4u * 2u && open > blocks * 99u / 100u);
}

/* the overload shedding (audio.c drum_shed): a drum voice goes before an older held ACID note; with nothing else
 * sounding, the held note goes */
static void test_shed_held(void)
{
    track_t *t = acid();
    drum_play(t, 96, 0);                               /* the oldest, its gate open */
    drum_play(&trk[1], 96, 0);
    drum_shed();
    check("acid: overload shedding takes a drum voice before a held ACID note", t->v[0].active && !trk[1].v[0].active);
    drum_shed();
    check("acid: ... and the held note when nothing else sounds", !t->v[0].active);
}

/* the sequencer's corner cases (review): RATCH + SLIDE, TIE + SLIDE, a TIE at step 0 after the wrap, a mute */
static void test_seq_rat_slide(void)                 /* the last ratchet hit alone holds the gate, the next glides */
{
    track_t *t = acid_seq();
    uint32_t age, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    int hit1_counts, hit2_held;
    t->step[0].on = 1;
    t->step[0].rat = 1;                                /* 2 hits */
    t->step[0].slide = 1;
    t->step[1].on = 1;
    t->step[1].nt = 7;
    seq_to(t, 2u * CTL);                               /* just after hit 1 */
    hit1_counts = (t->v[0].x[2] & 1) && t->gate_n != 0u && t->gate_n != GATE_HELD;
    seq_to(t, len / 2u + 3u * CTL);                    /* hit 2 */
    hit2_held = t->rat_k == 2u && t->gate_n == GATE_HELD;
    age = t->v[0].age;
    seq_to(t, len + len / 4u);                         /* into step 1 */
    check("acid seq: RATCH + SLIDE: hit 1's gate counts, the last hit's is held, the next step glides (same voice)",
          hit1_counts && hit2_held && t->v[0].active && t->v[0].age == age && t->v[0].x[1] == (60 + 7) * 16);
}

static void test_seq_tie_slide(void)                 /* a TIE with SLIDE: the step after the tie glides */
{
    track_t *t = acid_seq();
    uint32_t age, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    t->step[0].on = 1;
    t->step[1].tie = 1;
    t->step[1].slide = 1;
    t->step[2].on = 1;
    t->step[2].nt = 12;
    seq_to(t, len / 4u);
    age = t->v[0].age;
    seq_to(t, len + len * 3u / 4u);                    /* late in the TIE */
    check("acid seq: TIE + SLIDE holds the note and sets slide_in", t->slide_in && t->gate_n == GATE_HELD &&
          (t->v[0].x[2] & 1));
    seq_to(t, 2u * len + len / 4u);                    /* step 2 */
    check("acid seq: ... the step after the tie glides (same voice, the new target)",
          t->v[0].active && t->v[0].age == age && t->v[0].x[1] == (60 + 12) * 16);
}

static void test_seq_tie_wrap(void)                  /* LEN 4: step 3 ON, step 0 TIE: held across the wrap */
{
    track_t *t = acid_seq();
    uint32_t age, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    int held3;
    t->p[P_SLEN] = 4;
    t->step[3].on = 1;
    t->step[0].tie = 1;
    t->step[0].nt = 5;                                 /* a TIE's own note is ignored */
    seq_to(t, 3u * len + len * 3u / 4u);               /* step 3, past its GATE */
    held3 = t->v[0].active && (t->v[0].x[2] & 1) && t->gate_n == GATE_HELD;
    age = t->v[0].age;
    seq_to(t, 4u * len + len * 3u / 4u);               /* step 0 (the TIE), 3/4 through */
    check("acid seq: a TIE at step 0 holds the last step's note across the wrap",
          held3 && t->v[0].active && (t->v[0].x[2] & 1) && t->v[0].age == age && t->v[0].x[1] == 60 * 16);
    seq_to(t, 5u * len + len / 4u);                    /* step 1, a rest */
    check("acid seq: ... and the rest after it closes the gate", !(t->v[0].x[2] & 1) && t->gate_n == 0u);
}

static void test_seq_mute(void)                      /* a mute (track_mute_toggle: panic_req) ends a held note */
{
    track_t *t = acid();
    uint32_t k, i, len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    int held;
    for (k = 0; k < NTRK; k++)
        memset(trk[k].step, 0, sizeof trk[k].step);
    t->step[0].on = 1;
    t->step[0].slide = 1;                              /* held into step 1 */
    transport_req = 1;
    for (i = 0; i < len * 3u / 4u; i += CTL)
        render_mix(0, 0, CTL);
    held = t->v[0].active && (t->v[0].x[2] & 1) && t->gate_n == GATE_HELD && t->slide_in;
    t->p[P_MUTE] = 1;
    panic_req |= 1u;
    render_mix(0, 0, 2u * CTL);
    check("acid seq: a mute cuts a held note (no voice, gate_n 0, slide_in 0)",
          held && !t->v[0].active && t->gate_n == 0u && !t->slide_in);
    seq_stop();
}

/* Review Focus 2 (plan B): a live hit (MIDI, the keys: C4) between a ratchet's hits leaves the roll's note: its later
 * hits play the step's note */
static void test_rat_note_live(void)
{
    track_t *t = acid_seq();
    uint32_t len = (uint32_t)step_samples(t, div_period((uint32_t)t->p[P_SDIV], 0), 0);
    t->p[P_MODEL] = DM_K808;                           /* a drum */
    t->model = DM_K808;
    t->step[0].on = 1;
    t->step[0].rat = 3;                                /* 4 hits */
    t->step[0].nt = 5;
    seq_to(t, 2u * CTL);                               /* after hit 1 */
    drum_hit(t, 100);                                  /* a live hit, C4 */
    seq_to(t, len * 7u / 8u + CTL);                    /* hits 2..4 */
    check("seq: a live hit during a drum's ratchet: the roll's later hits keep the step's note",
          t->rat_k == 4u && t->nt == 5);
    t = acid_seq();                                    /* ACID */
    t->step[0].on = 1;
    t->step[0].rat = 1;                                /* 2 hits */
    t->step[0].nt = 5;
    seq_to(t, 2u * CTL);
    drum_hit(t, 100);
    seq_to(t, len / 2u + 3u * CTL);                    /* hit 2 */
    check("acid seq: a live hit during a ratchet: hit 2 plays the step's note (F4), not C4",
          t->rat_k == 2u && t->v[0].x[1] == (60 + 5) * 16);
}

/* PRIO (spec 2026-10-11 §10): a held ACID note still goes last, on a LOW track too; between two held notes the LOW
 * track's goes first */
static void test_shed_held_prio(void)
{
    track_t *t = acid();
    t->p[P_PRIO] = 1;                                  /* LOW */
    drum_play(t, 96, 0);                               /* the oldest, its gate open */
    drum_play(&trk[1], 96, 0);                         /* a HIGH drum voice */
    drum_shed();
    check("acid: a held ACID note on a LOW track still goes after a HIGH drum voice", t->v[0].active && !trk[1].v[0].active);
    t = acid();                                        /* two held notes: track 1 HIGH (older), track 2 LOW */
    drum_set_model(&trk[1], DM_ACID);
    trk[1].p[P_PRIO] = 1;
    drum_play(t, 96, 0);
    drum_play(&trk[1], 96, 0);
    drum_shed();
    check("acid: of two held notes the LOW track's goes first", t->v[0].active && !trk[1].v[0].active);
}

int main(void)
{
    test_pitch();
    test_gate();
    test_slide();
    test_accent();
    test_self_osc();
    test_alias();
    test_extremes();
    test_level();
    test_slope();
    test_wow();
    test_slide_unaccent();
    test_self_osc_far();
    test_cutoff_smooth();
    test_level_ramp();
    test_seq_gate();
    test_seq_slide();
    test_seq_tie();
    test_seq_rest();
    test_seq_prob_tie();
    test_seq_stop_closes();
    test_seq_drum_tie();
    test_hit_gate();
    test_live_hit_playing();
    test_row_change();
    test_steal_held();
    test_shed_held();
    test_shed_held_prio();
    test_seq_rat_slide();
    test_seq_tie_slide();
    test_seq_tie_wrap();
    test_seq_mute();
    test_rat_note_live();
    printf(fails ? "acid: %d FAILED\n" : "acid: all checks passed\n", fails);
    return fails != 0;
}
