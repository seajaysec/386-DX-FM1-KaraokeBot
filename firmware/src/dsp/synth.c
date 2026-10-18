/* SPDX-License-Identifier: GPL-3.0-only */
/* voices: sixteen voices playing eight engines (synth.h), each with two ADSRs, a filter (the
 * five types), a steal fade and sends; per track an LFO, a mono stack, parameter locks and
 * performance offsets.
 *
 * Time runs in SY_CHUNK (64-sample) chunks: at a chunk boundary a voice reads its track's parameters
 * (the knob, or its lock, plus the LFO's offset on the destination) and turns them into the units the
 * sample loop wants (increments, envelope rates, gains); inside the chunk only the per-sample state
 * moves, and the filter's coefficients refresh every 16 samples. A render of any length splits itself
 * at those boundaries, and every voice carries its own position in the chunk, so a block rendered as
 * 100 + 156 frames equals one of 256, and voices 0..8 on one core and 8..16 on the other never share a
 * word that either writes: the track-level values they read (LFO, duck, tremolo) are computed into
 * per-chunk slots by synth_prepare() before the block is split. The LFO's fade envelope is the
 * voice's own (it counts from its note), so it scales the track's slot values per voice.
 *
 * Levels: an engine's raw cycle peaks near 1; the amp envelope, velocity (0.25 + 0.75 vel^2), the
 * preset volume ((v/127)^2) and the mix level (v/127) bring a default note to about -9 dBFS, so a
 * chord of six leaves the master's limiter something to do but not much. Float, no libm (fastmath.h),
 * no malloc, no stack buffer over 128 bytes in the render path (the audio interrupt's stack). */
#include "synth.h"
#include "fastmath.h"
/* 386DX builds five of the eight engines (flash is shared with the speech engine): wavetable,
 * hardsync and swarm stay in the source, out of the build */
#ifndef SYNTH_ALL_ENGINES
#define SYNTH_ALL_ENGINES 0
#endif
#include "drumkit.h"
#if SYNTH_ALL_ENGINES
#include "wavetables.h"
#endif

#define SUB 16                                /* filter coefficient refresh, steal fade steps */
#define NCK 8                                 /* chunk slots a prepare may cover (n <= 512) */
#define STEAL_RATE (1.0f / (0.015f * SY_SR))  /* the 15 ms steal fade, per sample */
#define INV_SR (1.0f / SY_SR)
#define CHUNK_S ((float)SY_CHUNK / SY_SR)     /* a chunk in seconds */
#define LD_PITCH (-2)                         /* the LFO destination "pitch" (no TP_* holds it) */
#define LD_NONE (-1)
/* the sample loops must be inlined into their callers (the compiler folds their constant arguments
 * away); left to itself it outlines the bigger ones, at a call with thirty spills a sub-block */
#if defined(__GNUC__) || defined(__clang__)
#define SY_INLINE static inline __attribute__((always_inline))
#else
#define SY_INLINE static inline
#endif

/* ------------------------------------------------------------------ names --- */
const char *const EG_NAME[NENGINE] = {"simple", "stack", "wavetable", "duo", "epiano", "organ", "hardsync",
                                      "swarm", "drumkit"};
const char *const EG_KNOB[NENGINE][4] = {
    {"shape", "pw", "noise", "stereo"},   {"shape", "ratio", "detune", "stereo"}, {"table", "position", "warp", "drift"},
    {"tone", "ratio", "shape", "tremolo"}, {"tone", "texture", "punch", "tine"},  {"type", "bass", "tremolo", "speed"},
    {"freq", "sub", "noise", "lowcut"},   {"swarm", "am", "fm", "detune"},        {"-", "-", "-", "-"}};
/* the printed defaults (00..99) times 1.28; the organ's four and stack's ratio are discrete 1..8 */
static const uint8_t EG_DEF[NENGINE][4] = {{37, 42, 26, 127}, {19, 2, 6, 28}, {0, 59, 127, 0}, {51, 104, 32, 26},
                                           {0, 0, 52, 68},    {1, 3, 1, 4},   {17, 10, 127, 23}, {0, 12, 41, 0},
                                           {0, 0, 0, 0}};
static const char *const VMODE_N[3] = {"poly", "mono", "legato"};
static const char *const FTYPE_N[NFT] = {"svf", "ladder", "z lowpass", "z hipass", "dynamic"};
static const char *const LTYPE_N[NLT] = {"off", "value", "tremolo", "random", "duck"};
static const char *const VSHAPE_N[4] = {"square", "sine", "ramp", "saw"};
static const char *const TSHAPE_N[5] = {"sine", "saw", "expo", "square", "blip"};
static const char *const LDEST_N[NLD] = {"synth", "envelope", "filter", "amp", "synth free", "env free", "filter free",
                                         "amp free"};
static const char *const DUCK_N[12] = {"track 1", "track 2", "track 3", "track 4", "track 5", "track 6", "track 7",
                                       "track 8", "follow", "punch", "tape", "metro"};
static const char *const PLAYER_N[NPL] = {"off", "arp", "roll", "hold"};
static const char *const ARPSPD_N[9] = {"1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64"};
static const char *const ARPPAT_N[6] = {"up", "down", "updown 1", "updown 2", "played", "random"};
static const char *const ARPLEN_N[3] = {"short", "medium", "long"};
static const char *const ARPSTY_N[6] = {"off", "A", "B", "C", "D", "E"};
static const char *const ARPGLD_N[3] = {"off", "low", "high"};
static const char *const ARPSTE_N[3] = {"mono", "narrow", "wide"};
static const char *const MPAT_N[4] = {"up", "down", "updown", "random"};
static const char *const FOLLOW_N[3] = {"no", "diatonic", "transp"};
static const char *const BEND_N[9] = {"off", "1", "2", "3", "4", "5", "6", "7", "octave"};
#if SYNTH_ALL_ENGINES
#if SYNTH_ALL_ENGINES
static const char *const WTNAME_N[WT_NTABLE] = {WT_NAMES};
#endif
#endif
static const char *const SYNC_N[LS_SYNC_N] = {"2/1", "1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64"};
static const float SYNC_BEATS[LS_SYNC_N] = {8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f};
static const char *const ENVP_N[4] = {"attack", "decay", "sustain", "release"};
static const char *const FLTP_N[4] = {"cutoff", "reso", "env", "keytrack"};
static const char *const AMPP_N[4] = {"volume", "pitch", "pan", "-"};
/* the LFO's module / knob to a parameter index */
static const int16_t LD_TP[4][4] = {{TP_E1, TP_E2, TP_E3, TP_E4},
                                    {TP_A, TP_D, TP_S, TP_R},
                                    {TP_CUT, TP_RES, TP_FENV, TP_KTRK},
                                    {TP_VOLUME, LD_PITCH, TP_PAN, LD_NONE}};

/* --------------------------------------------------------------- structs --- */
enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };
typedef struct { uint8_t stage; float level; } env_t;

/* a voice's chunk configuration: the parameters in sample-loop units */
typedef struct {
    float freq;                       /* Hz, after glide, bend, vibrato */
    float amp_ar, amp_dc, amp_sl, amp_rc, flt_ar, flt_dc, flt_sl, flt_rc;
    float cut_oct;                    /* log2(cutoff / 50 Hz) without the envelope */
    float fenv_oct;                   /* the filter envelope's reach, octaves */
    float kq;                         /* 1 / Q */
    float kres;                       /* the ladder's feedback 0..4 */
    float gl, gr;                     /* dry gains (pan, level, volume, velocity, tremolo, duck) */
    float g1, g2, gt;                 /* sends, relative to the dry level */
    float pitch_off;                  /* cents added to the note (bend, perf, vibrato, an LFO on pitch) */
    float e[4];                       /* the engine knobs 0..1 */
    float ek[4];                      /* the same, raw (the discrete knobs: 1..8, a table index) */
    float ex[20];                     /* the engine's derived values (eng_prep) */
    uint8_t ftype, stereo, mute, eng;
} cfg_t;

typedef struct {
    uint8_t trk, held, engine, steal_phase;   /* held: the key is down; steal: 1 fading out, 2 in */
    uint8_t note, pend_note, pend_vel, pend_held;
    uint32_t tag, pend_tag, age;
    float vel, pitch, pitch_cur, glide_k;     /* semitones; glide_k: the chunk's approach fraction */
    int16_t pend_glide;
    uint16_t pos;                     /* sample in the chunk */
    uint8_t ck;                       /* chunk slot since prepare */
    uint8_t nchunk;                   /* chunks so far (the slow refreshes count on it, not on the block size) */
    env_t amp, flt;                   /* stepped once a sub-block (SUB samples) */
    uint8_t fresh;                    /* configured mid-block: refresh the coefficients before the next sample */
    float amp_cur, amp_step;          /* the amp level, ramped inside the sub-block */
    float steal_gain, sg_step;
    float glc, grc, gl_step, gr_step; /* the dry gains, ramped */
    float a1, a2, a3, b1, b2, b3;     /* the filter's coefficients (TPT SVF, the svf's second stage; the ladder's g and k) */
    float f[8];                       /* filter: ic1 ic2 jc1 jc2 of L, then R (the ladder: its four poles a side) */
    float peak;
    float tsec;                       /* seconds since the note (the LFO's fade envelope counts on it) */
    uint32_t rng;
    cfg_t c;
    float env_in[8];                  /* the envelope knobs the coefficients were made from */
    float pan_in, pan_l, pan_r;       /* the pan the gains were made from, and those gains */
    float pitch_in, e_in[4];          /* the pitch and the knobs the frequency and increments were made from */
    float o[16];                      /* the engine's state */
} voice_t;

typedef struct {
    uint8_t lock[NTP];
    perf_t perf;
    int16_t bend_c;
    uint8_t mono_n, mono_note[8], mono_vel[8];
    uint32_t mono_tag[8];
    int8_t mono_voice;
    float last_pitch;                 /* the previous note: poly glide starts here */
    float lfo_ph, lfo_sh, lfo_sm;     /* the LFO's phase, the random's target and its slewed value */
    float duck, duck_hold;            /* the duck's level 0..1 and the hold time left, seconds */
    float lf_in, lf_out;              /* the fade envelope: seconds to fade in (or out), 0 = none */
    float lscale;                     /* the destination's range / 127: the offset in the knob's units */
    int16_t ldest;                    /* the resolved destination: a TP_* index, LD_PITCH or LD_NONE */
    uint32_t rng;
    float loff[NCK], lpitch[NCK], lgain[NCK];   /* the chunk slots: offset (param units), cents, gain */
} trk_t;

struct synth {
    uint8_t (*par)[NTP];
    float bpm;
    uint32_t counter;                 /* voice ages */
    uint16_t pos;                     /* chunk phase at the next prepare */
    uint8_t nck, max_voices;
    trk_t t[NTRK];
    voice_t v[NVOICE];
};
_Static_assert(sizeof(struct synth) <= SYNTH_BYTES, "synth_t outgrew SYNTH_BYTES");
#ifdef DX_HOST
int synth_sizeof(void) { return (int)sizeof(struct synth); }
#endif

volatile uint16_t synth_peak[NTRK];

