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

void song_blank(song_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = SONG_MAGIC;
    s->version = SONG_VERSION;
    s->bpm = 100;
    memcpy(s->name, "New song", 9);
    s->steps = 16;
    s->bars = 8;
    for (unsigned t = 0; t < NTRACKS; t++) {
        track_t *k = &s->track[t];
        k->level = t == T_VOICE ? 85 : 70;
        k->reverb = t == T_VOICE ? 25 : t == T_DRUMS ? 8 : 15;
        k->delay = t == T_VOICE ? 15 : 0;
        memset(k->param, PARAM_PRESET, sizeof k->param);
    }
    s->track[T_KEYS].sound = SOUND_KEYS_DEFAULT;
    s->track[T_BASS].sound = SOUND_BASS_DEFAULT;
    memcpy(s->voice, VOICE_DEFAULT, sizeof s->voice);
    memcpy(s->fx, FX_DEFAULT, sizeof s->fx);
    for (unsigned p = 0; p < SONG_PATTERNS; p++)
        for (unsigned t = 0; t < NBACKING; t++)
            for (unsigned i = 0; i < SONG_STEPS; i++) {
                s->pattern[p].step[t][i].velocity = 100;
                s->pattern[p].step[t][i].length = t == T_DRUMS - 1 ? 1 : 2;
            }
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
        if (s->voice[v] > (v == V_OCTAVE ? 2 : 100))
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
            if (!n->length || n->pitch < 36 || n->pitch > 84 || n->syllable >= SONG_SYLLABLES || n->tie > 1)
                return 0;
            if (i) {
                const note_t *p = n - 1;
                if (n->at < p->at + p->length)
                    return 0;
                if (n->tie && (n->at != p->at + p->length || n->syllable != p->syllable))
                    return 0;
            } else if (n->tie)
                return 0;
        }
    }
    if (next != s->notes)
        return 0;
    for (unsigned r = 0; r < s->rows; r++) {
        if (s->row[r].line >= SONG_LINES || s->row[r].at >= (unsigned)s->bars * s->steps)
            return 0;
        if (r && s->row[r].at <= s->row[r - 1].at)
            return 0;
    }
    for (unsigned b = 0; b < s->bars; b++)
        if (s->chain[b] >= SONG_PATTERNS)
            return 0;
    for (unsigned p = 0; p < SONG_PATTERNS; p++)
        for (unsigned t = 0; t < NBACKING; t++)
            for (unsigned i = 0; i < SONG_STEPS; i++) {
                const step_t *st = &s->pattern[p].step[t][i];
                if (st->count > (t == T_KEYS - 1 ? 4 : 1) || st->velocity > 127 || !st->length || st->length > 16 ||
                    st->drums >= 2048)
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
    return r + 1 < s->rows ? s->row[r + 1].at : song_ticks(s);
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
        s->note[at].tie = 0;            /* a tie can't follow a note that is gone */
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
    while (i < s->notes && s->note[i].tie)
        i++;
    return i;
}
