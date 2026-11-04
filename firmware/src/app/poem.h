/* SPDX-License-Identifier: GPL-3.0-only */
/* SLAMJAM's poet and band (GLO's mode): a whole performance made up at once and written into the song, so it plays,
 * loops, saves and edits as any other song does. A poem is a topic, a hero and a seed; each player has a style (the
 * stage's knobs: poem_style), which rewrites its part on the spot, playing or not:
 *
 *   the words  poem.txt's templates, filled with the topic's words (tools/gen_poem.py packs them): an opening,
 *              images, a question, a refrain said again and again, a rise, vows, a quiet return and a sign-off
 *   the arc    a 4-bar intro, four stanzas of four lines (two bars a line), the band answering between them, a
 *              stop-time break before the last stanza, a 4-bar outro
 *   the band   moody jazz-bar: a minor ii-V or i-iv cycle, four bars round. The drums' style picks the kick
 *              figure for the chord and the mood (the rise is another figure, not the same bar with more hats);
 *              the bass lands on those kicks and steps into the next root, the keys comp against them. The
 *              offbeat hats and the cymbal wait for the bar to come round again. Quieter in the intro and the
 *              return, busier in the rise, silent in the break
 *   the poet   chants each line on a reciting tone, a syllable an eighth (rap: a sixteenth), the last held.
 *              A crowded line shortens its breaths first, and only falls to sixteenths when the tempo is slow
 *              enough to hear them. Questions rise at the end, the return falls, the rise sits higher
 *
 * Every word comes from poem.txt, so everything it can say is family-friendly by construction. Part of the
 * unity build (ui.c). */
#include "poem_data.h"

static uint32_t poem_seed, poem_salt;
static unsigned prand(unsigned n)
{
    poem_seed ^= poem_seed << 13;
    poem_seed ^= poem_seed >> 17;
    poem_seed ^= poem_seed << 5;
    return n ? poem_seed % n : 0;
}

