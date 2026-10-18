/* SPDX-License-Identifier: GPL-3.0-only */
/* The audio engine (engine.h). Signal flow, a block at a time:
 *
 *   Keys, Bass  -> synth voices ----+-> melodic group --+
 *   Voice clips (and the hum guide) -+                   +-> master (EQ, saturator, compressor,
 *   Drums       -> drum kit -----------> drum group ----+    limiter) -> pump -> filter -> punch-in -> out
 *   every track's delay and reverb sends -> the two effect slots -> back into the melodic group
 *
 * The transport splits each block at the sixteenth boundaries, so a step's notes start on their sample.
 * The main loop talks to the renderer only through the command queue; the renderer reads the song and
 * the instruments' parameter arrays, which the main loop changes only between notes (a byte at a time)
 * or while stopped. */
#include "engine.h"
#include "song.h"
#include "vocal.h"
#include "punch.h"
#include "../dsp/fastmath.h"
#include "../dsp/synth.h"
#include "../dsp/drumkit.h"
#include "../dsp/fxunit.h"
#include <string.h>

#ifdef DX_HOST
#define POOL
#else
#define POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")
#define BLOCK 128u

/* ---- the instruments and effects, in memory of their own ---- */
static uint8_t synth_mem[SYNTH_BYTES] __attribute__((aligned(8)));
static uint8_t kit_mem[DRUMKIT_BYTES] POOL __attribute__((aligned(8)));
static uint8_t reverb_mem[FXSLOT_BYTES] POOL __attribute__((aligned(8)));
static uint8_t master_mem[MASTER_BYTES] __attribute__((aligned(8)));
static int16_t reverb_line[FX_RV_LEN] POOL;
#define SY ((synth_t *)(void *)synth_mem)
#define KIT ((drumkit_t *)(void *)kit_mem)
#define REVERB ((fxslot_t *)(void *)reverb_mem)
#define MASTER ((master_t *)(void *)master_mem)

/* ---- the tape echo: a quarter-rate line (one second), its repeats darker each time round, the
 * tape's wow moving the head a little; the left hears the head, the right a head a third later ---- */
#define ECHO_LEN 11025u
static int16_t echo_line[ECHO_LEN] POOL;
static struct {
    uint32_t w;
    uint8_t phase;
    float acc, lp, fb_lp, time, target, feedback, wow, out_l0, out_l1, out_r0, out_r1, quiet;
} echo;

static void echo_set(void)
{
    /* the time: a division of the beat (a sixteenth .. two beats), halved while it is over a second */
    static const float BEATS[6] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f};
    unsigned k = song.fx[FX_DELAY_TIME] * 6u / 101u;
    float t = BEATS[k] * 60.0f / (float)song.bpm;
    while (t > 0.95f)
        t *= 0.5f;
    echo.target = t * ((float)RATE / 4.0f);                   /* quarter-rate samples */
    echo.feedback = (float)song.fx[FX_DELAY_FEEDBACK] * 0.0088f;   /* up to 0.88 */
}

static void echo_process(const float *in, float *l, float *r, uint32_t n)
{
    float peak = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float x = in[i];
        peak += x < 0 ? -x : x;
        echo.lp += 0.2f * (x - echo.lp);                      /* a gentle low-pass before the decimation */
        echo.acc += echo.lp;
        if (++echo.phase == 4) {
            echo.phase = 0;
            echo.time += 0.0005f * (echo.target - echo.time); /* a change of time slides (a tape speeding up) */
            echo.wow += 0.0011f;
            if (echo.wow > FM_TWO_PI)
                echo.wow -= FM_TWO_PI;
            float d = echo.time + 3.0f * fm_sinf(echo.wow);
            float rd = (float)echo.w + (float)ECHO_LEN - d;
            uint32_t j = (uint32_t)rd;
            float f = rd - (float)j, a = echo_line[j % ECHO_LEN], b = echo_line[(j + 1) % ECHO_LEN];
            float tap = (a + (b - a) * f) * (1.0f / 16384.0f);
            float rr = (float)echo.w + (float)ECHO_LEN - d * 1.333f;
            if (rr < (float)echo.w)
                rr += (float)ECHO_LEN;
            uint32_t jr = (uint32_t)rr;
            float tapr = echo_line[jr % ECHO_LEN] * (1.0f / 16384.0f);
            echo.fb_lp += 0.45f * (tap - echo.fb_lp);         /* each repeat a little darker */
            float y = echo.acc * 0.25f + echo.feedback * echo.fb_lp;
            y = y / (1.0f + (y < 0 ? -y : y) * 0.5f);         /* the tape saturates: the loop stays bounded */
            echo_line[echo.w] = (int16_t)(y * 16384.0f);
            echo.w = (echo.w + 1u) % ECHO_LEN;
            echo.acc = 0.0f;
            echo.out_l0 = echo.out_l1;
            echo.out_l1 = tap;
            echo.out_r0 = echo.out_r1;
            echo.out_r1 = tapr;
        }
        float t = (float)echo.phase * 0.25f;
        l[i] += echo.out_l0 + (echo.out_l1 - echo.out_l0) * t;
        r[i] += echo.out_r0 + (echo.out_r1 - echo.out_r0) * t;
    }
    (void)peak;
}
static uint8_t par[NTRK][NTP];        /* the instruments' parameters: Keys, Bass, Drums (synth tracks 0..2) */

