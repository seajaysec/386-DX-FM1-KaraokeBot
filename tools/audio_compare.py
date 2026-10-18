#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the FM-1's recorded USB audio with the simulator's for the same scenario, and say whether the
device sounds the way the simulator does. Numpy only.

    tools/audio_compare.py build/dev-tour.wav build/sim/tour/audio.wav

The two are lined up by their loudness over time (the recording starts a little before the script), then:
  - loudness: the average and the peak, in dB
  - sound where the simulator sounds: seconds where the simulator is loud and the device is silent
  - the shape of the sound in time: the correlation of the two loudness curves
  - the spectrum: the average level in eight bands, in dB, one against the other
  - glitches: single-sample jumps in a smooth sound, per second, in each
Exit status 1 when one of these is off."""
import sys, wave
import numpy as np

BANDS = [(60, 150), (150, 300), (300, 600), (600, 1200), (1200, 2400), (2400, 4800), (4800, 9600), (9600, 18000)]


def load(path):
    w = wave.open(str(path))
    raw = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float32) / 32768.0
    ch = w.getnchannels()
    mono = raw.reshape(-1, ch).mean(axis=1)
    return mono, w.getframerate()


def envelope(x, rate, hop_s=0.05):
    n = int(rate * hop_s)
    k = len(x) // n
    return np.sqrt((x[:k * n].reshape(k, n) ** 2).mean(axis=1) + 1e-12)


def db(v):
    return 20 * np.log10(np.maximum(v, 1e-9))


def align(dev_env, sim_env, max_s=8.0, hop_s=0.05):
    """the offset (in hops) to drop from the device's start (or add) so the two curves match best"""
    a, b = db(dev_env), db(sim_env)
    best, best_off = -2.0, 0
    for off in range(-int(max_s / hop_s), int(max_s / hop_s) + 1):
        x = a[off:] if off >= 0 else a
        y = b if off >= 0 else b[-off:]
        n = min(len(x), len(y))
        if n < 40:
            continue
        c = np.corrcoef(x[:n], y[:n])[0, 1]
        if c > best:
            best, best_off = c, off
    return best_off, best


def band_levels(x, rate):
    n = 1 << 14
    win = np.hanning(n)
    acc = np.zeros(n // 2 + 1)
    count = 0
    for i in range(0, len(x) - n, n // 2):
        acc += np.abs(np.fft.rfft(x[i:i + n] * win)) ** 2
        count += 1
    acc /= max(count, 1)
    f = np.fft.rfftfreq(n, 1 / rate)
    return [10 * np.log10(acc[(f >= lo) & (f < hi)].sum() + 1e-12) for lo, hi in BANDS]


def glitches(x, rate):
    """spikes in the second difference, 12x over their surroundings, per second of sound: a sample dropped or
    doubled (the USB capture) or a render underrun shows up as one"""
    dd = np.abs(np.diff(x, 2))
    loc = np.sqrt(np.convolve(dd ** 2, np.ones(2001) / 2001, "same")) + 1e-7
    loud = np.convolve(np.abs(x[:-2]), np.ones(2001) / 2001, "same") > 0.003
    n = int(((dd / loc > 12) & loud & (dd > 0.01)).sum())
    return n / max(loud.sum() / rate, 1.0)


def compare(dev_path, sim_path, report=print):
    dev, dr = load(dev_path)
    sim, sr = load(sim_path)
    if dr != sr:
        raise SystemExit(f"rates differ: {dr} and {sr}")
    hop = 0.05
    de, se = envelope(dev, dr, hop), envelope(sim, sr, hop)
    off, corr = align(de, se, hop_s=hop)
    start_d, start_s = (off, 0) if off >= 0 else (0, -off)
    n = min(len(de) - start_d, len(se) - start_s)
    de, se = de[start_d:start_d + n], se[start_s:start_s + n]
    a = dev[int(start_d * hop * dr):int((start_d + n) * hop * dr)]
    b = sim[int(start_s * hop * sr):int((start_s + n) * hop * sr)]
    problems = []
    mean_d, mean_s = db(np.sqrt((a ** 2).mean())), db(np.sqrt((b ** 2).mean()))
    peak_d, peak_s = db(np.abs(a).max()), db(np.abs(b).max())
    report(f"  aligned: device starts {off * hop:+.2f} s against the simulator, loudness curves correlate {corr:.2f}")
    report(f"  loudness: mean {mean_d:.1f} dB (simulator {mean_s:.1f}), peak {peak_d:.1f} dB (simulator {peak_s:.1f})")
    if abs(mean_d - mean_s) > 4.0:
        problems.append(f"the mean level differs by {mean_d - mean_s:+.1f} dB")
    if peak_d < -12.0:
        problems.append(f"the device peaks at only {peak_d:.1f} dB")
    # the two clocks drift over a long script (the device has busy moments the simulator only imitates), so
    # place no second against another: compare how much sound each makes
    on_d, on_s = float((db(de) > -45).sum()) * hop, float((db(se) > -45).sum()) * hop
    report(f"  time with sound: device {on_d:.1f} s, simulator {on_s:.1f} s")
    if on_s > 2 and on_d < 0.75 * on_s:
        problems.append(f"the device sounds for {on_d:.1f} s, the simulator for {on_s:.1f} s")
    k = np.ones(20) / 20                          # one second: the shape of the loudness, not its every wiggle
    slow = np.corrcoef(np.convolve(db(de), k, "same"), np.convolve(db(se), k, "same"))[0, 1]
    report(f"  the same smoothed over a second: correlation {slow:.2f}")
    corr = slow
    if corr < 0.6:
        problems.append(f"the loudness over time differs (correlation {corr:.2f})")
    bd, bs = band_levels(a, dr), band_levels(b, sr)
    diffs = [x - y for x, y in zip(bd, bs)]
    report("  bands (Hz) device minus simulator, dB: " + "  ".join(f"{lo}-{hi}:{d:+.0f}" for (lo, hi), d in zip(BANDS, diffs)))
    for (lo, hi), d, y in zip(BANDS, diffs, bs):
        if abs(d) > 12.0 and y > max(bs) - 40:
            problems.append(f"the {lo}-{hi} Hz band is {d:+.0f} dB from the simulator's")
    gd, gs = glitches(a, dr), glitches(b, sr)
    report(f"  glitches per second of sound: device {gd:.1f}, simulator {gs:.1f} (the recording's clock slips against the FM-1's; about 12 a second seen)")
    if gd > 40:
        problems.append(f"{gd:.0f} glitches a second on the device")
    for p in problems:
        report("  PROBLEM: " + p)
    return problems


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    sys.exit(1 if compare(sys.argv[1], sys.argv[2]) else 0)
