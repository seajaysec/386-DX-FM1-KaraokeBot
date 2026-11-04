/* SPDX-License-Identifier: GPL-3.0-only */
/* The song: its defaults, its checks and the edits the UI makes. Main loop only; the renderer reads
 * the song while playing, so structural edits wait for the stop (the UI says STOP FIRST). */
#include "song.h"
#include <string.h>

#ifdef DX_HOST
song_t song;
#else
song_t song __attribute__((section(".pool")));
#endif

static const uint8_t VOICE_DEFAULT[NVOICE] = {50, 20, 20, 20, 35, 45, 1, 80};
static const uint8_t FX_DEFAULT[NFX] = {55, 50, 45, 30, 0, 20, 0, 50};

/* every knob as a new song has it: the tracks' levels, sends and sound knobs, the Voice's, the effects' */
void song_default_knobs(song_t *s)
{
    for (unsigned t = 0; t < NTRACKS; t++) {
        track_t *k = &s->track[t];
        k->level = t == T_VOICE ? 100 : 90;
        k->reverb = t == T_VOICE ? 25 : t == T_DRUMS ? 8 : 15;
        k->delay = t == T_VOICE ? 15 : 0;
        memset(k->param, PARAM_PRESET, sizeof k->param);
    }
    memcpy(s->voice, VOICE_DEFAULT, sizeof s->voice);
    memcpy(s->fx, FX_DEFAULT, sizeof s->fx);
}

void song_blank(song_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = SONG_MAGIC;
    s->version = SONG_VERSION;
    s->bpm = 100;
    memcpy(s->name, "New song", 9);
    s->steps = 16;
    s->bars = 8;
    song_default_knobs(s);
    s->track[T_KEYS].sound = SOUND_KEYS_DEFAULT;
    s->track[T_BASS].sound = SOUND_BASS_DEFAULT;
    for (unsigned p = 0; p < SONG_PATTERNS; p++)
        for (unsigned t = 0; t < NBACKING; t++)
            for (unsigned i = 0; i < SONG_STEPS; i++) {
                s->pattern[p].step[t][i].velocity = 100;
                s->pattern[p].step[t][i].length = t == T_DRUMS - 1 ? 1 : 2;
            }
    /* each chain one phrase: the Voice's 4 empty bars, a bar of Keys and of Bass (empty) and the beat */
    for (unsigned t = 0; t < NTRACKS; t++) {
        s->lane[t][0] = LANE_START;
        s->lane_bars[t] = 1;
    }
    s->lane_bars[T_VOICE] = 4;
    /* a beat to sing over: kick on the beats, snare on two and four, closed hats on the eighths */
    for (unsigned i = 0; i < 16; i++) {
        step_t *d = &s->pattern[0].step[T_DRUMS - 1][i];
        if (i % 4 == 0)
            d->drums |= 1u << 0;
        if (i % 8 == 4)
            d->drums |= 1u << 1;
        if (i % 2 == 0)
            d->drums |= 1u << 10;
        d->count = d->drums != 0;
    }
}

static int text_ok(const char *t, unsigned n)
{
    unsigned i = 0;
    for (; i < n && t[i]; i++)
        if ((uint8_t)t[i] < 32 || (uint8_t)t[i] > 126)
            return 0;
    return i < n;
}

