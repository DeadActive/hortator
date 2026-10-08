/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The lab's ids for the landing page (web/sim/lab.js): the firmware's parameter, global, model and effect ids with
 * their labels, ranges, defaults and name tables, printed as JSON at build time (tools/build_sim.sh -> lab_ids.json),
 * so the page never carries a copy of a number. A name the firmware drops fails this build. */
#include "sim_core.c"
#include <stdio.h>

static int first;
static void key(const char *k) { printf("%s\"%s\": ", first ? "" : ", ", k); first = 0; }
static void str(const char *s)
{
    putchar('"');
    for (; s && *s; s++)
        if (*s == '"' || *s == '\\')
            printf("\\%c", *s);
        else
            putchar(*s);
    putchar('"');
}
static void desc(const param_desc_t *d)
{
    printf("{\"label\": ");
    str(d->label ? d->label : "");
    printf(", \"min\": %d, \"max\": %d, \"def\": %d", d->min, d->max, d->def);
    if (d->names) {
        int i;
        printf(", \"names\": [");
        for (i = 0; i <= d->max; i++) {
            printf(i ? ", " : "");
            str(d->names[i]);
        }
        printf("]");
    }
    if (d->unit)
        printf(", \"unit\": "), str(d->unit);
    printf("}");
}
static void names(const char *k, const char *const *n, int count)
{
    int i;
    key(k);
    printf("[");
    for (i = 0; i < count; i++) {
        printf(i ? ", " : "");
        str(n[i]);
    }
    printf("]");
}

#define ID(prefix, name) key(#name), printf("%d", prefix##name)
#define TPD(name) key(#name), desc(&TP[P_##name])
#define GPD(name) key(#name), desc(&GP[G_##name])