/* the work buffers of one block */
static float mel_l[BLOCK], mel_r[BLOCK], drum_l[BLOCK], drum_r[BLOCK];
static float send_dly[BLOCK], send_rev[BLOCK], send_tape[BLOCK], out_l[BLOCK], out_r[BLOCK];

/* ---- state the main loop reads ---- */
volatile uint8_t eng_playing;
volatile uint16_t eng_tick;
volatile uint32_t eng_queue_drops;
volatile uint8_t eng_sing_on, eng_sing_level, eng_sing_open;
volatile float eng_sing_pitch, eng_sing_vibrato;
volatile int16_t eng_sing_row = -1, eng_sing_note = -1;
volatile int8_t eng_sing_clip = -1;
volatile uint8_t eng_meter[4];
volatile uint16_t eng_drum_hits;
volatile uint32_t eng_punch;

/* ---- the command queue ---- */
enum { C_PLAY, C_NOTE, C_OFF, C_SING, C_SING_OFF, C_GUIDE, C_PUNCH, C_TAP, C_ALLOFF, C_TEMPO, C_VOICES };
typedef struct { uint8_t op, a, b, c; uint16_t d; uint32_t e; } cmd_t;
#define QSIZE 64u
static cmd_t queue[QSIZE];
static volatile uint32_t qr, qw;

static int send(const cmd_t *c)
{
    uint32_t w = qw;
    if (w - qr >= QSIZE) {
        eng_queue_drops++;
        return 0;
    }
    queue[w % QSIZE] = *c;
    BARRIER();
    qw = w + 1;
    return 1;
}

int engine_clip_queued(unsigned clip)
{
    for (uint32_t i = qr; i != qw; i++) {
        const cmd_t *c = &queue[i % QSIZE];
        if (c->op == C_SING && vocal_find(c->d) == (int)clip)
            return 1;
    }
    return 0;
}

/* ---- sounds: the instruments' parameters from the song's tracks ---- */
static int preset_named(const char *name)
{
    for (int i = 0; i < NPRESETS; i++) {
        const char *a = PRESETS[i].name, *b = name;
        while (*a && *a == *b)
            a++, b++;
        if (!*a && !*b)
            return i;
    }
    return 0;
}

/* the drum kits: the default kit's models, retuned a kit at a time (tune, length, punch / tone) */
typedef struct { const char *name; int8_t tune; uint8_t length, drive, tone; } kit_t;
static const kit_t KITS[] = {
    {"Studio", 0, 64, 0, 127},  {"Tight", 2, 40, 20, 110}, {"Boom", -5, 96, 40, 100},
    {"Dusty", -2, 70, 70, 70},  {"Bright", 4, 56, 10, 127}, {"Crunch", 0, 64, 110, 90},
};
#define NKITS (sizeof KITS / sizeof KITS[0])
static int8_t kit_tune;           /* semitones on every hit (the Drums' TUNE knob) */

unsigned engine_kits(void) { return NKITS; }
const char *engine_kit_name(unsigned k) { return KITS[k % NKITS].name; }

