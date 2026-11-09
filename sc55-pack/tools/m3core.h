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
static unsigned long m3_ticks(void) { return _farpeekl(_dos_ds, 0x46C); }   /* BIOS clock, 18.2 a second */

#define M3W 640
#define M3H 480
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
static unsigned char m3_line[M3W * 4];
static double    m3_tsc_us = 0;     /* TSC ticks per microsecond */
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

static int m3_vbe_open(void)
{
    __dpmi_regs r; unsigned char vi[512], mi[256]; unsigned short modes[256]; int n = 0, i, best = -1, bestscore = 0;
    unsigned long lp;
    memset(&r, 0, sizeof r); dosmemput("VBE2", 4, __tb);
    r.x.ax = 0x4F00; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) return 0;
    dosmemget(__tb, 512, vi);
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
        if ((mi[0x12] | (mi[0x13] << 8)) != M3W || (mi[0x14] | (mi[0x15] << 8)) != M3H) continue;
        if (mi[0x1B] != 6) continue;                                   /* direct colour */
        score = bpp == 32 ? 40 : bpp == 16 ? 30 : bpp == 15 ? 20 : bpp == 24 ? 10 : 0;
        if (!score) continue;
        if ((attr & 0x80) && (vi[5] >= 2)) score += 5;                  /* linear frame buffer */
        if (score > bestscore) { bestscore = score; best = modes[i]; }
    }
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
            }
            else { __dpmi_free_physical_address_mapping(&m3_map); m3_sel = 0; }
        }
    }
    if (!m3_lfb && !(mi[2] & 4)) return 0;                             /* window A not writable */
    memset(&r, 0, sizeof r); r.x.ax = 0x4F02; r.x.bx = (unsigned short)(best | (m3_lfb ? 0x4000 : 0)); __dpmi_int(0x10, &r);
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

