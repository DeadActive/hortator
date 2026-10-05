/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The compressor fidelity cases (tests/comp_ref.cc, tests/comp_fidelity.c): knob settings and integer test
 * signals, 4096 samples each: kick-like decaying bursts, a tone stepping through four levels, silence - a loud
 * burst - silence (release). */
#define COMP_N 4096
#define COMP_SIGNALS 3
typedef struct { int atk, thr, amt, rel, knee; } comp_case_t;
static const int CC_ATK[3] = {0, 64, 127}, CC_REL[2] = {0, 127}, CC_THR[3] = {0, 26, 127};
static const int CC_AMT[6] = {0, 22, 63, 64, 100, 127};
#define CC_ALL (3 * 2 * 3 * 6 * 2)
static int comp_case(int i, comp_case_t *c)
{
    if (i < 0 || i >= CC_ALL)
        return 0;
    c->atk = CC_ATK[i % 3];
    c->rel = CC_REL[i / 3 % 2];
    c->thr = CC_THR[i / 6 % 3];
    c->amt = CC_AMT[i / 18 % 6];
    c->knee = i / 108;
    return 1;
}
static unsigned comp_case_k16(int v) { return (unsigned)v * 65535u / 127u; }
static unsigned comp_case_amount16(int v)
{
    return v < 64 ? 32767u - (unsigned)v * 32767u / 63u : 32768u + (unsigned)(v - 64) * 32767u / 63u;
}
static int comp_signal(int sig, int n)
{
    int p = n & 63, tri = p < 32 ? p * 64 - 1024 : (64 - p) * 64 - 1024;      /* -1024 .. 1024 */
    static const int LV[4] = {500, 4000, 16000, 32000};
    if (sig == 0) {
        int env = 1024 - (n & 1023);
        return tri * (31 * env * env / 1024) / 1024;
    }
    if (sig == 1)
        return tri * LV[(n >> 10) & 3] / 1024;
    return n >= 1024 && n < 1536 ? tri * 32000 / 1024 : 0;
}
