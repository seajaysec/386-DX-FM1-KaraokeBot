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

enum { P_SING, P_ARRANGE, P_PHRASE, P_SOUND, P_ENV, P_EFFECTS, P_MIXER, P_SETUP, P_ARP, NPAGES };
#define MODE_MAGIC 0x45444F4Du             /* "MODE" */
static const char *const PAGE_NAME[NPAGES] = {"Sing", "Song", "Phrase", "Sound", "Envelope", "Karaoke", "Mixer",
                                              "Setup", "Arp"};
static const uint8_t PAGE_BUTTON[NPAGES] = {B_HOME, B_SEQ, B_SEQ, B_EDIT, B_ENV, B_FX, B_LFO, B_GLO, B_ARP};
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
enum { Q_NONE, Q_LOAD, Q_SAVE_OVER, Q_NEW, Q_DELETE_PHRASE, Q_CLEAR_NOTES, Q_RESET_KNOBS, Q_CLEAR_SLOT, Q_MODE };
enum { M_INFO, M_DONE, M_NO, M_UNDO, M_GATE /* a box saying how to go on, till a key */ };
static struct {
    uint8_t page, sub[NPAGES], track;
    int8_t oct[NTRACKS];              /* OCT- / OCT+ on Keys, Bass, Drums: the whole track (the Voice: its QUALITY) */
    uint32_t buttons, keys, press_at, held_since;
    uint8_t used, card;               /* a button used with another (no release action); the help card shown */
    uint8_t pressed;                  /* the button held alone, for its card */
    /* lines */
    uint8_t row;                      /* the cursor: a row, or song.rows (the END row) */
    uint8_t syllables[SONG_LINES];    /* each line's syllable count (0: not counted yet / no words) */
    uint8_t counted[SONG_LINES];
    uint64_t touched;                 /* the lines written to or re-worded since the song came: checked on leaving and saving */
    uint8_t poetry;                   /* the mode (GLO): 0 karaoke, 1 poetry (a band and a poet made up as it goes) */
    uint8_t after_stop;
    uint8_t tune[24], ntune, tune_chord;  /* SLAMJAM's room: the notes played on the Voice, for the next line to sing */
    int8_t tune_for;                  /* the row they are gathered for */
    uint32_t tune_at;
    uint8_t draw_ms, gap_ms, animated;                  /* the slowest screen drawn, and the longest wait between two moving ones, since a test driver last asked (ms, companion 53) */               /* an action (+1) waiting for the song to stop: loading, a new poem, the mode */
    uint8_t poem_topic;               /* poetry: the topic of the next poem */
    int8_t karaoke_slot;              /* poetry: the song you had, to come back to (-1 none) */
    uint8_t offsets[SONG_SYLLABLES];  /* the focused line's syllables' character offsets */
    int8_t offsets_line;
    /* words */
    uint8_t words, wtarget;           /* editing; 0 a line, 1 the song's name */
    uint8_t wline, wcur;
    char wtext[SONG_TEXT];
    /* the Voice keys */
    uint8_t live_syl, live_line, sing_key;
    uint32_t live_at;
    /* steps */
    uint8_t bar, step, lane, velo;   /* Phrase: the cursor (ui.bar of the chain, the step); Drums' lane; velocity */
    int8_t hold;                      /* Drums: the step a white key holds, -1 none */
    uint8_t hold_key, hold_used, hold_fresh;   /* its key; something else done while held; written by this press */
    uint8_t last_key;                 /* the key pressed last (its light blinks), 255 none */
    /* the phrases: the cursor's phrase of the track shown (Song), the bar of it edited (Phrase: ui.bar is
     * that bar of the track's chain), the Voice's pitch for a step written */
    uint8_t ph, pbar, vpitch;
    /* recording over the playing song */
    uint8_t rec, armed;
    /* songs: the Setup list's LOAD and SAVE TO */
    uint8_t pick, pick_save, pick_new;
    uint16_t punch_held, punch_last;  /* the punch-in keys down, and the ones last down together (their light blinks) */
    /* the stage (Mister Wiggles' pages): when a knob was turned last; the note played last and when; in the
     * karaoke room, the knob turned last (bank x 4 + knob), which way, and a count of turns */
    uint8_t turn_k, turn_n;
    int8_t turn_d;
    int16_t played;
    uint32_t played_at, knob_at;
    uint8_t slot_for;                 /* list 2: the slot list for LOAD, SAVE TO or CLEAR SLOT (an A_*) */
    uint8_t songs;                    /* the list open is SAVE's songs list */
    uint8_t moving;                   /* Song: a phrase carried by SELECT */
    int8_t move_net;                  /* .. how far it has gone (HOME takes it back) */
    int8_t slot;                      /* the slot the song came from, -1 none */
    uint8_t dirty, present, old, save_when_stopped;   /* slots: holding a song; one of 1.0's (to be updated) */
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
static int words_unsung(int quiet);

static void changed(void)
{
    ui.dirty = 1;
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
/* GLO's scales: each one's seven steps up from the root, and whether its pentatonic is the major one */
#define NSCALES 8
static const char *const SCALE_NAME[NSCALES] = {"Chromatic", "Major", "Minor", "Dorian", "Mixolydian", "Lydian",
                                                "Phrygian", "Harmonic"};
static const uint8_t SCALE_STEP[NSCALES][7] = {{0}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10},
                                               {0, 2, 4, 5, 7, 9, 10}, {0, 2, 4, 6, 7, 9, 11}, {0, 1, 3, 5, 7, 8, 10},
                                               {0, 2, 3, 5, 7, 8, 11}};
static const uint8_t PENTA[2][5] = {{0, 3, 5, 7, 10}, {0, 2, 4, 7, 9}};    /* minor, major */

/* the note a key plays. Chromatic: the key's own. A scale: the white keys its notes up from the root (from F3 up),
 * the black keys its pentatonic. Keys' and Bass's octave moves the whole track in the engine, so not here */
static unsigned key_note(unsigned k)            /* (the Voice's register is QUALITY's, applied as it sings) */
{
    unsigned s = song.scale % NSCALES, n = 53u + k;
    if (s) {
        unsigned root = 48u + song.root % 12u;
        int w = white_index(k), b = black_index(k);
        if (root < 53u)
            root += 12u;
        n = w >= 0 ? root + 12u * (unsigned)(w / 7) + SCALE_STEP[s][w % 7]
                   : root + 12u * (unsigned)(b / 5) + PENTA[s == 1 || s == 4 || s == 5][b % 5];
    }
    return n - (ui.track == T_BASS ? 24u : 0u);
}

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

/* poetry, playing: the row the Voice's keys say, the one being said, or once its words are done the next */
static int poet_row(void)
{
    unsigned t = lane_tick(&song, T_VOICE, eng_tick);
    int r = row_at(&song, t);
    if (r >= 0 && t < song.row[r].at + line_span(&song, song.row[r].line))
        return r;
    for (unsigned i = 0; i < song.rows; i++)
        if (song.row[i].at > t)
            return (int)i;
    return song.rows ? 0 : -1;
}

static int focus_line(void)
{
    if (ui.poetry && eng_playing && ui.page == P_SING) {
        int r = poet_row();
        return r < 0 ? -1 : song.row[r].line;
    }
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
        int n = vocal_syllables(song.line[line].text, ui.offsets);
        ui.syllables[line] = (uint8_t)(n > 0 ? n : 0);
        ui.counted[line] = 1;
        ui.offsets_line = (int8_t)line;
    }
    return ui.syllables[line];
}
/* where in its words syllable s's word starts, 255 if that can't be said now (the speech engine is busy) */
static unsigned line_word(unsigned line, unsigned s)
{
    if (eng_playing || vocal_preparing || line >= SONG_LINES)
        return 255;
    if (ui.offsets_line != (int8_t)line)
        ui.counted[line] = 0;                     /* (counting again gives the offsets) */
    return line_syllables(line) > s ? ui.offsets[s] : 255;
}
static void words_changed(unsigned line)
{
    ui.counted[line] = 0;
    if (ui.offsets_line == (int8_t)line)
        ui.offsets_line = -1;
    vocal_reset();
}

/* a line's notes take its words' syllables in order. A note after the last syllable holds the vowel before it
 * (a tie); with syllables left over, the held notes at the end are given back to them */
__attribute__((noinline)) static void line_renumber(unsigned line)
{
    line_t *L = &song.line[line];
    note_t *n = &song.note[L->first];
    unsigned syl = line_syllables(line), cap = syl ? syl : SONG_SYLLABLES, heads = L->count ? 1u : 0u, s = 0;
    for (unsigned i = 1; i < L->count; i++)
        heads += !note_tie(&n[i]);
    for (unsigned i = L->count; i-- > 1 && syl && heads < syl;)
        if (note_tie(&n[i]))
            n[i].tie &= (uint8_t)~1u, heads++;
    for (unsigned i = 0; i < L->count; i++) {
        if (i && (note_tie(&n[i]) || s >= cap)) {
            n[i].tie |= 1u;
            n[i].syllable = n[i - 1].syllable;
        } else {
            n[i].tie &= (uint8_t)~1u;
            n[i].syllable = (uint8_t)s++;
        }
    }
    ui.touched |= 1ull << line;
    vocal_reset();
}

/* the syllables of a line no note sings */
static unsigned line_unsung(unsigned line)
{
    const line_t *L = &song.line[line];
    unsigned syl = line_syllables(line), left = syl;
    uint64_t seen = 0;
    for (unsigned i = 0; i < L->count; i++)
        seen |= 1ull << song.note[L->first + i].syllable;
    for (unsigned s = 0; s < syl; s++)
        left -= (unsigned)(seen >> s & 1u);
    return left;
}

/* BUFF OUT keeps a phrase: its chain bars and what they play (Keys, Bass, Drums: the materials themselves;
 * the Voice: its rows, by line). BUFF IN puts a copy of its own after the cursor: new materials, new lines. Another song loaded: the
 * Voice's is gone (its lines were that song's) */
static struct {
    uint8_t track, bars, nrows;                   /* bars 0: empty */
    uint8_t lane[SONG_BARS];
    struct { int16_t at; uint8_t line; } row[SONG_ROWS];
    step_t mat[SONG_PATTERNS][SONG_STEPS];
} buff X0X_POOL;

