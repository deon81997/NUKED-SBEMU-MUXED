/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* m3core.h - the CPU-drawn 640x480 VESA graphics core shared by the 3D menu (menu3d.h) and MIDPLAY's
 * 3D view: VBE mode set and present, timing, colours, panels, bevels, text in the pack's fonts, a small
 * perspective-correct 3D rasterizer with z-buffer, boxes, knobs, the starry sky and the mirror floor. */
#ifndef M3CORE_INCLUDED
#define M3CORE_INCLUDED
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <math.h>
#include <pc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <sys/nearptr.h>
#include "fontdata.h"
#include "wcset.h"
#ifndef M3_WHO
#define M3_WHO "3D"
#endif
static unsigned long m3_ticks(void) { return _farpeekl(_dos_ds, 0x46C); }   /* BIOS clock, 18.2 a second */

#ifdef M3_DYNAMIC_RES
/* MIDPLAY: the screen size is chosen at run time (any VESA mode up to M3WMAX x M3HMAX, or VGA 320x200). The other
   programs keep the fixed 640x480 below. m3_fyk stretches the picture vertically for modes that are not 4:3. */
#define M3WMAX 1600
#define M3HMAX 1200
static int m3_W = 640, m3_H = 480;
#define M3W m3_W
#define M3H m3_H
#else
#define M3W 640
#define M3H 480
#define M3WMAX 640
#endif
#define M3_FBASE 640.0f           /* the focal length m3_f is for a 640 wide picture; wider modes scale it */
static float m3_fyk = 1.0f;
typedef struct { int w, h; uint32_t* p; } M3SURF;

static uint32_t* m3_fb;
static float*    m3_zb;
static M3SURF    m3_scr;
static int       m3_style;
/* speed: the 3D scene can be drawn at half resolution (320x240, doubled) on slow PCs. m3_quality: 0 = auto
 * (half when under 15 frames a second), 1 = full, 2 = half; F4 cycles it, MENU3D.CFG keeps it */
static int       m3_quality, m3_half, m3_rw = 640, m3_rh = 480;
static float     m3_fps;
static double    m3_note_until;           /* show the quality / speed note in the top bar until then */
static int       m3_bpp, m3_pitch, m3_sel, m3_lfb, m3_winseg, m3_gran, m3_bank = -1, m3_mode;
static int       m3_rs, m3_rp, m3_gs, m3_gp, m3_bs, m3_bp;
static unsigned long m3_lin;
static __dpmi_meminfo m3_map;
static unsigned char m3_line[M3WMAX * 4];
static double    m3_tsc_us = 0;     /* TSC ticks per microsecond */
/* called between scan lines while a frame is drawn (MIDPLAY sends its due MIDI events from it); NULL elsewhere */
static void (*m3_hook)(void);
#define M3_HOOK() do { if (m3_hook) m3_hook(); } while (0)
static unsigned long long m3_t0;

/* ------------------------------------------------------------------------------------------- VBE */
static void m3_put(unsigned long off, const void* src, int n)
{
    if (m3_lfb) { movedata(_my_ds(), (unsigned)src, m3_sel, off, n); return; }
    while (n > 0)
    {
        unsigned long g = (unsigned long)m3_gran * 1024, bank = off / g, o = off % g;
        int k = n; if ((unsigned long)k > 65536 - o) k = (int)(65536 - o);
        if ((unsigned long)k > g - o) k = (int)(g - o);
        if ((int)bank != m3_bank) { __dpmi_regs r; memset(&r, 0, sizeof r); r.x.ax = 0x4F05; r.x.dx = (unsigned short)bank; __dpmi_int(0x10, &r); m3_bank = (int)bank; }
        dosmemput(src, k, (unsigned long)m3_winseg * 16 + o);
        src = (const char*)src + k; off += k; n -= k;
    }
}

#ifdef M3_DYNAMIC_RES
/* every graphics mode the card offers that the 3D view can draw in: 8 bit (packed, dithered to 252 colours) and 15, 16,
   24, 32 bit direct colour, 320x200 up to M3WMAX x M3HMAX; mode 0x13 (VGA 320x200, works in a Windows DOS box window)
   is always first. Sorted by size, then colours. */
typedef struct { int mode, w, h, bpp, lfb, win; } M3MODE;
static M3MODE m3_modes[64]; static int m3_nmodes, m3_vbever;
static void m3_list_add(int mode, const unsigned char* mi, int ver)
{
    int attr = mi[0] | (mi[1] << 8), w = mi[0x12] | (mi[0x13] << 8), h = mi[0x14] | (mi[0x15] << 8), bpp = mi[0x19], mm = mi[0x1B], i;
    M3MODE m;
    if (!(attr & 1) || !(attr & 0x10) || m3_nmodes >= 64) return;          /* supported, graphics */
    if (!((mm == 4 && bpp == 8) || (mm == 6 && (bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32)))) return;
    if (w < 320 || h < 200 || w > M3WMAX || h > M3HMAX) return;
    m.mode = mode; m.w = w; m.h = h; m.bpp = bpp; m.lfb = (attr & 0x80) && ver >= 2; m.win = (mi[2] & 4) != 0;
    if (!m.lfb && !m.win) return;                                            /* nowhere to write */
    for (i = 0; i < m3_nmodes; i++) if (m3_modes[i].w == w && m3_modes[i].h == h && m3_modes[i].bpp == bpp) return;   /* same mode twice */
    i = m3_nmodes++;
    while (i > 1 && (m3_modes[i - 1].w > w || (m3_modes[i - 1].w == w && (m3_modes[i - 1].h > h || (m3_modes[i - 1].h == h && m3_modes[i - 1].bpp > bpp)))))
    { m3_modes[i] = m3_modes[i - 1]; i--; }
    m3_modes[i] = m;
}
#endif
/* the VBE controller info and the best 640x480 mode are looked up once (about a hundred BIOS calls) and kept:
 * MIDPLAY calls m3_vbe_probe() before the music starts, so switching to 3D later costs only the mode set */
static unsigned char m3_vi[512]; static int m3_vbest = -2;
static void m3_vbe_probe(void)
{
    __dpmi_regs r; unsigned char mi[256]; unsigned short modes[256]; int n = 0, i, best = -1, bestscore = 0;
    unsigned long lp; unsigned char* vi = m3_vi;
    if (m3_vbest != -2) return;
    m3_vbest = -1;
#ifdef M3_DYNAMIC_RES
    m3_nmodes = 1; m3_modes[0].mode = 0x13; m3_modes[0].w = 320; m3_modes[0].h = 200; m3_modes[0].bpp = 8; m3_modes[0].lfb = 0; m3_modes[0].win = 1;
#endif
    memset(&r, 0, sizeof r); dosmemput("VBE2", 4, __tb);
    r.x.ax = 0x4F00; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) return;
    dosmemget(__tb, 512, vi);
#ifdef M3_DYNAMIC_RES
    m3_vbever = vi[5];