int song_valid(const song_t *s)
{
    if (s->magic != SONG_MAGIC || s->version != SONG_VERSION || s->bpm < 40 || s->bpm > 240)
        return 0;
    if ((s->steps != 16 && s->steps != 12) || !s->bars || s->bars > SONG_BARS || s->rows > SONG_ROWS ||
        s->notes > SONG_NOTES || !text_ok(s->name, sizeof s->name))
        return 0;
    for (unsigned t = 0; t < NTRACKS; t++) {
        const track_t *k = &s->track[t];
        if (k->level > 100 || k->mute > 1 || k->reverb > 100 || k->delay > 100)
            return 0;
        for (unsigned p = 0; p < NPARAM; p++)
            if (k->param[p] > 127 && k->param[p] != PARAM_PRESET)
                return 0;
    }
    for (unsigned v = 0; v < NVOICE; v++)
        if (s->voice[v] > (v == V_OCTAVE ? 4 : 100))
            return 0;
    for (unsigned f = 0; f < NFX; f++)
        if (s->fx[f] > 100)
            return 0;
    /* the lines own consecutive, non-overlapping runs of the notes, in line order */
    unsigned next = 0;
    for (unsigned l = 0; l < SONG_LINES; l++) {
        const line_t *L = &s->line[l];
        if (!text_ok(L->text, SONG_TEXT) || L->first != next || L->first + L->count > s->notes)
            return 0;
        next += L->count;
        for (unsigned i = 0; i < L->count; i++) {
            const note_t *n = &s->note[L->first + i];
            if (!n->length || n->pitch < 36 || n->pitch > 84 || n->syllable >= SONG_SYLLABLES || n->tie > 15 ||
                note_nudge(n) < -3 || note_nudge(n) > 3)
                return 0;
            if (i) {
                const note_t *p = n - 1;
                if (n->at < p->at + p->length)
                    return 0;
                if (note_tie(n) && (n->at != p->at + p->length || n->syllable != p->syllable))
                    return 0;
            } else if (note_tie(n))
                return 0;
        }
    }
    if (next != s->notes)
        return 0;
    for (unsigned r = 0; r < s->rows; r++) {
        if (s->row[r].line >= SONG_LINES || s->row[r].at >= (unsigned)SONG_BARS * s->steps)
            return 0;
        if (r && s->row[r].at <= s->row[r - 1].at)
            return 0;
    }
    /* the chains: each starts a phrase on its first bar; the backing's bars are material 0..15 or a rest */
    for (unsigned t = 0; t < NTRACKS; t++) {
        unsigned n = s->lane_bars[t];
        if (!n || n > SONG_BARS || !(s->lane[t][0] & LANE_START))
            return 0;
        for (unsigned b = 0; b < n; b++) {
            unsigned m = s->lane[t][b] & 31u;
            if ((s->lane[t][b] & 0x60u) || (t != T_VOICE && m >= SONG_PATTERNS && m != LANE_REST) || (t == T_VOICE && m))
                return 0;
        }
    }
    for (unsigned p = 0; p < SONG_PATTERNS; p++)
        for (unsigned t = 0; t < NBACKING; t++)
            for (unsigned i = 0; i < SONG_STEPS; i++) {
                const step_t *st = &s->pattern[p].step[t][i];
                if (st->count > (t == T_KEYS - 1 ? 4 : 1) || st->velocity > 127 || !st->length || st->length > 16 ||
                    (st->drums & ~(DRUM_SOUNDS | DRUM_ALT)) || st->nudge < -3 || st->nudge > 3)
                    return 0;
                for (unsigned k = 0; k < st->count; k++)
                    if (st->note[k] > 127)
                        return 0;
            }
    return 1;
}

unsigned song_ticks(const song_t *s) { return (unsigned)s->bars * s->steps; }

unsigned line_span(const song_t *s, unsigned line)
{
    const line_t *L = &s->line[line % SONG_LINES];
    if (!L->count)
        return 0;
    const note_t *n = &s->note[L->first + L->count - 1];
    return n->at + n->length;
}

unsigned row_end(const song_t *s, unsigned r)
{
    return r + 1 < s->rows ? s->row[r + 1].at : (unsigned)s->lane_bars[T_VOICE] * s->steps;
}

int row_at(const song_t *s, unsigned tick)
{
    int found = -1;
    for (unsigned r = 0; r < s->rows && s->row[r].at <= tick; r++)
        found = (int)r;
    return found;
}

int line_rows(const song_t *s, unsigned line)
{
    int n = 0;
    for (unsigned r = 0; r < s->rows; r++)
        n += s->row[r].line == line;
    return n;
}

int line_unused(const song_t *s)
{
    for (unsigned l = 0; l < SONG_LINES; l++)
        if (!s->line[l].text[0] && !s->line[l].count && !line_rows(s, l))
            return (int)l;
    return -1;
}

int line_set_text(song_t *s, unsigned line, const char *text)
{
    if (line >= SONG_LINES)
        return -1;
    char t[SONG_TEXT];
    unsigned n = 0;
    while (text[n] && n < SONG_TEXT - 1) {
        uint8_t c = (uint8_t)text[n];
        t[n] = c >= 32 && c <= 126 ? (char)c : ' ';
        n++;
    }
    t[n] = 0;
    memcpy(s->line[line].text, t, n + 1);
    return 0;
}

