/* SPDX-License-Identifier: GPL-3.0-only */
/* eSpeak's allocations stay outside the render path in a fixed, coalescing
 * arena. The target never calls an OS allocator or firmware ROM libc. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#ifdef DX_HOST
#define POOL
#else
#define POOL __attribute__((section(".pool")))
#endif
#define HEAP_SIZE (24u*1024u)   /* the library's 252 lines peak at 20,152 B (64-bit host; less on the FM-1) */
static uint64_t arena[HEAP_SIZE/8] POOL;
typedef struct { uint32_t size,used; } header_t;
static int initialized;
uint32_t es_heap_peak,es_heap_used,es_heap_failed;
void *es_malloc(size_t size)
{
    uint32_t off=0,n;
    if(size>HEAP_SIZE-8u) {es_heap_failed++;return 0;}
    n=((uint32_t)size+7u)&~7u;if(!n)n=8;
    if(!initialized) {header_t *h=(header_t *)arena;h->size=HEAP_SIZE-8;h->used=0;initialized=1;}
    while(off<HEAP_SIZE) {
        header_t *h=(header_t *)((uint8_t *)arena+off);
        if(!h->used && h->size>=n) {
            if(h->size>=n+16) {header_t *next=(header_t *)((uint8_t *)(h+1)+n);next->size=h->size-n-8;next->used=0;h->size=n;}
            h->used=1;es_heap_used+=h->size;
            if(es_heap_used>es_heap_peak)es_heap_peak=es_heap_used;
            return h+1;
        }
        off+=h->size+8;
    }
    es_heap_failed++;return 0;
}
void es_free(void *p)
{
    uint32_t off=0;header_t *h;
    if(!p)return;
    h=(header_t *)p-1;if(!h->used)return;
    es_heap_used-=h->size;h->used=0;
    while(off<HEAP_SIZE) {
        header_t *a=(header_t *)((uint8_t *)arena+off);
        uint32_t next=off+8+a->size;
        if(next<HEAP_SIZE) {
            header_t *b=(header_t *)((uint8_t *)arena+next);
            if(!a->used && !b->used) {a->size+=8+b->size;continue;}
        }
        off=next;
    }
}
void *es_calloc(size_t n,size_t size)
{
    void *p;if(size && n>HEAP_SIZE/size) {es_heap_failed++;return 0;}
    p=es_malloc(n*size);if(p)memset(p,0,n*size);return p;
}
#ifndef DX_HOST
/* No OS file or heap services are used. */
int _write(int fd,const void *p,int n) {(void)fd;(void)p;return n;}
int _read(int fd,void *p,int n) {(void)fd;(void)p;(void)n;return 0;}
int _close(int fd) {(void)fd;return -1;}
int _lseek(int fd,int p,int w) {(void)fd;(void)p;(void)w;return -1;}
int _isatty(int fd) {(void)fd;return 0;}
void *_sbrk(int n) {(void)n;return (void *)-1;}
void _exit(int code) {(void)code;for(;;){} }
int _kill(int pid,int sig) {(void)pid;(void)sig;return -1;}
int _getpid(void) {return 1;}
#endif