/* ------------------------------------------------------------ small maths --- */
static inline uint32_t xs(uint32_t *r)
{
    uint32_t x = *r;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *r = x;
    return x;
}
static inline float frand(uint32_t *r) { return (float)(int32_t)xs(r) * (1.0f / 2147483648.0f); }   /* -1..1 */
static inline float blep(float t, float dt, float idt)      /* the PolyBLEP residual at phase t, step dt */
{
    if (t < dt) {
        t *= idt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) * idt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float wrap1(float p) { return p - fm_floorf(p); }
static inline float step1(float p, float d)      /* a phase accumulator: p + d wrapped to [0, 1), d < 1 */
{
    p += d;
    return p >= 1.0f ? p - 1.0f : p;
}
static inline float soft3(float x)               /* a cubic soft clip: linear near 0, flat at +-1.5 */
{
    x = fm_clampf(x, -1.5f, 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static float env_seconds(float p) { return p < 0.001f ? 0.001f : 0.001f * fm_exp2f(p * 13.2877124f); }  /* 1 ms .. 10 s */
/* the envelopes step once a sub-block of SUB samples: the attack's increment, the decay's and the
 * release's multiplier (to 0.1 % in secs) for that step */
static float env_rate(float secs) { return (float)SUB / (secs * SY_SR); }
static float env_coef(float secs) { return fm_expf(-6.9f * (float)SUB / (secs * SY_SR)); }
static float lfo_hz(int v, float bpm)
{
    if (v < LS_SYNC_N)
        return bpm * (1.0f / 60.0f) / SYNC_BEATS[v];
    return 0.1f * fm_exp2f((float)(v - LS_SYNC_N) * (8.22881869f / 119.0f));   /* 0.1 .. 30 Hz */
}
/* the LFO's fade envelope knob (synth.h): seconds to fade in, or to fade out */
static void fade_secs(int v, float *in, float *out)
{
    *in = *out = 0.0f;
    if (v < 64)
        *in = (float)(64 - v) * (3.0f / 64.0f);
    else if (v > 64)
        *out = fm_maxf((float)(127 - v) * (3.0f / 63.0f), 0.05f);
}

static inline void env_step(env_t *e, float ar, float dc, float sl, float rc)
{
    switch (e->stage) {
    case ENV_ATTACK:
        e->level += ar;
        if (e->level >= 1.0f) {
            e->level = 1.0f;
            e->stage = ENV_DECAY;
        }
        break;
    case ENV_DECAY:
        e->level = sl + (e->level - sl) * dc;
        if (e->level <= sl + 0.0001f) {
            e->level = sl;
            e->stage = ENV_SUSTAIN;
        }
        break;
    case ENV_SUSTAIN: e->level = sl; break;
    case ENV_RELEASE:
        e->level *= rc;
        if (e->level < 0.0001f) {
            e->level = 0.0f;
            e->stage = ENV_OFF;
        }
        break;
    default: e->level = 0.0f; break;
    }
}

/* ----------------------------------------------------------- parameters --- */
static inline int P(const synth_t *s, int trk, int p)      /* the knob, or its lock */
{
    uint8_t l = s->t[trk].lock[p];
    return l != LOCK_NONE ? l : s->par[trk][p];
}
/* the effective value: the LFO adds to its destination (clamped to the byte; the knob's own range is
 * applied by the engine that reads it) */
static inline float pf(const synth_t *s, int trk, int p, int dest, float off)
{
    float x = (float)P(s, trk, p);
    if (p == dest)
        x = fm_clampf(x + off, 0.0f, 127.0f);
    return x;
}

/* ----------------------------------------------------------- the engines --- */
/* each: eng_prep() turns c.e[] / c.ek[] and c.freq into c.ex[] once a chunk (or after a steal);
 * eng_run() writes m <= SUB raw samples to xl (and xr when c.stereo); the amplitude of a cycle is
 * about 1; eng_chunk() moves the engine's own slow parts every chunk */
static void eng_init(voice_t *v)
{
    int i;
    for (i = 0; i < 16; i++)
        v->o[i] = 0.0f;
    if (v->engine == EG_ORGAN)
        for (i = 0; i < 6; i++)
            v->o[i * 2] = 1.0f;                  /* the rotating phasors start at 1 + 0i */
    v->o[15] = 1.0f;                             /* epiano: its brightness envelopes start full */
    v->o[14] = 1.0f;
    if (v->engine == EG_ORGAN)
        v->o[13] = 1.0f, v->o[14] = 0.0f;        /* the tremolo gain and its ramp */
}

/* simple: a PolyBLEP saw morphing to a square (shape), pw narrows the pulse (strongest once the shape
 * is past the middle: the pulse's share), white noise, a detuned copy spread across the field */
static void simple_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float w = c->e[3];
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * fm_exp2f(w * 0.012f) * INV_SR, 0.45f);   /* the copy: up to 14 cents */
    c->ex[2] = 0.5f - c->e[1] * 0.47f;           /* pulse width 50 % .. 3 % */
    c->ex[3] = c->e[0];                          /* saw .. pulse */
    c->ex[4] = c->e[2] * 0.5f;                   /* noise */
    c->ex[5] = fm_cosf((1.0f - w) * 0.78539816f);   /* the field: both in the middle .. one each side */
    c->ex[6] = fm_sinf((1.0f - w) * 0.78539816f);
    c->stereo = w > 0.001f;
}
SY_INLINE float saw_pulse(float p, float dt, float idt, float pw, float mix)
{
    float b0 = blep(p, dt, idt), saw = 2.0f * p - 1.0f - b0, pl;   /* the wrap's residual serves both */
    if (mix <= 0.0f)
        return saw;
    pl = (p < pw ? 1.0f : -1.0f) + b0 - blep(wrap1(p + 1.0f - pw), dt, idt);
    if (mix >= 1.0f)
        return pl;
    return saw + (pl - saw) * mix;
}
static void simple_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float p1 = v->o[0], p2 = v->o[1], d1 = c->ex[0], d2 = c->ex[1], i1 = 1.0f / d1, i2 = 1.0f / d2;
    float pw = c->ex[2], mix = c->ex[3], nz = c->ex[4], ga = c->ex[5], gb = c->ex[6];
    int i;
    if (c->stereo) {
        float norm = 1.0f / (ga + gb);           /* the same loudness whatever the width */
        ga *= norm;
        gb *= norm;
        for (i = 0; i < m; i++) {
            float a, b, n;
            p1 = step1(p1, d1);
            p2 = step1(p2, d2);
            a = saw_pulse(p1, d1, i1, pw, mix);
            b = saw_pulse(p2, d2, i2, pw, mix);
            n = frand(&v->rng) * nz;
            xl[i] = a * ga + b * gb + n;
            xr[i] = a * gb + b * ga + n;
        }
    } else {
        for (i = 0; i < m; i++) {
            p1 = step1(p1, d1);
            xl[i] = saw_pulse(p1, d1, i1, pw, mix) + frand(&v->rng) * nz;
        }
    }
    v->o[0] = p1;
    v->o[1] = p2;
}

/* stack: two oscillators, the second at ratio N:1 (1..8, the knob's own range) and detuned, one each
 * side of the field as stereo opens; shape = saw (0) through a square at the middle to narrowing pulses */
static void stack_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float w = c->e[3], ratio = fm_clampf(fm_floorf(c->ek[1] + 0.5f), 1.0f, 8.0f), cents = c->e[2] * 50.0f;
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * ratio * fm_exp2f(cents * (1.0f / 1200.0f)) * INV_SR, 0.45f);
    if (c->e[0] < 0.5f) {                        /* saw .. square */
        c->ex[2] = c->e[0] * 2.0f;
        c->ex[3] = 0.5f;
    } else {                                     /* square .. a 10 % pulse */
        c->ex[2] = 1.0f;
        c->ex[3] = 0.5f - (c->e[0] - 0.5f) * 0.8f;
    }
    c->ex[5] = fm_cosf((1.0f - w) * 0.78539816f);
    c->ex[6] = fm_sinf((1.0f - w) * 0.78539816f);
    c->stereo = w > 0.001f;
}
static void stack_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float p1 = v->o[0], p2 = v->o[1], d1 = c->ex[0], d2 = c->ex[1], i1 = 1.0f / d1, i2 = 1.0f / d2;
    float mix = c->ex[2], pw = c->ex[3], ga = c->ex[5], gb = c->ex[6], norm = 1.0f / (ga + gb);
    int i;
    ga *= norm;
    gb *= norm;
    for (i = 0; i < m; i++) {
        float a, b;
        p1 = step1(p1, d1);
        p2 = step1(p2, d2);
        a = saw_pulse(p1, d1, i1, pw, mix);
        b = saw_pulse(p2, d2, i2, pw, mix);
        if (c->stereo) {
            xl[i] = a * ga + b * gb;
            xr[i] = a * gb + b * ga;
        } else {
            xl[i] = (a + b) * 0.5f;
        }
    }
    v->o[0] = p1;
    v->o[1] = p2;
}

#if SYNTH_ALL_ENGINES
/* wavetable: a frame crossfade through one of nine tables, warp bends the phase (symmetry), drift
 * phase-modulates it by a second oscillator that climbs from a slow wobble to the note's own rate */
static void wt_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float pos = c->e[1] * (float)(WT_NFRAME - 1), fr = fm_floorf(pos), dr = c->e[3];
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_clampf(fm_floorf(c->ek[0] + 0.5f), 0.0f, (float)(WT_NTABLE - 1));
    c->ex[2] = fm_minf(fr, (float)(WT_NFRAME - 2));
    c->ex[3] = pos - c->ex[2];
    c->ex[4] = 0.5f + c->e[2] * 0.45f;           /* the warp point: 0.5 = straight */
    c->ex[5] = dr > 0.001f ? c->freq * fm_exp2f(-7.0f + 8.0f * dr) * INV_SR : 0.0f;   /* 1/128 .. 2x the note */
    c->ex[6] = dr * dr * 0.6f;                   /* depth, turns */
    c->stereo = 0;
}
static void wt_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    const int16_t *ta = WT[(int)c->ex[1]][(int)c->ex[2]], *tb = ta + WT_N;
    float p = v->o[0], mp = v->o[1], d = c->ex[0], md = c->ex[5], fb = c->ex[3], w = c->ex[4];
    float ka = 0.5f / w, kb = 0.5f / (1.0f - w), depth = c->ex[6];
    int i;
    (void)xr;
    for (i = 0; i < m; i++) {
        float q, s, fa, a, b;
        int k;
        p = step1(p, d);
        q = p < w ? p * ka : 0.5f + (p - w) * kb;
        if (md > 0.0f) {
            mp = step1(mp, md);
            q = wrap1(q + depth * fm_sin_turns(mp));
        }
        s = q * (float)WT_N;
        k = (int)s;
        fa = s - (float)k;
        k &= WT_N - 1;
        a = (float)ta[k] + ((float)ta[(k + 1) & (WT_N - 1)] - (float)ta[k]) * fa;
        b = (float)tb[k] + ((float)tb[(k + 1) & (WT_N - 1)] - (float)tb[k]) * fa;
        xl[i] = (a + (b - a) * fb) * (1.0f / 32768.0f);
    }
    v->o[0] = p;
    v->o[1] = mp;
}
#endif

/* duo: two-operator FM for strings: tone = index (brightness), ratio from a detuned 1:1 (0..50: slow
 * beating) to harmonic divisions (51..100), shape = modulator feedback (sine toward saw), tremolo = the
 * speed of a pitch wobble that fades in over the first second of a note (0 = none) */
static const float DUO_RATIO[12] = {0.5f, 0.6666667f, 0.75f, 1.25f, 1.3333334f, 1.5f, 1.75f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
static void duo_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float r = c->e[1] * 100.0f, ratio;
    if (r <= 50.0f)
        ratio = fm_exp2f(r * 0.6f * (1.0f / 1200.0f));          /* 0 .. 30 cents */
    else {
        int k = (int)((r - 51.0f) * (11.99f / 49.0f));
        ratio = DUO_RATIO[k < 0 ? 0 : k > 11 ? 11 : k];
    }
    c->ex[6] = fm_minf(c->freq * INV_SR, 0.45f);                 /* the base increments; duo_chunk wobbles them */
    c->ex[7] = fm_minf(c->freq * ratio * INV_SR, 0.45f);
    c->ex[0] = c->ex[6];
    c->ex[1] = c->ex[7];
    c->ex[2] = c->e[0] * c->e[0] * 5.0f;                        /* index, turns */
    c->ex[3] = c->e[2] * 1.3f;                                  /* feedback */
    c->ex[4] = c->e[3] > 0.004f ? (0.3f + c->e[3] * 7.7f) * INV_SR : 0.0f;   /* the wobble's rate, 0.3 .. 8 Hz */
    c->stereo = 0;
}
static void duo_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float cp = v->o[0], mp = v->o[1], fbv = v->o[2];
    float dc = c->ex[0], dm = c->ex[1], idx = c->ex[2] * 0.25f, fb = c->ex[3] * 0.25f;
    int i;
    (void)xr;
    for (i = 0; i < m; i++) {
        float mo;
        mp = step1(mp, dm);
        cp = step1(cp, dc);
        mo = fm_sin_turns(mp + fbv * fb);
        fbv = mo;
        xl[i] = fm_sin_turns(cp + mo * idx);
    }
    v->o[0] = cp;
    v->o[1] = mp;
    v->o[2] = fbv;
}
static void duo_chunk(voice_t *v)               /* the wobble: +-20 cents at the knob's speed, fading in */
{
    cfg_t *c = &v->c;
    float w;
    if (c->ex[4] <= 0.0f)
        return;
    v->o[3] = wrap1(v->o[3] + c->ex[4] * (float)SY_CHUNK);
    v->o[4] += (1.0f - v->o[4]) * 0.0015f;       /* the fade: most of the way in a second */
    w = 1.0f + 0.012f * v->o[4] * fm_sin_turns(v->o[3]);
    c->ex[0] = fm_minf(c->ex[6] * w, 0.45f);
    c->ex[1] = fm_minf(c->ex[7] * w, 0.45f);
}

/* epiano: a tine (carrier with a 1:1 modulator whose index decays, as a struck reed's brightness),
 * texture = a 2:1 modulator for grit, punch = a short index burst at the start, tine = a high partial
 * (14x) that dies quickly. The brightness envelopes live in o[13..15] and step once a chunk. */
static void ep_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * 14.0f * INV_SR, 0.45f);
    c->ex[2] = (0.3f + c->e[0] * c->e[0] * 2.4f) * (0.4f + 0.6f * v->vel);   /* tone: index */
    c->ex[3] = c->e[1] * 1.2f;                                              /* texture: the 2:1 index */
    c->ex[4] = c->e[2] * 3.0f;                                              /* punch: the burst's index */
    c->ex[5] = c->e[3] * 0.5f;                                              /* tine level */
    c->stereo = 0;
}
static void ep_chunk(voice_t *v)                 /* the brightness envelopes, once a chunk */
{
    v->o[15] = 0.25f + (v->o[15] - 0.25f) * 0.9982f;   /* the index settles over ~0.8 s */
    v->o[14] *= 0.93f;                                 /* the punch burst: ~30 ms */
    v->o[13] = v->o[13] * 0.994f;                      /* the tine partial: ~0.25 s */
}
SY_INLINE void ep_loop(voice_t *v, float *xl, int m, const int has_tine)
{
    const cfg_t *c = &v->c;
    float cp = v->o[0], mp = v->o[1], tp = v->o[2];
    float d = c->ex[0], dt = c->ex[1], idx = c->ex[2] * v->o[15] + c->ex[4] * v->o[14], tex = c->ex[3] * v->o[15];
    float tine = c->ex[5] * v->o[13];
    int i;
    for (i = 0; i < m; i++) {
        float mo;
        mp = step1(mp, d);
        cp = step1(cp, d);
        mo = fm_sin_turns(mp) * idx + fm_sin_turns(mp + mp) * tex;
        xl[i] = fm_sin_turns(cp + mo * 0.25f) * 0.9f;
        if (has_tine) {
            tp = step1(tp, dt);
            xl[i] += fm_sin_turns(tp) * tine;
        }
    }
    v->o[0] = cp;
    v->o[1] = mp;
    v->o[2] = tp;
}
static void ep_run(voice_t *v, float *xl, float *xr, int m)
{
    (void)xr;
    if (v->c.ex[5] * v->o[13] > 0.0005f)        /* the tine partial dies in a quarter second */
        ep_loop(v, xl, m, 1);
    else
        ep_loop(v, xl, m, 0);
}

/* organ: six drawbars (16' 8' 4' 2 2/3' 2' 1 1/3') as rotating phasors (a complex multiply a sine:
 * no polynomial). type picks one of eight registrations, transistor to church; bass 1..8 is the 16'
 * bar; the tremolo's amount and speed are 1..8 (1 = none / slowest) */
