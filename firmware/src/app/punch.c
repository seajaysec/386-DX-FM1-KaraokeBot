/* SPDX-License-Identifier: GPL-3.0-only */
/* Pump, filter and punch-in effects (punch.h).
 *
 * The pump: each kick drops the mix's gain by up to 9 dB, rising over 12 ms so the kick's own attack
 * gets through, then recovering over about a beat and a half's worth of 200 ms.
 * The filter: a state-variable filter, its cutoff swept from 18 kHz down to 70 Hz on the left half of
 * the knob and a high-pass from 20 Hz up to 7 kHz on the right half; the middle is off (bypassed).
 * The punch-in effects: sources (the brrrs and tape stop: one at a time, the lowest held) play from a
 * history of the last ~280 ms (mono, 22 kHz, frozen while a source plays); then the filters, the crushers,
 * the ring modulator and the chops, on top. Build is the engine's (engine.c): a snare roll and a riser.
 * SLAMJAM (GLO) has its own tone family: a wah swept twice a beat, a dub echo on a triplet eighth that rings on after
 * the key is let go, a scratch (the last eighth rocked back and forth, a source as the brrrs are) and an air horn;
 * the brrr keys are the engine's snaps there (engine.c), and are no sources. */
#include "punch.h"
#include "../dsp/fastmath.h"
#include <string.h>

#ifdef DX_HOST
#define POOL
#else
#define POOL __attribute__((section(".pool")))
#endif

/* each is drawn in a tile 53 px wide: whole short words, never clipped */
/* the chops and brrrs say how strong with their last letters */
const char *const PUNCH_NAME[PU_N] = {"lo-pass", "hi-pass", "GAMEBOIII", "ring mod", "chop", "choppp", "chopppppp",
                                      "choppppppppp", "brr", "brrrrr", "brrrrrrrr", "brrrrrrrrrrr", "build",
                                      "crush", "lo-fi", "tape off"};
#define SOURCES (1u << PU_STUTTER8 | 1u << PU_STUTTER16 | 1u << PU_STUTTER32 | 1u << PU_STUTTER64 | 1u << PU_TAPESTOP)
#define SLAM_SOURCES (1u << PU_SCRATCH | 1u << PU_TAPESTOP)
#define HIST 6144u                    /* half-rate samples: ~279 ms */
static int16_t hist[HIST] POOL;
#define ECHO_N 3328u                  /* SLAMJAM's echo line, quarter-rate samples: ~302 ms */
static int16_t echo_line[ECHO_N] POOL;

static volatile uint32_t held;
static volatile uint8_t poetry;
/* SLAMJAM's: the tone family's own, then the snaps (saying how many with their a's) on the brrr keys */
static const char *const SLAM_NAME[8] = {"wah", "echo", "scratch", "air horn", "snap", "snaap", "snaaaap", "snaaaaaap"};
void punch_poetry(int on) { poetry = (uint8_t)(on != 0); }
const char *punch_name(unsigned k)
{
    return poetry && k <= PU_AIRHORN ? SLAM_NAME[k] : poetry && k >= PU_STUTTER8 && k <= PU_STUTTER64
               ? SLAM_NAME[4 + k - PU_STUTTER8] : PUNCH_NAME[k % PU_N];
}
static uint32_t beat = 26460u;        /* a beat in samples at 44.1 kHz (100 BPM) */
static struct {
    uint32_t hw, half, src, start, len, crush_n, chop_n, held_was, reps;
    float pos, speed, side;
    float lp[2][2], hp[2][2], bp[2][2];
    float gate, ring_c, ring_s, crush_l, crush_r;
    float pump_env, pump_target;      /* the duck now (0..1) and where it is going */
    float f_ic1[2], f_ic2[2], f_g;    /* the master filter */
    /* SLAMJAM: the wah's sweep (0..1 up and down); the echo's darkening, its feedback and how long it rings on;
     * the scratch's back and forth (a rotating pair); the air horn's phases, gate and tone */
    float wah, echo_lp, echo_hp, echo_fb, echo_out, scr_c, scr_s, scr_at, horn_ph[5], horn_env, horn_lp;
    float snap_env, snap_pan, snap_bp[2], echo_env, snap_f;
    uint32_t wah_n, echo_tail, echo_w, echo_q, horn_n, noise, snaps;
    uint8_t horn_held;
} p;

void punch_init(void)
{
    memset(&p, 0, sizeof p);
    memset(hist, 0, sizeof hist);
    memset(echo_line, 0, sizeof echo_line);
    p.src = PU_N;
    p.gate = 1.0f;
    p.ring_c = 1.0f;
    p.noise = 22222u;
}

