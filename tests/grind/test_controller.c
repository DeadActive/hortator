/* SPDX-License-Identifier: GPL-3.0-only */
/* T10: the controller's state machine alone: latest wins, revisions by equality, stage ownership. */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "controller.h"

static int fails;
static int check(const char *what, int ok)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
    return ok;
}

static gm_request req(const gm_controller *c, uint32_t rev, uint8_t live)
{
    gm_request r;
    memset(&r, 0, sizeof r);
    r.revision = rev;
    r.base_revision = c->native_revision;
    r.follow_live = live;
    gm_factory_config(2, &r.config);
    return r;
}

static gm_controller fresh(void)
{
    gm_config a;
    gm_controller c;
    gm_factory_config(0, &a);
    gm_ctl_init(&c, &a);
    return c;
}
static void stage(gm_controller *c, uint32_t rev)
{
    gm_request r = req(c, rev, 0), got;
    assert(gm_ctl_submit(c, &r) == GM_OK);
    assert(gm_ctl_begin(c, &got) == GM_OK && got.revision == rev);
    assert(gm_ctl_finish(c, rev, GM_OK) == GM_OK);
}

static void happy_path(void)
{
    gm_controller c = fresh();
    uint32_t base, ok = 1;
    stage(&c, 1);
    base = c.native_revision;
    ok &= c.stage_state == GM_STAGE_READY && c.active_revision == 0;      /* (nothing live before the boundary) */
    ok &= gm_ctl_consume(&c, 1) == 1 && c.stage_state == GM_STAGE_READING;
    ok &= gm_ctl_consume(&c, 1) == 0;                                      /* (consumed exactly once) */
    ok &= c.active_revision == 0;
    gm_ctl_commit(&c, 1);
    ok &= c.active_revision == 1 && c.acked_revision == 1 && c.native_revision == base + 1u && c.commits == 1u;
    ok &= gm_ctl_reconcile(&c) == 1 && c.stage_state == GM_STAGE_FREE && gm_ctl_reconcile(&c) == 0;
    check("controller: submit, stage, consume once, commit, reconcile; nothing live before the boundary", ok);
}

static void latest_wins(void)
{
    gm_controller c = fresh();
    gm_request r2, got;
    uint32_t ok = 1;
    stage(&c, 1);
    r2 = req(&c, 2, 0);
    ok &= gm_ctl_submit(&c, &r2) == GM_OK;
    ok &= c.stage_state == GM_STAGE_FREE && gm_ctl_consume(&c, 1) == 0;     /* revision 1 can never publish now */
    ok &= gm_ctl_begin(&c, &got) == GM_OK && got.revision == 2;
    ok &= gm_ctl_finish(&c, 2, GM_OK) == GM_OK && gm_ctl_consume(&c, 2) == 1;
    gm_ctl_commit(&c, 2);
    ok &= c.commits == 1u && c.active_revision == 2;
    check("controller: a newer request before the boundary replaces the staged one: only revision 2 publishes", ok);
}

static void replaced_while_pending(void)
{
    gm_controller c = fresh();
    gm_request r1 = req(&c, 1, 0), r2 = req(&c, 2, 0), got;
    uint32_t ok = 1;
    ok &= gm_ctl_submit(&c, &r1) == GM_OK && gm_ctl_submit(&c, &r2) == GM_OK;    /* (knob turned twice, no poll) */
    ok &= gm_ctl_begin(&c, &got) == GM_OK && got.revision == 2 && gm_ctl_begin(&c, &got) == GM_NO_CHANGE;
    check("controller: two submits before a poll leave one request: the latest", ok);
}

static void edit_invalidates(void)
{
    gm_controller c = fresh();
    uint32_t ok = 1, before;
    stage(&c, 3);
    before = c.native_revision;
    gm_ctl_native_edit(&c);                                                  /* a manual step edit */
    ok &= c.native_revision == before + 1u && c.stage_state == GM_STAGE_FREE && c.status == GM_STALE;
    ok &= gm_ctl_consume(&c, 3) == 0 && c.active_revision == 0 && c.commits == 0u;
    check("controller: a native edit after staging rejects the candidate; the edit stands, nothing is published", ok);
}

static void edit_while_writing(void)
{
    gm_controller c = fresh();
    gm_request r = req(&c, 4, 0), got;
    uint32_t ok = 1;
    gm_ctl_submit(&c, &r);
    ok &= gm_ctl_begin(&c, &got) == GM_OK && c.stage_state == GM_STAGE_WRITING;
    gm_ctl_native_edit(&c);                                                  /* during generation */
    ok &= gm_ctl_finish(&c, 4, GM_OK) == GM_STALE && c.stage_state == GM_STAGE_FREE;
    check("controller: an edit while the candidate is being generated drops it at finish", ok);
}

