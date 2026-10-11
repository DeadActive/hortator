/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* METRO (spec 2026-10-11-fixes §8): GLOBAL 3/3's metronome. While playing (own clock or following one), a click on
 * every quarter note (Grids' clock: gclk.cnt % 4), the bar's first at a higher pitch, mixed after the FX layer at the MASTER level
 * (fx.c metro_block). Tested through the whole mix (render_mix): the exact spacing, the pitch, the levels, nothing
 * stopped, an external clock's beats, swing, and OFF bit-identical to 0.16.0.
 *   metro_test [WAV]   also writes a demo (a beat at 120 BPM: 2 bars each LOW, MID, HIGH) to WAV */
#include "drum_host.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

#define MAXN (12u * FS / CTL * CTL)
static int32_t L[MAXN], R[MAXN];

static uint32_t fnv(const int32_t *x, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)x[i]) * 16777619u;
    return h;
}

/* a simple beat: 909 kick on the quarters, snare on 2 and 4, closed hats on the 8ths (reverb on the snare) */
static void beat(void)
{
    uint32_t k;
    host_init();
    drum_set_model(&trk[0], DM_K909);
    drum_set_model(&trk[1], DM_S909);
    drum_set_model(&trk[2], DM_HATC);
    for (k = 0; k < 16u; k++) {
        trk[0].step[k].on = (k & 3u) == 0u;
        trk[1].step[k].on = (k & 7u) == 4u;
        trk[2].step[k].on = (k & 1u) == 0u;
    }
    trk[1].p[P_REV] = 40;
    song.g[G_BPM] = 120;
}

static void empty(void)                              /* no steps: the output is the clicks only */
{
    host_init();
    song.g[G_BPM] = 120;
}

/* the clicks in x[0..n): the sample each begins at (its first sample above thr, the sine's 0 one before it, after at
 * least 2 000 samples at or under thr), up to max */
#define QUIET 32                                     /* -60 dBFS: a click's DC (the master's DC blocker) settles */
static uint32_t onsets(const int32_t *x, uint32_t n, uint32_t *on, uint32_t max, int32_t thr)
{
    uint32_t i, quiet = 2000u, c = 0;
    for (i = 0; i < n; i++) {
        if (abs(x[i]) <= thr) {
            quiet++;
            continue;
        }
        if (quiet >= 2000u && c < max)
            on[c++] = i - 1u;
        quiet = 0;
    }
    return c;
}

static int32_t peak(const int32_t *x, uint32_t n)
{
    int32_t p = 0;
    uint32_t i;
    for (i = 0; i < n; i++)
        p = abs(x[i]) > p ? abs(x[i]) : p;
    return p;
}

static double freq(const int32_t *x)                 /* zero crossings over the click's first 400 samples */
{
    uint32_t i, z = 0;
    for (i = 2; i < 400u; i++)
        z += (x[i] > 0) != (x[i - 1] > 0);
    return z * (double)FS / (2.0 * 398.0);
}

static double dbfs(int32_t p) { return 20.0 * log10(p / 32767.0); }

/* 0.16.0's render of beat() at 120 BPM, 4 s, through the whole mix (L then R): METRO OFF keeps it */
#define OFF_HASH_L 0x70ce896au
#define OFF_HASH_R 0x70ce896au

static void test_off_identical(void)
{
    uint32_t n = 4u * FS / CTL * CTL, hl, hr;
    beat();
    fx_metro = 0;
    transport_req = 1;
    render_mix(L, R, n);
    hl = fnv(L, n);
    hr = fnv(R, n);
    if (hl != OFF_HASH_L || hr != OFF_HASH_R)
        printf("     OFF render %08x %08x, 0.16.0 %08x %08x\n", hl, hr, OFF_HASH_L, OFF_HASH_R);
    check("METRO OFF: the beat's whole-mix render is bit-identical to 0.16.0", hl == OFF_HASH_L && hr == OFF_HASH_R);
}

