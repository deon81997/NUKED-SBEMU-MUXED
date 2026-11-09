/*
 * sbcore.c - Sound Blaster 16 / OPL3 / MPU-401 virtualization core of
 * SC55W.VXD (ring 0, Open Watcom C, flat model, no C runtime).
 *
 * The VxD shell (sc55w.asm) traps the I/O ports, owns the IRQ and the DMA
 * channels and calls into this file. This file keeps the virtual hardware
 * state, answers port reads at once, and hands audio to the ring-3 helper
 * (SC55W.EXE) through the shared block:
 *   - OPL and MIDI writes become time-stamped events,
 *   - DMA audio is read out of the DOS program's buffer at real-time pace
 *     (driven by a 1 ms timer) and converted to stereo 16-bit frames,
 *   - the virtual IRQ is raised at the end of each DSP block.
 *
 * DSP behaviour follows SBEMU's sbemu.c (crazii et al., GPL-2) and the
 * Creative "Sound Blaster Series Hardware Programming Guide".
 * ADPCM tables are from DOSBox's sblaster.cpp (GPL-2).
 */
#include "../common/sc55wif.h"

/* ---- services implemented in sc55w.asm (cdecl) ---- */
extern u32  __cdecl vxd_time_us(void);
extern u32  __cdecl vxd_muldiv(u32 a, u32 b, u32 c);
extern void __cdecl vxd_raise_irq(u32 vm);
extern void __cdecl vxd_lower_irq(u32 vm);
extern int  __cdecl vxd_dma_get(int idx, u32 vm, u32 *addr, u32 *count, u32 *mode);
extern void __cdecl vxd_dma_set(int idx, u32 vm, u32 addr, u32 count, u32 mode);
extern void __cdecl vxd_timer_arm(u32 ms);
extern int  __cdecl vxd_install_io(u32 port);
extern void __cdecl vxd_remove_io(u32 port);
extern int  __cdecl vxd_virt_irq(u32 irq);
extern void __cdecl vxd_unvirt_irq(void);
extern int  __cdecl vxd_virt_dma(int idx, u32 chan);
extern void __cdecl vxd_unvirt_dma(int idx);
extern u32  __cdecl vxd_map_block(u32 addr, u32 size);
extern void __cdecl vxd_unmap_block(u32 alias, u32 size);
extern void __cdecl vxd_min_period(int on);
extern u32  __cdecl vxd_sys_vm(void);
extern u32  __cdecl vxd_port_in(u32 port);
extern void __cdecl vxd_port_out(u32 port, u32 val);
extern int  __cdecl vxd_hook_int15(int on);

#ifndef BUILD_ID
#define BUILD_ID 0
#endif
#define DMA_MASKED    0x01
#define DMA_AUTOINIT  0x10

static SC55W_SHARED *sh;
static u32 sh_size;
static u32 owner_vm;            /* VM that last programmed the DSP */
static SC55W_CONFIG cfg;
static u32 trapped;

static void trace(u32 code, u32 a, u32 b)
{
    u32 h;
    if (!sh) return;
    h = sh->tr_head & (TRACE_SIZE - 1);
    sh->tr[h][0] = vxd_time_us(); sh->tr[h][1] = code; sh->tr[h][2] = a; sh->tr[h][3] = b;
    sh->tr_head++;
}

static void mset(void *d, int v, u32 n) { u8 *p = (u8 *)d; while (n--) *p++ = (u8)v; }
static void mcpy(void *d, const void *s, u32 n) { u8 *a = (u8 *)d; const u8 *b = (const u8 *)s; while (n--) *a++ = *b++; }

/* ====================================================================== */
/* events to the helper                                                   */
/* ====================================================================== */
static void ev_put(u8 type, u8 a, u8 b, u8 c)
{
    u32 h;
    SC55W_EVENT *e;
    if (!sh) return;
    h = sh->ev_head;
    if (h - sh->ev_tail >= EVRING_SIZE) { sh->ev_overflow++; return; }
    e = &sh->ev[h & (EVRING_SIZE - 1)];
    e->time = vxd_time_us();
    e->type = type; e->a = a; e->b = b; e->c = c;
    sh->ev_head = h + 1;
}

/* ====================================================================== */
/* MPU-401, UART mode                                                     */
/* ====================================================================== */
static u8  mpu_uart;
static u8  mpu_q[8];
static int mpu_qn;

static void mpu_queue(u8 v) { if (mpu_qn < 8) mpu_q[mpu_qn++] = v; }

static u8 mpu_read(u32 port)
{
    if (port == (u32)cfg.mpu_port + 1)
        return (u8)(mpu_qn ? 0x3F : 0xBF);   /* bit6 0 = ready, bit7 0 = data waiting */
    if (mpu_qn) {
        u8 v = mpu_q[0];
        int i;
        for (i = 1; i < mpu_qn; i++) mpu_q[i - 1] = mpu_q[i];
        mpu_qn--;
        return v;
    }
    return 0xFE;
}

static void mpu_write(u32 port, u8 v)
{
    if (port == (u32)cfg.mpu_port + 1) {
        if (v == 0xFF) { mpu_uart = 0; mpu_qn = 0; mpu_queue(0xFE); }
        else if (v == 0x3F) { mpu_uart = 1; mpu_queue(0xFE); }
        else if (!mpu_uart) mpu_queue(0xFE);
        return;
    }
    ev_put(EV_MIDI, v, 0, 0);
}

/* ====================================================================== */
/* OPL: writes go to the helper; timers are emulated here                 */
/* ====================================================================== */
static u8  opl_index[2];
static u8  opl_t1val, opl_t2val, opl_ctrl, opl_status;
static u32 opl_t1start, opl_t2start;

static u8 opl_read_status(void)
{
    u32 now = vxd_time_us();
    if ((opl_ctrl & 0x01) && !(opl_ctrl & 0x40) &&
        now - opl_t1start >= 80UL * (256 - opl_t1val)) opl_status |= 0x40;
    if ((opl_ctrl & 0x02) && !(opl_ctrl & 0x20) &&
        now - opl_t2start >= 320UL * (256 - opl_t2val)) opl_status |= 0x20;
    if (opl_status & 0x60) opl_status |= 0x80;
    return opl_status;                        /* low bits 0: an OPL3 */
}

