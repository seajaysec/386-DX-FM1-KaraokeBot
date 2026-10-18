/* SPDX-License-Identifier: GPL-3.0-only */
#include "speech.h"
#include <espeak-ng/speak_lib.h>
#include <espeak-ng/espeak_ng.h>
#include <string.h>
#include "voice.h"
#include "../dsp/fastmath.h"
#ifdef DX_HOST
#define POOL
#else
#define POOL __attribute__((section(".pool")))
#endif
dx_speech_cache_t dx_speech_cache POOL;
uint32_t dx_speech_length[DX_SPEECH_CACHES];
uint8_t dx_speech_truncated[DX_SPEECH_CACHES];
uint32_t dx_speech_clipped[DX_SPEECH_CACHES];
unsigned es_pitch_q16=65536;
static int current;
static uint8_t pending_sound[4]={50,0,0,0};
void dx_speech_sound(const uint8_t sound[4]){for(unsigned k=0;k<4;k++)pending_sound[k]=sound[k]>100?100:sound[k];}
static int16_t *destination;
static unsigned capacity;
/* Klatt is synthesized at its native 22050 Hz. Low-pass before decimating
 * into the existing 11025 Hz phrase caches; playback duration stays unchanged. */
static int16_t history[31];
static unsigned history_pos, decimate_phase;

