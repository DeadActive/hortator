/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* GLO SYNC (MIDI clock out), MIDI THRU and GLO ROUT (MIDI in routing) on the host (midi_out.c, seq.c midi_block).
 * The FM-1's timing is modelled: TIMER4 ticks (24 MHz); the audio IRQ every half (256 samples), its render taking
 * LOAD of the half (in_audio); TIMER5 every 100 us, main.c's handler polling USB on every 5th tick outside the render
 * (its `owed`); a USB poll sends what the MIDI out ring holds then. A Clock's time is the poll that sent it.
 *   clock    PLAY: Start, then 24 Clocks a beat on the sequencer's unswung 1/8 grid, Stop on STOP; the largest
 *            distance of a sent Clock from the straight line through them (jitter), at 97 / 120 / 180 BPM, swing on and
 *            off, the render idle and busy; tempo changes keep 6 Clocks a 1/16 (no drift); a Hortator following the
 *            Clocks (CLK USB) finds the tempo
 *   ports    SYNC OFF / USB (/ BLE / BOTH in the Bluetooth build): only the chosen outputs
 *   thru     following a clock: its Clock / Start / Stop forwarded to the SYNC outputs, never back to the port it
 *            came from; THRU ON: notes / CCs from each input to every other output; OFF: nothing
 *   update   SysEx (an update) on USB: nothing sent while it runs and for 3 s after
 *   routing  ROUT NOTE (today's), SEL (the selected track, pitched from C4), TRACK (channels 1..8 = tracks 1..8,
 *            pitched); the keys' MIDI out on the track's channel with TRACK */
#define FELUCCA_OTA 1
#include "drum_host.h"
#include <math.h>
#if FELUCCA_BLE
#define T_BLE 1u
#else
#define T_BLE 0u
#endif

static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) {}

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

/* ---- the FM-1's timing (see the header) ---- */
#define T_TICK 2400u                                   /* TIMER5: 100 us of TIMER4 */
#define T_HALF ((double)HALF_FRAMES * 24000000.0 / FS)
static uint32_t now_t, audio_end, main_sub, main_owed;
static double half_at, load;
typedef struct { uint32_t pkt, t; } sent_t;
static sent_t sent[40000];
static uint32_t nsent;

static void usb_poll_model(void)                       /* usb_app.c ep1_tx: the ring out (the host takes it at once) */
{
    while (mo_r != mo_w) {
        if (nsent < 40000u)
            sent[nsent++] = (sent_t){midi_out_q[mo_r % MQ], now_t};
        mo_r++;
    }
}
static void hw_tick(void)                              /* 100 us: the audio IRQ when its half is due, TIMER5 */
{
    uint32_t in_audio;
    now_t += T_TICK;
    while ((double)now_t >= half_at) {                 /* the audio ISR (audio.c): its render, LOAD of a half long */
        uint32_t t0 = (uint32_t)half_at;
        fm1_ms = t0 / 24000u;
        mout_half(t0);
        render_mix(0, 0, HALF_FRAMES);
        audio_end = t0 + (uint32_t)(load * T_HALF);
        mout_half_done(audio_end);
        half_at += T_HALF;
    }
    fm1_ms = now_t / 24000u;
    in_audio = (int32_t)(audio_end - now_t) > 0;
    mout_tick(now_t, in_audio);                        /* felucca.c: before main.c's handler */
    if (main_sub % 5u == 0u)                           /* main.c fm1_timer5_irq */
        main_owed |= 1u;
    if (++main_sub == 10u)
        main_sub = 0;
    if (!in_audio) {
        if (main_owed & 1u)
            usb_poll_model();
        main_owed = 0;
    }
}
static void hw_run_ms(uint32_t ms)
{
    uint32_t i;
    for (i = 0; i < ms * 10u; i++)
        hw_tick();
}
#if FELUCCA_BLE
/* bt_midi.c's bt_sync_out as a device linked in MIDI mode takes it (its gating by MODE / link, its filter and its Clock
 * room: tests/bt_midi_test.c): what midi_out.c hands to Bluetooth */