static const float ORGAN_SET[8][5] = {{1.0f, 0.15f, 0.0f, 0.6f, 0.0f},    /* 1 transistor: reedy, hollow */
                                      {1.0f, 0.6f, 0.0f, 0.4f, 0.2f},     /* 2 combo: bright, thin */
                                      {1.0f, 0.7f, 0.5f, 0.1f, 0.0f},     /* 3 jazz */
                                      {1.0f, 0.9f, 0.7f, 0.7f, 0.5f},     /* 4 gospel: all bars out */
                                      {1.0f, 0.8f, 0.6f, 0.3f, 0.3f},     /* 5 rock */
                                      {1.0f, 0.5f, 0.35f, 0.5f, 0.6f},    /* 6 theatre */
                                      {1.0f, 0.4f, 0.1f, 0.2f, 0.05f},    /* 7 pipe: flutes */
                                      {1.0f, 0.8f, 0.9f, 0.9f, 0.9f}};    /* 8 church: the full chorus */
static const float ORGAN_RATIO[6] = {0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f};
static inline int knob8(float raw) { int k = (int)(raw + 0.5f); return k < 1 ? 1 : k > 8 ? 8 : k; }
static void organ_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    int k = knob8(c->ek[0]) - 1, j;
    float bass = (float)(knob8(c->ek[1]) - 1) * (1.0f / 7.0f), amt = (float)(knob8(c->ek[2]) - 1) * (1.0f / 7.0f);
    float tr = 0.5f * fm_exp2f((float)(knob8(c->ek[3]) - 1) * (4.0f / 7.0f)) * INV_SR, sum, base;   /* 0.5 .. 8 Hz */
    sum = bass;
    c->ex[0] = bass * 0.8f;
    for (j = 1; j < 6; j++) {
        c->ex[j] = ORGAN_SET[k][j - 1];
        sum += c->ex[j];
    }
    sum = 1.0f / (sum + 0.5f);
    for (j = 0; j < 6; j++)
        c->ex[j] *= sum;
    c->ex[18] = tr;
    c->ex[19] = amt;
    base = c->freq * INV_SR * (1.0f + amt * 0.004f * fm_sin_turns(v->o[12]));   /* the tremolo's pitch wobble */
    for (j = 0; j < 6; j++) {
        float a = fm_minf(base * ORGAN_RATIO[j], 0.45f) * FM_TWO_PI, re = v->o[j * 2], im = v->o[j * 2 + 1], r2;
        c->ex[6 + j] = fm_cosf(a);
        c->ex[12 + j] = fm_sinf(a);
        r2 = re * re + im * im;                  /* renormalise the phasor once a chunk */
        if (r2 > 0.5f) {
            float g = fm_rsqrtf(r2);
            v->o[j * 2] = re * g;
            v->o[j * 2 + 1] = im * g;
        } else {
            v->o[j * 2] = 1.0f;
            v->o[j * 2 + 1] = 0.0f;
        }
    }
    c->stereo = 0;
}
static void organ_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float re[6], im[6];
    const float *cr = c->ex + 6, *ci = c->ex + 12;
    int i, j;
    (void)xr;
    for (j = 0; j < 6; j++) {
        re[j] = v->o[j * 2];
        im[j] = v->o[j * 2 + 1];
    }
    {   /* the tremolo: its gain ramps across the chunk (organ_chunk aims it) */
        float g0 = v->o[13], dg = v->o[14];
        for (i = 0; i < m; i++) {
            float s = 0.0f;
            for (j = 0; j < 6; j++) {
                float nr = re[j] * cr[j] - im[j] * ci[j];
                im[j] = re[j] * ci[j] + im[j] * cr[j];
                re[j] = nr;
                s += im[j] * c->ex[j];
            }
            g0 += dg;
            xl[i] = s * g0;
        }
        v->o[13] = g0;
    }
    for (j = 0; j < 6; j++) {
        v->o[j * 2] = re[j];
        v->o[j * 2 + 1] = im[j];
    }
}
static void organ_chunk(voice_t *v)              /* the tremolo's phase and the chunk's gain ramp */
{
    const cfg_t *c = &v->c;
    float g1;
    v->o[12] = wrap1(v->o[12] + c->ex[18] * (float)SY_CHUNK);
    g1 = 1.0f - c->ex[19] * 0.5f * (0.5f - 0.5f * fm_sin_turns(v->o[12]));
    v->o[14] = (g1 - v->o[13]) * (1.0f / (float)SY_CHUNK);
}

#if SYNTH_ALL_ENGINES
/* hardsync: a saw reset by a master at the note's pitch, freq = the follower's ratio (which harmonics
 * ring), sub = a sine an octave down, noise brightens, lowcut = a high-pass before the filter. The
 * follower's output runs one sample late so both samples around a reset get their PolyBLEP share. */
static void hs_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float hz = 20.0f * fm_exp2f(c->e[3] * 7.2f), g;                         /* 20 Hz .. 2.9 kHz */
    c->ex[0] = fm_minf(c->freq * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * (1.0f + c->e[0] * 7.0f) * INV_SR, 0.45f);
    c->ex[2] = c->e[1] * 0.6f;
    c->ex[3] = c->e[2] * c->e[2] * 0.25f;
    g = fm_tanf(FM_PI * fm_minf(hz, 18000.0f) * INV_SR);
    c->ex[4] = g;
    c->ex[5] = 1.0f / (1.0f + g * (g + 1.41421356f));
    c->ex[6] = c->e[3] > 0.001f;
    c->stereo = 0;
}
static void hs_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float mp = v->o[0], sp = v->o[1], prev = v->o[2], subp = v->o[3], h1 = v->o[4], h2 = v->o[5];
    float dm = c->ex[0], ds = c->ex[1], idm = 1.0f / dm, ids = 1.0f / ds, sub = c->ex[2], nz = c->ex[3];
    float g = c->ex[4], a1 = c->ex[5], a2 = g * a1, hk = 1.41421356f;
    int hp = c->ex[6] > 0.5f, has_sub = sub > 0.001f, has_nz = nz > 0.001f, i;
    (void)xr;
    for (i = 0; i < m; i++) {
        float y, x, t, h, cont;
        mp += dm;
        cont = sp + ds;                          /* the follower as it would run on */
        if (mp >= 1.0f) {                        /* the master wraps: reset the follower t samples ago */
            mp -= 1.0f;
            t = mp * idm;
            sp = t * ds;
            y = 2.0f * sp - 1.0f;
            h = y - (2.0f * cont - 1.0f);        /* the step, to the fresh ramp from the old one */
            prev += h * 0.5f * t * t;
            y -= h * 0.5f * (1.0f - t) * (1.0f - t);
        } else if (cont >= 1.0f) {               /* the follower's own wrap */
            sp = cont - 1.0f;
            t = sp * ids;
            y = 2.0f * sp - 1.0f;
            prev -= t * t;
            y += (1.0f - t) * (1.0f - t);
        } else {
            sp = cont;
            y = 2.0f * sp - 1.0f;
        }
        x = prev;
        prev = y;
        if (has_sub) {
            subp = step1(subp, dm * 0.5f);
            x += fm_sin_turns(subp) * sub;
        }
        if (has_nz)
            x += frand(&v->rng) * nz;
        if (hp) {                                /* 2-pole TPT high-pass */
            float v3 = x - h2, v1 = a1 * h1 + a2 * v3, v2 = h2 + g * v1;
            h1 = 2.0f * v1 - h1;
            h2 = 2.0f * v2 - h2;
            x = x - hk * v1 - v2;
        }
        xl[i] = x;
    }
    v->o[0] = mp;
    v->o[1] = sp;
    v->o[2] = prev;
    v->o[3] = subp;
    v->o[4] = fm_flush(h1);
    v->o[5] = fm_flush(h2);
}

/* swarm: two sines a detune apart, their pitch pushed around by low-passed noise (swarm), a 1:1
 * modulator for tone (fm) that also rings their level (am: half-wave features, grit) */
static void dis_prep(voice_t *v)
{
    cfg_t *c = &v->c;
    float sw = c->e[0], det = c->e[3] * 0.04f;
    c->ex[0] = fm_minf(c->freq * (1.0f - det) * INV_SR, 0.45f);
    c->ex[1] = fm_minf(c->freq * (1.0f + det) * INV_SR, 0.45f);
    c->ex[2] = 0.002f + sw * sw * 0.3f;          /* the noise's lowpass: slow drifts .. a hiss */
    c->ex[3] = sw * 0.8f;                        /* pitch swing, as a fraction of the increment (< 1: the step wraps once) */
    c->ex[4] = c->e[1];                          /* am depth */
    c->ex[5] = c->e[2] * c->e[2] * 3.0f;         /* fm index */
    c->ex[6] = fm_minf(c->freq * INV_SR, 0.45f);
    c->stereo = 0;
}
static void dis_run(voice_t *v, float *xl, float *xr, int m)
{
    const cfg_t *c = &v->c;
    float p1 = v->o[0], p2 = v->o[1], mp = v->o[2], n1 = v->o[3];
    float d1 = c->ex[0], d2 = c->ex[1], k = c->ex[2], sw = c->ex[3], idx = c->ex[5] * 0.25f, dm = c->ex[6];
    float am = c->ex[4] * 0.5f, g0 = 1.0f - am;
    int i;
    (void)xr;
    for (i = 0; i < m; i++) {
        float w, ms, mo;
        n1 += (frand(&v->rng) - n1) * k;
        w = 1.0f + n1 * sw;                      /* -1..1 noise: up to +-80 % of the pitch */
        mp = step1(mp, dm);
        ms = fm_sin_turns(mp);
        mo = ms * idx;
        p1 = step1(p1, d1 * w);
        p2 = step1(p2, d2 * w);
        xl[i] = (fm_sin_turns(p1 + mo) + fm_sin_turns(p2 + mo)) * 0.5f * (g0 + am * ms);
    }
    v->o[0] = p1;
    v->o[1] = p2;
    v->o[2] = mp;
    v->o[3] = n1;
}
#endif

static void eng_chunk(voice_t *v)                /* every chunk: the engines' own slow movements */
{
    switch (v->engine) {
    case EG_DUO: duo_chunk(v); break;
    case EG_EPIANO: ep_chunk(v); break;
    case EG_ORGAN: organ_chunk(v); break;
    default: break;
    }
}
static void eng_prep(voice_t *v)
{
    switch (v->engine) {
    case EG_SIMPLE: simple_prep(v); break;
    case EG_STACK: stack_prep(v); break;
#if SYNTH_ALL_ENGINES
    case EG_WAVETABLE: wt_prep(v); break;
#endif
    case EG_DUO: duo_prep(v); break;
    case EG_EPIANO: ep_prep(v); break;
    case EG_ORGAN: organ_prep(v); break;
#if SYNTH_ALL_ENGINES
    case EG_HARDSYNC: hs_prep(v); break;
    default: dis_prep(v); break;
#else
    default: break;
#endif
    }
}
static void eng_run(voice_t *v, float *xl, float *xr, int m)
{
    switch (v->engine) {
    case EG_SIMPLE: simple_run(v, xl, xr, m); break;
    case EG_STACK: stack_run(v, xl, xr, m); break;
#if SYNTH_ALL_ENGINES
    case EG_WAVETABLE: wt_run(v, xl, xr, m); break;
#endif
    case EG_DUO: duo_run(v, xl, xr, m); break;
    case EG_EPIANO: ep_run(v, xl, xr, m); break;
    case EG_ORGAN: organ_run(v, xl, xr, m); break;
#if SYNTH_ALL_ENGINES
    case EG_HARDSYNC: hs_run(v, xl, xr, m); break;
    default: dis_run(v, xl, xr, m); break;
#else
    default: break;
#endif
    }
}

/* ------------------------------------------------------------ the voice --- */
static inline int v_sounding(const voice_t *v) { return v->amp.stage != ENV_OFF || v->steal_phase != 0; }

static float porta_ms(int glide_ms, int porta) { return glide_ms > 0 ? (float)glide_ms : (float)(porta * porta) * 0.125f; }   /* 0 .. 2 s */

__attribute__((minsize)) static void voice_start(synth_t *s, voice_t *v, int note, int vel, int glide_ms, uint32_t tag, int held, int in_render)
{
    trk_t *t = &s->t[v->trk];
    float ms = porta_ms(glide_ms, P(s, v->trk, TP_PORTA));
    int eng = P(s, v->trk, TP_ENGINE);
    if (eng >= EG_DRUMKIT)
        eng = EG_SIMPLE;
    if (eng != v->engine || !v_sounding(v)) {
        v->engine = (uint8_t)eng;
        eng_init(v);
    }
    v->note = (uint8_t)note;
    v->tag = tag;
    v->vel = (float)vel * (1.0f / 127.0f);
    v->held = (uint8_t)held;
    v->pitch = (float)note;
    v->pitch_cur = (ms > 0.5f && t->last_pitch > 0.0f) ? t->last_pitch : v->pitch;
    v->glide_k = ms > 0.5f ? 1.0f - fm_expf(-(float)SY_CHUNK * 1000.0f / (ms * SY_SR)) : 1.0f;
    if (!in_render)                              /* a render (the steal) writes nothing the tracks share */
        v->age = s->counter++;
    v->amp.stage = ENV_ATTACK;
    v->flt.stage = ENV_ATTACK;
    v->tsec = 0.0f;
    if (v->engine == EG_EPIANO) {
        v->o[15] = 1.0f;
        v->o[14] = 1.0f;
        v->o[13] = 1.0f;
    }
    if (!in_render)
        t->last_pitch = v->pitch;
    v->fresh = 1;
}

static int track_voices(const synth_t *s, int trk)
{
    int i, n = 0;
    for (i = 0; i < s->max_voices; i++)
        n += s->v[i].trk == trk && v_sounding(&s->v[i]);
    return n;
}

/* a voice for a new note: a free one; else the track's oldest if it holds NVOICE_TRK already; else
 * the quietest releasing; else the oldest of all. A stolen voice fades 15 ms first. */
