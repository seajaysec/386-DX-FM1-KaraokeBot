/* SPDX-License-Identifier: GPL-3.0-only */
/* The FM-1 on a computer: plat.h's functions over memory (the screen a framebuffer, the flash a table of
 * objects, the panel whatever the caller holds), and the firmware's UI, song, sung clips and engine on
 * top, run a millisecond at a time. The simulator (sim.c) drives it from scripts; the emulator (emu.c)
 * from a page in the browser. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define X0X_HOST 1

static uint16_t screen[240 * 240];
static uint32_t blit_count;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    blit_count++;
    for (uint32_t j = 0; j < h; j++)
        for (uint32_t i = 0; i < w; i++)
            if (x + i < 240 && y + j < 240) {
                uint16_t v = p[j * w + i];
                screen[(y + j) * 240 + x + i] = (uint16_t)((v >> 8) | (v << 8));
            }
}
#include "../firmware/src/gfx.c"
#include "../firmware/src/app/plat.h"

/* ---- the platform ---- */
static uint32_t now, buttons, keys;
void host_busy(unsigned ms);
static int enc[NE];
static uint32_t led_b, led_k, red;
uint32_t plat_ms(void) { return now; }
/* as on the FM-1: a press that came and went between two looks still counts once, and the main loop is
 * away now and then (a sung clip being prepared, a song being written), so it does not look every ms */
static uint32_t latch_b, latch_k, last_b, last_k, stall;
void host_busy(unsigned ms) { if (!getenv("SIM_NOSTALL")) stall += ms; }
void host_wait_idle(void (*run)(uint32_t))
{
    while (stall)
        run(10);
    run(60);                                      /* and one more look at the input */
}
void host_settled(void) { stall = 0; }               /* power-on is done: the setup's writes took no time */
uint32_t plat_buttons(void)
{
    uint32_t b = buttons | latch_b;
    latch_b = 0;
    return b;
}
uint32_t plat_keys(void)
{
    uint32_t k = keys | latch_k;
    latch_k = 0;
    return k;
}
int32_t plat_enc(int role)
{
    int v = enc[role];
    enc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return 4096; }
void plat_leds(uint32_t b, uint32_t k) { led_b = b; led_k = k; }
uint32_t host_key_leds(void) { return led_k; }
void plat_glow(int on) { (void)on; }
void plat_play_red(int r) { red = (uint32_t)r; }
int plat_midi_in(uint32_t *p) { (void)p; return 0; }
void plat_midi_out(uint32_t p) { (void)p; }
uint32_t plat_cpu_pct(void) { return 0; }
uint32_t plat_cpu_peak_pct(void) { return 0; }
uint32_t plat_xruns(void) { return 0; }
/* the flash: objects in memory, kept in OUTDIR/store.bin between runs when asked */
static uint8_t store[32][16384];
static int store_len[32];
static unsigned saves;
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj >= 32 || store_len[obj] <= 0)
        return -1;
    uint32_t n = (uint32_t)store_len[obj] < max ? (uint32_t)store_len[obj] : max;
    memcpy(dst, store[obj], n);
    return (int)n;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= 32 || len > sizeof store[0])
        return -1;
    memcpy(store[obj], src, len);
    store_len[obj] = (int)len;
    saves++;
    host_busy(900);                               /* a flash write: erase and program take about a second */
    return 0;
}
int plat_store_read(uint32_t obj, uint32_t offset, void *dst, uint32_t n)
{
    if (obj >= 32 || store_len[obj] <= 0)
        return -1;
    if (offset < (uint32_t)store_len[obj])
        memcpy(dst, store[obj] + offset, offset + n > (uint32_t)store_len[obj] ? store_len[obj] - offset : n);
    return store_len[obj];
}
int host_restarts;
void plat_restart(void) { host_restarts++; }
int plat_store_forget(uint32_t obj)
{
    if (obj >= 32)
        return -1;
    store_len[obj] = 0;
    return 0;
}

#include "../firmware/src/app/ui.c"
void dx_speech_idle(void) {}

/* switched on: the speech engine, the last song saved (else a new one), the screen */
static int host_boot(void)
{
    if (dx_speech_init() != 0)
        return -1;
    song_blank(&song);
    engine_init();
    int slot = ui_boot_song(0);
    ui_init();
    ui_boot_slot(slot);
    return 0;
}

/* one millisecond of the main loop: input, the service (MIDI, autosave, the sung clips), the screen */
static void host_ms(void)
{
    now++;
    latch_b |= buttons & ~last_b;
    latch_k |= keys & ~last_k;
    last_b = buttons;
    last_k = keys;
    if (stall) {                                  /* the loop is busy: time and the audio go on, input waits */
        stall--;
        return;
    }
    ui_service();
    ui_frame();
}