static uint32_t bq[64], bq_w, bq_r;
static int bt_sync_out(uint32_t pkt, int rt)
{
    (void)rt;
    if (bq_w - bq_r >= 64u)
        return 0;
    bq[bq_w++ % 64u] = pkt;
    return 1;
}
static void bt_midi_out_isr(uint32_t pkt) { (void)pkt; }   /* (the keys' notes to the device: tests/bt_midi_test.c) */
static void bt_fml_block(uint32_t n, uint32_t *seq_n)      /* (FM-Link: tests/bt_fml_test.c) */
{
    (void)n;
    (void)seq_n;
}
#endif
static void hw_reset(void)
{
    host_init_live();
    memset(&mout, 0, sizeof mout);
    midi_thru = 0;
    mo_r = mo_w = 0;
    usb.config = 1;
    now_t = audio_end = main_sub = main_owed = 0;
    half_at = T_HALF;
    nsent = 0;
    sx_collect = 0;
    sx_ready = 0;
    so_r = so_w = 0;
    usb.sx_on = 0;
    usb.ota_req = 0;
#if FELUCCA_BLE
    bq_w = bq_r = 0;
#endif
}
static uint32_t st_of(uint32_t pkt) { return (pkt >> 8) & 0xFFu; }
static uint32_t count_st(uint32_t st)
{
    uint32_t i, n = 0;
    for (i = 0; i < nsent; i++)
        n += st_of(sent[i].pkt) == st;
    return n;
}

/* the sent Clocks' largest distance (us) from the least-squares line through them (the device's own tempo: its
 * beat is a whole number of samples) */
static double clock_jitter_us(double *bpm_out)
{
    double sn = 0, st = 0, snn = 0, snt = 0, a, b, worst = 0;
    uint32_t i, n = 0;
    for (i = 0; i < nsent; i++)
        if (st_of(sent[i].pkt) == 0xF8u) {
            double x = n, y = sent[i].t;
            sn += x;
            st += y;
            snn += x * x;
            snt += x * y;
            n++;
        }
    if (n < 3u)
        return 1e9;
    b = (n * snt - sn * st) / (n * snn - sn * sn);
    a = (st - b * sn) / n;
    for (i = 0, n = 0; i < nsent; i++)
        if (st_of(sent[i].pkt) == 0xF8u) {
            double r = fabs(sent[i].t - (a + b * n)) / 24.0;
            worst = r > worst ? r : worst;
            n++;
        }
    if (bpm_out)
        *bpm_out = 60.0 * 24000000.0 / (b * 24.0);
    return worst;
}

static void kit_pattern(void)                          /* hits on the tracks, so the render and the swing are real */
{
    uint32_t k, s;
    for (k = 0; k < 4u; k++)
        for (s = 0; s < 16u; s++)
            trk[k].step[s].on = (uint8_t)((s + k) % (k + 2u) == 0u);
}

/* SYNC USB, own clock: the jitter of the sent Clocks */
static double run_clock(int32_t bpm, int32_t swing, double ld, uint32_t ms, double *bpm_seen)
{
    hw_reset();
    load = ld;
    kit_pattern();
    song.g[G_BPM] = (int16_t)bpm;
    song.g[G_SWING] = (int16_t)swing;
    trk[1].p[P_SSWING] = (int16_t)swing;
    song.g[G_SYNC] = 1;
    hw_run_ms(20);
    transport_req = 1;
    hw_run_ms(ms);
    return clock_jitter_us(bpm_seen);
}

static void test_clock_jitter(void)
{
    static const int32_t BPM[3] = {120, 97, 180};
    static const double LOAD[3] = {0.05, 0.5, 0.8};
    uint32_t b, s, l;
    char what[160];
    for (l = 0; l < 3u; l++)
        for (b = 0; b < 3u; b++)
            for (s = 0; s < 2u; s++) {
                double seen = 0, j = run_clock(BPM[b], s ? 60 : 0, LOAD[l], 4000, &seen);
                /* the bound: half the gap between two USB polls (0.25 ms) where no render is near; a Clock due inside
                 * a render goes to the poll before it or the one after, whichever is nearer: half the render + a
                 * poll and a tick */
                double bound = 250.0 + LOAD[l] * T_HALF / 24.0 / 2.0 + 600.0 * (LOAD[l] > 0.1);
                snprintf(what, sizeof what, "clock out: %d BPM, swing %s, render %2.0f %%: jitter %4.0f us (<= %4.0f), "
                         "tempo %.2f", (int)BPM[b], s ? "60" : "off", LOAD[l] * 100.0, j, bound, seen);
                check(what, j <= bound && fabs(seen - BPM[b]) < 0.02 * BPM[b]);
            }
}

