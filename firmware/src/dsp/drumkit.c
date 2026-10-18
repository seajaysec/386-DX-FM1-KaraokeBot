/* SPDX-License-Identifier: GPL-3.0-only */
/* drumkit (drumkit.h): the ten drum models, analytic, on NDVOICE voices.
 *
 * Every sound is a few oscillators, a noise source and a filter or two, shaped by exponential
 * envelopes (env *= k a sample): no samples, no tables, so a kit costs nothing in flash and a voice
 * stays well under 80 instructions a sample. The kick has a pitch drop and a click,
 * the snare two detuned sines over band-passed noise, the hat six square waves through high-passes,
 * the clap four bursts.
 *
 * Each key of the kit is a model instance: par[TP_DRUM(key, DP_MODEL)] picks the model, DP_K1..K4
 * are the model's own four knobs (drumkit.h DM_KNOB, read at the hit), DP_RANDOM varies those four
 * a little differently every hit, DP_PAN and DP_VOLUME (-30..+20 dB) place it, DP_MODE says how it
 * plays (one-shot, gated, loop, choke groups). The track's par[TP_A..TP_R] are the drumkit envelope
 * the design calls funk / attack / random / length: a delay before the hit, a fade-in, a
 * track-wide randomisation (the kick's pitch excepted) and a scale on every model's length.
 *
 * Levels: "Drum kit operates with a balanced gain per key": every model is trimmed so that its
 * defaults at velocity 127 and volume 0 dB peak around -6 dBFS. The key's DP_VOLUME, DP_PAN and the
 * hit's velocity are applied here; the track's TP_SEND1/2/T scale the three send buffers (mono, pre
 * track fader, pre filter). The track's TP_LEVEL and TP_PAN (and the punch-in mute) belong to the
 * engine layer, which applies them to all five buffers after drumkit_render. Pan, volume and the
 * sends are re-read every SY_CHUNK samples and ramped across the chunk, so a knob turn is heard
 * without zipper noise. The dynamic filter (drumkit_filter, FT_DYNAMIC) runs on the summed dry
 * output of the track: a 12 dB low cut and hi cut, a one-knob compressor and a tanh-shaped drive.
 *
 * Threads: drumkit_hit / release / choke / filter and drumkit_render run in one context (the
 * audio interrupt: the engine queues the keys' hits and the page's knobs). par[] is written by the
 * main loop a byte at a time. */
#include "drumkit.h"
#include "synth.h"
#include "fastmath.h"

#define SR SY_SR
#define CR SY_CHUNK                   /* pan / volume / sends re-read this often */
#define SUB 32                        /* a voice renders this many at once into a 128 B stack buffer */
#define RESO_LEN 512                  /* a resonator string, int16: 86 Hz at the longest */
#define NRESO 2                       /* strings: resonator sounds past two steal the older's */

const char *const DM_NAME[NDM] = {"kick", "snare", "hi-hat", "clap", "shaker", "tambourine", "resonator",
                                  "congas", "tom", "fm"};
const char *const DM_KNOB[NDM][4] = {
    [DM_KICK] = {"pitch", "length", "punch", "tone"},    [DM_SNARE] = {"pitch", "length", "snare", "punch"},
    [DM_HIHAT] = {"pitch", "length", "era", "tone"},     [DM_CLAP] = {"tone", "length", "spread", "contour"},
    [DM_SHAKER] = {"color", "length", "attack", "onset"}, [DM_TAMB] = {"tone", "length", "shake", "ring"},
    [DM_RESO] = {"pitch", "length", "tone", "strike"},   [DM_CONGA] = {"pitch", "length", "slap", "mush"},
    [DM_TOM] = {"pitch", "length", "sweep", "rate"},     [DM_FM] = {"pitch", "length", "mod", "feedback"},
};
/* printed defaults (00..99) * 1.28 */
const uint8_t DM_DEFAULT[NDM][4] = {
    [DM_KICK] = {100, 40, 64, 64},  [DM_SNARE] = {100, 40, 64, 64}, [DM_HIHAT] = {93, 23, 64, 88},
    [DM_CLAP] = {100, 64, 64, 38},  [DM_SHAKER] = {31, 64, 64, 101}, [DM_TAMB] = {93, 23, 64, 88},
    [DM_RESO] = {69, 54, 78, 122},  [DM_CONGA] = {100, 46, 64, 0},  [DM_TOM] = {101, 46, 38, 64},
    [DM_FM] = {93, 100, 46, 15},
};
const char *const PM_NAME[NPM] = {"one-shot", "gated", "loop", "choke 1", "choke 2", "choke 3"};
const char *const DL_SOUND_NAME[DL_NSOUND] = {"kick", "snare", "low tom", "mid tom", "high tom", "rim", "clap",
                                              "cowbell", "cymbal", "open hat", "closed hat"};
/* the default kit, in the song's eleven drum lanes: kick, snare, three toms, rim, clap, cowbell, cymbal,
 * open hat and closed hat (the same model, choke group 1, so the closed cuts the open).
 * DP_MODEL, DP_K1..K4, DP_RANDOM, DP_PAN, DP_VOLUME (76 = 0 dB), DP_MODE */
#define VOL0 76
const uint8_t DL_DEFAULT_KIT[DL_NSOUND][9] = {
    {DM_KICK, 100, 40, 64, 64, 0, 64, VOL0, PM_ONESHOT},
    {DM_SNARE, 100, 40, 64, 64, 0, 64, VOL0, PM_ONESHOT},
    {DM_TOM, 50, 50, 38, 64, 0, 44, VOL0, PM_ONESHOT},
    {DM_TOM, 78, 46, 38, 64, 0, 64, VOL0, PM_ONESHOT},
    {DM_TOM, 104, 42, 38, 64, 0, 84, VOL0, PM_ONESHOT},
    {DM_CONGA, 127, 12, 110, 0, 0, 58, 70, PM_ONESHOT},
    {DM_CLAP, 100, 64, 64, 38, 0, 70, VOL0, PM_ONESHOT},
    {DM_FM, 110, 40, 70, 20, 0, 60, 66, PM_ONESHOT},
    {DM_HIHAT, 70, 110, 100, 60, 0, 50, 70, PM_ONESHOT},
    {DM_HIHAT, 93, 80, 64, 88, 0, 74, VOL0, PM_CHOKE1},
    {DM_HIHAT, 93, 23, 64, 88, 0, 74, VOL0, PM_CHOKE1},
};
volatile uint8_t drumkit_hitv[DL_NSOUND];