static void opl_write_data(int bank, u8 v)
{
    u8 r = opl_index[bank];
    if (!bank && r >= 2 && r <= 4) {
        u32 now = vxd_time_us();
        if (r == 2) { opl_t1val = v; return; }
        if (r == 3) { opl_t2val = v; return; }
        if (v & 0x80) { opl_status = 0; return; }
        if ((v & 1) && !(opl_ctrl & 1)) opl_t1start = now;
        if ((v & 2) && !(opl_ctrl & 2)) opl_t2start = now;
        opl_ctrl = v;
        if (v & 0x40) opl_status &= ~0x40;
        if (v & 0x20) opl_status &= ~0x20;
        if (!(opl_status & 0x60)) opl_status = 0;
        return;
    }
    ev_put(EV_OPL, r, v, (u8)bank);
}

/* ====================================================================== */
/* DSP                                                                    */
/* ====================================================================== */
/* index = /T type as in SBEMU: 1-3 SB, 4-5 SB Pro, 6 SB16 */
static const u16 dsp_versions[7] = { 0x0405, 0x0105, 0x0201, 0x0201, 0x0300, 0x0302, 0x0405 };
static const char copyright[] = "COPYRIGHT (C) CREATIVE TECHNOLOGY LTD, 1992.";

static struct {
    u16 version;
    int cmd;            /* command collecting parameters, -1 if idle */
    int need;           /* parameter bytes still needed */
    u8  p[4];
    int np;
    u8  out[64];        /* reply bytes for port 2xAh */
    int outn, outp;
    int reset_state;
    u8  speaker;
    u8  test_reg;
    u8  dmaid_a, dmaid_x;
    u8  time_const;
    int use_tc;
    u32 rate;           /* Hz, per channel */
    u32 blocksize;      /* command 48h, bytes */
    u8  highspeed;
    u8  ws;
    u8  midi_uart;
    u8  adpcm_ref;
    int adpcm_step;
    u8  adpcm_useref;
} dsp;

static struct {
    int active, paused;
    int autoinit;
    int bits;           /* 8, 16, 2/3/4 = ADPCM, 0 = silence (80h) */
    int chans, sign;
    int idx;            /* 0 = 8-bit DMA channel, 1 = 16-bit channel */
    int record;
    u32 block;          /* bytes per DSP block */
    u32 done;           /* bytes done in this block */
    u32 bps;            /* bytes per second */
    u32 start_us;
    u32 consumed;       /* bytes since start_us */
    u8  carry[4];
    int carryn;
} xf;

/* our own view of each DMA channel. VDMAD recomputes the count from the
 * (never programmed) physical 8237 whenever the program reads the count or
 * address registers, so we keep the real position here and put it back
 * from the read-notify callback. */
static struct {
    int valid;
    u32 addr, count, mode;          /* current position (high linear, bytes) */
    u32 base_addr, base_count;      /* for 8237 auto-init */
} dmach[2];

/* VDMAD stores count-1; a count of 0 would leave FFFFFFFFh in its 32-bit
 * field and the program's next 16-bit count write would not clear the top.
 * At terminal count a real 8237 reads FFFFh, i.e. 10000h transfers. */
static u32 vdmad_count(int idx, u32 c)
{
    if (c) return c;
    return idx ? 0x20000UL : 0x10000UL;
}

/* clamp a count read back from VDMAD to what the 8237 can hold */
static u32 clamp_count(int idx, u32 c)
{
    if (idx) return ((((c >> 1) - 1) & 0xFFFFUL) + 1) << 1;
    return ((c - 1) & 0xFFFFUL) + 1;
}

static void dma_push(int idx)
{
    vxd_dma_set(idx, owner_vm, dmach[idx].addr, vdmad_count(idx, dmach[idx].count), dmach[idx].mode);
}

static u8  irq_status;      /* mixer 82h bit0 = 8-bit, bit1 = 16-bit */
static u8  irq_line;

static void lower_line(void)
{
    if (irq_line) { irq_line = 0; vxd_lower_irq(owner_vm); }
}

static void raise_irq(int sixteen)
{
    irq_status |= (u8)(sixteen ? 2 : 1);
    if (sh) sh->irq_count++;
    if (!(trapped & ST_OK_IRQ)) return;
    irq_line = 1;
    vxd_raise_irq(owner_vm);
}

static void ack_irq(int sixteen)
{
    irq_status &= (u8)(sixteen ? ~2 : ~1);
    if (!irq_status) lower_line();
}

static void dsp_reply(u8 v) { if (dsp.outn < (int)sizeof(dsp.out)) dsp.out[dsp.outn++] = v; }

/* ---- mixer ---- */
static u8 mix_index;
static u8 mixer_shadow[256];
static u8 *mixer(void) { return sh ? (u8 *)sh->mixer : mixer_shadow; }
static int stereo_bit(void) { return (mixer()[0x0E] & 2) != 0; }

static void update_tc_rate(void)
{
    u32 r, ch;
    u8 tc = dsp.time_const;
    if (!dsp.use_tc) return;
    ch = (dsp.version >= 0x300 && dsp.version < 0x400 && stereo_bit()) ? 2 : 1;
    r = 256000000UL / (65536UL - ((u32)tc << 8)) / ch;
    if (cfg.fixtc) {
        if (tc >= 0xA5 - 3 && tc <= 0xA5 + 3) r = 11025 / ch;
        else if (tc >= 0xD2 - 3 && tc <= 0xD2 + 3) r = 22050 / ch;
        else if (tc >= 0xE9 - 3 && tc <= 0xE9 + 3) r = 44100 / ch;
    }
    dsp.rate = r;
}

static void mixer_reset(void)
{
    static const u8 irqmap[4] = { 2, 5, 7, 10 };
    u8 *m = mixer();
    int i;
    mset(m, 0, 256);
    for (i = 0; i < 4; i++) if (irqmap[i] == cfg.irq) m[0x80] = (u8)(1 << i);
    m[0x81] = (u8)((1 << cfg.dma8) | (cfg.dma16 ? (1 << cfg.dma16) : 0));
    m[0x02] = 0x0E; m[0x06] = 0x0E; m[0x08] = 0x0E; m[0x0A] = 0x06;
    m[0x0E] = 0x11;
    if (dsp.version < 0x400) {
        m[0x04] = 0xEE; m[0x22] = 0xEE; m[0x26] = 0xEE; m[0x28] = 0xEE;
    } else {
        m[0x04] = 0xFF; m[0x22] = 0xFF; m[0x26] = 0xFF; m[0x28] = 0xFF;
        for (i = 0x30; i <= 0x37; i++) m[i] = 0xF8;
        m[0x3C] = 0x1F; m[0x3D] = 0x15; m[0x3E] = 0x0B;
        m[0x44] = m[0x45] = m[0x46] = m[0x47] = 0x80;
    }
}

