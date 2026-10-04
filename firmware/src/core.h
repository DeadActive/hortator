/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Drum machine fork: 2026 Eugene Vech */
/* Drum machine core types: 8 drum tracks; each plays one drum model (1 or 2 voices) and an
 * optional sample layer, through Felucca's per-track DIST / SLICER / sends (fx.c). */
#include <stdint.h>
#define NTRK 8                   /* drum tracks */
#define NPART NTRK               /* fx.c / slicer.c: every track is mixed the same way */
#define NDV 2                    /* voices per track; a model uses 1 or 2 */
#define NSTEP 64
#define HALF_FRAMES 256          /* I2S half buffer: 5.8 ms at 44.1 kHz */
#define UP_SLOTS 32u             /* user preset slots (storage layout; presets are off in M1) */

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS
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

enum {                          /* per-track parameters */
    P_MODEL,
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,   /* the model's: TUNE DECAY TONE CHAR + 4 extras */
    P_LEVEL, P_PAN, P_MUTE, P_CHOKE, P_NOTE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,
    P_SLEN, P_SDIV, P_SSWING,
    P_LSET, P_LKEY, P_LLEVEL, P_LTUNE, P_LDEC,       /* sample layer */
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
    G_COUNT
};

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
} dvoice_t;

struct track;
typedef struct {
    const char *name;            /* <= 5 chars (UI value) */
    uint8_t voices;              /* 1 or 2 */
    uint8_t choke;               /* default choke group, 0 = none */
    param_desc_t edit[8];        /* P_E0..P_E7: TUNE DECAY TONE CHAR + 4 extras */
    void (*trigger)(struct track *t, dvoice_t *v);
    void (*render)(struct track *t, dvoice_t *v, int32_t *out, uint32_t n);
} dmodel_t;

/* ------------------------------------------------------------ track --- */
typedef struct {                 /* one sequencer step */
    uint8_t on, acc;             /* hit; accent (velocity 127, else 96) */
} step_t;

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
    uint8_t rskip, rskip_idx;    /* live recording put a hit into the step about to play: skip it once */
    /* mix runtime (fx.c) */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail) */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
