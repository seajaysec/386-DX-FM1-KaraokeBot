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
/* the main loop's calls and the once-a-tick or once-a-block work: small rather than fast (the samples are the same) */
#define COLD __attribute__((minsize, noinline))

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

COLD static void echo_set(void)
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
static volatile uint8_t poetry;       /* SLAMJAM: the brrr keys snap (punch.h has the rest) */
static volatile uint8_t hush;         /* SLAMJAM outside its room: the band plays, the poet keeps quiet */
void engine_hush(int on) { hush = (uint8_t)(on != 0); }
void engine_poetry(int on)
{
    poetry = (uint8_t)(on != 0);
    punch_poetry(on);
}
volatile uint8_t eng_duck, eng_gate, eng_reps, eng_look[4];

/* ---- the command queue ---- */
enum { C_PLAY, C_NOTE, C_OFF, C_SING, C_SING_OFF, C_GUIDE, C_PUNCH, C_TAP, C_ALLOFF, C_TEMPO, C_VOICES, C_OCTAVE, C_PREVIEW };
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
static void build_pick(void);
static int8_t track_oct[NTRACKS];  /* OCT- / OCT+ on Keys, Bass, Drums: the whole track an octave down or up */

unsigned engine_kits(void) { return NKITS; }
const char *engine_kit_name(unsigned k) { return KITS[k % NKITS].name; }

COLD void engine_sound(unsigned t)
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

COLD static void effects_set(void)
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
    uint8_t steered;                  /* this syllable's clip was pronounced at another pitch (a tune given in the
                                       * SLAMJAM room): repitched as it plays */
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

COLD static void sing_start_clip(int clip, unsigned velocity)
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
COLD static void sing_scored(unsigned row, unsigned note)
{
    int clip = vocal_find((uint16_t)(CLIP_SCORED | note));
    sing.live = 0;
    sing.note = (uint16_t)note;
    sing.group_end = (uint16_t)group_end(&song, note);
    sing.row = (int16_t)row;
    sing.row_at = song.row[row].at;
    sing.target = (float)((int)song.note[note].pitch + voice_register(&song));
    sing.steered = clip >= 0 && (int)clips[clip].source_pitch != (int)sing.target;
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
                sing.target = (float)((int)song.note[i].pitch + voice_register(&song));
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
        float rate = sing.looping || sing.live || sing.steered ? 0.25f * fm_exp2f((sing.pitch + vib - (float)c->source_pitch) * (1.0f / 12.0f))
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
    if (sing.left && !--sing.left) {           /* a guide's or a written note's time is up: as a key let go */
        sing.release = 1;
        sing.live = 0;
        sing.looping = 0;
    }
    return x * sing.gain * sing.velocity;
}

/* ---- sequencing ---- */
/* the clock counts sixths of a sixteenth: a step or a sung note nudged (-3..+3 sixths) plays off the grid, and
 * the arpeggiator's rates (1/8T, 1/16T) land on them. A block is cut only at the sixths something plays on
 * (sub_mask, and every one while an arpeggio runs): a song with neither renders as it did on sixteenths */
static uint8_t sub, sub_mask;

/* ---- the arpeggiator (Keys, Bass): the keys held, else the sequenced step's notes while it lasts, one at a
 * time in the track's order and rate ---- */
static struct {
    uint8_t held[8], nheld;               /* the keys down, in the order played */
    uint8_t seq[4], nseq, vel;
    uint16_t seq_left, phase, off_in;     /* in sixths: the step's time left, since the run began, to the note's end */
    uint8_t i, on;                        /* notes played this run; the one sounding (0 none) */
    uint32_t seed;
} arp[2];

unsigned arp_order(unsigned type, unsigned n, unsigned i, uint32_t *seed)
{
    if (n < 2)
        return 0;
    switch (type) {
    case ARP_DOWN: return n - 1u - i % n;
    case ARP_BOUNCE: {
        unsigned p = i % (2u * n - 2u);
        return p < n ? p : 2u * n - 2u - p;
    }
    case ARP_RANDOM:
        *seed = *seed * 1664525u + 1013904223u;
        return (*seed >> 16) % n;
    default: return i % n;                 /* up, and as played */
    }
}

static int arp_on(unsigned b) { return (song.track[b + 1].arp[0] & 15u) != ARP_OFF; }
COLD static int arp_busy(void)
{
    for (unsigned b = 0; b < 2; b++)
        if (arp[b].on || (arp_on(b) && (arp[b].nheld || arp[b].seq_left)))
            return 1;
    return 0;
}

