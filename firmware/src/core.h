/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 DEADACTIVE */
/* Drum machine core types: 8 drum tracks; each plays one drum model (1 or 2 voices) and an
 * optional sample layer, through Felucca's per-track DIST / SLICER / sends (fx.c). */
#include <stdint.h>
#include "dm_state.h"
#define NTRK 8                   /* drum tracks */
#define NPART NTRK               /* fx.c / slicer.c: every track is mixed the same way */
#define NDV 2                    /* voices per track; a model uses 1 or 2 */
#define NSTEP 64
#define HALF_FRAMES 256          /* I2S half buffer: 5.8 ms at 44.1 kHz */
#define UP_SLOTS 32u             /* user preset slots (storage layout; presets are off in M1) */

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS, F_CTHR, F_CRAT, F_CATK, F_CREL, F_CMKUP, F_LRATE1, F_LRATE2, F_LDEST, F_LPHASE,
    F_NOTEO, F_RDECAY
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;
#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

enum { LF_WAVE, LF_MODE, LF_RATE, LF_MORPH, LF_DEPTH, LF_DEST, LF_TRIG, LF_PHASE, LF_N };   /* an LFO's knobs */
enum { LW_SQUARE, LW_SAW, LW_RSAW, LW_SINE, LW_TRI, LW_SH, LW_WANDER, LW_EXPUP, LW_EXPDN, LW_RWALK, LW_COUNT };   /* LFO waves (lfo.c) */
enum { LM_SYNC, LM_HZ, LM_TIME };
enum { LT_FREE, LT_HIT, LT_PLAY };
enum { RS_OFF, RS_STRNG, RS_PIPE, RS_CHORD, RS_NMODEL };   /* RESON models (reson.c); modal models come after CHORD */
#define RS_NCHORD 20                                 /* CHORD types (reson.c RS_CHORD_IV, params.c N_RCHORD) */

enum {                          /* per-track parameters */
    P_MODEL,
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,   /* the model's: TUNE DECAY TONE CHAR + 4 extras */
    P_LEVEL, P_PAN, P_MUTE, P_CHOKE, P_NOTE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,
    P_SLEN, P_SDIV, P_SSWING,
    P_LSET, P_LKEY, P_LLEVEL, P_LTUNE, P_LDEC,       /* sample layer */
    P_SRC,                       /* what the track plays: 0 its steps, 1..3 a Grids channel (kick, snare, hats) */
    P_DUCK,                      /* M3: the COMP source ducks this track */
    P_LFO1,                      /* LFO 1: LF_N knobs (lfo.c) */
    P_LFO2 = P_LFO1 + LF_N,      /* LFO 2 */
    P_RMODEL = P_LFO2 + LF_N,    /* RESON (reson.c): MODEL TUNE DECAY MIX, TONE STRCT POS */
    P_RTUNE, P_RDECAY, P_RMIX, P_RTONE, P_RSTRCT, P_RPOS,
    P_COUNT
};

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_CLRSEQ, G_INITSND,
    G_DRCH,                      /* MIDI channel of the drum tracks, 1..16 */
    G_GMODE, G_GX, G_GY, G_GCHAOS,                   /* Grids (grids.c): MAP / EUCLID, the map point, chaos */
    G_GFILL1, G_GFILL2, G_GFILL3,                    /* fill per channel (kick, snare, hats) */
    G_GLEN1, G_GLEN2, G_GLEN3,                       /* Euclidean length per channel, 1..32 sixteenths */
    G_CSRC, G_CTHR, G_CRAT, G_CREL,                  /* COMP (comp.c): source track (0 off), threshold, ratio, release */
    G_CATK, G_CKNEE, G_CMKUP,                        /* attack, soft knee, makeup (127 = limiter) */
    G_CGHOST,                                        /* a muted / any source heard: CG_MUTE CG_KEEP CG_HIDE (fx.c) */
    G_COUNT
};
enum { CG_MUTE, CG_KEEP, CG_HIDE };                 /* GHOST: muted = muted; muted still keys; never heard, keys */
enum { G_CLRALL = G_COUNT, G_INITALL, G_MUTEBAR };   /* TOOLS actions, the MUTE setting: shown like globals, not stored
                                                     * in projects */

/* ----------------------------------------------------------- voices --- */
typedef struct {                 /* sample playback state (eng_sample.c sample_next, dm_sample.c) */
    uint32_t ph[3];              /* ph[0] position, ph[1] fraction Q16 */
    int32_t s[8];                /* s[0] predictor, s[1] step index, s[2..3] interpolation, s[4] zone,
                                  * s[5] step Q16, s[6] tone state */
} voice_t;

typedef struct { int32_t a1, a2, a3, k; } dsvf_t;   /* dm_dsp.c: SVF with LP / BP / HP */

