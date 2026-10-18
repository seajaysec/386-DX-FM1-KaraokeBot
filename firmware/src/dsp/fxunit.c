/* SPDX-License-Identifier: GPL-3.0-only */
/* effects (fx.h): the send slots, the tape loop and the master section.
 *
 * Send slots: each takes a mono send and adds a stereo return. The reverb:
 * four int16 combs with damping in the loop and two allpasses a side, the right side reading the
 * combs with alternate signs; here the combs are circular buffers read at a modulated, interpolated
 * tap so "size" scales their lengths and "mod" wanders them (at a fixed 0.5 Hz), and "tone" filters
 * the wet: a low-pass below the centre, a high-pass above it, flat in the middle. The delay is a
 * ping-pong over the two int16 lines, timed in divisions of the tempo (1/32 to 1/2,
 * dotted ones between) and slewed so a change of time warps instead of clicking; the
 * chorus a stepped, slewed triangle on a short int16 line; the distortion a
 * tanh-to-hard-clip ("clip" blends them) between a low cut and a high cut; the lofi a sample-and-hold
 * with a quantiser, its "drift" a stereo spread: the right side reads the lofi output back from the
 * chorus's idle line a few milliseconds late, the lateness wandering slowly; the phaser four first-
 * order allpasses a side, the right side's sweep a quarter turn behind. The delay's and the reverb's
 * "dry" is the share of the send passed through alongside the effect, which is always there.
 *
 * Knob values are turned into the effect's numbers in fx_set (main loop) and glide toward them in
 * the interrupt a block at a time (~10 ms): no zipper noise on a turn. A change of effect clears
 * the states at the next block; fx_set zeroes the new effect's lines itself, which is safe because
 * the running effect never uses another's line (the lofi borrows the chorus's, which the chorus
 * cannot be using at the same time). The slot's filter page (high-pass, low-pass) runs on the mono
 * send before the effect: one filter instead of two, and the same result for the linear effects.
 *
 * Tape: the routed tracks' send is low-passed, taken every fourth sample and written to the int16
 * ring. A held segment loops a sixteenth of the last 16 steps, counted
 * back from the last step boundary tape_bar() reported, read at the rate the pitch (whole octaves)
 * and speed (50..200 %) knobs give, with linear interpolation; the write head stands still meanwhile.
 *
 * Master: the two groups' levels, a 3-band EQ (RBJ shelves and a peak, their gains scaled by blend
 * so blend 0 is a true bypass), the saturator (a tanh / hard clip mix with a tilt, "mix" into the
 * bus), a bus compressor behind its one-knob macro, the master level and a lookahead limiter.
 *
 * Everything here runs in the audio interrupt; fx_set / tape_set / master_set write plain words from
 * the main loop. Float, fastmath.h; stack locals stay small (a 32-sample block). */
#include "fxunit.h"
/* 386DX uses the delay and the reverb; the other types stay in the source, out of the build */
#ifndef FX_ALL_TYPES
#define FX_ALL_TYPES 0
#endif
#include "fastmath.h"

#define SR 44100.0f
#define SUB 32                                /* the block: parameter glides step here */
#define GLIDE 0.07f                           /* a block's share of the distance: ~10 ms to settle */
#define CHO_N 2048                            /* the chorus line: 46 ms (int16; the lofi's spread reads it too) */
#define RV_MOD_HZ 0.5f                        /* the reverb's wander */
#define RV_WET 0.8f                           /* the reverb's return level: the effect is always present */
#define LOFI_SPREAD 480.0f                    /* drift 99: the right side ~11 ms behind the left */

const char *const FXT_NAME[NFXT] = {"chorus", "delay", "distortion", "lofi", "phaser", "reverb"};
const char *const FXT_KNOB[NFXT][4] = {
    {"rate", "depth", "feedback", "stereo"}, {"size", "fine", "feedback", "dry"},
    {"drive", "clip", "lo cut", "hi cut"},   {"rate", "bits", "quality", "drift"},
    {"frequency", "depth", "rate", "feedback"}, {"size", "mod", "tone", "dry"},
};
/* printed defaults (00..99), stored as 0..127: the value * 1.28, rounded */
const uint8_t FXT_DEFAULT[NFXT][4] = {
    {76, 64, 64, 64},                         /* chorus      59 50 50 50 */
    {83, 64, 64, 127},                        /* delay       65 50 50 99: size 1/8D */
    {8, 127, 64, 127},                        /* distortion  06 99 50 99 */
    {64, 93, 78, 127},                        /* lofi        50 73 61 99 */
    {38, 78, 38, 64},                         /* phaser      30 61 30 50 */
    {88, 0, 37, 127},                         /* reverb      69 00 29 99 */
};

/* the reverb's lines inside the int16 buffer: the comb lengths, each with room to grow 1.9x,
 * and its four allpasses (left 556 / 441, right 579 / 457) */
static const int32_t RV_COMB[4] = {1116, 1188, 1277, 1356};
static const int32_t RV_CAP[4] = {2125, 2262, 2431, 2581};
static const int32_t RV_AP[4] = {556, 441, 579, 457};
#define RV_AP0 (2125 + 2262 + 2431 + 2581)
_Static_assert(RV_AP0 + 556 + 441 + 579 + 457 <= FX_RV_LEN, "the reverb's lines do not fit FX_RV_LEN");
/* the delay's sizes in sixteenths: 1/32, 1/32D, 1/16, 1/16D, 1/8, 1/8D, 1/4, 1/2 (dotted = 1.5x) */
static const float DL_DIV[8] = {0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 8.0f};

struct fxslot {
    int16_t cho[CHO_N];
    int16_t *dl, *dr, *rv;
    uint8_t type, type_t, hpv, lpv;
    uint8_t knob[4];
    float bpm;
    float pt[6], pc[6];                       /* the effect's numbers: targets, and gliding */
    float hpc, lpc, hps, lps;                 /* the filter page */
    /* chorus */
    int32_t cw;
    float cph, cs1, cs2, chl, chr, cfbz;
    /* delay */
    int32_t dw, dquiet;
    float dcur, dlpl, dlpr;
    /* distortion */
    float xhp, xlp;
    /* lofi */
    float lph, lhold, lin1, lout1, lwph;
    /* phaser */
    float pxl[4], pyl[4], pxr[4], pyr[4], pph, pzl, pzr, pal, par_;
    /* reverb */
    int32_t cpos[4], apos[4], rquiet;
    float cdmp[4], cd[4], rph, rhp, rtl[2], rtr[2];   /* rtl / rtr: the tone's low-pass and high-pass a side */
};
_Static_assert(sizeof(struct fxslot) <= FXSLOT_BYTES, "fxslot_t does not fit FXSLOT_BYTES");

