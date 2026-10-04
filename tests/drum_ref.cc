// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// Host-only reference for the M1-C fidelity tests: renders Mutable Instruments' original drum code (MIT,
// fetched by tests/fetch_ref.sh into build/drum_ref/, never part of the firmware) at 48 kHz, with the
// engines' knob mappings (plaits/dsp/engine/{bass,snare,hi_hat}_drum_engine.cc @ 08460a6).
//   drum_ref grid FILE   the metrics (tests/drum_metrics.h) of every grid render (tests/drum_ref_grid.h)
//   drum_ref wav DIR     DIR/ref_NAME.wav: the hits of tests/drumsim.c (velocity 127 96 64 32, each knob low / high)
//   drum_ref selftest    the metrics accept a perfect port at 44.1 kHz and reject a 3 dB level error
//   drum_ref peaks       peak |out| of each model's default hit at velocity 127
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <vector>
#include "plaits/dsp/drums/analog_bass_drum.h"
#include "plaits/dsp/drums/synthetic_bass_drum.h"
#include "plaits/dsp/drums/analog_snare_drum.h"
#include "plaits/dsp/drums/synthetic_snare_drum.h"
#include "plaits/dsp/drums/hi_hat.h"
#include "plaits/dsp/fx/overdrive.h"
#include "plaits/dsp/engine/engine.h"
extern "C" {
#include "drum_ref_grid.h"
#include "drum_metrics.h"
}
using namespace plaits;

static std::vector<float> render(int m, int note, int decay, int tone, int chr, int vel, double seconds)
{
  const float f0 = NoteToFrequency((float)note), timbre = tone / 127.0f, morph = decay / 127.0f,
              harmonics = chr / 127.0f, accent = vel / 127.0f;
  const size_t n = (size_t)(seconds * kSampleRate);
  std::vector<float> out(n + kBlockSize);
  float temp[2 * kBlockSize], blk[kBlockSize];
  static AnalogBassDrum abd; static SyntheticBassDrum sbd; static Overdrive od;
  static AnalogSnareDrum asd; static SyntheticSnareDrum ssd;
  static HiHat<SquareNoise, SwingVCA, true, false> hh1;
  static HiHat<RingModNoise, LinearVCA, false, true> hh2;
  abd.Init(); sbd.Init(); od.Init(); asd.Init(); ssd.Init(); hh1.Init(); hh2.Init();
  stmlib::Random::Seed(0x21);
  const size_t idle = (size_t)(0.1 * kSampleRate) / kBlockSize;   // 0.1 s idle before the hit: interpolators and
                                                                   // filters settle, as in the running module
  for (size_t done = 0, b = 0; done < n; b++) {
    bool trig = b == idle;
    switch (m) {
    case 0: {
      float afm = std::min(harmonics * 4.0f, 1.0f);
      float sfm = std::max(std::min(harmonics * 4.0f - 1.0f, 1.0f), 0.0f);
      float drive = std::max(harmonics * 2.0f - 1.0f, 0.0f) * std::max(1.0f - 16.0f * f0, 0.0f);
      abd.Render(false, trig, accent, f0, timbre, morph, afm, sfm, blk, kBlockSize);
      od.Process(0.5f + 0.5f * drive, blk, kBlockSize);
      break;
    }
    case 1:
      sbd.Render(false, trig, accent, f0, timbre, morph, 0.4f - 0.25f * morph * morph,
                 std::min(harmonics * 2.0f, 1.0f), std::max(harmonics * 2.0f - 1.0f, 0.0f), blk, kBlockSize);
      break;
    case 2: asd.Render(false, trig, accent, f0, timbre, morph, harmonics, blk, kBlockSize); break;
    case 3: ssd.Render(false, trig, accent, f0, timbre, morph, harmonics, blk, kBlockSize); break;
    case 4: hh1.Render(false, trig, accent, f0, timbre, morph, harmonics, temp, temp + kBlockSize, blk, kBlockSize); break;
    default: hh2.Render(false, trig, accent, f0, timbre, morph, harmonics, temp, temp + kBlockSize, blk, kBlockSize); break;
    }
    if (b >= idle) {
      memcpy(&out[done], blk, sizeof blk);
      done += kBlockSize;
    }
  }
  out.resize(n);
  return out;
}

