/* SPDX-License-Identifier: GPL-3.0-only */
// The importer's pronunciation checks and the song preview, through the engine worker (dsp/worker.mjs).
import {alignPronunciation, fitArticulation} from './karaoke.mjs';
export class Engine {
  constructor() { this.seq = 0; this.pending = new Map(); }
  worker() {
    if (!this.w) {
      this.w = new Worker(new URL('./dsp/worker.mjs', import.meta.url), {type: 'module'});
      this.w.onmessage = ({data: d}) => { const p = this.pending.get(d.id); if (!p) return;
        if (d.chunk) { p.chunk?.(d); return; } this.pending.delete(d.id); d.error ? p.reject(Error(d.error)) : p.resolve(d.result); };
    }
    return this.w;
  }
  ask(kind, body = {}, chunk) {
    return new Promise((resolve, reject) => { const id = ++this.seq; this.pending.set(id, {resolve, reject, chunk}); this.worker().postMessage({id, kind, ...body}); });
  }
  // words checked against how the FM-1 pronounces them; syllables given room to be sung
  async check(model, useGaps = true, autoTempo = false) {
    const aligned = await alignPronunciation(model, t => this.ask('scan', {text: t}), t => this.ask('offsets', {text: t}));
    return fitArticulation(aligned, t => this.ask('minimum', {text: t}), useGaps, autoTempo);
  }
  // hear a song: chunks of interleaved stereo go to onChunk while the worker renders
  play(bytes, seconds, onChunk) { return this.ask('play', {bytes, seconds, from: 0}, onChunk); }
  stop() { this.w?.postMessage({kind: 'stop'}); }
}
