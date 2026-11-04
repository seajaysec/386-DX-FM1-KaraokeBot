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
long es_strtol(const char *,char **,int);
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
            char *end;long v=es_strtol(s,&end,*fmt=='x'?16:10);if(end==s)break;*va_arg(a,int *)=(int)v;s=end;n++;
        }else break;
        fmt++;
    }
    va_end(a);return n;
}
/* strtol, atoi and strtok without the C library's per-thread state (its 1 KB of .data) */
long es_strtol(const char *s,char **end,int base)
{
    const char *p=s;
    unsigned long v=0;
    int neg=0,any=0,d;
    while(*p==' '||(*p>='\t'&&*p<='\r'))p++;
    if(*p=='+'||*p=='-')neg=*p++=='-';
    if(base==16&&p[0]=='0'&&(p[1]=='x'||p[1]=='X'))p+=2;
    for(;;p++){
        d=*p>='0'&&*p<='9'?*p-'0':*p>='a'&&*p<='z'?*p-'a'+10:*p>='A'&&*p<='Z'?*p-'A'+10:99;
        if(d>=base)break;
        any=1;
        v=v>(0x7fffffffUL-(unsigned)d)/(unsigned)base?0x80000000UL:v*(unsigned)base+(unsigned)d;
    }
    if(end)*end=(char *)(any?p:s);
    if(v>=0x80000000UL)return neg?(long)(-0x7fffffffL-1):0x7fffffffL;
    return neg?-(long)v:(long)v;
}
int es_atoi(const char *s){return (int)es_strtol(s,0,10);}
char *es_strtok(char *s,const char *sep)
{
    static char *next;
    char *t;
    if(!s)s=next;
    if(!s)return 0;
    while(*s&&strchr(sep,*s))s++;
    if(!*s){next=0;return 0;}
    t=s;
    while(*s&&!strchr(sep,*s))s++;
    if(*s)*s++=0;
    next=s;
    return t;
}
/* qsort for the library's short voice lists: an insertion sort */
void es_qsort(void *base,size_t n,size_t size,int (*cmp)(const void *,const void *))
{
    unsigned char *b=base;
    size_t i,j,k;
    for(i=1;i<n;i++)
        for(j=i;j>0&&cmp(b+(j-1)*size,b+j*size)>0;j--)
            for(k=0;k<size;k++){unsigned char t=b[(j-1)*size+k];b[(j-1)*size+k]=b[j*size+k];b[j*size+k]=t;}
}
char *es_strdup(const char *s){size_t n=strlen(s)+1;char *p=es_malloc(n);if(p)memcpy(p,s,n);return p;}

/* sin and cos for the speech library's filter set-up and flutter: the argument brought to within pi/4 of a
 * quarter turn (pi/2 in two parts, exact while the turn count stays under 2^20), then the classic minimax
 * kernels. Within an ulp or two of the C library's, in a few hundred bytes instead of its 4 KB. */
static double es_kernel(double r,int odd)
{
    double z=r*r;
    if(odd)
        return r+r*z*(-1.66666666666666324348e-01+z*(8.33333333332248946124e-03+z*(-1.98412698298579493134e-04
               +z*(2.75573137070700676789e-06+z*(-2.50507602534068634195e-08+z*1.58969099521155010221e-10)))));
    return 1.0-0.5*z+z*z*(4.16666666666666019037e-02+z*(-1.38888888888741095749e-03+z*(2.48015872894767294178e-05
           +z*(-2.75573143513906633035e-07+z*(2.08757232129817482790e-09-z*1.13596475577881948265e-11)))));
}
static double es_turn(double x,int q)          /* q: 0 sin, 1 cos */
{
    double t=x*6.36619772367581382433e-01;
    int k=(int)(t>=0.0?t+0.5:t-0.5);
    double r=(x-k*1.57079632673412561417e+00)-k*6.07710050650619224932e-11;
    k+=q;
    double v=es_kernel(r,!(k&1));
    return k&2?-v:v;
}
double es_sin(double x){return es_turn(x,0);}
double es_cos(double x){return es_turn(x,1);}
/* exp, the same way as the C library's: x = k ln2 + r, a rational kernel for e^r, then k added to the
 * exponent's bits */
double es_exp(double x)
{
    union {double d;uint64_t u;} y;
    int k;
    double hi,lo,r,t,c;
    if(x<-700.0)return 0.0;
    if(x>700.0)x=700.0;
    k=x>-0.34657359027997264 && x<0.34657359027997264 ? 0 : (int)(x*1.44269504088896338700e+00+(x<0.0?-0.5:0.5));
    hi=x-k*6.93147180369123816490e-01;
    lo=k*1.90821492927058770002e-10;
    r=hi-lo;
    t=r*r;
    c=r-t*(1.66666666666666019037e-01+t*(-2.77777777770155933842e-03+t*(6.61375632143793436117e-05
      +t*(-1.65339022054652515390e-06+t*4.13813679705723846039e-08))));
    if(!k)return 1.0-((r*c)/(c-2.0)-r);
    y.d=1.0-((lo-(r*c)/(2.0-c))-hi);
    y.u+=(uint64_t)(int64_t)k<<52;
    return y.d;
}
