/* SPDX-License-Identifier: GPL-3.0-only */
/* The stage: wherever Mister Wiggles is out (Sing, the Voice's Sound and Envelope, FX's karaoke room). He stands on
 * the bottom bar in front of a wall of CRT cells playing a slow picture, a monitor speaker either side of him.
 * Every effect and every knob shows somewhere on it, so nothing needs a number:
 *
 *   FILTER     the wall's hour: night blues to the left, the evening's plums and pinks in the middle, a bright
 *              day to the right; rainbow stars twinkle on it at night
 *   REVERB     a haze over the wall's foot, its cells glowing into each other
 *   TONE       the wall warmer (dark) or cooler (bright)
 *   DELAY      each burst of notes echoes, one echo for each repeat, at the delay's time
 *   DRIVE      the speakers glow hot and spit sparks, the wall runs hot
 *   PUMP       the speaker cones push and the wall dips on each kick, measured
 *   BODY       his shadow's weight; BREATH, puffs as he sings; EDGE, laser beams; OCTAVE, the notes' size
 *   GLIDE      the notes' trails; VIBRATO and RATE, his wobble and the wall's; the voice's REVERB, how long notes linger
 *   the bank   in the karaoke room, the speakers' colour says which bank the knobs are on
 *   a turn     in the karaoke room every knob turned throws its own picture: REVERB a ring, TONE a colour wash,
 *              DELAY notes in a row, REPEATS rings in rings, DRIVE sparks, SQUEEZE stars rushing in, PUMP rings off
 *              the speakers, FILTER a band of light sweeping down or up, BODY dust at his feet, BREATH puffs, EDGE
 *              lasers, OCTAVE big notes leaping, GLIDE a comet, VIBRATO the wall rippling, RATE a ring of stars,
 *              THROAT bubbles from his mouth
 *
 *   lo-pass the wall's top goes dark, deep blue; hi-pass only the top lit, pale, champagne bubbles fizzing;
 *   GAMEBOIII everything in two greens with scanlines; ring mod rings from him and from the speakers over a ringed
 *   wall; chop the stage blacked out whenever the gate shuts and lit in strobe colours when it opens, with
 *   lasers, more beams the faster; brrr the picture freezing and tearing into strips at each repeat, wilder the
 *   faster; build the wall filling up from his feet, flashing on every snare, him lifting and trembling, sparks
 *   streaming up, then a white flash when it is let go; tape off the wall switching off like a tube and him
 *   falling asleep; lo-fi and crush the wall in bigger cells, the scene in blocks
 *
 * Several effects held mix their pictures. Everything is read once a frame (stage_frame), so the three bands
 * draw one moment. Part of the unity build (ui_pages.h). */

#define ST_TOP 23                     /* the wall's rows: under the top bar, down to his feet */
#define ST_WALL 160
#define ST_FEET 160                   /* where he stands: the bottom bar's top */
#define ST_NP 28                      /* specks */
enum { PK_NONE, PK_NOTE, PK_STAR, PK_BUBBLE, PK_SPARK, PK_PUFF, PK_Z, PK_RING };

static const uint8_t ST_NOTE[8] = {0x06, 0x05, 0x04, 0x04, 0x1C, 0x3C, 0x3C, 0x18};   /* an eighth note, 6 wide */
static const uint8_t ST_STAR[7] = {0x08, 0x08, 0x7F, 0x3E, 0x1C, 0x36, 0x41};         /* a star, 7 wide */

static struct {
    struct {
        float x, y, vx, vy;
        uint16_t life, age;           /* ms */
        uint8_t kind, hue;
    } p[ST_NP];
    uint32_t frame, at, seed, rep_was, tape_at, tick_at, hop_at;
    uint16_t tick_was;
    uint32_t punch;
    float clock, hop_clock, vib;      /* the stage's own time (ms): tape off winds it down */
    uint32_t drop_at;                 /* build let go: the flash */
    int lift;                         /* build's riser, 0..127 */
    /* this frame */
    float t, rate;                    /* the clock in seconds; how fast it runs */
    uint16_t pal[4];                  /* the wall's colours */
    int night, day, haze, drive, duck, gate_shut, sway, hop, cx, echoes, lag, body, sleep, jit, strobe;
    uint32_t burst_at;                /* the last burst of notes; its echoes so far; its colour */
    int burst_echo, burst_hue, shut_was;
    uint8_t turn_seen, turn_k;        /* the karaoke room's last knob turn: which, which way, when */
    int8_t turn_d;
    uint32_t turn_at, now;
    int tears, tear_seed, cell, off_h, off_w, beat, edge, size, glide;
    face_t face;
} st;

/* a colour's lightness, 0..63 */
static unsigned st_luma(uint16_t c) { return ((c >> 11) * 2u * 77u + ((c >> 5) & 63u) * 150u + (c & 31u) * 2u * 29u) >> 8; }

static uint32_t st_rand(void)
{
    st.seed = st.seed * 1664525u + 1013904223u;
    return st.seed >> 8;
}
static int st_ri(int lo, int hi) { return lo + (int)(st_rand() % (unsigned)(hi - lo + 1)); }


/* a bright colour round the wheel, h of 256 */
static uint16_t st_hue(unsigned h)
{
    static const uint16_t W[6] = {RGB(255, 92, 120), RGB(255, 196, 70), RGB(150, 230, 90), RGB(70, 220, 210),
                                  RGB(110, 140, 255), RGB(220, 110, 240)};
    h &= 255u;
    return g_mix(W[h * 6u / 256u], W[(h * 6u / 256u + 1u) % 6u], (int)(h * 6u % 256u));
}

static void st_spawn(int kind, int x, int y, int vx, int vy, unsigned life, unsigned hue)
{
    for (unsigned i = 0; i < ST_NP; i++)
        if (st.p[i].kind == PK_NONE) {
            st.p[i].kind = (uint8_t)kind;
            st.p[i].x = (float)x;
            st.p[i].y = (float)y;
            st.p[i].vx = (float)vx;
            st.p[i].vy = (float)vy;
            st.p[i].life = (uint16_t)life;
            st.p[i].age = 0;
            st.p[i].hue = (uint8_t)hue;
            return;
        }
}

