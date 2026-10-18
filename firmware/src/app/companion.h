/* SPDX-License-Identifier: GPL-3.0-only */
/* The companion page's protocol: SysEx F0 7D 4F 4D cmd data.. F7 over USB MIDI, a reply to each
 * request (the same framing, the command first). Numbers are little-endian base 128. Part of the unity
 * build (ui.c).
 *
 *   2  buttons LE7x2, keys LE7x4      the panel held from the page (no reply)
 *   3  encoder, delta + 64            a turn from the page (no reply)
 *  32  -                              status: "386DX", protocol, playing, page, track, slot, unsaved,
 *                                     tempo LE7x2, the row and note sung, the song's name
 *  40  size LE7x2, hash LE7x5, slot  start sending a song to a slot (stopped; the song being played or
 *                                     edited is not touched)
 *  41  offset LE7x2, bytes as pairs   a piece of it
 *  42  -                              the end: checked, then written to the slot (error 3: not a 386DX song)
 *  Flash is not touched while the voice is being prepared: 42, 49 (index 0), 50 and 51 reply error 5, ask again
 *  43  offset LE7x2                   read the song: error, size LE7x2, count, bytes as pairs
 *  44  text                           syllables the speech engine finds: count (127: none)
 *  45  text                           the same, and each syllable's shortest length, ms LE7x2
 *  47  -                              stop
 *  49  index                          the eight slots: error, total, index, kind (1 song, 2 empty),
 *                                     name
 *  51  slot                           the slot emptied: error
 *  53                                 the screen: page, list/words/ask bits, rows, row, notes, cpu, level, message, line,
 *                                     bar, mute bits, missed syllables LE7x2
 *  52                                 restart (a soft reset once answered, as at power-on)
 *  50  slot, offset LE7x3             a slot's stored bytes as they are (for a download):
 *                                     error, length LE7x3, count, bytes as pairs */
#define PROTOCOL 8
static uint16_t rx_at, rx_size;
static uint32_t rx_expected, rx_time;
static uint8_t rx_on, rx_slot;

static void companion_send(const uint8_t *p, unsigned n)
{
    while (n) {
        unsigned take = n > 3 ? 3 : n;
        uint32_t pkt = n > 3 ? 4 : take == 3 ? 7 : take == 2 ? 6 : 5;
        for (unsigned i = 0; i < take; i++)
            pkt |= (uint32_t)p[i] << (8 + 8 * i);
        plat_midi_out(pkt);
        p += take;
        n -= take;
    }
}
static unsigned le7(uint8_t *p, uint32_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        p[i] = (uint8_t)((v >> (7 * i)) & 127u);
    return n;
}
static uint32_t rd7(const uint8_t *p, unsigned n)
{
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++)
        v |= (uint32_t)(p[i] & 127u) << (7 * i);
    return v;
}

static int busy_for_song(void) { return eng_playing || ui.words || ui.ask; }
/* the speech engine runs on the second core from flash, and a flash access (a write, an erase, even a
 * read) stops it: while it works the main loop is still answering the page, so a request that touches
 * flash is told to ask again (reply 5) */
static int speech_busy(void) { return vocal_preparing || dx_speech_active(); }

