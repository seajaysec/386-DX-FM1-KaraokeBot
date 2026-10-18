/* SPDX-License-Identifier: GPL-3.0-only */
// The companion page: bring a karaoke song in, keep songs on the FM-1, install the firmware.
import {readMidi} from './karaoke.mjs?v=1.0.1';
import {planKaraoke} from './onboarding.mjs?v=1.0.1';
import {songFromModel, songBytes7, decodeSong7} from './song7.mjs?v=1.0.1';
import {Device} from './protocol.mjs?v=1.0.1';
import {Engine} from './singing.mjs?v=1.0.1';
import {Updater, IS_OTA, PRESET_PORTS, setTrace} from './firmware-update.mjs?v=1.0.1';
import {pickPort, remember, rememberedNames, portNames} from './ports.mjs?v=1.0.1';
import {productOf, logicalImage} from './firmware-package.mjs?v=1.0.1';

const $ = id => document.getElementById(id);
const say = (id, text) => { $(id).textContent = text; };
const engine = new Engine();
let access = null, device = null, fw = null, busy = false;

async function midiAccess() {
  if (!navigator.requestMIDIAccess) throw Error('This browser has no Web MIDI. Use Chrome or Edge.');
  if (!access) {
    access = await navigator.requestMIDIAccess({sysex: true});
    access.onstatechange = watchPorts;      // (the port list puts its own in place while open, then this back)
  }
  return access;
}

// ---- the connection: one state, kept true. Ports that go (unplugged, restarted, flashed) are noticed
// at once; when an FM-1 with 386DX comes back the page connects again by itself ----
let wanted = false, watchTimer = null;
function disconnected(text) {
  device?.close(); device = null;
  $('songs').hidden = true;
  say('device', text);
}
function lost() {
  if (!device) return;
  disconnected('The FM-1 went away. Waiting for it to come back…');
}
function watchPorts() {
  clearTimeout(watchTimer);
  watchTimer = setTimeout(async () => {
    if (device?.gone) lost();
    if (device || !wanted || busy) return;
    const back = await findOurs(access);     // quietly: only an FM-1 that answers as 386DX
    if (back && !device && !busy) { back.close(); guard(connect, 'device')(); }
  }, 800);
}
// ---- the report: what the page did and saw, shown only after something went wrong ----
const trail = [];
const note = (text) => { trail.push(`${((performance.now() - t0) / 1000).toFixed(1).padStart(6)} s  ${text}`); if (trail.length > 300) trail.shift(); };
const t0 = performance.now();
setTrace(note);
function ports() {
  if (!access) return ['(no MIDI access yet)'];
  const line = (kind, p) => `${kind} “${p.name}” by “${p.manufacturer || ''}” id ${p.id} ${p.state}/${p.connection}${p.version ? ' v' + p.version : ''}`;
  return [...[...access.inputs.values()].map(p => line('in ', p)), ...[...access.outputs.values()].map(p => line('out', p))];
}
function showReport(where, e) {
  note(`ERROR in ${where}: ${e.message}${e.code ? ` (code ${e.code})` : ''}${e.stack ? '\n' + e.stack.split('\n').slice(0, 3).join('\n') : ''}`);
  const lines = [
    `386DX page report, ${new Date().toISOString()}`,
    `page: ${location.origin}${location.pathname}, firmware on this page ${fw?.version ?? '?'} (${fw?.product ?? '?'})`,
    `browser: ${navigator.userAgent}`,
    `platform: ${navigator.userAgentData?.platform ?? navigator.platform}`,
    `error: ${e.message}`,
    `MIDI ports the browser sees:`, ...ports().map(l => '  ' + l),
    `remembered port names: ${JSON.stringify(rememberedNames())}`,
    `what the page did:`, ...trail.map(l => '  ' + l),
  ];
  $('report-text').value = lines.join('\n');
  $('report').hidden = false;
}
$('report-copy').addEventListener('click', async () => {
  try { await navigator.clipboard.writeText($('report-text').value); say('report-status', 'Copied.'); }
  catch { $('report-text').select(); say('report-status', 'Select all and copy it by hand.'); }
});
function guard(fn, where) {
  return async (...a) => {
    if (busy) return;
    busy = true;
    note(`start ${fn.name}`);
    try { await fn(...a); note(`done ${fn.name}`); } catch (e) { say(where, e.message); showReport(fn.name, e); } finally { busy = false; }
  };
}

