/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include "singing.h"
#define DX_SPEECH_CACHES 8
#define DX_SPEECH_LONG_SAMPLES 49612 /* 4.5 seconds at 11025 Hz: one clip's most */
#define DX_SPEECH_ARENA_SAMPLES 71680 /* 6.5 seconds: the sung clips share it (vocal.c) */
typedef union {
    int16_t samples[DX_SPEECH_ARENA_SAMPLES];
    int16_t line[1][DX_SPEECH_LONG_SAMPLES];  /* scratch for measuring (nothing is written there) */
} dx_speech_cache_t;
extern dx_speech_cache_t dx_speech_cache;
extern uint32_t dx_speech_length[DX_SPEECH_CACHES];
extern uint8_t dx_speech_truncated[DX_SPEECH_CACHES];
extern uint32_t dx_speech_clipped[DX_SPEECH_CACHES]; /* raw synth or resampler overloads */
int dx_speech_init(void);
void dx_speech_idle(void); /* platform watchdog hook */
/* Render one syllable at explicit musical pitches and exact total duration into dst (a span of the
 * arena nothing plays). -2: syllable missing; -3: too short for articulation; -4: dst too short. */

int dx_speech_sing_into(int slot,const char *text,unsigned syllable,const dx_sing_note_t *notes,unsigned count,unsigned expression,int16_t *dst,unsigned capacity);
int dx_speech_make_into(int slot,const char *text,int pitch,int16_t *dst,unsigned capacity);

int dx_speech_scan(const char *text);
/* Minimum compact articulation in milliseconds, per pronounced syllable.
 * Measurement only: no PCM is written. */
int dx_speech_articulation(const char *text,uint16_t minimum[64]);

void dx_speech_sound(const uint8_t sound[4]); /* next job captures immutable values */
int dx_speech_active(void); /* main-loop reentry must defer pronunciation requests */

int dx_speech_articulation_native(const char *text,uint16_t minimum[64]); /* ordinary uncompressed articulation */