/* a model's length range, seconds to -60 dB at the length knob's ends, and its trim (the balanced gain) */
typedef struct { float dec_lo, dec_hi, trim; } dm_spec_t;
static const dm_spec_t SPEC[NDM] = {
    [DM_KICK] = {0.05f, 1.5f, 0.55f},   [DM_SNARE] = {0.06f, 1.0f, 0.4f},  [DM_HIHAT] = {0.02f, 2.5f, 2.2f},
    [DM_CLAP] = {0.05f, 1.2f, 0.65f},    [DM_SHAKER] = {0.02f, 0.6f, 0.85f},  [DM_TAMB] = {0.03f, 1.5f, 1.5f},
    [DM_RESO] = {0.05f, 2.5f, 0.5f},    [DM_CONGA] = {0.05f, 0.9f, 0.55f},  [DM_TOM] = {0.08f, 1.5f, 0.55f},
    [DM_FM] = {0.03f, 1.5f, 0.6f},
};
/* six metal oscillators (205.3, 304.4, 369.6, 522.7, 540, 800 Hz) and the tambourine's
 * jingles: six inharmonic partials of a small brass disc */
static const float METAL_HZ[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};
static const float JINGLE_HZ[6] = {4120.0f, 5230.0f, 6010.0f, 7340.0f, 8560.0f, 9870.0f};

typedef struct {
    uint8_t on, sound, model, line;   /* line: the resonator's string, 0..NRESO-1 */
    uint8_t mode, held, hvel, hflags; /* the play mode; the key is still down; the hit, for a loop's restart */
    int8_t hpitch;
    uint8_t pad[3];
    uint32_t age, rng;
    int32_t cnt, bursts;              /* the clap / shaker: samples to the next burst, bursts left */
    int32_t wait;                     /* funk: samples of silence before the hit starts */
    float att, attk;                  /* attack: the fade-in gain and its step a sample */
    float env, k;                     /* the amplitude envelope and its per-sample multiplier */
    float env2, k2;                   /* a second envelope: click, body, burst, attack, FM index */
    float ph, inc, ph2, inc2;         /* oscillators, phase in turns [-0.5, 0.5) */
    float pd, pdk, pda;               /* pitch drop: inc * (1 + pda * pd), pd decays by pdk */
    float f1, f2, f3, f4;             /* filter states */
    float c1, c2, c3;                 /* filter coefficients */
    float a, b, g;                    /* two model amounts and the model's output gain */
    float gl, gr, gs1, gs2, gst;      /* gains now: left, right, the three sends (set each chunk) */
    float dgl, dgr;                   /* left and right ramp toward the chunk's targets */
    uint32_t mp[6], mi[6];            /* the hat's / tambourine's six square phases and increments */
    int32_t len, w;                   /* the resonator: string length, write position */
    float fr, lp, fb, damp, pk;       /* its fractional length, loop filter, feedback, damping, peak */
} dvoice_t;

/* the dynamic filter: coefficients from drumkit_filter, states per channel */
typedef struct {
    uint8_t on, lo_on, hi_on, drv_on;
    float lc, hc;                     /* one-pole coefficients, two in series each = 12 dB */
    float thr, slope, makeup;         /* the compressor: threshold (linear), dB taken per dB over, gain */
    float drive, dcomp;               /* tanh drive and its level compensation */
    float env, gain;                  /* the follower and the gain it last chose */
    float lo[4], hi[4];               /* low cut L1 L2 R1 R2, hi cut the same */
} dynf_t;

struct drumkit {
    const uint8_t *par;
    dvoice_t v[NDVOICE];
    dynf_t f;
    int8_t line_owner[NRESO];         /* the voice holding each string, -1 */
    int16_t line[NRESO][RESO_LEN];
};
_Static_assert(sizeof(struct drumkit) <= DRUMKIT_BYTES, "drumkit_t does not fit DRUMKIT_BYTES");

static inline float noise(uint32_t *r)
{
    *r = *r * 1664525u + 1013904223u;
    return (float)(int32_t)*r * (1.0f / 2147483648.0f);
}
/* the multiplier that takes an envelope to -60 dB in t seconds */
static inline float dec_k(float t) { return fm_expf(-6.9078f / (fm_maxf(t, 0.001f) * SR)); }
/* a one-pole's coefficient for a cutoff in Hz */
static inline float lp_c(float hz) { return 1.0f - fm_expf(-FM_TWO_PI * fm_minf(hz, 18000.0f) / SR); }
/* a two-pole resonator (bandpass) at hz, bandwidth bw: y = x c3 + c1 y1 + c2 y2 */
__attribute__((minsize)) static void reso_set(dvoice_t *v, float hz, float bw)
{
    float r = fm_expf(-FM_PI * bw / SR);
    hz = fm_minf(hz, 18000.0f);
    v->c1 = 2.0f * r * fm_cosf(FM_TWO_PI * hz / SR);
    v->c2 = -r * r;
    v->c3 = (1.0f - r) * 2.0f;
}
/* a sine of a phase in turns [-0.5, 0.5): the parabola 4x(1 - |x|) with Blinn's correction, harmonics
 * under -60 dB, a third of fm_sin_pi's cost: these are drums */