#endif
    lp = ((unsigned long)(vi[0x10] | (vi[0x11] << 8)) << 4) + (vi[0x0E] | (vi[0x0F] << 8));
    while (n < 255) { unsigned short m; dosmemget(lp + n * 2, 2, &m); if (m == 0xFFFF) break; modes[n++] = m; }
    for (i = 0; i < n; i++)
    {
        int attr, bpp, score;
        memset(&r, 0, sizeof r); r.x.ax = 0x4F01; r.x.cx = modes[i]; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
        if (r.x.ax != 0x004F) continue;
        dosmemget(__tb, 256, mi);
        attr = mi[0] | (mi[1] << 8); bpp = mi[0x19];
        if (!(attr & 1) || !(attr & 0x10)) continue;
#ifdef M3_DYNAMIC_RES
        m3_list_add(modes[i], mi, vi[5]);
#endif
        if ((mi[0x12] | (mi[0x13] << 8)) != 640 || (mi[0x14] | (mi[0x15] << 8)) != 480) continue;
        if (mi[0x1B] != 6) continue;                                   /* direct colour */
        score = bpp == 32 ? 40 : bpp == 16 ? 30 : bpp == 15 ? 20 : bpp == 24 ? 10 : 0;
        if (!score) continue;
        if ((attr & 0x80) && (vi[5] >= 2)) score += 5;                  /* linear frame buffer */
        if (score > bestscore) { bestscore = score; best = modes[i]; }
    }
    m3_vbest = best;
}
static int m3_vbe_open(void)
{
    __dpmi_regs r; unsigned char mi[256]; int best; unsigned char* vi = m3_vi;
    m3_vbe_probe();
    best = m3_vbest;
    if (best < 0) return 0;
    memset(&r, 0, sizeof r); r.x.ax = 0x4F01; r.x.cx = best; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
    dosmemget(__tb, 256, mi);
    m3_mode = best; m3_bpp = mi[0x19]; m3_pitch = mi[0x10] | (mi[0x11] << 8);
    m3_rs = mi[0x1F]; m3_rp = mi[0x20]; m3_gs = mi[0x21]; m3_gp = mi[0x22]; m3_bs = mi[0x23]; m3_bp = mi[0x24];
    m3_winseg = mi[8] | (mi[9] << 8); m3_gran = mi[4] | (mi[5] << 8); if (!m3_gran) m3_gran = 64;
    m3_lfb = 0;
    if (((mi[0] | (mi[1] << 8)) & 0x80) && vi[5] >= 2)
    {
        unsigned long phys = mi[0x28] | (mi[0x29] << 8) | ((unsigned long)mi[0x2A] << 16) | ((unsigned long)mi[0x2B] << 24);
        if (vi[5] >= 3 && (mi[0x32] | (mi[0x33] << 8))) m3_pitch = mi[0x32] | (mi[0x33] << 8);
        m3_map.address = phys; m3_map.size = (unsigned long)m3_pitch * M3H;
        if (phys && __dpmi_physical_address_mapping(&m3_map) == 0)
        {
            m3_lin = m3_map.address;
            m3_sel = __dpmi_allocate_ldt_descriptors(1);
            if (m3_sel > 0)
            {
                __dpmi_set_segment_base_address(m3_sel, m3_lin);
                __dpmi_set_segment_limit(m3_sel, ((unsigned long)m3_pitch * M3H - 1) | 0xFFF);
                m3_lfb = 1;
                wc_startup(M3_WHO, phys);                                  /* write-combining (WCSET.CFG) */
            }
            else { __dpmi_free_physical_address_mapping(&m3_map); m3_sel = 0; }
        }
    }
    if (!m3_lfb && !(mi[2] & 4)) return 0;                             /* window A not writable */
    memset(&r, 0, sizeof r); r.x.ax = 0x4F02; r.x.bx = (unsigned short)(best | (m3_lfb ? 0x4000 : 0) | 0x8000);   /* 8000h: the BIOS does not clear (slow); every frame is drawn whole */ __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) { if (m3_lfb) { __dpmi_free_ldt_descriptor(m3_sel); __dpmi_free_physical_address_mapping(&m3_map); } m3_lfb = 0; return 0; }
    m3_bank = -1;
    return 1;
}

static void m3_vbe_close(void)
{
    __dpmi_regs r;
    if (m3_lfb) { __dpmi_free_ldt_descriptor(m3_sel); __dpmi_free_physical_address_mapping(&m3_map); m3_lfb = 0; m3_sel = 0; }
    memset(&r, 0, sizeof r); r.x.ax = 0x0003; __dpmi_int(0x10, &r);
}