static inline float onepole_k(float hz) { return hz >= 20000.0f ? 1.0f : 1.0f - fm_expf(-FM_TWO_PI * hz / SR); }
static inline float k127(int v) { return (float)(v < 0 ? 0 : v > 127 ? 127 : v) * (1.0f / 127.0f); }
static inline int16_t f16(float x)
{
    float v = x * 16384.0f;
    return (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
}
static inline float s16f(int16_t v) { return (float)v * (1.0f / 16384.0f); }

static void zero16(int16_t *b, int32_t n)
{
    int32_t i;
    for (i = 0; i < n; i++)
        b[i] = 0;
}

static void fx_states(fxslot_t *f)              /* a fresh effect: its states at rest */
{
    int i;
    zero16(f->cho, CHO_N);
    f->cw = 0; f->cph = 0.0f; f->cs1 = f->cs2 = f->chl = f->chr = f->cfbz = 0.0f;
    f->dw = 0; f->dquiet = FX_DLY_LEN; f->dlpl = f->dlpr = 0.0f;
    f->dcur = f->pt[0];                       /* a new delay starts on time (the slew is for changes) */
    f->xhp = f->xlp = 0.0f;
    f->lph = f->lhold = f->lin1 = f->lout1 = f->lwph = 0.0f;
    for (i = 0; i < 4; i++) {
        f->pxl[i] = f->pyl[i] = f->pxr[i] = f->pyr[i] = 0.0f;
        f->cpos[i] = f->apos[i] = 0;
        f->cdmp[i] = 0.0f;
        f->cd[i] = f->pt[0] * (float)RV_COMB[i];
    }
    f->pph = 0.0f; f->pzl = f->pzr = 0.0f; f->pal = f->par_ = 0.0f;
    f->rquiet = FX_RV_LEN; f->rph = f->rhp = 0.0f;
    f->rtl[0] = f->rtl[1] = f->rtr[0] = f->rtr[1] = 0.0f;
    for (i = 0; i < 6; i++)
        f->pc[i] = f->pt[i];
}

static void delay_time(fxslot_t *f)
{
    float bpm = f->bpm > 20.0f ? f->bpm : 120.0f;
    float t = DL_DIV[(f->knob[0] * 8) >> 7] * 15.0f / bpm;           /* sixteenths: 60 / (bpm * 4) */
    float max_s = (float)(FX_DLY_LEN - 256) / SR;
    t *= 1.0f + ((float)f->knob[1] - 64.0f) * (0.25f / 64.0f);        /* fine: +-25 % */
    while (t > max_s)                                                 /* too long for the line: half, */
        t *= 0.5f;                                                    /* still on the beat */
    f->pt[0] = t * SR;
}

/* the knobs to the effect's numbers (its pt[]: what each means is in the effect's loop) */
__attribute__((minsize)) static void fx_derive(fxslot_t *f)
{
    const uint8_t *k = f->knob;
    float a = k127(k[0]), b = k127(k[1]), c = k127(k[2]), d = k127(k[3]);
    switch (f->type_t) {
#if FX_ALL_TYPES
    case FXT_CHORUS:
        f->pt[0] = 0.05f * fm_exp2f(a * 6.9f);                        /* rate 0.05 .. 6 Hz */
        f->pt[1] = (0.3f + 7.7f * b) * (SR / 1000.0f);                /* depth 0.3 .. 8 ms */
        f->pt[2] = 0.7f * c;                                          /* feedback */
        f->pt[3] = d;                                                 /* stereo: 0 mono .. 1 opposite */
        break;
    case FXT_DIST:
        f->pt[0] = fm_exp2f(a * 6.0f);                                /* drive 1 .. 64 */
        f->pt[1] = b;                                                 /* clip: tanh .. hard clip */
        f->pt[2] = onepole_k(20.0f * fm_exp2f(c * 6.64f));            /* low cut 20 Hz .. 2 kHz */
        f->pt[3] = onepole_k(20000.0f * fm_exp2f(-(1.0f - d) * 5.0f)); /* high cut 625 Hz .. 20 kHz */
        f->pt[4] = fm_powf(f->pt[0], -0.3f);                          /* the louder drive, trimmed */
        break;
    case FXT_LOFI: {
        float fs = 500.0f * fm_exp2f(a * 6.46f);                      /* 500 Hz .. 44.1 kHz */
        float bits = 2.0f + 14.0f * b;
        f->pt[0] = fm_minf(fs / SR, 1.0f);
        f->pt[1] = fm_exp2f(bits - 1.0f);                             /* quantiser levels */
        f->pt[2] = c;                                                 /* quality: anti-alias filter */
        f->pt[3] = d * LOFI_SPREAD;                                   /* drift: the right's lateness, samples */
        f->pt[4] = onepole_k(fm_minf(0.45f * fs, 19000.0f));
        f->pt[5] = 1.0f / f->pt[1];
        break;
    }
    case FXT_PHASER:
        f->pt[0] = 100.0f * fm_exp2f(a * 5.64f);                      /* 100 Hz .. 5 kHz */
        f->pt[1] = 3.0f * b;                                          /* depth, octaves */
        f->pt[2] = 0.03f * fm_exp2f(c * 8.06f);                       /* rate 0.03 .. 8 Hz */
        f->pt[3] = 0.9f * d;                                          /* feedback */
        break;
#endif
    case FXT_DELAY:
        delay_time(f);                                                /* size and fine */
        f->pt[2] = 0.95f * c;                                         /* feedback */
        f->pt[3] = d;                                                 /* dry through */
        break;
    case FXT_REVERB:
        f->pt[0] = 0.5f * fm_exp2f(a * 1.926f);                       /* size: the combs x 0.5 .. 1.9 */
        f->pt[1] = 24.0f * b * b;                                     /* mod, samples of wander */
        /* tone: below the centre a low-pass closes to 800 Hz, above it a high-pass opens to 2 kHz */
        f->pt[2] = c < 0.5f ? onepole_k(20000.0f * fm_exp2f(-(0.5f - c) * 9.29f)) : 1.0f;
        f->pt[5] = c > 0.5f ? onepole_k(20.0f * fm_exp2f((c - 0.5f) * 13.29f)) : 0.0f;
        f->pt[3] = d;                                                 /* dry through */
        f->pt[4] = 0.55f + 0.42f * a;                                 /* the loop's feedback: decay with size */
        break;
    default:
        break;
    }
}

__attribute__((minsize)) void fx_init(fxslot_t *f, int16_t *dly_l, int16_t *dly_r, int16_t *rv)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)f;
    for (i = 0; i < sizeof *f; i++)
        z[i] = 0;
    f->dl = dly_l;
    f->dr = dly_r;
    f->rv = rv;
    f->bpm = 120.0f;
    f->lpv = 127;
    f->hpc = onepole_k(20.0f);
    f->lpc = onepole_k(20000.0f);
    if (dly_l) zero16(dly_l, FX_DLY_LEN);
    if (dly_r) zero16(dly_r, FX_DLY_LEN);
    if (rv) zero16(rv, FX_RV_LEN);
    for (i = 0; i < 4; i++)
        f->knob[i] = FXT_DEFAULT[FXT_CHORUS][i];
    f->type = f->type_t = FXT_CHORUS;
    fx_derive(f);
    fx_states(f);
}

