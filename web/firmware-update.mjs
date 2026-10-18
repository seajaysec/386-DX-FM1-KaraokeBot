// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Copyright (C) 2026 Chris Farrell (modifications)
//
// From Felucca's web/fm1ota.js (https://github.com/hugelton/Felucca).
// Modified 2026-10-06 by Chris Farrell for fm-static: port discovery (scan, probe, ports, select,
// setPatterns) and watch(), so that an FM-1 whose MIDI port name changes during an update (the loader
// has another USB product id; macOS can show a cached name) is still found. Taken into 386DX's
// companion page 2026-10-17 with "386DX" among the usual port names.
//
// The FM-1 USB-MIDI update protocol over WebMIDI (after tools/fm1_install.py).
// Step 1: the running firmware (official
// or Felucca) reads parts of the package and stages the update loader. Step 2:
// the loader reads the whole image and writes it. Both steps are "the device
// asks, we answer": cmd 0x30 read requests on the logical image.

const HS_QUERY = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];
const MAXDATA = 512;
// port names probed without asking (substrings, any case); any other port only after the user picks it
// "Update": the update loader on a Mac that names it from an earlier firmware ("Melodee Update")
export const PRESET_PORTS = ["FM-1", "FM1", "386DX", "Felucca", "OTA", "Update", "Composite", "SINCO", "USB-MIDI"];
// the page can listen to what is sent and received (for a report when something goes wrong)
let tracer = null;
export function setTrace(fn) { tracer = fn; }
const hex = (b) => Array.from(b.slice(0, 48), (x) => x.toString(16).padStart(2, "0")).join(" ") + (b.length > 48 ? ` …(${b.length} bytes)` : "");
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// open a port, but not forever (a port another program holds can hang); false when it did not open
async function openPort(port) {
  try { return await Promise.race([port.open().then(() => true), sleep(5000).then(() => false)]); } catch (_) { return false; }
}
export const IS_OTA = (id) => /^ota-/i.test(id.model);

// errors carry a code the pages translate: notfound, model, stopped, noloader, lost, noreturn,
// mismatch (detail: the identity the device reports), badreq
export function fail(code, msg, detail = "") { const e = new Error(msg); e.code = code; e.detail = detail; return e; }

export function pack7(data) {
  const out = []; let acc = 0, nb = 0;
  for (const b of data) {
    acc |= b << nb; nb += 8;
    while (nb >= 7) { out.push(acc & 0x7F); acc >>>= 7; nb -= 7; }
  }
  if (nb) out.push(acc & 0x7F);
  return out;
}

export function unpack7(s) {
  const out = []; let acc = 0, nb = 0;
  for (const b of s) {
    acc |= (b & 0x7F) << nb; nb += 7;
    while (nb >= 8) { out.push(acc & 0xFF); acc >>>= 8; nb -= 8; }
  }
  return out;
}

function response(addr, data, fl = 0) {
  const len = data.length;
  const body = [0x00, 0x59, 0x30, (len + 8) & 0xFF, ((len + 8) >> 8) & 0xFF, 0, fl,
    addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF,
    len & 0xFF, (len >> 8) & 0xFF, 0, ...data];
  let s = 0;
  for (let i = 6; i < body.length; i++) s += body[i];
  body.push(~s & 0xFF);
  return [0xF0, ...pack7(body), 0xF7];
}

function parseRequest(pkt) {
  if (pkt[0] !== 0xF0 || pkt[pkt.length - 1] !== 0xF7) return null;
  const u = unpack7(pkt.slice(1, -1));
  if (u.length !== 15 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x30) return null;
  let s = 0;
  for (let i = 6; i < 14; i++) s += u[i];
  if ((~s & 0xFF) !== u[14]) return null;
  return { fl: u[6], addr: (u[7] | (u[8] << 8) | (u[9] << 16) | (u[10] << 24)) >>> 0, len: u[11] | (u[12] << 8) | (u[13] << 16) };
}

// a command frame of the vendor protocol (00 59 cmd len16 0 payload ~sum), and the payload of a reply to cmd
function frame(cmd, payload = []) {
  let sum = 0;
  for (const b of payload) sum = (sum + b) & 255;
  return [0xF0, ...pack7([0, 0x59, cmd, payload.length & 255, payload.length >> 8, 0, ...payload, (~sum) & 255]), 0xF7];
}
function reply(pkt, cmd) {
  const u = unpack7(pkt.slice(1, -1));
  if (u.length < 7 || u[0] !== 0 || u[1] !== 0x59 || u[2] !== cmd) return null;
  return u.slice(6, -1);
}
// M-VAVE's open firmware (identities as its own updater lists them) takes an update only once armed
const OPEN_ID = /^(FM1_OFFICIAL_OPEN|FM1_COMMUNITY_\d+|FM-1_8\d{6})$/;

