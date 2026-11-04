/* SPDX-License-Identifier: GPL-3.0-only */
/* The 386DX simulator: the firmware's UI, song, sung clips and audio engine on a computer, played from
 * a script. The screen goes to images, the sound to a WAV file, and `expect` lines check what the
 * firmware did, so a scenario fails when the firmware stops doing what it says.
 *
 *   sim SCRIPT OUTDIR
 *
 * Script lines (# comments):
 *   tap BUTTON [ms]        press and release (HOME EDIT SEQ ENV FX LFO GLO ARP SEL SAVE PLAY REC OCT- OCT+)
 *   down BUTTON / up BUTTON
 *   key N [ms]             tap key N (0 = F3 .. 26 = G5); keydown N / keyup N
 *   turn ENC N             SELECT ALGO PRESET K1 K2 K3 K4, N detents (+ clockwise)
 *   type TEXT              the typing keys for TEXT (letters and spaces)
 *   wait MS                time passes: the main loop, the screen and the audio run
 *   shot NAME              the screen to OUTDIR/NAME.ppm
 *   load N                 the Songs list's entry N, through the panel's own load
 *   expect WHAT VALUE      page, track, rows, line_text R TEXT, notes, playing, msg TEXT, slot,
 *                          bpm, sung (a clip played), misses MAX, peak_min DB, saved S,
 *                          button_led BUTTON 0|1, punch W 0|1 (the effect on white key W sounding),
 *                          heard 0|1 (sound since the last one), syllables ROW LIST, layer N (the page's),
 *                          tune_max PITCH (the line given your tune: G2 up to PITCH)
 *   echo TEXT */
#include <math.h>
#include "plat_host.c"


/* ---- the audio, to a WAV ---- */
static FILE *wav;
static uint32_t frames_out, audio_due;
static double peak, sum_sq, heard;               /* heard: the loudest since the last `expect heard` */
static int32_t block[512];
static void audio_run(uint32_t ms)
{
    audio_due += ms * 441u;                          /* frames owed, x10 */
    while (audio_due >= 2560u) {
        audio_due -= 2560u;
        engine_render(block, 256);
        int16_t out[512];
        for (unsigned i = 0; i < 512; i++) {
            double x = block[i] / 8388608.0;
            if (fabs(x) > peak)
                peak = fabs(x);
            if (fabs(x) > heard)
                heard = fabs(x);
            sum_sq += x * x;
            out[i] = (int16_t)(x * 32767.0);
        }
        fwrite(out, 2, 512, wav);
        frames_out += 256;
    }
}
static void wav_header(uint32_t frames)
{
    uint32_t data = frames * 4u, chunk = 36u + data, rate = 44100u, byte_rate = rate * 4u;
    uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    fwrite(&chunk, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    uint32_t sixteen = 16;
    fwrite(&sixteen, 4, 1, wav);
    fwrite(&pcm, 2, 1, wav);
    fwrite(&ch, 2, 1, wav);
    fwrite(&rate, 4, 1, wav);
    fwrite(&byte_rate, 4, 1, wav);
    fwrite(&align, 2, 1, wav);
    fwrite(&bits, 2, 1, wav);
    fwrite("data", 1, 4, wav);
    fwrite(&data, 4, 1, wav);
}

/* time passes in 1 ms steps: input, the main loop's service, the screen, the audio */
uint32_t host_key_leds(void);
uint32_t host_button_leds(void);
static unsigned sung_frames, hold_ms = 120, pace, turn_ms = 75;   /* pace: SIM_PACE ms an action (a trailer's brisk hands) */
static FILE *video;                                  /* SIM_VIDEO: the screen, 30 frames a second, raw RGB */
static FILE *state;                                  /* SIM_STATE: with each frame, a line of what the hands and lights do */
static int enc_pos[NE];                              /* each encoder's detents turned so far */
static unsigned video_phase;
static void video_frame(void)
{
    static uint8_t rgb[240 * 240 * 3];
    for (unsigned i = 0; i < 240 * 240; i++) {
        uint16_t c = screen[i];
        rgb[i * 3] = (uint8_t)((c >> 11) * 255 / 31);
        rgb[i * 3 + 1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
        rgb[i * 3 + 2] = (uint8_t)((c & 31) * 255 / 31);
    }
    fwrite(rgb, 1, sizeof rgb, video);
    if (!state)
        return;
    /* buttons held, keys held, button lights, key lights, play red, the encoders, the page, the track, then
     * the four knobs as NAME=value, tab separated */
    fprintf(state, "%u %u %u %u %u", buttons, keys, host_button_leds(), host_key_leds(), red);
    for (unsigned e = 0; e < NE; e++)
        fprintf(state, " %d", enc_pos[e]);
    fprintf(state, "\t%s\t%s", PAGE_NAME[ui.page], TRACK_NAME[ui.track]);
    for (unsigned k = 0; k < 4; k++) {
        knob_t r = knob_info(k);
        char v[24];
        knob_text(k, &r, v);
        fprintf(state, "\t%s=%s", r.name, v);
    }
    fprintf(state, "\t%s\t%u %u %u\n", ui.list ? "list" : ui.words ? "words" : ui.ask ? "ask" : "", eng_playing,
            eng_sing_level, eng_sing_on);
}
static uint32_t sim_ms;
static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t++) {
        host_ms();
        sim_ms++;
        if (video && (video_phase += 30) >= 1000) {
            video_phase -= 1000;
            video_frame();
        }
        audio_run(1);
        if (eng_sing_on && eng_sing_clip >= 0)
            sung_frames++;
        static uint32_t misses_seen;
        if (vocal_misses != misses_seen) {
            misses_seen = vocal_misses;
            if (getenv("SIM_MISSES"))
            {
                printf("  miss at tick %u, note %d, row %d; clips:", eng_tick, eng_sing_note, eng_sing_row);
                for (unsigned c = 0; c < CLIPS; c++)
                    printf(" %04x/%u", clips[c].id, clips[c].size);
                printf("\n");
            }
        }
    }
}