int fx_has(const fxslot_t *f, int type)
{
    if (type == FXT_REVERB)
        return f->rv != 0;
    if (type == FXT_DELAY)
        return f->dl != 0 && f->dr != 0;
    return type >= 0 && type < NFXT;
}

__attribute__((minsize)) void fx_set(fxslot_t *f, int type, const uint8_t knob[4])
{
    int i;
    if (type < 0 || type >= NFXT || !fx_has(f, type))
        type = FXT_CHORUS;
    for (i = 0; i < 4; i++)
        f->knob[i] = knob[i] > 127 ? 127 : knob[i];
    if (type != f->type_t) {                  /* the lines the new effect reads are idle now: clear them */
        if (type == FXT_DELAY) {
            zero16(f->dl, FX_DLY_LEN);
            zero16(f->dr, FX_DLY_LEN);
        } else if (type == FXT_REVERB) {
            zero16(f->rv, FX_RV_LEN);
        }
    }
    f->type_t = (uint8_t)type;
    fx_derive(f);
}

void fx_tempo(fxslot_t *f, float bpm)
{
    if (bpm > 20.0f && bpm != f->bpm) {
        f->bpm = bpm;
        if (f->type_t == FXT_DELAY)
            delay_time(f);
    }
}

void fx_filter(fxslot_t *f, int hp, int lp)
{
    f->hpv = (uint8_t)(hp < 0 ? 0 : hp > 127 ? 127 : hp);
    f->lpv = (uint8_t)(lp < 0 ? 0 : lp > 127 ? 127 : lp);
    f->hpc = onepole_k(20.0f * fm_exp2f(k127(hp) * 8.64f));            /* 20 Hz .. 8 kHz */
    f->lpc = onepole_k(20000.0f * fm_exp2f(-(1.0f - k127(lp)) * 8.0f)); /* 78 Hz .. 20 kHz */
}

/* ---- the effects: a block of m samples of x[] (the filtered send) into out (added) ---- */
static inline float cho_tap(const fxslot_t *f, float p)      /* the short line, read between samples */
{
    int32_t i0 = (int32_t)p;
    float a = s16f(f->cho[i0 & (CHO_N - 1)]);
    return a + (p - (float)i0) * (s16f(f->cho[(i0 + 1) & (CHO_N - 1)]) - a);
}

static void do_chorus(fxslot_t *f, const float *x, float *ol, float *orr, int m)
{
    const float inc = f->pc[0] / SR, range = f->pc[1], fb = f->pc[2], side = 1.0f - 2.0f * f->pc[3];
    const float centre = 15.0f * (SR / 1000.0f), slew = 1.0f - fm_expf(-1.0f / (0.070f * SR));
    const float hpk = 0.028f;                 /* ~200 Hz: the wet without its lows */
    float ph = f->cph, s1 = f->cs1, s2 = f->cs2, hl = f->chl, hr = f->chr, fz = f->cfbz;
    int32_t w = f->cw;
    int i;
    for (i = 0; i < m; i++) {
        float tri, st, pl, pr, wl, wr;
        f->cho[w] = f16(x[i] + fz * fb);
        w = (w + 1) & (CHO_N - 1);
        tri = 2.0f * fm_fabsf(2.0f * ph - 1.0f) - 1.0f;     /* the JP's stepped triangle */
        ph += inc;
        if (ph >= 1.0f)
            ph -= 1.0f;
        st = fm_floorf(tri * 4.0f + 0.5f) * 0.25f;
        s1 += (st - s1) * slew;
        s2 += (s1 - s2) * slew;
        pl = (float)w - (centre + s2 * range);
        pr = (float)w - (centre + s2 * range * side);
        if (pl < 0.0f) pl += CHO_N;
        if (pr < 0.0f) pr += CHO_N;
        wl = cho_tap(f, pl);
        wr = cho_tap(f, pr);
        fz = wl;
        hl += (wl - hl) * hpk;
        hr += (wr - hr) * hpk;
        ol[i] += (wl - hl) * 0.85f;
        orr[i] += (wr - hr) * 0.85f;
    }
    f->cph = ph; f->cs1 = s1; f->cs2 = s2; f->chl = fm_flush(hl); f->chr = fm_flush(hr); f->cfbz = fz;
    f->cw = w;
}

static inline float tap16(const int16_t *b, float rp)
{
    int32_t i0 = (int32_t)rp, i1;
    float fr = rp - (float)i0, a, c;
    if (i0 >= FX_DLY_LEN) i0 -= FX_DLY_LEN;
    i1 = i0 + 1 >= FX_DLY_LEN ? 0 : i0 + 1;
    a = s16f(b[i0]);
    c = s16f(b[i1]);
    return a + (c - a) * fr;
}