static void mixer_write(u8 v)
{
    u8 *m = mixer();
    u8 i = mix_index, n;
    if (i == 0x00) { mixer_reset(); return; }
    if (i >= 0x80 && i <= 0x82) return;           /* resources are fixed by the config */
    m[i] = v;
    if (dsp.version >= 0x400) {
        n = (u8)((v >> 4) & 0x0F);
        switch (i) {
        case 0x30: m[0x22] = (u8)((m[0x22] & 0x0F) | (n << 4)); break;
        case 0x31: m[0x22] = (u8)((m[0x22] & 0xF0) | n); break;
        case 0x32: m[0x04] = (u8)((m[0x04] & 0x0F) | (n << 4)); break;
        case 0x33: m[0x04] = (u8)((m[0x04] & 0xF0) | n); break;
        case 0x34: m[0x26] = (u8)((m[0x26] & 0x0F) | (n << 4)); break;
        case 0x35: m[0x26] = (u8)((m[0x26] & 0xF0) | n); break;
        case 0x36: m[0x28] = (u8)((m[0x28] & 0x0F) | (n << 4)); break;
        case 0x37: m[0x28] = (u8)((m[0x28] & 0xF0) | n); break;
        case 0x22: m[0x30] = (u8)(v & 0xF0); m[0x31] = (u8)(v << 4); break;
        case 0x04: m[0x32] = (u8)(v & 0xF0); m[0x33] = (u8)(v << 4); break;
        case 0x26: m[0x34] = (u8)(v & 0xF0); m[0x35] = (u8)(v << 4); break;
        case 0x28: m[0x36] = (u8)(v & 0xF0); m[0x37] = (u8)(v << 4); break;
        }
    }
    if (i == 0x0E) update_tc_rate();
}

static u8 mixer_read(void)
{
    if (mix_index == 0x82) return (u8)(irq_status | (dsp.version >= 0x400 ? 0x20 : 0));
    return mixer()[mix_index];
}

/* ---- PCM out ---- */
static u32 pcm_rate_sent;

static void pcm_rate(u32 rate)
{
    u32 h;
    if (!sh || rate == pcm_rate_sent) return;
    h = sh->fmt_head;
    if (h - sh->fmt_tail >= PCMFMT_SIZE) return;
    sh->fmt[h & (PCMFMT_SIZE - 1)].frame = sh->pcm_head;
    sh->fmt[h & (PCMFMT_SIZE - 1)].rate = rate;
    sh->fmt_head = h + 1;
    pcm_rate_sent = rate;
}

static void pcm_frame(s16 l, s16 r)
{
    u32 h, i;
    if (!sh) return;
    h = sh->pcm_head;
    if (h - sh->pcm_tail >= PCMRING_SIZE) { sh->pcm_overflow++; return; }
    i = (h & (PCMRING_SIZE - 1)) * 2;
    sh->pcm[i] = l; sh->pcm[i + 1] = r;
    sh->pcm_head = h + 1;
}

/* ---- Creative ADPCM ---- */
static u8 adpcm_clamp(int r) { return (u8)(r > 255 ? 255 : r < 0 ? 0 : r); }

static u8 adpcm4(u8 s)
{
    static const signed char sm[64] = {
        0,1,2,3,4,5,6,7,0,-1,-2,-3,-4,-5,-6,-7, 1,3,5,7,9,11,13,15,-1,-3,-5,-7,-9,-11,-13,-15,
        2,6,10,14,18,22,26,30,-2,-6,-10,-14,-18,-22,-26,-30, 4,12,20,28,36,44,52,60,-4,-12,-20,-28,-36,-44,-52,-60 };
    static const u8 am[64] = {
        0,0,0,0,0,16,16,16, 0,0,0,0,0,16,16,16, 240,0,0,0,0,16,16,16, 240,0,0,0,0,16,16,16,
        240,0,0,0,0,16,16,16, 240,0,0,0,0,16,16,16, 240,0,0,0,0,0,0,0, 240,0,0,0,0,0,0,0 };
    int i = s + dsp.adpcm_step;
    if (i < 0) i = 0;
    if (i > 63) i = 63;
    dsp.adpcm_ref = adpcm_clamp(dsp.adpcm_ref + sm[i]);
    dsp.adpcm_step = (dsp.adpcm_step + am[i]) & 0xFF;
    return dsp.adpcm_ref;
}
static u8 adpcm3(u8 s)
{
    static const signed char sm[40] = {
        0,1,2,3,0,-1,-2,-3, 1,3,5,7,-1,-3,-5,-7, 2,6,10,14,-2,-6,-10,-14, 4,12,20,28,-4,-12,-20,-28, 5,15,25,35,-5,-15,-25,-35 };
    static const u8 am[40] = {
        0,0,0,8,0,0,0,8, 248,0,0,8,248,0,0,8, 248,0,0,8,248,0,0,8, 248,0,0,8,248,0,0,8, 248,0,0,0,248,0,0,0 };
    int i = s + dsp.adpcm_step;
    if (i < 0) i = 0;
    if (i > 39) i = 39;
    dsp.adpcm_ref = adpcm_clamp(dsp.adpcm_ref + sm[i]);
    dsp.adpcm_step = (dsp.adpcm_step + am[i]) & 0xFF;
    return dsp.adpcm_ref;
}
static u8 adpcm2(u8 s)
{
    static const signed char sm[24] = { 0,1,0,-1, 1,3,-1,-3, 2,6,-2,-6, 4,12,-4,-12, 8,24,-8,-24, 6,48,-16,-48 };
    static const u8 am[24] = { 0,4,0,4, 252,4,252,4, 252,4,252,4, 252,4,252,4, 252,4,252,4, 252,0,252,0 };
    int i = s + dsp.adpcm_step;
    if (i < 0) i = 0;
    if (i > 23) i = 23;
    dsp.adpcm_ref = adpcm_clamp(dsp.adpcm_ref + sm[i]);
    dsp.adpcm_step = (dsp.adpcm_step + am[i]) & 0xFF;
    return dsp.adpcm_ref;
}

