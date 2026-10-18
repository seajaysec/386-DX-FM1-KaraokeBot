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
 *                          bpm, sung (a clip played), misses MAX, peak_min DB, saved S
 *   echo TEXT */
#include <math.h>
#include "plat_host.c"


/* ---- the audio, to a WAV ---- */
static FILE *wav;
static uint32_t frames_out, audio_due;
static double peak, sum_sq;
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
static unsigned sung_frames, hold_ms = 120, pace;   /* pace: SIM_PACE ms an action (a trailer's brisk hands) */
static FILE *video;                                  /* SIM_VIDEO: the screen, 30 frames a second, raw RGB */
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
    if (getenv("SIM_VIDEO"))
        video = fopen(getenv("SIM_VIDEO"), "wb");
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
                run(pace ? pace / 4 : 30);
            }
            run(pace ? pace : 300);
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
            /* wait until the song plays tick N (sixteenths from its start) */
            unsigned want = (unsigned)atoi(a);
            for (unsigned guard = 0; guard < 60000; guard++) {
                if (eng_playing && eng_tick == want)
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
            } else if (!strcmp(a, "sung") && (sung_frames > 0) != atoi(b)) {
                snprintf(msg, sizeof msg, "sung for %u ms, expected %s", sung_frames, atoi(b) ? "some" : "none");
                fail(ln, msg);
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
                /* expect step <track> <step 1..16> <value>: the cursor bar's pattern; Keys/Bass the step's
                 * first note (0: empty), Drums its lanes (a bit each) */
                char tn[16];
                int st = 0, want = 0, t;
                sscanf(b, "%15s %d %d", tn, &st, &want);
                t = find(TRACK_NAME, NTRACKS, tn);
                const step_t *x = &song.pattern[song.chain[ui.bar] % SONG_PATTERNS].step[(t > 0 ? t : 1) - 1][(st - 1) & 15];
                int got = t == T_DRUMS ? x->drums : x->count ? x->note[0] : 0;
                if (t < 1 || got != want) {
                    snprintf(msg, sizeof msg, "%s step %d is %d, expected %d", tn, st, got, want);
                    fail(ln, msg);
                }
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
    printf("%s: %.1f s of audio, peak %.1f dBFS, rms %.1f dBFS, sung %u ms, misses %u, drops %u, %d failures\n", argv[1],
           frames_out / 44100.0, 20.0 * log10(peak + 1e-9), 10.0 * log10(sum_sq / (frames_out * 2.0 + 1) + 1e-12),
           sung_frames, vocal_misses, eng_queue_drops, failures);
    return failures ? 1 : 0;
}
