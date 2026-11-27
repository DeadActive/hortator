/* SPDX-License-Identifier: GPL-3.0-only */
/* The Grind controller on the native sequencer: the main loop generates a candidate into a RAM stage, the audio ISR
 * publishes it at the native cue boundary with a bounded copy. Built only with GRIND_NATIVE; included once, after
 * seq.c (cue, song, tracks), the amalgam and seq_adapter.c.
 *
 * Main loop: gm_runtime_submit / gm_runtime_poll / gm_touch / gm_runtime_cancel. Every shared transition runs with the
 * IRQ off (fm1_irq_off, the guard the cue already uses), the generator and the conversion run with it on.
 * Audio ISR: gm_runtime_commit (from cue_switch), gm_runtime_stop (from cue_stop), gm_touch_isr (live recording). No
 * generation, no flash, no loops over the generator there: one memcpy per track. */
#ifndef GRIND_RUNTIME_C
#define GRIND_RUNTIME_C

/* What the ISR copies: the native steps and the timing bytes of every track. */
typedef struct {
    step_t st[NTRK][NSTEP];
    uint8_t len[NTRK], div[NTRK], swing[NTRK], src[NTRK];
} gm_native_stage;
typedef char gm_stage_fits_the_pattern[sizeof(gm_native_stage) <= sizeof(gm_pattern) ? 1 : -1];

/* `base` (what the candidate is made from) is only used while a stage is WRITING, the native stage only while it is
 * READY or READING; the controller never has both, so they share the bytes. */
static struct {
    gm_controller ctl;
    uint32_t next_rev;
    uint8_t lock[GM_LOCK_BYTES];                          /* KEEP: the cells the generator leaves as they are */
    gm_native_stage undo;                                 /* the pattern before the last GRIND publish (or, after an UNDO, the one it undid) */
    uint8_t undo_ok, undo_was_undo;
    gm_pattern out;
    union {
        gm_pattern base;
        gm_native_stage n;
    } u;
} gmrt;

static void gm_runtime_reset(void)
{
    gm_config c;
    memset(&gmrt, 0, sizeof gmrt);
    gm_factory_config(0, &c);
    gm_ctl_init(&gmrt.ctl, &c);
    cue.q_gm = 0;
}
static uint32_t gm_runtime_commits(void) { return gmrt.ctl.commits; }
static gm_result gm_runtime_status(void) { return gmrt.ctl.status; }
static void gm_runtime_ack_status(void) { gmrt.ctl.status = GM_OK; }   /* (shown: said once) */

/* A native write of a step happened (or is about to): any candidate made from the old pattern is dropped. */
static void gm_touch_isr(void)
{
    gm_ctl_native_edit(&gmrt.ctl);
    if (gmrt.ctl.stage_state != GM_STAGE_READY && gmrt.ctl.stage_state != GM_STAGE_READING)
        cue.q_gm = 0;
}
static void gm_touch(void)
{
    fm1_irq_off();
    gm_touch_isr();
    fm1_irq_on();
}

/* The pattern in the tracks is another one (a load, a cue's pattern switch): its KEEPs and its UNDO are not this one's. */
static void gm_context_isr(void)
{
    memset(gmrt.lock, 0, sizeof gmrt.lock);
    gmrt.undo_ok = 0;
    gm_touch_isr();
}

/* STOP (seq_stop -> cue_stop) and a load or context reset: nothing pending, nothing waiting for a boundary. */
static void gm_runtime_stop(void)
{
    gm_ctl_cancel(&gmrt.ctl);
    cue.q_gm = 0;
}
static void gm_runtime_cancel(void)
{
    fm1_irq_off();
    gm_runtime_stop();
    fm1_irq_on();
}

/* The boundary (cue_switch, the ISR; or the main loop with the IRQ off while stopped): the stage goes live, once and
 * all or nothing. 0, or CUE_E_SAME when there is no valid stage any more (a stale one is a silent no-op). */