static s16 u8s(u8 v) { return (s16)(((int)v - 128) << 8); }
static s16 s8s(u8 v) { return (s16)((signed char)v * 256); }
static void mono(s16 v) { pcm_frame(v, v); }

static void convert(const u8 *p, u32 n)
{
    u32 i;
    int mute = !dsp.speaker && dsp.version < 0x400;   /* speaker off mutes the DAC on SB/SB Pro */
    if (xf.bits == 8 || xf.bits == 16) {
        int fb = (xf.bits == 16 ? 2 : 1) * xf.chans;
        for (i = 0; i < n; i++) {
            xf.carry[xf.carryn++] = p[i];
            if (xf.carryn == fb) {
                s16 l, r;
                xf.carryn = 0;
                if (xf.bits == 8) {
                    l = xf.sign ? s8s(xf.carry[0]) : u8s(xf.carry[0]);
                    r = xf.chans == 2 ? (xf.sign ? s8s(xf.carry[1]) : u8s(xf.carry[1])) : l;
                } else {
                    l = (s16)(xf.carry[0] | (xf.carry[1] << 8));
                    r = xf.chans == 2 ? (s16)(xf.carry[2] | (xf.carry[3] << 8)) : l;
                    if (!xf.sign) { l = (s16)(l ^ 0x8000); r = (s16)(r ^ 0x8000); }
                }
                if (mute) l = r = 0;
                pcm_frame(l, r);
            }
        }
        return;
    }
    i = 0;
    if (dsp.adpcm_useref && n) {
        dsp.adpcm_useref = 0; dsp.adpcm_ref = p[0]; dsp.adpcm_step = 0;
        mono(mute ? 0 : u8s(p[0]));
        i = 1;
    }
    for (; i < n; i++) {
        u8 b = p[i];
        if (xf.bits == 4) {
            s16 a = u8s(adpcm4((u8)(b >> 4))), c = u8s(adpcm4((u8)(b & 15)));
            mono(mute ? 0 : a); mono(mute ? 0 : c);
        } else if (xf.bits == 3) {
            s16 a = u8s(adpcm3((u8)((b >> 5) & 7)));
            s16 c = u8s(adpcm3((u8)((b >> 2) & 7)));
            s16 d = u8s(adpcm3((u8)((b & 3) << 1)));
            mono(mute ? 0 : a); mono(mute ? 0 : c); mono(mute ? 0 : d);
        } else {
            s16 a = u8s(adpcm2((u8)((b >> 6) & 3)));
            s16 c = u8s(adpcm2((u8)((b >> 4) & 3)));
            s16 d = u8s(adpcm2((u8)((b >> 2) & 3)));
            s16 e = u8s(adpcm2((u8)(b & 3)));
            mono(mute ? 0 : a); mono(mute ? 0 : c); mono(mute ? 0 : d); mono(mute ? 0 : e);
        }
    }
}

/* ====================================================================== */
/* transfer engine                                                        */
/* ====================================================================== */
static u32 xf_bytes_per_sec(void)
{
    u32 r = dsp.rate ? dsp.rate : 22050;
    switch (xf.bits) {
    case 16: return r * 2 * xf.chans;
    case 8:  return r * xf.chans;
    case 4:  return (r + 1) / 2;
    case 3:  return (r + 2) / 3;
    case 2:  return (r + 3) / 4;
    default: return r;
    }
}

static void xf_rebase(u32 now)
{
    xf.start_us = now - vxd_muldiv(xf.consumed, 1000000UL, xf.bps);
}

static void xf_start(int bits, int chans, int sign, int autoinit, u32 bytes, int record)
{
    xf.bits = bits; xf.chans = chans; xf.sign = sign; xf.autoinit = autoinit;
    xf.record = record;
    xf.idx = (bits == 16 && cfg.dma16) ? 1 : 0;
    xf.block = bytes ? bytes : 1;
    xf.done = 0;
    xf.consumed = 0;
    xf.carryn = 0;
    xf.paused = 0;
    xf.bps = xf_bytes_per_sec();
    if (!xf.bps) xf.bps = 1;
    xf.start_us = vxd_time_us();
    xf.active = 1;
    trace(1, ((u32)bits << 16) | ((u32)chans << 8) | (u32)autoinit, bytes);
    trace(2, dsp.rate, xf.bps);
    if (bits && !record) pcm_rate(dsp.rate ? dsp.rate : 22050);
    if (sh) {
        sh->dma_active = 1;
        sh->debug[0] = (u32)bits; sh->debug[1] = (u32)chans; sh->debug[2] = dsp.rate;
        sh->debug[3] = bytes; sh->debug[4] = (u32)autoinit;
    }
    vxd_timer_arm(1);
}

static void xf_stop(void)
{
    xf.active = 0;
    if (!xf.autoinit) dsp.highspeed = 0;
    if (sh) sh->dma_active = 0;
}

/* move up to n bytes through the DOS program's DMA buffer; returns bytes moved */
static u32 xf_pull(u32 n)
{
    u32 got = 0;
    int idx = xf.idx;
    if (!(trapped & (idx ? ST_OK_DMA16 : ST_OK_DMA8)) || !dmach[idx].valid) {
        if (xf.done == 0) trace(8, (u32)idx, (u32)dmach[idx].valid);
        return 0;
    }
    while (got < n) {
        u32 k;
        if (dmach[idx].mode & DMA_MASKED) { if (xf.done == 0 && got == 0) trace(9, dmach[idx].mode, dmach[idx].count); break; }
        if (dmach[idx].count == 0) {
            if (!(dmach[idx].mode & DMA_AUTOINIT) || !dmach[idx].base_count) break;
            dmach[idx].addr = dmach[idx].base_addr;
            dmach[idx].count = dmach[idx].base_count;
        }
        if (xf.done == 0 && got == 0) trace(4, dmach[idx].addr, (dmach[idx].count << 8) | (dmach[idx].mode & 0xFF));
        k = n - got;
        if (k > dmach[idx].count) k = dmach[idx].count;
        if (xf.record) mset((void *)dmach[idx].addr, xf.bits == 16 ? 0 : 0x80, k);
        else convert((const u8 *)dmach[idx].addr, k);
        dmach[idx].addr += k; dmach[idx].count -= k; got += k;
        if (dmach[idx].count == 0 && !(dmach[idx].mode & DMA_AUTOINIT)) dmach[idx].mode |= DMA_MASKED;
        if (sh) sh->dma_bytes += k;
    }
    if (got) dma_push(idx);
    return got;
}

