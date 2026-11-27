/* SPDX-License-Identifier: GPL-3.0-only */
/* The portable core as one translation unit, for the builds that textually include their modules (felucca.c, the host
 * fixtures): include it exactly once. The standalone suites compile the modules separately and never include this. */
#ifndef GRIND_AMALGAM_C
#define GRIND_AMALGAM_C
#include "grid.c"
#include "rng.c"
#include "locks.c"
#include "rules.c"
#include "core.c"
#include "serialize.c"
#endif