function parseIdentity(pkt) {
  const d = unpack7(pkt.slice(1, -1));
  if (d.length !== 34 || d[0] !== 0 || d[1] !== 0x59 || d[2] !== 0x11) return null;
  const txt = String.fromCharCode(...d.slice(6, 33)).replace(/\0+$/, "");
  if (!/^[\x20-\x7e]{1,27}$/.test(txt)) return null;
  // the usual FM-1_123 (the stock firmware, Felucca and the firmwares made on it); any other printable name
  // (M-VAVE's open firmware: FM1_OFFICIAL_OPEN, FM1_COMMUNITY_3) is kept whole, its model the part before "_"
  const m = /^([^_]+)_(\d+)$/.exec(txt);
  return m ? { text: txt, model: m[1], version: parseInt(m[2], 10) } : { text: txt, model: txt.split("_")[0], version: 0 };
}

// one MIDI in/out pair with a SysEx queue
class Link {
  constructor(input, output) {
    this.input = input; this.output = output; this.q = []; this.waiter = null;
    this.part = null;           // a SysEx that came in pieces (some USB stacks split them)
    input.onmidimessage = (ev) => {
      if (tracer && !(ev.data.length === 1 && ev.data[0] >= 0xF8)) tracer(`rx ${input.name}: ${hex(ev.data)}`);
      let d = Array.from(ev.data);
      if (d[0] === 0xF0) this.part = null;
      else if (this.part && d.every((b) => b < 0x80 || b === 0xF7)) { d = this.part.concat(d); this.part = null; }
      else return;
      if (d[d.length - 1] !== 0xF7) { this.part = d.length < 4096 ? d : null; return; }
      this.q.push(d);
      if (this.waiter) { const w = this.waiter; this.waiter = null; w(); }
    };
  }
  get lost() { return this.input.state === "disconnected" || this.output.state === "disconnected"; }
  // false when the port is gone (unplugged): WebMIDI throws on a disconnected output
  send(bytes) {
    if (tracer) tracer(`tx ${this.output.name}: ${hex(bytes)}`);
    try { this.output.send(bytes); return true; } catch (e) { if (tracer) tracer(`tx failed: ${e.message}`); return false; }
  }
  drain() { this.q.length = 0; }
  async read(ms) {
    if (!this.q.length) {
      let tm;
      await Promise.race([new Promise((r) => { this.waiter = r; }), new Promise((r) => { tm = setTimeout(r, ms); })]);
      clearTimeout(tm);
      this.waiter = null;
    }
    return this.q.shift() || null;
  }
  close() { this.input.onmidimessage = null; }
}

async function handshake(link, tries = 3, ms = 1000) {
  link.drain();
  for (let t = 0; t < tries; t++) {
    if (!link.send(HS_QUERY)) return null;
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const p = await link.read(end - Date.now());
      if (!p) break;
      const id = parseIdentity(p);
      if (id) return id;
    }
  }
  return null;
}

export class Updater {
  constructor(access, log) {
    this.access = access;
    this.log = log || (() => {});
    this.allowed = new Set();   // ports the user chose although their names are none of the usual ones
    this.patterns = PRESET_PORTS.slice();   // the user's own names can be added (see setPatterns)
    this.target = null;
    this.baseline = null;       // the ports before the update started (see watch())
  }

  inputNames() {
    return [...new Set([...this.access.inputs.values()].filter((i) => i.state !== "disconnected").map((i) => i.name || ""))];
  }

  allow(name) { this.allowed.add(name); }

  setPatterns(list) { this.patterns = list.map((x) => String(x).trim()).filter(Boolean); }

  isKnown(name) {
    const n = (name || "").toLowerCase();
    return this.patterns.some((p) => n.includes(p.toLowerCase())) || this.allowed.has(name);
  }

  // the port the user picked: used for the first contact (several FM-1s may be connected);
  // once the update has started, the renamed ports are followed (see watch())
  select(name) { this.target = name; if (name) this.allow(name); }

