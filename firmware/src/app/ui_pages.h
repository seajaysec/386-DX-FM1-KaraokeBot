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
static void draw_header(void)
{
    g_rect(0, 0, 240, 22, K_PAPER);
    g_rect(0, 22, 240, 1, K_RULE);
    if (plat_ms() < ui.msg_until && ui.msg[0][0]) {
        uint16_t bg = ui.msg_kind == M_NO ? K_INK : ui.msg_kind == M_UNDO ? K_PANEL : K_PAPER;
        uint16_t fg = ui.msg_kind == M_NO ? K_PAPER : K_INK;
        g_round(2, 1, 236, 20, 6, bg);
        int x = 8;
        if (ui.msg_kind == M_NO) {
            g_disc(14, 11, 7, K_CORAL);
            g_center(7, 1, 14, &FONT_XS, "!", K_WHITE);
            x = 26;
        } else if (ui.msg_kind == M_DONE) {
            g_line(9, 11, 12, 15, 3, K_TEAL);
            g_line(12, 15, 19, 6, 3, K_TEAL);
            x = 26;
        }
        int end = x + g_tw(&FONT_S, ui.msg[0]);
        g_text(x, 0, 230 - x, &FONT_S, ui.msg[0], fg);
        if (ui.msg[1][0])
            g_text(end + 8, 3, 232 - end - 8, &FONT_XS, ui.msg[1], ui.msg_kind == M_NO ? K_FAINT : K_DIM);
        return;
    }
    int x = g_cap(4, 4, BUTTON_CAP[PAGE_BUTTON[ui.page]], 0);
    g_text(x + 5, 0, 90, &FONT_S, PAGE_NAME[ui.page], K_INK);
    int px = 118;
    if (ui.page == P_SING || ui.page == P_SONG) {
        /* HOME's other view, a press away */
        const char *other = PAGE_NAME[ui.page == P_SING ? P_SONG : P_SING];
        int ox = x + 5 + g_tw(&FONT_S, PAGE_NAME[ui.page]) + 6;
        g_text(ox, 3, 60, &FONT_XS, other, K_DIM);
        px = ox + g_tw(&FONT_XS, other) + 8;
    }
    /* the track, in its colour */
    int tw = g_tw(&FONT_XS, TRACK_NAME[ui.track]) + 10;
    g_round(px, 4, tw, 15, 7, K_TRACK[ui.track]);
    g_text(px + 5, 0, tw, &FONT_XS, TRACK_NAME[ui.track], K_WHITE);
    char t[24] = "";
    if (eng_playing) {
        s_place(t, eng_tick, song.steps);
        g_right(150, 1, 72, &FONT_XS, t, K_TEAL);
        /* the play triangle */
        for (int i = 0; i < 9; i++)
            g_rect(226 + i / 2, 6 + i / 2, 1, 10 - (i / 2) * 2, ui.rec ? K_CORAL : K_TEAL);
    } else {
        s_num(t, song.bpm);
        s_cat(t, " BPM");
        g_right(150, 1, 78, &FONT_XS, t, K_DIM);
    }
    if (ui.dirty)
        g_disc(234, 4, 3, K_CORAL);
}

