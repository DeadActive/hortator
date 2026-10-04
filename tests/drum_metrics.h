/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M1-C fidelity metrics, one implementation for both sides: tests/drum_ref.cc measures the reference
 * (48 kHz) with it, tests/drum_fidelity.c measures ours (44.1 kHz). Everything is in time / Hz units, so
 * the two sample rates compare directly. Plain C (also compiles as C++). Signals: 1.0 = a reference
 * sample of 1.0 (ours are scaled back from the track output). */
#include <math.h>
#include <string.h>
#include <stdio.h>
#define DMM_SECONDS 1.5
#define DMM_NENV 150                   /* envelope windows over 1.5 s (10 ms; longer, see dmm_measure) */
#define DMM_NPITCH 30                  /* 50 ms pitch windows */
#define DMM_FFT 16384

typedef struct {
    int nenv;
    double env_db[DMM_NENV];           /* RMS per window, dB re 1.0 (-200 = silence) */
    double env_max;                    /* the loudest window */
    double win;                        /* envelope window, s */
    double peak_db;                    /* loudest 1 ms RMS window (noise-based: 40 ms), dB */
    double decay_s;                    /* end of the last window >= env_max - 40 dB */
    double centroid;                   /* Hz: power-weighted, 0..20 kHz, first 200 ms (noise-based: 4 x 50 ms, those >= loudest - 40 dB) */
    double pitch[DMM_NPITCH];          /* Hz per 50 ms window from zero-crossing periods, 0 = not measurable */
} dmm_t;

static double dmm_db(double x) { return x > 1e-10 ? 20.0 * log10(x) : -200.0; }

/* in-place radix-2 FFT (re, im), n a power of two */
static void dmm_fft(double *re, double *im, int n)
{
    int i, j, k, len;
    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double a = -2.0 * M_PI / len, wr = cos(a), wi = sin(a);
        for (i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                double t;
                re[i + k] = ur + vr; im[i + k] = ui + vi;
                re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                t = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = t;
            }
        }
    }
}

/* power-weighted centroid (Hz, 0..20 kHz) of y[a, a + len) with a Hann window */
static double dmm_centroid(const float *y, int a, int len, double sr)
{
    static double re[DMM_FFT], im[DMM_FFT];
    double num = 0.0, den = 0.0;
    int i;
    memset(re, 0, sizeof re);
    memset(im, 0, sizeof im);
    for (i = 0; i < len && i < DMM_FFT; i++)
        re[i] = y[a + i] * (0.5 - 0.5 * cos(2.0 * M_PI * i / len));
    dmm_fft(re, im, DMM_FFT);
    for (i = 1; i < DMM_FFT / 2; i++) {
        double f = i * sr / DMM_FFT, p = re[i] * re[i] + im[i] * im[i];
        if (f > 20000.0)
            break;
        num += f * p;
        den += p;
    }
    return den > 0.0 ? num / den : 0.0;
}

/* the audible band both sample rates share: a signal above 44.1 kHz is low-passed at 20 kHz first
 * (64-tap windowed sinc), so content a 44.1 kHz port cannot have does not count against it */
#define DMM_MAXN 80000
static const float *dmm_band(const float *y, int n, double sr)
{
    static float b[DMM_MAXN];
    static double tap[65], tap_sr;
    int i, k;
    if (sr <= 44100.0 || n > DMM_MAXN)
        return y;
    if (tap_sr != sr) {
        double fc = 20000.0 / sr;
        for (k = -32; k <= 32; k++)
            tap[k + 32] = (0.5 + 0.5 * cos(M_PI * k / 32.0)) * (k == 0 ? 2.0 * fc : sin(2.0 * M_PI * fc * k) / (M_PI * k));
        tap_sr = sr;
    }
    for (i = 0; i < n; i++) {
        double acc = 0.0;
        for (k = -32; k <= 32; k++)
            if (i - k >= 0 && i - k < n)
                acc += y[i - k] * tap[k + 32];
        b[i] = (float)acc;
    }
    return b;
}

/* y: n samples at sr from the hit; noisy: the noise-based models (40 ms windows); base_hz: the hit's base
 * pitch (0 = none): envelope windows span at least two of its periods, so a sub-bass tone measures its
 * envelope, not where in its cycle a window falls */
