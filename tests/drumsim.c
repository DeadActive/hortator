/* WAV renders for listening: one file per model, the kit demo and the demo kits (808, 909, Plaits, mixed).
 * drumsim OUTDIR */
#include "drum_host.h"
#include <sys/stat.h>

static void wav_hdr(FILE *f, uint32_t frames)
{
    uint32_t v;
    uint16_t a = 1, ch = 2, ba = 4, bits = 16;
    uint32_t sr = FS, br = FS * 4;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}

static void wav_put(FILE *f, int32_t l, int32_t r)
{
    int16_t s[2] = {(int16_t)clamp(l, -32768, 32767), (int16_t)clamp(r, -32768, 32767)};
    fwrite(s, 2, 2, f);
}

#define GAP (FS * 6 / 10 / CTL * CTL)
static int32_t L[GAP], R[GAP];

static void hit_and_write(FILE *f, track_t *t, uint32_t vel, uint32_t *frames)
{
    uint32_t i;
    drum_hit(t, vel);
    render_mix(L, R, GAP);
    for (i = 0; i < GAP; i++)
        wav_put(f, L[i], R[i]);
    *frames += GAP;
}

/* a demo kit: 8 tracks (model, TUNE, DECAY or -1 = default), a 16-step pattern ('x' hit, 'X' accent) per track,
 * played 4 bars at 120 BPM through the whole mix (FX sends on the snare, clap and hats), written to DIR/NAME */
typedef struct {
    const char *name;
    uint32_t model[NTRK];
    int16_t tune[NTRK], decay[NTRK];
    const char *pat[NTRK];
} demo_kit_t;

static const demo_kit_t KITS[] = {
    {"kit_808.wav", {DM_K808, DM_S808, DM_C808, DM_HATC, DM_HATO, DM_TOM, DM_COWB, DM_CYMB},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "..........x.x.xX", ".x.....x...x....", "X..............."}},
    {"kit_909.wav", {DM_K909, DM_S909, DM_C909, DM_HATC, DM_HATO, DM_RIM, DM_CLAVE, DM_CONGA},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X...x...x...x...", "....X.......X..x", "....x.......x...", "xxxxxxxxxxxxxxXx", "..x...x...x...x.",
      "...x.....x....x.", ".x....x...x.....", "......x.x.....x."}},
    {"kit_plaits.wav", {DM_KBOOM, DM_SSNAP, DM_SCRAK, DM_HMETL, DM_HNOIS, DM_KPUNC, DM_KBOOM, DM_SSNAP},
     {0, 0, 0, 0, 0, 0, 12, 12}, {-1, -1, -1, -1, -1, 40, 30, 10},
     {"X..x..x...x..x..", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "X.......X.......", "..........x.x.x.", ".x.....x...x...."}},
    {"kit_mix.wav", {DM_KBOOM, DM_S808, DM_SCRAK, DM_HATC, DM_HNOIS, DM_K909, DM_TOM, DM_CYMB},
     {0, 0, 0, 0, 0, 0, 0, 0}, {-1, -1, -1, -1, -1, -1, -1, -1},
     {"X...x...x...x...", "....X.......X...", "....x.......x..x", "x.x.x.x.x.x.x.x.", "..x...x...x...x.",
      "..x.......x..x..", "..........x.x.xX", "X..............."}},
};

static void write_kit(const char *dir, const demo_kit_t *kit)
{
    char path[256];
    uint32_t total = 4 * 16 * (FS * 60 / 120 / 4) / CTL * CTL, frames, i, k;
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, kit->name);
    f = fopen(path, "wb");
    wav_hdr(f, total);
    host_init();
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        drum_set_model(t, kit->model[i]);
        t->p[P_E0] = kit->tune[i];
        if (kit->decay[i] >= 0)
            t->p[P_E1] = kit->decay[i];
        for (k = 0; k < 16; k++) {
            t->step[k].on = kit->pat[i][k] != '.';
            t->step[k].acc = kit->pat[i][k] == 'X';
        }
    }
    trk[1].p[P_REV] = 40;
    trk[2].p[P_REV] = 50;
    trk[3].p[P_DLY] = 25;
    transport_req = 1;
    for (frames = 0; frames < total; frames += GAP < total - frames ? GAP : total - frames) {
        uint32_t n = GAP < total - frames ? GAP : total - frames;
        render_mix(L, R, n);
        for (i = 0; i < n; i++)
            wav_put(f, L[i], R[i]);
    }
    fclose(f);
    printf("drumsim: %s\n", path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/drum_renders";
    char path[256];
    uint32_t mi, frames, i, k;
    mkdir(dir, 0755);
    for (mi = 0; mi < NMODELS; mi++) {
        static const uint8_t VEL[4] = {127, 96, 64, 32};
        FILE *f;
        snprintf(path, sizeof path, "%s/%02u_%s.wav", dir, mi, N_MODEL[mi]);
        f = fopen(path, "wb");
        wav_hdr(f, 0);
        frames = 0;
        host_init();
        drum_set_model(&trk[0], mi);
        for (i = 0; i < 4; i++)
            hit_and_write(f, &trk[0], VEL[i], &frames);
        for (k = 0; k < 4; k++) {                    /* TUNE, DECAY, TONE, CHAR at low then high */
            const param_desc_t *d = &DMODELS[mi].edit[k];
            int16_t keep = trk[0].p[P_E0 + k];
            trk[0].p[P_E0 + k] = k == 0 ? -12 : d->min;
            hit_and_write(f, &trk[0], 127, &frames);
            trk[0].p[P_E0 + k] = k == 0 ? 12 : d->max;
            hit_and_write(f, &trk[0], 127, &frames);
            trk[0].p[P_E0 + k] = keep;
        }
        fseek(f, 0, SEEK_SET);
        wav_hdr(f, frames);
        fclose(f);
        printf("drumsim: %s\n", path);
    }
    {
        static const char *const PAT[NTRK] = {
            "x...x...x...x..x", "....x.......x...", "....x.......x..x", "x.x.x.x.x.x.x.x.",
            "..x...x...x...x.", "......x....x....", ".x.....x..x.....", "x...............",
        };
        uint32_t total = 8 * 16 * (FS * 60 / 120 / 4) / CTL * CTL;
        FILE *f;
        snprintf(path, sizeof path, "%s/kit.wav", dir);
        f = fopen(path, "wb");
        wav_hdr(f, total);
        host_init();
        for (i = 0; i < NTRK; i++)
            for (k = 0; k < 16; k++)
                trk[i].step[k].on = PAT[i][k] == 'x';
        trk[1].p[P_REV] = 40;
        trk[2].p[P_REV] = 50;
        trk[3].p[P_DLY] = 25;
        transport_req = 1;
        for (frames = 0; frames < total; frames += GAP < total - frames ? GAP : total - frames) {
            uint32_t n = GAP < total - frames ? GAP : total - frames;
            render_mix(L, R, n);
            for (i = 0; i < n; i++)
                wav_put(f, L[i], R[i]);
        }
        fclose(f);
        printf("drumsim: %s\n", path);
    }
    for (i = 0; i < sizeof KITS / sizeof KITS[0]; i++)
        write_kit(dir, &KITS[i]);
    return 0;
}
