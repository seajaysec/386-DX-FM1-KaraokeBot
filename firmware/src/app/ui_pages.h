/* SPDX-License-Identifier: GPL-3.0-only */
/* The screen: the header (where you are), the page's view, the knob strip (what the four knobs do) and
 * the footer (what the keys and buttons do here). Lists, questions, help cards and the words editor
 * draw over the page. Part of the unity build (ui.c). */

#define Y_MAIN 24
#define LANDMARKS 0x2306183u                  /* typing: keys to find your place by */
#define H_MAIN 134
#define Y_STRIP 160
#define Y_FOOT 224

static int flashing(void) { return plat_ms() - ui.flash_at < 500u; }

/* ---- the header ---- */
/* its colour says the state: plum stopped, teal playing, red recording; a message's colour says what
 * kind it is (done green, refused red, undo amber); the punch-ins held pile their names on magenta */
__attribute__((noinline)) static void draw_header(void)
{
    if (!g_visible(0, 23))
        return;                               /* (each band draws the whole scene: only the top band holds this) */
    int msg = plat_ms() < ui.msg_until && ui.msg[0][0];
    uint16_t bg = eng_playing ? (ui.rec ? RGB(200, 46, 60) : kp[KC_HEAD_PLAY]) : kp[KC_HEAD];
    if (msg)
        bg = ui.msg_kind == M_NO ? RGB(170, 40, 50) : ui.msg_kind == M_DONE ? RGB(30, 130, 90)
                 : ui.msg_kind == M_UNDO ? RGB(210, 140, 40) : RGB(130, 70, 190);
    if (!msg && eng_punch)
        bg = RGB(210, 50, 150);
    g_rect(0, 0, 240, 23, bg);
    g_rect(0, 20, 240, 3, K_TRACK[ui.track]);     /* its lower edge: the selected track's colour, on every page */
    if (msg) {
        uint16_t fg = K_WHITE;
        int x = 8;
        if (ui.msg_kind == M_NO) {
            g_disc(14, 11, 7, K_CORAL);
            g_center(7, 1, 14, &FONT_XS, "!", K_WHITE);
            x = 26;
        } else if (ui.msg_kind == M_DONE) {
            g_line(9, 11, 12, 15, 3, K_WHITE);
            g_line(12, 15, 19, 6, 3, K_WHITE);
            x = 26;
        }
        int end = x + g_tw(&FONT_S, ui.msg[0]);
        g_text(x, 0, 230 - x, &FONT_S, ui.msg[0], fg);
        if (ui.msg[1][0])
            g_text(end + 8, 3, 232 - end - 8, &FONT_XS, ui.msg[1], C_DIM);
        return;
    }
    if (eng_punch) {
        /* each effect held, its name piled on the others */
        static const uint16_t INK[4] = {K_WHITE, RGB(255, 222, 96), RGB(150, 240, 255), RGB(255, 190, 220)};
        int n = 0;
        for (unsigned i = 0; i < PU_N; i++)
            if (eng_punch >> i & 1u) {
                char w[16];
                s_copy(w, punch_name(i), 16);
                if (i == PU_BUILD)                /* two Ds for each track it took this time: buil + 2, 4, 6 or 8 Ds */
                    for (unsigned t = 0, first = 1; t < NTRACKS; t++)
                        if (eng_build_mask >> t & 1u) {
                            s_cat(w, first ? "d" : "dd");
                            first = 0;
                        }
                s_upper(w);
                g_center(n * 13 - (n & 1) * 26, (n % 3) - 1, 240, &FONT_S, w, INK[n & 3]);
                n++;
            }
        return;
    }
    if (ui.poetry && ui.page == P_EFFECTS) {
        /* the SLAMJAM room: the show's own header, the track your black keys play and its sound (PRESETS changes it) */
        int tw = g_tw(&FONT_XS, TRACK_NAME[ui.track]) + 10;
        g_round(4, 4, tw, 15, 7, K_TRACK[ui.track]);
        g_text(9, 0, tw, &FONT_XS, TRACK_NAME[ui.track], K_WHITE);
        const track_t *k = &song.track[ui.track];
        const char *snd = ui.track == T_VOICE ? JAM_NAME[0][poem.style[0] % 3u]
                          : ui.track == T_DRUMS ? engine_kit_name(k->sound) : PRESETS[k->sound].name;
        char t[40] = "";
        if (ui.track == T_VOICE && ui.ntune) {    /* the tune being gathered: its last notes, and how many of how many */
            int r = tune_row();
            for (unsigned i = ui.ntune > 5u ? ui.ntune - 5u : 0u; i < ui.ntune; i++) {
                s_cat(t, NOTE_NAME[ui.tune[i] % 12u]);
                s_cat(t, " ");
            }
            s_num(t, ui.ntune);
            s_cat(t, "/");
            s_num(t, r >= 0 ? song.line[song.row[r].line].count : 0);
            snd = t;
        }
        g_text(4 + tw + 8, 0, 200 - tw, &FONT_S, snd, ui.ntune && ui.track == T_VOICE ? K_WHITE : C_INK);
        if (eng_playing)
            for (int i = 0; i < 9; i++)
                g_rect(226 + i / 2, 6 + i / 2, 1, 10 - (i / 2) * 2, K_WHITE);
        if (ui.dirty)
            g_disc(234, 4, 3, K_CORAL);
        return;
    }
    int x = g_cap(4, 4, BUTTON_CAP[PAGE_BUTTON[ui.page]], 0);
    g_text(x + 5, 0, 90, &FONT_S, ui.poetry && ui.page == P_SING ? "Practice" : PAGE_NAME[ui.page], C_INK);
    int px = 118;
    /* the track, in its colour */
    int tw = g_tw(&FONT_XS, TRACK_NAME[ui.track]) + 10;
    g_round(px, 4, tw, 15, 7, K_TRACK[ui.track]);
    g_text(px + 5, 0, tw, &FONT_XS, TRACK_NAME[ui.track], K_WHITE);
    char t[24] = "";
    if (eng_playing) {
        s_place(t, eng_tick, song.steps);
        g_right(150, 1, 72, &FONT_XS, t, K_WHITE);
        /* the play triangle */
        for (int i = 0; i < 9; i++)
            g_rect(226 + i / 2, 6 + i / 2, 1, 10 - (i / 2) * 2, K_WHITE);
    } else {
        s_num(t, song.bpm);
        s_cat(t, " BPM");
        g_right(150, 1, 78, &FONT_XS, t, C_DIM);
    }
    if (ui.dirty)
        g_disc(234, 4, 3, K_CORAL);
}