/* MID, 120 BPM: a click every 22 050 samples exactly, the first in PLAY's block; every 4th (the bar's) higher */
static void test_spacing_pitch(void)
{
    uint32_t n = 10u * FS / CTL * CTL, on[64], c, k, exact = 1, pitch = 1;
    empty();
    fx_metro = 2;
    render_mix(L, R, CTL * 10u);                     /* stopped: nothing */
    check("stopped: no click (METRO MID)", peak(L, CTL * 10u) == 0 && peak(R, CTL * 10u) == 0);
    transport_req = 1;
    render_mix(L, R, n);
    c = onsets(L, n, on, 64, QUIET);
    for (k = 1; k < c; k++)
        exact &= on[k] - on[0] == k * 22050u;
    check("120 BPM: 20 clicks in 10 s", c == 20u);
    check("120 BPM: one every 22 050 samples, exactly (the beat's sample in the block)", c > 1u && exact);
    check("the first click in PLAY's block (with the steps of beat 1)", c && on[0] < CTL);
    check("left and right the same", !memcmp(L, R, n * sizeof L[0]));
    for (k = 0; k < c; k++) {
        double f = freq(L + on[k]);
        if (k < 8u)
            printf("     click %u at %u: %.0f Hz, peak %.1f dBFS\n", k, on[k], f, dbfs(peak(L + on[k], 2000u)));
        pitch &= (k & 3u) == 0u ? fabs(f - 1600.0) < 80.0 : fabs(f - 1000.0) < 80.0;
    }
    check("the bar's first click at 1.6 kHz, the other beats at 1 kHz", c && pitch);
    {
        uint32_t len = 0, i;
        for (i = on[1]; i < on[1] + 4000u; i++)
            if (abs(L[i]) > QUIET)
                len = i - on[1];
        printf("     a click lasts %u samples (%.1f ms) to -60 dBFS\n", len, len * 1000.0 / FS);
        check("the click is short: under -60 dBFS before 25 ms, -40 dB under its peak by 15 ms",
              len < 25u * FS / 1000u && peak(L + on[1] + 15u * FS / 1000u, 400u) * 100 < peak(L + on[1], 2000u));
    }
}

/* LOW MID HIGH at full MASTER: about -24 -15 -6 dBFS peak (+-1 dB), both pitches; OFF none. The MASTER level turns
 * the click down with the mix: half MASTER, 6 dB lower */
static void test_levels(void)
{
    static const double DB[4] = {0.0, -24.0, -15.0, -6.0};
    uint32_t m, n = 3u * FS / CTL * CTL, on[8], c;
    for (m = 0; m < 4u; m++) {
        int ok;
        empty();
        song.master_q12 = MASTER_FULL;
        fx_metro = (uint8_t)m;
        transport_req = 1;
        render_mix(L, R, n);
        c = onsets(L, n, on, 8, QUIET);
        if (!m) {
            check("METRO OFF: no click", c == 0u && peak(L, n) == 0);
            continue;
        }
        {
            double b = dbfs(peak(L + on[0], 2000u)), q = dbfs(peak(L + on[1], 2000u));
            char what[96];
            ok = c == 6u && fabs(b - DB[m]) <= 1.0 && fabs(q - DB[m]) <= 1.0;
            printf("     %s: bar %.1f dBFS, beat %.1f dBFS\n", N_METRO[m], b, q);
            snprintf(what, sizeof what, "METRO %s: about %.0f dBFS peak (+-1 dB), the bar's and the others", N_METRO[m],
                     DB[m]);
            check(what, ok);
        }
    }
    {                                                /* HIGH at half MASTER: 6 dB lower (+-1 dB) */
        double h;
        empty();
        song.master_q12 = MASTER_FULL / 2;
        fx_metro = 3;
        transport_req = 1;
        render_mix(L, R, n);
        c = onsets(L, n, on, 8, QUIET);
        h = c ? dbfs(peak(L + on[1], 2000u)) : -200.0;
        printf("     HIGH at half MASTER: %.1f dBFS\n", h);
        check("the MASTER level turns the click down: half MASTER, about -12 dBFS at HIGH", c == 6u && fabs(h + 12.0) <= 1.0);
    }
}

/* nothing stopped: STOP ends the clicks (the last one rings out its 30 ms) */
static void test_stop(void)
{
    uint32_t n = 2u * FS / CTL * CTL;
    empty();
    fx_metro = 3;
    transport_req = 1;
    render_mix(L, R, FS / 2u / CTL * CTL + CTL * 10u);   /* beat 2 has begun */
    transport_req = 2;
    render_mix(L, R, n);
    check("STOP: no click after it (2 s, HIGH; the last one's tail under -60 dBFS after 30 ms)",
          peak(L + 30u * FS / 1000u, n - 30u * FS / 1000u) <= QUIET);
    check("STOP: not playing", !song.playing);
}

/* the kick on every quarter (MASTER low: the limiter stays out): each click begins in the block its beat's kick is
 * struck in, every beat_samples() exactly (swing moves the 8ths and 16ths, never the quarters) */
