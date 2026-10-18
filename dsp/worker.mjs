/* SPDX-License-Identifier: GPL-3.0-only */
// The firmware's speech and audio engine in a worker: pronunciation questions for the importer, and a
// song rendered as the FM-1 would play it.
const query = new URL(import.meta.url).search;
const ready = import('./engine.mjs' + query).then(({default: create}) => create({locateFile: p => new URL(p + query, import.meta.url).href}))
  .then(m => { if (m._browser_init()) throw Error('The singing engine could not start.'); return m; });
let queue = Promise.resolve(), stopped = 0;
self.onmessage = ({data}) => {
  if (data.kind === 'stop') { stopped++; return; }
  queue = queue.then(async () => {
    const {id, kind, text, bytes, from, seconds} = data;
    try {
      const m = await ready;
      if (kind === 'scan' || kind === 'minimum' || kind === 'offsets') {
        const at = m._browser_text(); m.HEAPU8.fill(0, at, at + 64); m.HEAPU8.set(new TextEncoder().encode(text.slice(0, 63)), at);
        const n = kind === 'minimum' ? m._browser_articulation() : m._browser_scan();
        if (n < 1 || n > 64) throw Error(`Could not pronounce “${text}”.`);
        const result = kind === 'scan' ? n : kind === 'offsets' ? Array.from({length: n}, (_, i) => m._browser_offset(i))
          : Array.from(m.HEAPU16.slice(m._browser_minimum() / 2, m._browser_minimum() / 2 + n));
        self.postMessage({id, result}); return;
      }
      if (kind === 'play') {
        m.HEAPU8.set(bytes, m._browser_song());
        if (m._browser_start(from || 0)) throw Error('Not a 386DX song.');
        const mine = stopped, frames = Math.ceil(seconds * 44100);
        for (let done = 0; done < frames && mine === stopped; ) {
          const chunk = new Float32Array(8192 * 2);
          for (let i = 0; i < 8192; i += 256) {
            const p = m._browser_render(256) / 4; chunk.set(m.HEAPF32.subarray(p, p + 512), i * 2);
          }
          done += 8192;
          self.postMessage({id, chunk, tick: m._browser_tick(), row: m._browser_row(), misses: m._browser_misses()}, [chunk.buffer]);
          await new Promise(r => setTimeout(r, 0));
        }
        m._browser_stop(); self.postMessage({id, result: 'done'}); return;
      }
      throw Error('Unknown request.');
    } catch (e) { self.postMessage({id, error: e.message}); }
  });
};