__attribute__((minsize)) static int find_voice(synth_t *s, int trk)
{
    int i, best = -1;
    float quiet = 1e9f;
    uint32_t oldest = 0xFFFFFFFFu;
    if (track_voices(s, trk) >= NVOICE_TRK) {
        for (i = 0; i < s->max_voices; i++)
            if (s->v[i].trk == trk && s->v[i].steal_phase == 0 && s->v[i].age < oldest) {
                oldest = s->v[i].age;
                best = i;
            }
        if (best >= 0)
            return best;
    }
    for (i = 0; i < s->max_voices; i++)
        if (!v_sounding(&s->v[i]))
            return i;
    for (i = 0; i < s->max_voices; i++)
        if (s->v[i].amp.stage == ENV_RELEASE && s->v[i].steal_phase == 0 && s->v[i].amp.level < quiet) {
            quiet = s->v[i].amp.level;
            best = i;
        }
    if (best >= 0)
        return best;
    for (i = 0; i < s->max_voices; i++)
        if (s->v[i].steal_phase == 0 && s->v[i].age < oldest) {
            oldest = s->v[i].age;
            best = i;
        }
    if (best >= 0)
        return best;
    for (i = 0, oldest = 0xFFFFFFFFu; i < s->max_voices; i++)
        if (s->v[i].age < oldest) {
            oldest = s->v[i].age;
            best = i;
        }
    return best >= 0 ? best : 0;
}

static void voice_chunk(synth_t *s, voice_t *v);
static void voice_config_now(synth_t *s, voice_t *v)   /* a voice started mid-chunk: its parameters now */
{
    v->ck = (uint8_t)(v->ck - 1);                /* voice_chunk takes the slot the boundary read */
    v->nchunk = (uint8_t)(v->nchunk - 1);
    voice_chunk(s, v);
}

__attribute__((minsize)) static void voice_take(synth_t *s, int vi, int trk, int note, int vel, int glide_ms, uint32_t tag)
{
    voice_t *v = &s->v[vi];
    if (!v_sounding(v)) {
        int i;
        v->trk = (uint8_t)trk;
        v->steal_gain = 1.0f;
        v->steal_phase = 0;
        v->amp.level = v->flt.level = 0.0f;
        for (i = 0; i < 8; i++)
            v->f[i] = 0.0f;
        v->amp_cur = 0.0f;
        v->glc = v->grc = 0.0f;
        voice_start(s, v, note, vel, glide_ms, tag, 1, 0);
        voice_config_now(s, v);
    } else {                                     /* fade the old note out, then start (voice_steal) */
        v->pend_note = (uint8_t)note;
        v->pend_vel = (uint8_t)vel;
        v->pend_tag = tag;
        v->pend_glide = (int16_t)(glide_ms > 32767 ? 32767 : glide_ms);
        v->pend_held = 1;
        v->trk = (uint8_t)trk;                   /* the track's from now: its note off finds it */
        v->steal_phase = 1;
        v->age = s->counter++;
    }
}

/* ---------------------------------------------------------------- notes --- */
/* a regular (not free) value or random LFO restarts on every note of its track */
static void lfo_retrigger(synth_t *s, int trk)
{
    trk_t *t = &s->t[trk];
    int type = P(s, trk, TP_LTYPE);
    if ((type != LT_VALUE && type != LT_RANDOM) || P(s, trk, TP_LDEST) >= LD_FREE)
        return;
    t->lfo_ph = 0.0f;
    if (type == LT_RANDOM) {
        t->lfo_sh = frand(&t->rng);
        t->lfo_sm = t->lfo_sh;
    }
}

__attribute__((minsize)) void synth_note_on(synth_t *s, int trk, int note, int vel, int glide_ms, uint32_t tag)
{
    trk_t *t;
    int mode;
    if (trk < 0 || trk >= NTRK || note < 0 || note > 127)
        return;
    if (P(s, trk, TP_ENGINE) == EG_DRUMKIT)
        return;                                  /* dsp/drumkit.c plays those */
    if (vel < 1)
        vel = 1;
    if (vel > 127)
        vel = 127;
    t = &s->t[trk];
    mode = P(s, trk, TP_VMODE);
    lfo_retrigger(s, trk);
    if (mode == VM_POLY) {
        voice_take(s, find_voice(s, trk), trk, note, vel, glide_ms, tag);
        return;
    }
    {   /* mono / legato: one voice, a stack of held notes */
        int i, k = -1;
        voice_t *v = t->mono_voice >= 0 ? &s->v[t->mono_voice] : 0;
        for (i = 0; i < t->mono_n; i++)
            if (t->mono_note[i] == note)
                k = i;
        if (k < 0 && t->mono_n < 8) {
            t->mono_note[t->mono_n] = (uint8_t)note;
            t->mono_vel[t->mono_n] = (uint8_t)vel;
            t->mono_tag[t->mono_n] = tag;
            t->mono_n++;
        }
        if (v && v->trk == trk && v_sounding(v) && v->steal_phase == 0) {
            int was_held = v->held;
            float ms = porta_ms(glide_ms, P(s, trk, TP_PORTA));
            v->note = (uint8_t)note;
            v->tag = tag;
            v->vel = (float)vel * (1.0f / 127.0f);
            v->held = 1;
            v->pitch = (float)note;
            v->glide_k = ms > 0.5f ? 1.0f - fm_expf(-(float)SY_CHUNK * 1000.0f / (ms * SY_SR)) : 1.0f;
            t->last_pitch = v->pitch;
            if (mode == VM_MONO || !was_held || v->amp.stage == ENV_RELEASE) {   /* legato: only from silence */
                v->amp.stage = ENV_ATTACK;       /* retrigger from the level now */
                v->flt.stage = ENV_ATTACK;
                v->tsec = 0.0f;
                if (v->engine == EG_EPIANO)
                    v->o[15] = v->o[14] = v->o[13] = 1.0f;
            }
            return;
        }
        i = find_voice(s, trk);
        t->mono_voice = (int8_t)i;
        voice_take(s, i, trk, note, vel, glide_ms, tag);
    }
}

static void voice_release(voice_t *v)
{
    v->held = 0;
    if (v->amp.stage != ENV_OFF && v->amp.stage != ENV_RELEASE) {
        v->amp.stage = ENV_RELEASE;
        v->flt.stage = ENV_RELEASE;
    }
}

__attribute__((minsize)) void synth_note_off(synth_t *s, int trk, uint32_t tag)
{
    trk_t *t;
    int i;
    if (trk < 0 || trk >= NTRK)
        return;
    t = &s->t[trk];
    for (i = 0; i < t->mono_n; i++)              /* the mono stack (kept in every mode: harmless in poly) */
        if (t->mono_tag[i] == tag) {
            int j;
            for (j = i; j < t->mono_n - 1; j++) {
                t->mono_note[j] = t->mono_note[j + 1];
                t->mono_vel[j] = t->mono_vel[j + 1];
                t->mono_tag[j] = t->mono_tag[j + 1];
            }
            t->mono_n--;
            break;
        }
    if (P(s, trk, TP_VMODE) != VM_POLY && t->mono_voice >= 0) {
        voice_t *v = &s->v[t->mono_voice];
        if (v->trk != trk)
            return;
        if (v->steal_phase == 1 && v->pend_tag == tag) {
            v->pend_held = 0;
            return;
        }
        if (v->tag != tag)
            return;
        if (t->mono_n > 0) {                     /* back to the note still held */
            int k = t->mono_n - 1;
            v->note = t->mono_note[k];
            v->tag = t->mono_tag[k];
            v->pitch = (float)v->note;
            t->last_pitch = v->pitch;
        } else {
            voice_release(v);
        }
        return;
    }
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &s->v[i];
        if (v->trk != trk)
            continue;
        if (v->held && v->tag == tag && v->steal_phase != 1)
            voice_release(v);
        if (v->steal_phase == 1 && v->pend_tag == tag)
            v->pend_held = 0;                    /* let go before its steal finished: just its release */
    }
}

void synth_all_off(synth_t *s, int trk)
{
    int i;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &s->v[i];
        if (trk >= 0 && v->trk != trk)
            continue;
        voice_release(v);
        if (v->steal_phase == 1)
            v->pend_held = 0;
    }
    for (i = 0; i < NTRK; i++)
        if (trk < 0 || i == trk) {
            s->t[i].mono_n = 0;
            s->t[i].mono_voice = -1;
        }
}

void synth_bend(synth_t *s, int trk, int cents)
{
    if (trk >= 0 && trk < NTRK)
        s->t[trk].bend_c = (int16_t)(cents < -32000 ? -32000 : cents > 32000 ? 32000 : cents);
}
perf_t *synth_perf(synth_t *s, int trk) { return &s->t[trk < 0 ? 0 : trk >= NTRK ? NTRK - 1 : trk].perf; }
void synth_lock(synth_t *s, int trk, int param, int val)
{
    if (trk >= 0 && trk < NTRK && param >= 0 && param < NTP)
        s->t[trk].lock[param] = (uint8_t)(val < 0 ? 0 : val > 255 ? 255 : val);
}
void synth_tempo(synth_t *s, float bpm) { s->bpm = bpm < 20.0f ? 20.0f : bpm > 400.0f ? 400.0f : bpm; }
void synth_limit(synth_t *s, int max_voices)
{
    s->max_voices = (uint8_t)(max_voices < 1 ? 1 : max_voices > NVOICE ? NVOICE : max_voices);
}
void synth_duck_trigger(synth_t *s, int src)
{
    int i;
    for (i = 0; i < NTRK; i++)
        if (P(s, i, TP_LTYPE) == LT_DUCK && P(s, i, TP_LSPEED) == src) {
            s->t[i].duck = 1.0f;
            s->t[i].duck_hold = env_seconds((float)P(s, i, TP_LDEST) * (1.0f / 127.0f));
        }
}

__attribute__((minsize)) void synth_init(synth_t *s, uint8_t (*par)[NTP])
{
    int i, j;
    uint8_t *b = (uint8_t *)s;
    for (i = 0; i < (int)sizeof *s; i++)
        b[i] = 0;
    s->par = par;
    s->bpm = 120.0f;
    s->max_voices = NVOICE;
    for (i = 0; i < NTRK; i++) {
        trk_t *t = &s->t[i];
        for (j = 0; j < NTP; j++)
            t->lock[j] = LOCK_NONE;
        t->mono_voice = -1;
        t->rng = 0x9E3779B9u * (uint32_t)(i + 1);
        t->ldest = LD_NONE;
        for (j = 0; j < NCK; j++)
            t->lgain[j] = 1.0f;
    }
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &s->v[i];
        v->rng = 0x2545F491u * (uint32_t)(i + 1) + 1u;
        v->steal_gain = 1.0f;
        v->engine = EG_SIMPLE;
        for (j = 0; j < 8; j++)
            v->env_in[j] = -1.0f;                /* the caches: nothing made yet */
        v->pan_in = v->pitch_in = -1.0f;
        eng_init(v);
    }
    for (i = 0; i < NTRK; i++)
        synth_peak[i] = 0;
}

/* -------------------------------------------------------------- prepare --- */
static inline float lfo_wave_value(int shape, float ph)   /* square, sine, ramp, saw */
{
    switch (shape & 3) {
    case 0: return ph < 0.5f ? 1.0f : -1.0f;
    case 1: return fm_sin_turns(ph);
    case 2: return 2.0f * ph - 1.0f;
    default: return 1.0f - 2.0f * ph;
    }
}
static inline float lfo_wave_trem(int shape, float ph)    /* sine, saw, exponential, square, blip */
{
    switch (shape) {
    case 1: return 1.0f - 2.0f * ph;
    case 2: return 2.0f * fm_exp2f(-8.0f * ph) - 1.0f;
    case 3: return ph < 0.5f ? 1.0f : -1.0f;
    case 4: return ph < 0.08f ? 1.0f : -1.0f;
    default: return fm_sin_turns(ph);
    }
}

/* the track-level values of every chunk boundary inside the next n frames: the LFO (value, random:
 * an offset on its destination; tremolo: a gain and a vibrato; duck: a gain), one slot each. The
 * voices read the slots in order as they cross the boundaries. Also the meters. */
__attribute__((minsize)) void synth_prepare(synth_t *s, uint32_t n)
{
    int i, k, nck = 0;
    uint32_t off = (SY_CHUNK - s->pos) % SY_CHUNK;
    for (; off < n && nck < NCK; off += SY_CHUNK)
        nck++;
    s->nck = (uint8_t)(nck ? nck : 1);
    for (i = 0; i < NTRK; i++) {
        trk_t *t = &s->t[i];
        int type = P(s, i, TP_LTYPE), lx = P(s, i, TP_LX), lx2 = P(s, i, TP_LX2), engine = P(s, i, TP_ENGINE);
        float hz = type == LT_DUCK ? 0.0f : lfo_hz(P(s, i, TP_LSPEED), s->bpm), dph = hz * CHUNK_S;
        float amt = (float)P(s, i, TP_LAMT), pk = 0.0f, vib = 0.0f, vol = 0.0f;
        t->ldest = LD_NONE;
        t->lscale = 0.0f;
        t->lf_in = t->lf_out = 0.0f;
        if (type == LT_VALUE || type == LT_RANDOM) {
            int d = LD_TP[P(s, i, TP_LDEST) & 3][lx & 3];
            t->ldest = (int16_t)d;
            if (d >= 0) {
                const tp_info_t *in = tp_info(d, engine);
                t->lscale = (float)(in->hi - in->lo) * (1.0f / 127.0f);
            } else if (d == LD_PITCH) {
                t->lscale = 1.0f;
            }
            if (type == LT_RANDOM)
                fade_secs(lx2, &t->lf_in, &t->lf_out);
        } else if (type == LT_TREMOLO) {
            fade_secs(lx, &t->lf_in, &t->lf_out);
            vib = (amt - 64.0f) * (100.0f / 63.0f);                 /* cents */
            vol = ((float)P(s, i, TP_LDEST) - 64.0f) * (1.0f / 63.0f);
        }
        for (k = 0; k < nck; k++) {
            float ph, val = 0.0f, pitch = 0.0f, gain = 1.0f;
            ph = t->lfo_ph + dph;
            if (ph >= 1.0f) {
                ph -= 1.0f;
                t->lfo_sh = frand(&t->rng);      /* a fresh sample on every cycle */
            }
            t->lfo_ph = ph;
            switch (type) {
            case LT_VALUE:
                val = lfo_wave_value(lx2, ph) * (amt - 64.0f) * (127.0f / 63.0f) * t->lscale;
                break;
            case LT_RANDOM: {                    /* a wander: slews to each cycle's sample within a quarter cycle */
                float sm = fm_minf(4.0f * dph, 1.0f);
                t->lfo_sm += (t->lfo_sh - t->lfo_sm) * sm;
                val = (0.5f + 0.5f * t->lfo_sm) * amt * t->lscale;
                break;
            }
            case LT_TREMOLO: {
                float w = lfo_wave_trem(lx2, ph);
                pitch = w * vib;
                gain = vol >= 0.0f ? 1.0f - vol * (0.5f - 0.5f * w) : 1.0f + vol * (0.5f + 0.5f * w);
                break;
            }
            case LT_DUCK: {
                if (t->duck_hold > 0.0f)
                    t->duck_hold -= CHUNK_S;
                else
                    t->duck *= fm_expf(-6.9f * CHUNK_S / env_seconds((float)lx * (1.0f / 127.0f)));
                gain = 1.0f - amt * (1.0f / 127.0f) * t->duck;
                break;
            }
            default: break;
            }
            t->loff[k] = val;
            t->lpitch[k] = pitch;
            t->lgain[k] = gain;
        }
        for (k = 0; k < NVOICE; k++)             /* the meter: the loudest voice of the track */
            if (s->v[k].trk == i) {
                if (s->v[k].peak > pk)
                    pk = s->v[k].peak;
                s->v[k].peak = 0.0f;
            }
        {
            uint32_t q = (uint32_t)(fm_minf(pk, 1.0f) * 32767.0f), old = synth_peak[i];
            old = old - (old >> 4);              /* the fall: about 1/16 a block */
            synth_peak[i] = (uint16_t)(q > old ? q : old);
        }
    }
    for (k = 0; k < NVOICE; k++)
        s->v[k].ck = 0;
    s->pos = (uint16_t)((s->pos + n) % SY_CHUNK);
}