void __cdecl sc_tick(void)
{
    u32 now = vxd_time_us();
    if (sh) sh->now_us = now;
    if (xf.active && !xf.paused) {
        u32 target = vxd_muldiv(now - xf.start_us, xf.bps, 1000000UL);
        u32 todo = target > xf.consumed ? target - xf.consumed : 0;
        int stalled = 0;
        if (todo > 2000) trace(5, target, xf.consumed);
        if (todo > xf.bps / 4) todo = xf.bps / 4;          /* never more than 250 ms at once */
        while (todo && xf.active) {
            u32 n = xf.block - xf.done, got;
            if (n > todo) n = todo;
            got = xf.bits ? xf_pull(n) : n;
            xf.done += got; xf.consumed += got; todo -= got;
            if (xf.done >= xf.block) {
                xf.done = 0;
                trace(3, xf.consumed, now - xf.start_us);
                raise_irq(xf.bits == 16);
                if (!xf.autoinit) { xf_stop(); break; }
            }
            if (got < n) { stalled = 1; break; }
        }
        if (xf.active) {
            if (stalled || target > xf.consumed + xf.bps / 4) xf_rebase(now);
            if (xf.consumed >= xf.bps * 4) {               /* keep the numbers small */
                xf.start_us += vxd_muldiv(xf.consumed, 1000000UL, xf.bps);
                xf.consumed = 0;
            }
        }
    }
    if (xf.active) vxd_timer_arm(1);
}

/* VDMAD callback. why = 1/2/3: the program is reading the count, page or
 * address register (restore our position over VDMAD's recalculation);
 * anything else: the program changed the channel (reload it). */
void __cdecl sc_dma_changed(int idx, u32 vm, u32 why)
{
    u32 addr, count, mode;
    trace(7, why, idx);
    why &= 0xFF;
    if (why >= 1 && why <= 3) {
        if (sh) sh->debug[11 + (dmach[idx].valid ? 0 : 1)]++;
        trace(11, dmach[idx].valid, BUILD_ID);
        if (dmach[idx].valid) {
            vxd_dma_set(idx, vm, dmach[idx].addr, vdmad_count(idx, dmach[idx].count), dmach[idx].mode);
            if (idx && vxd_dma_get(idx, vm, &addr, &count, &mode) && sh) { sh->debug[13] = count; sh->debug[14] = mode; sh->debug[15] = dmach[idx].count; }
            if (idx && vxd_dma_get(idx, vm, &addr, &count, &mode))
                trace(10, dmach[idx].count | (dmach[idx].mode << 20), count | (mode << 20));
        }
        return;
    }
    if (!vxd_dma_get(idx, vm, &addr, &count, &mode)) return;
    count = clamp_count(idx, count);
    trace(6, ((u32)idx << 8) | (mode & 0xFF), count);
    dmach[idx].addr = addr; dmach[idx].count = count; dmach[idx].mode = mode;
    dmach[idx].valid = 1;
    if (!(mode & DMA_MASKED)) {
        dmach[idx].base_addr = addr;
        dmach[idx].base_count = count;
        if (sh) { sh->debug[8] = addr; sh->debug[9] = count; sh->debug[10] = mode; }
    }
}

/* VPICD EOI callback */
void __cdecl sc_eoi(u32 vm)
{
    (void)vm;
    lower_line();
}

/* ---- DSP commands ---- */
static void dsp_reset(void)
{
    if (xf.active) ev_put(EV_RESET, 0, 0, 0);
    xf.active = 0;
    if (sh) sh->dma_active = 0;
    dsp.cmd = -1; dsp.need = 0; dsp.np = 0;
    dsp.outn = dsp.outp = 0;
    dsp.highspeed = 0;
    dsp.midi_uart = 0;
    dsp.dmaid_a = 0xAA; dsp.dmaid_x = 0x96;
    dsp.speaker = dsp.version >= 0x400 ? 1 : 0;
    dsp.adpcm_useref = 0;
    irq_status = 0;
    lower_line();
    mixer()[0x0E] &= ~2;
}

static int stereo8(void) { return (dsp.version >= 0x300 && dsp.version < 0x400 && stereo_bit()) ? 2 : 1; }

static u32 len16(void) { return (u32)dsp.p[0] | ((u32)dsp.p[1] << 8); }

/* parameter bytes each command takes */
static int params_for(u8 c)
{
    if (c >= 0xB0 && c <= 0xCF) return 3;
    switch (c) {
    case 0x10: case 0x38: case 0x40: case 0xE0: case 0xE2: case 0xE4: case 0x0F: case 0xF9:
        return 1;
    case 0x14: case 0x16: case 0x17: case 0x24: case 0x41: case 0x42: case 0x48:
    case 0x74: case 0x75: case 0x76: case 0x77: case 0x80: case 0x05: case 0x0E:
        return 2;
    }
    return 0;
}

static void dsp_dma_id(u8 v)
{
    dsp.dmaid_a = (u8)(dsp.dmaid_a + (v ^ dsp.dmaid_x));
    dsp.dmaid_x = (u8)((dsp.dmaid_x >> 2) | (dsp.dmaid_x << 6));
    if (!(trapped & ST_OK_DMA8) || !dmach[0].valid) return;
    if ((dmach[0].mode & DMA_MASKED) || !dmach[0].count) return;
    *(volatile u8 *)dmach[0].addr = dsp.dmaid_a;
    dmach[0].addr++; dmach[0].count--;
    if (!dmach[0].count && !(dmach[0].mode & DMA_AUTOINIT)) dmach[0].mode |= DMA_MASKED;
    dma_push(0);
}