COLD static void arp_sub(void)
{
    static const uint8_t SIX[NARP_RATE] = {24, 12, 8, 6, 4, 3};
    for (unsigned b = 0; b < 2; b++) {
        const uint8_t *cfg = song.track[b + 1].arp;
        typeof(arp[0]) *A = &arp[b];
        unsigned type = cfg[0] & 15u, oct = (cfg[0] >> 4 & 3u) + 1u, rate = SIX[cfg[1] % NARP_RATE];
        unsigned gate = cfg[2] ? cfg[2] : 50u, n = A->nheld ? A->nheld : A->seq_left ? A->nseq : 0u;
        if (A->seq_left)
            A->seq_left--;
        if (A->on && (!A->off_in || !--A->off_in || !n || !type)) {
            synth_note_off(SY, (int)b, (3u << 16) | A->on);
            A->on = 0;
        }
        if (!n || !type || type >= NARP) {
            A->phase = A->i = 0;
            continue;
        }
        if (A->phase++ % rate)
            continue;
        uint8_t src[8];
        memcpy(src, A->nheld ? A->held : A->seq, n);
        if (type != ARP_PLAYED)                    /* low to high */
            for (unsigned x = 1; x < n; x++)
                for (unsigned y = x; y && src[y - 1] > src[y]; y--) {
                    uint8_t t = src[y];
                    src[y] = src[y - 1];
                    src[y - 1] = t;
                }
        unsigned j = arp_order(type, n * oct, A->i++, &A->seed), note = src[j % n] + 12u * (j / n);
        if (A->on)
            synth_note_off(SY, (int)b, (3u << 16) | A->on);
        A->on = (uint8_t)(note > 127u ? 127u : note);
        synth_note_on(SY, (int)b, A->on, A->vel, 0, (3u << 16) | A->on);
        A->off_in = (uint16_t)(rate * gate / 100u ? rate * gate / 100u : 1u);
    }
}

/* the steps and sung notes of tick t nudged `nudge` sixths: played, or (dry) only counted */
COLD static int play_backing(unsigned t, int nudge, int dry)
{
    unsigned steps = song.steps, step = t % steps;
    uint32_t len = tick_samples();
    int found = 0;
    for (unsigned b = 0; b < NBACKING; b++) {
        unsigned trk = b + 1;
        const step_t *row = lane_steps(&song, trk, t);           /* each track's own chain */
        if (!row)
            continue;
        const step_t *s = &row[step];
        if (!s->count || song.track[trk].mute || s->nudge != nudge || (found++, dry))
            continue;
        if (trk == T_DRUMS) {
            int hats = (s->drums & DRUM_ALT) && (t / steps & 4u) == 0;   /* the return of the bar brings the hats */
            for (unsigned lane = 0; lane < DL_NSOUND; lane++)
                if (s->drums & (1u << lane)) {
                    if (hats && lane >= 8)             /* cymbal, open hat, closed hat */
                        continue;
                    drumkit_hit(KIT, (int)lane, s->velocity, kit_tune + track_oct[T_DRUMS] * 12, 0, 0);
                    eng_drum_hits |= (uint16_t)(1u << lane);
                    if (lane == 0)
                        punch_pump(s->velocity);
                }
        } else if (arp_on(b)) {                  /* the arpeggiator plays them, one at a time, while the step lasts */
            memcpy(arp[b].seq, s->note, 4);
            arp[b].nseq = s->count;
            arp[b].vel = s->velocity;
            arp[b].seq_left = (uint16_t)(s->length * 6u);
            arp[b].phase = arp[b].i = 0;
        } else
            for (unsigned k = 0; k < s->count; k++) {
                uint32_t tag = (1u << 16) | t << 4 | k;   /* a sequenced note: its tick and place */
                synth_note_on(SY, (int)b, s->note[k], s->velocity, 0, tag);
                schedule_off(b, tag, len * s->length - len / 8u);
            }
    }
    return found;
}

