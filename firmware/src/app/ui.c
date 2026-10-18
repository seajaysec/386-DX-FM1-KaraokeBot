/* SPDX-License-Identifier: GPL-3.0-only */
/* 386DX's controls and screens. Part of the unity build (main.c), after the platform.
 * A button goes to one place; HOME's two are two views of one idea (the robot, the song it sings).
 *
 *   HOME   Sing: the robot and the lyric; again: Song, the rows of lyrics
 *   SEQ    Steps: a backing track's pattern (the white keys are its steps)
 *   EDIT   Sound: the selected track's sound     ENV   Envelope: its shape in time
 *   FX     Effects (the keys punch in)           LFO   Mixer
 *   GLO    Setup, Status; SEL: load, save to, new, rename
 *   ARP    Change: shift, transpose, weave another pattern in
 *   SELECT the place's pages; in a list or a grid, moves
 *   ALGORITHM the track (Voice, Keys, Bass, Drums); in a list, the item's value
 *   PRESETS the place's thing (sound, line, pattern)
 *   KNOB 1-4 the four values on the strip        PLAY  play / stop (from the page's place)
 *   REC    write what you play: notes into a line, steps into a pattern, or over the playing song
 *   SAVE   save; held + white key 1-8: save to that slot
 *   SEL    the list of everything else here; yes      HOME held: shift
 *   HOME + REC undo, + PLAY redo, + SELECT tempo, + white key punch-in, + black key 1-4 mute
 *   a button held alone: what it does here */
#include "song.h"
#include "vocal.h"
#include "engine.h"
#include "punch.h"
#include "speech.h"
#include "../dsp/synth.h"
#include "../dsp/drumkit.h"

unsigned engine_kits(void);
const char *engine_kit_name(unsigned k);
void engine_effects(void);
int factory_load(int i, song_t *s);

#ifndef DX_BUILD
#define DX_BUILD "dev"                /* tools/build.py sets the commit, +changes when uncommitted */
#endif
#include "ui_gfx.h"
#include "face.h"
static void companion_packet(uint32_t pkt, unsigned n);
static uint32_t remote_buttons, remote_keys, remote_at;
static uint32_t remote_tap_b, remote_tap_k;
static int8_t remote_enc[8];                  /* turns from the page or a test driver, taken at the next poll */   /* the panel as the companion page holds it */

enum { P_SING, P_SONG, P_STEPS, P_SOUND, P_ENV, P_EFFECTS, P_MIXER, P_SETUP, P_CHANGE, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"Sing", "Song", "Steps", "Sound", "Envelope", "Effects", "Mixer", "Setup",
                                              "Change"};
static const uint8_t PAGE_BUTTON[NPAGES] = {B_HOME, B_HOME, B_SEQ, B_EDIT, B_ENV, B_FX, B_LFO, B_GLO, B_ARP};
static const char *const BUTTON_CAP[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                           "PLAY", "REC", "OCT-", "OCT+"};
static const char *const TRACK_NAME[NTRACKS] = {"Voice", "Keys", "Bass", "Drums"};
static const char *const DRUM_NAME[11] = {"Kick", "Snare", "Low tom", "Mid tom", "High tom", "Rim", "Clap", "Cowbell",
                                          "Cymbal", "Open hat", "Closed hat"};
/* the typing keyboard: the key under each letter, as printed on the 386DX key map */
static const char TEXT_KEYS[28] = "lambncopdqersftguhvwixjyzk ";
static int white_index(unsigned k)    /* white keys 0..15, or -1 */
{
    static const int8_t W[27] = {0, -1, 1, -1, 2, -1, 3, 4, -1, 5, -1, 6, 7, -1, 8, -1, 9, -1, 10, 11, -1, 12, -1, 13, 14, -1, 15};
    return k < 27 ? W[k] : -1;
}
static int black_index(unsigned k)    /* black keys 0..10, or -1 */
{
    int n = -1;
    for (unsigned j = 0; j <= k && j < 27; j++)
        if (white_index(j) < 0)
            n++;
    return k < 27 && white_index(k) < 0 ? n : -1;
}

/* ------------------------------------------------------------------ state --- */
enum { Q_NONE, Q_LOAD, Q_SAVE_OVER, Q_NEW, Q_DELETE_ROW, Q_CLEAR_NOTES, Q_CLEAR_TRACK, Q_CLEAR_PATTERN };
enum { M_INFO, M_DONE, M_NO, M_UNDO };
static struct {
    uint8_t page, sub[NPAGES], track;
    int8_t octave;
    uint32_t buttons, keys, press_at, held_since;
    uint8_t used, card;               /* a button used with another (no release action); the help card shown */
    uint8_t pressed;                  /* the button held alone, for its card */
    /* lines */
    uint8_t row;                      /* the cursor: a row, or song.rows (the END row) */
    uint8_t syllables[SONG_LINES];    /* each line's syllable count (0: not counted yet / no words) */
    uint8_t counted[SONG_LINES];
    uint8_t offsets[SONG_SYLLABLES];  /* the focused line's syllables' character offsets */
    int8_t offsets_line;
    /* words */
    uint8_t words, wtarget;           /* editing; 0 a line, 1 the song's name */
    uint8_t wline, wcur;
    char wtext[SONG_TEXT];
    /* note entry into a line */
    uint8_t entry, entry_line, entry_syl, entry_len, entry_key, entry_held;
    uint16_t entry_at;
    /* the Voice keys */
    uint8_t live_syl, live_line, sing_key;
    uint32_t live_at;
    /* steps */
    uint8_t bar, step, lane, velo, length, srec;
    uint8_t chord[4], nchord, chord_keys;
    int8_t hold;                      /* the step a white key holds on Steps, -1 none */
    uint8_t hold_key, hold_used, hold_fresh;   /* its key; something else done while held; written by this press */
    uint8_t last_key;                 /* the key pressed last (its light blinks), 255 none */
    uint8_t mem[2][4], nmem[2];       /* Keys' and Bass's notes a white key writes on Steps (SEQ held + keys sets them) */
    /* change */
    uint8_t weave;                    /* the pattern WEAVE takes from */
    int8_t shifted, transposed;       /* how far SHIFT and PITCH have gone here */
    /* recording over the playing song */
    uint8_t rec, armed;
    /* songs: the Setup list's LOAD and SAVE TO */
    uint8_t pick, pick_save, pick_new, home_toggle;
    uint16_t punch_held, punch_last;  /* the punch-in keys down, and the ones last down together (their light blinks) */
    uint8_t slot_for;                 /* list 2: the slot list for LOAD, SAVE TO or NEW SONG (an A_*) */
    uint8_t moving;                   /* Song: a row's line carried by SELECT */
    int8_t move_net;                  /* .. how far it has gone (HOME takes it back) */
    int8_t slot;                      /* the slot the song came from, -1 none */
    uint8_t dirty, present, save_when_stopped;
    uint32_t dirty_at;
    char names[SONG_SLOTS][24];
    /* lists, questions, messages */
    uint8_t list, list_pick, ask, ask_arg;
    char ask_text[40];
    char msg[2][32];
    uint8_t msg_kind;
    uint32_t msg_until;
    uint32_t flash_at;
    int16_t flash_row, flash_step;
    /* undo: one step, a whole copy */
    uint8_t undo_valid, redo_valid;
    uint32_t undo_at;
    uint32_t frame_hash, frame_at;
    uint32_t idle_at, last_input;
    uint8_t save_soon;                /* words just kept: written to the slot as soon as the speech engine is idle */
    uint32_t restart_at;              /* the companion asked for a soft reset: when, 0 none */
} ui;

static song_t undo_song X0X_POOL;

/* ---------------------------------------------------------------- helpers --- */
static void say_kind(const char *a, const char *b, unsigned kind, unsigned ms)
{
    s_copy(ui.msg[0], a ? a : "", sizeof ui.msg[0]);
    s_copy(ui.msg[1], b ? b : "", sizeof ui.msg[1]);
    s_upper(ui.msg[0]);
    ui.msg_kind = (uint8_t)kind;
    ui.msg_until = plat_ms() + ms;
}
static void say(const char *a, const char *b) { say_kind(a, b, M_INFO, 1400); }
static void done(const char *a, const char *b) { say_kind(a, b, M_DONE, 1400); }
static void refuse(const char *a, const char *b) { say_kind(a, b, M_NO, 2000); }
void ui_say(const char *a) { say(a, 0); }

static void ask(unsigned q, unsigned arg, const char *text)
{
    ui.ask = (uint8_t)q;
    ui.ask_arg = (uint8_t)arg;
    s_copy(ui.ask_text, text, sizeof ui.ask_text);
    s_upper(ui.ask_text);
}

static void answer(int yes);

static void changed(void)
{
    ui.dirty = 1;
    ui.dirty_at = plat_ms();
}

/* call before changing the song: the first change after a pause keeps a copy to go back to */
static void undo_mark(void)
{
    if (plat_ms() - ui.undo_at > 1000u || !ui.undo_valid) {
        memcpy(&undo_song, &song, sizeof song);
        ui.undo_valid = 1;
        ui.redo_valid = 0;
    }
    ui.undo_at = plat_ms();
}

static int stopped_or_say(void)
{
    if (eng_playing) {
        refuse("STOP FIRST", 0);
        return 0;
    }
    return 1;
}

/* the key's note: from F3, an octave a step of OCT; Bass two octaves down, where basses live (F1..G3) */
static unsigned key_note(unsigned k) { return 53u + k + (unsigned)(ui.octave * 12) - (ui.track == T_BASS ? 24u : 0u); }

/* each synth track's own sounds (numbers into PRESETS, so a song keeps meaning what it chose) */
static const uint8_t BASS_SOUNDS[] = {40, 41, 42, 43, 44, 45, 46, 47, 4, 8, 13, 18, 27, 36};
#define SOUND_RETIRED 7                       /* "acid line": all but silent (measured); "acid squelch" took its place */
static unsigned sound_list(unsigned track, uint8_t *out)
{
    unsigned n = 0;
    if (track == T_BASS) {
        for (unsigned i = 0; i < sizeof BASS_SOUNDS; i++)
            if (BASS_SOUNDS[i] < (unsigned)NPRESETS)
                out[n++] = BASS_SOUNDS[i];
        return n;
    }
    for (unsigned p = 0; p < (unsigned)NPRESETS && p < 64u; p++) {
        unsigned bass = 0;
        for (unsigned i = 0; i < sizeof BASS_SOUNDS; i++)
            bass |= BASS_SOUNDS[i] == p;
        if (!bass && p != SOUND_RETIRED)
            out[n++] = (uint8_t)p;
    }
    return n;
}
static unsigned sound_place(unsigned track, const uint8_t *list, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        if (list[i] == song.track[track].sound)
            return i;
    return 0;
}