static void dsp_exec(u8 c)
{
    u8 *p = dsp.p;
    switch (c) {
    /* --- output --- */
    case 0x10: ev_put(EV_DAC, p[0], 0, 0); break;
    case 0x14: xf_start(8, stereo8(), 0, 0, len16() + 1, 0); break;
    case 0x1C: xf_start(8, stereo8(), 0, 1, dsp.blocksize, 0); break;
    case 0x90: dsp.highspeed = 1; xf_start(8, stereo8(), 0, 1, dsp.blocksize, 0); break;
    case 0x91: dsp.highspeed = 1; xf_start(8, stereo8(), 0, 0, dsp.blocksize, 0); break;
    case 0x16: case 0x17:
        dsp.adpcm_useref = (c == 0x17); dsp.adpcm_step = 0;
        xf_start(2, 1, 0, 0, len16() + 1, 0); break;
    case 0x74: case 0x75:
        dsp.adpcm_useref = (c == 0x75); dsp.adpcm_step = 0;
        xf_start(4, 1, 0, 0, len16() + 1, 0); break;
    case 0x76: case 0x77:
        dsp.adpcm_useref = (c == 0x77); dsp.adpcm_step = 0;
        xf_start(3, 1, 0, 0, len16() + 1, 0); break;
    case 0x1F: dsp.adpcm_useref = 1; dsp.adpcm_step = 0; xf_start(2, 1, 0, 1, dsp.blocksize, 0); break;
    case 0x7D: dsp.adpcm_useref = 1; dsp.adpcm_step = 0; xf_start(4, 1, 0, 1, dsp.blocksize, 0); break;
    case 0x7F: dsp.adpcm_useref = 1; dsp.adpcm_step = 0; xf_start(3, 1, 0, 1, dsp.blocksize, 0); break;
    case 0x80: xf_start(0, 1, 0, 0, len16() + 1, 0); break;
    /* --- input: the buffer is filled with silence --- */
    case 0x20: dsp_reply(0x80); break;
    case 0x24: xf_start(8, stereo8(), 0, 0, len16() + 1, 1); break;
    case 0x2C: case 0x98: xf_start(8, stereo8(), 0, 1, dsp.blocksize, 1); break;
    case 0x99: xf_start(8, stereo8(), 0, 0, dsp.blocksize, 1); break;
    /* --- MIDI --- */
    case 0x30: case 0x31: break;
    case 0x34: case 0x35: case 0x36: case 0x37: dsp.midi_uart = 1; break;
    case 0x38: ev_put(EV_MIDI, p[0], 0, 0); break;
    /* --- rate and size --- */
    case 0x40:
        dsp.time_const = p[0]; dsp.use_tc = 1; update_tc_rate(); break;
    case 0x41: case 0x42:
        dsp.rate = ((u32)p[0] << 8) | p[1]; dsp.use_tc = 0;
        if (dsp.rate < 1000) dsp.rate = 1000;
        break;
    case 0x48: dsp.blocksize = len16() + 1; break;
    /* --- control --- */
    case 0xD0: if (xf.active && xf.bits != 16) xf.paused = 1; break;
    case 0xD4: case 0x45:
        if (xf.active && xf.paused) { xf.paused = 0; xf_rebase(vxd_time_us()); vxd_timer_arm(1); }
        break;
    case 0xD5: if (xf.active && xf.bits == 16) xf.paused = 1; break;
    case 0xD6: case 0x47:
        if (xf.active && xf.paused) { xf.paused = 0; xf_rebase(vxd_time_us()); vxd_timer_arm(1); }
        break;
    case 0xD1: dsp.speaker = 1; ev_put(EV_SPEAKER, 1, 0, 0); break;
    case 0xD3: dsp.speaker = 0; ev_put(EV_SPEAKER, 0, 0, 0); break;
    case 0xD8: dsp_reply(dsp.speaker ? 0xFF : 0x00); break;
    case 0xD9: case 0xDA: if (xf.active) xf.autoinit = 0; break;   /* stop after this block */
    /* --- identification --- */
    case 0xE0: dsp_reply((u8)~p[0]); break;
    case 0xE1: dsp_reply((u8)(dsp.version >> 8)); dsp_reply((u8)dsp.version); break;
    case 0xE2: dsp_dma_id(p[0]); break;
    case 0xE3: {
        int i;
        for (i = 0; copyright[i]; i++) dsp_reply((u8)copyright[i]);
        dsp_reply(0);
        break;
    }
    case 0xE4: dsp.test_reg = p[0]; break;
    case 0xE8: dsp_reply(dsp.test_reg); break;
    case 0xF2: raise_irq(0); break;
    case 0xF3: if (dsp.version >= 0x400) raise_irq(1); break;
    case 0xF1: dsp_reply(0); break;
    case 0xF8: dsp_reply(0); break;
    case 0x0F: dsp_reply(0); break;           /* SB16 ASP register read */
    case 0x04: dsp_reply(0xFF); break;        /* SB16 DSP status */
    default:
        if (c >= 0xB0 && c <= 0xCF && dsp.version >= 0x400) {
            int sixteen = c < 0xC0;
            int autoinit = (c & 0x04) != 0;
            int record = (c & 0x08) != 0;
            int chans = (p[0] & 0x20) ? 2 : 1;
            int sign = (p[0] & 0x10) != 0;
            u32 n = ((u32)p[1] | ((u32)p[2] << 8)) + 1;
            xf_start(sixteen ? 16 : 8, chans, sign, autoinit, n * (sixteen ? 2 : 1), record);
        }
        break;
    }
}

static void dsp_write(u8 v)
{
    if (dsp.highspeed && xf.active) return;            /* high-speed mode ignores commands */
    if (dsp.midi_uart && dsp.cmd < 0) { ev_put(EV_MIDI, v, 0, 0); return; }
    if (dsp.cmd < 0) {
        if (sh) sh->dsp_cmds++;
        dsp.np = 0;
        dsp.need = params_for(v);
        if (!dsp.need) { dsp_exec(v); return; }
        dsp.cmd = v;
        return;
    }
    dsp.p[dsp.np++] = v;
    if (--dsp.need <= 0) {
        u8 c = (u8)dsp.cmd;
        dsp.cmd = -1;
        dsp_exec(c);
    }
}

static u8 dsp_read_data(void)
{
    if (dsp.outp < dsp.outn) {
        u8 v = dsp.out[dsp.outp++];
        if (dsp.outp >= dsp.outn) dsp.outp = dsp.outn = 0;
        return v;
    }
    return 0xFF;
}

