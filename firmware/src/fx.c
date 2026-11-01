/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Effects: per-track DIST insert, then sends into three
 * shared buses (chorus, tempo delay, reverb). Mono buses, stereo dry mix; the delay's PING-PONG and the reverb's HALL
 * return stereo (their left - right in dly_side). */
#define DLY_LEN 65536u           /* 1.49 s: 1/4 at 40 BPM fits */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
#if FELUCCA_BLE                  /* the Bluetooth build: in .bss, so the pool keeps its 8 KiB spare beside the BT heap
                                 * (23,552 B) and task stacks (tools/build.py); .bss keeps its 4 KiB */
static int16_t cho_buf[CHO_LEN];
#else
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
#endif
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps); HALL's diffusers */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;   /* BASS+ (spk_bass) */
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING, (Skiff) 2 HALL) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
} fx;
static struct {                          /* the delay's MODE (G_DMODE; fx's layout kept: bench.c) */
    uint8_t mode;                        /* the mode running: 0 NORMAL, 1 PING-PONG */
    uint8_t side;                        /* dly_side holds this block's left - right (PING-PONG or HALL ran) */
    uint32_t clr;                        /* a mode change: the line's samples still to clear (the delay silent) */
    int32_t lp1, lp2, s;                 /* PING-PONG: the low-passes of its two passes, the right's last entry */
} dpp;
static int32_t dly_side[CTL];            /* PING-PONG, HALL: the return's left - right (the left = wet + this) */

/* SPRING (G_RTYPE 1, upstream 1.0.2): one spring of a spring tank, mono like the other buses, in the ROOM's own
 * buffers (no RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass)
 * -> SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");

/* (Skiff) HALL (G_RTYPE 2; Skiff 1.3.2's, Skiff #19, Felucca's): a smooth, dense, stereo reverb, again in ROOM's
 * buffers (its few states and one block of work in the pool, no .bss). ROOM's 4 combs each ring on their own (the
 * metallic tone); here every echo goes round 8 lines that all feed each other, and they run at half the rate
 * (22.05 kHz: the same memory holds twice the time, so twice the resonances, at half the work; the tail stops near
 * 10 kHz, as a hall's air does). The send (every other sample of it, low-passed 1 2 1; ROOM's level) -> a low cut
 * (~110 Hz: no boom in a long tail) -> 2 allpass diffusers (rev_ap: a drum's click smeared into a burst) -> into the
 * 8 lines (rev_comb, mutually prime lengths, 21 .. 34 ms). Lines 1, 2, 4, 7 are read through slow sines (+-HL_MOD
 * samples, between samples; 0.37 or 0.74 Hz, each in its own phase): the resonances drift instead of ringing. Lines
 * 0, 3, 5, 6, read at their ends, go through a one-pole low-pass (DAMP: the mixing takes every echo through them);
 * every line through its gain (from its length: all decay alike; RT60 below the damping 0.5 s at SIZE 0, 4 s at 90,
 * 9 s at 127; ~2.4 s heard at 90 with DAMP 60); then the 8 x 8 mixing: a conference matrix (Paley, order 8: every
 * line feeds the 7 others, +-1 each, none itself: no flutter at a line's own length), with the send added. Stereo:
 * left lines 0 3 4 -7, right 1 2 5 6 (sums spread over the matrix's rows); the wet bus gets left + right, rev_side
 * left - right, which fx_buses folds into the return's stereo (rev_side_fold: wet the right, dly_side left - right,
 * as the delay's PING-PONG). Back to 44.1 kHz between samples. Rounding: a loud line's gain rounds to the nearest
 * (the tail decays as it should), a quiet one's towards zero (it ends in exact silence: no limit cycle), and the
 * low-passes keep their steps' remainders (no offset). */
#define HL_NL 8u                         /* lines */
#define HL_NAP 2u                        /* diffusers */
#define HL_NC (CTL / 2u)                 /* a block at half the rate */
#define HL_MOD 8                         /* the taps' swing, samples (read 2 .. 18 samples short of the end) */
#define HL_LPM 0x69u                     /* the lines with a low-pass */
#define HL_KD(d) (32767 - (d) * 180)     /* its coefficient (Q15) for DAMP d: none at 0 .. ~1 kHz at 127 */
#define HL_LOUD 32                       /* a line this loud (two samples' sum) rounds its gain to the nearest */
#define HL_OUT 4                         /* the outputs' gain: the level of ROOM's tail */
static const uint16_t HL_LEN[HL_NL] = {467, 509, 547, 593, 631, 673, 709, 743};   /* (+1 each: the mirror) */
static const uint16_t HL_AP[HL_NAP] = {131, 307};
static const int16_t HL_APG[HL_NAP] = {11469, 10240};               /* Q14: 0.7, 0.625 */
_Static_assert(467 + 509 + 547 + 593 + 631 + 673 + 709 + 743 + HL_NL <= sizeof rev_comb / 2u &&
               131 + 307 <= sizeof rev_u.ap / 2u, "HALL in ROOM's buffers");