/* the notes are one array, line 0's first: moving a line's notes shifts every later line's first */
static void shift_firsts(song_t *s, unsigned after_line, int by)
{
    for (unsigned l = after_line + 1; l < SONG_LINES; l++)
        s->line[l].first = (uint16_t)((int)s->line[l].first + by);
}

int note_insert(song_t *s, unsigned line, unsigned index, const note_t *n)
{
    line_t *L = &s->line[line % SONG_LINES];
    if (line >= SONG_LINES || s->notes >= SONG_NOTES || L->count >= 255 || index > L->count)
        return -1;
    unsigned at = L->first + index;
    memmove(&s->note[at + 1], &s->note[at], (s->notes - at) * sizeof *n);
    s->note[at] = *n;
    s->notes++;
    L->count++;
    shift_firsts(s, line, 1);
    return 0;
}

int note_delete(song_t *s, unsigned line, unsigned index)
{
    line_t *L = &s->line[line % SONG_LINES];
    if (line >= SONG_LINES || index >= L->count)
        return -1;
    unsigned at = L->first + index;
    memmove(&s->note[at], &s->note[at + 1], (s->notes - at - 1) * sizeof(note_t));
    s->notes--;
    L->count--;
    shift_firsts(s, line, -1);
    if (index < L->count)
        s->note[at].tie &= (uint8_t)~1u;  /* a tie can't follow a note that is gone (its nudge stays) */
    return 0;
}

void line_clear_notes(song_t *s, unsigned line)
{
    while (s->line[line % SONG_LINES].count)
        note_delete(s, line, s->line[line].count - 1u);
}

int line_copy(song_t *s, unsigned from)
{
    int to = line_unused(s);
    if (to < 0 || from >= SONG_LINES || s->notes + s->line[from].count > SONG_NOTES)
        return -1;
    memcpy(s->line[to].text, s->line[from].text, SONG_TEXT);
    for (unsigned i = 0; i < s->line[from].count; i++) {
        note_t n = s->note[s->line[from].first + i];
        note_insert(s, (unsigned)to, i, &n);
    }
    return to;
}

int row_insert(song_t *s, unsigned at, unsigned line)
{
    if (s->rows >= SONG_ROWS || line >= SONG_LINES)
        return -1;
    unsigned r = 0;
    while (r < s->rows && s->row[r].at < at)
        r++;
    if (r < s->rows && s->row[r].at == at)
        return -1;                       /* one row per start */
    memmove(&s->row[r + 1], &s->row[r], (s->rows - r) * sizeof(row_t));
    s->row[r].at = (uint16_t)at;
    s->row[r].line = (uint8_t)line;
    s->row[r].spare = 0;
    s->rows++;
    /* the song grows to hold what was written past its end */
    unsigned need = (at + (line_span(s, line) ? line_span(s, line) : s->steps) + s->steps - 1) / s->steps;
    if (need > s->bars)
        s->bars = (uint8_t)(need > SONG_BARS ? SONG_BARS : need);
    return (int)r;
}

int row_delete(song_t *s, unsigned r)
{
    if (r >= s->rows)
        return -1;
    memmove(&s->row[r], &s->row[r + 1], (s->rows - r - 1) * sizeof(row_t));
    s->rows--;
    return 0;
}

int row_move(song_t *s, unsigned r, int by)
{
    int o = (int)r + by;
    if (r >= s->rows || o < 0 || o >= s->rows)
        return -1;
    uint8_t line = s->row[r].line;
    s->row[r].line = s->row[o].line;
    s->row[o].line = line;
    return o;
}

int row_set_at(song_t *s, unsigned r, unsigned at)
{
    if (r >= s->rows || at >= song_ticks(s))
        return -1;
    if ((r && at <= s->row[r - 1].at) || (r + 1 < s->rows && at >= s->row[r + 1].at))
        return -1;
    s->row[r].at = (uint16_t)at;
    return 0;
}

unsigned row_next_at(const song_t *s)
{
    if (!s->rows)
        return 0;
    const row_t *last = &s->row[s->rows - 1];
    unsigned span = line_span(s, last->line);
    if (!span)
        span = 1;
    return (last->at + span + s->steps - 1) / s->steps * s->steps;
}

unsigned group_end(const song_t *s, unsigned note)
{
    unsigned i = note + 1;
    while (i < s->notes && note_tie(&s->note[i]))
        i++;
    return i;
}