static int capture(short *samples,int count,espeak_EVENT *events)
{
    (void)events;
    if(samples)dx_sing_samples+=count;
    if(dx_sing_measure){dx_speech_idle();return 0;}
    static const int16_t filter[31]={0,1,-10,-27,35,129,-29,-377,-146,802,772,-1332,-2493,1786,10063,14420,10063,1786,-2493,-1332,772,802,-146,-377,-29,129,35,-27,-10,1,0};
    if(samples)for(int i=0;i<count;i++){
        if(samples[i]==32767 || samples[i]==-32768)dx_speech_clipped[current]++;
        history[history_pos]=samples[i];history_pos=(history_pos+1)%31;
        decimate_phase^=1;if(decimate_phase)continue;
        int32_t sum=0;unsigned pos=history_pos;
        for(unsigned j=0;j<31;j++){pos=pos?pos-1:30;sum+=(int32_t)history[pos]*filter[j];}
        int sample=sum/32768;
        if(sample>32767){sample=32767;dx_speech_clipped[current]++;}
        if(sample< -32768){sample=-32768;dx_speech_clipped[current]++;}
        unsigned n=dx_speech_length[current];
        if(n<capacity){destination[n]=(int16_t)sample;dx_speech_length[current]=n+1;}
        else dx_speech_truncated[current]=1;
    }
    dx_speech_idle();return 0;
}
int dx_speech_init(void)
{
    espeak_ng_InitializePath(0);
    if(espeak_ng_Initialize(0)!=ENS_OK || espeak_ng_GetSampleRate()!=22050)return -1;
    if(espeak_ng_InitializeOutput(ENOUTPUT_MODE_SYNCHRONOUS,20,0)!=ENS_OK)return -1;
    espeak_SetSynthCallback(capture);
    if(espeak_ng_SetVoiceByFile("/espeak/klatt-en")!=ENS_OK || !voice || !voice->klattv[0])return -1;
    if(espeak_ng_SetParameter(espeakVOLUME,60,0)!=ENS_OK)return -1;
    return espeak_ng_SetParameter(espeakRATE,175,0)==ENS_OK?0:-1;
}
static int make(int slot,const char *text,int semitones,int16_t *dst,unsigned max)
{
    char input[65];unsigned n,i=0;
    if((unsigned)slot>=DX_SPEECH_CACHES || !text || semitones < -12 || semitones > 12)return -1;
    destination=dst;capacity=max;
    memset(history,0,sizeof history);history_pos=decimate_phase=0;
    current=slot;if(!dx_sing_measure){dx_speech_clipped[slot]=0;dx_speech_length[slot]=0;dx_speech_truncated[slot]=0;}
    dx_sing_samples=0;
    while(text[i] && i<63){unsigned char c=text[i];input[i]=(c>=32 && c<=126)?c:' ';i++;}input[i]=0;
    if(dx_sing_active)for(unsigned j=0;j<i;j++)if(strchr(".,!?;:",input[j]))input[j]=' ';
    if(!i)return 0;
    espeak_ng_SetParameter(espeakRATE,dx_sing_active?dx_sing_rate:175,0);
    espeak_ng_SetParameter(espeakVOLUME,dx_sing_active?(dx_sing_compact<100?10:40):60,0); /* compact transients get headroom before fixed phrase normalization */
    es_pitch_q16=(unsigned)(65536.0f*fm_exp2f((float)semitones/12.0f)+0.5f);
    int rc=espeak_ng_Synthesize(input,i+1,0,POS_CHARACTER,0,espeakCHARS_UTF8|(dx_sing_active?0:espeakENDPAUSE),0,0);
    n=dx_speech_length[slot];
    if(dx_sing_measure)return rc==ENS_OK?0:-1;
    for(i=0;i<64 && i<n/2;i++){
        dst[i]=(int16_t)(dst[i]*(int)i/64);
        dst[n-1-i]=(int16_t)(dst[n-1-i]*(int)i/64);
    }
    /* Match prepared phrases to the instrument's cache level. This is one
     * constant gain per phrase, so consonants and dynamics are preserved. */
    int peak=0;
    for(i=0;i<n;i++){int v=dst[i];if(v<0)v=-v;if(v>peak)peak=v;}
    if(peak)for(i=0;i<n;i++)dst[i]=(int16_t)((int32_t)dst[i]*28672/peak);
    return rc==ENS_OK?0:-1;
}
static int sing_local(int slot,const char *text,unsigned syllable,
                   const dx_sing_note_t *notes,unsigned count,unsigned expression,int16_t *dst,unsigned max)
{
    if((unsigned)slot>=DX_SPEECH_CACHES || !dst || max>DX_SPEECH_LONG_SAMPLES || !text || dx_sing_begin(syllable,notes,count,expression))return -1;
    unsigned native=0;for(unsigned i=0;i<count;i++)native+=notes[i].samples;
    unsigned target=native/2;
    if(target>max){dx_sing_end();return -4;}
    dx_sing_measure=1;
    int rc=make(slot,text,0,dst,max);
    unsigned normal=dx_sing_samples,body=dx_sing_body;
    /* Preserve ordinary articulation whenever it fits. Only compact the
     * consonants when the requested short note cannot contain them. */
    if(!rc && native+64<normal-body+220 && dx_sing_rate<300){
        dx_sing_rate=300;dx_sing_body=0;
        rc=make(slot,text,0,dst,max);normal=dx_sing_samples;body=dx_sing_body;
    }
    if(syllable>=dx_sing_count()){dx_sing_end();return -2;}
    /* Short beats keep their requested onset, pitch and duration. Compress
     * native articulation commands rather than resampling pitched PCM. */
    for(unsigned tries=0;!rc&&native+64<normal-body+220&&tries<8;tries++){
        unsigned need=normal>body?normal-body+220:220;
        unsigned next=dx_sing_compact*(native>64?native-64:1)/need;
        if(next>=dx_sing_compact)next=dx_sing_compact-1;
        if(next<1)next=1;dx_sing_compact=next;dx_sing_body=0;
        rc=make(slot,text,0,dst,max);normal=dx_sing_samples;body=dx_sing_body;
    }
    if(rc || native+64<normal-body+220){dx_sing_end();return rc?rc:-3;}
    dx_sing_measure=0;dx_sing_adjust=(int)native-(int)normal;dx_sing_body=0;
    rc=make(slot,text,0,dst,max);
    unsigned n=dx_speech_length[slot];
    /* Native segments round to synthesis frames; only a sub-frame tail is
     * adjusted. Do not time-stretch consonants or silently chop a long result. */
    unsigned actual=dx_sing_samples/2;
    if(actual>target+64 || (target>actual+64 && dx_sing_style!=2))rc=-3;
    else {
        if(n<target)memset(dst+n,0,(target-n)*2);
        dx_speech_length[slot]=target;dx_speech_truncated[slot]=0;
        for(unsigned i=0;i<32 && i<target;i++)dst[target-1-i]=(int16_t)((int32_t)dst[target-1-i]*i/32);
    }
    dx_sing_end();return rc;
}

static int make_local(int slot,const char *text,int pitch,int16_t *dst,unsigned max)
{return make(slot,text,pitch,dst,max);}