/* ---- the knob strip ---- */
/* each knob its colour, on every page: blue, yellow, green, orange */
static const uint16_t KNOB_COL[4] = {RGB(70, 140, 240), RGB(240, 196, 40), RGB(70, 190, 90), RGB(250, 140, 40)};
__attribute__((noinline)) static void draw_strip(const char *tag)
{
    if (!g_visible(Y_STRIP, 64))
        return;
    g_rect(0, Y_STRIP, 240, 64, C_BG);
    g_rect(4, Y_STRIP + 8, 232, 1, C_RULE);
    int tw = g_tw(&FONT_XS, tag) + 8;
    g_rect(4, Y_STRIP, tw, 16, C_BG);
    g_text(6, Y_STRIP - 1, 180, &FONT_XS, tag, C_DIM);
    unsigned n = page_count();
    if (n > 1)
        for (unsigned i = 0; i < n; i++)
            g_disc(232 - (int)(n - 1 - i) * 9, Y_STRIP + 8, 3, i == ui.sub[ui.page] ? C_INK : C_FAINT);
    for (unsigned k = 0; k < 4; k++) {
        knob_t r = knob_info(k);
        int x = 4 + (int)k * 59;
        int hot = flashing() && ui.flash_step == (int16_t)(100 + k);
        g_text(x + 2, Y_STRIP + 15, 56, &FONT_XS, r.name, r.name[0] == '-' ? C_FAINT : C_INK);
        if (r.name[0] == '-')
            continue;
        int range = r.hi - r.lo, fill = range ? (r.value - r.lo) * 52 / range : 0;
        g_round(x + 2, Y_STRIP + 33, 52, 5, 2, C_RULE);
        g_round(x + 2, Y_STRIP + 33, fill < 4 ? 4 : fill, 5, 2, hot ? C_INK : KNOB_COL[k]);
        char v[16];
        knob_text(k, &r, v);
        const felucca_font_t *vf = g_tw(&FONT_S, v) > 56 ? &FONT_XS : &FONT_S;   /* a long word: the small face */
        g_text(x + 2, Y_STRIP + 39 + (vf == &FONT_XS ? 3 : 0), 56, vf, v, hot ? K_CORAL : C_INK);
    }
}

/* ---- the footer: controls as printed, each with its job ---- */
__attribute__((noinline)) static void draw_footer(const char *const *parts, unsigned n)
{
    if (!g_visible(Y_FOOT, 16))
        return;
    g_rect(0, Y_FOOT, 240, 16, C_PANEL);
    int x = 3;
    for (unsigned i = 0; i + 1 < n + 1 && i < n; i += 2) {
        int round = parts[i][0] == '(';
        const char *cap = round ? parts[i] + 1 : parts[i];
        x = g_cap(x, Y_FOOT + 1, cap, round);
        if (i + 1 < n) {
            g_text(x + 3, Y_FOOT - 1, 236 - x, &FONT_XS, parts[i + 1], C_INK);
            x += g_tw(&FONT_XS, parts[i + 1]) + 9;
        }
        if (x > 230)
            break;
    }
}

/* ---- Sing: the robot and the words ---- */
/* the face's state, read once a frame: the screen is drawn in three bands, and each must draw the
 * same face */
/* one character of FONT_S: its width, and drawn */
static int ch_w(char ch)
{
    char c[2] = {ch, 0};
    return g_tw(&FONT_S, c);
}
static void ch_put(int x, int y, char ch, uint16_t col)
{
    char c[2] = {ch, 0};
    cv_text(x, y, &FONT_S, c, col);
}

/* the words, laid out once a frame like the face: the line, where it breaks in two (-1: one line),
 * the word sung, and the width of the rest's bar (-1: no rest) */
static struct {
    const char *t;
    int n, brk, word_at, word_end, bar;
    int pw[SONG_TEXT + 1];               /* the width of the first i characters */
} ly;
static void lyric_frame(void);

static void face_state(face_t *f)
{
    static face_t frame;
    static uint32_t frame_at = 1;
    if (frame_at == ui.frame_at) {
        *f = frame;
        return;
    }
    frame_at = ui.frame_at;
    lyric_frame();
    static int16_t last_note = -1;
    static int8_t last_clip = -1;
    static uint8_t last_on;
    static uint32_t hop_at;
    /* a syllable starts: a scored note, a live clip, or the voice coming on */
    if (eng_sing_on && (eng_sing_note != last_note || eng_sing_clip != last_clip || !last_on))
        hop_at = plat_ms();
    last_note = eng_sing_note;
    last_clip = eng_sing_clip;
    last_on = eng_sing_on;
    f->singing = eng_sing_on;
    f->level = (float)eng_sing_level / 127.0f;
    f->open = (float)eng_sing_open / 127.0f;
    f->pitch = eng_sing_pitch;
    f->vibrato = eng_sing_vibrato;
    f->body = 0.5f;
    f->hop_at = hop_at;
    f->punch = eng_punch;
    f->now = plat_ms();
    frame = *f;
}

static void lyric_frame(void)
{
    int line;
    ly.t = 0;
    ly.brk = ly.bar = -1;
    ly.word_at = ly.word_end = 0;           /* no word sung: an empty range */
    if (!eng_playing)                         /* the words show while the song plays; stopped, the name */
        return;
    line = song.row[eng_sing_row >= 0 ? eng_sing_row : 0].line;      /* before the first row: the first line */
    if (eng_sing_clip >= 0) {
        ly.word_at = clips[eng_sing_clip].word_at;
        ly.word_end = ly.word_at + clips[eng_sing_clip].word_len;
    }
    /* a rest of two bars or more between lines: a bar fills where the words go, by the song's own clock;
     * the last two half notes of it the bar is gone and the next line's words show */
    unsigned t = eng_tick, from = 0, to = song.row[0].at;      /* (no rows: 0, no rest) */
    if (eng_sing_row >= 0) {
        const row_t *R = &song.row[eng_sing_row];
        from = R->at + line_span(&song, R->line);
        to = row_end(&song, (unsigned)eng_sing_row);
    }
    if (to >= from + 2 * song.steps && t >= from && t < to) {
        if (t + 16 < to) {
            ly.bar = 200u * (t - from) / (to - from);
            return;
        }
        const row_t *N = &song.row[eng_sing_row + 1];
        if (N < song.row + song.rows)
            line = N->line;
    }
    if (line < 0 || !song.line[line].text[0])
        return;
    /* a line too wide for the screen breaks at the space that leaves the two halves most even */
    ly.t = song.line[line].text;
    ly.pw[0] = 0;
    for (ly.n = 0; ly.t[ly.n]; ly.n++) {
        int i = ly.n;
        ly.pw[i + 1] = ly.pw[i] + ch_w(ly.t[i]);
    }
    for (int i = 1, best = ly.pw[ly.n] > 228 ? 0x7fff : 0; i < ly.n; i++) {
        int d = ly.pw[i] + ly.pw[i + 1] - ly.pw[ly.n];      /* left minus right */
        d = d < 0 ? -d : d;
        if (ly.t[i] == ' ' && d < best) {
            best = d;
            ly.brk = i;
        }
    }
}

/* one run of the line, ly.t[a..e), centred, the word sung in teal */
static void lyric_run(int y, int a, int e)
{
    int x = (240 - (ly.pw[e] - ly.pw[a])) / 2;
    for (int i = a; i < e; i++) {
        int cw = ly.pw[i + 1] - ly.pw[i], sung = i >= ly.word_at && i < ly.word_end;
        if (sung)
            g_rect(x - 1, y + 2, cw + 2, 20, K_TEAL);
        ch_put(x, y, ly.t[i], sung ? K_WHITE : C_INK);
        x += cw;
    }
}

/* ---- Lines: the rows ---- */
static void line_label(char *d, unsigned line)
{
    s_cat(d, "L");
    s_num(d, (int)line + 1);
}

