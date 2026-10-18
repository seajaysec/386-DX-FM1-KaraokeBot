/* SPDX-License-Identifier: GPL-3.0-only */
/* synth: a pool of voices playing eight engines (simple, stack, wavetable, duo, epiano, organ,
 * hardsync, swarm), each voice with two ADSRs, a state-variable filter, an LFO and sends.
 *
 * A track's sound is a flat byte array par[NTP] (the four modules, four knobs each, plus the
 * play page, sends, player and mix). Continuous values are 0..127, switches small integers (ranges in
 * tp_info()). The same index space addresses parameter locks (seq/pattern.h lane_t.param), LFO
 * destinations and MIDI CCs. The drum engine's 11 sounds take TP_DRUM0 + sound * DP_N + DP_*.
 *
 * Threads: synth_render() runs in the audio interrupt; the main loop only writes par[] bytes and
 * calls synth_note_* through the engine's command queue (app/engine.c). Float, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define SY_SR 44100.0f
#define NTRK 3                        /* instrument tracks: Keys, Bass, Drums */
#define SY_CHUNK 64                   /* control rate: envelopes, LFO, glides, locks */
#define NVOICE 10                     /* synth voices, all tracks */
#define NVOICE_TRK 6                  /* the most one track takes */

enum { EG_SIMPLE, EG_STACK, EG_WAVETABLE, EG_DUO, EG_EPIANO, EG_ORGAN, EG_HARDSYNC, EG_SWARM, EG_DRUMKIT,
       NENGINE };
extern const char *const EG_NAME[NENGINE];         /* "simple", "stack", ... "drumkit" */
extern const char *const EG_KNOB[NENGINE][4];      /* each engine's four knob names, */

enum {
    TP_ENGINE, TP_E1, TP_E2, TP_E3, TP_E4,         /* Engine page: the engine and its four knobs */
    TP_VMODE, TP_PORTA, TP_BEND, TP_VOLUME,        /* Play page: poly / mono / legato, glide (0 = off), bend range
                                                     * (0 off, 1..7 semitones, 8 = an octave: tp_bend_semi), volume */
    TP_A, TP_D, TP_S, TP_R,                         /* amp ADSR */
    TP_FA, TP_FD, TP_FS, TP_FR,                     /* filter ADSR */
    TP_CUT, TP_RES, TP_FENV, TP_KTRK, TP_FTYPE,     /* Filter page (cutoff 50 Hz .. 20 kHz) and the type (FT_*); on a
                                                     * drumkit track low cut / compression / drive / hi cut */
    TP_SEND1, TP_SEND2, TP_SENDT,                   /* sends: fx I, fx II, tape */
    TP_LTYPE, TP_LSPEED, TP_LAMT, TP_LDEST, TP_LX,  /* LFO: type (off, value, tremolo, random, duck), then its four
                                                     * knobs as the comment below the enum says */
    TP_LX2,                                         /* a fifth LFO value: shape (value, tremolo) or envelope (random) */
    TP_PLAYER, TP_P1, TP_P2, TP_P3, TP_P4,          /* Players: off / arp / roll / hold and its four knobs */
    TP_AP1, TP_AP2, TP_AP3, TP_AP4,                 /* the arp's second page: length, style, glide, stereo */
    TP_LEVEL, TP_PAN,                               /* Mix */
    TP_FOLLOW,                                       /* follows the follow: 0 no, 1 diatonic, 2 transpose only */
    TP_CHAN,                                        /* MIDI channel 0 = off, 1..16 */
    TP_DRUM0,                                       /* the drum kit: NDRUM keys * DP_N */
    NTP = TP_DRUM0 + 11 * 9
};
/* a drumkit key: its model (drumkit.h DM_*), the model's four knobs (names per model: DM_KNOB), then the
 * shift page random / pan / volume (-30..+20 dB, 0..127, 76 = 0 dB) and the play mode (drumkit.h PM_*).
 * On a drumkit track TP_A..TP_R are the track-wide drum envelope: funk (pre-attack delay), attack, random
 * (randomise every model's knobs per hit, the kick's pitch excepted), length (sustain / release scale). */
enum { DP_MODEL, DP_K1, DP_K2, DP_K3, DP_K4, DP_RANDOM, DP_PAN, DP_VOLUME, DP_MODE, DP_N };
#define TP_DRUM(s, k) (TP_DRUM0 + (s) * DP_N + (k))
#define NTP_SOUND TP_PLAYER                          /* a preset keeps par[0..TP_PLAYER) */

