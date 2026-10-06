/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Sound pack (docs/superpowers/specs/2026-10-06-sound-pack-design.md): BASS+, SPRING and the slow divisions against
 * upstream 1.0.2's code (sound_pack_ref.h), and today's sound kept bit for bit: HASH_FLAT / HASH_LOWCUT are the
 * kit below rendered by the build before the sound pack (trs-midi 08866e0 .. bf98902). */
#include "drum_host.h"
#include <stdlib.h>
#include <math.h>
#include "sound_pack_ref.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t fnv(const int32_t *x, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)x[i]) * 16777619u;
    return h;
}

#define KIT_N (2u * 16u * (FS * 60 / 120 / 4) / CTL * CTL)   /* 2 bars at 120 BPM */
static int32_t KL[KIT_N], KR[KIT_N];
/* kick, snare (REV), clap (REV + CHO), hats (DLY), 2 bars, every bus and the master in use */
static uint32_t kit_hash(uint32_t lowcut)
{
    uint32_t i, k;
    host_init();
    fx_lowcut = (uint8_t)lowcut;
    for (i = 0; i < 4u; i++)
        for (k = 0; k < 16u; k++)
            trk[i].step[k].on = k % (i + 2u) == 0;
    trk[1].p[P_REV] = 90;
    trk[2].p[P_REV] = 60;
    trk[2].p[P_CHOR] = 70;
    trk[3].p[P_DLY] = 80;
    transport_req = 1;
    render_mix(KL, KR, KIT_N);
    return fnv(KL, KIT_N) ^ fnv(KR, KIT_N) * 31u;
}

static double sine(double f, uint32_t i, double amp) { return amp * sin(2.0 * M_PI * f * i / FS); }

/* spk_bass = upstream's, sample for sample: a 55 Hz tone switching on and off every 1/4 s, plus noise, 4 s */
static void test_bass_port(void)
{
    uint32_t i, rng = 1, ok = 1;
    host_init();
    ref_bass_reset();
    for (i = 0; i < 4u * FS; i++) {
        int32_t x;
        rng = rng * 1664525u + 1013904223u;
        x = (int32_t)(sine(55.0, i, 12000.0) * (double)((i / (FS / 4u)) & 1u)) + (int32_t)(rng >> 20) - 2048;
        ok &= spk_bass(x) == ref_spk_bass(x);
    }
    check("BASS+: spk_bass gives upstream 1.0.2's samples (tone bursts + noise, 4 s)", ok);
}

/* #42: BASS+ (its 220 Hz high-pass + spk_bass) against the same high-pass alone, 300..600 Hz: within 0.5 dB */
static void test_bass_mids(void)
{
    uint32_t ok = 1;
    double f;
    for (f = 300.0; f <= 600.0; f += 50.0) {
        int32_t l1 = 0, l2 = 0, e1 = 0, e2 = 0, m1 = 0, m2 = 0, f1 = 0, f2 = 0;
        double s0 = 0, s1 = 0, db;
        uint32_t i;
        host_init();
        for (i = 0; i < 2u * FS; i++) {
            int32_t x = (int32_t)sine(f, i, 8000.0), b = spk_bass(x);
            int32_t y1 = lowcut1(lowcut1(x, &l1, &e1, 5), &l2, &e2, 5) + b;
            int32_t y0 = lowcut1(lowcut1(x, &m1, &f1, 5), &m2, &f2, 5);
            if (i >= FS) {
                s0 += (double)y0 * y0;
                s1 += (double)y1 * y1;
            }
        }
        db = 10.0 * log10(s1 / s0);
        if (db < -0.5) {
            printf("     %.0f Hz: %+.2f dB\n", f, db);
            ok = 0;
        }
    }
    check("BASS+ (#42): 300..600 Hz lose less than 0.5 dB", ok);
}

/* a 60 Hz bass comes out as its harmonics: over 30 % of the input level, the 60 Hz itself under 20 % of the power */
static void test_bass_harmonics(void)
{
    double re = 0, im = 0, so = 0, si = 0, fund;
    uint32_t i;
    host_init();
    for (i = 0; i < 2u * FS; i++) {
        int32_t x = (int32_t)sine(60.0, i, 16000.0), y = spk_bass(x);
        if (i >= FS) {
            si += (double)x * x;
            so += (double)y * y;
            re += y * cos(2.0 * M_PI * 60.0 * i / FS);
            im += y * sin(2.0 * M_PI * 60.0 * i / FS);
        }
    }
    fund = 2.0 * (re * re + im * im) / FS / FS / (so / FS);
    check("BASS+: a 60 Hz bass becomes harmonics (level > 30 % of the input, fundamental < 20 % of the power)",
          sqrt(so / si) > 0.3 && fund < 0.2);
}

/* the master: BASS+ adds harmonics LOWCUT does not have; FLAT and LOWCUT unchanged (main: the kit hashes) */
static void test_bass_master(void)
{
    double h[3];
    uint32_t mode, i;
    for (mode = 1; mode <= 2u; mode++) {
        double re = 0, im = 0, so = 0;
        host_init();
        fx_lowcut = (uint8_t)mode;
        for (i = 0; i < 2u * FS; i++) {
            int32_t l = (int32_t)sine(60.0, i, 12000.0), r = l;
            master_out(&l, &r);
            if (i >= FS) {
                so += (double)l * l;
                re += l * cos(2.0 * M_PI * 60.0 * i / FS);
                im += l * sin(2.0 * M_PI * 60.0 * i / FS);
            }
        }
        h[mode] = so / FS - 2.0 * (re * re + im * im) / FS / FS;   /* the power that is not 60 Hz */
    }
    fx_lowcut = 0;
    check("BASS+ on the master: a 60 Hz bass gets 10x the harmonic power LOWCUT gives it", h[2] > 10.0 * h[1]);
}