/* ---- the knob strip ---- */
static void draw_strip(const char *tag)
{
    g_rect(0, Y_STRIP, 240, 64, K_PAPER);
    g_rect(4, Y_STRIP + 8, 232, 1, K_RULE);
    int tw = g_tw(&FONT_XS, tag) + 8;
    g_rect(4, Y_STRIP, tw, 16, K_PAPER);
    g_text(6, Y_STRIP - 1, 180, &FONT_XS, tag, K_DIM);
    unsigned n = page_count();
    if (n > 1)
        for (unsigned i = 0; i < n; i++)
            g_disc(232 - (int)(n - 1 - i) * 9, Y_STRIP + 8, 3, i == ui.sub[ui.page] ? K_INK : K_FAINT);
    for (unsigned k = 0; k < 4; k++) {
        knob_t r = knob_info(k);
        int x = 4 + (int)k * 59;
        int hot = flashing() && ui.flash_step == (int16_t)(100 + k);
        g_text(x + 2, Y_STRIP + 15, 56, &FONT_XS, r.name, r.name[0] == '-' ? K_FAINT : K_INK);
        if (r.name[0] == '-')
            continue;
        int range = r.hi - r.lo, fill = range ? (r.value - r.lo) * 52 / range : 0;
        g_round(x + 2, Y_STRIP + 33, 52, 5, 2, K_RULE);
        g_round(x + 2, Y_STRIP + 33, fill < 4 ? 4 : fill, 5, 2, hot ? K_CORAL : K_DIM);
        char v[16];
        knob_text(k, &r, v);
        g_text(x + 2, Y_STRIP + 39, 56, &FONT_S, v, hot ? K_CORAL : K_INK);
    }
}

