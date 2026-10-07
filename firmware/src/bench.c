/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* BENCH: the same performance cases on the host (tests/bench_host.c: host instructions per sample) and on the
 * FM-1 (console `bench yes`, console.c cdc_task: the audio ISR's render time, audio.c). bench_reset puts the
 * machine as the host tests' host_init does; bench_setup(c) builds case c on it. Nothing here touches flash; a
 * run ends with the machine as at power-on (the pattern in RAM is gone). */

/* the FM-1 run's state (in .pool: no new small globals). mode: the audio ISR (audio.c) in HOLD renders nothing and
 * outputs silence; in LIVE it renders each half as usual but into out (the DAC gets silence), adds its render time
 * to halves / sum_us / max_us / late, never sheds a voice, and rests one half (renders nothing) after a half
 * above BENCH_REST % of its time, so the main loop (the watchdog, the console) still runs at any load */
enum { BENCH_OFF, BENCH_HOLD, BENCH_LIVE };
#define BENCH_REST 80u
static struct {
    volatile uint8_t mode, rest;
    volatile uint32_t halves, sum_us, max_us, late, held;   /* held: HOLD halves */
    uint8_t phase;                                       /* the run (bench_task) */
    uint8_t cur;
    uint32_t at;
    int32_t out[HALF_FRAMES * 2u];                       /* LIVE renders here */
} bench __attribute__((section(".pool")));

/* the audio ISR's side (audio.c; out of line: the ISR's own code as without BENCH) */
static __attribute__((noinline)) uint32_t bench_rest(void)   /* 1: this half renders nothing (HOLD, or a rest) */
{
    if (bench.mode == BENCH_HOLD) {
        bench.held++;
        return 1;
    }
    if (bench.rest) {
        bench.rest = 0;
        return 1;
    }
    return 0;
}

static __attribute__((noinline)) void bench_mute(int32_t *o)
{
    uint32_t i;
    for (i = 0; i < HALF_FRAMES * 2u; i++)
        o[i] = 0;
}

static __attribute__((noinline)) void bench_time(uint32_t us, uint32_t late)   /* a LIVE half rendered in us */
{
    bench.halves++;
    bench.sum_us += us;
    if (us > bench.max_us)
        bench.max_us = us;
    bench.late += late;
    bench.rest = us * 100u > (HALF_FRAMES * 1000000u / FS) * BENCH_REST;
}

/* the cases: idle; 8 tracks of each model without and with every FX; each RESON model on 8 x S909 (8 tracks
 * and the device's cap); the realistic heavy kit, then without one feature each; the PHYS-heavy kit */
enum { BH_LAYER = 1, BH_DIST = 2, BH_SLCR = 4, BH_CHOR = 8, BH_DLY = 16, BH_REV = 32, BH_RESON = 64 };
enum { BH_FILTER = 128, BH_COMP = 256, BH_SIDE = 512 };   /* the cases after the PHYS kits: added, not taken out */
#define BH_FX (BH_DIST | BH_SLCR | BH_CHOR | BH_DLY | BH_REV)
static const uint16_t BENCH_MORE_OFF[5] = {BH_FILTER, BH_COMP, BH_FX | BH_COMP, BH_COMP | BH_SIDE, BH_FX | BH_COMP | BH_SIDE};
static const char *const BENCH_MORE_NAME[5] = {"heavy+filter", "heavy+comp", "heavy-fx+comp", "heavy+sidechain",
                                               "heavy-fx+sidechain"};
static const uint8_t BENCH_HEAVY_OFF[] = {0, BH_LAYER, BH_DIST, BH_SLCR, BH_CHOR, BH_DLY, BH_REV, BH_RESON};
static const char *const BENCH_OFF_NAME[] = {"", "-layer", "-dist", "-slicer", "-chorus", "-delay", "-reverb",
                                             "-reson"};
static const char *const BENCH_RS_NAME[RS_NMODEL] = {"OFF", "STRNG", "PIPE", "CHORD", "MODAL"};
static const uint8_t BENCH_RS_CAP[RS_NMODEL] = {0, 4, 4, 2, 2};   /* the UI's caps (reson.c) */
#define BENCH_MODELS 1u                                  /* first model case */
#define BENCH_RESON (BENCH_MODELS + 2u * NMODELS)        /* first RESON case */
#define BENCH_HEAVY (BENCH_RESON + 2u * (RS_NMODEL - 1u))
#define BENCH_PHYS (BENCH_HEAVY + sizeof BENCH_HEAVY_OFF)
#define BENCH_MORE (BENCH_PHYS + 2u)                      /* + PHYS-heavy, PHYS-heavy -reson */
#define BENCH_N (BENCH_MORE + 5u)                         /* + the FILTER, COMP and sidechain cases */
#define BENCH_BPM_FAST 240                               /* the model cases: 1/32 notes, as drum_test's kit_cost */

static char *bench_cat(char *s, const char *a)
{
    while (*a)
        *s++ = *a++;
    *s = 0;
    return s;
}

/* the case's name, one token for the console line: idle, K808, K808+fx, S909-reson-MODALx8, heavy-dist, ... */
static void bench_name(uint32_t c, char *s)
{
    *s = 0;
    if (c == 0) {
        bench_cat(s, "idle");
    } else if (c < BENCH_RESON) {
        s = bench_cat(s, N_MODEL[(c - BENCH_MODELS) / 2u]);
        bench_cat(s, (c - BENCH_MODELS) & 1u ? "+fx" : "");
    } else if (c < BENCH_HEAVY) {
        uint32_t r = 1u + (c - BENCH_RESON) / 2u;
        char n[3] = {'x', (char)('0' + ((c - BENCH_RESON) & 1u ? BENCH_RS_CAP[r] : NTRK)), 0};
        s = bench_cat(s, "S909-reson-");
        s = bench_cat(s, BENCH_RS_NAME[r]);
        bench_cat(s, n);
    } else if (c < BENCH_PHYS) {
        s = bench_cat(s, "heavy");
        bench_cat(s, BENCH_OFF_NAME[c - BENCH_HEAVY]);
    } else if (c < BENCH_MORE) {
        s = bench_cat(s, "phys-heavy");
        bench_cat(s, c == BENCH_PHYS ? "" : "-reson");
    } else {
        bench_cat(s, BENCH_MORE_NAME[c - BENCH_MORE]);
    }
}

/* fx (fx.c) zeroed field by field: memset(&fx) would take its address, and the compiler then reloads fx's fields
 * in rev_room's loop (+15 % there, tests/target_budget.txt). A new field in fx: add it here (the size check) */
typedef char bench_fx_fields[sizeof fx == 100u ? 1 : -1];
static void bench_fx_clear(void)
{
    uint32_t k;
    fx.dly_w = fx.cho_w = fx.cho_ph = 0;
    fx.dly_lp = 0;
    for (k = 0; k < 4u; k++) {
        fx.comb_i[k] = 0;
        fx.comb_lp[k] = 0;
    }
    fx.ap_i[0] = fx.ap_i[1] = 0;
    fx.sb_lp1 = fx.sb_lp2 = fx.sb_lp3 = fx.sb_lp4 = fx.sb_env = fx.sb_h1 = fx.sb_h2 = fx.sb_hl = 0;
    fx.rtype = 0;
    fx.sp_w = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = fx.sp_size = 0;
    fx.sp_ph = 0;
}

/* the machine as at power-on, as the host tests' host_init (tests/drum_host.h host_reset_fx): FX buses, master,
 * slicer, PERFORM, MOTION, SONG, the song settings (zero, then the tempo), the tracks' power-on kit */
static void bench_reset(void)
{
    uint32_t k;
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(rev_ap, 0, sizeof rev_ap);
    bench_fx_clear();                                /* (not memset(&fx): see there) */
    memset(sl, 0, sizeof sl);
    memset(sl_buf, 0, sizeof sl_buf);
    lim_env = LIM_T;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
    memset(lce, 0, sizeof lce);
    dvage = 0;
    mi_r = mi_w = 0;
    kb_prev = 0;
    transport_req = panic_req = 0;
    memset(metal_ph, 0, sizeof metal_ph);
    metal_blk = 0xFFFFFFFFu;
    dblock = 0;
    memset(&grids, 0, sizeof grids);
    memset(&gclk, 0, sizeof gclk);
    memset(&pf, 0, sizeof pf);
    pf.src = pf.next = PF_N;
    pf.lc = PF_TOP;
    for (k = 0; k < NTRK; k++)
        pf.mg[k] = 32768;
    perf_mask = kb_layer = perf_held = perf_act = 0;
    perf_kill = 0;
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    perf_seq = 0;
    memset(perf_ord, 0, sizeof perf_ord);
    sl_lent = 0;
    memset(&mo, 0, sizeof mo);
    memset(&chain, 0, sizeof chain);
    comp_reset();
    comp_was = NTRK;
    memset(&song, 0, sizeof song);
    drum_tracks_init();
}

/* 8 tracks of model mi, 1/32 at 240 BPM, DECAY 127 (drum_test's kit_cost); fx: every FX (DIST, SLICER, the
 * three sends), no layer; RESON model r on the first nr tracks, DECAY max */
static void bench_kit(uint32_t mi, int fx_on, int32_t r, uint32_t nr)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        drum_set_model(t, mi);
        t->p[P_E1] = 127;
        if (mi == DM_SMPL) {                             /* the heaviest sample: 3 octaves up, driven */
            t->p[P_E0] = 24;
            t->p[P_E3] = 127;
            t->p[P_E5] = 38;
        }
        t->p[P_LLEVEL] = 0;
        t->p[P_DIST] = (int16_t)(fx_on ? 60 : 0);
        t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = (int16_t)(fx_on ? 60 : 0);
        t->p[P_SLCR] = (int16_t)(fx_on ? 1 : 0);
        t->p[P_SDIV] = 3;
        t->p[P_RMODEL] = (int16_t)(i < nr ? r : RS_OFF);
        t->p[P_RDECAY] = 127;
        memset(t->step, 0, sizeof t->step);
        t->step[0].on = t->step[1].on = 1;
        t->p[P_SLEN] = 2;
    }
    song.g[G_BPM] = BENCH_BPM_FAST;
}