/* (gen_poem.py: the words as symbols, a character or a pair of symbols each, Huffman coded high bit first) */
static char *poem_unpack(char *d, char *end, unsigned sym)
{
    if (sym >= POEM_CHARS) {
        d = poem_unpack(d, end, POEM_PAIR[sym - POEM_CHARS][0]);
        return poem_unpack(d, end, POEM_PAIR[sym - POEM_CHARS][1]);
    }
    if (d < end)
        *d++ = (char)POEM_CHAR[sym];
    return d;
}
/* the next symbol from bit *at of p: its code's length found a bit at a time (a canonical code) */
static unsigned poem_sym(const uint8_t *p, uint32_t *at)
{
    int code = 0, first = 0, index = 0;
    for (unsigned len = 0; len < 16; len++) {
        code |= p[*at >> 3] >> (7u - (*at & 7u)) & 1;
        ++*at;
        int count = POEM_COUNT[len];
        if (code - count < first)
            return POEM_ORDER[index + code - first];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return 0;
}
/* group g's i-th string (each frame's kinds, then each topic's classes, the wildcards, the cores, the topics' and
 * frames' and the cores' names) */
static void poem_pick(unsigned g, unsigned i, char *buf, unsigned cap)
{
    const uint8_t *p = POEM_DATA + POEM_GROUP[g][0];
    uint32_t at = 0;
    char *d = buf;
    for (i %= POEM_GROUP[g][1];;) {
        unsigned sym = poem_sym(p, &at);
        if (sym == 0 && !i--)                       /* (symbol 0: the character 0, a string's end) */
            break;
        if (sym && !i)
            d = poem_unpack(d, buf + cap - 1, sym);
    }
    *d = 0;
}
#define PG_KIND(k) (poem.frame * NPK + (k))
#define PG_TOPIC(t, c) (POEM_FRAMES * NPK + (t) * NPC + (c))
#define PG_WILD (POEM_FRAMES * NPK + POEM_TOPICS * NPC)         /* + the slot's letter's place in POEM_WILD */
#define PG_CORE (PG_WILD + sizeof POEM_WILD - 1)                 /* + the core */
#define PG_TOPIC_NAMES (PG_CORE + POEM_CORES)
#define PG_FRAME_NAMES (PG_TOPIC_NAMES + 1)
#define PG_CORE_NAMES (PG_TOPIC_NAMES + 2)

/* a word's singular or plural ("goose/geese"; no slash: add s) */
static void poem_form(char *w, int plural)
{
    char *slash = strchr(w, '/');
    if (slash) {
        if (plural)
            memmove(w, slash + 1, strlen(slash + 1) + 1);
        else
            *slash = 0;
    } else if (plural)
        s_cat(w, "s");
}

/* the players' styles: the poet's, the keys', the bass's, the drums' (by track) */
enum { PS_CHANT, PS_SING, PS_RAP };
enum { KS_RHODES, KS_STABS, KS_PAD, KS_ARP, KS_ORGAN, KS_VIBES, KS_BOSSA, KS_POWER };
enum { BS_WALK, BS_BOOM, BS_PEDAL, BS_FUNK, BS_808, BS_BOSSA, BS_PUNK };
enum { DS_BOOMBAP, DS_SWING, DS_BRUSHES, DS_DOUBLE, DS_BOSSA, DS_TRAP, DS_BONGOS, DS_ROCK };
/* each player's last style is "yours": the band leaves that part to you (nothing written; what REC writes there is
 * kept, its sound and level as you left them) */
#define NJAM 9
#define JAM_YOURS(t) (JAM_STYLES[t] - 1u)
static const uint8_t JAM_STYLES[NTRACKS] = {3, 9, 8, 9};
static const char *const JAM_NAME[NTRACKS][NJAM] = {
    {"chant", "sing", "rap"},
    {"rhodes", "stabs", "pad", "arp", "organ", "vibes", "bossa", "power", "YOURS"},
    {"walk", "boom", "pedal", "funk", "808", "bossa", "punk", "YOURS"},
    {"boom bap", "swing", "brushes", "double", "bossa", "trap", "bongos", "rock", "YOURS"}};
/* each style's sound: the keys' and the bass's presets by name, the drums' kit (Studio, Tight, Boom, Dusty, Bright,
 * Crunch) */
static const char *const JAM_SOUND[2][NJAM] = {
    {"soft tines", "octave stab", "ep pad", "fm pluck", "gospel", "fm bell", "epiano", "square pluck"},
    {"ep bass", "deep sub", "duo bass", "pluck bass", "boom", "bass one", "stack bass"}};
static const uint8_t JAM_KIT[NJAM] = {2, 3, 3, 1, 0, 2, 4, 5}; /* Boom, Dusty, Dusty, Tight, Studio, Boom, Bright, Crunch */
/* each style's level, so a style changes the sound and not the balance: the three alone about -29, -28 and -26 dB
 * RMS on the simulator, the band together near the poet (as measured on the FM-1 at the old 86 and 92) */
static const uint8_t JAM_LEVEL[3][NJAM] = {{65, 65, 57, 75, 74, 100, 67, 48}, {59, 52, 56, 61, 60, 85, 60},
                                           {58, 63, 100, 78, 82, 50, 85, 53}};   /* (the 808 3 dB up: it bumps) */

/* the bands: a set is a sound for the night, its tempo (from bpm, in steps of 4 up to span), the key's scale (the
 * keys you jam on), its chord cycles (a bit each, CYCLE), its groove (GROOVE), and where each player starts */
enum { Q_MIN7, Q_DOM7, Q_MAJ7, Q_HALFDIM, Q_MAJ, Q_MIN };
enum { GV_STRAIGHT, GV_RIDE, GV_LEAN, GV_LOOSE, GV_POKE, GV_PUSH, GV_LATE, GV_DRIVE };
static const struct {
    const char *name;
    uint8_t bpm, span, scale, major, groove, cycles, style[NTRACKS];
} SETS[] = {
    {"jazz bar", 80, 16, 3, 0, GV_RIDE, 0x07, {PS_CHANT, KS_RHODES, BS_WALK, DS_SWING}},
    {"boom bap", 76, 8, 2, 0, GV_LEAN, 0x89, {PS_RAP, KS_STABS, BS_BOOM, DS_BOOMBAP}}, /* 76..84: a sixteenth stays hearable */
    {"beatnik", 100, 12, 3, 0, GV_LOOSE, 0x05, {PS_CHANT, KS_VIBES, BS_WALK, DS_BONGOS}},
    {"funk", 98, 12, 4, 1, GV_POKE, 0x10, {PS_CHANT, KS_ORGAN, BS_FUNK, DS_DOUBLE}},
    {"bossa", 108, 12, 1, 1, GV_PUSH, 0x20, {PS_SING, KS_BOSSA, BS_BOSSA, DS_BOSSA}}, /* an eighth, never a rushed one */
    {"lo-fi", 70, 12, 3, 0, GV_LATE, 0x82, {PS_SING, KS_PAD, BS_BOOM, DS_BRUSHES}},
    {"trap", 66, 8, 2, 0, GV_STRAIGHT, 0x88, {PS_RAP, KS_ARP, BS_808, DS_TRAP}}, /* (slow: the half time of 132-148) */
    {"punk", 108, 12, 1, 1, GV_DRIVE, 0x40, {PS_CHANT, KS_POWER, BS_PUNK, DS_ROCK}}}; /* 108..120: the line stays on eighths */
#define NSETS (sizeof SETS / sizeof SETS[0])

static struct {
    uint8_t frame, set, core, core2, topic, hero;   /* (core: the lead; core2: the support, or the lead again) */
    uint32_t used[NPK];               /* the templates said in this poem: none twice, till a kind runs out */
    uint8_t style[NTRACKS];           /* the players' styles (kept from poem to poem) */
    uint8_t key, cycle, tone, nrows, nlines;
    struct { uint16_t slot; uint8_t line, kind; } plan[24];     /* the voice: each row's two bars, its line, its kind */
} poem;

/* a title's small words, which stay small */
static int poem_small(const char *w)
{
    static const char *const SMALL[] = {"a ", "an ", "the ", "to ", "of ", "and "};
    for (unsigned i = 0; i < sizeof SMALL / sizeof SMALL[0]; i++)
        if (!strncmp(w, SMALL[i], strlen(SMALL[i])))
            return 1;
    return 0;
}

/* a template of kind k in the poem's frame, its slots filled; "a" before a vowel becomes "an"; the first letter a
 * capital. A topic's noun, adjective, place or verb is now and then a wildcard's instead (a thing, a strange
 * adjective, a strange place, a silly verb): the madlib's swerve. {T} is the title. 0 if it ran over cap */
static int poem_line(unsigned kind, char *out, unsigned cap, const char *title)
{
    char t[64], w[40];
    unsigned g = PG_KIND(kind), nt = POEM_GROUP[g][1], pick = prand(nt), over = 0;
    if ((poem.used[kind] & ((1u << nt) - 1u)) == ((1u << nt) - 1u))
        poem.used[kind] = 0;
    while (poem.used[kind] >> pick & 1u)
        pick = (pick + 1u) % nt;
    poem.used[kind] |= 1u << pick;
    poem_pick(g, pick, t, sizeof t);
    unsigned n = 0;
    for (const char *s = t; *s;) {
        if (*s != '{') {
            if (n + 1 < cap)
                out[n++] = *s;
            else
                over = 1;
            s++;
            continue;
        }
        const char *e = strchr(s, '}');
        if (!e)
            break;
        unsigned len = (unsigned)(e - s - 1), plural = len == 2;
        char c = s[1];
        const char *wild = strchr(POEM_WILD, c);
        static const char SWERVE[5] = {0, 'x', 'X', 'l', 'm'};                  /* by the topic's class */
        unsigned cls = c == 'H' ? PC_HERO : c == 'n' ? PC_NOUN : c == 'a' ? PC_ADJ : c == 'p' ? PC_PLACE : PC_VERB;
        if (!wild && c != 'T' && c != 'f' && cls != PC_HERO && prand(4) == 0)
            c = SWERVE[cls], wild = strchr(POEM_WILD, c);
        if (c == 'T')
            s_copy(w, title, sizeof w);
        else if (c == 'f')                         /* the poem's core: food, tools, gadgets ..., a third of them its support's */
            poem_pick(PG_CORE + (prand(3) ? poem.core : poem.core2), prand(65535u), w, sizeof w);
        else
            poem_pick(wild ? PG_WILD + (unsigned)(wild - POEM_WILD) : PG_TOPIC(poem.topic, cls),
                      c == 'H' ? poem.hero : prand(65535u), w, sizeof w);
        if (c == 'H' || c == 'n' || c == 'x')
            poem_form(w, (int)plural);
        if (c == 'l' && n >= 4 && !strncmp(out + n - 4, "the ", 4))
            n -= 4;                                /* (a strange place says its own article: not "the the moon") */
        if (n >= 2 && out[n - 1] == ' ' && out[n - 2] == 'a' && (n == 2 || out[n - 3] == ' ') && strchr("aeiou", w[0])
            && strncmp(w, "uni", 3) && n + 1 < cap) {    /* (but "a unicycle") */
            out[n - 1] = 'n';                      /* "a onion" is "an onion" */
            out[n++] = ' ';
        }
        for (const char *p = w; *p; p++)
            if (n + 1 < cap)
                out[n++] = *p;
            else
                over = 1;
        s = e + 1;
    }
    out[n] = 0;
    /* capitals: the first letter, each after a sentence's end, and every word of a title */
    for (unsigned i = 0; i < n; i++)
        if (out[i] >= 'a' && out[i] <= 'z' &&
            (!i || (i >= 2 && out[i - 1] == ' ' && (strchr(".!?", out[i - 2]) || (kind == PK_TITLE && !poem_small(out + i))))))
            out[i] = (char)(out[i] - 32);
    return !over;
}

/* a preset by its name (the presets' order may change; the poem's sounds stay) */
static unsigned preset_named(const char *name, unsigned fallback)
{
    for (unsigned p = 0; p < (unsigned)NPRESETS; p++)
        if (!strcmp(PRESETS[p].name, name))
            return p;
    return fallback;
}

/* the harmony: a four-bar cycle of chords in the key, each a root (semitones over the key's) and a quality */
static const int8_t Q_TONE[6][4] = {{0, 3, 7, 10}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 6, 10}, {0, 4, 7, 12}, {0, 3, 7, 12}};
static const int8_t CYCLE[8][4][2] = {
    {{0, Q_MIN7}, {5, Q_MIN7}, {0, Q_MIN7}, {7, Q_DOM7}},             /* i  iv  i  V */
    {{0, Q_MIN7}, {8, Q_MAJ7}, {2, Q_HALFDIM}, {7, Q_DOM7}},          /* i  VI  ii  V */
    {{2, Q_HALFDIM}, {7, Q_DOM7}, {0, Q_MIN7}, {0, Q_MIN7}},          /* ii V  i  i */
    {{0, Q_MIN7}, {10, Q_DOM7}, {8, Q_MAJ7}, {7, Q_DOM7}},            /* i  VII VI V */
    {{0, Q_DOM7}, {5, Q_DOM7}, {0, Q_DOM7}, {7, Q_DOM7}},             /* I7 IV7 I7 V7: funk */
    {{2, Q_MIN7}, {7, Q_DOM7}, {0, Q_MAJ7}, {0, Q_MAJ7}},             /* ii V I I: bossa */
    {{0, Q_MAJ}, {7, Q_MAJ}, {9, Q_MIN}, {5, Q_MAJ}},                 /* I V vi IV: punk */
    {{0, Q_MIN7}, {0, Q_MIN7}, {8, Q_MAJ7}, {10, Q_DOM7}}};           /* i i VI VII */