static void companion_reply(const uint8_t *c, unsigned n)
{
    uint8_t r[200] = {0xF0, 0x7D, 0x4F, 0x4D};
    unsigned i = 4;
    r[i++] = c[0];
    if (speech_busy() && (c[0] == 42 || c[0] == 50 || c[0] == 51 || (c[0] == 49 && !c[1]))) {
        r[i++] = 5;
        r[i++] = 0xF7;
        companion_send(r, i);
        return;
    }
    switch (c[0]) {
    case 32: {
        memcpy(r + i, "386DX", 5);
        i += 5;
        r[i++] = PROTOCOL;
        r[i++] = eng_playing;
        r[i++] = ui.page;
        r[i++] = ui.track;
        r[i++] = ui.slot < 0 ? 127 : (uint8_t)ui.slot;
        r[i++] = ui.dirty;
        i += le7(r + i, song.bpm, 2);
        i += le7(r + i, (uint32_t)(eng_sing_row + 1), 1);
        i += le7(r + i, (uint32_t)(eng_sing_note + 1), 2);
        i += le7(r + i, eng_tick, 2);
        for (unsigned j = 0; j < 24; j++)
            r[i++] = (uint8_t)(song.name[j] & 127);
        for (const char *v = DX_BUILD; *v && i < 70; v++)    /* the build: older pages read only up to the name */
            r[i++] = (uint8_t)(*v & 127);
        break;
    }
    case 40: {
        uint32_t size = rd7(c + 1, 2);
        unsigned err = busy_for_song() ? 1 : n != 9 || size != sizeof song || c[8] >= SONG_SLOTS ? 3 : 0;
        r[i++] = (uint8_t)err;
        if (!err) {
            rx_on = 1;
            rx_size = (uint16_t)size;
            rx_at = 0;
            rx_slot = c[8];
            rx_expected = rd7(c + 3, 5);
            rx_time = plat_ms();
            ui.undo_valid = ui.redo_valid = 0;         /* the song arrives in the undo buffer */
        }
        break;
    }
    case 41: {
        uint32_t off = rd7(c + 1, 2), take = (n - 3) / 2;
        unsigned err = !rx_on || busy_for_song() || plat_ms() - rx_time > 5000u ? 1 : off != rx_at || off + take > rx_size ? 3 : 0;
        r[i++] = (uint8_t)err;
        if (!err) {
            uint8_t *dst = (uint8_t *)&undo_song;
            for (unsigned j = 0; j < take; j++)
                dst[rx_at++] = (uint8_t)(c[3 + j * 2] | c[4 + j * 2] << 7);
            rx_time = plat_ms();
        }
        break;
    }
    case 42: {                                        /* the song goes to its slot from the buffer: no scratch song */
        unsigned err = !rx_on || busy_for_song() ? 1 : rx_at != rx_size || !song_valid(&undo_song) ? 3 : 0;
        if (!err) {
            uint32_t h = 2166136261u;                  /* what the page sent, and what edits left in the buffer */
            for (unsigned j = 0; j < sizeof song; j++)
                h = (h ^ ((const uint8_t *)&undo_song)[j]) * 16777619u;
            if (h != rx_expected)
                err = 3;
        }
        rx_on = 0;
        if (!err && plat_store_save(OBJ_BANK0 + rx_slot, &undo_song, sizeof song) != 0)
            err = 2;
        r[i++] = (uint8_t)err;
        if (!err) {
            if (ui.slot == (int8_t)rx_slot)            /* the song being edited came from this slot: it is the new one */
                load_entry(rx_slot);
            slots_read();
        }
        break;
    }
    case 43: {
        uint32_t off = rd7(c + 1, 2), take = off < sizeof song ? sizeof song - off : 0;
        if (take > 32)
            take = 32;
        r[i++] = 0;
        i += le7(r + i, sizeof song, 2);
        r[i++] = (uint8_t)take;
        const uint8_t *src = (const uint8_t *)&song;
        for (unsigned j = 0; j < take; j++) {
            r[i++] = src[off + j] & 127u;
            r[i++] = src[off + j] >> 7;
        }
        break;
    }
    case 44:
    case 45: {
        char text[SONG_TEXT];
        unsigned len = n - 1 < SONG_TEXT - 1 ? n - 1 : SONG_TEXT - 1, err = vocal_preparing || dx_speech_active() ? 1 : 0;
        for (unsigned j = 0; j < len; j++) {
            text[j] = (char)c[1 + j];
            if (c[1 + j] < 32 || c[1 + j] > 126)
                err = 3;
        }
        text[len] = 0;
        r[i++] = (uint8_t)err;
        if (!err) {
            uint16_t minimum[64];
            int count = c[0] == 45 ? dx_speech_articulation(text, minimum) : dx_speech_scan(text);
            r[i++] = (uint8_t)(count < 0 || count > 64 ? 127 : count);
            if (c[0] == 45 && count > 0 && count <= 64)
                for (unsigned j = 0; j < (unsigned)count; j++)
                    i += le7(r + i, minimum[j], 2);
        }
        break;
    }
    case 51:                                          /* a slot emptied; the song playing stays */
        if (n != 2 || c[1] >= SONG_SLOTS || plat_store_forget(OBJ_BANK0 + c[1]) != 0)
            r[i++] = 2;
        else {
            if (ui.slot == (int8_t)c[1]) {
                ui.slot = -1;
                ui.dirty = 1;
            }
            slots_read();
            r[i++] = 0;
        }
        break;
    case 52:                                          /* a soft reset in a moment, once this is answered */
        engine_play(0, 0);
        ui.restart_at = plat_ms() + 400u;
        if (!ui.restart_at)
            ui.restart_at = 1;
        r[i++] = 0;
        break;
    case 53: {                                        /* what the screen shows, for a test driver */
        r[i++] = ui.page;
        r[i++] = (uint8_t)(ui.list | ui.words << 2 | (ui.ask ? 8 : 0) | ui.moving << 4 | ui.entry << 5 | ui.srec << 6);
        r[i++] = song.rows;
        r[i++] = ui.row;
        i += le7(r + i, song.notes, 2);
        r[i++] = (uint8_t)(plat_cpu_pct() > 127 ? 127 : plat_cpu_pct());
        r[i++] = (uint8_t)(eng_sing_level >> 1);
        for (unsigned j = 0; j < 32; j++)
            r[i++] = (uint8_t)((j < 16 ? ui.msg[0][j] : ui.msg[1][j - 16]) & 127);
        const char *t = ui.words ? ui.wtext : ui.row < song.rows ? song.line[song.row[ui.row].line].text : "";
        for (unsigned j = 0; j < 32; j++)
            r[i++] = (uint8_t)(t[j] & 127);
        r[i++] = ui.bar;
        r[i++] = (uint8_t)(song.track[0].mute | song.track[1].mute << 1 | song.track[2].mute << 2 | song.track[3].mute << 3);
        i += le7(r + i, vocal_misses > 16383u ? 16383u : vocal_misses, 2);
        break;
    }
    case 47:
        engine_play(0, 0);
        r[i++] = 0;
        break;
    case 49: {
        unsigned index = c[1], total = SONG_SLOTS;
        if (!index)
            slots_read();                              /* the eight slots are checked once a listing, not per request */
        r[i++] = index >= total ? 3 : 0;
        r[i++] = (uint8_t)total;
        r[i++] = (uint8_t)index;
        if (index < total) {
            const char *name = ui.names[index];
            r[i++] = (ui.present >> index & 1u) ? 1 : 2;
            for (unsigned j = 0; j < 24; j++)
                r[i++] = (uint8_t)(j < strlen(name) ? name[j] & 127 : 0);
        }
        break;
    }
    case 50: {
        unsigned slot = c[1];
        uint32_t off = rd7(c + 2, 3);
        uint8_t buf[32];
        int len = slot < SONG_SLOTS ? plat_store_read(OBJ_BANK0 + slot, off, buf, sizeof buf) : -1;
        r[i++] = len < 0 ? 2 : 0;
        i += le7(r + i, len < 0 ? 0 : (uint32_t)len, 3);
        unsigned take = len > 0 && off < (uint32_t)len ? ((uint32_t)len - off > 32 ? 32 : (uint32_t)len - off) : 0;
        r[i++] = (uint8_t)take;
        for (unsigned j = 0; j < take; j++) {
            r[i++] = buf[j] & 127u;
            r[i++] = buf[j] >> 7;
        }
        break;
    }
    default:
        r[i++] = 2;
        break;
    }
    r[i++] = 0xF7;
    companion_send(r, i);
}