static int focus_line(void)
{
    if (eng_playing && eng_sing_row >= 0 && ui.page == P_SING)
        return song.row[eng_sing_row].line;
    if (ui.row < song.rows)
        return song.row[ui.row].line;
    return ui.live_line < SONG_LINES && song.line[ui.live_line].text[0] ? ui.live_line : -1;
}

/* a line's syllables, counted by the speech engine (cached until its words change) */
static unsigned line_syllables(unsigned line)
{
    if (line >= SONG_LINES)
        return 0;
    if (!ui.counted[line]) {
        int n = vocal_syllables(song.line[line].text, ui.offsets_line == (int8_t)line ? ui.offsets : 0);
        ui.syllables[line] = (uint8_t)(n > 0 ? n : 0);
        ui.counted[line] = 1;
    }
    return ui.syllables[line];
}
static void line_offsets(unsigned line)
{
    if (ui.offsets_line == (int8_t)line)
        return;
    memset(ui.offsets, 255, sizeof ui.offsets);
    if (line < SONG_LINES && song.line[line].text[0]) {
        vocal_syllables(song.line[line].text, ui.offsets);
    }
    ui.offsets_line = (int8_t)line;
}
static void words_changed(unsigned line)
{
    ui.counted[line] = 0;
    if (ui.offsets_line == (int8_t)line)
        ui.offsets_line = -1;
    vocal_reset();
}

/* syllables with notes in a line: one past the highest syllable written */
static unsigned line_pitched(unsigned line)
{
    const line_t *L = &song.line[line];
    unsigned n = 0;
    for (unsigned i = 0; i < L->count; i++)
        if (song.note[L->first + i].syllable + 1u > n)
            n = song.note[L->first + i].syllable + 1u;
    return n;
}

/* ---------------------------------------------------------- songs on flash --- */
static void slots_read(void)
{
    struct { uint32_t magic; uint16_t version, bpm; char name[24]; } h;
    ui.present = 0;
    for (unsigned i = 0; i < SONG_SLOTS; i++) {
        int n = plat_store_load(OBJ_BANK0 + i, &h, sizeof h);
        if (n == (int)sizeof h && h.magic == SONG_MAGIC && h.version == SONG_VERSION) {
            ui.present |= (uint8_t)(1u << i);
            s_copy(ui.names[i], h.name, sizeof ui.names[i]);
        } else
            ui.names[i][0] = 0;
    }
}

static void song_loaded(void)
{
    eng_playing = 0;
    engine_song();
    vocal_reset();
    memset(ui.counted, 0, sizeof ui.counted);
    ui.offsets_line = -1;
    ui.row = 0;
    ui.bar = ui.step = 0;
    ui.dirty = 0;
    ui.undo_valid = ui.redo_valid = 0;
    ui.entry = 0;
    ui.rec = ui.armed = 0;
    punch_tempo(song.bpm);
    /* the face rests at the middle of the song's tune */
    unsigned lo = 127, hi = 0;
    for (unsigned i = 0; i < song.notes; i++) {
        if (song.note[i].pitch < lo)
            lo = song.note[i].pitch;
        if (song.note[i].pitch > hi)
            hi = song.note[i].pitch;
    }
    face_mid = song.notes ? (float)(lo + hi) * 0.5f : 62.0f;
}

/* the song, in its slot, now: for a change that must not be lost (the words just kept) */
static void save_now(void)
{
    if (ui.slot < 0 || eng_playing || !song_valid(&song))
        return;
    if (plat_store_save(OBJ_BANK0 + (unsigned)ui.slot, &song, sizeof song) == 0)
        ui.dirty = 0;
}

static int save_slot(unsigned s)
{
    if (eng_playing) {
        ui.save_when_stopped = (uint8_t)(s + 1);
        say("SAVES WHEN STOPPED", 0);
        return 0;
    }
    if (!song_valid(&song)) {
        refuse("NOT SAVED", "song damaged");
        return -1;
    }
    if (plat_store_save(OBJ_BANK0 + s, &song, sizeof song) != 0) {
        refuse("NOT SAVED", "flash error");
        return -1;
    }
    uint32_t boot[2] = {0x44584200u | s, SONG_MAGIC};
    plat_store_save(OBJ_SET, boot, sizeof boot);
    ui.slot = (int8_t)s;
    ui.dirty = 0;
    slots_read();
    char t[8] = "S";
    s_num(t, (int)s + 1);
    done("SAVED", t);
    return 0;
}

static int load_entry(unsigned pick)
{
    /* S1..S8, or past them a new song */
    song_blank(&song);
    ui.slot = -1;
    if (pick < SONG_SLOTS) {
        int n = plat_store_load(OBJ_BANK0 + pick, &song, sizeof song);
        if (n != (int)sizeof song || !song_valid(&song)) {
            song_blank(&song);
            song_loaded();
            refuse("CAN'T LOAD", "the slot is empty");
            return -1;
        }
        ui.slot = (int8_t)pick;
    }
    song_loaded();
    return 0;
}

/* --------------------------------------------------------- the four knobs --- */
typedef struct { const char *name; int lo, hi, value; } knob_t;
static knob_t knob_info(unsigned k);
static void knob_set(unsigned k, int v);

static unsigned page_count(void)
{
    switch (ui.page) {
    case P_EFFECTS: return 2u;
    case P_MIXER: return 3u;
    default: return 1u;
    }
}

static const char *const SOUND_KNOB_ENGINE[NENGINE][4] = {
    {"SHAPE", "WIDTH", "NOISE", "STEREO"}, {"SHAPE", "RATIO", "DETUNE", "STEREO"}, {"TABLE", "MORPH", "WARP", "DRIFT"},
    {"TONE", "RATIO", "SHAPE", "TREMOLO"}, {"TONE", "TEXTURE", "PUNCH", "TINE"},  {"TYPE", "BASS", "TREMOLO", "SPEED"},
    {"FREQ", "SUB", "NOISE", "LOWCUT"},    {"SWARM", "AM", "FM", "DETUNE"},       {"-", "-", "-", "-"}};
/* Sound (EDIT) and Envelope (ENV): the Voice's settings, and the track parameter each knob turns (-1 none) */
static const uint8_t VOICE_KNOB[2][4] = {{V_BODY, V_BREATH, V_EDGE, V_OCTAVE}, {V_GLIDE, V_VIBRATO, V_RATE, NVOICE}};
static const char *const VOICE_KNOB_NAME[2][4] = {{"BODY", "BREATH", "EDGE", "OCTAVE"}, {"GLIDE", "VIBRATO", "RATE", "-"}};
static const char *const DRUM_KNOB_NAME[2][4] = {{"TUNE", "DRIVE", "TONE", "-"}, {"FUNK", "ATTACK", "RANDOM", "LENGTH"}};
static int sound_param(unsigned k)
{
    static const int8_t P[2][2][4] = {{{0, 1, 2, 3}, {0, 2, 3, -1}}, {{6, 7, 4, 5}, {4, 5, 6, 1}}};
    return P[ui.page == P_ENV][ui.track == T_DRUMS][k & 3u];
}

/* a sound knob's value: the song's own, else the preset's */
static int sound_value(unsigned t, unsigned j)
{
    const track_t *k = &song.track[t];
    if (k->param[j] != PARAM_PRESET)
        return k->param[j];
    if (t == T_DRUMS)
        return j == 0 || j == 1 ? 64 : j == 2 || j >= 4 ? 0 : 127;
    static const uint8_t TP[NPARAM] = {TP_E1, TP_E2, TP_E3, TP_E4, TP_CUT, TP_RES, TP_A, TP_R};
    unsigned n = k->sound < (unsigned)NPRESETS ? k->sound : 0;
    return PRESETS[n].par[TP[j]];
}

/* ---------------------------------------------------- changing a pattern --- */
static pattern_t *bar_pattern(void) { return &song.pattern[song.chain[ui.bar] % SONG_PATTERNS]; }
static step_t *step_at(unsigned step);
static void step_flash(unsigned step);

/* SHIFT: the track's steps turn around the bar, d steps later */
static void steps_rotate(step_t *s, int d)
{
    int n = song.steps;
    step_t t[SONG_STEPS];
    for (int i = 0; i < n; i++)
        t[((i + d) % n + n) % n] = s[i];
    memcpy(s, t, (unsigned)n * sizeof *s);
}

/* PITCH: every note of the track's pattern (Keys, Bass), or of the Song cursor's line (Voice) */
static int notes_transpose(int d)
{
    if (ui.track == T_VOICE) {
        if (ui.row >= song.rows) {
            refuse("NO LINE HERE", 0);
            return -1;
        }
        const line_t *L = &song.line[song.row[ui.row].line];
        for (unsigned i = 0; i < L->count; i++) {
            int p = song.note[L->first + i].pitch + d;
            if (p < 36 || p > 84) {
                refuse(d > 0 ? "TOO HIGH" : "TOO LOW", "the voice sings C2 to C6");
                return -1;
            }
        }
        for (unsigned i = 0; i < L->count; i++)
            song.note[L->first + i].pitch = (uint8_t)(song.note[L->first + i].pitch + d);
        vocal_reset();
        return 0;
    }
    step_t *s = bar_pattern()->step[ui.track - 1];
    for (unsigned i = 0; i < SONG_STEPS; i++)
        for (unsigned j = 0; j < s[i].count; j++)
            if (s[i].note[j] + d < 0 || s[i].note[j] + d > 127) {
                refuse(d > 0 ? "TOO HIGH" : "TOO LOW", 0);
                return -1;
            }
    for (unsigned i = 0; i < SONG_STEPS; i++)
        for (unsigned j = 0; j < s[i].count; j++)
            s[i].note[j] = (uint8_t)(s[i].note[j] + d);
    return 0;
}

/* WEAVE: another pattern's steps into this one: its drum hits added, its Keys notes added to a chord
 * (up to four), its Bass notes where this one rests. Nothing here is taken away */
static void pattern_weave(unsigned from)
{
    unsigned to = song.chain[ui.bar] % SONG_PATTERNS;
    if (from == to) {
        refuse("SAME PATTERN", "KNOB 3: another");
        return;
    }
    undo_mark();
    const pattern_t *a = &song.pattern[from];
    pattern_t *b = &song.pattern[to];
    for (unsigned i = 0; i < SONG_STEPS; i++) {
        const step_t *x = &a->step[T_DRUMS - 1][i];
        step_t *y = &b->step[T_DRUMS - 1][i];
        if (x->drums) {
            if (!y->drums)
                y->velocity = x->velocity;
            y->drums |= x->drums;
            y->count = 1;
            y->length = 1;
        }
        x = &a->step[T_KEYS - 1][i];
        y = &b->step[T_KEYS - 1][i];
        if (!y->count)
            *y = *x;
        else
            for (unsigned j = 0; j < x->count && y->count < 4; j++) {
                unsigned have = 0;
                for (unsigned m = 0; m < y->count; m++)
                    have |= y->note[m] == x->note[j];
                if (!have)
                    y->note[y->count++] = x->note[j];
            }
        x = &a->step[T_BASS - 1][i];
        y = &b->step[T_BASS - 1][i];
        if (!y->count)
            *y = *x;
    }
    changed();
    char t[24] = "WOVE P";
    s_num(t, (int)from + 1);
    s_cat(t, " IN");
    done(t, 0);
}

