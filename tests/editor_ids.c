/* SPDX-License-Identifier: GPL-3.0-only
 * Drum machine fork: 2026 DEADACTIVE */
/* The web editor's device data (web/EDITOR_PROTOCOL.md, Hortator editor protocol 1): the firmware's parameter and
 * global tables, the models' SOUND knobs, the pages, the factory presets, the PROB codes and the stored objects'
 * layouts, printed as JSON (tools/gen_editor_data.sh -> web/editor/device_data.json). The editor's mock device
 * (web/editor/mock_device.mjs) answers from it, so it never carries a copy of a number; web/editor/test_editor.mjs
 * fails when the committed file differs from what this prints. Same sources as tests/sim_ids.c (tests/sim_core.c). */
#include "sim_core.c"
#include <stdio.h>
#include <stddef.h>

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
/* a knob as the editor's DESC gives it; names: those of min..max (an enum) or the split list (F_INT + names) */
static void desc(const param_desc_t *d, int lock)
{
    int i;
    printf("{\"label\": ");
    str(d->label ? d->label : "");
    printf(", \"fmt\": %u, \"min\": %d, \"max\": %d, \"def\": %d", d->fmt, d->min, d->max, d->def);
    if (d->names) {
        printf(", \"names\": [");
        if (d->fmt == F_ENUM) {
            for (i = d->min; i <= d->max; i++)
                printf(i > d->min ? ", " : ""), str(d->names[i]);
        } else {
            for (i = 0; d->names[i]; i++)
                printf(i ? ", " : ""), str(d->names[i]);
        }
        printf("]");
        if (d->fmt != F_ENUM)
            printf(", \"split\": 1");               /* F_INT: the range split evenly over the names */
    }
    if (d->unit)
        printf(", \"unit\": "), str(d->unit);
    if (enum_order(d)) {                          /* the shown order (DIV, the delay's TIME): value - min by place */
        printf(", \"order\": [");
        for (i = 0; i <= d->max - d->min; i++)
            printf("%s%u", i ? ", " : "", enum_order(d)[i]);
        printf("]");
    }
    if (lock >= 0)
        printf(", \"lock\": %d", lock);
    printf("}");
}

static const char *PN[P_COUNT], *GN[G_COUNT];
#define PNAME(n) PN[P_##n] = #n
#define GNAME(n) GN[G_##n] = #n