/* PLAY: Start before the first Clock; 24 Clocks a beat; STOP: Stop, nothing after it; no Continue from the own clock
 * (Hortator plays from the top: PLAY is a Start); PLAY while playing: Stop, Start */
static void test_transport(void)
{
    uint32_t i, first_clock = 0xFFFFFFFFu, start_at = 0xFFFFFFFFu, stop_at = 0xFFFFFFFFu, after_stop = 0, n;
    hw_reset();
    song.g[G_BPM] = 120;
    song.g[G_SYNC] = 1;
    hw_run_ms(50);
    check("transport: stopped: nothing sent", nsent == 0u);
    transport_req = 1;
    hw_run_ms(2000);                                   /* 4 beats at 120 */
    transport_req = 2;
    hw_run_ms(200);
    for (i = 0; i < nsent; i++) {
        uint32_t st = st_of(sent[i].pkt);
        if (st == 0xFAu && start_at == 0xFFFFFFFFu)
            start_at = i;
        if (st == 0xF8u && first_clock == 0xFFFFFFFFu)
            first_clock = i;
        if (st == 0xFCu && stop_at == 0xFFFFFFFFu)
            stop_at = i;
        if (stop_at != 0xFFFFFFFFu && i > stop_at)
            after_stop++;
    }
    n = count_st(0xF8u);
    check("transport: PLAY sends Start first, then the Clocks", start_at == 0u && first_clock == 1u);
    check("transport: 24 Clocks a beat (4 beats: 96, +-1)", n >= 95u && n <= 97u);
    check("transport: STOP sends Stop; nothing after it", stop_at != 0xFFFFFFFFu && after_stop == 0u);
    check("transport: no Continue from the own clock", count_st(0xFBu) == 0u);
    check("transport: one packet each (USB-MIDI CIN F, cable 0)", (sent[start_at].pkt & 0xFFu) == 0x0Fu &&
          sent[start_at].pkt == 0xFA0Fu && sent[first_clock].pkt == 0xF80Fu && sent[stop_at].pkt == 0xFC0Fu);
    nsent = 0;
    transport_req = 1;
    hw_run_ms(300);
    transport_req = 1;                                 /* PLAY again while playing: from the top */
    hw_run_ms(300);
    {
        uint32_t fa = 0, fc_before_fa = 0;
        for (i = 0; i < nsent; i++) {
            if (st_of(sent[i].pkt) == 0xFAu) {
                fa++;
                if (fa == 2u)
                    fc_before_fa = i > 0u && st_of(sent[i - 1u].pkt) == 0xFCu;
            }
        }
        check("transport: PLAY while playing (from the top): Stop, then Start", fa == 2u && fc_before_fa);
    }
    nsent = 0;
    song.g[G_SYNC] = 0;
    transport_req = 2;
    hw_run_ms(100);
    transport_req = 1;
    hw_run_ms(500);
    transport_req = 2;
    hw_run_ms(100);
    check("transport: SYNC OFF: nothing at all", nsent == 0u);
    nsent = 0;
    song.g[G_SYNC] = 1;
    usb.config = 0;                                    /* no computer */
    transport_req = 1;
    hw_run_ms(500);
    check("transport: no USB host: nothing queued", mo_w == mo_r && nsent == 0u);
}

/* a tempo walk (BPM every 40 ms, swing on): 6 Clocks for each 1/16 the sequencer's grid (Grids' clock) has started,
 * at every block: no drift, none lost or extra */