static void stale_pending(void)
{
    gm_controller c = fresh();
    gm_request r = req(&c, 5, 0), got;
    uint32_t ok = 1;
    gm_ctl_submit(&c, &r);
    gm_ctl_native_edit(&c);                                                  /* the base moves before it starts */
    ok &= gm_ctl_begin(&c, &got) == GM_STALE && !c.has_pending && c.status == GM_STALE;
    r = req(&c, 6, 1);                                                       /* a knob: made from what is live now */
    r.base_revision = 12345u;
    gm_ctl_submit(&c, &r);
    ok &= gm_ctl_begin(&c, &got) == GM_OK && got.base_revision == c.native_revision;
    check("controller: a request on an old base is refused; a follow-live knob request adopts the live base", ok);
}

static void stop_cancels(void)
{
    gm_controller c = fresh();
    gm_request r = req(&c, 7, 0), got;
    uint32_t ok = 1;
    stage(&c, 7);
    gm_ctl_cancel(&c);
    ok &= gm_ctl_consume(&c, 7) == 0 && !c.has_pending && c.stage_state == GM_STAGE_FREE;
    r.revision = 8;
    gm_ctl_submit(&c, &r);
    gm_ctl_begin(&c, &got);
    gm_ctl_cancel(&c);                                                       /* STOP while it is being written */
    ok &= gm_ctl_finish(&c, 8, GM_OK) == GM_STALE && c.stage_state == GM_STAGE_FREE;
    r.revision = 9;
    gm_ctl_submit(&c, &r);                                                   /* stopped: a new command is accepted */
    ok &= gm_ctl_begin(&c, &got) == GM_OK;
    check("controller: STOP drops pending and READY stages, a late writer finds none, new commands work", ok);
}

static void reading_is_owned_by_audio(void)
{
    gm_controller c = fresh();
    gm_request r = req(&c, 10, 0), got;
    uint32_t ok = 1;
    stage(&c, 10);
    gm_ctl_consume(&c, 10);
    gm_ctl_cancel(&c);                                                       /* (a stage being read completes) */
    ok &= c.stage_state == GM_STAGE_READING;
    r.revision = 11;
    ok &= gm_ctl_submit(&c, &r) == GM_OK && c.stage_state == GM_STAGE_READING;
    ok &= gm_ctl_begin(&c, &got) == GM_NO_CHANGE;                            /* (never reuse a READING buffer) */
    gm_ctl_native_edit(&c);                                                  /* an edit now cannot undo the read */
    ok &= c.stage_state == GM_STAGE_READING;
    gm_ctl_commit(&c, 10);
    ok &= c.stage_state == GM_STAGE_RETIRED && gm_ctl_begin(&c, &got) == GM_NO_CHANGE;
    gm_ctl_reconcile(&c);
    ok &= gm_ctl_begin(&c, &got) == GM_STALE;                                /* (11 was made on the old base) */
    check("controller: a stage being read is never reused or cancelled; a request behind it needs the new base", ok);
}

static void invalid_and_wrap(void)
{
    gm_controller c = fresh();
    gm_request r = req(&c, 1, 0), got;
    uint32_t ok = 1, k;
    r.config.algorithm = 200;
    ok &= gm_ctl_submit(&c, &r) != GM_OK && !c.has_pending;
    c.native_revision = 0xFFFFFFFEu;                                         /* wrap by equality, not ordering */
    for (k = 0; k < 4u; k++) {
        r = req(&c, 100u + k, 0);
        ok &= gm_ctl_submit(&c, &r) == GM_OK && gm_ctl_begin(&c, &got) == GM_OK;
        ok &= gm_ctl_finish(&c, got.revision, GM_OK) == GM_OK && gm_ctl_consume(&c, got.revision) == 1;
        gm_ctl_commit(&c, got.revision);
        gm_ctl_reconcile(&c);
    }
    ok &= c.native_revision == 2u && c.commits == 4u;
    check("controller: an invalid config never queues; revisions wrap past 2^32 without a stale or lost commit", ok);
}

int main(void)
{
    happy_path();
    latest_wins();
    replaced_while_pending();
    edit_invalidates();
    edit_while_writing();
    stale_pending();
    stop_cancels();
    reading_is_owned_by_audio();
    invalid_and_wrap();
    printf("test_controller: %s\n", fails ? "FAILED" : "all passed");
    return fails ? 1 : 0;
}