enum { VM_POLY, VM_MONO, VM_LEGATO };
/* filter types, the names: svf (24 dB, aggressive), ladder (transistor-ladder style, saturating feedback), z lowpass
 * (gentle 12 dB), z hipass (12 dB), dynamic (drum tracks: low cut, compression, drive, hi cut on the
 * Filter page's four knobs instead of cutoff / res / env / keytrack) */
enum { FT_SVF, FT_LADDER, FT_ZLP, FT_ZHP, FT_DYNAMIC, NFT };
/* the LFO: TP_LSPEED is one knob: 0..7 synced 2/1, 1, 1/2, 1/4, 1/8, 1/16, 1/32, 1/64
 * (counter-clockwise), 8..127 free (clockwise, 0.1..30 Hz). Per type the five values TP_LSPEED / TP_LAMT /
 * TP_LDEST / TP_LX / TP_LX2 mean:
 *   value:   speed, amount -100..100 % (64 = 0), destination (LD_*: the module synth / envelope / filter / amp,
 *            + LD_FREE for the free-running version, as the list), parameter (TP_LX: the module's knob
 *            0..3: the engine's four, attack / decay / sustain / release, cutoff / reso / env / keytrack, volume /
 *            pitch / pan / -), shape (TP_LX2: square, sine, ramp, saw).
 *            100 % = the destination knob's whole range; pitch = +-12 semitones.
 *   random:  speed, amount 0..100 % (unipolar: the offset only adds), destination (LD_*), parameter (TP_LX),
 *            envelope (TP_LX2: fade in / fade out, below)
 *   tremolo: rate, vibrato amount -100..100 % (64 = 0, 100 % = +-1 semitone), volume amount -100..100 % (TP_LDEST,
 *            64 = 0), envelope (TP_LX), shape (TP_LX2: sine, saw, exponential, square, blip)
 *   duck:    source (TP_LSPEED: 0..7 tracks 1-8, 8..10 follow / punch / tape, 11 metronome), amount 0..100 %,
 *            hold (TP_LDEST, ms), release (TP_LX, ms)
 * Regular (not free): the LFO's phase restarts on every note of the track (random draws a fresh value too).
 * The envelope knob: 64 = none; below, the LFO fades in from the note over up to 3 s (0 = 3 s, 63 = at once);
 * above, it fades out over up to 3 s (65 = 3 s, 127 = 50 ms). */
enum { LT_OFF, LT_VALUE, LT_TREMOLO, LT_RANDOM, LT_DUCK, NLT };
enum { LD_SYNTH, LD_ENV, LD_FILTER, LD_AMP, LD_FREE = 4, NLD = 8 };   /* TP_LDEST: module, + LD_FREE = free-running */
#define LS_SYNC_N 8                                /* TP_LSPEED values below this are synced divisions */
enum { PL_OFF, PL_ARP, PL_ROLL, PL_HOLD, NPL };
/* arp: P1 speed (1/4 1/4T 1/8 1/8T 1/16 1/16T 1/32 1/32T 1/64), P2 pattern (up, down, up-down 1, up-down 2 (the
 * ends repeated), as played, random), P3 range 1..4 octaves, P4 hold; its page 2 (the shift page) is
 * TP_AP1..AP4 = note length (short medium long), style (off A B C D E), glide (off low high), stereo (mono
 * narrow wide). roll: P1 roll 0..63 (63 = a note per sequencer step of the
 * track), P2 pattern (up down up-down random), P3 -, P4 hold */

/* how a value is shown: a number, a percentage, bipolar (64 = 0), a name from the list, semitones, on / off,
 * milliseconds / seconds, the LFO speed (a division or Hz), 00..99 (the engine knobs,),
 * Hz / kHz (cutoff), an LFO fade ("in 50 %" / "off" / "out 50 %"), dB */
enum { PK_NUM, PK_PCT, PK_BIP, PK_NAMES, PK_SEMI, PK_SWITCH, PK_MS, PK_HZ_SYNC, PK_NN, PK_HZ, PK_FADE, PK_DB };
typedef struct {
    const char *name;                 /* short, as on the knob row (<= 8 chars) */
    uint8_t lo, hi, def;
    uint8_t kind;                     /* PK_* */
    const char *const *names;         /* PK_NAMES */
} tp_info_t;
const tp_info_t *tp_info(int p, int engine);        /* engine: the track's (TP_E1..E4 names depend on it) */
void tp_text(int p, int v, int engine, char *num, char *unit);   /* the value as the screen shows it (<= 11, <= 4) */
/* the same, knowing the whole track: the player's knobs (TP_P1..P4) and the LFO's fields (TP_LAMT, TP_LDEST,
 * TP_LX) take their names, ranges and kinds from TP_PLAYER / TP_LTYPE */