/* rev_spring = upstream's, sample for sample: a burst send, 6 s, SIZE and DAMP turned twice while it rings */
static void test_spring_matches_upstream(void)
{
    static int32_t in[CTL], a[CTL], b[CTL];
    uint32_t blk, i, ok = 1;
    int32_t peak = 0;
    host_init();
    ref_spring_reset();
    song.g[G_RSIZE] = 90;
    song.g[G_RDAMP] = 60;
    for (blk = 0; blk < 6u * FS / CTL; blk++) {
        if (blk == 2u * FS / CTL)
            song.g[G_RSIZE] = 10, song.g[G_RDAMP] = 120;
        if (blk == 4u * FS / CTL)
            song.g[G_RSIZE] = 127, song.g[G_RDAMP] = 0;
        for (i = 0; i < CTL; i++) {
            in[i] = blk < 40u ? (int32_t)((i * 2654435761u) >> 16) - 32768 : 0;
            a[i] = b[i] = 0;
        }
        rev_spring(in, a, CTL);
        ref_rev_spring(in, b, CTL, song.g[G_RSIZE], song.g[G_RDAMP]);
        for (i = 0; i < CTL; i++) {
            ok &= a[i] == b[i];
            peak = a[i] > peak ? a[i] : -a[i] > peak ? -a[i] : peak;
        }
    }
    check("SPRING: rev_spring gives upstream 1.0.2's samples (SIZE / DAMP turned while it rings)", ok);
    check("SPRING: bounded (|out| < 2^20) while SIZE glides", peak < (1 << 20));
}

/* after the send stops, SPRING's output reaches exactly 0 (no offset held in its loop): SIZE 0, 64, 127, 10 s */
static void test_spring_silence(void)
{
    static int32_t in[CTL], out[CTL];
    static const int16_t SIZES[3] = {0, 64, 127};
    uint32_t z, blk, i, ok = 1;
    for (z = 0; z < 3u; z++) {
        int32_t last = 0;
        host_init();
        song.g[G_RSIZE] = SIZES[z];
        song.g[G_RDAMP] = 60;
        for (blk = 0; blk < 10u * FS / CTL; blk++) {
            for (i = 0; i < CTL; i++) {
                in[i] = blk < 4u && i == 0u ? 600000 : 0;
                out[i] = 0;
            }
            rev_spring(in, out, CTL);
            for (i = 0; i < CTL; i++)
                last |= blk >= 9u * FS / CTL ? out[i] : 0;
        }
        ok &= last == 0;
    }
    check("SPRING: silent (exactly 0) once its tail has died (SIZE 0 / 64 / 127)", ok);
}

/* review focus 1 / 2: TYPE switched while a steady 200 Hz send keeps coming, straight into the buses (no drums: a
 * hit's attack would hide a click). A 200 Hz tone moves ~3 % of its level a sample; a model cut off hard would jump
 * by the whole level. In the switch block the old model fades out over the block, and the new one is silent for its
 * first ~1100 samples (its delay lines), so the 3 blocks after the switch may not step more than the steady tone. */
static void test_switch_while_sending(void)
{
    static int32_t z[CTL], in[CTL], w[CTL];
    uint32_t blk, i, ok = 1, sw, t = 0, clear = 1;
    host_init();
    for (sw = 0; sw < 2u; sw++) {                    /* ROOM -> SPRING, then SPRING -> ROOM */
        int32_t before = 0, at = 0, prev = 0, d;
        for (blk = 0; blk < 203u; blk++) {
            if (blk == 200u)
                song.g[G_RTYPE] = sw ? 0 : 1;
            for (i = 0; i < CTL; i++, t++) {
                in[i] = (int32_t)sine(200.0, t, 20000.0);
                z[i] = 0;
            }
            fx_buses(z, z, in, w, CTL);
            for (i = 0; i < CTL; i++) {
                d = w[i] - prev;
                d = d < 0 ? -d : d;
                prev = w[i];
                if (blk >= 100u && blk < 200u)
                    before = d > before ? d : before;
                if (blk >= 200u)
                    at = d > at ? d : at;
            }
            if (blk == 200u)
                for (i = 0; i < sizeof rev_comb / 2u; i++)
                    clear &= rev_comb[i] == 0;
        }
        if (at > 2 * before + 64)
            printf("     switch %u: step %d after, %d before\n", sw, at, before);
        ok &= at <= 2 * before + 64 && before > 0 && fx.rtype == (uint8_t)song.g[G_RTYPE];
    }
    check("TYPE switched while sending (ROOM -> SPRING -> ROOM): no click, the new model runs", ok);
    check("TYPE switch: the shared reverb buffer is cleared in the switch block", clear);
}


#define HASH_FLAT 0xb35154deu
#define HASH_LOWCUT 0xd5959edeu

int main(void)
{
    if (getenv("SOUND_PACK_RECORD")) {
        printf("HASH_FLAT 0x%08xu\nHASH_LOWCUT 0x%08xu\n", kit_hash(0), kit_hash(1));
        return 0;
    }
    check("today's sound: the kit (ROOM reverb, delay, chorus) with FLAT, bit for bit", kit_hash(0) == HASH_FLAT);
    check("today's sound: the kit with LOWCUT, bit for bit", kit_hash(1) == HASH_LOWCUT);
    test_bass_port();
    test_bass_mids();
    test_bass_harmonics();
    test_bass_master();
    test_spring_matches_upstream();
    test_spring_silence();
    test_switch_while_sending();
    printf(fails ? "sound_pack_test: %d FAILED\n" : "sound_pack_test: all passed\n", fails);
    return fails ? 1 : 0;
}
