/* SPDX-License-Identifier: GPL-3.0-only */
/* X0X platform on the FM-1: plat.h over Felucca's HAL, panel map, USB rings and storage.
 * Part of the unity build (x0x.c), after the HAL, usb.c, storage.c and panel.c. */

uint32_t plat_ms(void) { return fm1_ms; }

uint32_t plat_buttons(void)
{
    /* a press that came and went since the last look still counts once (the main loop can be busy for
     * longer than a quick tap: preparing a sung syllable takes a while) */
    uint32_t raw = fm1_in.buttons | fm1_input_edges(0), out = 0, i;
    for (i = 0; i < NB; i++)
        if (raw & (1u << panel.btn[i]))
            out |= 1u << i;
    return out;
}

uint32_t plat_keys(void) { return (fm1_in.notes | fm1_input_note_edges()) & ((1u << NKEYS) - 1u); }   /* (the same: quick taps count) */

int32_t plat_enc(int role)
{
    if (role < 0 || role >= NE)
        return 0;
    return fm1_enc_take(panel.enc[role]) * panel.dir[role];
}

static volatile uint32_t master_q12 = 2048;
uint32_t plat_master(void) { return master_q12; }

/* LEDs: the picture is built off-line and copied one byte per column (Felucca: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker) */
static uint8_t led_pos[FM1_NKEY];
static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < FM1_NKEY; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
/* Glow: every button not lit is lit dim, on one scan frame in GLOW_DIV (the HAL's fm1_led_dim) */
#define GLOW_DIV 4u
static uint8_t glow_on = 1;
void plat_glow(int on) { glow_on = (uint8_t)(on != 0); }
/* PLAY has two LEDs: its red at the button's own position, a green at the keyless matrix position
 * column 8, row PA5 (found on the device). Green, unless recording. */
static uint8_t play_red;
void plat_play_red(int red) { play_red = (uint8_t)(red != 0); }
void plat_leds(uint32_t buttons, uint32_t keys)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t i, c;
    for (i = 0; i < NB + NKEYS; i++) {
        uint32_t id = i < NB ? panel.btn[i] : 14u + (i - NB);
        int on = i < NB ? (buttons >> i) & 1u : (keys >> (i - NB)) & 1u;
        uint8_t q = led_pos[id];
        if (i == B_PLAY && !play_red)            /* PLAY's green LED instead (keyless position col 8, row PA5) */
            q = (uint8_t)((8u << 3) | 1u);
        if (q == 0xFF)
            continue;
        if (on)
            nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
        else if (i < NB && glow_on)
            dl[q >> 3] |= (uint8_t)(1u << (q & 7u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
    }
    fm1_led_dim_div = glow_on ? GLOW_DIV : 0u;
}

/* MIDI: Felucca's rings carry 4-byte USB-MIDI event packets, byte 0 = cable / CIN */
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = midi_in_q[mi_r % MQ];
    mi_r++;
    return 1;
}
void plat_midi_out(uint32_t pkt) { midi_out_event(pkt); }

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? st_save(obj, src, len) : -9; }
/* part of an object as stored (its newest good copy): the object's length, or -1 */
int plat_store_read(uint32_t obj, uint32_t offset, void *dst, uint32_t n)
{
    st_hdr_t h;
    if (!flash_ok || !st_valid_obj(obj))
        return -1;
    int copy = st_current(obj, &h);
    if (copy < 0)
        return -1;
    if (offset < h.len) {
        if (n > h.len - offset)
            n = h.len - offset;
        if (st_read(st_sector(obj, (uint32_t)copy) + ST_PAYLOAD_OFF + offset, dst, n))
            return -1;
    }
    return (int)h.len;
}
int plat_store_forget(uint32_t obj)
{
    if (!flash_ok || !st_valid_obj(obj))
        return -1;
    return st_erase(st_sector(obj, 0)) | st_erase(st_sector(obj, 1));   /* the headers: the commit records */
}

static volatile uint32_t audio_cpu_pct, audio_xruns, audio_peak_pct;
uint32_t plat_cpu_pct(void) { return audio_cpu_pct; }
uint32_t plat_xruns(void) { return audio_xruns; }

static int perf_cyc_ok, perf_stalls_on;
void plat_perf_init(void) { perf_cyc_ok = fm1_perf_cycles_run(); }
uint32_t plat_cycles(void) { return perf_cyc_ok ? fm1_perf_cycles() : fm1_ticks(); }
int plat_cycles_cpu(void) { return perf_cyc_ok; }
uint32_t plat_cycles_hz(void) { return 24000000u; }   /* the timer; the CPU's rate is measured */
uint32_t plat_ticks24(void) { return fm1_ticks(); }
/* the interrupts' stack (fm1_guard.h): filled with a mark before any interrupt runs (irq_stack_mark,
 * fm1_cstart), so the deepest point reached is the lowest word no longer marked */
#define IRQ_STACK_MARK 0x49525153u                  /* "IRQS" */
static void irq_stack_mark(void)
{
    uint32_t *p = (uint32_t *)(void *)_sstack_lo, *top = (uint32_t *)(void *)_sstack_top - 16;
    uint32_t here = (uint32_t)(uintptr_t)&p;        /* never mark below a live frame of our own */
    if (here > (uint32_t)(uintptr_t)p && here < (uint32_t)(uintptr_t)top)
        top = (uint32_t *)(uintptr_t)((here - 256u) & ~3u);
    for (; p < top; p++)
        *p = IRQ_STACK_MARK;
}
uint32_t plat_irq_stack_size(void) { return (uint32_t)(_sstack_top - _sstack_lo); }
uint32_t plat_irq_stack_used(void)
{
    const uint32_t *p = (const uint32_t *)(const void *)_sstack_lo, *top = (const uint32_t *)(const void *)_sstack_top;
    while (p < top && *p == IRQ_STACK_MARK)
        p++;
    return (uint32_t)((const char *)top - (const char *)p);
}

uint32_t plat_cpu_peak_pct(void)
{
    uint32_t p = audio_peak_pct;
    audio_peak_pct = 0;
    return p;
}
int plat_stalls(uint32_t s[3])
{
    if (!perf_stalls_on)
        return -1;
    fm1_perf_stalls(s);
    return 0;
}
void plat_stalls_enable(int on)
{
    perf_stalls_on = on;
    fm1_perf_stalls_enable(on);
}

void plat_uac_state(uint32_t u[8])
{
    uint32_t f=irq_save();
    u[0]=uac.alt;u[1]=uac.flowing;u[2]=uac.go;u[3]=uac.pkts;
    u[4]=uac.underruns;u[5]=uac.missed;
    u[6]=uac.sample_peak;u[7]=uac.render_peak;
    uac.sample_peak=uac.render_peak=0;irq_restore(f);
}
