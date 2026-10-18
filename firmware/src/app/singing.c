/* SPDX-License-Identifier: GPL-3.0-only */
#include "singing.h"
#include "../dsp/fastmath.h"
#include <string.h>
#include "synthesize.h"
#include "phoneme.h"
#include "wavegen.h"

int dx_sing_style,dx_sing_rate;
unsigned dx_sing_compact=100;
unsigned dx_sing_body_size=50,dx_sing_breath,dx_sing_edge,dx_sing_glide;
int dx_sing_active,dx_sing_measure,dx_sing_adjust,dx_sing_body;
unsigned dx_sing_samples;
unsigned char *dx_sing_buffer;
static unsigned selected,total,curve_count,express;
static dx_sing_note_t curve[DX_SING_NOTES];
static uint8_t word_offsets[64];
unsigned dx_sing_offset(unsigned syllable){return syllable<64?word_offsets[syllable]:255;}
static unsigned word_start,word_length,word_first,word_last;
unsigned dx_sing_word_offset(void){return word_start?word_start-1:0;}
unsigned dx_sing_word_first(void){return word_first;}
unsigned dx_sing_word_last(void){return word_last;}
int dx_sing_begin(unsigned syllable,const dx_sing_note_t *notes,unsigned count,unsigned expression)
{
    if(!count || count>DX_SING_NOTES || syllable>=64)return -1;
    for(unsigned i=0;i<count;i++)if(notes[i].note<24 || notes[i].note>96 || !notes[i].samples)return -1;
    memcpy(curve,notes,count*sizeof *notes);curve_count=count;selected=syllable;
    dx_sing_style=(expression>>1)&3;if(dx_sing_style==2 && count>1)dx_sing_style=0; /* an explicit tie requests sustain */
    dx_sing_compact=100;express=expression&1;dx_sing_rate=dx_sing_style==1?300:175;dx_sing_active=1;dx_sing_measure=dx_sing_adjust=dx_sing_body=0;
    dx_sing_samples=total=word_start=word_length=0;return 0;
}
void dx_sing_end(void){dx_sing_active=dx_sing_measure=0;}
unsigned dx_sing_count(void){return total;}
void dx_sing_word(char *dst,unsigned size)
{
    /* The caller supplies the original line in dst; retain its selected word. */
    unsigned n=(unsigned)strlen(dst),start=word_start?word_start-1:0;
    if(start>n)start=n;
    unsigned len=word_length;if(len>n-start)len=n-start;if(len>=size)len=size-1;
    memmove(dst,dst+start,len);dst[len]=0;
}
void dx_sing_select(void)
{
    if(!dx_sing_active)return;
    unsigned nuclei[64],begins[64],words[64],lengths[64],count=0,ws=0,wl=0;
    unsigned n=n_phoneme_list,first=1,last=n;
    for(unsigned i=1;i<n;i++){
        PHONEME_LIST *p=&phoneme_list[i];
        if(p->newword & PHLIST_START_OF_WORD){ws=p->sourceix&0x7ff;wl=p->sourceix>>11;}
        if((p->synthflags&SFLAG_SYLLABLE) && p->type!=phPAUSE && count<64){
            nuclei[count]=i;words[count]=ws;lengths[count]=wl;count++;
        }
    }
    total=count;
    memset(word_offsets,255,sizeof word_offsets);for(unsigned j=0;j<count;j++)word_offsets[j]=words[j]?words[j]-1:0;
    if(!count || selected>=count){n_phoneme_list=0;return;}
    while(first<n && phoneme_list[first].type==phPAUSE)first++;
    while(last>first && phoneme_list[last-1].type==phPAUSE)last--;
    begins[0]=first;
    for(unsigned j=1;j<count;j++){
        unsigned at=nuclei[j],prev=nuclei[j-1];
        /* Explicit word boundaries take precedence. Within a word preserve
         * a final onset consonant, or stop/fricative + liquid onset cluster. */
        for(unsigned k=prev+1;k<=at;k++)if(phoneme_list[k].newword&PHLIST_START_OF_WORD){at=k;break;}
        if(at==nuclei[j] && !(phoneme_list[at].newword&PHLIST_START_OF_WORD) && at>prev+1){
            at--;
            if(at>prev+1 && phoneme_list[at].type==phLIQUID){
                unsigned t=phoneme_list[at-1].type;
                if(t==phSTOP||t==phVSTOP||t==phFRICATIVE||t==phVFRICATIVE)at--;
            }
        }
        begins[j]=at;
    }
    unsigned begin=begins[selected],end=selected+1<count?begins[selected+1]:last;
    word_start=words[selected];word_length=lengths[selected];
    word_first=word_last=selected;
    while(word_first && words[word_first-1]==word_start)word_first--;
    while(word_last+1<count && words[word_last+1]==word_start)word_last++;
    PHONEME_LIST pause=phoneme_list[0];pause.length=0;pause.prepause=0;pause.newword=0;
    unsigned len=end-begin;
    memmove(phoneme_list+1,phoneme_list+begin,len*sizeof *phoneme_list);
    phoneme_list[0]=pause;
    phoneme_list[1].prepause=0;phoneme_list[1].newword=PHLIST_START_OF_WORD;
    for(unsigned k=0;k<3;k++)phoneme_list[len+1+k]=pause;
    n_phoneme_list=len+2;
}
int dx_sing_f0(void)
{
    if(!dx_sing_active)return 0;
    unsigned at=dx_sing_samples+(unsigned)(out_ptr-dx_sing_buffer)/2,begin=0,i=0;
    while(i+1<curve_count && at>=begin+curve[i].samples){begin+=curve[i].samples;i++;}
    float note=curve[i].note;
    unsigned elapsed=at-begin;
    unsigned glide=441u+dx_sing_glide*110u;if(i && elapsed<glide)note=curve[i-1].note+(note-curve[i-1].note)*(float)elapsed/glide;
    /* A small delayed vibrato leaves short consonants/notes steady. */
    if(express && at>3307u){
        float depth=(float)(at-3307u)/2205.0f;if(depth>1)depth=1;
        note+=0.14f*depth*fm_sinf((float)(at%22050u)*(6.0f*FM_TWO_PI/22050.0f));
    }
    return (int)(4400.0f*fm_exp2f((note-69.0f)/12.0f)+0.5f);
}