_Static_assert(CTL % 2u == 0u && HL_NC + 2u * HL_MOD + 2u < 467u && HL_NC <= 131u, "HALL: one wrap a block at most");
static struct {                          /* HALL's state (in the pool: no .bss) */
    uint16_t i[HL_NL], d[HL_NAP];        /* the lines' write indices, the diffusers' */
    int32_t lp[HL_NL], le[HL_NL];        /* the low-passes and their steps' remainders */
    int32_t x, m, s, hp, he;             /* the send's last sample, its low cut and that step's remainder; the
                                          * last mid and side out (half rate) */
    int16_t g[HL_NL], sz;                /* the lines' gains / sqrt(7) (Q14), for SIZE sz - 1 (0: none yet) */
    uint32_t ph;                         /* the taps' drift */
    uint8_t side;                        /* sd holds this block's stereo difference (HALL running) */
    int32_t sd[CTL];                     /* the block's left minus right (fx_buses: rev_side_fold) */
    int32_t bx[HL_NC], l[HL_NC], r[HL_NC];   /* the block at half the rate: the diffused send, left, right */
    int16_t y[HL_NL][HL_NC];             /* the lines' ends, then what goes back into them */
} hl __attribute__((section(".pool")));
#define rev_side (hl.sd)

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain. State per part (track_t dist_*). */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;                                         /* states kept: switching on does not click */
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* MENU > USB LEVEL FIXED (upstream 1.0.2, #42: record over USB with the speaker turned down): the mix goes to
 * master_out at the full MASTER level, USB audio takes that (audio.c uac_tap), and only then does MASTER scale what
 * the DAC gets (usb_fixed_dac). MASTER (0, the default): MASTER before master_out, as always (USB follows the knob) */
static volatile uint8_t fx_usb_fixed;
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
static __attribute__((noinline)) void usb_fixed_dac(int32_t *out, uint32_t n)   /* audio ISR, after uac_tap */
{
    uint32_t i;
    int32_t m = (int32_t)song.master_q12;
    for (i = 0; i < 2u * n; i++)
        out[i] = (out[i] * m) >> 12;                /* (|out| <= 32767 after the soft clip: fits) */
}
static volatile uint8_t fx_lowcut;     /* settings (MENU SPEAKER EQ): 0 FLAT, 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the
                                        * small speaker): 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;
/* METRO (GLOBAL 3/7, a device setting: project.c keeps it in the settings record's lowcut bits 5..6; not .noinit, so
 * only the flash record sets it): 0 OFF, 1 LOW, 2 MID, 3 HIGH. While playing, seq.c grids_tick marks each quarter of
 * Grids' clock (gclk.cnt % 4 == 0; bar: the bar's first, % 16): due = 1 + how far the clock had run past the quarter
 * when the block's advance (adv) ended. metro_block starts the click at that point mapped onto the block, a block less
 * a sample after it (own clock: always n - 1 samples, 0.7 ms): in the block whose start fires the beat's steps, and
 * exactly a beat after the click before */
static volatile uint8_t fx_metro;
static struct { uint32_t ph, inc, due, adv; int32_t env; uint8_t bar; } metro;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (It was rounded, and a rounded step of (x - dc) / 4096 stops moving at |x - dc| < 2048: a
 * constant offset of up to +-31 stayed at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+ (upstream 1.0.2): what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is
 * clipped at its own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and
 * added. The low-pass has 4 poles (#42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself, late,
 * and cancelled up to 6 dB of it; now under 0.5 dB). Upstream's text; its state lives in fx (no new globals). */
static inline int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    fx.sb_lp1 += ((m - fx.sb_lp1) * 692) >> 15;
    fx.sb_lp2 += ((fx.sb_lp1 - fx.sb_lp2) * 692) >> 15;
    fx.sb_lp3 += ((fx.sb_lp2 - fx.sb_lp3) * 692) >> 15;
    fx.sb_lp4 += ((fx.sb_lp3 - fx.sb_lp4) * 692) >> 15;
    a = fx.sb_lp4 < 0 ? -fx.sb_lp4 : fx.sb_lp4;
    if (a > fx.sb_env)
        fx.sb_env += (a - fx.sb_env) >> 2;
    else if (fx.sb_env > 0)
        fx.sb_env -= (fx.sb_env >> 11) + 1;
    t = clamp(fx.sb_lp4 * 8, -fx.sb_env, fx.sb_env);
    fx.sb_h1 += (t - fx.sb_h1) >> 5;
    u = t - fx.sb_h1;
    fx.sb_h2 += (u - fx.sb_h2) >> 5;
    u -= fx.sb_h2;
    fx.sb_hl += (u - fx.sb_hl) >> 3;
    return fx.sb_hl * 3;
}

static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        uint32_t sh = fx_lowcut == 2u ? 5u : 6u;    /* rounded step stopped at |x - lc| < 32: an offset) */
        int32_t b = fx_lowcut == 2u ? spk_bass((*l + *r) >> 1) : 0;
        *l = lowcut1(*l, &lc_l1, &lce[0], sh);
        *l = lowcut1(*l, &lc_l2, &lce[1], sh) + b;
        *r = lowcut1(*r, &lc_r1, &lce[2], sh);
        *r = lowcut1(*r, &lc_r2, &lce[3], sh) + b;
    }
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = softclip(*l);
    *r = softclip(*r);
}

static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)                    /* a quarter note: the clock's tempo when following, else BPM */
{
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM];
}
/* length of one division (N_DIV id) in samples at the song tempo: ids 0..5 a quarter / DEN, the slow ids 6..9
 * (1/2 1/1 2BAR 4BAR) a quarter x 2, 4, 8, 16 (upstream 1.0.2); the delay TIME's own ids 10..14 (N_DTIME: 4T 1/4D
 * 1/8D 1/16D 32T) a quarter x XN / XD, floored */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* beats = 1 / DEN; original ids fixed, slow rates append */
static const uint8_t DIV_XN[5] = {2, 3, 3, 3, 1}, DIV_XD[5] = {3, 2, 4, 8, 12};
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u)
         : div < 15u ? quarter * DIV_XN[div - 10u] / DIV_XD[div - 10u] : quarter / DIV_DEN[div % 6u];
}

#include "perform.c"                                 /* the FX hold layer's effects (the master) */
#include "insert.c"                                  /* SRR and the INSERT, per track (mix_part, trk_inserts) */

static uint32_t delay_samples(void)
{
    uint32_t s = div_samples((uint32_t)song.g[G_DTIME]);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* m / 2^sh rounded towards zero (Skiff's, HALL's and ROOM's): a product that feeds back must not floor (a loop of
 * floors holds an offset and a little noise for ever) */
static inline int32_t sh_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }

