// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// M3 compressor fidelity: Mutable Instruments' original streams::Compressor (fetched by tests/fetch_ref.sh) on
// the cases of tests/comp_cases.h; writes every sample's gain (uint16, little endian).   comp_ref OUT.bin
#include <cstdio>
#include <cstdlib>
#include <new>
#include "streams/compressor.h"
#include "tests/comp_cases.h"

int main(int argc, char **argv) {
  FILE *f = fopen(argc > 1 ? argv[1] : "comp_ref.bin", "wb");
  comp_case_t c;
  if (!f) return 1;
  for (int i = 0; comp_case(i, &c); i++)
    for (int sig = 0; sig < COMP_SIGNALS; sig++) {
      void *m = calloc(1, sizeof(streams::Compressor));        // zeroed state, as a fresh module
      streams::Compressor *cp = new (m) streams::Compressor();
      int32_t globals[4] = {(int32_t)comp_case_k16(c.atk), (int32_t)comp_case_k16(c.thr),
                            (int32_t)comp_case_k16(c.rel), (int32_t)comp_case_k16(c.amt)};
      cp->Init();
      cp->Configure(c.knee != 0, globals, globals);
      for (int n = 0; n < COMP_N; n++) {
        int16_t x = (int16_t)comp_signal(sig, n);
        uint16_t g, fr;
        cp->Process(x, x, &g, &fr);
        fputc(g & 0xff, f);
        fputc(g >> 8, f);
      }
      free(m);
    }
  fclose(f);
  return 0;
}