void engine_sound(unsigned t)
{
    if (t == T_VOICE || t >= NTRACKS)
        return;
    const track_t *k = &song.track[t];
    uint8_t *p = par[t - 1];
    if (t == T_DRUMS) {
        const kit_t *kit = &KITS[k->sound % NKITS];
        sound_default(p, EG_DRUMKIT);
        p[TP_R] = k->param[1] != PARAM_PRESET ? k->param[1] : kit->length;   /* the length scale, 64 = 1x */
        p[TP_FENV] = k->param[2] != PARAM_PRESET ? k->param[2] : kit->drive;
        p[TP_KTRK] = k->param[3] != PARAM_PRESET ? k->param[3] : kit->tone;
        if (k->param[4] != PARAM_PRESET)
            p[TP_A] = k->param[4];                    /* funk: the hits pushed late, a little each */
        if (k->param[5] != PARAM_PRESET)
            p[TP_D] = k->param[5];                    /* attack */
        if (k->param[6] != PARAM_PRESET)
            p[TP_S] = k->param[6];                    /* random: each hit a little different */
        p[TP_CUT] = 0;
        p[TP_RES] = 30;
        kit_tune = (int8_t)(kit->tune + (k->param[0] != PARAM_PRESET ? ((int)k->param[0] - 64) / 5 : 0));
    } else {
        unsigned n = k->sound < (unsigned)NPRESETS ? k->sound : 0;
        int engine = PRESETS[n].par[TP_ENGINE];
        sound_default(p, engine);
        memcpy(p, PRESETS[n].par, NTP_SOUND);
        static const uint8_t KNOB_TP[NPARAM] = {TP_E1, TP_E2, TP_E3, TP_E4, TP_CUT, TP_RES, TP_A, TP_R};
        for (unsigned j = 0; j < NPARAM; j++)
            if (k->param[j] != PARAM_PRESET)
                p[KNOB_TP[j]] = k->param[j];
    }
    p[TP_LEVEL] = 127;                /* the mix is the engine's: song.track[].level */
    p[TP_PAN] = 64;
    p[TP_SEND1] = (uint8_t)(k->delay * 127u / 100u);
    p[TP_SEND2] = (uint8_t)(k->reverb * 127u / 100u);
    p[TP_SENDT] = 0;
    p[TP_PLAYER] = PL_OFF;
    p[TP_CHAN] = 0;
    if (t == T_DRUMS) {
        cmd_t c = {C_ALLOFF, (uint8_t)t, 1, 0, 0, 0};   /* b = 1: the kit's filter changed */
        send(&c);
    }
}

static void effects_set(void)
{
    const uint8_t *f = song.fx;
    uint8_t rv[4] = {(uint8_t)(f[FX_REVERB_SIZE] * 127u / 100u), 30, (uint8_t)(f[FX_REVERB_TONE] * 127u / 100u), 0};
    fx_set(REVERB, FXT_REVERB, rv);
    fx_filter(REVERB, 15, 127);
    echo_set();
    master_set(MASTER, MS_SAT_GAIN, f[FX_DRIVE] * 127 / 100);
    master_set(MASTER, MS_SAT_BLEND, f[FX_DRIVE] ? 40 + f[FX_DRIVE] * 87 / 100 : 0);
    master_set(MASTER, MS_COMP, f[FX_SQUEEZE] * 127 / 100);
}

/* ---- the transport ---- */
static uint32_t clock_phase;          /* sample position inside the sixteenth, in RATE * 60 units */
static uint16_t next_tick;
static uint8_t started;
/* sequenced notes' ends: a note's tag and the samples left */
typedef struct { uint32_t tag, left; uint8_t trk, on; } off_t;
static off_t offs[24];

static uint32_t tick_samples(void) { return RATE * 60u / (song.bpm * 4u); }

static void schedule_off(unsigned trk, uint32_t tag, uint32_t samples)
{
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++)
        if (!offs[i].on) {
            offs[i] = (off_t){tag, samples, (uint8_t)trk, 1};
            return;
        }
    synth_note_off(SY, (int)trk, tag);    /* no room to wait: end it now rather than hang */
}

static void offs_advance(uint32_t n)
{
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++)
        if (offs[i].on) {
            if (offs[i].left <= n) {
                synth_note_off(SY, offs[i].trk, offs[i].tag);
                offs[i].on = 0;
            } else
                offs[i].left -= n;
        }
}

