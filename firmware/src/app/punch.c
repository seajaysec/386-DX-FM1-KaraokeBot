/* SPDX-License-Identifier: GPL-3.0-only */
/* Pump, filter and punch-in effects (punch.h).
 *
 * The pump: each kick drops the mix's gain by up to 9 dB, rising over 12 ms so the kick's own attack
 * gets through, then recovering over about a beat and a half's worth of 200 ms.
 * The filter: a state-variable filter, its cutoff swept from 18 kHz down to 70 Hz on the left half of
 * the knob and a high-pass from 20 Hz up to 7 kHz on the right half; the middle is off (bypassed).
 * The punch-in effects: sources (stutters, tape stop, reverse, half speed: one at a time, the lowest
 * held) play from a history of the last ~280 ms (mono, 22 kHz, frozen while a source plays); then the
 * filters, the crushers, the ring modulator, the chops and the mute, on top. */
#include "punch.h"
#include "../dsp/fastmath.h"
#include <string.h>

#ifdef DX_HOST
#define POOL
#else
#define POOL __attribute__((section(".pool")))
#endif

/* each is drawn in a tile 53 px wide: whole short words, never clipped */
const char *const PUNCH_NAME[PU_N] = {"lo-pass", "hi-pass", "phone", "crush", "brrr 8", "brrr 16",
                                      "brrr 32", "brrr 64", "tape off", "reverse", "chop 8",
                                      "chop 16", "ring mod", "half time", "mute", "lo-fi"};
#define SOURCES (1u << PU_STUTTER8 | 1u << PU_STUTTER16 | 1u << PU_STUTTER32 | 1u << PU_STUTTER64 | \
                 1u << PU_TAPESTOP | 1u << PU_REVERSE | 1u << PU_HALF)
#define HIST 6144u                    /* half-rate samples: ~279 ms */
static int16_t hist[HIST] POOL;

static volatile uint32_t held;
static uint32_t beat = 26460u;        /* a beat in samples at 44.1 kHz (100 BPM) */
static struct {
    uint32_t hw, half, src, start, len, crush_n, chop_n, held_was;
    float pos, speed;
    float lp[2][2], hp[2][2], bp[2][2];
    float gate, ring_c, ring_s, crush_l, crush_r;
    float pump_env, pump_target;      /* the duck now (0..1) and where it is going */
    float f_ic1[2], f_ic2[2], f_g;    /* the master filter */
} p;

void punch_init(void)
{
    memset(&p, 0, sizeof p);
    memset(hist, 0, sizeof hist);
    p.src = PU_N;
    p.gate = 1.0f;
    p.ring_c = 1.0f;
}

void punch_set(uint32_t h) { held = h; }
void punch_tempo(unsigned bpm) { beat = 44100u * 60u / (bpm ? bpm : 100u); }
void punch_pump(unsigned velocity) { p.pump_target = (float)velocity / 127.0f; }

/* a Chamberlin state-variable filter: f = 2 sin(pi fc / fs), q = 1 / Q; returns low, band or high */
static inline float svf(float *st, float x, float f, float q, int out)
{
    float lo = st[0] + f * st[1], hi = x - lo - q * st[1], bd = f * hi + st[1];
    st[0] = lo;
    st[1] = bd;
    return out == 0 ? lo : out == 1 ? bd : hi;
}

static float hist_at(float pos)
{
    uint32_t i = (uint32_t)pos;
    float t = pos - (float)i, a = hist[i % HIST], b = hist[(i + 1u) % HIST];
    return (a + (b - a) * t) * (1.0f / 32768.0f);
}

