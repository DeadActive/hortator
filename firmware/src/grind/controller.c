/* SPDX-License-Identifier: GPL-3.0-only */
#include "controller.h"

void gm_ctl_init(gm_controller *c, const gm_config *active)
{
    uint32_t i;
    uint8_t *p = (uint8_t *)c;
    for (i = 0; i < sizeof *c; i++)
        p[i] = 0;
    if (active)
        c->active_config = *active;
}

gm_result gm_ctl_submit(gm_controller *c, const gm_request *r)
{
    gm_result v = gm_validate_config(&r->config);
    if (v != GM_OK)
        return v;
    if (!r->revision)
        return GM_INVALID;                                       /* (0 is no request: gm_ctl_reconcile
                                                              * returns it for "nothing acknowledged") */
    c->pending = *r;
    c->has_pending = 1;
    if (c->stage_state == GM_STAGE_READY && c->stage_req.revision != r->revision)
        c->stage_state = GM_STAGE_FREE;                       /* (a candidate of an older request: never published) */
    return GM_OK;
}

gm_result gm_ctl_begin(gm_controller *c, gm_request *out)
{
    if (!c->has_pending || c->stage_state != GM_STAGE_FREE)
        return GM_NO_CHANGE;
    if (c->pending.follow_live)
        c->pending.base_revision = c->native_revision;
    if (c->pending.base_revision != c->native_revision) {
        c->has_pending = 0;
        c->status = GM_STALE;
        return GM_STALE;
    }
    c->stage_req = c->pending;
    c->has_pending = 0;
    c->stage_state = GM_STAGE_WRITING;
    *out = c->stage_req;
    return GM_OK;
}

gm_result gm_ctl_finish(gm_controller *c, uint32_t revision, gm_result result)
{
    if (c->stage_state != GM_STAGE_WRITING || c->stage_req.revision != revision)
        return GM_STALE;                                     /* (cancelled meanwhile: its stage is not ours) */
    if (result != GM_OK) {
        c->stage_state = GM_STAGE_FREE;
        c->status = result;
        return result;
    }
    if (c->has_pending || c->stage_req.base_revision != c->native_revision) {   /* (superseded, or its base moved) */
        c->stage_state = GM_STAGE_FREE;
        c->status = GM_STALE;
        return GM_STALE;
    }
    c->stage_state = GM_STAGE_READY;
    c->status = GM_OK;
    return GM_OK;
}

void gm_ctl_native_edit(gm_controller *c)
{
    c->native_revision++;
    if (c->stage_state == GM_STAGE_READY) {
        c->stage_state = GM_STAGE_FREE;
        c->status = GM_STALE;
    }
}

void gm_ctl_cancel(gm_controller *c)
{
    c->has_pending = 0;
    if (c->stage_state == GM_STAGE_READY || c->stage_state == GM_STAGE_WRITING)
        c->stage_state = GM_STAGE_FREE;                      /* (a writer finishing later finds no stage: GM_STALE) */
}

int gm_ctl_consume(gm_controller *c, uint32_t revision)
{
    if (c->stage_state != GM_STAGE_READY || c->stage_req.revision != revision ||
        c->stage_req.base_revision != c->native_revision)
        return 0;
    c->stage_state = GM_STAGE_READING;
    return 1;
}

void gm_ctl_commit(gm_controller *c, uint32_t revision)
{
    if (c->stage_state != GM_STAGE_READING || c->stage_req.revision != revision)
        return;
    c->active_revision = revision;
    c->active_config = c->stage_req.config;
    c->native_revision++;                                    /* (the base changed: whatever was made from the old one
                                                              * is stale) */
    c->acked_revision = revision;
    c->commits++;
    c->stage_state = GM_STAGE_RETIRED;
}

uint32_t gm_ctl_reconcile(gm_controller *c)
{
    if (c->stage_state != GM_STAGE_RETIRED)
        return 0;
    c->stage_state = GM_STAGE_FREE;
    return c->acked_revision;
}