/* ====================================================================== */
/* port dispatch (called by the I/O trap stub for byte I/O)               */
/* ====================================================================== */
/* ====================================================================== */
/* game port 201h and INT 15h AH=84h for DOS boxes                        */
/* (the stick values come from the helper, which reads the Windows        */
/* joystick API; Windows' own programs keep the real port)                */
/* ====================================================================== */
static u32 joy_t0, sys_vm;
static u32 vol_saved = 0xFFFFFFFFUL, vol_set;

static u8 joy_read(void)
{
    u32 dt = vxd_time_us() - joy_t0;
    u8 v = 0;
    int i;
    for (i = 0; i < 4; i++) {
        /* an axis bit stays 1 for 8..1008 us after a write, by stick position;
           no stick: it never drops, as on a real port */
        if (!(sh->joy_present & (1 << (i >> 1)))) { v |= (u8)(1 << i); continue; }
        if (dt < 8 + (u32)sh->joy_axis[i] * 1000u / 65535u) v |= (u8)(1 << i);
    }
    v |= (u8)((~sh->joy_buttons & 0x0F) << 4);    /* buttons: 0 = pressed */
    sh->joy_reads++;
    return v;
}

/* INT 15h AH=84h from the asm hook; returns 1 when answered */
int __cdecl sc_int15(u32 vm, u8 *cl)
{
    u32 *eax = (u32 *)(cl + 0x1C), *ebx = (u32 *)(cl + 0x10), *ecx = (u32 *)(cl + 0x18),
        *edx = (u32 *)(cl + 0x14), *efl = (u32 *)(cl + 0x2C);
    u32 dx = *edx & 0xFFFF;
    if (!sh || !(trapped & ST_OK_JBIOS) || vm == sys_vm || !sh->joy_present) return 0;
    if (dx == 0) {                                  /* buttons: AL bits 4-7, 0 = pressed */
        *eax = (*eax & 0xFFFFFF00UL) | (u8)((~sh->joy_buttons & 0x0F) << 4);
    } else if (dx == 1) {                           /* axes: AX BX CX DX, 0..255 */
        u32 a[4];
        int i;
        for (i = 0; i < 4; i++) a[i] = (sh->joy_present & (1 << (i >> 1))) ? (u32)sh->joy_axis[i] >> 8 : 0;
        *eax = (*eax & 0xFFFF0000UL) | a[0];
        *ebx = (*ebx & 0xFFFF0000UL) | a[1];
        *ecx = (*ecx & 0xFFFF0000UL) | a[2];
        *edx = (*edx & 0xFFFF0000UL) | a[3];
    } else return 0;
    *efl &= ~1UL;
    sh->joy_reads++;
    return 1;
}

u32 __cdecl sc_in(u32 vm, u32 port)
{
    u32 off;
    if ((trapped & ST_OK_JOY) && port == 0x201) {
        if (vm == sys_vm) return vxd_port_in(port);
        return joy_read();
    }
    if ((trapped & ST_OK_SB) && port >= cfg.sb_port && port < (u32)cfg.sb_port + 16) {
        off = port - cfg.sb_port;
        switch (off) {
        case 0x0: case 0x2: case 0x8: return cfg.opl ? opl_read_status() : 0xFF;
        case 0x1: case 0x3: case 0x9: return 0xFF;
        case 0x4: return dsp.version >= 0x300 ? mix_index : 0xFF;
        case 0x5: return dsp.version >= 0x300 ? mixer_read() : 0xFF;
        case 0x6: return 0xFF;
        case 0xA: return dsp_read_data();
        case 0xC: dsp.ws = (u8)(dsp.ws + 0x80); return (u8)(dsp.ws | 0x7F);
        case 0xE: ack_irq(0); return (u8)(dsp.outp < dsp.outn ? 0xFF : 0x7F);
        case 0xF: ack_irq(1); return 0xFF;
        }
        return 0xFF;
    }
    if ((trapped & ST_OK_OPL) && port >= 0x388 && port <= 0x38B) {
        return (port == 0x388) ? opl_read_status() : 0xFF;
    }
    if ((trapped & ST_OK_MPU) && (port == cfg.mpu_port || port == (u32)cfg.mpu_port + 1))
        return mpu_read(port);
    (void)vm;
    return 0xFF;
}

void __cdecl sc_out(u32 vm, u32 port, u32 val)
{
    u8 v = (u8)val;
    u32 off;
    if ((trapped & ST_OK_JOY) && port == 0x201) {
        if (vm == sys_vm) vxd_port_out(port, val);
        else joy_t0 = vxd_time_us();
        return;
    }
    if ((trapped & ST_OK_SB) && port >= cfg.sb_port && port < (u32)cfg.sb_port + 16) {
        off = port - cfg.sb_port;
        switch (off) {
        case 0x0: case 0x8: if (cfg.opl) opl_index[0] = v; return;
        case 0x1: case 0x9: if (cfg.opl) opl_write_data(0, v); return;
        case 0x2: if (cfg.opl && dsp.version >= 0x300) opl_index[1] = v; return;
        case 0x3: if (cfg.opl && dsp.version >= 0x300) opl_write_data(1, v); return;
        case 0x4: mix_index = v; return;
        case 0x5: if (dsp.version >= 0x300) mixer_write(v); return;
        case 0x6: owner_vm = vm;
            if (v & 1) { dsp.reset_state = 1; dsp_reset(); }
            else if (dsp.reset_state == 1) { dsp.reset_state = 0; dsp.outn = dsp.outp = 0; dsp_reply(0xAA); }
            return;
        case 0xC: owner_vm = vm; if (sh) sh->last_vm = vm; dsp_write(v); return;
        }
        return;
    }
    if ((trapped & ST_OK_OPL) && port >= 0x388 && port <= 0x38B) {
        int bank = (port >= 0x38A) ? 1 : 0;
        if (port & 1) opl_write_data(bank, v);
        else opl_index[bank] = v;
        return;
    }
    if ((trapped & ST_OK_MPU) && (port == cfg.mpu_port || port == (u32)cfg.mpu_port + 1)) {
        mpu_write(port, v);
        return;
    }
}

/* ====================================================================== */
/* attach / detach                                                        */
/* ====================================================================== */
static int trap_range(u32 base, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) {
        if (vxd_install_io(base + i) != 0) {
            while (i--) vxd_remove_io(base + i);
            return 0;
        }
    }
    return 1;
}