/* the realistic heavy kit (drum_test's test_cost: the demo with a layer, DIST, SLICER and sends on every track,
 * RESON CHORD on 2, STRNG on 4) at 120 BPM; phys: MEMB on 5 and 7, RESON 2 CHORD + 2 MODAL (DECAY 110);
 * off: the features taken out (BH_), or added (BH_FILTER on all 8, BH_COMP keyed by T1, BH_SIDE: DUCK on 2..8) */
static void bench_heavy(int phys, uint32_t off)
{
    static const char *const PAT[NTRK] = {
        "x...x...x...x..x", "....x.......x...", "....x.......x..x", "x.x.x.x.x.x.x.x.",
        "..x...x...x...x.", "......x....x....", ".x.....x..x.....", "x...............",
    };
    uint32_t i, k;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (phys && (i == 4u || i == 6u))
            drum_set_model(t, DM_MEMB);
        for (k = 0; k < 16; k++)
            t->step[k].on = PAT[i][k] == 'x';
        t->p[P_LLEVEL] = (int16_t)(off & BH_LAYER ? 0 : 100);
        t->p[P_LKEY] = 38;
        t->p[P_DIST] = (int16_t)(off & BH_DIST ? 0 : 40);
        t->p[P_SLCR] = (int16_t)(off & BH_SLCR ? 0 : 1);
        t->p[P_CHOR] = (int16_t)(off & BH_CHOR ? 0 : 40);
        t->p[P_DLY] = (int16_t)(off & BH_DLY ? 0 : 40);
        t->p[P_REV] = (int16_t)(off & BH_REV ? 0 : 40);
        if (phys) {
            t->p[P_RMODEL] = (int16_t)(i < 2u ? RS_CHORD : i < 4u ? RS_MODAL : RS_OFF);
            t->p[P_RDECAY] = 110;
        } else {
            t->p[P_RMODEL] = (int16_t)(i == 1u ? RS_CHORD : i == 3u ? RS_STRNG : RS_OFF);
        }
        if (off & BH_RESON)
            t->p[P_RMODEL] = RS_OFF;
        if (off & BH_FILTER) {
            t->p[P_FTYPE] = FT_LP;
            t->p[P_FCUT] = 70;
            t->p[P_FRESO] = 60;
            t->p[P_FENV] = 30;
            t->p[P_FDEC] = 40;
        }
        t->p[P_DUCK] = (int16_t)((off & BH_SIDE) && i != 0u);
    }
    if (off & BH_COMP)
        song.g[G_CSRC] = 1;                              /* COMP keyed by T1 (the kick) */
    song.g[G_BPM] = 120;
}