  // every MIDI port that has both an input and an output of the same name
  // (or, with a single output in all, every input: Windows can name the two halves differently)
  ports() {
    const live = [...this.access.outputs.values()].filter((o) => o.state !== "disconnected");
    const outs = new Set(live.map((o) => o.name));
    return this.inputNames().filter((n) => outs.has(n) || live.length === 1);
  }
  output(name) {
    const live = [...this.access.outputs.values()].filter((o) => o.state !== "disconnected");
    return live.find((o) => o.name === name) || (live.length === 1 ? live[0] : null);
  }

  // ask one port who it is (the FM-1's identity query; harmless to other gear but sent only on request)
  async probe(name) {
    const input = [...this.access.inputs.values()].find((i) => i.name === name && i.state !== "disconnected");
    const output = this.output(name);
    if (!input || !output) return null;
    if (!(await openPort(input)) || !(await openPort(output))) { if (tracer) tracer(`could not open “${name}”`); return null; }
    let link = new Link(input, output);
    let id = await handshake(link, 4, 1500);
    link.close();
    if (!id) {
      // a port left in a bad state by an earlier program or page: close it, open it again, ask again
      if (tracer) tracer(`no answer from “${name}”: closing and opening the port again`);
      try { await input.close(); await output.close(); } catch (_) { /* gone */ }
      await sleep(300);
      if (!(await openPort(input)) || !(await openPort(output))) return null;
      link = new Link(input, output);
      id = await handshake(link, 2, 1500);
      link.close();
    }
    return id ? { name, id } : null;
  }

  // probe the ports with a usual FM-1 name; -> [{ name, id }]
  async scan() {
    const found = [];
    for (const name of this.ports()) {
      if (!this.isKnown(name)) continue;
      const d = await this.probe(name);
      if (d) found.push(d);
    }
    return found;
  }

  // remember the ports now: the port name can change when the device restarts. The loader has another
  // USB product id, and macOS CoreMIDI names a port from its own cache keyed by vendor/product id and USB
  // location, so a port can carry the name of whatever firmware last used those ids there. After this,
  // a port that is new also counts.
  watch() { this.baseline = new Set(this.inputNames()); }

  // every in/out pair with the same port name that answers the handshake
  async find(filter) {
    const outs = [...this.access.outputs.values()];
    for (const input of this.access.inputs.values()) {
      if (input.state === "disconnected") continue;
      const known = this.isKnown(input.name);
      const fresh = this.baseline && !this.baseline.has(input.name || "");
      if (!known && !fresh) continue;   // never probe other gear
      if (!this.baseline && this.target && input.name !== this.target) continue;
      const output = this.output(input.name);
      if (!output) continue;
      if (!(await openPort(input)) || !(await openPort(output))) continue;
      const link = new Link(input, output);
      const id = await handshake(link, 2);
      if (id && (!filter || filter(id))) return { link, id, name: input.name };
      link.close();
    }
    return null;
  }

  async waitFor(filter, ms) {
    const end = Date.now() + ms;
    while (Date.now() < end) {
      const dev = await this.find(filter);
      if (dev) return dev;
      await sleep(1000);
    }
    return null;
  }

  // serve read requests until the device asks for one of the finish addresses; stops early
  // when the port disappears (lost: true) instead of waiting out idleMs
  async serve(link, image, finishAddr, idleMs, onProgress) {
    let served = 0, last = Date.now();
    for (;;) {
      const pkt = await link.read(1000);
      if (!pkt) {
        if (link.lost) return { served, finished: false, lost: true };
        if (Date.now() - last > idleMs) return { served, finished: false };
        continue;
      }
      const r = parseRequest(pkt);
      if (!r) continue;
      last = Date.now();
      if (r.addr === 0xE0000000 || r.addr === 0xF0000000) {
        link.send(response(r.addr, [...new TextEncoder().encode("success"), 0]));
        if (r.addr === finishAddr) return { served, finished: true };
        continue;
      }
      if (r.len > MAXDATA || r.addr + r.len > image.length) throw fail("badreq", `bad request ${r.addr.toString(16)}+${r.len}`);
      await sleep(10);
      if (!link.send(response(r.addr, Array.from(image.subarray(r.addr, r.addr + r.len)), r.fl))) {
        return { served, finished: false, lost: true };
      }
      served++;
      if (onProgress) onProgress(served, r.addr);
    }
  }

  // full install: running firmware -> loader -> new firmware
  async install(image, product, onStep) {
    const dev = await this.find((id) => !IS_OTA(id));
    if (!dev) throw fail("notfound", "FM-1 not found (USB cable, and no other app using it?)");
    return this.run(dev, image, product, onStep);
  }