/* ---- the voice ---- */
static struct {
    int8_t clip;                      /* -1: no clip (the hum, or silence) */
    uint8_t live, key, looping, hum, release;
    uint16_t note;                    /* scored: the group's first note; its pitch follows the notes */
    uint16_t group_end;
    int16_t row;
    uint32_t row_at;                  /* scored: the row's start, for the notes' times */
    float pos, gain, level, velocity;
    float pitch, target;              /* semitones: sounding, and where the glide goes */
    uint32_t age, left;               /* samples since the start; samples left (0 = held by a key) */
    float hum_phase;
} sing = {.clip = -1, .row = -1};
static float vib_phase, vib_depth, sing_peak, sing_bright, sing_lp;

static void sing_stop(void)
{
    if (sing.clip >= 0 || sing.hum)
        sing.release = 1;
}

static void sing_start_clip(int clip, unsigned velocity)
{
    sing.clip = (int8_t)clip;
    vocal_locked = (int8_t)clip;
    sing.pos = 0;
    sing.gain = 0;
    sing.age = 0;
    sing.release = 0;
    sing.hum = 0;
    sing.velocity = (float)velocity / 127.0f;
    sing.looping = clips[clip].looped;
}

/* a scored group starts: its first note, the row it belongs to */
static void sing_scored(unsigned row, unsigned note)
{
    int clip = vocal_find((uint16_t)(CLIP_SCORED | note));
    sing.live = 0;
    sing.note = (uint16_t)note;
    sing.group_end = (uint16_t)group_end(&song, note);
    sing.row = (int16_t)row;
    sing.row_at = song.row[row].at;
    int octave = ((int)song.voice[V_OCTAVE] - 1) * 12;
    sing.target = (float)((int)song.note[note].pitch + octave);
    if (sing.pitch < 1.0f)
        sing.pitch = sing.target;
    uint32_t ticks = 0;
    for (unsigned i = note; i < sing.group_end; i++)
        ticks += song.note[i].length;
    sing.left = ticks * tick_samples();
    if (clip < 0) {
        vocal_misses++;
        sing.clip = -1;
        sing.release = 1;             /* nothing ready to sing: stay silent rather than guess */
        return;
    }
    sing_start_clip(clip, 100);
}

static float sing_render_one(void)
{
    if (sing.clip < 0 && !sing.hum)
        return 0.0f;
    const float glide = 0.0004f + 0.02f / (1.0f + (float)song.voice[V_GLIDE]);
    if (!sing.live && sing.clip >= 0 && sing.looping) {
        /* a long scored group: the pitch follows its notes */
        unsigned ticks = (unsigned)(sing.age / tick_samples()), at = 0;
        for (unsigned i = sing.note; i < sing.group_end; i++) {
            at += song.note[i].length;
            if (ticks < at || i + 1 == sing.group_end) {
                sing.target = (float)((int)song.note[i].pitch + ((int)song.voice[V_OCTAVE] - 1) * 12);
                break;
            }
        }
    }
    sing.pitch += glide * (sing.target - sing.pitch);
    /* vibrato: delayed so short notes and consonants stay steady */
    float onset = (float)sing.age * (1.0f / 11025.0f);
    float depth = vib_depth * (onset > 1.0f ? 1.0f : onset);
    float vib = depth * fm_sinf(vib_phase);
    float x = 0.0f;
    if (sing.clip >= 0) {
        const clip_t *c = &clips[sing.clip];
        const int16_t *pcm = clip_arena + c->offset;
        float rate = sing.looping || sing.live ? 0.25f * fm_exp2f((sing.pitch + vib - (float)c->source_pitch) * (1.0f / 12.0f))
                                               : 0.25f * fm_exp2f(vib * (1.0f / 12.0f));
        uint32_t i = (uint32_t)sing.pos;
        if (i + 1 >= c->length) {
            sing.clip = -1;
            vocal_locked = -1;
            return 0.0f;
        }
        float a = (float)pcm[i], b = (float)pcm[i + 1];
        x = (a + (b - a) * (sing.pos - (float)i)) * (1.0f / 32768.0f);
        int holding = sing.looping && !sing.release && c->loop_end > c->loop_start + 8;
        if (holding) {
            float span = (float)(c->loop_end - c->loop_start), fade = span * 0.25f;
            if (sing.pos >= (float)c->loop_end - fade) {
                float p = (float)c->loop_start + sing.pos - ((float)c->loop_end - fade);
                uint32_t j = (uint32_t)p;
                float o = ((float)pcm[j] + ((float)pcm[j + 1] - (float)pcm[j]) * (p - (float)j)) * (1.0f / 32768.0f);
                float f = (sing.pos - ((float)c->loop_end - fade)) / fade;
                x += (o - x) * f;
            }
        }
        sing.pos += rate;
        if (holding && sing.pos >= (float)c->loop_end)
            sing.pos -= (float)(c->loop_end - c->loop_start) - (float)(c->loop_end - c->loop_start) * 0.25f;
    } else {
        /* the hum: a soft two-partial tone, for a pitch with no words ready */
        float f = 440.0f * fm_exp2f((sing.pitch + vib - 69.0f) * (1.0f / 12.0f));
        sing.hum_phase += f * (1.0f / (float)RATE);
        if (sing.hum_phase >= 1.0f)
            sing.hum_phase -= 1.0f;
        float ph = sing.hum_phase * FM_TWO_PI;
        x = 0.30f * fm_sinf(ph) + 0.08f * fm_sinf(2.0f * ph);
    }
    /* the envelope: a quick rise, and a release when the note, the key or the clip ends */
    if (sing.release) {
        sing.gain *= sing.live && sing.clip >= 0 ? 0.9996f : 0.997f;
        if (sing.gain < 0.0005f) {
            sing.clip = -1;
            sing.hum = 0;
            sing.release = 0;
            vocal_locked = -1;
            return 0.0f;
        }
    } else
        sing.gain += 0.02f * (1.0f - sing.gain);
    sing.age++;
    if (sing.left) {
        if (!--sing.left)
            sing.release = 1;
    }
    return x * sing.gain * sing.velocity;
}

