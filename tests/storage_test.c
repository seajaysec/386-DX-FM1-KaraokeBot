/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

static uint8_t nor[0x100000];
static int io_fail_after=-1;
static int io_step(void){if(io_fail_after==0)return -1;if(io_fail_after>0)io_fail_after--;return 0;}
static int fail_after = -1;            /* torn-write injection: stop after N programs */

static int st_read(uint32_t off, void *dst, uint32_t n) { assert(off<=sizeof nor && n<=sizeof nor-off);memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { assert(!(off%4096) && off+4096<=sizeof nor);if(io_step())return -9;memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    assert(off<=sizeof nor && n<=sizeof nor-off);if(io_step())return -9;
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/app/plat.h"     /* X0X: the object map */
#include "../firmware/src/storage.c"
#define OBJ_PROJECT0 OBJ_SOUND                 /* the test's three data objects: SOUND, PAT0, PAT1 */
#define OBJ_SETTINGS OBJ_SET

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(OBJ_PROJECT0, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_PROJECT0, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_PROJECT0, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_PROJECT0, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_PROJECT0 + 1, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_PROJECT0 + 2, a, sizeof a);
    st_save(OBJ_PROJECT0 + 2, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_PROJECT0 + 2, &h);
        nor[st_sector(OBJ_PROJECT0 + 2, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_PROJECT0 + 2, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_PROJECT0 + 2, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_PROJECT0 + 2, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_PROJECT0 + 2, got, sizeof got) < 0);
    {   /* X0X map: every object in a data region, none in the sample slots (0xA0000..0xDBFFF), no overlaps */
        uint32_t o, p, ok = 1;
        for (o = 0; o < OBJ_NOBJ; o++) {
            uint32_t a0 = st_sector(o, 0), a1 = st_sector(o, 1) + 4096;
            int in_data = (a0 >= 0x97000 && a1 <= 0xA0000) || (a0 >= 0xDC000 && a1 <= 0xE0000) ||
                          (a0 >= 0xFC000 && a1 <= 0xFF000);
            if (!in_data || a1 - a0 != 8192)
                ok = 0;
            for (p = 0; p < o; p++)
                if (a0 < st_sector(p, 1) + 4096 && st_sector(p, 0) < a1)
                    ok = 0;
        }
        bad += check("objects in data regions, clear of the sample slots", (int)ok);
    }
    {
        static uint8_t previous[0x100000],song_a[PLAT_SONG_MAX],song_b[PLAT_SONG_MAX],readback[PLAT_SONG_MAX];
        memset(nor,0xA5,sizeof nor);memset(nor+0x9C000,0xFF,0x40000);
        memset(song_a,0x31,sizeof song_a);memset(song_b,0x73,sizeof song_b);
        for(unsigned slot=0;slot<8;slot++){
            unsigned obj=OBJ_BANK0+slot;
            assert(st_sector(obj,0)==0x9C000u+slot*32768u);
            assert(st_sector(obj,1)+st_span(obj)<=0xDC000u);
            song_a[0]=slot;
            assert(!st_save(obj,song_a,sizeof song_a));
            memcpy(previous,nor,sizeof nor);
            /* Four sector erases, every payload page and the commit header. */
            unsigned operations=4+(sizeof song_b+255)/256+1;
            for(unsigned cut=0;cut<operations;cut++){
                memcpy(nor,previous,sizeof nor);io_fail_after=cut;
                assert(st_save(obj,song_b,sizeof song_b)!=0);io_fail_after=-1;
                assert(st_load(obj,readback,sizeof readback)==sizeof readback);
                assert(!memcmp(readback,song_a,sizeof readback));
                assert(!memcmp(nor,previous,st_sector(obj,0)));
                unsigned end=st_sector(obj,1)+st_span(obj);
                assert(!memcmp(nor+end,previous+end,sizeof nor-end));
            }
            memcpy(nor,previous,sizeof nor);
            assert(!st_save(obj,song_b,sizeof song_b));
            assert(st_load(obj,readback,sizeof readback)==sizeof readback);
            assert(!memcmp(readback,song_b,sizeof readback));
        }
        assert(st_load(0xFFFFFFFFu,readback,sizeof readback)<0);
        assert(st_save(OBJ_BANK0,song_a,PLAT_SONG_MAX+1)<0);
        /* Sequence wrap must select the new zero-sequence commit. */
        st_hdr_t h;int copy=st_current(OBJ_BANK0,&h);h.seq=0xFFFFFFFFu;h.hcrc=st_crc32(&h,sizeof h-4);
        memset(nor+st_sector(OBJ_BANK0,1-copy),0xFF,st_span(OBJ_BANK0));
        memcpy(nor+st_sector(OBJ_BANK0,copy),&h,sizeof h);
        assert(!st_save(OBJ_BANK0,song_a,sizeof song_a));
        assert(st_load(OBJ_BANK0,readback,sizeof readback)==sizeof readback && !memcmp(readback,song_a,sizeof readback));
        puts("eight song slots: every erase/page power cut, boundaries and sequence wrap passed");
    }
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}