enum { V_SPARSE, V_NORMAL, V_BUSY, V_ANSWER };                        /* the band's moods: a pattern each, per chord */
enum { DK, DSN, DLT, DMT, DHT, DRM, DCP, DCB, DCY, DOH, DCH };         /* the drum lanes */

static void poem_put(step_t *bar, unsigned i, unsigned vel, unsigned len, int nudge)
{
    bar[i].velocity = (uint8_t)vel;
    bar[i].length = (uint8_t)len;
    bar[i].nudge = (int8_t)nudge;
}
/* nudge in sixths of a step, then velocity added. Straight is all zeros. The and of each beat is late in the
 * ride, lean and loose rows; poke and drive stay on the grid and only change how hard a step is. */
#define G(n, v) {n, v}
#define O G(0, 0)
static const int8_t GROOVE[][16][2] = {
    {O, O, O, O, O, O, O, O, O, O, O, O, O, O, O, O}, /* straight */
    {G(0, 6), O, G(3, -8), O, G(0, 10), O, G(3, -8), O, G(0, 4), O, G(3, -8), O, G(0, 10), O, G(3, -6), O}, /* ride */
    {G(0, 8), O, G(2, -4), O, G(1, 12), O, G(2, -4), O, G(0, 4), O, G(2, -4), O, G(1, 8), O, G(2, -4), O}, /* lean */
    {G(0, 4), O, G(2, -6), O, G(0, 8), O, G(2, -6), O, G(0, 4), O, G(2, -6), O, G(0, 8), O, G(2, -4), O}, /* loose */
    {G(0, 16), O, O, G(0, -8), G(0, 4), O, G(0, 12), G(0, -8), G(0, 6), O, G(0, 12), G(0, -8), G(0, 4), O, G(0, 8), O}, /* poke */
    {O, O, O, G(1, 6), O, O, G(1, 8), O, O, O, G(1, 6), O, O, G(1, 6), O, O}, /* push */
    {G(1, 4), G(1, -6), G(1, -8), G(1, -6), G(2, 6), G(1, -6), G(1, -8), G(1, -6), G(1, 4), G(1, -6), G(1, -8), G(1, -6), G(2, 6), G(1, -6), G(1, -8), G(1, -6)}, /* late */
    {G(0, 10), O, G(0, -8), O, G(0, 10), O, G(0, -8), O, G(0, 10), O, G(0, -8), O, G(0, 10), O, G(0, -8), O}, /* drive */
};
#undef G
#undef O
static void poem_note(step_t *bar, unsigned i, unsigned vel, unsigned len, unsigned n)
{
    poem_put(bar, i, vel, len, 0);
    if (bar[i].count < 4)
        bar[i].note[bar[i].count++] = (uint8_t)n;
}
static void poem_hit(step_t *bar, unsigned i, unsigned vel, unsigned lanes)
{
    poem_put(bar, i, vel > bar[i].velocity || !bar[i].drums ? vel : bar[i].velocity, 1, 0);
    bar[i].drums |= (uint16_t)lanes;
    if ((lanes & (1u << DCY | 1u << DOH | 1u << DCH)) && (i & 3u))
        bar[i].drums |= DRUM_ALT;                               /* not the quarters: those stay every time */
    bar[i].count = 1;                                           /* (the engine skips a step that counts none) */
}
static void poem_feel(step_t *bar)
{
    const int8_t (*g)[2] = GROOVE[SETS[poem.set].groove % (sizeof GROOVE / sizeof GROOVE[0])];
    for (unsigned i = 0; i < SONG_STEPS && i < 16; i++) {
        if (!bar[i].count && !(bar[i].drums & DRUM_SOUNDS))
            continue;
        int n = bar[i].nudge + g[i][0];
        bar[i].nudge = (int8_t)(n > 3 ? 3 : n < -3 ? -3 : n);
        int v = (int)bar[i].velocity + g[i][1];
        bar[i].velocity = (uint8_t)(v > 127 ? 127 : v < 1 ? 1 : v);
    }
}