/* ---- sequencing ---- */
static void play_tick(unsigned t)
{
    unsigned steps = song.steps, bar = t / steps, step = t % steps;
    const pattern_t *pat = &song.pattern[song.chain[bar < song.bars ? bar : 0] % SONG_PATTERNS];
    uint32_t len = tick_samples();
    for (unsigned b = 0; b < NBACKING; b++) {
        const step_t *s = &pat->step[b][step];
        unsigned trk = b + 1;
        if (!s->count || song.track[trk].mute)
            continue;
        if (trk == T_DRUMS) {
            for (unsigned lane = 0; lane < DL_NSOUND; lane++)
                if (s->drums & (1u << lane)) {
                    drumkit_hit(KIT, (int)lane, s->velocity, kit_tune, 0, 0);
                    eng_drum_hits |= (uint16_t)(1u << lane);
                    if (lane == 0)
                        punch_pump(s->velocity);
                }
        } else
            for (unsigned k = 0; k < s->count; k++) {
                uint32_t tag = (1u << 16) | t << 4 | k;   /* a sequenced note: its tick and place */
                synth_note_on(SY, (int)b, s->note[k], s->velocity, 0, tag);
                schedule_off(b, tag, len * s->length - len / 8u);
            }
    }
    /* the voice: a group starting on this sixteenth */
    int r = row_at(&song, t);
    eng_sing_row = (int16_t)r;
    if (r >= 0 && !song.track[T_VOICE].mute && t < row_end(&song, (unsigned)r)) {
        const row_t *R = &song.row[r];
        const line_t *L = &song.line[R->line];
        for (unsigned i = 0; i < L->count; i++) {
            const note_t *n = &song.note[L->first + i];
            if (n->tie || R->at + n->at != t)
                continue;
            if (!sing.live)
                sing_scored((unsigned)r, L->first + i);
            eng_sing_note = (int16_t)(L->first + i);
            break;
        }
    } else if (r < 0 && !sing.live)
        eng_sing_note = -1;
    /* a row that ends while its last group still sings is cut there */
    if (!sing.live && sing.clip >= 0 && sing.row >= 0 && (r != sing.row || t >= row_end(&song, (unsigned)sing.row)))
        sing_stop();
}

static void transport_stop(void)
{
    eng_playing = 0;
    started = 0;
    synth_all_off(SY, -1);
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++)
        offs[i].on = 0;
    if (!sing.live)
        sing_stop();
    eng_sing_row = -1;
    eng_sing_note = -1;
}