int main(void)
{
    uint32_t m, e;
    printf("{\n\"P\": {");
    first = 1;
    ID(P_, MODEL); ID(P_, E0); ID(P_, E1); ID(P_, E2); ID(P_, E3); ID(P_, E4); ID(P_, E5); ID(P_, E6); ID(P_, E7);
    ID(P_, LEVEL); ID(P_, PAN); ID(P_, MUTE); ID(P_, DIST); ID(P_, CHOR); ID(P_, DLY); ID(P_, REV);
    ID(P_, SLCR); ID(P_, SLPAT); ID(P_, SLRATE); ID(P_, SLDEPTH); ID(P_, SLEN); ID(P_, SDIV); ID(P_, SSWING);
    ID(P_, SRC); ID(P_, DUCK); ID(P_, LFO1); ID(P_, LFO2);
    ID(P_, RMODEL); ID(P_, RTUNE); ID(P_, RDECAY); ID(P_, RMIX); ID(P_, RTONE); ID(P_, RSTRCT); ID(P_, RPOS);
    ID(P_, FTYPE); ID(P_, FCUT); ID(P_, FRESO); ID(P_, FENV); ID(P_, FDEC);
    printf("},\n\"LF\": {");
    first = 1;
    ID(LF_, WAVE); ID(LF_, MODE); ID(LF_, RATE); ID(LF_, MORPH); ID(LF_, DEPTH); ID(LF_, DEST); ID(LF_, TRIG); ID(LF_, PHASE);
    printf("},\n\"G\": {");
    first = 1;
    ID(G_, BPM); ID(G_, SWING); ID(G_, CLOCK);
    ID(G_, CSRC); ID(G_, CTHR); ID(G_, CRAT); ID(G_, CREL); ID(G_, CATK); ID(G_, CKNEE); ID(G_, CMKUP); ID(G_, CGHOST);
    ID(G_, GMODE); ID(G_, GX); ID(G_, GY); ID(G_, GCHAOS); ID(G_, GFILL1); ID(G_, GFILL2); ID(G_, GFILL3);
    ID(G_, DTIME); ID(G_, DFDBK); ID(G_, DMIX); ID(G_, RSIZE); ID(G_, RDAMP); ID(G_, RTYPE); ID(G_, CRATE); ID(G_, CDEPTH);
    printf("},\n\"PF\": {");
    first = 1;
    ID(PF_, R8); ID(PF_, R16); ID(PF_, R32); ID(PF_, REV); ID(PF_, LPF); ID(PF_, HPF); ID(PF_, TAPE); ID(PF_, FRZ);
    ID(PF_, OUP); ID(PF_, ODN); ID(PF_, M1);
    printf("},\n\"RS\": {");
    first = 1;
    ID(RS_, OFF); ID(RS_, STRNG); ID(RS_, PIPE); ID(RS_, CHORD); ID(RS_, MODAL);
    printf("},\n\"DM\": {");
    first = 1;
    ID(DM_, K808); ID(DM_, K909); ID(DM_, S808); ID(DM_, S909); ID(DM_, C808); ID(DM_, C909); ID(DM_, HATC); ID(DM_, HATO);
    ID(DM_, CYMB); ID(DM_, COWB); ID(DM_, TOM); ID(DM_, CONGA); ID(DM_, RIM); ID(DM_, CLAVE); ID(DM_, SMPL);
    ID(DM_, KBOOM); ID(DM_, KPUNC); ID(DM_, SSNAP); ID(DM_, SCRAK); ID(DM_, HMETL); ID(DM_, HNOIS); ID(DM_, MEMB); ID(DM_, FM);
    printf("},\n\"tp\": {");
    first = 1;
    TPD(LEVEL); TPD(PAN); TPD(DIST); TPD(CHOR); TPD(DLY); TPD(REV); TPD(SLCR); TPD(SLPAT); TPD(SLRATE); TPD(SLDEPTH);
    TPD(DUCK); TPD(RMODEL); TPD(RTUNE); TPD(RDECAY); TPD(RMIX); TPD(RTONE); TPD(RSTRCT); TPD(RPOS);
    TPD(FTYPE); TPD(FCUT); TPD(FRESO); TPD(FENV); TPD(FDEC);
    key("LFO_WAVE"); desc(&TP[P_LFO1 + LF_WAVE]);
    key("LFO_RATE"); desc(&TP[P_LFO1 + LF_RATE]);
    key("LFO_MORPH"); desc(&TP[P_LFO1 + LF_MORPH]);
    key("LFO_DEPTH"); desc(&TP[P_LFO1 + LF_DEPTH]);
    key("LFO_DEST"); desc(&TP[P_LFO1 + LF_DEST]);
    printf("},\n\"gp\": {");
    first = 1;
    GPD(BPM); GPD(CSRC); GPD(CTHR); GPD(CRAT); GPD(CREL); GPD(CATK); GPD(CMKUP); GPD(CGHOST);
    GPD(GMODE); GPD(GX); GPD(GY); GPD(GCHAOS); GPD(GFILL1); GPD(GFILL2); GPD(GFILL3);
    GPD(DTIME); GPD(DFDBK); GPD(DMIX); GPD(RSIZE); GPD(RDAMP); GPD(RTYPE); GPD(CRATE); GPD(CDEPTH);
    printf("},\n\"models\": [");
    for (m = 0; m < NMODELS; m++) {
        printf("%s\n {\"id\": %u, \"name\": ", m ? "," : "", m);
        str(N_MODEL[m]);
        printf(", \"voices\": %u, \"edit\": [", (unsigned)DMODELS[m].voices);
        for (e = 0; e < 8u; e++) {
            printf(e ? ", " : "");
            desc(&DMODELS[m].edit[e]);
        }
        printf("]}");
    }
    printf("],\n\"names\": {");
    first = 1;
    names("lwave", N_LWAVE, LW_COUNT);
    names("rmodel", N_RMODEL, RS_NMODEL);
    names("rchord", N_RCHORD, RS_NCHORD);
    names("ftype", N_FTYPE, FT_N);
    names("rtype", N_RTYPE, 2);
    names("ghost", N_GHOST, 3);
    names("lsync", N_LSYNC, 17);                       /* an LFO's RATE in SYNC: N_LSYNC[k * 17 / 128] */
    printf("},\n\"probe_size\": %u, \"max_frames\": %u, \"fs\": %u\n}\n", (unsigned)sizeof(lab_probe_t),
           (unsigned)LAB_MAX_FRAMES, (unsigned)FS);
    return 0;
}