/* ROOM's comb k (its line at c, REV_COMB[k] long): a sample of in in, its output out; the low-pass and the feedback
 * rounded towards zero (below). Inlined 4 times (each comb's length and states at fixed places: the rounding's cost
 * paid by the loop it saves) */
static inline __attribute__((always_inline)) int32_t room_comb(int16_t *c, uint32_t k, int32_t in, int32_t size,
                                                               int32_t damp)
{
    uint32_t j = fx.comb_i[k];
    int32_t o = c[j], l = o + sh_tz((fx.comb_lp[k] - o) * (32767 - damp), 15);
    fx.comb_lp[k] = l;
    c[j] = (int16_t)clamp(in + sh_tz(l * size, 15), -32768, 32767);
    fx.comb_i[k] = (uint16_t)(j + 1u >= REV_COMB[k] ? 0u : j + 1u);
    return o;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out. The loops' products (the
 * combs' low-pass and feedback, the allpasses' half) round towards zero, as SPRING's and HALL's: the tail ends in
 * exact silence (rounded down, they held a DC offset and, at low DAMP, a faint buzz for ever) */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB */
        a = room_comb(c, 0, in, size, damp) + room_comb(c + 1116, 1, in, size, damp) +
            room_comb(c + 1116 + 1188, 2, in, size, damp) + room_comb(c + 1116 + 1188 + 1277, 3, in, size, damp);
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            uint32_t j = fx.ap_i[k];
            int32_t o = c[j];
            int32_t v = a + sh_tz(o, 1);
            c[j] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v would be a notch comb) */
            fx.ap_i[k] = (uint16_t)(j + 1u >= REV_AP[k] ? 0u : j + 1u);
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* (Skiff) HALL's helpers (sh_tz above rev_room) */
/* 2^-x, x Q16 (0 .. 16): Q15 (32768 at x = 0). 2^-x = 2^u / 2^(i + 1), u = 1 - frac(x); 2^u by a cubic (1e-4) */
static uint32_t exp2_neg(uint32_t x)
{
    uint32_t i = x >> 16, u = 32768u - ((x & 0xFFFFu) >> 1), p = 2579u;
    p = ((p * u) >> 15) + 7409u;
    p = ((p * u) >> 15) + 22780u;
    p = ((p * u) >> 15) + 32768u;
    return i < 15u ? p >> (i + 1u) : 0u;
}
/* the lines' gains for SIZE s: RT60 = 0.5 s * 2^(s / 30); a line of D samples (22.05 kHz) loses 60 dB in
 * RT60 * 22050 / D passes: g = 2^(-D * 3 log2(10) / (22050 * RT60)) = 2^(-D * 59.2 / 65536 * 2^(-s / 30)),
 * at most 0.99 (hall_gain), over sqrt(7) (the matrix's rows have 7 entries of +-1: / sqrt(7) makes it orthogonal) */
static uint32_t hall_gain(uint32_t len, uint32_t q)     /* a line of len samples' gain, Q15; q = 2^(-s / 30), Q15 */
{
    uint32_t g = exp2_neg((len * 59u * q) >> 15);
    return g > 32440u ? 32440u : g;
}
static __attribute__((noinline)) void hall_gains(int32_t s)
{
    uint32_t q = exp2_neg((uint32_t)s * 65536u / 30u), k;
    for (k = 0; k < HL_NL; k++)
        hl.g[k] = (int16_t)((hall_gain(HL_LEN[k], q) * 6193u) >> 15);
    hl.sz = (int16_t)(s + 1);
}

/* an allpass diffuser over x[0 .. n) from q (its next samples, no wrap): its state rounded towards zero */
static inline void hall_ap(int16_t *q, int32_t g, int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = q[i], v = x[i] + sh_tz(y * g, 14);
        q[i] = (int16_t)clamp(v, -32768, 32767);
        x[i] = y - ((v * g + 8192) >> 14);
    }
}
/* a line's end over y[0 .. n) from b (no wrap; b[n] is still the line's: its mirror at the end): between b[i]
 * and b[i + 1], f / 256 of the way (rounded to the nearest: it stays between the two), times its gain g (Q14).
 * The gain's product: rounded to the nearest while the line is loud (lo 0: the tail decays as it should; a
 * truncation would make it die early, the sooner the quieter), towards zero once it is quiet (lo 1: the last
 * steps of the tail, which then ends in exact silence) */
static inline void hall_end(const int16_t *b, int32_t f, int16_t *y, uint32_t n, int32_t g, int lo)
{
    uint32_t i;
    if (lo)
        for (i = 0; i < n; i++)
            y[i] = (int16_t)sh_tz((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g, 14);
    else
        for (i = 0; i < n; i++)
            y[i] = (int16_t)(((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g + 8192) >> 14);
}
/* a line's end over y[0 .. n) from b (no wrap), through the low-pass (kd, Q15; the state *lp and its step's
 * remainder *le), times its gain g (rounded as hall_end's) */
static inline void hall_end_lp(const int16_t *b, int16_t *y, uint32_t n, int32_t g, int lo, int32_t *lp, int32_t *le,
                               int32_t kd)
{
    uint32_t i;
    int32_t l = *lp, r = *le, e;
    if (lo)
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)sh_tz(l * g, 14);
        }
    else
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)((l * g + 8192) >> 14);
        }
    *lp = l;
    *le = r;
}
/* the block's HL_NC samples of a circular buffer of len from index d in at most two runs, no wrap inside them:
 * body with s (the run's start in the block) and k (its length); d moves on */
#define HL_RUNS(d, len, k, s, body)                                                         \
    for (s = 0; s < HL_NC; s += k) {                                                        \
        k = (len) - (d) < HL_NC - s ? (len) - (d) : HL_NC - s;                              \
        body;                                                                               \
        d = (d) + k >= (len) ? 0u : (d) + k;                                                \
    }