static void test_tempo_changes(void)
{
    uint32_t i, worst = 0, rng = 5;
    int32_t bpm = 120;
    hw_reset();
    load = 0.3;
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 40;
    song.g[G_SYNC] = 1;
    transport_req = 1;
    hw_run_ms(10);
    for (i = 0; i < 300u; i++) {
        uint32_t want, got, d;
        rng = rng * 1664525u + 1013904223u;
        bpm = clamp(bpm + (int32_t)((rng >> 28) % 9u) - 4, 60, 240);
        song.g[G_BPM] = (int16_t)bpm;
        hw_run_ms(40);
        want = 6u * (gclk.cnt + 1u);                  /* the 1/16s started (cnt from 0) */
        got = mout.pulses;
        d = got > want ? got - want : want - got;
        worst = d > worst ? d : worst;
    }
    check("tempo changes: 6 Clocks a 1/16 throughout a tempo walk with swing (no drift)", worst <= 6u);
    song.g[G_BPM] = 200;
    hw_run_ms(50);
    nsent = 0;
    hw_run_ms(3000);
    {
        double seen, j = clock_jitter_us(&seen);
        char what[120];
        snprintf(what, sizeof what, "tempo changes: after a jump to 200 BPM the Clocks run at it (%.1f), steady (%0.f us)",
                 seen, j);
        check(what, fabs(seen - 200.0) < 1.0 && j < 2000.0);
    }
}

/* another device follows our Clocks: Hortator itself with CLK USB, fed the sent stream (Start, Clocks) at their
 * times: it finds the tempo and its steps move 1/16 for every 6 Clocks */
static void test_follower(void)
{
    static sent_t rec[20000];
    static const int32_t BPM[3] = {120, 97, 180};
    uint32_t b;
    for (b = 0; b < 3u; b++) {
        uint32_t n, i, k = 0, clocks = 0;
        uint64_t smp = 0;
        char what[120];
        run_clock(BPM[b], 0, 0.5, 6000, 0);
        n = nsent < 20000u ? nsent : 20000u;
        memcpy(rec, sent, n * sizeof rec[0]);
        host_init_live();
        mi_r = mi_w = 0;
        song.g[G_CLOCK] = 1;
        song.g[G_BPM] = 100;
        render_mix(0, 0, CTL);
        for (i = 0; i < n; i++)
            rec[i].t -= rec[0].t;
        while (k < n) {                                /* the packets into the input ring at their times, block by block */
            uint64_t end = smp + CTL;
            while (k < n && (uint64_t)rec[k].t * FS / 24000000u < end) {
                fm1_ms = (uint32_t)(rec[k].t / 24000u);
                midi_enqueue(rec[k].pkt, 1u);
                clocks += st_of(rec[k].pkt) == 0xF8u;
                k++;
            }
            fm1_ms = (uint32_t)(end * 1000u / FS);
            render_mix(0, 0, CTL);
            smp = end;
        }
        snprintf(what, sizeof what, "follower: a Hortator on CLK USB follows our %d BPM (it shows %d), 1/16 per 6 Clocks",
                 (int)BPM[b], (int)song.g[G_BPM]);
        check(what, abs(song.g[G_BPM] - BPM[b]) <= 1 && song.playing &&
                        (int32_t)(gclk.cnt + 1u) - (int32_t)(clocks / 6u) <= 1 &&
                        (int32_t)(clocks / 6u) - (int32_t)(gclk.cnt + 1u) <= 1);
    }
}

/* ---- the input side: packets in the ring as usb_app.c / midi_uart.c / bt_midi.c put them ---- */
static void feed(uint32_t pkt, uint32_t src)
{
    if (src == 2u) {                                   /* TRS: bytes through the parser */
        uint32_t st = (pkt >> 8) & 0xFFu;
        um_byte(st);
        if (st < 0xF0u) {
            um_byte((pkt >> 16) & 0x7Fu);
            if ((st & 0xE0u) != 0xC0u)
                um_byte((pkt >> 24) & 0x7Fu);
        }
    } else {
        midi_enqueue(pkt, src);
    }
}
static uint32_t usb_out(uint32_t *pkts, uint32_t max)  /* what the USB poll would send now */
{
    uint32_t n = 0;
    while (mo_r != mo_w) {
        if (n < max)
            pkts[n++] = midi_out_q[mo_r % MQ];
        mo_r++;
    }
    return n;
}
#if FELUCCA_BLE
static uint32_t ble_out(uint32_t *pkts, uint32_t max)
{
    uint32_t n = 0;
    while (bq_r != bq_w) {
        if (n < max)
            pkts[n++] = bq[bq_r % 64u];
        bq_r++;
    }
    return n;
}
#endif