/* ---------------------------------------------------------- songs on flash --- */
static void slots_read(void)
{
    struct { uint32_t magic; uint16_t version, bpm; char name[24]; } h;
    ui.present = ui.old = 0;
    for (unsigned i = 0; i < SONG_SLOTS; i++) {
        int n = plat_store_load(OBJ_BANK0 + i, &h, sizeof h);
        if (n == (int)sizeof h && h.magic == SONG_MAGIC && (h.version == SONG_VERSION || h.version == SONG_VERSION_OLD)) {
            ui.present |= (uint8_t)(1u << i);             /* 1.0's songs too: listed, kept, not loaded */
            ui.old |= (uint8_t)((h.version != SONG_VERSION) << i);
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
    ui.touched = 0;
    ui.offsets_line = -1;
    ui.row = 0;
    ui.bar = ui.step = ui.ph = ui.pbar = 0;
    ui.dirty = 0;
    ui.undo_valid = ui.redo_valid = 0;
    ui.rec = ui.armed = 0;
    if (buff.track == T_VOICE)
        buff.bars = 0;
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
    if (ui.slot < 0 || eng_playing || !song_valid(&song) || words_unsung(1))
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
    if (words_unsung(0))
        return -1;
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
    if (pick < SONG_SLOTS && (ui.old >> pick & 1u)) {
        refuse("A 1.0 SONG", "update it on the companion page");
        return -1;
    }
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

/* a list's place turned past its end comes round to the start, and back (the tracks, the sounds, the lists) */
static int wrap(int v, int n) { return n > 0 ? ((v % n) + n) % n : 0; }

/* --------------------------------------------------------- the four knobs --- */
typedef struct { const char *name; int lo, hi, value; } knob_t;
static knob_t knob_info(unsigned k);
__attribute__((noinline)) static void knob_set(unsigned k, int v);

static unsigned page_count(void)
{
    switch (ui.page) {
    case P_EFFECTS: return 4u + ui.poetry;        /* the room's banks (SELECT turns them); SLAMJAM's fifth: the band */
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
/* the Voice's registers, low to high (QUALITY's knob positions), and each one's stored value (song.h) */
static const char *const VOICE_RANGE[5] = {"Profondo", "Baritone", "Tenor", "Alto", "Soprano"};
static const uint8_t RANGE_CODE[5] = {3, 0, 1, 2, 4};
static unsigned range_pos(void)
{
    static const uint8_t POS[5] = {1, 2, 3, 0, 4};
    return POS[song.voice[V_OCTAVE] % 5u];
}
static const char *const VOICE_KNOB_NAME[2][4] = {{"BODY", "BREATH", "EDGE", "QUALITY"}, {"GLIDE", "VIBRATO", "RATE", "-"}};
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

/* ------------------------------------------------------------ the phrases --- */
static step_t *step_at(unsigned step);
static void step_flash(unsigned step);

/* the cursor kept inside the shown track's chain: its phrase, the bar of it, that bar of the chain; on the
 * Voice, the row there (the one whose words the keys sing, REC writes and EDIT WORDS opens) */
static void ph_clamp(void)
{
    unsigned t = ui.track, n = lane_count(&song, t);
    if (ui.ph >= n)
        ui.ph = (uint8_t)(n - 1u);
    unsigned b = lane_start(&song, t, ui.ph), len = lane_len(&song, t, ui.ph);
    if (ui.pbar >= len)
        ui.pbar = (uint8_t)(len - 1u);
    ui.bar = (uint8_t)(b + ui.pbar);
    if (t == T_VOICE) {
        unsigned r = song.rows;
        for (unsigned i = song.rows; i-- > 0;) {
            unsigned rb = row_bar(&song, i);
            if (rb >= b && rb < b + len && (rb <= ui.bar || r == song.rows || row_bar(&song, r) > ui.bar))
                r = i;                            /* the last row starting by the bar, else the phrase's first */
        }
        ui.row = (uint8_t)r;
    }
}

/* bar `bar` of track t's chain, its steps (Keys, Bass, Drums). A bar of rest has none: written, it gets a
 * material of its own */
static step_t *lane_bar_steps(unsigned t, unsigned bar, int write)
{
    static step_t none[SONG_STEPS];
    uint8_t *b = &song.lane[t][bar];
    if ((*b & 31u) >= SONG_PATTERNS) {
        int m = write ? lane_slot_free(&song, t) : -1;
        if (m < 0) {
            if (write)
                refuse("BARS FULL", "16 a track");
            memset(none, 0, sizeof none);
            return none;
        }
        *b = (uint8_t)((*b & LANE_START) | (unsigned)m);
    }
    return song.pattern[*b & 31u].step[t - 1];
}
static step_t *bar_steps(int write) { return lane_bar_steps(ui.track, ui.bar, write); }

/* a pitch d notes of the song's scale away (Chromatic: semitones) */
static int scale_move(int p, int d)
{
    unsigned s = song.scale % NSCALES;
    while (d && p > 0 && p < 127) {
        p += d > 0 ? 1 : -1;
        int in = !s;
        for (unsigned j = 0; j < 7; j++)
            in |= SCALE_STEP[s][j] == (unsigned)(p + 120 - song.root % 12u) % 12u;
        if (in)
            d += d > 0 ? -1 : 1;
    }
    return p;
}

/* Drums: the bar's steps where the shown drum hits (a bit a step), the Phrase page's lights */
static uint32_t drum_marks(void)
{
    uint32_t m = 0;
    const step_t *s = bar_steps(0);
    for (unsigned i = 0; i < song.steps; i++)
        m |= (uint32_t)(s[i].drums >> ui.lane & 1u) << i;
    return m;
}

/* ---- Phrase's cursor (Voice, Keys, Bass): the keys write at it, SELECT moves it ---- */
static unsigned cur_tick(void) { return (unsigned)ui.bar * song.steps + ui.step; }

/* the note of pitch p (any, 0) sounding at chain tick t on the shown track: the tick it starts on, or -1; the
 * Voice's: its row and index in song.note */
static int note_over(unsigned t, unsigned p, unsigned *row, int *note)
{
    if (ui.track == T_VOICE) {
        unsigned b = lane_start(&song, T_VOICE, ui.ph), e = b + lane_len(&song, T_VOICE, ui.ph);
        for (unsigned r = 0; r < song.rows; r++) {
            const line_t *L = &song.line[song.row[r].line];
            if (row_bar(&song, r) < b || row_bar(&song, r) >= e)
                continue;
            for (unsigned i = 0; i < L->count; i++) {
                const note_t *n = &song.note[L->first + i];
                unsigned a = song.row[r].at + n->at;
                if (a <= t && t < a + n->length && (!p || n->pitch == p)) {
                    *row = r;
                    *note = (int)(L->first + i);
                    return (int)a;
                }
            }
        }
        return -1;
    }
    for (unsigned s = t + 1; s-- > 0 && s + 16 > t;) {
        const step_t *st = &lane_bar_steps(ui.track, s / song.steps, 0)[s % song.steps];
        for (unsigned k = 0; k < st->count; k++)
            if (s + st->length > t && (!p || st->note[k] == p))
                return (int)s;
    }
    return -1;
}

/* the notes sounding at the cursor (up to 8): their pitches, and whether each starts there */
static unsigned under_cursor(uint8_t *p, uint8_t *starts)
{
    unsigned t = cur_tick(), n = 0;
    if (ui.track == T_VOICE) {
        unsigned row;
        int i, at = note_over(t, 0, &row, &i);
        if (at >= 0)
            p[0] = song.note[i].pitch, starts[0] = at == (int)t, n = 1;
        return n;
    }
    for (unsigned s = t + 1; s-- > 0 && s + 16 > t;) {
        const step_t *st = &lane_bar_steps(ui.track, s / song.steps, 0)[s % song.steps];
        for (unsigned k = 0; k < st->count && n < 8 && s + st->length > t; k++)
            p[n] = st->note[k], starts[n++] = s == t;
    }
    return n;
}

/* the backing step a note starts on */
static step_t *step_of(unsigned at, int write) { return &lane_bar_steps(ui.track, at / song.steps, write)[at % song.steps]; }

/* a note on the Voice at chain tick t, a sixteenth: into the row singing there (the line's later syllables move up
 * one), or a new row with a line of its own. 0, or -1 refused */
__attribute__((noinline)) static int vnote_add(unsigned t)
{
    int r = row_at(&song, t);
    if (r < 0 || t >= row_end(&song, (unsigned)r) || row_bar(&song, (unsigned)r) < lane_start(&song, T_VOICE, ui.ph)) {
        int line = line_unused(&song);
        if (line < 0 || (r = row_insert(&song, t, (unsigned)line)) < 0) {
            refuse("LINES FULL", "64 lines, 96 rows");
            return -1;
        }
    }
    unsigned line = song.row[r].line, rel = t - song.row[r].at, i = 0;
    line_t *L = &song.line[line];
    while (i < L->count && song.note[L->first + i].at < rel)
        i++;
    if (i < L->count && song.note[L->first + i].at == rel) {   /* one note at a time: the one here takes this pitch */
        song.note[L->first + i].pitch = ui.vpitch;
        return 0;
    }
    if (i < L->count && note_tie(&song.note[L->first + i])) {
        refuse("A HELD VOWEL", 0);
        return -1;
    }
    note_t n = {(uint16_t)rel, 1, ui.vpitch, 0, 0};
    if (note_insert(&song, line, i, &n) != 0) {
        refuse("NOTES FULL", 0);
        return -1;
    }
    if (i) {                                      /* the note before ends where this one starts */
        note_t *p = &song.note[L->first + i - 1];
        if (p->at + p->length > rel)
            p->length = (uint8_t)(rel - p->at);
    }
    line_renumber(line);                          /* the later notes take the later syllables */
    return 0;
}

/* the note gone; the line's later notes take the syllables from it */
__attribute__((noinline)) static void vnote_delete(unsigned row, unsigned note)
{
    unsigned line = song.row[row].line;
    note_delete(&song, line, note - song.line[line].first);
    line_renumber(line);
}

/* a note's length (sixteenths): the Voice's as long as it is given, the notes it now covers written over (their
 * syllables go to the notes after), the backing's 1..16 */
static void note_len(unsigned at, unsigned p, unsigned len)
{
    unsigned r;
    int n;
    if (note_over(at, p, &r, &n) != (int)at)
        return;
    if (ui.track != T_VOICE) {
        step_of(at, 1)->length = (uint8_t)(len < 1 ? 1 : len > 16 ? 16 : len);
        return;
    }
    unsigned line = song.row[r].line, gone = 0;
    line_t *L = &song.line[line];
    note_t *x = &song.note[n];
    len = len < 1 ? 1 : len > 255 ? 255 : len;
    while ((unsigned)n + 1u < L->first + L->count && song.note[n + 1].at < x->at + len) {
        note_delete(&song, line, (unsigned)n + 1u - L->first);
        gone++;
    }
    x->length = (uint8_t)len;
    if (gone)
        line_renumber(line);
    vocal_reset();
}

/* the Voice's cursor: the syllable of the note it is on, else the one a note written there takes */
static unsigned cursor_syl(unsigned row)
{
    const line_t *L = &song.line[song.row[row].line];
    unsigned t = cur_tick(), s = 0;
    for (unsigned i = 0; i < L->count; i++) {
        const note_t *n = &song.note[L->first + i];
        unsigned a = song.row[row].at + n->at;
        if (a > t)
            break;
        s = n->syllable + (t >= a + n->length);
    }
    return s;
}

/* the notes under the cursor heard, each for its length */
static unsigned note_ms(unsigned len) { return len * 15000u / song.bpm; }
static void hear_at(unsigned at, unsigned p)
{
    unsigned r;
    int n;
    if (note_over(at, p, &r, &n) != (int)at)
        return;
    if (ui.track == T_VOICE)
        engine_preview(T_VOICE, song.note[n].pitch, 100, clip_live_id(song.row[r].line, song.note[n].syllable),
                       note_ms(song.note[n].length));
    else {
        const step_t *st = step_of(at, 0);
        for (unsigned k = 0; k < st->count; k++)
            engine_preview(ui.track, st->note[k], st->velocity, CLIP_NONE, note_ms(st->length));
    }
}

/* a track's sound heard, a note of its own (the Voice's "ah", Drums' kick), when the song is not playing: a sound or
 * a setting changed is something to hear */
static void hear_sound(unsigned t)
{
    if (eng_playing)
        return;
    if (t == T_VOICE)
        engine_preview(T_VOICE, 62, 100, CLIP_AH, 500);
    else
        engine_preview(t, t == T_BASS ? 36 : 60, 90, CLIP_NONE, 500);
}

/* the cursor moved by d steps through the phrase (it stops at its ends) */
static void cursor_move(int d)
{
    int len = (int)lane_len(&song, ui.track, ui.ph) * song.steps, at = (int)ui.pbar * song.steps + ui.step + d;
    at = at < 0 ? 0 : at >= len ? len - 1 : at;
    ui.pbar = (uint8_t)(at / song.steps);
    ui.step = (uint8_t)(at % song.steps);
    ph_clamp();
}

/* keys down at the cursor: a note each, grabbed while held. A key on a note already there grabs it. SELECT turned
 * while held stretches them to the cursor; let go untouched, a new note moves the cursor on a step and a note that
 * was there goes */
static struct { uint16_t at[4]; uint8_t pitch[4], fresh[4], n, moved; } grab;

/* the cursor to chain tick t of the phrase shown */
static void cursor_at(unsigned t)
{
    unsigned f = lane_start(&song, ui.track, ui.ph) * song.steps, len = lane_len(&song, ui.track, ui.ph) * song.steps;
    t = t < f ? 0 : t - f;
    t = t >= len ? len - 1 : t;
    ui.pbar = (uint8_t)(t / song.steps);
    ui.step = (uint8_t)(t % song.steps);
    ph_clamp();
}

/* 0, or -1 with the way on said (a box, till a key): no line written to may keep a syllable no note sings. The
 * cursor goes to where its next note belongs. Quiet: only the answer (an autosave waits) */
__attribute__((noinline)) static int words_unsung(int quiet)
{
    for (unsigned r = 0; r < song.rows; r++) {
        unsigned line = song.row[r].line, n = ui.touched >> line & 1u ? line_unsung(line) : 0;
        if (!n)
            continue;
        if (quiet)
            return -1;
        const line_t *L = &song.line[line];
        char t[32] = "";
        s_num(t, (int)n);
        s_cat(t, n > 1 ? " MORE NOTES NEEDED" : " MORE NOTE NEEDED");
        say_kind(t, 0, M_GATE, 8000);
        ui.page = P_PHRASE;
        ui.track = T_VOICE;
        ui.list = 0;
        ui.ph = (uint8_t)lane_find(&song, T_VOICE, row_bar(&song, r));
        grab.n = 0;
        cursor_at(song.row[r].at + (L->count ? song.note[L->first + L->count - 1].at + song.note[L->first + L->count - 1].length : 0u));
        return -1;
    }
    return 0;
}

/* leaving the Voice's phrase: not while a syllable has no note */
static int leaving_voice(void) { return ui.page == P_PHRASE && ui.track == T_VOICE && words_unsung(0); }
/* --------------------------------------------------------- the arpeggiator --- */
static const char *const ARP_TYPE[NARP] = {"off", "up", "down", "bounce", "random", "played"};
static const char *const ARP_RATE[NARP_RATE] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};

#include "poem.h"

static knob_t knob_info(unsigned k)
{
    knob_t r = {"-", 0, 0, 0};
    unsigned sub = ui.sub[ui.page];
    const uint8_t *v = song.voice, *f = song.fx;
    if (ui.words)
        return k == 0 ? (knob_t){"CURSOR", 0, (int)strlen(ui.wtext), ui.wcur} : r;
    switch (ui.page == P_EFFECTS && sub == 4 ? P_SING : ui.page) {   /* (the SLAMJAM room's band bank: the Stage's) */
    case P_SING: {
        if (ui.poetry) {                          /* SLAMJAM: each player's style */
            static const char *const P[4] = {"POET", "KEYS", "BASS", "DRUMS"};
            r = (knob_t){P[k], 0, JAM_STYLES[k] - 1, poem.style[k]};
            break;
        }
        static const char *const N[4] = {"BODY", "VIBRATO", "FILTER", "PUMP"};
        static const uint8_t SRC[4] = {V_BODY, V_VIBRATO, FX_FILTER, FX_PUMP};
        r.name = N[k];
        r.hi = 100;
        r.value = k < 2 ? v[SRC[k]] : f[SRC[k]];
        break;
    }
    case P_ARRANGE:
        if (k == 0)
            r = (knob_t){"PHRASE", 1, (int)lane_count(&song, ui.track), ui.ph + 1};
        else if (k == 3)
            r = (knob_t){"LENGTH", 1, 32, (int)lane_len(&song, ui.track, ui.ph)};
        break;
    case P_PHRASE: {
        if (ui.track == T_DRUMS) {                        /* a held step's, else the next hits' and the bar */
            const step_t *st = ui.hold >= 0 ? &bar_steps(0)[ui.hold] : 0;
            if (k == 0)
                r = (knob_t){"VELO", 1, 127, st ? st->velocity : ui.velo};
            else if (k == 1)
                r = (knob_t){"DRUM", 1, 11, ui.lane + 1};
            else if (k == 2)
                r = (knob_t){"BAR", 1, (int)lane_len(&song, ui.track, ui.ph), ui.pbar + 1};
            else if (st)
                r = (knob_t){"NUDGE", -3, 3, st->nudge};
            break;
        }
        unsigned row;                                     /* the note under the cursor */
        int n, at = note_over(cur_tick(), 0, &row, &n);
        if (k == 0 && (ui.track == T_VOICE || at < 0)) {  /* the Voice has no velocity: knob 1 goes bar to bar */
            r = (knob_t){"BAR", 1, (int)lane_len(&song, ui.track, ui.ph), ui.pbar + 1};
            break;
        }
        if (at < 0)
            break;
        const note_t *x = ui.track == T_VOICE ? &song.note[n] : 0;
        const step_t *st = x ? 0 : step_of((unsigned)at, 0);
        if (k == 0 && st)
            r = (knob_t){"VELO", 1, 127, st->velocity};
        else if (k == 1)
            r = (knob_t){"LENGTH", 1, x ? 64 : 16, x ? x->length : st->length};
        else if (k == 2)
            r = (knob_t){"NOTE", x ? 36 : 24, x ? 84 : 96, x ? x->pitch : st->note[0]};
        else if (k == 3)
            r = (knob_t){"NUDGE", -3, 3, x ? note_nudge(x) : st->nudge};
        break;
    }
    case P_SOUND:
    case P_ENV: {
        unsigned env = ui.page == P_ENV;
        if (ui.track == T_VOICE) {
            unsigned src = VOICE_KNOB[env][k];
            if (src < NVOICE)
                r = (knob_t){VOICE_KNOB_NAME[env][k], 0, src == V_OCTAVE ? 4 : 100, src == V_OCTAVE ? (int)range_pos() : v[src]};
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
        if (sub >= 2) {                           /* the stage's voice banks */
            unsigned src = VOICE_KNOB[sub - 2][k];
            r = src < NVOICE ? (knob_t){VOICE_KNOB_NAME[sub - 2][k], 0, src == V_OCTAVE ? 4 : 100, src == V_OCTAVE ? (int)range_pos() : v[src]}
                             : (knob_t){"THROAT", 0, 100, song.track[T_VOICE].reverb};
            break;
        }
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
    case P_SETUP:                                 /* the song's: its tempo, its key, its meter */
        if (k == 0)
            r = (knob_t){"TEMPO", 40, 240, song.bpm};
        else if (k == 1)
            r = (knob_t){"ROOT", 0, 11, song.root % 12u};
        else if (k == 2)
            r = (knob_t){"SCALE", 0, NSCALES - 1, song.scale % NSCALES};
        else
            r = (knob_t){"METER", 0, 1, song.steps == 12};
        break;
    case P_ARP:
        if (ui.track == T_KEYS || ui.track == T_BASS) {
            static const char *const N[4] = {"TYPE", "RATE", "RANGE", "GATE"};
            const uint8_t *a = song.track[ui.track].arp;
            static const int8_t HI[4] = {NARP - 1, NARP_RATE - 1, 4, 100};
            int val[4] = {a[0] & 15, a[1] % NARP_RATE, (a[0] >> 4 & 3) + 1, a[2] ? a[2] : 50};
            r = (knob_t){N[k], k == 2 || k == 3 ? (k == 2 ? 1 : 5) : 0, HI[k], val[k]};
        }
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
    if ((ui.page == P_SING || (ui.page == P_EFFECTS && ui.sub[P_EFFECTS] == 4)) && ui.poetry) {
        s_cat(out, JAM_NAME[k & 3][r->value % NJAM]);
    } else if (ui.page == P_PHRASE && ui.track == T_DRUMS && k == 1) {
        s_cat(out, DRUM_NAME[r->value - 1]);
    } else if (ui.page == P_PHRASE && ui.track != T_DRUMS && k == 1) {
        int n = r->value, d = 16;                 /* sixteenths, as the fraction of a bar (past a bar: as many) */
        while (n <= 16 && !(n & 1) && d > 1)
            n >>= 1, d >>= 1;
        s_num(out, n);
        if (d > 1 && r->value <= 16) {
            s_cat(out, "/");
            s_num(out, d);
        }
    } else if (ui.page == P_PHRASE && ui.track != T_DRUMS && k == 2) {
        s_note(out, (unsigned)r->value);
    } else if (ui.page == P_PHRASE && r->name[0] == 'N') {     /* NUDGE */
        if (r->value > 0)
            s_cat(out, "+");
        s_num(out, r->value);
        if (r->value)
            s_cat(out, "/6");
    } else if (ui.page == P_ARRANGE && k == 3) {
        s_num(out, r->value);
        s_cat(out, r->value == 1 ? " bar" : " bars");
    } else if (ui.page == P_SETUP && k)
        s_cat(out, k == 3 ? (r->value ? "3/4" : "4/4") : k == 1 ? NOTE_NAME[r->value % 12] : SCALE_NAME[r->value % NSCALES]);
    else if (r->name == VOICE_KNOB_NAME[0][3])    /* QUALITY, wherever it is: the register's name */
        s_cat(out, VOICE_RANGE[r->value % 5]);
    else if (ui.page == P_ARP && k < 2)
        s_cat(out, k ? ARP_RATE[r->value % NARP_RATE] : ARP_TYPE[r->value % NARP]);
    else if ((ui.page == P_EFFECTS && ui.sub[P_EFFECTS] == 1 && k == 3) || (ui.page == P_SING && k == 2)) {
        if (r->value >= 47 && r->value <= 53)
            s_cat(out, "off");
        else {
            s_cat(out, r->value < 50 ? "LP " : "HP ");
            s_num(out, r->value < 50 ? (50 - r->value) * 2 : (r->value - 50) * 2);
        }
    } else {
        s_num(out, r->value);
        if (ui.page == P_ARP && k == 2)
            s_cat(out, " oct");
        else if (ui.page == P_ARP)
            s_cat(out, "%");
    }
}

__attribute__((noinline)) static void knob_set(unsigned k, int v)
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
    switch (ui.page == P_EFFECTS && sub == 4 ? P_SING : ui.page) {
    case P_SING:
        if (ui.poetry) {
            uint16_t was = song.bpm;
            undo_mark();                          /* (HOME + REC brings back the part a style rewrote) */
            poem_style(&song, k, (unsigned)v);
            if (k && (unsigned)v == JAM_YOURS(k)) {   /* the part left to you: said plainly */
                char m[24] = "";
                s_cat(m, TRACK_NAME[k]);
                s_cat(m, ": YOURS");
                say(m, "REC and play them");
            } else if (!k && song.bpm != was)
                say(JAM_NAME[k][v % NJAM], "tempo 84");   /* sixteenths only while a word still lands */
            else if (k == T_DRUMS &&
                     (poem.style[T_KEYS] % JAM_STYLES[T_KEYS] != JAM_YOURS(T_KEYS) ||
                      poem.style[T_BASS] % JAM_STYLES[T_BASS] != JAM_YOURS(T_BASS)))
                say(JAM_NAME[k][v % NJAM], "the band follows");
            else
                say(JAM_NAME[k][v % NJAM], TRACK_NAME[k]);
            break;
        }
        if (k == 0)
            song.voice[V_BODY] = (uint8_t)v;
        else if (k == 1)
            song.voice[V_VIBRATO] = (uint8_t)v;
        else
            song.fx[k == 2 ? FX_FILTER : FX_PUMP] = (uint8_t)v;
        hear_sound(k < 2 ? T_VOICE : ui.track);
        break;
    case P_ARRANGE:
        if (k == 0) {
            ui.ph = (uint8_t)(v - 1);
            ui.pbar = 0;
            return;
        }
        if (!stopped_or_say())
            return;
        undo_mark();
        if (lane_resize(&song, ui.track, ui.ph, (unsigned)v) != 0) {
            refuse("CHAIN FULL", "128 bars");
            return;
        }
        vocal_reset();
        break;
    case P_PHRASE: {
        if (ui.track == T_DRUMS) {
            if (k == 1) {                                     /* the drum shown, heard */
                ui.lane = (uint8_t)(v - 1);
                if (!eng_playing)
                    engine_note(T_DRUMS, ui.lane, ui.velo);
                return;
            }
            if (k == 2) {
                ui.pbar = (uint8_t)(v - 1);
                ph_clamp();
                return;
            }
            if (ui.hold < 0) {
                ui.velo = (uint8_t)v;
                return;
            }
            step_t *st = step_at((unsigned)ui.hold);
            undo_mark();
            if (k == 0)
                st->velocity = (uint8_t)v;
            else
                st->nudge = (int8_t)v;
            ui.hold_used = 1;
            step_flash((unsigned)ui.hold);
            return;
        }
        unsigned row;
        int n, at = note_over(cur_tick(), 0, &row, &n);
        if (k == 0 && (ui.track == T_VOICE || at < 0)) {
            ui.pbar = (uint8_t)(v - 1);
            ph_clamp();
            return;
        }
        if (at < 0 || (ui.track == T_VOICE && !stopped_or_say()))
            return;
        note_t *x = ui.track == T_VOICE ? &song.note[n] : 0;
        step_t *st = x ? 0 : step_of((unsigned)at, 1);
        undo_mark();
        if (k == 0)
            st->velocity = (uint8_t)v;
        else if (k == 1)
            note_len((unsigned)at, 0, (unsigned)v);
        else if (k == 3) {
            if (x)
                x->tie = (uint8_t)((x->tie & 1u) | ((unsigned)v & 7u) << 1);
            else
                st->nudge = (int8_t)v;
        } else if (x) {
            x->pitch = (uint8_t)v;
            vocal_reset();
        } else {                                          /* the chord moves with its first note */
            int d = v - st->note[0];
            for (unsigned i = 0; i < st->count; i++)
                st->note[i] = (uint8_t)(st->note[i] + d < 0 ? 0 : st->note[i] + d > 127 ? 127 : st->note[i] + d);
        }
        hear_at((unsigned)at, 0);
        step_flash(ui.step);
        return;
    }
    case P_SOUND:
    case P_ENV:
        if (ui.track == T_VOICE)
            song.voice[VOICE_KNOB[ui.page == P_ENV][k]] = VOICE_KNOB[ui.page == P_ENV][k] == V_OCTAVE ? RANGE_CODE[v] : (uint8_t)v;
        else {
            song.track[ui.track].param[sound_param(k)] = (uint8_t)v;
            engine_sound(ui.track);
        }
        hear_sound(ui.track);
        break;
    case P_EFFECTS: {
        static const uint8_t SRC[2][4] = {{FX_REVERB_SIZE, FX_REVERB_TONE, FX_DELAY_TIME, FX_DELAY_FEEDBACK},
                                          {FX_DRIVE, FX_SQUEEZE, FX_PUMP, FX_FILTER}};
        if (sub >= 2) {
            unsigned src = VOICE_KNOB[sub - 2][k];
            if (src < NVOICE)
                song.voice[src] = src == V_OCTAVE ? RANGE_CODE[v] : (uint8_t)v;
            else
                song.track[T_VOICE].reverb = (uint8_t)v;
            hear_sound(T_VOICE);
            break;
        }
        song.fx[SRC[sub][k]] = (uint8_t)v;
        engine_effects();
        hear_sound(ui.track);
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
        hear_sound(k);
        break;
    }
    case P_SETUP:
        if (k == 0) {
            song.bpm = (uint16_t)v;
            engine_tempo();
            punch_tempo(song.bpm);
        } else if (k == 1 || k == 2) {
            if (k == 1) {
                song.root = (uint8_t)v;
                if (ui.poetry)
                    poem_rekey(&song);                /* SLAMJAM: the band and the poet move with it */
            } else
                song.scale = (uint8_t)v;
            for (unsigned i = 0; i < 3 && !eng_playing; i++)      /* the key's chord: its 1st, 3rd and 5th */
                engine_preview(T_KEYS, key_note(i * 4u - (i > 1)) + (ui.track == T_BASS ? 24u : 0u), 90, CLIP_NONE, 600);
        } else {
            if (!stopped_or_say())
                return;
            if (ui.poetry && v) {                 /* the lines are sixteen to the bar */
                refuse("A POEM IS 4/4", 0);
                return;
            }
            undo_mark();
            song.steps = v ? 12 : 16;
        }
        break;
    case P_ARP: {
        uint8_t *a = song.track[ui.track].arp;
        if (k == 0)
            a[0] = (uint8_t)((a[0] & 0x30u) | (unsigned)v);
        else if (k == 1)
            a[1] = (uint8_t)v;
        else if (k == 2)
            a[0] = (uint8_t)((a[0] & 15u) | (unsigned)(v - 1) << 4);
        else
            a[2] = (uint8_t)v;
        break;
    }
    default:
        return;
    }
    changed();
}

static int keep_song(void);

/* the mode's look, the stage, the FX room's name; kept for the next start */
static void mode_look(unsigned poetry)
{
    ui.poetry = (uint8_t)poetry;
    ui.sub[P_EFFECTS] = 0;                        /* (the band's bank is SLAMJAM's own) */
    look_set(poetry);
    engine_poetry((int)poetry);
    uint32_t mode[2] = {MODE_MAGIC, poetry};
    plat_store_save(OBJ_MOTION, mode, sizeof mode);
}
/* a song loaded plays in the mode it was made in: a saved poem in SLAMJAM (its band back), any other in karaoke */
static void mode_follow(int before)
{
    unsigned poetry = (unsigned)poem_restore(&song);
    if (poetry == ui.poetry)
        return;
    if (poetry)
        ui.karaoke_slot = (int8_t)before;
    mode_look(poetry);
    say(poetry ? "SLAMJAM MODE" : "KARAOKE MODE", 0);
}
/* karaoke or poetry (GLO's list) */
static void poem_new(unsigned topic);
static void mode_set(unsigned poetry)
{
    if (poetry) {                                  /* the song you had kept, a poem made */
        if (words_unsung(0) || keep_song() != 0)
            return;
        ui.karaoke_slot = ui.slot;
    } else if (ui.karaoke_slot < 0 || load_entry((unsigned)ui.karaoke_slot) != 0) {
        song_blank(&song);                         /* (back: the song you had, else a new one) */
        song_loaded();
        ui.slot = -1;
    }
    mode_look(poetry);
    if (poetry)
        poem_new(ui.poem_topic);
    done(poetry ? "SLAMJAM MODE" : "KARAOKE MODE", 0);
}

/* a new poem on the topic: the song now (not kept until you SAVE it) */
static void poem_new(unsigned topic)
{
    ui.poem_topic = (uint8_t)(topic % POEM_TOPICS);
    poem_make(&song, plat_ms() * 2654435761u ^ (uint32_t)eng_tick, ui.poem_topic);
    song_loaded();
    ui.slot = -1;
    ui.page = P_SING;
}

/* ------------------------------------------------------- the lists (SEL) --- */
/* SELECT moves, SEL does it. The ones from A_CLEAR_NOTES on change the song's shape: stopped only */
enum { A_EDIT, A_BUFF_OUT, A_RESET_SOUND, A_SAVE_TO, A_RENAME, A_RESET_KNOBS, A_MODE, A_POEM, A_TOPIC,
       A_WORDS, A_DELETE_NOTE, A_CLEAR_NOTES, A_ADD, A_INSERT, A_DUPE, A_DELETE, A_MOVE, A_BUFF_IN, A_CLEAR_BAR, A_LOAD, A_CLEAR_SLOT,
       NACTIONS };
static const char *const ACTION_NAME[NACTIONS] = {
    "EDIT", "BUFF OUT", "SOUND AS PRESET", "SAVE TO", "RENAME SONG", "RESET KNOBS", "MODE", "NEW POEM", "TOPIC",
    "EDIT WORDS", "DELETE NOTE", "CLEAR NOTES", "ADD", "INSERT", "DUPE", "DELETE", "MOVE", "BUFF IN", "CLEAR BAR", "LOAD", "CLEAR SLOT"};


static unsigned list_actions(uint8_t *a)
{
    unsigned n = 0;
    if (ui.songs) {                               /* SAVE's list: the songs */
        a[n++] = A_LOAD;
        a[n++] = A_SAVE_TO;
        a[n++] = A_CLEAR_SLOT;
        a[n++] = A_RENAME;
        return n;
    }
    switch (ui.page) {
    case P_ARRANGE:
        for (unsigned i = A_ADD; i <= A_MOVE; i++)
            a[n++] = (uint8_t)i;
        a[n++] = A_EDIT;
        a[n++] = A_BUFF_OUT;
        if (buff.bars && buff.track == ui.track)
            a[n++] = A_BUFF_IN;
        break;
    case P_PHRASE: {
        unsigned row;
        int nn;
        if (ui.track == T_VOICE)                  /* (on an empty phrase EDIT WORDS starts its line) */
            a[n++] = A_WORDS;
        if (ui.track != T_DRUMS && note_over(cur_tick(), 0, &row, &nn) >= 0)
            a[n++] = A_DELETE_NOTE;               /* the note at the cursor, whole */
        if (ui.track != T_VOICE)
            a[n++] = A_CLEAR_BAR;
        else if (ui.row < song.rows)
            a[n++] = A_CLEAR_NOTES;
        break;
    }
    case P_SOUND:
    case P_ENV:
        if (ui.track != T_VOICE)
            a[n++] = A_RESET_SOUND;
        break;
    case P_SING:
        if (ui.poetry) {
            a[n++] = A_POEM;
            a[n++] = A_TOPIC;
        }
        break;
    case P_SETUP:
        a[n++] = A_RESET_KNOBS;
        break;
    default:
        break;
    }
    return n;
}

/* an item's value, "" for none */
static void list_value(unsigned a, char *out)
{
    out[0] = 0;
    if (a == A_TOPIC || a == A_POEM) {
        char t[24];
        poem_pick(PG_TOPIC_NAMES, (ui.poem_topic + (a == A_TOPIC)) % POEM_TOPICS, t, sizeof t);
        s_cat(out, t);
    }
    if (a == A_BUFF_IN) {
        s_num(out, buff.bars);
        s_cat(out, buff.bars == 1 ? " bar" : " bars");
    }
}

/* LOAD, SAVE TO and CLEAR SLOT open the eight slots, each with its song's name: SELECT moves, SEL does it */
static void slot_list_open(unsigned action)
{
    slots_read();
    ui.list = 2;
    ui.slot_for = (uint8_t)action;
    unsigned at = ui.slot >= 0 ? (unsigned)ui.slot : 0;
    if (ui.slot < 0)
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
    s_copy(ui.wtext, target ? song.name : line < SONG_LINES ? song.line[line].text : "", target ? sizeof song.name : SONG_TEXT);
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
    if (words_unsung(0))
        return;
    if (pick < SONG_SLOTS && !(ui.present >> pick & 1u)) {   /* an empty slot: a new song in it */
        ui.ask = Q_NEW;
        ui.ask_arg = (uint8_t)pick;
        answer(1);
        return;
    }
    if (ui.old >> pick & 1u) {
        load_entry(pick);                         /* (says why not) */
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

/* BUFF IN: the kept phrase, a copy of its own, after the cursor's */
__attribute__((noinline)) static int buff_in(void)
{
    unsigned t = ui.track, b, i = ui.ph + 1u;
    if (lane_insert(&song, t, i, buff.bars) != 0)
        return -1;
    b = lane_start(&song, t, i);
    if (t == T_VOICE) {
        for (unsigned r = 0; r < buff.nrows; r++) {
            int at = (int)(b * song.steps) + buff.row[r].at, line = line_copy(&song, buff.row[r].line);
            if (at < 0 || line < 0 || row_insert(&song, (unsigned)at, (unsigned)line) < 0)
                return -1;
        }
        return 0;
    }
    uint8_t map[SONG_PATTERNS];
    memset(map, 255, sizeof map);
    for (unsigned j = 0; j < buff.bars; j++) {
        unsigned m = buff.lane[j] & 31u;
        if (m >= SONG_PATTERNS)
            continue;
        if (map[m] == 255) {
            int f = lane_slot_free(&song, t);
            if (f < 0)
                return -1;
            memcpy(song.pattern[f].step[t - 1], buff.mat[m], sizeof buff.mat[m]);
            map[m] = (uint8_t)f;
        }
        song.lane[t][b + j] = (uint8_t)((song.lane[t][b + j] & LANE_START) | map[m]);
    }
    return 0;
}

static void run_action(unsigned a)
{
    unsigned t = ui.track;
    ui.list = 0;
    if (eng_playing && (a == A_LOAD || a == A_POEM || a == A_TOPIC || a == A_MODE)) {
        engine_play(0, 0);                        /* another song or the other mode: this one stops first, then it */
        ui.rec = 0;
        ui.after_stop = (uint8_t)(a + 1u);
        return;
    }
    if (a >= A_WORDS && !stopped_or_say())
        return;
    switch (a) {
    case A_EDIT:
        ui.page = P_PHRASE;
        ui.pbar = 0;
        break;
    case A_WORDS:                                 /* (no line in this phrase yet: words kept make one, at its bar) */
        words_open(0, ui.row < song.rows ? song.row[ui.row].line : 255u);
        break;
    case A_DELETE_NOTE: {
        unsigned row;
        int nn, at = note_over(cur_tick(), 0, &row, &nn);
        if (at < 0)
            break;
        undo_mark();
        if (ui.track == T_VOICE)
            vnote_delete(row, (unsigned)nn);
        else
            step_of((unsigned)at, 1)->count = 0;
        vocal_reset();
        changed();
        done("NOTE DELETED", 0);
        break;
    }
    case A_CLEAR_NOTES:
        ask(Q_CLEAR_NOTES, ui.row, "CLEAR THE NOTES?");
        break;
    case A_ADD:
    case A_INSERT:
    case A_DUPE:
    case A_BUFF_IN: {
        undo_mark();
        unsigned i = ui.ph + (a == A_INSERT ? 0u : 1u);
        int r = a == A_DUPE ? lane_dupe(&song, t, ui.ph) : a == A_BUFF_IN ? buff_in()
                : lane_insert(&song, t, i, lane_len(&song, t, ui.ph));
        if (r != 0) {
            refuse("FULL", "128 bars, 96 rows");
            return;
        }
        ui.ph = (uint8_t)i;
        ui.pbar = 0;
        vocal_reset();
        changed();
        done(a == A_DUPE ? "DUPED" : a == A_BUFF_IN ? "BUFFED IN" : "ADDED", a == A_DUPE ? "the same bars" : 0);
        break;
    }
    case A_DELETE:
        if (lane_count(&song, t) < 2) {
            refuse("ONE PHRASE", "a chain keeps one");
            return;
        }
        ask(Q_DELETE_PHRASE, ui.ph, "DELETE THIS PHRASE?");
        break;
    case A_MOVE:                                  /* SELECT carries the phrase, SEL drops it, HOME puts it back */
        if (lane_count(&song, t) < 2) {
            refuse("ONE PHRASE", 0);
            return;
        }
        undo_mark();
        ui.moving = 1;
        ui.move_net = 0;
        break;
    case A_BUFF_OUT: {
        unsigned b = lane_start(&song, t, ui.ph), n = lane_len(&song, t, ui.ph);
        buff.track = (uint8_t)t;
        buff.bars = (uint8_t)n;
        buff.nrows = 0;
        memcpy(buff.lane, song.lane[t] + b, n);
        if (t == T_VOICE) {
            for (unsigned r = 0; r < song.rows; r++)
                if (row_bar(&song, r) >= b && row_bar(&song, r) < b + n) {
                    buff.row[buff.nrows].at = (int16_t)(song.row[r].at - (int)(b * song.steps));
                    buff.row[buff.nrows++].line = song.row[r].line;
                }
        } else
            for (unsigned m = 0; m < SONG_PATTERNS; m++)
                memcpy(buff.mat[m], song.pattern[m].step[t - 1], sizeof buff.mat[m]);
        done("BUFFED OUT", "BUFF IN: a copy");
        break;
    }
    case A_CLEAR_BAR:
        undo_mark();
        song.lane[t][ui.bar] = (uint8_t)((song.lane[t][ui.bar] & LANE_START) | LANE_REST);
        changed();
        done("A BAR OF REST", 0);
        break;
    case A_RESET_SOUND:
        memset(song.track[t].param, PARAM_PRESET, NPARAM);
        engine_sound(t);
        changed();
        done("SOUND AS PRESET", 0);
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
    case A_RESET_KNOBS:
        ask(Q_RESET_KNOBS, 0, "RESET ALL KNOBS?");
        break;
    case A_MODE:
        mode_set(!ui.poetry);
        break;
    case A_POEM:
    case A_TOPIC:                                 /* a new poem: the same topic, or the next */
        if (!stopped_or_say())
            return;
        undo_mark();                              /* (the poem before: HOME + REC) */
        poem_new(ui.poem_topic + (a == A_TOPIC));
        {
            char m[32] = "A NEW ";                /* what kind of poem: A NEW EULOGY */
            poem_pick(PG_FRAME_NAMES, poem.frame, m + 6, sizeof m - 6);
            for (char *c = m; *c; c++)
                *c = (char)(*c >= 'a' && *c <= 'z' ? *c - 32 : *c);
            done(m, song.name);
        }
        break;
    case A_CLEAR_SLOT: {                          /* the slot picked emptied, after a question */
        unsigned slot = ui.pick_new % SONG_SLOTS;
        if (!(ui.present >> slot & 1u)) {
            refuse("EMPTY", 0);
            break;
        }
        char m[24] = "CLEAR S";
        s_num(m, (int)slot + 1);
        s_cat(m, "?");
        ask(Q_CLEAR_SLOT, slot, m);
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
    case Q_LOAD: {
        int before = ui.slot;
        if (load_entry(arg) == 0) {
            int fresh = arg >= SONG_SLOTS;
            say(fresh ? "A NEW SONG" : "LOADED", fresh ? 0 : ui.slot >= 0 ? ui.names[ui.slot] : song.name);
            ui.page = fresh ? P_PHRASE : P_SING;
            if (!fresh)
                mode_follow(before);
        }
        break;
    }
    case Q_SAVE_OVER:
        save_slot(arg);
        break;
    case Q_MODE:
        run_action(A_MODE);
        break;
    case Q_RESET_KNOBS:                           /* every knob as a new song has it, and the octaves home (undo has it back) */
        undo_mark();
        song_default_knobs(&song);
        for (unsigned t = 0; t < NTRACKS; t++) {
            engine_sound(t);
            ui.oct[t] = 0;
            if (t != T_VOICE)
                engine_octave(t, 0);
        }
        engine_effects();
        changed();
        done("KNOBS RESET", 0);
        break;
    case Q_CLEAR_SLOT:
        if (ui.slot == (int)arg) {                /* the song you are in: a new one in its place */
            load_entry(SONG_SLOTS);
            save_slot(arg);
        } else {
            plat_store_forget(OBJ_BANK0 + arg);
            slots_read();
        }
        done("CLEARED", 0);
        break;
    case Q_NEW:
        if (ui.slot != (int)arg)
            keep_song();
        load_entry(SONG_SLOTS);
        save_slot(arg);                           /* it lives in its slot from the start */
        done("A NEW SONG", "a key writes a note");
        ui.page = P_PHRASE;                       /* ready to write: the Voice's first phrase */
        ui.track = T_VOICE;
        break;
    case Q_DELETE_PHRASE:
        undo_mark();
        lane_delete(&song, ui.track, arg);
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
    if (ui.wline >= SONG_LINES) {                 /* a new line: made now, if there are words for it */
        if (!t[0])
            return;
        int line = line_unused(&song), r = line < 0 ? -1 : row_insert(&song, (unsigned)ui.bar * song.steps, (unsigned)line);
        if (r < 0) {
            refuse("LINES FULL", "64 lines, 96 rows");
            return;
        }
        ui.row = (uint8_t)r;
        ui.wline = (uint8_t)line;
    }
    line_set_text(&song, ui.wline, t);
    words_changed(ui.wline);
    line_renumber(ui.wline);                      /* its notes take the new words' syllables */
    changed();
    ui.save_soon = 1;                             /* the words are kept; the syllables are counted when notes are written */
    done(t[0] ? "KEPT" : "CLEARED", t[0] ? "REC: its notes" : 0);
}

/* ----------------------------------------------------- steps into a phrase --- */
static step_t *step_at(unsigned step) { return &bar_steps(1)[step % SONG_STEPS]; }
static void step_flash(unsigned step)
{
    ui.flash_step = (int16_t)step;
    ui.flash_at = plat_ms();
    changed();
}

/* Drums: a white key is a step of the drum shown. Pressed, an empty step gets it; a step that has it loses it when
 * the key comes up, unless something was done while it was held: the knobs (velocity, nudge), a black key (that
 * drum on it or off) */
__attribute__((noinline)) static void step_press(unsigned k, unsigned step)
{
    if (ui.hold >= 0)
        return;
    step_t *s = step_at(step);
    undo_mark();
    ui.hold = (int8_t)step;
    ui.hold_key = (uint8_t)k;
    ui.hold_used = 0;
    ui.hold_fresh = !(s->drums >> ui.lane & 1u);
    if (ui.hold_fresh) {
        if (!s->drums)
            s->velocity = ui.velo, s->nudge = 0;
        s->drums |= (uint16_t)(1u << ui.lane);
        s->count = 1;
        s->length = 1;
        engine_note(T_DRUMS, ui.lane, s->velocity);
    }
    step_flash(step);
}
__attribute__((noinline)) static void step_release(unsigned k)
{
    if (ui.hold < 0 || k != ui.hold_key)
        return;
    step_t *s = step_at((unsigned)ui.hold);
    if (!ui.hold_used && !ui.hold_fresh) {
        s->drums &= (uint16_t)~(1u << ui.lane);
        if (!(s->drums & DRUM_SOUNDS))
            s->drums = 0;
        s->count = s->drums != 0;
        step_flash((unsigned)ui.hold);
    }
    ui.hold = -1;
}

/* Voice, Keys, Bass: a key down writes its note at the cursor (or grabs the one there) */
__attribute__((noinline)) static void cursor_key(unsigned k)
{
    unsigned t = cur_tick(), p = key_note(k), row;
    int n;
    if (ui.track == T_VOICE) {
        if (!stopped_or_say())
            return;
        p = p < 36 ? 36 : p > 84 ? 84 : p;
    }
    if (grab.n >= 4)
        return;
    undo_mark();
    int at = note_over(t, p, &row, &n), fresh = at < 0;
    if (fresh) {
        if (ui.track == T_VOICE) {
            ui.vpitch = (uint8_t)p;
            if (vnote_add(t) != 0)
                return;
            vocal_reset();
        } else {
            step_t *st = step_of(t, 1);
            if (!st->count || ui.track == T_BASS) {
                st->count = 1;
                st->length = 1;
                st->velocity = ui.velo;
                st->nudge = 0;
            } else if (st->count < 4)
                st->count++;
            st->note[st->count - 1] = (uint8_t)p;
        }
        at = (int)t;
    }
    grab.at[grab.n] = (uint16_t)at;
    grab.pitch[grab.n] = (uint8_t)p;
    grab.fresh[grab.n++] = (uint8_t)fresh;
    hear_at((unsigned)at, p);
    step_flash(ui.step);
}

/* SELECT on Phrase: the cursor moves; the notes grabbed stretch (or shrink) to it */
static void cursor_turn(int d)
{
    cursor_move(d);
    if (!grab.n) {
        if (!eng_playing)
            hear_at(cur_tick(), 0);               /* a note the cursor lands on starts is heard */
        return;
    }
    grab.moved = 1;
    for (unsigned i = 0; i < grab.n; i++) {
        int len = (int)cur_tick() - grab.at[i] + 1;
        note_len(grab.at[i], grab.pitch[i], (unsigned)(len < 1 ? 1 : len));
    }
    changed();
}

/* every key up: a note just written moves the cursor on a step; one that was there, tapped, goes */
static void grab_done(void)
{
    int on = 0;
    for (unsigned i = 0; i < grab.n; i++) {
        unsigned row;
        int n;
        on |= grab.fresh[i];
        if (grab.fresh[i] || grab.moved || note_over(grab.at[i], grab.pitch[i], &row, &n) != grab.at[i])
            continue;
        if (ui.track == T_VOICE)
            vnote_delete(row, (unsigned)n), vocal_reset();
        else {
            step_t *st = step_of(grab.at[i], 1);
            for (unsigned j = 0; j < st->count; j++)
                if (st->note[j] == grab.pitch[i])
                    st->note[j] = st->note[--st->count];
        }
        changed();
    }
    if (on)
        cursor_move(1);
    grab.n = grab.moved = 0;
}

/* recording over the playing song: a note lands on the sixteenth of the track's bar playing (a bar of rest
 * gets a material of its own) */
static void rec_note(unsigned track, unsigned note)
{
    unsigned t = lane_tick(&song, track, eng_tick);
    if (!ui.undo_valid || plat_ms() - ui.undo_at > 4000u)
        undo_mark();
    ui.undo_at = plat_ms();
    step_t *s = &lane_bar_steps(track, t / song.steps, 1)[t % song.steps];
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
        s->length = 2;
    }
    s->nudge = 0;
    changed();
}

/* --------------------------------------------------------------- the keys --- */
/* SLAMJAM's room, the Voice: the next line far enough ahead to be given a tune (its row), or -1 */
static int tune_row(void)
{
    unsigned t = lane_tick(&song, T_VOICE, eng_tick);
    for (unsigned r = 0; r < song.rows; r++)
        if (song.row[r].at >= t + 8u)
            return (int)r;
    return -1;
}
/* the gathered notes given to row r's line, round again if there are fewer than its syllables */
static void tune_set(int r)
{
    if (r < 0 || r >= song.rows || !ui.ntune)
        return;
    const line_t *L = &song.line[song.row[r].line];
    for (unsigned i = 0; i < L->count; i++)
        song.note[L->first + i].pitch = ui.tune[i % ui.ntune];
    ui.ntune = 0;
    say("YOUR TUNE", "this line sings it");
}
/* the notes played are kept (a chord's in a random order) until there are as many as the next line has syllables, or
 * until it comes up: then it sings them, the poet's words and timing to your tune */
static void tune_key(unsigned note)
{
    uint32_t now = plat_ms();
    while (note > 64u)                            /* in the poet's own range (poem_notes): E4 and above thins him out */
        note -= 12u;
    while (note < 43u)
        note += 12u;
    unsigned n = ui.ntune < sizeof ui.tune ? ui.ntune : 0u, at = n;
    if (now - ui.tune_at < 60u && n > ui.tune_chord)
        at = ui.tune_chord + (now * 2654435761u >> 16) % (n - ui.tune_chord + 1u);
    else
        ui.tune_chord = (uint8_t)n;
    for (unsigned i = n; i > at; i--)
        ui.tune[i] = ui.tune[i - 1];
    ui.tune[at] = (uint8_t)(note < 36 ? 36 : note > 84 ? 84 : note);
    ui.ntune = (uint8_t)(n + 1u);
    ui.tune_at = now;
    engine_preview(T_KEYS, note, 70, CLIP_NONE, 160);
    int r = tune_row();
    ui.tune_for = (int8_t)r;
    if (r >= 0 && ui.ntune >= song.line[song.row[r].line].count)
        tune_set(r);
}

static void voice_key_down(unsigned k)
{
    unsigned note = key_note(k);
    int ah = ui.page == P_SING || ui.page == P_EFFECTS;
    int line = ah ? -1 : focus_line();                   /* Sing and the stage: pitches, sung "ah"; else the words */
    uint16_t id = ah ? CLIP_AH : CLIP_NONE;
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
    ui.vpitch = (uint8_t)(note < 36 ? 36 : note > 84 ? 84 : note);   /* the pitch a Voice step is written at */
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

__attribute__((noinline)) static void key_down(unsigned k)
{
    int w = white_index(k), b = black_index(k);
    uint32_t held = ui.buttons;
    if (ui.msg_kind == M_GATE)
        ui.msg_until = 0;                         /* a box saying how to go on goes at the next touch */
    ui.last_key = (uint8_t)k;
    if (ui.moving)
        return;
    if (ui.words) {
        words_key(k);
        return;
    }
    if (held & (1u << B_HOME)) {
        ui.used = 1;
        if (w >= 0) {                                 /* (effects are the rooms' own: FX) */
            if (ui.page == P_EFFECTS)
                punch_key((unsigned)w, 1);
            return;
        }
        if (b >= 0 && b < NTRACKS)
            goto mute;
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
    ui.played = (int16_t)key_note(k);                 /* he hops at it, its notes fly */
    ui.played_at = plat_ms();
    if (ui.page == P_MIXER && b >= 0 && b < NTRACKS) {
    mute:
        song.track[b].mute ^= 1;
        changed();
        say(TRACK_NAME[b], song.track[b].mute ? "muted" : "plays");
        return;
    }
    /* Phrase: Drums' white keys are its steps and the black keys pick the drum (playing too, unless recording);
     * the other tracks' keys write at the cursor while stopped, and play while the song plays */
    if (ui.page == P_PHRASE && !(eng_playing && (ui.rec || ui.track != T_DRUMS))) {
        if (ui.track != T_DRUMS) {
            cursor_key(k);
            return;
        }
        if (w >= 0 && (unsigned)w < song.steps) {
            step_press(k, (unsigned)w);
            return;
        }
        if (b >= 0) {
            if (ui.hold >= 0) {                           /* a held step: that drum on it, or off */
                step_t *s = step_at((unsigned)ui.hold);
                undo_mark();
                s->drums ^= (uint16_t)(1u << b);
                if (!(s->drums & DRUM_SOUNDS))    /* the every-other hat flag goes with the last sound */
                    s->drums = 0;
                s->count = s->drums != 0;
                ui.hold_used = 1;
                step_flash((unsigned)ui.hold);
            } else
                ui.lane = (uint8_t)b;
            engine_note(T_DRUMS, (unsigned)b, ui.velo);
            return;
        }
    }
    unsigned note = key_note(k);
    switch (ui.track) {
    case T_VOICE:
        if (ui.poetry && ui.page == P_EFFECTS && eng_playing && !song.track[T_VOICE].mute) {
            tune_key(note);                        /* SLAMJAM: kept for the next line's tune */
            break;
        }
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
        if (ui.rec && eng_playing)
            rec_note(ui.track, note);
        break;
    }
}

__attribute__((noinline)) static void key_up(unsigned k)
{
    int w = white_index(k);
    if (ui.words)
        return;
    if (w >= 0 && (ui.punch_held >> w & 1u))
        punch_key((unsigned)w, 0);
    if (ui.page == P_PHRASE && (int)k == ui.hold_key && ui.hold >= 0) {
        step_release(k);
        return;
    }
    if (ui.page == P_PHRASE && !eng_playing && ui.track != T_DRUMS)
        return;                                   /* (written: heard for its length) */
    unsigned note = key_note(k);
    if (ui.track == T_VOICE)
        engine_sing_off(note);
    else if (ui.track != T_DRUMS)
        engine_off(ui.track, note);
}

/* ------------------------------------------------------------ the buttons --- */
static void play_press(void);
/* SLAMJAM: the room is the show: walking in starts the poem, walking out stops it */
static void room_play(int in)
{
    if (ui.poetry && (!eng_playing) == in)
        play_press();
}

__attribute__((noinline)) static void play_press(void)
{
    ui.after_stop = 0;
    if (eng_playing) {
        engine_play(0, 0);
        ui.rec = 0;
        return;
    }
    unsigned from = 0;                            /* the song from its top; on Phrase, the phrase being written */
    if (ui.page == P_PHRASE)
        from = lane_start(&song, ui.track, ui.ph) * song.steps;
    /* the first sung clips before the start, so the first words are there on time */
    vocal_cue((int)from);
    int made = vocal_prepare(1);
#ifdef X0X_HOST
    if (getenv("SIM_MISSES"))
        printf("  PLAY from %u: prepared %d\n", from, made);
#endif
    (void)made;
    engine_play(1, from);                         /* the cue stays until the engine plays: ui_service */
    if (ui.armed) {
        ui.armed = 0;
        ui.rec = 1;
    }
}

__attribute__((noinline)) static void rec_press(void)
{
    if (ui.track == T_VOICE) {
        refuse("VOICE NOTES", "SEQ: the keys write them");
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
    if (ui.poetry)
        poem_restore(&song);                      /* (a poem undone: its band as it was) */
    vocal_reset();
    memset(ui.counted, 0, sizeof ui.counted);
    ui.offsets_line = -1;
    ph_clamp();
    changed();
    say_kind(redo ? "REDO" : "UNDO", 0, M_UNDO, 1400);
}

/* the help cards (cards.txt, packed by tools/gen_cards.py into build/gen/cards.h): a button held alone shows its
 * screen's. card_text unpacks the title and the text of the one for this button and screen; a page button's card
 * gets, after it, the knobs and footer buttons the screen does label (draw_card) */
#include "cards.h"
static char card_buf[320], card_title[24];
static char *card_unpack(char *d, char *end, unsigned tok)      /* a byte of 128 or more is a pair of bytes */
{
    if (tok >= 128u) {
        d = card_unpack(d, end, CARD_PAIR[tok - 128u][0]);
        return card_unpack(d, end, CARD_PAIR[tok - 128u][1]);
    }
    if (d < end)
        *d++ = (char)tok;
    return d;
}
static const char *card_string(unsigned i, char *buf, unsigned cap)
{
    char *d = buf;
    for (const uint8_t *p = CARD_DATA + CARD_OFF[i]; *p; p++)
        d = card_unpack(d, buf + cap - 1, *p);
    *d = 0;
    return buf;
}
static const char *card_text(unsigned b, const char **title)
{
    unsigned voice = ui.track == T_VOICE, id;
    switch (b) {
    case B_HOME: id = ui.poetry ? CARD_HOME_POETRY : CARD_HOME; break;
    case B_SEQ: id = ui.page == P_ARRANGE ? CARD_SONG : ui.track == T_DRUMS ? CARD_DRUMS : voice ? CARD_VOICE : CARD_NOTES; break;
    case B_EDIT: id = voice ? CARD_SOUND_VOICE : CARD_SOUND; break;
    case B_ENV: id = voice ? CARD_ENV_VOICE : CARD_ENV; break;
    case B_ARP: id = CARD_ARP; break;
    case B_FX: id = ui.poetry ? CARD_FX_POETRY : CARD_FX; break;
    case B_LFO: id = CARD_MIXER; break;
    case B_GLO: id = CARD_SETUP; break;
    case B_REC: id = CARD_REC; break;
    case B_PLAY: id = CARD_PLAY; break;
    default: return 0;
    }
    *title = card_string(2u * id, card_title, sizeof card_title);
    return card_string(2u * id + 1u, card_buf, sizeof card_buf);
}

/* a page's button: its page (the sequencer: the layer it is on, else the phrase). With HOME held, a layer on: the
 * sequencer's song, phrase and (the Voice's) words; the karaoke room's and the mixer's banks */
__attribute__((noinline)) static void page_button(unsigned b, int deeper)
{
    unsigned p = 0;
    while (p < NPAGES && PAGE_BUTTON[p] != b)
        p++;
    if (p == NPAGES)
        return;
    int seq = b == B_SEQ, here = seq ? ui.page == P_ARRANGE || ui.page == P_PHRASE : p == ui.page;
    if (deeper && here) {
        if (!seq) {
            if (page_count() > 1) {
                ui.sub[p] = (uint8_t)((ui.sub[p] + 1u) % page_count());
                ui.knob_at = plat_ms();
            } else
                say("ONE LAYER", 0);
        } else if (ui.page == P_ARRANGE) {
            ui.pbar = ui.step = 0;                    /* into the song's phrase: at its start */
            ui.page = P_PHRASE;
            ph_clamp();
        } else if (ui.track == T_VOICE && !eng_playing)
            run_action(A_WORDS);                      /* (words are for a stopped song) */
        else
            ui.page = P_ARRANGE;
        return;
    }
    if (seq)
        p = here ? ui.page : P_PHRASE;
    if (p != ui.page && leaving_voice())
        return;
    if ((p == P_EFFECTS) != (ui.page == P_EFFECTS)) {
        say(p == P_EFFECTS ? (ui.poetry ? "ENTERING SLAMJAM ROOM" : "ENTERING KARAOKE ROOM")
                           : (ui.poetry ? "EXITING SLAMJAM ROOM" : "EXITING KARAOKE ROOM"), 0);
        room_play(p == P_EFFECTS);
    }
    if (p != ui.page)
        ui.sub[p] = 0;                            /* (pressed again, where it is: the layer stays, for its help card) */
    ui.page = (uint8_t)p;
    ui.list = 0;
    grab.n = 0;
    if (p == P_SETUP)
        slots_read();
}

__attribute__((noinline)) static void button_down(unsigned b)
{
    uint32_t held = ui.buttons;
    if (ui.msg_kind == M_GATE)
        ui.msg_until = 0;
    if (b == B_HOME && (ui.moving || ui.ask || ui.list))
        ui.used = 1;                              /* HOME closes them, and goes nowhere on its release */
    if (ui.words) {                               /* (HOME cancels on its release, so HOME + SEQ can go on) */
        if (b == B_SEL)
            words_close(1);
        else if (b == B_SEQ && (held & (1u << B_HOME))) {
            words_close(1);
            if (!leaving_voice())
                ui.page = P_ARRANGE;
        } else if (b == B_OCTDN)
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
                int back = ui.move_net > 0;
                if (lane_swap(&song, ui.track, back ? ui.ph - 1u : ui.ph) != 0)
                    break;
                ui.ph = (uint8_t)(ui.ph + (back ? -1 : 1));
                ui.move_net = (int8_t)(ui.move_net + (back ? -1 : 1));
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
        else
            page_button(b, 1);
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
                                           a[ui.list_pick] == A_CLEAR_SLOT))
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
        if (ui.track == T_VOICE) {                    /* the Voice: its QUALITY, the register it sings in */
            int p = (int)range_pos() + (b == B_OCTUP ? 1 : -1);
            if (p < 0 || p > 4)
                break;
            song.voice[V_OCTAVE] = RANGE_CODE[p];
            changed();
            say(VOICE_RANGE[p], 0);
            hear_sound(T_VOICE);
            break;
        }
        {
            int o = ui.oct[ui.track] + (b == B_OCTUP ? 1 : -1);
            if (o < -2 || o > 2)
                break;
            ui.oct[ui.track] = (int8_t)o;
            engine_octave(ui.track, o);           /* the track's notes, played and sequenced; Drums' kit re-pitched */
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
            ui.songs = 0;
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
        if (b != B_HOME)                          /* (HOME goes on its release, unless it was a shift: button_up) */
            page_button(b, 0);
        break;
    }
}

static void button_up(unsigned b, uint32_t held_ms)
{
    if (b == B_HOME && ui.words && !ui.used) {
        words_close(0);
        return;
    }
    if (b == B_HOME && !ui.used && held_ms < 900u && !ui.card && !ui.list && !ui.ask && !ui.words && !ui.moving && !leaving_voice()) {
        if (ui.page == P_EFFECTS) {
            say(ui.poetry ? "EXITING SLAMJAM ROOM" : "EXITING KARAOKE ROOM", 0);
            room_play(0);
        }
        ui.page = P_SING;
        grab.n = 0;
    }
    if (b == B_SAVE && !ui.used && held_ms < 700u) {   /* a tap: the songs list (held, it saved: input_poll) */
        ui.songs = 1;
        ui.list = 1;
        ui.list_pick = 0;
    }
}

__attribute__((noinline)) static void encoder(unsigned e, int d)
{
    if (!d)
        return;
    if (ui.msg_kind == M_GATE)
        ui.msg_until = 0;
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
    if (ui.moving) {                              /* SELECT carries the phrase past its neighbours */
        if (e == EN_SELECT && lane_swap(&song, ui.track, d > 0 ? ui.ph : ui.ph - 1u) == 0) {
            ui.ph = (uint8_t)(ui.ph + (d > 0 ? 1 : -1));
            ui.move_net = (int8_t)(ui.move_net + (d > 0 ? 1 : -1));
            vocal_reset();
            changed();
        }
        return;
    }
    if (ui.list) {
        uint8_t a[NACTIONS];
        int n = ui.list == 2 ? (int)SONG_SLOTS : (int)list_actions(a);
        if (e == EN_SELECT) {
            int p = (int)ui.list_pick + d;
            ui.list_pick = (uint8_t)wrap(p, n);
        }
        return;
    }
    switch (e) {
    case EN_SELECT:
        if (ui.page == P_ARRANGE) {
            int p = (int)ui.ph + d;
            ui.ph = (uint8_t)(p < 0 ? 0 : p);
            ui.pbar = 0;
            ph_clamp();
        } else if (ui.page == P_PHRASE && ui.track == T_DRUMS) {
            int p = (int)ui.pbar + (d > 0 ? 1 : -1);    /* Drums: the phrase's bar before or after */
            ui.pbar = (uint8_t)(p < 0 ? 0 : p);
            ph_clamp();
        } else if (ui.page == P_PHRASE) {
            cursor_turn(!grab.n && (d > 2 || d < -2) ? d * 4 : d);   /* turned fast: a beat at a time */
        } else if (ui.page == P_SETUP) {
            ask(Q_MODE, 0, ui.poetry ? "BACK TO KARAOKE?" : "SWITCH TO SLAMJAM?");   /* GLO: SELECT, the mode */
        } else {
            int p = (int)ui.sub[ui.page] + d, n = (int)page_count();
            ui.sub[ui.page] = (uint8_t)wrap(p, n);
            ui.knob_at = plat_ms();               /* the stage shows the strip a moment, though the song plays */
        }
        break;
    case EN_ALGO: {
        int t = (int)ui.track + d;
        t = wrap(t, NTRACKS);
        if (t != ui.track && !leaving_voice()) {
            ui.track = (uint8_t)t;
            ui.hold = -1;
            grab.n = 0;
            ph_clamp();
            say(TRACK_NAME[t], 0);
            hear_sound((unsigned)t);
        }
        break;
    }
    case EN_PRESET:
        if (ui.page == P_SOUND || ui.page == P_ENV || ui.page == P_SING || ui.page == P_EFFECTS) {   /* (the rooms' black keys play it) */
            if (ui.track == T_VOICE)
                break;
            track_t *k = &song.track[ui.track];
            if (ui.track == T_DRUMS) {
                int n = (int)engine_kits(), s = (int)k->sound + d;
                k->sound = (uint8_t)wrap(s, n);
            } else {                              /* the next of the track's own sounds */
                uint8_t list[64];
                int n = (int)sound_list(ui.track, list), at = (int)sound_place(ui.track, list, (unsigned)n) + d;
                k->sound = list[wrap(at, n)];
            }
            engine_sound(ui.track);
            changed();
            say(ui.track == T_DRUMS ? engine_kit_name(k->sound) : PRESETS[k->sound].name, TRACK_NAME[ui.track]);
            hear_sound(ui.track);
        }
        break;
    default: {
        unsigned k = e - EN_K1;
        knob_t r = knob_info(k);
        int step = r.hi - r.lo > 60 && (d > 2 || d < -2) ? d * 2 : d;
        knob_set(k, r.value + step);
        ui.flash_at = ui.knob_at = plat_ms();
        ui.flash_step = (int16_t)(100 + k);
        ui.turn_k = (uint8_t)(ui.sub[P_EFFECTS] * 4u + k);
        ui.turn_d = (int8_t)(d > 0 ? 1 : -1);
        ui.turn_n++;
        break;
    }
    }
}

/* ------------------------------------------------------------- the input --- */
static void input_poll(void)
{
    uint32_t now = plat_ms(), remote = now - remote_at < 2000u;
    ph_clamp();
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
    /* a key tapped and let go while the loop was busy came before a button pressed in that time (else a key tapped
     * just before HOME would be taken as HOME + key): it is pressed and let go first */
    uint32_t tapped = bdown ? kdown & ~(plat_keys_now() | (remote ? remote_keys : 0)) : 0;
    for (unsigned i = 0; i < 27; i++)
        if (tapped >> i & 1u) {
            ui.keys |= 1u << i;
            key_down(i);
            ui.keys &= ~(1u << i);
            key_up(i);
            if (grab.n && !(ui.keys & 0x7FFFFFFu))
                grab_done();
            any = 1;
        }
    kdown &= ~tapped;
    /* buttons let go first: one let go and another pressed within one look are one after the other, not held
     * together (a press taken for a shift would go nowhere) */
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
    if (grab.n && !(ui.keys & 0x7FFFFFFu))       /* Phrase: notes let go before new ones come (a quick tap) */
        grab_done();
    for (unsigned i = 0; i < 27; i++)
        if (kdown >> i & 1u) {
            if (ui.buttons)
                ui.used = 1;
            ui.keys |= 1u << i;
            key_down(i);
            any = 1;
        }
    /* step entry: a chord is written when its keys are all up */
    if (grab.n && !(ui.keys & 0x7FFFFFFu))       /* Phrase: the keys written at the cursor, all let go */
        grab_done();
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
    /* SAVE held alone a moment: saved where the song lives (no slot yet: SAVE TO's slots) */
    if (ui.buttons == (1u << B_SAVE) && ui.pressed == B_SAVE && !ui.used && now - ui.press_at > 700u) {
        ui.used = 1;
        if (ui.slot >= 0)
            save_slot((unsigned)ui.slot);
        else {
            ui.songs = 1;
            slot_list_open(A_SAVE_TO);
            say("PICK A SLOT", 0);
        }
    }
    /* a button held alone for a moment: its card */
    if (ui.buttons && !ui.used && !ui.card && !ui.words && now - ui.press_at > 900u && ui.buttons == (1u << ui.pressed)) {
        const char *title;
        if (card_text(ui.pressed, &title))
            ui.card = (uint8_t)(ui.pressed + 1);
    }
    if (any)
        ui.last_input = now;
    ph_clamp();
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
    /* the clips: what is coming, and the line the Voice keys sing. Until the engine has started, the clips
     * wanted are still the ones from PLAY's cue (else the first syllable's could go to the keys' clips) */
    if (eng_playing)
        vocal_cue(-1);
    int line = focus_line();
    int poet = ui.poetry && eng_playing;          /* (the Voice keys say the poet's own syllables then: made ahead) */
    vocal_stopped_focus((ui.page == P_ARRANGE || ui.page == P_PHRASE) && ui.track == T_VOICE && ui.row < song.rows ? ui.row : -1,
                        ui.page == P_SING ? -1 : line, line == ui.live_line ? ui.live_syl : 0);
    vocal_ah(ui.track == T_VOICE && ui.page == P_SING && !poet);
    if (!ui.words && !ui.ask)
        vocal_prepare(0);
}

static void ui_draw(void);
static void speech_background_service(void)
{
    ui_midi();
    ui_draw();                                    /* (a clip takes 100 to 250 ms: the screen goes on) */
}

void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.slot = -1;
    ui.velo = 100;
    ui.offsets_line = -1;
    ui.flash_row = ui.flash_step = -1;
    ui.track = T_VOICE;
    ui.hold = -1;
    ui.last_key = 255;
    ui.vpitch = 60;
    ui.karaoke_slot = -1;
    uint32_t mode[2];                             /* the mode, kept in the settings object 386DX had spare */
    ui.poetry = plat_store_load(OBJ_MOTION, mode, sizeof mode) == (int)sizeof mode && mode[0] == MODE_MAGIC && mode[1] == 1u;
    look_set(ui.poetry);
    engine_poetry(ui.poetry);
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
    if (ui.poetry) {                              /* poetry mode: a poem to start with; the song stays in its slot */
        ui.karaoke_slot = ui.slot;
        poem_new(plat_ms());
    }
}

#include "ui_pages.h"
#include "companion.h"
