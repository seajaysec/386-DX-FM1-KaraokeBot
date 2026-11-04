/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen's look: the colours (each one meaning one thing), shapes and text helpers.
 * The screen is drawn as six 240 x 40 bands into the canvas (gfx.c), each drawn while the one before it is
 * sent; every draw call takes screen coordinates and the band being drawn clips it. Part of the unity build
 * (ui.c). */
#define BAND 40
#define NBANDS (240 / BAND)

/* colours: paper and ink for everything static, teal for what sounds now, coral for you (the cursor,
 * an edit landing, REC, a question), the robot's own red for the robot */
/* two looks, the mode's (GLO): karaoke's paper, plum and teal; poetry's parchment, oxblood and brass. The same roles in
 * both (teal: what sounds now; coral: you), so every page draws the same way in either */
enum { KC_PAPER, KC_PANEL, KC_RULE, KC_INK, KC_DIM, KC_FAINT, KC_TEAL, KC_TEAL_T, KC_CORAL, KC_CORAL_T,
       KC_BG, KC_CPANEL, KC_CRULE, KC_CINK, KC_CDIM, KC_CFAINT, KC_HEAD, KC_HEAD_PLAY, NKC };
static const uint16_t K_LOOK[2][NKC] = {
    {RGB(244, 241, 232), RGB(232, 228, 216), RGB(206, 200, 186), RGB(38, 36, 34), RGB(112, 104, 96), RGB(170, 162, 150),
     RGB(28, 138, 128), RGB(206, 232, 226), RGB(222, 84, 52), RGB(250, 222, 210),
     RGB(70, 40, 112), RGB(122, 48, 132), RGB(136, 100, 176), RGB(255, 248, 255), RGB(214, 196, 236), RGB(160, 136, 190),
     RGB(104, 56, 150), RGB(20, 150, 140)},
    {RGB(228, 214, 190), RGB(212, 196, 170), RGB(182, 162, 134), RGB(40, 28, 24), RGB(108, 86, 70), RGB(154, 134, 112),
     RGB(170, 118, 30), RGB(238, 218, 172), RGB(168, 40, 72), RGB(240, 202, 208),
     RGB(32, 24, 42), RGB(84, 30, 52), RGB(126, 84, 98), RGB(252, 238, 214), RGB(220, 194, 160), RGB(152, 122, 112),
     RGB(74, 26, 44), RGB(150, 100, 28)}};
static uint16_t kp[NKC];
static void look_set(unsigned poetry)
{
    for (unsigned i = 0; i < NKC; i++)
        kp[i] = K_LOOK[poetry & 1u][i];
}
#define K_PAPER kp[KC_PAPER]
#define K_PANEL kp[KC_PANEL]
#define K_RULE kp[KC_RULE]
#define K_INK kp[KC_INK]
#define K_DIM kp[KC_DIM]
#define K_FAINT kp[KC_FAINT]
#define K_TEAL kp[KC_TEAL]
#define K_TEAL_T kp[KC_TEAL_T]
#define K_CORAL kp[KC_CORAL]
#define K_CORAL_T kp[KC_CORAL_T]
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

/* a and b mixed, t of 256 toward b (each channel rounded down) */
static uint16_t g_mix(uint16_t a, uint16_t b, int t)
{
    t = t < 0 ? 0 : t > 256 ? 256 : t;
    unsigned r = ((a >> 11) * (unsigned)(256 - t) + (b >> 11) * (unsigned)t) >> 8;
    unsigned g = (((a >> 5) & 63u) * (unsigned)(256 - t) + ((b >> 5) & 63u) * (unsigned)t) >> 8;
    unsigned bl = ((a & 31u) * (unsigned)(256 - t) + (b & 31u) * (unsigned)t) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* blend c over the canvas pixel at (x, y) by a / 16 */
static void g_blend(int x, int y, uint16_t c, unsigned a)
{
    int yy = y + cv_oy;
    if ((unsigned)x >= cv_w || yy < cv_y0 || yy >= cv_y1 || !a)
        return;
    uint16_t *p = &cv_px[(uint32_t)yy * cv_w + (uint32_t)x];
    *p = swap16(a >= 16 ? c : g_mix(swap16(*p), c, (int)a * 16));
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
    /* the corners, 4 x 4 samples a pixel, in eighths of a pixel (exact: the samples sit on eighths) */
    for (int dy = 0; dy < r; dy++) {
        if (!g_visible(y + dy, 1) && !g_visible(y + h - 1 - dy, 1))
            continue;
        for (int dx = 0; dx < r; dx++) {
            unsigned a = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    int px = 8 * (r - dx) - 1 - 2 * sx, py = 8 * (r - dy) - 1 - 2 * sy;
                    a += px * px + py * py <= 64 * r * r;
                }
            if (!a)
                continue;
            g_blend(x + dx, y + dy, c, a);
            g_blend(x + w - 1 - dx, y + dy, c, a);
            g_blend(x + dx, y + h - 1 - dy, c, a);
            g_blend(x + w - 1 - dx, y + h - 1 - dy, c, a);
        }
    }
}

/* an outline of thickness t, rounded */
static void g_ring(int x, int y, int w, int h, int r, int t, uint16_t c, uint16_t inside)
{
    g_round(x, y, w, h, r, c);
    g_round(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0, inside);
}

/* a filled disc, anti-aliased. The solid middle is a span; only the rim is 2 x 2 sampled
 * (the same eighths as before, so the edge does not move). */
static void g_disc(int cx, int cy, int r, uint16_t c)
{
    if (r <= 0 || !g_visible(cy - r, 2 * r + 1))
        return;
    int lim = 64 * r * r;
    for (int dy = -r; dy <= r; dy++) {
        if (!g_visible(cy + dy, 1))
            continue;
        int ay = dy < 0 ? -dy : dy, solid = -1;
        for (int dx = 0; dx <= r; dx++) {
            int px = 8 * dx + 1, py = 8 * ay + 1;        /* the pixel's far corner: inside means the pixel is solid */
            if (px * px + py * py > lim)
                break;
            solid = dx;
        }
        if (solid >= 0)
            cv_rect(cx - solid, cy + dy, solid * 2 + 1, 1, c);
        for (int dx = solid + 1; dx <= r; dx++) {
            unsigned a = 0;
            for (int s = 0; s < 4; s++) {
                int px = 8 * dx + 2 * (s & 1) - 1, py = 8 * dy + 2 * (s >> 1) - 1;
                a += px * px + py * py <= lim;
            }
            if (!a)
                break;
            g_blend(cx + dx, cy + dy, c, a * 4);
            if (dx)
                g_blend(cx - dx, cy + dy, c, a * 4);
        }
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
/* the bars (top and bottom, every page): light on violet */
static int g_dark;                    /* Mister Wiggles' pages (the stage) */
#define C_BG kp[KC_BG]
#define C_PANEL kp[KC_CPANEL]
#define C_RULE kp[KC_CRULE]
#define C_INK kp[KC_CINK]
#define C_DIM kp[KC_CDIM]
#define C_FAINT kp[KC_CFAINT]

static int g_cap(int x, int y, const char *name, int round)
{
    int w = g_tw(&FONT_XS, name) + 8;
    g_round(x, y, w, 15, round ? 7 : 3, C_INK);
    cv_text(x + 4, y - 1, &FONT_XS, name, C_BG);
    return x + w;
}