// ---- the FM-1 ----
// First what every FM-1 answers, the identity query, as the other installers do; only a port that says it
// runs 386DX (FM-1_386...) is then spoken to in 386DX's own messages. Nothing else is sent to other firmware.
const OURS = /^FM-1_386/;
function updater(access) {
  const up = new Updater(access);
  up.setPatterns([...PRESET_PORTS, ...rememberedNames()]);
  return up;
}
// an FM-1 right after an install or a restart takes a few seconds to answer in 386DX's messages
async function statusOf(dev) {
  for (let tries = 0; ; tries++) {
    try { return await dev.status(); } catch (e) {
      if (!e.lost || tries >= 4) throw e;
      await new Promise(r => setTimeout(r, 1500));
    }
  }
}
// d: {name, id}, a port that answered the identity query
async function adopt(access, d) {
  if (IS_OTA(d.id)) return offer('This FM-1 is waiting in update mode.', 'finish');
  if (!OURS.test(d.id.text)) return offer(`Connected: this FM-1 runs ${d.id.text}, not 386DX.`, 'install');
  device = Device.open(access, d.name);
  if (!device) throw Error('The FM-1 went away. Plug it in again.');
  let s;
  try { s = await statusOf(device); } catch (e) {
    device.close(); device = null;
    if (e.older) return offer(e.message, 'update');
    throw e;
  }
  remember(d.name);
  device.onLost = lost;
  const build = s.build || 'an older build';
  showChanges(s.build || '');
  say('device', `Connected: “${s.name}”${s.slot >= 0 ? ` (S${s.slot + 1})` : ''}, ${s.bpm} BPM${s.playing ? ', playing' : ''}. 386DX ${build}.`);
  offer('', !fw ? 'install' : s.build === fw.version ? 'current' : 'update');
  await listSlots();
  $('songs').hidden = false;
}
// quietly: an FM-1 that answers as 386DX, opened
async function findOurs(access) {
  for (const d of await updater(access).scan()) {
    if (!OURS.test(d.id.text)) continue;
    const dev = Device.open(access, d.name);
    if (!dev) continue;
    try { await dev.status(); return dev; } catch { dev.close(); }
  }
  return null;
}
async function connect() {
  wanted = true;
  disconnected('Connecting…');               // the slots show only for an FM-1 running 386DX
  const access = await midiAccess();
  note('MIDI ports: ' + portNames(access).join(' | '));
  const found = await updater(access).scan();
  note('answered the identity query: ' + (found.map(x => `${x.name} = ${x.id.text}`).join('; ') || 'nothing'));
  const d = found.find(x => IS_OTA(x.id)) || found.find(x => OURS.test(x.id.text)) || found.find(x => x.id.model === 'FM-1') || found[0];
  if (d) return adopt(access, d);
  // none under a usual name: the list of every MIDI port, and the one picked is asked the same question
  const name = await pickPort(access, 'No FM-1 answered under its usual name. Which of these is it?');
  if (!name) { say('device', 'Not connected.'); return offer('', 'install'); }
  const up = updater(access);
  up.allow(name);
  const picked = await up.probe(name);
  if (!picked) throw Error(`“${name}” did not answer as an FM-1. Check the cable and close other apps that use MIDI.`);
  remember(name);
  return adopt(access, picked);
}