/* ---- once a frame: what the stage shows, the clock run and the specks moved ---- */
static void stage_frame(void)
{
    if (st.frame == ui.frame_at)
        return;
    st.frame = ui.frame_at;
    uint32_t now = plat_ms(), h = eng_punch;
    st.now = now;
    float dt = (float)(now - st.at);
    st.at = now;
    dt = dt > 100.0f ? 100.0f : dt;
    /* the clock: tape off winds it down to a stop */
    if ((h >> PU_TAPESTOP & 1u) && !(st.punch >> PU_TAPESTOP & 1u))
        st.tape_at = now;
    if (!(h >> PU_BUILD & 1u) && (st.punch >> PU_BUILD & 1u) && st.lift > 60)
        st.drop_at = now;                                     /* the drop */
    st.lift = eng_build;
    int tape = h >> PU_TAPESTOP & 1u;
    float rate = 1.0f;
    if (tape) {
        float k = 1.0f - (float)(now - st.tape_at) * (1.0f / 1200.0f);
        rate *= k < 0.0f ? 0.0f : k;
    }
    st.rate = rate;
    st.clock += dt * rate;
    if (st.clock > 600000.0f || st.clock < -600000.0f) {        /* kept small, for the sines */
        st.hop_clock -= st.clock;
        st.clock = 0.0f;
    }
    st.t = st.clock * 0.001f;
    st.sleep = tape && now - st.tape_at > 700u;
    /* him: the Voice drives the face as on Sing; the stage's clock runs his wobble, his hops and his blink */
    face_state(&st.face);
    st.face.poet = (uint8_t)(ui.poetry ? (ui.page == P_EFFECTS ? 2 : 1) : 0);
    face_t *f = &st.face;
    uint32_t hop_at = ui.played >= 0 ? ui.played_at : 0;
    if (f->singing && f->hop_at > hop_at)
        hop_at = f->hop_at;
    int fresh = hop_at && hop_at != st.hop_at;
    st.hop_at = hop_at;
    if (fresh)
        st.hop_clock = st.clock;
    float vib_hz = 3.0f + (float)song.voice[V_RATE] * 0.05f;
    st.vib += dt * rate * vib_hz * (FM_TWO_PI / 1000.0f);
    if (st.vib > FM_TWO_PI)
        st.vib -= FM_TWO_PI;
    if (st.vib < 0.0f)
        st.vib += FM_TWO_PI;
    f->vibrato = (float)song.voice[V_VIBRATO] * 0.006f * fm_sinf(st.vib);
    float since = st.clock - st.hop_clock;
    since = since < 0.0f ? -since : since;
    uint32_t d = st.hop_at ? (uint32_t)since : 100000u;
    st.hop = d < 360u ? (int)(14u * 4u * d * (360u - d) / (360u * 360u)) : 0;
    f->now = (uint32_t)(st.clock < 0.0f ? -st.clock : st.clock);
    f->hop_at = f->now - (d < 100000u ? d : 100000u);
    if (st.sleep) {                                   /* asleep: eyes shut, mouth still */
        f->singing = 0;
        f->now = 4400u;
        f->hop_at = 0;
    }
    /* the beat: a sway over two beats while the song plays, and a flash on each beat */
    if (eng_tick != st.tick_was) {
        st.tick_was = eng_tick;
        st.tick_at = now;
    }
    st.sway = 0;
    st.beat = 0;
    if (eng_playing && rate != 0.0f) {
        float frac = (float)(now - st.tick_at) * (float)song.bpm * (1.0f / 15000.0f);
        frac = frac > 1.0f ? 1.0f : frac;
        st.sway = (int)(8.0f * fm_sinf(((float)(eng_tick % 8u) + frac) * (FM_TWO_PI / 8.0f)));
        st.beat = eng_tick % 4u == 0 ? (int)(80.0f * (1.0f - frac)) : 0;
    }
    st.duck = eng_duck;
    st.gate_shut = (h & (1u << PU_CHOP8 | 1u << PU_CHOP16 | 1u << PU_CHOP32 | 1u << PU_CHOP64)) && eng_gate < 64;
    if (st.shut_was && !st.gate_shut)
        st.strobe += 43;                                     /* each opening of the gate a new strobe colour */
    st.shut_was = st.gate_shut;
    st.cx = 120 + st.sway;
    st.body = song.voice[V_BODY];
    st.edge = song.voice[V_EDGE];
    st.size = song.voice[V_OCTAVE];
    st.glide = song.voice[V_GLIDE];
    /* the knobs */
    const uint8_t *fx = song.fx;
    st.night = fx[FX_FILTER] < 47 ? (47 - fx[FX_FILTER]) * 256 / 47 : 0;
    st.day = fx[FX_FILTER] > 53 ? (fx[FX_FILTER] - 53) * 256 / 47 : 0;
    st.haze = fx[FX_REVERB_SIZE] * 120 / 100 + song.track[T_VOICE].reverb * 40 / 100;
    st.drive = fx[FX_DRIVE];
    st.echoes = song.track[T_VOICE].delay ? 1 + fx[FX_DELAY_FEEDBACK] * 3 / 101 : 0;
    {
        static const uint8_t EIGHTHS[6] = {2, 4, 6, 8, 12, 16};     /* the echo's time: eighths of a beat */
        unsigned ms = 7500u * EIGHTHS[fx[FX_DELAY_TIME] * 6u / 101u] / (song.bpm ? song.bpm : 100u);
        while (ms > 950u)
            ms /= 2u;
        st.lag = (int)ms;
    }
    /* the wall's colours: the hour from FILTER, warmed or cooled by TONE */
    static const uint16_t EVE[4] = {RGB(70, 34, 92), RGB(186, 60, 128), RGB(244, 150, 170), RGB(240, 176, 70)};
    static const uint16_t NITE[4] = {RGB(16, 18, 52), RGB(48, 40, 120), RGB(110, 64, 170), RGB(40, 110, 150)};
    static const uint16_t DAY[4] = {RGB(96, 170, 220), RGB(130, 214, 190), RGB(250, 224, 140), RGB(248, 180, 200)};
    int tone = (int)fx[FX_REVERB_TONE] - 50;
    for (int i = 0; i < 4; i++) {
        uint16_t c = g_mix(g_mix(EVE[i], NITE[i], st.night), DAY[i], st.day);
        c = tone < 0 ? g_mix(c, RGB(255, 170, 90), -tone * 2) : g_mix(c, RGB(120, 200, 255), tone * 2);
        if (!ui.poetry && (h >> PU_LOWPASS & 1u))
            c = g_mix(c, RGB(10, 30, 110), 150);
        if (!ui.poetry && (h >> PU_RING & 1u))
            c = g_mix(c, i & 1 ? RGB(180, 120, 230) : RGB(60, 50, 90), 160);
        if (st.drive)
            c = g_mix(c, RGB(255, 100, 60), st.drive * 70 / 100);
        if (ui.page != P_EFFECTS) {                     /* outside the karaoke room: the same, calmer (half greyed) */
            unsigned l = st_luma(c);
            c = g_mix(c, (uint16_t)((l >> 1) << 11 | l << 5 | (l >> 1)), 130);
        }
        st.pal[i] = g_mix(g_mix(c, K_WHITE, 40 + st.beat / 2), 0, st.duck * 90 / 127);    /* bright, flashing on the beat */
    }
    st.cell = h >> PU_CRUSH & 1u ? 4 : h >> PU_LOFI & 1u ? 2 : 1;
    /* brrr: at each repeat the picture tears into strips, more and wider the faster */
    st.tears = 0;
    for (unsigned i = PU_STUTTER8; i <= PU_STUTTER64 && !ui.poetry; i++)
        if (h >> i & 1u)
            st.tears = (int)(i - PU_STUTTER8) + 1;
    if (st.tears && eng_reps != st.rep_was)
        st.tear_seed = (int)st_rand();
    st.rep_was = eng_reps;
    st.jit = st.tears ? (int)((unsigned)st.tear_seed % (unsigned)(st.tears * 12 + 1)) - st.tears * 6
                      : st.lift > 20 ? (int)(st_rand() % (unsigned)(st.lift / 20 + 1)) - st.lift / 40 : 0;
    /* tape off: the wall switches off like a tube, to a line, then to a dot */
    uint32_t ta = tape ? now - st.tape_at : 0;
    st.off_h = !tape ? ST_WALL - ST_TOP : ta < 600u ? ST_WALL - ST_TOP - (int)(ta * (ST_WALL - ST_TOP - 2) / 600u) : 2;
    st.off_w = !tape || ta < 600u ? 240 : ta < 1000u ? 240 - (int)((ta - 600u) * 236u / 400u) : ta < 1600u ? 4 : 0;
    /* the specks: on the stage's clock; stuck while a brrr holds */
    float s = dt * rate * 0.001f;
    unsigned stars = 0;
    for (unsigned i = 0; i < ST_NP; i++) {
        if (!st.p[i].kind)
            continue;
        if (!st.tears) {
            float a = (float)st.p[i].age + dt * rate;
            st.p[i].age = (uint16_t)(a > 60000.0f ? 60000.0f : a);
        }
        if (st.p[i].age >= st.p[i].life) {
            st.p[i].kind = PK_NONE;
            continue;
        }
        stars += st.p[i].kind == PK_STAR;
        if (st.tears)
            continue;
        if (st.p[i].kind == PK_NOTE || st.p[i].kind == PK_SPARK)
            st.p[i].vy += 260.0f * s;                     /* they arc down */
        if (st.p[i].kind == PK_RING)
            continue;                                     /* a ring grows where it is (vx: its speed) */
        float wob = st.p[i].kind == PK_BUBBLE ? 16.0f * fm_sinf((float)st.p[i].age * 0.02f + (float)i) : 0.0f;
        st.p[i].x += (st.p[i].vx + wob) * s;
        st.p[i].y += st.p[i].vy * s;
        if (st.p[i].kind == PK_NOTE && st.p[i].y > ST_FEET - 8 && st.p[i].vy > 0.0f) {
            st.p[i].y = ST_FEET - 8;
            st.p[i].vy *= -0.55f;                          /* a bounce on the stage */
        }
        if (st.p[i].x < -10.0f || st.p[i].x > 250.0f || st.p[i].y < (float)ST_TOP || st.p[i].y > (float)ST_FEET)
            st.p[i].kind = PK_NONE;
    }
    if (rate > 0.0f && !st.tears) {
        if (st.night > 90 && stars < 6 && (st_rand() & 15u) == 0)    /* rainbow stars at night */
            st_spawn(PK_STAR, st_ri(6, 234), st_ri(ST_TOP + 6, 110), 0, 0, 1500 + (st_rand() & 1023u), st_rand());
        if (!ui.poetry && (h >> PU_HIGHPASS & 1u))
            for (int k = 0; k < 2; k++)                    /* champagne: fine bubbles in streams */
                st_spawn(PK_BUBBLE, 30 + (int)(st_rand() % 6u) * 36 + st_ri(-3, 3), ST_WALL - 2, 0,
                         st_ri(-150, -90), 1600, 0);
        if (st.lift && (int)(st_rand() % 140u) < st.lift + 20)       /* build: sparks streaming up */
            st_spawn(PK_SPARK, st_ri(4, 236), ST_WALL, 0, -120 - st.lift * 3, 900, 0);
        if (now - st.drop_at < 40u)                                   /* the drop: notes everywhere */
            for (int n = 0; n < 8; n++)
                st_spawn(PK_NOTE, st.cx, 100, st_ri(-220, 220), st_ri(-320, -120), 1200, st_rand());
        if (st.drive && (int)(st_rand() % 160u) < st.drive)          /* sparks off the speakers */
            st_spawn(PK_SPARK, st_rand() & 1u ? 30 : 210, 136, st_ri(-50, 50), st_ri(-160, -80), 900, 20);
        if (f->singing && song.voice[V_BREATH] > 20 && (int)(st_rand() % 300u) < song.voice[V_BREATH])
            st_spawn(PK_PUFF, st.cx + st_ri(-6, 6), 114 - st.hop, st_ri(-20, 20), st_ri(-40, -20), 700, 0);
    }
    if (st.sleep && (st_rand() & 31u) == 0)
        st_spawn(PK_Z, st.cx + 30, 90, 12, -24, 2200, 0);
    if (fresh && rate != 0.0f && !st.tears) {
        /* a note: a burst of music notes in its colour, a star for a high one; any track's key, the Voice's song */
        int pc = (hop_at == ui.played_at && ui.track != T_VOICE ? ui.played : (int)(f->pitch + 0.5f)) % 12;
        st.burst_hue = (pc < 0 ? pc + 12 : pc) * 21;
        st.burst_at = now;
        st.burst_echo = 0;
        unsigned life = 1100u + song.track[T_VOICE].reverb * 12u;
        for (int n = 0; n < 3; n++)
            st_spawn(PK_NOTE, st.cx + st_ri(-20, 20), 100, st_ri(-110, 110), st_ri(-260, -150), life,
                     (unsigned)st.burst_hue + (unsigned)n * 8u);
        if (f->pitch - face_mid >= 5.0f)
            st_spawn(PK_STAR, st.cx, 95, st_ri(-60, 60), -140, 900, (unsigned)st.burst_hue);
    }
    /* a knob turned in the karaoke room: its picture */
    if (ui.page == P_EFFECTS && ui.turn_n != st.turn_seen) {
        int d = ui.turn_d, cx = st.cx;
        st.turn_seen = ui.turn_n;
        st.turn_k = ui.turn_k;
        st.turn_d = (int8_t)d;
        st.turn_at = now;
        /* each knob's picture, a row of the table: what, how many, where from (x off him, or the speakers), how
         * far apart, how fast (with a random part), how long, which colours; TONE, FILTER, EDGE, VIBRATO are drawn
         * from the turn's time instead */
        enum { T_DIR = 1, T_SPK = 2, T_IN = 4, T_RING = 8, T_ALT = 16, T_EDGE = 32, T_UP = 64 };
        static const struct { uint8_t kind, n, flags; int8_t x, dx; uint8_t y; int16_t vx, vy; uint8_t rvx, rvy, dvx;
                              uint8_t life, hue, dhue; } T[16] = {
            {PK_RING, 1, 0, 0, 0, 100, 140, 0, 0, 0, 0, 90, 190, 0},          /* REVERB: a ring */
            {0},                                                           /* TONE */
            {PK_NOTE, 4, T_DIR, 0, 26, 70, 90, -20, 0, 0, 0, 90, 0, 40},       /* DELAY: notes in a row */
            {PK_RING, 3, 0, 0, 0, 100, 60, 0, 0, 0, 40, 80, 120, 30},          /* REPEATS: rings in rings */
            {PK_SPARK, 8, T_SPK, 0, 0, 136, 0, -200, 180, 120, 0, 80, 20, 0},  /* DRIVE: sparks off the speakers */
            {PK_STAR, 6, T_IN, 0, 0, 0, 0, 0, 0, 0, 0, 45, 0, 43},             /* SQUEEZE: stars rush in */
            {PK_RING, 2, T_SPK, 0, 0, 143, 120, 0, 0, 0, 0, 60, 60, 0},        /* PUMP: rings off the speakers */
            {0},                                                           /* FILTER */
            {PK_PUFF, 4, T_ALT, 30, 0, 154, 70, -10, 0, 0, 0, 60, 0, 0},       /* BODY: dust at his feet */
            {PK_PUFF, 4, 0, 0, 0, 114, 0, -50, 120, 40, 0, 70, 0, 0},          /* BREATH: puffs */
            {0},                                                           /* EDGE */
            {PK_NOTE, 3, T_UP, -30, 30, 100, 0, -160, 0, 0, 0, 90, 0, 70},     /* OCTAVE: big notes leaping */
            {PK_NOTE, 1, T_EDGE | T_DIR, 0, 0, 60, 260, -30, 0, 0, 0, 90, 0, 0},  /* GLIDE: a comet */
            {0},                                                           /* VIBRATO */
            {PK_STAR, 8, T_RING, 0, 0, 100, 160, 0, 0, 0, 0, 60, 0, 32},       /* RATE: a ring of stars */
            {PK_BUBBLE, 6, 0, 0, 0, 112, 0, -90, 16, 60, 0, 120, 0, 0}};       /* THROAT: bubbles from his mouth */
        unsigned k = st.turn_k & 15u;
        for (unsigned i = 0; i < T[k].n; i++) {
            unsigned f = T[k].flags;
            int x = cx + T[k].x + T[k].dx * (int)i * (f & T_DIR ? d : 1), y = T[k].y;
            int vx = (T[k].vx + T[k].dvx * (int)i) * (f & T_DIR ? d : 1) + st_ri(-T[k].rvx / 2, T[k].rvx / 2);
            int vy = T[k].vy + st_ri(-T[k].rvy / 2, T[k].rvy / 2);
            if (f & (T_SPK | T_ALT)) {                    /* the speakers, or either side of him */
                x = f & T_SPK ? (i & 1u ? 210 : 30) : cx + (i & 1u ? T[k].x : -T[k].x);
                vx = f & T_ALT ? (i & 1u ? vx : -vx) : vx;
            }
            if (f & T_EDGE)
                x = d > 0 ? 10 : 230;
            if (f & T_UP)
                vy = d > 0 ? -340 : -160;
            if (f & T_IN) {                               /* from the sides, in to him */
                x = i & 1u ? 236 : 4;
                y = st_ri(40, 140);
                vx = (cx - x) * 9 / 4;
                vy = (100 - y) * 9 / 4;
            }
            if (f & T_RING) {
                vx = (int)((float)T[k].vx * fm_cosf((float)i * 0.785f));
                vy = (int)((float)T[k].vx * fm_sinf((float)i * 0.785f));
            }
            st_spawn(T[k].kind, x, y, vx, vy, T[k].life * 10u, T[k].hue + T[k].dhue * i);
        }
    }
    /* the delay's echoes: the burst again, smaller each time, at the delay's time */
    if (st.burst_at && st.burst_echo < st.echoes && now - st.burst_at >= (uint32_t)((st.burst_echo + 1) * st.lag)) {
        st.burst_echo++;
        st_spawn(PK_NOTE, st.cx + st_ri(-30, 30), 100, st_ri(-90, 90), st_ri(-200, -120),
                 700u - (unsigned)st.burst_echo * 120u, (unsigned)st.burst_hue);
    }
    st.punch = h;
}

