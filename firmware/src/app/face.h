/* SPDX-License-Identifier: GPL-3.0-only */
/* The robot: the 386DX performer as it always looked, a coral head, two white eyes and a dark mouth
 * that opens with the voice. Its face never changes shape; it reacts by moving, a simple motion for
 * each thing the voice does:
 *
 *   loudness   the mouth opens (as the original)
 *   brightness the mouth widens for bright vowels (as the original)
 *   a syllable a little hop as it starts
 *   pitch      the whole face sits higher for high notes, lower for low ones (the song's middle level);
 *              five semitones over the middle or more, happy ^ ^ eyes; five under, calm - - eyes
 *   vibrato    the head's outline goes wavy, swinging at the vibrato's rate, further the deeper it is
 *   body       the whole head a little bigger or smaller
 *   at rest    a blink now and then (as the original)
 *   punch-in   held effects glitch the picture: stutters repeat a slice, crush blocks it, mute shuts
 *              the eyes, tape stop droops the face
 * Part of the unity build (ui.c), after ui_gfx.h. */
#include "../dsp/fastmath.h"

typedef struct {
    int singing;                      /* a clip or the hum sounds */
    float level, open;                /* 0..1: loudness, brightness */
    float pitch;                      /* semitones */
    float vibrato;                    /* semitones now */
    float body;                       /* 0..1 */
    uint32_t hop_at;                  /* when the syllable sounding now started (ms) */
    uint32_t punch;                   /* the punch-in effects held */
    uint32_t now;                     /* ms */
} face_t;

static float face_mid = 62.0f;        /* the pitch the face rests at: the song's middle */

#define FACE_HEAD RGB(233, 77, 53)
#define FACE_EYE RGB(255, 255, 246)
#define FACE_INK RGB(37, 41, 41)

static void face_draw(int cx, int top, const face_t *f)
{
    /* where the face is: hopped, raised by the pitch, wobbled by the vibrato */
    uint32_t d = f->now - f->hop_at;
    int hop = f->singing && d < 240u ? (int)(5u * 4u * d * (240u - d) / (240u * 240u)) : 0;
    float rise = f->singing ? (f->pitch - face_mid) * 0.5f : 0.0f;
    rise = rise > 6 ? 6 : rise < -6 ? -6 : rise;
    /* the original's 86 x 61 at the usual BODY, a little bigger or smaller with it */
    int w = 78 + (int)(f->body * 16.0f), h = 55 + (int)(f->body * 12.0f);
    int x = cx - w / 2, y = top + 34 - (int)rise - hop + ((f->punch >> PU_TAPESTOP & 1u) ? 6 : 0);
    /* the head a row at a time: its rounded corners, and the vibrato's wave along its sides (an S that
     * swings one way, then the other, with the pitch) */
    float wave = f->vibrato * 9.0f;
    wave = wave > 5 ? 5 : wave < -5 ? -5 : wave;
    for (int r = 0; r < h; r++) {
        int e = r < 12 ? 12 - r : r >= h - 12 ? r - (h - 13) : 0, in = 0;
        while (in < 12 && (12 - in) * (12 - in) + e * e > 144)    /* the corner's inset on this row */
            in++;
        if (!e)
            in = 0;
        int dx = (int)(wave * fm_sinf((float)r * (6.2832f / (float)h)));
        g_rect(x + in + dx, y + r, w - 2 * in, 1, FACE_HEAD);
    }
    /* the eyes, as the original: a blink now and then while quiet */
    int blink = !f->singing && f->now % 4500u > 4310u;
    int shut = blink || (f->punch >> PU_MUTE & 1u);
    int eh = shut ? 2 : 14, ey = y + (shut ? 21 : 14);
    if (!shut && f->singing && f->pitch - face_mid >= 5.0f) {
        /* a high note: happy ^ ^ eyes */
        for (int e = 0; e < 2; e++) {
            int ex = x + w / 2 + (e ? 14 : -24);
            g_line(ex - 1, ey + 12, ex + 5, ey + 3, 3, FACE_EYE);
            g_line(ex + 5, ey + 3, ex + 11, ey + 12, 3, FACE_EYE);
        }
    } else if (!shut && f->singing && f->pitch - face_mid <= -5.0f) {
        /* a low note: calm, half-closed - - eyes */
        g_round(x + w / 2 - 25, ey + 8, 12, 4, 2, FACE_EYE);
        g_round(x + w / 2 + 13, ey + 8, 12, 4, 2, FACE_EYE);
    } else {
        g_round(x + w / 2 - 24, ey, 10, eh, eh > 4 ? 3 : 1, FACE_EYE);
        g_round(x + w / 2 + 14, ey, 10, eh, eh > 4 ? 3 : 1, FACE_EYE);
    }
    /* the mouth, as the original: open with loudness, wide with brightness */
    float lvl = f->singing ? f->level : 0.0f;
    int mh = 3 + (int)(lvl * 16.0f), mw = 24 + (int)((f->singing ? f->open : 0.0f) * 12.0f);
    g_round(x + (w - mw) / 2, y + 47 - mh / 2, mw, mh, mh > 4 ? 4 : 1, FACE_INK);

    /* punch-in glitches */
    if (f->punch & (1u << PU_STUTTER8 | 1u << PU_STUTTER16 | 1u << PU_STUTTER32 | 1u << PU_STUTTER64)) {
        int band = top + 20 + (int)(f->now / 40u % 70u);
        for (int yy = band; yy < band + 8; yy++)
            for (int xx = x; xx < x + w - 6; xx++) {
                int sy = yy + cv_oy;
                if (sy >= 0 && sy < BAND && (unsigned)(xx + 6) < cv_w)
                    cv_px[(uint32_t)sy * cv_w + (uint32_t)xx] = cv_px[(uint32_t)sy * cv_w + (uint32_t)(xx + 6)];
            }
    }
    if (f->punch & (1u << PU_CRUSH | 1u << PU_LOFI)) {
        int b = f->punch >> PU_CRUSH & 1u ? 8 : 4;
        for (int yy = top; yy < top + 130; yy += b)
            for (int xx = x - 8; xx < x + w + 8; xx += b) {
                int sy = yy + cv_oy;
                if (sy < 0 || sy >= BAND || (unsigned)xx >= cv_w)
                    continue;
                uint16_t c = cv_px[(uint32_t)sy * cv_w + (uint32_t)xx];
                for (int j = 0; j < b; j++)
                    for (int i = 0; i < b; i++)
                        if (sy + j < BAND && (unsigned)(xx + i) < cv_w)
                            cv_px[(uint32_t)(sy + j) * cv_w + (uint32_t)(xx + i)] = c;
            }
    }
}