static knob_t knob_info(unsigned k)
{
    knob_t r = {"-", 0, 0, 0};
    unsigned sub = ui.sub[ui.page];
    const uint8_t *v = song.voice, *f = song.fx;
    if (ui.words)
        return k == 0 ? (knob_t){"CURSOR", 0, (int)strlen(ui.wtext), ui.wcur} : r;
    if (ui.page == P_SONG && ui.entry)
        return k == 3 ? (knob_t){"LENGTH", 1, 16, ui.entry_len} : r;
    switch (ui.page) {
    case P_SING: {
        static const char *const N[4] = {"BODY", "VIBRATO", "FILTER", "PUMP"};
        static const uint8_t SRC[4] = {V_BODY, V_VIBRATO, FX_FILTER, FX_PUMP};
        r.name = N[k];
        r.hi = 100;
        r.value = k < 2 ? v[SRC[k]] : f[SRC[k]];
        break;
    }
    case P_SONG:
        if (k == 0 && ui.row < song.rows) {
            r = (knob_t){"BAR", 1, song.bars, song.row[ui.row].at / song.steps + 1};
        } else if (k == 1 && ui.row < song.rows) {
            r = (knob_t){"BEAT", 0, song.steps - 1, song.row[ui.row].at % song.steps};
        } else if (k == 2 && ui.row < song.rows) {
            r = (knob_t){"LINE", 1, SONG_LINES, song.row[ui.row].line + 1};
        } else if (k == 3) {
            r = (knob_t){"LENGTH", 1, 16, ui.entry_len};
        }
        break;
    case P_STEPS:
        if (ui.hold >= 0) {                               /* the held step's own */
            const step_t *st = step_at((unsigned)ui.hold);
            if (k == 0)
                r = (knob_t){"VELO", 1, 127, st->velocity};
            else if (k == 1 && ui.track != T_DRUMS)
                r = (knob_t){"LENGTH", 1, 16, st->length};
            else if (k == 2 && ui.track != T_DRUMS && st->count)
                r = (knob_t){"NOTE", 24, 96, st->note[0]};
            break;
        }
        if (k == 0)
            r = (knob_t){"BAR", 1, song.bars, ui.bar + 1};
        else if (k == 1)
            r = (knob_t){"STEP", 1, song.steps, ui.step + 1};
        else if (k == 2)
            r = (knob_t){"VELO", 1, 127, ui.velo};
        else
            r = (knob_t){"LENGTH", 1, 16, ui.length};
        break;
    case P_SOUND:
    case P_ENV: {
        unsigned env = ui.page == P_ENV;
        if (ui.track == T_VOICE) {
            unsigned src = VOICE_KNOB[env][k];
            if (src < NVOICE)
                r = (knob_t){VOICE_KNOB_NAME[env][k], 0, src == V_OCTAVE ? 2 : 100, v[src]};
            break;
        }
        int j = sound_param(k);
        if (j < 0)
            break;
        if (ui.track == T_DRUMS)
            r.name = DRUM_KNOB_NAME[env][k];
        else if (env) {
            static const char *const N[4] = {"ATTACK", "RELEASE", "CUTOFF", "RESO"};
            r.name = N[k];
        } else {
            unsigned n = song.track[ui.track].sound < (unsigned)NPRESETS ? song.track[ui.track].sound : 0;
            r.name = SOUND_KNOB_ENGINE[PRESETS[n].par[TP_ENGINE]][k];
        }
        r.hi = 127;
        r.value = sound_value(ui.track, (unsigned)j);
        break;
    }
    case P_EFFECTS: {
        static const char *const N[2][4] = {{"REVERB", "TONE", "DELAY", "REPEATS"}, {"DRIVE", "SQUEEZE", "PUMP", "FILTER"}};
        static const uint8_t SRC[2][4] = {{FX_REVERB_SIZE, FX_REVERB_TONE, FX_DELAY_TIME, FX_DELAY_FEEDBACK},
                                          {FX_DRIVE, FX_SQUEEZE, FX_PUMP, FX_FILTER}};
        r = (knob_t){N[sub][k], 0, 100, f[SRC[sub][k]]};
        break;
    }
    case P_MIXER: {
        static const char *const N[4] = {"VOICE", "KEYS", "BASS", "DRUMS"};
        const track_t *t = &song.track[k];
        r = (knob_t){N[k], 0, 100, sub == 0 ? t->level : sub == 1 ? t->reverb : t->delay};
        break;
    }
    case P_SETUP:
        if (sub == 0) {
            if (k == 0)
                r = (knob_t){"TEMPO", 40, 240, song.bpm};
            else if (k == 1)
                r = (knob_t){"BARS", 1, SONG_BARS, song.bars};
            else if (k == 2)
                r = (knob_t){"METER", 0, 1, song.steps == 12};
        }
        break;
    case P_CHANGE:
        if (k == 0 && ui.track != T_VOICE)
            r = (knob_t){"SHIFT", -15, 15, ui.shifted};
        else if (k == 1 && ui.track != T_DRUMS)
            r = (knob_t){"PITCH", -24, 24, ui.transposed};
        else if (k == 2 && ui.track != T_VOICE)
            r = (knob_t){"WEAVE", 1, SONG_PATTERNS, ui.weave + 1};
        else if (k == 3)
            r = (knob_t){"BAR", 1, song.bars, ui.bar + 1};
        break;
    default:
        break;
    }
    return r;
}

static void knob_text(unsigned k, const knob_t *r, char *out)
{
    out[0] = 0;
    if (r->name[0] == '-')
        return;
    if ((ui.page == P_SONG && k == 3) || (ui.page == P_STEPS && (ui.hold >= 0 ? k == 1 : k == 3))) {
        static const char *const L[17] = {"", "1/16", "1/8", "3/16", "1/4", "5/16", "3/8", "7/16", "1/2",
                                          "9/16", "5/8", "11/16", "3/4", "13/16", "7/8", "15/16", "1"};
        s_cat(out, L[r->value < 17 ? r->value : 16]);
    } else if (ui.page == P_STEPS && ui.hold >= 0 && k == 2) {
        s_note(out, (unsigned)r->value);
    } else if (ui.page == P_SONG && k == 1) {
        s_num(out, r->value / 4 + 1);
        s_cat(out, ".");
        s_num(out, r->value % 4 + 1);
    } else if (ui.page == P_SETUP && ui.sub[P_SETUP] == 0 && k == 2)
        s_cat(out, r->value ? "3/4" : "4/4");
    else if (ui.page == P_SOUND && ui.track == T_VOICE && k == 3) {
        static const char *const O[3] = {"-1", "0", "+1"};
        s_cat(out, O[r->value % 3]);
    } else if (ui.page == P_CHANGE && k == 2) {
        s_cat(out, "P");
        s_num(out, r->value);
    } else if (ui.page == P_CHANGE && k < 2 && r->value > 0) {
        s_cat(out, "+");
        s_num(out, r->value);
    } else if ((ui.page == P_EFFECTS && ui.sub[P_EFFECTS] == 1 && k == 3) || (ui.page == P_SING && k == 2)) {
        if (r->value >= 47 && r->value <= 53)
            s_cat(out, "off");
        else {
            s_cat(out, r->value < 50 ? "LP " : "HP ");
            s_num(out, r->value < 50 ? (50 - r->value) * 2 : (r->value - 50) * 2);
        }
    } else
        s_num(out, r->value);
}

static void knob_set(unsigned k, int v)
{
    knob_t r = knob_info(k);
    if (r.name[0] == '-' || r.hi == r.lo)
        return;
    if (v < r.lo)
        v = r.lo;
    if (v > r.hi)
        v = r.hi;
    if (v == r.value)
        return;
    unsigned sub = ui.sub[ui.page];
    switch (ui.page) {
    case P_SING:
        if (k == 0)
            song.voice[V_BODY] = (uint8_t)v;
        else if (k == 1)
            song.voice[V_VIBRATO] = (uint8_t)v;
        else
            song.fx[k == 2 ? FX_FILTER : FX_PUMP] = (uint8_t)v;
        break;
    case P_SONG:
        if (!stopped_or_say())
            return;
        if (k == 3) {
            ui.entry_len = (uint8_t)v;
            return;
        }
        undo_mark();
        if (k == 0 || k == 1) {
            unsigned at = k == 0 ? (unsigned)(v - 1) * song.steps + song.row[ui.row].at % song.steps
                                 : song.row[ui.row].at / song.steps * song.steps + (unsigned)v;
            if (row_set_at(&song, ui.row, at) != 0) {
                refuse("NEXT ROW IS THERE", 0);
                return;
            }
        } else {
            song.row[ui.row].line = (uint8_t)(v - 1);
        }
        ui.flash_row = (int16_t)ui.row;
        ui.flash_at = plat_ms();
        vocal_reset();
        break;
    case P_STEPS:
        if (ui.hold >= 0) {
            step_t *st = step_at((unsigned)ui.hold);
            undo_mark();
            if (k == 0)
                st->velocity = (uint8_t)v;
            else if (k == 1)
                st->length = (uint8_t)v;
            else {                                        /* the chord moves with its first note */
                int d = v - st->note[0];
                for (unsigned i = 0; i < st->count; i++)
                    st->note[i] = (uint8_t)(st->note[i] + d < 0 ? 0 : st->note[i] + d > 127 ? 127 : st->note[i] + d);
                if (ui.track == T_BASS || ui.track == T_KEYS)
                    engine_note(ui.track, st->note[0], 90), engine_off(ui.track, st->note[0]);
            }
            ui.hold_used = 1;
            step_flash((unsigned)ui.hold);
            return;
        }
        if (k == 0)
            ui.bar = (uint8_t)(v - 1);
        else if (k == 1)
            ui.step = (uint8_t)(v - 1);
        else if (k == 2)
            ui.velo = (uint8_t)v;
        else
            ui.length = (uint8_t)v;
        return;
    case P_SOUND:
    case P_ENV:
        if (ui.track == T_VOICE)
            song.voice[VOICE_KNOB[ui.page == P_ENV][k]] = (uint8_t)v;
        else {
            song.track[ui.track].param[sound_param(k)] = (uint8_t)v;
            engine_sound(ui.track);
        }
        break;
    case P_EFFECTS: {
        static const uint8_t SRC[2][4] = {{FX_REVERB_SIZE, FX_REVERB_TONE, FX_DELAY_TIME, FX_DELAY_FEEDBACK},
                                          {FX_DRIVE, FX_SQUEEZE, FX_PUMP, FX_FILTER}};
        song.fx[SRC[sub][k]] = (uint8_t)v;
        engine_effects();
        break;
    }
    case P_MIXER: {
        track_t *t = &song.track[k];
        if (sub == 0)
            t->level = (uint8_t)v;
        else if (sub == 1)
            t->reverb = (uint8_t)v;
        else
            t->delay = (uint8_t)v;
        engine_sound(k);
        break;
    }
    case P_SETUP:
        if (k == 0) {
            song.bpm = (uint16_t)v;
            engine_tempo();
            punch_tempo(song.bpm);
        } else if (k == 1) {
            if (!stopped_or_say())
                return;
            undo_mark();
            song.bars = (uint8_t)v;
            while (song.rows && song.row[song.rows - 1].at >= song_ticks(&song))
                song.rows--;
        } else if (k == 2) {
            if (!stopped_or_say())
                return;
            undo_mark();
            song.steps = v ? 12 : 16;
        }
        break;
    case P_CHANGE:
        if (k == 2) {
            ui.weave = (uint8_t)(v - 1);
            return;
        }
        if (k == 3) {
            ui.bar = (uint8_t)(v - 1);
            ui.shifted = ui.transposed = 0;
            return;
        }
        if (ui.track == T_VOICE && !stopped_or_say())
            return;
        undo_mark();
        if (k == 0) {
            steps_rotate(bar_pattern()->step[ui.track - 1], v - ui.shifted);
            ui.shifted = (int8_t)v;
        } else {
            if (notes_transpose(v - ui.transposed) != 0)
                return;
            ui.transposed = (int8_t)v;
        }
        break;
    default:
        return;
    }
    changed();
}

