/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The features' reel: for each chapter of the landing page (web/sim/features.js), its page on the FM-1's screen while
 * a scripted hand turns the knobs to their ends: models and waves stepped through, ranges swept up and down, the
 * PERFORM keys held. Each clip starts from a fresh demo 1, playing. FM1R (sim_reel.h) at 15 fps, and the clips as JSON.
 *   sim_feature_reel OUT.bin OUT.json */
#include "sim_core.c"
#include <stdio.h>

#define FPS 15u
#define FRAME_SAMPLES (FS / FPS)

/* b tap a button, B / U hold / let go of one; e turn an encoder by val; s turn it by val every frame for n frames;
 * k tap a note key, K / u hold / let go of one (note keys 0 = F3 .. 26 = G5) */
typedef struct { float t; char what; uint8_t id; int8_t val; uint8_t n; } act_t;
typedef struct { const char *name; float seconds; const act_t *acts; uint32_t n; } clip_t;

#define W1 0u                                             /* the white keys used: track 1 .. 4 (TRACKS, COMP) and */
#define W2 2u                                             /* the steps 1 .. 6 on SEQ */
#define W3 4u
#define W4 6u
#define W5 7u
#define W6 9u

static const act_t VOICES[] = {                           /* SOUND: the MODEL list's three banks from demo 1's BDTRX
                                                           * (#01): through MD to MODERN, then to CLASSIC; DECAY and
                                                           * TUNE to their ends */
    {0.3f, 'b', B_EDIT, 0, 0},
    {1.2f, 's', EN_K1, 1, 21},                            /* the 21 MD engines in turn, to BOOM (#22) */
    {3.0f, 'e', EN_K1, 1, 0}, {3.45f, 'e', EN_K1, 1, 0}, {3.9f, 'e', EN_K1, 1, 0},   /* PUNCH SNAP CRACK */
    {4.5f, 's', EN_K1, 1, 7},                             /* to K808 (#32), the CLASSIC bank */
    {5.4f, 'e', EN_K1, 1, 0}, {5.85f, 'e', EN_K1, 1, 0},
    {6.4f, 's', EN_K3, 5, 14}, {7.4f, 's', EN_K3, -5, 14},
    {8.4f, 's', EN_K2, 3, 12}, {9.2f, 's', EN_K2, -3, 12},
};
static const act_t MD[] = {                               /* SOUND: the MD bank engine by engine, BDTRX to CYEFM */
    {0.3f, 'b', B_EDIT, 0, 0},
    {0.9f, 'e', EN_K1, 1, 0}, {1.35f, 'e', EN_K1, 1, 0}, {1.8f, 'e', EN_K1, 1, 0}, {2.25f, 'e', EN_K1, 1, 0},
    {2.7f, 'e', EN_K1, 1, 0}, {3.15f, 'e', EN_K1, 1, 0}, {3.6f, 'e', EN_K1, 1, 0}, {4.05f, 'e', EN_K1, 1, 0},
    {4.5f, 'e', EN_K1, 1, 0}, {4.95f, 'e', EN_K1, 1, 0}, {5.4f, 'e', EN_K1, 1, 0}, {5.85f, 'e', EN_K1, 1, 0},
    {6.3f, 'e', EN_K1, 1, 0}, {6.75f, 'e', EN_K1, 1, 0}, {7.2f, 'e', EN_K1, 1, 0}, {7.65f, 'e', EN_K1, 1, 0},
    {8.1f, 'e', EN_K1, 1, 0}, {8.55f, 'e', EN_K1, 1, 0}, {9.0f, 'e', EN_K1, 1, 0}, {9.45f, 'e', EN_K1, 1, 0},
};
static const act_t RESON[] = {                            /* FX to RESON; the four models, TUNE, DECAY, MIX swept */
    {0.3f, 'b', B_FX, 0, 0}, {0.6f, 'b', B_FX, 0, 0}, {0.9f, 'b', B_FX, 0, 0}, {1.2f, 'b', B_FX, 0, 0},
    {1.5f, 'b', B_FX, 0, 0},
    {2.0f, 'e', EN_K1, 1, 0}, {2.8f, 'e', EN_K1, 1, 0}, {3.6f, 'e', EN_K1, 1, 0}, {4.4f, 'e', EN_K1, 1, 0},
    {5.2f, 'e', EN_K1, -3, 0},
    {5.6f, 's', EN_K2, 4, 14}, {6.6f, 's', EN_K2, -4, 14},
    {7.6f, 's', EN_K3, 6, 14}, {8.6f, 's', EN_K4, 6, 14},
};
static const act_t COMP[] = {                             /* COMP: keyed by track 1, three tracks ducked, THR / RATIO /
                                                           * REL to their ends while the meter answers (from TRACKS:
                                                           * HOME to the HOME screen, SELECT to COMP 2/3) */
    {0.3f, 'b', B_HOME, 0, 0}, {0.6f, 'e', EN_SELECT, 1, 0},
    {0.9f, 'e', EN_K1, 1, 0},
    {1.3f, 'k', W2, 0, 0}, {1.6f, 'k', W3, 0, 0}, {1.9f, 'k', W4, 0, 0},
    {2.4f, 's', EN_K2, -5, 18}, {3.8f, 's', EN_K3, 6, 18}, {5.2f, 's', EN_K4, 6, 16},
    {6.6f, 's', EN_K2, 5, 18},
};
static const act_t LFO[] = {                              /* LFO 1 on TONE at full DEPTH; every wave, MORPH, RATE */
    {0.3f, 'b', B_LFO, 0, 0},
    {0.7f, 'e', EN_SELECT, 1, 0}, {1.0f, 'e', EN_K1, 3, 0}, {1.4f, 'e', EN_SELECT, -1, 0},
    {1.8f, 's', EN_K4, 5, 14},
    {2.9f, 'e', EN_K1, 1, 0}, {3.35f, 'e', EN_K1, 1, 0}, {3.8f, 'e', EN_K1, 1, 0}, {4.25f, 'e', EN_K1, 1, 0},
    {4.7f, 'e', EN_K1, 1, 0}, {5.15f, 'e', EN_K1, 1, 0}, {5.6f, 'e', EN_K1, 1, 0}, {6.05f, 'e', EN_K1, -4, 0},
    {6.5f, 's', EN_K3, 6, 20}, {8.0f, 's', EN_K2, 4, 12}, {8.9f, 's', EN_K2, -4, 12},
};
static const act_t SEQ[] = {                              /* STEP: steps tapped in, one held for PROB and RATCH */
    {0.3f, 'b', B_SEQ, 0, 0},
    {1.0f, 'k', W2, 0, 0}, {1.3f, 'k', W4, 0, 0}, {1.6f, 'k', W6, 0, 0},
    {2.4f, 'K', W3, 0, 0}, {2.6f, 's', EN_K1, -1, 12}, {3.6f, 'e', EN_K2, 1, 0}, {3.9f, 'e', EN_K2, 1, 0},
    {4.2f, 'e', EN_K2, 1, 0}, {4.8f, 'u', W3, 0, 0},
    {5.4f, 'K', W5, 0, 0}, {5.6f, 's', EN_K1, 1, 8}, {6.4f, 'u', W5, 0, 0},
    {7.0f, 'k', W1, 0, 0}, {7.4f, 'k', W1, 0, 0},
};
static const act_t GRIDS[] = {                            /* GRIDS: X and Y across the map, CHAOS up, the fills */
    {0.3f, 'b', B_ARP, 0, 0},
    {0.9f, 's', EN_K2, 6, 20}, {2.4f, 's', EN_K3, 6, 20}, {3.9f, 's', EN_K2, -6, 20},
    {5.4f, 's', EN_K4, 6, 20},
    {6.9f, 'e', EN_SELECT, 1, 0}, {7.3f, 's', EN_K1, -6, 12}, {8.2f, 's', EN_K2, 6, 12},
};
static const act_t FX[] = {                               /* FILTER's CUT swept, then FX held: the PERFORM keys in turn */
    {0.3f, 'b', B_FX, 0, 0},
    {0.9f, 'e', EN_K1, 1, 0}, {1.2f, 's', EN_K2, -6, 16}, {2.4f, 's', EN_K2, 6, 16},
    {3.8f, 'B', B_FX, 0, 0},
    {4.4f, 'K', 1, 0, 0}, {5.1f, 'u', 1, 0, 0},           /* REPEAT 1/8 */
    {5.3f, 'K', 8, 0, 0}, {6.0f, 'u', 8, 0, 0},           /* REVERSE */
    {6.2f, 'K', 10, 0, 0}, {6.9f, 'u', 10, 0, 0},         /* TAPE STOP */
    {7.1f, 'K', 13, 0, 0}, {7.2f, 's', EN_K1, -6, 10}, {8.0f, 'u', 13, 0, 0},   /* LPF, its FILTER macro */
    {8.2f, 'K', 17, 0, 0}, {8.9f, 'u', 17, 0, 0},         /* FREEZE */
    {9.1f, 'K', 20, 0, 0}, {9.8f, 'u', 20, 0, 0},         /* OCT UP */
    {10.0f, 'U', B_FX, 0, 0},
};
#define CLIP(name, s, a) {name, s, a, sizeof a / sizeof a[0]}
static const clip_t CLIPS[] = {
    CLIP("md", 10.4f, MD), CLIP("voices", 10.0f, VOICES), CLIP("reson", 9.8f, RESON), CLIP("comp", 8.2f, COMP), CLIP("lfo", 10.0f, LFO),
    CLIP("seq", 8.0f, SEQ), CLIP("grids", 9.4f, GRIDS), CLIP("fx", 10.6f, FX),
};
#define NCLIPS (sizeof CLIPS / sizeof CLIPS[0])

