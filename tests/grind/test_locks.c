/* SPDX-License-Identifier: GPL-3.0-only */
#include "common.h"
#include <string.h>

int main(void)
{
    gm_pattern p = {0};
    assert(!gm_is_locked(&p, 7, 63));
    assert(gm_set_lock(&p, 0, 0, 1) == GM_OK);
    assert(gm_set_lock(&p, 7, 63, 1) == GM_OK);
    assert(p.lock[0] == 1 && p.lock[63] == 128);
    assert(gm_is_locked(&p, 0, 0) && gm_is_locked(&p, 7, 63));
    assert(gm_set_lock(&p, 7, 63, 0) == GM_OK);
    gm_pattern before = p;
    assert(gm_set_lock(&p, 8, 0, 1) == GM_INVALID);
    assert(gm_set_lock(&p, 0, 64, 0) == GM_INVALID);
    assert(gm_set_lock(NULL, 0, 0, 1) == GM_INVALID);
    assert(!gm_is_locked(NULL, 0, 0));
    assert(!memcmp(&p, &before, sizeof p));
    return 0;
}