/* ---- Song (the sequencer zoomed out): every track's chain, and the shown track's phrases ---- */
/* the four chains over the song, a bar a cell (a bar past a chain's end is its repeat, faint), each phrase's
 * start a gap; the cursor's phrase ringed; then the shown track's phrases, a row each */
__attribute__((noinline)) static void page_arrange(void)
{
    unsigned T = ui.track, total = song.bars;
    int playing_bar = eng_playing ? (int)(eng_tick / song.steps) : -1;
    for (unsigned t = 0; t < NTRACKS; t++) {
        int y = Y_MAIN + 3 + (int)t * 11;
        for (unsigned b = 0; b < total; b++) {
            unsigned lb = b % song.lane_bars[t], v = song.lane[t][lb];
            int x = 4 + (int)(b * 232u / total), w = 4 + (int)((b + 1) * 232u / total) - x;
            uint16_t c = (t != T_VOICE && (v & 31u) >= SONG_PATTERNS) ? K_PANEL : b < song.lane_bars[t] ? K_TRACK[t] : K_FAINT;
            g_rect(x + ((v & LANE_START) && w > 2), y, w - ((v & LANE_START) && w > 2), 9, c);
        }
    }
    unsigned b0 = lane_start(&song, T, ui.ph), n = lane_len(&song, T, ui.ph);
    int x0 = 4 + (int)(b0 * 232u / total), x1 = 4 + (int)((b0 + n) * 232u / total);
    g_ring(x0 - 2, Y_MAIN + 1 + (int)T * 11, x1 - x0 + 4, 13, 2, 2, ui.moving ? K_INK : K_CORAL, K_TRACK[T]);
    if (playing_bar >= 0)
        g_rect(4 + playing_bar * 232 / (int)total, Y_MAIN + 1, 2, 45, K_TEAL);
    /* the phrases */
    unsigned count = lane_count(&song, T), first = ui.ph > 1 ? ui.ph - 1u : 0;
    if (first + 4 > count)
        first = count > 4 ? count - 4 : 0;
    int now = playing_bar >= 0 ? (int)lane_find(&song, T, lane_tick(&song, T, eng_tick) / song.steps) : -1;
    for (unsigned i = first; i < count && i < first + 4; i++) {
        int y = Y_MAIN + 50 + (int)(i - first) * 21;
        unsigned b = lane_start(&song, T, i), len = lane_len(&song, T, i);
        uint16_t inside = (int)i == now ? K_TEAL_T : flashing() && ui.flash_row == (int16_t)i ? K_CORAL_T : K_PAPER;
        if (i == ui.ph)
            g_ring(1, y - 1, 238, 21, 6, 2, ui.moving ? K_INK : K_CORAL, inside);
        else if (inside != K_PAPER)
            g_round(2, y, 236, 19, 5, inside);
        char t[48] = "";
        s_num(t, (int)b + 1);
        if (len > 1) {
            s_cat(t, "-");
            s_num(t, (int)(b + len));
        }
        g_right(2, y + 2, 34, &FONT_XS, t, K_DIM);
        t[0] = 0;
        if (T == T_VOICE) {                       /* its first line's words */
            for (unsigned r = 0; r < song.rows && !t[0]; r++)
                if (row_bar(&song, r) >= b && row_bar(&song, r) < b + len)
                    s_copy(t, song.line[song.row[r].line].text[0] ? song.line[song.row[r].line].text : "(no words)", sizeof t);
            if (!t[0])
                s_cat(t, "(rest)");
        } else {                                  /* its bars, a block each: pale a rest, dotted when linked */
            for (unsigned j = 0; j < len && j < 18; j++) {
                unsigned m = song.lane[T][b + j] & 31u, uses = 0;
                for (unsigned c = 0; c < song.lane_bars[T]; c++)
                    uses += (song.lane[T][c] & 31u) == m;
                g_round(44 + (int)j * 10, y + 4, 8, 12, 2, m < SONG_PATTERNS ? K_TRACK[T] : K_RULE);
                if (m < SONG_PATTERNS && uses > 1)
                    g_rect(47 + (int)j * 10, y + 8, 2, 4, K_PAPER);
            }
            continue;
        }
        g_text(42, y - 1, 190, &FONT_S, t, t[0] == '(' ? K_FAINT : (int)i == now ? K_TEAL : K_INK);
    }
    if (count < 4)                                /* what comes next, under the last */
        g_text(42, Y_MAIN + 52 + (int)(count - first) * 21, 190, &FONT_XS, "SEL, ADD: a new phrase", K_FAINT);
}

/* ---- Phrase: a bar of the cursor's phrase ---- */
/* ---- the Voice's phrase, under its roll: the words, the cursor's word lit, those no note sings in coral ---- */
__attribute__((noinline)) static void phrase_words(void)
{
    if (ui.row >= song.rows)
        return;
    unsigned line = song.row[ui.row].line, syl = line_syllables(line), left = line_unsung(line), n = 0;
    unsigned ws = line_word(line, cursor_syl(ui.row)), we = ws, tail = left ? line_word(line, syl - left) : 255;
    const char *t = song.line[line].text;
    while (t[n])
        n++;
    while (ws < n && t[we] && t[we] != ' ')
        we++;
    int x = 4, y = Y_STRIP - 15;
    for (unsigned a = ws < n && ws > 10 ? ws - 10 : 0, e; a < n && x < 236; a = e) {
        unsigned k = a >= ws && a < we ? 2u : a >= tail;
        char b[64];
        for (e = a; e < n && (e >= ws && e < we ? 2u : e >= tail) == k && e - a < 63; e++)
            b[e - a] = t[e];
        b[e - a] = 0;
        int w = g_tw(&FONT_XS, b);
        if (k == 2)
            g_rect(x - 1, y - 1, w + 2, 13, K_CORAL);
        g_text(x, y, 236 - x, &FONT_XS, b, k == 2 ? K_WHITE : k ? K_CORAL : K_INK);
        x += w;
    }
}