/* ---- the footer: controls as printed, each with its job ---- */
static void draw_footer(const char *const *parts, unsigned n)
{
    g_rect(0, Y_FOOT, 240, 16, K_PANEL);
    int x = 3;
    for (unsigned i = 0; i + 1 < n + 1 && i < n; i += 2) {
        int round = parts[i][0] == '(';
        const char *cap = round ? parts[i] + 1 : parts[i];
        x = g_cap(x, Y_FOOT + 1, cap, round);
        if (i + 1 < n) {
            g_text(x + 3, Y_FOOT - 1, 236 - x, &FONT_XS, parts[i + 1], K_INK);
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
    f->body = (float)song.voice[V_BODY] * 0.01f;
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
            g_rect(x - 1, y + 2, cw + 2, 20, K_TEAL_T);
        ch_put(x, y, ly.t[i], sung ? K_TEAL : K_INK);
        x += cw;
    }
}

static void page_sing(void)
{
    face_t f;
    face_state(&f);
    int y = Y_MAIN + 112;
    face_draw(120, Y_MAIN - (ly.brk >= 0 ? 18 : 0), &f);
    if (ly.bar >= 0) {
        g_rect(20, y + 8, 200, 8, K_RULE);
        g_rect(20, y + 8, ly.bar, 8, K_TEAL);
    } else if (!ly.t)
        g_center(0, y + 2, 240, &FONT_S, song.name, K_DIM);
    else {                                    /* one line, or two with the second where the one always sat */
        int two = ly.brk >= 0;
        for (int r = 0; r <= two; r++)
            lyric_run(y - 20 * (two - r), r ? ly.brk + 1 : 0, r == two ? ly.n : ly.brk);
    }
}

/* ---- Lines: the rows ---- */
static void line_label(char *d, unsigned line)
{
    s_cat(d, "L");
    s_num(d, (int)line + 1);
}

static void page_lines_entry(void)
{
    unsigned line = ui.entry_line, syl = line_syllables(line);
    const line_t *L = &song.line[line];
    line_offsets(line);
    /* the words, the next syllable to write in coral */
    const char *t = L->text;
    int x = 8, y = Y_MAIN + 4, n = (int)strlen(t);
    int cur = ui.entry_syl < SONG_SYLLABLES && ui.offsets[ui.entry_syl] != 255 ? ui.offsets[ui.entry_syl] : -1;
    int cur_end = ui.entry_syl + 1u < syl && ui.offsets[ui.entry_syl + 1] != 255 ? ui.offsets[ui.entry_syl + 1] : n;
    for (int i = 0; i < n; i++) {
        int w = ch_w(t[i]);
        if (x + w > 232) {
            x = 8;
            y += 22;
        }
        int here = cur >= 0 && i >= cur && i < cur_end && t[i] != ' ';
        if (here)
            g_rect(x - 1, y + 2, w + 2, 20, K_CORAL_T);
        ch_put(x, y, t[i], here ? K_CORAL : i < (cur < 0 ? n : cur) ? K_INK : K_DIM);
        x += w;
    }
    /* the tune so far: a bar a note, height by pitch */
    int top = Y_MAIN + 72, h = 56, span = (int)ui.entry_at + ui.entry_len;
    if (span < 16)
        span = 16;
    /* the pitch range: the line's notes, at least an octave */
    int lo = 127, hi = 0;
    for (unsigned i = 0; i < L->count; i++) {
        int p = song.note[L->first + i].pitch;
        lo = p < lo ? p : lo;
        hi = p > hi ? p : hi;
    }
    if (lo > hi)
        lo = hi = 60;
    if (hi - lo < 12) {
        lo -= (12 - (hi - lo)) / 2;
        hi = lo + 12;
    }
    g_rect(4, top - 2, 232, h + 4, K_PANEL);
    for (unsigned i = 0; i < L->count; i++) {
        const note_t *nt = &song.note[L->first + i];
        int nx = 6 + (int)nt->at * 228 / span, nw = (int)nt->length * 228 / span - 1;
        int ny = top + h - 8 - ((int)nt->pitch - lo) * (h - 10) / (hi - lo);
        g_round(nx, ny, nw < 2 ? 2 : nw, 5, 2, nt->tie ? K_DIM : K_INK);
    }
    int cx = 6 + (int)ui.entry_at * 228 / span;
    g_rect(cx, top - 2, 2, h + 4, K_CORAL);
    char m[40] = "";
    s_num(m, ui.entry_syl < syl ? ui.entry_syl + 1 : (int)syl);
    s_cat(m, " of ");
    s_num(m, (int)syl);
    s_cat(m, " syllables");
    g_text(8, Y_MAIN + 50, 220, &FONT_XS, m, K_DIM);
}

static void page_lines(void)
{
    if (ui.entry) {
        page_lines_entry();
        return;
    }
    unsigned rows = song.rows + 1u, first = ui.row > 2 ? ui.row - 2u : 0;
    if (first + 6 > rows)
        first = rows > 6 ? rows - 6 : 0;
    for (unsigned i = first; i < rows && i < first + 6; i++) {
        int y = Y_MAIN + 2 + (int)(i - first) * 22;
        int cursor = i == ui.row, playing = i < song.rows && eng_playing && eng_sing_row == (int)i;
        /* the place: the cursor's ring (coral), the row sounding now (teal), an edit landing (coral tint) */
        uint16_t inside = playing ? K_TEAL_T : flashing() && ui.flash_row == (int16_t)i ? K_CORAL_T : K_PAPER;
        if (cursor && ui.moving)
            g_round(1, y - 1, 238, 23, 6, K_CORAL_T), g_ring(1, y - 1, 238, 23, 6, 3, K_CORAL, K_CORAL_T);
        else if (cursor)
            g_ring(1, y - 1, 238, 23, 6, 2, K_CORAL, inside);
        else if (inside != K_PAPER)
            g_round(2, y, 236, 21, 5, inside);
        if (i == song.rows) {
            g_text(46, y, 190, &FONT_S, "+", cursor ? K_CORAL : K_DIM);
            g_text(62, y + 3, 170, &FONT_XS, "SEL: new line", cursor ? K_DIM : K_FAINT);
            continue;
        }
        const row_t *R = &song.row[i];
        char p[16] = "";
        s_num(p, R->at / song.steps + 1);
        if (R->at % song.steps) {
            s_cat(p, ".");
            s_num(p, (int)(R->at % song.steps / 4 + 1));
        }
        g_right(2, y + 3, 26, &FONT_XS, p, K_DIM);
        /* the line's number: dark when other rows sing the same line */
        char l[8] = "";
        line_label(l, R->line);
        int shared = line_rows(&song, R->line) > 1;
        g_round(32, y + 3, 26, 15, 4, shared ? K_INK : K_PANEL);
        g_center(32, y + 1, 26, &FONT_XS, l, shared ? K_PAPER : K_INK);
        const line_t *L = &song.line[R->line];
        g_text(64, y, 162, &FONT_S, L->text[0] ? L->text : "(no words)", L->text[0] ? (playing ? K_TEAL : K_INK) : K_FAINT);
        if (L->text[0] && !L->count)
            g_disc(230, y + 11, 3, K_CORAL);          /* words with no tune yet */
    }
}

/* ---- Steps: a pattern ---- */
static void page_steps(void)
{
    unsigned pat = song.chain[ui.bar] % SONG_PATTERNS, steps = song.steps;
    const pattern_t *P = &song.pattern[pat];
    int cw = 208 / (int)steps, x0 = 28;
    unsigned play_step = eng_playing && eng_tick / steps == ui.bar ? eng_tick % steps : 255;
    if (ui.track == T_DRUMS || ui.track == T_VOICE) {
        for (unsigned lane = 0; lane < 11; lane++) {
            int y = Y_MAIN + 2 + (int)lane * 12;
            static const char *const S[11] = {"KK", "SN", "LT", "MT", "HT", "RM", "CP", "CB", "CY", "OH", "CH"};
            g_text(4, y - 2, 24, &FONT_XS, S[lane], lane == ui.lane ? K_CORAL : K_DIM);
            for (unsigned s = 0; s < steps; s++) {
                const step_t *st = &P->step[T_DRUMS - 1][s];
                int on = (st->drums >> lane) & 1u;
                int x = x0 + (int)s * cw;
                uint16_t bg = s == play_step ? K_TEAL_T : (s / 4) & 1 ? K_PANEL : K_PAPER;
                g_rect(x, y, cw - 1, 11, bg);
                if (on)
                    g_round(x + 1, y + 1, cw - 3, 9, 2, lane == ui.lane ? K_INK : K_DIM);
            }
        }
    } else {
        /* a roll: each step's notes at their pitch, as long as they last */
        const step_t *S = P->step[ui.track - 1];
        int lo = 127, hi = 0;
        for (unsigned s = 0; s < steps; s++)
            for (unsigned k = 0; k < S[s].count; k++) {
                if (S[s].note[k] < lo)
                    lo = S[s].note[k];
                if (S[s].note[k] > hi)
                    hi = S[s].note[k];
            }
        if (lo > hi)
            lo = hi = ui.track == T_BASS ? 40 : 60;
        if (hi - lo < 12) {
            lo -= (12 - (hi - lo)) / 2;
            hi = lo + 12;
        }
        int top = Y_MAIN + 4, h = 128;
        for (unsigned s = 0; s < steps; s++) {
            int x = x0 + (int)s * cw;
            g_rect(x, top, cw - 1, h, s == play_step ? K_TEAL_T : (s / 4) & 1 ? K_PANEL : K_PAPER);
        }
        for (unsigned s = 0; s < steps; s++)
            for (unsigned k = 0; k < S[s].count; k++) {
                int y = top + h - 8 - (S[s].note[k] - lo) * (h - 10) / (hi - lo);
                int w = (int)S[s].length * cw - 2;
                if (x0 + (int)s * cw + w > x0 + (int)steps * cw)
                    w = x0 + (int)steps * cw - (x0 + (int)s * cw) - 2;
                g_round(x0 + (int)s * cw + 1, y, w, 6, 2, flashing() && ui.flash_step == (int16_t)s ? K_CORAL : K_INK);
            }
        char t[8] = "";
        s_note(t, (unsigned)hi);
        g_text(2, top - 3, 26, &FONT_XS, t, K_DIM);
        t[0] = 0;
        s_note(t, (unsigned)lo);
        g_text(2, top + h - 14, 26, &FONT_XS, t, K_DIM);
    }
    /* the cursor step */
    int cx = x0 + (int)ui.step * cw;
    g_rect(cx - 1, Y_MAIN, 2, H_MAIN - 2, ui.srec ? K_CORAL : K_FAINT);
    g_rect(cx + cw - 2, Y_MAIN, 2, H_MAIN - 2, ui.srec ? K_CORAL : K_FAINT);
}

/* ---- Sound ---- */
static void page_sound(void)
{
    if (ui.track == T_VOICE) {
        face_t f;
        face_state(&f);
        face_draw(120, Y_MAIN, &f);
        return;
    }
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
static void page_env(void)
{
    if (ui.track == T_VOICE) {
        face_t f;
        face_state(&f);
        face_draw(120, Y_MAIN, &f);
        return;
    }
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

/* ---- Change: the bar's pattern, what SHIFT, PITCH and WEAVE do to it ---- */
static void page_steps(void);
static void page_change(void)
{
    if (ui.track == T_VOICE) {
        if (ui.row < song.rows) {
            const line_t *L = &song.line[song.row[ui.row].line];
            g_text(8, Y_MAIN + 4, 224, &FONT_S, L->text[0] ? L->text : "(no words)", K_INK);
        }
        if (ui.row < song.rows) {             /* its tune: what PITCH moves */
            const line_t *L = &song.line[song.row[ui.row].line];
            int lo = 127, hi = 0, span = (int)line_span(&song, song.row[ui.row].line), top = Y_MAIN + 40, h = 80;
            for (unsigned i = 0; i < L->count; i++) {
                int p = song.note[L->first + i].pitch;
                lo = p < lo ? p : lo;
                hi = p > hi ? p : hi;
            }
            if (lo > hi || span < 1)
                return;
            if (hi - lo < 12) {
                lo -= (12 - (hi - lo)) / 2;
                hi = lo + 12;
            }
            g_rect(4, top - 2, 232, h + 4, K_PANEL);
            for (unsigned i = 0; i < L->count; i++) {
                const note_t *nt = &song.note[L->first + i];
                int nx = 6 + (int)nt->at * 228 / span, nw = (int)nt->length * 228 / span - 1;
                int ny = top + h - 8 - ((int)nt->pitch - lo) * (h - 10) / (hi - lo);
                g_round(nx, ny, nw < 2 ? 2 : nw, 6, 2, flashing() ? K_CORAL : K_INK);
            }
        }
        return;
    }
    page_steps();
}

/* ---- Effects: the punch-in keys ---- */
static void page_effects(void)
{
    for (unsigned i = 0; i < PU_N; i++) {
        int x = 4 + (int)(i % 4) * 58, y = Y_MAIN + 4 + (int)(i / 4) * 32;
        int on = eng_punch >> i & 1u;
        g_round(x, y, 56, 29, 6, on ? K_CORAL : K_PANEL);
        char num[4] = "";
        s_num(num, (int)i + 1);
        g_text(x + 4, y - 1, 20, &FONT_XS, num, on ? K_WHITE : K_DIM);
        g_text(x + 3, y + 12, 53, &FONT_XS, PUNCH_NAME[i], on ? K_WHITE : K_INK);
    }
}

/* ---- Mixer ---- */
static void page_mixer(void)
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
static void page_setup(void)
{
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
static void draw_slot_list(void)
{
    static const char *const FOR[] = {"LOAD", "SAVE TO", "NEW SONG"};
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

static void draw_list(void)
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

static void draw_box(const char *title, const char *a, const char *b, uint16_t edge)
{
    g_round(10, Y_MAIN + 14, 220, 100, 10, edge);
    g_round(13, Y_MAIN + 17, 214, 94, 8, K_PAPER);
    g_text(22, Y_MAIN + 22, 200, &FONT_S, title, K_INK);
    /* the text, wrapped at spaces */
    const char *parts[2] = {a, b};
    int y = Y_MAIN + 48;
    for (unsigned p = 0; p < 2; p++) {
        const char *s = parts[p];
        while (s && *s && y < Y_MAIN + 104) {
            char row[48];
            unsigned n = 0, space = 0;
            while (s[n] && n < sizeof row - 1) {
                row[n] = s[n];
                row[n + 1] = 0;
                if (g_tw(&FONT_XS, row) > 196)
                    break;
                if (s[n] == ' ')
                    space = n;
                n++;
            }
            if (s[n] && space)
                n = space;
            memcpy(row, s, n);
            row[n] = 0;
            g_text(22, y, 200, &FONT_XS, row, p ? K_DIM : K_INK);
            y += 15;
            s += n;
            while (*s == ' ')
                s++;
        }
    }
}

static void draw_words(void)
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

/* ---- one frame: both halves ---- */
static void draw_scene(void)
{
    draw_header();
    g_rect(0, 23, 240, Y_STRIP - 23, K_PAPER);
    const char *foot[10];
    unsigned nf = 0;
    char tag[48] = "";
    switch (ui.page) {
    case P_SING:
        page_sing();
        s_cat(tag, TRACK_NAME[ui.track]);
        s_cat(tag, ui.track == T_VOICE ? " sings" : " plays");
        foot[nf++] = "KEYS";
        foot[nf++] = ui.track == T_VOICE ? "sing" : "play";
        if (ui.track != T_VOICE) {
            foot[nf++] = "(PRESET";
            foot[nf++] = "sound";
        }
        break;
    case P_SONG:
        page_lines();
        if (ui.row < song.rows) {
            unsigned line = song.row[ui.row].line;
            line_label(tag, line);
            int n = line_rows(&song, line);
            if (n > 1) {
                s_cat(tag, " in ");
                s_num(tag, n);
                s_cat(tag, " rows");
            }
            s_cat(tag, " / row ");
            s_num(tag, ui.row + 1);
        } else
            s_cat(tag, "the end");
        if (ui.entry) {
            foot[nf++] = "KEYS";
            foot[nf++] = "note";
            foot[nf++] = "OCT+";
            foot[nf++] = "rest";
            foot[nf++] = "OCT-";
            foot[nf++] = "back";
            foot[nf++] = "REC";
            foot[nf++] = "done";
        } else {
            foot[nf++] = "KEYS";
            foot[nf++] = "sing it";
            foot[nf++] = "REC";
            foot[nf++] = "notes";
            foot[nf++] = "(PRESET";
            foot[nf++] = "line";
        }
        break;
    case P_STEPS: {
        page_steps();
        s_cat(tag, "bar ");
        s_num(tag, ui.bar + 1);
        s_cat(tag, " plays P");
        s_num(tag, song.chain[ui.bar] % SONG_PATTERNS + 1);
        unsigned uses = 0;
        for (unsigned b = 0; b < song.bars; b++)
            uses += song.chain[b] == song.chain[ui.bar];
        if (uses > 1) {
            s_cat(tag, " (");
            s_num(tag, (int)uses);
            s_cat(tag, " bars)");
        }
        if (ui.track == T_DRUMS) {
            foot[nf++] = "WHITE";
            foot[nf++] = "steps";
            foot[nf++] = "BLACK";
            foot[nf++] = DRUM_NAME[ui.lane];
            foot[nf++] = "(PRESET";
            foot[nf++] = "pattern";
        } else if (ui.track == T_VOICE) {
            foot[nf++] = "(ALGO";
            foot[nf++] = "track";
        } else if (ui.srec) {
            foot[nf++] = "KEYS";
            foot[nf++] = "write";
            foot[nf++] = "OCT+";
            foot[nf++] = "rest";
            foot[nf++] = "REC";
            foot[nf++] = "done";
        } else {
            static char notes[24];
            unsigned m = ui.track - T_KEYS;
            notes[0] = 0;
            for (unsigned i = 0; i < ui.nmem[m]; i++) {
                if (i)
                    s_cat(notes, " ");
                s_note(notes, ui.mem[m][i]);
            }
            foot[nf++] = "WHITE";
            foot[nf++] = notes;
            foot[nf++] = "SEQ+KEYS";
            foot[nf++] = "notes";
            foot[nf++] = "(PRESET";
            foot[nf++] = "pattern";
        }
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
    case P_CHANGE: {
        page_change();
        if (ui.track == T_VOICE) {
            s_cat(tag, "the line in Song");
        } else {
            s_cat(tag, "bar ");
            s_num(tag, ui.bar + 1);
            s_cat(tag, " plays P");
            s_num(tag, song.chain[ui.bar] % SONG_PATTERNS + 1);
        }
        foot[nf++] = "SEL";
        foot[nf++] = "weave in";
        foot[nf++] = "(PRESET";
        foot[nf++] = "pattern";
        break;
    }
    case P_EFFECTS:
        page_effects();
        s_cat(tag, ui.sub[P_EFFECTS] ? "the master" : "reverb and delay");
        foot[nf++] = "WHITE";
        foot[nf++] = "effects";
        break;
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
        foot[nf++] = "SEL";
        foot[nf++] = "songs";
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
    if (ui.ask) {
        draw_box(ui.ask_text, 0, 0, K_CORAL);
        nf = 0;
        foot[nf++] = "SEL";
        foot[nf++] = "yes";
        foot[nf++] = "HOME";
        foot[nf++] = "no";
    }
    if (ui.card) {
        const char *second, *first = card_text(ui.card - 1u, &second);
        char title[16] = "";
        s_cat(title, BUTTON_CAP[ui.card - 1u]);
        draw_box(title, first, second, K_INK);
    }
    draw_strip(tag);
    draw_footer(foot, nf);
}

/* the lights: lit, what works here; the keys, what they do here (as 386DX always lit them) */
static int key_of(unsigned pitch)
{
    int k = (int)pitch - 53 - ui.octave * 12;
    return k >= 0 && k < 27 ? k : -1;
}
static void leds(void)
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
        if (ui.rec || ui.entry || ui.srec || (ui.armed && blink))
            b |= 1u << B_REC;
        if (eng_playing || (ui.armed && blink))
            b |= 1u << B_PLAY;
        if (ui.dirty)
            b |= 1u << B_SAVE;
        if (ui.page == P_STEPS && ui.track != T_VOICE) {
            /* the steps that play; the one playing now and the one held blink */
            const pattern_t *P = &song.pattern[song.chain[ui.bar] % SONG_PATTERNS];
            unsigned now_step = eng_playing && eng_tick / song.steps == ui.bar ? eng_tick % song.steps : 255;
            for (unsigned kk = 0; kk < 27; kk++) {
                int w = white_index(kk);
                if (w < 0 || (unsigned)w >= song.steps)
                    continue;
                const step_t *st = &P->step[ui.track - 1][w];
                int on = ui.track == T_DRUMS ? (st->drums >> ui.lane & 1u) : st->count != 0;
                if ((unsigned)w == now_step || w == ui.hold)
                    on = blink ? !on : on;
                if (on)
                    k |= 1u << kk;
            }
        } else if (ui.track == T_VOICE && (ui.page == P_SONG || (ui.page == P_SING && eng_playing)) && !ui.entry) {
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
                    if (eng_playing ? (int)(L->first + i) == eng_sing_note : !n->tie && n->syllable == next && next_key < 0)
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
            /* the effects are four to a row on the screen: the first key and every fourth after it are lit, to
             * find your place by; the keys last used blink (steady while held, which ui.keys below lights) */
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
            for (unsigned kk = 0; kk < 27; kk++)  /* the white keys, there to play */
                if (white_index(kk) >= 0)
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
    uint32_t now = plat_ms();
    int animate = eng_playing || eng_sing_on || now < ui.msg_until || flashing() || ui.words || ui.card ||
                  (ui.page == P_EFFECTS && eng_punch);
    if (!animate && now - ui.frame_at < 200u && ui.last_input < ui.frame_at)
        return;
    if (animate && now - ui.frame_at < 33u)
        return;
    ui.frame_at = now;
    for (int band = 0; band < 3; band++) {
        g_begin(band, K_PAPER);
        draw_scene();
        cv_blit(0, (uint32_t)band * BAND);
    }
    eng_drum_hits = 0;
    leds();
}