#ifdef M3_DYNAMIC_RES
static double m3_now(void);
static unsigned char m3_dr[4][4 * 256], m3_dg[4][4 * 256], m3_db[4][4 * 256];   /* [row][col * 256 + value] -> palette part */
static void m3_pal252(void)
{
    static const int bay[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
    int y, x, v, i;
    for (y = 0; y < 4; y++) for (x = 0; x < 4; x++) for (v = 0; v < 256; v++)
    {
        int t = bay[y][x] * 16 + 8;                                   /* threshold 8..248 */
        int r = (v * 5 * 256 + t * 255) / (255 * 256), g = (v * 6 * 256 + t * 255) / (255 * 256), b = r;
        m3_dr[y][x * 256 + v] = (unsigned char)((r > 5 ? 5 : r) * 42);
        m3_dg[y][x * 256 + v] = (unsigned char)((g > 6 ? 6 : g) * 6);
        m3_db[y][x * 256 + v] = (unsigned char)(b > 5 ? 5 : b);
    }
    {
        unsigned char pal[1024]; __dpmi_regs r;
        for (i = 0; i < 256; i++)
        {
            pal[i * 4 + 2] = (unsigned char)(i < 252 ? i / 42 * 63 / 5 : 0); pal[i * 4 + 1] = (unsigned char)(i < 252 ? i / 6 % 7 * 63 / 6 : 0);
            pal[i * 4 + 0] = (unsigned char)(i < 252 ? i % 6 * 63 / 5 : 0); pal[i * 4 + 3] = 0;
        }
        if (m3_mode != 0x13 && m3_vbever >= 2)
        {   /* VBE 4F09h first: some cards' DAC in VESA modes is not VGA compatible */
            dosmemput(pal, 1024, __tb);
            memset(&r, 0, sizeof r); r.x.ax = 0x4F09; r.h.bl = 0; r.x.cx = 256; r.x.dx = 0; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
            if (r.x.ax == 0x004F) return;
        }
        outportb(0x3C8, 0);                                            /* the VGA DAC (6 bits per colour) */
        for (i = 0; i < 256; i++) { outportb(0x3C9, pal[i * 4 + 2]); outportb(0x3C9, pal[i * 4 + 1]); outportb(0x3C9, pal[i * 4]); }
    }
}
static int m3_refresh;            /* the refresh rate set (Hz, 0 = the BIOS's own) */
static double m3_refresh_seen;    /* measured from the retrace, 0 = unknown */
/* VBE 3.0 CRTC timings for w x h at hz (VESA GTF), pixel clock rounded by the BIOS (4F0Bh); 0 = not possible */
static int m3_gtf(int mode, int w, int h, int hz, unsigned char* blk)
{
    double hp_est, vfe, hp, duty, pclk; int vsbp, vtot, hblank, htot, hsync, hfp, vss; unsigned long clk; __dpmi_regs r;
    w = w / 8 * 8;
    hp_est = (1.0 / hz - 550e-6) / (h + 1) * 1e6;                     /* us */
    vsbp = (int)(550.0 / hp_est + 0.5); vtot = h + vsbp + 1;
    vfe = 1.0 / hp_est / vtot * 1e6; hp = hp_est / (hz / vfe);
    duty = 30.0 - 300.0 * hp / 1000.0;
    hblank = (int)(w * duty / (100.0 - duty) / 16.0 + 0.5) * 16; htot = w + hblank;
    pclk = htot / hp * 1e6;                                              /* Hz */
    hsync = (int)(0.08 * htot / 8 + 0.5) * 8; hfp = hblank / 2 - hsync; vss = h + 1;
    memset(&r, 0, sizeof r); r.x.ax = 0x4F0B; r.h.bl = 0; r.d.ecx = (unsigned long)pclk; r.x.dx = (unsigned short)mode; __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F || !r.d.ecx) return 0;
    clk = r.d.ecx;
    memset(blk, 0, 64);
    blk[0] = htot & 255; blk[1] = htot >> 8; blk[2] = (w + hfp) & 255; blk[3] = (w + hfp) >> 8;
    blk[4] = (w + hfp + hsync) & 255; blk[5] = (w + hfp + hsync) >> 8;
    blk[6] = vtot & 255; blk[7] = vtot >> 8; blk[8] = vss & 255; blk[9] = vss >> 8; blk[10] = (vss + 3) & 255; blk[11] = (vss + 3) >> 8;
    blk[12] = 0x04;                                                      /* hsync -, vsync + (GTF) */
    memcpy(blk + 13, &clk, 4);
    { unsigned rr = (unsigned)((double)clk * 100.0 / ((double)htot * vtot) + 0.5); blk[17] = rr & 255; blk[18] = rr >> 8; }
    return 1;
}
/* set one of m3_modes (hz > 0: that refresh rate, VBE 3.0); M3W/M3H follow it. 0 = the mode could not be set. */
static int m3_open_mode(const M3MODE* m, int hz)
{
    __dpmi_regs r; unsigned char mi[256];
    m3_lfb = 0; m3_refresh = 0; m3_refresh_seen = 0;
    if (m->mode == 0x13)
    {
        memset(&r, 0, sizeof r); r.x.ax = 0x0013; __dpmi_int(0x10, &r);
        if ((_farpeekb(_dos_ds, 0x449) & 0x7F) != 0x13) return 0;
        m3_mode = 0x13; m3_bpp = 8; m3_pitch = 320; m3_winseg = 0xA000; m3_gran = 64; m3_bank = 0;
    }
    else
    {
        memset(&r, 0, sizeof r); r.x.ax = 0x4F01; r.x.cx = (unsigned short)m->mode; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
        if (r.x.ax != 0x004F) return 0;
        dosmemget(__tb, 256, mi);
        m3_mode = m->mode; m3_bpp = mi[0x19]; m3_pitch = mi[0x10] | (mi[0x11] << 8);
        m3_rs = mi[0x1F]; m3_rp = mi[0x20]; m3_gs = mi[0x21]; m3_gp = mi[0x22]; m3_bs = mi[0x23]; m3_bp = mi[0x24];
        m3_winseg = mi[8] | (mi[9] << 8); if (!m3_winseg) m3_winseg = 0xA000; m3_gran = mi[4] | (mi[5] << 8); if (!m3_gran) m3_gran = 64;
        if (m->lfb)
        {
            unsigned long phys = mi[0x28] | (mi[0x29] << 8) | ((unsigned long)mi[0x2A] << 16) | ((unsigned long)mi[0x2B] << 24);
            if (m3_vbever >= 3 && (mi[0x32] | (mi[0x33] << 8))) m3_pitch = mi[0x32] | (mi[0x33] << 8);
            m3_map.address = phys; m3_map.size = (unsigned long)m3_pitch * m->h;
            if (phys && __dpmi_physical_address_mapping(&m3_map) == 0)
            {
                m3_lin = m3_map.address; m3_sel = __dpmi_allocate_ldt_descriptors(1);
                if (m3_sel > 0)
                {
                    __dpmi_set_segment_base_address(m3_sel, m3_lin);
                    __dpmi_set_segment_limit(m3_sel, ((unsigned long)m3_pitch * m->h - 1) | 0xFFF);
                    m3_lfb = 1;
                    wc_startup(M3_WHO, phys);                              /* write-combining (WCSET.CFG) */
                }
                else { __dpmi_free_physical_address_mapping(&m3_map); m3_sel = 0; }
            }
        }
        if (!m3_lfb && !m->win) return 0;
        r.x.ax = 0;
        if (hz > 0 && m3_vbever >= 3 && m->w >= 640 && m->h >= 480)
        {   /* the refresh rate through a CRTC block; if the BIOS refuses it, the same mode at its own rate */
            unsigned char blk[64];
            if (m3_gtf(m->mode, m->w, m->h, hz, blk))
            {
                dosmemput(blk, 64, __tb);
                memset(&r, 0, sizeof r); r.x.ax = 0x4F02; r.x.bx = (unsigned short)(m->mode | (m3_lfb ? 0x4000 : 0) | 0x0800);
                r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
                if (r.x.ax == 0x004F) m3_refresh = hz;
            }
        }
        if (r.x.ax != 0x004F)
        {
            memset(&r, 0, sizeof r); r.x.ax = 0x4F02; r.x.bx = (unsigned short)(m->mode | (m3_lfb ? 0x4000 : 0)); __dpmi_int(0x10, &r);
        }
        if (r.x.ax != 0x004F)
        {
            if (m3_lfb) { __dpmi_free_ldt_descriptor(m3_sel); __dpmi_free_physical_address_mapping(&m3_map); m3_lfb = 0; m3_sel = 0; }
            return 0;
        }
        m3_bank = -1;
    }
    m3_W = m->w; m3_H = m->h; m3_rw = m3_W; m3_rh = m3_H; m3_half = 0;
    m3_fyk = (m3_W * 4 == m3_H * 5) ? 1.0f : (float)(m3_H * 4) / (float)(m3_W * 3);   /* 4:3 screen (1280x1024: square pixels) */
    if (m3_bpp == 8) m3_pal252();
    return 1;
}
/* the refresh rate seen on the VGA status port (3DAh): two retrace starts; 0 when it does not look like one */
static double m3_measure_refresh(void)
{
    double t0, t1, tend; int i;
    tend = m3_now() + 0.1;
    while ((inportb(0x3DA) & 8) && m3_now() < tend) ;
    while (!(inportb(0x3DA) & 8) && m3_now() < tend) ;
    t0 = m3_now();
    for (i = 0; i < 4; i++)
    {
        while ((inportb(0x3DA) & 8) && m3_now() < tend + 0.2) M3_HOOK();
        while (!(inportb(0x3DA) & 8) && m3_now() < tend + 0.2) M3_HOOK();
    }
    t1 = m3_now();
    if (t1 >= tend + 0.2 || t1 - t0 < 4 / 250.0) return 0;
    return 4 / (t1 - t0);
}
#endif
static void m3_present(void)
{
    int x, y;
    for (y = 0; y < M3H; y++)
    {
        const uint32_t* s = m3_fb + y * M3W;
        if (!(y & 7)) M3_HOOK();
        if (m3_bpp == 32 && m3_rp == 16 && m3_gp == 8 && m3_bp == 0) { m3_put((unsigned long)y * m3_pitch, s, M3W * 4); continue; }
#ifdef M3_DYNAMIC_RES
        if (m3_bpp == 8)
        {   /* 252 colours (6 red x 7 green x 6 blue), ordered 4x4 dither */
            const unsigned char *tr = m3_dr[y & 3], *tg = m3_dg[y & 3], *tb = m3_db[y & 3];
            for (x = 0; x < M3W; x++) { uint32_t c = s[x]; int k = (x & 3) << 8; m3_line[x] = (unsigned char)(tr[k + (c >> 16 & 255)] + tg[k + (c >> 8 & 255)] + tb[k + (c & 255)]); }
            m3_put((unsigned long)y * m3_pitch, m3_line, M3W);
            continue;
        }
#endif
        if (m3_bpp == 32)
            for (x = 0; x < M3W; x++) { uint32_t c = s[x]; ((uint32_t*)m3_line)[x] = ((c >> 16 & 255) << m3_rp) | ((c >> 8 & 255) << m3_gp) | ((c & 255) << m3_bp); }
        else if (m3_bpp == 24)
            for (x = 0; x < M3W; x++) { uint32_t c = s[x]; m3_line[x * 3 + m3_bp / 8] = c & 255; m3_line[x * 3 + m3_gp / 8] = c >> 8 & 255; m3_line[x * 3 + m3_rp / 8] = c >> 16 & 255; }
        else
            for (x = 0; x < M3W; x++)
            {
                uint32_t c = s[x];
                ((uint16_t*)m3_line)[x] = (uint16_t)(((c >> 16 & 255) >> (8 - m3_rs) << m3_rp) | ((c >> 8 & 255) >> (8 - m3_gs) << m3_gp) | ((c & 255) >> (8 - m3_bs) << m3_bp));
            }
        m3_put((unsigned long)y * m3_pitch, m3_line, M3W * ((m3_bpp + 7) / 8));
    }
}

