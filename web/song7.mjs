/* SPDX-License-Identifier: GPL-3.0-only */
// The 386DX song as the device stores it (firmware/src/app/song.h, song_t), and the importer's
// model turned into it. A song is lines (words and their tune), rows (a line starting at a place in
// the song) and a backing of three tracks played from sixteen one-bar patterns.
//
// The importer's model has the sung notes in song time, each naming its line; here every place a line
// is sung becomes a row, and identical words sung to an identical tune become one line that several
// rows name (a chorus is written once).

export const SONG_MAGIC = 0x37535844, SONG_VERSION = 7, SONG_SIZE = 15752;
export const LINES = 64, TEXT = 64, NOTES = 512, ROWS = 96, PATTERNS = 16, BARS = 128;
// the layout, as the C compiler lays out song_t
const O = {magic: 0, version: 4, bpm: 6, name: 8, steps: 32, bars: 33, rows: 34, notes: 36, track: 40,
  voice: 104, fx: 112, line: 136, note: 4488, row: 7560, chain: 7944, pattern: 8072};
const TRACK = 16, LINE = 68, NOTE = 6, ROW = 4, STEP = 10, PATTERN = 480;
export const NPARAM = 8, PARAM_PRESET = 255;
// the synth's presets: Keys starts on the first electric piano, Bass on the first stacked bass
export const SOUND_KEYS = 24, SOUND_BASS = 8, KIT = 0;
const VOICE_DEFAULT = [50, 20, 20, 20, 35, 45, 1, 80];
const FX_DEFAULT = [55, 50, 45, 30, 0, 20, 0, 50];

const ascii = s => s.normalize('NFKD').replace(/[̀-ͯ]/g, '').replace(/[‘’]/g, "'").replace(/[“”]/g, '"')
  .replace(/[–—]/g, '-').replace(/[^\x20-\x7e]/g, ' ');

// how many lines (words with their tune) and rows a model's notes make
export function usage(notes, lines) {
  const runs = [];
  for (const n of [...notes].sort((a, b) => a.at - b.at)) {
    const last = runs.at(-1), prev = last?.notes.at(-1);
    if (!last || n.line !== last.line || (!n.tie && n.syllable <= prev.syllable && n.at >= prev.at + prev.length)) runs.push({line: n.line, at: n.at, notes: []});
    runs.at(-1).notes.push(n);
  }
  const keys = new Set(runs.map(r => lines[r.line] + '|' + r.notes.map(n => [n.at - r.at, n.length, n.note, n.syllable, n.tie ? 1 : 0]).join(';')));
  return {lines: keys.size, rows: runs.length};
}

// the importer's model -> the stored song; throws when it does not fit
export function songFromModel(model) {
  const steps = model.steps === 12 ? 12 : 16, bars = Math.min(BARS, model.bars | 0);
  if (!bars) throw Error('The song has no bars.');
  const notes = [...(model.notes || [])].sort((a, b) => a.at - b.at);
  // each run of one line's notes is a place it is sung: a new run starts when the line changes or its
  // syllables start again from the top
  const runs = [];
  for (const n of notes) {
    const last = runs.at(-1), prev = last?.notes.at(-1);
    const fresh = !last || n.line !== last.line || (!n.tie && n.syllable <= prev.syllable && n.at >= prev.at + prev.length);
    if (fresh) runs.push({line: n.line, at: n.at, notes: []});
    runs.at(-1).notes.push(n);
  }
  const lines = [], rows = [];
  for (const run of runs) {
    const text = ascii(model.lines[run.line] || '').trim().slice(0, TEXT - 1);
    const tune = run.notes.map(n => ({at: n.at - run.at, length: Math.max(1, Math.min(255, n.length)), pitch: n.note,
      syllable: n.syllable, tie: n.tie ? 1 : 0}));
    const key = text + '|' + JSON.stringify(tune);
    let id = lines.findIndex(l => l.key === key);
    if (id < 0) {
      if (lines.length >= LINES) throw Error(`More than ${LINES} different lines (words with their tune).`);
      id = lines.length;
      lines.push({key, text, notes: tune});
    }
    if (rows.length && rows.at(-1).at === run.at) throw Error('Two lines start at the same moment.');
    if (run.at >= bars * steps) continue;
    rows.push({at: run.at, line: id});
  }
  if (rows.length > ROWS) throw Error(`More than ${ROWS} sung rows.`);
  const total = lines.reduce((s, l) => s + l.notes.length, 0);
  if (total > NOTES) throw Error(`${total} sung notes: the most is ${NOTES}.`);
  return {
    name: ascii(model.name || 'Song').slice(0, 23), bpm: model.bpm, steps, bars,
    tracks: [
      {sound: 0, level: 85, mute: 0, reverb: 25, delay: 15},
      {sound: SOUND_KEYS, level: 70, mute: 0, reverb: 15, delay: 0},
      {sound: SOUND_BASS, level: 70, mute: 0, reverb: 15, delay: 0},
      {sound: KIT, level: 70, mute: 0, reverb: 8, delay: 0}],
    voice: [...VOICE_DEFAULT], fx: [...FX_DEFAULT],
    lines: lines.map(l => ({text: l.text, notes: l.notes})), rows,
    chain: Array.from({length: bars}, (_, b) => (model.backing?.order?.[b] || 0) % PATTERNS),
    patterns: Array.from({length: PATTERNS}, (_, p) => [0, 1, 2].map(t => Array.from({length: 16}, (_, s) => {
      const n = model.backing?.patterns?.[p]?.[t]?.[s];
      if (!n || !n.enabled) return {drums: 0, notes: [], velocity: 100, length: t === 2 ? 1 : 2};
      const length = n.gate > 100 ? Math.min(16, n.gate - 100) : 1;
      if (t === 2) return {drums: n.drums & 2047, notes: [], velocity: n.velocity || 100, length: 1};
      return {drums: 0, notes: n.notes.slice(0, t === 0 ? Math.min(4, n.count) : Math.min(1, n.count)), velocity: n.velocity || 100, length: Math.max(1, length)};
    }))),
  };
}