__attribute__((noinline)) static void page_phrase(void)
{
    unsigned steps = song.steps, len = lane_len(&song, ui.track, ui.ph);
    int cw = 208 / (int)steps, x0 = 28, top = Y_MAIN + (ui.track == T_DRUMS ? 7 : 12), h = (ui.track == T_VOICE ? 144 : 160) - top;
    /* the phrase's bars, this one dark */
    for (unsigned j = 0; j < len && j < 16; j++)
        g_round(x0 + (int)j * 13, Y_MAIN + 1, 11, 5, 2, j == ui.pbar ? K_INK : K_RULE);
    unsigned now = eng_playing ? lane_tick(&song, ui.track, eng_tick) : 0;
    unsigned play_step = eng_playing && now / steps == ui.bar ? now % steps : 255;
    for (unsigned s = 0; s < steps; s++)
        g_rect(x0 + (int)s * cw, top, cw - 1, h, s == play_step ? K_TEAL_T : (int)s == (ui.track == T_DRUMS ? ui.hold : ui.step)
                                                   ? K_CORAL_T : (s / 4) & 1 ? K_PANEL : K_PAPER);
    if (ui.track == T_DRUMS) {
        const step_t *S = bar_steps(0);
        for (unsigned lane = 0; lane < 11; lane++) {
            int y = Y_MAIN + 7 + (int)lane * 12;
            static const char *const N[11] = {"KK", "SN", "LT", "MT", "HT", "RM", "CP", "CB", "CY", "OH", "CH"};
            g_text(4, y - 4, 24, &FONT_XS, N[lane], lane == ui.lane ? K_CORAL : K_DIM);
            for (unsigned s = 0; s < steps; s++)
                if (S[s].drums >> lane & 1u)
                    g_round(x0 + (int)s * cw + 1 + S[s].nudge, y, cw - 3, 8, 2, lane == ui.lane ? K_INK : K_DIM);
        }
        return;
    }
    /* a roll: the notes at their pitch, as long as they last, a nudge a pixel or two off the grid */
    int lo = 127, hi = 0, nn = 0;
    struct { int16_t x, w; uint8_t p, hot; } nt[48];
    if (ui.track == T_VOICE) {
        unsigned b = lane_start(&song, T_VOICE, ui.ph), t0 = (unsigned)ui.bar * steps;
        for (unsigned r = 0; r < song.rows; r++) {
            const line_t *L = &song.line[song.row[r].line];
            if (row_bar(&song, r) < b || row_bar(&song, r) >= b + len)
                continue;
            for (unsigned i = 0; i < L->count && nn < 48; i++) {
                const note_t *n = &song.note[L->first + i];
                int a = (int)(song.row[r].at + n->at) - (int)t0;
                if (a + n->length <= 0 || a >= (int)steps)
                    continue;
                nt[nn++] = (typeof(nt[0])){(int16_t)(a * cw + note_nudge(n)), (int16_t)(n->length * cw - 2), n->pitch,
                                           (uint8_t)(a <= ui.step && ui.step < a + n->length)};
            }
        }
    } else {
        const step_t *S = bar_steps(0);
        for (unsigned s = 0; s < steps; s++)
            for (unsigned k = 0; k < S[s].count && nn < 48; k++)
                nt[nn++] = (typeof(nt[0])){(int16_t)((int)s * cw + S[s].nudge), (int16_t)(S[s].length * cw - 2), S[s].note[k],
                                           (uint8_t)(s <= ui.step && ui.step < s + S[s].length)};
    }
    for (int i = 0; i < nn; i++) {
        lo = nt[i].p < lo ? nt[i].p : lo;
        hi = nt[i].p > hi ? nt[i].p : hi;
    }
    if (lo > hi)
        lo = hi = ui.track == T_BASS ? 40 : 60;
    if (hi - lo < 12) {
        lo -= (12 - (hi - lo)) / 2;
        hi = lo + 12;
    }
    /* the roll's top and bottom rows, ruled and named where its notes sit (a note is 6 tall, centred on its row) */
    int y_hi = top + 5, y_lo = top + h - 5;
    g_rect(x0, y_hi, (int)steps * cw, 1, K_RULE);
    g_rect(x0, y_lo, (int)steps * cw, 1, K_RULE);
    for (int i = 0; i < nn; i++) {
        int x = nt[i].x, w = nt[i].w;
        if (x < 0)
            w += x, x = 0;
        if (x + w > (int)steps * cw - 2)
            w = (int)steps * cw - 2 - x;
        g_round(x0 + x + 1, top + h - 8 - (nt[i].p - lo) * (h - 10) / (hi - lo), w < 2 ? 2 : w, 6, 2, nt[i].hot ? K_CORAL : K_INK);
    }
    char t[8] = "";
    s_note(t, (unsigned)hi);
    g_text(2, y_hi - 5, 26, &FONT_XS, t, K_DIM);
    t[0] = 0;
    s_note(t, (unsigned)lo);
    g_text(2, y_lo - 5, 26, &FONT_XS, t, K_DIM);
    int cx = x0 + (int)ui.step * cw;               /* the cursor: the keys write here */
    g_rect(cx - 1, top, 2, h, K_CORAL);
    g_rect(cx + cw - 2, top, 2, h, K_CORAL);
    if (ui.track == T_VOICE)
        phrase_words();
}

/* ---- Arp: the notes a held C major chord would make ---- */
__attribute__((noinline)) static void page_arp(void)
{
    if (ui.track != T_KEYS && ui.track != T_BASS) {
        g_center(0, Y_MAIN + 52, 240, &FONT_S, "Keys and Bass arpeggiate", K_DIM);   /* (the strip's knobs are dark) */
        return;
    }
    const uint8_t *a = song.track[ui.track].arp;
    unsigned type = a[0] & 15u, oct = (a[0] >> 4 & 3u) + 1u, n = 3u * oct, gate = a[2] ? a[2] : 50u;
    static const uint8_t SIX[NARP_RATE] = {24, 12, 8, 6, 4, 3};
    int w = SIX[a[1] % NARP_RATE] * 2, top = Y_MAIN + 8, h = 120;
    uint32_t seed = 1;
    g_rect(4, top + h, 232, 1, K_RULE);
    for (int x = 4, i = 0; x < 236; x += w, i++) {
        unsigned j = type ? arp_order(type, n, (unsigned)i, &seed) : 0;
        int y = top + h - 10 - (int)j * (h - 14) / (int)(n > 1 ? n - 1 : 1);
        int nw = w * (int)gate / 100 - 1;
        if (!type)
            break;                                /* off: the notes as written */
        g_round(x, y, nw < 2 ? 2 : nw > 236 - x ? 236 - x : nw, 6, 2, K_TRACK[ui.track]);
    }
}

/* ---- Sound ---- */
__attribute__((noinline)) static void page_sound(void)
{
    if (ui.track == T_VOICE)
        return;                               /* the stage shows him */
    const track_t *k = &song.track[ui.track];
    if (ui.track == T_DRUMS) {
        for (unsigned i = 0; i < engine_kits(); i++) {
            int y = Y_MAIN + 6 + (int)i * 21;
            if (i == k->sound)
                g_ring(30, y - 1, 180, 22, 6, 2, K_CORAL, K_PAPER);
            g_center(30, y, 180, &FONT_S, engine_kit_name(i), i == k->sound ? K_INK : K_DIM);
        }
        return;
    }
    /* the track's own sounds, eight at a time, the one playing ringed */
    uint8_t list[64];
    unsigned n = sound_list(ui.track, list), at = sound_place(ui.track, list, n), first = at / 8u * 8u;
    char t[24] = "";
    s_num(t, (int)at + 1);
    s_cat(t, " of ");
    s_num(t, (int)n);
    g_right(8, Y_MAIN, 224, &FONT_XS, t, K_DIM);
    for (unsigned i = 0; i < 8 && first + i < n; i++) {
        unsigned p = list[first + i];
        int x = 6 + (int)(i / 4) * 116, y = Y_MAIN + 18 + (int)(i % 4) * 28;
        if (first + i == at)
            g_ring(x, y - 2, 112, 25, 6, 2, K_CORAL, K_PAPER);
        g_text(x + 8, y, 100, &FONT_S, PRESETS[p].name, first + i == at ? K_INK : K_DIM);
    }
}