/* one pixel blended a of 256 over what is there; a line; a ring; a small bitmap */
static void st_blend(int x, int y, uint16_t c, int a)
{
    int yy = y + cv_oy;
    if ((unsigned)x >= 240u || yy < 0 || yy >= BAND || a <= 0)
        return;
    uint16_t *p = &cv_px[(uint32_t)yy * cv_w + (uint32_t)x];
    *p = swap16(a >= 256 ? c : g_mix(swap16(*p), c, a));
}
static void st_line(int x0, int y0, int x1, int y1, uint16_t c, int a)
{
    if (!g_visible(y0 < y1 ? y0 : y1, (y1 > y0 ? y1 - y0 : y0 - y1) + 1))
        return;                                       /* not in this band */
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    for (int e = dx + dy;;) {
        st_blend(x0, y0, c, a);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * e;
        if (e2 >= dy)
            e += dy, x0 += sx;
        if (e2 <= dx)
            e += dx, y0 += sy;
    }
}
static void st_circle(int cx, int cy, int r, uint16_t c, int a)
{
    if (r < 1 || !g_visible(cy - r, 2 * r + 1))
        return;
    for (int x = 0, y = r, e = 1 - r; x <= y; x++) {
        for (int i = 0; i < 8; i++) {                     /* the eight octants: swap, then the signs */
            int px = i & 1 ? y : x, py = i & 1 ? x : y;
            px = i & 2 ? -px : px;
            py = i & 4 ? -py : py;
            if (cy + py >= ST_TOP && cy + py < ST_WALL)
                st_blend(cx + px, cy + py, c, a);
        }
        if (e < 0)
            e += 2 * x + 3;
        else
            e += 2 * (x - y) + 5, y--;
    }
}
static void st_bmp(int x, int y, const uint8_t *rows, int n, int w, int sc, uint16_t c, int a)
{
    if (!g_visible(y, n * sc))
        return;
    for (int r = 0; r < n * sc; r++)
        for (int i = 0; i < w * sc; i++)
            if (rows[r / sc] >> (w - 1 - i / sc) & 1u)
                st_blend(x + i, y + r, c, a);
}