/* ------------------------------------------------------------------------------------------- time */
static unsigned long long m3_rdtsc(void) { unsigned long long t; asm volatile("rdtsc" : "=A"(t)); return t; }
static int m3_time_ticks = 3;      /* BIOS ticks to measure the TSC over; a Windows DOS box needs 18 (late, bursty ticks) */
static void m3_time_init(void)
{
    unsigned long t = m3_ticks(); unsigned long long a;
    while (m3_ticks() == t) ;
    t = m3_ticks(); a = m3_rdtsc();
    while (m3_ticks() < t + (unsigned long)m3_time_ticks) ;
    m3_tsc_us = (double)(m3_rdtsc() - a) / (m3_time_ticks * 54925.4);
    if (m3_tsc_us < 1) m3_tsc_us = 1;
    m3_t0 = m3_rdtsc();
}
static double m3_now(void) { return (double)(m3_rdtsc() - m3_t0) / (m3_tsc_us * 1e6); }

/* ------------------------------------------------------------------------------------------- 2D */
static inline uint32_t m3_rgb(int r, int g, int b) { return (uint32_t)((r < 0 ? 0 : r > 255 ? 255 : r) << 16 | (g < 0 ? 0 : g > 255 ? 255 : g) << 8 | (b < 0 ? 0 : b > 255 ? 255 : b)); }
static inline uint32_t m3_blend(uint32_t d, uint32_t s, int a)
{
    uint32_t rb = ((s & 0xFF00FF) * a + (d & 0xFF00FF) * (256 - a)) >> 8 & 0xFF00FF;
    uint32_t g = ((s & 0x00FF00) * a + (d & 0x00FF00) * (256 - a)) >> 8 & 0x00FF00;
    return rb | g;
}
static inline uint32_t m3_mul(uint32_t c, int l)          /* l: 256 = 1.0, up to 511 */
{
    int r = ((c >> 16 & 255) * l) >> 8, g = ((c >> 8 & 255) * l) >> 8, b = ((c & 255) * l) >> 8;
    return m3_rgb(r, g, b);
}
static uint32_t m3_lerpc(uint32_t a, uint32_t b, int t) { return m3_blend(a, b, t < 0 ? 0 : t > 256 ? 256 : t); }

static void m3_rect(M3SURF* s, int x, int y, int w, int h, uint32_t c, int a)
{
    int i, j;
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x + w > s->w) w = s->w - x; if (y + h > s->h) h = s->h - y;
    if (w <= 0 || h <= 0) return;
    for (j = y; j < y + h; j++)
    {
        uint32_t* p = s->p + j * s->w + x;
        if (a >= 256) for (i = 0; i < w; i++) p[i] = c; else for (i = 0; i < w; i++) p[i] = m3_blend(p[i], c, a);
    }
}
static void m3_vgrad(M3SURF* s, int x, int y, int w, int h, uint32_t c0, uint32_t c1, int a)
{
    int j;
    for (j = 0; j < h; j++) m3_rect(s, x, y + j, w, 1, m3_lerpc(c0, c1, h > 1 ? j * 256 / (h - 1) : 0), a);
}
/* raised (in = 0) or pressed (in = 1) bevel of thickness t */
static void m3_bevel(M3SURF* s, int x, int y, int w, int h, int t, int in, int a)
{
    uint32_t hi = in ? 0x000000 : 0xFFFFFF, lo = in ? 0xFFFFFF : 0x000000; int i;
    for (i = 0; i < t; i++)
    {
        m3_rect(s, x + i, y + i, w - 2 * i, 1, hi, a / (i + 1));
        m3_rect(s, x + i, y + i, 1, h - 2 * i, hi, a / (i + 1));
        m3_rect(s, x + i, y + h - 1 - i, w - 2 * i, 1, lo, a / (i + 1));
        m3_rect(s, x + w - 1 - i, y + i, 1, h - 2 * i, lo, a / (i + 1));
    }
}
static void m3_shadow(M3SURF* s, int x, int y, int w, int h, int d)
{
    int i; for (i = 1; i <= d; i++) m3_rect(s, x + i, y + i, w, h, 0, 40 / i + 8);
}

/* fonts: 0 = Terminus 8x16, 1 = 10x20, 2 = 12x24 bold, 3 = 16x32 bold */
static int m3_fw(int f) { return f == 0 ? 8 : f == 1 ? 10 : f == 2 ? 12 : 16; }
static int m3_fh(int f) { return f == 0 ? 16 : f == 1 ? 20 : f == 2 ? 24 : 32; }
static unsigned m3_glyph_row(int f, unsigned char ch, int row)          /* bits left-aligned in 32 */
{
    const TUI_GFONT* g;
    if (f == 0) { if (ch < 32 || ch > 126) return 0; return (unsigned)tui_ter16n[(ch - 32) * 16 + row] << 24; }
    g = f == 1 ? &tui_gfonts_n[1] : f == 2 ? &tui_gfonts_b[3] : &tui_gfonts_b[5];
    if (!(g->have[ch >> 3] >> (ch & 7) & 1)) return 0;
    {
        int bpr = (g->w + 7) / 8; const unsigned char* b = g->bits + ((size_t)ch * g->h + row) * bpr; unsigned v = 0; int i;
        for (i = 0; i < bpr; i++) v |= (unsigned)b[i] << (24 - 8 * i);
        return v;
    }
}
static int m3_text(M3SURF* s, int x, int y, int f, const char* t, uint32_t c, int a)
{
    /* the bigger fonts (10x20 and up) get their stair-steps smoothed: an empty pixel in the inside corner of a step
     * (a set pixel beside it AND one above or below it) is filled at ~40%, so curves and diagonals look cleaner
     * without blurring the straight strokes. Font 0 (8x16) stays crisp: at that size it would look smudged. */
    int w = m3_fw(f), h = m3_fh(f), i, r, aa = f >= 1, a2 = a * 100 / 256;
    unsigned wm = w >= 32 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> w);
    for (; *t; t++, x += w)
    {
        unsigned prev = 0, cur, next;
        if (x >= s->w) break;
        if (x + w <= 0 || *t == ' ') continue;
        cur = m3_glyph_row(f, (unsigned char)*t, 0);
        for (r = 0; r < h; r++, prev = cur, cur = next)
        {
            unsigned bits = cur, soft; int yy = y + r;
            next = r + 1 < h ? m3_glyph_row(f, (unsigned char)*t, r + 1) : 0;
            soft = aa ? ~bits & ((bits >> 1) | (bits << 1)) & (prev | next) & wm : 0;
            if (!(bits | soft) || yy < 0 || yy >= s->h) continue;
            for (i = 0; i < w; i++)
            {
                int xx = x + i; uint32_t* p;
                if (xx < 0 || xx >= s->w) continue;
                p = s->p + yy * s->w + xx;
                if (bits >> (31 - i) & 1) *p = a >= 256 ? c : m3_blend(*p, c, a);
                else if (soft >> (31 - i) & 1) *p = m3_blend(*p, c, a2);
            }
        }
    }
    return x;
}
/* 90s neon: the text over a soft halo of its glow colour (a ring of faint copies), then a crisp shadowless top */
static int m3_text(M3SURF* s, int x, int y, int f, const char* t, uint32_t c, int a);
static void m3_text_glow(M3SURF* s, int x, int y, int f, const char* t, uint32_t c, uint32_t glow)
{
    static const signed char o[12][2] = { {-2,0},{2,0},{0,-2},{0,2},{-1,-1},{1,-1},{-1,1},{1,1},{-3,0},{3,0},{0,-3},{0,3} };
    int i; for (i = 0; i < 12; i++) m3_text(s, x + o[i][0], y + o[i][1], f, t, glow, i < 8 ? 48 : 22);
    m3_text(s, x, y, f, t, c, 256);
}
static void m3_text_sh(M3SURF* s, int x, int y, int f, const char* t, uint32_t c)
{ m3_text(s, x + 2, y + 2, f, t, 0, 140); m3_text(s, x, y, f, t, c, 256); }
static void m3_text_c(M3SURF* s, int cx, int y, int f, const char* t, uint32_t c, int sh)
{ int x = cx - (int)strlen(t) * m3_fw(f) / 2; if (sh) m3_text_sh(s, x, y, f, t, c); else m3_text(s, x, y, f, t, c, 256); }
/* word wrap into at most maxl lines of w pixels; returns the number of lines drawn */
static int m3_wrap(M3SURF* s, int x, int y, int w, int maxl, int f, const char* t, uint32_t c, int sh)
{
    int cols = w / m3_fw(f), n = 0; char line[128];
    if (cols > 127) cols = 127;
    while (*t && n < maxl)
    {
        int len = (int)strlen(t), cut = len;
        while (*t == ' ') { t++; len--; }
        if (len > cols) { cut = cols; while (cut > 0 && t[cut] != ' ') cut--; if (cut == 0) cut = cols; }
        else cut = len;
        if (n == maxl - 1 && len > cut) { if (cut > 3) { memcpy(line, t, cut - 3); strcpy(line + cut - 3, "..."); } else snprintf(line, sizeof line, "..."); }
        else { memcpy(line, t, cut); line[cut] = 0; }
        if (sh) m3_text_sh(s, x, y + n * (m3_fh(f) + 2), f, line, c); else m3_text(s, x, y + n * (m3_fh(f) + 2), f, line, c, 256);
        t += cut; n++;
    }
    return n;
}