static void do_delay(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any)
{
    const float target = f->pt[0], fb = f->pc[2], dry = f->pc[3] * 0.7071f, tc = 0.35f;
    float dcur = f->dcur, lpl = f->dlpl, lpr = f->dlpr;
    int32_t w = f->dw, quiet = f->dquiet;
    int i;
    if (!any && quiet >= FX_DLY_LEN) {        /* silent in, an empty line: time moves on, nothing to read */
        f->dw = (w + m) % FX_DLY_LEN;
        f->dcur += (target - dcur) * (1.0f - fm_expf(-0.0008f * (float)m));
        return;
    }
    for (i = 0; i < m; i++) {
        float rp, tl, tr, a, b;
        dcur += (target - dcur) * 0.0008f;
        rp = (float)w - dcur;
        if (rp < 0.0f)
            rp += (float)FX_DLY_LEN;
        tl = tap16(f->dl, rp);
        tr = tap16(f->dr, rp);
        lpl += tc * (tl - lpl);               /* the repeats darken */
        lpr += tc * (tr - lpr);
        a = x[i] + lpr * fb;                  /* ping-pong: the send and the right into the left, */
        b = lpl * fb;                         /* the left into the right */
        if (a > 1.0f || a < -1.0f) a = fm_tanhf(a);
        if (b > 1.0f || b < -1.0f) b = fm_tanhf(b);
        {
            int16_t ka = f16(a), kb = f16(b);
            f->dl[w] = ka;
            f->dr[w] = kb;
            quiet = (ka == 0 && kb == 0 && x[i] == 0.0f) ? quiet + 1 : 0;
        }
        if (++w >= FX_DLY_LEN)
            w = 0;
        ol[i] += tl + x[i] * dry;
        orr[i] += tr + x[i] * dry;
    }
    f->dw = w;
    f->dcur = dcur;
    f->dlpl = fm_flush(lpl);
    f->dlpr = fm_flush(lpr);
    f->dquiet = quiet;
}

static void do_dist(fxslot_t *f, const float *x, float *ol, float *orr, int m)
{
    const float pre = f->pc[0], clip = f->pc[1], hpk = f->pt[2], lpk = f->pt[3], trim = f->pc[4];
    float hp = f->xhp, lp = f->xlp;
    int i;
    for (i = 0; i < m; i++) {
        float y, soft, hard;
        hp += hpk * (x[i] - hp);
        y = (x[i] - hp) * pre;
        soft = fm_tanhf(y);                   /* clip 0: the tanh's bend; clip 99: a hard edge */
        hard = fm_clampf(y, -1.0f, 1.0f);
        y = soft + clip * (hard - soft);
        lp += lpk * (y - lp);
        ol[i] += lp * trim;
        orr[i] += lp * trim;
    }
    f->xhp = fm_flush(hp);
    f->xlp = fm_flush(lp);
}

static void do_lofi(fxslot_t *f, const float *x, float *ol, float *orr, int m)
{
    const float q = f->pc[1], iq = f->pc[5], qual = f->pc[2], aak = f->pt[4], step = f->pc[0];
    float ph = f->lph, hold = f->lhold, in1 = f->lin1, out1 = f->lout1, late;
    int32_t w = f->cw;
    int i;
    f->lwph += 0.3f * (float)m / SR;          /* the spread wanders: a 0.3 Hz wow of the right's lateness */
    if (f->lwph >= 1.0f)
        f->lwph -= 1.0f;
    late = f->pc[3] * (1.0f + 0.04f * fm_sin_turns(f->lwph));
    for (i = 0; i < m; i++) {
        float xf, y, r;
        in1 += aak * (x[i] - in1);
        xf = x[i] + qual * (in1 - x[i]);      /* quality: how much is band-limited before the hold */
        ph += step;
        if (ph >= 1.0f) {
            ph -= 1.0f;
            hold = fm_floorf(xf * q + 0.5f) * iq;
        }
        out1 += aak * (hold - out1);
        y = hold + qual * (out1 - hold);
        f->cho[w] = f16(y);                   /* the right side reads the left back, late */
        if (late < 1.0f) {
            r = y;
        } else {
            float rp = (float)w - late;
            if (rp < 0.0f)
                rp += CHO_N;
            r = cho_tap(f, rp);
        }
        w = (w + 1) & (CHO_N - 1);
        ol[i] += y;
        orr[i] += r;
    }
    f->lph = ph; f->lhold = hold; f->lin1 = fm_flush(in1); f->lout1 = fm_flush(out1);
    f->cw = w;
}

static inline float ap_coef(float hz)
{
    float t = fm_tanf(FM_PI * fm_clampf(hz, 20.0f, 18000.0f) / SR);
    return (t - 1.0f) / (t + 1.0f);
}

static void do_phaser(fxslot_t *f, const float *x, float *ol, float *orr, int m)
{
    const float centre = f->pc[0], depth = f->pc[1], fb = f->pc[3];
    float al_t, ar_t, al = f->pal, ar = f->par_, dal, dar, zl = f->pzl, zr = f->pzr, im = 1.0f / (float)m;
    int i, s;
    f->pph += f->pc[2] * (float)m / SR;
    if (f->pph >= 1.0f)
        f->pph -= 1.0f;
    al_t = ap_coef(centre * fm_exp2f(depth * fm_sin_turns(f->pph)));
    ar_t = ap_coef(centre * fm_exp2f(depth * fm_sin_turns(f->pph + 0.25f)));
    dal = (al_t - al) * im;
    dar = (ar_t - ar) * im;
    for (i = 0; i < m; i++) {
        float xl = x[i] + zl * fb, xr = x[i] + zr * fb;
        al += dal;
        ar += dar;
        for (s = 0; s < 4; s++) {
            float yl = al * xl + f->pxl[s] - al * f->pyl[s], yr = ar * xr + f->pxr[s] - ar * f->pyr[s];
            f->pxl[s] = xl; f->pyl[s] = yl; xl = yl;
            f->pxr[s] = xr; f->pyr[s] = yr; xr = yr;
        }
        zl = fm_flush(xl);
        zr = fm_flush(xr);
        ol[i] += 0.5f * (x[i] + xl);
        orr[i] += 0.5f * (x[i] + xr);
    }
    f->pal = al; f->par_ = ar; f->pzl = zl; f->pzr = zr;
}

