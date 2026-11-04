/* SPDX-License-Identifier: GPL-3.0-only */
// A song's key from its notes: how long each pitch class sounds (drums left out), matched against the major and
// minor key profiles (Krumhansl and Kessler's listening tests) in all twelve keys; the best is the key. A minor key
// whose sixth is clearly more often major than minor is Dorian, one whose seventh is clearly raised Harmonic minor;
// a major key whose seventh is clearly flat is Mixolydian. The scale numbers are the FM-1's (GLO's SCALE).
export const NOTE_NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
export const SCALE_NAMES = ['Chromatic', 'Major', 'Minor', 'Dorian', 'Mixolydian', 'Lydian', 'Phrygian', 'Harmonic minor'];
const MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
const MINOR = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];

function correlate(h, profile, root) {
  const n = 12, mh = h.reduce((a, b) => a + b) / n, mp = profile.reduce((a, b) => a + b) / n;
  let num = 0, dh = 0, dp = 0;
  for (let i = 0; i < n; i++) {
    const x = h[(i + root) % n] - mh, y = profile[i] - mp;
    num += x * y; dh += x * x; dp += y * y;
  }
  return dh && dp ? num / Math.sqrt(dh * dp) : 0;
}

// the tracks as readMidi gives them; {root, scale, name, sure} (sure: the best match's correlation, 0..1)
export function findKey(tracks) {
  const h = new Array(12).fill(0);
  for (const t of tracks)
    for (const n of t.notes)
      if (n.channel !== 9) h[n.note % 12] += Math.max(1, n.end - n.tick);
  if (!h.some(x => x)) return {root: 0, scale: 0, name: 'Chromatic', sure: 0};
  let best = {r: -1};
  for (let root = 0; root < 12; root++)
    for (const [minor, profile] of [[0, MAJOR], [1, MINOR]]) {
      const r = correlate(h, profile, root);
      if (r > best.r) best = {r, root, minor};
    }
  const at = i => h[(best.root + i) % 12];
  const more = (a, b) => at(a) > 2 * at(b);                 // (clearly: a chord borrowed now and then is not a mode)
  const scale = best.minor ? (more(11, 10) ? 7 : more(9, 8) ? 3 : 2) : more(10, 11) ? 4 : 1;
  return {root: best.root, scale, name: `${NOTE_NAMES[best.root]} ${SCALE_NAMES[scale]}`, sure: Math.max(0, best.r)};
}