/* ---- the chains ---- */
unsigned lane_count(const song_t *s, unsigned t)
{
    unsigned n = 0;
    for (unsigned b = 0; b < s->lane_bars[t]; b++)
        n += s->lane[t][b] >> 7;
    return n;
}

unsigned lane_start(const song_t *s, unsigned t, unsigned i)
{
    unsigned b = 0;
    for (unsigned n = 0; b < s->lane_bars[t]; b++)
        if ((s->lane[t][b] & LANE_START) && n++ == i)
            return b;
    return b;
}

unsigned lane_len(const song_t *s, unsigned t, unsigned i)
{
    unsigned b = lane_start(s, t, i), e = b + 1;
    while (e < s->lane_bars[t] && !(s->lane[t][e] & LANE_START))
        e++;
    return b < s->lane_bars[t] ? e - b : 0;
}

unsigned lane_find(const song_t *s, unsigned t, unsigned bar)
{
    unsigned n = 0;
    for (unsigned b = 1; b <= bar && b < s->lane_bars[t]; b++)
        n += s->lane[t][b] >> 7;
    return n;
}

/* the Voice's rows from bar `from` on move by `bars` (rows whose bar falls in a removed stretch are gone) */
static int rows_shift(song_t *s, unsigned from, int bars)
{
    int d = bars * (int)s->steps;
    for (unsigned r = 0; r < s->rows; r++)
        if (row_bar(s, r) >= from && (int)s->row[r].at + d >= (int)(SONG_BARS * s->steps))
            return -1;
    for (unsigned r = 0; r < s->rows; r++)
        if (row_bar(s, r) >= from) {
            int at = (int)s->row[r].at + d;
            s->row[r].at = (uint16_t)(at < 0 ? 0 : at);   /* a pickup can't go before the song's start */
        }
    return 0;
}

/* rows back in time order, and no two at one place */
static int rows_sort(song_t *s)
{
    for (unsigned i = 1; i < s->rows; i++)
        for (unsigned j = i; j > 0 && s->row[j - 1].at > s->row[j].at; j--) {
            row_t x = s->row[j];
            s->row[j] = s->row[j - 1];
            s->row[j - 1] = x;
        }
    for (unsigned i = 1; i < s->rows; i++)
        if (s->row[i].at == s->row[i - 1].at)
            return -1;
    return 0;
}

/* bars at b of track t's chain: room made (n > 0, filled with fill) or taken away (n < 0) */
static int lane_bars_move(song_t *s, unsigned t, unsigned b, int n, uint8_t fill)
{
    unsigned have = s->lane_bars[t], longest = 0;
    if ((int)have + n > SONG_BARS || (int)have + n < 1)
        return -1;
    for (unsigned k = 0; k < NTRACKS; k++)
        longest = s->lane_bars[k] > longest ? s->lane_bars[k] : longest;
    uint8_t *L = s->lane[t];
    if (n > 0) {
        memmove(L + b + n, L + b, have - b);
        memset(L + b, fill, (unsigned)n);
    } else
        memmove(L + b, L + b - n, have - b + (unsigned)n);
    s->lane_bars[t] = (uint8_t)((int)have + n);
    if (s->lane_bars[t] > s->bars)
        s->bars = s->lane_bars[t];              /* the song is as long as its longest chain */
    else if (n < 0 && s->bars == longest) {     /* .. and shrinks with it, unless BARS made it longer */
        unsigned m = 0;
        for (unsigned k = 0; k < NTRACKS; k++)
            m = s->lane_bars[k] > m ? s->lane_bars[k] : m;
        s->bars = (uint8_t)m;
    }
    return 0;
}

int lane_insert(song_t *s, unsigned t, unsigned i, unsigned bars)
{
    unsigned b = i < lane_count(s, t) ? lane_start(s, t, i) : s->lane_bars[t];
    if (!bars || (t == T_VOICE && rows_shift(s, b, (int)bars) != 0))
        return -1;
    if (lane_bars_move(s, t, b, (int)bars, t == T_VOICE ? 0 : LANE_REST) != 0) {
        if (t == T_VOICE)
            rows_shift(s, b + bars, -(int)bars);
        return -1;
    }
    s->lane[t][b] |= LANE_START;
    return 0;
}