static int scan_local(const char *text)
{
    dx_sing_note_t note={22050,60};
    if(!text || dx_sing_begin(0,&note,1,0))return -1;
    dx_sing_measure=1;
    int rc=make(0,text,0,dx_speech_cache.line[0],DX_SPEECH_LONG_SAMPLES);
    unsigned count=dx_sing_count();dx_sing_end();return rc?-1:(int)count;
}
static int articulation_local(const char *text,uint16_t minimum[64])
{
    int count=scan_local(text);if(count<1||count>64)return -1;
    dx_sing_note_t note={22050,60};
    for(unsigned i=0;i<(unsigned)count;i++){
        if(dx_sing_begin(i,&note,1,0))return -1;
        dx_sing_rate=300;dx_sing_measure=1;
        int rc=make(0,text,0,dx_speech_cache.line[0],DX_SPEECH_LONG_SAMPLES);
        unsigned normal=dx_sing_samples,body=dx_sing_body;
        unsigned needed=normal>body?normal-body+220:220;
        dx_sing_end();if(rc)return -1;
        minimum[i]=(needed*1000u+22049u)/22050u;
    }
    return count;
}

/* One pronunciation owner. CPU0 stages a complete immutable request and waits
 * while servicing controls; CPU1 alone touches eSpeak globals and destination.
 * The destination is unpublished and cannot be the locked playback cache.
 * A fault abandons the engine, never reruns partially mutated pronunciation. */
static struct {char text[64];dx_sing_note_t notes[DX_SING_NOTES];unsigned count,syllable,expression,max;int slot,pitch,op,result;uint8_t sound[4];int16_t *dst;uint16_t *minimum;} voice_job;
static int voice_job_active,voice_job_faulted;
int dx_speech_active(void){return voice_job_active;}
static void voice_execute(void *unused)
{
    (void)unused;
    dx_sing_body_size=voice_job.sound[0];dx_sing_breath=voice_job.sound[1];dx_sing_edge=voice_job.sound[2];dx_sing_glide=voice_job.sound[3];
    voice_job.result=voice_job.op==0?sing_local(voice_job.slot,voice_job.text,voice_job.syllable,voice_job.notes,voice_job.count,voice_job.expression,voice_job.dst,voice_job.max):voice_job.op==1?make_local(voice_job.slot,voice_job.text,voice_job.pitch,voice_job.dst,voice_job.max):voice_job.op==2?scan_local(voice_job.text):articulation_local(voice_job.text,voice_job.minimum);
}
static int voice_run(const char *text,int op)
{
    if(voice_job_active||voice_job_faulted||!text)return -1;
    unsigned i=0;while(text[i]&&i<63){voice_job.text[i]=text[i];i++;}voice_job.text[i]=0;voice_job.op=op;memcpy(voice_job.sound,pending_sound,4);voice_job_active=1;
#if defined(DX_HOST) && !defined(DX_SPEECH_WORKER)
    voice_execute(0);int rc=1;
#else
    extern int dx_speech_run(void (*)(void *),void *);int rc=dx_speech_run(voice_execute,0);
#endif
    voice_job_active=0;if(rc<0)voice_job_faulted=1;return rc==1?voice_job.result:-1;
}
int dx_speech_sing_into(int slot,const char *text,unsigned syllable,const dx_sing_note_t *notes,unsigned count,unsigned expression,int16_t *dst,unsigned max)
{
    if(voice_job_active||count>DX_SING_NOTES||!notes)return -1;
    voice_job.slot=slot;voice_job.syllable=syllable;voice_job.count=count;voice_job.expression=expression;voice_job.dst=dst;voice_job.max=max;memcpy(voice_job.notes,notes,count*sizeof *notes);return voice_run(text,0);
}
int dx_speech_make_into(int slot,const char *text,int pitch,int16_t *dst,unsigned max)
{if(voice_job_active)return -1;voice_job.slot=slot;voice_job.pitch=pitch;voice_job.dst=dst;voice_job.max=max;return voice_run(text,1);}
int dx_speech_scan(const char *text){return voice_run(text,2);}
int dx_speech_articulation_native(const char *text,uint16_t minimum[64]){if(voice_job_active)return -1;voice_job.minimum=minimum;return voice_run(text,3);}
int dx_speech_articulation(const char *text,uint16_t minimum[64]){int count=dx_speech_articulation_native(text,minimum);for(int i=0;i<count;i++)minimum[i]=14+(minimum[i]+99)/100;return count;}