// the one install button says what it would do for the FM-1 connected
function offer(text, what) {
  if (text) say('device', text);
  const b = $('install');
  b.textContent = what === 'finish' ? 'Finish the update' : what === 'update' ? `Update to ${fw?.version ?? 'this page’s 386DX'}` : what === 'current' ? 'Install again' : 'Install 386DX';
  if (fw) say('install-status', what === 'current' ? `Up to date: ${fw.version}.` : what === 'update' ? `This page has 386DX ${fw.version}.` : `Ready: 386DX ${fw.version}.`);
}

// ---- the slots: a change chosen for each, then Sync does them all ----
let library = [], slotList = [];
const pending = new Map();           // slot -> {kind: 'put', name, from: {library} | {data}} | {kind: 'download' | 'delete'}
async function loadLibrary() {
  try { library = await (await fetch('songs/karaoke-library.json')).json(); } catch { library = []; }
}

function changeText(c) {
  return c.kind === 'put' ? `will become ${c.name}` : c.kind === 'download' ? 'will be downloaded' : 'will be deleted';
}

async function listSlots() {
  slotList = (await device.catalog()).filter(s => s.index < 8);
  drawSlots();
}

function drawSlots() {
  const body = $('slots');
  body.replaceChildren();
  for (const s of slotList) {
    const c = pending.get(s.index), tr = document.createElement('tr'), td = () => document.createElement('td');
    if (c) tr.className = 'pending';
    const holds = td();
    holds.append(s.kind === 2 ? 'empty' : s.name);
    if (c) holds.append(' ', Object.assign(document.createElement('span'), {className: 'change', textContent: changeText(c)}));
    const pick = document.createElement('select');
    pick.setAttribute('aria-label', `Change for S${s.index + 1}`);
    pick.append(new Option('No change', ''));
    if (s.kind === 1) pick.append(new Option('Download it', 'download'), new Option('Delete it', 'delete'));
    const ours = document.createElement('optgroup');
    ours.label = 'Put a song here';
    ours.append(...library.map((e, i) => new Option(e.name, `l${i}`)), new Option('Import a file…', 'import'));
    pick.append(ours);
    if (c?.kind === 'put' && c.from.data) pick.append(new Option(c.name, 'file'));
    pick.value = !c ? '' : c.kind !== 'put' ? c.kind : c.from.data ? 'file' : `l${c.from.library}`;
    pick.addEventListener('change', () => choose(s.index, pick.value));
    const cell = td();
    cell.append(pick);
    tr.append(Object.assign(document.createElement('th'), {scope: 'row', textContent: `S${s.index + 1}`}), holds, cell);
    body.append(tr);
  }
  const n = pending.size;
  $('sync').disabled = !n || busy;
  $('sync').textContent = n ? `Sync ${n} change${n > 1 ? 's' : ''}` : 'Sync';
  $('sync-notice').hidden = !n;
}

function choose(slot, value) {
  if (value === 'import') return importFor(slot);
  if (!value) pending.delete(slot);
  else if (value === 'download' || value === 'delete') pending.set(slot, {kind: value});
  else if (value[0] === 'l') {
    const e = library[Number(value.slice(1))];
    pending.set(slot, {kind: 'put', name: e.deviceName || e.name, from: {library: Number(value.slice(1))}});
  }
  drawSlots();
}

// the import dialog: the format explained, a file chosen for this slot
function importFor(slot) {
  const dialog = $('import-dialog'), input = $('import-file');
  input.value = '';
  input.onchange = async () => {
    const f = input.files[0];
    if (!f) return;
    pending.set(slot, {kind: 'put', name: f.name.replace(/\.(kar|midi?)$/i, ''), from: {data: await f.arrayBuffer()}});
    dialog.close('file');
  };
  dialog.addEventListener('close', () => drawSlots(), {once: true});
  dialog.returnValue = '';
  dialog.showModal();
}

