/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define memset checked_memset
#define memcpy checked_memcpy
#define memcmp checked_memcmp
#define strcmp checked_strcmp
#define strcpy checked_strcpy
#define memmove checked_memmove
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
#undef strcmp
#undef strcpy
#undef memmove
int main(void){
    {   /* memcpy and memmove: the plain copies the firmware links (the toolchain library's were wrong on the FM-1) */
        unsigned char a[64], b[64];
        for (int i = 0; i < 64; i++)
            a[i] = (unsigned char)(i * 7 + 1);
        for (unsigned off = 0; off < 4; off++)
            for (unsigned len = 0; len < 40; len++) {
                memset(b, 0, sizeof b);
                checked_memcpy(b + off, a + 1, len);
                for (unsigned i = 0; i < len; i++)
                    if (b[off + i] != a[1 + i]) { puts("memcpy wrong"); return 1; }
                if (len < 30 && b[off + len] != 0) { puts("memcpy wrote past its end"); return 1; }
            }
        for (int i = 0; i < 64; i++)
            b[i] = (unsigned char)i;
        checked_memmove(b + 3, b, 20);                    /* overlapping, upward */
        for (int i = 0; i < 20; i++)
            if (b[3 + i] != i) { puts("memmove up wrong"); return 1; }
        for (int i = 0; i < 64; i++)
            b[i] = (unsigned char)i;
        checked_memmove(b, b + 3, 20);                    /* overlapping, downward */
        for (int i = 0; i < 20; i++)
            if (b[i] != i + 3) { puts("memmove down wrong"); return 1; }
    }

 for(unsigned prefix=0;prefix<=31;prefix+=31)for(unsigned a=0;a<256;a++)for(unsigned b=0;b<256;b++){
  char x[34],y[34];memset(x,'p',prefix);memset(y,'p',prefix);x[prefix]=a;y[prefix]=b;x[prefix+1]=y[prefix+1]=0;
  int actual=checked_strcmp(x,y),host=strcmp(x,y);assert(actual==(int)a-(int)b);assert((actual>0)-(actual<0)==(host>0)-(host<0));
 }
 for(unsigned offset=0;offset<8;offset++)for(unsigned n=0;n<256;n++){
  char src[257],dst[272];for(unsigned i=0;i<n;i++)src[i]=(char)(1+i%255);src[n]=0;memset(dst,0x5a,sizeof dst);
  assert(checked_strcpy(dst+offset,src)==dst+offset);assert(!memcmp(dst+offset,src,n+1));for(unsigned i=0;i<offset;i++)assert(dst[i]==0x5a);for(unsigned i=offset+n+1;i<sizeof dst;i++)assert(dst[i]==0x5a);
 }
 size_t page=sysconf(_SC_PAGESIZE);char *a=mmap(0,page*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(a!=MAP_FAILED);
 assert(!mprotect(a+page,page,PROT_NONE)&&!mprotect(a+page*3,page,PROT_NONE));
 char *x=a+page-2,*y=a+page*3-2;x[0]=y[0]='a';x[1]=y[1]=0;
 assert(!checked_strcmp(x,y)&&!checked_strcmp(x+1,y+1));y[0]='b';assert(checked_strcmp(x,y)==-1);assert(checked_strcmp(x+1,y)==-'b');
 x[0]='a';x[1]=0;assert(checked_strcpy(y,x)==y&&!memcmp(x,y,2));assert(checked_strcpy(y+1,x+1)==y+1&&!y[1]);
 assert(!munmap(a,page*4));puts("PASS target strcpy boundary/canary/return and strcmp: all unsigned-byte pairs with empty/long prefixes, host ordering and guard-page terminators");return 0;
}
