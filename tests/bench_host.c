/* BENCH on the host (firmware/src/bench.c): `bench_host` checks the cases; `bench_host measure` prints each
 * case's cost in host instructions per sample (tools/fm1_bench.py compares it with the FM-1's console run). */
#include "drum_host.h"                             /* (with firmware/src/bench.c) */

#define SECS(s) ((uint32_t)((s) * FS) / CTL * CTL)
static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t render_hash(uint32_t frames, int32_t *peak)    /* the mix of the next frames, FNV-1a */
{
    static int32_t o[2 * CTL];
    uint32_t h = 2166136261u, f, i;
    *peak = 0;
    for (f = 0; f < frames; f += CTL) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            h = (h ^ (uint32_t)o[i]) * 16777619u;
            if (abs(o[i]) > *peak)
                *peak = abs(o[i]);
        }
    }
    return h;
}

static double measure(uint32_t c)                    /* host instructions per sample, as the FM-1 run: 0.5 s
                                                      * to settle, then 2 s */
{
    uint64_t i0;
    host_init();
    bench_setup(c);
    render_mix(0, 0, SECS(0.5));
    i0 = instr_now();
    render_mix(0, 0, SECS(2));
    return i0 ? (double)(instr_now() - i0) / SECS(2) : 0;
}

/* the audio ISR's side of the run (audio.c, checked by audio_isr_test): HOLD counts the halves held, LIVE times
 * a render of 1 ms */
static void fake_half(void)
{
    if (bench.mode == BENCH_HOLD) {
        bench.held++;
    } else if (bench.mode == BENCH_LIVE) {
        render_mix(0, 0, HALF_FRAMES);
        bench.halves++;
        bench.sum_us += 1000;
        bench.max_us = 1000;
    }
}

static char lines[BENCH_N + 4][96];
static uint32_t nlines, says, held_at_setup_ok = 1;
static void out(const char *s)
{
    if (nlines < BENCH_N + 4u)
        snprintf(lines[nlines], sizeof lines[0], "%s", s);
    nlines++;
}
static void say(const char *a, const char *b) { (void)a; (void)b; says++; }

/* the console's run: a start line, each case once in order (set up only after the ISR held 2 halves), done, then
 * (once the last line has had its time) the power-on state, the audio as usual */
static void test_runner(void)
{
    uint32_t ms = 0, c, ok = 1, restart = 0, steps = 0, held0;
    char want[96], nm[32];
    host_init();
    memset(&bench, 0, sizeof bench);
    nlines = says = 0;
    bench_start(out);
    while (!restart && steps++ < 2000000u) {
        uint32_t ph = bench.phase, at = bench.at;
        held0 = bench.held;
        restart = bench_task(ms, out, say);
        if (ph == BP_SETUP && bench.phase != BP_SETUP && held0 - at < 2u)
            held_at_setup_ok = 0;                    /* set up before the ISR held 2 halves */
        fake_half();
        ms += 6;
    }
    ok &= restart && nlines == BENCH_N + 2u;
    snprintf(want, sizeof want, "bench start %u period_us %u\r\n", (unsigned)BENCH_N,
             (unsigned)(HALF_FRAMES * 1000000u / FS));
    ok &= nlines > 0 && !strcmp(lines[0], want);
    for (c = 0; c < BENCH_N && c + 1u < nlines; c++) {
        bench_name(c, nm);
        snprintf(want, sizeof want, "bench %u %s halves %u sum_us %u max_us 1000 late 0\r\n", (unsigned)c, nm,
                 (unsigned)BENCH_MEAS, (unsigned)BENCH_MEAS * 1000u);
        if (strcmp(lines[c + 1u], want)) {
            printf("     line %u: %s     want %s", (unsigned)c + 1u, lines[c + 1u], want);
            ok = 0;
        }
    }
    ok &= nlines == BENCH_N + 2u && !strcmp(lines[BENCH_N + 1u], "bench done\r\n");
    ok &= says == BENCH_N + 1u && held_at_setup_ok && bench.mode == BENCH_OFF && bench.phase == BP_OFF;
    {                                                /* the end: as at power-on (host_init) */
        int32_t pa, pb;
        uint32_t ha = render_hash(SECS(0.2), &pa);
        host_init();
        ok &= ha == render_hash(SECS(0.2), &pb);
    }
    check("bench run: start line, every case in order (set up while the ISR holds), done, then the power-on state", ok);
}

int main(int argc, char **argv)
{
    uint32_t c, d, ok = 1;
    char a[32], b[32];
    if (argc > 1 && !strcmp(argv[1], "measure")) {
        for (c = 0; c < BENCH_N; c++) {
            double best = 1e18, x;
            int k;
            for (k = 0; k < 3; k++)                  /* the lowest of 3 (scheduling noise) */
                if ((x = measure(c)) < best)
                    best = x;
            bench_name(c, a);
            printf("host %u %s %.1f\n", c, a, best);
        }
        return 0;
    }
    printf("     bench: %u cases\n", (unsigned)BENCH_N);
    for (c = 0; c < BENCH_N; c++) {
        bench_name(c, a);
        ok &= a[0] && strlen(a) < 24u && !strchr(a, ' ');
        for (d = 0; d < c; d++) {
            bench_name(d, b);
            ok &= strcmp(a, b) != 0;
        }
    }
    check("bench: every case has a unique one-word name", ok);
    ok = 1;
    for (c = 0; c < BENCH_N; c++) {                  /* the FM-1's reset = the host tests' host_init */
        int32_t pa, pb;
        uint32_t ha, hb;
        host_init();
        bench_setup(c);
        ha = render_hash(SECS(1), &pa);
        host_init();                                 /* a used machine: the heaviest case played a while */
        bench_setup(BENCH_PHYS);
        render_mix(0, 0, SECS(0.5));
        bench_reset();
        bench_setup(c);
        hb = render_hash(SECS(1), &pb);
        if (ha != hb) {
            bench_name(c, a);
            printf("     %s: after bench_reset %08x, after host_init %08x\n", a, hb, ha);
            ok = 0;
        }
        ok &= c == 0 ? pa == 0 : pa > 0;             /* idle is silent, every other case sounds */
    }
    check("bench: bench_reset + a case renders as host_init + it (every case); idle silent, the rest sound", ok);
    test_runner();
    printf(fails ? "bench_host: %d FAILED\n" : "bench_host: all passed\n", fails);
    return fails ? 1 : 0;
}