/* ---- Envelope: the shape of a note in time ---- */
__attribute__((noinline)) static void page_env(void)
{
    if (ui.track == T_VOICE)
        return;                               /* the stage shows him */
    /* rise, hold, fall: the attack and the release (Drums: the attack and the length) */
    int drums = ui.track == T_DRUMS;
    int atk = sound_value(ui.track, drums ? 5u : 6u), rel = sound_value(ui.track, drums ? 1u : 7u);
    int top = Y_MAIN + 16, bot = Y_MAIN + 112, x0 = 16, xa = x0 + 4 + atk * 60 / 127, xh = xa + 50;
    int xr = xh + 8 + rel * (220 - xh - 8) / 127;
    g_rect(8, bot, 224, 1, K_RULE);
    g_line(x0, bot, xa, top, 3, K_INK);
    g_line(xa, top, xh, top, 3, K_INK);
    g_line(xh, top, xr, bot, 3, K_INK);
    if (drums) {
        int funk = sound_value(T_DRUMS, 4), rnd = sound_value(T_DRUMS, 6);
        if (funk)
            g_rect(x0 - 8, bot - 8, 4 + funk * 30 / 127, 3, K_CORAL);
        for (int i = 0; i < rnd / 16; i++)
            g_disc(xa + 8 + i * 12, top + 10 + (i * 7) % 20, 2, K_DIM);
    }
}

/* ---- Mixer ---- */
__attribute__((noinline)) static void page_mixer(void)
{
    unsigned sub = ui.sub[P_MIXER];
    static const char *const WHAT[3] = {"LEVEL", "REVERB", "DELAY"};
    for (unsigned t = 0; t < NTRACKS; t++) {
        const track_t *k = &song.track[t];
        int x = 8 + (int)t * 58, top = Y_MAIN + 4, h = 96;
        unsigned v = sub == 0 ? k->level : sub == 1 ? k->reverb : k->delay;
        g_round(x + 16, top, 22, h, 6, K_PANEL);
        int fill = (int)v * (h - 4) / 100;
        g_round(x + 18, top + h - 2 - fill, 18, fill, 5, k->mute ? K_FAINT : K_TRACK[t]);
        int m = eng_meter[t] * (h - 4) / 127;
        if (m > 2)
            g_rect(x + 40, top + h - 2 - m, 4, m, K_TEAL);
        g_center(x, top + h + 2, 54, &FONT_XS, TRACK_NAME[t], k->mute ? K_FAINT : K_INK);
        if (k->mute)
            g_center(x, top + h + 16, 54, &FONT_XS, "muted", K_CORAL);
    }
    (void)WHAT;
}

/* ---- Setup and Status ---- */
__attribute__((noinline)) static void page_setup(void)
{
    /* the mode, first: what the FM-1 is (SELECT switches) */
    g_round(150, Y_MAIN + 84, 82, 22, 6, ui.poetry ? K_CORAL : K_TEAL);
    g_center(150, Y_MAIN + 86, 82, &FONT_S, ui.poetry ? "SLAMJAM" : "KARAOKE", K_WHITE);
    g_text(10, Y_MAIN + 4, 220, &FONT_S, song.name, K_INK);
    char t[48] = "";
    s_num(t, song.bars);
    s_cat(t, " bars, ");
    s_num(t, song.rows);
    s_cat(t, " rows, ");
    s_num(t, song.notes);
    s_cat(t, " notes");
    g_text(10, Y_MAIN + 40, 220, &FONT_S, t, K_DIM);
    t[0] = 0;
    s_cat(t, ui.slot >= 0 ? "kept in S" : "not saved yet");
    if (ui.slot >= 0)
        s_num(t, ui.slot + 1);
    g_text(10, Y_MAIN + 64, 220, &FONT_S, t, K_DIM);
    g_text(10, Y_MAIN + 108, 220, &FONT_XS, "386DX " DX_BUILD, K_FAINT);
}

/* ---- overlays ---- */
__attribute__((noinline)) static void draw_slot_list(void)
{
    static const char *const FOR[] = {"LOAD", "SAVE TO", "CLEAR SLOT"};
    g_rect(0, Y_MAIN, 240, H_MAIN + 2, K_PAPER);
    g_text(8, Y_MAIN - 1, 224, &FONT_XS, FOR[ui.slot_for == A_LOAD ? 0 : ui.slot_for == A_SAVE_TO ? 1 : 2], K_DIM);
    unsigned first = ui.list_pick > 4 ? ui.list_pick - 4u : 0;
    for (unsigned i = first; i < SONG_SLOTS && i < first + 6; i++) {
        int y = Y_MAIN + 14 + (int)(i - first) * 20, on = i == ui.list_pick;
        char l[4] = "S";
        s_num(l, (int)i + 1);
        int full = ui.present >> i & 1u;
        if (on)
            g_round(4, y, 232, 19, 5, K_INK);
        g_text(12, y, 30, &FONT_S, l, on ? K_PAPER : K_INK);
        g_text(46, y, 186, &FONT_S, full ? ui.names[i] : "empty", on ? K_PAPER : full ? K_INK : K_FAINT);
        if ((int)i == ui.slot)
            g_disc(228, y + 9, 3, on ? K_PAPER : K_TEAL);          /* the song you are in */
    }
}

__attribute__((noinline)) static void draw_list(void)
{
    if (ui.list == 2) {
        draw_slot_list();
        return;
    }
    uint8_t a[NACTIONS];
    unsigned n = list_actions(a), first = ui.list_pick > 4 ? ui.list_pick - 4u : 0;
    g_rect(0, Y_MAIN, 240, H_MAIN + 2, K_PAPER);
    for (unsigned i = first; i < n && i < first + 6; i++) {
        int y = Y_MAIN + 2 + (int)(i - first) * 22, on = i == ui.list_pick;
        char v[40];
        list_value(a[i], v);
        if (on)
            g_round(4, y, 232, 21, 5, K_INK);
        int w = g_tw(&FONT_S, ACTION_NAME[a[i]]);
        g_text(12, y, 220, &FONT_S, ACTION_NAME[a[i]], on ? K_PAPER : K_INK);
        if (v[0])
            g_text(12 + w + 8, y + 3, 216 - w - 8, &FONT_XS, v, on ? K_PAPER : K_DIM);
    }
}

/* text wrapped at spaces to `width`, a new paragraph at each '|'; drawn from (x, y) when y >= 0. Returns its height */
static int wrap_text(const char *s, int x, int y, int width, uint16_t fg)
{
    int h = 0;
    while (*s) {
        char row[48];
        unsigned n = 0, space = 0;
        while (s[n] && s[n] != '|' && n < sizeof row - 1) {
            row[n] = s[n];
            row[n + 1] = 0;
            if (g_tw(&FONT_XS, row) > width)
                break;
            if (s[n] == ' ')
                space = n;
            n++;
        }
        if (s[n] && s[n] != '|' && space)
            n = space;
        row[n] = 0;
        if (y >= 0)
            g_text(x, y + h, width + 8, &FONT_XS, row, fg);
        h += 15;
        s += n;
        while (*s == ' ')
            s++;
        if (*s == '|') {
            s++;
            h += 5;
        }
    }
    return h;
}

static void draw_box(const char *title, const char *a, const char *b, uint16_t edge)
{
    g_round(10, Y_MAIN + 14, 220, 100, 10, edge);
    g_round(13, Y_MAIN + 17, 214, 94, 8, K_PAPER);
    g_text(22, Y_MAIN + 22, 200, &FONT_S, title, K_INK);
    int h = a ? wrap_text(a, 22, Y_MAIN + 48, 196, K_INK) : 0;
    if (b)
        wrap_text(b, 22, Y_MAIN + 48 + h, 196, K_DIM);
}