static void effects(float *l, float *r, uint32_t h)
{
    uint32_t src = PU_N, k;
    float m;
    for (k = 0; k < PU_N; k++)
        if ((h >> k & 1u) && (SOURCES >> k & 1u)) {
            src = k;
            break;
        }
    if (src != p.src) {
        p.src = src;
        if (src >= PU_STUTTER8 && src <= PU_STUTTER64) {
            uint32_t n = beat >> (2u + (src - PU_STUTTER8));     /* an eighth .. a 64th at half rate */
            while (n > HIST - 64u)
                n >>= 1;
            p.len = n ? n : 1u;
            p.start = (p.hw + HIST - p.len) % HIST;
            p.pos = (float)p.start;
            p.speed = 0.5f;
        } else if (src == PU_TAPESTOP || src == PU_HALF) {
            p.start = (p.hw + 1u) % HIST;
            p.pos = (float)p.start;
            p.speed = src == PU_TAPESTOP ? 0.5f : 0.25f;
        } else if (src == PU_REVERSE) {
            p.pos = (float)((p.hw + HIST - 1u) % HIST);
            p.speed = -0.5f;
        }
    }
    if (src == PU_N) {
        if (++p.half & 1u) {
            float x = (*l + *r) * 16383.0f;
            hist[p.hw = (p.hw + 1u) % HIST] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
        }
    } else {
        float v = hist_at(p.pos);
        *l = *r = v;
        p.pos += p.speed;
        if (src >= PU_STUTTER8 && src <= PU_STUTTER64) {
            if (p.pos >= (float)p.start + (float)p.len)
                p.pos -= (float)p.len;
        } else if (src == PU_TAPESTOP)
            p.speed *= 0.99996f;
        if (p.pos >= (float)HIST)
            p.pos -= (float)HIST;
        if (p.pos < 0.0f)
            p.pos += (float)HIST;
    }
    if (h >> PU_LOWPASS & 1u) {
        *l = svf(p.lp[0], *l, 0.0712f, 0.5f, 0);
        *r = svf(p.lp[1], *r, 0.0712f, 0.5f, 0);
    }
    if (h >> PU_HIGHPASS & 1u) {
        *l = svf(p.hp[0], *l, 0.2133f, 0.7f, 2);
        *r = svf(p.hp[1], *r, 0.2133f, 0.7f, 2);
    }
    if (h >> PU_PHONE & 1u) {
        m = svf(p.bp[0], (*l + *r) * 0.5f, 0.1424f, 0.6f, 1) * 2.5f;
        m = m > 0.8f ? 0.8f : m < -0.8f ? -0.8f : m;
        *l = *r = m;
    }
    if (h & (1u << PU_CRUSH | 1u << PU_LOFI)) {
        uint32_t hold = h >> PU_CRUSH & 1u ? 6u : 2u;
        float lv = h >> PU_CRUSH & 1u ? 12.0f : 48.0f;
        if (++p.crush_n >= hold) {
            p.crush_n = 0;
            p.crush_l = (float)(int32_t)(*l * lv) / lv;
            p.crush_r = (float)(int32_t)(*r * lv) / lv;
        }
        *l = p.crush_l;
        *r = p.crush_r;
    }
    if (h >> PU_RING & 1u) {
        float c = p.ring_c, s = p.ring_s, e = 0.04700f;          /* 330 Hz */
        p.ring_c = c - e * s;
        p.ring_s = s + e * p.ring_c;
        *l *= p.ring_s;
        *r *= p.ring_s;
    }
    {
        float target = 1.0f;
        uint32_t chops = h & (1u << PU_CHOP8 | 1u << PU_CHOP16);
        if (chops & ~p.held_was)
            p.chop_n = 0;
        p.held_was = h;
        p.chop_n++;
        if (chops) {
            uint32_t period = h >> PU_CHOP16 & 1u ? beat >> 2 : beat >> 1;
            if (period < 64u)
                period = 64u;
            target = p.chop_n % period < period / 2u ? 1.0f : 0.0f;
        }
        if (h >> PU_MUTE & 1u)
            target = 0.0f;
        p.gate += (target - p.gate) * 0.011f;
        *l *= p.gate;
        *r *= p.gate;
    }
}

void punch_process(float *l, float *r, uint32_t n, float pump, float filter)
{
    uint32_t h = held;
    /* the filter's coefficient once a block: g = tan(pi fc / fs), gliding */
    int mode = filter < 47.0f ? 1 : filter > 53.0f ? 2 : 0;
    float g_t = 0.0f;
    if (mode == 1)
        g_t = fm_tanf(FM_PI * (70.0f * fm_exp2f((filter / 47.0f) * 8.0f)) / 44100.0f);
    else if (mode == 2)
        g_t = fm_tanf(FM_PI * (20.0f * fm_exp2f(((filter - 53.0f) / 47.0f) * 8.45f)) / 44100.0f);
    if (mode)
        p.f_g += 0.5f * (g_t - p.f_g);
    float k = 1.2f, a1 = 1.0f / (1.0f + p.f_g * (p.f_g + k));
    for (uint32_t i = 0; i < n; i++) {
        /* the pump: a kick sets a target, the duck eases in (12 ms) and out (200 ms) */
        if (pump > 0.0f) {
            float want = p.pump_target * pump;
            p.pump_env += (want > p.pump_env ? 0.0019f : 0.000113f) * (want - p.pump_env);
            p.pump_target *= 0.9996f;
            float gain = 1.0f - 0.65f * p.pump_env;
            l[i] *= gain;
            r[i] *= gain;
        }
        if (mode) {
            for (int c = 0; c < 2; c++) {
                float *x = c ? &r[i] : &l[i];
                float v3 = *x - p.f_ic2[c], v1 = a1 * (p.f_ic1[c] + p.f_g * v3), v2 = p.f_ic2[c] + p.f_g * v1;
                p.f_ic1[c] = 2.0f * v1 - p.f_ic1[c];
                p.f_ic2[c] = 2.0f * v2 - p.f_ic2[c];
                *x = mode == 1 ? v2 : *x - k * v1 - v2;
            }
        }
        if (h || p.src != PU_N)
            effects(&l[i], &r[i], h);
        else if (++p.half & 1u) {
            float x = (l[i] + r[i]) * 16383.0f;
            hist[p.hw = (p.hw + 1u) % HIST] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
            p.gate = 1.0f;
        }
    }
}