void punch_set(uint32_t h) { held = h; }
void punch_tempo(unsigned bpm) { beat = 44100u * 60u / (bpm ? bpm : 100u); }
void punch_pump(unsigned velocity) { p.pump_target = (float)velocity / 127.0f; }
float punch_duck(void) { return p.pump_env; }
float punch_gate(void) { return p.gate; }
uint32_t punch_reps(void) { return p.reps; }
void punch_snap(unsigned velocity)
{
    if (!velocity) {                                           /* (let go: the next snaps start low again) */
        p.snap_f = 0.0f;
        return;
    }
    p.snap_f = p.snap_f < 0.3f ? 0.3f : p.snap_f > 0.85f ? 0.85f : p.snap_f * 1.025f;   /* each a little higher */
    p.snap_env = (float)velocity * (1.0f / 127.0f);
    p.snap_pan = (float)(++p.snaps * 7u % 5u) * 0.2f;        /* someone else in the room each time */
}
void punch_look(uint8_t *look)
{
    float e = p.echo_env;
    look[0] = (uint8_t)(p.wah * 127.0f);
    look[1] = (uint8_t)(e * 400.0f > 127.0f ? 127.0f : e * 400.0f);
    look[2] = (uint8_t)(64.0f + 63.0f * (p.src == PU_SCRATCH ? p.scr_c : 1.0f));
    look[3] = (uint8_t)(p.horn_env * 127.0f);
}

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
        if ((h >> k & 1u) && ((poetry ? SLAM_SOURCES : SOURCES) >> k & 1u)) {
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
            p.side = 0.0f;
        } else if (src == PU_TAPESTOP) {
            p.start = (p.hw + 1u) % HIST;
            p.pos = (float)p.start;
            p.speed = 0.5f;
        } else if (src == PU_SCRATCH) {
            /* the record's last sixteenth: pulled back over it and pushed on again, an eighth a stroke */
            uint32_t n = beat >> 3;
            while (n > HIST - 64u)
                n >>= 1;
            p.len = n;
            p.start = (p.hw + HIST - n) % HIST;
            p.scr_c = 1.0f;
            p.scr_s = 0.0f;
            p.scr_at = 0.0f;
        }
    }
    if (src == PU_N) {
        if (++p.half & 1u) {
            float x = (*l + *r) * 16383.0f;
            hist[p.hw = (p.hw + 1u) % HIST] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
        }
    } else if (src == PU_SCRATCH) {
        /* a baby scratch: the hand's position a cosine over a stroke, so the record runs fastest mid-stroke and is
         * silent as it turns; muffled as a record under a hand is */
        float e = FM_TWO_PI / (float)(beat >> 1), c = p.scr_c, sn = p.scr_s;
        p.scr_c = c - e * sn;
        p.scr_s = sn + e * p.scr_c;
        float pos = (float)p.start + (float)p.len * 0.5f * (1.0f - p.scr_c);
        float v = hist_at(pos), sp = p.scr_s < 0.0f ? -p.scr_s : p.scr_s;
        p.scr_at += 0.35f * (v - p.scr_at);
        v = p.scr_at * (sp * 1.8f > 1.0f ? 1.0f : sp * 1.8f) * 0.7f;
        *l = *r = v;
    } else {
        float v = hist_at(p.pos);
        *l = *r = v;
        p.pos += p.speed;
        if (src >= PU_STUTTER8 && src <= PU_STUTTER64) {
            /* a brrr: each repeat a little higher and, past brrr 16, a little shorter (a roll); the repeats
             * bounce between the sides; each slice fades in and out over 24 samples, so the loop never clicks */
            static const float RISE[4] = {1.0f, 1.0293f, 1.0595f, 1.1225f};    /* 0, 1/2, 1, 2 semitones */
            static const float ROLL[4] = {1.0f, 1.0f, 0.94f, 0.88f};
            unsigned n = src - PU_STUTTER8;
            float in = p.pos - (float)p.start, edge = in < (float)p.len - in ? in : (float)p.len - in;
            edge = edge < 0.0f ? 0.0f : edge;
            if (edge < 24.0f) {
                *l *= edge * (1.0f / 24.0f);
                *r *= edge * (1.0f / 24.0f);
            }
            *l *= 1.0f - 0.7f * p.side;
            *r *= 1.0f - 0.7f * (1.0f - p.side);
            if (p.pos >= (float)p.start + (float)p.len) {
                p.pos -= (float)p.len;
                p.reps++;
                p.side = 1.0f - p.side;
                p.speed *= RISE[n];
                if (p.speed > 1.0f)
                    p.speed = 1.0f;                   /* an octave up at most */
                uint32_t len = (uint32_t)((float)p.len * ROLL[n]);
                p.len = len < 48u ? 48u : len;
            }
        } else if (src == PU_TAPESTOP)
            p.speed *= 0.99996f;
        /* (a brrr's loop may run past the history's end: hist_at reads round it, so it is not wrapped here) */
        if (p.pos >= (float)HIST && !(src >= PU_STUTTER8 && src <= PU_STUTTER64))
            p.pos -= (float)HIST;
    }
    if (poetry) {
        if ((h >> PU_ECHO & 1u) || p.echo_tail) {
            /* the echo: its own line, a triplet eighth long at a quarter of the rate, darkened and thinned each time
             * round; held, it takes the mix in and builds (the feedback rising to 0.75); let go, what is in it rings
             * on for up to 4 s and nothing new goes in (a dub throw) */
            int on = h >> PU_ECHO & 1u;
            if (on && !p.echo_tail)
                p.echo_fb = 0.5f;
            p.echo_tail = on ? 176400u : p.echo_tail - 1u;
            if (++p.echo_q >= 4u) {
                p.echo_q = 0;
                uint32_t d = beat / 12u;
                while (d > ECHO_N - 4u)
                    d >>= 1;
                float e = (float)echo_line[(p.echo_w + ECHO_N - d) % ECHO_N] * (1.0f / 32768.0f);
                p.echo_hp += 0.04f * (e - p.echo_hp);
                e -= p.echo_hp;
                p.echo_fb += on ? (0.75f - p.echo_fb) * 0.0001f : 0.0f;
                float x = (on ? (*l + *r) * 0.5f : 0.0f) + fm_tanhf(e * p.echo_fb * 1.3f) * 0.77f;
                x *= 32767.0f;
                echo_line[p.echo_w = (p.echo_w + 1u) % ECHO_N] =
                    (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
                p.echo_lp = e;
            }
            p.echo_out += 0.12f * (p.echo_lp - p.echo_out);          /* (smoothed: the line is a quarter rate) */
            float a = p.echo_out < 0.0f ? -p.echo_out : p.echo_out;
            p.echo_env = a > p.echo_env ? a : p.echo_env * 0.99995f;
            p.echo_env *= p.echo_tail > 1u;
            *l += p.echo_out * 0.42f;
            *r += p.echo_out * 0.34f;
            if (!p.echo_tail)
                memset(echo_line, 0, sizeof echo_line);
        }
        if (h >> PU_WAH & 1u) {
            /* the wah: a resonant band swept 350 Hz to 2.5 kHz and back on each eighth, and a little of the dry */
            uint32_t per = beat >> 1;
            p.wah_n = (p.wah_n + 1u) % (per ? per : 1u);
            float tri = (float)p.wah_n / (float)per;
            tri = tri < 0.5f ? tri * 2.0f : 2.0f - tri * 2.0f;
            p.wah = tri;
            float f = (350.0f + 2200.0f * tri * tri) * (FM_TWO_PI / 44100.0f);
            *l = svf(p.lp[0], *l, f, 0.2f, 1) * 0.85f + *l * 0.15f;
            *r = svf(p.lp[1], *r, f, 0.2f, 1) * 0.85f + *r * 0.15f;
        } else
            p.wah = 0.0f;
        if (p.snap_env > 0.002f) {
            /* a finger snap: noise through a band from 2.1 kHz, each snap held a little higher (to 6.4 kHz), gone in about
             * 10 ms, somewhere across the room */
            p.noise ^= p.noise << 13;
            p.noise ^= p.noise >> 17;
            p.noise ^= p.noise << 5;
            float n = (float)(int32_t)p.noise * (1.0f / 2147483648.0f);
            float v = svf(p.snap_bp, n, p.snap_f, 0.35f, 1) * p.snap_env * 0.6f;
            p.snap_env *= 0.9972f;
            *l += v * (1.2f - p.snap_pan);
            *r += v * (0.4f + p.snap_pan);
        }
        if ((h >> PU_AIRHORN & 1u) || p.horn_n) {
            /* the air horn, one blast a press: a low honk, a shriek up to about G6, down to D5 a little sharp where it
             * holds with the octave below, the fifth and two octaves above, then dying away; three seconds in all */
            int on = h >> PU_AIRHORN & 1u;
            if (on && !p.horn_held)
                p.horn_n = 1;                          /* (pressed again: blown again) */
            p.horn_held = (uint8_t)on;
            float t = (float)p.horn_n * (1.0f / 44100.0f), hz, amp = 1.0f, layer = 0.0f;
            if (t < 0.20f) {                           /* the honk: 335 to 395 Hz, hit hard */
                hz = 335.0f + 300.0f * t;
                amp = t < 0.012f ? t * (1.0f / 0.012f) : 1.0f;
            } else if (t < 0.50f) {                    /* up to 1500 Hz fast, and held up there */
                float u = (t - 0.20f) * (1.0f / 0.15f);
                hz = 395.0f * fm_exp2f((u < 1.0f ? u : 1.0f) * 1.925f);
            } else if (t < 0.82f)                      /* down to 605 Hz */
                hz = 1500.0f * fm_exp2f((t - 0.50f) * (1.0f / 0.32f) * -1.310f);
            else {                                     /* 603 Hz (D5 47 cents sharp) and its chord; fading, then gone */
                hz = 603.0f;
                layer = t < 0.92f ? (t - 0.82f) * 10.0f : 1.0f;
                amp = t < 1.42f ? 1.0f - 0.3f * (t - 0.82f) * (1.0f / 0.6f) : 0.7f * (1.0f - (t - 1.42f) * (1.0f / 1.58f));
            }
            if (amp <= 0.0f) {
                amp = 0.0f;
                p.horn_n = 0;
            } else
                p.horn_n++;
            p.horn_env = amp;
            static const float MUL[5] = {1.0f, 0.5f, 1.5f, 2.0f, 4.0f}, GAIN[5] = {1.0f, 0.6f, 0.5f, 0.4f, 0.25f};
            float v = 0.0f;
            for (int o = 0; o < 5; o++) {
                if (o && layer <= 0.0f)
                    break;
                p.horn_ph[o] += hz * MUL[o] * (1.0f / 44100.0f);
                p.horn_ph[o] -= p.horn_ph[o] >= 1.0f ? 1.0f : 0.0f;
                v += (p.horn_ph[o] * 2.0f - 1.0f) * GAIN[o] * (o ? layer : 1.0f);
            }
            v = fm_tanhf(v * 1.6f);                    /* harsh */
            p.horn_lp += 0.45f * (v - p.horn_lp);
            v = p.horn_lp * amp * 0.16f;
            *l += v;
            *r += v;
        }
    }
    if (!poetry && (h >> PU_LOWPASS & 1u)) {
        *l = svf(p.lp[0], *l, 0.0712f, 0.5f, 0);
        *r = svf(p.lp[1], *r, 0.0712f, 0.5f, 0);
    }
    if (!poetry && (h >> PU_HIGHPASS & 1u)) {
        *l = svf(p.hp[0], *l, 0.2133f, 0.7f, 2);
        *r = svf(p.hp[1], *r, 0.2133f, 0.7f, 2);
    }
    if (!poetry && (h >> PU_PHONE & 1u)) {
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
    if (!poetry && (h >> PU_RING & 1u)) {
        float c = p.ring_c, s = p.ring_s, e = 0.04700f;          /* 330 Hz */
        p.ring_c = c - e * s;
        p.ring_s = s + e * p.ring_c;
        *l *= p.ring_s;
        *r *= p.ring_s;
    }
    {
        float target = 1.0f;
        uint32_t chops = h & (1u << PU_CHOP8 | 1u << PU_CHOP16 | 1u << PU_CHOP32 | 1u << PU_CHOP64);
        if (chops & ~p.held_was)
            p.chop_n = 0;
        p.held_was = h;
        p.chop_n++;
        if (chops) {                                   /* the fastest held: 8, 16, 32 or 64 to the bar */
            uint32_t period = beat >> (h >> PU_CHOP64 & 1u ? 4 : h >> PU_CHOP32 & 1u ? 3 : h >> PU_CHOP16 & 1u ? 2 : 1);
            if (period < 64u)
                period = 64u;
            target = p.chop_n % period < period / 2u ? 1.0f : 0.0f;
        }
        p.gate += (target - p.gate) * 0.011f;
        *l *= p.gate;
        *r *= p.gate;
    }
    if (poetry)                                        /* (the echo, snaps and horn come after the limiter: kept in) */
        for (int c = 0; c < 2; c++) {
            float *x = c ? r : l, a = *x < 0.0f ? -*x : *x;
            if (a > 0.85f)
                *x = (*x < 0.0f ? -1.0f : 1.0f) * (0.85f + 0.14f * fm_tanhf((a - 0.85f) * (1.0f / 0.14f)));
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
        if (h || p.src != PU_N || p.echo_tail || p.horn_env > 0.0005f || p.snap_env > 0.002f)
            effects(&l[i], &r[i], h);
        else if (++p.half & 1u) {
            float x = (l[i] + r[i]) * 16383.0f;
            hist[p.hw = (p.hw + 1u) % HIST] = (int16_t)(x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x);
            p.gate = 1.0f;
        }
    }
}
