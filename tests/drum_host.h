/* Host build of the drum fork's DSP, models, mix and sequencer (no hardware). Same sources as the
 * firmware; reset helpers for independent tests. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <libproc.h>
#include <sys/resource.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;
#include "../firmware/src/core.h"
#include "../firmware/src/dsp.c"
#include "../firmware/src/eng_sample.c"
#include "../firmware/src/dmodels.c"
#include "../firmware/src/params.c"
#include "../firmware/src/drum_core.c"
#include "../firmware/src/slicer.c"
#include "../firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"
#include "../firmware/src/grids.c"
#include "../firmware/src/seq.c"

static void host_reset_fx(void)                     /* FX buses, master, slicer, metal: as at power-on */
{
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    memset(&fx, 0, sizeof fx);
    memset(sl, 0, sizeof sl);
    memset(sl_buf, 0, sizeof sl_buf);
    lim_env = LIM_T;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
    memset(lce, 0, sizeof lce);
    dvage = 0;
    mi_r = mi_w = 0;
    kb_prev = 0;
    fm1_in.notes = 0;
    transport_req = panic_req = 0;
    memset(metal_ph, 0, sizeof metal_ph);
    metal_blk = 0xFFFFFFFFu;
    dblock = 0;
    memset(&grids, 0, sizeof grids);
}

static void host_init(void)
{
    host_reset_fx();
    memset(&song, 0, sizeof song);
    drum_tracks_init();
}

static int32_t blk[CTL];
static void render_track(track_t *t, int32_t *dst, uint32_t frames)   /* one track, no FX */
{
    uint32_t f, i;
    for (f = 0; f < frames; f += CTL) {
        drum_block_begin();
        track_render(t, blk, CTL);
        for (i = 0; i < CTL && f + i < frames; i++)
            if (dst)
                dst[f + i] = blk[i];
    }
}

static int32_t mixo[2 * CTL];
static void render_mix(int32_t *l, int32_t *r, uint32_t frames)      /* the whole mix (events, FX, master) */
{
    uint32_t f, i;
    for (f = 0; f < frames; f += CTL) {
        mix_block(mixo, CTL);
        for (i = 0; i < CTL && f + i < frames; i++) {
            if (l)
                l[f + i] = mixo[2 * i];
            if (r)
                r[f + i] = mixo[2 * i + 1];
        }
    }
}

static uint32_t hit_age(const track_t *t)          /* changes on every hit of t (voice ages) */
{
    uint32_t a = 0, i;
    for (i = 0; i < NDV; i++)
        if (t->v[i].age > a)
            a = t->v[i].age;
    return a;
}

static uint64_t instr_now(void)
{
    struct rusage_info_v4 ri;
    if (!proc_pid_rusage(getpid(), RUSAGE_INFO_V4, (rusage_info_t *)&ri))
        return ri.ri_instructions;
    return 0;
}