int lane_delete(song_t *s, unsigned t, unsigned i)
{
    unsigned b = lane_start(s, t, i), n = lane_len(s, t, i);
    if (!n || n >= s->lane_bars[t])
        return -1;                               /* a chain keeps one phrase */
    if (t == T_VOICE) {
        for (unsigned r = 0; r < s->rows;)
            if (row_bar(s, r) >= b && row_bar(s, r) < b + n)
                row_delete(s, r);
            else
                r++;
        rows_shift(s, b + n, -(int)n);
    }
    lane_bars_move(s, t, b, -(int)n, 0);
    s->lane[t][0] |= LANE_START;
    return 0;
}

int lane_dupe(song_t *s, unsigned t, unsigned i)
{
    unsigned b = lane_start(s, t, i), n = lane_len(s, t, i);
    if (!n || s->lane_bars[t] + n > SONG_BARS)
        return -1;
    if (t != T_VOICE) {
        uint8_t copy[SONG_BARS];
        memcpy(copy, s->lane[t] + b, n);
        lane_bars_move(s, t, b + n, (int)n, 0);
        memcpy(s->lane[t] + b + n, copy, n);
        return 0;
    }
    unsigned had = s->rows, add = 0;
    for (unsigned r = 0; r < had; r++)
        add += row_bar(s, r) >= b && row_bar(s, r) < b + n;
    if (s->rows + add > SONG_ROWS || rows_shift(s, b + n, (int)n) != 0)
        return -1;
    for (unsigned r = 0; r < had; r++)
        if (row_bar(s, r) >= b && row_bar(s, r) < b + n) {
            s->row[s->rows] = s->row[r];
            s->row[s->rows++].at = (uint16_t)(s->row[r].at + n * s->steps);
        }
    lane_bars_move(s, t, b + n, (int)n, 0);
    s->lane[t][b + n] |= LANE_START;
    return rows_sort(s);
}

int lane_swap(song_t *s, unsigned t, unsigned i)
{
    if (i + 1 >= lane_count(s, t))
        return -1;
    unsigned a = lane_start(s, t, i), na = lane_len(s, t, i), nb = lane_len(s, t, i + 1);
    uint8_t copy[SONG_BARS];
    memcpy(copy, s->lane[t] + a, na + nb);
    memcpy(s->lane[t] + a, copy + na, nb);
    memcpy(s->lane[t] + a + nb, copy, na);
    if (t == T_VOICE)
        for (unsigned r = 0; r < s->rows; r++) {
            unsigned rb = row_bar(s, r);
            if (rb >= a && rb < a + na)
                s->row[r].at = (uint16_t)(s->row[r].at + nb * s->steps);
            else if (rb >= a + na && rb < a + na + nb)
                s->row[r].at = (uint16_t)(s->row[r].at - na * s->steps);
        }
    return t == T_VOICE ? rows_sort(s) : 0;
}

int lane_resize(song_t *s, unsigned t, unsigned i, unsigned bars)
{
    unsigned b = lane_start(s, t, i), n = lane_len(s, t, i);
    if (!n || !bars || bars == n)
        return -1;
    if (bars > n) {
        if (t == T_VOICE && rows_shift(s, b + n, (int)(bars - n)) != 0)
            return -1;
        return lane_bars_move(s, t, b + n, (int)(bars - n), t == T_VOICE ? 0 : LANE_REST);
    }
    if (t == T_VOICE) {
        for (unsigned r = 0; r < s->rows;)
            if (row_bar(s, r) >= b + bars && row_bar(s, r) < b + n)
                row_delete(s, r);
            else
                r++;
        rows_shift(s, b + n, -(int)(n - bars));
    }
    return lane_bars_move(s, t, b + bars, -(int)(n - bars), 0);
}

int lane_slot_free(song_t *s, unsigned t)
{
    unsigned used = 0;
    for (unsigned b = 0; b < s->lane_bars[t]; b++)
        if ((s->lane[t][b] & 31u) < SONG_PATTERNS)
            used |= 1u << (s->lane[t][b] & 31u);
    for (unsigned p = 0; p < SONG_PATTERNS; p++)
        if (!(used >> p & 1u)) {
            for (unsigned i = 0; i < SONG_STEPS; i++) {
                step_t *st = &s->pattern[p].step[t - 1][i];
                st->count = 0;
                st->drums = 0;
                st->velocity = 100;
                st->length = t == T_DRUMS ? 1 : 2;
                st->nudge = 0;
            }
            return (int)p;
        }
    return -1;
}
