/* SPDX-License-Identifier: GPL-3.0-only */


#pragma once
static int fm1_core_faulted(void);
#include <stdint.h>

#define FM1_C1_CON (*(volatile uint32_t *)0x1EEE004u)
#define FM1_C1_CLK (*(volatile uint32_t *)0x00010008u)
#define FM1_C1_T4 (*(volatile uint32_t *)0x10804u)       
#define FM1_C1_SYNC() __asm__ volatile("csync" ::: "memory")
static inline void fm1_worker_sync(void){FM1_C1_SYNC();}

extern void fm1_c1_entry(void);
extern uint32_t _c1_ustack[], _c1_sstack_top[];
#define FM1_C1_STACK_WORDS (20480u / 4u)
#define FM1_C1_MARK 0x43314D4Bu                  
typedef struct {
    volatile uint32_t job, arg;                 
    void (*volatile fn)(void *);
    volatile uint32_t check;                    
    uint32_t pad0[12];
    volatile uint32_t alive, done;              
    volatile uint32_t bad, bad_job, bad_done, bad_fn;   
    uint32_t pad1[10];
} fm1_c1_mb_t;
static fm1_c1_mb_t fm1_c1_mb __attribute__((aligned(64)));
volatile uint32_t fm1_c1_trace;                 
static uint8_t fm1_c1_on;                       
#define FM1_C1_CHECK(fn, arg, job) ((uint32_t)(uintptr_t)(fn) ^ (arg) ^ (job) * 0x9E3779B1u)
static uint8_t fm1_c1_gave_up;                  
struct fm1_worker_bar { uint32_t magic, retired, stack_bytes; };
struct fm1_worker_bar fm1_worker_bar __attribute__((section(".noinit")));

void fm1_c1_main(void);
void __attribute__((section(".worker_text"), noreturn, used)) fm1_c1_main(void)
{
    
    uint32_t lo = (uint32_t)(uintptr_t)_c1_ustack + 256u, hi = (uint32_t)(uintptr_t)_c1_sstack_top - 1u;
    uint32_t done, last_bad = 0xFFFFFFFFu;
    *(volatile uint32_t *)0x1EEF2D8u = hi;
    *(volatile uint32_t *)0x1EEF2DCu = lo;
    *(volatile uint32_t *)0x1EEF2E0u = hi;
    *(volatile uint32_t *)0x1EEF2E4u = lo;
    *(volatile uint32_t *)0x1EEF2D0u = (*(volatile uint32_t *)0x1EEF2D0u & ~((1u << 2) | (0x1Fu << 16))) | (1u << 3);
    fm1_c1_trace = 0xC1000002;
    done = fm1_c1_mb.done;
    fm1_c1_mb.alive = 1;
    FM1_C1_SYNC();
    for (;;) {
        uint32_t j = fm1_c1_mb.job;
        FM1_C1_SYNC();
        if (j == done)
            continue;
        if (j == done + 1u) {
            void (*fn)(void *) = fm1_c1_mb.fn;
            uint32_t arg = fm1_c1_mb.arg;
            if (fn && fm1_c1_mb.check == FM1_C1_CHECK(fn, arg, j)) {
                fn((void *)(uintptr_t)arg);                        
                done = j;
                FM1_C1_SYNC();
                fm1_c1_mb.done = j;
                FM1_C1_SYNC();
                continue;
            }
            fm1_c1_mb.bad_fn = (uint32_t)(uintptr_t)fn;
        }
        if (j != last_bad) {                    
            last_bad = j;
            fm1_c1_mb.bad_job = j;
            fm1_c1_mb.bad_done = done;
            FM1_C1_SYNC();
            fm1_c1_mb.bad++;
            FM1_C1_SYNC();
        }
    }
}


static inline void fm1_cpu1_halt(void)
{
    FM1_C1_CON |= 0x2u;
    FM1_C1_CON &= ~0x8u;
    FM1_C1_SYNC();
}

static void fm1_cpu1_hold(void)
{
    fm1_cpu1_halt();
    fm1_c1_on = 0;
}

static void fm1_worker_retire(unsigned reason)
{
    fm1_worker_bar.magic=0x57424152u;fm1_worker_bar.retired=1;fm1_worker_bar.stack_bytes=FM1_C1_STACK_WORDS*4;
    fm1_c1_gave_up=reason;fm1_cpu1_hold();
}


static int fm1_cpu1_start(void)
{
    uint32_t saved, t0, i;
    if(fm1_worker_bar.magic==0x57424152u&&fm1_worker_bar.retired==1u&&fm1_worker_bar.stack_bytes==FM1_C1_STACK_WORDS*4)return -1;
    if (fm1_c1_on)
        return 0;
    fm1_cpu1_hold();
    for (i = 0; i < FM1_C1_STACK_WORDS; i++)
        _c1_ustack[i] = FM1_C1_MARK;
    fm1_c1_mb.alive = 0;
    fm1_c1_trace = 0;
    fm1_c1_mb.done = fm1_c1_mb.job;
    fm1_c1_mb.fn = 0;
    FM1_C1_SYNC();
    for (i = 0; i < 32u; i++)                   
        *(volatile uint32_t *)(0x1EEF300u + 4u * i) = 0;
    *(volatile uint32_t *)0x01C7FFF8u = (uint32_t)(uintptr_t)&fm1_c1_entry;
    saved = FM1_C1_CLK;
    FM1_C1_CLK = saved | 0x8u;
    FM1_C1_SYNC();
    FM1_C1_CON |= 0x8u;
    FM1_C1_CON &= ~0x2u;
    t0 = FM1_C1_T4;
    while (!fm1_c1_mb.alive && FM1_C1_T4 - t0 < 24000u * 20u)   
        FM1_C1_SYNC();
    FM1_C1_CLK = saved;
    if (!fm1_c1_mb.alive) {
        fm1_cpu1_hold();
        return -1;
    }
    fm1_c1_on = 1;
    return 0;
}


static inline int fm1_cpu1_run(void (*fn)(void *), uint32_t arg)
{
    uint32_t job;
    if (!fm1_c1_on || fm1_c1_mb.job!=fm1_c1_mb.done)
        return 0;
    job = fm1_c1_mb.job + 1u;
    fm1_c1_mb.fn = fn;
    fm1_c1_mb.arg = arg;
    fm1_c1_mb.check = FM1_C1_CHECK(fn, arg, job);
    FM1_C1_SYNC();
    fm1_c1_mb.job = job;
    FM1_C1_SYNC();
    return 1;
}


static inline int fm1_cpu1_wait(uint32_t budget)
{
    uint32_t t0 = FM1_C1_T4;
    for (;;) {
        FM1_C1_SYNC();
        if (fm1_c1_mb.bad || fm1_core_faulted()) {                    
            fm1_cpu1_hold();
            fm1_c1_gave_up = 1;
            return -1;
        }
        if (fm1_c1_mb.done == fm1_c1_mb.job)
            return 0;
        if (FM1_C1_T4 - t0 > budget) {     
            fm1_cpu1_hold();
            fm1_c1_gave_up = 2;
            return -1;
        }
    }
}