/* ------------------------------------------------------------ the chunk --- */
static void voice_pitch(synth_t *s, voice_t *v)
{
    (void)s;
    v->c.freq = 440.0f * fm_exp2f((v->pitch_cur - 69.0f + v->c.pitch_off * 0.01f) * (1.0f / 12.0f));
    if (v->c.freq > 18000.0f)
        v->c.freq = 18000.0f;
    eng_prep(v);
}

/* a chunk boundary: the parameters into c */
__attribute__((minsize)) static void voice_chunk(synth_t *s, voice_t *v)
{
    trk_t *t = &s->t[v->trk];
    cfg_t *c = &v->c;
    int trk = v->trk, k = v->ck < s->nck ? v->ck : s->nck - 1, dest = t->ldest, i;
    float lenv = 1.0f, off, lg, vol, lev, pan, vg, res;
    v->ck = (uint8_t)(v->ck + 1);
    v->nchunk = (uint8_t)(v->nchunk + 1);
    if (!v_sounding(v))
        return;
    /* the LFO's fade envelope: this voice's time since its note */
    if (t->lf_in > 0.0f)
        lenv = fm_minf(v->tsec / t->lf_in, 1.0f);
    else if (t->lf_out > 0.0f)
        lenv = fm_maxf(1.0f - v->tsec / t->lf_out, 0.0f);
    v->tsec += CHUNK_S;
    off = t->loff[k] * lenv;
    lg = 1.0f - (1.0f - t->lgain[k]) * lenv;
    for (i = 0; i < 4; i++) {
        c->ek[i] = fm_minf(pf(s, trk, TP_E1 + i, dest, off), 127.0f);   /* a foreign lock value stays in range */
        c->e[i] = c->ek[i] * (1.0f / 127.0f);
    }
    {   /* the envelopes' coefficients cost an exp each: only for a knob that moved */
        static const uint8_t EP[8] = {TP_A, TP_D, TP_S, TP_R, TP_FA, TP_FD, TP_FS, TP_FR};
        float *co[8];
        co[0] = &c->amp_ar; co[1] = &c->amp_dc; co[2] = &c->amp_sl; co[3] = &c->amp_rc;
        co[4] = &c->flt_ar; co[5] = &c->flt_dc; co[6] = &c->flt_sl; co[7] = &c->flt_rc;
        for (i = 0; i < 8; i++) {
            float x = pf(s, trk, EP[i], dest, off) * (1.0f / 127.0f);
            if (x == v->env_in[i])
                continue;
            v->env_in[i] = x;
            *co[i] = (i & 3) == 0 ? env_rate(env_seconds(x)) : (i & 3) == 2 ? x : env_coef(env_seconds(x));
        }
    }
    /* the filter: 50 Hz .. 20 kHz over the knob, the performance offset, keytrack, velocity */
    c->cut_oct = (pf(s, trk, TP_CUT, dest, off) + (float)t->perf.cut) * (8.64385619f / 127.0f)
                 + pf(s, trk, TP_KTRK, dest, off) * (1.0f / 127.0f) * (v->pitch - 60.0f) * (1.0f / 12.0f);
    c->fenv_oct = (pf(s, trk, TP_FENV, dest, off) - 64.0f) * (6.0f / 64.0f) * (0.5f + 0.5f * v->vel);
    res = pf(s, trk, TP_RES, dest, off) * (1.0f / 127.0f);
    c->kq = 2.0f * fm_exp2f(-res * 4.3f);        /* Q 0.5 .. 10 */
    c->kres = res * 4.0f;                        /* the ladder's feedback */
    c->ftype = (uint8_t)P(s, trk, TP_FTYPE);
    if (c->ftype >= NFT)
        c->ftype = FT_SVF;
    c->mute = t->perf.mute;
    /* gains */
    vol = pf(s, trk, TP_VOLUME, dest, off) * (1.0f / 127.0f);
    lev = (float)P(s, trk, TP_LEVEL) * (1.0f / 127.0f);
    vg = (0.25f + 0.75f * v->vel * v->vel) * vol * vol * lev * lg * (1.0f - 0.6f * res);
    pan = fm_clampf((pf(s, trk, TP_PAN, dest, off) - 64.0f + (float)t->perf.pan) * (1.0f / 64.0f), -1.0f, 1.0f);
    if (pan != v->pan_in || v->fresh) {          /* the pan's cos / sin: only when it moved */
        v->pan_in = pan;
        v->pan_l = pan == 0.0f ? 0.70710678f : fm_cosf((pan + 1.0f) * 0.78539816f);   /* centre: exactly equal */
        v->pan_r = pan == 0.0f ? 0.70710678f : fm_sinf((pan + 1.0f) * 0.78539816f);
    }
    c->gl = v->pan_l * vg;
    c->gr = v->pan_r * vg;
    c->g1 = fm_minf((float)(P(s, trk, TP_SEND1) + t->perf.send1), 127.0f) * (1.0f / 127.0f);
    c->g2 = fm_minf((float)(P(s, trk, TP_SEND2) + t->perf.send2), 127.0f) * (1.0f / 127.0f);
    c->gt = (float)P(s, trk, TP_SENDT) * (1.0f / 127.0f);
    /* pitch: the glide, bend, performance pitch, vibrato, an LFO on the pitch (+-12 semitones at 100 %) */
    v->pitch_cur += (v->pitch - v->pitch_cur) * v->glide_k;
    c->pitch_off = (float)t->bend_c + (float)t->perf.pitch_c + t->lpitch[k] * lenv;
    if (dest == LD_PITCH)
        c->pitch_off += off * (1200.0f / 127.0f);
    {   /* the frequency and the engine's increments: when the pitch or a knob moved (or every 4th
         * chunk: the organ's pitch wobble and phasor trim) */
        float pin = v->pitch_cur + c->pitch_off * 0.01f;
        int moved = pin != v->pitch_in || v->fresh || (v->nchunk & 3) == 0;
        for (i = 0; i < 4 && !moved; i++)
            moved = c->e[i] != v->e_in[i];
        if (moved) {
            v->pitch_in = pin;
            for (i = 0; i < 4; i++)
                v->e_in[i] = c->e[i];
            voice_pitch(s, v);
        }
    }
    eng_chunk(v);
}

/* the steal's turn: the old note has faded; start the pending one here, mid-chunk */
static void voice_steal(synth_t *s, voice_t *v)
{
    int held = v->pend_held;
    v->amp.stage = ENV_OFF;                      /* so voice_start sees a fresh voice */
    v->amp.level = v->flt.level = 0.0f;
    v->steal_phase = 2;
    v->amp_cur = 0.0f;
    voice_start(s, v, v->pend_note, v->pend_vel, v->pend_glide, v->pend_tag, held, 1);
    v->pitch_cur = v->pitch;                     /* a stolen voice does not glide from the old note */
    if (!held)
        voice_release(v);
    voice_config_now(s, v);
}

/* a sub-block boundary (SUB samples, aligned in the chunk, or a voice configured mid-block): the
 * envelopes step, the filter's coefficients follow the filter envelope, the ramps (amp level, steal
 * fade, dry gains) aim at their next values. Between boundaries only the per-sample state moves. */
static void voice_subblock(synth_t *s, voice_t *v, int aligned)
{
    cfg_t *c;
    float hz, g, gl, gr;
    const float hk = 1.41421356f;
    if (v->steal_phase == 1 && v->steal_gain <= 0.0f) {   /* the old note has faded: the new one starts */
        int i;
        voice_steal(s, v);
        for (i = 0; i < 8; i++)
            v->f[i] = 0.0f;
    }
    c = &v->c;
    v->fresh = 0;
    if (aligned) {
        env_step(&v->amp, c->amp_ar, c->amp_dc, c->amp_sl, c->amp_rc);
        env_step(&v->flt, c->flt_ar, c->flt_dc, c->flt_sl, c->flt_rc);
    }
    hz = 50.0f * fm_exp2f(c->cut_oct + v->flt.level * c->fenv_oct);
    hz = hz > 20000.0f ? 20000.0f : hz < 50.0f ? 50.0f : hz;
    if (c->ftype == FT_LADDER) {                 /* four one-poles: g per pole, k the feedback */
        v->b1 = fm_minf(1.0f - fm_expf(-FM_TWO_PI * hz * INV_SR), 0.9f);
        v->b2 = c->kres;
    } else {
        g = fm_tanf(FM_PI * fm_minf(hz, 0.45f * SY_SR) * INV_SR);
        v->a1 = 1.0f / (1.0f + g * (g + c->kq));
        v->a2 = g * v->a1;
        v->a3 = g * v->a2;
        if (c->ftype == FT_SVF) {
            v->b1 = 1.0f / (1.0f + g * (g + hk));
            v->b2 = g * v->b1;
            v->b3 = g * v->b2;
        }
    }
    v->amp_step = (v->amp.level - v->amp_cur) * (1.0f / (float)SUB);
    if (v->steal_phase == 1) {
        v->sg_step = v->steal_gain > STEAL_RATE * (float)SUB ? -STEAL_RATE : -v->steal_gain * (1.0f / (float)SUB);
    } else if (v->steal_phase == 2) {
        if (v->steal_gain + STEAL_RATE * (float)SUB >= 1.0f) {
            v->sg_step = (1.0f - v->steal_gain) * (1.0f / (float)SUB);
            v->steal_phase = 0;                  /* reaches 1 at the end of this block */
        } else {
            v->sg_step = STEAL_RATE;
        }
    } else {
        v->sg_step = 0.0f;
    }
    gl = c->mute ? 0.0f : c->gl;
    gr = c->mute ? 0.0f : c->gr;
    v->gl_step = (gl - v->glc) * (0.35f / (float)SUB);
    v->gr_step = (gr - v->grc) * (0.35f / (float)SUB);
}

/* the sample loop for one sub-block: the filter (one of five), the ramps, the outputs. Instantiated
 * for each filter type and for mono / stereo with constant arguments, so the compiler folds the
 * branches away; the generic switch is in voice_samples.
 *   svf:       a TPT state-variable low-pass (resonant) into a second, flat one: 24 dB, aggressive
 *   ladder:    four one-poles in series with the last one's output fed back through a soft clip
 *   z lowpass: the one resonant TPT stage, 12 dB
 *   z hipass:  its high-pass output, 12 dB
 *   dynamic:   the drum tracks' filter (drumkit.c): on a synth voice it is a bypass */