int main(void)
{
    uint32_t i, k, m;
    static const char *const LFN[LF_N] = {"WAVE", "MODE", "RATE", "MORPH", "DEPTH", "DEST", "TRIG", "PHASE"};
    static char lfo[2][LF_N][16];
    static const char *const FMT[] = {"INT", "PCT", "BIPCT", "TIME", "LFOHZ", "CUTOFF", "DB", "SEMI", "ENUM", "BPM",
                                      "NOTE", "ONOFF", "OCT", "STEPS", "CTHR", "CRAT", "CATK", "CREL", "CMKUP",
                                      "LRATE1", "LRATE2", "LDEST", "LPHASE", "NOTEO", "RDECAY", "FDEC", "SRR", "SLOT",
                                      "LSLOT"};
    _Static_assert(sizeof FMT / sizeof FMT[0] == F_LSLOT + 1, "every F_ named");
    PNAME(MODEL); PNAME(E0); PNAME(E1); PNAME(E2); PNAME(E3); PNAME(E4); PNAME(E5); PNAME(E6); PNAME(E7);
    PNAME(LEVEL); PNAME(PAN); PNAME(MUTE); PNAME(CHOKE); PNAME(NOTE); PNAME(DIST); PNAME(CHOR); PNAME(DLY); PNAME(REV);
    PNAME(SLCR); PNAME(SLPAT); PNAME(SLRATE); PNAME(SLDEPTH); PNAME(SLEN); PNAME(SDIV); PNAME(SSWING);
    PNAME(LSET); PNAME(LKEY); PNAME(LLEVEL); PNAME(LTUNE); PNAME(LDEC); PNAME(SRC); PNAME(DUCK);
    PNAME(RMODEL); PNAME(RTUNE); PNAME(RDECAY); PNAME(RMIX); PNAME(RTONE); PNAME(RSTRCT); PNAME(RPOS);
    PNAME(FTYPE); PNAME(FCUT); PNAME(FRESO); PNAME(FENV); PNAME(FDEC); PNAME(PRIO); PNAME(LSRC1); PNAME(LSRC2);
    PNAME(LS3SRC); PNAME(LS3DEST); PNAME(LS3DEPTH); PNAME(LS4SRC); PNAME(LS4DEST); PNAME(LS4DEPTH);
    PNAME(RKEY); PNAME(SRR); PNAME(ITYPE); PNAME(IA); PNAME(IB); PNAME(IC); PNAME(IMIX);
    PNAME(SATK); PNAME(SSUS); PNAME(SREL); PNAME(SLOOP); PNAME(SPLAY); PNAME(SCUT);
    for (k = 0; k < 2u; k++)
        for (i = 0; i < LF_N; i++) {
            snprintf(lfo[k][i], sizeof lfo[k][i], "LFO%u_%s", (unsigned)k + 1u, LFN[i]);
            PN[(k ? P_LFO2 : P_LFO1) + i] = lfo[k][i];
        }
    GNAME(BPM); GNAME(SWING); GNAME(CLOCK); GNAME(DTIME); GNAME(DFDBK); GNAME(DCOLOR); GNAME(DMIX); GNAME(RSIZE);
    GNAME(RDAMP); GNAME(CRATE); GNAME(CDEPTH); GNAME(MIDI); GNAME(SYNC); GNAME(ROUTE); GNAME(INFO); GNAME(SLOT);
    GNAME(NAME); GNAME(LOAD); GNAME(SAVE); GNAME(CLRSEQ); GNAME(INITSND); GNAME(DRCH); GNAME(GMODE); GNAME(GX);
    GNAME(GY); GNAME(GCHAOS); GNAME(GFILL1); GNAME(GFILL2); GNAME(GFILL3); GNAME(GLEN1); GNAME(GLEN2); GNAME(GLEN3);
    GNAME(CSRC); GNAME(CTHR); GNAME(CRAT); GNAME(CREL); GNAME(CATK); GNAME(CKNEE); GNAME(CMKUP); GNAME(CGHOST);
    GNAME(RTYPE); GNAME(DMODE);
    for (i = 0; i < P_COUNT; i++)
        if (!PN[i])
            return fprintf(stderr, "editor_ids: P id %u has no name\n", (unsigned)i), 1;
    for (i = 0; i < G_COUNT; i++)
        if (!GN[i])
            return fprintf(stderr, "editor_ids: G id %u has no name\n", (unsigned)i), 1;

    printf("{\n\"generated\": \"tests/editor_ids.c (tools/gen_editor_data.sh): do not edit\",\n");
    printf("\"consts\": {\"NTRK\": %u, \"NSTEP\": %u, \"P_COUNT\": %u, \"G_COUNT\": %u, \"P_NSTORED\": %u, "
           "\"G_NSTORED\": %u, \"NMODELS\": %u, \"PRE_NFACT\": %u, \"PRE_NUSER\": %u, \"COND_MAX\": %u, "
           "\"NT_MIN\": %d, \"NT_MAX\": %d, \"LK_MAX\": %u, \"VAR_BYTES\": %u, \"VAR_N\": %u, \"NAME_LEN\": %u, "
           "\"ACID\": %u, \"ACID_MAXTRK\": %u, \"RS_OFF\": %u, \"RS_CHORD\": %u, \"RS_MODAL\": %u, \"RS_MAXTRK\": %u, "
           "\"RS_MAXMODAL\": %u, \"MICRO_MAX\": %u, \"UPR_NOLD\": %u, \"UPR_NFX\": %u, \"UPR_USED\": %u, "
           "\"UPR_VOICE\": %u, \"DM_SMPL\": %u, \"UPR_FENV\": %u, \"SND_N\": %u},\n",
           NTRK, NSTEP, P_COUNT, G_COUNT, P_NSTORED, G_NSTORED, NMODELS, (unsigned)PRE_NFACT, (unsigned)PRE_NUSER,
           (unsigned)COND_MAX, NT_MIN, NT_MAX, (unsigned)LK_MAX, (unsigned)VAR_BYTES, (unsigned)VAR_N,
           (unsigned)NAME_LEN, (unsigned)DM_ACID, (unsigned)ACID_MAXTRK, (unsigned)RS_OFF, (unsigned)RS_CHORD,
           (unsigned)RS_MODAL, (unsigned)RS_MAXTRK, (unsigned)RS_MAXMODAL, (unsigned)MICRO_MAX, (unsigned)UPR_NOLD,
           (unsigned)UPR_NFX, (unsigned)UPR_USED, (unsigned)UPR_VOICE, (unsigned)DM_SMPL, (unsigned)UPR_FENV,
           (unsigned)SND_N);
    printf("\"uprFx\": [");                         /* the FX a user preset holds (presets.c UPR_FX), in its order */
    for (i = 0; i < UPR_NFX; i++)
        printf("%s%u", i ? ", " : "", (unsigned)UPR_FX[i]);
    printf("],\n");
    printf("\"nameSet\": "), str(NAME_SET), printf(",\n");
    printf("\"fmt\": [");
    for (i = 0; i <= F_LSLOT; i++)
        printf(i ? ", " : ""), str(FMT[i]);
    printf("],\n\"P\": [");
    for (i = 0; i < P_COUNT; i++)
        printf(i ? ", " : ""), str(PN[i]);
    printf("],\n\"G\": [");
    for (i = 0; i < G_COUNT; i++)
        printf(i ? ", " : ""), str(GN[i]);
    printf("],\n\"tp\": [");
    for (i = 0; i < P_COUNT; i++)
        printf(i ? ",\n " : "\n "), desc(&TP[i], motion_param(i));
    printf("],\n\"gp\": [");
    for (i = 0; i < G_COUNT; i++)
        printf(i ? ",\n " : "\n "), desc(&GP[i], -1);
    printf("],\n\"insDesc\": [");                  /* INSERT A B C by TYPE: 0 the drives, 1 CRUSH, 2 the swept ones */
    for (k = 0; k < 3u; k++) {
        printf(k ? ", [" : "[");
        for (i = 0; i < 3u; i++)
            printf(i ? ", " : ""), desc(&INS_DESC[k][i], -1);
        printf("]");
    }
    printf("],\n\"rsChordDesc\": "), desc(&RS_CHORD_DESC, -1);
    printf(",\n\"lsId\": [");
    for (k = 0; k < 4u; k++)
        printf("%s[%u, %u, %u]", k ? ", " : "", LS_ID[k][0], LS_ID[k][1], LS_ID[k][2]);
    printf("],\n\"ldest\": [");                    /* an LFO DEST value 1..27: the knob it moves (lfo_dest_param), or its name */
    for (i = 1; i <= 27u; i++)
        if (i < 11u || i > 18u)
            printf("%s%u", i > 1u ? ", " : "", (unsigned)lfo_dest_param(i));
        else
            printf(", "), str(N_RDEST[i - 11u]);
    printf("],\n\"models\": [");
    for (m = 0; m < NMODELS; m++) {
        printf("%s\n {\"name\": ", m ? "," : "");
        str(N_MODEL[m]);
        printf(", \"voices\": %u, \"edit\": [", (unsigned)DMODELS[m].voices);
        for (k = 0; k < 8u; k++)
            printf(k ? ", " : ""), desc(&DMODELS[m].edit[k], -1);
        printf("]}");
    }
    printf("],\n\"modelList\": [");
    for (m = 0; m < NMODELS; m++)
        printf("%s%u", m ? ", " : "", (unsigned)MODEL_LIST[m]);
    printf("],\n\"banks\": [");
    for (k = 0; k < PB_USER; k++) {
        printf("%s{\"name\": ", k ? ", " : "");
        str(N_PBANK[k]);
        printf(", \"start\": %u, \"count\": %u}", PRE_BANK_START[k], PRE_BANK_START[k + 1u] - PRE_BANK_START[k]);
    }
    printf("],\n\"presets\": [");
    for (i = 0; i < PRE_NFACT; i++) {
        const preset_t *p = &PRE_FACTORY[i];
        printf("%s\n {\"uid\": %u, \"model\": %u, \"name\": ", i ? "," : "", PRE_UID[i], p->model);
        str(p->name);
        printf(", \"k\": [%d, %d, %d, %d, %d, %d, %d, %d]}", p->k[0], p->k[1], p->k[2], p->k[3], p->k[4], p->k[5],
               p->k[6], p->k[7]);
    }
    printf("],\n\"pages\": [");
    for (i = 0; i < NPAGES; i++) {
        const page_t *g = &PAGES[i];
        printf("%s\n {\"title\": ", i ? "," : "");
        str(g->title);
        printf(", \"fam\": %u, \"scope\": %u, \"graph\": %u, \"id\": [%u, %u, %u, %u]}", g->fam, g->scope, g->graph,
               g->id[0], g->id[1], g->id[2], g->id[3]);
    }
    printf("],\n\"sndSlot\": %u, \"gr\": {\"SLOTS\": %u, \"MOTION\": %u, \"SONG\": %u, \"GRIDS\": %u, \"LDEST\": %u, "
           "\"LFO\": %u},\n",
           SND_SLOT, GR_SLOTS, GR_MOTION, GR_SONG, GR_GRIDS, GR_LDEST, GR_LFO);
    printf("\"sc\": {\"TRACK\": %u, \"GLOBAL\": %u}, \"preNone\": %u, \"preRecUser\": %u,\n", SC_TRACK, SC_GLOBAL,
           PRE_NONE, PRE_REC_USER);
    printf("\"conds\": [");                         /* the PROB knob, position by position: stored code, name */
    for (i = 0; i <= COND_MAX; i++) {
        char s[8];
        cond_format(cond_store(i), s);
        printf("%s{\"store\": %u, \"name\": ", i ? ", " : "", (unsigned)cond_store(i));
        str(s);
        printf("}");
    }
    printf("],\n\"magic\": {\"PROJ\": %u, \"LK\": %u, \"VAR\": %u, \"UPR\": %u, \"UPR1\": %u, \"ST\": %u},\n",
           PROJ_MAGIC, LK_MAGIC, VAR_MAGIC, UPR_MAGIC, UPR1_MAGIC, ST_MAGIC);
    printf("\"oldProjects\": [");                   /* the older records the firmware converts at load: magic, size */
    {
        static const uint32_t MAGIC[15] = {PROJ_MAGIC_V1, PROJ_MAGIC_V2, PROJ_MAGIC_V3, PROJ_MAGIC_V4, PROJ_MAGIC_V5,
                                           PROJ_MAGIC_V6, PROJ_MAGIC_V7, PROJ_MAGIC_V8, PROJ_MAGIC_V9, PROJ_MAGIC_VA,
                                           PROJ_MAGIC_VB, PROJ_MAGIC_VC, PROJ_MAGIC_VD, PROJ_MAGIC_VE,
                                           PROJ_MAGIC_VF};
        const uint32_t SIZE[15] = {sizeof proj_old.v1, sizeof proj_old.v2, sizeof proj_old.v3, sizeof proj_old.v4,
                                   sizeof proj_old.v5, sizeof proj_old.v6, sizeof proj_old.v7, sizeof proj_old.v8,
                                   sizeof proj_old.v9, sizeof proj_old.va, sizeof proj_old.vb, sizeof proj_old.vc,
                                   sizeof proj_old.vd, sizeof proj_old.ve, sizeof proj_old.vf};
        for (i = 0; i < 15u; i++)
            printf("%s[%u, %u]", i ? ", " : "", MAGIC[i], SIZE[i]);
    }
    printf("],\n\"layout\": {\n");
#define OFF(t, f) (unsigned)offsetof(t, f)
    printf(" \"project\": {\"size\": %u, \"magic\": %u, \"sizeField\": %u, \"g\": %u, \"sel\": %u, \"rsv\": %u, "
           "\"t\": %u, \"trk\": %u, \"trkP\": %u, \"trkStep\": %u, \"lk_n\": %u, \"lk_link\": %u, \"var_link\": %u, "
           "\"var_len\": %u, \"xp\": %u, \"xp2\": %u, \"xp3\": %u, \"rsv_mo\": %u, \"song\": %u, \"name\": %u, \"pre\": %u, \"pre_ed\": %u, \"sum\": %u},\n",
           (unsigned)sizeof(project_t), OFF(project_t, magic), OFF(project_t, size), OFF(project_t, g),
           OFF(project_t, sel), OFF(project_t, rsv), OFF(project_t, t), (unsigned)sizeof(proj_trk_t),
           OFF(proj_trk_t, p), OFF(proj_trk_t, step), OFF(project_t, lk_n), OFF(project_t, lk_link),
           OFF(project_t, var_link), OFF(project_t, var_len), OFF(project_t, xp), OFF(project_t, xp2), OFF(project_t, xp3), OFF(project_t, rsv_mo), OFF(project_t, song),
           OFF(project_t, name), OFF(project_t, pre), OFF(project_t, pre_ed), OFF(project_t, sum));
    printf(" \"locks\": {\"size\": %u, \"magic\": %u, \"slot\": %u, \"on\": %u, \"count\": %u, \"key\": %u, "
           "\"val\": %u, \"lock\": %u},\n",
           (unsigned)sizeof(lk_bank_t), OFF(lk_bank_t, magic), OFF(lk_bank_t, slot), OFF(lk_bank_t, on),
           OFF(lk_bank_t, count), OFF(lk_bank_t, key), OFF(lk_bank_t, val), OFF(lk_bank_t, lock));
    printf(" \"vars\": {\"size\": %u, \"magic\": %u, \"slot\": %u, \"used\": %u, \"d\": %u},\n",
           (unsigned)sizeof(var_bank_t), OFF(var_bank_t, magic), OFF(var_bank_t, slot), OFF(var_bank_t, used),
           OFF(var_bank_t, d));
    printf(" \"presets\": {\"size\": %u, \"slot\": %u, \"fx\": %u, \"mv\": %u, \"sizeV1\": %u},\n \"songSize\": %u\n}\n}\n",
           (unsigned)sizeof(upr_rec_t), (unsigned)sizeof(upr_slot_t), OFF(upr_slot_t, f), OFF(upr_rec_t, mv),
           (unsigned)UPR1_SIZE, (unsigned)sizeof(chain_config_t));
#undef OFF
    return 0;
}