static inline float sin_t(float ph)
{
    float x = ph + ph, y = 4.0f * x * (1.0f - fm_fabsf(x));
    return y + 0.225f * (y * fm_fabsf(y) - y);
}
static inline float sin_t_any(float ph)                        /* any phase in turns */
{
    return sin_t(ph - fm_floorf(ph + 0.5f));
}
/* a cubic soft clip: x (1.5 - 0.5 x^2) inside +-1, flat outside */
static inline float clip3(float y)
{
    return y > 1.0f ? 1.0f : y < -1.0f ? -1.0f : y * (1.5f - 0.5f * y * y);
}
#define WRAP(ph) do { if ((ph) >= 0.5f) (ph) -= 1.0f; } while (0)
#define HF_SHORT 1                    /* hflags: the hit's short_gate and long_tail */
#define HF_LONG 2

__attribute__((minsize)) void drumkit_init(drumkit_t *d, const uint8_t *par)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)d;
    for (i = 0; i < sizeof *d; i++)
        z[i] = 0;
    d->par = par;
    d->f.gain = 1.0f;
    for (i = 0; i < NRESO; i++)
        d->line_owner[i] = -1;
    for (i = 0; i < NDVOICE; i++)
        d->v[i].rng = 0x9E3779B9u * (i + 1);
}

/* a voice for the sound: its own (a retrigger), a free one, else the oldest */
static dvoice_t *alloc(drumkit_t *d, int sound)
{
    int i, best = -1;
    uint32_t oldest = 0;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on && d->v[i].sound == sound)
            return &d->v[i];
    for (i = 0; i < NDVOICE; i++)
        if (!d->v[i].on)
            return &d->v[i];
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].age >= oldest) {
            oldest = d->v[i].age;
            best = i;
        }
    return &d->v[best];
}

static void fade_out(dvoice_t *v)                     /* a choke: 5 ms to silence */
{
    float k = dec_k(0.005f);
    v->held = 0;
    if (v->wait > 0) {                                /* not started yet: nothing to fade */
        v->on = 0;
        return;
    }
    v->k = k;
    v->k2 = k;
    v->bursts = 0;
    if (v->model == DM_RESO) {
        v->fb = 0.3f;
        v->env2 = 0.0f;
    }
}

void drumkit_choke(drumkit_t *d)
{
    int i;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on)
            fade_out(&d->v[i]);
}

void drumkit_release(drumkit_t *d, int sound)
{
    int i;
    for (i = 0; i < NDVOICE; i++) {
        dvoice_t *v = &d->v[i];
        if (!v->on || v->sound != sound)
            continue;
        v->held = 0;
        if (v->mode == PM_GATED)
            fade_out(v);
    }
}

/* the knob with its random variations: the key's DP_RANDOM and the track's (TP_S), each up to about a
 * third of the range either way; the kick's pitch takes only the key's */
static float knob(const uint8_t *p, int i, float amt, uint32_t *rng)
{
    float x = (float)p[DP_K1 + i] * (1.0f / 127.0f);
    if (amt > 0.0f)
        x = fm_clampf(x + noise(rng) * amt * 0.33f, 0.0f, 1.0f);
    return x;
}

/* the hit itself, on a voice already chosen: with_funk = the envelope's delay applies (not on a loop's
 * restart) */