/* case c on a machine just reset (bench_reset); the transport starts (idle: stopped) */
static void bench_setup(uint32_t c)
{
    if (c == 0) {
        song.g[G_BPM] = 120;
        return;
    }
    if (c < BENCH_RESON)
        bench_kit((c - BENCH_MODELS) / 2u, (int)((c - BENCH_MODELS) & 1u), RS_OFF, 0);
    else if (c < BENCH_HEAVY)
        bench_kit(DM_S909, 0, (int32_t)(1u + (c - BENCH_RESON) / 2u),
                  (c - BENCH_RESON) & 1u ? BENCH_RS_CAP[1u + (c - BENCH_RESON) / 2u] : NTRK);
    else if (c < BENCH_PHYS)
        bench_heavy(0, BENCH_HEAVY_OFF[c - BENCH_HEAVY]);
    else if (c < BENCH_MORE)
        bench_heavy(1, c == BENCH_PHYS ? 0 : BH_RESON);
    else
        bench_heavy(0, BENCH_MORE_OFF[c - BENCH_MORE]);
    transport_req = 1;
}

/* ---- the run on the FM-1 (console `bench yes`, console.c cdc_task calls bench_task from the main loop): for
 * each case the ISR holds (silence, no render), the case is set up once it has held 2 halves (none in progress),
 * then it plays LIVE: BENCH_WARM halves to settle, BENCH_MEAS halves measured (~2 s, more with rests), one line */
