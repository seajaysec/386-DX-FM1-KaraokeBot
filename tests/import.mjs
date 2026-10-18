/* SPDX-License-Identifier: GPL-3.0-only */
// The companion's import, end to end, on every song of the library: the file read, its words and tune
// found, each syllable checked with the FM-1's own speech engine (the firmware compiled to WebAssembly),
// the song made, loaded into that engine, and played for 30 s: no missed syllables, and sound.
//   node tests/import.mjs
import fs from 'node:fs';
import {readMidi, alignPronunciation, fitArticulation} from '../web/karaoke.mjs';
import {planKaraoke} from '../web/onboarding.mjs';
import {songFromModel, songBytes7, decodeSong7} from '../web/song7.mjs';
import create from '../web/dsp/engine.mjs';
const m = await create();
if (m._browser_init()) throw Error('engine');
const ask = (text, kind) => {
  const at = m._browser_text(); m.HEAPU8.fill(0, at, at + 64); m.HEAPU8.set(new TextEncoder().encode(text.slice(0, 63)), at);
  const n = kind === 'minimum' ? m._browser_articulation() : m._browser_scan();
  if (n < 1 || n > 64) throw Error(`cannot pronounce ${text}`);
  return kind === 'scan' ? n : kind === 'offsets' ? Array.from({length: n}, (_, i) => m._browser_offset(i))
    : Array.from(m.HEAPU16.slice(m._browser_minimum() / 2, m._browser_minimum() / 2 + n));
};
const library = JSON.parse(fs.readFileSync('firmware/assets/preset_songs.json'));
let failures = 0;
for (const entry of library) {
  try {
    const midi = readMidi(fs.readFileSync('web/' + entry.source)), melody = entry.melody ?? midi.melody;
    const notes = midi.tracks[melody].notes, lo = Math.min(...notes.map(n => n.note)), hi = Math.max(...notes.map(n => n.note));
    const transpose = Math.max(36 - lo, Math.min(84 - hi, Math.round((60 - (lo + hi) / 2) / 12) * 12));
    const assignments = Object.fromEntries(midi.tracks.filter(t => t.notes.length).map(t => [t.id, t.id === melody ? 'omit'
      : t.notes.every(n => n.channel === 9) ? 'drums' : /bass|contra/i.test(t.name) || t.notes.reduce((s, n) => s + n.note, 0) / t.notes.length < 48 ? 'bass' : 'melody']));
    const plan = planKaraoke(midi, {melody, transpose, compress: true, ornaments: true, assignments, name: (entry.deviceName || entry.name).slice(0, 23)});
    if (plan.errors.length) throw Error(plan.errors.join(' '));
    const out = [];
    for (const part of plan.parts) {
      const aligned = await alignPronunciation(part, t => ask(t, 'scan'), t => ask(t, 'offsets'));
      const checked = await fitArticulation(aligned, t => ask(t, 'minimum'), true, false);
      const song = songFromModel(checked), bytes = songBytes7(song);
      decodeSong7(bytes);
      m.HEAPU8.set(bytes, m._browser_song());
      if (m._browser_start(0)) throw Error('the engine refused the song');
      let peak = 0;
      const seconds = Math.min(30, song.bars * song.steps * 15 / song.bpm);
      for (let i = 0; i < seconds * 44100; i += 256) {
        const p = m._browser_render(256) / 4;
        for (let j = 0; j < 512; j++) peak = Math.max(peak, Math.abs(m.HEAPF32[p + j]));
      }
      m._browser_stop();
      const misses = m._browser_misses();
      out.push(`${song.lines.length} lines, ${song.rows.length} rows, ${song.lines.reduce((n, l) => n + l.notes.length, 0)} notes, ${misses} misses, peak ${(20 * Math.log10(peak + 1e-9)).toFixed(1)} dB`);
      if (misses || peak < 0.05) failures++;
    }
    console.log(`ok   ${entry.name}: ${out.join(' | ')}`);
  } catch (e) { failures++; console.log(`FAIL ${entry.name}: ${e.message}`); }
}
console.log(failures ? `${failures} failures` : 'all songs imported, sung and played');
process.exit(failures ? 1 : 0);
