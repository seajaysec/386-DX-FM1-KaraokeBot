#!/usr/bin/env python3
"""Prepare the pinned English/Klatt eSpeak NG experiment, entirely locally."""
import os, re, shutil, subprocess, hashlib
from pathlib import Path
REVISION = '4870adfa25b1a32b4361592f1be8a40337c58d6c'
UNITS = 'common mnemonics error dictionary encoding intonation langopts numbers phoneme phonemelist readclause setlengths soundicon ssml synthdata synthesize tr_languages translate translateword voices wavegen speech espeak_api klatt'.split()

def replace_function(s, signature, body):
    start=s.index(signature); a=s.index('{',start); depth=1; b=a+1
    while depth:
        if s[b]=='{':depth+=1
        elif s[b]=='}':depth-=1
        b+=1
    return s[:a]+'{\n'+body+'\n}'+s[b:]

def klatt_filters(source, header):
    """Use the target FPU for audio-rate arithmetic; retain double control math.

    Keep frame interpolation and coefficient design in double precision:
    accumulated rounding there changes the voice much more than the sample path.
    The regression suite compares this against the pinned upstream synthesizer.
    """
    names=('resonator', 'antiresonator', 'sampled_source', 'impulsive_source',
           'natural_source', 'gen_noise', 'parwave')
    for name in names:
        result='int' if name=='parwave' else 'double'
        start=source.rindex('static '+result+' '+name+'(')
        a=source.index('{',start); depth=1; end=a+1
        while depth:
            if source[end]=='{': depth+=1
            elif source[end]=='}': depth-=1
            end+=1
        body=re.sub(r'\bdouble\b','float',source[start:end])
        body=re.sub(r'(?<![\w.])(\d+\.\d+)(?![\w.])',r'\1f',body)
        source=source[:start]+body+source[end:]
        source=re.sub(r'static double '+name+r'\(([^;]+)\);',
                      lambda m:'static float '+name+'('+m.group(1).replace('double','float')+');',source)
    start=header.index('typedef struct {'); end=header.index('} resonator_t',start)
    header=header[:start]+re.sub(r'\bdouble\b','float',header[start:end])+header[end:]
    for name in ('onemd','decay','amp_bypas','amp_voice','par_amp_voice',
                 'amp_aspir','amp_frica','amp_breth','amp_gain0',
                 'pulse_shape_a','pulse_shape_b','sample_factor'):
        header=header.replace('double '+name+';', 'float '+name+';')
    # Cache exact coefficient results, not a quantized frequency lookup table.
    # These functions are often called again with the same integer f/bw. Keep
    # filter histories and caller-applied amplitude scaling out of the cache.
    cache='''
typedef struct {
    long f, bw, rate;
    float a, b, c;
    unsigned char valid, zero;
} coefficient_cache_t;
static coefficient_cache_t coefficient_cache[64];
'''
    at=source.index('static klatt_global_t kt_globals;')
    source=source[:at]+cache+source[at:]
    for name,zero in (('setabc',0),('setzeroabc',1)):
        start=source.rindex('static void '+name+'(')
        a=source.index('{',start); depth=1; end=a+1
        while depth:
            if source[end]=='{':depth+=1
            elif source[end]=='}':depth-=1
            end+=1
        prefix=f'''
    long original_f = f;
    coefficient_cache_t *cached = &coefficient_cache[
        ((uint32_t)f ^ ((uint32_t)bw * 31u) ^ {zero}u) & 63u];
    if (cached->valid && cached->f == f && cached->bw == bw &&
        cached->rate == kt_globals.samrate && cached->zero == {zero}) {{
        rp->a = cached->a; rp->b = cached->b; rp->c = cached->c;
        return;
    }}
'''
        suffix=f'''
    cached->f = original_f; cached->bw = bw; cached->rate = kt_globals.samrate;
    cached->a = rp->a; cached->b = rp->b; cached->c = rp->c;
    cached->zero = {zero}; cached->valid = 1;
'''
        source=source[:a+1]+prefix+source[a+1:end-1]+suffix+source[end-1:]
    return source,header

