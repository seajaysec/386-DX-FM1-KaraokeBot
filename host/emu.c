/* SPDX-License-Identifier: GPL-3.0-only */
/* 386DX in the browser: the whole firmware (plat_host.c's FM-1 and the app on it) compiled to WebAssembly
 * and run in an AudioWorklet (web/emu/worklet.js). Asking it for audio runs the device's clock forward a
 * millisecond at a time: the main loop each millisecond, the engine as audio is owed. The page sends the
 * panel and reads the screen, the lights and the saved songs back through these exports, the names the
 * page's worklet expects.
 *   tools/build_web.py  ->  web/emu/app.wasm */
#include "plat_host.c"

#define EXPORT __attribute__((used, visibility("default")))
#define RING 8192u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, owed, booted;
static int32_t blk[512];
static uint32_t blits;

static void step_ms(void)
{
    host_ms();
    owed += 441u;                                   /* frames x 10 a millisecond */
    while (owed >= 2560u) {
        owed -= 2560u;
        engine_render(blk, 256);
        for (unsigned k = 0; k < 256; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)blk[2 * k] / 8388608.0f;
            ring[i + 1] = (float)blk[2 * k + 1] / 8388608.0f;
        }
    }
}

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
EXPORT void web_render(uint32_t n)
{
    if (n > 1024u)
        n = 1024u;
    while (booted && ring_w - ring_r < n)
        step_ms();
    for (uint32_t k = 0; k < n; k++) {
        if (ring_w == ring_r) {
            out_l[k] = out_r[k] = 0.0f;
            continue;
        }
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}
EXPORT void web_boot(void) { booted = host_boot() == 0; }
EXPORT void web_buttons(uint32_t m) { buttons = m; }
EXPORT void web_keys(uint32_t m) { keys = m; }
EXPORT void web_key(uint32_t k, uint32_t vel, uint32_t press)
{
    (void)press;
    if (k < NKEYS)
        keys = vel ? keys | 1u << k : keys & ~(1u << k);
}
EXPORT void web_enc(int role, int32_t n)
{
    if (role >= 0 && role < NE)
        enc[role] += n;
}
EXPORT void web_master(uint32_t v) { (void)v; }
EXPORT uint32_t web_lit_buttons(void) { return led_b; }
EXPORT uint32_t web_lit_keys(void) { return led_k; }
EXPORT uint32_t web_dim_buttons(void) { return 0; }
EXPORT uint32_t web_dim_keys(void) { return 0; }
/* the screen: a new frame whenever a band was sent */
static uint16_t fb[240 * 240];
EXPORT uint16_t *web_fb(void)                       /* as the panel receives it: RGB565, high byte first */
{
    for (unsigned i = 0; i < 240u * 240u; i++)
        fb[i] = (uint16_t)(screen[i] >> 8 | screen[i] << 8);
    return fb;
}
EXPORT uint32_t web_typing(void) { return ui.words ? 1u : 0u; }   /* the FM-1 is asking for words or a name */
EXPORT uint32_t web_blits(void) { return blit_count; }
EXPORT uint32_t web_now(void) { return now; }
EXPORT uint32_t web_cpu(void) { return 0; }
/* the screen reader: not spoken by 386DX yet */
static uint8_t said[4], sx[64];
EXPORT uint8_t *web_said_buf(void) { return said; }
EXPORT uint32_t web_said_len(void) { return 0; }
EXPORT void web_said_clear(void) {}
EXPORT uint8_t *web_sysex_buf(void) { return sx; }
EXPORT void web_sysex(uint32_t n) { (void)n; }
/* the saved songs: the page keeps them (localStorage) and gives them back before switching on */
EXPORT uint8_t *web_store(uint32_t obj) { return obj < 32 ? store[obj] : 0; }
EXPORT int web_store_len(uint32_t obj) { return obj < 32 && store_len[obj] > 0 ? store_len[obj] : -1; }
EXPORT void web_store_set_len(uint32_t obj, int n)
{
    if (obj < 32)
        store_len[obj] = n < 0 ? 0 : n > (int)sizeof store[0] ? (int)sizeof store[0] : n;
}
EXPORT uint32_t web_store_writes(void) { return saves; }
EXPORT uint32_t web_nobj(void) { return 32; }
EXPORT uint32_t web_store_max(void) { return sizeof store[0]; }