/* the wall's cell (cx, cy): a slow plasma of the evening's colours, or what an effect puts there */
static uint16_t st_cell(int cx, int cy)
{
    uint32_t h = st.punch;
    float t = st.tears ? (float)(st.tear_seed & 255) * 0.05f : st.t;
    uint32_t age = st.now - st.turn_at;
    float wob = (float)song.voice[V_VIBRATO] * 0.03f * fm_sinf(st.vib + (float)cy * 0.8f);
    if (st.turn_k == 13 && age < 700u)                    /* VIBRATO turned: the wall ripples */
        wob += 3.0f * (float)(700u - age) / 700.0f * fm_sinf((float)cy * 1.3f + (float)age * 0.03f);
    float x = (float)cx + wob, y = (float)cy, v;
    if (h >> PU_RING & 1u) {
        float dx = x - 10.0f, dy = y - 13.0f;
        v = fm_sinf(fm_sqrtf(dx * dx + dy * dy) * 1.2f - t * 6.0f) * 1.5f + 1.5f;
    } else
        v = fm_sinf(x * 0.45f + t * 0.9f) + fm_sinf(y * 0.6f - t * 0.7f) + fm_sinf((x - y) * 0.3f + t * 0.5f) + 1.5f;
    int u = (int)(v * (768.0f / 4.5f));
    u = u < 0 ? 0 : u > 767 ? 767 : u;
    uint16_t c = g_mix(st.pal[u >> 8], st.pal[(u >> 8) + 1], u & 255);
    uint32_t sp = (uint32_t)(cx * 7919 + cy * 104729) ^ (uint32_t)(st.t * 6.0f) * 2654435761u;
    if ((sp >> 9) % (ui.page == P_EFFECTS ? 11u : 41u) == 0)
        c = g_mix(c, st_hue(sp >> 20), 200);              /* disco: cells sparkle in every colour */
    else if ((sp >> 9) % 29u == 1)
        c = g_mix(c, K_WHITE, 200);
    if (st.turn_k == 1 && age < 600u)                     /* TONE turned: a wash, warm down, cool up */
        c = g_mix(c, st.turn_d < 0 ? RGB(255, 150, 60) : RGB(80, 200, 255), (int)(600u - age) * 200 / 600);
    if (st.turn_k == 7 && age < 500u) {                   /* FILTER turned: a band of light sweeping down or up */
        int at = (int)age * 12 / 500;
        if ((st.turn_d < 0 ? cy - at : (11 - cy) - at) * (st.turn_d < 0 ? cy - at : (11 - cy) - at) <= 1)
            c = g_mix(c, K_WHITE, 200);
    }
    if (h >> PU_LOWPASS & 1u)
        c = g_mix(c, 0, 256 - cy * 18);                   /* only the low rows lit */
    if (h >> PU_HIGHPASS & 1u)
        c = g_mix(g_mix(c, RGB(240, 248, 255), 120), 0, cy * 20);   /* only the high rows, pale */
    return c;
}