static void dmm_measure(const float *y, int n, double sr, int noisy, double base_hz, dmm_t *m)
{
    double win = noisy ? 0.040 : 0.010;
    int w, i, wlen, plen = (int)(sr * 0.050 + 0.5);
    double pk = 0.0;
    y = dmm_band(y, n, sr);
    memset(m, 0, sizeof *m);
    if (base_hz > 0.0 && 2.0 / base_hz > win)
        win = 2.0 / base_hz;
    wlen = (int)(sr * win + 0.5);
    m->win = win;
    m->nenv = (int)(DMM_SECONDS / win);
    m->env_max = -200.0;
    for (w = 0; w < m->nenv; w++) {
        double s = 0.0;
        for (i = 0; i < wlen && w * wlen + i < n; i++)
            s += (double)y[w * wlen + i] * y[w * wlen + i];
        m->env_db[w] = dmm_db(sqrt(s / wlen));
        if (m->env_db[w] > m->env_max)
            m->env_max = m->env_db[w];
    }
    {
        int l1 = (int)(sr * (noisy ? 0.040 : 0.001) + 0.5);   /* noise-based: 40 ms (a short window of noise is luck) */
        for (i = 0; i + l1 <= n; i += l1 / 2) {     /* 1 ms windows, half overlapping */
            double s = 0.0;
            int k;
            for (k = 0; k < l1; k++)
                s += (double)y[i + k] * y[i + k];
            if (s > pk)
                pk = s;
        }
        m->peak_db = dmm_db(sqrt(pk / l1));
    }
    for (w = 0; w < m->nenv; w++)
        if (m->env_db[w] >= m->env_max - 40.0)
            m->decay_s = (w + 1) * win;
    if (noisy) {                 /* the windows within 40 dB of the loudest: a tail far below hearing does not count (user, M1-C) */
        int l = (int)(sr * 0.05), k, nw = 0;
        double sum = 0.0;
        for (w = 0; w < 4; w++) {
            double e = 0.0;
            for (k = 0; k < l && w * l + k < n; k++)
                e += (double)y[w * l + k] * y[w * l + k];
            if (dmm_db(sqrt(e / l)) >= m->env_max - 40.0) {
                sum += dmm_centroid(y, w * l, l, sr);
                nw++;
            }
        }
        m->centroid = nw ? sum / nw : 0.0;
    } else {
        m->centroid = dmm_centroid(y, 0, (int)(sr * 0.2), sr);
    }
    for (w = 0; w < DMM_NPITCH; w++) {           /* rising zero crossings, interpolated; >= 2 per window */
        double first = -1.0, last = -1.0, s = 0.0;
        int c = 0, i0 = w * plen + 1 + (w ? 0 : (int)(sr * 0.010));   /* the first window from 10 ms: after the
                                                       * click / noise transient, whose crossings are luck (user, M1-C) */
        for (i = i0; i < (w + 1) * plen && i < n; i++)
            s += (double)y[i] * y[i];
        if (dmm_db(sqrt(s / plen)) < m->env_max - 40.0)
            continue;
        for (i = i0; i < (w + 1) * plen && i < n; i++)
            if (y[i - 1] < 0.0f && y[i] >= 0.0f) {
                double t = (i - 1 + y[i - 1] / (double)(y[i - 1] - y[i])) / sr;
                if (first < 0.0)
                    first = t;
                last = t;
                c++;
            }
        if (c >= 3)
            m->pitch[w] = (c - 1) / (last - first);
    }
}

/* tolerances: strict (target) and loose (fallback), spec §5 */
typedef struct { double env_db, env_floor, decay, centroid, pitch, peak_db; } dmm_tol_t;
static const dmm_tol_t DMM_STRICT = {1.5, 50.0, 0.10, 0.08, 0.03, 1.0};
static const dmm_tol_t DMM_LOOSE = {3.0, 40.0, 0.20, 0.15, 0.05, 2.0};

/* 1 = within tolerance; else 0 and why names the first metric that misses */
static int dmm_compare(const dmm_t *ref, const dmm_t *our, int kick, const dmm_tol_t *t, char *why, size_t wn)
{
    int w;
    double wsec = ref->win;
    if (fabs(our->peak_db - ref->peak_db) > t->peak_db) {
        snprintf(why, wn, "peak %.1f dB vs %.1f", our->peak_db, ref->peak_db);
        return 0;
    }
    for (w = 0; w < ref->nenv && w < our->nenv; w++)
        if (ref->env_db[w] > ref->env_max - t->env_floor && fabs(our->env_db[w] - ref->env_db[w]) > t->env_db) {
            snprintf(why, wn, "envelope at %.2f s: %.1f dB vs %.1f", w * wsec, our->env_db[w], ref->env_db[w]);
            return 0;
        }
    if (fabs(our->decay_s - ref->decay_s) > t->decay * ref->decay_s + wsec) {
        snprintf(why, wn, "decay %.3f s vs %.3f", our->decay_s, ref->decay_s);
        return 0;
    }
    if (ref->centroid > 0.0 && fabs(our->centroid / ref->centroid - 1.0) > t->centroid) {
        snprintf(why, wn, "brightness %.0f Hz vs %.0f", our->centroid, ref->centroid);
        return 0;
    }
    if (kick)
        for (w = 0; w < DMM_NPITCH; w++)
            if (ref->pitch[w] > 0.0 && our->pitch[w] > 0.0 && fabs(our->pitch[w] / ref->pitch[w] - 1.0) > t->pitch) {
                snprintf(why, wn, "pitch at %.2f s: %.1f Hz vs %.1f", w * 0.05, our->pitch[w], ref->pitch[w]);
                return 0;
            }
    return 1;
}
