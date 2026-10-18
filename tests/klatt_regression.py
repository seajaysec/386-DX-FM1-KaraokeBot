#!/usr/bin/env python3
"""Compare complete prepared phrases to pinned, unoptimized Klatt (host ASan)."""
import json, math, os, struct, subprocess, sys
import argparse
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from prepare_espeak import UNITS
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
parser=argparse.ArgumentParser()
parser.add_argument('--candidate', type=Path, default=ROOT/'build/espeak/src')
args=parser.parse_args()
out=ROOT/'build/klatt-regression';out.mkdir(exist_ok=True)
texts=['hello world', 'electric', 'make some noise', 'robot',
       'the quick brown fox jumps over the lazy dog',
       'shy ships sail south while bright stars shine',
       'we sing a song of friendship and remember everyone',
       'one two three four five six seven eight nine ten',
       '1234567890', 'abcdefghijklmnopqrstuvwxyz',
       'ah eh ee oh oo ay oy ow', 'red leather yellow leather',
       "please don't stop! why? because music matters.",
       'a', 's', 'f', 'th', 'ch', 'z', 'v', 'b', 'p', 'd', 'g', 'm', 'n',
       'one two three four five six seven eight nine ten eleven twelve',
       'abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijk']
# Reproducible fixtures, independent of the user's local lyric collection.
inputs=''.join(f'{p}|{t}\n' for p in (-12,0,12) for t in texts)
probe=r'''#include "speech.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
void dx_speech_idle(void){}
int main(int argc,char **argv){char line[128];assert(dx_speech_init()==0);FILE *f=fopen(argv[1],"wb");assert(f);while(fgets(line,sizeof line,stdin)){char *t=strchr(line,'|');assert(t);*t++=0;t[strcspn(t,"\n")]=0;static int16_t pcm[441000];assert(!dx_speech_make_into(0,t,atoi(line),pcm,441000));assert(!dx_speech_truncated[0]&&!dx_speech_clipped[0]);fwrite(&dx_speech_length[0],4,1,f);fwrite(pcm,2,dx_speech_length[0],f);}fclose(f);}
'''
(out/'probe.c').write_text(probe)
# Full phrases rather than the instrument's legacy two-second cache cutoff.
h=(ROOT/'firmware/src/app/speech.h').read_text().replace('#define DX_SPEECH_SAMPLES 22050','#define DX_SPEECH_SAMPLES 220500')
(out/'speech.h').write_text(h)
s=(ROOT/'firmware/src/app/speech.c').read_text().replace('"../dsp/fastmath.h"','"'+str(ROOT/'firmware/src/dsp/fastmath.h')+'"')
(out/'speech.c').write_text(s)
base=['clang','-O1','-g','-ffp-contract=off','-fsanitize=address,undefined','-fno-sanitize=shift,signed-integer-overflow','-DDX_HOST=1','-I'+str(out),'-Ivendor/espeak-ng/src/include','-Ibuild/espeak/src','-Ifirmware/src/app']
objs=[ROOT/'build/host'/('build_espeak_src_'+n+'.c.o') for n in UNITS if n!='klatt']
objs += [ROOT/'build/host'/n for n in ('build_espeak_data.c.o','build_espeak_unicode.c.o','vendor_espeak-ng_src_ucd-tools_src_ctype.c.o','firmware_src_app_speech_runtime.c.o','firmware_src_app_singing.c.o')]
for mode in ('reference','candidate'):
    src=ROOT/'vendor/espeak-ng/src/libespeak-ng' if mode=='reference' else args.candidate
    (out/'klatt.h').write_bytes((src/'klatt.h').read_bytes())
    (out/'klatt.c').write_bytes((src/'klatt.c').read_bytes())
    subprocess.run(base+['-include','firmware/src/app/espeak_port.h','-c',str(out/'klatt.c'),'-o',str(out/'klatt.o')],check=True)
    subprocess.run(base+[str(out/'speech.c'),str(out/'probe.c'),str(out/'klatt.o'),*map(str,objs),'-lm','-o',str(out/mode)],check=True)
    subprocess.run([str(out/mode),str(out/(mode+'.pcm'))],input=inputs,text=True,check=True,env=dict(os.environ,UBSAN_OPTIONS='halt_on_error=1'))
def read(path):
    b=path.read_bytes();at=0;result=[]
    while at<len(b):
        n=struct.unpack_from('=I',b,at)[0];at+=4
        result.append(struct.unpack_from('='+str(n)+'h',b,at));at+=n*2
    return result
refs=read(out/'reference.pcm');cands=read(out/'candidate.pcm');assert len(refs)==len(cands)==len(texts)*3
rows=[]
for text,a,b in zip(inputs.splitlines(),refs,cands):
    assert len(a)==len(b),(text,len(a),len(b))
    mse=sum((x-y)**2 for x,y in zip(a,b))/len(a)
    peak=max(abs(x-y) for x,y in zip(a,b))
    err_db=10*math.log10(max(mse,1e-30)/(32768**2))
    assert peak<=32 and err_db< -85,(text,peak,err_db)
    rows.append(dict(phrase=text,samples=len(a),max_error=peak,error_dbfs=err_db))
(out/'comparison.json').write_text(json.dumps(rows,indent=2))
print(f'Klatt reference: {len(rows)} full phrases; identical lengths; max error {max(r["max_error"] for r in rows)} counts; worst RMS error {max(r["error_dbfs"] for r in rows):.2f} dBFS')