/* following a clock from src (CLK = src), SYNC sync, THRU thru: one Clock, a note-on and a CC from src; where they go */
static void route_case(uint32_t src, uint32_t sync, uint32_t thru, uint32_t *usb_rt, uint32_t *usb_ch, uint32_t *ble_rt,
                       uint32_t *ble_ch)
{
    uint32_t p[16], n, i;
    hw_reset();
    song.g[G_CLOCK] = (int16_t)src;
    song.g[G_SYNC] = (int16_t)sync;
    midi_thru = (uint8_t)thru;
    render_mix(0, 0, CTL);                             /* (the clock mode taken) */
    fm1_ms = 1000;
    feed(0xFA0Fu, src);
    feed(0xF80Fu, src);
    feed(0x09u | 0x99u << 8 | 36u << 16 | 100u << 24, src);
    feed(0x0Bu | 0xB0u << 8 | 74u << 16 | 64u << 24, src);
    now_t = 24000000u;
    mout_tick(now_t, 0);
    *usb_rt = *usb_ch = *ble_rt = *ble_ch = 0;
    n = usb_out(p, 16);
    for (i = 0; i < n; i++) {
        if (st_of(p[i]) >= 0xF8u)
            ++*usb_rt;
        else
            ++*usb_ch;
    }
#if FELUCCA_BLE
    n = ble_out(p, 16);
    for (i = 0; i < n; i++) {
        if (st_of(p[i]) >= 0xF8u)
            ++*ble_rt;
        else
            ++*ble_ch;
    }
#endif
}

static void test_thru(void)
{
    static const char *const SRC[4] = {"", "USB", "TRS", "BLE"};
#if FELUCCA_BLE
    uint32_t nsrc = 3u, nsync = 4u;
#else
    uint32_t nsrc = 2u, nsync = 2u;
#endif
    uint32_t src, sync, thru, all = 1;
    char what[160];
    for (src = 1; src <= nsrc; src++)
        for (sync = 0; sync < nsync; sync++)
            for (thru = 0; thru < 2u; thru++) {
                uint32_t urt, uch, brt, bch, want_urt, want_brt, want_uch, want_bch;
                route_case(src, sync, thru, &urt, &uch, &brt, &bch);
                want_urt = (sync & 1u) && src != 1u ? 2u : 0u;      /* Start + Clock; never back to USB */
                want_brt = T_BLE && (sync & 2u) && src != 3u ? 2u : 0u;
                want_uch = thru && src != 1u ? 2u : 0u;             /* the note and the CC */
                want_bch = T_BLE && thru && src != 3u ? 2u : 0u;
                if (urt != want_urt || uch != want_uch || brt != want_brt || bch != want_bch) {
                    snprintf(what, sizeof what, "thru: from %s, SYNC %u, THRU %u: USB %u+%u BLE %u+%u (want %u+%u %u+%u)",
                             SRC[src], sync, thru, urt, uch, brt, bch, want_urt, want_uch, want_brt, want_bch);
                    check(what, 0);
                    all = 0;
                }
            }
    check("thru: every input x SYNC x THRU: clock thru to the SYNC outputs, notes / CCs to the others, never back "
          "to the input", all);
    {                                                  /* own clock (INT): incoming clocks are never forwarded */
        uint32_t p[16];
        hw_reset();
        song.g[G_SYNC] = 1;
        feed(0xF80Fu, 2u);
        mout_tick(24000000u, 0);
        check("thru: CLK INT: a TRS Clock is not forwarded (our own clock is sent)", usb_out(p, 16) == 0u);
    }
    {                                                  /* following TRS: USB's Clock is not the one followed */
        uint32_t p[16];
        hw_reset();
        song.g[G_CLOCK] = 2;
        song.g[G_SYNC] = 1;
        render_mix(0, 0, CTL);
        feed(0xF80Fu, 1u);
        mout_tick(24000000u, 0);
        check("thru: CLK TRS: a USB Clock is not forwarded", usb_out(p, 16) == 0u);
    }
    {                                                  /* forwarded on receipt: a TRS Clock leaves in the same tick */
        uint32_t p[4], n;
        hw_reset();
        song.g[G_CLOCK] = 2;
        song.g[G_SYNC] = 1;
        render_mix(0, 0, CTL);
        feed(0xF80Fu, 2u);
        mout_tick(24000000u, 0);
        n = usb_out(p, 4);
        check("thru: following TRS, its Clock is in the USB ring in the tick after it was read", n == 1u &&
              p[0] == 0xF80Fu);
    }
    {                                                  /* a computer not reading: the clock never fills the ring */
        uint32_t i;
        hw_reset();
        song.g[G_CLOCK] = 2;
        song.g[G_SYNC] = 1;
        render_mix(0, 0, CTL);
        for (i = 0; i < 200u; i++) {
            feed(0xF80Fu, 2u);
            mout_tick(24000000u + i * T_TICK, 0);
        }
        check("thru: a computer not reading: clocks stop at 8 waiting, room left for notes", mo_w - mo_r <= 8u);
    }
}

