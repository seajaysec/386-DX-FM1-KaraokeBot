/* SPDX-License-Identifier: GPL-3.0-only */
/* Sung clips: each syllable group of the song (a note and its tied notes) is pronounced ahead of time
 * by the speech engine into a shared PCM arena at 11025 Hz, and the renderer plays it on its note.
 * The main loop prepares what is coming next; the renderer only reads clips that are published.
 *
 * Two kinds of clip:
 *   scored  CLIP_SCORED | note index of the group's first note: the group at its written pitches
 *           and lengths. Long groups (over 4.5 s or 16 notes) are one held vowel the renderer loops.
 *   live    CLIP_LIVE | line << 6 | syllable: one syllable at middle C, held, for the Voice keys.
 *           The renderer plays it at the key's pitch and loops its vowel while the key is down. */
#pragma once
#include <stdint.h>

#define CLIPS 8
#define CLIP_NONE 0xFFFFu
#define CLIP_SCORED 0x0000u
#define CLIP_LIVE 0x8000u
#define CLIP_RATE 11025u
#define LIVE_PITCH 60u
#define CLIP_AH 0x7FFFu               /* "ah": the Voice keys on Sing sing pitches, no words */
#define clip_live_id(line, syl) ((uint16_t)(CLIP_LIVE | ((line) & 63u) << 6 | ((syl) & 63u)))

typedef struct {
    volatile uint16_t id;             /* CLIP_NONE: free; written last when published */
    uint16_t word_at, word_len;       /* the word the clip sings, in its line's text (for the screen) */
    uint8_t source_pitch;             /* the pitch the clip was pronounced at (loop playback repitches it) */
    uint8_t looped;                   /* 1: play the vowel loop for the held length (long groups, live) */
    uint32_t signature;               /* the voice settings it was made with */
    uint32_t offset, size;            /* its span of the arena, samples */
    uint32_t length;                  /* samples written */
    uint32_t loop_start, loop_end;    /* the vowel loop, samples */
} clip_t;

extern clip_t clips[CLIPS];
extern int16_t *const clip_arena;
extern volatile int8_t vocal_locked;  /* the clip the renderer is playing, -1 none (renderer writes) */
extern volatile uint32_t vocal_misses;/* notes that found no clip */
extern uint8_t vocal_preparing;       /* the speech engine is busy for the vocal manager */

void vocal_reset(void);               /* forget every clip (the song or its words changed) */
uint32_t vocal_signature(void);       /* the current voice settings, as a clip's signature */
/* the clip for an id: one made with today's settings first, else an older one, -1 none (renderer) */
int vocal_find(uint16_t id);

/* what to prepare while stopped: the row being auditioned (-1 none) and the line the Voice keys sing
 * from (its syllables, starting at `syllable`) */
/* the "ah" clip kept ready (the Voice keys on Sing) */
void vocal_ah(int on);
void vocal_stopped_focus(int row, int line, unsigned syllable);
/* prepare as if playing from `tick` (PLAY is about to start there); -1: back to the playhead */
void vocal_cue(int tick);
/* main loop: prepare the next missing clip (all: every clip that fits). 1 made one, 0 nothing to do,
 * -1 the speech engine failed */
int vocal_prepare(int all);
/* how many syllables the line's words have (the speech engine counts), -1 if it can't pronounce them;
 * fills offsets[] with each syllable's character offset when given */
int vocal_syllables(const char *text, uint8_t offsets[64]);