/* a button's help: the screen, then the buttons its screen doesn't label (card_text), then the knobs and the
 * footer's buttons it does (made here, so they are always the screen's own) */
#define CARD_W 218
#define CARD_H 154                            /* the room for its text */
static int card_h;
static char *card_put(char *d, const char *s)
{
    while (*s && d < card_buf + sizeof card_buf - 1)
        *d++ = *s++;
    *d = 0;
    return d;
}
static void draw_card(const char *const *foot, unsigned nf)
{
    const char *title, *t = card_text(ui.card - 1u, &title);
    char *d = card_buf + strlen(t);
    if (PAGE_BUTTON[ui.page] == ui.card - 1u) {
        for (unsigned k = 0, n = 0; k < 4 && ui.page != P_PHRASE; k++) {
            knob_t r = knob_info(k);
            if (r.name[0] != '-') {
                d = card_put(d, n++ ? ", " : "|Knobs: ");
                d = card_put(d, r.name);
            }
        }
        for (unsigned i = 0; i + 1 < nf; i += 2) {
            d = card_put(d, i ? "; " : "|Shown: ");
            d = card_put(d, foot[i] + (foot[i][0] == '('));
            d = card_put(d, " ");
            d = card_put(d, foot[i + 1]);
        }
    }
    g_round(4, Y_MAIN + 2, 232, 196, 10, K_INK);   /* over the page and its knob strip; the footer stays */
    g_round(7, Y_MAIN + 5, 226, 190, 8, K_PAPER);
    g_text(16, Y_MAIN + 9, 208, &FONT_S, title, K_INK);
    card_h = wrap_text(card_buf, 16, Y_MAIN + 32, CARD_W, K_INK);
}

__attribute__((noinline)) static void draw_words(void)
{
    g_rect(0, Y_MAIN, 240, H_MAIN + 2, K_PAPER);
    if (ui.wtarget)
        g_text(8, Y_MAIN, 220, &FONT_XS, "NAME", K_DIM);
    /* the text, wrapped, the cursor a coral bar */
    int x = 8, y = Y_MAIN + 16, n = (int)strlen(ui.wtext);
    for (int i = 0; i <= n; i++) {
        if (i == ui.wcur && (plat_ms() / 400u & 1u) == 0)
            g_rect(x, y + 3, 2, 20, K_CORAL);
        if (i == n)
            break;
        int w = ch_w(ui.wtext[i]);
        if (x + w > 232) {
            x = 8;
            y += 22;
        }
        ch_put(x, y, ui.wtext[i], K_INK);
        x += w;
    }
    /* the key map: the letters as the keys lie, white keys below, black above. Landmark keys wear a
     * heavy ring (as their lights are lit); the last key typed blinks coral (as its light does) */
    int ky = Y_MAIN + 84, blink = plat_ms() / 250u & 1u;
    for (unsigned k = 0; k < 27; k++) {
        int w = white_index(k);
        char c[2] = {TEXT_KEYS[k] == ' ' ? '_' : (char)(TEXT_KEYS[k] - 32), 0};
        if (w >= 0) {
            int x0 = 4 + w * 14, hot = (ui.last_key == k && blink) || (ui.keys >> k & 1u);
            if (LANDMARKS >> k & 1u) {
                g_round(x0, ky + 22, 13, 24, 3, K_INK);
                g_round(x0 + 2, ky + 24, 9, 20, 2, hot ? K_CORAL : K_WHITE);
            } else
                g_round(x0, ky + 22, 13, 24, 3, hot ? K_CORAL : K_WHITE);
            g_center(x0, ky + 26, 13, &FONT_XS, c, hot ? K_WHITE : K_INK);
        }
    }
    for (unsigned k = 0; k < 27; k++) {
        if (white_index(k) >= 0)
            continue;
        int left = white_index(k - 1), hot = (ui.last_key == k && blink) || (ui.keys >> k & 1u);
        int x0 = 4 + left * 14 + 8;
        char c[2] = {(char)(TEXT_KEYS[k] - 32), 0};
        if (LANDMARKS >> k & 1u) {
            g_round(x0 - 1, ky - 1, 14, 24, 4, K_WHITE);
            g_round(x0 + 1, ky + 1, 10, 20, 2, hot ? K_CORAL : K_INK);
        } else
            g_round(x0, ky, 12, 22, 3, hot ? K_CORAL : K_INK);
        g_center(x0, ky + 3, 12, &FONT_XS, c, K_PAPER);
    }
}

#include "stage.h"

/* Mister Wiggles' pages, where the stage is: Sing, the Voice's Sound and Envelope, FX's karaoke room */
static int stage_page(void)
{
    return ui.page == P_SING || ui.page == P_EFFECTS || ((ui.page == P_SOUND || ui.page == P_ENV) && ui.track == T_VOICE);
}

/* the bottom bar while the song plays on the stage: the words being sung (or a rest's bar), the song's progress */
__attribute__((noinline)) static void draw_lyrics(void)
{
    if (!g_visible(Y_STRIP, 240 - Y_STRIP))
        return;
    g_rect(0, Y_STRIP, 240, 240 - Y_STRIP, C_BG);
    int y = Y_STRIP + 18;
    if (ly.bar >= 0) {
        g_rect(20, y + 12, 200, 8, C_RULE);
        g_rect(20, y + 12, ly.bar, 8, K_TEAL);
    } else if (!ly.t)
        g_center(0, y + 6, 240, &FONT_S, song.name, C_DIM);
    else {                                    /* one line, or two */
        int two = ly.brk >= 0;
        for (int r = 0; r <= two; r++)
            lyric_run(y + (two ? 22 * r : 10), r ? ly.brk + 1 : 0, r == two ? ly.n : ly.brk);
    }
    unsigned len = song_ticks(&song);
    g_rect(0, 234, 240, 3, C_PANEL);
    g_rect(0, 234, len ? (int)(eng_tick * 240u / len) : 0, 3, K_TEAL);
}