/* an update (SysEx on USB): no clock, no thru while it runs and for 3 s after its last message */
static void test_update_pause(void)
{
    uint32_t n0, n1, n2, kind;
    for (kind = 0; kind < 4u; kind++) {
        char what[120];
        static const char *const K[4] = {"a SysEx frame arriving", "a frame waiting", "a reply going out",
                                          "the update command"};
        hw_reset();
        song.g[G_BPM] = 120;
        song.g[G_SYNC] = 1;
        midi_thru = 1;
        transport_req = 1;
        hw_run_ms(500);
        n0 = nsent;
        if (kind == 0u)
            sx_collect = 1;
        else if (kind == 1u)
            sx_ready = 1;
        else if (kind == 2u)
            so_w = so_r + 3u;
        else
            usb.ota_req = 1;
        hw_run_ms(10);                                 /* (a frame or a reply in flight: a few ms at least) */
        sx_collect = sx_ready = 0;
        so_r = so_w;
        usb.ota_req = 0;
        feed(0x09u | 0x99u << 8 | 36u << 16 | 100u << 24, 2u);   /* a TRS note: THRU would send it */
        hw_run_ms(2890);
        n1 = nsent;
        hw_run_ms(600);
        n2 = nsent;
        snprintf(what, sizeof what, "update: %s pauses clock and thru for 3 s (%u sent), then the clock goes on",
                 K[kind], n1 - n0);
        check(what, n1 - n0 <= 1u && n2 - n1 >= 20u);
    }
}

/* ---- ROUT ---- */
static uint32_t ages[NTRK];
static void ages_take(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        ages[i] = hit_age(&trk[i]);
}
static uint32_t hits(void)                             /* tracks hit since ages_take, bit per track */
{
    uint32_t i, b = 0;
    for (i = 0; i < NTRK; i++)
        b |= (uint32_t)(hit_age(&trk[i]) != ages[i]) << i;
    return b;
}
static uint32_t note_in(uint32_t ch, uint32_t note, uint32_t vel)   /* a note-on from USB; the tracks it hit */
{
    ages_take();
    midi_enqueue(0x09u | (0x90u | ch) << 8 | note << 16 | vel << 24, 1u);
    render_mix(0, 0, CTL);
    return hits();
}

