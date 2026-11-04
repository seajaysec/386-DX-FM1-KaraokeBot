/* SPDX-License-Identifier: GPL-3.0-only */
/* The master's last stage: the pump (the mix ducks on each kick), a one-knob filter (low-pass to the
 * left of the middle, high-pass to the right) and the sixteen punch-in effects held on the white keys.
 * Audio interrupt only, except the names. */
#pragma once
#include <stdint.h>

/* in the white keys' order, four families of four: tone, chop, brrr (the stronger to the right), then build,
 * crush, lo-fi and tape off */
enum { PU_LOWPASS, PU_HIGHPASS, PU_PHONE, PU_RING, PU_CHOP8, PU_CHOP16, PU_CHOP32, PU_CHOP64, PU_STUTTER8,
       PU_STUTTER16, PU_STUTTER32, PU_STUTTER64, PU_BUILD, PU_CRUSH, PU_LOFI, PU_TAPESTOP, PU_N };
extern const char *const PUNCH_NAME[PU_N];
/* SLAMJAM (GLO): the tone family's keys are its own (wah, echo, scratch, air horn), the brrr keys its snaps */
enum { PU_WAH = PU_LOWPASS, PU_ECHO = PU_HIGHPASS, PU_SCRATCH = PU_PHONE, PU_AIRHORN = PU_RING };
void punch_poetry(int on);
const char *punch_name(unsigned k);
void punch_snap(unsigned velocity);   /* SLAMJAM: a finger snap from the room */
void punch_look(uint8_t *look);       /* SLAMJAM's pictures: the wah's sweep, the echo's level, the scratch's place, the
                                       * horn's level, 0..127 each */

void punch_init(void);
void punch_set(uint32_t held);        /* the effects held, a bit each */
void punch_tempo(unsigned bpm);
void punch_pump(unsigned velocity);   /* a kick: the pump's key */
/* in place: pump 0..1, filter 0..100 (50 = off) */
void punch_process(float *l, float *r, uint32_t n, float pump, float filter);
float punch_duck(void);               /* the pump's duck now, 0..1 */
float punch_gate(void);               /* the chops' gate now, 0 shut .. 1 open */
uint32_t punch_reps(void);            /* a brrr's repeats so far */