/* a figure for this chord and mood, from the poem's salt (the word picker is left alone) */
static unsigned poem_fig(unsigned a, unsigned b, unsigned n)
{
    uint32_t x = poem_salt ^ a * 0x9E3779B9u ^ b * 0x85EBCA6Bu;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    return n ? x % n : 0;
}

/* the bar's kicks, from the drums' style. The same mask whoever asks. Busy is another figure. Sparse keeps
 * the downbeats of the plain one. */
static uint16_t poem_kicks(unsigned c, unsigned v)
{
    static const uint16_t FIG[8][3] = {
        {0x0401, 0x0421, 0x2401}, /* boom bap */
        {0x1111, 0x1511, 0x5111}, /* swing */
        {0x0101, 0x0111, 0x0501}, /* brushes */
        {0x1111, 0x5555, 0x1515}, /* double */
        {0x0241, 0x0A41, 0x1241}, /* bossa */
        {0x0401, 0x0041, 0x0449}, /* trap */
        {0x1111, 0x2222, 0x4444}, /* bongos: the pulse, then shifted */
        {0x0101, 0x0501, 0x1111}, /* rock */
    };
    unsigned st = poem.style[T_DRUMS] % JAM_STYLES[T_DRUMS];
    if (st >= 8)
        return 0x0101;
    unsigned which = v == V_BUSY ? 2u : v == V_SPARSE ? 0u : poem_fig(st * 4u + c, v, 2);
    uint16_t m = FIG[st][which];
    if (v == V_SPARSE)
        m &= (uint16_t)0x0101;
    return m ? m : 1u;
}

