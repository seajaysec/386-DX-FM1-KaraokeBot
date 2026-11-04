/* SPDX-License-Identifier: GPL-3.0-only */
// The 386DX song as the device stores it from 1.1 on (firmware/src/app/song.h, song_t, version 8), the importer's
// model turned into it, and 1.0's songs (version 7, web/song7.mjs) brought up to it.
//
// Every track is a chain of phrases, a byte a bar: bit 7 starts a phrase; Keys', Bass' and Drums' low five bits
// name the bar's material (one of sixteen step rows, the patterns' row for that track), 31 a bar of rest. The
// Voice's phrases hold rows (a line starting at a place), each at its exact sixteenth: a row belongs to the
// phrase its first half-bar falls in.

import {songFromModel as model7, decodeSong7, SONG_SIZE as SONG_SIZE7} from './song7.mjs';
export {usage, LINES, ROWS, NOTES, PATTERNS, BARS, NPARAM, PARAM_PRESET} from './song7.mjs';
import {LINES, PATTERNS, BARS, NPARAM, PARAM_PRESET} from './song7.mjs';

export const SONG_MAGIC = 0x37535844, SONG_VERSION = 8, SONG_SIZE = 16128, SONG_VERSION_OLD = 7;
export const LANE_START = 0x80, LANE_REST = 31;
const O = {magic: 0, version: 4, bpm: 6, name: 8, steps: 32, bars: 33, rows: 34, notes: 36, track: 40,
  voice: 104, fx: 112, scale: 120, root: 121, laneBars: 122, line: 126, note: 4478, row: 7550, lane: 7934, pattern: 8446};
const TRACK = 16, LINE = 68, NOTE = 6, ROW = 4, STEP = 10, PATTERN = 480;
const VOICE = 0;

const ascii = s => s.normalize('NFKD').replace(/[̀-ͯ]/g, '').replace(/[‘’]/g, "'").replace(/[“”]/g, '"')
  .replace(/[–—]/g, '-').replace(/[^\x20-\x7e]/g, ' ');

const rowBar = (at, steps) => Math.floor((at + steps / 2) / steps);

// 1.0's one chain (a pattern a bar, for every backing track) as each track's own: a bar whose pattern has
// nothing for that track rests; phrases of four bars. The Voice's phrases start at bar 0 and then where a row
// starts, once the phrase is two bars long, or at four bars whatever comes
export function lanesFromChain(chain, patterns, rows, steps, bars) {
  const lanes = [[], [], [], []];
  const empty = (p, t) => patterns[p][t].every(s => (t === 2 ? !s.drums : !s.notes.length));
  for (let t = 1; t < 4; t++)
    for (let b = 0; b < bars; b++) {
      const p = chain[b] % PATTERNS;
      lanes[t].push((b % 4 ? 0 : LANE_START) | (empty(p, t - 1) ? LANE_REST : p));
    }
  const anchors = new Set(rows.map(r => rowBar(r.at, steps)));
  let cur = 0;
  for (let b = 0; b < bars; b++) {
    const start = !b || (anchors.has(b) && b - cur >= 2) || b - cur >= 4;
    if (start) cur = b;
    lanes[VOICE].push(start ? LANE_START : 0);
  }
  return {lanes, laneBars: [bars, bars, bars, bars]};
}

// the importer's model -> the stored song (an object songBytes writes); throws when it does not fit
export function songFromModel(model) {
  const s = model7(model);
  const {lanes, laneBars} = lanesFromChain(s.chain, s.patterns, s.rows, s.steps, s.bars);
  delete s.chain;
  return {...s, scale: 0, root: 0, lanes, laneBars};
}

export function songBytes(song) {
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
    a.set((t.arp || [0, 0, 0]).slice(0, 3), at + 13);
  });
  a.set(song.voice, O.voice);
  a.set(song.fx, O.fx);
  a[O.scale] = song.scale || 0;
  a[O.root] = song.root || 0;
  a.set(song.laneBars, O.laneBars);
  let first = 0;
  for (let l = 0; l < LINES; l++) {
    const line = song.lines[l] || {text: '', notes: []}, at = O.line + l * LINE;
    a.set([...line.text].map(c => c.charCodeAt(0)), at);
    v.setUint16(at + 64, first, true);
    a[at + 66] = line.notes.length;
    for (const n of line.notes) {
      const p = O.note + first * NOTE;
      v.setUint16(p, n.at, true);
      a.set([n.length, n.pitch, n.syllable, (n.tie ? 1 : 0) | ((n.nudge || 0) & 7) << 1], p + 2);
      first++;
    }
  }
  v.setUint16(O.notes, first, true);
  song.rows.forEach((r, i) => { v.setUint16(O.row + i * ROW, r.at, true); a[O.row + i * ROW + 2] = r.line; });
  song.lanes.forEach((lane, t) => a.set(lane.slice(0, BARS), O.lane + t * BARS));
  song.patterns.forEach((pat, p) => pat.forEach((track, t) => track.forEach((s, i) => {
    const at = O.pattern + p * PATTERN + (t * 16 + i) * STEP;
    v.setUint16(at, s.drums || 0, true);
    a.set(s.notes.slice(0, 4), at + 2);
    a[at + 6] = t === 2 ? (s.drums ? 1 : 0) : s.notes.length;
    a[at + 7] = s.velocity;
    a[at + 8] = s.length;
    v.setInt8(at + 9, s.nudge || 0);
  })));
  return a;
}