/* (Skiff) HALL (see the top): a block of the send (n = CTL samples: audio.c) -> the mid added to out, the side into
 * rev_side. At half the rate (HL_NC samples) in passes over the block, each with few states (every line is
 * longer than a block: what the lines give in this block was written before it): the send, every other
 * sample, low cut, diffusers (hl.bx); the lines' ends at their drifting taps, low-passed, scaled (hl.y); the
 * outputs and the matrix; back into the lines; the outputs back to 44.1 kHz. */
#define HL_LFO ((uint32_t)(0.37 * CTL / FS * 4294967296.0))   /* the drift: 0.37 Hz (even lines), 0.74 Hz (odd) */
static __attribute__((noinline)) void rev_hall(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    int16_t *b = rev_comb, *a = rev_ap;
    uint32_t i, k, s, m, d;
    int32_t kd = HL_KD(song.g[G_RDAMP]);
    int32_t xp = hl.x, hp = hl.hp, he = hl.he, mp = hl.m, sp = hl.s;
    (void)n;
    if (hl.sz != song.g[G_RSIZE] + 1)
        hall_gains(song.g[G_RSIZE]);
    for (i = 0; i < HL_NC; i++) {
        int32_t x = (xp + 2 * rev_in[2u * i] + rev_in[2u * i + 1u]) >> 2, v;   /* every other sample, 1 2 1 */
        xp = rev_in[2u * i + 1u];
        x = mulq15(clamp(x, -262143, 262143), 2580);    /* (ROOM's level) */
        v = x - hp + he;                                /* the low cut, its step's remainder kept */
        he = v & 31;
        hp += v >> 5;
        hl.bx[i] = x - hp;
    }
    hl.x = xp, hl.hp = hp, hl.he = he;
    for (k = 0; k < HL_NAP; k++) {                      /* the diffusers */
        d = hl.d[k];
        HL_RUNS(d, HL_AP[k], m, s, hall_ap(a + d, HL_APG[k], hl.bx + s, m));
        hl.d[k] = (uint16_t)d;
        a += HL_AP[k];
    }
    hl.ph += HL_LFO;
    for (k = 0; k < HL_NL; k++) {                       /* the lines' ends */
        int32_t g = hl.g[k], f = 0, lo;
        if ((HL_LPM >> k) & 1u)
            d = hl.i[k];                                /* (low-passed: the line's end) */
        else {                                          /* (drifting: HL_MOD + 2 +- HL_MOD samples short of it) */
            uint32_t r = (uint32_t)(((HL_MOD + 2) << 8) +
                                    ((osc_sine(hl.ph * (1u + (k & 1u)) + k * 0x2C000000u) * HL_MOD) >> 7));
            f = (int32_t)(r & 255u);
            d = hl.i[k] + (r >> 8);
            d = d >= HL_LEN[k] ? d - HL_LEN[k] : d;
        }
        s = d + HL_LEN[k] / 2u;                         /* quiet? (two samples half a line apart) */
        s = s >= HL_LEN[k] ? s - HL_LEN[k] : s;
        lo = (b[d] < 0 ? -b[d] : b[d]) + (b[s] < 0 ? -b[s] : b[s]) < HL_LOUD;
        if ((HL_LPM >> k) & 1u) {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end_lp(b + d, hl.y[k] + s, m, g, lo, &hl.lp[k], &hl.le[k], kd));
        } else {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end(b + d, f, hl.y[k] + s, m, g, lo));
        }
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {
        int32_t y0 = hl.y[0][i], y1 = hl.y[1][i], y2 = hl.y[2][i], y3 = hl.y[3][i];
        int32_t y4 = hl.y[4][i], y5 = hl.y[5][i], y6 = hl.y[6][i], y7 = hl.y[7][i], x = hl.bx[i];
        int32_t t, p0, p1, p2, p3, p4, p5, p6;
        hl.l[i] = y0 + y3 + y4 - y7;                    /* the outputs */
        hl.r[i] = y1 + y2 + y5 + y6;
        /* the matrix: C = [0 1; -1 Q], Q (7 x 7) = chi(j - i), the quadratic character mod 7 (+1 at distances
         * 1 2 4, -1 at 3 5 6): row 1 + i of Q y = 2 (the three at +1) - (the sum of the 7) + y_1+i */
        t = y1 + y2 + y3 + y4 + y5 + y6 + y7;
        p0 = y2 + y3 + y5, p1 = y3 + y4 + y6, p2 = y4 + y5 + y7, p3 = y5 + y6 + y1;
        p4 = y6 + y7 + y2, p5 = y7 + y1 + y3, p6 = y1 + y2 + y4;
        y0 += t;
        hl.y[0][i] = (int16_t)clamp(t + x, -32768, 32767);   /* (+ the send: signs spread over the rows) */
        hl.y[1][i] = (int16_t)clamp(2 * p0 + y1 - y0 + x, -32768, 32767);
        hl.y[2][i] = (int16_t)clamp(2 * p1 + y2 - y0 + x, -32768, 32767);
        hl.y[3][i] = (int16_t)clamp(2 * p2 + y3 - y0 + x, -32768, 32767);
        hl.y[4][i] = (int16_t)clamp(2 * p3 + y4 - y0 + x, -32768, 32767);
        hl.y[5][i] = (int16_t)clamp(2 * p4 + y5 - y0 + x, -32768, 32767);
        hl.y[6][i] = (int16_t)clamp(2 * p5 + y6 - y0 + x, -32768, 32767);
        hl.y[7][i] = (int16_t)clamp(2 * p6 + y7 - y0 - x, -32768, 32767);
    }
    b = rev_comb;
    for (k = 0; k < HL_NL; k++) {                       /* back into the lines (and the mirror after a wrap) */
        d = hl.i[k];
        HL_RUNS(d, HL_LEN[k], m, s, for (i = 0; i < m; i++) b[d + i] = hl.y[k][s + i]; if (!d) b[HL_LEN[k]] = b[0]);
        hl.i[k] = (uint16_t)d;
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {                       /* back to 44.1 kHz, between samples */
        int32_t mm = (hl.l[i] + hl.r[i]) * HL_OUT, ss = (hl.l[i] - hl.r[i]) * HL_OUT;
        out[2u * i] += (mp + mm) >> 1;
        out[2u * i + 1u] += mm;
        rev_side[2u * i] = (sp + ss) >> 1;
        rev_side[2u * i + 1u] = ss;
        mp = mm;
        sp = ss;
    }
    hl.m = mp, hl.s = sp;
    hl.side = 1;
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept (as */
        fx.sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.ap / 2u; i++)          /* (int16: 997 of them, an odd count the int32 view misses one
                                                         * of: ROOM would play the stale one back after a switch as a
                                                         * click; Felucca 1.0.5.2, PR #121) */
        rev_u.ap[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.comb_lp[i] = 0;
    for (i = 0; i < HL_NL; i++)
        hl.lp[i] = hl.le[i] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
    hl.x = hl.m = hl.s = hl.hp = hl.he = 0;
}
static void rev_model(uint32_t m, const int32_t *rev_in, int32_t *out, uint32_t n)   /* G_RTYPE m, added to out */
{
    if (m == 1u)
        rev_spring(rev_in, out, n);
    else if (m == 2u)
        rev_hall(rev_in, out, n);
    else
        rev_room(rev_in, out, n);
}

static int32_t part_buf[CTL];                            /* a part's block (mix_part); the fade of a model change */
static int32_t mix_fade = 32767;                         /* every track's gain at this block's start, Q15 (cue.c: a
                                                          * slot switch's 5 ms fade out) */
static int32_t mix_fade_d;                               /* .. a sample's step (0: still) */
static uint32_t mix_fade_at = CTL;                       /* .. full gain from this sample of the block on (the switch) */
/* a track's block times the cue's fade (cue.c), before its level and sends; noinline: mix_part's loops stay as
 * they were (target_budget.py) */
static __attribute__((noinline)) void mix_fade_block(int32_t *b, uint32_t n)
{
    int32_t g = mix_fade;
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (i >= mix_fade_at)
            g = 32767;
        else
            g = clamp(g + mix_fade_d, 0, 32767);
        b[i] = (int32_t)(((int64_t)b[i] * g) >> 15);
    }
}