void *es_realloc(void *old,size_t n)
{
    if(!old)return es_malloc(n);
    if(!n){es_free(old);return 0;}
    header_t *h=(header_t *)old-1;
    if(n<=h->size)return old;
    void *p=es_malloc(n);if(!p)return 0;
    memcpy(p,old,h->size);es_free(old);return p;
}
#include <stdio.h>
#include <stdarg.h>
extern const void *es_data(const char *,int *);
typedef struct {const unsigned char *data;size_t length,pos;} es_file;
static es_file files[4];
FILE *es_fopen(const char *path,const char *mode)
{
    int n;const void *p=es_data(path,&n);
    if(!p || mode[0]!='r')return 0;
    for(unsigned i=0;i<4;i++)if(!files[i].data){files[i]=(es_file){p,(size_t)n,0};return (FILE *)&files[i];}
    return 0;
}
int es_fclose(FILE *f){if(!f)return -1;((es_file *)f)->data=0;return 0;}
int es_fgetc(FILE *f){es_file *p=(es_file *)f;return p->pos<p->length?p->data[p->pos++]:EOF;}
int es_feof(FILE *f){es_file *p=(es_file *)f;return p->pos==p->length;}
long es_ftell(FILE *f){return ((es_file *)f)->pos;}
int es_fseek(FILE *f,long off,int whence)
{
    es_file *p=(es_file *)f;long pos=off+(whence==SEEK_CUR?(long)p->pos:whence==SEEK_END?(long)p->length:0);
    if(pos<0 || (size_t)pos>p->length)return -1;p->pos=pos;return 0;
}
size_t es_fread(void *out,size_t size,size_t count,FILE *f)
{
    es_file *p=(es_file *)f;if(!size)return 0;
    if(count>(p->length-p->pos)/size)count=(p->length-p->pos)/size;
    memcpy(out,p->data+p->pos,size*count);p->pos+=size*count;return count;
}
char *es_fgets(char *out,int n,FILE *f)
{
    int i=0,c;if(n<=1)return 0;
    while(i<n-1 && (c=es_fgetc(f))!=EOF){out[i++]=c;if(c=='\n')break;}
    out[i]=0;return i?out:0;
}
int es_fprintf(FILE *f,const char *fmt,...){
#ifdef DX_HOST
    va_list a;va_start(a,fmt);int n=vfprintf(f,fmt,a);va_end(a);return n;
#else
    (void)f;(void)fmt;return 0;
#endif
}
int es_printf(const char *fmt,...){(void)fmt;return 0;}
/* Diagnostic rule decoding and on-device dictionary compilation are disabled. */
void print_dictionary_flags(unsigned int *flags,char *buf,int n){(void)flags;if(n)buf[0]=0;}
char *DecodeRule(const char *group,int length,char *rule,int control,char *out)
{(void)group;(void)length;(void)rule;(void)control;out[0]=0;return out;}
/* The embedded eSpeak paths use string, character and integer conversions. */
static int es_format(char *out,size_t cap,const char *fmt,va_list args)
{
    size_t n=0;
#define PUT(c) do {if(n+1<cap)out[n]=(c);n++;}while(0)
    while(*fmt){
        if(*fmt!='%'){PUT(*fmt++);continue;}
        fmt++;
        if(*fmt=='s'){const char *s=va_arg(args,const char *);while(*s){PUT(*s++);}}
        else if(*fmt=='c'){int c=va_arg(args,int);PUT(c);}
        else if(*fmt=='d' || *fmt=='u' || *fmt=='x'){
            unsigned v=va_arg(args,unsigned),base=*fmt=='x'?16:10;char b[16];unsigned i=0;
            if(*fmt=='d' && (int)v<0){PUT('-');v=0u-v;}
            do{b[i++]="0123456789abcdef"[v%base];v/=base;}while(v);
            while(i){PUT(b[--i]);}
        }else{PUT(*fmt);}
        if(*fmt)fmt++;
    }
    if(cap)out[n<cap?n:cap-1]=0;
#undef PUT
    return (int)n;
}
int es_sprintf(char *out,const char *fmt,...){va_list a;va_start(a,fmt);int n=es_format(out,(size_t)-1,fmt,a);va_end(a);return n;}
int es_snprintf(char *out,size_t cap,const char *fmt,...){va_list a;va_start(a,fmt);int n=es_format(out,cap,fmt,a);va_end(a);return n;}
static int es_space(char c){return c==' ' || c=='\t' || c=='\r' || c=='\n';}
int es_sscanf(const char *s,const char *fmt,...)
{
    int n=0;va_list a;va_start(a,fmt);
    while(*fmt){
        if(es_space(*fmt)){while(es_space(*s))s++;fmt++;continue;}
        if(*fmt!='%'){if(*s++!=*fmt++)break;continue;}
        fmt++;
        if(*fmt=='s'){char *p=va_arg(a,char *);while(es_space(*s))s++;if(!*s)break;while(*s && !es_space(*s))*p++=*s++;*p=0;n++;}
        else if(*fmt=='c'){if(!*s)break;*va_arg(a,char *)=*s++;n++;}
        else if(*fmt=='d' || *fmt=='x'){
            char *end;long v=strtol(s,&end,*fmt=='x'?16:10);if(end==s)break;*va_arg(a,int *)=(int)v;s=end;n++;
        }else break;
        fmt++;
    }
    va_end(a);return n;
}
char *es_strdup(const char *s){size_t n=strlen(s)+1;char *p=es_malloc(n);if(p)memcpy(p,s,n);return p;}
