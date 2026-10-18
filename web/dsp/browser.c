/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware's speech engine, sung clips and audio engine in the companion page (WebAssembly):
 * the importer asks how words are pronounced (syllables, where each starts, how long each needs), and
 * a song can be heard before it is sent: the same clips, instruments and effects as the FM-1. */
#include "song.h"
#include "vocal.h"
#include "engine.h"
#include "speech.h"
#include <string.h>
#include <emscripten/emscripten.h>
static char phrase[64];
static uint16_t minimum[64];
static int32_t mix[512];
static float pcm[512];
void dx_speech_idle(void) {}
EMSCRIPTEN_KEEPALIVE int browser_init(void) { song_blank(&song); engine_init(); return dx_speech_init(); }
EMSCRIPTEN_KEEPALIVE void *browser_text(void) { return phrase; }
EMSCRIPTEN_KEEPALIVE void *browser_minimum(void) { return minimum; }
EMSCRIPTEN_KEEPALIVE int browser_scan(void) { return dx_speech_scan(phrase); }
EMSCRIPTEN_KEEPALIVE int browser_articulation(void) { return dx_speech_articulation(phrase, minimum); }
EMSCRIPTEN_KEEPALIVE unsigned browser_offset(unsigned syllable) { return dx_sing_offset(syllable); }
/* the preview: the song's bytes go to browser_song(), then start, then render blocks */
EMSCRIPTEN_KEEPALIVE void *browser_song(void) { return &song; }
EMSCRIPTEN_KEEPALIVE unsigned browser_song_size(void) { return sizeof song; }
EMSCRIPTEN_KEEPALIVE int browser_start(unsigned from)
{
    if (!song_valid(&song))
        return -1;
    engine_play(0, 0);
    engine_render(mix, 1);
    engine_song();
    vocal_reset();
    vocal_misses = 0;
    vocal_cue((int)from);
    vocal_prepare(1);
    vocal_cue(-1);
    engine_play(1, from);
    return 0;
}
EMSCRIPTEN_KEEPALIVE void browser_stop(void) { engine_play(0, 0); }
EMSCRIPTEN_KEEPALIVE float *browser_render(unsigned frames)
{
    if (frames > 256)
        return 0;
    vocal_prepare(0);
    engine_render(mix, frames);
    for (unsigned i = 0; i < frames * 2; i++)
        pcm[i] = (float)mix[i] / 8388608.0f;
    return pcm;
}
EMSCRIPTEN_KEEPALIVE unsigned browser_tick(void) { return eng_tick; }
EMSCRIPTEN_KEEPALIVE unsigned browser_misses(void) { return vocal_misses; }
EMSCRIPTEN_KEEPALIVE int browser_row(void) { return eng_sing_row; }