/* ------------------------------------------------------------------------------------------- 3D */
typedef struct { float x, y, z; } M3V;
typedef struct { float x, y, z, u, v, l; } M3P;          /* camera space + texture + light (1 = normal) */
static M3V m3_cam; static float m3_yaw, m3_campitch, m3_cy_, m3_sy_, m3_cp_, m3_sp_;
static const float m3_f = 520.0f;                       /* focal length in pixels */
static uint32_t m3_fog = 0x000010; static float m3_fog0 = 1e9f, m3_fog1 = 2e9f;

static void m3_camera(float x, float y, float z, float yaw, float pitch)
{ m3_cam.x = x; m3_cam.y = y; m3_cam.z = z; m3_yaw = yaw; m3_campitch = pitch; m3_cy_ = cosf(yaw); m3_sy_ = sinf(yaw); m3_cp_ = cosf(pitch); m3_sp_ = sinf(pitch); }
static M3P m3_xf(M3V w, float u, float v, float l)
{
    M3P p; float x = w.x - m3_cam.x, y = w.y - m3_cam.y, z = w.z - m3_cam.z, x1, z1;
    x1 = x * m3_cy_ - z * m3_sy_; z1 = x * m3_sy_ + z * m3_cy_;
    p.x = x1; p.y = y * m3_cp_ - z1 * m3_sp_; p.z = y * m3_sp_ + z1 * m3_cp_;
    p.u = u; p.v = v; p.l = l;
    return p;
}

typedef struct { float sx, sy, iz, uz, vz, lz; } M3S;
static int m3_texwrap;                     /* the texture repeats (the floor) instead of being clamped */
/* float -> int clamped to +-lim (also NaN): a plain (int) of a value out of range is undefined in C (x86: 80000000h, and
   the 16.16 steps made from it then overflow). Points close to the camera give such values. */
