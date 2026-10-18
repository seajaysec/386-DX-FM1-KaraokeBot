/* SPDX-License-Identifier: GPL-3.0-only */
/* A 386DX song: lyric lines with their tunes, the rows that place those lines in time, and a backing
 * of three tracks (Keys, Bass, Drums) played from sixteen one-bar patterns chained bar by bar.
 *
 *   line   a lyric line (up to 63 characters) and its notes: one note per sung syllable, or more
 *          for a held vowel (a tie). Note times count sixteenths from the line's own start.
 *   row    "line N starts at bar B": the song sings line N there. Several rows may name the same
 *          line: they share its words and tune (a chorus written once, sung four times).
 *   bar    each bar plays one pattern: a step grid for Keys, Bass and Drums.
 *
 * Rows are kept in time order. A row sings until the next row starts (or the song ends).
 * The whole song is one flat structure, saved as it is in RAM (song_t is the stored format). */
#pragma once
#include <stdint.h>

#define SONG_MAGIC 0x37535844u        /* "DXS7" */
#define SONG_VERSION 7
#define SONG_LINES 64
#define SONG_TEXT 64                  /* 63 characters and the terminator */
#define SONG_NOTES 512
#define SONG_ROWS 96
#define SONG_PATTERNS 16
#define SONG_BARS 128
#define SONG_STEPS 16                 /* a pattern's steps; 3/4 songs use the first 12 */
#define SONG_SLOTS 8                  /* user songs in flash */
#define SONG_SYLLABLES 64             /* the most syllables a line may have */
#define NO_LINE 255
#define SOUND_KEYS_DEFAULT 24         /* the synth's presets: the first electric piano */
#define SOUND_BASS_DEFAULT 8          /* the first stacked bass */

enum { T_VOICE, T_KEYS, T_BASS, T_DRUMS, NTRACKS };
#define NBACKING 3                    /* Keys, Bass, Drums: the pattern's tracks (T_KEYS - 1 ...) */

/* the voice's settings, song_t.voice[]: 0..100 unless said */
enum { V_BODY, V_BREATH, V_EDGE, V_GLIDE, V_VIBRATO, V_RATE, V_OCTAVE /* 0..2, 1 = as written */, V_LEVEL, NVOICE };
/* the effects, song_t.fx[]: 0..100 */
enum { FX_REVERB_SIZE, FX_REVERB_TONE, FX_DELAY_TIME, FX_DELAY_FEEDBACK, FX_DRIVE, FX_SQUEEZE, FX_PUMP, FX_FILTER, NFX };

typedef struct {
    uint16_t at;                      /* sixteenths from the line's start */
    uint8_t length;                   /* sixteenths, 1..255 */
    uint8_t pitch;                    /* MIDI note, 36..84 */
    uint8_t syllable;                 /* which syllable of the line's words, 0..63 */
    uint8_t tie;                      /* 1: the previous note's vowel goes on at this pitch */
} note_t;

typedef struct {
    char text[SONG_TEXT];
    uint16_t first;                   /* its notes: note[first .. first + count) */
    uint8_t count;
    uint8_t spare;
} line_t;

typedef struct {
    uint16_t at;                      /* sixteenths from the song's start */
    uint8_t line;                     /* 0..63 */
    uint8_t spare;
} row_t;

typedef struct {
    uint16_t drums;                   /* a bit per drum sound (Drums) */
    uint8_t note[4];                  /* Keys up to four, Bass one */
    uint8_t count;                    /* notes used; 0 = rest */
    uint8_t velocity;                 /* 1..127 */
    uint8_t length;                   /* sixteenths, 1..16 */
    uint8_t spare;
} step_t;

typedef struct { step_t step[NBACKING][SONG_STEPS]; } pattern_t;

#define PARAM_PRESET 255              /* a sound knob left as its preset has it */
#define NPARAM 8                      /* a sound's knobs: the engine's four, then cutoff, resonance, attack, release
                                       * (Drums: tune, length, drive, tone, then four spare) */
