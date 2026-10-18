/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#define DX_SING_NOTES 16
typedef struct { uint32_t samples; uint8_t note; } dx_sing_note_t;
/* One pronunciation owner. A syllable can span several contiguous notes. */
extern int dx_sing_style,dx_sing_rate;
extern int dx_sing_active, dx_sing_measure, dx_sing_adjust, dx_sing_body;
extern unsigned dx_sing_samples;
extern unsigned char *dx_sing_buffer;
void dx_sing_select(void);
int dx_sing_f0(void);
int dx_sing_begin(unsigned syllable,const dx_sing_note_t *notes,unsigned count,unsigned expression);
void dx_sing_end(void);
unsigned dx_sing_count(void);
void dx_sing_word(char *dst,unsigned size);

unsigned dx_sing_word_first(void);
unsigned dx_sing_word_last(void);

unsigned dx_sing_word_offset(void);

unsigned dx_sing_offset(unsigned syllable);

extern unsigned dx_sing_body_size,dx_sing_breath,dx_sing_edge,dx_sing_glide; /* immutable pronunciation-job controls */

extern unsigned dx_sing_compact; /* 100 normal; native command durations only */