/* each core's thing on a table, 16 x 14 drawn at twice the size: a row two bits a pixel (the top two the left
 * pixel), 0 nothing or which of its three colours */
static const uint16_t ST_THING_INK[8][3] = {
    {RGB(214, 140, 60), RGB(255, 214, 70), RGB(210, 40, 40)},
    {RGB(255, 60, 40), RGB(255, 220, 60), RGB(226, 234, 244)},
    {RGB(84, 96, 130), RGB(240, 196, 70), RGB(220, 220, 230)},
    {RGB(160, 90, 230), RGB(80, 230, 240), RGB(210, 170, 255)},
    {RGB(236, 244, 250), RGB(120, 255, 60), RGB(255, 255, 255)},
    {RGB(70, 190, 80), RGB(255, 90, 170), RGB(200, 40, 120)},
    {RGB(80, 210, 70), RGB(250, 250, 250), RGB(20, 30, 20)},
    {RGB(230, 40, 40), RGB(250, 250, 250), RGB(220, 190, 130)}};
static const uint32_t ST_THING[8][14] = {
    {1431655765u, 1789569705u, 736799720u, 736799720u, 178977440u, 178977440u, 46049952u, 46049920u, 11186048u, 11185920u, 2795520u, 696320u, 696320u, 163840u}   /* food (a slice of pizza: crust, cheese, pepperoni) */,
    {1397760u, 5592320u, 6723840u, 6723840u, 6723840u, 5592320u, 1397760u, 1044480u, 245760u, 245760u, 245760u, 245760u, 245760u, 1044480u}   /* tools (a screwdriver: the red handle, its yellow grooves, the steel shaft) */,
    {143165568u, 143165568u, 357913920u, 2505397600u, 492131648u, 2505397600u, 357913920u, 2505397600u, 357913920u, 2505397600u, 357913920u, 143165568u, 143165568u, 0u}   /* gadgets (a microchip: its body, its gold pins, the dot) */,
    {348160u, 5592320u, 22369600u, 89478480u, 100660560u, 2863311530u, 2863311530u, 89478480u, 90174800u, 22369600u, 5592320u, 348160u, 0u, 0u}   /* astronomy (a ringed planet: the purple planet, its cyan ring, a band) */,
    {1397760u, 266240u, 266240u, 315392u, 1049600u, 4391168u, 27961920u, 111913616u, 447392420u, 447408804u, 357913940u, 0u, 0u, 0u}   /* chemistry (a flask: the white glass, a bright green potion, bubbles) */,
    {33718400u, 42640000u, 44739232u, 45788800u, 45788800u, 11184640u, 2795520u, 81920u, 83968080u, 88162640u, 22103360u, 5591360u, 81920u, 81920u}   /* flora (a tulip: the green stem and leaves, the pink petals, their shade) */,
    {0u, 89130320u, 446962340u, 452205300u, 357913940u, 1431655765u, 1431655765u, 1610612725u, 1431655765u, 357913940u, 89478480u, 352669780u, 1409286165u, 0u}   /* fauna (a frog: green, its white eyes, the black pupils and mouth) */,
    {3495168u, 22369600u, 173282720u, 709887400u, 708837800u, 3892314155u, 3892314155u, 708837800u, 709887400u, 173282720u, 22369600u, 5594112u, 0u, 0u}   /* nautical (a life ring: red, white, the rope) */
};

static void st_fill(uint16_t *p, int n, uint16_t sw)   /* n pixels, already swapped, a pair at a time */
{
    uint32_t pair, *d;
    if (n <= 0)
        return;
    if ((uintptr_t)p & 2u) {
        *p++ = sw;
        n--;
    }
    pair = (uint32_t)sw | ((uint32_t)sw << 16);
    d = (uint32_t *)p;
    while (n >= 2) {
        *d++ = pair;
        n -= 2;
    }
    if (n > 0)
        *(uint16_t *)d = sw;
}

/* a row of 240 turned sh to the right, coming round: d[x] = s[(x - sh) mod 240], two copies and no division */
static void st_turn(uint16_t *d, const uint16_t *s, int sh)
{
    int k = ((-sh) % 240 + 240) % 240;
    memcpy(d, s + k, (unsigned)(240 - k) * sizeof *d);
    memcpy(d + 240 - k, s, (unsigned)k * sizeof *d);
}

static void st_thing(int x, unsigned k)       /* a core's thing standing on a table, its left edge at x, twice the size */
{
    if (!g_visible(140 - 28, 28))
        return;
    for (int l = 0; l < 3; l++)
        for (int r = 0; r < 14; r++)
            for (int i = 0; i < 16; i++)
                if ((ST_THING[k & 7u][r] >> (30 - 2 * i) & 3u) == (unsigned)l + 1u)
                    g_rect(x + i * 2, 140 - 28 + r * 2, 2, 2, ST_THING_INK[k & 7u][l]);
}

/* SLAMJAM's club: a brick wall. On the stage (HOME) the house lights are up: warm brick, no sign (the band rehearsing). In the
 * SLAMJAM room (FX) it is the show: the brick in the dark (the knobs' colours still in it, as the karaoke wall has
 * them), a warm spotlight on him, the neon sign flickering; two cafe tables with candles either side. The room's
 * effects: the wah a band of light riding up and down the wall, the echo the sign said again below itself, fainter
 * each time, the snaps sparks off the tables on each snap */