static int32_t M[MAXN];
static void test_with_steps(uint32_t swing, uint32_t bpm)
{
    uint32_t b, nb = 8u * FS / CTL, hb[64], h = 0, on[64], c, k, exact = 1, same, a0, run;
    char what[128];
    for (run = 0; run < 2u; run++) {                 /* METRO MID, then OFF: the difference is the clicks */
        beat();
        song.master_q12 = 512;
        song.g[G_BPM] = (int16_t)bpm;
        song.g[G_SWING] = (int16_t)swing;
        fx_metro = run ? 0 : 2;
        transport_req = 1;
        a0 = hit_age(&trk[0]);
        for (b = 0; b < nb; b++) {
            render_mix((run ? L : M) + b * CTL, R + b * CTL, CTL);
            if (!run && hit_age(&trk[0]) != a0) {
                a0 = hit_age(&trk[0]);
                if (h < 64u)
                    hb[h++] = b;
            }
        }
    }
    for (b = 0; b < nb * CTL; b++)
        M[b] -= L[b];
    c = onsets(M, nb * CTL, on, 64, 50);           /* (MASTER 512: the click ~-33 dBFS; both pitches cross 50 at their 2nd sample) */
    for (k = 1; k < c; k++)
        exact &= on[k] - on[0] == k * beat_samples();
    same = c == h;
    for (k = 0; k < c && k < h; k++)
        same &= on[k] / CTL == hb[k];
    snprintf(what, sizeof what, "%u BPM, SWING %u: a click every %u samples exactly, each in its kick's block (%u clicks)",
             bpm, swing, beat_samples(), c);
    check(what, c >= 8u && exact && same);
}

/* following a clock (CLK USB): the clicks come on its beats (each in the block of its 24th pulse, as the steps) */
static void test_external(void)
{
    uint64_t now = 0;
    double next = 100.0;
    uint32_t pulses = 0, b, pblk[400], c, on[64], k, ok = 1;
    empty();
    song.g[G_CLOCK] = 1;
    fx_metro = 2;
    render_mix(0, 0, CTL);                           /* the mode change settles */
    now = CTL;
    fm1_ms = (uint32_t)(now * 1000u / FS);
    midi_enqueue(0x0Fu | 0xFAu << 8, 1u);            /* Start */
    for (b = 0; b < 8u * FS / CTL; b++) {
        while (next < (double)(now + CTL)) {
            fm1_ms = (uint32_t)(next * 1000.0 / FS);
            midi_enqueue(0x0Fu | 0xF8u << 8, 1u);    /* Clock, 120 BPM */
            if (pulses < 400u)
                pblk[pulses] = b;
            pulses++;
            next += FS * 60.0 / 120.0 / 24.0;
        }
        fm1_ms = (uint32_t)((now + CTL) * 1000u / FS);
        render_mix(L + b * CTL, R + b * CTL, CTL);
        now += CTL;
    }
    c = onsets(L, 8u * FS / CTL * CTL, on, 64, QUIET);
    for (k = 0; k < c; k++) {
        uint32_t want = pblk[24u * k];
        if (24u * k >= 400u || on[k] / CTL < want || on[k] / CTL > want + 2u) {
            printf("     click %u in block %u, its pulse in %u\n", k, on[k] / CTL, 24u * k < 400u ? want : 0u);
            ok = 0;
        }
    }
    check("CLK USB 120 BPM: a click per 24 pulses, each in its pulse's block (or 2 after, as the steps)", c >= 15u && ok);
    check("CLK USB: the bar's first higher", c >= 5u && freq(L + on[0]) > 1400.0 && freq(L + on[1]) < 1200.0 &&
                                               freq(L + on[4]) > 1400.0);
}

static void write_demo(const char *path)
{
    FILE *f = fopen(path, "wb");
    uint32_t bar = 4u * 22050u, total = 6u * bar, done = 0, i, v;
    uint16_t a = 1, ch = 2, ba = 4, bits = 16;
    uint32_t sr = FS, br = FS * 4;
    if (!f) {
        check("demo WAV written", 0);
        return;
    }
    fwrite("RIFF", 1, 4, f); v = 36 + total * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = total * 4; fwrite(&v, 4, 1, f);
    beat();
    transport_req = 1;
    while (done < total) {
        uint32_t n = total - done < CTL * 64u ? total - done : CTL * 64u;
        fx_metro = (uint8_t)(1u + done / (2u * bar));   /* 2 bars LOW, 2 MID, 2 HIGH */
        render_mix(L, R, n);
        for (i = 0; i < n; i++) {
            int16_t s[2] = {(int16_t)clamp(L[i], -32768, 32767), (int16_t)clamp(R[i], -32768, 32767)};
            fwrite(s, 2, 2, f);
        }
        done += n;
    }
    fclose(f);
    fx_metro = 0;
    printf("     demo: %s\n", path);
}

int main(int argc, char **argv)
{
    test_off_identical();
    test_spacing_pitch();
    test_levels();
    test_stop();
    test_with_steps(0, 120);
    test_with_steps(40, 120);
    test_with_steps(25, 97);
    test_external();
    if (argc > 1)
        write_demo(argv[1]);
    fx_metro = 0;
    printf(fails ? "metro_test: %d FAILED\n" : "metro_test: all passed\n", fails);
    return fails ? 1 : 0;
}
