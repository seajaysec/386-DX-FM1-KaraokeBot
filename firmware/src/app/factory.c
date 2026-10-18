/* SPDX-License-Identifier: GPL-3.0-only */
/* The demo songs: song images made by the companion's importer at build time (tools/gen_songs.py),
 * packed (a byte-oriented LZ: literal runs and back references) into flash. */
#include "song.h"
#include <string.h>
typedef struct { const char *name; const uint8_t *data; unsigned size; } demo_t;
#include "song_demos.h"

int factory_count(void) { return (int)(sizeof demos / sizeof demos[0]); }
const char *factory_name(int i) { return i >= 0 && i < factory_count() ? demos[i].name : ""; }

int factory_load(int index, song_t *s)
{
    if (index < 0 || index >= factory_count())
        return -1;
    const demo_t *d = &demos[index];
    uint8_t *out = (uint8_t *)s;
    unsigned pos = 0, at = 0;
    while (at < d->size) {
        unsigned token = d->data[at++], n = (token & 127u) + (token & 128u ? 3u : 1u);
        if (pos + n > sizeof *s)
            return -1;
        if (token & 128u) {
            if (at >= d->size)
                return -1;
            unsigned off = d->data[at++];
            if (off & 128u) {
                if (at >= d->size)
                    return -1;
                off = (off & 127u) | (unsigned)d->data[at++] << 7;
            }
            if (!off || off > pos)
                return -1;
            while (n--) {
                out[pos] = out[pos - off];
                pos++;
            }
        } else {
            if (at + n > d->size)
                return -1;
            memcpy(out + pos, d->data + at, n);
            pos += n;
            at += n;
        }
    }
    return pos == sizeof *s && song_valid(s) ? 0 : -1;
}
