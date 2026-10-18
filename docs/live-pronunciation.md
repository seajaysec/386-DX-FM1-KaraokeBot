# Live pronunciation engine

Pronunciation preparation is separate from playback.
CPU0 owns audio rendering, controls, USB, scheduling and cache publication.
CPU1 owns one copied pronunciation request at a time. Text, notes and voice
settings are captured before dispatch; changes made during that request affect
subsequent preparation. The current vocal segment finishes with its existing
settings. Brightness, grit and level are applied in playback; octave and
vibrato require prepared audio and show **Queued** until replacement is ready.

Preparation writes only an unpublished clip span (`firmware/src/app/vocal.c`). Playing
clips and clips queued for the audio interrupt are protected. The clips share a 6.5-second
PCM arena at 11025 Hz; a clip stays until its room is needed, so a song played again finds
its syllables ready.

The worker has a 20 KB stack in otherwise unused executable SRAM, separate from
the sample pool and flash-driver code. The worker polls from SRAM, allowing ordinary SAVE while it is idle. Every
reset/update transition holds CPU1 before bootloader memory can be reused.
A fault, corrupt request or ten-second timeout retires the worker and abandons
pronunciation for that session. Published audio remains available; no partial
result is published and no corrupted synthesis job is replayed. Retirement
survives soft resets until power-off for the same stack capacity. A firmware
upgrade that increases the stack may retry with the larger capacity. Failed
launch uses the serial path.

During playback, preparation follows the playhead: the next sung groups, row by row and round to
the start, with one clip kept for the Voice keys' next syllable. Stopped, it prepares the row under
the Song cursor and the next three syllables the keys will sing. Queued commands protect a clip
until the audio interrupt takes its playing lock.

Host verification includes pronunciation on a real separate thread while the
renderer advances, unchanged published PCM and rejection of every synthesis
entry point after dispatch failure. This is separate from target evidence:
query companion command 48 for worker online/busy/retirement, dispatched and
completed jobs, rejected requests, faults and measured stack use. Device
acceptance must also record audio while changing voice settings during playback.

## Verification

The measurements that were here were made on the firmware before the rebuild (candidate 30) and do
not describe this one. The host simulator and the library import (`tests/run_tests.sh`) show no
missed syllables; the device measurements are to be made again: a USB recording of a whole song
and of the Voice keys, with the Status page's audio load and missed syllables.
