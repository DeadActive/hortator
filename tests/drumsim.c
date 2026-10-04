/* WAV renders for listening: one file per model and the kit demo.  drumsim OUTDIR */
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
    return 0;
}
