/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio engine: the transport, the backing's patterns, the instruments, the sung clips, the
 * effects and the mix. engine_render runs in the audio interrupt; every other call is the main loop's
 * and reaches the renderer through a command queue, so the two never write the same state.
 * The renderer reads the song (song.h) while playing; the main loop edits it only when stopped. */
#pragma once
#include <stdint.h>

#define RATE 44100u
#define PUNCH_N 16                    /* punch-in effects: one a white key */

/* ---- read by the main loop (written by the renderer) ---- */
extern volatile uint8_t eng_playing;
extern volatile uint16_t eng_tick;    /* the sixteenth playing now */
extern volatile uint32_t eng_queue_drops;
/* the voice, for the face and the lyric on screen */
extern volatile uint8_t eng_sing_on;          /* a clip or a guide is sounding */
extern volatile uint8_t eng_sing_level;       /* 0..127, the sung loudness (fast up, slow down) */
extern volatile uint8_t eng_sing_open;        /* 0..127, how bright the sound is now (the mouth's opening) */
extern volatile float eng_sing_pitch;         /* the sung pitch now, semitones (vibrato and glides included) */
extern volatile float eng_sing_vibrato;       /* the vibrato's offset now, semitones */
extern volatile int16_t eng_sing_row;         /* the row being sung, -1 none */
extern volatile int16_t eng_sing_note;        /* the note index being sung (scored), -1 none */
extern volatile int8_t eng_sing_clip;         /* the clip playing, -1 none */
extern volatile uint8_t eng_meter[4];         /* each track's level, 0..127 */
extern volatile uint16_t eng_drum_hits;       /* drum lanes struck since the main loop last cleared it */
extern volatile uint32_t eng_punch;           /* the punch-in effects held, a bit each */

void engine_init(void);
/* the song was loaded or rebuilt: every sound from its tracks (stopped) */
void engine_song(void);
/* a track's sound or knob changed: rebuild its instrument parameters */
void engine_sound(unsigned track);

void engine_play(int on, unsigned from_tick);
void engine_tempo(void);                      /* song.bpm changed */

/* live notes: Keys, Bass (track), Drums (note = drum lane 0..10) */
void engine_note(unsigned track, unsigned note, unsigned velocity);
void engine_off(unsigned track, unsigned note);
/* the Voice keys: sing clip `id` (vocal.h) at `note` while held, or a hummed guide if it isn't ready */
void engine_sing(unsigned note, unsigned velocity, uint16_t id);
void engine_sing_off(unsigned note);
/* hum `note` for `ms` (a pitch guide while entering notes) */
void engine_guide(unsigned note, unsigned ms);
/* punch-in effects: k 0..15 held or let go */
void engine_punch(unsigned k, int on);
void engine_tap(void);                        /* the click while recording (one tick of the metronome) */
/* the overload guard (main loop): fewer synth voices while the audio's load is over 85 % */
void engine_guard(unsigned load_pct, uint32_t now_ms);
extern volatile uint8_t eng_guard;

int engine_clip_queued(unsigned clip);        /* a queued command still names this clip */
/* one block, n <= 256 frames, interleaved stereo int32 (24-bit) */
void engine_render(int32_t *out, uint32_t n);