static void commands(void)
{
    unsigned n = 0;
    while (qr != qw && n++ < QSIZE) {
        cmd_t c = queue[qr % QSIZE];
        BARRIER();
        qr++;
        switch (c.op) {
        case C_PLAY:
            if (c.a) {
                eng_playing = 1;
                started = 0;
                clock_phase = 0;
                next_tick = (uint16_t)(c.e < song_ticks(&song) ? c.e : 0);
                eng_tick = next_tick;
            } else
                transport_stop();
            break;
        case C_TEMPO:
            synth_tempo(SY, (float)song.bpm);
            fx_tempo(REVERB, (float)song.bpm);
            echo_set();
            break;
        case C_NOTE:
            if (c.a == T_DRUMS) {
                if (c.b < DL_NSOUND) {
                    drumkit_hit(KIT, c.b, c.c, kit_tune, 0, 0);
                    eng_drum_hits |= (uint16_t)(1u << c.b);
                    if (c.b == 0)
                        punch_pump(c.c);
                }
            } else if (c.a == T_KEYS || c.a == T_BASS)
                synth_note_on(SY, c.a - 1, c.b, c.c, 0, (2u << 16) | c.b);
            break;
        case C_OFF:
            if (c.a == T_KEYS || c.a == T_BASS)
                synth_note_off(SY, c.a - 1, (2u << 16) | c.b);
            break;
        case C_ALLOFF:
            if (c.a == T_DRUMS) {
                const uint8_t *p = par[T_DRUMS - 1];
                drumkit_filter(KIT, FT_DYNAMIC, p[TP_CUT], p[TP_RES], p[TP_FENV], p[TP_KTRK]);
            } else if (c.a < NTRACKS && c.a)
                synth_all_off(SY, c.a - 1);
            break;
        case C_SING: {
            int clip = vocal_find(c.d);
            sing.live = 1;
            sing.key = c.b;
            sing.target = (float)c.b;
            if (sing.pitch < 1.0f || (sing.clip < 0 && !sing.hum))
                sing.pitch = sing.target;
            sing.left = 0;
            sing.row = -1;
            if (clip >= 0)
                sing_start_clip(clip, c.c);
            else {
                if (sing.clip >= 0)
                    vocal_locked = -1;
                sing.clip = -1;
                sing.hum = 1;
                sing.release = 0;
                sing.gain = 0;
                sing.age = 0;
                sing.velocity = (float)c.c / 127.0f;
            }
            break;
        }
        case C_SING_OFF:
            if (sing.live && sing.key == c.b) {
                sing.live = 0;
                sing.release = 1;
                if (sing.clip >= 0)
                    sing.looping = 0;          /* let the syllable finish: its last consonant */
            }
            break;
        case C_GUIDE:
            if (sing.clip >= 0)
                vocal_locked = -1;
            sing.clip = -1;
            sing.live = 0;
            sing.hum = 1;
            sing.release = 0;
            sing.gain = 0;
            sing.age = 0;
            sing.velocity = 0.8f;
            sing.target = sing.pitch = (float)c.b;
            sing.left = c.e;
            break;
        case C_PUNCH:
            if (c.b)
                eng_punch |= 1u << c.a;
            else
                eng_punch &= ~(1u << c.a);
            punch_set(eng_punch);
            break;
        case C_TAP:
            drumkit_hit(KIT, 5, 60, 12, 1, 0);
            break;
        case C_VOICES:
            synth_limit(SY, c.a);
            break;
        }
    }
}

/* ---- the block ---- */
static float level_cur[NTRACKS], vox_peak;

