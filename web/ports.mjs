/* SPDX-License-Identifier: GPL-3.0-only */
// Which MIDI port is the FM-1. The usual names are tried by themselves; when none answers, the page
// asks: a list of every MIDI port connected, the one picked is remembered in this browser. On a Mac a
// port can carry another device's name (Audio MIDI Setup's cache), which is why the list exists.
const KEY = '386dx.ports';
const USUAL = /fm-1|386dx|felucca|ota|composite|sinco|usb-midi/i;

export function rememberedNames() {
  try { const v = JSON.parse(localStorage.getItem(KEY) || '[]'); return Array.isArray(v) ? v.filter(x => typeof x === 'string') : []; }
  catch { return []; }
}
export function remember(name) {
  const list = [...new Set([name, ...rememberedNames()])].slice(0, 8);
  try { localStorage.setItem(KEY, JSON.stringify(list)); } catch { /* a private window: this visit only */ }
}
export const isUsual = name => USUAL.test(name || '') || rememberedNames().includes(name);

// every port with an input and an output of the same name
export function portNames(access) {
  const live = m => [...m.values()].filter(p => p.state !== 'disconnected').map(p => p.name || '');
  const outs = new Set(live(access.outputs));
  return [...new Set(live(access.inputs))].filter(n => outs.has(n));
}

// the list, in the page's <dialog id="ports-dialog">; resolves to the name picked, or null
export function pickPort(access, why) {
  const dialog = document.getElementById('ports-dialog'), list = document.getElementById('ports-list');
  document.getElementById('ports-why').textContent = why;
  const fill = () => {
    const names = portNames(access);
    list.replaceChildren(...names.map((n, i) => {
      const id = `port-${i}`, input = Object.assign(document.createElement('input'), {type: 'radio', name: 'port', value: n, id, checked: i === 0});
      const label = Object.assign(document.createElement('label'), {htmlFor: id, textContent: n});
      const li = document.createElement('li'); li.append(input, ' ', label); return li;
    }));
    document.getElementById('ports-none').hidden = names.length > 0;
    document.getElementById('ports-use').disabled = !names.length;
  };
  fill();
  const before = access.onstatechange;
  access.onstatechange = fill;                 // plugged in while the list is open: it appears
  dialog.showModal();
  return new Promise(resolve => {
    dialog.addEventListener('close', () => {
      access.onstatechange = before;
      const picked = dialog.returnValue === 'use' ? list.querySelector('input:checked')?.value : null;
      resolve(picked ?? null);
    }, {once: true});
  });
}