static inline int m3_fi(float v, float lim) { if (!(v > -lim)) return -(int)lim; if (v > lim) return (int)lim; return (int)v; }
#define M3_FXL 536870912.0f        /* 2^29: 16.16 values and their differences stay inside an int */
static void m3_tri_raster(const M3S* a0, const M3S* b0, const M3S* c0, const M3SURF* tex, uint32_t col, int alpha, int zwrite)
{
    const M3S *a = a0, *b = b0, *c = c0, *t;
    int y, ys, ye;
    if (a->sy > b->sy) { t = a; a = b; b = t; }
    if (b->sy > c->sy) { t = b; b = c; c = t; }
    if (a->sy > b->sy) { t = a; a = b; b = t; }
    if (c->sy - a->sy < 1e-4f) return;
    ys = m3_fi(ceilf(a->sy), 65536.0f); ye = m3_fi(ceilf(c->sy), 65536.0f);
    if (ys < 0) ys = 0; if (ye > m3_rh) ye = m3_rh;
    for (y = ys; y < ye; y++)
    {
        float fy = (float)y, tl = (fy - a->sy) / (c->sy - a->sy), ts;
        M3S L, R; const M3S *p, *q; int x, xs, xe; float dx, iz, uz, vz, lz, diz, duz, dvz, dlz;
        if (!(y & 7)) M3_HOOK();
        L.sx = a->sx + (c->sx - a->sx) * tl; L.iz = a->iz + (c->iz - a->iz) * tl; L.uz = a->uz + (c->uz - a->uz) * tl;
        L.vz = a->vz + (c->vz - a->vz) * tl; L.lz = a->lz + (c->lz - a->lz) * tl;
        if (fy < b->sy) { p = a; q = b; } else { p = b; q = c; }
        ts = q->sy - p->sy > 1e-6f ? (fy - p->sy) / (q->sy - p->sy) : 0;
        R.sx = p->sx + (q->sx - p->sx) * ts; R.iz = p->iz + (q->iz - p->iz) * ts; R.uz = p->uz + (q->uz - p->uz) * ts;
        R.vz = p->vz + (q->vz - p->vz) * ts; R.lz = p->lz + (q->lz - p->lz) * ts;
        if (L.sx > R.sx) { M3S tt = L; L = R; R = tt; }
        dx = R.sx - L.sx; if (dx < 1e-6f) continue;
        diz = (R.iz - L.iz) / dx; duz = (R.uz - L.uz) / dx; dvz = (R.vz - L.vz) / dx; dlz = (R.lz - L.lz) / dx;
        xs = m3_fi(ceilf(L.sx), 65536.0f); xe = m3_fi(ceilf(R.sx), 65536.0f);
        if (xs < 0) xs = 0; if (xe > m3_rw) xe = m3_rw;
        { float pre = (float)xs - L.sx; iz = L.iz + diz * pre; uz = L.uz + duz * pre; vz = L.vz + dvz * pre; lz = L.lz + dlz * pre; }
        /* perspective-correct every 16 pixels, linear in between: one divide per 16 pixels, and the pixels
         * stepped in 16.16 fixed point (float-to-int conversions are slow on Pentium-class FPUs) */
        x = xs;
        {
            float fk = m3_fog1 > m3_fog0 ? 256.0f / (m3_fog1 - m3_fog0) : 0;
            int tw = tex ? tex->w : 1, th = tex ? tex->h : 1;
            while (x < xe)
            {
                int n = xe - x > 16 ? 16 : xe - x, j;
                float z0 = 1.0f / iz, iz1 = iz + diz * n, z1 = 1.0f / iz1;
                float f0 = (z0 - m3_fog0) * fk, f1 = (z1 - m3_fog0) * fk;
                int U = m3_fi(uz * z0 * tw * 65536.0f, M3_FXL), V = m3_fi(vz * z0 * th * 65536.0f, M3_FXL), Lq = m3_fi(lz * z0 * 256.0f * 65536.0f, M3_FXL);
                int U1 = m3_fi((uz + duz * n) * z1 * tw * 65536.0f, M3_FXL), V1 = m3_fi((vz + dvz * n) * z1 * th * 65536.0f, M3_FXL);
                int L1 = m3_fi((lz + dlz * n) * z1 * 256.0f * 65536.0f, M3_FXL);
                int F = m3_fi(f0 * 65536.0f, M3_FXL), F1 = m3_fi(f1 * 65536.0f, M3_FXL);
                int dU = (U1 - U) / n, dV = (V1 - V) / n, dL = (L1 - Lq) / n, dF = (F1 - F) / n;
                float* zp = m3_zb + y * M3W + x;
                uint32_t* fp = m3_fb + y * M3W + x;
                int fog = f0 > 0 || f1 > 0;
                for (j = 0; j < n; j++, zp++, fp++, iz += diz, U += dU, V += dV, Lq += dL, F += dF)
                {
                    uint32_t px; int li;
                    if (iz <= *zp) continue;
                    li = Lq >> 16; if (li < 0) li = 0; if (li > 511) li = 511;
                    if (tex)
                    {
                        int tu = U >> 16, tv = V >> 16;
                        if (m3_texwrap) { tu &= tw - 1; tv &= th - 1; }   /* tiled (power-of-2 texture) */
                        else { tu = tu < 0 ? 0 : tu >= tw ? tw - 1 : tu; tv = tv < 0 ? 0 : tv >= th ? th - 1 : tv; }
                        px = tex->p[tv * tw + tu];
                        if (li != 256) px = m3_mul(px, li);
                    }
                    else px = li == 256 ? col : m3_mul(col, li);
                    if (fog && F > 0) px = m3_lerpc(px, m3_fog, F >> 16);
                    if (alpha >= 256) *fp = px; else *fp = m3_blend(*fp, px, alpha);
                    if (zwrite) *zp = iz;
                }
                x += n; uz += duz * n; vz += dvz * n; lz += dlz * n;
            }
        }
    }
}
/* a convex polygon (3 or 4 points, camera space), clipped at the near plane */
static void m3_poly(const M3P* in, int n, const M3SURF* tex, uint32_t col, int alpha)
{
    M3P o[8]; M3S s[8]; int m = 0, i; const float zn = 0.15f;
    for (i = 0; i < n; i++)
    {
        const M3P *a = &in[i], *b = &in[(i + 1) % n];
        if (a->z >= zn) o[m++] = *a;
        if ((a->z >= zn) != (b->z >= zn))
        {
            float t = (zn - a->z) / (b->z - a->z); M3P p;
            p.x = a->x + (b->x - a->x) * t; p.y = a->y + (b->y - a->y) * t; p.z = zn;
            p.u = a->u + (b->u - a->u) * t; p.v = a->v + (b->v - a->v) * t; p.l = a->l + (b->l - a->l) * t;
            o[m++] = p;
        }
    }
    if (m < 3) return;
    for (i = 0; i < m; i++)
    {
        float iz = 1.0f / o[i].z;
        float f = m3_f * m3_rw / M3_FBASE;
        s[i].sx = m3_rw / 2 + o[i].x * f * iz; s[i].sy = m3_rh / 2 - o[i].y * f * m3_fyk * iz;
        s[i].iz = iz; s[i].uz = o[i].u * iz; s[i].vz = o[i].v * iz; s[i].lz = o[i].l * iz;
    }
    for (i = 1; i + 1 < m; i++) m3_tri_raster(&s[0], &s[i], &s[i + 1], tex, col, alpha, alpha >= 256);
}
static M3V m3_v(float x, float y, float z) { M3V v; v.x = x; v.y = y; v.z = z; return v; }
static float m3_light(M3V a, M3V b, M3V c)              /* flat light from a fixed sun, face normal */
{
    M3V u = m3_v(b.x - a.x, b.y - a.y, b.z - a.z), w = m3_v(c.x - a.x, c.y - a.y, c.z - a.z), nn;
    float len, d;
    nn.x = u.y * w.z - u.z * w.y; nn.y = u.z * w.x - u.x * w.z; nn.z = u.x * w.y - u.y * w.x;
    len = sqrtf(nn.x * nn.x + nn.y * nn.y + nn.z * nn.z); if (len < 1e-6f) return 1;
    d = (nn.x * -0.35f + nn.y * 0.75f + nn.z * -0.56f) / len;
    return 0.45f + 0.75f * (d > 0 ? d : 0);
}
/* world quad a b c d (clockwise seen from the front), texture corners (0,0) (1,0) (1,1) (0,1) */
static void m3_quad(M3V a, M3V b, M3V c, M3V d, const M3SURF* tex, uint32_t col, float glow, int alpha, int cull)
{
    M3P p[4]; float l = m3_light(a, b, c) + glow;
    p[0] = m3_xf(a, 0, 0, l); p[1] = m3_xf(b, 1, 0, l); p[2] = m3_xf(c, 1, 1, l); p[3] = m3_xf(d, 0, 1, l);
    if (cull)
    {   /* back face: screen winding */
        float ax = p[0].x / (p[0].z > .01f ? p[0].z : .01f), ay = p[0].y / (p[0].z > .01f ? p[0].z : .01f);
        float bx = p[1].x / (p[1].z > .01f ? p[1].z : .01f), by = p[1].y / (p[1].z > .01f ? p[1].z : .01f);
        float cx = p[2].x / (p[2].z > .01f ? p[2].z : .01f), cy = p[2].y / (p[2].z > .01f ? p[2].z : .01f);
        if ((bx - ax) * (cy - ay) - (by - ay) * (cx - ax) > 0) return;
    }
    m3_poly(p, 4, tex, col, alpha);
}
/* a box: centre, half sizes, turned by yaw about Y; front (-Z side) textured */
static uint32_t m3_boxback;   /* colour of the next boxes' back face (0 = darker body colour) */
static void m3_box(M3V c, float hx, float hy, float hz, float yaw, const M3SURF* front, uint32_t col, float glow, int alpha)
{
    uint32_t back = m3_boxback;
    M3V v[8]; int i; float cy = cosf(yaw), sy = sinf(yaw);
    static const int k[8][3] = { {-1,1,-1},{1,1,-1},{1,-1,-1},{-1,-1,-1},{-1,1,1},{1,1,1},{1,-1,1},{-1,-1,1} };
    for (i = 0; i < 8; i++)
    {
        float x = k[i][0] * hx, y = k[i][1] * hy, z = k[i][2] * hz;
        v[i] = m3_v(c.x + x * cy + z * sy, c.y + y, c.z - x * sy + z * cy);
    }
    m3_quad(v[0], v[1], v[2], v[3], front, col, glow, alpha, 1);          /* front */
    m3_quad(v[5], v[4], v[7], v[6], NULL, back ? back : m3_mul(col, 180), glow, alpha, 1); /* back */
    m3_quad(v[4], v[5], v[1], v[0], NULL, col, glow, alpha, 1);            /* top */
    m3_quad(v[3], v[2], v[6], v[7], NULL, m3_mul(col, 140), glow, alpha, 1); /* bottom */
    m3_quad(v[4], v[0], v[3], v[7], NULL, m3_mul(col, 200), glow, alpha, 1); /* left */
    m3_quad(v[1], v[5], v[6], v[2], NULL, m3_mul(col, 200), glow, alpha, 1); /* right */
}
/* a cylinder along Z (a knob): centre of its front face, radius, depth, turned by rot */
static void m3_knob3d(M3V c, float r, float d, float rot, float press)
{
    int i, n = 28; M3V fc = m3_v(c.x, c.y, c.z + press);
    for (i = 0; i < n; i++)
    {
        float a0 = rot + i * 6.2831853f / n, a1 = rot + (i + 1) * 6.2831853f / n;
        M3V p0 = m3_v(fc.x + r * cosf(a0), fc.y + r * sinf(a0), fc.z), p1 = m3_v(fc.x + r * cosf(a1), fc.y + r * sinf(a1), fc.z);
        M3V q0 = m3_v(p0.x, p0.y, fc.z + d), q1 = m3_v(p1.x, p1.y, fc.z + d);
        uint32_t ridge = (i & 1) ? 0x2A2A30 : 0x3A3A42;
        /* machined look: a knurled rim and a cap shaded by its angle to a light at the top left */
        float am = (a0 + a1) * 0.5f - rot, sh = cosf(am + rot - 2.3f);
        int lv = 0x58 + (int)(0x22 * sh), rv = (i & 1) ? 0x30 : 0x44;
        M3V m0 = m3_v(fc.x + r * 0.78f * cosf(a0), fc.y + r * 0.78f * sinf(a0), fc.z - 0.001f), m1 = m3_v(fc.x + r * 0.78f * cosf(a1), fc.y + r * 0.78f * sinf(a1), fc.z - 0.001f);
        M3P t[3];
        (void)am;
        m3_quad(p1, p0, q0, q1, NULL, ridge, 0, 256, 1);
        m3_quad(m1, m0, p0, p1, NULL, (uint32_t)(rv << 16 | rv << 8 | (rv + 6)), 0, 256, 0);       /* knurled rim */
        t[0] = m3_xf(m3_v(fc.x, fc.y, fc.z - 0.001f), 0, 0, 1.1f); t[1] = m3_xf(m0, 0, 0, 1.1f); t[2] = m3_xf(m1, 0, 0, 1.1f);
        m3_poly(t, 3, NULL, (uint32_t)(lv << 16 | lv << 8 | (lv + 8)), 256);                         /* the cap */
    }
    {   /* the pointer: a bright line from near the centre to the rim */
        float ca = cosf(rot), sa = sinf(rot), px = -sa * r * 0.06f, py = ca * r * 0.06f, z = fc.z - 0.003f;
        M3V a = m3_v(fc.x + ca * r * 0.22f - px, fc.y + sa * r * 0.22f - py, z), b = m3_v(fc.x + ca * r * 0.92f - px, fc.y + sa * r * 0.92f - py, z),
            c2 = m3_v(fc.x + ca * r * 0.92f + px, fc.y + sa * r * 0.92f + py, z), d2 = m3_v(fc.x + ca * r * 0.22f + px, fc.y + sa * r * 0.22f + py, z);
        m3_quad(a, b, c2, d2, NULL, 0xFF8020, 0.3f, 256, 0);
    }
}