const tp_info_t *tp_info_par(int p, const uint8_t *par);
void tp_text_par(int p, int v, const uint8_t *par, char *num, char *unit);
void sound_default(uint8_t *par, int engine);       /* par[NTP] for a fresh track of this engine */
static inline int tp_bend_semi(int v) { return v >= 8 ? 12 : v; }   /* TP_BEND as semitones */

/* factory presets: the sound part of par[] */
typedef struct {
    const char *name;
    uint8_t par[NTP_SOUND];
} preset_t;
extern const preset_t PRESETS[];
extern const int NPRESETS;
int preset_first(int engine);                       /* the first preset of an engine, -1 */

/* --------------------------------------------------------------- voices --- */
/* performance state per track, written by the sequencer / punch-in FX, read by the voices each chunk */
typedef struct {
    int16_t pitch_c;                  /* cents, from bends, ramps, punch octave */
    int16_t cut;                      /* filter cutoff offset, -127..127 */
    int8_t pan;                       /* pan offset */
    uint8_t mute;                     /* 1 = silent (punch mute: voices keep running) */
    uint8_t send1, send2;             /* send offsets 0..127 (punch delay / reverb up) */
    uint8_t long_tail;                /* drums: decay x4 */
    uint8_t hp;                       /* high-pass 0..127 (the punch filter on drums; the engine also tells the
                                       * drumkit's dynamic filter); the synth may ignore it */
} perf_t;

typedef struct synth synth_t;         /* opaque: dsp/synth.c */
#define SYNTH_BYTES 6144             /* room for the struct (checked at compile time in synth.c) */

void synth_init(synth_t *s, uint8_t (*par)[NTP]);   /* par: the tracks' parameter arrays (NTRK of them) */
void synth_tempo(synth_t *s, float bpm);
/* notes: tag identifies a note for its off (the sequencer uses the step, the keys the key) */
void synth_note_on(synth_t *s, int trk, int note, int vel, int glide_ms, uint32_t tag);
void synth_note_off(synth_t *s, int trk, uint32_t tag);
void synth_all_off(synth_t *s, int trk);            /* -1: every track (panic) */
void synth_bend(synth_t *s, int trk, int cents);    /* performance bend (the bend component, MIDI) */
perf_t *synth_perf(synth_t *s, int trk);            /* the track's performance state (write from the interrupt) */
#ifndef LOCK_NONE
#define LOCK_NONE 0xFFu               /* seq/pattern.h's: no lock */
#endif
void synth_lock(synth_t *s, int trk, int param, int val);   /* a parameter lock in effect (LOCK_NONE: the knob's) */
void synth_duck_trigger(synth_t *s, int src);      /* a hit on a duck source (0..7 tracks, 8..10 follow / punch / tape,
                                                    * 11 metronome): duck LFOs listening to it drop */
/* a block: synth_prepare(n) once (the tracks' LFO, tremolo and duck values for every chunk boundary inside
 * the next n frames, n <= 512, and the meters), then synth_render(n) for each voice range, on any core: a
 * render touches only its voices' state, so 0..8 on one core and 8..16 on the other may run at once.
 * Notes, locks, bends and perf writes happen between blocks (the command queue), never during a render.
 * Every voice range must be rendered each block, with the n given to prepare. */
void synth_prepare(synth_t *s, uint32_t n);
/* render n frames (any n; chunks inside): dry L/R and the three sends (mono), added into the buffers.
 * v0..v1 is the range of voices to render (the two cores split the pool) */
void synth_render(synth_t *s, int v0, int v1, float *l, float *r, float *s1, float *s2, float *st, uint32_t n);
int synth_sounding(const synth_t *s, int v0, int v1);          /* a voice in the range is sounding */
int synth_voices_used(const synth_t *s);
void synth_limit(synth_t *s, int max_voices);                  /* the overload guard: fewer voices */
extern volatile uint16_t synth_peak[NTRK];                     /* per track meters, Q15, decaying (UI) */