/* the model changed (to rt): the old one's block fades out (HALL's stereo difference with it), its buffers are
 * cleared, the new one starts from silence */
static __attribute__((noinline)) void rev_switch(const int32_t *rev_in, int32_t *wet, uint32_t n, int32_t rt)
{
    int32_t *t = part_buf, g = 65536, d = 65536 / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++)
        t[i] = 0;
    rev_model(fx.rtype, rev_in, t, n);
    for (i = 0; i < n; i++, g -= d) {
        wet[i] += mulq16(t[i], (uint32_t)g);
        rev_side[i] = mulq16(rev_side[i], (uint32_t)g);   /* (HALL only: hl.side set) */
    }
    rev_clear();
    fx.rtype = (uint8_t)rt;
}
/* (Skiff) HALL ran: its stereo difference into the return as the delay's PING-PONG gives its own: the right (wet)
 * mid - side, dly_side (the left minus the right) + 2 side; noinline: fx_buses' loop code stays as it was */
static __attribute__((noinline)) void rev_side_fold(int32_t *wet, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        wet[i] -= rev_side[i];
        dly_side[i] = (dpp.side ? dly_side[i] : 0) + 2 * rev_side[i];
    }
    dpp.side = 1;
}

/* chorus: modulated short delay, 5..15 ms; one sample of in, its output */
static inline __attribute__((always_inline)) int32_t cho_tick(int32_t in, uint32_t cinc, int32_t cdepth)
{
    int32_t r;
    cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(in >> 1, -32768, 32767);
    fx.cho_ph += cinc;
    r = (400 << 8) + ((osc_sine(fx.cho_ph) + 32768) * cdepth >> 8);   /* Q8 delay: read between samples */
    {
        uint32_t ri = (uint32_t)r >> 8;
        int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
        int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
        fx.cho_w++;
        return (c0 + (((c1 - c0) * f) >> 8)) << 1;
    }
}

/* PING-PONG (G_DMODE 1): the echoes of a mono send alternate left and right, at NORMAL's longest TIME too, in
 * NORMAL's line (no more RAM). The line runs at half the rate: each pair of sends, averaged (the decimation's
 * low-pass), is one entry h[m], so its 65536 entries hold 2.97 s and a feedback tap at 2 d fits (d = TIME, up to
 * DLY_LEN - 1 samples as NORMAL). One loop, two passes of FEEDBACK and COLOR per round trip:
 *   s[m] = FEEDBACK x lowpass1(h[m])             the right's echo (one pass)
 *   h[m] = send pair + FEEDBACK x lowpass2(s[m - d])   (d entries = 2 d samples: the round trip)
 *   left  = h read d samples back (d / 2 entries), right = s read 2 d samples back (d entries)
 * read between entries by linear interpolation (a half entry: the mean of two). A send's echoes: the left at d, the
 * right at 2 d (one pass), the left at 3 d (two), ...: NORMAL's echoes, at NORMAL's level, every other one on the
 * other side. The low-passes run at the half rate with NORMAL's corner (1 - (1 - col)^2); the decimation and the
 * interpolation soften the top end a little (an echo -3 dB near 7 kHz against NORMAL). n even (CTL); the delay's
 * left - right into dly_side, the right added to wet (the master and USB 10 CH FX add dly_side on the left) */
