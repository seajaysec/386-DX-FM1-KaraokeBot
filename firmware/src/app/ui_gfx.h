/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen's look: the colours (each one meaning one thing), shapes and text helpers.
 * The screen is drawn as three 240 x 80 bands into the canvas (gfx.c); every draw call takes screen
 * coordinates and the band being drawn clips it. Part of the unity build (ui.c). */
#define BAND 80

/* colours: paper and ink for everything static, teal for what sounds now, coral for you (the cursor,
 * an edit landing, REC, a question), the robot's own red for the robot */
#define K_PAPER RGB(244, 241, 232)
#define K_PANEL RGB(232, 228, 216)
#define K_RULE RGB(206, 200, 186)
#define K_INK RGB(38, 36, 34)
#define K_DIM RGB(112, 104, 96)
#define K_FAINT RGB(170, 162, 150)
#define K_TEAL RGB(28, 138, 128)
#define K_TEAL_T RGB(206, 232, 226)
#define K_CORAL RGB(222, 84, 52)
#define K_CORAL_T RGB(250, 222, 210)
#define K_ROBOT RGB(214, 66, 44)
#define K_ROBOT_DK RGB(150, 40, 28)
#define K_WHITE RGB(255, 255, 250)
static const uint16_t K_TRACK[4] = {RGB(214, 66, 44), RGB(52, 104, 170), RGB(150, 98, 20), RGB(70, 120, 40)};

static int g_band;                    /* rows g_band * BAND .. + BAND */
static void g_begin(int band, uint16_t bg)
{
    cv_begin(240, BAND, bg);
    g_band = band;
    cv_oy = -band * BAND;
    cv_y0 = 0;
    cv_y1 = BAND;
}
static int g_visible(int y, int h) { return y + h > g_band * BAND && y < (g_band + 1) * BAND; }

static void g_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0 && g_visible(y, h))
        cv_rect(x, y, w, h, c);
}

/* blend c over the canvas pixel at (x, y) by a / 16 */
static void g_blend(int x, int y, uint16_t c, unsigned a)
{
    int yy = y + cv_oy;
    if ((unsigned)x >= cv_w || yy < cv_y0 || yy >= cv_y1 || !a)
        return;
    if (a >= 16) {
        cv_px[(uint32_t)yy * cv_w + (uint32_t)x] = swap16(c);
        return;
    }
    uint32_t bg = swap16(cv_px[(uint32_t)yy * cv_w + (uint32_t)x]);
    uint32_t r = ((c >> 11) * a + (bg >> 11) * (16 - a)) / 16, g = (((c >> 5) & 63) * a + ((bg >> 5) & 63) * (16 - a)) / 16,
             b = ((c & 31) * a + (bg & 31) * (16 - a)) / 16;
    cv_px[(uint32_t)yy * cv_w + (uint32_t)x] = swap16((uint16_t)(r << 11 | g << 5 | b));
}

/* a filled rounded rectangle with anti-aliased corners (4 x 4 supersampled) */
static void g_round(int x, int y, int w, int h, int r, uint16_t c)
{
    if (w <= 0 || h <= 0 || !g_visible(y, h))
        return;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;
    cv_rect(x + r, y, w - 2 * r, h, c);
    cv_rect(x, y + r, r, h - 2 * r, c);
    cv_rect(x + w - r, y + r, r, h - 2 * r, c);
    for (int dy = 0; dy < r; dy++)
        for (int dx = 0; dx < r; dx++) {
            unsigned a = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float px = (float)(r - dx) - 0.125f - 0.25f * (float)sx, py = (float)(r - dy) - 0.125f - 0.25f * (float)sy;
                    a += px * px + py * py <= (float)(r * r);
                }
            if (!a)
                continue;
            g_blend(x + dx, y + dy, c, a);
            g_blend(x + w - 1 - dx, y + dy, c, a);
            g_blend(x + dx, y + h - 1 - dy, c, a);
            g_blend(x + w - 1 - dx, y + h - 1 - dy, c, a);
        }
}

/* an outline of thickness t, rounded */
static void g_ring(int x, int y, int w, int h, int r, int t, uint16_t c, uint16_t inside)
{
    g_round(x, y, w, h, r, c);
    g_round(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0, inside);
}

