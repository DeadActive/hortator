/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M3 compressor fidelity: comp.c's Streams configuration (comp_configure_streams, Streams' own 31,089 Hz
 * coefficients, AMOUNT over its whole native range) against the original's gain for every
 * sample of every case (tests/comp_ref.cc): identical.   comp_fidelity REF.bin */
#include <stdint.h>
#include "comp_lp31k.h"
#define COMP_LP_TABLE COMP_LP_31K
#include "drum_host.h"
#include "comp_cases.h"

int main(int argc, char **argv)
{
    static uint8_t ref[CC_ALL * COMP_SIGNALS * COMP_N * 2];
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : 0;
    comp_case_t c;
    int i, sig, n, bad = 0, first = -1;
    if (!f || fread(ref, 1, sizeof ref, f) != sizeof ref) {
        printf("FAIL  comp fidelity: cannot read %s\n", argc > 1 ? argv[1] : "(none)");
        return 1;
    }
    fclose(f);
    for (i = 0; comp_case(i, &c); i++)
        for (sig = 0; sig < COMP_SIGNALS; sig++) {
            comp_cfg_t cf;
            int64_t det = 0;
            int32_t gr = 0;
            comp_configure_streams(comp_case_k16(c.atk), comp_case_k16(c.thr), comp_case_k16(c.rel), comp_case_k16(c.amt),
                                   c.knee, &cf);
            for (n = 0; n < COMP_N; n++) {
                uint32_t g = comp_process(&cf, &det, &gr, comp_signal(sig, n));
                int k = ((i * COMP_SIGNALS + sig) * COMP_N + n) * 2;
                if (g != (uint32_t)(ref[k] | ref[k + 1] << 8)) {
                    bad++;
                    if (first < 0)
                        first = k / 2;
                }
            }
        }
    if (bad)
        printf("FAIL  comp fidelity: %d of %d samples differ (first: case %d signal %d sample %d)\n", bad,
               CC_ALL * COMP_SIGNALS * COMP_N, first / (COMP_SIGNALS * COMP_N), first / COMP_N % COMP_SIGNALS, first % COMP_N);
    else
        printf("ok    comp fidelity: %d cases x %d signals x %d samples identical to Streams\n", CC_ALL, COMP_SIGNALS, COMP_N);
    return bad ? 1 : 0;
}