// a karaoke or MIDI file made into a 386DX song: the tune and words found, every syllable checked
async function makeSong(data, name, melody) {
  const m = readMidi(data);
  if (melody !== undefined) m.melody = melody;
  const notes = m.tracks[m.melody].notes, lo = Math.min(...notes.map(n => n.note)), hi = Math.max(...notes.map(n => n.note));
  const transpose = Math.max(36 - lo, Math.min(84 - hi, Math.round((60 - (lo + hi) / 2) / 12) * 12));
  const assignments = Object.fromEntries(m.tracks.filter(t => t.notes.length).map(t => [t.id, t.id === m.melody ? 'omit'
    : t.notes.every(n => n.channel === 9) ? 'drums' : /bass|contra/i.test(t.name) || t.notes.reduce((s, n) => s + n.note, 0) / t.notes.length < 48 ? 'bass' : 'melody']));
  const plan = planKaraoke(m, {lyric: m.lyric, melody: m.melody, transpose, compress: true, ornaments: true, assignments,
    name: (name || m.title || 'Song').slice(0, 23)});
  if (plan.errors.length) throw Error(`${name}: ${plan.errors.join(' ')}`);
  const checked = await engine.check(plan.parts[0], true, false);
  return songBytes7(songFromModel(checked));
}

function save(name, data) {
  const a = Object.assign(document.createElement('a'), {href: URL.createObjectURL(new Blob([data])), download: name});
  a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}

// every change, slot by slot; then the FM-1 restarts and the page connects again
async function sync() {
  if (!device) throw Error('Connect the FM-1 first.');
  const jobs = [...pending.entries()].sort((a, b) => a[0] - b[0]), bar = $('sync-progress');
  bar.hidden = false;
  let done = 0;
  const step = text => { say('songs-status', `${text} (${done + 1} of ${jobs.length}). Keep the FM-1 on.`); };
  try {
    for (const [slot, c] of jobs) {
      const s = slotList.find(x => x.index === slot);
      if (c.kind === 'download') {
        step(`Downloading S${slot + 1}`);
        const data = await device.readSlot(slot);
        decodeSong7(data);
        save(`${s?.name || 'song'}.386dx`, data);
      } else if (c.kind === 'delete') {
        step(`Emptying S${slot + 1}`);
        await device.deleteSlot(slot);
      } else {
        step(`${c.name}: checking every syllable`);
        const bytes = await makeSong(c.from.data ?? await (await fetch(library[c.from.library].source)).arrayBuffer(),
          c.name, c.from.data ? undefined : library[c.from.library].melody);
        step(`${c.name}: sending to S${slot + 1}`);
        await device.sendSong(bytes);
        await device.saveSlot(slot);
      }
      pending.delete(slot);
      bar.value = ++done / jobs.length;
    }
  } catch (e) {
    if (!e.lost) throw e;
    throw Error(`The FM-1 went away during the sync. ${pending.size} change${pending.size === 1 ? '' : 's'} still to do: press Sync when it is back.`);
  } finally {
    drawSlots();
  }
  say('songs-status', 'Synced. The FM-1 is restarting…');
  await device.restart();
  disconnected('The FM-1 is restarting…');
  await new Promise(r => setTimeout(r, 4000));
  bar.hidden = true;
  await connect();
  say('songs-status', 'Synced.');
}

// ---- the firmware: the package comes with the page ----
async function loadFirmware() {
  try {
    const r = await fetch('firmware/386dx.fwsc', {cache: 'no-cache'});
    if (!r.ok) throw Error();
    const data = new Uint8Array(await r.arrayBuffer());
    const about = await (await fetch('firmware/386dx.json', {cache: 'no-cache'})).json().catch(() => ({}));
    fw = {image: logicalImage(data), product: productOf(data), version: about.version || 'unknown'};
    $('install').disabled = false;
    say('install-status', `Ready: 386DX ${fw.version}.`);
  } catch {
    say('install-status', 'The firmware is not on this page yet. Build it with tools/build.py.');
  }
}