SY_INLINE void sample_loop(voice_t *v, const float *xl, const float *xr, float *l, float *r, float *s1, float *s2,
                               float *st, int k, const int ftype, const int stereo)
{
    const cfg_t *c = &v->c;
    const float a1 = v->a1, a2 = v->a2, a3 = v->a3, b1 = v->b1, b2 = v->b2, b3 = v->b3, kq = c->kq;
    const float es = v->amp_step, sgs = v->sg_step, gls = v->gl_step, grs = v->gr_step;
    const float g1 = c->g1, g2 = c->g2, gt = c->gt, wide = stereo ? 1.41421356f : 1.0f;
    const float lcomp = 1.0f + b2 * 0.25f;       /* the ladder: its passband falls as the feedback rises */
    const int sends = g1 > 0.0f || g2 > 0.0f || gt > 0.0f;
    float f0 = v->f[0], f1 = v->f[1], f2 = v->f[2], f3 = v->f[3], f4 = v->f[4], f5 = v->f[5], f6 = v->f[6], f7 = v->f[7];
    float e = v->amp_cur, sg = v->steal_gain, glc = v->glc, grc = v->grc, pk = 0.0f;
    int j;
    for (j = 0; j < k; j++) {
        float yl, yr, ol, or_, a;
        if (ftype == FT_LADDER) {
            float x = xl[j] - b2 * soft3(f3);
            f0 += b1 * (x - f0);
            f1 += b1 * (f0 - f1);
            f2 += b1 * (f1 - f2);
            f3 += b1 * (f2 - f3);
            yl = f3 * lcomp;
        } else if (ftype == FT_DYNAMIC) {
            yl = xl[j];
        } else {
            float t3 = xl[j] - f1, t1 = a1 * f0 + a2 * t3, t2 = f1 + a2 * f0 + a3 * t3;
            f0 = 2.0f * t1 - f0;
            f1 = 2.0f * t2 - f1;
            yl = ftype == FT_ZHP ? xl[j] - kq * t1 - t2 : t2;
            if (ftype == FT_SVF) {
                float w3 = yl - f3, w1 = b1 * f2 + b2 * w3, w2 = f3 + b2 * f2 + b3 * w3;
                f2 = 2.0f * w1 - f2;
                f3 = 2.0f * w2 - f3;
                yl = w2;
            }
        }
        if (stereo) {
            if (ftype == FT_LADDER) {
                float x = xr[j] - b2 * soft3(f7);
                f4 += b1 * (x - f4);
                f5 += b1 * (f4 - f5);
                f6 += b1 * (f5 - f6);
                f7 += b1 * (f6 - f7);
                yr = f7 * lcomp;
            } else if (ftype == FT_DYNAMIC) {
                yr = xr[j];
            } else {
                float u3 = xr[j] - f5, u1 = a1 * f4 + a2 * u3, u2 = f5 + a2 * f4 + a3 * u3;
                f4 = 2.0f * u1 - f4;
                f5 = 2.0f * u2 - f5;
                yr = ftype == FT_ZHP ? xr[j] - kq * u1 - u2 : u2;
                if (ftype == FT_SVF) {
                    float w3 = yr - f7, w1 = b1 * f6 + b2 * w3, w2 = f7 + b2 * f6 + b3 * w3;
                    f6 = 2.0f * w1 - f6;
                    f7 = 2.0f * w2 - f7;
                    yr = w2;
                }
            }
        } else {
            yr = yl;
        }
        e += es;
        sg += sgs;
        glc += gls;
        grc += grs;
        a = e * sg * wide;
        ol = yl * a * glc;
        or_ = yr * a * grc;
        l[j] += ol;
        r[j] += or_;
        if (sends) {
            float mono = (ol + or_) * 0.5f;
            s1[j] += mono * g1;
            s2[j] += mono * g2;
            st[j] += mono * gt;
        }
        yl = fm_fabsf(yl);
        if (yl > pk)
            pk = yl;
    }
    v->amp_cur = e;
    v->steal_gain = sg;
    v->glc = glc;
    v->grc = grc;
    pk *= e * sg * wide * (glc > grc ? glc : grc);   /* the meter: this block's loudest, roughly */
    if (pk > v->peak)
        v->peak = pk;
    v->f[0] = fm_flush(f0); v->f[1] = fm_flush(f1); v->f[2] = fm_flush(f2); v->f[3] = fm_flush(f3);
    if (stereo) {
        v->f[4] = fm_flush(f4); v->f[5] = fm_flush(f5); v->f[6] = fm_flush(f6); v->f[7] = fm_flush(f7);
    }
}

/* m samples (inside a chunk) of one voice into the buffers */
static void voice_samples(synth_t *s, voice_t *v, float *l, float *r, float *s1, float *s2, float *st, int m)
{
    float xl[SUB], xr[SUB];
    int i = 0;
    while (i < m) {
        int k = SUB - (v->pos & (SUB - 1));
        if (k > m - i)
            k = m - i;
        if ((v->pos & (SUB - 1)) == 0 || v->fresh)
            voice_subblock(s, v, (v->pos & (SUB - 1)) == 0);
        eng_run(v, xl, xr, k);
        switch (v->c.ftype * 2 + (v->c.stereo != 0)) {
        case FT_SVF * 2: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_SVF, 0); break;
        case FT_SVF * 2 + 1: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_SVF, 1); break;
        case FT_LADDER * 2: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_LADDER, 0); break;
        case FT_LADDER * 2 + 1: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_LADDER, 1); break;
        case FT_ZLP * 2: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_ZLP, 0); break;
        case FT_ZLP * 2 + 1: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_ZLP, 1); break;
        case FT_ZHP * 2: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_ZHP, 0); break;
        case FT_ZHP * 2 + 1: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_ZHP, 1); break;
        case FT_DYNAMIC * 2: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_DYNAMIC, 0); break;
        default: sample_loop(v, xl, xr, l + i, r + i, s1 + i, s2 + i, st + i, k, FT_DYNAMIC, 1); break;
        }
        i += k;
        v->pos = (uint16_t)(v->pos + k);
    }
}

void synth_render(synth_t *s, int v0, int v1, float *l, float *r, float *s1, float *s2, float *st, uint32_t n)
{
    int vi;
    if (v0 < 0)
        v0 = 0;
    if (v1 > NVOICE)
        v1 = NVOICE;
    for (vi = v0; vi < v1; vi++) {
        voice_t *v = &s->v[vi];
        uint32_t i = 0;
        while (i < n) {
            uint32_t m = SY_CHUNK - v->pos;
            if (m > n - i)
                m = n - i;
            if (v->pos == 0)
                voice_chunk(s, v);
            if (v_sounding(v))
                voice_samples(s, v, l + i, r + i, s1 + i, s2 + i, st + i, (int)m);
            else
                v->pos = (uint16_t)(v->pos + m);
            if (v->pos >= SY_CHUNK)
                v->pos = 0;
            i += m;
        }
    }
}

int synth_sounding(const synth_t *s, int v0, int v1)
{
    int i;
    for (i = v0 < 0 ? 0 : v0; i < v1 && i < NVOICE; i++)
        if (v_sounding(&s->v[i]))
            return 1;
    return 0;
}
int synth_voices_used(const synth_t *s)
{
    int i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += v_sounding(&s->v[i]);
    return n;
}

/* ------------------------------------------------------- parameter info --- */
static const tp_info_t FIXED[NTP_SOUND + 13] = {
    {"engine", 0, NENGINE - 1, 0, PK_NAMES, EG_NAME}, {"", 0, 127, 0, PK_NN, 0}, {"", 0, 127, 0, PK_NN, 0},
    {"", 0, 127, 0, PK_NN, 0},                          {"", 0, 127, 0, PK_NN, 0},
    {"mode", 0, 2, VM_POLY, PK_NAMES, VMODE_N},         {"porta", 0, 127, 0, PK_NUM, 0},
    {"bend", 0, 8, 2, PK_NAMES, BEND_N},                {"volume", 0, 127, 100, PK_PCT, 0},
    {"attack", 0, 127, 0, PK_MS, 0},                    {"decay", 0, 127, 60, PK_MS, 0},
    {"sustain", 0, 127, 100, PK_PCT, 0},                {"release", 0, 127, 40, PK_MS, 0},
    {"attack", 0, 127, 0, PK_MS, 0},                    {"decay", 0, 127, 60, PK_MS, 0},
    {"sustain", 0, 127, 0, PK_PCT, 0},                  {"release", 0, 127, 40, PK_MS, 0},
    {"cutoff", 0, 127, 110, PK_HZ, 0},                  {"reso", 0, 127, 20, PK_PCT, 0},
    {"env", 0, 127, 64, PK_BIP, 0},                     {"keytrack", 0, 127, 64, PK_PCT, 0},
    {"type", 0, NFT - 1, FT_SVF, PK_NAMES, FTYPE_N},    {"fx I", 0, 127, 0, PK_PCT, 0},
    {"fx II", 0, 127, 0, PK_PCT, 0},                    {"tape", 0, 127, 0, PK_PCT, 0},
    {"type", 0, NLT - 1, LT_OFF, PK_NAMES, LTYPE_N},    {"speed", 0, 127, 3, PK_HZ_SYNC, 0},
    {"amount", 0, 127, 64, PK_BIP, 0},                  {"dest", 0, NLD - 1, LD_FILTER, PK_NAMES, LDEST_N},
    {"param", 0, 3, 0, PK_NUM, 0},                      {"shape", 0, 3, 1, PK_NAMES, VSHAPE_N},
    /* TP_PLAYER .. TP_CHAN */
    {"player", 0, NPL - 1, PL_OFF, PK_NAMES, PLAYER_N}, {"speed", 0, 8, 4, PK_NAMES, ARPSPD_N},
    {"pattern", 0, 5, 0, PK_NAMES, ARPPAT_N},           {"range", 1, 4, 1, PK_NUM, 0},
    {"hold", 0, 1, 0, PK_SWITCH, 0},                    {"length", 0, 2, 1, PK_NAMES, ARPLEN_N},
    {"style", 0, 5, 0, PK_NAMES, ARPSTY_N},             {"glide", 0, 2, 0, PK_NAMES, ARPGLD_N},
    {"stereo", 0, 2, 0, PK_NAMES, ARPSTE_N},            {"level", 0, 127, 100, PK_PCT, 0},
    {"pan", 0, 127, 64, PK_BIP, 0},                     {"follow", 0, 2, 1, PK_NAMES, FOLLOW_N},
    {"channel", 0, 16, 0, PK_NUM, 0}};
/* a drumkit key's nine values; its model's knobs take their names and defaults from drumkit.c (tp_info_par) */
static const tp_info_t DRUMP[DP_N] = {{"model", 0, NDM - 1, 0, PK_NAMES, DM_NAME}, {"knob 1", 0, 127, 64, PK_NN, 0},
                                      {"knob 2", 0, 127, 64, PK_NN, 0}, {"knob 3", 0, 127, 64, PK_NN, 0},
                                      {"knob 4", 0, 127, 64, PK_NN, 0}, {"random", 0, 127, 0, PK_PCT, 0},
                                      {"pan", 0, 127, 64, PK_BIP, 0}, {"volume", 0, 127, 76, PK_DB, 0},
                                      {"mode", 0, NPM - 1, PM_ONESHOT, PK_NAMES, PM_NAME}};
/* a drumkit track's Envelope and Filter pages */
static const tp_info_t DRUMENV[4] = {{"funk", 0, 127, 0, PK_PCT, 0}, {"attack", 0, 127, 0, PK_PCT, 0},
                                     {"random", 0, 127, 0, PK_PCT, 0}, {"length", 0, 127, 64, PK_PCT, 0}};
static const tp_info_t DRUMFLT[4] = {{"lowcut", 0, 127, 0, PK_HZ, 0}, {"comp", 0, 127, 0, PK_PCT, 0},
                                     {"drive", 0, 127, 0, PK_PCT, 0}, {"hicut", 0, 127, 127, PK_HZ, 0}};
static const tp_info_t NONE_P = {"-", 0, 0, 0, PK_NUM, 0};
static const tp_info_t ROLL_P[4] = {{"roll", 0, 63, 30, PK_NUM, 0}, {"pattern", 0, 3, 0, PK_NAMES, MPAT_N},
                                       {"-", 0, 0, 0, PK_NUM, 0}, {"hold", 0, 1, 0, PK_SWITCH, 0}};
/* the LFO's knobs per type (synth.h): speed / amount / dest / x / x2 */
static const tp_info_t LFO_SPEED_DUCK = {"source", 0, 11, 0, PK_NAMES, DUCK_N};
static const tp_info_t LFO_AMT[NLT] = {{"-", 0, 0, 0, PK_NUM, 0}, {"amount", 0, 127, 64, PK_BIP, 0},
                                       {"vibrato", 0, 127, 64, PK_BIP, 0}, {"amount", 0, 127, 64, PK_PCT, 0},
                                       {"amount", 0, 127, 80, PK_PCT, 0}};
static const tp_info_t LFO_DEST_TREM = {"volume", 0, 127, 96, PK_BIP, 0};
static const tp_info_t LFO_DEST_DUCK = {"hold", 0, 127, 54, PK_MS, 0};
static const tp_info_t LFO_X_TREM = {"envelope", 0, 127, 64, PK_FADE, 0};
static const tp_info_t LFO_X_DUCK = {"release", 0, 127, 76, PK_MS, 0};
static const tp_info_t LFO_X2_TREM = {"shape", 0, 4, 0, PK_NAMES, TSHAPE_N};
static const tp_info_t LFO_X2_RAND = {"envelope", 0, 127, 64, PK_FADE, 0};
static const tp_info_t LFO_PARAM_ENV = {"param", 0, 3, 0, PK_NAMES, ENVP_N};
static const tp_info_t LFO_PARAM_FLT = {"param", 0, 3, 0, PK_NAMES, FLTP_N};
static const tp_info_t LFO_PARAM_AMP = {"param", 0, 3, 0, PK_NAMES, AMPP_N};
static tp_info_t eknob[NENGINE][4];
static tp_info_t lparam[NENGINE];                /* the LFO's parameter knob on the synth module: the engine's names */
static tp_info_t dknob[NDM][4];                  /* a drum model's four knobs */
static uint8_t eknob_ready;

__attribute__((minsize)) static void eknob_init(void)
{
    int e, k;
    for (e = 0; e < NENGINE; e++) {
        for (k = 0; k < 4; k++) {
            tp_info_t *i = &eknob[e][k];
            i->name = EG_KNOB[e][k];
            i->lo = 0;
            i->hi = 127;
            i->def = EG_DEF[e][k];
            i->kind = PK_NN;
            i->names = 0;
#if SYNTH_ALL_ENGINES
            if (e == EG_WAVETABLE && k == 0) {
                i->hi = WT_NTABLE - 1;
                i->kind = PK_NAMES;
                i->names = WTNAME_N;
            }
#endif
            if ((e == EG_STACK && k == 1) || e == EG_ORGAN) {   /* the discrete 1..8 knobs */
                i->lo = 1;
                i->hi = 8;
                i->kind = PK_NUM;              /* stack's ratio prints "N:1" (tp_text) */
            }
        }
        lparam[e] = LFO_PARAM_ENV;
        lparam[e].names = EG_KNOB[e];
    }
    for (e = 0; e < NDM; e++)
        for (k = 0; k < 4; k++) {
            dknob[e][k] = DRUMP[DP_K1 + k];
            dknob[e][k].name = DM_KNOB[e][k];
            dknob[e][k].def = DM_DEFAULT[e][k];
        }
    eknob_ready = 1;
}