/* one player's bar for chord c in mood v, in its style */
static void poem_bar(step_t *bar, unsigned t, unsigned c, unsigned v)
{
    const int8_t (*cyc)[2] = CYCLE[poem.cycle & 7u];
    unsigned root = poem.key + (unsigned)cyc[c][0], next = poem.key + (unsigned)cyc[(c + 1) & 3][0], st = poem.style[t];
    const int8_t *q = Q_TONE[cyc[c][1]];
    for (unsigned i = 0; i < SONG_STEPS; i++)
        bar[i] = (step_t){0, {0}, 0, 100, 1, 0};
    if (st == JAM_YOURS(t))
        return;                                    /* yours: the band leaves it empty */
    if (t == T_KEYS) {
        /* the 3rd, 7th and 9th over the root, around middle C */
        unsigned vo[3] = {48 + (root + (unsigned)q[1]) % 12, 48 + (root + (unsigned)q[3]) % 12, 48 + (root + 2) % 12};
        for (unsigned k = 0; k < 3; k++)
            vo[k] += vo[k] < 53 ? 12 : 0;
        if (v == V_ANSWER) {                       /* every style answers the poet with a little line up the chord */
            for (unsigned h = 0; h < 4; h++)
                poem_note(bar, 4 + h * 2, 92, 2, 60 + (root + (unsigned)q[h]) % 12);
            return;
        }
        uint16_t kicks = poem_kicks(c, v);
        if (st == KS_POWER) {                      /* power chords on the kicks, the eighths between them quieter */
            unsigned r = 40 + root % 12;
            for (unsigned i = 0; i < 16; i++)
                if (kicks >> i & 1u)
                    for (unsigned k = 0; k < 3; k++)
                        poem_note(bar, i, 116, v == V_BUSY ? 1 : 2, r + (unsigned)"\0\7\14"[k]);
            if (v != V_SPARSE)
                for (unsigned i = 0; i < 16; i += 2)
                    if (!bar[i].count)
                        for (unsigned k = 0; k < 3; k++)
                            poem_note(bar, i, 88, 1, r + (unsigned)"\0\7\14"[k]);
            return;
        }
        if (st == KS_PAD || v == V_SPARSE) {       /* held through the bar */
            for (unsigned k = 0; k < 3; k++)
                poem_note(bar, 0, st == KS_PAD && v == V_BUSY ? 100 : 76, 16, vo[k]);
            return;
        }
        if (st == KS_ARP) {                        /* the chord broken across the kicks */
            unsigned n = 0;
            for (unsigned i = 0; i < 16; i++)
                if (kicks >> i & 1u) {
                    unsigned k = n++;
                    poem_note(bar, i, 88, 2, vo[k % 3] + (k % 4 == 3 ? 12 : 0));
                }
            return;
        }
        if (st == KS_VIBES) {                      /* vibes: a mallet on each kick */
            unsigned n = 0;
            for (unsigned i = 0; i < 16; i++)
                if (kicks >> i & 1u) {
                    unsigned k = n++;
                    poem_note(bar, i, k ? 80 : 96, 2,
                              60 + (root + (unsigned)q[(k * 3u + c) % 4]) % 12 + (k == 2 ? 12 : 0));
                }
            return;
        }
        /* comping on the kicks: rhodes and organ on them, stabs just after, bossa pushed */
        for (unsigned i = 0; i < 16; i++) {
            if (!(kicks >> i & 1u))
                continue;
            unsigned at = st == KS_STABS ? (i + (v == V_BUSY ? 1u : 2u)) & 15u
                        : st == KS_BOSSA ? (i + (i & 4u ? 3u : 0u)) & 15u : i;
            if (bar[at].count)
                continue;
            unsigned len = st == KS_STABS ? 1u : st == KS_ORGAN ? (at % 8 ? 2u : 6u) : st == KS_BOSSA ? 2u : (i ? 2u : 4u);
            for (unsigned k = 0; k < 3; k++)
                poem_note(bar, at, v == V_BUSY ? 104 : 90, len, vo[k]);
            if (v == V_BUSY && st != KS_STABS) {
                unsigned extra = (at + 2u) & 15u;
                if (!bar[extra].count)
                    for (unsigned k = 0; k < 3; k++)
                        poem_note(bar, extra, 84, 1, vo[k]);
            }
        }
        return;
    }
    if (t == T_BASS) {
        unsigned r = 36 + root % 12;
        if (v == V_ANSWER) {                       /* a run up the chord into the next bar */
            for (unsigned h = 0; h < 4; h++)
                poem_note(bar, h * 4, 110, 3, 36 + (root + (unsigned)q[h]) % 12);
            return;
        }
        uint16_t kicks = poem_kicks(c, v);
        if (st == BS_WALK) {                       /* on the kicks: root, a chord tone, then a semitone into the next */
            unsigned chord_toss = prand(2), approach = prand(2);   /* two draws: the word picker is not shifted */
            unsigned hits = 0, n = 0, prev = 0;
            for (unsigned i = 0; i < 16; i++)
                hits += (kicks >> i) & 1u;
            for (unsigned i = 0; i < 16; i++) {
                if (!(kicks >> i & 1u))
                    continue;
                n++;
                unsigned last = n == hits;
                unsigned p = last ? next + (approach ? 11u : 1u)
                                  : n == 1 ? root : root + (unsigned)q[1 + (chord_toss + n) % 3];
                poem_note(bar, i, 112, 2, 36 + p % 12);
                if (n > 1)
                    bar[prev].length = (uint8_t)(i - prev);
                prev = i;
            }
            if (hits)
                bar[prev].length = (uint8_t)(16 - prev);
        } else if (st == BS_BOOM || st == BS_808) { /* the root held on each kick; the last one up when it pushes */
            unsigned lo = st == BS_808 && r >= 41 ? r - 12 : r, hits = 0, n = 0, prev = 0;
            for (unsigned i = 0; i < 16; i++)
                hits += (kicks >> i) & 1u;
            for (unsigned i = 0; i < 16; i++) {
                if (!(kicks >> i & 1u))
                    continue;
                n++;
                unsigned up = n == hits && v != V_SPARSE;
                poem_note(bar, i, n == 1 ? 124 : 112, 2, lo + (up ? 12 : 0));
                if (n > 1)
                    bar[prev].length = (uint8_t)(i - prev);
                prev = i;
            }
            if (hits)
                bar[prev].length = (uint8_t)(16 - prev);
        } else if (st == BS_PEDAL || st == BS_PUNK) { /* eighths, louder where the kick is */
            for (unsigned i = 0; i < 16; i += v == V_SPARSE ? 4 : 2)
                poem_note(bar, i, (kicks >> i & 1u) || !(i % 4) ? 120 : 96, 1, r);
            for (unsigned i = 0; i < 16; i++)
                if ((kicks >> i & 1u) && !bar[i].count)
                    poem_note(bar, i, 120, 2, r);
        } else if (st == BS_BOSSA) {               /* root and fifth, on the kicks */
            unsigned n = 0;
            for (unsigned i = 0; i < 16; i++)
                if (kicks >> i & 1u) {
                    unsigned k = n++;
                    poem_note(bar, i, k & 1u ? 100 : 116, 2, 36 + (root + (unsigned)(k & 1u ? q[2] : 0)) % 12);
                }
        } else {                                   /* funk: the octave and the seventh, on the kicks */
            static const uint8_t IV[4] = {0, 10, 7, 12};
            unsigned n = 0;
            for (unsigned i = 0; i < 16; i++)
                if (kicks >> i & 1u) {
                    unsigned k = n++;
                    poem_note(bar, i, k % 2 ? 100 : 120, 1, r + IV[k % 4]);
                }
        }
        return;
    }
    /* drums */
    if (v == V_ANSWER) {                           /* a fill: the snare, then down the toms */
        poem_hit(bar, 0, 110, 1u << DK);
        for (unsigned i = 8; i < 16; i += 2)
            poem_hit(bar, i, 96 + i, 1u << (i == 8 ? DSN : i == 10 ? DHT : i == 12 ? DMT : DLT));
        poem_hit(bar, 15, 110, 1u << DSN);
        return;
    }
    uint16_t kicks = poem_kicks(c, v);
    if (st != DS_BONGOS) {                         /* the figure: the same kicks the bass and the keys use */
        unsigned vel = st == DS_BRUSHES ? 72u : st == DS_BOSSA ? 96u : 124u;
        for (unsigned i = 0; i < 16; i++)
            if (kicks >> i & 1u)
                poem_hit(bar, i, i ? vel - 8 : vel, 1u << DK);
    }
    if (st == DS_BOOMBAP) {                        /* a cracking snare on two and four, hats around the kicks */
        poem_hit(bar, 4, 124, 1u << DSN);
        poem_hit(bar, 12, 124, 1u << DSN);
        for (unsigned i = 0; i < 16; i += v == V_SPARSE ? 4 : 2)
            poem_hit(bar, i, 76, 1u << DCH);
        if (v == V_BUSY)
            poem_hit(bar, 14, 96, 1u << DOH);
    } else if (st == DS_SWING) {                   /* the ride and its swung skip, the hi-hat foot on two and four */
        for (unsigned i = 0; i < 16; i += 4)
            poem_hit(bar, i, 96, 1u << DCY);
        if (v != V_SPARSE)
            poem_hit(bar, 6, 80, 1u << DCY), poem_hit(bar, 14, 80, 1u << DCY);
        poem_hit(bar, 4, 90, 1u << DCH);
        poem_hit(bar, 12, 90, 1u << DCH);
        if (v != V_SPARSE)
            poem_hit(bar, 15, 60, 1u << DSN);
        if (v == V_BUSY)
            poem_hit(bar, 6, 70, 1u << DSN);
    } else if (st == DS_BRUSHES) {                 /* soft: the cross-stick on four, the ride on the beats */
        for (unsigned i = 0; i < 16; i += 4)
            poem_hit(bar, i, 70, 1u << DCY);
        poem_hit(bar, 12, 84, 1u << DRM);
        if (v != V_SPARSE)
            poem_hit(bar, 6, 56, 1u << DCY);
    } else if (st == DS_BOSSA) {                   /* bossa: the rim's clave, soft eighths, the kick from the figure */
        static const uint8_t CL[5] = {0, 3, 6, 10, 12};
        for (unsigned n = 0; n < 5; n++)
            poem_hit(bar, CL[n], 90, 1u << DRM);
        for (unsigned i = 0; i < 16; i += 2)
            poem_hit(bar, i, i % 4 ? 50 : 64, 1u << DCH);
    } else if (st == DS_TRAP) {                    /* trap: the clap on two and four, the hats in sixteenths with
                                                    * triplet rolls (nudged a third), the open hat */
        poem_hit(bar, 4, 124, 1u << DSN | 1u << DCP);
        poem_hit(bar, 12, 124, 1u << DSN | 1u << DCP);
        for (unsigned i = 0; i < 16; i++) {
            unsigned roll = (i >= 12 || (v == V_BUSY && i >= 4 && i < 8)) && v != V_SPARSE, at = i & 3u;
            if (roll ? at == 2 : v == V_SPARSE && (i & 1u))
                continue;                          /* (a roll's beat: three, not four) */
            poem_hit(bar, i, at ? 64 + (i * 7u) % 3u * 10u : 92, 1u << DCH);
            if (roll && at)
                bar[i].nudge = at == 1 ? 2 : -2;   /* the beat in thirds: on it, a third in, two thirds in */
        }
        if (v != V_SPARSE)
            poem_hit(bar, 6, 84, 1u << DOH);
    } else if (st == DS_BONGOS) {                  /* bongos (the toms up high): the coffee house's beat, shifted */
        static const uint8_t HI[6] = {0, 3, 6, 8, 11, 14}, LO[3] = {2, 10, 12};
        unsigned shift = v == V_BUSY ? 2u : poem_fig(c, v, 3);
        for (unsigned n = 0; n < 6; n++)
            poem_hit(bar, (HI[n] + shift) & 15u, n % 3 ? 84 : 108, 1u << DHT);
        for (unsigned n = 0; n < 3 && v != V_SPARSE; n++)
            poem_hit(bar, (LO[n] + shift) & 15u, 96, 1u << DMT);
        for (unsigned i = 0; i < 16; i += 2)
            poem_hit(bar, i, 44, 1u << DCH);
        if (v == V_BUSY)
            poem_hit(bar, 4, 90, 1u << DRM), poem_hit(bar, 12, 90, 1u << DRM);
    } else if (st == DS_ROCK) {                    /* the snare on two and four, hats on the eighths */
        if (v == V_BUSY)
            poem_hit(bar, 0, 124, 1u << DCY);
        poem_hit(bar, 4, 124, 1u << DSN);
        poem_hit(bar, 12, 124, 1u << DSN);
        for (unsigned i = 0; i < 16; i += 2)
            poem_hit(bar, i, i % 4 ? 80 : 100, 1u << (v == V_BUSY ? DOH : DCH));
    } else {                                       /* double time: sixteenth hats, the backbeat */
        for (unsigned i = 0; i < 16; i += v == V_SPARSE ? 2 : 1)
            poem_hit(bar, i, i % 2 ? 64 : 84, 1u << DCH);
        poem_hit(bar, 4, 124, 1u << DSN);
        poem_hit(bar, 12, 124, 1u << DSN);
        if (v == V_BUSY)
            poem_hit(bar, 15, 100, 1u << DOH);
    }
}