static void companion_message(const uint8_t *c, unsigned n)
{
    if (!n)
        return;
    if (c[0] >= 32) {
        companion_reply(c, n);
        return;
    }
    remote_at = plat_ms();
    if (c[0] == 3 && n == 3 && c[1] < NE) {
        int d = remote_enc[c[1]] + (int)c[2] - 64;
        remote_enc[c[1]] = (int8_t)(d < -100 ? -100 : d > 100 ? 100 : d);
    }
    if (c[0] == 2 && n == 7) {
        uint32_t nb = rd7(c + 1, 2), nk = rd7(c + 3, 4);
        remote_tap_b |= nb & ~remote_buttons;           /* a press is kept until the main loop has seen it */
        remote_tap_k |= nk & ~remote_keys;
        remote_buttons = nb;
        remote_keys = nk;
    }
}

static void companion_packet(uint32_t pkt, unsigned n)
{
    static uint8_t sx[160];
    static unsigned len, on;
    for (unsigned i = 0; i < n; i++) {
        uint8_t b = (uint8_t)(pkt >> (8 + 8 * i));
        if (b == 0xF0) {
            len = 0;
            on = 1;
        } else if (!on)
            continue;
        else if (b == 0xF7) {
            if (len >= 4 && sx[0] == 0x7D && sx[1] == 0x4F && sx[2] == 0x4D)
                companion_message(sx + 3, len - 3);
            on = 0;
        } else if ((b & 128u) || len >= sizeof sx)
            on = 0;
        else
            sx[len++] = b;
    }
}