enum { BP_OFF, BP_HOLD, BP_SETUP, BP_WARM, BP_MEAS, BP_END };
#define BENCH_WARM 86u
#define BENCH_MEAS 344u
#define BENCH_END_MS 500u                                /* the end: power-on state after the last lines left */

static char *bench_num(char *s, uint32_t v)
{
    char t[11];
    uint32_t n = 0;
    do
        t[n++] = (char)('0' + v % 10u);
    while ((v /= 10u) != 0u);
    while (n)
        *s++ = t[--n];
    *s = 0;
    return s;
}

static void bench_start(void (*out)(const char *))
{
    char l[48], *s = l;
    if (bench.phase)
        return;
    s = bench_cat(s, "bench start ");
    s = bench_num(s, BENCH_N);
    s = bench_cat(s, " period_us ");
    s = bench_num(s, HALF_FRAMES * 1000000u / FS);
    bench_cat(s, "\r\n");
    out(l);
    bench.cur = 0;
    bench.phase = BP_HOLD;
}

static void bench_stop(uint32_t now_ms)                  /* (console `bench stop`): the end, soon */
{
    if (bench.phase) {
        bench.mode = BENCH_HOLD;
        bench.phase = BP_END;
        bench.at = now_ms;
    }
}

/* one step of the run; out: a console line, say: the screen's message. 1: the run is over (the machine back to its
 * power-on state, the audio as usual) */
static int bench_task(uint32_t now_ms, void (*out)(const char *), void (*say)(const char *, const char *))
{
    char l[96], *s;
    switch (bench.phase) {
    case BP_HOLD:
        bench.mode = BENCH_HOLD;
        bench.at = bench.held;
        bench.phase = BP_SETUP;
        return 0;
    case BP_SETUP:
        if (bench.held - bench.at < 2u)
            return 0;                                    /* (HOLD from here: no half renders the state set up) */
        bench_reset();
        bench_setup(bench.cur);
        bench.halves = bench.sum_us = bench.max_us = bench.late = 0;
        bench.rest = 0;
        bench.mode = BENCH_LIVE;
        bench_name(bench.cur, l);
        say("BENCH ", l);
        bench.phase = BP_WARM;
        return 0;
    case BP_WARM:
        if (bench.halves < BENCH_WARM)
            return 0;
        bench.mode = BENCH_HOLD;                         /* (the counters zeroed between two halves) */
        bench.halves = bench.sum_us = bench.max_us = bench.late = 0;
        bench.mode = BENCH_LIVE;
        bench.phase = BP_MEAS;
        return 0;
    case BP_MEAS:
        if (bench.halves < BENCH_MEAS)
            return 0;
        bench.mode = BENCH_HOLD;
        s = bench_cat(l, "bench ");
        s = bench_num(s, bench.cur);
        s = bench_cat(s, " ");
        bench_name(bench.cur, s);
        while (*s)
            s++;
        s = bench_cat(s, " halves ");
        s = bench_num(s, bench.halves);
        s = bench_cat(s, " sum_us ");
        s = bench_num(s, bench.sum_us);
        s = bench_cat(s, " max_us ");
        s = bench_num(s, bench.max_us);
        s = bench_cat(s, " late ");
        s = bench_num(s, bench.late);
        bench_cat(s, "\r\n");
        out(l);
        if (++bench.cur < BENCH_N) {
            bench.phase = BP_HOLD;
        } else {
            out("bench done\r\n");
            bench.phase = BP_END;
            bench.at = now_ms;
        }
        return 0;
    case BP_END:
        if (now_ms - bench.at < BENCH_END_MS)
            return 0;
        bench_reset();                                   /* (HOLD: nothing renders meanwhile) */
        bench.phase = BP_OFF;
        bench.mode = BENCH_OFF;
        say("BENCH ", "DONE");
        return 1;
    }
    return 0;
}