static M3SURF m3_floor, m3_floor_solid;   /* floor_solid: the floor already blended over the sky */
static M3SURF m3_surf(int w, int h) { M3SURF s; s.w = w; s.h = h; s.p = (uint32_t*)calloc((size_t)w * h, 4); return s; }

/* the scene's colours: the defaults are the original look; m3_theme_from_pal() takes them from a text style's
 * 16-colour palette (roles: 1 desk, 5 highlight, 8 dim, 9-14 bright accents, 11 cyan-ish, 13 magenta-ish) */
static uint32_t m3_sky0 = 0x02030C, m3_sky1 = 0x1A1040, m3_gridc = 0x2088C0, m3_tile0 = 0x0A0E1C, m3_tile1 = 0x0C1224,
                m3_floorbg = 0x0E0A2C, m3_fogc = 0x0A0820, m3_glowc = 0x40E0FF, m3_accent[6] =
                { 0x2E7DFF, 0xB050FF, 0x20C080, 0xFF6040, 0xE0B020, 0x40B8D8 };
static int m3_theme_gen;                                  /* counts changes: screens rebuild their textures */
static uint32_t m3_prgb(const unsigned char (*pal)[3], int i) { return (uint32_t)pal[i][0] << 16 | pal[i][1] << 8 | pal[i][2]; }

static void m3_make_floor(void)
{
    int x, y;
    static uint32_t px[128 * 128], px2[128 * 128];            /* static: never out of memory */
    if (!m3_floor.p) { m3_floor.w = m3_floor.h = 128; m3_floor.p = px; }
    if (!m3_floor_solid.p) { m3_floor_solid.w = m3_floor_solid.h = 128; m3_floor_solid.p = px2; }
    for (y = 0; y < 128; y++) for (x = 0; x < 128; x++)
    {
        int l = (x % 32 == 0 || y % 32 == 0) ? 1 : 0, l2 = (x % 32 == 1 || y % 32 == 1 || x % 32 == 31 || y % 32 == 31);
        uint32_t t = (x / 32 + y / 32) & 1 ? m3_tile0 : m3_tile1;
        m3_floor.p[y * 128 + x] = l ? m3_gridc : l2 ? m3_blend(t, m3_gridc, 70) : t;   /* lines with a soft glow edge */
    }
    for (x = 0; x < 128 * 128; x++) m3_floor_solid.p[x] = m3_blend(m3_floorbg, m3_floor.p[x], 160);   /* over the sky's lower part */
}
static void m3_theme_from_pal(const unsigned char (*pal)[3])
{
    uint32_t desk = m3_prgb(pal, 1), hi = m3_prgb(pal, 5);
    int i; static const int acc[6] = { 9, 13, 10, 12, 14, 11 };
    m3_sky0 = m3_mul(desk, 40); m3_sky1 = m3_mul(m3_lerpc(desk, hi, 110), 150);
    m3_gridc = m3_prgb(pal, 11); m3_glowc = m3_prgb(pal, 11);
    m3_tile0 = m3_mul(desk, 70); m3_tile1 = m3_mul(desk, 95);
    m3_floorbg = m3_mul(m3_lerpc(desk, hi, 80), 120); m3_fogc = m3_mul(m3_lerpc(desk, hi, 110), 110);
    for (i = 0; i < 6; i++) m3_accent[i] = m3_prgb(pal, acc[i]);
    m3_make_floor(); m3_theme_gen++;
}
#ifdef TUI_INCLUDED
/* follow the text screens' colour style (F3 there; F6 in the 3D menu): re-colours when it changed */
static void m3_theme_sync(void)
{
    static int last = -1;
    if (tui_theme != last || !m3_floor.p) { last = tui_theme; m3_theme_from_pal(tui_palette()); }
}
#else
static void m3_theme_sync(void) { if (!m3_floor.p) m3_make_floor(); }
#endif

static void m3_upscale(void)
{
    int x, y;
    if (!m3_half) return;
    for (y = m3_rh - 1; y >= 0; y--)
    {
        const uint32_t* s = m3_fb + y * M3W;
        if (!(y & 15)) M3_HOOK();
        uint32_t* d0 = m3_fb + (2 * y) * M3W; uint32_t* d1 = d0 + M3W;
        for (x = m3_rw - 1; x >= 0; x--) { uint32_t c = s[x]; d1[2 * x] = d1[2 * x + 1] = c; d0[2 * x + 1] = c; d0[2 * x] = c; }
    }
}