/* ------------------------------------------------------- the lists (SEL) --- */
/* SELECT moves, SEL does it; an item with a value (WEAVE P5, LOAD S2) has it changed by ALGORITHM */
enum { A_WORDS, A_NEW_LINE, A_COPY_LINE, A_MOVE, A_CLEAR_NOTES, A_DELETE_ROW,
       A_CLEAR_TRACK, A_CLEAR_PATTERN, A_RESET_SOUND, A_WEAVE, A_NEW_PATTERN, A_ALL_BARS,
       A_LOAD, A_SAVE_TO, A_NEW_SONG, A_RENAME, NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {
    "EDIT WORDS", "NEW LINE", "COPY AS NEW LINE", "MOVE", "CLEAR NOTES", "DELETE ROW",
    "CLEAR TRACK", "CLEAR PATTERN", "SOUND AS PRESET", "WEAVE IN", "NEW PATTERN FOR THIS BAR", "EVERY BAR PLAYS THIS",
    "LOAD", "SAVE TO", "NEW SONG", "RENAME SONG"};
static unsigned list_actions(uint8_t *a)
{
    unsigned n = 0;
    switch (ui.page) {
    case P_SONG:
        if (ui.row < song.rows)
            a[n++] = A_WORDS;
        a[n++] = A_NEW_LINE;
        if (ui.row < song.rows) {
            a[n++] = A_COPY_LINE;
            a[n++] = A_MOVE;
            a[n++] = A_CLEAR_NOTES;
            a[n++] = A_DELETE_ROW;
        }
        break;
    case P_STEPS:
        a[n++] = A_CLEAR_TRACK;
        a[n++] = A_CLEAR_PATTERN;
        break;
    case P_SOUND:
    case P_ENV:
        if (ui.track != T_VOICE)
            a[n++] = A_RESET_SOUND;
        break;
    case P_CHANGE:
        a[n++] = A_WEAVE;
        a[n++] = A_NEW_PATTERN;
        a[n++] = A_ALL_BARS;
        break;
    case P_SETUP:
        a[n++] = A_LOAD;
        a[n++] = A_SAVE_TO;
        a[n++] = A_NEW_SONG;
        a[n++] = A_RENAME;
        break;
    default:
        break;
    }
    return n;
}

static unsigned load_count(void) { return SONG_SLOTS; }

/* an item's value, "" for none */
static void list_value(unsigned a, char *out)
{
    out[0] = 0;
    if (a == A_WEAVE) {
        s_cat(out, "P");
        s_num(out, ui.weave + 1);
    }
}

/* ALGORITHM on an item with a value */
static void list_turn(unsigned a, int d)
{
    if (a == A_WEAVE)
        ui.weave = (uint8_t)((ui.weave + d + SONG_PATTERNS) % SONG_PATTERNS);
}

/* LOAD, SAVE TO and NEW SONG open the eight slots, each with its song's name: SELECT moves, SEL does it */
static void slot_list_open(unsigned action)
{
    slots_read();
    ui.list = 2;
    ui.slot_for = (uint8_t)action;
    unsigned at = ui.slot >= 0 ? (unsigned)ui.slot : 0;
    if (action == A_NEW_SONG || ui.slot < 0)
        for (unsigned i = 0; i < SONG_SLOTS; i++)
            if (!(ui.present >> i & 1u)) {
                at = i;
                break;
            }
    ui.list_pick = (uint8_t)at;
}

static void words_open(unsigned target, unsigned line)
{
    ui.words = 1;
    ui.wtarget = (uint8_t)target;
    ui.wline = (uint8_t)line;
    s_copy(ui.wtext, target ? song.name : song.line[line].text, target ? sizeof song.name : SONG_TEXT);
    ui.wcur = (uint8_t)strlen(ui.wtext);
}

static void save_to(unsigned s)
{
    if ((ui.present >> s & 1u) && ui.slot != (int)s) {
        char t[40] = "SAVE OVER S";
        s_num(t, (int)s + 1);
        s_cat(t, "?");
        ask(Q_SAVE_OVER, s, t);
    } else
        save_slot(s);
}

/* before another song comes: this one is kept, in its slot or the first empty one; 0 when it is
 * safe to go on, -1 when there is nowhere to keep it (all eight slots hold songs) */
static int keep_song(void)
{
    if (!ui.dirty)
        return 0;
    if (ui.slot >= 0)
        return save_slot((unsigned)ui.slot) == 0 ? 0 : -1;
    for (unsigned i = 0; i < SONG_SLOTS; i++)
        if (!(ui.present >> i & 1u))
            return save_slot(i) == 0 ? 0 : -1;
    return -1;
}

static void load_pick(unsigned pick)
{
    if (pick < SONG_SLOTS && !(ui.present >> pick & 1u)) {
        refuse("EMPTY", 0);
        return;
    }
    if (keep_song() != 0) {
        ask(Q_LOAD, pick, "SLOTS FULL: LOSE THIS SONG?");
        return;
    }
    ui.ask = Q_LOAD;
    ui.ask_arg = (uint8_t)pick;
    answer(1);
}

static void run_action(unsigned a)
{
    ui.list = 0;
    if (a <= A_DELETE_ROW || (a >= A_CLEAR_TRACK && a <= A_CLEAR_PATTERN) || (a >= A_NEW_PATTERN && a <= A_ALL_BARS) ||
        a == A_LOAD || a == A_NEW_SONG)
        if (!stopped_or_say())
            return;
    switch (a) {
    case A_WORDS:
        words_open(0, song.row[ui.row].line);
        break;
    case A_NEW_LINE: {
        int line = line_unused(&song);
        if (line < 0) {
            refuse("LINES FULL", "64 lines");
            return;
        }
        undo_mark();
        unsigned at = ui.row < song.rows ? row_end(&song, ui.row) : row_next_at(&song);
        if (ui.row < song.rows && ui.row + 1 < song.rows) {
            /* between two rows: halfway, on a bar */
            unsigned a0 = song.row[ui.row].at, a1 = song.row[ui.row + 1].at;
            at = (a0 + line_span(&song, song.row[ui.row].line) + song.steps - 1) / song.steps * song.steps;
            if (at >= a1)
                at = a0 + (a1 - a0) / 2;
        }
        if (at >= SONG_BARS * (unsigned)song.steps) {
            refuse("SONG IS FULL", "128 bars");
            return;
        }
        int r = row_insert(&song, at, (unsigned)line);
        if (r < 0) {
            refuse("NO ROOM HERE", 0);
            return;
        }
        ui.row = (uint8_t)r;
        changed();
        words_open(0, (unsigned)line);
        break;
    }
    case A_COPY_LINE: {
        undo_mark();
        int line = line_copy(&song, song.row[ui.row].line);
        if (line < 0) {
            refuse("FULL", "no free line");
            return;
        }
        song.row[ui.row].line = (uint8_t)line;
        ui.counted[line] = 0;
        changed();
        char t[24] = "NOW LINE ";
        s_num(t, line + 1);
        done(t, "its own copy");
        break;
    }
    case A_MOVE:                                  /* SELECT carries the line, SEL drops it, HOME puts it back */
        if (song.rows < 2) {
            refuse("ONE ROW", 0);
            return;
        }
        undo_mark();
        ui.moving = 1;
        ui.move_net = 0;
        break;
    case A_CLEAR_NOTES:
        ask(Q_CLEAR_NOTES, ui.row, "CLEAR THE NOTES?");
        break;
    case A_DELETE_ROW:
        ask(Q_DELETE_ROW, ui.row, "DELETE THIS ROW?");
        break;
    case A_NEW_PATTERN: {
        unsigned used[SONG_PATTERNS] = {0};
        for (unsigned b = 0; b < song.bars; b++)
            used[song.chain[b]]++;
        int free = -1;
        for (unsigned p = 0; p < SONG_PATTERNS; p++)
            if (!used[p]) {
                free = (int)p;
                break;
            }
        if (free < 0) {
            refuse("PATTERNS FULL", "16 in use");
            return;
        }
        undo_mark();
        song.pattern[free] = song.pattern[song.chain[ui.bar]];
        song.chain[ui.bar] = (uint8_t)free;
        changed();
        char t[24] = "BAR ";
        s_num(t, ui.bar + 1);
        s_cat(t, " PLAYS P");
        s_num(t, free + 1);
        done(t, 0);
        break;
    }
    case A_CLEAR_TRACK:
    {
        char t[24] = "CLEAR ";
        s_cat(t, TRACK_NAME[ui.track]);
        s_cat(t, "?");
        ask(Q_CLEAR_TRACK, ui.track, t);
    }
        break;
    case A_CLEAR_PATTERN:
    {
        char t[24] = "CLEAR P";
        s_num(t, song.chain[ui.bar] % SONG_PATTERNS + 1);
        s_cat(t, "?");
        ask(Q_CLEAR_PATTERN, 0, t);
    }
        break;
    case A_ALL_BARS:
        undo_mark();
        memset(song.chain, song.chain[ui.bar], sizeof song.chain);
        changed();
        done("EVERY BAR PLAYS IT", 0);
        break;
    case A_RESET_SOUND:
        memset(song.track[ui.track].param, PARAM_PRESET, NPARAM);
        engine_sound(ui.track);
        changed();
        done("SOUND AS PRESET", 0);
        break;
    case A_WEAVE:
        pattern_weave(ui.weave);
        break;
    case A_LOAD:
        load_pick(ui.pick);
        break;
    case A_SAVE_TO:
        save_to(ui.pick_save);
        break;
    case A_RENAME:
        words_open(1, 0);
        break;
    case A_NEW_SONG: {
        /* a new song in the slot picked: the song there is replaced (after a question), the one
         * playing kept first */
        unsigned slot = ui.pick_new % SONG_SLOTS;
        if ((ui.present >> slot & 1u) && ui.slot != (int)slot) {
            char t[24] = "REPLACE S";
            s_num(t, (int)slot + 1);
            s_cat(t, "?");
            ask(Q_NEW, slot, t);
            break;
        }
        ui.ask = Q_NEW;
        ui.ask_arg = (uint8_t)slot;
        answer(1);
        break;
    }
    }
}

static void answer(int yes)
{
    unsigned q = ui.ask, arg = ui.ask_arg;
    ui.ask = Q_NONE;
    if (!yes)
        return;
    switch (q) {
    case Q_LOAD:
        if (load_entry(arg) == 0) {
            int fresh = arg >= SONG_SLOTS;
            say(fresh ? "A NEW SONG" : "LOADED", fresh ? "SEL: its first line" : ui.slot >= 0 ? ui.names[ui.slot] : song.name);
            ui.page = fresh ? P_SONG : P_SING;      /* a new song starts where its first line is written */
        }
        break;
    case Q_SAVE_OVER:
        save_slot(arg);
        break;
    case Q_NEW:
        if (ui.slot != (int)arg)
            keep_song();
        load_entry(SONG_SLOTS);
        save_slot(arg);                           /* it lives in its slot from the start */
        done("A NEW SONG", "SEL: its first line");
        ui.page = P_SONG;
        break;
    case Q_DELETE_ROW:
        undo_mark();
        row_delete(&song, arg);
        if (ui.row > song.rows)
            ui.row = song.rows;
        vocal_reset();
        changed();
        done("DELETED", 0);
        break;
    case Q_CLEAR_NOTES:
        undo_mark();
        line_clear_notes(&song, song.row[arg].line);
        vocal_reset();
        changed();
        done("CLEARED", 0);
        break;
    case Q_CLEAR_TRACK:
    case Q_CLEAR_PATTERN: {
        undo_mark();
        pattern_t *p = &song.pattern[song.chain[ui.bar]];
        for (unsigned t = 0; t < NBACKING; t++)
            if (q == Q_CLEAR_PATTERN || t + 1 == arg)
                for (unsigned i = 0; i < SONG_STEPS; i++) {
                    p->step[t][i].count = 0;
                    p->step[t][i].drums = 0;
                }
        changed();
        done("CLEARED", 0);
        break;
    }
    }
}

/* ------------------------------------------------------------ the words --- */
static void words_key(unsigned k)
{
    char c = TEXT_KEYS[k];
    unsigned n = (unsigned)strlen(ui.wtext), cap = ui.wtarget ? 23u : SONG_TEXT - 1u;
    if (n >= cap)
        return;
    memmove(ui.wtext + ui.wcur + 1, ui.wtext + ui.wcur, n - ui.wcur + 1);
    ui.wtext[ui.wcur++] = c;
}
static void words_back(void)
{
    if (!ui.wcur)
        return;
    unsigned n = (unsigned)strlen(ui.wtext);
    memmove(ui.wtext + ui.wcur - 1, ui.wtext + ui.wcur, n - ui.wcur + 1);
    ui.wcur--;
}
static void words_close(int keep)
{
    ui.words = 0;
    if (!keep)
        return;
    undo_mark();
    if (ui.wtarget) {
        memset(song.name, 0, sizeof song.name);
        s_copy(song.name, ui.wtext[0] ? ui.wtext : "Song", sizeof song.name);
        changed();
        done("RENAMED", 0);
        return;
    }
    /* trim spaces at the ends */
    char t[SONG_TEXT];
    unsigned a = 0, n = (unsigned)strlen(ui.wtext);
    while (ui.wtext[a] == ' ')
        a++;
    while (n > a && ui.wtext[n - 1] == ' ')
        n--;
    for (unsigned i = a; i < n; i++)              /* a plain loop: a memcpy here came out empty on the device */
        t[i - a] = ui.wtext[i];
    t[n - a] = 0;
    line_set_text(&song, ui.wline, t);
    words_changed(ui.wline);
    changed();
    ui.save_soon = 1;                             /* the words are kept; the syllables are counted when notes are written */
    done(t[0] ? "KEPT" : "CLEARED", t[0] ? "REC: its notes" : 0);
}

/* ------------------------------------------------------- notes into a line --- */
static void entry_start(void)
{
    if (!stopped_or_say())
        return;
    if (ui.row >= song.rows) {
        refuse("NO ROW HERE", "SEL: new line");
        return;
    }
    unsigned line = song.row[ui.row].line, syl = line_syllables(line);
    if (!song.line[line].text[0]) {
        refuse("WORDS FIRST", "SEL: edit words");
        return;
    }
    if (!syl) {
        refuse("CAN'T SING THAT", "check the words");
        return;
    }
    const line_t *L = &song.line[line];
    ui.entry = 1;
    ui.entry_line = (uint8_t)line;
    ui.entry_syl = (uint8_t)line_pitched(line);
    ui.entry_at = L->count ? (uint16_t)line_span(&song, line) : 0;
    ui.entry_key = 255;
    if (ui.entry_syl >= syl) {
        /* every syllable has a note: start again from the top */
        undo_mark();
        line_clear_notes(&song, line);
        ui.entry_syl = 0;
        ui.entry_at = 0;
        vocal_reset();
        say("WRITING AGAIN", 0);
    } else {
        char m[28] = "";
        s_num(m, (int)syl);
        s_cat(m, syl == 1 ? " SYLLABLE" : " SYLLABLES");
        say(m, "a key each");
    }
}

static void entry_key_down(unsigned k)
{
    unsigned line = ui.entry_line, syl = line_syllables(line);
    unsigned note = key_note(k);
    if (note < 36)
        note = 36;
    if (note > 84)
        note = 84;
    int tie = ui.entry_key != 255 && ui.entry_held && ui.entry_syl > 0;   /* a second key while one is held: the same vowel */
    if (!tie && ui.entry_syl >= syl) {
        refuse("LINE DONE", "REC: finish");
        return;
    }
    undo_mark();
    note_t n = {ui.entry_at, ui.entry_len, (uint8_t)note, (uint8_t)(tie ? ui.entry_syl - 1 : ui.entry_syl), (uint8_t)tie};
    if (note_insert(&song, line, song.line[line].count, &n) != 0) {
        refuse("NOTES FULL", "512 in the song");
        return;
    }
    ui.entry_at = (uint16_t)(ui.entry_at + ui.entry_len);
    if (!tie)
        ui.entry_syl++;
    ui.entry_key = (uint8_t)k;
    ui.entry_held = 1;
    vocal_reset();
    changed();
    /* hear it: the syllable at this pitch if it is ready, else the hum */
    engine_sing(note, 100, clip_live_id(line, n.syllable));
    ui.sing_key = (uint8_t)note;
    if (ui.entry_syl >= syl && !tie)
        say("LAST SYLLABLE", 0);
}

static void entry_key_up(unsigned k)
{
    if (ui.entry_key == k) {
        ui.entry_held = 0;
        engine_sing_off(ui.sing_key);
    }
}

static void entry_rest(void)
{
    ui.entry_at = (uint16_t)(ui.entry_at + ui.entry_len);
    say("REST", 0);
}

static void entry_back(void)
{
    const line_t *L = &song.line[ui.entry_line];
    if (!L->count) {
        refuse("NOTHING TO TAKE BACK", 0);
        return;
    }
    undo_mark();
    note_t n = song.note[L->first + L->count - 1];
    note_delete(&song, ui.entry_line, L->count - 1u);
    ui.entry_at = n.at;
    if (!n.tie)
        ui.entry_syl = n.syllable;
    vocal_reset();
    changed();
    say("BACK", 0);
}

/* ---------------------------------------------------- steps into a pattern --- */
static step_t *cursor_step(unsigned track)
{
    return &song.pattern[song.chain[ui.bar] % SONG_PATTERNS].step[track - 1][ui.step % SONG_STEPS];
}

static void step_write_chord(void)
{
    if (!ui.nchord)
        return;
    undo_mark();
    step_t *s = cursor_step(ui.track);
    s->count = ui.track == T_BASS ? 1 : ui.nchord;
    for (unsigned i = 0; i < s->count; i++)
        s->note[i] = ui.chord[i];
    s->velocity = ui.velo;
    s->length = ui.length;
    ui.flash_step = (int16_t)ui.step;
    ui.flash_at = plat_ms();
    ui.nchord = 0;
    changed();
    /* the cursor moves on by the length */
    unsigned next = ui.step + ui.length;
    if (next >= song.steps) {
        next -= song.steps;
        if (ui.bar + 1 < song.bars)
            ui.bar++;
    }
    ui.step = (uint8_t)next;
}

/* Steps: a white key is a step. Pressed, an empty step gets the remembered notes (Keys,
 * Bass) or the drum shown (Drums); a step that has them loses them when the key comes up, unless
 * something was done while it was held: the knobs (its velocity, length, pitch), a later white key
 * (its note reaches that step), a black key (Drums: that drum on the step, or off) */
static step_t *step_at(unsigned step) { return &bar_pattern()->step[ui.track - 1][step % SONG_STEPS]; }
static void step_flash(unsigned step)
{
    ui.flash_step = (int16_t)step;
    ui.flash_at = plat_ms();
    changed();
}
static void step_press(unsigned k, unsigned step)
{
    step_t *s = step_at(step);
    if (ui.hold >= 0 && ui.track != T_DRUMS) {            /* a later step: the held note reaches it */
        if (step > (unsigned)ui.hold) {
            undo_mark();
            step_at((unsigned)ui.hold)->length = (uint8_t)(step - (unsigned)ui.hold + 1u);
            ui.hold_used = 1;
            step_flash((unsigned)ui.hold);
        }
        return;
    }
    if (ui.hold >= 0)
        return;
    undo_mark();
    ui.hold = (int8_t)step;
    ui.hold_key = (uint8_t)k;
    ui.hold_used = 0;
    if (ui.track == T_DRUMS) {
        ui.hold_fresh = !(s->drums >> ui.lane & 1u);
        if (ui.hold_fresh) {
            if (!s->drums)
                s->velocity = ui.velo;
            s->drums |= (uint16_t)(1u << ui.lane);
            s->count = 1;
            s->length = 1;
            engine_note(T_DRUMS, ui.lane, s->velocity);
        }
    } else {
        unsigned m = ui.track - T_KEYS;
        ui.hold_fresh = !s->count;
        if (ui.hold_fresh) {
            s->count = ui.nmem[m];
            memcpy(s->note, ui.mem[m], sizeof s->note);
            s->velocity = ui.velo;
            s->length = ui.length;
        }
    }
    step_flash(step);
}
static void step_release(unsigned k)
{
    if (ui.hold < 0 || k != ui.hold_key)
        return;
    step_t *s = step_at((unsigned)ui.hold);
    if (!ui.hold_used && !ui.hold_fresh) {
        if (ui.track == T_DRUMS) {
            s->drums &= (uint16_t)~(1u << ui.lane);
            s->count = s->drums != 0;
        } else
            s->count = 0;
        step_flash((unsigned)ui.hold);
    }
    ui.hold = -1;
}

/* SEQ held on Steps: the keys played are the notes a white key writes (a chord on Keys, one note on Bass) */
static void note_remember(unsigned k, unsigned note)
{
    unsigned m = ui.track - T_KEYS;
    if (ui.keys == 1u << k)
        ui.nmem[m] = 0;
    if (ui.nmem[m] < (ui.track == T_BASS ? 1u : 4u))
        ui.mem[m][ui.nmem[m]++] = (uint8_t)note;
}

/* recording over the playing song: a note lands on the nearest sixteenth of the bar playing */
static void rec_note(unsigned track, unsigned note)
{
    unsigned t = eng_tick, bar = t / song.steps, step = t % song.steps;
    pattern_t *p = &song.pattern[song.chain[bar] % SONG_PATTERNS];
    step_t *s = &p->step[track - 1][step];
    if (!ui.undo_valid || plat_ms() - ui.undo_at > 4000u)
        undo_mark();
    ui.undo_at = plat_ms();
    if (track == T_DRUMS) {
        s->drums |= (uint16_t)(1u << note);
        s->count = 1;
        s->velocity = ui.velo;
        s->length = 1;
    } else {
        for (unsigned i = 0; i < s->count; i++)
            if (s->note[i] == note)
                return;
        if (track == T_BASS || s->count >= 4)
            s->count = 0;
        s->note[s->count++] = (uint8_t)note;
        s->velocity = ui.velo;
        s->length = ui.length;
    }
    changed();
}

/* --------------------------------------------------------------- the keys --- */
static void voice_key_down(unsigned k)
{
    unsigned note = key_note(k);
    int line = ui.page == P_SING ? -1 : focus_line();   /* Sing: pitches, sung "ah"; Song: the line's words */
    uint16_t id = ui.page == P_SING ? CLIP_AH : CLIP_NONE;
    if (line >= 0) {
        unsigned syl = line_syllables((unsigned)line);
        if ((unsigned)line != ui.live_line || plat_ms() - ui.live_at > 3000u)
            ui.live_syl = 0;
        ui.live_line = (uint8_t)line;
        if (syl) {
            if (ui.live_syl >= syl)
                ui.live_syl = 0;
            id = clip_live_id(line, ui.live_syl);
            ui.live_syl++;
        }
        ui.live_at = plat_ms();
    }
    engine_sing(note, 100, id);
    ui.sing_key = (uint8_t)note;
}

/* a punch-in effect on or off; several can be on at once. The keys last down together are kept: their light
 * blinks after they are let go */
static void punch_key(unsigned w, int on)
{
    engine_punch(w, on);
    if (on) {
        ui.punch_held |= (uint16_t)(1u << w);
        ui.punch_last = ui.punch_held;           /* the keys down together at the latest press */
    } else
        ui.punch_held &= (uint16_t)~(1u << w);
}

static void key_down(unsigned k)
{
    int w = white_index(k), b = black_index(k);
    uint32_t held = ui.buttons;
    ui.last_key = (uint8_t)k;
    if (ui.moving)
        return;
    if (ui.words) {
        words_key(k);
        return;
    }
    if (held & (1u << B_HOME)) {
        ui.used = 1;
        if (w >= 0) {
            punch_key((unsigned)w, 1);
            return;
        }
        if (b >= 0 && b < NTRACKS) {
            song.track[b].mute ^= 1;
            changed();
            say(TRACK_NAME[b], song.track[b].mute ? "muted" : "plays");
            return;
        }
        return;
    }
    if (held & (1u << B_SAVE)) {
        ui.used = 1;
        if (w >= 0 && w < SONG_SLOTS) {
            if ((ui.present >> w & 1u) && ui.slot != w) {
                char t[40] = "SAVE OVER S";
                s_num(t, w + 1);
                s_cat(t, "?");
                ask(Q_SAVE_OVER, (unsigned)w, t);
            } else
                save_slot((unsigned)w);
        }
        return;
    }
    if (ui.page == P_EFFECTS && w >= 0) {
        punch_key((unsigned)w, 1);
        return;
    }
    if (ui.page == P_MIXER && b >= 0 && b < NTRACKS) {
        song.track[b].mute ^= 1;
        changed();
        say(TRACK_NAME[b], song.track[b].mute ? "muted" : "plays");
        return;
    }
    if (ui.page == P_SONG && ui.entry) {
        entry_key_down(k);
        return;
    }
    if (ui.page == P_STEPS && ui.track != T_VOICE && !ui.srec && !(held & (1u << B_SEQ))) {
        if (w >= 0 && (unsigned)w < song.steps) {
            step_press(k, (unsigned)w);
            return;
        }
        if (ui.track == T_DRUMS && b >= 0) {
            if (ui.hold >= 0) {                           /* a held step: that drum on it, or off */
                step_t *s = step_at((unsigned)ui.hold);
                undo_mark();
                s->drums ^= (uint16_t)(1u << b);
                s->count = s->drums != 0;
                ui.hold_used = 1;
                step_flash((unsigned)ui.hold);
            } else
                ui.lane = (uint8_t)b;
            engine_note(T_DRUMS, (unsigned)b, ui.velo);
            return;
        }
    }
    /* play the track */
    unsigned note = key_note(k);
    switch (ui.track) {
    case T_VOICE:
        voice_key_down(k);
        break;
    case T_DRUMS: {
        int lane = b >= 0 ? b : w >= 0 && w < 11 ? w : -1;
        if (lane < 0)
            return;
        engine_note(T_DRUMS, (unsigned)lane, ui.velo);
        if (ui.rec && eng_playing)
            rec_note(T_DRUMS, (unsigned)lane);
        break;
    }
    default:
        engine_note(ui.track, note, ui.velo);
        if (ui.page == P_STEPS && (held & (1u << B_SEQ)))
            note_remember(k, note);
        if (ui.page == P_STEPS && ui.srec && ui.nchord < 4) {
            ui.chord[ui.nchord++] = (uint8_t)note;
            ui.chord_keys |= 1u;
        }
        if (ui.rec && eng_playing)
            rec_note(ui.track, note);
        break;
    }
}

static void key_up(unsigned k)
{
    int w = white_index(k);
    if (ui.words)
        return;
    if (w >= 0 && (ui.punch_held >> w & 1u))
        punch_key((unsigned)w, 0);
    if (ui.page == P_SONG && ui.entry) {
        entry_key_up(k);
        return;
    }
    if (ui.page == P_STEPS && (int)k == ui.hold_key && ui.hold >= 0) {
        step_release(k);
        return;
    }
    unsigned note = key_note(k);
    if (ui.track == T_VOICE)
        engine_sing_off(note);
    else if (ui.track != T_DRUMS)
        engine_off(ui.track, note);
}

/* ------------------------------------------------------------ the buttons --- */
static void play_press(void)
{
    if (eng_playing) {
        engine_play(0, 0);
        ui.rec = 0;
        return;
    }
    unsigned from = 0;
    if (ui.page == P_SONG && ui.row < song.rows)
        from = song.row[ui.row].at / song.steps * song.steps;
    else if (ui.page == P_STEPS)
        from = (unsigned)ui.bar * song.steps;
    ui.entry = 0;
    ui.srec = 0;
    /* the first sung clips before the start, so the first words are there on time */
    vocal_cue((int)from);
    int made = vocal_prepare(1);
#ifdef X0X_HOST
    if (getenv("SIM_MISSES"))
        printf("  PLAY from %u: prepared %d\n", from, made);
#endif
    (void)made;
    vocal_cue(-1);
    engine_play(1, from);
    if (ui.armed) {
        ui.armed = 0;
        ui.rec = 1;
    }
}

static void rec_press(void)
{
    if (ui.page == P_SONG && !eng_playing) {
        if (ui.entry) {
            ui.entry = 0;
            say("NOTES WRITTEN", 0);
        } else
            entry_start();
        return;
    }
    if (ui.page == P_STEPS && !eng_playing && (ui.track == T_KEYS || ui.track == T_BASS)) {
        ui.srec = !ui.srec;
        ui.nchord = 0;
        say(ui.srec ? "WRITING STEPS" : "STEPS WRITTEN", 0);
        return;
    }
    if (ui.track == T_VOICE) {
        refuse("VOICE NOTES", "HOME twice: Song");
        return;
    }
    if (eng_playing) {
        ui.rec = !ui.rec;
        say(ui.rec ? "RECORDING" : "RECORD OFF", 0);
    } else {
        ui.armed = !ui.armed;
        say(ui.armed ? "RECORD ARMED" : "RECORD OFF", ui.armed ? "PLAY starts it" : 0);
    }
}

static void undo(int redo)
{
    if (!stopped_or_say())
        return;
    if (redo ? !ui.redo_valid : !ui.undo_valid) {
        refuse(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO", 0);
        return;
    }
    /* one copy serves both ways: swap */
    for (unsigned i = 0; i < sizeof song; i += 4) {
        uint32_t a, b;
        memcpy(&a, (uint8_t *)&song + i, 4);
        memcpy(&b, (uint8_t *)&undo_song + i, 4);
        memcpy((uint8_t *)&song + i, &b, 4);
        memcpy((uint8_t *)&undo_song + i, &a, 4);
    }
    ui.undo_valid = (uint8_t)redo;
    ui.redo_valid = (uint8_t)!redo;
    ui.undo_at = 0;
    engine_song();
    vocal_reset();
    memset(ui.counted, 0, sizeof ui.counted);
    ui.offsets_line = -1;
    if (ui.row > song.rows)
        ui.row = song.rows;
    changed();
    say_kind(redo ? "REDO" : "UNDO", 0, M_UNDO, 1400);
}

static const char *card_text(unsigned b, const char **second)
{
    *second = 0;
    switch (b) {
    case B_HOME: *second = "Again: Song, the rows sung. Held: + REC undo, + PLAY redo, + SELECT tempo, + white key punch-in"; return "Sing: the robot and the words";
    case B_EDIT: *second = "PRESETS: the sound. The knobs: its own four"; return "Sound: the selected track's sound";
    case B_SEQ: *second = "White keys are the steps. Drums: black keys pick the drum. Keys, Bass: hold SEQ and play the notes"; return "Steps: a backing track's pattern";
    case B_ENV: *second = "Attack, release, filter; the Voice's glide and vibrato"; return "Envelope: the sound in time";
    case B_FX: *second = "The white keys punch in effects while held"; return "Effects: reverb, delay, drive, pump";
    case B_LFO: *second = "Black keys 1-4: mute. SELECT: levels, reverb, delay"; return "Mixer: the four tracks";
    case B_GLO: *second = "SEL: songs: load, save to, new, rename"; return "Setup: tempo, length, meter";
    case B_ARP: *second = "SHIFT, PITCH, and WEAVE: another pattern's steps added to this one"; return "Change: the bar's pattern";
    case B_SAVE: *second = "Held + white key 1-8: save to that slot"; return "Save the song";
    case B_REC: return "Write what you play";
    case B_PLAY: return "Play from here, or stop";
    case B_SEL: return "Everything else on this page";
    default: return 0;
    }
}

static void button_down(unsigned b)
{
    uint32_t held = ui.buttons;
    if (b == B_HOME)
        ui.home_toggle = 0;
    if (ui.words) {
        if (b == B_SEL)
            words_close(1);
        else if (b == B_HOME)
            words_close(0);
        else if (b == B_OCTDN)
            words_back();
        else if (b == B_OCTUP && ui.wcur < strlen(ui.wtext))
            ui.wcur++;
        return;
    }
    if (ui.moving) {
        if (b == B_SEL) {
            ui.moving = 0;
            if (ui.move_net)
                done("MOVED", 0);
        } else if (b == B_HOME) {
            while (ui.move_net) {                     /* back where it was */
                int r = row_move(&song, ui.row, ui.move_net > 0 ? -1 : 1);
                if (r < 0)
                    break;
                ui.row = (uint8_t)r;
                ui.move_net = (int8_t)(ui.move_net + (ui.move_net > 0 ? -1 : 1));
            }
            ui.moving = 0;
            vocal_reset();
        }
        return;
    }
    if (ui.ask) {
        if (b == B_SEL)
            answer(1);
        else if (b == B_HOME || b == B_PLAY)
            answer(0);
        return;
    }
    if ((held & (1u << B_HOME)) && b != B_HOME) {
        ui.used = 1;
        if (b == B_REC)
            undo(0);
        else if (b == B_PLAY)
            undo(1);
        return;
    }
    if (ui.list) {
        uint8_t a[NACTIONS];
        unsigned n = list_actions(a);
        if (b == B_SEL) {
            if (ui.list == 2) {                      /* a slot picked */
                ui.pick = ui.pick_save = ui.pick_new = ui.list_pick;
                run_action(ui.slot_for);
            } else if (ui.list_pick < n && (a[ui.list_pick] == A_LOAD || a[ui.list_pick] == A_SAVE_TO ||
                                           a[ui.list_pick] == A_NEW_SONG))
                slot_list_open(a[ui.list_pick]);
            else if (ui.list_pick < n)
                run_action(a[ui.list_pick]);
            else
                ui.list = 0;
        } else if (b == B_HOME && ui.list == 2) {
            ui.list = 1;                             /* back to the actions */
            for (unsigned i = 0; i < n; i++)
                if (a[i] == ui.slot_for)
                    ui.list_pick = (uint8_t)i;
        } else if (b == B_HOME)
            ui.list = 0;
        else if (b != B_SAVE)
            ui.list = 0;
        if (b == B_SEL || b == B_HOME)
            return;
    }
    switch (b) {
    case B_PLAY:
        play_press();
        break;
    case B_REC:
        rec_press();
        break;
    case B_OCTDN:
    case B_OCTUP:
        if (ui.page == P_SONG && ui.entry) {
            if (b == B_OCTUP)
                entry_rest();
            else
                entry_back();
        } else if (ui.page == P_STEPS && !ui.srec && ui.track != T_VOICE) {
            int bar = (int)ui.bar + (b == B_OCTUP ? 1 : -1);    /* the bar before or after */
            if (bar >= 0 && bar < song.bars)
                ui.bar = (uint8_t)bar;
        } else if (ui.page == P_STEPS && ui.srec) {
            if (b == B_OCTUP) {
                undo_mark();
                cursor_step(ui.track)->count = 0;
                changed();
                ui.step = (uint8_t)((ui.step + ui.length) % song.steps);
            } else
                ui.step = (uint8_t)(ui.step ? ui.step - 1 : 0);
        } else {
            int o = ui.octave + (b == B_OCTUP ? 1 : -1);
            if (o < -2 || o > 2)
                break;
            ui.octave = (int8_t)o;
            char t[16] = "OCTAVE ";
            if (o > 0)
                s_cat(t, "+");
            s_num(t, o);
            say(t, 0);
        }
        break;
    case B_SEL:
        {
            uint8_t a[NACTIONS];
            if (list_actions(a)) {
                ui.list = 1;
                ui.list_pick = 0;
            } else
                refuse("NOTHING MORE HERE", 0);
        }
        break;
    case B_SAVE:
        ui.pressed = (uint8_t)b;
        break;                                /* acts on release (held: a slot with a key) */
    default:
        for (unsigned p = 0; p < NPAGES; p++)
            if (PAGE_BUTTON[p] == b) {
                if (b == B_HOME && (ui.page == P_SING || ui.page == P_SONG)) {
                    ui.home_toggle = 1;          /* on its release, alone: the other of the two */
                    break;
                }
                ui.page = (uint8_t)p;
                ui.sub[p] = 0;
                ui.list = 0;
                if (p == P_CHANGE)
                    ui.shifted = ui.transposed = 0;
                if (ui.entry && p != P_SONG) {
                    ui.entry = 0;
                    say("NOTES WRITTEN", 0);
                }
                if (ui.srec && p != P_STEPS)
                    ui.srec = 0;
                if (p == P_SETUP)
                    slots_read();
                break;
            }
        break;
    }
}

static void button_up(unsigned b, uint32_t held_ms)
{
    if (b == B_HOME && ui.home_toggle && !ui.used && held_ms < 900u && !ui.card) {
        ui.page = ui.page == P_SING ? P_SONG : P_SING;
        ui.list = 0;
        if (ui.entry && ui.page != P_SONG) {
            ui.entry = 0;
            say("NOTES WRITTEN", 0);
        }
    }
    if (b == B_SAVE && !ui.used && held_ms < 900u && !ui.card) {
        if (ui.slot >= 0)
            save_slot((unsigned)ui.slot);
        else {
            /* no slot yet: Setup's SAVE TO, the slots listed */
            ui.page = P_SETUP;
            slot_list_open(A_SAVE_TO);
            say("PICK A SLOT", 0);
        }
    }
}

static void encoder(unsigned e, int d)
{
    if (!d)
        return;
    if (ui.words) {
        if (e == EN_SELECT || e == EN_K1) {
            int c = (int)ui.wcur + d, n = (int)strlen(ui.wtext);
            ui.wcur = (uint8_t)(c < 0 ? 0 : c > n ? n : c);
        }
        return;
    }
    if (ui.buttons & (1u << B_HOME)) {
        ui.used = 1;
        if (e == EN_SELECT) {
            int bpm = (int)song.bpm + d;
            song.bpm = (uint16_t)(bpm < 40 ? 40 : bpm > 240 ? 240 : bpm);
            engine_tempo();
            punch_tempo(song.bpm);
            changed();
            char t[16] = "TEMPO ";
            s_num(t, song.bpm);
            say(t, 0);
        }
        return;
    }
    if (ui.moving) {                              /* SELECT carries the row's line past its neighbours */
        if (e == EN_SELECT) {
            int r = row_move(&song, ui.row, d > 0 ? 1 : -1);
            if (r >= 0) {
                ui.row = (uint8_t)r;
                ui.move_net = (int8_t)(ui.move_net + (d > 0 ? 1 : -1));
                vocal_reset();
                changed();
            }
        }
        return;
    }
    if (ui.list) {
        uint8_t a[NACTIONS];
        int n = ui.list == 2 ? (int)SONG_SLOTS : (int)list_actions(a);
        if (e == EN_SELECT) {
            int p = (int)ui.list_pick + d;
            ui.list_pick = (uint8_t)(p < 0 ? 0 : p >= n ? n - 1 : p);
        } else if (e == EN_ALGO && ui.list == 1 && ui.list_pick < n)
            list_turn(a[ui.list_pick], d);
        return;
    }
    switch (e) {
    case EN_SELECT:
        if (ui.page == P_SONG) {
            int r = (int)ui.row + d;
            ui.row = (uint8_t)(r < 0 ? 0 : r > song.rows ? song.rows : r);
            if (ui.entry)
                ui.entry = 0;
        } else if (ui.page == P_STEPS) {
            int s = (int)ui.step + d;
            ui.step = (uint8_t)(s < 0 ? 0 : s >= song.steps ? song.steps - 1 : s);
        } else {
            int p = (int)ui.sub[ui.page] + d, n = (int)page_count();
            ui.sub[ui.page] = (uint8_t)(p < 0 ? 0 : p >= n ? n - 1 : p);
        }
        break;
    case EN_ALGO: {
        int t = (int)ui.track + d;
        t = t < 0 ? 0 : t >= NTRACKS ? NTRACKS - 1 : t;
        if (t != ui.track) {
            ui.track = (uint8_t)t;
            ui.srec = 0;
            ui.shifted = ui.transposed = 0;
            say(TRACK_NAME[t], 0);
        }
        break;
    }
    case EN_PRESET:
        if (ui.page == P_SOUND || ui.page == P_ENV || ui.page == P_SING) {
            if (ui.track == T_VOICE)
                break;
            track_t *k = &song.track[ui.track];
            if (ui.track == T_DRUMS) {
                int n = (int)engine_kits(), s = (int)k->sound + d;
                k->sound = (uint8_t)(s < 0 ? 0 : s >= n ? n - 1 : s);
            } else {                              /* the next of the track's own sounds */
                uint8_t list[64];
                int n = (int)sound_list(ui.track, list), at = (int)sound_place(ui.track, list, (unsigned)n) + d;
                k->sound = list[at < 0 ? 0 : at >= n ? n - 1 : at];
            }
            engine_sound(ui.track);
            changed();
            say(ui.track == T_DRUMS ? engine_kit_name(k->sound) : PRESETS[k->sound].name, TRACK_NAME[ui.track]);
            /* hear it, when nothing else is playing */
            if (!eng_playing) {
                if (ui.track == T_DRUMS)
                    engine_note(T_DRUMS, 0, 100);
                else {
                    engine_note(ui.track, ui.track == T_BASS ? 36 : 60, 90);
                    engine_off(ui.track, ui.track == T_BASS ? 36 : 60);
                }
            }
        } else if (ui.page == P_SONG && ui.row < song.rows && stopped_or_say()) {
            int l = (int)song.row[ui.row].line + d;
            undo_mark();
            song.row[ui.row].line = (uint8_t)(l < 0 ? 0 : l >= SONG_LINES ? SONG_LINES - 1 : l);
            ui.flash_row = (int16_t)ui.row;
            ui.flash_at = plat_ms();
            vocal_reset();
            changed();
        } else if ((ui.page == P_STEPS || ui.page == P_CHANGE) && stopped_or_say()) {
            int p = (int)song.chain[ui.bar] + d;
            undo_mark();
            song.chain[ui.bar] = (uint8_t)(p < 0 ? 0 : p >= SONG_PATTERNS ? SONG_PATTERNS - 1 : p);
            changed();
        }
        break;
    default: {
        unsigned k = e - EN_K1;
        knob_t r = knob_info(k);
        int step = r.hi - r.lo > 60 && (d > 2 || d < -2) ? d * 2 : d;
        knob_set(k, r.value + step);
        ui.flash_at = plat_ms();
        ui.flash_step = (int16_t)(100 + k);
        break;
    }
    }
}

/* ------------------------------------------------------------- the input --- */
static void input_poll(void)
{
    uint32_t now = plat_ms(), remote = now - remote_at < 2000u;
    /* the loop was away for a while (preparing a sung clip, a flash write): that time is not a button held */
    static uint32_t polled;
    if (polled && now - polled > 150u && ui.buttons)
        ui.press_at += now - polled - 20u;
    polled = now;
    uint32_t b = plat_buttons() | (remote ? remote_buttons | remote_tap_b : 0);
    uint32_t k = plat_keys() | (remote ? remote_keys | remote_tap_k : 0);
    remote_tap_b = remote_tap_k = 0;
    uint32_t bdown = b & ~ui.buttons, bup = ui.buttons & ~b, kdown = k & ~ui.keys, kup = ui.keys & ~k;
    int any = 0;
    for (unsigned i = 0; i < NB; i++)
        if (bdown >> i & 1u) {
            if (!ui.buttons) {
                ui.used = 0;
                ui.card = 0;
                ui.pressed = (uint8_t)i;
                ui.press_at = now;
            } else
                ui.used = 1;
            ui.buttons |= 1u << i;
            button_down(i);
            any = 1;
        }
    /* releases first: a key let go and another pressed within one look are one after the other, not a tie */
    for (unsigned i = 0; i < 27; i++)
        if (kup >> i & 1u) {
            ui.keys &= ~(1u << i);
            key_up(i);
            any = 1;
        }
    for (unsigned i = 0; i < 27; i++)
        if (kdown >> i & 1u) {
            if (ui.buttons)
                ui.used = 1;
            ui.keys |= 1u << i;
            key_down(i);
            any = 1;
        }
    /* step entry: a chord is written when its keys are all up */
    if (ui.srec && ui.nchord && !(ui.keys & 0x7FFFFFFu))
        step_write_chord();
    for (unsigned e = 0; e < NE; e++) {
        int d = plat_enc((int)e) + (int)remote_enc[e];
        remote_enc[e] = 0;
        if (d) {
            if (ui.buttons && e != EN_SELECT)
                ui.used = 1;
            if (ui.buttons && e == EN_SELECT && !(ui.buttons & (1u << B_HOME)))
                ui.used = 1;
            if (ui.card) {
                ui.card = 0;
                ui.used = 1;
            }
            encoder(e, d);
            any = 1;
        }
    }
    for (unsigned i = 0; i < NB; i++)
        if (bup >> i & 1u) {
            ui.buttons &= ~(1u << i);
            button_up(i, now - ui.press_at);
            if ((uint8_t)i == ui.pressed)
                ui.card = 0;
            if (i == B_HOME)
                for (unsigned w = 0; w < 16; w++)
                    if (ui.punch_held >> w & 1u && ui.page != P_EFFECTS)
                        punch_key(w, 0);
            any = 1;
        }
    /* a button held alone for a moment: its card */
    if (ui.buttons && !ui.used && !ui.card && now - ui.press_at > 900u && ui.buttons == (1u << ui.pressed)) {
        const char *second;
        if (card_text(ui.pressed, &second))
            ui.card = (uint8_t)(ui.pressed + 1);
    }
    if (any)
        ui.last_input = now;
}

void ui_input_only(void) { input_poll(); }

/* ------------------------------------------------------------- the service --- */
static void ui_midi(void)
{
    uint32_t pkt;
    for (unsigned n = 0; n < 32 && plat_midi_in(&pkt); n++) {
        unsigned cin = pkt & 15u;
        if (cin >= 4 && cin <= 7) {
            companion_packet(pkt, cin == 4 || cin == 7 ? 3 : cin == 6 ? 2 : 1);
            continue;
        }
        unsigned st = (pkt >> 8) & 255u, a = (pkt >> 16) & 127u, v = (pkt >> 24) & 127u;
        unsigned ch = st & 15u, t = ch == 9 ? T_DRUMS : ch < 3 ? ch + 1u : T_KEYS;
        if ((st & 0xF0u) == 0x90u && v) {
            if (t == T_DRUMS) {
                static const uint8_t GM[11] = {36, 38, 41, 45, 48, 37, 39, 56, 49, 46, 42};
                for (unsigned l = 0; l < 11; l++)
                    if (GM[l] == a)
                        engine_note(T_DRUMS, l, v);
            } else
                engine_note(t, a, v);
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            engine_off(t, a);
    }
}

void ui_service(void)
{
    uint32_t now = plat_ms();
    /* a flash write stops the second core's code (it runs from flash): only with the speech engine idle */
    if (ui.save_soon && !ui.words && !eng_playing && !vocal_preparing && !dx_speech_active()) {
        ui.save_soon = 0;
        save_now();
    }
    if (ui.restart_at && (int32_t)(now - ui.restart_at) >= 0)
        plat_restart();
    ui_midi();
    engine_guard(plat_cpu_pct(), now);
    if (!eng_playing && ui.save_when_stopped) {
        unsigned s = ui.save_when_stopped - 1u;
        ui.save_when_stopped = 0;
        save_slot(s);
    }
    /* autosave: a song with a slot, stopped, nothing touched for four seconds */
    if (ui.dirty && ui.slot >= 0 && !eng_playing && !ui.words && !ui.entry && now - ui.dirty_at > 4000u &&
        now - ui.last_input > 4000u) {
        if (song_valid(&song) && plat_store_save(OBJ_BANK0 + (unsigned)ui.slot, &song, sizeof song) == 0) {
            ui.dirty = 0;
            say("AUTOSAVED", 0);
        }
    }
    /* the clips: what is coming, and the line the Voice keys sing */
    int line = focus_line();
    vocal_stopped_focus(ui.page == P_SONG && ui.row < song.rows ? ui.row : -1, ui.page == P_SING ? -1 : line,
                        line == ui.live_line ? ui.live_syl : 0);
    vocal_ah(ui.track == T_VOICE && ui.page == P_SING);
    if (!ui.words && !ui.ask)
        vocal_prepare(0);
}

static void speech_background_service(void)
{
    ui_midi();
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.slot = -1;
    ui.entry_len = 4;
    ui.velo = 100;
    ui.length = 2;
    ui.entry_key = 255;
    ui.offsets_line = -1;
    ui.flash_row = ui.flash_step = -1;
    ui.track = T_VOICE;
    ui.hold = -1;
    ui.last_key = 255;
    static const uint8_t KEYS_CHORD[3] = {60, 64, 67};
    memcpy(ui.mem[0], KEYS_CHORD, 3);
    ui.nmem[0] = 3;
    ui.mem[1][0] = 41;
    ui.nmem[1] = 1;
    ui.weave = 1;
    slots_read();
}

/* at power-on, before the audio: the song last saved, else a new one. The first time 386DX starts
 * (the settings hold no record of ours: another firmware ran here), the eight slots are cleared, so
 * nothing another firmware left is taken for a song, and Greensleeves is saved in S1. After a boot
 * that failed (failed > 0) no slot is loaded: a song that stops the FM-1 can't do it again */
int ui_boot_song(unsigned failed)
{
    uint32_t boot[2] = {0};
    int ours = plat_store_load(OBJ_SET, boot, sizeof boot) == (int)sizeof boot && boot[1] == SONG_MAGIC &&
               (boot[0] & 0xffffff00u) == 0x44584200u;
    song_blank(&song);
    if (!ours) {
        for (unsigned i = 0; i < SONG_SLOTS; i++)
            plat_store_forget(OBJ_BANK0 + i);
        int demo = factory_load(0, &song) == 0 && song_valid(&song) && plat_store_save(OBJ_BANK0, &song, sizeof song) == 0;
        boot[0] = 0x44584200u | (demo ? 0u : 255u);  /* ours: S1, or no slot yet */
        boot[1] = SONG_MAGIC;
        plat_store_save(OBJ_SET, boot, sizeof boot);
        if (demo)
            return 0;
        song_blank(&song);
        return -1;
    }
    unsigned s = boot[0] & 255u;
    if (!failed && s < SONG_SLOTS && plat_store_load(OBJ_BANK0 + s, &song, sizeof song) == (int)sizeof song && song_valid(&song))
        return (int)s;
    song_blank(&song);
    return -1;
}

void ui_boot_slot(int slot)
{
    song_loaded();
    if (slot < 0)                                 /* never a song without a slot: the first empty one */
        for (unsigned i = 0; i < SONG_SLOTS; i++)
            if (!(ui.present >> i & 1u)) {
                slot = (int)i;
                break;
            }
    ui.slot = (int8_t)slot;
}

#include "ui_pages.h"
#include "companion.h"