/* a player's whole part (its 16 bars of material) in its style, and its sound; while the song plays too */
static void poem_player(song_t *s, unsigned t)
{
    step_t bar[SONG_STEPS];
    for (unsigned c = 0; c < 4; c++)
        for (unsigned v = 0; v < 4; v++) {
            poem_bar(bar, t, c, v);
            poem_feel(bar);
            memcpy(s->pattern[c * 4u + v].step[t - 1], bar, sizeof bar);
        }
    unsigned st = poem.style[t] % JAM_STYLES[t];
    if (st == JAM_YOURS(t))
        return;                                    /* (its sound and level as you left them) */
    s->track[t].level = JAM_LEVEL[t - 1][st];
    s->track[t].sound = (uint8_t)(t == T_DRUMS ? JAM_KIT[st] : preset_named(JAM_SOUND[t - 1][st], s->track[t].sound));
}

/* the nearest note of the key's pentatonic, minor or major as the set is (the voice sits in the band's harmony) */
static int poem_snap(int p, unsigned key)
{
    uint16_t PENT = SETS[poem.set].major ? 1u << 0 | 1u << 2 | 1u << 4 | 1u << 7 | 1u << 9
                                         : 1u << 0 | 1u << 3 | 1u << 5 | 1u << 7 | 1u << 10;
    for (int d = 0; d < 6; d++)
        for (int sg = 1; sg >= -1; sg -= 2) {
            int q = p + d * sg, r = ((q - (int)key) % 12 + 12) % 12;
            if (PENT >> r & 1u)
                return q;
        }
    return p;
}

/* a line's notes, said as a slam poet says it: a syllable an eighth, swung (rap: sixteenths, sing: longer and
 * wider); the hero's name long and high; a breath at a comma, a longer one at a full stop; the last syllable
 * held, rising for a question, falling at the end of the poem; lines that open on a small word come in on a
 * pickup. Returns where the line starts against its two bars (-2: a pickup) */
static int poem_notes(song_t *s, unsigned line, const char *text, const uint8_t *off, unsigned syl, unsigned tone,
                      unsigned kind, int min_lead)
{
    line_t *L = &s->line[line];
    L->first = s->notes;
    L->count = 0;
    unsigned style = poem.style[T_VOICE], unit = style == PS_RAP ? 1u : style == PS_SING ? 3u : 2u;
    char hero[24];
    poem_pick(PG_TOPIC(poem.topic, PC_HERO), poem.hero, hero, sizeof hero);
    unsigned hl = 0;
    while (hero[hl] && hero[hl] != ' ' && hero[hl] != '/')
        hl++;
    uint8_t len[64], gap[64], hot[64];
    unsigned total = 0;
    for (unsigned i = 0; i < syl; i++) {
        unsigned w = off[i], first = !i || off[i - 1] != off[i], last = i + 1 == syl || off[i + 1] != off[i], e = w;
        while (text[e] && text[e] != ' ')
            e++;
        char c = e > w ? text[e - 1] : 0;
        unsigned same = first && e - w >= hl;
        for (unsigned k = 0; same && k < hl; k++)
            same = (text[w + k] | 32) == hero[k];
        hot[i] = (uint8_t)same;
        len[i] = (uint8_t)(hot[i] ? unit * 2u : unit);
        gap[i] = (uint8_t)(!last || i + 1 == syl ? 0 : c == ',' ? 2 : c == '.' || c == '!' || c == '?' ? 4 : 0);
        total += len[i] + gap[i];
    }
    total += 4u - len[syl - 1];
    len[syl - 1] = 4;
    /* too long for its two bars: the breaths shorter, then the emphasis. Above 84 a crowded line stops at
     * an eighth (a sixteenth there is too short to hear). At 84 and under it may fall to sixteenths. */
    for (unsigned i = 0; i < syl && total > 30; i++)
        if (gap[i])
            total -= gap[i] - gap[i] / 2, gap[i] /= 2;
    for (unsigned i = 0; i < syl && total > 30; i++)
        if (hot[i] && i + 1 < syl)
            total -= len[i] - unit, len[i] = (uint8_t)unit;
    unsigned floor = s->bpm > 84 ? (unit > 2 ? 2u : unit) : 1u;
    for (unsigned u = unit; u > floor && total > 30; u--)
        for (unsigned i = 0; i + 1 < syl && total > 30; i++)
            if (len[i] == u)
                len[i]--, total--;
    int lead = 0;
    unsigned small = off[0] == 0 && (text[1] == ' ' || !strncmp(text, "The ", 4) || !strncmp(text, "And ", 4));
    if (small && total <= 30)
        lead = -2;                                 /* in on the and of four */
    else if (total <= 22)
        lead = 2 * (int)prand(3);                  /* a short line comes in late, on the and of one or on two */
    if (lead < min_lead)
        lead = min_lead;                           /* (never over the line before's last word) */
    unsigned at = 0;
    const int8_t (*cyc)[2] = CYCLE[poem.cycle & 7u];
    for (unsigned i = 0; i < syl && s->notes < SONG_NOTES; i++) {
        int p = (int)tone;
        if (style == PS_SING)                      /* sung: the chord's tones, walking up and down the line */
            p = (int)tone + Q_TONE[cyc[(i / 4) & 3][1]][(i * 3u + (unsigned)prand(2)) % 4] - 3;
        if (i + 1 == syl)
            p += kind == PK_ASK ? 5 : kind == PK_RETURN || kind == PK_CLOSE ? -5 : -3;
        else if (hot[i])
            p += 5;
        else if (i && gap[i - 1])
            p -= 2;                                /* after a breath: a step down */
        else if (style == PS_CHANT && i % 3 == 2)
            p += (int)prand(5) - 2;
        p = poem_snap(p, poem.key);
        while (p > 64)                             /* E4 and above this voice thins out; the reciting tone stays under C4 */
            p -= 12;
        p = poem_snap(p, poem.key);
        int swing = 0;
        if (style != PS_RAP) {                     /* rap stays on the sixteenths; the band still leans */
            int pos = (lead + (int)at + 64) & 15;
            swing = GROOVE[SETS[poem.set].groove % (sizeof GROOVE / sizeof GROOVE[0])][pos][0];
        }
        note_t *n = &s->note[s->notes++];
        *n = (note_t){(uint16_t)at, len[i], (uint8_t)(p < 36 ? 36 : p > 84 ? 84 : p), (uint8_t)i, (uint8_t)((swing & 7) << 1)};
        at += len[i] + gap[i];
        L->count++;
    }
    return lead;
}