#include "sim_reel.h"

static void run(uint32_t samples)
{
    while (samples) {
        uint32_t n = samples < SIM_MAX_FRAMES ? samples : SIM_MAX_FRAMES;
        sim_render(n);
        samples -= n;
    }
}

int main(int argc, char **argv)
{
    FILE *f, *j;
    uint32_t c, total = 0, from[NCLIPS], frames[NCLIPS];
    if (argc != 3) {
        fprintf(stderr, "usage: sim_feature_reel OUT.bin OUT.json\n");
        return 2;
    }
    for (c = 0; c < NCLIPS; c++) {
        frames[c] = (uint32_t)(CLIPS[c].seconds * FPS);
        from[c] = total;
        total += frames[c];
    }
    f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    fwrite("FM1R", 1, 4, f);
    put16(f, 1);
    put16(f, FPS);
    put32(f, total);
    for (c = 0; c < NCLIPS; c++) {
        const clip_t *cl = &CLIPS[c];
        uint32_t frame, next = 0, k, sweep_left[8] = {0};
        int8_t sweep_val[8] = {0};
        sim_flash_reset();
        sim_init(1);                                      /* a fresh demo 1, playing, LOADED gone */
        memset(ui.fam_last, 0, sizeof ui.fam_last);   /* each family's first page, as at power-on */
        ui.bank = 0;
        tap_btn(B_PLAY);
        run(FS * 3u / 2u);
        for (frame = 0; frame < frames[c]; frame++) {
            int8_t enc[8] = {0};
            while (next < cl->n && cl->acts[next].t * FPS <= (float)frame) {
                const act_t *a = &cl->acts[next++];
                switch (a->what) {
                case 'b': tap_btn(a->id); break;
                case 'B': sim_btn(a->id, 1); break;
                case 'U': sim_btn(a->id, 0); break;
                case 'e':
                    sim_enc(a->id, a->val);
                    enc[a->id] = (int8_t)(enc[a->id] + a->val);
                    break;
                case 's': sweep_val[a->id] = a->val; sweep_left[a->id] = a->n; break;
                case 'k': sim_key(a->id, 1); run(FS * 12u / 100u); sim_key(a->id, 0); break;
                case 'K': sim_key(a->id, 1); break;
                case 'u': sim_key(a->id, 0); break;
                default: break;
                }
            }
            for (k = 0; k < 8u; k++)
                if (sweep_left[k]) {
                    sim_enc(k, sweep_val[k]);
                    enc[k] = (int8_t)(enc[k] + sweep_val[k]);
                    sweep_left[k]--;
                }
            run(FRAME_SAMPLES);
            write_frame(f, enc, from[c] + frame == 0u);
        }
        for (k = 0; k < 14u; k++)                         /* nothing held into the next clip */
            sim_btn(k, 0);
        for (k = 0; k < 27u; k++)
            sim_key(k, 0);
    }
    fclose(f);
    j = fopen(argv[2], "w");
    if (!j)
        return 1;
    fprintf(j, "[");
    for (c = 0; c < NCLIPS; c++)
        fprintf(j, "%s\n  {\"name\": \"%s\", \"from\": %u, \"to\": %u}", c ? "," : "", CLIPS[c].name, from[c],
                from[c] + frames[c]);
    fprintf(j, "\n]\n");
    fclose(j);
    printf("sim_feature_reel: %u clips, %u frames at %u fps -> %s\n", (unsigned)NCLIPS, total, FPS, argv[1]);
    return 0;
}