static void do_reverb(fxslot_t *f, const float *x, float *ol, float *orr, int m, int any)
{
    const float damp = 0.45f, fb = f->pc[4], dry = f->pc[3] * 0.7071f, hpk = 0.017f;   /* ~120 Hz on the send */
    const float tlk = f->pc[2], thk = f->pc[5];                                        /* the tone's filters */
    float cdd[4], cd[4], cdmp[4], hp = f->rhp, tl0 = f->rtl[0], tl1 = f->rtl[1], th0 = f->rtr[0], th1 = f->rtr[1];
    int16_t *cb[4], *ab[4];
    int32_t cp[4], ap[4], quiet = f->rquiet;
    int c, i;
    if (!any && quiet >= FX_RV_LEN)
        return;
    f->rph += RV_MOD_HZ * (float)m / SR;
    if (f->rph >= 1.0f)
        f->rph -= 1.0f;
    cb[0] = f->rv;
    for (c = 0; c < 4; c++) {                 /* where each comb's tap wants to be: size, then the wander */
        float d = f->pc[0] * (float)RV_COMB[c] + f->pc[1] * fm_sin_turns(f->rph + 0.25f * (float)c);
        d = fm_clampf(d, 8.0f, (float)(RV_CAP[c] - 3));
        cd[c] = f->cd[c];
        cdd[c] = (d - cd[c]) / (float)m;
        cdmp[c] = f->cdmp[c];
        cp[c] = f->cpos[c];
        ap[c] = f->apos[c];
        if (c)
            cb[c] = cb[c - 1] + RV_CAP[c - 1];
    }
    ab[0] = f->rv + RV_AP0;
    ab[1] = ab[0] + RV_AP[0];
    ab[2] = ab[1] + RV_AP[1];
    ab[3] = ab[2] + RV_AP[2];
    for (i = 0; i < m; i++) {
        float acc = 0.0f, accr = 0.0f, y, yr;
        int32_t wr = 0;
        hp += hpk * (x[i] - hp);
        y = x[i] - hp;
        for (c = 0; c < 4; c++) {
            int16_t *b = cb[c];
            const int32_t cap = RV_CAP[c], p = cp[c];
            float rp, a, bb, v;
            int32_t i0, i1, k;
            cd[c] += cdd[c];
            rp = (float)p - cd[c];
            if (rp < 0.0f)
                rp += (float)cap;
            i0 = (int32_t)rp;
            i1 = i0 + 1 >= cap ? 0 : i0 + 1;
            a = (float)b[i0];
            bb = (float)b[i1];
            v = (a + (bb - a) * (rp - (float)i0)) * (1.0f / 2048.0f);
            acc += v;
            accr += (c & 1) ? -v : v;
            cdmp[c] = v + (cdmp[c] - v) * damp;
            k = (int32_t)((y + cdmp[c] * fb) * 2048.0f);      /* 12 bits in the loop, as an early rack reverb */
            if (k > 32767) k = 32767;
            if (k < -32767) k = -32767;
            b[p] = (int16_t)k;
            wr |= k;
            cp[c] = p + 1 >= cap ? 0 : p + 1;
        }
        y = acc * 0.25f;
        yr = accr * 0.25f;
        for (c = 0; c < 4; c++) {             /* two allpasses a side, int16 */
            int16_t *b = ab[c];
            const int32_t p = ap[c];
            float *s = c < 2 ? &y : &yr, bo = (float)b[p] * (1.0f / 2048.0f), nv = (*s + bo * 0.5f) * 2048.0f;
            b[p] = (int16_t)fm_clampf(nv, -32767.0f, 32767.0f);
            *s = bo - *s * 0.5f;
            ap[c] = p + 1 >= RV_AP[c] ? 0 : p + 1;
        }
        tl0 += tlk * (y - tl0);               /* the tone: a low-pass (k = 1 passes), then a high-pass (k = 0 passes) */
        tl1 += tlk * (yr - tl1);
        th0 += thk * (tl0 - th0);
        th1 += thk * (tl1 - th1);
        ol[i] += (tl0 - th0) * RV_WET + x[i] * dry;
        orr[i] += (tl1 - th1) * RV_WET + x[i] * dry;
        quiet = (wr == 0 && x[i] == 0.0f) ? quiet + 1 : 0;
    }
    for (c = 0; c < 4; c++) {
        f->cd[c] = cd[c];
        f->cdmp[c] = fm_flush(cdmp[c]);
        f->cpos[c] = cp[c];
        f->apos[c] = ap[c];
    }
    f->rhp = fm_flush(hp);
    f->rtl[0] = fm_flush(tl0); f->rtl[1] = fm_flush(tl1);
    f->rtr[0] = fm_flush(th0); f->rtr[1] = fm_flush(th1);
    f->rquiet = quiet;
}

void fx_process(fxslot_t *f, const float *in, float *out_l, float *out_r, uint32_t n)
{
    uint32_t o;
    if (f->type_t != f->type) {               /* a new effect: start it at rest */
        f->type = f->type_t;
        fx_states(f);
    }
    for (o = 0; o < n; o += SUB) {
        float x[SUB];
        int m = (int)(n - o < SUB ? n - o : SUB), i, any = 0;
        const int hp_on = f->hpv > 0, lp_on = f->lpv < 127;
        float hps = f->hps, lps = f->lps;
        const float hpc = f->hpc, lpc = f->lpc;
        for (i = 0; i < 6; i++)
            f->pc[i] += (f->pt[i] - f->pc[i]) * GLIDE;
        for (i = 0; i < m; i++) {
            float v = in[o + i];
            any |= v != 0.0f;
            if (hp_on) {
                hps += hpc * (v - hps);
                v -= hps;
            }
            if (lp_on) {
                lps += lpc * (v - lps);
                v = lps;
            }
            x[i] = v;
        }
        f->hps = fm_flush(hps);
        f->lps = fm_flush(lps);
        switch (f->type) {
#if FX_ALL_TYPES
        case FXT_CHORUS: do_chorus(f, x, out_l + o, out_r + o, m); break;
        case FXT_DIST: do_dist(f, x, out_l + o, out_r + o, m); break;
        case FXT_LOFI: do_lofi(f, x, out_l + o, out_r + o, m); break;
        case FXT_PHASER: do_phaser(f, x, out_l + o, out_r + o, m); break;
#endif
        case FXT_DELAY: do_delay(f, x, out_l + o, out_r + o, m, any); break;
        case FXT_REVERB: do_reverb(f, x, out_l + o, out_r + o, m, any); break;
        default: break;
        }
    }
}

/* ================================================================== tape === */
struct tape {
    int16_t *buf;
    uint32_t w;                               /* the write head, quarter-rate samples */
    uint32_t mark;                            /* the head at the last step boundary */
    int32_t ph;                               /* 0..3: the next input sample taken is at 0 */
    float lp1, lp2;                           /* the input's two one-poles (~4.8 kHz) */
    uint32_t step;                            /* samples a step at 44.1 kHz */
    int32_t seg, on;                          /* the segment held, looping */
    float rate, wet, fade, dry;
    float start, len, pos;                    /* the loop: ring position, length, offset into it */
    uint8_t pitch, speed, length, mix;
};
_Static_assert(sizeof(struct tape) <= TAPE_BYTES, "tape_t does not fit TAPE_BYTES");
#define TAPE_SCALE 12000.0f                   /* int16 = 1.0 at 12000: 2.7x of headroom */
#define TAPE_LPK 0.495f                       /* one-pole 4.8 kHz: under the quarter rate's Nyquist */