COLD static int play_voice(unsigned t, int nudge, int dry)
{
    t = lane_tick(&song, T_VOICE, t);
    int r = row_at(&song, t);
    if (r < 0 || song.track[T_VOICE].mute || hush || t >= row_end(&song, (unsigned)r))
        return 0;
    const row_t *R = &song.row[r];
    const line_t *L = &song.line[R->line];
    for (unsigned i = 0; i < L->count; i++) {
        const note_t *n = &song.note[L->first + i];
        if (note_tie(n) || R->at + n->at != t || note_nudge(n) != nudge)
            continue;
        if (!dry) {
            if (!sing.live)
                sing_scored((unsigned)r, L->first + i);
            eng_sing_note = (int16_t)(L->first + i);
        }
        return 1;
    }
    return 0;
}

/* tick t's sixth u: t's steps and sung notes nudged u later, and the next tick's nudged 6 - u earlier */
static int play_sixth(unsigned t, unsigned next, unsigned u, int dry)
{
    int n = 0;
    if (u <= 3)
        n += play_backing(t, (int)u, dry) + play_voice(t, (int)u, dry);
    if (u >= 3)
        n += play_backing(next, (int)u - 6, dry) + play_voice(next, (int)u - 6, dry);
    return n;
}

/* SLAMJAM's snaps (the brrr keys): the room snapping its fingers (punch.c makes the snap) on every beat, every
 * eighth, every sixteenth or every thirty-second, the backbeat harder; half: the thirty-second between sixteenths */
static void snap(unsigned t, int half)
{
    uint32_t h = poetry ? eng_punch >> PU_STUTTER8 & 15u : 0u;
    if (!h) {
        punch_snap(0);                            /* (none held: they rise from the bottom next time) */
        return;
    }
    unsigned n = h & 8u ? 3u : h & 4u ? 2u : h & 2u ? 1u : 0u;
    if (half ? n < 3u : t % (4u >> (n > 2u ? 2u : n)) != 0u)
        return;
    unsigned vel = t % 8u == 4u && !half ? 127u : t % 4u == 0u && !half ? 116u : 100u;
    punch_snap(vel);
}

COLD static void play_tick(unsigned t)
{
    unsigned next = t + 1u < song_ticks(&song) ? t + 1u : 0u;
    if (!sub) {                                   /* the sixths of this tick that something nudged plays on */
        sub_mask = 0;
        for (unsigned u = 1; u < 6; u++)
            if (play_sixth(t, next, u, 1))
                sub_mask |= (uint8_t)(1u << u);
    }
    play_sixth(t, next, sub, 0);
    if (sub) {
        if (sub == 3)
            snap(t, 1);
        return;
    }
    snap(t, 0);
    /* the voice's row now (its chain plays round again) */
    unsigned vt = lane_tick(&song, T_VOICE, t);
    int r = row_at(&song, vt);
    eng_sing_row = (int16_t)r;
    if (r < 0 && !sing.live)
        eng_sing_note = -1;
    if (hush && !sing.live && sing.clip >= 0)
        sing_stop();                              /* (out of the room: his line stops there) */
    /* a row that ends while its last group still sings is cut there */
    if (!sing.live && sing.clip >= 0 && sing.row >= 0 && (r != sing.row || vt >= row_end(&song, (unsigned)sing.row)))
        sing_stop();
}

COLD static void transport_stop(void)
{
    eng_playing = 0;
    started = 0;
    synth_all_off(SY, -1);
    for (unsigned i = 0; i < sizeof offs / sizeof offs[0]; i++)
        offs[i].on = 0;
    arp[0].seq_left = arp[1].seq_left = 0;
    arp[0].on = arp[1].on = 0;
    if (!sing.live)
        sing_stop();
    eng_sing_row = -1;
    eng_sing_note = -1;
}