/* ---- one frame: both halves ---- */
static void draw_scene(void)
{
    g_dark = stage_page();
    draw_header();
    if (g_dark)
        stage_draw();
    else
        g_rect(0, 23, 240, Y_STRIP - 23, K_PAPER);
    const char *foot[10];
    unsigned nf = 0;
    char tag[48] = "";
    switch (ui.page) {
    case P_SING:
        if (ui.poetry) {                          /* SLAMJAM: the knobs are the players' styles; a track taken is yours */
            s_cat(tag, SETS[poem.set].name);      /* the band tonight, and whose keys are yours */
            s_cat(tag, ", your keys: ");
            s_cat(tag, TRACK_NAME[ui.track]);
            if (song.track[ui.track].mute)
                s_cat(tag, " (taken)");
            foot[nf++] = "KEYS";
            foot[nf++] = ui.track == T_VOICE ? "sing" : "play";
            foot[nf++] = "HOME+BLACK";
            foot[nf++] = "jam";
            break;
        }
        s_cat(tag, TRACK_NAME[ui.track]);
        s_cat(tag, ui.track == T_VOICE ? " sings" : " plays");
        foot[nf++] = "KEYS";
        foot[nf++] = ui.track == T_VOICE ? "sing" : "play";
        if (ui.track != T_VOICE) {
            foot[nf++] = "(PRESET";
            foot[nf++] = "sound";
        }
        break;
    case P_ARRANGE: {
        page_arrange();
        s_cat(tag, TRACK_NAME[ui.track]);
        s_cat(tag, ": phrase ");
        s_num(tag, ui.ph + 1);
        s_cat(tag, " of ");
        s_num(tag, (int)lane_count(&song, ui.track));
        foot[nf++] = "SEL";
        foot[nf++] = "add, dupe, move";
        foot[nf++] = "HOME+SEQ";
        foot[nf++] = "phrase";
        break;
    }
    case P_PHRASE: {
        page_phrase();
        s_cat(tag, TRACK_NAME[ui.track]);
        s_cat(tag, " bar ");
        s_num(tag, ui.pbar + 1);
        s_cat(tag, "/");
        s_num(tag, (int)lane_len(&song, ui.track, ui.ph));
        if (ui.track != T_DRUMS) {                /* where the cursor is */
            s_cat(tag, " step ");
            s_num(tag, ui.step + 1);
        }
        if (ui.track != T_VOICE) {                /* a rest, or how many bars of the chain play this one */
            unsigned m = song.lane[ui.track][ui.bar] & 31u, uses = 0;
            for (unsigned b = 0; b < song.lane_bars[ui.track]; b++)
                uses += (song.lane[ui.track][b] & 31u) == m;
            if (m >= SONG_PATTERNS)
                s_cat(tag, ", rest");
            else if (uses > 1) {
                s_cat(tag, ", linked to ");
                s_num(tag, (int)uses - 1);
            }
        } else if (ui.row < song.rows) {
            unsigned line = song.row[ui.row].line, syl = line_syllables(line), cs = cursor_syl(ui.row);
            line_label(s_cat(tag, " "), line);
            if (syl) {                            /* the syllable the cursor is on, of the line's */
                s_cat(tag, " syl ");
                s_num(tag, (int)(cs < syl ? cs : syl - 1u) + 1);
                s_cat(tag, "/");
                s_num(tag, (int)syl);
            }
        }
        foot[nf++] = ui.track == T_DRUMS ? "WHITE" : "KEYS";
        foot[nf++] = ui.track == T_DRUMS ? "steps" : eng_playing ? "play" : "write";
        foot[nf++] = "HOME+SEQ";
        foot[nf++] = ui.track == T_VOICE ? "words" : "song";
        break;
    }
    case P_SOUND:
        page_sound();
        s_cat(tag, TRACK_NAME[ui.track]);
        if (ui.track == T_DRUMS) {
            s_cat(tag, " > ");
            s_cat(tag, engine_kit_name(song.track[T_DRUMS].sound));
        } else if (ui.track != T_VOICE) {
            s_cat(tag, " > ");
            s_cat(tag, PRESETS[song.track[ui.track].sound % NPRESETS].name);
        }
        if (ui.track != T_VOICE) {
            foot[nf++] = "(PRESET";
            foot[nf++] = "sound";
        }
        foot[nf++] = "KEYS";
        foot[nf++] = ui.track == T_VOICE ? "sing" : "play";
        break;
    case P_ENV:
        page_env();
        s_cat(tag, TRACK_NAME[ui.track]);
        s_cat(tag, ui.track == T_VOICE ? " > glide, vibrato" : " > envelope");
        if (ui.track != T_VOICE) {
            foot[nf++] = "(PRESET";
            foot[nf++] = "sound";
        }
        foot[nf++] = "KEYS";
        foot[nf++] = ui.track == T_VOICE ? "sing" : "play";
        break;
    case P_ARP:
        page_arp();
        s_cat(tag, TRACK_NAME[ui.track]);
        s_cat(tag, " arpeggiator");
        foot[nf++] = "KEYS";
        foot[nf++] = "hold notes";
        break;
    case P_EFFECTS: {
        static const char *const BANK[5] = {"echoes: reverb and delay", "the master", "the voice", "the voice's motion and reverb",
                                            "the band: each player's style"};
        s_cat(tag, BANK[ui.sub[P_EFFECTS] % 5u]);
        foot[nf++] = "WHITE";
        foot[nf++] = "effects";
        foot[nf++] = "BLACK";
        foot[nf++] = ui.poetry && ui.track == T_VOICE ? "his tune" : "play";
        break;
    }
    case P_MIXER: {
        page_mixer();
        static const char *const W[3] = {"levels", "reverb sends", "delay sends"};
        s_cat(tag, W[ui.sub[P_MIXER]]);
        foot[nf++] = "BLACK 1-4";
        foot[nf++] = "mute";
        break;
    }
    case P_SETUP:
        page_setup();
        s_cat(tag, "the song");
        foot[nf++] = "(SELECT";
        foot[nf++] = ui.poetry ? "karaoke" : "SLAMJAM";
        foot[nf++] = "SEL";
        foot[nf++] = "reset knobs";
        break;
    }
    if (ui.moving) {
        nf = 0;
        foot[nf++] = "SEL";
        foot[nf++] = "drop";
        foot[nf++] = "HOME";
        foot[nf++] = "back";
    }
    if (ui.list) {
        draw_list();
        nf = 0;                                   /* the rows say it all: SELECT, SEL and HOME are known */
    }
    if (ui.words) {
        draw_words();
        nf = 0;
        foot[nf++] = "SEL";
        foot[nf++] = "keep";
        foot[nf++] = "HOME";
        foot[nf++] = "cancel";
        foot[nf++] = "OCT-";
        foot[nf++] = "delete";
    }
    if (ui.msg_kind == M_GATE && plat_ms() < ui.msg_until) {
        draw_box(ui.msg[0], card_string(2u * CARD_GATE + 1u, card_buf, sizeof card_buf), 0, K_CORAL);
        nf = 0;
    }
    if (ui.ask) {
        draw_box(ui.ask_text, 0, 0, K_CORAL);
        nf = 0;
        foot[nf++] = "SEL";
        foot[nf++] = "yes";
        foot[nf++] = "HOME";
        foot[nf++] = "no";
    }
    if (ui.card)
        draw_card(foot, nf);
    if (g_dark && eng_playing && !ui.list && !ui.ask && !ui.words && !ui.card && plat_ms() - ui.knob_at > 1500u &&
        !(ui.poetry && ui.page != P_EFFECTS))
        draw_lyrics();                        /* while it plays: the words; a knob turned shows the strip a moment (SLAMJAM:
                                               * the words in the room only, where he says them) */
    else {
        if (!ui.card)
            draw_strip(tag);
        draw_footer(foot, nf);
    }
    if (g_dark)
        stage_lip();
}