static void render_part(uint32_t n)
{
    memset(mel_l, 0, n * sizeof(float));
    memset(mel_r, 0, n * sizeof(float));
    memset(drum_l, 0, n * sizeof(float));
    memset(drum_r, 0, n * sizeof(float));
    memset(send_dly, 0, n * sizeof(float));
    memset(send_rev, 0, n * sizeof(float));
    memset(send_tape, 0, n * sizeof(float));

    float g[NTRACKS];
    for (unsigned t = 0; t < NTRACKS; t++) {
        float v = song.track[t].mute ? 0.0f : (float)song.track[t].level * 0.01f;
        g[t] = v * v;
    }
    /* Keys and Bass: the synth adds their dry sound and sends; each track's level is applied through
     * its volume knob's performance offset, so here only the gains above need the mix */
    for (unsigned t = 0; t < 2; t++) {
        perf_t *p = synth_perf(SY, (int)t);
        p->mute = song.track[t + 1].mute;
    }
    par[0][TP_LEVEL] = (uint8_t)(g[T_KEYS] * 64.0f);
    par[1][TP_LEVEL] = (uint8_t)(g[T_BASS] * 110.0f);
    synth_prepare(SY, n);
    synth_render(SY, 0, NVOICE, mel_l, mel_r, send_dly, send_rev, send_tape, n);
    drumkit_render(KIT, drum_l, drum_r, send_dly, send_rev, send_tape, n);
    float dg = g[T_DRUMS] * 0.7f;       /* the kit's hits peak near -6 dBFS each: a beat of them sits under the voice */
    for (uint32_t i = 0; i < n; i++) {
        drum_l[i] *= dg;
        drum_r[i] *= dg;
    }

    /* the voice: mono, in the middle, with its own sends */
    float vg = g[T_VOICE] * ((float)song.voice[V_LEVEL] * 0.01f) * 0.9f;
    float vd = (float)song.track[T_VOICE].delay * 0.01f, vr = (float)song.track[T_VOICE].reverb * 0.01f;
    float rate = 3.0f + (float)song.voice[V_RATE] * 0.05f;           /* 3..8 Hz */
    vib_depth = (float)song.voice[V_VIBRATO] * 0.006f;                /* up to 0.6 semitone */
    float peak = 0.0f, bright = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float x = sing_render_one() * vg;
        vib_phase += rate * (FM_TWO_PI / (float)RATE);
        if (vib_phase >= FM_TWO_PI)
            vib_phase -= FM_TWO_PI;
        mel_l[i] += x;
        mel_r[i] += x;
        send_dly[i] += x * vd;
        send_rev[i] += x * vr;
        float ax = x < 0 ? -x : x;
        if (ax > peak)
            peak = ax;
        sing_lp += 0.08f * (x - sing_lp);
        float hi = x - sing_lp;
        bright += hi < 0 ? -hi : hi;
    }
    vox_peak = peak;
    sing_peak += (peak > sing_peak ? 0.6f : 0.08f) * (peak - sing_peak);
    float open = peak > 0.0005f ? bright / ((float)n * (peak + 0.0001f)) * 260.0f : 0.0f;
    sing_bright += 0.3f * ((open > 127.0f ? 127.0f : open) - sing_bright);

    /* the effects' returns go into the melodic group */
    echo_process(send_dly, mel_l, mel_r, n);
    fx_process(REVERB, send_rev, mel_l, mel_r, n);
    master_process(MASTER, drum_l, drum_r, mel_l, mel_r, out_l, out_r, n);
    punch_process(out_l, out_r, n, (float)song.fx[FX_PUMP] * 0.01f, (float)song.fx[FX_FILTER]);
    (void)level_cur;
}

void engine_render(int32_t *out, uint32_t frames)
{
    commands();
    uint32_t done = 0;
    while (done < frames) {
        uint32_t n = frames - done;
        if (n > BLOCK)
            n = BLOCK;
        if (eng_playing) {
            uint32_t inc = song.bpm * 4u, whole = RATE * 60u;
            if (!started) {
                play_tick(next_tick);
                eng_tick = next_tick;
                started = 1;
            }
            uint32_t until = (whole - clock_phase + inc - 1u) / inc;
            if (n > until)
                n = until;
            if (!n)
                n = 1;
        }
        render_part(n);
        for (uint32_t i = 0; i < n; i++) {
            float l = out_l[i], r = out_r[i];
            l = l > 0.999f ? 0.999f : l < -0.999f ? -0.999f : l;
            r = r > 0.999f ? 0.999f : r < -0.999f ? -0.999f : r;
            out[2u * (done + i)] = (int32_t)(l * 8388607.0f);
            out[2u * (done + i) + 1u] = (int32_t)(r * 8388607.0f);
        }
        offs_advance(n);
        if (eng_playing) {
            clock_phase += n * song.bpm * 4u;
            if (clock_phase >= RATE * 60u) {
                clock_phase -= RATE * 60u;
                next_tick++;
                if (next_tick >= song_ticks(&song))
                    next_tick = 0;               /* the song goes round */
                eng_tick = next_tick;
                play_tick(next_tick);
            }
        }
        done += n;
    }
    /* what the screen reads */
    float lv = sing_peak * 400.0f;
    eng_sing_level = (uint8_t)(lv > 127.0f ? 127.0f : lv);
    eng_sing_open = (uint8_t)sing_bright;
    eng_sing_on = sing.clip >= 0 || sing.hum;
    eng_sing_pitch = sing.pitch;
    eng_sing_vibrato = vib_depth * fm_sinf(vib_phase);
    eng_sing_clip = sing.clip;
    for (unsigned t = 1; t < NTRACKS; t++) {
        float v = t == T_DRUMS ? 0.0f : (float)synth_peak[t - 1] * (1.0f / 32767.0f);
        eng_meter[t] = (uint8_t)(v * 127.0f > 127.0f ? 127.0f : v * 127.0f);
    }
    eng_meter[T_VOICE] = eng_sing_level;
}