COLD static void commands(void)
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
                    drumkit_hit(KIT, c.b, c.c, kit_tune + track_oct[T_DRUMS] * 12, 0, 0);
                    eng_drum_hits |= (uint16_t)(1u << c.b);
                    if (c.b == 0)
                        punch_pump(c.c);
                }
            } else if ((c.a == T_KEYS || c.a == T_BASS) && arp_on(c.a - 1u)) {
                typeof(arp[0]) *A = &arp[c.a - 1u];
                if (!A->nheld)
                    A->phase = A->i = 0;         /* the first key down starts the run */
                if (A->nheld < 8 && !memchr(A->held, c.b, A->nheld))
                    A->held[A->nheld++] = c.b;
                A->vel = c.c;
            } else if (c.a == T_KEYS || c.a == T_BASS)
                synth_note_on(SY, c.a - 1, c.b, c.c, 0, (2u << 16) | c.b);
            break;
        case C_OFF:
            if (c.a == T_KEYS || c.a == T_BASS) {
                typeof(arp[0]) *A = &arp[c.a - 1u];
                uint8_t *h = memchr(A->held, c.b, A->nheld);
                if (h) {
                    memmove(h, h + 1, (size_t)(A->held + A->nheld - h - 1));
                    A->nheld--;
                }
                synth_note_off(SY, c.a - 1, (2u << 16) | c.b);
            }
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
            sing.target = (float)((int)c.b + voice_register(&song));      /* the keys sing in QUALITY's register */
            if (sing.pitch < 1.0f || (sing.clip < 0 && !sing.hum))
                sing.pitch = sing.target;
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
            sing.left = c.e;                   /* a note written: heard for its length (0: while the key is down) */
            break;
        }
        case C_PREVIEW:                        /* a note written on Keys, Bass or Drums, heard for its length */
            if (c.a == T_DRUMS)
                drumkit_hit(KIT, c.b, c.c, kit_tune + track_oct[T_DRUMS] * 12, 0, 0);
            else {
                synth_note_on(SY, c.a - 1, c.b, c.c, 0, (4u << 16) | c.b);
                schedule_off(c.a - 1u, (4u << 16) | c.b, c.e);
            }
            break;
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
        case C_OCTAVE:
            track_oct[c.a % NTRACKS] = (int8_t)((int)c.b - 8);
            break;
        case C_PUNCH:
            if (c.b && c.a == PU_BUILD && !(eng_punch >> PU_BUILD & 1u))
                build_pick();
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

/* build, held: a riser on 1 to 4 of the tracks, picked at random at each press (eng_build_mask, a bit a track):
 * Drums a snare roll that speeds up every two beats (eighths, sixteenths, 32nds, 64ths) and grows louder, and a
 * high-pass sweeping up from 30 Hz to about 2 kHz over two bars, swelling 3 dB; Keys and Bass rising an octave with
 * their filters opening; the Voice the high-pass's sweep. Let go, the riser falls away in about 50 ms. */
static struct {
    uint32_t held, next, seed;        /* samples held; samples to the next hit */
    float rise, k, g, hp[3];          /* 0..1; the high-pass's coefficient and swell; its states (drums L, R; voice) */
} build;
volatile uint8_t eng_build, eng_build_mask;

COLD static void build_pick(void)
{
    build.seed = build.seed * 1664525u + 1013904223u + eng_tick * 2654435761u;   /* the sixteenth pressed on decides */
    eng_build_mask = (uint8_t)(1u + (build.seed >> 16) % 15u);
}

COLD static void build_roll(uint32_t n)
{
    uint32_t beat = RATE * 60u / song.bpm;
    int held = eng_punch >> PU_BUILD & 1u;
    if (!held)
        build.held = build.next = 0;
    if (held && (eng_build_mask >> T_DRUMS & 1u) && build.next <= n) {
        unsigned k = build.held / (2u * beat);
        unsigned vel = 50u + (build.held < 8u * beat ? build.held * 77u / (8u * beat) : 77u);
        drumkit_hit(KIT, 1, (int)vel, kit_tune + track_oct[T_DRUMS] * 12, 0, 0);      /* the snare */
        eng_drum_hits |= 2u;
        build.next += beat >> (1u + (k > 3u ? 3u : k));
    }
    if (held) {
        build.next = build.next > n ? build.next - n : 0;
        build.held += n;
    }
    /* the riser: up while held, away in ~50 ms after */
    float want = held ? (float)build.held / (float)(8u * beat) : 0.0f;
    want = want > 1.0f ? 1.0f : want;
    build.rise += (want > build.rise ? 1.0f : 0.25f) * (want - build.rise);
    if (build.rise < 0.001f) {
        build.rise = 0.0f;
        build.hp[0] = build.hp[1] = build.hp[2] = 0.0f;
    }
    build.k = 1.0f - fm_expf(-FM_TWO_PI * 30.0f * fm_exp2f(build.rise * 6.0f) / (float)RATE);
    build.g = 1.0f + 0.41f * build.rise;
    /* Keys and Bass: the octave, and the riser's climb and opening filter */
    for (unsigned t = T_KEYS; t <= T_BASS; t++) {
        perf_t *p = synth_perf(SY, (int)t - 1);
        int on = eng_build_mask >> t & 1u;
        p->pitch_c = (int16_t)(track_oct[t] * 1200 + (on ? (int)(build.rise * 1200.0f) : 0));
        p->cut = (int16_t)(on ? (int)(build.rise * 90.0f) : 0);
    }
}

