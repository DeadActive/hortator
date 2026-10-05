// SPDX-License-Identifier: GPL-3.0-only
// Drum machine fork: 2026 DEADACTIVE
// M2 Grids fidelity: Mutable Instruments' original PatternGenerator (fetched by tests/fetch_ref.sh, AVR headers
// stubbed in tests/grids_stub) over a grid of settings; per case the state (bits 0..5) after the first clock tick
// of 96 steps, three ticks a step, seed 0x21. tests/grids_fidelity.c runs grids.c the same way and compares.
//   grids_ref OUT.bin
#include <cstdio>
#include "grids/pattern_generator.h"
#include "avrlib/random.h"
#include "tests/grids_cases.h"
using namespace grids;

static uint8_t byte_of(int v) { return (uint8_t)(v * 2 + (v >> 6)); }    // knob 0..127 -> 0..255

int main(int argc, char **argv) {
  FILE *f = fopen(argc > 1 ? argv[1] : "grids_ref.bin", "wb");
  grids_case_t c;
  if (!f) return 1;
  PatternGenerator::Init();
  PatternGenerator::set_swing(0);
  PatternGenerator::set_output_clock(0);
  for (int i = 0; grids_case(i, &c); i++) {
    PatternGeneratorSettings *s = PatternGenerator::mutable_settings();
    PatternGenerator::set_output_mode(c.mode ? OUTPUT_MODE_EUCLIDEAN : OUTPUT_MODE_DRUMS);
    if (c.mode) {
      for (int k = 0; k < 3; k++) s->options.euclidean_length[k] = (uint8_t)((c.len[k] - 1) << 3);
    } else {
      s->options.drums.x = byte_of(c.x);
      s->options.drums.y = byte_of(c.y);
      s->options.drums.randomness = byte_of(c.chaos);
    }
    for (int k = 0; k < 3; k++) s->density[k] = byte_of(c.fill[k]);
    avrlib::Random::Seed(0x21);
    PatternGenerator::Reset();
    for (int step = 0; step < GRIDS_STEPS; step++) {
      PatternGenerator::TickClock(1);
      fputc(PatternGenerator::state() & 0x3f, f);
      PatternGenerator::TickClock(1);
      PatternGenerator::TickClock(1);
    }
  }
  fclose(f);
  return 0;
}