static void untrap_range(u32 base, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++) vxd_remove_io(base + i);
}

static void detach(void)
{
    xf.active = 0;
    lower_line();
    if (trapped & ST_OK_SB)  untrap_range(cfg.sb_port, 16);
    if (trapped & ST_OK_OPL) untrap_range(0x388, 4);
    if (trapped & ST_OK_MPU) untrap_range(cfg.mpu_port, 2);
    if (trapped & ST_OK_IRQ) vxd_unvirt_irq();
    if (trapped & ST_OK_DMA8) vxd_unvirt_dma(0);
    if (trapped & ST_OK_DMA16) vxd_unvirt_dma(1);
    if (trapped & ST_OK_JOY) vxd_remove_io(0x201);
    if (trapped & ST_OK_JBIOS) vxd_hook_int15(0);
    if (trapped) vxd_min_period(0);
    trapped = 0;
    if (sh) { vxd_unmap_block((u32)sh, sh_size); sh = 0; }
}

static u32 attach(u32 user_addr)
{
    u32 st = 0;
    SC55W_SHARED *s;
    if (sh) return ST_ERR_BUSY;
    sh_size = sizeof(SC55W_SHARED);
    s = (SC55W_SHARED *)vxd_map_block(user_addr, sh_size);
    if (!s) return ST_ERR_LOCK;
    if (s->magic != SC55W_MAGIC || s->if_version != SC55W_IF_VERSION || s->size != sizeof(SC55W_SHARED)) {
        vxd_unmap_block((u32)s, sh_size);
        return ST_ERR_BLOCK;
    }
    mcpy(&cfg, &s->cfg, sizeof cfg);
    if (cfg.sbtype < 1 || cfg.sbtype > 6) cfg.sbtype = 6;
    mset(&dsp, 0, sizeof dsp);
    mset(&xf, 0, sizeof xf);
    mset(dmach, 0, sizeof dmach);
    dsp.version = dsp_versions[cfg.sbtype];
    dsp.cmd = -1;
    dsp.rate = 22050;
    dsp.blocksize = 0x800;
    irq_status = 0; irq_line = 0; owner_vm = 0;
    mpu_uart = 0; mpu_qn = 0;
    opl_ctrl = opl_status = 0;
    pcm_rate_sent = 0;
    sh = s;
    sys_vm = vxd_sys_vm();
    s->midi_volume = vol_saved;
    s->midi_volume_set = vol_set;
    mixer_reset();
    dsp_reset();

    if (cfg.sb_port) {
        if (trap_range(cfg.sb_port, 16)) st |= ST_OK_SB; else st |= ST_ERR_SBPORT;
    }
    if (cfg.opl) {
        if (trap_range(0x388, 4)) st |= ST_OK_OPL; else st |= ST_ERR_OPLPORT;
    }
    if (cfg.mpu_port) {
        if (trap_range(cfg.mpu_port, 2)) st |= ST_OK_MPU; else st |= ST_ERR_MPUPORT;
    }
    if (cfg.sb_port) {
        if (vxd_virt_irq(cfg.irq) == 0) st |= ST_OK_IRQ; else st |= ST_ERR_IRQ;
        if (vxd_virt_dma(0, cfg.dma8) == 0) st |= ST_OK_DMA8; else st |= ST_ERR_DMA8;
        if (cfg.dma16 && dsp.version >= 0x400) {
            if (vxd_virt_dma(1, cfg.dma16) == 0) st |= ST_OK_DMA16; else st |= ST_ERR_DMA16;
        }
    }
    if (cfg.joy & 1) {
        if (vxd_install_io(0x201) == 0) st |= ST_OK_JOY; else st |= ST_ERR_JOYPORT;
    }
    if (cfg.joy & 2) {
        if (vxd_hook_int15(1) == 0) st |= ST_OK_JBIOS; else st |= ST_ERR_JBIOS;
    }
    trapped = st & 0xFF;
    if (trapped) vxd_min_period(1);
    s->attach_status = st;
    s->now_us = vxd_time_us();
    return st;
}

/* ====================================================================== */
/* DeviceIoControl                                                        */
/* ====================================================================== */
/* returns number of bytes written to out, or -1 for an unknown code */
int __cdecl sc_ioctl(u32 code, u8 *in, u32 inlen, u8 *out, u32 outlen)
{
    switch (code) {
    case 0:            /* DIOC_OPEN */
        return 0;
    case 0xFFFFFFFFUL: /* DIOC_CLOSEHANDLE: the helper went away */
        detach();
        return 0;
    case IOCTL_SC55W_VERSION:
        if (outlen >= 4) { *(u32 *)out = SC55W_IF_VERSION; }
        if (outlen >= 8) { *(u32 *)(out + 4) = BUILD_ID; return 8; }
        return outlen >= 4 ? 4 : 0;
    case IOCTL_SC55W_ATTACH: {
        u32 st;
        if (inlen < 4) return -1;
        st = attach(*(u32 *)in);
        if (outlen >= 4) { *(u32 *)out = st; return 4; }
        return 0;
    }
    case IOCTL_SC55W_DETACH:
        detach();
        return 0;
    case IOCTL_SC55W_TIME:
        if (outlen >= 4) { *(u32 *)out = vxd_time_us(); return 4; }
        return 0;
    case IOCTL_SC55W_MIDI: {
        u32 i;
        for (i = 0; i < inlen; i++) ev_put(EV_MIDI, in[i], 0, 0);
        return 0;
    }
    case IOCTL_SC55W_RESET:
        dsp_reset();
        mpu_uart = 0; mpu_qn = 0;
        return 0;
    }
    return -1;
}

/* 16-bit MIDI driver entry (protected-mode API): one byte at a time */
void __cdecl sc_api_midi(u32 b)
{
    ev_put(EV_MIDI, (u8)b, 0, 0);
}

/* 16-bit MIDI driver: Windows MIDI volume (low word left, high word right) */
void __cdecl sc_api_volume(u32 v)
{
    vol_saved = v;
    vol_set = 1;
    if (sh) { sh->midi_volume = v; sh->midi_volume_set = 1; }
}

/* the VxD is being unloaded */
void __cdecl sc_exit(void)
{
    detach();
}