const INSTALL_ERRORS = {
  notfound: 'No FM-1 answered. Use a USB data cable and close other apps that use MIDI.',
  model: 'That device is not an FM-1.',
  lost: 'The USB connection was lost. Plug it back in and press Install again: it finishes where it stopped.',
  noloader: 'The FM-1 did not switch to update mode. Replug the cable and press Install again.',
  noreturn: 'The FM-1 did not come back by itself. Switch it off and on.',
  stopped: 'The FM-1 stopped answering. Replug it and press Install again.',
};

async function install() {
  disconnected('Installing…');
  const access = await midiAccess();
  const updater = new Updater(access);
  updater.setPatterns([...PRESET_PORTS, ...rememberedNames()]);
  say('install-status', 'Looking for the FM-1…');
  let dev = (await updater.scan())[0];
  if (!dev) {
    const name = await pickPort(access, 'No FM-1 answered under its usual name. Which of these is it?');
    if (!name) { say('install-status', `Ready: 386DX ${fw.version}.`); return; }
    updater.allow(name);
    dev = await updater.probe(name);
    if (!dev) throw Error(`“${name}” did not answer as an FM-1. Check the cable and close other apps that use MIDI.`);
    remember(name);
  }
  updater.select(dev.name);
  const bar = $('install-progress'); bar.hidden = false; bar.value = 0;
  const total = Math.max(1, Math.round(fw.image.length / 512));
  const step = (phase, n) => {
    if (phase === 'start') say('install-status', 'Starting. Keep the cable in until it says installed.');
    if (phase === 'loader') say('install-status', 'Switching to update mode…');
    if (phase === 'write') { bar.value = Math.min(1, n / total); say('install-status', `Writing: ${Math.round(bar.value * 100)} percent`); }
    if (phase === 'reboot') say('install-status', 'Restarting…');
  };
  try {
    if (IS_OTA(dev.id)) {
      say('install-status', 'Finishing an update that was cut off…');
      if (!await updater.resume(fw.image, step, fw.product)) throw Object.assign(Error(), {code: 'stopped'});
    } else await updater.install(fw.image, fw.product, step);
    bar.value = 1;
    say('install-status', `Installed 386DX ${fw.version}.`);
    setTimeout(() => guard(connect, 'device')(), 2500);   // read the build back from the FM-1
  } catch (e) {
    throw Error(e.code === 'mismatch' ? `Written, but the FM-1 reports ${e.detail}.` : INSTALL_ERRORS[e.code] || e.message);
  }
}

$('connect').addEventListener('click', guard(connect, 'device'));
$('install').addEventListener('click', guard(install, 'install-status'));
$('sync').addEventListener('click', guard(sync, 'songs-status'));
// ---- what changed: the versions, newest first; the ones after the FM-1's build open up and say so
let changes = [];
function showChanges(build) {
  const box = $('changes');
  box.replaceChildren();
  const have = build === null ? -1 : changes.findIndex(c => c.build === build);
  const fresh = build === null ? 0 : have < 0 ? changes.length : have;      // an unlisted build: all of them are new
  changes.forEach((c, i) => {
    const d = document.createElement('details');
    d.open = i === 0 || i < fresh;
    const sum = document.createElement('summary');
    sum.append(`${c.version}: ${c.label} `);
    const code = document.createElement('code');
    code.textContent = c.build;
    sum.append(code);
    const when = document.createElement('span');
    when.className = 'when';
    when.textContent = c.date;
    sum.append(when);
    if (i < fresh) {
      const tag = document.createElement('span');
      tag.className = 'tag';
      tag.textContent = 'new since your update';
      sum.append(tag);
    }
    d.append(sum);
    const ul = document.createElement('ul');
    for (const n of c.notes) ul.append(Object.assign(document.createElement('li'), {textContent: n}));
    d.append(ul);
    box.append(d);
  });
}
async function loadChanges() {
  try { changes = await (await fetch('changelog.json', {cache: 'no-cache'})).json(); } catch { changes = []; }
  showChanges(null);
}

loadFirmware();
loadLibrary();
loadChanges();