void tape_init(tape_t *t, int16_t *buf)
{
    uint32_t i;
    uint8_t *z = (uint8_t *)t;
    for (i = 0; i < sizeof *t; i++)
        z[i] = 0;
    t->buf = buf;
    zero16(buf, TAPE_LEN);
    t->step = 11025;                          /* a sixteenth at 120 BPM until the sequencer says */
    t->rate = 1.0f;
    t->dry = 1.0f;
    t->pitch = t->speed = 64;
    t->length = 0;
    t->mix = 127;
    t->wet = 1.0f;
}

void tape_set(tape_t *t, int pitch, int speed, int length, int mix)
{
    float oct;
    t->pitch = (uint8_t)(pitch < 0 ? 0 : pitch > 127 ? 127 : pitch);
    t->speed = (uint8_t)(speed < 0 ? 0 : speed > 127 ? 127 : speed);
    t->length = (uint8_t)(length < 0 ? 0 : length > 127 ? 127 : length);
    t->mix = (uint8_t)(mix < 0 ? 0 : mix > 127 ? 127 : mix);
    oct = (float)(((int)t->pitch * 5) >> 7) - 2.0f;                   /* five zones: -2 .. +2 octaves, 64 = x1 */
    t->rate = fm_exp2f(oct + ((float)t->speed - 64.0f) * (1.0f / 64.0f));   /* speed 50 .. 200 %, 64 = 100 % */
    t->wet = (float)t->mix * (1.0f / 127.0f);
}

void tape_bar(tape_t *t, uint32_t samples_per_step)
{
    if (samples_per_step >= 64)
        t->step = samples_per_step;
    t->mark = t->w;                           /* a step boundary: the segments count back from here */
}

void tape_segment(tape_t *t, int seg, int on)
{
    float step_q = (float)t->step * 0.25f, back, len, avail;
    int segs;
    if (seg < 0 || seg > 15)
        return;
    if (!on) {
        if (t->on && seg == t->seg)
            t->on = 0;                        /* fades out, then rests */
        return;
    }
    /* segment s = the s-th sixteenth of the 16 steps before the last boundary */
    back = (float)(16 - seg) * step_q + (float)TAPE_WRAP((int32_t)(t->w - t->mark));
    avail = (float)(TAPE_LEN - 8);
    if (back > avail)
        back = avail;
    segs = 1 + (t->length * 15) / 127;        /* length: 1 .. 16 steps */
    len = (float)segs * step_q;
    if (len > back - 4.0f)
        len = back - 4.0f;
    if (len < 16.0f)
        len = 16.0f;
    t->start = (float)t->w - back;
    if (t->start < 0.0f)
        t->start += (float)TAPE_LEN;
    t->len = len;
    t->pos = 0.0f;
    t->seg = seg;
    t->on = 1;
}

int tape_active(const tape_t *t) { return t->on || t->fade > 0.0f; }

float tape_process(tape_t *t, const float *in, float *out_l, float *out_r, uint32_t n)
{
    uint32_t i;
    float lp1 = t->lp1, lp2 = t->lp2, fade = t->fade;
    const float dfade = 1.0f / 88.0f;         /* 2 ms in, 2 ms out */
    int32_t ph = t->ph;
    uint32_t w = t->w;
    if (!t->on && fade <= 0.0f) {             /* recording */
        for (i = 0; i < n; i++) {
            lp1 += TAPE_LPK * (in[i] - lp1);
            lp2 += TAPE_LPK * (lp1 - lp2);
            if (++ph >= TAPE_RATE) {
                float v = lp2 * TAPE_SCALE;
                ph = 0;
                t->buf[w] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
                w = TAPE_WRAP((int32_t)w + 1);
            }
        }
        t->lp1 = fm_flush(lp1);
        t->lp2 = fm_flush(lp2);
        t->ph = ph;
        t->w = w;
        t->dry = 1.0f;
        return 1.0f;
    }
    {   /* looping: the head stands, the loop plays at its rate, faded in and out */
        const float wet = t->wet, inc = t->rate * 0.25f, start = t->start, len = t->len;
        float pos = t->pos;
        const int on = t->on;
        for (i = 0; i < n; i++) {
            float rp = start + pos, a, b, y;
            int32_t i0, i1;
            fade += on ? dfade : -dfade;
            fade = fm_clampf(fade, 0.0f, 1.0f);
            if (rp >= (float)TAPE_LEN)
                rp -= (float)TAPE_LEN;
            i0 = (int32_t)rp;
            i1 = TAPE_WRAP(i0 + 1);
            a = (float)t->buf[TAPE_WRAP(i0)];
            b = (float)t->buf[i1];
            y = (a + (b - a) * (rp - (float)i0)) * (fade * wet / TAPE_SCALE);
            out_l[i] += y;
            out_r[i] += y;
            pos += inc;
            if (pos >= len)
                pos -= len;
        }
        t->pos = pos;
        t->fade = fade;
        t->dry = 1.0f - wet * fade;
        return t->dry;
    }
}

/* ================================================================ master === */
/* names (pp. 171-173), short enough for the knob strip: percussion, melodic, compressor
 * and master level are perc, mel, comp, level */
const char *const MS_NAME[NMS] = {"low", "mid", "high", "blend", "gain", "clip", "tone", "mix",
                                  "perc", "mel", "comp", "level"};

typedef struct { float b0, b1, b2, a1, a2; } biq_t;
typedef struct { float x1, x2, y1, y2; } biqs_t;

struct master {
    uint8_t pot[NMS];
    float gp, gm, glev;                       /* the group and master levels (targets) */
    float gp_c, gm_c, lev_c;                  /* gliding */
    /* EQ */
    biq_t eq[3];
    biqs_t eqs[2][3];
    uint8_t eq_on, sat_on, comp_on;
    /* saturator */
    float pre, clip, tlo, thi, sblend, strim, slp[2];
    /* compressor */
    float thr_db, slope, knee, a_att, a_rel, makeup, gr, ms, a_det, want, g_lin, g_step, gr_view;
    uint8_t cr;
#define MST_CR 4
    /* limiter */
#define MST_LA 64
    float env, gain, inv_env;
    float la[MST_LA], la_r[MST_LA];
    int32_t la_pos, hold;
};
_Static_assert(sizeof(struct master) <= MASTER_BYTES, "master_t does not fit MASTER_BYTES");