  // a port the user picked although it never answered the identity question: the update is asked for anyway
  // (the running firmware answers it, or nothing happens and nothing is written)
  async installBlind(name, image, product, onStep) {
    const input = [...this.access.inputs.values()].find((i) => i.name === name && i.state !== "disconnected");
    const output = this.output(name);
    if (!input || !output || !(await openPort(input)) || !(await openPort(output))) throw fail("notfound", `“${name}” could not be opened`);
    return this.run({ link: new Link(input, output), id: { text: "(no answer)", model: "?" }, name }, image, product, onStep);
  }

  async run(dev, image, product, onStep) {
    const step = onStep || (() => {});
    const [model] = product.split("_");
    // any FM-1: the stock firmware and Felucca's say FM-1, M-VAVE's open firmware FM1
    if (dev.id.model !== "?" && !/^fm-?1$/i.test(dev.id.model) && dev.id.model !== model) {
      dev.link.close();
      throw fail("model", `the device is ${dev.id.text}, the package is for ${product}`);
    }
    step("start", dev.id.text);
    this.watch();
    if (OPEN_ID.test(dev.id.text)) await this.arm(dev.link, image);
    dev.link.send(UPGRADE);
    await sleep(2000);
    const s1 = await this.serve(dev.link, image, 0xE0000000, 8000, (n) => step("verify", n));
    dev.link.close();
    if (!s1.finished) throw fail(s1.lost ? "lost" : "stopped", `the device ${s1.lost ? "was disconnected" : "stopped"} after ${s1.served} requests: nothing was written`);
    step("loader");
    await sleep(3000);
    const ota = await this.waitFor(IS_OTA, 30000);    // any update loader (some name themselves differently)
    if (!ota) throw fail("noloader", "the update loader did not appear. Replug the USB cable and press Install again: the device stays in update mode until it is finished.");
    const s2 = await this.write(ota, image, step);
    if (!s2.finished) throw fail(s2.lost ? "lost" : "stopped", `the loader ${s2.lost ? "was disconnected" : "stopped"} after ${s2.served} requests. Replug and press Install again to resume.`);
    step("reboot");
    await sleep(3000);
    const back = await this.waitFor((id) => !IS_OTA(id), 40000);
    if (!back) throw fail("noreturn", "the device did not come back: power-cycle it");
    back.link.close();
    if (back.id.text !== product) throw fail("mismatch", `installed, but the device reports ${back.id.text}`, back.id.text);
    step("done", back.id.text);
    return back.id.text;
  }

  // M-VAVE's open firmware: its capability (cmd 0x31), then arming (0x32) with the image's size and a tag
  async arm(link, image) {
    const ask = async (cmd, payload) => {
      link.drain();
      link.send(frame(cmd, payload));
      const end = Date.now() + 1500;
      while (Date.now() < end) {
        const p = await link.read(end - Date.now());
        if (!p) break;
        const r = reply(p, cmd);
        if (r) return r;
      }
      return null;
    };
    const cap = await ask(49);
    if (!cap || cap.length !== 47 || String.fromCharCode(...cap.slice(0, 4)) !== "FMC1") return;   // not armed: the older way
    const tag = Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256", image)));
    const n = image.length;
    const ack = await ask(50, [1, cap[5], n & 255, (n >>> 8) & 255, (n >>> 16) & 255, (n >>> 24) & 255, ...tag]);
    if (tracer) tracer(`armed: ${ack ? hex(ack) : "no answer"}`);
  }

  // step 2 with the loader found by find()/waitFor()
  async write(ota, image, step) {
    ota.link.send(UPGRADE);
    await sleep(2000);
    const s2 = await this.serve(ota.link, image, 0xF0000000, 180000, (n) => step("write", n));
    ota.link.close();
    return s2;
  }

  // resume: the device is already in update mode (loader) -> true when the write finished
  async resume(image, onStep, product = null) {
    const model = product && product.split("_")[0];
    const ota = await this.find(IS_OTA);              // any update loader
    if (!ota) return false;
    const step = onStep || (() => {});
    this.watch();   // the restarted device may come back under another port name
    const result = await this.write(ota, image, step);
    if (!result.finished) return false;
    if (product) {
      step("reboot");
      await sleep(3000);
      const back = await this.waitFor((id) => id.model === model, 40000);
      if (!back) throw fail("noreturn", "The FM-1 did not return after resuming the write.");
      back.link.close();
      if (back.id.text !== product) throw fail("mismatch", "The FM-1 reports a different firmware after resuming.", back.id.text);
      step("done", back.id.text);
    }
    return true;
  }
}