/* the voice: every planned row's line said in the poet's style (the words stay; the notes are made again) */
static void poem_voice(song_t *s)
{
    int lead[SONG_LINES];
    unsigned made = 0;
    s->notes = 0;
    s->rows = 0;
    for (unsigned k = 0; k < poem.nrows; k++) {
        unsigned l = poem.plan[k].line, slot = poem.plan[k].slot;
        int min_lead = s->rows ? (int)(s->row[s->rows - 1].at + line_span(s, s->row[s->rows - 1].line)) + 1 - (int)slot : -2;
        if (l >= made) {                           /* (the lines are said first in their order: the notes stay in line order) */
            uint8_t off[64];
            const char *text = s->line[l].text;
            int syl = vocal_syllables(text, off);
            if (syl < 1)
                syl = 1, off[0] = 0;
            lead[l] = poem_notes(s, l, text, off, (unsigned)(syl > 64 ? 64 : syl),
                                 poem.tone + (poem.plan[k].kind == PK_RISE ? 3u : 0u), poem.plan[k].kind, min_lead);
            made = l + 1;
        }
        int at = (int)slot + (lead[l] < min_lead ? min_lead : lead[l]);
        if (s->rows < SONG_ROWS && at >= 0)
            s->row[s->rows++] = (row_t){(uint16_t)at, (uint8_t)l, 0};
    }
    for (unsigned l = made; l < SONG_LINES; l++)
        s->line[l].first = s->notes, s->line[l].count = 0;
}

/* a poem's band, kept in the song's spare bytes so a saved poem knows itself: SLAMJAM's, and its players */
#define POEM_MARK 0x80u
static void poem_keep(song_t *s)
{
    unsigned yours = 0, st[NTRACKS];
    for (unsigned t = 0; t < NTRACKS; t++) {
        st[t] = poem.style[t] % JAM_STYLES[t];
        if (t && st[t] == JAM_YOURS(t))
            yours |= 1u << t, st[t] = 0;
    }
    s->spare0 = (uint8_t)(POEM_MARK | poem.set | (poem.cycle & 7u) << 3 | (yours >> 1 & 1u) << 6);
    s->spare1 = (uint16_t)(st[0] | st[1] << 2 | st[2] << 5 | st[3] << 8 | poem.core << 11 | (yours >> 2) << 14);
}
/* a loaded song: 1 if it is a poem, its band back (the rows give the plan, each line a two-bar slot), else 0 */
static int poem_restore(const song_t *s)
{
    if (!(s->spare0 & POEM_MARK))
        return 0;
    poem.set = (uint8_t)(s->spare0 & 7u) % NSETS;
    poem.cycle = (uint8_t)(s->spare0 >> 3 & 7u);
    for (unsigned t = 0; t < NTRACKS; t++)
        poem.style[t] = (uint8_t)((s->spare1 >> (t ? 3u * t - 1u : 0u) & (t ? 7u : 3u)) % JAM_STYLES[t]);
    if (s->spare0 >> 6 & 1u)                       /* (the players left to you) */
        poem.style[T_KEYS] = (uint8_t)JAM_YOURS(T_KEYS);
    for (unsigned t = T_BASS; t < NTRACKS; t++)
        if (s->spare1 >> (14u + t - T_BASS) & 1u)
            poem.style[t] = (uint8_t)JAM_YOURS(t);
    poem.core = poem.core2 = (uint8_t)(s->spare1 >> 11 & 7u);
    poem_salt = 2166136261u;
    for (unsigned i = 0; i < sizeof s->name && s->name[i]; i++)
        poem_salt = (poem_salt ^ (uint8_t)s->name[i]) * 16777619u;
    if (!poem_salt)
        poem_salt = 1;
    poem.key = s->root;
    poem.tone = (uint8_t)(55 + poem.key % 12 > 60 ? 43 + poem.key % 12 : 55 + poem.key % 12);
    poem.nrows = 0;
    for (unsigned r = 0; r < s->rows && poem.nrows < 24; r++)
        poem.plan[poem.nrows++] = (typeof(poem.plan[0])){(uint16_t)((s->row[r].at + 2u) / 32u * 32u), s->row[r].line,
                                                          (uint8_t)(r ? PK_IMAGE : PK_TITLE)};
    return 1;
}

/* a player's style: its part rewritten now (the poet's notes too: the words stay), playing or not */
static void poem_style(song_t *s, unsigned t, unsigned style)
{
    poem.style[t] = (uint8_t)(style % JAM_STYLES[t]);
    if (t == T_VOICE && poem.style[t] == PS_RAP && s->bpm > 84) { /* sixteenths only while a word still lands */
        s->bpm = 84;
        engine_tempo();
    }
    if (t == T_VOICE) {
        poem_voice(s);
        vocal_reset();
    } else {
        poem_player(s, t);
        engine_sound(t);
        if (t == T_DRUMS) {                        /* the kick is the plan: the others land on the new one */
            for (unsigned o = T_KEYS; o <= T_BASS; o++)
                if (poem.style[o] % JAM_STYLES[o] != JAM_YOURS(o)) {
                    poem_player(s, o);
                    engine_sound(o);
                }
        }
    }
    poem_keep(s);
}