static void wav_hdr(FILE *f, uint32_t frames)
{
  uint32_t sr = 48000, br = sr * 2, d = frames * 2, r = 36 + d;
  uint16_t one = 1, two = 2, bits = 16;
  uint32_t sixteen = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&r, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
  fwrite(&one, 2, 1, f); fwrite(&one, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
  fwrite(&two, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&d, 4, 1, f);
}

int main(int argc, char **argv)
{
  if (argc >= 3 && !strcmp(argv[1], "grid")) {     // the reference metrics of every grid render
    FILE *f = fopen(argv[2], "wb");
    uint32_t hdr[4] = {0x31524D44u, REF_NMODELS, REF_NGRID, (uint32_t)sizeof(dmm_t)};   // "DMR1"
    if (!f) { perror(argv[2]); return 1; }
    fwrite(hdr, sizeof hdr, 1, f);
    for (int m = 0; m < REF_NMODELS; m++)
      for (int g = 0; g < REF_NGRID; g++)
        for (int v = 0; v < 2; v++) {
          int note = REF_NOTE[m] + ref_knob(m, g, 0);
          std::vector<float> y = render(m, note, ref_knob(m, g, 1), ref_knob(m, g, 2), ref_knob(m, g, 3), REF_VEL[v],
                                        REF_SECONDS);
          dmm_t r;
          dmm_measure(y.data(), (int)y.size(), 48000.0, REF_NOISY[m], ref_hz(note), &r);
          fwrite(&r, sizeof r, 1, f);
        }
    fclose(f);
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "wav")) {     // the sequence of tests/drumsim.c: 4 velocities, each knob low / high
    static const int VEL[4] = {127, 96, 64, 32};
    for (int m = 0; m < REF_NMODELS; m++) {
      char path[512];
      int kn[4] = {0, REF_DEF[m][1], REF_DEF[m][2], REF_DEF[m][3]};
      snprintf(path, sizeof path, "%s/ref_%s.wav", argv[2], REF_NAME[m]);
      FILE *f = fopen(path, "wb");
      if (!f) { perror(path); return 1; }
      const size_t hit = (size_t)(REF_SECONDS * kSampleRate);
      wav_hdr(f, (uint32_t)(12 * hit));
      for (int h = 0; h < 12; h++) {
        int p[4] = {kn[0], kn[1], kn[2], kn[3]}, vel = h < 4 ? VEL[h] : 127;
        if (h >= 4)
          p[(h - 4) / 2] = (h - 4) / 2 == 0 ? ((h & 1) ? 12 : -12) : ((h & 1) ? 127 : 0);
        std::vector<float> y = render(m, REF_NOTE[m] + p[0], p[1], p[2], p[3], vel, REF_SECONDS);
        for (float s : y) {
          int16_t q = (int16_t)std::max(-32767.0f, std::min(32767.0f, s * 16384.0f));   // 1.0 = -6 dBFS
          fwrite(&q, 2, 1, f);
        }
      }
      fclose(f);
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "peaks")) {
    for (int m = 0; m < REF_NMODELS; m++) {
      std::vector<float> y = render(m, REF_NOTE[m], REF_DEF[m][1], REF_DEF[m][2], REF_DEF[m][3], 127, REF_SECONDS);
      float p = 0, pmax = 0;
      for (float s : y) p = std::max(p, std::fabs(s));
      for (int g = 0; g < REF_NGRID; g++) {
        std::vector<float> z = render(m, REF_NOTE[m] + ref_knob(m, g, 0), ref_knob(m, g, 1), ref_knob(m, g, 2),
                                      ref_knob(m, g, 3), 127, REF_SECONDS);
        for (float s : z) pmax = std::max(pmax, std::fabs(s));
      }
      printf("%s default %.4f grid_max %.4f\n", REF_NAME[m], p, pmax);
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "selftest")) {   // the metrics accept a perfect port at 44.1 kHz
    int bad = 0, total = 0;
    for (int m = 0; m < REF_NMODELS; m++)
      for (int g = 0; g < REF_NGRID; g += 4) {
        std::vector<float> y = render(m, REF_NOTE[m] + ref_knob(m, g, 0), ref_knob(m, g, 1), ref_knob(m, g, 2),
                                      ref_knob(m, g, 3), 127, REF_SECONDS);
        size_t n44 = (size_t)(REF_SECONDS * 44100);
        std::vector<float> z(n44), q(y.size());
        for (size_t i = 0; i < n44; i++) {             // windowed-sinc resampling 48 -> 44.1 kHz (20 kHz)
          double t = i * 48000.0 / 44100.0, acc = 0.0, fc = 20000.0 / 48000.0;
          long k0 = (long)t;
          for (long k = k0 - 31; k <= k0 + 32; k++) {
            double x = t - k, w;
            if (k < 0 || k >= (long)y.size() || fabs(x) >= 32.0) continue;
            w = 0.5 + 0.5 * cos(M_PI * x / 32.0);
            acc += y[k] * w * (x == 0.0 ? 2.0 * fc : sin(2.0 * M_PI * fc * x) / (M_PI * x));
          }
          z[i] = (float)acc;
        }
        for (size_t i = 0; i < y.size(); i++) q[i] = y[i] * 0.7f;   // -3.1 dB
        dmm_t a, b, c;
        char why[160];
        int noisy = REF_NOISY[m];
        dmm_measure(y.data(), (int)y.size(), 48000.0, noisy, ref_hz(REF_NOTE[m] + ref_knob(m, g, 0)), &a);
        dmm_measure(z.data(), (int)z.size(), 44100.0, noisy, ref_hz(REF_NOTE[m] + ref_knob(m, g, 0)), &b);
        dmm_measure(q.data(), (int)q.size(), 48000.0, noisy, ref_hz(REF_NOTE[m] + ref_knob(m, g, 0)), &c);
        total++;
        if (!dmm_compare(&a, &b, REF_KICK[m], &DMM_STRICT, why, sizeof why)) {
          printf("%s g%d resampled misses strict: %s\n", REF_NAME[m], g, why);
          bad++;
        }
        if (dmm_compare(&a, &c, REF_KICK[m], &DMM_LOOSE, why, sizeof why)) {
          printf("%s g%d -3 dB copy passes loose\n", REF_NAME[m], g);
          bad++;
        }
      }
    printf("selftest: %d of %d bad\n", bad, total);
    return bad != 0;
  }
  fprintf(stderr, "usage: drum_ref grid FILE | wav DIR | selftest | peaks\n");
  return 2;
}
