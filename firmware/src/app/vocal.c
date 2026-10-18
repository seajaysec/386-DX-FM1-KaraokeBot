/* SPDX-License-Identifier: GPL-3.0-only */
/* The sung clips' manager: which groups are wanted next, where their PCM goes, and pronouncing them.
 * Main loop only, except vocal_find (the renderer). A clip is published by writing its id last; it is
 * taken back by writing CLIP_NONE first, and its PCM is reused only when the renderer is not playing it
 * and has no command queued for it. */
#include "vocal.h"
#include "song.h"
#include "speech.h"
#include "engine.h"
#include "../dsp/fastmath.h"
#include <string.h>

#define BARRIER() __asm__ volatile("" ::: "memory")
#define ARENA (sizeof dx_speech_cache.samples / sizeof dx_speech_cache.samples[0])
#define LONGEST DX_SPEECH_LONG_SAMPLES          /* one clip's most samples (4.5 s) */
#define LIVE_SAMPLES (22050u * 7u / 10u)        /* a live syllable: 0.7 s pronounced at 22050 Hz */

clip_t clips[CLIPS];
int16_t *const clip_arena = dx_speech_cache.samples;
volatile int8_t vocal_locked = -1;
volatile uint32_t vocal_misses;
uint8_t vocal_preparing;

static uint16_t wanted[CLIPS];
static unsigned nwanted;
static int focus_row = -1, focus_line = -1;
static unsigned focus_syllable;
static int cue = -1;
static uint16_t failed_id = CLIP_NONE;
static uint32_t failed_sig;

void vocal_reset(void)
{
    for (unsigned i = 0; i < CLIPS; i++) {
        clips[i].id = CLIP_NONE;
        BARRIER();
        if (vocal_locked != (int)i)
            clips[i].size = 0;
    }
    failed_id = CLIP_NONE;
}

uint32_t vocal_signature(void)
{
    const uint8_t *v = song.voice;
    return (uint32_t)v[V_BODY] | (uint32_t)v[V_BREATH] << 7 | (uint32_t)v[V_EDGE] << 14 | (uint32_t)v[V_GLIDE] << 21 |
           (uint32_t)v[V_OCTAVE] << 28 | (uint32_t)(song.bpm & 3u) << 30;
}

int vocal_find(uint16_t id)
{
    uint32_t sig = vocal_signature();
    int stale = -1;
    for (unsigned i = 0; i < CLIPS; i++)
        if (clips[i].id == id) {
            if (clips[i].signature == sig)
                return (int)i;
            stale = (int)i;
        }
    return stale;
}

static int fresh(uint16_t id)
{
    uint32_t sig = vocal_signature();
    for (unsigned i = 0; i < CLIPS; i++)
        if (clips[i].id == id && clips[i].signature == sig)
            return 1;
    return 0;
}

static void want(uint16_t id, unsigned limit)
{
    if (nwanted >= limit)
        return;
    for (unsigned k = 0; k < nwanted; k++)
        if (wanted[k] == id)
            return;
    wanted[nwanted++] = id;
}

/* the groups a row sings from tick `from` on (in time order) */
static void want_row(unsigned r, unsigned from, unsigned limit)
{
    const row_t *R = &song.row[r];
    const line_t *L = &song.line[R->line];
    unsigned end = row_end(&song, r);
    if (!L->text[0])
        return;
    for (unsigned i = 0; i < L->count && nwanted < limit; i++) {
        const note_t *n = &song.note[L->first + i];
        unsigned at = R->at + n->at;
        if (n->tie || at < from || at >= end)
            continue;
        want((uint16_t)(CLIP_SCORED | (L->first + i)), limit);
    }
}

void vocal_cue(int tick) { cue = tick; }

#ifdef DX_HOST
__attribute__((weak)) void host_busy(unsigned ms) { (void)ms; }   /* the simulator makes the main loop wait; the browser engine does not */
#endif
static int ah_wanted;
void vocal_ah(int on) { ah_wanted = on; }

void vocal_stopped_focus(int row, int line, unsigned syllable)
{
    focus_row = row;
    focus_line = line;
    focus_syllable = syllable;
}

/* the wanted list, nearest first. Playing: the rows from the playhead on, round to the start again.
 * Stopped: the focused row's groups, and the next few syllables the Voice keys will sing. */
