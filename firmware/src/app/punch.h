/* SPDX-License-Identifier: GPL-3.0-only */
/* The master's last stage: the pump (the mix ducks on each kick), a one-knob filter (low-pass to the
 * left of the middle, high-pass to the right) and the sixteen punch-in effects held on the white keys.
 * Audio interrupt only, except the names. */
#pragma once
#include <stdint.h>

enum { PU_LOWPASS, PU_HIGHPASS, PU_PHONE, PU_CRUSH, PU_STUTTER8, PU_STUTTER16, PU_STUTTER32, PU_STUTTER64,
       PU_TAPESTOP, PU_REVERSE, PU_CHOP8, PU_CHOP16, PU_RING, PU_HALF, PU_MUTE, PU_LOFI, PU_N };
extern const char *const PUNCH_NAME[PU_N];

void punch_init(void);
void punch_set(uint32_t held);        /* the effects held, a bit each */
void punch_tempo(unsigned bpm);
void punch_pump(unsigned velocity);   /* a kick: the pump's key */
/* in place: pump 0..1, filter 0..100 (50 = off) */
void punch_process(float *l, float *r, uint32_t n, float pump, float filter);