static float m3_bgphase;

static void m3_sky(void)
{
    int y, i;
    for (y = 0; y < m3_rh; y++)
    {
        uint32_t c = m3_lerpc(m3_sky0, m3_sky1, y * 256 / m3_rh);
        if (!(y & 15)) M3_HOOK();
        uint32_t* p = m3_fb + y * M3W; for (i = 0; i < m3_rw; i++) p[i] = c;
        memset(m3_zb + y * M3W, 0, m3_rw * sizeof(float));
    }
    for (i = 0; i < 90; i++)
    {   /* drifting stars */
        unsigned h = (unsigned)i * 2654435761u; int x = (int)((h % M3W + (int)(m3_bgphase * (4 + h % 7))) % M3W), yy = (int)(h / M3W % (M3H / 2));
        if (m3_half) { x /= 2; yy /= 2; }
        m3_fb[yy * M3W + x] = m3_lerpc(0x404060, 0xFFFFFF, 80 + (int)(h % 170));
    }
}

/* The opaque floor drawn row by row: the camera only turns (yaw) and tilts (pitch), never rolls, so every
 * screen row of a flat floor lies at ONE depth. Per row: one divide, one fog value, and u/v stepping linearly
 * (exact, no perspective error), instead of a perspective divide every 16 pixels and per-pixel fog. Same picture
 * as the 4 textured quads (tile = 2 units, 128-pixel texture, -12..12 in x and z), about 3x faster. */
static void m3_floor_rows(float fy, int alpha)          /* alpha < 256: the see-through mirror floor (no z write) */
{
    const M3SURF* tex = alpha >= 256 ? &m3_floor_solid : &m3_floor;
    float f = m3_f * m3_rw / M3_FBASE, hgt = fy - m3_cam.y, cx = m3_rw / 2.0f, cyy = m3_rh / 2.0f;
    float fk = m3_fog1 > m3_fog0 ? 256.0f / (m3_fog1 - m3_fog0) : 0;
    int y;
    if (hgt >= 0) return;                                   /* camera below the floor: nothing to see */
    for (y = 0; y < m3_rh; y++)
    {
        float dy = (cyy - (float)y) / (f * m3_fyk), wy = dy * m3_cp_ + m3_sp_, t, z1, ax, bx, az, bz, lo, hi, iz;
        int xs, xe, x, U, V, dU, dV, F;
        uint32_t* fp; float* zp;
        if (!(y & 7)) M3_HOOK();
        if (wy >= -1e-6f) continue;                         /* at or above the horizon */
        t = hgt / wy;                                       /* camera-space depth of this row */
        if (t < 0.15f) continue;                            /* the near plane, as m3_poly */
        z1 = -dy * m3_sp_ + m3_cp_;
        /* world x and z along the row, linear in the pixel's dx = (x - cx) / f */
        ax = m3_cam.x + z1 * m3_sy_ * t; bx = m3_cy_ * t;   /* x_w = ax + bx * dx */
        az = m3_cam.z + z1 * m3_cy_ * t; bz = -m3_sy_ * t;  /* z_w = az + bz * dx */
        lo = -1e30f; hi = 1e30f;                            /* dx range with -12 <= x_w, z_w < 12 */
        #define M3_CLIP(a, b) do { if ((b) > 1e-9f) { float l_ = (-12.0f - (a)) / (b), h_ = (12.0f - (a)) / (b); if (l_ > lo) lo = l_; if (h_ < hi) hi = h_; } \
                                   else if ((b) < -1e-9f) { float l_ = (12.0f - (a)) / (b), h_ = (-12.0f - (a)) / (b); if (l_ > lo) lo = l_; if (h_ < hi) hi = h_; } \
                                   else if ((a) < -12.0f || (a) >= 12.0f) { lo = 1; hi = 0; } } while (0)
        M3_CLIP(ax, bx); M3_CLIP(az, bz);
        #undef M3_CLIP
        if (lo >= hi) continue;
        { float sl = cx + lo * f, sh = cx + hi * f;
          xs = sl < 0 ? 0 : (int)ceilf(sl); xe = sh > (float)m3_rw ? m3_rw : (int)ceilf(sh); }
        if (xs >= xe) continue;
        {   /* texture: u = x_w / 2 tiles, v = -z_w / 2 tiles; 16.16 texels, wrapped */
            float dx0 = ((float)xs - cx) / f, du = bx / f * 64.0f * 65536.0f, dv = -bz / f * 64.0f * 65536.0f;
            double u0 = (ax + bx * dx0) * 64.0 * 65536.0, v0 = -(az + bz * dx0) * 64.0 * 65536.0;
            u0 = fmod(u0, 128.0 * 65536.0); if (u0 < 0) u0 += 128.0 * 65536.0;   /* keep 16.16 in range */
            v0 = fmod(v0, 128.0 * 65536.0); if (v0 < 0) v0 += 128.0 * 65536.0;
            U = (int)u0; V = (int)v0; dU = (int)du; dV = (int)dv;
        }
        F = (int)((t - m3_fog0) * fk); if (F > 256) F = 256;
        iz = 1.0f / t;
        fp = m3_fb + y * M3W + xs; zp = m3_zb + y * M3W + xs;
        if (alpha < 256)
            for (x = xs; x < xe; x++, fp++, zp++, U += dU, V += dV)
            { if (iz > *zp) { uint32_t px = tex->p[((V >> 16) & 127) * 128 + ((U >> 16) & 127)]; if (F > 0) px = m3_lerpc(px, m3_fog, F); *fp = m3_blend(*fp, px, alpha); } }
        else if (F > 0)
            for (x = xs; x < xe; x++, fp++, zp++, U += dU, V += dV)
            { if (iz > *zp) { *fp = m3_lerpc(tex->p[((V >> 16) & 127) * 128 + ((U >> 16) & 127)], m3_fog, F); *zp = iz; } }
        else
            for (x = xs; x < xe; x++, fp++, zp++, U += dU, V += dV)
            { if (iz > *zp) { *fp = tex->p[((V >> 16) & 127) * 128 + ((U >> 16) & 127)]; *zp = iz; } }
    }
}

static void m3_draw_floor(float y, int alpha)            /* alpha 256: opaque (faster), less: the mirror shows */
{
    int i, j; float s = 2.0f;
    if (m3_floor_solid.w == 128 && m3_floor_solid.h == 128 && m3_floor.w == 128 && m3_floor.h == 128) { m3_floor_rows(y, alpha); return; }
    /* 4 big quads with the tile repeated 6 times each (was 144 small ones: too slow on Pentium-class PCs) */
    m3_texwrap = 1;
    for (j = -1; j < 1; j++) for (i = -1; i < 1; i++)
    {
        float S = 6 * s;
        M3V a = m3_v(i * S, y, (j + 1) * S), b = m3_v((i + 1) * S, y, (j + 1) * S), c = m3_v((i + 1) * S, y, j * S), d = m3_v(i * S, y, j * S);
        M3P p[4];
        p[0] = m3_xf(a, 0, 0, 1); p[1] = m3_xf(b, 6, 0, 1); p[2] = m3_xf(c, 6, 6, 1); p[3] = m3_xf(d, 0, 6, 1);
        m3_poly(p, 4, alpha >= 256 ? &m3_floor_solid : &m3_floor, 0, alpha);
    }
    m3_texwrap = 0;
}

#endif
