/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* phys_dsp.c's MODAL as a plain C call for tests/phys_ref.cpp (DaisySP's float original against it). */
#include <stdint.h>
#include "../firmware/src/phys_dsp.c"
#define CTL 32

void phys_fixed_render(int model, double hz, double structure, double brightness, double damping, double accent,
                       double bow, const float *exc, float *out, int n)
{
    static px_modal_t M;
    int32_t y[CTL], ax[CTL];
    uint32_t f0 = (uint32_t)(hz / 44100.0 * 4294967296.0), i, k;
    (void)model;
    (void)exc;
    for (i = 0; i < sizeof M / 4u; i++)
        ((uint32_t *)&M)[i] = 0;
    M.rng = 0x13579BDFu;
    M.trig = 1;
    for (i = 0; i < (uint32_t)n; i += CTL) {
        uint32_t m = (uint32_t)n - i < CTL ? (uint32_t)n - i : CTL;
        px_modal_blk_t B;
        px_modal_block(&B, &M, f0, (int32_t)(structure * 65536), (int32_t)(brightness * 65536),
                       (int32_t)(damping * 65536), (int32_t)(accent * 65536), 0, (int32_t)(bow * 65536));
        px_modal_run(&B, &M, y, ax, m);
        for (k = 0; k < m; k++)
            out[i + k] = (float)y[k] / (1 << 20);
    }
}