/* the riser's high-pass on one buffer, its state st */
static void build_hp(float *x, float *st, uint32_t n)
{
    float s = *st;
    for (uint32_t i = 0; i < n; i++) {
        s += build.k * (x[i] - s);
        x[i] = (x[i] - s) * build.g;
    }
    *st = fm_flush(s);
}

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
    build_roll(n);
    synth_prepare(SY, n);
    synth_render(SY, 0, NVOICE, mel_l, mel_r, send_dly, send_rev, send_tape, n);
    drumkit_render(KIT, drum_l, drum_r, send_dly, send_rev, send_tape, n);
    float dg = g[T_DRUMS] * 0.7f;       /* the kit's hits peak near -6 dBFS each: a beat of them sits under the voice */
    for (uint32_t i = 0; i < n; i++) {
        drum_l[i] *= dg;
        drum_r[i] *= dg;
    }
    if (build.rise > 0.0f && (eng_build_mask >> T_DRUMS & 1u)) {
        build_hp(drum_l, &build.hp[0], n);
        build_hp(drum_r, &build.hp[1], n);
    }
    int vox_rise = build.rise > 0.0f && (eng_build_mask >> T_VOICE & 1u);

    /* the voice: mono, in the middle, with its own sends */
    float vg = g[T_VOICE] * ((float)song.voice[V_LEVEL] * 0.01f) * 0.9f;
    float vd = (float)song.track[T_VOICE].delay * 0.01f, vr = (float)song.track[T_VOICE].reverb * 0.01f;
    float rate = 3.0f + (float)song.voice[V_RATE] * 0.05f;           /* 3..8 Hz */
    vib_depth = (float)song.voice[V_VIBRATO] * 0.006f;                /* up to 0.6 semitone */
    float peak = 0.0f, bright = 0.0f;
    for (uint32_t i = 0; i < n; i++) {
        float x = sing_render_one() * vg;
        if (vox_rise) {                                 /* build on the Voice: the riser's sweep */
            build.hp[2] += build.k * (x - build.hp[2]);
            x = (x - build.hp[2]) * build.g;
        }
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
        uint32_t inc = song.bpm * 24u, whole = RATE * 60u;     /* a sixth of a sixteenth (it runs stopped too: the arp) */
        if (eng_playing && !started) {
            sub = 0;
            play_tick(next_tick);
            eng_tick = next_tick;
            started = 1;
        }
        int busy = arp_busy();
        if (eng_playing || busy) {               /* the block cut at the next sixth that matters */
            unsigned k = 1;
            while (!busy && sub + k < 6 && !(sub_mask >> (sub + k) & 1u))
                k++;
            uint32_t until = (k * whole - clock_phase + inc - 1u) / inc;
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
        clock_phase += n * inc;
        while (clock_phase >= whole) {
            clock_phase -= whole;
            if (++sub == 6)
                sub = 0;
            if (eng_playing) {
                if (!sub && ++next_tick >= song_ticks(&song))
                    next_tick = 0;               /* the song goes round */
                eng_tick = next_tick;
                play_tick(next_tick);
            }
            arp_sub();
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
    eng_duck = (uint8_t)(punch_duck() * 127.0f);
    eng_gate = (uint8_t)(punch_gate() * 127.0f);
    eng_reps = (uint8_t)punch_reps();
    punch_look((uint8_t *)eng_look);
    eng_build = (uint8_t)(build.rise * 127.0f);
}

/* ---- the main loop's calls ---- */
COLD void engine_init(void)
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

COLD void engine_song(void)
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

void engine_preview(unsigned t, unsigned note, unsigned vel, uint16_t id, unsigned ms)
{
    cmd_t c = {t == T_VOICE ? C_SING : C_PREVIEW, (uint8_t)t, (uint8_t)note, (uint8_t)(vel ? vel : 1), id, RATE / 1000u * ms};
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

void engine_octave(unsigned track, int octave)
{
    cmd_t c = {C_OCTAVE, (uint8_t)track, (uint8_t)(octave + 8), 0, 0, 0};
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
COLD void engine_guard(unsigned load_pct, uint32_t now_ms)
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