static void m3_present(void)
{
    int x, y;
    for (y = 0; y < M3H; y++)
    {
        const uint32_t* s = m3_fb + y * M3W;
        if (m3_bpp == 32 && m3_rp == 16 && m3_gp == 8 && m3_bp == 0) { m3_put((unsigned long)y * m3_pitch, s, M3W * 4); continue; }
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
static void m3_time_init(void)
{
    unsigned long t = m3_ticks(); unsigned long long a;
    while (m3_ticks() == t) ;
    t = m3_ticks(); a = m3_rdtsc();
    while (m3_ticks() < t + 3) ;
    m3_tsc_us = (double)(m3_rdtsc() - a) / (3 * 54925.4);
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
    int w = m3_fw(f), h = m3_fh(f), i, r;
    for (; *t; t++, x += w)
    {
        if (x >= s->w) break;
        if (x + w <= 0 || *t == ' ') continue;
        for (r = 0; r < h; r++)
        {
            unsigned bits = m3_glyph_row(f, (unsigned char)*t, r); int yy = y + r;
            if (!bits || yy < 0 || yy >= s->h) continue;
            for (i = 0; i < w; i++)
                if (bits >> (31 - i) & 1) { int xx = x + i; if (xx >= 0 && xx < s->w) { uint32_t* p = s->p + yy * s->w + xx; *p = a >= 256 ? c : m3_blend(*p, c, a); } }
        }
    }
    return x;
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
static void m3_tri_raster(const M3S* a0, const M3S* b0, const M3S* c0, const M3SURF* tex, uint32_t col, int alpha, int zwrite)
{
    const M3S *a = a0, *b = b0, *c = c0, *t;
    int y, ys, ye;
    if (a->sy > b->sy) { t = a; a = b; b = t; }
    if (b->sy > c->sy) { t = b; b = c; c = t; }
    if (a->sy > b->sy) { t = a; a = b; b = t; }
    if (c->sy - a->sy < 1e-4f) return;
    ys = (int)ceilf(a->sy); ye = (int)ceilf(c->sy);
    if (ys < 0) ys = 0; if (ye > m3_rh) ye = m3_rh;
    for (y = ys; y < ye; y++)
    {
        float fy = (float)y, tl = (fy - a->sy) / (c->sy - a->sy), ts;
        M3S L, R; const M3S *p, *q; int x, xs, xe; float dx, iz, uz, vz, lz, diz, duz, dvz, dlz;
        L.sx = a->sx + (c->sx - a->sx) * tl; L.iz = a->iz + (c->iz - a->iz) * tl; L.uz = a->uz + (c->uz - a->uz) * tl;
        L.vz = a->vz + (c->vz - a->vz) * tl; L.lz = a->lz + (c->lz - a->lz) * tl;
        if (fy < b->sy) { p = a; q = b; } else { p = b; q = c; }
        ts = q->sy - p->sy > 1e-6f ? (fy - p->sy) / (q->sy - p->sy) : 0;
        R.sx = p->sx + (q->sx - p->sx) * ts; R.iz = p->iz + (q->iz - p->iz) * ts; R.uz = p->uz + (q->uz - p->uz) * ts;
        R.vz = p->vz + (q->vz - p->vz) * ts; R.lz = p->lz + (q->lz - p->lz) * ts;
        if (L.sx > R.sx) { M3S tt = L; L = R; R = tt; }
        dx = R.sx - L.sx; if (dx < 1e-6f) continue;
        diz = (R.iz - L.iz) / dx; duz = (R.uz - L.uz) / dx; dvz = (R.vz - L.vz) / dx; dlz = (R.lz - L.lz) / dx;
        xs = (int)ceilf(L.sx); xe = (int)ceilf(R.sx);
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
                int U = (int)(uz * z0 * tw * 65536.0f), V = (int)(vz * z0 * th * 65536.0f), Lq = (int)(lz * z0 * 256.0f * 65536.0f);
                int U1 = (int)((uz + duz * n) * z1 * tw * 65536.0f), V1 = (int)((vz + dvz * n) * z1 * th * 65536.0f);
                int L1 = (int)((lz + dlz * n) * z1 * 256.0f * 65536.0f);
                int F = (int)(f0 * 65536.0f), F1 = (int)(f1 * 65536.0f);
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
        float f = m3_f * m3_rw / M3W;
        s[i].sx = m3_rw / 2 + o[i].x * f * iz; s[i].sy = m3_rh / 2 - o[i].y * f * iz;
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
        M3P t[3];
        m3_quad(p1, p0, q0, q1, NULL, ridge, 0, 256, 1);
        t[0] = m3_xf(fc, 0, 0, 1.1f); t[1] = m3_xf(p0, 0, 0, 1.1f); t[2] = m3_xf(p1, 0, 0, 1.1f);
        m3_poly(t, 3, NULL, i == 0 || i == n - 1 ? 0xFF8020 : 0x50505A, 256);   /* the face, with a marker */
    }
}

static M3SURF m3_floor, m3_floor_solid;   /* floor_solid: the floor already blended over the sky */
static M3SURF m3_surf(int w, int h) { M3SURF s; s.w = w; s.h = h; s.p = (uint32_t*)calloc((size_t)w * h, 4); return s; }

static void m3_make_floor(void)
{
    int x, y;
    m3_floor = m3_surf(128, 128);
    for (y = 0; y < 128; y++) for (x = 0; x < 128; x++)
    {
        int l = (x % 32 == 0 || y % 32 == 0) ? 1 : 0;
        m3_floor.p[y * 128 + x] = l ? 0x2088C0 : ((x / 32 + y / 32) & 1 ? 0x0A0E1C : 0x0C1224);
    }
    m3_floor_solid = m3_surf(128, 128);
    for (x = 0; x < 128 * 128; x++) m3_floor_solid.p[x] = m3_blend(0x0E0A2C, m3_floor.p[x], 160);   /* over the sky's lower part */
}

static void m3_upscale(void)
{
    int x, y;
    if (!m3_half) return;
    for (y = m3_rh - 1; y >= 0; y--)
    {
        const uint32_t* s = m3_fb + y * M3W;
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
        uint32_t c = m3_lerpc(0x02030C, 0x1A1040, y * 256 / m3_rh);
        uint32_t* p = m3_fb + y * M3W; for (i = 0; i < m3_rw; i++) p[i] = c;
        memset(m3_zb + y * M3W, 0, m3_rw * sizeof(float));
    }
    for (i = 0; i < 90; i++)
    {   /* drifting stars */
        unsigned h = (unsigned)i * 2654435761u; int x = (int)((h % M3W + (int)(m3_bgphase * (4 + h % 7))) % M3W), yy = (int)(h / M3W % 240);
        if (m3_half) { x /= 2; yy /= 2; }
        m3_fb[yy * M3W + x] = m3_lerpc(0x404060, 0xFFFFFF, 80 + (int)(h % 170));
    }
}

static void m3_draw_floor(float y, int alpha)            /* alpha 256: opaque (faster), less: the mirror shows */
{
    int i, j; float s = 2.0f;
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