static void poetry_room(uint32_t now)
{
    int show = ui.page == P_EFFECTS, wah = st.punch >> PU_WAH & 1u ? ST_WALL - eng_look[0] * (ST_WALL - ST_TOP) / 127 : -99;
    /* a course of bricks at a time: each brick's colour worked out once, then the rows written straight in; only the
     * spotlight's cone is mixed a pixel at a time (the room was 6 to 9 frames a second drawn a pixel at a time) */
    uint16_t base[13], litb[13], dark[13], mor_base = 0;
    int course = -1;
    /* tape off: the room switches off like a tube, as the karaoke room's wall does (stage_frame's off_h, off_w) */
    int tube = st.off_h < ST_WALL - ST_TOP, mid = (ST_TOP + ST_WALL) / 2, th = st.off_h / 2;
    int tl = 120 - st.off_w / 2, tr = 120 + st.off_w / 2, glow = tube ? 256 - st.off_h * 256 / (ST_WALL - ST_TOP) : 0;
    for (int y = ST_TOP; y < ST_WALL; y++) {
        int yy = y + cv_oy;
        if (yy < 0 || yy >= BAND)
            continue;
        int row = (y - ST_TOP) / 9, off = row & 1 ? 11 : 0, half = 22 + (y - ST_TOP) * 62 / (ST_WALL - ST_TOP);
        int band = y - wah < 0 ? wah - y : y - wah, wm = band < 14 ? (14 - band) * 14 : 0, joint = (y - ST_TOP) % 9 == 8;
        if (row != course) {
            course = row;
            uint16_t tint = st.pal[row & 3];
            for (int j = 0; j < 13; j++) {
                uint32_t k = (uint32_t)(j * 7919 + row * 104729) * 2654435761u;
                base[j] = show ? g_mix(RGB(70 + (int)(k >> 28), 28, 30), tint, 34)
                               : g_mix(RGB(206 + (int)(k >> 28) * 2, 192 + (int)(k >> 28), 170), tint, 16);
                litb[j] = g_mix(base[j], 0, 60);
            }
            mor_base = show ? RGB(22, 16, 22) : RGB(160, 148, 140);   /* (the lights up: whitewashed, him in front) */
        }
        uint16_t mor = show ? g_mix(mor_base, 0, 170) : mor_base, mor_lit = g_mix(mor_base, 0, 60);
        for (int j = 0; j < 13; j++) {
            dark[j] = show ? g_mix(base[j], 0, 170) : base[j];
            if (wm)
                dark[j] = g_mix(dark[j], RGB(255, 80, 200), wm);
        }
        if (wm)
            mor = g_mix(mor, RGB(255, 80, 200), wm);
        uint16_t *p = &cv_px[(uint32_t)yy * cv_w];
        if (tube && (y < mid - th || y >= mid + th || st.off_w <= 0)) {
            st_fill(p, 240, 0);                                      /* switched off */
            continue;
        }
        if (glow) {                                                  /* the tube's last line, brighter as it narrows */
            mor = g_mix(mor, K_WHITE, glow);
            mor_lit = g_mix(mor_lit, K_WHITE, glow);
            for (int j = 0; j < 13; j++)
                dark[j] = g_mix(dark[j], K_WHITE, glow);
        }
        uint16_t sm = swap16(mor);
        int x0 = st.cx - half + 1, x1 = st.cx + half - 1;            /* the spotlight's cone, when the show is on */
        x0 = x0 < 0 ? 0 : x0 > 240 ? 240 : x0;
        x1 = x1 < -1 ? -1 : x1 > 239 ? 239 : x1;
        if (joint)
            st_fill(p, 240, sm);                                     /* the mortar course, one span */
        else
            for (int pass = 0; pass < (show ? 2 : 1); pass++) {      /* the cone is written once, below */
                int x = pass ? x1 + 1 : 0, end = pass || !show ? 240 : x0, j = (x + off) / 22, ph = (x + off) % 22;
                while (x < end) {
                    int n = ph == 21 ? 1 : 21 - ph;
                    if (n > end - x)
                        n = end - x;
                    st_fill(p + x, n, ph == 21 ? sm : swap16(dark[j]));
                    x += n;
                    ph += n;
                    if (ph >= 22)
                        ph -= 22, j++;
                }
            }
        int inv = 30 * 65536 / half;                                 /* (no division a pixel: the counts kept) */
        for (int x = x0, j = (x0 + off) / 22, ph = (x0 + off) % 22; show && x <= x1;) {
            int n = joint || ph == 21 ? 1 : 21 - ph, dx;
            uint16_t from, c;
            if (n > x1 + 1 - x)
                n = x1 + 1 - x;
            if (n > 4 && !joint && ph != 21)
                n = 4;                                               /* the cone's fall is gentle: four pixels, one mix */
            dx = x + n / 2 - st.cx;
            from = joint || ph == 21 ? mor_lit : litb[j];
            c = g_mix(from, RGB(255, 210, 140), 40 - (((dx < 0 ? -dx : dx) * inv) >> 16));
            if (wm)
                c = g_mix(c, RGB(255, 80, 200), wm);
            if (glow)
                c = g_mix(c, K_WHITE, glow);
            st_fill(p + x, n, swap16(c));
            x += n;
            ph += n;
            if (ph >= 22)
                ph -= 22, j++;
        }
        if (tube && st.off_w < 240) {                                /* the line narrowing to a dot */
            st_fill(p, tl, 0);
            st_fill(p + tr, 240 - tr, 0);
        }
    }
    int32_t clip0 = cv_y0, clip1 = cv_y1;                            /* the sign and the tables go off with the wall */
    if (tube) {
        cv_y0 = mid - th + cv_oy > clip0 ? mid - th + cv_oy : clip0;
        cv_y1 = st.off_w < 240 ? cv_y0 : mid + th + cv_oy < clip1 ? mid + th + cv_oy : clip1;
    }
    if (show) {
        int echo = eng_look[1];
        for (int e = 2; e >= 0; e--) {                                   /* the sign, and its echoes under it */
            int y = ST_TOP + 6 + e * 17, a = e ? echo * 2 - e * 60 : 256;
            if (a <= 0 || !g_visible(y, 20) || (!e && !((now / 70u) % 47u)))
                continue;                                                /* (it flickers now and then) */
            uint16_t pink = RGB(255, 96, 190 + (eng_look[3] > 8 ? 60 : 0));
            g_center(1, y + 1, 240, &FONT_S, "SLAMJAM", g_mix(0, RGB(120, 20, 80), a > 256 ? 256 : a));
            g_center(0, y, 240, &FONT_S, "SLAMJAM", g_mix(0, pink, a > 256 ? 256 : a));
        }
    }
    if (g_visible(104, 56)) {
        uint32_t sn = st.punch >> PU_STUTTER8 & 15u;
        unsigned per = sn & 12u ? 1u : sn & 2u ? 2u : 4u;
        for (int t = 0; t < 2; t++) {                                    /* the tables, their candles */
            int x = t ? 204 : 36;
            if (sn && !tube && eng_playing && (eng_tick / per + (unsigned)t) % 2u == 0u)
                for (int s = 0; s < 3; s++)                              /* snaps: sparks off the tables */
                    st_bmp(x - 14 + s * 11, 112 - (s & 1) * 6, ST_STAR, 7, 7, 1, RGB(255, 236, 160), 230);
            g_round(x - 26, 140, 52, 8, 4, RGB(48, 32, 30));
            g_rect(x - 2, 147, 4, 13, RGB(36, 24, 24));
            int cx = t ? x + 18 : x - 18, fl = (int)((now / 90u + (unsigned)t * 3u) % 3u);   /* the candle, at the edge */
            g_rect(cx - 2, 130, 4, 10, RGB(236, 226, 200));
            g_disc(cx, 127 - fl, 3, RGB(255, 190, 70));
            g_disc(cx, 128 - fl, 1, RGB(255, 250, 210));
            if (show && !tube)                  /* the poem's core, chosen for the show: its lead left, its support right */
                st_thing(t ? x - 20 : x - 12, t ? poem.core2 : poem.core);
        }
    }
    cv_y0 = clip0;
    cv_y1 = clip1;
}

