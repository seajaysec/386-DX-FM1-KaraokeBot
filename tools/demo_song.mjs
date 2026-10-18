/* SPDX-License-Identifier: GPL-3.0-only */
// The song an FM-1 finds in S1 the first time 386DX starts: Greensleeves, from the lyric-underlaid
// arrangement in firmware/assets/demo_songs.json (traditional melody; underlay after Mjchael and
// JuLiBu, Wikibooks, CC BY-SA; extra verses underlaid here). Each line's syllables are checked
// against the FM-1's own speech engine; prints the 386DX song bytes as base64 for tools/gen_songs.py.
//   node tools/demo_song.mjs
import fs from 'node:fs';
import {songFromModel, songBytes7} from '../web/song7.mjs';
import create from '../web/dsp/engine.mjs';

const src = JSON.parse(fs.readFileSync(new URL('../firmware/assets/demo_songs.json', import.meta.url)))
  .find(s => s.name === 'Greensleeves');
const words = t => t.replace(/\bi\b/g, 'I').replace(/greensleeves/g, 'Greensleeves').replace(/^./, c => c.toUpperCase());
const lines = src.phrases.map(words);

// the vocal score: [at (sixteenths), length, pitch, line, syllable | 128 when it carries the vowel on]
const notes = src.vocals.map(([at, length, note, line, s]) => ({at, length, note, line, syllable: s & 127, tie: s >> 7 ? 1 : 0}));

// the speech engine counts the syllables the robot will sing; every line must have a note for each
const m = await create();
if (m._browser_init()) throw Error('engine');
const count = text => {
  const at = m._browser_text(); m.HEAPU8.fill(0, at, at + 64); m.HEAPU8.set(new TextEncoder().encode(text.slice(0, 63)), at);
  return m._browser_scan();
};
lines.forEach((text, l) => {
  const sung = notes.filter(n => n.line === l);
  if (!sung.length) return;
  const want = count(text), have = Math.max(...sung.map(n => n.syllable)) + 1;
  if (want !== have) throw Error(`line ${l + 1} "${text}": the engine hears ${want} syllables, the score has ${have}`);
});

// the backing: each pattern's chords on Keys, their lowest note on Bass, a light waltz on Drums
const step = () => ({notes: [0, 0, 0, 0], count: 0, velocity: 100, gate: 100, drums: 0, enabled: 0});
const patterns = src.patterns.map(chords => {
  const t = [0, 1, 2].map(() => Array.from({length: 16}, step));
  for (const [s, ns, len] of chords) {
    t[0][s] = {notes: [...ns, 0, 0, 0].slice(0, 4), count: Math.min(4, ns.length), velocity: 80, gate: 100 + len, drums: 0, enabled: 1};
    t[1][s] = {notes: [Math.min(...ns) - 12, 0, 0, 0], count: 1, velocity: 90, gate: 100 + len, drums: 0, enabled: 1};
  }
  t[2][0] = {...step(), drums: 1, velocity: 90, enabled: 1};               // kick on one
  for (const s of [4, 8]) t[2][s] = {...step(), drums: 1 << 10, velocity: 60, enabled: 1};   // closed hat on two, three
  return t;
});
const bars = src.order.length;
const song = songFromModel({name: 'Greensleeves', bpm: src.bpm, steps: src.steps, bars, notes, lines,
  backing: {order: src.order, patterns}});
process.stdout.write(JSON.stringify({name: song.name, bytes: Buffer.from(songBytes7(song)).toString('base64'),
  summary: `${bars} bars, ${song.lines.length} lines, ${song.rows.length} rows, ${notes.length} notes`}));