typedef struct {
    uint8_t sound;                    /* the sound preset (Keys, Bass, Drums) */
    uint8_t level;                    /* 0..100 */
    uint8_t mute;
    uint8_t reverb, delay;            /* sends, 0..100 */
    uint8_t param[NPARAM];            /* 0..127, or PARAM_PRESET */
    uint8_t spare[3];
} track_t;

typedef struct {
    uint32_t magic;
    uint16_t version, bpm;
    char name[24];
    uint8_t steps;                    /* 16 (4/4) or 12 (3/4) */
    uint8_t bars;                     /* 1..128 */
    uint8_t rows;                     /* rows used */
    uint8_t spare0;
    uint16_t notes;                   /* notes used, across every line */
    uint16_t spare1;
    track_t track[NTRACKS];
    uint8_t voice[NVOICE];
    uint8_t fx[NFX];
    uint8_t spare2[16];
    line_t line[SONG_LINES];
    note_t note[SONG_NOTES];
    row_t row[SONG_ROWS];
    uint8_t chain[SONG_BARS];         /* the pattern each bar plays */
    pattern_t pattern[SONG_PATTERNS];
} song_t;

/* the stored layout: web/song7.mjs writes the same bytes */
_Static_assert(sizeof(song_t) == 15752u, "song_t's layout is web/song7.mjs's");
_Static_assert(sizeof(song_t) <= 16128u, "a song fits one flash slot");
_Static_assert(__builtin_offsetof(song_t, line) == 136 && __builtin_offsetof(song_t, note) == 4488 &&
               __builtin_offsetof(song_t, row) == 7560 && __builtin_offsetof(song_t, chain) == 7944 &&
               __builtin_offsetof(song_t, pattern) == 8072, "song_t's layout is web/song7.mjs's");

extern song_t song;                   /* the song being played and edited */

/* a new song: 8 bars of 4/4 at 100 BPM, one pattern with a beat, no lines */
void song_blank(song_t *s);
/* 1 if every field is in range and the notes, lines and rows agree; loaded songs are checked first */
int song_valid(const song_t *s);

/* ---- time ---- */
unsigned song_ticks(const song_t *s);                     /* the song's length in sixteenths */
unsigned line_span(const song_t *s, unsigned line);      /* sixteenths from the line's start to its last note's end */
unsigned row_end(const song_t *s, unsigned row);         /* where row r stops singing: the next row, or the end */
/* the row singing at tick t (the last row starting at or before it, if it still sings there), or -1 */
int row_at(const song_t *s, unsigned tick);

/* ---- lines and their notes (stopped only: the renderer reads them while playing) ---- */
int line_rows(const song_t *s, unsigned line);           /* how many rows sing this line */
int line_unused(const song_t *s);                         /* a line no row uses and with no words or notes, or -1 */
int line_set_text(song_t *s, unsigned line, const char *text);   /* keeps the notes */
int note_insert(song_t *s, unsigned line, unsigned index, const note_t *n);   /* index within the line */
int note_delete(song_t *s, unsigned line, unsigned index);
void line_clear_notes(song_t *s, unsigned line);
int line_copy(song_t *s, unsigned from);                  /* a new line with the same words and tune, or -1 */

/* ---- rows ---- */
int row_insert(song_t *s, unsigned at, unsigned line);   /* returns the new row's index, or -1 when full */
int row_delete(song_t *s, unsigned row);
int row_move(song_t *s, unsigned row, int by);            /* swap its line with a neighbour's (times stay) */
int row_set_at(song_t *s, unsigned row, unsigned at);     /* keeps rows in order: refused past a neighbour */

/* the next free start after the last row: the bar after its line ends */
unsigned row_next_at(const song_t *s);

/* syllable groups: a note and the tied notes after it, sung as one prepared sound */
unsigned group_end(const song_t *s, unsigned note);       /* one past the group's last note */
