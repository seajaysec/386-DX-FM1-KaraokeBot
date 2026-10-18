#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The host simulator (host/sim.c): the firmware's UI, song, sung clips and audio on a computer,
built with the native compiler and the address / undefined-behaviour sanitizers.

    tools/build_host.py        -> build/host/sim"""
import os, subprocess, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
root = Path(__file__).resolve().parents[1]
os.chdir(root)
sys.path.insert(0, str(root / 'tools'))
from prepare_espeak import prepare
gen = root / 'build/gen'
gen.mkdir(parents=True, exist_ok=True)
subprocess.run([sys.executable, 'tools/gen_font.py', str(gen / 'felucca_font.h')], check=True)
subprocess.run([sys.executable, 'tools/gen_songs.py'], check=True)
speech = prepare(root)
out = root / 'build/host'
out.mkdir(parents=True, exist_ok=True)
(out / 'sim').unlink(missing_ok=True)
cc = os.environ.get('CC', 'clang')
base = [cc, '-g', '-O1', '-ffp-contract=off', '-fsanitize=address,undefined', '-fno-sanitize=shift,signed-integer-overflow',
        '-DDX_HOST=1', '-Ivendor/espeak-ng/src/include', '-Ivendor/espeak-ng/src/ucd-tools/src/include', '-Ibuild/gen',
        '-Ifirmware/src/app', '-Ibuild/espeak/src', '-Wno-everything']
units = list(speech) + [Path('firmware/src/app') / u for u in ('speech.c', 'speech_runtime.c', 'singing.c', 'engine.c', 'punch.c',
                                                              'vocal.c', 'song.c', 'factory.c')]
units += [Path('firmware/src/dsp') / u for u in ('synth.c', 'drumkit.c', 'fxunit.c')]
def compile(f):
    f = Path(f).relative_to(root) if Path(f).is_absolute() else Path(f)
    obj = out / (str(f).replace('/', '_') + '.o')
    extra = ['-include', 'firmware/src/app/espeak_port.h'] if str(f).startswith(('vendor/', 'build/espeak/')) else []
    subprocess.run([*base, *extra, '-c', str(f), '-o', str(obj)], check=True)
    return obj
with ThreadPoolExecutor(8) as pool:
    objects = list(pool.map(compile, units))
subprocess.run([*base, 'host/sim.c', *map(str, objects), '-lm', '-o', str(out / 'sim')], check=True)
print('build/host/sim')