static int gm_runtime_commit(void)
{
    uint32_t k, rev = gmrt.ctl.stage_req.revision;
    if (!gm_ctl_consume(&gmrt.ctl, rev))
        return CUE_E_SAME;
    for (k = 0; k < NTRK; k++) {
        memcpy(gmrt.undo.st[k], trk[k].step, sizeof trk[k].step);       /* (what UNDO brings back) */
        gmrt.undo.len[k] = (uint8_t)trk[k].p[P_SLEN];
        gmrt.undo.div[k] = (uint8_t)trk[k].p[P_SDIV];
        gmrt.undo.swing[k] = (uint8_t)trk[k].p[P_SSWING];
        gmrt.undo.src[k] = (uint8_t)trk[k].p[P_SRC];
        memcpy(trk[k].step, gmrt.u.n.st[k], sizeof trk[k].step);
        trk[k].p[P_SLEN] = gmrt.u.n.len[k];
        trk[k].p[P_SDIV] = gmrt.u.n.div[k];
        trk[k].p[P_SSWING] = gmrt.u.n.swing[k];
        trk[k].p[P_SRC] = gmrt.u.n.src[k];
    }
    gmrt.undo_ok = 1;
    gmrt.undo_was_undo = 0;
    gm_ctl_commit(&gmrt.ctl, rev);
    return 0;
}

/* UNDO / REDO (main loop): the tracks swap with the pattern kept before the last publish, so a second one undoes the
 * first. Whatever waits for a boundary is dropped. 1 done, 0 nothing kept yet, -1 a song runs. The IRQ is off for
 * the swap: the audio ISR sees the old pattern or the new one, never a mix. */
static int gm_runtime_undo(void)
{
    uint32_t k;
    step_t tmp[NSTEP];
    if (chain.running)
        return -1;
    if (!gmrt.undo_ok)
        return 0;
    fm1_irq_off();
    gm_runtime_stop();
    for (k = 0; k < NTRK; k++) {
        uint8_t t8;
        memcpy(tmp, trk[k].step, sizeof tmp);
        memcpy(trk[k].step, gmrt.undo.st[k], sizeof tmp);
        memcpy(gmrt.undo.st[k], tmp, sizeof tmp);
        t8 = (uint8_t)trk[k].p[P_SLEN]; trk[k].p[P_SLEN] = gmrt.undo.len[k]; gmrt.undo.len[k] = t8;
        t8 = (uint8_t)trk[k].p[P_SDIV]; trk[k].p[P_SDIV] = gmrt.undo.div[k]; gmrt.undo.div[k] = t8;
        t8 = (uint8_t)trk[k].p[P_SSWING]; trk[k].p[P_SSWING] = gmrt.undo.swing[k]; gmrt.undo.swing[k] = t8;
        t8 = (uint8_t)trk[k].p[P_SRC]; trk[k].p[P_SRC] = gmrt.undo.src[k]; gmrt.undo.src[k] = t8;
    }
    gmrt.undo_was_undo ^= 1u;
    fm1_irq_on();
    return 1;
}

/* KEEP (main loop only: the ISR never reads the mask) */
static int gm_keep_get(uint32_t t, uint32_t s)
{
    uint32_t i = t * NSTEP + s;
    return (gmrt.lock[i >> 3] >> (i & 7u)) & 1u;
}
static void gm_keep_set(uint32_t t, uint32_t s, int on)
{
    uint32_t i = t * NSTEP + s;
    if (on)
        gmrt.lock[i >> 3] |= (uint8_t)(1u << (i & 7u));
    else
        gmrt.lock[i >> 3] &= (uint8_t)~(1u << (i & 7u));
}
static uint32_t gm_keep_count(uint32_t t)
{
    uint32_t s, n = 0;
    for (s = 0; s < NSTEP; s++)
        n += (uint32_t)gm_keep_get(t, s);
    return n;
}
static void gm_keep_track(uint32_t t, int on)             /* the steps the track has: the core refuses a keep past its length */
{
    uint32_t s, n = (uint32_t)trk[t].p[P_SLEN];
    for (s = 0; s < NSTEP; s++)
        gm_keep_set(t, s, on && s < n);
}
static void gm_keep_free(void) { memset(gmrt.lock, 0, sizeof gmrt.lock); }


