/* SPDX-License-Identifier: GPL-3.0-only */
// The FM-1's 386DX protocol over Web MIDI (firmware/src/app/companion.h): F0 7D 4F 4D cmd data.. F7,
// a reply to each request. Numbers are little-endian base 128; bytes travel as two 7-bit halves.
import {portNames, isUsual} from './ports.mjs?v=1.0.6';
export const HEADER = [0xf0, 0x7d, 0x4f, 0x4d];
export const PROTOCOL = 8;
export const le7 = (n, k = 2) => Array.from({length: k}, (_, i) => Math.floor(n / 2 ** (7 * i)) & 127);
export const rd7 = (a, at, k = 2) => Array.from({length: k}, (_, i) => (a[at + i] || 0) * 2 ** (7 * i)).reduce((x, y) => x + y, 0);
const fnv = bytes => { let h = 2166136261; for (const b of bytes) h = Math.imul(h ^ b, 16777619) >>> 0; return h; };
const text = (a, at, n) => String.fromCharCode(...a.slice(at, at + n)).replace(/\0.*$/s, '');

export class Device {
  constructor(input, output) {
    this.input = input; this.output = output; this.queue = Promise.resolve(); this.pending = null;
    this.listener = e => this.receive(e.data); input.addEventListener('midimessage', this.listener);
  }
  // the port named `name`, or null when it is gone
  static open(access, name) {
    const live = p => p.name === name && p.state !== 'disconnected';
    const outs = [...access.outputs.values()].filter(p => p.state !== 'disconnected');
    const input = [...access.inputs.values()].find(live), output = outs.find(live) || (outs.length === 1 ? outs[0] : null);
    return input && output ? new Device(input, output) : null;
  }
  // the first port with a usual or remembered name that answers as 386DX
  static async find(access) {
    for (const name of portNames(access).filter(isUsual)) {
      const d = Device.open(access, name);
      try { if (d && await d.status()) return d; } catch {}
      d?.close();
    }
    return null;
  }
  close() { this.input.removeEventListener('midimessage', this.listener); }
  receive(data) {
    const p = this.pending;
    if (!p || data.length < 6 || !HEADER.every((b, i) => data[i] === b) || data.at(-1) !== 0xf7 || data[4] !== p.command) return;
    clearTimeout(p.timer); this.pending = null; p.resolve(Array.from(data.slice(5, -1)));
  }
  // the ports gone (unplugged, restarted, flashed)
  get gone() { return this.input.state === 'disconnected' || this.output.state === 'disconnected'; }
  request(command, body = [], ms = 2000, quiet = false) {
    const lost = () => { const e = Object.assign(Error('The FM-1 stopped answering.'), {lost: true}); if (!quiet) this.onLost?.(); return e; };
    const run = () => new Promise((resolve, reject) => {
      if (this.gone) return reject(lost());
      const timer = setTimeout(() => { this.pending = null; reject(lost()); }, ms);
      this.pending = {command, resolve, timer};
      try { this.output.send([...HEADER, command, ...body, 0xf7]); } catch { clearTimeout(timer); this.pending = null; reject(lost()); }
    });
    const r = this.queue.then(run); this.queue = r.catch(() => {}); return r;
  }
  async status() {
    const d = await this.request(32);
    if (text(d, 0, 5) !== '386DX') throw Object.assign(Error('This FM-1 does not run 386DX.'), {other: true});
    if (d[5] !== PROTOCOL) throw Object.assign(Error(`This FM-1 runs an older 386DX (protocol ${d[5]}).`), {older: true});
    return {playing: !!d[6], page: d[7], track: d[8], slot: d[9] === 127 ? -1 : d[9], unsaved: !!d[10], bpm: rd7(d, 11),
      row: d[13] - 1, note: rd7(d, 14) - 1, tick: rd7(d, 16), name: text(d, 18, 24),
      build: text(d, 42, 28)};   // '' from a 386DX older than the build field
  }
  async stop() { await this.request(47); }
  // a request that touches flash, asked again while the FM-1 is preparing its voice (reply 5). `again`: one
  // that can be asked twice (a read, a delete) is also asked again when its reply is late, up to three times.
  async patient(command, body, ms, again = true) {
    for (let tries = 0, late = 0; ; tries++) {
      let d;
      try { d = await this.request(command, body, ms, again && late < 3); }
      catch (e) { if (!again || !e.lost || this.gone || late++ >= 3) throw e; continue; }
      if (d[0] !== 5 || tries >= 40) return d;
      await new Promise(r => setTimeout(r, 500));
    }
  }
  // a song straight into a slot (0-7); the song the FM-1 is playing or editing is not touched
  async sendSong(slot, bytes, progress = () => {}) {
    const ok = d => { if (d[0] === 1) throw Error('Stop the FM-1 first (and leave typing or a question).'); if (d[0] === 5) throw Error('The FM-1 is busy preparing its voice. Try again in a moment.'); if (d[0]) throw Error('The FM-1 refused the song.'); };
    ok(await this.request(40, [...le7(bytes.length, 2), ...le7(fnv(bytes), 5), slot]));
    for (let at = 0; at < bytes.length; at += 30) {
      const piece = bytes.slice(at, at + 30), body = [...le7(at, 2)];
      for (const b of piece) body.push(b & 127, b >> 7);
      ok(await this.request(41, body)); progress(Math.min(1, (at + 30) / bytes.length));
    }
    ok(await this.patient(42, [], 6000, false));
  }
  async readSong() {
    const out = []; let size = 1;
    for (let at = 0; at < size; ) {
      const d = await this.request(43, le7(at, 2)); size = rd7(d, 1); const n = d[3];
      for (let i = 0; i < n; i++) out.push(d[4 + i * 2] | d[5 + i * 2] << 7);
      at += n; if (!n) break;
    }
    return new Uint8Array(out);
  }
  // the Songs list: the eight slots, then the demos; kind 0 demo, 1 song, 2 empty, 3 an older 386DX song
  async catalog() {
    const first = await this.patient(49, [0]), total = first[1], list = [];
    for (let i = 0; i < total; i++) {
      const d = i ? await this.request(49, [i]) : first;
      list.push({index: i, kind: d[3], name: text(d, 4, 24)});
    }
    return list;
  }
  // a slot's stored bytes as they are (older songs included)
  async readSlot(slot) {
    const out = []; let length = 1;
    for (let at = 0; at < length; ) {
      const d = await this.patient(50, [slot, ...le7(at, 3)]);
      if (d[0]) throw Error('That slot cannot be read.');
      length = rd7(d, 1, 3); const n = d[4];
      for (let i = 0; i < n; i++) out.push(d[5 + i * 2] | d[6 + i * 2] << 7);
      at += n; if (!n) break;
    }
    return new Uint8Array(out);
  }
  async deleteSlot(slot) {
    const d = await this.patient(51, [slot], 6000);
    if (d[0]) throw Error('The FM-1 could not empty that slot (its 386DX may be too old: update it).');
  }
  // a soft reset once answered: the FM-1 starts again as at power-on, with the song saved last
  async restart() { await this.request(52); }
  async syllables(t) { const d = await this.request(44, Array.from(t, c => c.charCodeAt(0))); return d[0] ? -1 : d[1] === 127 ? -1 : d[1]; }
}