const tp_info_t *tp_info(int p, int engine)
{
    if (!eknob_ready)
        eknob_init();
    if (p >= TP_E1 && p <= TP_E4) {
        if (engine < 0 || engine >= NENGINE)
            engine = EG_SIMPLE;
        return &eknob[engine][p - TP_E1];
    }
    if (engine == EG_DRUMKIT) {
        if (p >= TP_A && p <= TP_R)
            return &DRUMENV[p - TP_A];
        if (p >= TP_CUT && p <= TP_KTRK)
            return &DRUMFLT[p - TP_CUT];
    }
    if (p >= TP_DRUM0 && p < NTP)
        return &DRUMP[(p - TP_DRUM0) % DP_N];
    if (p >= 0 && p < (int)(sizeof FIXED / sizeof FIXED[0]))
        return &FIXED[p];
    return &NONE_P;
}

const tp_info_t *tp_info_par(int p, const uint8_t *par)
{
    int engine = par[TP_ENGINE], player = par[TP_PLAYER], lt = par[TP_LTYPE];
    if (!eknob_ready)
        eknob_init();
    if (p >= TP_P1 && p <= TP_AP4) {
        if (player == PL_ARP)
            return &FIXED[p];
        if (player == PL_ROLL && p <= TP_P4)
            return &ROLL_P[p - TP_P1];
        return &NONE_P;
    }
    if (p >= TP_DRUM0 && p < NTP) {
        int s = (p - TP_DRUM0) / DP_N, k = (p - TP_DRUM0) % DP_N;
        if (k >= DP_K1 && k <= DP_K4)
            return &dknob[par[TP_DRUM(s, DP_MODEL)] % NDM][k - DP_K1];
        return &DRUMP[k];
    }
    if (lt >= NLT)
        lt = LT_OFF;
    switch (p) {
    case TP_LSPEED: return lt == LT_OFF ? &NONE_P : lt == LT_DUCK ? &LFO_SPEED_DUCK : &FIXED[p];
    case TP_LAMT: return &LFO_AMT[lt];
    case TP_LDEST:
        return lt == LT_VALUE || lt == LT_RANDOM ? &FIXED[p] : lt == LT_TREMOLO ? &LFO_DEST_TREM
               : lt == LT_DUCK ? &LFO_DEST_DUCK : &NONE_P;
    case TP_LX:
        if (lt == LT_VALUE || lt == LT_RANDOM) {
            switch (par[TP_LDEST] & 3) {
            case LD_SYNTH: return &lparam[engine < NENGINE ? engine : EG_SIMPLE];
            case LD_ENV: return &LFO_PARAM_ENV;
            case LD_FILTER: return &LFO_PARAM_FLT;
            default: return &LFO_PARAM_AMP;
            }
        }
        return lt == LT_TREMOLO ? &LFO_X_TREM : lt == LT_DUCK ? &LFO_X_DUCK : &NONE_P;
    case TP_LX2:
        return lt == LT_VALUE ? &FIXED[p] : lt == LT_TREMOLO ? &LFO_X2_TREM : lt == LT_RANDOM ? &LFO_X2_RAND : &NONE_P;
    default: break;
    }
    return tp_info(p, engine);
}

/* number formatting without printf (the firmware's libc is small) */
static char *put_s(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}
static char *put_i(char *b, int v)
{
    char tmp[12];
    int n = 0;
    if (v < 0) {
        *b++ = '-';
        v = -v;
    }
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        *b++ = tmp[--n];
    *b = 0;
    return b;
}
static char *put_f1(char *b, float x)           /* one decimal */
{
    int t = (int)(x * 10.0f + 0.5f);
    b = put_i(b, t / 10);
    *b++ = '.';
    return put_i(b, t % 10);
}
static void ms_text(float secs, char *num, char *unit)
{
    if (secs < 1.0f) {
        put_i(num, (int)(secs * 1000.0f + 0.5f));
        put_s(unit, "ms");
    } else {
        put_f1(num, secs);
        put_s(unit, "s");
    }
}
static void hz_text(float hz, char *num, char *unit)
{
    if (hz < 1000.0f) {
        put_i(num, (int)(hz + 0.5f));
        put_s(unit, "Hz");
    } else {
        put_f1(num, hz * 0.001f);
        put_s(unit, "kHz");
    }
}

static void tp_text_i(const tp_info_t *i, int p, int v, int engine, char *num, char *unit)
{
    unit[0] = 0;
    if (p == TP_CHAN && v == 0) {
        put_s(num, "off");
        return;
    }
    if (p == TP_PORTA && v == 0) {
        put_s(num, "off");
        return;
    }
    if (p == TP_E2 && engine == EG_STACK) {
        put_i(num, v < 1 ? 1 : v > 8 ? 8 : v);
        put_s(num + 1, ":1");
        return;
    }
    switch (i->kind) {
    case PK_PCT:
        put_i(num, (v * 100 + 63) / 127);
        put_s(unit, "%");
        break;
    case PK_BIP:
        if (v - 64 > 0)
            *num++ = '+';
        put_i(num, v - 64);
        break;
    case PK_NAMES:
        if (i->names && v >= i->lo && v <= i->hi)
            put_s(num, i->names[v - i->lo]);
        else
            put_i(num, v);
        break;
    case PK_SEMI:
        put_i(num, v);
        put_s(unit, "st");
        break;
    case PK_SWITCH: put_s(num, v ? "on" : "off"); break;
    case PK_MS: ms_text(env_seconds((float)v * (1.0f / 127.0f)), num, unit); break;
    case PK_HZ_SYNC:
        if (v < LS_SYNC_N) {
            put_s(num, SYNC_N[v]);
        } else {
            put_f1(num, lfo_hz(v, 120.0f));
            put_s(unit, "Hz");
        }
        break;
    case PK_NN: {                                /* two digits, 00..99 */
        int d = (v * 100 + 64) / 128;
        num[0] = (char)('0' + d / 10);
        num[1] = (char)('0' + d % 10);
        num[2] = 0;
        break;
    }
    case PK_HZ: hz_text(50.0f * fm_exp2f((float)v * (8.64385619f / 127.0f)), num, unit); break;
    case PK_FADE:
        if (v == 64) {
            put_s(num, "off");
        } else if (v < 64) {
            put_i(put_s(num, "in "), (64 - v) * 100 / 64);
            put_s(unit, "%");
        } else {
            put_i(put_s(num, "out "), (v - 64) * 100 / 63);
            put_s(unit, "%");
        }
        break;
    case PK_DB: {                                /* -30 .. +20 dB, 76 = 0 */
        int d = ((v - 76) * 50 + (v >= 76 ? 63 : -63)) / 127;
        if (d > 0)
            *num++ = '+';
        put_i(num, d);
        put_s(unit, "dB");
        break;
    }
    default: put_i(num, v); break;
    }
}
void tp_text(int p, int v, int engine, char *num, char *unit) { tp_text_i(tp_info(p, engine), p, v, engine, num, unit); }
void tp_text_par(int p, int v, const uint8_t *par, char *num, char *unit)
{
    tp_text_i(tp_info_par(p, par), p, v, par[TP_ENGINE], num, unit);
}

__attribute__((minsize)) void sound_default(uint8_t *par, int engine)
{
    int p, k;
    if (engine < 0 || engine >= NENGINE)
        engine = EG_SIMPLE;
    for (p = 0; p < TP_DRUM0; p++)
        par[p] = tp_info(p, engine)->def;
    par[TP_ENGINE] = (uint8_t)engine;
    for (k = 0; k < DL_NSOUND; k++)
        for (p = 0; p < DP_N; p++)
            par[TP_DRUM(k, p)] = DL_DEFAULT_KIT[k][p];
    if (engine == EG_DRUMKIT)
        par[TP_FTYPE] = FT_DYNAMIC;
}

/* -------------------------------------------------------------- presets --- */
/* ENGINE E1 E2 E3 E4 | MODE PORTA BEND VOL | A D S R | FA FD FS FR | CUT RES FENV KTRK TYPE | S1 S2 ST |
 * LTYPE LSPEED LAMT LDEST LX LX2. The LFO fields follow synth.h: value / random: amount, module, knob, shape
 * (or envelope); tremolo: vibrato, volume, envelope, shape. A rate of 90 is about 5 Hz. */
#define LFO_(lt, ls, la, ld, lx, lx2) lt, ls, la, ld, lx, lx2
#define PR(nm, eng, e1, e2, e3, e4, vm, po, vol, a, d, su, r, fa, fd, fs, fr, cut, res, fenv, kt, ft, s1, s2, lfo) \
    {nm, {eng, e1, e2, e3, e4, vm, po, 2, vol, a, d, su, r, fa, fd, fs, fr, cut, res, fenv, kt, ft, s1, s2, 0, LFO_ lfo}}