typedef struct {                 /* one drum voice; the fields' meaning is the model's */
    uint8_t active, vel;
    uint32_t age;                /* hit order (voice stealing) */
    uint32_t t;                  /* samples since the hit (models that need it) */
    uint32_t ph[3], inc[3];      /* oscillator phases and increments */
    int32_t env[3];              /* envelopes, Q24 (1 << 24 = full) */
    uint32_t k[3];               /* their per-sample factors, Q16 (DECAY_K) */
    dsvf_t c[2];                 /* filters */
    int32_t f[4];                /* filter states (2 per filter) */
    int32_t x[4];                /* model scratch: gains, amounts */
    int32_t rng;                 /* noise state, seeded per hit */
    int32_t last;                /* last sample added to the output (declick when cut) */
    voice_t sv;                  /* sample playback (SAMPLE model, layer) */
    dm_state_t ms;               /* M1-C model state (dm_state.h) */
} dvoice_t;

struct track;
typedef struct {
    const char *name;            /* <= 5 chars (UI value) */
    uint8_t voices;              /* 1 or 2 */
    uint8_t choke;               /* default choke group, 0 = none */
    param_desc_t edit[8];        /* P_E0..P_E7: TUNE DECAY TONE CHAR + 4 extras */
    void (*trigger)(struct track *t, dvoice_t *v);
    void (*render)(struct track *t, dvoice_t *v, int32_t *out, uint32_t n);
    uint8_t weight;              /* what a voice counts toward DRUM_MAXV: 0 / 1 = 1, 2 = a heavy model (M1-C) */
} dmodel_t;

/* ------------------------------------------------------------ track --- */
typedef struct {                 /* one sequencer step; all zero = a plain step, off */
    uint8_t on, acc;             /* hit; accent (velocity 127, else 96) */
    uint8_t cond;                /* PROB: 0 = 100 %, 1..20 = 0..95 %, 21 = 1-SHOT, 22..56 = A/B (params.c cond_*) */
    uint8_t rat;                 /* RATCH: hits - 1 (0..3) */
} step_t;

typedef struct { uint32_t ph, sub; int32_t out, from, to; uint8_t fresh; } lfo_state_t;   /* lfo.c: phase, sub-phase,
                                                                           * outputs, restarted */

typedef struct {                 /* RESON runtime (reson.c): per line (STRNG / PIPE 1, CHORD 4) */
    uint32_t len[4];             /* the line's delay, Q8 samples (the loop filters' delay taken off) */
    int32_t g[4];                /* loop gain, Q15 (PIPE: negative) */
    int32_t lp[4], lr[4], apx[4], apy[4];   /* damping low-pass and its step's remainder; the all-pass' x, y[n-1] */
    int32_t k, a;                /* damping coefficient (Q15), all-pass coefficient (Q15, <= 0) */
    uint16_t tap[4];             /* POS: the second pickup tap (0 = none) */
    uint16_t w, seg, quiet, peak;   /* write position, segment length, quiet blocks, last block's line peak */
    uint8_t ns, model, ring, kill;  /* lines, the model running, ringing, fade out this block */
    uint8_t hold;                /* after a cut: no input until the track's next hit (its declick tail excites nothing) */
} reson_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t model;               /* model the voices were started with (a change cuts them) */
    dvoice_t v[NDV];             /* model voices */
    dvoice_t lv[NDV];            /* sample-layer voices */
    int32_t dtail;               /* declick: last output of cut voices, decaying */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* samples into the current step */
    uint16_t seq_idx;
    uint32_t seq_cnt;            /* steps played since PLAY: swing pairs follow it, so any length stays on the bar */
    uint8_t rskip, rskip_idx;    /* live recording put a hit into the step about to play: skip it once */
    uint32_t rng;                /* PROB: the track's random sequence (LCG), seeded at PLAY */
    uint32_t rat_len;            /* RATCH: the playing roll's step length (samples) */
    uint8_t rat_n, rat_k, rat_vel;   /* its hits, the next hit, their velocity; rat_n 0 = no roll */
    uint16_t gfade;              /* M3 ghost key: how silenced the COMP source's sound is (0 heard .. 32767 muted) */
    lfo_state_t lfo[2];          /* LFO 1 / 2 runtime (lfo.c) */
    reson_t rs;                  /* RESON runtime (reson.c) */
    int32_t rfine;               /* RESON fine pitch from the LFOs (R.TUN), 1/256 semitone (lfo.c) */
    uint32_t lrng;               /* random generator of S&H / WANDER / RWALK, reseeded at PLAY */
    uint8_t lon, lnum, lpid[2];       /* knobs written this block, which */
    int16_t lsave[2], lval[2];   /* their set values (restored at the end of the block), the modulated values */
    /* mix runtime (fx.c) */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t octdn;               /* seq_mode 2 (TRACKS, armed): the keys are the UI's only while this button (OCT-) is
                                  * held */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t cpu_rem;            /* its average's remainder (audio.c) */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
    int16_t act[3];              /* TOOLS action knobs CLR* INIT* (G_CLRALL ..): GO buttons, never stored; MUTE: a
                                  * copy of settings.mutebar */
    uint8_t mute_q;              /* TRACKS: mutes to flip on the next bar (bit per track; seq.c) */
    uint8_t lfo_in;              /* lfo.c: inside an audio block, between lfo_apply and lfo_restore */
    uint8_t lsel;                /* the LFO pages show LFO 1 / 2 (OCT-): screen state, never stored */
} song_t;

static track_t trk[NTRK];
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