export function songBytes7(song) {
  const a = new Uint8Array(SONG_SIZE), v = new DataView(a.buffer);
  v.setUint32(O.magic, SONG_MAGIC, true);
  v.setUint16(O.version, SONG_VERSION, true);
  v.setUint16(O.bpm, song.bpm, true);
  a.set([...ascii(song.name).slice(0, 23)].map(c => c.charCodeAt(0)), O.name);
  a[O.steps] = song.steps;
  a[O.bars] = song.bars;
  a[O.rows] = song.rows.length;
  song.tracks.forEach((t, i) => {
    const at = O.track + i * TRACK;
    a.set([t.sound, t.level, t.mute ? 1 : 0, t.reverb, t.delay], at);
    a.fill(PARAM_PRESET, at + 5, at + 5 + NPARAM);
    (t.param || []).forEach((p, j) => { if (j < NPARAM) a[at + 5 + j] = p; });
  });
  a.set(song.voice, O.voice);
  a.set(song.fx, O.fx);
  let first = 0;
  for (let l = 0; l < LINES; l++) {
    const line = song.lines[l] || {text: '', notes: []}, at = O.line + l * LINE;
    a.set([...line.text].map(c => c.charCodeAt(0)), at);
    v.setUint16(at + 64, first, true);
    a[at + 66] = line.notes.length;
    for (const n of line.notes) {
      const p = O.note + first * NOTE;
      v.setUint16(p, n.at, true);
      a.set([n.length, n.pitch, n.syllable, n.tie ? 1 : 0], p + 2);
      first++;
    }
  }
  v.setUint16(O.notes, first, true);
  song.rows.forEach((r, i) => { v.setUint16(O.row + i * ROW, r.at, true); a[O.row + i * ROW + 2] = r.line; });
  song.chain.forEach((p, b) => { a[O.chain + b] = p; });
  song.patterns.forEach((pat, p) => pat.forEach((track, t) => track.forEach((s, i) => {
    const at = O.pattern + p * PATTERN + (t * 16 + i) * STEP;
    v.setUint16(at, s.drums || 0, true);
    a.set(s.notes.slice(0, 4), at + 2);
    a[at + 6] = t === 2 ? (s.drums ? 1 : 0) : s.notes.length;
    a[at + 7] = s.velocity;
    a[at + 8] = s.length;
  })));
  return a;
}

export function decodeSong7(bytes) {
  const a = new Uint8Array(bytes), v = new DataView(a.buffer, a.byteOffset, a.byteLength);
  if (a.length !== SONG_SIZE || v.getUint32(O.magic, true) !== SONG_MAGIC || v.getUint16(O.version, true) !== SONG_VERSION)
    throw Error('Not a 386DX song.');
  const str = (at, n) => String.fromCharCode(...a.slice(at, at + n)).split('\0')[0];
  const lines = [];
  for (let l = 0; l < LINES; l++) {
    const at = O.line + l * LINE, first = v.getUint16(at + 64, true), count = a[at + 66];
    lines.push({text: str(at, 64), notes: Array.from({length: count}, (_, i) => {
      const p = O.note + (first + i) * NOTE;
      return {at: v.getUint16(p, true), length: a[p + 2], pitch: a[p + 3], syllable: a[p + 4], tie: a[p + 5]};
    })});
  }
  const pattern = (p, t, i) => { const at = O.pattern + p * PATTERN + (t * 16 + i) * STEP;
    return {drums: v.getUint16(at, true), notes: Array.from(a.slice(at + 2, at + 2 + Math.min(4, a[at + 6]))), velocity: a[at + 7], length: a[at + 8]}; };
  return {
    name: str(O.name, 24), bpm: v.getUint16(O.bpm, true), steps: a[O.steps], bars: a[O.bars],
    tracks: Array.from({length: 4}, (_, i) => { const at = O.track + i * TRACK;
      return {sound: a[at], level: a[at + 1], mute: a[at + 2], reverb: a[at + 3], delay: a[at + 4], param: Array.from(a.slice(at + 5, at + 13))}; }),
    voice: Array.from(a.slice(O.voice, O.voice + 8)), fx: Array.from(a.slice(O.fx, O.fx + 8)), lines,
    rows: Array.from({length: a[O.rows]}, (_, i) => ({at: v.getUint16(O.row + i * ROW, true), line: a[O.row + i * ROW + 2]})),
    chain: Array.from(a.slice(O.chain, O.chain + a[O.bars])),
    patterns: Array.from({length: PATTERNS}, (_, p) => [0, 1, 2].map(t => Array.from({length: 16}, (_, i) => pattern(p, t, i)))),
  };
}