__attribute__((minsize)) static void start(drumkit_t *d, dvoice_t *v, int sound, int vel, int pitch_semi, int flags, int with_funk)
{
    const uint8_t *par = d->par, *p = par + TP_DRUM(sound, 0);
    const dm_spec_t *sp;
    int model = p[DP_MODEL] % NDM, i;
    float k1, k2, k3, k4, dec, hz = 0.0f, semi = (float)pitch_semi;
    float rnd_key = (float)p[DP_RANDOM] * (1.0f / 127.0f), rnd_trk = (float)par[TP_S] * (1.0f / 127.0f);
    float rnd = fm_minf(1.0f, rnd_key + rnd_trk);
    float lscale = fm_exp2f(((float)par[TP_R] - 64.0f) * (1.0f / 32.0f));   /* length: 0.25x .. 4x */
    sp = &SPEC[model];
    if (v->model == DM_RESO && model != DM_RESO && d->line_owner[v->line] == (int)(v - d->v))
        d->line_owner[v->line] = -1;                  /* it leaves its string (also one choked while waiting) */
    {   /* the voice's constant part: the gains start at the chunk's first control read */
        uint32_t rng = v->rng;
        uint8_t line = v->line;
        int keep_line = v->on && v->model == DM_RESO && model == DM_RESO;
        uint8_t *z = (uint8_t *)v;
        uint32_t n;
        for (n = 0; n < sizeof *v; n++)
            z[n] = 0;
        v->rng = rng;
        v->line = keep_line ? line : 0;
        v->on = 1;
        v->sound = (uint8_t)sound;
        v->model = (uint8_t)model;
        v->mode = (uint8_t)(p[DP_MODE] % NPM);
        v->held = (uint8_t)(v->mode == PM_GATED || v->mode == PM_LOOP);
        v->hvel = (uint8_t)vel;
        v->hpitch = (int8_t)fm_clampf(semi, -127.0f, 127.0f);
        v->hflags = (uint8_t)flags;
        if (!keep_line && model == DM_RESO) {         /* a string: a free one, else the older owner's */
            int li = -1, oldest = -1;
            for (i = 0; i < NRESO; i++)
                if (d->line_owner[i] < 0) {
                    li = i;
                    break;
                }
            if (li < 0) {
                uint32_t age = 0;
                for (i = 0; i < NRESO; i++) {
                    dvoice_t *o = &d->v[d->line_owner[i]];
                    if (o->age >= age) {
                        age = o->age;
                        li = i;
                        oldest = d->line_owner[i];
                    }
                }
                d->v[oldest].on = 0;
            }
            d->line_owner[li] = (int8_t)(v - d->v);
            v->line = (uint8_t)li;
            for (i = 0; i < RESO_LEN; i++)
                d->line[li][i] = 0;
        }
    }
    k1 = knob(p, 0, model == DM_KICK ? rnd_key : rnd, &v->rng);
    k2 = knob(p, 1, rnd, &v->rng);
    k3 = knob(p, 2, rnd, &v->rng);
    k4 = knob(p, 3, rnd, &v->rng);
    dec = sp->dec_lo * fm_powf(sp->dec_hi / sp->dec_lo, k2) * lscale;
    if (flags & HF_SHORT)
        dec = fm_minf(dec, 0.06f);
    if (flags & HF_LONG)
        dec *= 4.0f;
    v->env = 1.0f;
    v->k = dec_k(dec);
    v->env2 = 1.0f;
    v->k2 = v->k;
    v->g = (float)vel * (1.0f / 127.0f) * sp->trim;   /* the velocity: a gain; the chunk adds volume / pan */
    /* the track envelope: funk = the start waits 0..60 ms, attack = a fade-in of 0..100 ms */
    v->wait = with_funk ? (int32_t)((float)par[TP_A] * (0.060f / 127.0f) * SR) : 0;
    {
        float at = (float)par[TP_D] * (0.100f / 127.0f) * SR;
        v->att = at < 1.0f ? 1.0f : 0.0f;
        v->attk = at < 1.0f ? 0.0f : 1.0f / at;
    }

    switch (model) {
    case DM_KICK:                                     /* a sine falling from punch x its pitch over ~30 ms;
                                                       * punch = the drop, the click and the drive; tone = a
                                                       * low-pass on the whole */
        hz = 16.0f * fm_exp2f(k1 * 2.5f + semi * (1.0f / 12.0f));
        v->pda = 1.0f + 5.0f * k3;
        v->pd = 1.0f;
        v->pdk = dec_k(0.025f + 0.03f * k3);
        v->k2 = dec_k(0.0025f);                       /* the click: a 2.5 ms noise burst */
        v->a = 1.0f + 2.0f * k3;                      /* drive */
        v->b = 0.6f * k3;                             /* click level */
        v->c1 = lp_c(200.0f * fm_exp2f(6.0f * k4));
        break;
    case DM_SNARE:                                    /* two sines (1 : 1.58) over band-passed noise: snare =
                                                       * the wires' share, punch = the drop and the drive */
        hz = 90.0f * fm_exp2f(k1 * 1.8f + semi * (1.0f / 12.0f));
        v->inc2 = hz * 1.58f / SR;
        v->pda = 0.2f + 1.0f * k4;
        v->pd = 1.0f;
        v->pdk = dec_k(0.012f);
        v->k2 = dec_k(dec * 0.5f);                    /* the body falls faster than the wires */
        v->a = (1.0f - 0.6f * k3) * (1.0f + 1.5f * k4);   /* body drive */
        v->b = 0.15f + 1.1f * k3;                     /* the wires */
        reso_set(v, 2000.0f, 2500.0f);
        break;
    case DM_HIHAT: {                                  /* era: noise (left) to the six squares and their
                                                       * ring-mod (right); tone: the high-passed bell (left)
                                                       * to a bandpass focus (right) */
        float ratio = fm_exp2f((k1 - 0.73f) * 2.0f + semi * (1.0f / 12.0f));
        for (i = 0; i < 6; i++) {
            v->mi[i] = (uint32_t)(METAL_HZ[i] * ratio * (4294967296.0f / SR));
            v->mp[i] = v->rng * (uint32_t)(i * 0x3C6EF372u);
        }
        v->pdk = lp_c(3000.0f * fm_sqrtf(ratio));    /* the high-passes' coefficient (no pitch drop here) */
        v->k2 = dec_k(0.003f);                        /* a touch of click at the start */
        v->a = k3;
        v->b = k4;
        reso_set(v, 7000.0f * fm_sqrtf(ratio), 1500.0f);
        v->g *= 1.0f + 0.5f * k4;
        break;
    }
    case DM_CLAP:                                     /* four bursts, spread = their spacing and their
                                                       * length, then a tail whose level is the contour */
        hz = 400.0f * fm_exp2f(k1 * 2.3f + semi * (1.0f / 12.0f));
        v->k2 = dec_k(0.005f + 0.015f * k3);
        v->cnt = (int32_t)((0.005f + 0.012f * k3) * SR);
        v->a = (float)v->cnt;
        v->bursts = 3;
        v->b = 0.25f + 0.9f * k4;                     /* the tail's level */
        reso_set(v, hz, 900.0f + 600.0f * k4);
        break;
    case DM_SHAKER:                                   /* noise through a resonator (color), a fade-in
                                                       * (attack), then the shake back (onset = how soon) */
        hz = 2000.0f * fm_exp2f(k1 * 2.2f + semi * (1.0f / 12.0f));
        v->k2 = dec_k(0.002f + 0.06f * k3);           /* the attack: 2 .. 60 ms */
        v->cnt = (int32_t)(0.2f * fm_exp2f(-3.0f * k4) * SR);
        v->bursts = 1;
        reso_set(v, hz, 2500.0f - 1500.0f * k1);
        v->g *= 1.0f + 0.5f * k1;                     /* a narrow band is quieter in RMS, peakier */
        break;
    case DM_TAMB: {                                   /* jingles: six high partials (shake = how long they
                                                       * ring) and a high-passed noise (ring = its share and
                                                       * its ring-out); tone = brightness */
        float ratio = fm_exp2f((k1 - 0.73f) * 0.8f + semi * (1.0f / 12.0f));
        for (i = 0; i < 6; i++) {
            v->mi[i] = (uint32_t)(JINGLE_HZ[i] * ratio * (4294967296.0f / SR));
            v->mp[i] = v->rng * (uint32_t)(i * 0x3C6EF372u + 0x9E3779B9u);
        }
        v->c1 = lp_c(2500.0f * fm_exp2f(1.5f * k1));
        v->k2 = dec_k(fm_maxf(0.01f, dec * (0.15f + 0.85f * k3)));   /* the partials */
        v->k = dec_k(fm_maxf(0.01f, dec * (0.1f + 0.9f * k4)));      /* the noise */
        v->a = 0.35f + 0.65f * k4;                    /* the noise's share */
        v->b = 0.3f + 0.7f * k3;                      /* the partials' share */
        break;
    }
    case DM_RESO: {                                   /* a noise burst into a Karplus-Strong string: tone =
                                                       * the loop's damping, strike = the burst's length,
                                                       * level and brightness */
        float len;
        hz = 60.0f * fm_exp2f(k1 * 4.0f + semi * (1.0f / 12.0f));
        len = SR / hz;
        if (len > (float)(RESO_LEN - 2))
            len = (float)(RESO_LEN - 2);
        if (len < 4.0f)
            len = 4.0f;
        v->len = (int32_t)len;
        v->fr = len - (float)v->len;
        v->fb = fm_exp2f(-9.966f / (dec * hz));       /* -60 dB after dec seconds of round trips */
        if (v->fb > 0.999f)
            v->fb = 0.999f;
        v->damp = 0.15f + 0.85f * k3;                 /* the loop's low-pass: tone = brightness */
        v->k2 = dec_k(fm_maxf(0.004f, (1.5f + 3.0f * k4) * len / SR));
        v->a = 0.5f + 0.5f * k4;                      /* the burst's level */
        v->c1 = lp_c(2000.0f * fm_exp2f(3.0f * k4));  /* and its brightness */
        v->pk = 1.0f;
        break;
    }
    case DM_CONGA:                                    /* a pitch slide (slap: how far and how fast) with a
                                                       * second partial, through a soft clip (mush) */
        hz = 90.0f * fm_exp2f(k1 * 1.8f + semi * (1.0f / 12.0f));
        v->inc2 = hz * 2.4f / SR;
        v->pda = 0.1f + 1.4f * k3;
        v->pd = 1.0f;
        v->pdk = dec_k(0.004f + 0.02f * k3);
        v->k2 = dec_k(dec * 0.35f);
        v->a = 1.0f + 7.0f * k4;                      /* drive */
        v->b = 0.25f + 0.5f * k3;                     /* the partial's share */
        v->c1 = 1.0f / (1.0f + 0.6f * k4);            /* the drive's level compensation */
        break;
    case DM_TOM:                                      /* a sine sweeping down (sweep = how far, rate = how
                                                       * fast: the syndrum), a little filtered noise */
        hz = 50.0f * fm_exp2f(k1 * 2.5f + semi * (1.0f / 12.0f));
        v->pda = 3.0f * k3;
        v->pd = 1.0f;
        v->pdk = dec_k(0.5f * fm_exp2f(-4.6f * k4));
        v->k2 = dec_k(0.03f);
        v->c1 = lp_c(1500.0f);
        break;
    case DM_FM: {                                     /* 2-op: stepped pitch, mod = the index and the
                                                       * modulator's ratio, feedback = the modulator's
                                                       * self-modulation */
        float steps = fm_floorf(k1 * 48.0f + 0.5f) + semi;
        hz = 55.0f * fm_exp2f(steps * (1.0f / 12.0f));
        v->inc2 = hz * (1.0f + 3.0f * k3) / SR;
        v->pda = 1.2f * k3;                           /* the index in turns, falling with env2 */
        v->k2 = dec_k(dec * 0.4f);
        v->a = 0.4f * k4;
        break;
    }
    }
    v->inc = hz / SR;
}