static const char *const BTN[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};
static int find(const char *const *names, int n, const char *s)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(names[i], s))
            return i;
    return -1;
}

static char outdir[512];
static void shot(const char *name)
{
    char path[640];
    snprintf(path, sizeof path, "%s/%s.ppm", outdir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6 240 240 255\n");
    for (unsigned i = 0; i < 240 * 240; i++) {
        uint16_t c = screen[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static int failures;
static void fail(int line, const char *what)
{
    printf("  FAIL line %d: %s\n", line, what);
    failures++;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: sim SCRIPT OUTDIR\n");
        return 2;
    }
    snprintf(outdir, sizeof outdir, "%s", argv[2]);
    FILE *script = fopen(argv[1], "r");
    if (!script) {
        perror(argv[1]);
        return 2;
    }
    char path[640];
    snprintf(path, sizeof path, "%s/audio.wav", outdir);
    wav = fopen(path, "wb");
    if (!wav) {
        perror(path);
        return 2;
    }
    wav_header(0);
    /* a script that starts with "foreign": the slots hold another firmware's data at power-on */
    char first[32] = "";
    if (fgets(first, sizeof first, script) && !strncmp(first, "foreign", 7)) {
        static uint8_t junk[sizeof song];
        memset(junk, 0xA5, sizeof junk);
        for (unsigned i = 0; i < SONG_SLOTS; i++)
            plat_store_save(OBJ_BANK0 + i, junk, sizeof junk);
    }
    /* SIM_SONGS: a file listing song files (raw slot bytes), one for each slot from S1, in place at power-on */
    if (getenv("SIM_SONGS")) {
        FILE *list = fopen(getenv("SIM_SONGS"), "r");
        char name[640];
        for (unsigned i = 0; list && i < SONG_SLOTS && fgets(name, sizeof name, list); i++) {
            name[strcspn(name, "\n")] = 0;
            FILE *f = fopen(name, "rb");
            static uint8_t buf[sizeof song];
            if (f && fread(buf, 1, sizeof buf, f) == sizeof buf)
                plat_store_save(OBJ_BANK0 + i, buf, sizeof buf);
            if (f)
                fclose(f);
        }
        if (list) {
            uint32_t boot[2] = {0x44584200u, SONG_MAGIC};   /* ours, S1 last used: the first boot does not wipe them */
            plat_store_save(OBJ_SET, boot, sizeof boot);
            fclose(list);
        }
    }
    rewind(script);
    if (host_boot() != 0) {
        fprintf(stderr, "speech engine did not start\n");
        return 1;
    }
    if (getenv("SIM_HOLD"))
        hold_ms = (unsigned)atoi(getenv("SIM_HOLD"));
    if (getenv("SIM_PACE"))
        pace = (unsigned)atoi(getenv("SIM_PACE"));
    if (getenv("SIM_TURN"))
        turn_ms = (unsigned)atoi(getenv("SIM_TURN"));
    if (getenv("SIM_VIDEO"))
        video = fopen(getenv("SIM_VIDEO"), "wb");
    if (getenv("SIM_STATE"))
        state = fopen(getenv("SIM_STATE"), "w");
    host_settled();
    run(50);
    char line[512];
    int ln = 0;
    while (fgets(line, sizeof line, script)) {
        ln++;
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
        char cmd[32] = "", a[256] = "", b[256] = "";
        if (line[0] == '#' || !line[0])
            continue;
        int n = sscanf(line, "%31s %255s %255[^\n]", cmd, a, b);
        if (n < 1)
            continue;
        if (!strcmp(cmd, "tap") || !strcmp(cmd, "down") || !strcmp(cmd, "up")) {
            int k = find(BTN, NB, a);
            if (k < 0) {
                fail(ln, "no such button");
                continue;
            }
            /* a person's pace, as tools/device_run.py: the press (SIM_HOLD ms, 120 by default; a quick real
             * tap is 30-100), then a pause to let the device finish; a song takes a while to write */
            if (cmd[0] != 'u') {
                buttons |= 1u << k;
                run(n > 2 ? (uint32_t)atoi(b) : pace ? pace / 2 : hold_ms);
            }
            if (cmd[0] != 'd') {
                buttons &= ~(1u << k);
                run(pace ? pace - pace / 2 : 250);
                if (k == 7 && !pace)
                    run(2500);
            }
        } else if (!strcmp(cmd, "key") || !strcmp(cmd, "keydown") || !strcmp(cmd, "keyup")) {
            int k = atoi(a);
            if (strcmp(cmd, "keyup")) {
                keys |= 1u << k;
                run(!strcmp(cmd, "key") ? (getenv("SIM_HOLD") ? hold_ms : n > 2 ? (uint32_t)atoi(b) : pace ? pace / 2 : 120)
                                        : pace ? 10 : 60);
            }
            if (strcmp(cmd, "keydown")) {
                keys &= ~(1u << k);
                run(pace ? (!strcmp(cmd, "key") ? pace / 2 : 10) : 200);
            }
        } else if (!strcmp(cmd, "turn")) {
            int e = find(ENC, NE, a);
            if (e < 0) {
                fail(ln, "no such encoder");
                continue;
            }
            int d = atoi(b), s = d < 0 ? -1 : 1;
            for (int i = 0; i != d; i += s) {
                enc[e] += s;
                enc_pos[e] += s;
                run(pace ? turn_ms : 30);
            }
            run(pace ? pace : 300);
        } else if (!strcmp(cmd, "turns")) {
            /* turns ENC N ENC N ...: several knobs at once, a detent of each in turn (two hands, or a sweep) */
            char names[8][16];
            int amount[8], left[8], k = 0, used = 0;
            const char *p = line + 6;
            while (k < 8 && sscanf(p, "%15s %d%n", names[k], &amount[k], &used) == 2) {
                left[k] = amount[k] < 0 ? -amount[k] : amount[k];
                p += used;
                k++;
            }
            for (int busy = 1; busy;) {
                busy = 0;
                for (int j = 0; j < k; j++) {
                    int e = find(ENC, NE, names[j]);
                    if (e < 0 || !left[j])
                        continue;
                    enc[e] += amount[j] < 0 ? -1 : 1;
                    enc_pos[e] += amount[j] < 0 ? -1 : 1;
                    left[j]--;
                    busy = 1;
                }
                if (busy)
                    run(pace ? turn_ms : 30);
            }
            run(pace ? pace : 300);
        } else if (!strcmp(cmd, "pace")) {
            pace = (unsigned)atoi(a);                  /* a press's time from here on (0: the tests' own) */
        } else if (!strcmp(cmd, "turnspeed")) {
            turn_ms = (unsigned)atoi(a);               /* a detent's time from here on */
        } else if (!strcmp(cmd, "type")) {
            const char *t = line + 5;
            for (; *t; t++) {
                char c = (char)(*t >= 'A' && *t <= 'Z' ? *t + 32 : *t);
                const char *p = strchr(TEXT_KEYS, c);
                if (!p)
                    continue;
                keys |= 1u << (p - TEXT_KEYS);
                run(getenv("SIM_HOLD") ? hold_ms : pace ? pace / 2 : 70);
                keys &= ~(1u << (p - TEXT_KEYS));
                run(pace ? pace - pace / 2 : 70);
            }
        } else if (!strcmp(cmd, "waitstep")) {
            /* wait until the song plays step N of its bar: playing in time, as a player would */
            unsigned want = (unsigned)atoi(a), last = 1u << 30;
            for (unsigned guard = 0; guard < 30000; guard++) {
                unsigned now_step = eng_playing ? eng_tick % song.steps : 999;
                if (now_step == want && last != want)
                    break;
                last = now_step;
                run(1);
            }
        } else if (!strcmp(cmd, "waittick")) {
            /* wait until the song plays tick N (sixteenths from its start); just past it (up to 2 bars): go on */
            unsigned want = (unsigned)atoi(a);
            for (unsigned guard = 0; guard < 60000; guard++) {
                if (eng_playing && eng_tick >= want && eng_tick - want < 32u)
                    break;
                run(1);
            }
        } else if (!strcmp(cmd, "rows")) {
            for (unsigned r = 0; r < song.rows; r++)
                printf("ROW %u at %u: \"%s\" (%u notes, span %u)\n", r, song.row[r].at, song.line[song.row[r].line].text,
                       song.line[song.row[r].line].count, line_span(&song, song.row[r].line));
        } else if (!strcmp(cmd, "mark")) {
            printf("MARK %u %s\n", (unsigned)sim_ms, line + 5);   /* a caption for the trailer's editor */
        } else if (!strcmp(cmd, "dumpslot")) {
            /* a slot's bytes into a file: dumpslot N PATH */
            static uint8_t buf[sizeof song];
            int got = plat_store_load(OBJ_BANK0 + (unsigned)atoi(a), buf, sizeof buf);
            FILE *f = fopen(b, "wb");
            if (f && got == (int)sizeof buf)
                fwrite(buf, 1, sizeof buf, f);
            if (f)
                fclose(f);
        } else if (!strcmp(cmd, "foreign")) {
            /* handled before power-on */
        } else if (!strcmp(cmd, "wait")) {
            run((uint32_t)atoi(a));
        } else if (!strcmp(cmd, "intro") || !strcmp(cmd, "introdone")) {
            /* intro TEXT: Mister Wiggles is given TEXT to sing on Sing (a song of its own, a syllable a note, a tune
             * that rises and falls), the song before kept; introdone puts the song before back */
            static song_t before;
            if (!strcmp(cmd, "intro")) {
                char text[256];
                snprintf(text, sizeof text, "%s", line + 6);
                memcpy(&before, &song, sizeof song);
                song_blank(&song);
                song.bpm = 112;
                static const int8_t TUNE[8] = {0, 2, 4, 7, 4, 2, 4, 9};
                unsigned at = 4, l = 0;
                for (char *part = strtok(text, "|"); part && l < 4; part = strtok(0, "|"), l++) {
                    while (*part == ' ')
                        part++;
                    line_set_text(&song, l, part);
                    int n = vocal_syllables(part, 0);
                    for (int i = 0; i < n && i < 64; i++) {
                        int last = i + 1 == n;
                        note_t x = {(uint16_t)(i * 2), (uint8_t)(last ? 8 : 2), (uint8_t)(60 + TUNE[i % 8] - (last ? 5 : 0)),
                                    (uint8_t)i, 0};
                        note_insert(&song, l, (unsigned)i, &x);
                    }
                    row_insert(&song, at, l);
                    at += (unsigned)(n * 2 + 12 + 3) / 4u * 4u;
                }
                song_loaded();
                ui.page = P_SING;
            } else {
                memcpy(&song, &before, sizeof song);
                song_loaded();
            }
        } else if (!strcmp(cmd, "poem")) {
            /* poem CORE N: the N-th poem (by seed, the topics in turn) on that core whose lines in bars 5 to 12 say two
             * words of it, each told a different way: made, the song now; "POEM core|way|band" printed */
            int core = atoi(a), want = atoi(b), found = -1;
            uint32_t ways = 0;                        /* (each found a different way of telling it) */
            for (uint32_t seed = 1; seed < 20000 && found < want; seed++) {
                if (poem_make(&song, seed * 2654435761u, seed % POEM_TOPICS) || poem.core != core)
                    continue;
                int says = 0;
                for (unsigned r = 0; r < song.rows; r++) {
                    if (song.row[r].at < 64 || song.row[r].at >= 192)
                        continue;
                    for (unsigned w = 0; w < POEM_GROUP[PG_CORE + core][1]; w++) {
                        char word[40];
                        poem_pick(PG_CORE + core, w, word, sizeof word);
                        says += strcasestr(song.line[song.row[r].line].text, word) != 0;
                    }
                }
                if (says >= 2 && !(ways >> poem.frame & 1u))
                    found++, ways |= 1u << poem.frame;
            }
            song_loaded();
            ui.slot = -1;
            ui.page = P_SING;
            char way[24], name[24];
            poem_pick(PG_FRAME_NAMES, poem.frame, way, sizeof way);
            poem_pick(PG_CORE_NAMES, poem.core, name, sizeof name);
            printf("POEM %s|%s|%s\n", name, way, SETS[poem.set].name);
        } else if (!strcmp(cmd, "lyrics")) {
            /* the song's words, a row a line, with where each starts (bar.beat) */
            printf("  \"%s\", %u BPM, %u bars, %u rows, %u notes\n", song.name, song.bpm, song.bars, song.rows, song.notes);
            for (unsigned r = 0; r < song.rows; r++) {
                unsigned end = song.row[r].at + line_span(&song, song.row[r].line);
                printf("  %3u.%u  %s%s\n", song.row[r].at / song.steps + 1, song.row[r].at % song.steps / 4 + 1,
                       song.line[song.row[r].line].text, r + 1 < song.rows && end > song.row[r + 1].at ? "   OVERLAPS THE NEXT" : "");
            }
        } else if (!strcmp(cmd, "shot")) {
            ui.frame_at = 0;
            ui.last_input = now;
            run(1);
            shot(a);
        } else if (!strcmp(cmd, "dumpclips")) {
            for (unsigned c = 0; c < CLIPS; c++) {
                if (clips[c].id == CLIP_NONE)
                    continue;
                char p[700];
                snprintf(p, sizeof p, "%s/clip_%04x.raw", outdir, clips[c].id);
                FILE *f = fopen(p, "wb");
                fwrite(clip_arena + clips[c].offset, 2, clips[c].length, f);
                fclose(f);
                printf("  clip %04x: %u samples, source %u, looped %u, loop %u..%u, word %u+%u\n", clips[c].id, clips[c].length,
                       clips[c].source_pitch, clips[c].looped, clips[c].loop_start, clips[c].loop_end, clips[c].word_at, clips[c].word_len);
            }
        } else if (!strcmp(cmd, "solo")) {
            int t = find(TRACK_NAME, NTRACKS, a);
            for (int k = 0; k < NTRACKS; k++)
                song.track[k].mute = (uint8_t)(t >= 0 && k != t);
        } else if (!strcmp(cmd, "echo")) {
            printf("  %s\n", line + 5);
        } else if (!strcmp(cmd, "say")) {
            /* say TEXT: the speech engine makes it (tools/check_poem.py: every word, under the sanitizers) */
            fprintf(stderr, "say %s\n", line + 4);
            host_wait_idle(run);
            for (int w = 0; w < 200 && dx_speech_active(); w++)
                run(10);
            uint16_t art[64];
            int got = dx_speech_make_into(0, line + 4, 0, dx_speech_cache.samples, DX_SPEECH_LONG_SAMPLES);
            got = got < 0 ? got : dx_speech_articulation(line + 4, art);
            if (got < 0 || vocal_syllables(line + 4, 0) < 1) {
                char msg[300];
                snprintf(msg, sizeof msg, "cannot say \"%s\" (%d)", line + 4, got);
                fail(ln, msg);
            }
        } else if (!strcmp(cmd, "expect")) {
            char msg[600];
            host_wait_idle(run);                      /* the device answers when its main loop is free */
            if (!strcmp(a, "page") && strcmp(PAGE_NAME[ui.page], b)) {
                snprintf(msg, sizeof msg, "page %s, expected %s", PAGE_NAME[ui.page], b);
                fail(ln, msg);
            } else if (!strcmp(a, "track") && strcmp(TRACK_NAME[ui.track], b)) {
                snprintf(msg, sizeof msg, "track %s, expected %s", TRACK_NAME[ui.track], b);
                fail(ln, msg);
            } else if (!strcmp(a, "rows") && song.rows != atoi(b)) {
                snprintf(msg, sizeof msg, "%u rows, expected %s", song.rows, b);
                fail(ln, msg);
            } else if (!strcmp(a, "notes") && song.notes != atoi(b)) {
                snprintf(msg, sizeof msg, "%u notes, expected %s", song.notes, b);
                fail(ln, msg);
            } else if (!strcmp(a, "playing") && eng_playing != atoi(b)) {
                snprintf(msg, sizeof msg, "playing %u, expected %s", eng_playing, b);
                fail(ln, msg);
            } else if (!strcmp(a, "bpm") && song.bpm != atoi(b)) {
                snprintf(msg, sizeof msg, "bpm %u, expected %s", song.bpm, b);
                fail(ln, msg);
            } else if (!strcmp(a, "steps") && song.steps != atoi(b)) {
                snprintf(msg, sizeof msg, "steps %u, expected %s", song.steps, b);
                fail(ln, msg);
            } else if (!strcmp(a, "slot") && ui.slot != atoi(b)) {
                snprintf(msg, sizeof msg, "slot %d, expected %s", ui.slot, b);
                fail(ln, msg);
            } else if (!strcmp(a, "msg")) {
                char m[64];
                snprintf(m, sizeof m, "%s %s", ui.msg[0], ui.msg[1]);
                if (!strstr(m, b)) {
                    snprintf(msg, sizeof msg, "message \"%s\", expected \"%s\"", m, b);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "line_text")) {
                int r = atoi(b);
                const char *want = strchr(b, ' ');
                want = want ? want + 1 : "";
                if (r >= song.rows || strcmp(song.line[song.row[r].line].text, want)) {
                    snprintf(msg, sizeof msg, "row %d's line \"%s\", expected \"%s\"", r,
                             r < song.rows ? song.line[song.row[r].line].text : "(none)", want);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "sung")) {
                /* sung since the last `expect sung` (or the start): a clip sounding, ms */
                static unsigned sung_before;
                unsigned now_sung = sung_frames - sung_before;
                sung_before = sung_frames;
                if ((now_sung > 0) != (unsigned)atoi(b)) {
                    snprintf(msg, sizeof msg, "sung for %u ms, expected %s", now_sung, atoi(b) ? "some" : "none");
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "misses") && vocal_misses > (unsigned)atoi(b)) {
                snprintf(msg, sizeof msg, "%u voice misses, at most %s", vocal_misses, b);
                fail(ln, msg);
            } else if (!strcmp(a, "peak_min") && 20.0 * log10(peak + 1e-9) < atof(b)) {
                snprintf(msg, sizeof msg, "peak %.1f dBFS, expected at least %s", 20.0 * log10(peak + 1e-9), b);
                fail(ln, msg);
            } else if (!strcmp(a, "key_led")) {
                /* expect key_led KEY 0|1: the light under key KEY now */
                int key = atoi(b), want = atoi(strchr(b, ' ') ? strchr(b, ' ') + 1 : "1");
                if (((host_key_leds() >> key) & 1u) != (unsigned)want) {
                    snprintf(msg, sizeof msg, "key %d light is %d, expected %d", key, !want, want);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "button_led")) {
                /* expect button_led BUTTON 0|1: that button's light now */
                char nm[16] = "";
                int want = 1;
                sscanf(b, "%15s %d", nm, &want);
                int bt = find(BTN, NB, nm);
                if (bt < 0 || ((host_button_leds() >> bt) & 1u) != (unsigned)want) {
                    snprintf(msg, sizeof msg, "%s light is %d, expected %d", nm, bt < 0 ? -1 : !want, want);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "punch")) {
                /* expect punch W 0|1: the effect on white key W sounding */
                int w = 0, want = 1;
                sscanf(b, "%d %d", &w, &want);
                if (((eng_punch >> w) & 1u) != (unsigned)want) {
                    snprintf(msg, sizeof msg, "punch-in %d (%s) is %d, expected %d", w, PUNCH_NAME[w % PU_N], !want, want);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "key_blinks")) {
                /* expect key_blinks KEY: the light under KEY goes on and off within a second */
                int key = atoi(b), on = 0, off = 0;
                for (int i = 0; i < 40; i++) {
                    run(25);
                    if ((host_key_leds() >> key) & 1u)
                        on = 1;
                    else
                        off = 1;
                }
                if (!on || !off) {
                    snprintf(msg, sizeof msg, "key %d light does not blink", key);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "empty") && store_len[OBJ_BANK0 + atoi(b)]) {
                snprintf(msg, sizeof msg, "slot S%d holds something", atoi(b) + 1);
                fail(ln, msg);
            } else if (!strcmp(a, "saved") && (!store_len[OBJ_BANK0 + atoi(b)])) {
                snprintf(msg, sizeof msg, "slot S%d not saved", atoi(b) + 1);
                fail(ln, msg);
            } else if (!strcmp(a, "tune_max")) {
                /* expect tune_max <pitch>: the line given your tune (SLAMJAM) sings from G2 up to that pitch */
                int r = ui.tune_for, lo = 999, hi = -1;
                if (r >= 0 && r < song.rows) {
                    const line_t *L = &song.line[song.row[r].line];
                    for (unsigned i = 0; i < L->count; i++) {
                        int q = song.note[L->first + i].pitch;
                        lo = q < lo ? q : lo;
                        hi = q > hi ? q : hi;
                    }
                }
                if (hi < 0 || lo < 43 || hi > atoi(b)) {
                    snprintf(msg, sizeof msg, "tune %d..%d, expected 43..%s", lo, hi, b);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "pitch")) {
                /* expect pitch <row> <note> <pitch>: a note of the line a row sings */
                int r = 0, i = 0, want = 0;
                sscanf(b, "%d %d %d", &r, &i, &want);
                const line_t *L = &song.line[song.row[r % SONG_ROWS].line];
                int got = i < L->count ? song.note[L->first + i].pitch : -1;
                if (got != want) {
                    snprintf(msg, sizeof msg, "row %d note %d pitch %d, expected %d", r, i, got, want);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "step")) {
                /* expect step <track> <step 1..16> <value>: the Phrase cursor's bar of that track's chain; Keys/Bass the step's
                 * first note (0: empty), Drums its lanes (a bit each) */
                char tn[16];
                int st = 0, want = 0, t;
                sscanf(b, "%15s %d %d", tn, &st, &want);
                t = find(TRACK_NAME, NTRACKS, tn);
                unsigned m = song.lane[t > 0 ? t : 1][ui.bar] & 31u;
                static const step_t none;
                const step_t *x = m < SONG_PATTERNS ? &song.pattern[m].step[(t > 0 ? t : 1) - 1][(st - 1) & 15] : &none;
                int got = t == T_DRUMS ? x->drums : x->count ? x->note[0] : 0;
                if (t < 1 || got != want) {
                    snprintf(msg, sizeof msg, "%s step %d is %d, expected %d", tn, st, got, want);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "phrases") || !strcmp(a, "phrase_len")) {
                /* expect phrases <track> <n>: its chain's phrases; expect phrase_len <track> <i> <bars>: phrase i's bars */
                char tn[16];
                int x = 0, y = 0, got;
                sscanf(b, "%15s %d %d", tn, &x, &y);
                int t = find(TRACK_NAME, NTRACKS, tn), len = !strcmp(a, "phrase_len");
                got = t < 0 ? -1 : len ? (int)lane_len(&song, (unsigned)t, (unsigned)x) : (int)lane_count(&song, (unsigned)t);
                if (got != (len ? y : x)) {
                    snprintf(msg, sizeof msg, "%s %s is %d, expected %d", tn, a, got, len ? y : x);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "nudge")) {
                /* expect nudge <track> <step 1..16> <sixths>: the Phrase cursor's bar's step (Voice: the note there) */
                char tn[16];
                int st = 0, want = 0, got = 99;
                sscanf(b, "%15s %d %d", tn, &st, &want);
                int t = find(TRACK_NAME, NTRACKS, tn);
                if (t == T_VOICE) {
                    unsigned r;
                    int n, at = note_over((unsigned)ui.bar * song.steps + (unsigned)(st - 1), 0, &r, &n);
                    got = at >= 0 ? note_nudge(&song.note[n]) : 99;
                } else if (t > 0 && (song.lane[t][ui.bar] & 31u) < SONG_PATTERNS)
                    got = song.pattern[song.lane[t][ui.bar] & 31u].step[t - 1][(st - 1) & 15].nudge;
                if (got != want) {
                    snprintf(msg, sizeof msg, "%s step %d nudge %d, expected %d", tn, st, got, want);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "cards")) {
                /* every help card, on each screen it is held on, fits its box (CARD_H tall) */
                static const unsigned V[][3] = {
                    {B_HOME, P_SING, T_VOICE},   {B_SEQ, P_ARRANGE, T_VOICE}, {B_SEQ, P_PHRASE, T_VOICE},
                    {B_SEQ, P_PHRASE, T_KEYS},   {B_SEQ, P_PHRASE, T_DRUMS},  {B_EDIT, P_SOUND, T_VOICE},
                    {B_EDIT, P_SOUND, T_KEYS},   {B_EDIT, P_SOUND, T_DRUMS},  {B_ENV, P_ENV, T_VOICE},
                    {B_ENV, P_ENV, T_KEYS},      {B_ENV, P_ENV, T_DRUMS},     {B_ARP, P_ARP, T_KEYS},
                    {B_FX, P_EFFECTS, T_VOICE},  {B_LFO, P_MIXER, T_VOICE},   {B_GLO, P_SETUP, T_VOICE},
                    {B_REC, P_SING, T_VOICE},    {B_PLAY, P_SING, T_VOICE},   {B_HOME, P_SING, T_VOICE + 8},
                    {B_FX, P_EFFECTS, T_VOICE + 8}};          /* (+ 8: in poetry mode) */
                unsigned page = ui.page, track = ui.track, poetry = ui.poetry;
                for (unsigned i = 0; i < sizeof V / sizeof V[0]; i++) {
                    const char *title;
                    ui.page = (uint8_t)V[i][1];
                    ui.track = (uint8_t)(V[i][2] & 7u);
                    ui.poetry = (uint8_t)(V[i][2] >> 3);
                    ui.card = (uint8_t)(V[i][0] + 1u);
                    card_h = -1;
                    for (int tries = 0; tries < 20 && card_h < 0; tries++) {   /* until the card is drawn */
                        ui.frame_at = 0;
                        ui.last_input = now;
                        run(40);
                    }
                    char full[320];
                    memcpy(full, card_buf, sizeof full);
                    card_text(V[i][0], &title);
                    if (getenv("SIM_CARDS"))
                        fprintf(stderr, "card %-6s %-18s %3d of %d px%s%s\n", BUTTON_CAP[V[i][0]], title, card_h, CARD_H,
                                card_h > CARD_H ? ": " : "", card_h > CARD_H ? full : "");
                    if (card_h > CARD_H || g_tw(&FONT_S, title) > 208) {
                        snprintf(msg, sizeof msg, "the %s card (%s) is %d px, the box has %d", BUTTON_CAP[V[i][0]], title, card_h, CARD_H);
                        fail(ln, msg);
                    }
                }
                ui.card = 0;
                ui.poetry = (uint8_t)poetry;
                ui.page = (uint8_t)page;
                ui.track = (uint8_t)track;
            } else if (!strcmp(a, "sing_pitch")) {
                /* expect sing_pitch LO HI: the sung pitch now (semitones, glides and vibrato in) between them */
                float lo = 0, hi = 0;
                sscanf(b, "%f %f", &lo, &hi);
                if (eng_sing_pitch < lo || eng_sing_pitch > hi) {
                    snprintf(msg, sizeof msg, "sung pitch %.1f, expected %.0f to %.0f", eng_sing_pitch, lo, hi);
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "lines_clear")) {
                /* no row's words still being said when the next row starts */
                for (unsigned r = 0; r + 1 < song.rows; r++)
                    if (song.row[r].at + line_span(&song, song.row[r].line) > song.row[r + 1].at) {
                        snprintf(msg, sizeof msg, "row %u (\"%s\") runs into the next", r, song.line[song.row[r].line].text);
                        fail(ln, msg);
                        break;
                    }
            } else if (!strcmp(a, "mode") && ui.poetry != !strcmp(b, "slamjam")) {
                snprintf(msg, sizeof msg, "mode %s, expected %s", ui.poetry ? "slamjam" : "karaoke", b);
                fail(ln, msg);
            } else if (!strcmp(a, "layer") && ui.sub[ui.page] != atoi(b)) {
                snprintf(msg, sizeof msg, "layer %u, expected %s", ui.sub[ui.page], b);
                fail(ln, msg);
            } else if (!strcmp(a, "heard")) {
                /* sound (above -50 dBFS) since the last `expect heard` (or the start): 1, or silence: 0 */
                int on = heard > 0.003;
                heard = 0;
                if (on != atoi(b)) {
                    snprintf(msg, sizeof msg, "%s, expected %s", on ? "sound" : "silence", atoi(b) ? "sound" : "silence");
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "syllables")) {
                /* expect syllables <row> <each note's syllable, a + after a held one>: "0 1 2 2+" */
                int r = atoi(b);
                const char *want = strchr(b, ' ');
                char got[200] = "";
                for (unsigned i = 0; r < song.rows && i < song.line[song.row[r].line].count && strlen(got) < 180; i++) {
                    const note_t *n = &song.note[song.line[song.row[r].line].first + i];
                    snprintf(got + strlen(got), 12, "%s%u%s", i ? " " : "", n->syllable, note_tie(n) ? "+" : "");
                }
                if (strcmp(got, want ? want + 1 : "")) {
                    snprintf(msg, sizeof msg, "row %d's syllables \"%s\", expected \"%s\"", r, got, want ? want + 1 : "");
                    fail(ln, msg);
                }
            } else if (!strcmp(a, "buildmask") && eng_build_mask != atoi(b)) {
                snprintf(msg, sizeof msg, "build took tracks %u, expected %s", eng_build_mask, b);
                fail(ln, msg);
            } else if (!strcmp(a, "dirty") && ui.dirty != atoi(b)) {
                snprintf(msg, sizeof msg, "unsaved %u, expected %s", ui.dirty, b);
                fail(ln, msg);
            }
        } else
            fail(ln, "unknown command");
    }
    wav_header(frames_out);
    fclose(wav);
    if (video)
        fclose(video);
    if (state)
        fclose(state);
    printf("%s: %.1f s of audio, peak %.1f dBFS, rms %.1f dBFS, sung %u ms, misses %u, drops %u, %d failures\n", argv[1],
           frames_out / 44100.0, 20.0 * log10(peak + 1e-9), 10.0 * log10(sum_sq / (frames_out * 2.0 + 1) + 1e-12),
           sung_frames, vocal_misses, eng_queue_drops, failures);
    return failures ? 1 : 0;
}
