/* SPDX-License-Identifier: GPL-3.0-only */
/* The synth, the drum kit and the effects as one unit for the FM-1 build: the fast-math helpers they call out
 * of line (fm_exp2f, fm_expf, fm_db2lin) are then one copy, not one for each file. The host and the browser build
 * the three files apart (tools/music_sources.py); the code is the same either way. */
#include "synth.c"
#undef SUB
#include "drumkit.c"
#undef SUB
#undef SR
#include "fxunit.c"