static void collect(void)
{
    nwanted = 0;
    if (ah_wanted)
        want(CLIP_AH, CLIPS);
    unsigned live = focus_line >= 0 && song.line[focus_line].text[0] ? 3u : 0u;
    if (eng_playing || cue >= 0) {
        unsigned tick = cue >= 0 ? (unsigned)cue : eng_tick, limit = CLIPS - 1u - (live ? 1u : 0u);
        int r = row_at(&song, tick);
        for (unsigned pass = 0; pass < 2 && nwanted < limit; pass++) {
            unsigned start = pass ? 0 : (r < 0 ? 0 : (unsigned)r);
            for (unsigned k = start; k < song.rows && nwanted < limit; k++)
                want_row(k, pass ? 0 : tick, limit);
        }
        if (live)
            want(clip_live_id(focus_line, focus_syllable), CLIPS - 1u);
        return;
    }
    if (focus_row >= 0 && focus_row < song.rows)
        want_row((unsigned)focus_row, 0, CLIPS - 1u - live);
    for (unsigned k = 0; k < live; k++)
        want(clip_live_id(focus_line, focus_syllable + k), CLIPS - 1u);
}

static int wanted_rank(uint16_t id)
{
    for (unsigned k = 0; k < nwanted; k++)
        if (wanted[k] == id)
            return (int)k;
    return -1;
}

static int busy(unsigned i) { return vocal_locked == (int)i || engine_clip_queued(i); }

static void release(unsigned i)
{
    clips[i].id = CLIP_NONE;
    BARRIER();
    if (!busy(i))
        clips[i].size = 0;
}

/* a free descriptor with `need` contiguous samples, -1 none now. Clips stay until their room is needed
 * (a song played again finds them); then what is not wanted goes first, then what is wanted latest,
 * never one wanted sooner than this one. */
static int allocate(unsigned need, int rank)
{
    uint32_t sig = vocal_signature();
    for (unsigned i = 0; i < CLIPS; i++)
        if (clips[i].size && !busy(i) && clips[i].id != CLIP_NONE && clips[i].signature != sig && fresh(clips[i].id))
            release(i);                         /* a stale copy, once a fresh one is there */
    for (unsigned attempt = 0; attempt <= CLIPS; attempt++) {
        int slot = -1;
        for (unsigned i = 0; i < CLIPS; i++)
            if (!clips[i].size && !busy(i)) {
                slot = (int)i;
                break;
            }
        if (slot >= 0) {
            unsigned at = 0;
            while (at + need <= ARENA) {
                unsigned next = at;
                for (unsigned i = 0; i < CLIPS; i++)
                    if (clips[i].size && at < clips[i].offset + clips[i].size && at + need > clips[i].offset &&
                        clips[i].offset + clips[i].size > next)
                        next = clips[i].offset + clips[i].size;
                if (next == at) {
                    clips[slot].offset = at;
                    clips[slot].size = need;
                    return slot;
                }
                at = next;
            }
        }
        int victim = -1, far = rank;
        for (unsigned i = 0; i < CLIPS; i++) {
            if (!clips[i].size || busy(i))
                continue;
            int r = clips[i].id == CLIP_NONE ? -1 : wanted_rank(clips[i].id);
            if (r < 0) {
                victim = (int)i;               /* not wanted at all: first to go */
                break;
            }
            if (r > far) {
                far = r;
                victim = (int)i;
            }
        }
        if (victim < 0)
            return -1;
        release((unsigned)victim);
    }
    return -1;
}

/* the vowel loop: where the clip is loudest in its middle half, as long as eight periods at the source
 * pitch, the length then nudged to where the waveform repeats best (the voice's own period wanders a
 * little), so the seam lands on a matching cycle */
static void find_loop(clip_t *c)
{
    const int16_t *pcm = clip_arena + c->offset;
    float period = (float)CLIP_RATE / (440.0f * fm_exp2f(((float)c->source_pitch - 69.0f) / 12.0f));
    unsigned span = (unsigned)(period * 8.0f + 0.5f), size = c->length, best = size / 2, energy = 0;
    if (span * 2 + 16 > size) {
        c->loop_start = size / 4;
        c->loop_end = size * 3 / 4;
        return;
    }
    for (unsigned at = size / 4; at + span + 8 < size * 3 / 4; at += span / 4) {
        unsigned e = 0;
        for (unsigned j = 0; j < span; j += 4) {
            int x = pcm[at + j];
            e += (unsigned)(x < 0 ? -x : x);
        }
        if (e > energy) {
            energy = e;
            best = at;
        }
    }
    /* the length that repeats best: correlate the start with each candidate end */
    unsigned length = span;
    float score = -1e30f;
    unsigned n = span < 128 ? span : 128;
    for (int d = -4; d <= 4; d++) {
        unsigned L = (unsigned)((int)span + d);
        if (best + L + n >= size)
            continue;
        float xy = 0, xx = 1, yy = 1;
        for (unsigned j = 0; j < n; j++) {
            float x = pcm[best + j], y = pcm[best + L + j];
            xy += x * y;
            xx += x * x;
            yy += y * y;
        }
        float r = xy / fm_sqrtf(xx * yy);
        if (r > score) {
            score = r;
            length = L;
        }
    }
    c->loop_start = best;
    c->loop_end = best + length;
}

