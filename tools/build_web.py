#!/usr/bin/env python3
"""Compile the firmware's speech, sung clips and audio engine for the companion (web/dsp/engine.mjs)."""
import os,subprocess,re,json
from pathlib import Path
from prepare_espeak import prepare
from music_sources import MUSIC_UNITS
root=Path(__file__).resolve().parents[1];os.chdir(root)
files=prepare(root)
files += [root/'firmware/src/app'/n for n in ('engine.c','punch.c','vocal.c','song.c','speech.c','speech_runtime.c','singing.c')]
files += [root/'firmware/src'/n for n in MUSIC_UNITS]
files += [root/'web/dsp/browser.c']
out=root/'build/web';out.mkdir(parents=True,exist_ok=True)
base=['emcc','-O2','-ffp-contract=off','-DDX_HOST=1','-Wno-everything','-Ivendor/espeak-ng/src/include','-Ivendor/espeak-ng/src/ucd-tools/src/include','-Ibuild/gen','-Ifirmware/src/app','-Ibuild/espeak/src']
objects=[]
for f in files:
 f=f.relative_to(root);obj=out/(str(f).replace('/','_')+'.o');flags=['-include','firmware/src/app/espeak_port.h'] if str(f).startswith(('vendor/','build/espeak/')) else []
 subprocess.run([*base,*flags,'-c',str(f),'-o',str(obj)],check=True);objects.append(str(obj))
subprocess.run([*base,*objects,'--no-entry','-sMODULARIZE=1','-sEXPORT_ES6=1','-sENVIRONMENT=worker,node','-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU16,HEAPF32','-sFILESYSTEM=0','-sSTACK_SIZE=1048576','-sINITIAL_MEMORY=16777216','-sALLOW_MEMORY_GROWTH=1','-o','web/dsp/engine.mjs'],check=True)
print('Browser singing engine built:',(root/'web/dsp/engine.wasm').stat().st_size,'bytes')


# the emulator: the whole firmware (host/emu.c over plat_host.c) as one standalone WebAssembly module for the
# page's AudioWorklet (web/emu/index.html and worklet.js)
emu_out = out / 'emu'
emu_out.mkdir(parents=True, exist_ok=True)
emu_units = list(prepare(root)) + [root/'firmware/src/app'/n for n in ('engine.c','punch.c','vocal.c','song.c','factory.c','speech.c','speech_runtime.c','singing.c')]
emu_units += [root/'firmware/src/dsp'/n for n in ('synth.c','drumkit.c','fxunit.c')]
emu_base = ['emcc','-O2','-ffp-contract=off','-DDX_HOST=1','-DX0X_HOST=1','-Wno-everything','-Ivendor/espeak-ng/src/include','-Ivendor/espeak-ng/src/ucd-tools/src/include','-Ibuild/gen','-Ifirmware/src/app','-Ibuild/espeak/src']
emu_objs = []
for f in emu_units:
    f = f.relative_to(root); obj = emu_out/(str(f).replace('/','_')+'.o')
    flags = ['-include','firmware/src/app/espeak_port.h'] if str(f).startswith(('vendor/','build/espeak/')) else []
    subprocess.run([*emu_base,*flags,'-c',str(f),'-o',str(obj)],check=True); emu_objs.append(str(obj))
subprocess.run([*emu_base,'host/emu.c',*emu_objs,'--no-entry','-sSTANDALONE_WASM','-sSTACK_SIZE=1048576','-sINITIAL_MEMORY=33554432','-sFILESYSTEM=0','-o','web/emu/app.wasm'],check=True)
page = (root/'web/emu/index.html').read_text()
if '{{NAME}}' in page:
    (root/'web/emu/index.html').write_text(page.replace('{{NAME}}','386DX').replace('{{ID}}','386dx'))
print('Emulator built:',(root/'web/emu/app.wasm').stat().st_size,'bytes')