/* a filled disc, anti-aliased */
static void g_disc(int cx, int cy, int r, uint16_t c)
{
    if (r <= 0 || !g_visible(cy - r, 2 * r + 1))
        return;
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++) {
            unsigned a = 0;
            for (int s = 0; s < 4; s++) {
                float px = (float)dx + 0.25f * (float)(s & 1) - 0.125f, py = (float)dy + 0.25f * (float)(s >> 1) - 0.125f;
                a += px * px + py * py <= (float)r * (float)r;
            }
            if (a)
                g_blend(cx + dx, cy + dy, c, a * 4);
        }
}

/* a thick line (round ends) */
static void g_line(int x0, int y0, int x1, int y1, int t, uint16_t c)
{
    int dx = x1 - x0, dy = y1 - y0, n = (dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy);
    if (!n)
        n = 1;
    for (int i = 0; i <= n; i++)
        g_disc(x0 + dx * i / n, y0 + dy * i / n, t / 2 > 0 ? t / 2 : 1, c);
}

/* text: its width, drawn left, centred on x .. x + w, or right-aligned, cut to fit */
static int g_tw(const felucca_font_t *f, const char *s) { return text_w(f, s); }
static void g_text(int x, int y, int w, const felucca_font_t *f, const char *s, uint16_t c)
{
    if (!g_visible(y, f->h))
        return;
    char out[72];
    unsigned n = 0;
    int width = 0;
    while (s[n] && n < sizeof out - 1) {
        int a = f->adv[glyph(f, (uint8_t)s[n])];
        if (width + a > w)
            break;
        width += a;
        out[n] = s[n];
        n++;
    }
    out[n] = 0;
    cv_text(x, y, f, out, c);
}
static void g_center(int x, int y, int w, const felucca_font_t *f, const char *s, uint16_t c)
{
    int tw = g_tw(f, s);
    g_text(tw < w ? x + (w - tw) / 2 : x, y, w, f, s, c);
}
static void g_right(int x, int y, int w, const felucca_font_t *f, const char *s, uint16_t c)
{
    int tw = g_tw(f, s);
    g_text(tw < w ? x + w - tw : x, y, w, f, s, c);
}

/* small string helpers (no printf on the device) */
static char *s_cat(char *d, const char *s)
{
    char *e = d;
    while (*e)
        e++;
    while (*s)
        *e++ = *s++;
    *e = 0;
    return d;
}
static char *s_num(char *d, int v)
{
    char t[12];
    unsigned i = 0, u = v < 0 ? (unsigned)-v : (unsigned)v;
    do
        t[i++] = (char)('0' + u % 10u);
    while ((u /= 10u) && i < 11);
    char *e = d;
    while (*e)
        e++;
    if (v < 0)
        *e++ = '-';
    while (i)
        *e++ = t[--i];
    *e = 0;
    return d;
}
static void s_copy(char *d, const char *s, unsigned n)
{
    unsigned i = 0;
    while (s[i] && i + 1 < n) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void s_upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}
static const char *const NOTE_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static char *s_note(char *d, unsigned note)
{
    s_cat(d, NOTE_NAME[note % 12]);
    return s_num(d, (int)note / 12 - 1);
}
/* a place in the song: "bar 3.2" (bar, beat), with ".3" sixteenths after the beat when not on it */
static char *s_place(char *d, unsigned tick, unsigned steps)
{
    s_cat(d, "bar ");
    s_num(d, (int)(tick / steps + 1));
    s_cat(d, ".");
    s_num(d, (int)(tick % steps / 4 + 1));
    if (tick % 4) {
        s_cat(d, ".");
        s_num(d, (int)(tick % 4 + 1));
    }
    return d;
}

/* a control as printed on the panel: encoders in round pills, buttons and keys in square keycaps */
static int g_cap(int x, int y, const char *name, int round)
{
    int w = g_tw(&FONT_XS, name) + 8;
    g_round(x, y, w, 15, round ? 7 : 3, K_INK);
    cv_text(x + 4, y - 1, &FONT_XS, name, K_PAPER);
    return x + w;
}