/* Main loop: one request, the latest wins. A running song and a bar boundary are refused, not queued. */
static gm_result gm_runtime_submit(const gm_config *c, gm_boundary at, gm_request_kind kind)
{
    gm_request r;
    gm_result v;
    if (chain.running || (at != GM_BOUNDARY_STEP && at != GM_BOUNDARY_PATTERN))
        return GM_UNSUPPORTED;
    memset(&r, 0, sizeof r);
    if (!++gmrt.next_rev)
        ++gmrt.next_rev;                                  /* (0 is no request) */
    r.revision = gmrt.next_rev;
    r.at = at;
    r.kind = kind;
    r.follow_live = 1;
    r.config = *c;
    fm1_irq_off();
    v = gm_ctl_submit(&gmrt.ctl, &r);
    if (v == GM_OK && gmrt.ctl.stage_state == GM_STAGE_FREE)
        cue.q_gm = 0;                                     /* (the older stage is gone: nothing waits for a boundary) */
    fm1_irq_on();
    return v;
}

/* Main loop, every frame: take the pending request, make the candidate outside the IRQ, publish it. */
static void gm_runtime_poll(void)
{
    gm_request r;
    gm_result res;
    uint32_t k;
    fm1_irq_off();
    gm_ctl_reconcile(&gmrt.ctl);
    if (cue.q_slot || cue.q_var) {                        /* a native cue owns the boundary: the request waits */
        fm1_irq_on();
        return;
    }
    res = gm_ctl_begin(&gmrt.ctl, &r);
    fm1_irq_on();
    if (res != GM_OK)
        return;
    res = gm_native_export(&gmrt.u.base);
    if (res == GM_OK) {
        memcpy(gmrt.u.base.lock, gmrt.lock, sizeof gmrt.lock);
        for (k = 0; k < NTRK; k++) {                      /* (a LEN turned down since: the steps past it are not kept) */
            uint32_t s;
            for (s = gmrt.u.base.timing[k].length; s < NSTEP; s++)
                gmrt.u.base.lock[(k * NSTEP + s) >> 3] &= (uint8_t)~(1u << ((k * NSTEP + s) & 7u));
        }
        res = gm_generate(&r.config, &gmrt.u.base, &gmrt.out);
    }
    if (res == GM_OK)
        res = gm_native_check(&gmrt.out);
    if (res == GM_OK) {                                   /* (base is dead now: the native stage takes its bytes) */
        for (k = 0; k < NTRK; k++) {
            uint32_t s;
            uint8_t id;
            for (s = 0; s < NSTEP; s++)
                gm_to_native(&gmrt.u.n.st[k][s], gmrt.out.cell[k][s]);
            gm_native_division(gmrt.out.timing[k].division, &id);
            gmrt.u.n.len[k] = gmrt.out.timing[k].length;
            gmrt.u.n.div[k] = id;
            gmrt.u.n.swing[k] = gmrt.out.timing[k].swing;
            gmrt.u.n.src[k] = gmrt.out.timing[k].source;
        }
    }
    fm1_irq_off();
    res = gm_ctl_finish(&gmrt.ctl, r.revision, res);
    if (res == GM_OK) {
        if (cue.q_slot || cue.q_var) {                    /* a native cue came meanwhile: it wins, visibly */
            gm_ctl_cancel(&gmrt.ctl);
            gmrt.ctl.status = GM_STALE;
        } else if (song.playing) {
            cue.q_gm = 1;
            if (r.at == GM_BOUNDARY_STEP)
                cue.now = 1;
        } else {
            gm_runtime_commit();                          /* stopped: nothing to wait for, under the guard */
        }
    }
    fm1_irq_on();
}

#endif