static void word_of(clip_t *c, const char *text)
{
    char w[SONG_TEXT];
    memcpy(w, text, SONG_TEXT);
    dx_sing_word(w, sizeof w);
    c->word_at = (uint16_t)dx_sing_word_offset();
    c->word_len = (uint16_t)strlen(w);
}

static int make(uint16_t id, int rank)
{
    uint32_t sig = vocal_signature();
    if (failed_id == id && failed_sig == sig)
        return 0;
    dx_sing_note_t notes[DX_SING_NOTES];
    unsigned count = 0, native = 0, syllable, looped = 0, pitch_high = 0;
    const char *text;
    int octave = ((int)song.voice[V_OCTAVE] - 1) * 12;
    if (id == CLIP_AH || (id & CLIP_LIVE)) {
        unsigned line = (id >> 6) & 63u;
        static const char AH[SONG_TEXT] = "ah";   /* a whole line's room: word_of copies that much */
        text = id == CLIP_AH ? AH : song.line[line].text;
        syllable = id == CLIP_AH ? 0u : id & 63u;
        notes[0].samples = LIVE_SAMPLES;
        notes[0].note = LIVE_PITCH;
        count = 1;
        native = LIVE_SAMPLES;
        looped = 1;
        pitch_high = LIVE_PITCH;
    } else {
        unsigned head = id, end = group_end(&song, head), line = 0;
        for (unsigned l = 0; l < SONG_LINES; l++)
            if (head >= song.line[l].first && head < song.line[l].first + song.line[l].count)
                line = l;
        text = song.line[line].text;
        syllable = song.note[head].syllable;
        for (unsigned i = head; i < end; i++) {
            int p = (int)song.note[i].pitch + octave;
            unsigned pitch = p < 24 ? 24u : p > 96 ? 96u : (unsigned)p;
            uint32_t samples = 22050u * 60u * song.note[i].length / (song.bpm * 4u);
            if (count < DX_SING_NOTES) {
                notes[count].samples = samples;
                notes[count].note = (uint8_t)pitch;
            }
            count++;
            native += samples;
            if (pitch > pitch_high)
                pitch_high = pitch;
        }
        if (count > DX_SING_NOTES || native / 2u > LONGEST) {
            /* too long to pronounce whole: one second of the vowel at its highest pitch, looped */
            notes[0].samples = 44100u;
            notes[0].note = (uint8_t)pitch_high;
            count = 1;
            native = 44100u;
            looped = 1;
        }
    }
    unsigned need = native / 2u + 64u;
    if (need > LONGEST)
        need = LONGEST;
    int slot = allocate(need, rank);
    if (slot < 0)
        return 0;                       /* full of nearer clips: try again when one is played */
    clip_t *c = &clips[slot];
    dx_speech_sound(&song.voice[V_BODY]);
    vocal_preparing = 1;
    /* every clip is pronounced to its full length (a live syllable's vowel lasts the 0.7 s, so its loop
     * sits in the vowel): style 0, the speech engine's exact-duration singing */
    int rc = dx_speech_sing_into(slot, text, syllable, notes, count, 0u, clip_arena + c->offset, need);
    vocal_preparing = 0;
    if (rc) {
        failed_id = id;
        failed_sig = sig;
        c->size = 0;
        return rc == -1 ? -1 : 0;
    }
#ifdef DX_HOST
    host_busy(200);                               /* on the FM-1 a sung clip takes this long and the main loop waits */
#endif
    c->length = dx_speech_length[slot];
    c->source_pitch = (uint8_t)notes[0].note;
    c->looped = (uint8_t)looped;
    c->signature = sig;
    word_of(c, text);
    if (looped)
        find_loop(c);
    BARRIER();
    c->id = id;
    return 1;
}

int vocal_prepare(int all)
{
    if (vocal_preparing || dx_speech_active())
        return 0;
    collect();
    int made = 0;
    for (unsigned k = 0; k < nwanted; k++) {
        if (fresh(wanted[k]))
            continue;
        int rc = make(wanted[k], (int)k);
        if (rc < 0)
            return -1;
        if (!rc)
            break;                      /* keep the room for the earliest missing clip */
        made++;
        if (!all)
            break;
    }
    return made;
}

int vocal_syllables(const char *text, uint8_t offsets[64])
{
    if (!text[0])
        return 0;
    int n = dx_speech_scan(text);
    if (n > 0 && offsets)
        for (unsigned i = 0; i < 64; i++)
            offsets[i] = (uint8_t)dx_sing_offset(i);
    return n > 64 ? -1 : n;
}