static void stage_draw(void)
{
    stage_frame();
    uint32_t now = plat_ms(), h = st.punch;
    int y0 = g_band * BAND;
    if (ui.poetry)
        poetry_room(now);
    /* the wall: CRT cells, 12 px apart, a scanline through each; lo-fi and crush join them into bigger ones */
    int pitch = 12 * st.cell;
    for (int cy = 0; !ui.poetry && ST_TOP + cy * 12 < ST_WALL; cy += st.cell)
        for (int cx = 0; cx < 20; cx += st.cell) {
            int x = cx * 12, y = ST_TOP + cy * 12;
            if (!g_visible(y, pitch))
                continue;
            int mid = (ST_TOP + ST_WALL) / 2, half = st.off_h / 2;
            int on = y + pitch > mid - half && y < mid + half && x + pitch > 120 - st.off_w / 2 && x < 120 + st.off_w / 2;
            uint16_t c = on ? st_cell(cx, cy) : 0;
            if (st.off_h < ST_WALL - ST_TOP && on)
                c = g_mix(c, K_WHITE, 256 - st.off_h * 256 / (ST_WALL - ST_TOP));    /* the tube's last line */
            if ((h & (1u << PU_CHOP8 | 1u << PU_CHOP16 | 1u << PU_CHOP32 | 1u << PU_CHOP64)) && !st.gate_shut)
                c = g_mix(c, st_hue((unsigned)st.strobe + (unsigned)(cx + cy) * 6u), 170);   /* chop: strobe colours */
            if (st.lift)                                                   /* build: lit up to its level */
                c = y + pitch > ST_WALL - st.lift * (ST_WALL - ST_TOP + 12) / 127
                        ? g_mix(c, RGB(255, 236, 200), st.lift + (eng_drum_hits & 2u ? 80 : 0))
                        : g_mix(c, 0, 180);
            if (now - st.drop_at < 220u)                                   /* the drop */
                c = g_mix(c, K_WHITE, 256 - (int)(now - st.drop_at));
            uint16_t gap = g_mix(c, 0, 150 - st.haze / 3);   /* the cells' glow between them (REVERB more) */
            for (int r = 0; r < pitch && y + r < ST_WALL; r++) {
                int yy = y + r + cv_oy;
                if (yy < 0 || yy >= BAND)
                    continue;
                int lit = r > 0 && r < pitch - 1;
                uint16_t sw = swap16(r & 1 ? g_mix(c, 0, 40) : c), sg = swap16(gap);
                uint16_t *p = &cv_px[(uint32_t)yy * cv_w + (uint32_t)x];
                for (int i = 0; i < pitch && x + i < 240; i++)
                    p[i] = lit && i > 0 && i < pitch - 1 ? sw : sg;
            }
        }
    /* the speakers: monitor wedges at his feet, coloured by the knobs' bank in the karaoke room; the pump pushes
     * their cones, drive heats them */
    if (g_visible(126, 34) && !ui.poetry) {
        static const uint16_t BANK[4] = {RGB(40, 210, 196), RGB(255, 196, 40), RGB(250, 80, 190), RGB(160, 120, 255)};
        uint16_t box = ui.page == P_EFFECTS ? BANK[ui.sub[P_EFFECTS] & 3u] : RGB(96, 88, 120);
        for (int sp = 0; sp < 2; sp++) {
            int x = sp ? 186 : 6, cxs = x + 24, cys = 143;
            g_round(x, 126, 48, 34, 5, g_mix(box, 0, 90));
            g_round(x + 2, 128, 44, 30, 4, box);
            int r = 11 + st.duck * 4 / 127;
            g_disc(cxs, cys, r, g_mix(RGB(26, 24, 30), RGB(255, 96, 40), st.drive * 2));
            g_disc(cxs, cys, r / 3 + 1, g_mix(RGB(80, 76, 92), RGB(255, 200, 90), st.drive * 2));
            st_blend(x + 42, 131, st.gate_shut ? RGB(30, 60, 30) : RGB(120, 255, 120), 256);    /* the signal light */
        }
    }
    /* ring mod: rings out of him and the speakers */
    if (!ui.poetry && (h >> PU_RING & 1u))
        for (int k = 0; k < 3; k++) {
            int r = (int)((now / 7u + (unsigned)k * 50u) % 150u);
            st_circle(st.cx, 110, r, RGB(230, 200, 255), 200 - r);
            st_circle(30, 143, r / 2, RGB(230, 200, 255), 150 - r);
            st_circle(210, 143, r / 2, RGB(230, 200, 255), 150 - r);
        }
    /* lasers: from the top corners, sweeping; EDGE while he sings, the chops on the gate, more beams faster */
    int beams = st.face.singing ? st.edge / 34 : 0;
    if (st.turn_k == 10 && now - st.turn_at < 350u)
        beams += 4;                                       /* EDGE turned: lasers flash */
    if (h & (1u << PU_CHOP8 | 1u << PU_CHOP16 | 1u << PU_CHOP32 | 1u << PU_CHOP64))
        beams = st.gate_shut ? 0 : h >> PU_CHOP64 & 1u ? 8 : h >> PU_CHOP32 & 1u ? 6 : h >> PU_CHOP16 & 1u ? 4 : 2;
    static const uint16_t LASER[3] = {RGB(255, 60, 200), RGB(60, 240, 255), RGB(140, 255, 90)};
    for (int b = 0; b < beams; b++) {
        int from = b & 1 ? 239 : 0, to = 120 + (int)(110.0f * fm_sinf(st.t * 1.7f + (float)b * 1.3f));
        st_line(from, ST_TOP, to, ST_FEET - 1, LASER[b % 3], 220);
        st_line(from + (b & 1 ? -1 : 1), ST_TOP, to + 1, ST_FEET - 1, LASER[b % 3], 90);
    }
    /* him, standing on the stage */
    face_t f = st.face;
    f.punch &= ~(1u << PU_LOFI | 1u << PU_CRUSH);       /* the whole scene goes to blocks below, him with it */
    face_draw(st.cx + st.jit, ST_FEET - 95 - st.hop - st.lift / 8 + st.duck * 6 / 127 + (st.sleep ? 8 : 0), &f);
    if (ui.poetry && eng_look[3] > 8)                     /* air horn: blasts ringing out over the room */
        for (int k = 0; k < 3; k++) {
            int r = (int)((now / 5u + (unsigned)k * 40u) % 120u);
            st_circle(st.cx + 30, ST_FEET - 30, r, RGB(255, 220, 90), (220 - r * 2) * eng_look[3] / 127);
        }
    if (ui.poetry && ui.page == P_EFFECTS && g_visible(96, 64)) {   /* the mic on its boom, at his mouth (the room's) */
        int mx = st.cx + 30, my = ST_FEET - 18 - st.hop;
        g_line(st.cx + 62, ST_FEET - 1, st.cx + 62, ST_FEET - 46, 2, RGB(30, 26, 34));
        g_line(st.cx + 62, ST_FEET - 46, mx + 4, my + 2, 2, RGB(30, 26, 34));
        g_round(mx - 5, my - 4, 11, 8, 3, RGB(60, 56, 64));
        g_round(mx - 4, my - 3, 9, 3, 1, RGB(140, 136, 150));
    }
    /* the specks */
    for (unsigned i = 0; i < ST_NP; i++) {
        if (!st.p[i].kind || (st.gate_shut && st.p[i].kind != PK_Z))
            continue;
        int x = (int)st.p[i].x, y = (int)st.p[i].y;
        if (st.p[i].kind != PK_RING && (!g_visible(y - 16, 32) || y < ST_TOP + 2))
            continue;
        int left = 256 - (int)st.p[i].age * 256 / (int)st.p[i].life, a = left < 96 ? left * 8 / 3 : 256;
        uint16_t c = st_hue(st.p[i].hue);
        switch (st.p[i].kind) {
        case PK_NOTE: {
            int sc = st.p[i].age < 140u || st.size == 0 ? 2 : 1;          /* they pop out big; a low OCTAVE, big */
            if (st.glide > 10)                                           /* GLIDE: a trail */
                for (int k = 1; k <= st.glide / 34; k++)
                    st_bmp(x - (int)(st.p[i].vx * 0.012f * (float)k), y - (int)(st.p[i].vy * 0.012f * (float)k), ST_NOTE,
                           8, 6, sc, c, a / (k + 1));
            st_bmp(x, y, ST_NOTE, 8, 6, sc, c, a);
            break;
        }
        case PK_STAR:
            st_bmp(x - 3, y - 3, ST_STAR, 7, 7, 1, st_hue(st.p[i].hue + now / 8u), (now / 90u + i) % 4u ? a : a / 3);
            break;
        case PK_BUBBLE:
            st_blend(x, y - 1, RGB(255, 246, 214), a);
            st_blend(x - 1, y, RGB(255, 246, 214), a / 2);
            st_blend(x + 1, y, RGB(255, 246, 214), a / 2);
            st_blend(x, y + 1, RGB(255, 246, 214), a / 2);
            break;
        case PK_SPARK:
            for (int k = 0; k < 4; k++)
                st_blend(x + (k & 1), y + (k >> 1), k ? RGB(255, 120, 40) : RGB(255, 230, 120), a);
            break;
        case PK_PUFF:
            g_disc(x, y, 2 + (int)st.p[i].age / 200, g_mix(0, RGB(220, 220, 236), a));
            break;
        case PK_RING:
            st_circle(x, y, (int)(st.p[i].vx * (float)st.p[i].age * 0.001f), c, a * 3 / 4);
            break;
        case PK_Z:
            g_text(x, y, 20, &FONT_S, (st.p[i].age / 500u) & 1u ? "z" : "Z", RGB(200, 180, 255));
            break;
        }
    }
    /* afterwards, over everything */
    uint16_t row[240];
    for (int yy = 0; yy < BAND; yy++) {
        int y = y0 + yy;
        uint16_t *p = &cv_px[(uint32_t)yy * cv_w];
        /* brrr: the picture torn into strips at each repeat, wilder the faster: shifted sideways, then colours
         * swapped (brrr 16), smeared down from the strip's top row (brrr 32), turned negative (brrr 64) */
        if (y < ST_TOP || y >= ST_WALL)
            continue;
        if (st.tears) {
            int band = 40 / st.tears + 3;
            uint32_t k = (uint32_t)(y / band) * 2654435761u ^ (uint32_t)st.tear_seed * 40503u;
            unsigned kind = (k >> 13) % 6u;
            if (kind < 4u) {
                int sh = (int)((k >> 7) % (unsigned)(st.tears * 20)) - st.tears * 10;
                const uint16_t *src = p;
                if (kind == 3u && st.tears > 2 && y % band) {
                    int top = y - y % band;                  /* the strip's top row: here, or in the band before (the
                                                              * other half of the canvas, sent and still whole) */
                    src = top >= y0 ? p - (y % band) * (int)cv_w
                                    : (cv_px == cv_mem ? cv_mem + CV_MAX / 2u : cv_mem) + (uint32_t)(top - (y0 - BAND)) * cv_w;
                }
                st_turn(row, src, sh);
                for (int x = 0; x < 240; x++) {
                    uint16_t c = row[x];
                    if (kind == 1u && st.tears > 1) {
                        c = swap16(c);
                        c = swap16((uint16_t)((c & 31u) << 11 | (c & 0x07E0u) | c >> 11));
                    }
                    p[x] = kind == 2u && st.tears > 3 ? (uint16_t)~c : c;
                }
            }
        }
        /* scratch: the room dragged back and forth with the record, the top most (the hand's end of it) */
        if (ui.poetry && (h >> PU_SCRATCH & 1u)) {
            int sh = ((int)eng_look[2] - 64) * (ST_WALL - y) / (ST_WALL - ST_TOP);
            st_turn(row, p, sh);
            memcpy(p, row, sizeof row);
        }
        /* GAMEBOIII: two greens, scanlines; chop shut: dark */
        int phone = !ui.poetry && (h >> PU_PHONE & 1u);
        if (phone || st.gate_shut)
            for (int x = 0; x < 240; x++) {
                uint16_t c = swap16(p[x]);
                if (phone) {
                    int l = (int)st_luma(c);
                    c = g_mix(RGB(18, 44, 24), RGB(170, 236, 130), l * 4 - (y & 1 ? 60 : 0));
                }
                if (st.gate_shut)
                    c = 0;                                /* chop: blacked out while the gate is shut */
                p[x] = swap16(c);
            }
    }
    /* lo-fi and crush: the scene in blocks (the bars stay readable) */
    if (st.cell > 1) {
        int q = st.cell * 2;
        for (int yy = 0; yy < BAND; yy += q)
            for (int x = 0; x < 240; x += q) {
                int a = yy;
                while (a < yy + q - 1 && y0 + a < ST_TOP)
                    a++;
                if (y0 + a < ST_TOP || y0 + a >= ST_WALL)
                    continue;
                uint16_t c = cv_px[(uint32_t)a * cv_w + (uint32_t)x];
                for (int j = a - yy; j < q && yy + j < BAND && y0 + yy + j < ST_WALL; j++)
                    for (int i = 0; i < q; i++)
                        cv_px[(uint32_t)(yy + j) * cv_w + (uint32_t)(x + i)] = c;
            }
    }
}

/* the bottom bar's top, under his feet: the stage's lip, and his shadow on it (BODY its weight) */
static void stage_lip(void)
{
    if (!g_visible(ST_FEET, 6))
        return;
    int sh = 18 + st.body * 26 / 100 - (st.hop > 0 ? st.hop : 0) - st.lift / 16;
    for (int y = ST_FEET; y < ST_FEET + 6; y++)
        for (int x = 0; x < 240; x++) {
            if (y < ST_FEET + 2)
                st_blend(x, y, RGB(18, 110, 104), 256);
            int dx = x - st.cx, e = dx * dx * 256 / (sh * sh) + (y - ST_FEET) * (y - ST_FEET) * 256 / 30;
            if (e < 256)
                st_blend(x, y, 0, (60 + st.body * 150 / 100) * (256 - e) / 256);
        }
}