static float level_lin(int v)                 /* 0 .. 100 .. 127: silence, unity (exactly), 2x */
{
    float x = (float)(v < 0 ? 0 : v > 127 ? 127 : v);
    return x <= 100.0f ? x * x * 0.0001f : 1.0f + (x - 100.0f) * (1.0f / 27.0f);
}
static float coef_ms(float t) { return fm_expf(-1000.0f / (t * SR)); }

/* RBJ: a low shelf, a peak, a high shelf, gain in dB */
static void biq_shelf(biq_t *q, float hz, float db, int high)
{
    float A = fm_exp2f(db * (3.3219281f / 40.0f)), w = FM_TWO_PI * hz / SR, cs = fm_cosf(w), sn = fm_sinf(w);
    float al = sn * 0.5f * 1.4142136f, sa = 2.0f * fm_sqrtf(A) * al, ia, c = high ? -cs : cs;
    float a0 = (A + 1.0f) + (A - 1.0f) * c + sa;
    ia = 1.0f / a0;
    q->b0 = A * ((A + 1.0f) - (A - 1.0f) * c + sa) * ia;
    q->b1 = (high ? -2.0f : 2.0f) * A * ((A - 1.0f) - (A + 1.0f) * c) * ia;
    q->b2 = A * ((A + 1.0f) - (A - 1.0f) * c - sa) * ia;
    q->a1 = (high ? 2.0f : -2.0f) * ((A - 1.0f) + (A + 1.0f) * c) * ia;
    q->a2 = ((A + 1.0f) + (A - 1.0f) * c - sa) * ia;
}
static void biq_peak(biq_t *q, float hz, float db, float Q)
{
    float A = fm_exp2f(db * (3.3219281f / 40.0f)), w = FM_TWO_PI * hz / SR, cs = fm_cosf(w), sn = fm_sinf(w);
    float al = sn / (2.0f * Q), ia = 1.0f / (1.0f + al / A);
    q->b0 = (1.0f + al * A) * ia;
    q->b1 = -2.0f * cs * ia;
    q->b2 = (1.0f - al * A) * ia;
    q->a1 = -2.0f * cs * ia;
    q->a2 = (1.0f - al / A) * ia;
}

__attribute__((minsize)) static void eq_coefs(master_t *m)
{
    float bl = (float)m->pot[MS_EQ_BLEND] * (1.0f / 127.0f);
    float lo = ((float)m->pot[MS_EQ_LO] - 64.0f) * (12.0f / 64.0f) * bl;
    float mid = ((float)m->pot[MS_EQ_MID] - 64.0f) * (12.0f / 64.0f) * bl;
    float hi = ((float)m->pot[MS_EQ_HI] - 64.0f) * (12.0f / 64.0f) * bl;
    biq_shelf(&m->eq[0], 120.0f, lo, 0);
    biq_peak(&m->eq[1], 1000.0f, mid, 0.8f);
    biq_shelf(&m->eq[2], 6000.0f, hi, 1);
    m->eq_on = m->pot[MS_EQ_BLEND] != 0 &&
               (m->pot[MS_EQ_LO] != 64 || m->pot[MS_EQ_MID] != 64 || m->pot[MS_EQ_HI] != 64);
}

__attribute__((minsize)) static void sat_coefs(master_t *m)
{
    float t = ((float)m->pot[MS_SAT_TONE] - 64.0f) * (1.0f / 64.0f);
    m->pre = fm_exp2f((float)m->pot[MS_SAT_GAIN] * (4.0f / 127.0f));    /* 1 .. 16 */
    m->clip = (float)m->pot[MS_SAT_CLIP] * (1.0f / 127.0f);
    m->tlo = fm_exp2f(-t);                     /* the tilt: +-6 dB, lows against highs around 800 Hz */
    m->thi = fm_exp2f(t);
    m->sblend = (float)m->pot[MS_SAT_BLEND] * (1.0f / 127.0f);          /* mix: the saturator into the bus */
    m->strim = fm_powf(m->pre, -0.4f);
    m->sat_on = m->pot[MS_SAT_BLEND] != 0;
}

/* The one-knob COMP: the threshold falls (-4 .. -32 dB), the ratio climbs (1.5:1 .. 6:1), the
 * makeup follows (0.85 of the reduction at the threshold); attack 2.6 ms, release 106 ms */
__attribute__((minsize)) static void comp_coefs(master_t *m)
{
    static const float R[6] = {1.0f, 1.5f, 2.0f, 3.0f, 4.0f, 6.0f};
    int v = m->pot[MS_COMP], ri = v ? 1 + (int)((float)v * (4.0f / 127.0f) + 0.5f) : 0;
    float x = (float)v * (1.0f / 127.0f), thr = -4.0f - 28.0f * x, mk;
    uint8_t was = m->comp_on;
    mk = ri ? fm_minf(-thr * (1.0f - 1.0f / R[ri]) * 0.85f, 24.0f) : 0.0f;
    m->thr_db = thr;
    m->slope = 1.0f - 1.0f / R[ri];
    m->knee = 6.0f;
    m->a_att = coef_ms(2.6f);
    m->a_rel = coef_ms(106.0f);
    m->a_det = coef_ms(8.0f);
    m->makeup = fm_db2lin(mk);
    m->comp_on = ri != 0;
    if (m->comp_on && !was) {
        m->g_lin = m->makeup;
        m->g_step = 0.0f;
        m->cr = 0;
    }
}

__attribute__((minsize)) void master_init(master_t *m)
{
    static const uint8_t DEF[NMS] = {64, 64, 64, 0, 0, 0, 64, 0, 100, 100, 0, 100};
    uint32_t i;
    uint8_t *z = (uint8_t *)m;
    for (i = 0; i < sizeof *m; i++)
        z[i] = 0;
    for (i = 0; i < NMS; i++)
        m->pot[i] = DEF[i];
    m->gain = 1.0f;
    m->gp = m->gp_c = m->gm = m->gm_c = m->glev = m->lev_c = 1.0f;
    eq_coefs(m);
    sat_coefs(m);
    comp_coefs(m);
}