#define DLY_HM (DLY_LEN - 1u)
_Static_assert(CTL % 2u == 0u, "PING-PONG: a block is whole pairs");
static __attribute__((noinline)) void dly_pp(const int32_t *dly_in, int32_t *wet, uint32_t n)
{
    uint32_t i, d = delay_samples(), e = (d + 1u) >> 1, w = fx.dly_w;
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240, dmix = song.g[G_DMIX] * 258;
    int32_t c2 = 2 * col - ((col * col) >> 15), mo = -(int32_t)(d & 1u);   /* (odd d: the left a half entry on) */
    int32_t lp1 = dpp.lp1, lp2 = dpp.lp2, s0 = dpp.s;
    for (i = 0; i < n; i += 2u, w++) {
        int32_t a = dly_buf[(w - e) & DLY_HM], b = dly_buf[(w - e + 1u) & DLY_HM], h1 = (b - a) >> 1;
        int32_t s1, l0, l1, r0, r1;
        lp1 += mulq15(dly_buf[(w - d + 1u) & DLY_HM] - lp1, c2);   /* the right's next: s[m - d + 1] */
        s1 = mulq15(lp1, fb);
        lp2 += mulq15(s0 - lp2, c2);                    /* the round trip: s[m - d] */
        dly_buf[w & DLY_HM] = (int16_t)clamp(((dly_in[i] + dly_in[i + 1u]) >> 2) + mulq15(lp2, fb), -32768, 32767);
        l0 = mulq15((a + (h1 & mo)) << 1, dmix);         /* d even: a, the mean; odd: the mean, b */
        l1 = mulq15((a + h1 + ((b - a - h1) & mo)) << 1, dmix);
        r0 = mulq15(s0 << 1, dmix);                      /* s[m - d], the mean of it and s[m - d + 1] */
        r1 = mulq15((s0 + s1) & ~1, dmix);
        wet[i] += r0;
        wet[i + 1u] += r1;
        dly_side[i] = l0 - r0;
        dly_side[i + 1u] = l1 - r1;
        s0 = s1;
    }
    fx.dly_w = w;
    dpp.lp1 = lp1;
    dpp.lp2 = lp2;
    dpp.s = s0;
    dpp.side = 1;
}

/* the delay's mode changed (G_DMODE): the line is cleared, DLY_CLR samples a block (the delay silent till done; its
 * states from 0), so nothing of the other mode's echoes plays back; NORMAL runs on in fx_buses' loop */
#define DLY_CLR 4096u
static __attribute__((noinline)) void dly_alt(const int32_t *cho_in, const int32_t *dly_in, int32_t *wet, uint32_t n)
{
    uint32_t i, m = song.g[G_DMODE] == 1;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    int32_t cdepth = song.g[G_CDEPTH] * 6;
    for (i = 0; i < n; i++)
        wet[i] = cho_tick(cho_in[i], cinc, cdepth);     /* the chorus as in fx_buses' loop */
    if (m != dpp.mode) {
        dpp.mode = (uint8_t)m;
        dpp.clr = DLY_LEN;
        dpp.lp1 = dpp.lp2 = dpp.s = 0;
        fx.dly_lp = 0;
    }
    if (dpp.clr) {
        int16_t *p = dly_buf + (DLY_LEN - dpp.clr);
        for (i = 0; i < DLY_CLR; i++)
            p[i] = 0;
        dpp.clr -= DLY_CLR;
        return;
    }
    if (m)
        dly_pp(dly_in, wet, n);
}