void drumkit_hit(drumkit_t *d, int sound, int vel, int pitch_semi, int short_gate, int long_tail)
{
    const uint8_t *p;
    dvoice_t *v;
    int mode, i;
    if (sound < 0 || sound >= DL_NSOUND)
        return;
    if (vel < 1) vel = 1;
    if (vel > 127) vel = 127;
    drumkit_hitv[sound] = (uint8_t)(vel >= 127 ? 255 : vel * 2);
    p = d->par + TP_DRUM(sound, 0);
    mode = p[DP_MODE] % NPM;
    if (mode >= PM_CHOKE1)                            /* a choke group: the others of the group go */
        for (i = 0; i < NDVOICE; i++)
            if (d->v[i].on && d->v[i].mode == mode && d->v[i].sound != sound)
                fade_out(&d->v[i]);
    v = alloc(d, sound);
    start(d, v, sound, vel, pitch_semi, (short_gate ? HF_SHORT : 0) | (long_tail ? HF_LONG : 0), 1);
}

/* ---- one voice, m samples (<= SUB) of mono into t[] ---- */
static void render_voice(dvoice_t *v, int16_t *line, float *t, int m)
{
    int i;
    float env = v->env, k = v->k, env2 = v->env2, k2 = v->k2, ph = v->ph, inc = v->inc, g = v->g;
    uint32_t rng = v->rng;
    switch (v->model) {
    case DM_KICK: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, drive = v->a, click = v->b, c = v->c1, f1 = v->f1;
        for (i = 0; i < m; i++) {
            float y;
            pd *= pdk;
            ph += inc * (1.0f + pda * pd);
            WRAP(ph);
            y = clip3(sin_t(ph) * env * drive) + noise(&rng) * env2 * click;
            f1 += c * (y - f1);
            t[i] = f1 * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->f1 = f1;
        break;
    }
    case DM_SNARE: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, ph2 = v->ph2, inc2 = v->inc2;
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2, body = v->a, snap = v->b;
        for (i = 0; i < m; i++) {
            float r = 1.0f + pda * pd, y, n;
            pd *= pdk;
            ph += inc * r;
            ph2 += inc2 * r;
            WRAP(ph);
            WRAP(ph2);
            n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            y = clip3((sin_t(ph) * 0.6f + sin_t(ph2) * 0.4f) * env2 * body) + n * env * snap;
            t[i] = y * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->ph2 = ph2;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_HIHAT: {
        uint32_t p0 = v->mp[0], p1 = v->mp[1], p2 = v->mp[2], p3 = v->mp[3], p4 = v->mp[4], p5 = v->mp[5];
        const uint32_t i0 = v->mi[0], i1 = v->mi[1], i2 = v->mi[2], i3 = v->mi[3], i4 = v->mi[4], i5 = v->mi[5];
        float c = v->pdk, f1 = v->f1, f2 = v->f2, f3 = v->f3, f4 = v->f4, c1 = v->c1, c2 = v->c2, c3 = v->c3;
        float era = v->a, tone = v->b, nz = 1.0f - era, mt = era * 0.6f, rm = era * 0.4f;
        for (i = 0; i < m; i++) {
            uint32_t cnt;
            float x, y, bp;
            p0 += i0; p1 += i1; p2 += i2; p3 += i3; p4 += i4; p5 += i5;
            cnt = (p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31);
            x = (float)(int32_t)(6 - 2 * (int32_t)cnt) * (1.0f / 6.0f) * mt;
            x += (float)(int32_t)(1 - 2 * (int32_t)((p0 ^ p3) >> 31)) * rm;   /* the ring-mod of two */
            x += noise(&rng) * nz;
            f1 += c * (x - f1);                       /* two one-pole high-passes */
            y = x - f1;
            f2 += c * (y - f2);
            y -= f2;
            bp = y * c3 + c1 * f3 + c2 * f4;          /* the bandpass focus */
            f4 = f3;
            f3 = bp;
            y += (bp - y) * tone;
            t[i] = y * (env + 0.5f * env2) * g;
            env *= k;
            env2 *= k2;
        }
        v->mp[0] = p0; v->mp[1] = p1; v->mp[2] = p2; v->mp[3] = p3; v->mp[4] = p4; v->mp[5] = p5;
        v->f1 = f1;
        v->f2 = f2;
        v->f3 = fm_flush(f3);
        v->f4 = fm_flush(f4);
        break;
    }
    case DM_CLAP: {
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2, tail = v->b;
        int32_t cnt = v->cnt, bursts = v->bursts, gap = (int32_t)v->a;
        for (i = 0; i < m; i++) {
            float n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            if (bursts && --cnt == 0) {               /* the next burst */
                cnt = gap;
                bursts--;
                env2 = 1.0f;
            }
            t[i] = n * (env2 + tail * env) * 0.45f * g;
            env *= k;
            env2 *= k2;
        }
        v->cnt = cnt;
        v->bursts = bursts;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_SHAKER: {
        float c1 = v->c1, c2 = v->c2, c3 = v->c3, f1 = v->f1, f2 = v->f2;
        int32_t cnt = v->cnt, bursts = v->bursts;
        for (i = 0; i < m; i++) {
            float n = noise(&rng) * c3 + c1 * f1 + c2 * f2;
            f2 = f1;
            f1 = n;
            if (bursts && --cnt == 0) {               /* the shake back: a second, softer hit */
                bursts--;
                env2 = 1.0f;
                env = fm_maxf(env, 0.7f);
            }
            t[i] = n * env * (1.0f - env2) * 0.8f * g;
            env *= k;
            env2 *= k2;
        }
        v->cnt = cnt;
        v->bursts = bursts;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_TAMB: {
        uint32_t p0 = v->mp[0], p1 = v->mp[1], p2 = v->mp[2], p3 = v->mp[3], p4 = v->mp[4], p5 = v->mp[5];
        const uint32_t i0 = v->mi[0], i1 = v->mi[1], i2 = v->mi[2], i3 = v->mi[3], i4 = v->mi[4], i5 = v->mi[5];
        float c = v->c1, f1 = v->f1, f2 = v->f2, nz = v->a, mt = v->b;
        for (i = 0; i < m; i++) {
            uint32_t cnt;
            float x, y;
            p0 += i0; p1 += i1; p2 += i2; p3 += i3; p4 += i4; p5 += i5;
            cnt = (p0 >> 31) + (p1 >> 31) + (p2 >> 31) + (p3 >> 31) + (p4 >> 31) + (p5 >> 31);
            x = (float)(int32_t)(6 - 2 * (int32_t)cnt) * (1.0f / 6.0f) * env2 * mt;
            x += noise(&rng) * env * nz;
            f1 += c * (x - f1);                       /* two one-pole high-passes */
            y = x - f1;
            f2 += c * (y - f2);
            y -= f2;
            t[i] = y * g;
            env *= k;
            env2 *= k2;
        }
        v->mp[0] = p0; v->mp[1] = p1; v->mp[2] = p2; v->mp[3] = p3; v->mp[4] = p4; v->mp[5] = p5;
        v->f1 = f1;
        v->f2 = f2;
        break;
    }
    case DM_RESO: {
        int32_t w = v->w, len = v->len;
        float fr = v->fr, lp = v->lp, fb = v->fb, damp = v->damp, pk = 0.0f, bl = v->a, c = v->c1, f1 = v->f1;
        for (i = 0; i < m; i++) {
            int32_t r0 = w - len, r1 = r0 - 1;
            float a, b, y, x;
            if (r0 < 0) r0 += RESO_LEN;
            if (r1 < 0) r1 += RESO_LEN;
            a = (float)line[r0];
            b = (float)line[r1];
            y = (a + (b - a) * fr) * (1.0f / 16384.0f);
            lp += damp * (y - lp);                    /* the loop's low-pass: the string darkens */
            f1 += c * (noise(&rng) - f1);             /* the strike: a burst, bright or soft */
            x = f1 * env2 * bl + lp * fb;
            x *= 16384.0f;
            line[w] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
            if (++w >= RESO_LEN)
                w = 0;
            t[i] = y * 1.4f * g;
            pk = fm_maxf(pk, fm_fabsf(y));
            env2 *= k2;
        }
        v->w = w;
        v->lp = fm_flush(lp);
        v->f1 = f1;
        v->pk = fm_maxf(pk, v->pk * 0.7f);            /* held over a period: a quiet stretch of the
                                                       * string is not its end */
        break;
    }
    case DM_CONGA: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, ph2 = v->ph2, inc2 = v->inc2, p2g = v->b, drive = v->a;
        float comp = v->c1;
        for (i = 0; i < m; i++) {
            float r = 1.0f + pda * pd, y;
            pd *= pdk;
            ph += inc * r;
            ph2 += inc2 * r;
            WRAP(ph);
            WRAP(ph2);
            y = sin_t(ph) * env + sin_t(ph2) * env2 * p2g;
            t[i] = clip3(y * drive) * comp * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->ph2 = ph2;
        break;
    }
    case DM_TOM: {
        float pd = v->pd, pdk = v->pdk, pda = v->pda, c = v->c1, f1 = v->f1;
        for (i = 0; i < m; i++) {
            float y;
            pd *= pdk;
            ph += inc * (1.0f + pda * pd);
            WRAP(ph);
            f1 += c * (noise(&rng) - f1);
            y = sin_t(ph) * env + f1 * env2 * 0.25f;
            t[i] = y * g;
            env *= k;
            env2 *= k2;
        }
        v->pd = pd;
        v->f1 = f1;
        break;
    }
    case DM_FM: {
        float ph2 = v->ph2, inc2 = v->inc2, idx = v->pda, fb = v->a, mo = v->f1;
        for (i = 0; i < m; i++) {
            ph += inc;
            ph2 += inc2;
            WRAP(ph);
            WRAP(ph2);
            mo = sin_t_any(ph2 + mo * fb);               /* the modulator, with its own feedback */
            t[i] = sin_t_any(ph + mo * idx * env2) * env * g;
            env *= k;
            env2 *= k2;
        }
        v->ph2 = ph2;
        v->f1 = mo;
        break;
    }
    default:
        for (i = 0; i < m; i++)
            t[i] = 0.0f;
        env = 0.0f;
        break;
    }
    v->env = env;
    v->env2 = env2;
    v->ph = ph;
    v->rng = rng;
    v->age += (uint32_t)m;
    if (v->model == DM_RESO) {
        if (env2 < 1e-3f && v->pk < 1e-4f && v->age > 4u * (uint32_t)v->len)
            v->on = 0;
    } else if (env < 1e-4f && env2 < 1e-4f) {
        v->on = 0;
    }
}

/* the key's volume in dB: 0 silent, 1..76 = -30..0 dB, 76..127 = 0..+20 dB */
static float volume_lin(int v)
{
    if (v <= 0)
        return 0.0f;
    if (v < VOL0)
        return fm_db2lin((float)(v - VOL0) * (30.0f / (float)(VOL0 - 1)));
    return fm_db2lin((float)(v - VOL0) * (20.0f / (float)(127 - VOL0)));
}

/* the chunk's targets from par[]: volume, pan, the track's sends; ramped over m samples */
static void control(const uint8_t *par, dvoice_t *v, int m)
{
    const uint8_t *p = par + TP_DRUM(v->sound, 0);
    float a = (float)p[DP_PAN] * (1.0f / 127.0f), lin, l, r, s1, s2, st, im;
    lin = volume_lin(p[DP_VOLUME]);
    l = lin * fm_sin_turns(0.25f * (1.0f - a));       /* constant power: cos, sin of a quarter turn */
    r = lin * fm_sin_turns(0.25f * a);
    s1 = lin * (float)par[TP_SEND1] * (1.0f / 127.0f);
    s2 = lin * (float)par[TP_SEND2] * (1.0f / 127.0f);
    st = lin * (float)par[TP_SENDT] * (1.0f / 127.0f);
    if (v->age == 0) {                                /* a fresh voice starts at its gains */
        v->gl = l; v->gr = r; v->gs1 = s1; v->gs2 = s2; v->gst = st;
    }
    im = 1.0f / (float)m;
    v->dgl = (l - v->gl) * im;
    v->dgr = (r - v->gr) * im;
    v->gs1 = s1;                                      /* the sends step: a send's zipper is buried in the effect */
    v->gs2 = s2;
    v->gst = st;
}

/* ---- the dynamic filter ---- */
__attribute__((minsize)) void drumkit_filter(drumkit_t *d, int type, int locut, int comp, int drive, int hicut)
{
    dynf_t *f = &d->f;
    float c = fm_clampf((float)comp * (1.0f / 127.0f), 0.0f, 1.0f);
    float dr = fm_clampf((float)drive * (1.0f / 127.0f), 0.0f, 1.0f);
    if (type != FT_DYNAMIC) {
        if (f->on) {                                  /* leaving: the states start clean next time */
            int i;
            for (i = 0; i < 4; i++)
                f->lo[i] = f->hi[i] = 0.0f;
            f->env = 0.0f;
            f->gain = 1.0f;
        }
        f->on = 0;
        return;
    }
    f->on = 1;
    f->lo_on = (uint8_t)(locut > 0);
    f->hi_on = (uint8_t)(hicut < 127);
    f->drv_on = (uint8_t)(drive > 0);
    /* low cut 20 Hz .. 2 kHz, hi cut 300 Hz .. 20 kHz; each one-pole's cutoff is raised a little so that
     * the pair in series is -3 dB at the printed frequency */
    f->lc = lp_c(20.0f * fm_exp2f((float)locut * (6.64f / 127.0f)) * 0.64f);
    f->hc = lp_c(300.0f * fm_exp2f((float)hicut * (6.06f / 127.0f)) * 1.55f);
    /* the compressor: the threshold falls to -24 dB, the ratio rises to 10:1, with make-up gain */
    f->thr = fm_db2lin(-24.0f * c);
    f->slope = 1.0f - 1.0f / (1.0f + 9.0f * c);
    f->makeup = fm_db2lin(18.0f * c);
    /* the drive: a tanh shape on the signal times 1..16 */
    f->drive = 1.0f + 15.0f * dr * dr;
    f->dcomp = 0.6f + 0.4f / f->drive;
}

/* tanh's shape for a few instructions: x (27 + x^2) / (27 + 9 x^2) (a Pade form), flat past +-3 */
static inline float tanh_r(float x)
{
    float x2;
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* m samples (<= SUB) of the dry pair through the filter, in place */
static void dynf_run(dynf_t *f, float *l, float *r, int m)
{
    int i;
    float pk = 0.0f, g0 = f->gain, g1, dg;
    if (f->lo_on) {
        float c = f->lc, a0 = f->lo[0], a1 = f->lo[1], b0 = f->lo[2], b1 = f->lo[3];
        for (i = 0; i < m; i++) {
            float x = l[i], y;
            a0 += c * (x - a0);
            y = x - a0;
            a1 += c * (y - a1);
            l[i] = y - a1;
            x = r[i];
            b0 += c * (x - b0);
            y = x - b0;
            b1 += c * (y - b1);
            r[i] = y - b1;
        }
        f->lo[0] = fm_flush(a0); f->lo[1] = fm_flush(a1); f->lo[2] = fm_flush(b0); f->lo[3] = fm_flush(b1);
    }
    if (f->hi_on) {
        float c = f->hc, a0 = f->hi[0], a1 = f->hi[1], b0 = f->hi[2], b1 = f->hi[3];
        for (i = 0; i < m; i++) {
            a0 += c * (l[i] - a0);
            a1 += c * (a0 - a1);
            l[i] = a1;
            b0 += c * (r[i] - b0);
            b1 += c * (b0 - b1);
            r[i] = b1;
        }
        f->hi[0] = fm_flush(a0); f->hi[1] = fm_flush(a1); f->hi[2] = fm_flush(b0); f->hi[3] = fm_flush(b1);
    }
    /* the compressor: a peak follower (instant up, ~100 ms down, stepped per sub-block), the gain
     * ramped across the block */
    for (i = 0; i < m; i++)
        pk = fm_maxf(pk, fm_maxf(fm_fabsf(l[i]), fm_fabsf(r[i])));
    if (pk > f->env)
        f->env = pk;
    else
        f->env += (pk - f->env) * (0.0073f * (float)m * (1.0f / 32.0f));
    g1 = f->makeup;
    if (f->env > f->thr)
        g1 *= fm_exp2f(-f->slope * fm_log2f(f->env / f->thr));
    f->gain = g1;
    dg = (g1 - g0) * (1.0f / (float)m);
    if (f->drv_on) {
        float dv = f->drive, dc = f->dcomp;
        for (i = 0; i < m; i++) {
            g0 += dg;
            l[i] = tanh_r(l[i] * g0 * dv) * dc;
            r[i] = tanh_r(r[i] * g0 * dv) * dc;
        }
    } else {
        for (i = 0; i < m; i++) {
            g0 += dg;
            l[i] *= g0;
            r[i] *= g0;
        }
    }
}

void drumkit_render(drumkit_t *d, float *l, float *r, float *s1, float *s2, float *st, uint32_t n)
{
    uint32_t c;
    for (c = 0; c < n; c += CR) {
        int m = (int)(n - c < CR ? n - c : CR), vi, o;
        for (vi = 0; vi < NDVOICE; vi++)
            if (d->v[vi].on)
                control(d->par, &d->v[vi], m);
        for (o = 0; o < m; o += SUB) {
            float t[SUB], dl[SUB], dr[SUB];
            int mm = m - o < SUB ? m - o : SUB, i, any = 0;
            float *p1 = s1 + c + o, *p2 = s2 + c + o, *pt = st + c + o;
            for (i = 0; i < mm; i++)
                dl[i] = dr[i] = 0.0f;
            for (vi = 0; vi < NDVOICE; vi++) {
                dvoice_t *v = &d->v[vi];
                float gl, gr, att, attk;
                const float gs1 = v->gs1, gs2 = v->gs2, gst = v->gst, dgl = v->dgl, dgr = v->dgr;
                int from = 0;
                if (!v->on)
                    continue;
                if (v->wait > 0) {                    /* funk: the hit has not started yet */
                    if (v->wait >= mm) {
                        v->wait -= mm;
                        continue;
                    }
                    from = v->wait;
                    v->wait = 0;
                    for (i = 0; i < from; i++)
                        t[i] = 0.0f;
                }
                any = 1;
                render_voice(v, d->line[v->line], t + from, mm - from);
                gl = v->gl;
                gr = v->gr;
                att = v->att;
                attk = v->attk;
                for (i = 0; i < mm; i++) {
                    float x = t[i] * att;
                    att += attk;
                    if (att > 1.0f)
                        att = 1.0f;
                    gl += dgl;
                    gr += dgr;
                    dl[i] += x * gl;
                    dr[i] += x * gr;
                    p1[i] += x * gs1;
                    p2[i] += x * gs2;
                    pt[i] += x * gst;
                }
                v->gl = gl;
                v->gr = gr;
                v->att = att;
                if (!v->on) {
                    if (v->model == DM_RESO && d->line_owner[v->line] == vi)
                        d->line_owner[v->line] = -1;
                    if (v->held && v->mode == PM_LOOP)   /* loop: from the start again while held */
                        start(d, v, v->sound, v->hvel, v->hpitch, v->hflags, 0);
                }
            }
            if (d->f.on && (any || d->f.env > 1e-5f))
                dynf_run(&d->f, dl, dr, mm);
            for (i = 0; i < mm; i++) {
                l[c + o + i] += dl[i];
                r[c + o + i] += dr[i];
            }
        }
    }
}

int drumkit_sounding(const drumkit_t *d)
{
    int i;
    for (i = 0; i < NDVOICE; i++)
        if (d->v[i].on)
            return 1;
    return 0;
}
