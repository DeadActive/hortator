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
    uint8_t lock[GM_LOCK_BYTES];
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
        memcpy(trk[k].step, gmrt.u.n.st[k], sizeof trk[k].step);
        trk[k].p[P_SLEN] = gmrt.u.n.len[k];
        trk[k].p[P_SDIV] = gmrt.u.n.div[k];
        trk[k].p[P_SSWING] = gmrt.u.n.swing[k];
        trk[k].p[P_SRC] = gmrt.u.n.src[k];
    }
    gm_ctl_commit(&gmrt.ctl, rev);
    return 0;
}

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