/* the poem moved to the song's ROOT (GLO): the band and the poet in the new key */
static void poem_rekey(song_t *s)
{
    poem.key = s->root;
    poem.tone = (uint8_t)(55 + poem.key % 12 > 60 ? 43 + poem.key % 12 : 55 + poem.key % 12);
    for (unsigned t = 0; t < NTRACKS; t++)
        if (!t || poem.style[t] % JAM_STYLES[t] != JAM_YOURS(t))   /* (your parts stay as you played them) */
            poem_style(s, t, poem.style[t]);
}

/* the whole performance into s: 0, or -1 if it could not be made (s is then a blank song) */
__attribute__((noinline)) static int poem_make(song_t *s, uint32_t seed, unsigned topic)
{
    poem_seed = seed ? seed : 1u;
    for (unsigned i = 0; i < 4; i++)
        prand(0);                                  /* (stirred: close seeds, different poems) */
    poem_salt = poem_seed;                         /* the figures; the word picker keeps drawing after this */
    poem.topic = (uint8_t)(topic % POEM_TOPICS);
    poem.frame = (uint8_t)prand(POEM_FRAMES);
    poem.set = (uint8_t)prand(NSETS);
    poem.core = (uint8_t)prand(POEM_CORES);
    poem.core2 = (uint8_t)prand(POEM_CORES);       /* its support: any core, the lead's own as likely (a pure one) */
    poem.hero = (uint8_t)prand(POEM_GROUP[PG_TOPIC(poem.topic, PC_HERO)][1]);
    memset(poem.used, 0, sizeof poem.used);
    for (unsigned t = 0; t < NTRACKS; t++)
        poem.style[t] = SETS[poem.set].style[t];
    song_blank(s);
    char text[64];
    for (unsigned tries = 0; tries < 8 && !poem_line(PK_TITLE, text, 24, ""); tries++)
        ;
    memset(s->name, 0, sizeof s->name);
    s_copy(s->name, text, sizeof s->name);
    unsigned bpm = SETS[poem.set].bpm + prand(SETS[poem.set].span / 4u + 1u) * 4u;
    unsigned hi = poem.style[T_VOICE] == PS_RAP ? 84u : 120u; /* an eighth at 120 is 250 ms; a sixteenth at 84 is 179 */
    s->bpm = (uint16_t)(bpm > hi ? hi : bpm);
    s->root = (uint8_t)("\0\2\5\7\11\12"[prand(6)]);
    s->scale = SETS[poem.set].scale;               /* the keys you jam on fit the band */
    s->steps = 16;                                 /* a poem is 4/4: the lines sit on sixteen */
    s->track[T_VOICE].reverb = 30;
    s->voice[V_VIBRATO] = 8;
    s->voice[V_GLIDE] = 0;                         /* the words land; the knob still slides them */
    s->fx[FX_REVERB_SIZE] = 62;
    do
        poem.cycle = (uint8_t)prand(8);
    while (!(SETS[poem.set].cycles >> poem.cycle & 1u));
    poem.key = s->root;
    poem.tone = (uint8_t)(55 + poem.key % 12 > 60 ? 43 + poem.key % 12 : 55 + poem.key % 12);
    for (unsigned t = T_KEYS; t < NTRACKS; t++)
        poem_player(s, t);
    /* the arc: (bars, mood, lines' kinds) */
    static const uint8_t ARC[][6] = {
        {4, V_SPARSE, 255},                                         /* intro (the title said in it) */
        {8, V_NORMAL, PK_OPEN, PK_IMAGE, PK_IMAGE, PK_ASK},
        {2, V_ANSWER, 255},
        {8, V_NORMAL, PK_REFRAIN, PK_IMAGE, PK_RISE, PK_ASK},
        {2, V_ANSWER, 255},
        {8, V_BUSY, PK_REFRAIN, PK_VOW, PK_RISE, PK_VOW},
        {2, 254, 255},                                              /* the break: stop time */
        {8, V_SPARSE, PK_RETURN, PK_IMAGE, PK_REFRAIN, PK_CLOSE},
        {4, V_SPARSE, 255}};                                        /* outro */
    unsigned bar = 0, line = 0, refrain = 255;
    poem.nrows = 0;
    {                                              /* in the intro, as a poet at a mic does: what this one is called */
        char t[64];
        for (unsigned tries = 0; tries < 8 && !poem_line(PK_INTRO, t, 64, s->name); tries++)
            ;
        line_set_text(s, 0, t);
        poem.plan[poem.nrows++] = (typeof(poem.plan[0])){32, 0, PK_TITLE};
        line = 1;
    }
    for (unsigned a = 0; a < sizeof ARC / sizeof ARC[0]; a++) {
        for (unsigned b = 0; b < ARC[a][0]; b++, bar++)
            for (unsigned t = T_KEYS; t < NTRACKS; t++)
                s->lane[t][bar] = (uint8_t)(ARC[a][1] == 254 ? (t == T_KEYS ? (bar & 3u) * 4u + V_SPARSE : LANE_REST)
                                                             : (bar & 3u) * 4u + ARC[a][1]);
        for (unsigned l = 2; l < 6 && ARC[a][l] != 255 && poem.nrows < 24; l++) {
            unsigned kind = ARC[a][l], slot = (bar - ARC[a][0] + (l - 2) * 2u) * 16u, use = line;
            if (kind == PK_REFRAIN && refrain != 255)
                use = refrain;                     /* the same line again: the refrain */
            else {
                int syl = 0;
                for (unsigned tries = 0; tries < 8 && (syl < 2 || syl > 24); tries++)
                    syl = poem_line(kind, text, 64, "") ? vocal_syllables(text, 0) : 0;
                if (syl < 2 || syl > 24 || line >= SONG_LINES)
                    continue;
                line_set_text(s, line, text);
                if (kind == PK_REFRAIN)
                    refrain = line;
                line++;
            }
            poem.plan[poem.nrows++] = (typeof(poem.plan[0])){(uint16_t)slot, (uint8_t)use, (uint8_t)kind};
        }
    }
    poem.nlines = (uint8_t)line;
    poem_voice(s);
    poem_keep(s);
    s->bars = (uint8_t)bar;
    for (unsigned t = 0; t < NTRACKS; t++) {
        s->lane_bars[t] = (uint8_t)bar;
        s->lane[t][0] |= LANE_START;
    }
    if (!song_valid(s)) {
        song_blank(s);
        return -1;
    }
    return 0;
}