#define NOLFO (LT_OFF, 3, 64, LD_FILTER, 0, 1)
const preset_t PRESETS[] = {
    /* simple */
    PR("soft keys", EG_SIMPLE, 20, 0, 0, 30, VM_POLY, 0, 100, 5, 70, 70, 50, 0, 60, 0, 40, 68, 15, 90, 64, FT_SVF, 0, 30, NOLFO),
    PR("saw lead", EG_SIMPLE, 0, 0, 0, 20, VM_LEGATO, 40, 100, 2, 60, 100, 30, 0, 60, 0, 40, 100, 25, 64, 64, FT_SVF, 20, 10, (LT_TREMOLO, 92, 76, 64, 40, 0)),
    PR("square pluck", EG_SIMPLE, 127, 20, 0, 0, VM_POLY, 0, 100, 0, 45, 0, 30, 0, 40, 0, 30, 58, 30, 100, 80, FT_SVF, 10, 20, NOLFO),
    PR("wide pad", EG_SIMPLE, 30, 0, 5, 110, VM_POLY, 0, 95, 80, 80, 90, 90, 70, 80, 40, 80, 58, 20, 80, 50, FT_SVF, 0, 60, (LT_VALUE, 0, 80, LD_FILTER + LD_FREE, 0, 1)),
    PR("bass one", EG_SIMPLE, 0, 0, 0, 0, VM_MONO, 0, 110, 0, 65, 40, 30, 0, 55, 0, 30, 52, 35, 110, 40, FT_LADDER, 0, 0, NOLFO),
    PR("pwm strings", EG_SIMPLE, 127, 60, 0, 90, VM_POLY, 0, 95, 60, 70, 100, 70, 40, 60, 60, 60, 80, 10, 64, 64, FT_ZLP, 0, 50, (LT_VALUE, 2, 110, LD_SYNTH + LD_FREE, 1, 1)),
    PR("noise sweep", EG_SIMPLE, 0, 0, 127, 60, VM_POLY, 0, 100, 10, 100, 0, 60, 0, 100, 0, 60, 30, 70, 127, 0, FT_ZHP, 0, 40, NOLFO),
    PR("acid line", EG_SIMPLE, 0, 0, 0, 0, VM_LEGATO, 30, 105, 0, 50, 0, 30, 0, 45, 0, 30, 36, 90, 110, 64, FT_LADDER, 30, 0, NOLFO),
    /* stack */
    PR("stack bass", EG_STACK, 64, 1, 15, 0, VM_MONO, 0, 110, 0, 70, 50, 30, 0, 60, 0, 30, 56, 30, 100, 40, FT_LADDER, 0, 0, NOLFO),
    PR("fifth lead", EG_STACK, 80, 3, 10, 50, VM_LEGATO, 35, 100, 2, 60, 100, 40, 0, 60, 0, 40, 95, 20, 64, 64, FT_SVF, 20, 20, (LT_TREMOLO, 92, 74, 64, 40, 0)),
    PR("octave stab", EG_STACK, 110, 2, 8, 60, VM_POLY, 0, 100, 0, 50, 0, 30, 0, 45, 0, 30, 68, 30, 110, 64, FT_SVF, 10, 20, NOLFO),
    PR("detuned pad", EG_STACK, 50, 1, 40, 127, VM_POLY, 0, 95, 85, 80, 90, 95, 70, 80, 40, 80, 58, 15, 80, 50, FT_SVF, 0, 70, (LT_VALUE, 1, 85, LD_FILTER + LD_FREE, 0, 1)),
    PR("glass keys", EG_STACK, 0, 4, 5, 70, VM_POLY, 0, 100, 0, 75, 50, 60, 0, 65, 0, 50, 85, 10, 90, 80, FT_ZLP, 0, 35, NOLFO),
    PR("sub pulse", EG_STACK, 127, 1, 0, 0, VM_MONO, 0, 115, 0, 60, 80, 30, 0, 50, 0, 30, 42, 20, 90, 30, FT_SVF, 0, 0, NOLFO),
    PR("major third", EG_STACK, 64, 5, 12, 90, VM_POLY, 0, 95, 20, 70, 80, 60, 10, 60, 20, 60, 75, 20, 80, 64, FT_SVF, 0, 40, (LT_VALUE, 3, 90, LD_SYNTH + LD_FREE, 2, 1)),
    PR("rubber lead", EG_STACK, 30, 2, 25, 20, VM_LEGATO, 60, 100, 0, 60, 90, 30, 0, 40, 0, 30, 50, 60, 120, 64, FT_SVF, 20, 10, NOLFO),
#if SYNTH_ALL_ENGINES
    /* wavetable */
    PR("morph pad", EG_WAVETABLE, 0, 40, 0, 0, VM_POLY, 0, 100, 70, 80, 90, 90, 60, 80, 50, 80, 85, 10, 64, 64, FT_ZLP, 0, 60, (LT_VALUE, 1, 110, LD_SYNTH + LD_FREE, 1, 1)),
    PR("buzz lead", EG_WAVETABLE, 1, 30, 10, 0, VM_LEGATO, 20, 100, 2, 70, 100, 40, 0, 60, 0, 40, 100, 10, 64, 64, FT_ZLP, 10, 40, (LT_VALUE, 2, 100, LD_SYNTH + LD_FREE, 1, 1)),
    PR("zap bell", EG_WAVETABLE, 8, 90, 0, 0, VM_POLY, 0, 95, 0, 90, 0, 80, 0, 80, 0, 60, 90, 10, 80, 64, FT_ZLP, 0, 50, NOLFO),
    PR("drift lead", EG_WAVETABLE, 0, 110, 20, 70, VM_LEGATO, 30, 100, 2, 60, 100, 40, 0, 60, 0, 40, 100, 20, 64, 64, FT_SVF, 20, 20, NOLFO),
    PR("fractal bass", EG_WAVETABLE, 5, 50, 30, 0, VM_MONO, 0, 110, 0, 65, 40, 30, 0, 55, 0, 30, 56, 30, 100, 40, FT_LADDER, 0, 0, NOLFO),
    PR("crushed keys", EG_WAVETABLE, 2, 60, 0, 0, VM_POLY, 0, 100, 0, 70, 30, 50, 0, 60, 0, 40, 90, 20, 80, 64, FT_ZLP, 20, 30, NOLFO),
    PR("prime swell", EG_WAVETABLE, 7, 0, 0, 0, VM_POLY, 0, 100, 90, 80, 100, 90, 60, 60, 0, 60, 100, 0, 64, 64, FT_ZLP, 0, 70, (LT_VALUE, 0, 127, LD_SYNTH + LD_FREE, 1, 2)),
    PR("octave stack", EG_WAVETABLE, 6, 20, 0, 0, VM_POLY, 0, 100, 5, 70, 70, 50, 0, 60, 0, 40, 80, 15, 80, 64, FT_SVF, 0, 30, (LT_VALUE, 2, 100, LD_SYNTH + LD_FREE, 1, 1)),
#endif
    /* duo */
    PR("lush strings", EG_DUO, 45, 30, 20, 20, VM_POLY, 0, 100, 70, 80, 100, 80, 50, 70, 50, 70, 100, 5, 64, 64, FT_ZLP, 0, 60, NOLFO),
    PR("fm bell", EG_DUO, 60, 100, 0, 0, VM_POLY, 0, 95, 0, 95, 0, 90, 0, 90, 0, 80, 115, 0, 64, 64, FT_ZLP, 0, 50, NOLFO),
    PR("duo bass", EG_DUO, 50, 0, 60, 0, VM_MONO, 0, 110, 0, 70, 60, 30, 0, 60, 0, 30, 76, 20, 90, 40, FT_SVF, 0, 0, NOLFO),
    PR("fifth pad", EG_DUO, 35, 72, 10, 50, VM_POLY, 0, 95, 90, 80, 100, 100, 70, 80, 40, 80, 100, 10, 64, 64, FT_ZLP, 0, 70, (LT_VALUE, 0, 90, LD_SYNTH + LD_FREE, 0, 1)),
    PR("fm pluck", EG_DUO, 80, 104, 20, 0, VM_POLY, 0, 100, 0, 55, 0, 40, 0, 50, 0, 40, 100, 10, 90, 64, FT_ZLP, 10, 30, NOLFO),
    PR("hollow lead", EG_DUO, 70, 110, 60, 40, VM_LEGATO, 30, 100, 2, 60, 100, 40, 0, 60, 0, 40, 110, 10, 64, 64, FT_ZLP, 20, 20, NOLFO),
    PR("slow beat", EG_DUO, 40, 50, 0, 90, VM_POLY, 0, 100, 40, 80, 100, 70, 0, 60, 0, 40, 100, 0, 64, 64, FT_ZLP, 0, 50, NOLFO),
    PR("tines", EG_DUO, 55, 104, 0, 10, VM_POLY, 0, 100, 0, 80, 30, 60, 0, 70, 0, 50, 110, 0, 64, 64, FT_ZLP, 0, 40, NOLFO),
    /* epiano */
    PR("epiano", EG_EPIANO, 40, 10, 52, 68, VM_POLY, 0, 100, 0, 90, 60, 50, 0, 70, 0, 40, 105, 5, 64, 64, FT_ZLP, 10, 30, (LT_TREMOLO, 88, 64, 84, 64, 0)),
    PR("soft tines", EG_EPIANO, 10, 0, 30, 90, VM_POLY, 0, 100, 0, 95, 50, 60, 0, 70, 0, 40, 100, 0, 64, 64, FT_ZLP, 0, 40, NOLFO),
    PR("gritty keys", EG_EPIANO, 80, 80, 60, 20, VM_POLY, 0, 100, 0, 85, 50, 40, 0, 60, 0, 40, 100, 10, 64, 64, FT_ZLP, 20, 20, NOLFO),
    PR("ep bass", EG_EPIANO, 70, 40, 70, 0, VM_MONO, 0, 110, 0, 80, 50, 30, 0, 60, 0, 30, 86, 10, 64, 40, FT_ZLP, 0, 0, NOLFO),
    PR("ep lead", EG_EPIANO, 90, 50, 50, 40, VM_LEGATO, 20, 100, 0, 80, 90, 40, 0, 60, 0, 40, 110, 10, 64, 64, FT_ZLP, 20, 20, (LT_TREMOLO, 92, 74, 64, 30, 0)),
    PR("music box", EG_EPIANO, 40, 0, 0, 110, VM_POLY, 0, 95, 0, 80, 0, 60, 0, 60, 0, 40, 120, 0, 64, 64, FT_ZLP, 0, 50, NOLFO),
    PR("ep pad", EG_EPIANO, 50, 10, 0, 20, VM_POLY, 0, 95, 60, 90, 90, 90, 40, 70, 40, 70, 95, 5, 64, 64, FT_ZLP, 0, 60, (LT_TREMOLO, 1, 64, 90, 20, 0)),
    PR("punchy ep", EG_EPIANO, 65, 30, 110, 50, VM_POLY, 0, 100, 0, 85, 40, 40, 0, 60, 0, 40, 105, 5, 64, 64, FT_ZLP, 10, 20, NOLFO),
    /* organ (type, bass, tremolo amount, tremolo speed: 1..8) */
    PR("transistor", EG_ORGAN, 1, 2, 3, 5, VM_POLY, 0, 100, 0, 60, 100, 15, 0, 60, 0, 40, 110, 0, 64, 64, FT_ZLP, 0, 20, NOLFO),
    PR("jazz organ", EG_ORGAN, 3, 5, 5, 7, VM_POLY, 0, 100, 0, 60, 100, 15, 0, 60, 0, 40, 105, 0, 64, 64, FT_ZLP, 0, 20, NOLFO),
    PR("gospel", EG_ORGAN, 4, 6, 2, 6, VM_POLY, 0, 95, 0, 60, 100, 20, 0, 60, 0, 40, 110, 0, 64, 64, FT_ZLP, 0, 30, NOLFO),
    PR("church", EG_ORGAN, 8, 7, 1, 1, VM_POLY, 0, 95, 30, 60, 100, 60, 0, 60, 0, 40, 100, 0, 64, 64, FT_ZLP, 0, 90, NOLFO),
    PR("organ bass", EG_ORGAN, 2, 8, 1, 1, VM_MONO, 0, 110, 0, 60, 100, 15, 0, 60, 0, 40, 76, 0, 64, 40, FT_ZLP, 0, 0, NOLFO),
    PR("swell", EG_ORGAN, 6, 4, 7, 2, VM_POLY, 0, 95, 90, 60, 100, 90, 60, 60, 0, 40, 90, 0, 64, 64, FT_ZLP, 0, 60, NOLFO),
    PR("dizzy", EG_ORGAN, 5, 4, 8, 8, VM_POLY, 0, 95, 20, 60, 100, 40, 0, 60, 0, 40, 100, 0, 64, 64, FT_ZLP, 20, 40, NOLFO),
    PR("pipe", EG_ORGAN, 7, 5, 2, 3, VM_POLY, 0, 95, 40, 60, 100, 50, 0, 60, 0, 40, 95, 0, 64, 64, FT_ZLP, 0, 70, NOLFO),
    /* basses, after the rest so a song's sound numbers keep their meaning */
    PR("deep sub", EG_SIMPLE, 127, 0, 0, 0, VM_MONO, 0, 115, 0, 80, 90, 30, 0, 60, 0, 30, 26, 5, 64, 30, FT_LADDER, 0, 0, NOLFO),
    PR("acid squelch", EG_SIMPLE, 0, 0, 0, 0, VM_LEGATO, 25, 127, 0, 65, 50, 25, 0, 40, 0, 25, 52, 80, 118, 64, FT_LADDER, 25, 0, NOLFO),
    PR("reese", EG_STACK, 0, 1, 40, 30, VM_MONO, 0, 105, 2, 80, 100, 35, 0, 70, 60, 40, 48, 30, 80, 40, FT_LADDER, 0, 10, (LT_VALUE, 40, 80, LD_FILTER + LD_FREE, 0, 1)),
    PR("fm growl", EG_DUO, 90, 0, 90, 0, VM_MONO, 0, 108, 0, 65, 70, 30, 0, 55, 40, 30, 90, 20, 90, 40, FT_SVF, 0, 0, NOLFO),
    PR("pluck bass", EG_SIMPLE, 127, 30, 0, 0, VM_MONO, 0, 127, 0, 45, 15, 25, 0, 32, 0, 25, 34, 45, 118, 50, FT_LADDER, 0, 0, NOLFO),
    PR("wobble", EG_STACK, 0, 2, 12, 10, VM_MONO, 0, 105, 0, 80, 100, 30, 0, 60, 100, 40, 34, 60, 64, 40, FT_LADDER, 0, 0, (LT_VALUE, 4, 120, LD_FILTER, 0, 1)),   /* eighth notes, from each note */
    PR("boom", EG_SIMPLE, 127, 0, 0, 0, VM_MONO, 20, 120, 0, 95, 0, 60, 0, 70, 0, 40, 30, 20, 90, 30, FT_LADDER, 0, 0, NOLFO),
    PR("fat saw", EG_STACK, 0, 2, 15, 20, VM_MONO, 0, 108, 0, 80, 90, 30, 0, 60, 30, 35, 60, 20, 96, 40, FT_LADDER, 0, 0, NOLFO),
    /* hardsync */
#if SYNTH_ALL_ENGINES
    PR("sync stab", EG_HARDSYNC, 50, 30, 0, 0, VM_POLY, 0, 100, 0, 55, 0, 30, 0, 50, 0, 30, 100, 20, 80, 64, FT_SVF, 10, 20, NOLFO),
    PR("sync bass", EG_HARDSYNC, 25, 70, 0, 0, VM_MONO, 0, 110, 0, 70, 50, 30, 0, 60, 0, 30, 66, 20, 90, 40, FT_LADDER, 0, 0, NOLFO),
    PR("sweep lead", EG_HARDSYNC, 70, 20, 0, 20, VM_LEGATO, 30, 100, 2, 60, 100, 40, 0, 60, 0, 40, 100, 15, 64, 64, FT_SVF, 20, 20, (LT_VALUE, 3, 100, LD_SYNTH, 0, 1)),
    PR("jab", EG_HARDSYNC, 90, 0, 60, 40, VM_POLY, 0, 100, 0, 40, 0, 20, 0, 40, 0, 20, 110, 20, 64, 64, FT_ZLP, 10, 10, NOLFO),
    PR("sync pad", EG_HARDSYNC, 40, 40, 0, 10, VM_POLY, 0, 95, 80, 80, 90, 90, 60, 80, 40, 80, 68, 15, 80, 50, FT_SVF, 0, 60, (LT_VALUE, 1, 90, LD_SYNTH + LD_FREE, 0, 1)),
    PR("noisy pluck", EG_HARDSYNC, 60, 20, 110, 60, VM_POLY, 0, 100, 0, 50, 0, 30, 0, 45, 0, 30, 90, 30, 100, 64, FT_SVF, 10, 30, NOLFO),
    PR("thin lead", EG_HARDSYNC, 100, 0, 0, 90, VM_LEGATO, 20, 100, 0, 60, 100, 30, 0, 60, 0, 40, 110, 10, 64, 64, FT_ZLP, 20, 20, (LT_TREMOLO, 92, 76, 64, 30, 0)),
    PR("sync keys", EG_HARDSYNC, 35, 50, 0, 0, VM_POLY, 0, 100, 0, 75, 40, 50, 0, 65, 0, 40, 85, 15, 90, 64, FT_SVF, 0, 30, NOLFO),
    /* swarm */
    PR("hairdryer", EG_SWARM, 90, 60, 20, 30, VM_POLY, 0, 100, 40, 80, 100, 70, 0, 60, 0, 40, 110, 0, 64, 64, FT_ZLP, 0, 60, NOLFO),
    PR("noise pad", EG_SWARM, 50, 30, 10, 60, VM_POLY, 0, 95, 90, 80, 100, 100, 70, 80, 40, 80, 100, 10, 64, 64, FT_ZLP, 0, 80, NOLFO),
    PR("bright lead", EG_SWARM, 20, 10, 80, 10, VM_LEGATO, 30, 100, 2, 60, 100, 40, 0, 60, 0, 40, 110, 10, 64, 64, FT_ZLP, 20, 20, NOLFO),
    PR("shimmer", EG_SWARM, 30, 0, 40, 90, VM_POLY, 0, 95, 70, 80, 100, 90, 0, 60, 0, 40, 115, 0, 64, 64, FT_ZLP, 0, 90, (LT_VALUE, 0, 90, LD_SYNTH + LD_FREE, 0, 1)),
    PR("swarm bass", EG_SWARM, 10, 20, 60, 0, VM_MONO, 0, 110, 0, 70, 60, 30, 0, 60, 0, 30, 76, 20, 90, 40, FT_LADDER, 0, 0, NOLFO),
    PR("wind", EG_SWARM, 127, 100, 0, 50, VM_POLY, 0, 120, 60, 80, 100, 80, 0, 60, 0, 40, 40, 20, 64, 64, FT_ZHP, 0, 70, NOLFO),
    PR("sine keys", EG_SWARM, 0, 0, 20, 5, VM_POLY, 0, 100, 0, 80, 40, 60, 0, 60, 0, 40, 120, 0, 64, 64, FT_ZLP, 0, 40, NOLFO),
    PR("grit swarm", EG_SWARM, 70, 110, 50, 70, VM_POLY, 0, 100, 10, 70, 80, 50, 0, 60, 0, 40, 100, 10, 64, 64, FT_ZLP, 20, 40, NOLFO),
#endif
};
const int NPRESETS = (int)(sizeof PRESETS / sizeof PRESETS[0]);

int preset_first(int engine)
{
    int i;
    for (i = 0; i < NPRESETS; i++)
        if (PRESETS[i].par[TP_ENGINE] == engine)
            return i;
    return -1;
}
