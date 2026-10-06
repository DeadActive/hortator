/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* Sound pack (docs/superpowers/specs/2026-10-06-sound-pack-design.md): BASS+, SPRING and the slow divisions against
 * upstream 1.0.2's code (sound_pack_ref.h), and today's sound kept bit for bit: HASH_FLAT / HASH_LOWCUT are the
 * kit below rendered by the build before the sound pack (trs-midi 08866e0 .. bf98902). */
#include "drum_host.h"
#include <stdlib.h>

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static uint32_t fnv(const int32_t *x, uint32_t n)
{
    uint32_t h = 2166136261u, i;
    for (i = 0; i < n; i++)
        h = (h ^ (uint32_t)x[i]) * 16777619u;
    return h;
}

#define KIT_N (2u * 16u * (FS * 60 / 120 / 4) / CTL * CTL)   /* 2 bars at 120 BPM */
static int32_t KL[KIT_N], KR[KIT_N];
/* kick, snare (REV), clap (REV + CHO), hats (DLY), 2 bars, every bus and the master in use */
static uint32_t kit_hash(uint32_t lowcut)
{
    uint32_t i, k;
    host_init();
    fx_lowcut = (uint8_t)lowcut;
    for (i = 0; i < 4u; i++)
        for (k = 0; k < 16u; k++)
            trk[i].step[k].on = k % (i + 2u) == 0;
    trk[1].p[P_REV] = 90;
    trk[2].p[P_REV] = 60;
    trk[2].p[P_CHOR] = 70;
    trk[3].p[P_DLY] = 80;
    transport_req = 1;
    render_mix(KL, KR, KIT_N);
    return fnv(KL, KIT_N) ^ fnv(KR, KIT_N) * 31u;
}

#define HASH_FLAT 0xb35154deu
#define HASH_LOWCUT 0xd5959edeu

int main(void)
{
    if (getenv("SOUND_PACK_RECORD")) {
        printf("HASH_FLAT 0x%08xu\nHASH_LOWCUT 0x%08xu\n", kit_hash(0), kit_hash(1));
        return 0;
    }
    check("today's sound: the kit (ROOM reverb, delay, chorus) with FLAT, bit for bit", kit_hash(0) == HASH_FLAT);
    check("today's sound: the kit with LOWCUT, bit for bit", kit_hash(1) == HASH_LOWCUT);
    printf(fails ? "sound_pack_test: %d FAILED\n" : "sound_pack_test: all passed\n", fails);
    return fails ? 1 : 0;
}