__attribute__((minsize)) void master_set(master_t *m, int p, int v)
{
    if (p < 0 || p >= NMS)
        return;
    m->pot[p] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    if (p <= MS_EQ_BLEND)
        eq_coefs(m);
    else if (p <= MS_SAT_BLEND)
        sat_coefs(m);
    else if (p == MS_PERC)
        m->gp = level_lin(v);
    else if (p == MS_MEL)
        m->gm = level_lin(v);
    else if (p == MS_COMP)
        comp_coefs(m);
    else
        m->glev = level_lin(v);
}

float master_gr_db(const master_t *m) { return m->gr_view; }

static inline float biq(const biq_t *q, biqs_t *s, float x)
{
    float y = q->b0 * x + q->b1 * s->x1 + q->b2 * s->x2 - q->a1 * s->y1 - q->a2 * s->y2;
    s->x2 = s->x1;
    s->x1 = x;
    s->y2 = s->y1;
    s->y1 = fm_flush(y);
    return y;
}
static inline float gr_of(const master_t *m, float lv)       /* the static curve, soft knee (dB) */
{
    float over = lv - m->thr_db, h = 0.5f * m->knee;
    if (over <= -h)
        return 0.0f;
    if (over >= h)
        return m->slope * over;
    over += h;
    return m->slope * over * over / (2.0f * m->knee);
}
static inline float soft_ceiling(float s)
{
    if (s > 0.89f || s < -0.89f)
        s = s > 0.0f ? 0.89f + 0.11f * fm_tanhf((s - 0.89f) * 9.0f) : -0.89f - 0.11f * fm_tanhf((-s - 0.89f) * 9.0f);
    return s;
}
static inline float glide1(float cur, float tgt)             /* a level knob: ~10 ms, then exact */
{
    if (cur == tgt)
        return cur;
    cur += (tgt - cur) * 0.0022651f;
    return fm_fabsf(tgt - cur) < 1e-5f ? tgt : cur;
}

void master_process(master_t *m, float *perc_l, float *perc_r, float *mel_l, float *mel_r, float *out_l,
                    float *out_r, uint32_t n)
{
    uint32_t i;
    float gp = m->gp_c, gm = m->gm_c, lev = m->lev_c, grmax = 0.0f;
    const int eq_on = m->eq_on, sat_on = m->sat_on, comp_on = m->comp_on;
    for (i = 0; i < n; i++) {
        float s, sr;
        gp = glide1(gp, m->gp);
        gm = glide1(gm, m->gm);
        lev = glide1(lev, m->glev);
        s = perc_l[i] * gp + mel_l[i] * gm;
        sr = perc_r[i] * gp + mel_r[i] * gm;
        if (eq_on) {
            int b;
            for (b = 0; b < 3; b++) {
                s = biq(&m->eq[b], &m->eqs[0][b], s);
                sr = biq(&m->eq[b], &m->eqs[1][b], sr);
            }
        }
        if (sat_on) {
            const float pre = m->pre, clip = m->clip, tlo = m->tlo, thi = m->thi, bl = m->sblend, trim = m->strim;
            float y, soft, hard, lo;
            y = s * pre;
            soft = fm_tanhf(y);
            hard = fm_clampf(y, -1.0f, 1.0f);
            y = soft + clip * (hard - soft);
            m->slp[0] += 0.108f * (y - m->slp[0]);              /* ~800 Hz: the tilt's split */
            lo = m->slp[0];
            y = lo * tlo + (y - lo) * thi;
            s += bl * (y * trim - s);
            y = sr * pre;
            soft = fm_tanhf(y);
            hard = fm_clampf(y, -1.0f, 1.0f);
            y = soft + clip * (hard - soft);
            m->slp[1] += 0.108f * (y - m->slp[1]);
            lo = m->slp[1];
            y = lo * tlo + (y - lo) * thi;
            sr += bl * (y * trim - sr);
        }
        if (comp_on) {
            float want;
            /* the level: mean square over ~8 ms of both sides, x2 so a sine reads its peak; the static
             * curve every MST_CR samples; the reduction smoothed (attack / release) and ramped as a
             * linear gain (read every sample, the gain would chase each cycle of a bass) */
            m->ms = fm_flush(m->a_det * m->ms + 0.5f * ((1.0f - m->a_det) * s * s + (1.0f - m->a_det) * sr * sr));
            if (m->cr == 0) {
                float lv = 2.0f * m->ms;
                m->want = (lv > 1e-12f && m->slope > 0.0f) ? gr_of(m, 3.0103f * fm_log2f(lv)) : 0.0f;
            }
            want = m->want;
            m->gr = want > m->gr ? m->a_att * m->gr + (1.0f - m->a_att) * want
                                 : m->a_rel * m->gr + (1.0f - m->a_rel) * want;
            if (m->gr > grmax)
                grmax = m->gr;
            if (m->cr == 0) {
                m->g_step = (m->makeup * fm_db2lin(-m->gr) - m->g_lin) * (1.0f / MST_CR);
                m->cr = MST_CR;
            }
            m->cr--;
            m->g_lin += m->g_step;
            s *= m->g_lin;
            sr *= m->g_lin;
        }
        s *= lev;
        sr *= lev;
        {   /* the limiter: a peak is seen MST_LA samples before it comes out of the line; the gain
             * eases down over those samples and holds for them */
            float a = fm_maxf(fm_fabsf(s), fm_fabsf(sr)), d, dr, want;
            if (a >= m->env) {
                float r = 1.0f / fm_maxf(a, 0.8f);                   /* safe on its own: a clamp inside
                                                                      * the branch is folded away */
                m->env = a;
                m->hold = MST_LA;
                m->inv_env = a > 0.8f ? r : 0.0f;
            } else if (m->hold) {
                m->hold--;
            } else {
                m->env *= 0.99985f;
                m->inv_env *= 1.0001500225f;
            }
            want = m->env > 0.8f ? 0.8f * m->inv_env : 1.0f;
            m->gain += (want - m->gain) * (want < m->gain ? (5.0f / MST_LA) : 0.002f);
            d = m->la[m->la_pos];
            dr = m->la_r[m->la_pos];
            m->la[m->la_pos] = s;
            m->la_r[m->la_pos] = sr;
            m->la_pos = (m->la_pos + 1) & (MST_LA - 1);
            out_l[i] = soft_ceiling(d * m->gain);
            out_r[i] = soft_ceiling(dr * m->gain);
        }
    }
    m->gp_c = gp;
    m->gm_c = gm;
    m->lev_c = lev;
    m->slp[0] = fm_flush(m->slp[0]);
    m->slp[1] = fm_flush(m->slp[1]);
    m->gr_view = grmax > m->gr_view ? grmax : m->gr_view * 0.93f;
}