static void test_routing(void)
{
    uint32_t i, ok;
    hw_reset();
    check("ROUT: NOTE by default (0), SYNC OFF by default (0)", GP[G_ROUTE].def == 0 && GP[G_SYNC].def == 0 &&
          !strcmp(GP[G_ROUTE].names[0], "NOTE") && !strcmp(GP[G_SYNC].names[0], "OFF"));
    check("ROUT: NOTE SEL TRACK", GP[G_ROUTE].max == 2 && !strcmp(GP[G_ROUTE].names[1], "SEL") &&
          !strcmp(GP[G_ROUTE].names[2], "TRACK"));
#if FELUCCA_BLE
    check("SYNC: OFF USB BLE BOTH (the Bluetooth build)", GP[G_SYNC].max == 3 && !strcmp(GP[G_SYNC].names[1], "USB") &&
          !strcmp(GP[G_SYNC].names[2], "BLE") && !strcmp(GP[G_SYNC].names[3], "BOTH"));
#else
    check("SYNC: OFF USB (no TRS out on the FM-1, no Bluetooth)", GP[G_SYNC].max == 1 &&
          !strcmp(GP[G_SYNC].names[1], "USB"));
#endif
    /* NOTE: the drum channel, every track whose NOTE matches, at C4 */
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_NOTE] = (int16_t)(36 + i);
    trk[5].p[P_NOTE] = 36;
    song.g[G_DRCH] = 10;
    ok = note_in(9, 36, 100) == (1u | 1u << 5) && trk[0].nt == 0 && trk[5].nt == 0;
    ok &= note_in(0, 36, 100) == 0u && note_in(9, 50, 100) == 0u;
    check("ROUT NOTE: the drum channel, the tracks whose NOTE matches (as before)", ok);
    /* SEL: any note on the drum channel plays the selected track, pitched from C4 */
    song.g[G_ROUTE] = 1;
    song.sel = 3;
    ok = note_in(9, 64, 100) == 1u << 3 && trk[3].nt == 4;
    ok &= note_in(9, 48, 90) == 1u << 3 && trk[3].nt == -12;
    ok &= note_in(9, 36, 100) == 1u << 3;              /* (no NOTE match: the selected track) */
    ok &= note_in(0, 60, 100) == 0u;                   /* another channel: nothing */
    song.sel = 6;
    ok &= note_in(9, 60, 100) == 1u << 6 && trk[6].nt == 0;
    check("ROUT SEL: notes on the drum channel play the selected track, pitched from C4; other channels nothing", ok);
    /* TRACK: channel 1..8 = track 1..8, pitched; the drum channel's own setting does not matter */
    song.g[G_ROUTE] = 2;
    ok = note_in(0, 60, 100) == 1u && trk[0].nt == 0;
    ok &= note_in(2, 55, 100) == 1u << 2 && trk[2].nt == -5;
    ok &= note_in(7, 72, 100) == 1u << 7 && trk[7].nt == 12;
    ok &= note_in(8, 60, 100) == 0u && note_in(9, 36, 100) == 0u && note_in(15, 60, 100) == 0u;
    check("ROUT TRACK: channels 1..8 play tracks 1..8, pitched from C4; channels 9..16 nothing", ok);
    /* the velocity, note-offs ignored */
    ages_take();
    midi_enqueue(0x08u | 0x82u << 8 | 55u << 16, 1u);
    midi_enqueue(0x09u | 0x92u << 8 | 55u << 16, 1u);   /* note-on velocity 0 = off */
    render_mix(0, 0, CTL);
    check("ROUT TRACK: note-offs play nothing", hits() == 0u);
    /* recorded: an armed track while playing keeps the note on the step */
    {
        uint32_t s, found = 0;
        hw_reset();
        song.g[G_ROUTE] = 2;
        for (s = 0; s < NSTEP; s++)
            trk[2].step[s].on = 0;
        song.rec = 1u << 2;
        transport_req = 1;
        render_mix(0, 0, CTL * 4u);
        note_in(2, 67, 100);
        for (s = 0; s < 16u; s++)
            found |= trk[2].step[s].on && trk[2].step[s].nt == 7;
        song.rec = 0;
        check("ROUT TRACK: a note into an armed track is recorded with its pitch", found);
    }
    {                                                  /* the keys' MIDI out: the drum channel and NOTE; TRACK: the
                                                        * track's channel, C4 */
        uint32_t p[8], n;
        hw_reset();
        trk[2].p[P_NOTE] = 40;
        song.g[G_DRCH] = 10;
        fm1_in.notes = 1u << KEY_TRK_KEY[2];
        render_mix(0, 0, CTL);
        fm1_in.notes = 0;
        render_mix(0, 0, CTL);
        n = usb_out(p, 8);
        ok = n == 2u && p[0] == (0x09u | 0x99u << 8 | 40u << 16 | 100u << 24) && p[1] == (0x08u | 0x89u << 8 | 40u << 16);
        song.g[G_ROUTE] = 2;
        fm1_in.notes = 1u << KEY_TRK_KEY[2];
        render_mix(0, 0, CTL);
        fm1_in.notes = 0;
        render_mix(0, 0, CTL);
        n = usb_out(p, 8);
        ok &= n == 2u && p[0] == (0x09u | 0x92u << 8 | 60u << 16 | 100u << 24) && p[1] == (0x08u | 0x82u << 8 | 60u << 16);
        check("ROUT: the keys' MIDI out: NOTE / SEL on the drum channel with NOTE, TRACK on the track's channel at C4", ok);
    }
}

int main(void)
{
    test_routing();
    test_transport();
    test_clock_jitter();
    test_tempo_changes();
    test_follower();
    test_thru();
    test_update_pause();
    printf("%s\n", fails ? "SYNC TESTS FAILED" : "sync_test: all passed");
    return fails ? 1 : 0;
}
