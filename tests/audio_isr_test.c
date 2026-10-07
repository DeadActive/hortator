/* The audio ISR (audio.c) with stub ALNK0 / TIMER4 registers: felucca_dbg.in_audio is set before anything else
 * (TIMER5 nests into the ISR from there on and must not run its owed USB / UART polls in it, main.c, upstream
 * 1.0), and the render time excludes the ticks the nested TIMER5 spent. */
#include "drum_host.h"
#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 24u
static volatile uint32_t *in_audio_p;           /* &felucca_dbg.in_audio (declared in audio.c) */
static uint32_t in_audio_at_pending = 99, in_audio_at_t0 = 99, host_t, ticks_calls, nested_add, render_us = 1000;
static volatile uint32_t *nested_p;
static uint8_t fm1_audio_pending(void) { in_audio_at_pending = *in_audio_p; return FM1_AUDIO_HALF; }
static uint32_t fm1_ticks(void)
{
    if (ticks_calls++ == 0)
        in_audio_at_t0 = *in_audio_p;
    else
        *nested_p += nested_add;                  /* the nested TIMER5 ran during the render */
    return host_t += 24u * render_us;             /* 1 ms between the two reads (render_us) */
}
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*f)(void), int pri) { (void)b; (void)n; (void)f; (void)pri; }
void isr_alnk0(void) {}
#include "../firmware/src/audio.c"

static int nonzero_half(void)
{
    uint32_t i;
    for (i = 0; i < HALF_WORDS; i++)
        if (abuf[i])
            return 1;
    return 0;
}

static void bench_isr_case(void)                     /* a loud kit, every track hit each half */
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        drum_hit(&trk[i], 127);
}

/* BENCH (bench.c): HOLD renders nothing and outputs silence; LIVE renders (timed, counted, never shed) into
 * its own buffer, the DAC gets silence; a half above 80 % of its time is followed by one not rendered */
static int bench_isr(void)
{
    int fail = 0;
    uint32_t db, i;
    host_init();
    nested_add = 0;
    render_us = 1000;
    memset(&bench, 0, sizeof bench);
    bench.mode = BENCH_HOLD;
    for (i = 0; i < HALF_WORDS; i++)
        abuf[i] = 12345;
    bench_isr_case();
    db = dblock;
    ticks_calls = 0;
    fm1_alnk0_irq();
    if (dblock != db || nonzero_half() || bench.halves || bench.held != 1u) {
        printf("bench HOLD: rendered %u blocks, output %s, %u halves counted, %u held\n", dblock - db,
               nonzero_half() ? "not silent" : "silent", bench.halves, bench.held);
        fail = 1;
    }
    bench.mode = BENCH_LIVE;
    render_us = 5000;                                /* 86 % of the half: shedding would start */
    shed_req = 0;
    bench_isr_case();
    ticks_calls = 0;
    fm1_alnk0_irq();
    if (dblock == db || nonzero_half() || bench.halves != 1u || bench.sum_us != 5000u || bench.max_us != 5000u ||
        shed_req || !bench.rest) {
        printf("bench LIVE: blocks %u, output %s, halves %u, sum %u, max %u, shed %u, rest %u\n", dblock - db,
               nonzero_half() ? "not silent" : "silent", bench.halves, bench.sum_us, bench.max_us, shed_req,
               bench.rest);
        fail = 1;
    }
    db = dblock;
    ticks_calls = 0;
    fm1_alnk0_irq();                                 /* the rest after a heavy half */
    if (dblock != db || bench.halves != 1u || bench.rest || nonzero_half()) {
        printf("bench rest: blocks %u, halves %u, rest %u\n", dblock - db, bench.halves, bench.rest);
        fail = 1;
    }
    render_us = 1000;
    ticks_calls = 0;
    fm1_alnk0_irq();
    if (dblock == db || bench.halves != 2u || bench.sum_us != 6000u || bench.max_us != 5000u || bench.rest) {
        printf("bench LIVE light: halves %u, sum %u, max %u, rest %u\n", bench.halves, bench.sum_us, bench.max_us,
               bench.rest);
        fail = 1;
    }
    bench.mode = BENCH_OFF;
    if (!fail)
        printf("audio ISR: BENCH hold silent, live timed and muted, no shedding, a rest after a heavy half ok\n");
    return fail;
}

int main(void)
{
    int fail = 0;
    in_audio_p = &felucca_dbg.in_audio;
    nested_p = &t5_nested_ticks;
    host_init();
    nested_add = 0;
    fm1_alnk0_irq();
    if (in_audio_at_pending != 1u || in_audio_at_t0 != 1u) {
        printf("audio ISR: in_audio %u at the pending read, %u at t0: must be 1 first (TIMER5 nests from there)\n",
               in_audio_at_pending, in_audio_at_t0);
        fail = 1;
    }
    if (felucca_dbg.in_audio != 0u) {
        printf("audio ISR: in_audio left set\n");
        fail = 1;
    }
    if (felucca_dbg.last_us != 1000u) {
        printf("audio ISR: render %u us, want 1000\n", felucca_dbg.last_us);
        fail = 1;
    }
    ticks_calls = 0;
    nested_add = 24u * 300u;                       /* 300 us of nested scans */
    fm1_alnk0_irq();
    if (felucca_dbg.last_us != 700u) {
        printf("audio ISR: render with 300 us nested %u us, want 700\n", felucca_dbg.last_us);
        fail = 1;
    }
    if (!fail)
        printf("audio ISR: in_audio first, nested TIMER5 time excluded ok\n");
    fail |= bench_isr();
    return fail;
}
