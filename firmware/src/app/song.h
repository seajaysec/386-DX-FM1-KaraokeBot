/* SPDX-License-Identifier: GPL-3.0-only */
/* A 386DX song. Every track (Voice, Keys, Bass, Drums) is a chain of phrases: each phrase is 1 or more bars,
 * an empty one a rest, and a chain shorter than the song plays round again.
 *
 *   lane   a track's chain, a byte a bar: bit 7 marks the bar a phrase starts on; for Keys, Bass and Drums
 *          the low five bits are the bar's material (one of the sixteen patterns' step rows for that track),
 *          31 a bar of rest. lane_bars says how long each chain is.
 *   line   a lyric line (up to 63 characters) and its notes: one note per sung syllable, or more for a held
 *          vowel (a tie). Note times count sixteenths from the line's own start.
 *   row    "line N starts here" (in sixteenths): the Voice's phrases hold rows, each at its exact place; a
 *          row belongs to the phrase its first half-bar falls in, so a pickup sung before the bar line
 *          goes with the phrase after it. Several rows may name the same line: a chorus written once.
 *
 * Rows are kept in time order. A row sings until the next row starts (or its chain ends).
 * The whole song is one flat structure, saved as it is in RAM (song_t is the stored format; web/song8.mjs
 * writes the same bytes, and turns 1.0 songs, version 7, into this one). */
#pragma once
#include <stdint.h>

#define SONG_MAGIC 0x37535844u        /* "DXS7" (the magic stays; the version says which layout) */
#define SONG_VERSION 8
#define SONG_VERSION_OLD 7            /* 1.0's songs: listed, not loaded (the companion page updates them) */
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
#define LANE_START 0x80u              /* lane byte: a phrase starts on this bar */
#define LANE_REST 31u                 /* lane byte's material: a bar of rest */
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
    uint8_t tie;                      /* bit 0: the previous note's vowel goes on at this pitch; bits 1..3: the
                                       * nudge, -3..+3 sixths of a sixteenth (two's complement) */
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

#define DRUM_ALT 0x8000u              /* hats and cymbal on this step: every other time the bar comes round */
#define DRUM_SOUNDS 0x07ffu           /* the eleven sounds; DRUM_ALT sits above them */
typedef struct {
    uint16_t drums;                   /* a bit per drum sound (Drums), and DRUM_ALT */
    uint8_t note[4];                  /* Keys up to four, Bass one */
    uint8_t count;                    /* notes used; 0 = rest */
    uint8_t velocity;                 /* 1..127 */
    uint8_t length;                   /* sixteenths, 1..16 */
    int8_t nudge;                     /* -3..+3 sixths of a sixteenth, early or late */
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
    uint8_t arp[3];                   /* Keys' and Bass's arpeggiator: [0] its type | octaves - 1 << 4, [1] its rate,
                                       * [2] its gate (percent of the rate, 5..100; 0 is 50) */
} track_t;
enum { ARP_OFF, ARP_UP, ARP_DOWN, ARP_BOUNCE, ARP_RANDOM, ARP_PLAYED, NARP };
#define NARP_RATE 6                   /* 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 */

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
    uint8_t scale, root;              /* GLO's SCALE (0 chromatic) and ROOT (0 C .. 11 B): what the keys play */
    uint8_t lane_bars[NTRACKS];       /* each track's chain, in bars (1..128): it plays round again after */
    line_t line[SONG_LINES];
    note_t note[SONG_NOTES];
    row_t row[SONG_ROWS];
    uint8_t lane[NTRACKS][SONG_BARS]; /* each track's chain, a byte a bar (above) */
    pattern_t pattern[SONG_PATTERNS];
} song_t;

/* the stored layout: web/song8.mjs writes the same bytes */
_Static_assert(sizeof(song_t) == 16128u, "song_t's layout is web/song8.mjs's");
_Static_assert(sizeof(song_t) <= 16128u, "a song fits one flash slot");
_Static_assert(__builtin_offsetof(song_t, line) == 126 && __builtin_offsetof(song_t, note) == 4478 &&
               __builtin_offsetof(song_t, row) == 7550 && __builtin_offsetof(song_t, lane) == 7934 &&
               __builtin_offsetof(song_t, pattern) == 8446, "song_t's layout is web/song8.mjs's");

extern song_t song;                   /* the song being played and edited */

/* a new song: 8 bars of 4/4 at 100 BPM, one pattern with a beat, no lines */
void song_blank(song_t *s);
void song_default_knobs(song_t *s);   /* every knob back to a new song's */
/* 1 if every field is in range and the notes, lines and rows agree; loaded songs are checked first */
int song_valid(const song_t *s);
/* the Voice's QUALITY (voice[V_OCTAVE]), its register: stored 0 Baritone, 1 Tenor, 2 Alto (as songs always held it),
 * 3 Profondo, 4 Soprano; the semitones it moves the singing */
static inline int voice_register(const song_t *s)
{
    static const int8_t R[5] = {-12, 0, 12, -24, 24};
    return R[s->voice[V_OCTAVE] % 5u];
}

/* ---- time ---- */
unsigned song_ticks(const song_t *s);                     /* the song's length in sixteenths */
unsigned line_span(const song_t *s, unsigned line);      /* sixteenths from the line's start to its last note's end */
unsigned row_end(const song_t *s, unsigned row);         /* where row r stops singing: the next row, or its chain's end */
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
static inline int note_tie(const note_t *n) { return n->tie & 1; }
static inline int note_nudge(const note_t *n) { return (int)(int8_t)(uint8_t)(n->tie << 4) >> 5; }

/* ---- the chains (stopped only, as the lines): track t's phrases ---- */
unsigned lane_count(const song_t *s, unsigned t);                     /* its phrases */
unsigned lane_start(const song_t *s, unsigned t, unsigned i);         /* phrase i's first bar */
unsigned lane_len(const song_t *s, unsigned t, unsigned i);           /* phrase i's bars */
unsigned lane_find(const song_t *s, unsigned t, unsigned bar);        /* the phrase holding that bar of the chain */
/* where track t's chain is at song tick `tick` (the chain plays round again) */
static inline unsigned lane_tick(const song_t *s, unsigned t, unsigned tick) { return tick % (s->lane_bars[t] * s->steps); }
/* the pattern step row track t (Keys, Bass, Drums) plays at song tick `tick`, or 0 for a rest */
static inline const step_t *lane_steps(const song_t *s, unsigned t, unsigned tick)
{
    unsigned m = s->lane[t][lane_tick(s, t, tick) / s->steps] & 31u;
    return m < SONG_PATTERNS ? s->pattern[m].step[t - 1] : 0;
}
static inline unsigned row_bar(const song_t *s, unsigned r) { return (s->row[r].at + s->steps / 2u) / s->steps; }
/* each returns 0, or -1 when there is no room (128 bars, 96 rows) or nothing to do */
int lane_insert(song_t *s, unsigned t, unsigned i, unsigned bars);    /* an empty phrase before phrase i (i = count: at the end) */
int lane_delete(song_t *s, unsigned t, unsigned i);
int lane_dupe(song_t *s, unsigned t, unsigned i);                      /* the same phrase again after it (its bars or lines shared) */
int lane_swap(song_t *s, unsigned t, unsigned i);                      /* phrase i and the one after trade places */
int lane_resize(song_t *s, unsigned t, unsigned i, unsigned bars);    /* bars added (rest) or taken from its end */
int lane_slot_free(song_t *s, unsigned t);                             /* a backing track's unused bar of material, cleared; -1 none */