// the song's version: 8 (this layout), 7 (1.0's), or 0 (not a 386DX song)
export function songVersion(bytes) {
  const a = new Uint8Array(bytes), v = new DataView(a.buffer, a.byteOffset, a.byteLength);
  if (a.length < 8 || v.getUint32(O.magic, true) !== SONG_MAGIC) return 0;
  const n = v.getUint16(O.version, true);
  return (n === SONG_VERSION && a.length === SONG_SIZE) || (n === SONG_VERSION_OLD && a.length === SONG_SIZE7) ? n : 0;
}

export function decodeSong(bytes) {
  const a = new Uint8Array(bytes), v = new DataView(a.buffer, a.byteOffset, a.byteLength);
  if (songVersion(a) !== SONG_VERSION) throw Error('Not a 386DX song.');
  const str = (at, n) => String.fromCharCode(...a.slice(at, at + n)).split('\0')[0];
  const lines = [];
  for (let l = 0; l < LINES; l++) {
    const at = O.line + l * LINE, first = v.getUint16(at + 64, true), count = a[at + 66];
    lines.push({text: str(at, 64), notes: Array.from({length: count}, (_, i) => {
      const p = O.note + (first + i) * NOTE, tie = a[p + 5];
      return {at: v.getUint16(p, true), length: a[p + 2], pitch: a[p + 3], syllable: a[p + 4], tie: tie & 1, nudge: (tie << 28) >> 29};
    })});
  }
  const pattern = (p, t, i) => { const at = O.pattern + p * PATTERN + (t * 16 + i) * STEP;
    return {drums: v.getUint16(at, true), notes: Array.from(a.slice(at + 2, at + 2 + Math.min(4, a[at + 6]))), velocity: a[at + 7],
      length: a[at + 8], nudge: v.getInt8(at + 9)}; };
  const laneBars = Array.from(a.slice(O.laneBars, O.laneBars + 4));
  return {
    name: str(O.name, 24), bpm: v.getUint16(O.bpm, true), steps: a[O.steps], bars: a[O.bars], scale: a[O.scale], root: a[O.root],
    tracks: Array.from({length: 4}, (_, i) => { const at = O.track + i * TRACK;
      return {sound: a[at], level: a[at + 1], mute: a[at + 2], reverb: a[at + 3], delay: a[at + 4], param: Array.from(a.slice(at + 5, at + 13)),
        arp: Array.from(a.slice(at + 13, at + 16))}; }),
    voice: Array.from(a.slice(O.voice, O.voice + 8)), fx: Array.from(a.slice(O.fx, O.fx + 8)), lines,
    rows: Array.from({length: a[O.rows]}, (_, i) => ({at: v.getUint16(O.row + i * ROW, true), line: a[O.row + i * ROW + 2]})),
    laneBars, lanes: laneBars.map((n, t) => Array.from(a.slice(O.lane + t * BARS, O.lane + t * BARS + n))),
    patterns: Array.from({length: PATTERNS}, (_, p) => [0, 1, 2].map(t => Array.from({length: 16}, (_, i) => pattern(p, t, i)))),
  };
}

// 1.0's song (version 7) -> this one: its one chain becomes each track's, its scale and root carry over,
// everything else as it was
export function upgradeSong7(bytes) {
  const s = decodeSong7(bytes), a = new Uint8Array(bytes);
  const {lanes, laneBars} = lanesFromChain(s.chain, s.patterns, s.rows, s.steps, s.bars);
  delete s.chain;
  return songBytes({...s, scale: a[120] < 8 ? a[120] : 0, root: a[121] % 12, lanes, laneBars,
    tracks: s.tracks.map(t => ({...t, arp: [0, 0, 0]}))});
}

// whatever came off the device or a file: this version's bytes (1.0's updated), or a throw
export function songCurrent(bytes) {
  const n = songVersion(bytes);
  if (n === SONG_VERSION) return new Uint8Array(bytes);
  if (n === SONG_VERSION_OLD) return upgradeSong7(bytes);
  throw Error('Not a 386DX song.');
}