def prepare(root):
    import re
    upstream=root/'vendor/espeak-ng'
    if not (upstream/'src/libespeak-ng/klatt.c').exists():
        raise SystemExit('Run: git clone --branch 1.52.0 --depth 1 https://github.com/espeak-ng/espeak-ng.git vendor/espeak-ng')
    rev=subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip()
    if rev!=REVISION:raise SystemExit('eSpeak NG revision mismatch')
    native=root/'build/espeak-tools'; out=root/'build/espeak';out.mkdir(parents=True,exist_ok=True)
    fingerprint=hashlib.sha256(Path(__file__).read_bytes()+REVISION.encode()).hexdigest()
    stamp=out/'data.stamp'
    if not (out/'data.c').exists() or not stamp.exists() or stamp.read_text()!=fingerprint:
        (out/'unicode.c').unlink(missing_ok=True)
        subprocess.run(['cmake','-S',str(upstream),'-B',str(native),'-G','Ninja','-DUSE_ASYNC=OFF','-DUSE_MBROLA=OFF','-DUSE_LIBSONIC=OFF','-DUSE_LIBPCAUDIO=OFF','-DUSE_KLATT=ON','-DUSE_SPEECHPLAYER=OFF','-DBUILD_TESTING=OFF','-DCMAKE_BUILD_TYPE=Release'],check=True)
        subprocess.run(['cmake','--build',str(native),'--target','espeak-ng-bin','-j6'],check=True)
        staging=out/'english'; ph=staging/'phsource'; data=staging/'espeak-ng-data'; dct=staging/'dictsource'
        shutil.copytree(upstream/'phsource',ph,dirs_exist_ok=True)
        data.mkdir(parents=True,exist_ok=True); dct.mkdir(exist_ok=True)
        s=(ph/'phonemes').read_text();(ph/'phonemes').write_text(s[:s.index('phonemetable consonants')]+ '\nphonemetable en base1\ninclude ph_english\nphonemetable en-us en\ninclude ph_english_us\n')
        compiler=out/'compile.c'
        compiler.write_text('#include <espeak-ng/espeak_ng.h>\n#include <stdio.h>\nint main(int n,char **v){espeak_ng_ERROR_CONTEXT e=0;espeak_ng_InitializePath(v[1]);int r=espeak_ng_CompileIntonation(stdout,&e);if(!r)r=espeak_ng_CompilePhonemeDataPath(22050,v[2],0,stdout,&e);if(r)espeak_ng_PrintStatusCodeMessage(r,stderr,e);return !!r;}\n')
        subprocess.run(['cc','-I'+str(upstream/'src/include'),str(compiler),str(native/'src/libespeak-ng/libespeak-ng.a'),str(native/'src/ucd-tools/libucd.a'),'-lm','-o',str(out/'compile')],check=True)
        subprocess.run([str(out/'compile'),str(staging),str(ph)],cwd=ph,check=True)
        shutil.copytree(upstream/'espeak-ng-data/lang',data/'lang',dirs_exist_ok=True)
        for name in ('en_list','en_rules'):shutil.copyfile(upstream/'dictsource'/name,dct/name)
        env=os.environ.copy();env['ESPEAK_DATA_PATH']=str(staging)
        subprocess.run([str(native/'src/espeak-ng'),'--compile=en'],cwd=dct,env=env,check=True)
        voice=(upstream/'espeak-ng-data/lang/gmw/en-US').read_bytes()+b'\nklatt 1\n'
        assets={n:(data/n).read_bytes() for n in ('phondata','phonindex','phontab','intonations','en_dict')};assets['klatt-en']=voice
        c=['/* Generated from eSpeak NG 1.52.0; GPL-3.0-or-later. */','#include <stddef.h>','#include <string.h>']
        for n,b in assets.items():
            name=n.replace('-','_');c.append(f'static const unsigned char {name}[] __attribute__((aligned(4))) = {{')
            c.extend(','.join(str(x) for x in b[i:i+24])+',' for i in range(0,len(b),24));c.append('};')
            print(f'eSpeak asset {n}: {len(b)} bytes')
        c.append('const void *es_data(const char *path, int *size) { const char *name=strrchr(path,47);name=name?name+1:path;')
        for n in assets:
            name=n.replace('-','_');c.append(f'if(!strcmp(name,"{n}")){{*size=sizeof {name};return {name};}}')
        c.append('*size=0;return 0;}');(out/'data.c').write_text('\n'.join(c)+'\n');stamp.write_text(fingerprint)
    # Match upstream character properties exactly for the ASCII panel alphabet,
    # Latin-1 and common punctuation, without shipping all Unicode tables.
    uc=out/'unicode.c'
    if not uc.exists():
        gen=out/'unicode_gen.c'
        gen.write_text(r'''#include <ucd/ucd.h>
#include <stdio.h>
int main(void){puts("/* eSpeak UCD subset; GPL-3.0-or-later. */\n#include <ucd/ucd.h>\ntypedef struct {unsigned short upper,lower;unsigned char cat;ucd_property prop;} row;\nstatic const row rows[]={");for(unsigned i=0;i<304;i++){unsigned c=i<256?i:0x2010+i-256;ucd_category k=ucd_lookup_category(c);printf("{%u,%u,%u,0x%llxULL},\n",ucd_toupper(c),ucd_tolower(c),k,(unsigned long long)ucd_properties(c,k));}puts("};\nstatic const row *get(unsigned c){return c<256?rows+c:(c>=0x2010 && c<0x2040)?rows+256+c-0x2010:0;}\ncodepoint_t ucd_toupper(codepoint_t c){const row *r=get(c);return r?r->upper:c;}\ncodepoint_t ucd_tolower(codepoint_t c){const row *r=get(c);return r?r->lower:c;}\nucd_category ucd_lookup_category(codepoint_t c){const row *r=get(c);return r?(ucd_category)r->cat:UCD_CATEGORY_Cn;}\nucd_property ucd_properties(codepoint_t c,ucd_category k){(void)k;const row *r=get(c);return r?r->prop:0;}");}
''')
        subprocess.run(['cc','-I'+str(upstream/'src/ucd-tools/src/include'),str(gen),str(native/'src/ucd-tools/libucd.a'),'-o',str(out/'unicode_gen')],check=True)
        uc.write_bytes(subprocess.check_output([str(out/'unicode_gen')]))
    # Deduplicate character-property profiles without reducing Unicode coverage.
    # Every original row is checked before replacing the padded 16-byte layout.
    unicode_text=uc.read_text()
    if 'static const row rows[]' in unicode_text and 'profile_index' not in unicode_text:
        import re
        original=[(int(a),int(b),int(c),int(d,16)) for a,b,c,d in re.findall(r'\{(\d+),(\d+),(\d+),0x([0-9a-fA-F]+)ULL\}',unicode_text)]
        assert len(original)==304
        profiles=[];rows=[]
        for upper,lower,category,properties in original:
            pair=(category,properties)
            if pair not in profiles:profiles.append(pair)
            rows.append((upper,lower,profiles.index(pair)))
        assert len(profiles)<256
        for old,(upper,lower,index) in zip(original,rows):assert old==(upper,lower,*profiles[index])
        compact=['/* Lossless eSpeak UCD profile table; GPL-3.0-or-later. */','#include <ucd/ucd.h>',
            'typedef struct __attribute__((packed)) {unsigned short upper,lower;unsigned char profile_index;} row;',
            'typedef struct __attribute__((packed)) {unsigned char cat;ucd_property prop;} profile;',
            'static const profile profiles[]={']
        compact.extend('{%u,0x%xULL},'%pair for pair in profiles);compact.append('};\nstatic const row rows[]={')
        compact.extend('{%u,%u,%u},'%row for row in rows);compact.append('};')
        compact.extend(['static const row *get(unsigned c){return c<256?rows+c:(c>=0x2010 && c<0x2040)?rows+256+c-0x2010:0;}',
            'codepoint_t ucd_toupper(codepoint_t c){const row *r=get(c);return r?r->upper:c;}',
            'codepoint_t ucd_tolower(codepoint_t c){const row *r=get(c);return r?r->lower:c;}',
            'ucd_category ucd_lookup_category(codepoint_t c){const row *r=get(c);return r?(ucd_category)profiles[r->profile_index].cat:UCD_CATEGORY_Cn;}',
            'ucd_property ucd_properties(codepoint_t c,ucd_category k){(void)k;const row *r=get(c);return r?profiles[r->profile_index].prop:0;}'])
        uc.write_text('\n'.join(compact)+'\n')
        print('Unicode profiles:',len(profiles),'rows:',len(rows),'all properties identical')
    src=out/'src';shutil.copytree(upstream/'src/libespeak-ng',src,dirs_exist_ok=True)
    (src/'config.h').write_text('\n'.join('#define '+k+' '+v for k,v in {'USE_ASYNC':'0','USE_KLATT':'1','USE_LIBPCAUDIO':'0','USE_LIBSONIC':'0','USE_MBROLA':'0','USE_SPEECHPLAYER':'0','PACKAGE_VERSION':'"1.52.0"','PATH_ESPEAK_DATA':'"/espeak"'}.items())+'\n')
    (src/'endian.h').write_text('#define LITTLE_ENDIAN 1234\n#define BIG_ENDIAN 4321\n#define BYTE_ORDER LITTLE_ENDIAN\n')
    # Immutable language assets are read directly from flash, not heap copies.
    p=src/'synthdata.c';s=p.read_text();s=replace_function(s,'static espeak_ng_STATUS ReadPhFile(', 'int n; const void *p=es_data(fname,&n); (void)context; if(!p)return ENOENT; *ptr=(void *)p; if(size)*size=n;return ENS_OK;');s=replace_function(s,'void FreePhData(void)','/* Assets live in flash for the lifetime of the firmware. */');p.write_text(s)
    p=src/'dictionary.c';s=p.read_text();a=s.index('\tsprintf(fname, "%s%c%s_dict"');b=s.index('\n\tpw = (int *)(tr->data_dictlist);',a);s=s[:a]+'\tif(strcmp(name,"en"))return 1;\n\ttr->data_dictlist=(char *)es_data("en_dict",&size);\n\tif(!tr->data_dictlist)return 1;\n'+s[b:];p.write_text(s)
    p=src/'tr_languages.c';s=p.read_text();a=s.index("\tcase L('e', 'n'):");b=s.index("\tcase L('e', 'l'):",a);english=s[a:b];a=s.index('\tswitch (name2)',s.index('Translator *SelectTranslator'));b=s.index('\n\ttr->translator_name = name2;',a);s=s[:a]+'\tswitch(name2) {\n'+english+'\tdefault: DeleteTranslator(tr); return NULL;\n\t}\n'+s[b:];s=s.replace('free(tr->data_dictlist);','/* dictionary is immutable flash data */');p.write_text(s)
    p=src/'common.c';s=p.read_text();s=replace_function(s,'int GetFileLength(', 'int n; return es_data(filename,&n)?n:-ENOENT;');p.write_text(s)
    p=src/'speech.c';s=p.read_text();a=s.index('\t// It seems that the wctype');b=s.index('\tespeak_ng_STATUS result = LoadPhData',a);s=s[:a]+s[b:];s=s.replace('espeak_srand(time(NULL));','espeak_srand(1);');s=replace_function(s,'ESPEAK_NG_API void espeak_ng_InitializePath(', '(void)path;strcpy(path_home,"/espeak");');p.write_text(s)
    # 63 input characters; leave ample room for spoken numbers and long words.
    p=src/'translate.h';s=p.read_text().replace('N_CLAUSE_WORDS   300','N_CLAUSE_WORDS   80').replace('N_TR_SOURCE      800','N_TR_SOURCE      256');p.write_text(s)
    p=src/'synthesize.h';s=p.read_text().replace('N_PHONEME_LIST 1000','N_PHONEME_LIST 300');p.write_text(s)
    # Public selection is fixed to the one embedded voice; no directory scan.
    p=src/'voices.c';s=p.read_text();s=replace_function(s,'ESPEAK_NG_API espeak_ng_STATUS espeak_ng_SetVoiceByName(', '(void)name;return espeak_ng_SetVoiceByFile("/espeak/klatt-en");');p.write_text(s)
    p=src/'translate.c';s=p.read_text().replace('free(tr->data_dictlist);','(void)tr->data_dictlist; /* immutable flash dictionary */');p.write_text(s)
    p=src/'wavegen.c';s=p.read_text();needle='\t*pitch_range = base + (pitch2 * range)/2 - *pitch_base;';assert needle in s;s=s.replace(needle,needle+'\n\t*pitch_base = (int)(((int64_t)*pitch_base * es_pitch_q16) >> 16);\n\t*pitch_range = (int)(((int64_t)*pitch_range * es_pitch_q16) >> 16);');p.write_text(s)
    p=src/'voices.c';s=p.read_text().replace('#include <dirent.h>','');s=replace_function(s,'static void GetVoices(', '(void)path;');p.write_text(s)
    p=src/'espeak_api.c';s=p.read_text();s=replace_function(s,'ESPEAK_API void espeak_CompileDictionary(', '(void)path;(void)log;(void)flags;');p.write_text(s)
    p=src/'synthesize.c';s=p.read_text().replace('PHONEME_LIST phoneme_list[N_PHONEME_LIST+1];','PHONEME_LIST phoneme_list[N_PHONEME_LIST+1] ES_POOL;');p.write_text(s)
    p=src/'soundicon.c';s=p.read_text();s=replace_function(s,'static espeak_ng_STATUS LoadSoundFile(', '(void)fname;(void)index;(void)context;return ENOENT;');p.write_text(s)
    # The panel supplies plain text, never SSML or external audio references.
    p=src/'ssml.c';s=p.read_text();s=replace_function(s,'int ProcessSsmlTag(', 'return 0; /* SSML is not enabled in this instrument. */');p.write_text(s)
    # Lyrics spell their numbers out; a digit left in one is read digit by digit (the flash this saves
    # holds the instrument's synth). Roman numerals are read as words.
    p=src/'numbers.c';s=p.read_text()
    s=replace_function(s,'int TranslateNumber(Translator *tr, char *word1, char *ph_out, unsigned int *flags, WORD_TAB *wtab, int control)','(void)tr;(void)word1;(void)ph_out;(void)flags;(void)wtab;(void)control;return 0;')
    s=replace_function(s,'int TranslateRoman(Translator *tr, char *word, char *ph_out, WORD_TAB *wtab)','(void)tr;(void)word;(void)ph_out;(void)wtab;return 0;')
    p.write_text(s)
    p=src/'speech.c';s=p.read_text().replace('option_ssml = flags & espeakSSML;', 'option_ssml = 0; /* plain text only */');p.write_text(s)
    # Klatt's upstream initializer hard-codes 22050 Hz. Its excitation and
    # resonator coefficients must use the actual compiled phoneme sample rate.
    p=src/'klatt.c';s=p.read_text();assert 'kt_globals.samrate = 22050;' in s;s=s.replace('kt_globals.samrate = 22050;', 'kt_globals.samrate = samplerate;');p.write_text(s)
    s,h=klatt_filters(p.read_text(),(src/'klatt.h').read_text())
    s=s.replace('\t\tkt_frame.F0hz10 = (wdata->pitch * 10) / 4096;',
                '\t\tkt_frame.F0hz10 = dx_sing_active ? dx_sing_f0() : (wdata->pitch * 10) / 4096;')
    needle='\t\t// Get low-passed random number for aspiration and frication noise'
    assert needle in s
    s=s.replace(needle,'''\t\tif(dx_sing_measure) {
            *out_ptr++=0; *out_ptr++=0; sample_count++;
            if(out_ptr+2>out_end)return 1;
            continue;
        }
'''+needle)
    # A duration-only pass does not need filter coefficients. Keep its command
    # clock and flutter phase progression identical to a normal dry pass.
    needle='\t\tframe_init(&kt_frame); // get parameters for next frame of speech'
    assert needle in s
    s=s.replace(needle,'\t\tif(!dx_sing_measure)frame_init(&kt_frame); // coefficients only for audible synthesis')
    # Native excitation controls; these modify Klatt aspiration/open-phase
    # turbulence and source tilt, rather than a weak post-render EQ.
    assert '#include "klatt.h"' in s
    s=s.replace('#include "klatt.h"','#include "klatt.h"\n#include "singing.h"')
    native_formant='\t\t\tkt_frame.Fhz[ix] = peaks[ix].freq;'
    assert native_formant in s
    s=s.replace(native_formant,'\t\t\tkt_frame.Fhz[ix] = peaks[ix].freq*(125-(int)dx_sing_body_size/2)/100;')
    native='\t\tkt_frame.Kopen = klattp[KLATT_Kopen];'
    assert native in s
    s=s.replace(native,native+"\n\t\tkt_frame.ASP += dx_sing_breath*45/100;\n\t\tkt_frame.Aturb += dx_sing_breath*35/100;\n\t\tkt_frame.TLTdb -= dx_sing_edge*18/100;\n\t\tif(kt_frame.TLTdb<0)kt_frame.TLTdb=0;\n\t\tkt_frame.Kopen -= dx_sing_edge*20/100;\n\t\tif(kt_frame.Kopen<10)kt_frame.Kopen=10;\n\t\tkt_frame.Kskew += dx_sing_edge*20/100;")
    needle='\tstatic int time_count;'
    assert s.count(needle)==1
    s=s.replace(needle,needle+'\n\tif(dx_sing_measure){time_count++;return;}')
    # A compact frame can be shorter than Klatt's 64-sample control step.
    # Cap interpolation to its endpoint instead of overshooting the filter.
    begin=s.rindex('static void SetSynth_Klatt(');end=s.index('\n}',begin)+2
    body=s[begin:end];body=body.replace('{','{\n\tint interpolation_length=length<STEPSIZE?STEPSIZE:length;',1)
    body=re.sub(r'/\s*length\b','/ interpolation_length',body)
    s=s[:begin]+body+s[end:]
    p.write_text(s);(src/'klatt.h').write_text(h)
    p=src/'wavegen.c';s=p.read_text();s=s.replace('int WavegenFill(void)\n{','int WavegenFill(void)\n{\n\tdx_sing_buffer = out_ptr;');p.write_text(s)
    p=src/'synthesize.c';s=p.read_text();s='#include "singing.h"\n'+s
    s=s.replace('\tCalcLengths(translator);','\tCalcLengths(translator);\n\tdx_sing_select();')
    s=s.replace('int frame_lengths[N_SEQ_FRAMES];','int frame_lengths[N_SEQ_FRAMES];\n\tint dx_hold=0,dx_hold_frame=-1;')
    needle='\tfor (frameix = 1; frameix < n_frames; frameix++) {\n\t\tframe2 = frames[frameix].frame;'
    assert s.count(needle)==1
    s=s.replace(needle,'''\tif(dx_sing_active && dx_sing_compact<100)for(int j=1;j<n_frames;j++){frame_lengths[j]=frame_lengths[j]*dx_sing_compact/100;if(frame_lengths[j]<1)frame_lengths[j]=1;}\n\tif(dx_sing_active && this_ph->type==phVOWEL && which==2 && n_frames>1) {
        int body=0,longest=1;
        for(int j=1;j<n_frames;j++){
            body+=frame_lengths[j];
            if(frame_lengths[j]>frame_lengths[longest])longest=j;
        }
        dx_sing_body += body;
        if(dx_sing_adjust>=0){
            if(dx_sing_style==1)frame_lengths[longest]+=dx_sing_adjust;
            else if(dx_sing_style==0){dx_hold=dx_sing_adjust;dx_hold_frame=longest;}
        }
        else if(body>0){
            int target=body+dx_sing_adjust;if(target<220)target=220;
            int remaining=target;
            for(int j=1;j<n_frames;j++){
                int n=j==n_frames-1?remaining:(int)((int64_t)frame_lengths[j]*target/body);
                frame_lengths[j]=n;remaining-=n;
            }
        }
    }
'''+needle);p.write_text(s)
    p=src/'synthesize.c';s=p.read_text()
    s=s.replace('\tif (wav_scale == 0) {','\tif(dx_sing_active && dx_sing_compact<100){length=length*dx_sing_compact/100;if(length<2)length=2;}\n\tif (wav_scale == 0) {',1)
    s=s.replace('\twcmdq[wcmdq_tail][1] = len;','\tif(dx_sing_active && dx_sing_compact<100)len=len*dx_sing_compact/100;\n\twcmdq[wcmdq_tail][1] = len;',1)
    needle='\t\tlen = frame_lengths[frameix];'
    assert s.count(needle)==1
    s=s.replace(needle,'''        if(dx_hold && frameix==dx_hold_frame && modulation>=0){
            /* Sustain the vowel target; retain its original entry/exit motion. */
            q=wcmdq[wcmdq_tail];q[0]=wcmd_spect;
            q[1]=dx_hold+(modulation<<17);q[2]=q[3]=(intptr_t)frame1;
            WcmdqInc();pitch_length+=dx_hold;amp_length+=dx_hold;total_len+=dx_hold;
        }
'''+needle);p.write_text(s)
    # Spectral command lengths need 17 bits for a 4.5-second held vowel.
    # Keep modulation separate; sample/mix/marker command encodings stay intact.
    p=src/'synthesize.c';s=p.read_text()
    assert s.count('len = q[1] & 0xffff;')==2
    s=s.replace('len = q[1] & 0xffff;','len = q[1] & 0x1ffff;')
    s=s.replace('q[1] = len + (modulation << 16);','q[1] = len + (modulation << 17);');p.write_text(s)
    p=src/'wavegen.c';s=p.read_text()
    s=s.replace('Wavegen(length & 0xffff, q[1] >> 16,','Wavegen(length & 0x1ffff, q[1] >> 17,')
    s=s.replace('Wavegen_Klatt(length & 0xffff,','Wavegen_Klatt(length & 0x1ffff,');p.write_text(s)
    return [src/(n+'.c') for n in UNITS]+[out/'data.c']+[uc,upstream/'src/ucd-tools/src/ctype.c']


if __name__=='__main__':prepare(Path(__file__).resolve().parents[1])
