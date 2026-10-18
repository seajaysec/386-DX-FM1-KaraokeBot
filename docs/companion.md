# The companion protocol

The companion page talks to 386DX over USB MIDI with SysEx: `F0 7D 4F 4D cmd data… F7`, a reply to
each request in the same framing with the command first. Numbers are little-endian base 128; bytes
travel as two 7-bit halves (low, then high). Requests are sent one at a time. Protocol version 8.

| Command | Request | Reply |
|---|---|---|
| 2 | buttons LE7×2, keys LE7×4 | the panel held from the page, for 2 s (no reply) |
| 32 | — | `386DX`, protocol, playing, page, track, slot (127 none), unsaved, tempo LE7×2, row sung + 1, note sung + 1 LE7×2, tick LE7×2, name (24) |
| 40 | size LE7×2, FNV-1a hash LE7×5, slot 0–7 | error: start sending a song to a slot (stopped only; the song being played or edited is not touched) |
| 41 | offset LE7×2, bytes as pairs | error: a piece of it |
| 42 | — | error: the end; checked, then written to the slot (3: not a 386DX song, 5: the voice is being prepared, ask again) |
| 43 | offset LE7×2 | error, size LE7×2, count, up to 32 bytes as pairs: the song in memory |
| 44 | text | error, syllables the speech engine finds (127: it can't) |
| 45 | text | the same, then each syllable's shortest sung length in ms, LE7×2 |
| 47 | — | stop |
| 49 | index | error, total, index, kind (0 demo, 1 song, 2 empty, 3 an older 386DX song), name (24) |
| 50 | slot, offset LE7×3 | error, length LE7×3, count, up to 32 bytes as pairs: a slot as stored |
| 51 | slot | error: the slot emptied (5: the voice is being prepared, ask again) |

Errors: 0 done, 1 busy (playing, typing, a question, or the voice being prepared), 2 not possible
here, 3 refused.

The song is `song_t` (`firmware/src/app/song.h`), 15,752 bytes; `web/song7.mjs` writes and reads
the same layout, and both check it.