/* the lights: lit, what works here; the keys, what they do here (as 386DX always lit them) */
static int key_of(unsigned pitch)            /* the key that plays this pitch (through the scale), -1 none */
{
    for (unsigned k = 0; k < 27; k++)
        if (key_note(k) == pitch)
            return (int)k;
    return -1;
}
/* the keys that sound a pitch class in mask (bit 0 C), every octave */
static uint32_t keys_in(uint16_t mask)
{
    uint32_t k = 0;
    for (unsigned kk = 0; kk < 27; kk++)
        if (mask >> (key_note(kk) % 12u) & 1u)
            k |= 1u << kk;
    return k;
}
/* SLAMJAM: the band's chord now (stopped, its first), as pitch classes: the keys that fit it */
static uint16_t chord_now(void)
{
    const int8_t (*cyc)[2] = CYCLE[poem.cycle & 7u];
    unsigned c = eng_playing ? (eng_tick / song.steps) & 3u : 0u, root = poem.key + (unsigned)cyc[c][0];
    uint16_t m = 0;
    for (unsigned i = 0; i < 4; i++)
        m |= (uint16_t)(1u << (root + (unsigned)Q_TONE[cyc[c][1]][i]) % 12u);
    return m;
}
__attribute__((noinline)) static void leds(void)
{
    uint32_t now = plat_ms(), b, k = 0;
    int blink = now / 250u & 1u;
    if (ui.words) {
        b = 1u << B_SEL | 1u << B_HOME | 1u << B_OCTDN | 1u << B_OCTUP;
        k = LANDMARKS;
        if (ui.last_key < 27 && blink)
            k ^= 1u << ui.last_key;               /* the key pressed last */
    } else if (ui.ask || ui.list || ui.moving) {
        b = 1u << B_SEL | 1u << B_HOME;
    } else {
        b = 1u << PAGE_BUTTON[ui.page];
        if (ui.rec || (ui.armed && blink))
            b |= 1u << B_REC;
        if (eng_playing || (ui.armed && blink))
            b |= 1u << B_PLAY;
        if (ui.dirty)
            b |= 1u << B_SAVE;
        if (ui.page == P_PHRASE && ui.track == T_DRUMS) {
            uint32_t m = drum_marks();             /* the steps the shown drum hits (the playhead is on screen) */
            for (unsigned kk = 0; kk < 27; kk++)
                if (white_index(kk) >= 0 && m >> white_index(kk) & 1u)
                    k |= 1u << kk;
        } else if (ui.page == P_PHRASE) {
            /* the notes at the cursor: steady where they start, blinking where one goes on from before */
            uint8_t p[8], st[8];
            for (unsigned i = 0, n = under_cursor(p, st); i < n; i++) {
                int kk = key_of(p[i]);
                if (kk >= 0 && (st[i] || blink))
                    k |= 1u << kk;
            }
        } else if (ui.track == T_VOICE && (ui.page == P_ARRANGE || (ui.page == P_SING && eng_playing && !ui.poetry))) {
            /* the line's tune: the keys it sings lit, the next one to sing blinking */
            int line = focus_line();
            if (line >= 0) {
                const line_t *L = &song.line[line];
                unsigned next = ui.live_line == (unsigned)line ? ui.live_syl : 0;
                int next_key = -1;
                for (unsigned i = 0; i < L->count; i++) {
                    const note_t *n = &song.note[L->first + i];
                    int kk = key_of(n->pitch);
                    if (kk < 0)
                        continue;
                    k |= 1u << kk;
                    if (eng_playing ? (int)(L->first + i) == eng_sing_note : !note_tie(n) && n->syllable == next && next_key < 0)
                        next_key = kk;
                }
                if (next_key >= 0 && blink)
                    k &= ~(1u << next_key);
            }
        } else if (ui.page == P_MIXER) {
            for (unsigned t = 0; t < NTRACKS; t++)    /* black keys 1-4: the tracks that play */
                if (!song.track[t].mute)
                    for (unsigned kk = 0; kk < 27; kk++)
                        if (black_index(kk) == (int)t)
                            k |= 1u << kk;
        } else if (ui.page == P_EFFECTS) {
            /* the first key of each family of four is lit, to find your place by; the keys last used blink
             * (steady while held, which ui.keys below lights); SLAMJAM: the black keys in the band's chord */
            if (ui.poetry && ui.track != T_DRUMS)
                for (unsigned kk = 0, ch = keys_in(chord_now()); kk < 27; kk++)
                    if (black_index((unsigned)kk) >= 0 && (ch >> kk & 1u))
                        k |= 1u << kk;
            for (unsigned kk = 0; kk < 27; kk++) {
                int w = white_index(kk);
                if (w >= 0 && w % 4 == 0)
                    k |= 1u << kk;
                if (w >= 0 && (ui.punch_last >> w & 1u) && !(ui.punch_held >> w & 1u)) {
                    if (blink)
                        k |= 1u << kk;
                    else
                        k &= ~(1u << kk);
                }
            }
        } else {
            /* the keys that fit: SLAMJAM, the band's chord now; else the song's key note, home, every octave; Drums,
             * the keys with a drum; the key played last blinks */
            k = ui.track == T_DRUMS ? 0u : ui.poetry ? keys_in(chord_now()) : keys_in((uint16_t)(1u << song.root % 12u));
            for (unsigned kk = 0; ui.track == T_DRUMS && kk < 27; kk++)   /* Drums: the keys with a drum */
                if (black_index(kk) >= 0 || (white_index(kk) >= 0 && white_index(kk) < 11))
                    k |= 1u << kk;
            if (ui.last_key < 27 && blink)
                k ^= 1u << ui.last_key;
        }
        k |= ui.keys;
    }
    plat_play_red(ui.rec || ui.armed);
    plat_glow(1);
    plat_leds(b, k);
}

void ui_frame(void)
{
    input_poll();
    if (ui.after_stop && !eng_playing) {          /* stopped now: the load, the poem or the mode it was waiting for */
        unsigned a = ui.after_stop - 1u;
        ui.after_stop = 0;
        run_action(a);
    }
    engine_hush(ui.poetry && ui.page != P_EFFECTS);
    if (ui.ntune && eng_playing && tune_row() != ui.tune_for)
        tune_set(ui.tune_for);                    /* (its line came up before the notes were all in: what there is) */  /* SLAMJAM: the poet performs in the room, the band anywhere */
    ui_draw();
}

/* the screen, when it is due. Also while the main loop waits for a sung clip (speech_background_service):
 * drawing only reads, so the room keeps moving while the second core pronounces */
static void ui_draw(void)
{
    uint32_t now = plat_ms();
    int animate = eng_playing || eng_sing_on || now < ui.msg_until || flashing() || ui.words || ui.card || eng_punch;
    /* still: the stage's wall drifts at 15 frames a second, other pages wait for an input */
    if (!animate && now - ui.frame_at < (stage_page() ? 66u : 200u) && ui.last_input < ui.frame_at)
        return;
    if (animate && now - ui.frame_at < 33u)
        return;
    if (animate && ui.animated && now - ui.frame_at > ui.gap_ms)
        ui.gap_ms = (uint8_t)(now - ui.frame_at > 255u ? 255u : now - ui.frame_at);
    ui.animated = (uint8_t)animate;
    ui.frame_at = now;
    for (int band = 0; band < NBANDS; band++) {
        g_begin(band, K_PAPER);
        draw_scene();
        cv_blit(0, (uint32_t)band * BAND);
    }
    uint32_t took = plat_ms() - now;
    if (took > ui.draw_ms)
        ui.draw_ms = (uint8_t)(took > 255u ? 255u : took);
    eng_drum_hits = 0;
    leds();
}