/* process the three buses for one block; sends in, wet out (stereo-equal; PING-PONG, HALL: + dly_side on the left) */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet,
                     uint32_t n)
{
    uint32_t i, dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t cdepth = song.g[G_CDEPTH] * 6, rt;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    dpp.side = 0;
    if (song.g[G_DMODE] | dpp.mode | dpp.clr)
        dly_alt(cho_in, dly_in, wet, n);                /* PING-PONG, a mode change's clearing (the chorus too) */
    else
        for (i = 0; i < n; i++) {
            int32_t y = cho_tick(cho_in[i], cinc, cdepth), x;
            /* delay with a low-passed feedback */
            x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
            fx.dly_lp += mulq15(x - fx.dly_lp, col);
            dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
                (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
            fx.dly_w++;
            y += mulq15(x << 1, dmix);
            wet[i] = y;
        }
    rt = song.g[G_RTYPE] == 1 || song.g[G_RTYPE] == 2 ? song.g[G_RTYPE] : 0;   /* ((Skiff) HALL) */
    hl.side = 0;
    if (rt != fx.rtype)
        rev_switch(rev_in, wet, n, rt);
    else
        rev_model((uint32_t)rt, rev_in, wet, n);
    if (hl.side)
        rev_side_fold(wet, n);
}

/* one block of the whole mix (shared with the host tests): events -> each drum track -> RESON -> FILTER -> SRR -> dist
 * -> INSERT -> SLICER -> level / pan / sends -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static void midi_drop(void);                             /* seq.c */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet[CTL], mix_l[CTL], mix_r[CTL];
/* MENU > USB AUDIO 10 CH (ui_menu.c, usb_app8.c): each track's block for USB channels 3..10. The track as the dry mix
 * gets it (mix_part's x: after the inserts, DIST, SLICER, the mutes, the COMP duck and LEVEL) before PAN, the sends
 * and the master FX, scaled as the master scales the mix (MASTER, or full with USB LEVEL FIXED): with the pans centred
 * and no sends, the tracks add up to the master bus before its limiter (mix_l / mix_r), to rounding. Out of line, after
 * mix_part's loop, and only in 10 CH: 2 CH's mix is as before. A track that does not reach the mix this block stays 0. */
#if defined(FELUCCA_BLE) && FELUCCA_BLE
#define USB10_BUILD 0                           /* 10 CH: the standard build only (the Bluetooth build's RAM is full) */
#else
#define USB10_BUILD 1
#endif
#if USB10_BUILD
#define USB8_TAP_DEFINED 1
static volatile uint8_t usb10;                  /* the audio input the host enumerated: 0 2 CH, 1 10 CH (ui_menu.c) */
static volatile uint32_t usb_hold_until;        /* the reconnect (ui_menu.c): CLK USB's lost-clock stop waits till then */
static int16_t usb8_tap[NTRK][CTL];
/* 10 CH FX (MENU > USB AUDIO, ui_menu.c): channels 9-10 carry the send effects' return instead of the master: the
 * chorus, delay and reverb buses' output (fx_buses' wet, mono: the same on both, but for the delay's PING-PONG: its
 * left - right, dly_side, on channel 9; THROW's share in it), at the master's
 * level as the tracks are, before PERFORM's master effects, METRO and master_out (DC block, SPEAKER EQ, limiter, soft
 * clip). T1..T8 panned as on the FM-1 plus this pair add up to the master bus before those (mix_l / mix_r), to
 * rounding. MIX <-> FX cross-fades over 256 samples (5.8 ms; usb8_main). */
static volatile uint8_t usb_fxpair;             /* 1: 10 CH FX, 0: 10 CH MIX (the master on 9-10) */
static int16_t usb8_wet[CTL], usb8_wet_l[CTL];   /* the return: channel 10 (and 9), channel 9 */
static int32_t usb8_pair[2 * CTL], usb8_g;       /* channels 9-10 for uac8_tap; the cross-fade, 0 MIX .. 32768 FX */
static __attribute__((noinline)) void usb8_wet_tap(const int32_t *w, uint32_t n)
{
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (mix_block's mg) */
    uint32_t i;
    for (i = 0; i < n; i++) {
        usb8_wet[i] = (int16_t)clamp(((clamp(w[i], -2097151, 2097151) >> 2) * mg) >> 10, -32768, 32767);
        usb8_wet_l[i] = dpp.side ? (int16_t)clamp(((clamp(w[i] + dly_side[i], -2097151, 2097151) >> 2) * mg) >> 10,
                                                  -32768, 32767) : usb8_wet[i];
    }
}
/* audio ISR, after mix_block: channels 9-10 of the 10 CH stream (out: the master, Q15 stereo); MIX: out itself */
static __attribute__((noinline)) const int32_t *usb8_main(const int32_t *out, uint32_t n)
{
    int32_t to = usb_fxpair ? 32768 : 0, g = usb8_g;
    uint32_t i;
    if (!g && !to)
        return out;
    for (i = 0; i < n; i++) {
        g += clamp(to - g, -128, 128);
        usb8_pair[2u * i] = out[2u * i] + (((usb8_wet_l[i] - out[2u * i]) * (g >> 1)) >> 14);
        usb8_pair[2u * i + 1u] = out[2u * i + 1u] + (((usb8_wet[i] - out[2u * i + 1u]) * (g >> 1)) >> 14);
    }
    usb8_g = g;
    return usb8_pair;
}
static __attribute__((noinline)) void usb8_track_tap(uint32_t k, const int32_t *b, int32_t lvl, uint32_t n)
{
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (mix_block's mg) */
    int16_t *o = usb8_tap[k];
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = clamp(((b[i] >> 2) * lvl) >> 10, -524287, 524287);   /* mix_part's x */
        o[i] = (int16_t)clamp(((x >> 2) * mg) >> 10, -32768, 32767);     /* as mix_block scales the bus */
    }
}
static __attribute__((noinline)) void usb8_tap_clear(uint32_t n)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < n; i++)
            usb8_tap[k][i] = 0;
}
#endif

/* one track into the dry mix and the sends; a track with no voice sounding costs the LFO tick and a cleared
 * buffer only (after the DIST tail has run out). src: the COMP source (NTRK = off). The source's block (after
 * DIST / SLICER, before LEVEL and MUTE) feeds the compressor; a muted source (trk_silent: SOLO too) is heard by it
 * only (ghost key) with GHOST KEEP, by nobody with MUTE; with HIDE the source is never heard in the mix (G_CGHOST). A DUCK track is multiplied by the compressor's gain before its level, pan and sends. */
static void mix_part(track_t *t, uint32_t n, uint32_t src)
{
    int32_t *b = part_buf;
    uint32_t i, is_src = (uint32_t)(t - trk) == src, snd = track_render(t, b, n);
    snd = trk_inserts(t, b, n, snd);                   /* RESON, then FILTER (filter.c), SRR (insert.c), before DIST */
    if (snd)
        t->tail = (uint8_t)(16 + 48 * ins_on(t));       /* blocks of DIST / INSERT state to run out after the last voice
                                                         * (the INSERT's: 46 ms, a flanger's feedback) */
    else if ((!t->tail || !(t->p[P_DIST] | ins_on(t)) || !--t->tail) && !slicer_busy(t)) {
        ins_idle(t);                                    /* (insert.c: TYPE OFF / MIX 0 meanwhile: at rest) */
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        if (is_src) {
            comp_block(0, n);                           /* silence: the detector decays */
            t->gfade = trk_silent(t) || song.g[G_CGHOST] == CG_HIDE ? 32767u : 0u;   /* nothing sounds: at once */
        }
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN];
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
        int32_t xmax = c > d ? c : d;
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        track_dist(t, b, n);
        track_insert(t, b, n);                          /* insert.c: after DIST, before the SLICER (OFF: nothing) */
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
        if (is_src) {
            int sil = trk_silent(t), hide = sil || song.g[G_CGHOST] == CG_HIDE;
            comp_block(sil && song.g[G_CGHOST] == CG_MUTE ? 0 : b, n);   /* GHOST MUTE: muted keys nothing */
            if (hide || t->gfade) {                     /* ghost key: the compressor hears it, the mix does not;
                                                         * a mute / unmute / GHOST change fades over 5 ms */
                int32_t g = t->gfade, to = hide ? 32767 : 0;
                if (g == 32767 && to == 32767)
                    return;                             /* silent in the mix the whole block (a fade's last block
                                                         * still mixes its faded samples) */
                for (i = 0; i < n; i++) {
                    g = g < to ? (g + 150 < to ? g + 150 : to) : (g - 150 > to ? g - 150 : to);
                    b[i] = (int32_t)(((int64_t)b[i] * (32767 - g)) >> 15);
                }
                t->gfade = (uint16_t)g;
            }
        } else if (src < NTRK && t->p[P_DUCK]) {
            for (i = 0; i < n; i++)
                b[i] = clamp((int32_t)(((int64_t)b[i] * comp.gain[i]) >> 16), -(1 << 19), 1 << 19);
        }
        if (mix_fade != 32767 || mix_fade_d)
            mix_fade_block(b, n);                       /* cue.c: a slot switch's fade */
        for (i = 0; i < n; i++) {
            int32_t x = clamp(((b[i] >> 2) * lvl) >> 10, -524287, 524287), a = x < 0 ? -x : x;   /* pre-shift: 8 loud
                                                         * voices; the clamp: x * gl (Q12) fits (COMP makeup) */
            int32_t xs = clamp(x, -xmax, xmax);         /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            if (c)
                send_c[i] += mulq15(xs, c);
            if (d)
                send_d[i] += mulq15(xs, d);
            if (r)
                send_r[i] += mulq15(xs, r);
            mix_l[i] += (x * gl) >> 12;
            mix_r[i] += (x * gr) >> 12;
        }
        t->peak = pk;
#if USB10_BUILD
        if (usb10)
            usb8_track_tap((uint32_t)(t - trk), b, lvl, n);   /* MENU > USB AUDIO 10 CH */
#endif
    }
}

