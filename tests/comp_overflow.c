/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* M3: the COMP makeup gain at its extremes must not overflow the mix (built with -fsanitize=signed-integer-overflow,
 * any overflow aborts): a silent source (full makeup on the ducked tracks), every track ducked at LEVEL 127, busy,
 * THRSH 0 / 26 / 127 x MKUP 100 / 127, master at its loudest. */
#include "drum_host.h"

int main(void)
{
    static const int16_t THR[3] = {0, 26, 127}, MK[2] = {100, 127};
    uint32_t a, t, i, k;
    for (t = 0; t < 3u; t++)
        for (a = 0; a < 2u; a++) {
            host_init();
            song.master_q12 = 4088;
            song.g[G_CSRC] = 1;                      /* T1: no steps, silent: the ducked tracks get the full makeup */
            song.g[G_CTHR] = THR[t];
            song.g[G_CMKUP] = MK[a];
            for (i = 1; i < NTRK; i++) {
                trk[i].p[P_DUCK] = 1;
                trk[i].p[P_LEVEL] = 127;
                trk[i].p[P_REV] = trk[i].p[P_DLY] = 127;
                for (k = 0; k < 16u; k++)
                    trk[i].step[k].on = trk[i].step[k].acc = 1;
            }
            transport_req = 1;
            render_mix(0, 0, FS * 2 / CTL * CTL);
        }
    printf("ok    comp overflow: makeup extremes, all tracks ducked at LEVEL 127, master max: no signed overflow\n");
    return 0;
}