/* ---- the main loop's calls ---- */
void engine_init(void)
{
    qr = qw = 0;
    memset(par, 0, sizeof par);
    for (unsigned t = 1; t < NTRACKS; t++)
        sound_default(par[t - 1], t == T_DRUMS ? EG_DRUMKIT : EG_SIMPLE);
    synth_init(SY, par);
    drumkit_init(KIT, par[T_DRUMS - 1]);
    fx_init(REVERB, 0, 0, reverb_line);
    memset(echo_line, 0, sizeof echo_line);
    memset(&echo, 0, sizeof echo);
    master_init(MASTER);
    punch_init();
    eng_playing = 0;
}

void engine_song(void)
{
    for (unsigned t = 1; t < NTRACKS; t++)
        engine_sound(t);
    effects_set();
    engine_tempo();
}

void engine_effects(void) { effects_set(); }

void engine_play(int on, unsigned from)
{
    cmd_t c = {C_PLAY, (uint8_t)(on != 0), 0, 0, 0, from};
    send(&c);
}

void engine_tempo(void)
{
    cmd_t c = {C_TEMPO, 0, 0, 0, 0, 0};
    send(&c);
}

void engine_note(unsigned t, unsigned note, unsigned vel)
{
    cmd_t c = {C_NOTE, (uint8_t)t, (uint8_t)note, (uint8_t)(vel ? vel : 1), 0, 0};
    send(&c);
}

void engine_off(unsigned t, unsigned note)
{
    cmd_t c = {C_OFF, (uint8_t)t, (uint8_t)note, 0, 0, 0};
    send(&c);
}

void engine_sing(unsigned note, unsigned vel, uint16_t id)
{
    cmd_t c = {C_SING, 0, (uint8_t)note, (uint8_t)(vel ? vel : 1), id, 0};
    send(&c);
}

void engine_sing_off(unsigned note)
{
    cmd_t c = {C_SING_OFF, 0, (uint8_t)note, 0, 0, 0};
    send(&c);
}

void engine_guide(unsigned note, unsigned ms)
{
    cmd_t c = {C_GUIDE, 0, (uint8_t)note, 0, 0, RATE / 1000u * ms};
    send(&c);
}

void engine_punch(unsigned k, int on)
{
    cmd_t c = {C_PUNCH, (uint8_t)k, (uint8_t)(on != 0), 0, 0, 0};
    send(&c);
}

/* the overload guard: the audio's load measured by the platform (percent of a block's time). Over 85 %
 * the synth plays with fewer voices (a chord's oldest notes end sooner) rather than let a block run
 * late and drop out; back to all of them after three seconds under 60 %. Main loop, every pass. */
volatile uint8_t eng_guard;
void engine_guard(unsigned load_pct, uint32_t now_ms)
{
    static uint32_t calm_since;
    cmd_t c = {C_VOICES, 0, 0, 0, 0, 0};
    if (load_pct > 85u && !eng_guard) {
        eng_guard = 1;
        c.a = 6;
        send(&c);
    }
    if (load_pct >= 60u)
        calm_since = now_ms;
    else if (eng_guard && now_ms - calm_since > 3000u) {
        eng_guard = 0;
        c.a = NVOICE;
        send(&c);
    }
}

void engine_tap(void)
{
    cmd_t c = {C_TAP, 0, 0, 0, 0, 0};
    send(&c);
}