/* METRO's click, added after the FX layer (PERFORM's effects leave it alone), scaled by the MASTER level mg (the
 * volume knob turns it down with the mix): a sine, 1 kHz (1.6 kHz on the bar's
 * first), from phase 0 (no step), decaying 1/128 a sample (2.9 ms: -40 dB at 13 ms), ended below 1 LSB (~28 ms at
 * HIGH). LVL: the start amplitude (Q8 in env) for a peak of about -24 / -15 / -6 dBFS at the output (the decay before
 * the first peak and the soft clip counted). noinline: the audio ISR's loop code stays as it was (target_budget.py) */
static __attribute__((noinline)) void metro_block(int32_t *l, int32_t *r, uint32_t n, int32_t mg)
{
    static const int32_t LVL[4] = {0, 2230, 6330, 19400};
    uint32_t i, s = n;
    if (metro.due) {
        uint32_t p = metro.due - 1u, a = metro.adv;
        metro.due = 0;
        if (fx_metro)                                   /* following a clock, the advance maps onto the block */
            s = p < a ? (a - 1u - p) * n / a : 0u;
    }
    for (i = 0; i < n; i++) {
        int32_t x;
        if (i == s) {
            metro.ph = 0;
            metro.inc = metro.bar ? 155826478u : 97391549u;   /* 1.6 kHz, 1 kHz: f * 2^32 / FS */
            metro.env = LVL[fx_metro & 3u] << 8;
        }
        if (!metro.env)
            continue;
        x = (((sine_i(metro.ph) * (metro.env >> 8)) >> 15) * mg) >> 12;   /* MASTER_FULL = unity */
        metro.ph += metro.inc;
        metro.env -= metro.env >> 7;
        if (metro.env < 256)
            metro.env = 0;
        l[i] += x;
        r[i] += x;
    }
}

/* PING-PONG: the delay's left - right onto the dry left (the master adds wet, the right, to both); noinline: the
 * audio ISR's loop code stays as it was (target_budget.py) */
static __attribute__((noinline)) void dly_side_add(int32_t *l, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        l[i] += dly_side[i];
}

static uint32_t comp_was = NTRK;                 /* the COMP source of the last block (NTRK = off) */
static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int perf;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB LEVEL FIXED: MASTER after) */
    if (safe_start) {                                 /* safe start: silence, no track / model / FX code */
        for (i = 0; i < n; i++)
            out[2u * i] = out[2u * i + 1u] = 0;
        midi_drop();                                    /* MIDI in dropped, the ring kept empty: a full ring holds
                                                         * every USB packet back (usb_app.c), the installer's SysEx too */
        return;
    }
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
#if USB10_BUILD
    if (usb10)
        usb8_tap_clear(n);                              /* MENU > USB AUDIO 10 CH: the tracks' block */
#endif
    lfo_apply(n);                                       /* LFOs: the modulated knobs for this block (lfo.c) */
    events_block(n);
    perf = perf_begin(n);                               /* the FX hold layer at work (perform.c) */
    drum_block_begin();
    {
        uint32_t src = comp_src();
        if (src != comp_was) {                          /* another source, or OFF: a fresh detector (comp_was:
                                                         * file scope, before mix_block, = NTRK at start) */
            comp_reset();
            comp_was = src;
        }
        if (src < NTRK)
            mix_part(&trk[src], n, src);                /* the source first: its block keys the others */
        for (i = 0; i < NTRK; i++)
            if (i != src)
                mix_part(&trk[i], n, src);
    }
    lfo_restore();                                      /* the knobs as set again */
    if (perf)
        perf_pre(mix_l, mix_r, send_d, send_r, n);  /* THROW */
    fx_buses(send_c, send_d, send_r, wet, n);
#if USB10_BUILD
    if (usb10)
        usb8_wet_tap(wet, n);                           /* MENU > USB AUDIO 10 CH FX: the send effects' return */
#endif
    if (dpp.side)
        dly_side_add(mix_l, n);                         /* the delay's PING-PONG: its left */
    for (i = 0; i < n; i++) {                           /* the master level */
        mix_l[i] = (clamp((mix_l[i] + wet[i]) >> 2, -524287, 524287) * mg) >> 10;
        mix_r[i] = (clamp((mix_r[i] + wet[i]) >> 2, -524287, 524287) * mg) >> 10;   /* fits Q12 */
    }
    if (perf)
        perf_block(mix_l, mix_r, n);                    /* the FX layer's effects (perform.c) */
    if (metro.due | metro.env)
        metro_block(mix_l, mix_r, n, mg);               /* METRO: after the FX layer, at the MASTER level */
    for (i = 0; i < n; i++) {
        int32_t l = mix_l[i], r = mix_r[i];
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
}
