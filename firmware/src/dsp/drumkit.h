/* SPDX-License-Identifier: GPL-3.0-only */
/* drumkit: a drum synth with eleven keys on the black keys, each a model instance with the
 * model's own four knobs, random, pan, volume and a play mode (synth.h TP_DRUM(key, DP_*)). The ten
 * models (DP_MODEL, DM_*) are: kick, snare, hi-hat, clap, shaker, tambourine, resonator,
 * congas, tom, fm. Analytic (sines with pitch envelopes, filtered noise bursts, six-square metal, a
 * Karplus-like string, 2-op FM): cheap, no samples. Up to NDVOICE keys at once. The track's TP_A..TP_R
 * are the drumkit envelope: funk (a delay before the hit, 0..60 ms), attack (a fade-in, 0..100 ms),
 * random (every model's knobs vary per hit, the kick's pitch excepted), length (0.25x .. 4x the
 * model's length, 64 = 1x): neutral is 0 / 0 / 0 / 64. Float, fastmath.h. */
#pragma once
#include <stdint.h>

#define DL_NSOUND 11                 /* keys: the black keys, a model instance each */
#define NDVOICE 8
/* the ten models and each one's four knobs, with the printed defaults:
 *   kick       pitch length punch tone        78 31 50 50
 *   snare      pitch length snare punch       78 31 50 50
 *   hi-hat     pitch length era tone          73 18 50 69   (era: noise left, metallic ring-mod right; tone: bell left, bandpass right)
 *   clap       tone length spread contour     78 50 50 30
 *   shaker     color length attack onset      24 50 50 79
 *   tambourine tone length shake ring         73 18 50 69
 *   resonator  pitch length tone strike       54 42 61 95
 *   congas     pitch length slap mush         78 36 50 00
 *   tom        pitch length sweep rate        79 36 30 50
 *   fm         pitch length mod feedback      73 78 36 12
 * (values printed 00..99; stored 0..127 = printed * 1.28) */
enum { DM_KICK, DM_SNARE, DM_HIHAT, DM_CLAP, DM_SHAKER, DM_TAMB, DM_RESO, DM_CONGA, DM_TOM, DM_FM, NDM };
extern const char *const DM_NAME[NDM];
extern const char *const DM_KNOB[NDM][4];
extern const uint8_t DM_DEFAULT[NDM][4];
/* play modes (p. 69): one-shot, gated (while the key / step is held), loop, choke groups 1..3 (one-shots that cut
 * each other within the group) */
enum { PM_ONESHOT, PM_GATED, PM_LOOP, PM_CHOKE1, PM_CHOKE2, PM_CHOKE3, NPM };
extern const char *const PM_NAME[NPM];
extern const char *const DL_SOUND_NAME[DL_NSOUND];     /* the default kit's key names: "kick", "snare", "hat", "open", ... */
extern const uint8_t DL_DEFAULT_KIT[DL_NSOUND][9];     /* DP_* of each key of a new drum track (synth.h DP_N = 9) */

typedef struct drumkit drumkit_t;
#define DRUMKIT_BYTES 4096                            /* 8 voices, the dynamic filter and two resonator strings
                                                        * (int16, 512 each): 3960 bytes used */

void drumkit_init(drumkit_t *d, const uint8_t *par);  /* par: the track's par[] (reads TP_DRUM0.., TP_A..TP_R at a hit,
                                                         * TP_SEND* each chunk) */
/* a hit: the key's model with its knobs as they are now (plus the random variations), vel 1..127, pitch_semi
 * added to the model's pitch (the fm model: steps), short_gate caps the length at 60 ms (a short step),
 * long_tail x4 (the punch-in). A key retriggers its own voice; a choke-group key fades the group's others
 * (5 ms). Gated and loop modes hold the voice until drumkit_release(sound). */
void drumkit_hit(drumkit_t *d, int sound, int vel, int pitch_semi, int short_gate, int long_tail);
void drumkit_release(drumkit_t *d, int sound);       /* the key / step ends: gated and loop modes stop */
void drumkit_choke(drumkit_t *d);
/* the dynamic filter (synth.h FT_DYNAMIC on a drum track): the Filter page's knobs = low cut (12 dB, 20 Hz ..
 * 2 kHz, 0 = off), compression (one knob: threshold, ratio and make-up together), drive (tanh-shaped, 1..16x),
 * hi cut (12 dB, 300 Hz .. 20 kHz, 127 = off); any other type = off. It runs on the track's dry L/R after the
 * keys are summed; the sends are taken before it. Call it from the audio context when a knob changes (a few
 * exps; nothing to do per block). */
void drumkit_filter(drumkit_t *d, int type, int locut, int comp, int drive, int hicut);
/* render n frames: dry L/R (through the dynamic filter when on) and sends (the track's TP_SEND*), added into
 * the buffers. Stack: about 400 bytes (three 32-sample sub-block buffers) */
void drumkit_render(drumkit_t *d, float *l, float *r, float *s1, float *s2, float *st, uint32_t n);
int drumkit_sounding(const drumkit_t *d);
extern volatile uint8_t drumkit_hitv[DL_NSOUND];      /* the last hit of each sound, 255 = full (UI lets it fall) */
