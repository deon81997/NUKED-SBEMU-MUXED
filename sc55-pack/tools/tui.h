/* tui.h - flicker-free text screens for the pack's menu programs (SC55MENU, GAMETEST).
 *
 * Everything is drawn into an 80x25 buffer first; tui_flush() then copies only the rows that changed
 * to video memory (puttext), right after the vertical retrace starts, so nothing flickers or tears.
 * Coordinates are 1-based like conio (x 1..80, y 1..25). Header-only: #include it in one .c file.
 *
 * Two looks, chosen in MENU with F3 and kept in <home>\THEME.CFG (read by MENU, GAMETEST, SETTINGS):
 *   Groovy   - a brighter 90s palette loaded into the VGA DAC, raised 3D panels, an animated logo
 *   Classic  - the standard DOS colours, nothing animated (works on any display)
 * Both switch the attribute "blink" bit to bright backgrounds (INT 10h AX=1003h); the normal text mode
 * (blink, default palette) comes back with tui_done() or any mode set. */
#ifndef TUI_INCLUDED
#define TUI_INCLUDED
#define TUI_FN static __attribute__((unused))
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include <conio.h>
#include <pc.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <sys/segments.h>
#include "theme.h"

#define TUI_W 80
#define TUI_H 25

/* keys returned by tui_key(): plain characters as themselves, extended keys as 0x100 | scan code */
#define K_UP    0x148
#define K_DOWN  0x150
#define K_LEFT  0x14B
#define K_RIGHT 0x14D
#define K_PGUP  0x149
#define K_PGDN  0x151
#define K_HOME  0x147
#define K_END   0x14F
#define K_F1    0x13B
#define K_F2    0x13C
#define K_F3    0x13D
#define K_TAB   9
#define K_STAB  0x10F
#define K_ESC   27
#define K_ENTER 13
#define K_MOUSE 0x1F0   /* left click; the cell is tui_mx, tui_my (1-based, 80x25 like everything else) */
#define K_RCLICK 0x1F1  /* right click: "back" in dialogs and viewers */

/* ------------------------------------------------------------------ mouse (INT 33h, any mouse driver; with SBEMU's
 * USB mouse too). Its range is set to 640x200 so a cell is 8x8 mickey-pixels in every display mode; the pointer is
 * drawn by tui_flush itself (an inverted cell), so it works in the tweaked, VESA text and graphics modes as well.
 * Left click = K_MOUSE, right click = K_RCLICK, wheel = Up / Down. */
static int tui_mouse = -1, tui_mx = 40, tui_my = 13, tui_mbtn, tui_mshow;

static uint16_t tui_scr[TUI_H][TUI_W], tui_shown[TUI_H][TUI_W];
static void (*tui_anim)(unsigned long tick);  /* called ~9 times a second while waiting for a key (Groovy) */

static int tui_margin_dirty = 1;          /* the border around the 80x25 screen (bigger modes) needs drawing */
TUI_FN void tui_invalidate(void) { memset(tui_shown, 0xFF, sizeof tui_shown); tui_margin_dirty = 1; } /* next flush redraws all */
/* F3 in any program: the next style, saved in THEME.CFG for MENU and every other program (not in safe mode) */
TUI_FN void tui_theme_next(const char* home)
{
    if (tui_safe) return;
    tui_theme = (tui_theme + 1) % THEME_COUNT;
    if (home && *home) tui_theme_save(home);
    tui_apply_theme();
    tui_invalidate();
}

/* ------------------------------------------------------------------ display modes (F4)
 * The screens are always laid out for 80x25; in a bigger text mode they are drawn centred, with a
 * patterned border around them. Modes:
 *   VGA  80x25 80x28 80x50  (mode 3 with the 8x16 / 8x14 / 8x8 font, 400 lines)
 *   EGA  80x43              (350 lines, 8x8 font)
 *   TWK  80x30 80x34 80x60  (480 lines: the CRTC is reprogrammed with the standard 640x480 timing)
 *   VESA whatever text modes the video BIOS lists (132x25, 132x43, 132x50, 132x60, 80x60 ...)
 *   VESA graphics: every 256-colour mode from 640x400 up (640x480, 800x600, 1024x768, 1280x1024 ...).
 *        The 80x25 screen is drawn with the font scaled to fill it (linear frame buffer, or the
 *        64 KB bank window on cards without one).
 * Text modes are written directly at B800:0000, so every text mode works the same way. */
typedef struct { char id[16]; char name[44]; int cols, rows, font, lines, vesa, gfx; } TUI_MODE;
static int tui_cols = 80, tui_rows = 25, tui_ox = 0, tui_oy = 0;

/* VESA graphics state */
static int tui_gw, tui_gh, tui_gpitch, tui_gsel, tui_gwinseg, tui_ggran, tui_gbank = -1, tui_cw, tui_ch, tui_gx0, tui_gy0;
static unsigned long tui_gphys, tui_glin;
static unsigned char tui_gfont[256 * 16];               /* ROM 8x16 font with the chosen letters on top */
static unsigned char* tui_gglyph;                        /* scaled glyphs: 256 x cw x ch, 1 = ink */
TUI_FN void tui_geometry(void)
{
    if (tui_in_gfx) { tui_cols = tui_gw; tui_rows = tui_gh; tui_ox = tui_oy = 0; tui_margin_dirty = 1; return; }
    tui_cols = _farpeekw(_dos_ds, 0x44A);
    tui_rows = _farpeekb(_dos_ds, 0x484) + 1;
    if (tui_cols < 80 || tui_cols > 200) tui_cols = 80;
    if (tui_rows < 25 || tui_rows > 100) tui_rows = 25;
    tui_ox = (tui_cols - 80) / 2; tui_oy = (tui_rows - 25) / 2;
    tui_margin_dirty = 1;
}

static int tui_int10(int ax, int bx, int cx) { __dpmi_regs r; memset(&r, 0, sizeof r); r.x.ax = ax; r.x.bx = bx; r.x.cx = cx; __dpmi_int(0x10, &r); return r.x.ax; }

static void tui_crtc(int idx, int val) { outportb(0x3D4, idx); outportb(0x3D5, val); }

/* 480 scan lines on mode 3 (standard VGA 640x480 vertical timing) */
static void tui_tweak480(int font_height)
{
    int rows = 480 / font_height;
    outportb(0x3C2, (inportb(0x3CC) & 0x3F) | 0xC0);   /* sync polarity -/- = 480 lines */
    outportb(0x3D4, 0x11); outportb(0x3D5, inportb(0x3D5) & 0x7F); /* unlock CRTC 0-7 */
    tui_crtc(0x06, 0x0B); tui_crtc(0x07, 0x3E); tui_crtc(0x10, 0xEA); tui_crtc(0x11, 0x8C);
    tui_crtc(0x12, 0xDF); tui_crtc(0x15, 0xE7); tui_crtc(0x16, 0x04);
    tui_crtc(0x09, 0x40 | (font_height - 1));
    _farpokeb(_dos_ds, 0x484, rows - 1);
    _farpokew(_dos_ds, 0x485, font_height);
    _farpokew(_dos_ds, 0x44C, 80 * rows * 2);
}

TUI_FN int tui_mode_list(TUI_MODE* m, int max)
{
    static const struct { const char* id; const char* name; int cols, rows, font, lines; } vga[] = {
        { "VGA80x25", "80 x 25   standard VGA (8x16 font)",    80, 25, 16, 400 },
        { "VGA80x28", "80 x 28   VGA, 8x14 font",               80, 28, 14, 400 },
        { "EGA80x43", "80 x 43   EGA style, 350 lines",          80, 43,  8, 350 },
        { "VGA80x50", "80 x 50   VGA, 8x8 font",                 80, 50,  8, 400 },
        { "TWK80x30", "80 x 30   tweaked 480 lines, 8x16 font",  80, 30, 16, 480 },
        { "TWK80x34", "80 x 34   tweaked 480 lines, 8x14 font",  80, 34, 14, 480 },
        { "TWK80x60", "80 x 60   tweaked 480 lines, 8x8 font",   80, 60,  8, 480 },
    };
    int n = 0; unsigned i;
    for (i = 0; i < sizeof vga / sizeof vga[0] && n < max; i++, n++)
    {
        snprintf(m[n].id, sizeof m[n].id, "%s", vga[i].id); snprintf(m[n].name, sizeof m[n].name, "%s", vga[i].name);
        m[n].cols = vga[i].cols; m[n].rows = vga[i].rows; m[n].font = vga[i].font; m[n].lines = vga[i].lines; m[n].vesa = 0; m[n].gfx = 0;
    }
    /* VESA text modes the video BIOS offers (VBE 4F00 / 4F01) */
    {
        __dpmi_regs r; unsigned char info[512], mi[256]; unsigned short list[128]; unsigned long lp; int k, nl = 0;
        memset(info, 0, sizeof info); memcpy(info, "VBE2", 4);
        dosmemput(info, sizeof info, __tb);
        memset(&r, 0, sizeof r); r.x.ax = 0x4F00; r.x.es = __tb >> 4; r.x.di = __tb & 15;
        __dpmi_int(0x10, &r);
        if (r.x.ax != 0x004F) return n;
        dosmemget(__tb, sizeof info, info);
        if (memcmp(info, "VESA", 4)) return n;
        lp = ((unsigned long)(info[16] | (info[17] << 8)) << 4) + (info[14] | (info[15] << 8));
        for (k = 0; k < 128; k++) { dosmemget(lp + 2 * k, 2, &list[k]); if (list[k] == 0xFFFF) break; nl++; }
        for (k = 0; k < nl && n < max; k++)
        {
            int attr, cols, rows;
            memset(&r, 0, sizeof r); r.x.ax = 0x4F01; r.x.cx = list[k]; r.x.es = __tb >> 4; r.x.di = __tb & 15;
            __dpmi_int(0x10, &r);
            if (r.x.ax != 0x004F) continue;
            dosmemget(__tb, sizeof mi, mi);
            attr = mi[0] | (mi[1] << 8);
            if (!(attr & 1)) continue;                              /* not supported by this card / monitor */
            if (attr & 0x10)
            {   /* graphics: 256-colour packed-pixel modes from 640x400 */
                int w = mi[0x12] | (mi[0x13] << 8), h = mi[0x14] | (mi[0x15] << 8), j, dup = 0;
                char id[16];
                if (mi[0x19] != 8 || mi[0x1B] != 4 || w < 640 || h < 400 || w > 2048 || h > 1536) continue;
                if (!(attr & 0x80) && !(mi[2] & 4)) continue;      /* neither a frame buffer nor a writable window */
                snprintf(id, sizeof id, "G%dx%d", w, h);
                for (j = 0; j < n; j++) if (!strcmp(m[j].id, id)) dup = 1;
                if (dup) continue;
                snprintf(m[n].id, sizeof m[n].id, "%s", id);
                snprintf(m[n].name, sizeof m[n].name, "%d x %d   VESA graphics (%03Xh)", w, h, list[k]);
                m[n].cols = w; m[n].rows = h; m[n].font = 16; m[n].lines = 0; m[n].vesa = list[k]; m[n].gfx = 1;
                n++;
                continue;
            }
            cols = mi[0x12] | (mi[0x13] << 8); rows = mi[0x14] | (mi[0x15] << 8);
            if (cols < 80 || rows < 25 || cols > 200 || rows > 100) continue;
            snprintf(m[n].id, sizeof m[n].id, "VESA%03X", list[k]);
            snprintf(m[n].name, sizeof m[n].name, "%d x %d   VESA text mode %03Xh", cols, rows, list[k]);
            m[n].cols = cols; m[n].rows = rows; m[n].font = 0; m[n].lines = 0; m[n].vesa = list[k]; m[n].gfx = 0;
            n++;
        }
    }
    return n;
}

/* ---- VESA graphics: the font, scaled glyphs and frame buffer writes */
static void tui_gfx_font(void)
{
    __dpmi_regs r; int i, x, y, gw = 0, gh = 0, ox = 0, oy = 0;
    const unsigned char* t = tui_font == FONT_MODERN ? tui_ter16n : tui_font == FONT_THIN ? tui_ter16b : NULL;
    const TUI_GFONT* gf = NULL;
    memset(&r, 0, sizeof r); r.x.ax = 0x1130; r.h.bh = 6;              /* where the ROM 8x16 font is */
    __dpmi_int(0x10, &r);
    dosmemget(((unsigned long)r.x.es << 4) + r.x.bp, 256 * 16, tui_gfont);
    if (t) memcpy(tui_gfont + 32 * 16, t, 95 * 16);
    /* the largest Terminus size that fits the cell, drawn 1:1 (crisp) instead of stretching 8x16 */
    if (tui_font != FONT_VGA)
    {
        const TUI_GFONT* l = tui_font == FONT_THIN ? tui_gfonts_b : tui_gfonts_n;
        for (; l->w; l++) if (l->w <= tui_cw && l->h <= tui_ch) gf = l;
        if (gf) { gw = gf->w; gh = gf->h; ox = (tui_cw - gw) / 2; oy = (tui_ch - gh) / 2; }
    }
    free(tui_gglyph);
    tui_gglyph = (unsigned char*)malloc((size_t)256 * tui_cw * tui_ch);
    if (!tui_gglyph) return;
    for (i = 0; i < 256; i++)
    {
        unsigned char* g = tui_gglyph + (size_t)i * tui_ch * tui_cw;
        if (gf && (gf->have[i >> 3] >> (i & 7) & 1))
        {
            int bpr = (gw + 7) / 8;
            memset(g, 0, (size_t)tui_ch * tui_cw);
            for (y = 0; y < gh; y++)
                for (x = 0; x < gw; x++)
                    if (gf->bits[((size_t)i * gh + y) * bpr + x / 8] >> (7 - x % 8) & 1) g[(oy + y) * tui_cw + ox + x] = 1;
            continue;
        }
        if (!((i >= 0xB0 && i <= 0xDF) || i == 0xFE) && tui_cw >= 8 && tui_ch >= 16)
        {   /* letters and symbols: the 8x16 glyph enlarged by a whole number (crisp), centred */
            int k = tui_cw / 8 < tui_ch / 16 ? tui_cw / 8 : tui_ch / 16, x0 = (tui_cw - 8 * k) / 2, y0 = (tui_ch - 16 * k) / 2;
            memset(g, 0, (size_t)tui_ch * tui_cw);
            for (y = 0; y < 16 * k; y++)
                for (x = 0; x < 8 * k; x++)
                    if (tui_gfont[i * 16 + y / k] >> (7 - x / k) & 1) g[(y0 + y) * tui_cw + x0 + x] = 1;
            continue;
        }
        /* lines and blocks: the ROM 8x16 glyph stretched to fill the cell, so frames join up */
        for (y = 0; y < tui_ch; y++)
        {
            unsigned char bits = tui_gfont[i * 16 + y * 16 / tui_ch];
            for (x = 0; x < tui_cw; x++)
            {
                int sx = x * 9 / tui_cw;                                /* 9 dots like VGA text: the 9th repeats the 8th for lines */
                int on = sx < 8 ? (bits >> (7 - sx)) & 1 : (i >= 0xC0 && i <= 0xDF ? bits & 1 : 0);
                g[y * tui_cw + x] = (unsigned char)on;
            }
        }
    }
    tui_invalidate();
}
/* writes n bytes at offset off of the frame buffer */
static void tui_gput(unsigned long off, const unsigned char* p, int n)
{
    if (tui_gsel) { movedata(_my_ds(), (unsigned)p, tui_gsel, off, n); return; }
    while (n > 0)
    {
        unsigned long win = (unsigned long)tui_ggran * 1024UL, bank = off / win, o = off % win;
        int k = (int)(win - o < (unsigned long)n ? win - o : (unsigned long)n);
        if ((int)bank != tui_gbank) { __dpmi_regs r; memset(&r, 0, sizeof r); r.x.ax = 0x4F05; r.x.dx = (unsigned short)bank; __dpmi_int(0x10, &r); tui_gbank = (int)bank; }
        dosmemput(p, k, ((unsigned long)tui_gwinseg << 4) + o);
        off += k; p += k; n -= k;
    }
}
static void tui_gclear(void)
{
    static unsigned char line[2048];
    int y;
    memset(line, tui_dacidx[TUI_FANCY ? 1 : 0], sizeof line);
    for (y = 0; y < tui_gh; y++) tui_gput((unsigned long)y * tui_gpitch, line, tui_gw);
}
/* draws text row r of the buffer */
static void tui_grow(int r)
{
    static unsigned char buf[2048];
    int y, c, x;
    if (!tui_gglyph) return;
    for (y = 0; y < tui_ch; y++)
    {
        unsigned char* d = buf;
        for (c = 0; c < TUI_W; c++)
        {
            uint16_t v = tui_scr[r][c];
            const unsigned char* g = tui_gglyph + ((size_t)(v & 255) * tui_ch + y) * tui_cw;
            unsigned char fg = tui_dacidx[(v >> 8) & 15], bg = tui_dacidx[(v >> 12) & 15];
            for (x = 0; x < tui_cw; x++) *d++ = g[x] ? fg : bg;
        }
        tui_gput((unsigned long)(tui_gy0 + r * tui_ch + y) * tui_gpitch + tui_gx0, buf, TUI_W * tui_cw);
    }
}
static void tui_gfx_off(void)
{
    if (tui_gsel) { __dpmi_free_ldt_descriptor(tui_gsel); tui_gsel = 0; }
    if (tui_glin) { __dpmi_meminfo mi; mi.address = tui_glin; __dpmi_free_physical_address_mapping(&mi); tui_glin = 0; }
    tui_in_gfx = 0; tui_gbank = -1;
}
static int tui_gfx_on(int mode)
{
    __dpmi_regs r; unsigned char mi[256]; int attr, ver, lfb;
    unsigned char info[512];
    tui_gfx_off();
    memset(info, 0, sizeof info); memcpy(info, "VBE2", 4); dosmemput(info, sizeof info, __tb);
    memset(&r, 0, sizeof r); r.x.ax = 0x4F00; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
    dosmemget(__tb, 8, info); ver = info[4] | (info[5] << 8);
    memset(&r, 0, sizeof r); r.x.ax = 0x4F01; r.x.cx = mode; r.x.es = __tb >> 4; r.x.di = __tb & 15; __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) return 0;
    dosmemget(__tb, sizeof mi, mi);
    attr = mi[0] | (mi[1] << 8);
    tui_gw = mi[0x12] | (mi[0x13] << 8); tui_gh = mi[0x14] | (mi[0x15] << 8);
    tui_gpitch = mi[0x10] | (mi[0x11] << 8);
    tui_ggran = mi[4] | (mi[5] << 8); if (!tui_ggran) tui_ggran = 64;
    tui_gwinseg = mi[8] | (mi[9] << 8); if (!tui_gwinseg) tui_gwinseg = 0xA000;
    lfb = ver >= 0x200 && (attr & 0x80);
    if (lfb)
    {
        __dpmi_meminfo m;
        tui_gphys = (unsigned long)mi[0x28] | ((unsigned long)mi[0x29] << 8) | ((unsigned long)mi[0x2A] << 16) | ((unsigned long)mi[0x2B] << 24);
        if (ver >= 0x300 && (mi[0x32] | (mi[0x33] << 8))) tui_gpitch = mi[0x32] | (mi[0x33] << 8);
        m.address = tui_gphys; m.size = (unsigned long)tui_gpitch * tui_gh;
        if (!tui_gphys || __dpmi_physical_address_mapping(&m) != 0) lfb = 0;
        else
        {
            tui_glin = m.address;
            tui_gsel = __dpmi_allocate_ldt_descriptors(1);
            if (tui_gsel <= 0) { tui_gsel = 0; tui_gfx_off(); lfb = 0; }
            else { __dpmi_set_segment_base_address(tui_gsel, tui_glin); __dpmi_set_segment_limit(tui_gsel, ((unsigned long)tui_gpitch * tui_gh - 1) | 0xFFF); }
        }
    }
    if (!lfb && !(mi[2] & 4)) return 0;
    if (!lfb) { int pitch = tui_gpitch; tui_gfx_off(); tui_gpitch = pitch; }
    memset(&r, 0, sizeof r); r.x.ax = 0x4F02; r.x.bx = (unsigned short)(mode | (lfb ? 0x4000 : 0));
    __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) { tui_gfx_off(); return 0; }
    if (tui_gw > 2048) { tui_gfx_off(); return 0; }
    tui_in_gfx = 1; tui_gbank = -1;
    tui_cw = tui_gw / TUI_W; tui_ch = tui_gh / TUI_H;
    if (tui_ch > tui_cw * 3) tui_ch = tui_cw * 3;                    /* keep letters from getting too tall */
    tui_gx0 = (tui_gw - tui_cw * TUI_W) / 2; tui_gy0 = (tui_gh - tui_ch * TUI_H) / 2;
    tui_font_changed = tui_gfx_font;
    tui_gfx_font();
    return 1;
}

/* switch to a mode; returns 1 if the screen now has at least 80x25 */
TUI_FN int tui_set_mode(const TUI_MODE* m)
{
    if (m->gfx)
    {
        if (!tui_gfx_on(m->vesa)) { tui_int10(0x0003, 0, 0); tui_geometry(); return 0; }
    }
    else if (m->vesa)
    {
        tui_gfx_off();
        if (tui_int10(0x4F02, m->vesa, 0) != 0x004F) { tui_int10(0x0003, 0, 0); tui_geometry(); return 0; }
    }
    else
    {
        tui_gfx_off();
        tui_int10(0x1200 | (m->lines == 350 ? 0x01 : 0x02), 0x30, 0);   /* 350 or 400 scan lines (480 = 400 + tweak) */
        tui_int10(0x0003, 0, 0);
        if (m->font == 14) tui_int10(0x1111, 0, 0);
        else if (m->font == 8) tui_int10(0x1112, 0, 0);
        if (m->lines == 480) tui_tweak480(m->font);
    }
    tui_geometry();
    snprintf(tui_mode_id, sizeof tui_mode_id, "%s", m->id);
    _setcursortype(_NOCURSOR);
    tui_apply_theme();                                   /* a mode set resets the palette and the blink bit */
    tui_font_loaded = 0; tui_font_apply();               /* ... and the font */
    return tui_cols >= 80 && tui_rows >= 25;
}
/* the saved mode (THEME.CFG), if it is not plain 80x25 */
TUI_FN void tui_set_saved_mode(void)
{
    TUI_MODE m[40]; int n, i;
    if (!strcmp(tui_mode_id, "VGA80x25")) { tui_gfx_off(); tui_geometry(); return; }
    n = tui_mode_list(m, 40);
    for (i = 0; i < n; i++) if (!strcmp(m[i].id, tui_mode_id)) { tui_set_mode(&m[i]); return; }
    snprintf(tui_mode_id, sizeof tui_mode_id, "VGA80x25");
    tui_geometry();
}

static int tui_fade_pending;
TUI_FN void tui_init(void)
{
    tui_set_saved_mode();
    _setcursortype(_NOCURSOR);
    tui_font_apply();
    tui_apply_theme();
    if (TUI_FANCY && tui_animate) { tui_palette_level(0); tui_fade_pending = 1; } /* fades in after the first frame */
    tui_invalidate();
}
TUI_FN void tui_done(void)
{
    tui_fade(0);
    tui_font_restore();
    if (tui_in_gfx || tui_cols != 80 || tui_rows != 25) { tui_gfx_off(); tui_int10(0x1202, 0x30, 0); tui_int10(0x0003, 0, 0); tui_geometry(); }
    tui_plain();
    textattr(0x07); clrscr(); _setcursortype(_NORMALCURSOR);
}

/* ------------------------------------------------------------------ drawing */
TUI_FN void tui_cell(int x, int y, int attr, int ch)
{
    if (x < 1 || x > TUI_W || y < 1 || y > TUI_H) return;
    tui_scr[y - 1][x - 1] = (uint16_t)(((attr & 255) << 8) | (ch & 255));
}
TUI_FN void tui_clear(int attr)
{
    int x, y;
    for (y = 1; y <= TUI_H; ++y) for (x = 1; x <= TUI_W; ++x) tui_cell(x, y, attr, ' ');
}
TUI_FN void tui_fill(int x, int y, int w, int h, int attr, int ch)
{
    int i, j;
    for (j = 0; j < h; ++j) for (i = 0; i < w; ++i) tui_cell(x + i, y + j, attr, ch);
}
TUI_FN int tui_put(int x, int y, int attr, const char* s)
{
    int n = 0;
    while (*s) { tui_cell(x + n, y, attr, (unsigned char)*s++); ++n; }
    return n;
}
/* text padded / cut to exactly w columns */
TUI_FN void tui_putw(int x, int y, int w, int attr, const char* s)
{
    int i, n = (int)strlen(s);
    for (i = 0; i < w; ++i) tui_cell(x + i, y, attr, i < n ? (unsigned char)s[i] : ' ');
}
TUI_FN int tui_printf(int x, int y, int attr, const char* fmt, ...)
{
    char b[256]; va_list ap;
    va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    return tui_put(x, y, attr, b);
}
/* word-wrapped text in a w x h area; returns the lines used (the last line ends in "..." only if it
 * really does not fit - callers size their areas so that never happens) */
TUI_FN int tui_wrap(int x, int y, int w, int h, int attr, const char* s)
{
    int line = 0;
    while (*s && line < h)
    {
        int n = (int)strlen(s), cut = n;
        char buf[200];
        if (n > w) { cut = w; while (cut > w / 2 && s[cut] != ' ') --cut; if (s[cut] != ' ') cut = w; }
        if (line == h - 1 && n > w) { cut = w - 3; snprintf(buf, sizeof buf, "%.*s...", cut, s); }
        else snprintf(buf, sizeof buf, "%.*s", cut, s);
        tui_putw(x, y + line, w, attr, buf);
        s += cut; while (*s == ' ') ++s;
        ++line;
    }
    return line;
}
/* single-line box; double = 1 for a double-line frame */
TUI_FN void tui_box(int x, int y, int w, int h, int attr, int dbl)
{
    int i;
    int tl = dbl ? 0xC9 : 0xDA, tr = dbl ? 0xBB : 0xBF, bl = dbl ? 0xC8 : 0xC0, br = dbl ? 0xBC : 0xD9;
    int hz = dbl ? 0xCD : 0xC4, vt = dbl ? 0xBA : 0xB3;
    tui_cell(x, y, attr, tl); tui_cell(x + w - 1, y, attr, tr);
    tui_cell(x, y + h - 1, attr, bl); tui_cell(x + w - 1, y + h - 1, attr, br);
    for (i = 1; i < w - 1; ++i) { tui_cell(x + i, y, attr, hz); tui_cell(x + i, y + h - 1, attr, hz); }
    for (i = 1; i < h - 1; ++i) { tui_cell(x, y + i, attr, vt); tui_cell(x + w - 1, y + i, attr, vt); }
}
/* a box title: " text " set into the top edge */
TUI_FN void tui_title(int x, int y, int attr, const char* s)
{
    tui_cell(x, y, attr, ' '); x += 1 + tui_put(x + 1, y, attr, s); tui_cell(x, y, attr, ' ');
}
/* shadow right of and below a window: dark grey on black, characters kept */
TUI_FN void tui_shadow(int x, int y, int w, int h)
{
    int i, j;
    for (j = 1; j <= h; ++j) for (i = 0; i < 2; ++i)
        if (x + w + i <= TUI_W && y + j <= TUI_H) tui_scr[y + j - 1][x + w + i - 1] = (tui_scr[y + j - 1][x + w + i - 1] & 0xFF) | 0x0800;
    for (i = 2; i < w + 2; ++i)
        if (x + i <= TUI_W && y + h <= TUI_H) tui_scr[y + h - 1][x + i - 1] = (tui_scr[y + h - 1][x + i - 1] & 0xFF) | 0x0800;
}
/* a raised 3D panel: light top/left edge, dark bottom/right edge on background bg (0-15);
 * title in a coloured tab. Classic: an ordinary single frame. */
TUI_FN void tui_panel(int x, int y, int w, int h, int bg, int title_attr, const char* title)
{
    int i, hi = (bg << 4) | 0x0F, lo = (bg << 4) | (bg <= 1 ? 0x08 : 0x00);   /* on black / deep blue the dark edge is slate */
    tui_fill(x, y, w, h, bg << 4, ' ');
    if (tui_theme == THEME_CLASSIC) { tui_box(x, y, w, h, (bg << 4) | 0x0B, 0); }
    else
    {
        tui_cell(x, y, hi, 0xDA);
        for (i = 1; i < w - 1; ++i) { tui_cell(x + i, y, hi, 0xC4); tui_cell(x + i, y + h - 1, lo, 0xC4); }
        for (i = 1; i < h - 1; ++i) { tui_cell(x, y + i, hi, 0xB3); tui_cell(x + w - 1, y + i, lo, 0xB3); }
        tui_cell(x + w - 1, y, lo, 0xBF); tui_cell(x, y + h - 1, hi, 0xC0); tui_cell(x + w - 1, y + h - 1, lo, 0xD9);
    }
    if (title && *title) tui_title(x + 2, y, title_attr, title);
}
/* progress bar: w cells, filled to num/den */
TUI_FN void tui_bar(int x, int y, int w, long num, long den, int attr_on, int attr_off)
{
    int i, f = den > 0 ? (int)((num * w + den / 2) / den) : 0;
    if (f > w) f = w;
    for (i = 0; i < w; ++i) tui_cell(x + i, y, i < f ? attr_on : attr_off, i < f ? 0xDB : 0xB0);
}
/* a window "zooming" open from its centre (Groovy styles with animation on); the screen is left as it was */
TUI_FN void tui_flush(void);
TUI_FN void tui_zoom(int x, int y, int w, int h, int attr)
{
    static uint16_t base[TUI_H][TUI_W];
    int k;
    if (!TUI_FANCY || !tui_animate) return;
    memcpy(base, tui_scr, sizeof base);
    for (k = 1; k <= 4; k++)
    {
        int cw = w * k / 5, ch = h * k / 5;
        if (cw < 2) cw = 2;
        if (ch < 2) ch = 2;
        memcpy(tui_scr, base, sizeof base);
        tui_fill(x + (w - cw) / 2, y + (h - ch) / 2, cw, ch, attr, ' ');
        tui_box(x + (w - cw) / 2, y + (h - ch) / 2, cw, ch, attr, 1);
        tui_flush();
    }
    memcpy(tui_scr, base, sizeof base);
}
/* an empty window with frame, title and shadow */
TUI_FN void tui_window(int x, int y, int w, int h, int attr, const char* title)
{
    tui_fill(x, y, w, h, attr, ' ');
    tui_box(x, y, w, h, attr, 1);
    if (title && *title) tui_title(x + 2, y, attr, title);
    tui_shadow(x, y, w, h);
}

/* ------------------------------------------------------------------ big logo (half-block letters) */
/* 3 rows x 4 columns per letter (M: 5); supported: A B C D E G I K L M N O P R S T U 5 - space */
static const char* tui_glyph(char c, int row)
{
    static const struct { char c; const char* r[3]; } g[] = {
        {'S', {"\xDC\xDF\xDF\xDF", " \xDF\xDF\xDC", "\xDC\xDC\xDC\xDF"}},
        {'C', {"\xDC\xDF\xDF\xDF", "\xDB   ", "\xDF\xDC\xDC\xDC"}},
        {'B', {"\xDB\xDF\xDF\xDC", "\xDB\xDF\xDF\xDC", "\xDB\xDC\xDC\xDF"}},
        {'P', {"\xDB\xDF\xDF\xDC", "\xDB\xDC\xDC\xDF", "\xDB   "}},
        {'A', {"\xDC\xDF\xDF\xDC", "\xDB\xDC\xDC\xDB", "\xDB  \xDB"}},
        {'K', {"\xDB \xDC\xDF", "\xDB\xDF\xDC ", "\xDB  \xDB"}},
        {'G', {"\xDC\xDF\xDF\xDF", "\xDB \xDF\xDB", "\xDF\xDC\xDC\xDF"}},
        {'M', {"\xDB\xDC \xDC\xDB", "\xDB \xDF \xDB", "\xDB   \xDB"}},   /* 5 wide */
        {'E', {"\xDB\xDF\xDF\xDF", "\xDB\xDF\xDF ", "\xDB\xDC\xDC\xDC"}},
        {'T', {"\xDF\xDB\xDB\xDF", " \xDB\xDB ", " \xDB\xDB "}},
        {'5', {"\xDB\xDF\xDF\xDF", "\xDF\xDF\xDF\xDC", "\xDC\xDC\xDC\xDF"}},
        {'D', {"\xDB\xDF\xDF\xDC", "\xDB  \xDB", "\xDB\xDC\xDC\xDF"}},
        {'O', {"\xDC\xDF\xDF\xDC", "\xDB  \xDB", "\xDF\xDC\xDC\xDF"}},
        {'R', {"\xDB\xDF\xDF\xDC", "\xDB\xDC\xDC\xDF", "\xDB  \xDB"}},
        {'I', {"\xDF\xDB\xDB\xDF", " \xDB\xDB ", "\xDC\xDB\xDB\xDC"}},
        {'N', {"\xDB\xDC \xDB", "\xDB\xDF\xDC\xDB", "\xDB  \xDB"}},
        {'U', {"\xDB  \xDB", "\xDB  \xDB", "\xDF\xDC\xDC\xDF"}},
        {'L', {"\xDB   ", "\xDB   ", "\xDB\xDC\xDC\xDC"}},
        {'-', {"    ", "\xDF\xDF\xDF ", "    "}},
        {' ', {"    ", "    ", "    "}},
    };
    unsigned i;
    for (i = 0; i < sizeof g / sizeof g[0]; i++) if (g[i].c == c) return g[i].r[row];
    return "    ";
}
/* draws text in big letters, one colour per letter from cols[] (rotated by phase) */
TUI_FN int tui_biglogo(int x, int y, int bg, const char* text, const unsigned char* cols, int ncols, int phase)
{
    int row, i, k, cx = x;
    for (row = 0; row < 3; row++)
    {
        cx = x;
        for (i = 0; text[i]; i++)
        {
            const char* r = tui_glyph(text[i], row);
            int gw = (int)strlen(r);                /* 4 columns, M has 5 */
            for (k = 0; k < gw; k++, cx++)
            {
                int c = cols[(i + phase) % ncols];
                tui_cell(cx, y + row, (bg << 4) | c, (unsigned char)r[k]);
            }
            tui_cell(cx++, y + row, bg << 4, ' ');
        }
    }
    return cx - x;
}

/* ------------------------------------------------------------------ output and keys */
/* the border around the 80x25 screen in bigger modes */
static void tui_draw_margins(void)
{
    static uint16_t row[200];
    int r, i;
    uint16_t cell = (uint16_t)((TUI_FANCY ? 0x19 : 0x08) << 8 | 0xB0);
    tui_margin_dirty = 0;
    if (tui_cols == 80 && tui_rows == 25) return;
    for (i = 0; i < tui_cols; i++) row[i] = cell;
    for (r = 0; r < tui_rows; r++)
    {
        if (r >= tui_oy && r < tui_oy + 25)
        {
            if (tui_ox) { dosmemput(row, tui_ox * 2, 0xB8000 + r * tui_cols * 2); }
            if (tui_cols - tui_ox - 80 > 0) dosmemput(row, (tui_cols - tui_ox - 80) * 2, 0xB8000 + (r * tui_cols + tui_ox + 80) * 2);
        }
        else dosmemput(row, tui_cols * 2, 0xB8000 + r * tui_cols * 2);
    }
}
TUI_FN void tui_flush_now(void)
{
    int r;
    if (tui_in_gfx)
    {
        if (tui_margin_dirty) { tui_margin_dirty = 0; tui_gclear(); }
        for (r = 0; r < TUI_H; ++r)
            if (memcmp(tui_scr[r], tui_shown[r], sizeof tui_scr[r])) { tui_grow(r); memcpy(tui_shown[r], tui_scr[r], sizeof tui_scr[r]); }
        return;
    }
    if (tui_margin_dirty) tui_draw_margins();
    for (r = 0; r < TUI_H; ++r)
        if (memcmp(tui_scr[r], tui_shown[r], sizeof tui_scr[r]))
        {
            dosmemput(tui_scr[r], TUI_W * 2, 0xB8000 + ((tui_oy + r) * tui_cols + tui_ox) * 2);
            memcpy(tui_shown[r], tui_scr[r], sizeof tui_scr[r]);
        }
}
TUI_FN void tui_flush(void)
{
    int r, any = 0;
    long i;
    uint16_t keep = 0;
    int mx = tui_mx - 1, my = tui_my - 1, ptr = tui_mshow && mx >= 0 && mx < TUI_W && my >= 0 && my < TUI_H;
    if (ptr)
    {   /* the mouse pointer: the cell's colours swapped, only while it is copied to the screen */
        uint16_t c = keep = tui_scr[my][mx];
        tui_scr[my][mx] = (uint16_t)((c & 0x00FF) | ((c & 0x0F00) << 4) | ((c & 0xF000) >> 4) | (((c >> 12) == ((c >> 8) & 15)) ? 0x7000 : 0));
    }
    for (r = 0; r < TUI_H && !any; ++r) any = memcmp(tui_scr[r], tui_shown[r], sizeof tui_scr[r]) != 0;
    if (any)
    {   /* wait for the start of a vertical retrace (bounded, in case the port does not exist) */
        for (i = 0; i < 100000L && (inportb(0x3DA) & 8); ++i) {}
        for (i = 0; i < 100000L && !(inportb(0x3DA) & 8); ++i) {}
        tui_flush_now();
    }
    if (ptr) tui_scr[my][mx] = keep;
}
TUI_FN void tui_mouse_init(void)
{
    __dpmi_regs r;
    if (tui_mouse >= 0) return;
    memset(&r, 0, sizeof r); r.x.ax = 0; __dpmi_int(0x33, &r);
    tui_mouse = r.x.ax == 0xFFFF;
    if (!tui_mouse) return;
    memset(&r, 0, sizeof r); r.x.ax = 7; r.x.cx = 0; r.x.dx = 639; __dpmi_int(0x33, &r);
    memset(&r, 0, sizeof r); r.x.ax = 8; r.x.cx = 0; r.x.dx = 199; __dpmi_int(0x33, &r);
    memset(&r, 0, sizeof r); r.x.ax = 4; r.x.cx = (tui_mx - 1) * 8; r.x.dx = (tui_my - 1) * 8; __dpmi_int(0x33, &r);
}
/* a key made from the mouse, or 0; moves the pointer (shown once the mouse has moved) */
TUI_FN int tui_mouse_poll(void)
{
    __dpmi_regs r;
    int b, x, y, w, k = 0;
    if (tui_mouse < 0) tui_mouse_init();
    if (!tui_mouse) return 0;
    memset(&r, 0, sizeof r); r.x.ax = 3; __dpmi_int(0x33, &r);
    b = r.x.bx & 7; w = (signed char)r.h.bh;        /* wheel (CuteMouse): + = towards the user */
    x = r.x.cx / 8 + 1; y = r.x.dx / 8 + 1;
    if (x > TUI_W) x = TUI_W;
    if (y > TUI_H) y = TUI_H;
    if (x != tui_mx || y != tui_my) { tui_mx = x; tui_my = y; tui_mshow = 1; }
    if ((b & 1) && !(tui_mbtn & 1)) { k = K_MOUSE; tui_mshow = 1; }
    else if ((b & 2) && !(tui_mbtn & 2)) k = K_RCLICK;
    else if (w) k = w > 0 ? K_DOWN : K_UP;
    tui_mbtn = b;
    return k;
}
/* 1 if the last click was inside x..x+w-1, y..y+h-1 */
TUI_FN int tui_hit(int x, int y, int w, int h) { return tui_mx >= x && tui_mx < x + w && tui_my >= y && tui_my < y + h; }

TUI_FN unsigned long tui_ticks(void) { return _farpeekl(_dos_ds, 0x46C); } /* BIOS clock, 18.2 per second */

TUI_FN int tui_key(void)
{
    int c;
    tui_flush();
    if (tui_fade_pending) { tui_fade_pending = 0; tui_fade(1); }
    {
        unsigned long last = tui_ticks();
        int anim = tui_anim && TUI_FANCY && tui_animate;
        while (!kbhit())
        {
            unsigned long t = tui_ticks();
            int ox = tui_mx, oy = tui_my, m = tui_mouse_poll();
            if (m) { tui_flush(); return m; }
            if (anim && t != last && (t & 1) == 0) { last = t; tui_anim(t >> 1); tui_flush(); }
            else if (t != last) last = t;
            if (ox != tui_mx || oy != tui_my) tui_flush();
            __dpmi_yield();
        }
    }
    c = getch();
    if (c == 0 || c == 0xE0) c = 0x100 | getch();
    return c;
}

/* message box with Yes/No (returns 1 for Y/Enter on "Yes") */
TUI_FN int tui_yesno(const char* q)
{
    int w = (int)strlen(q) + 6, x, sel = 0, c, h, lines;
    uint16_t save[TUI_H][TUI_W];
    if (w < 34) w = 34;
    if (w > 76) w = 76;
    lines = ((int)strlen(q) + w - 7) / (w - 6); if (lines < 1) lines = 1; if (lines > 4) lines = 4;
    h = 5 + lines;
    x = (TUI_W - w) / 2 + 1;
    memcpy(save, tui_scr, sizeof save);
    tui_zoom(x, 10, w, h, 0x4F);
    for (;;)
    {
        tui_window(x, 10, w, h, 0x4F, "Question");
        tui_wrap(x + 3, 12, w - 6, lines, 0x4F, q);
        tui_put(x + w / 2 - 9, 12 + lines + 1, sel == 0 ? 0x2F : 0x47, "  Yes  ");
        tui_put(x + w / 2 + 2, 12 + lines + 1, sel == 1 ? 0x2F : 0x47, "  No   ");
        c = tui_key();
        if (c == 'y' || c == 'Y') { sel = 0; break; }
        if (c == 'n' || c == 'N' || c == K_ESC || c == K_RCLICK) { sel = 1; break; }
        if (c == K_LEFT || c == K_RIGHT || c == K_TAB) sel ^= 1;
        if (c == K_ENTER) break;
        if (c == K_MOUSE && tui_hit(x + w / 2 - 9, 12 + lines + 1, 7, 1)) { sel = 0; break; }
        if (c == K_MOUSE && tui_hit(x + w / 2 + 2, 12 + lines + 1, 7, 1)) { sel = 1; break; }
    }
    memcpy(tui_scr, save, sizeof save);
    return sel == 0;
}
/* message (wrapped), any key closes */
TUI_FN void tui_message(const char* title, const char* msg)
{
    int w = (int)strlen(msg) + 6, x, lines;
    uint16_t save[TUI_H][TUI_W];
    if (w < 34) w = 34;
    if (w > 76) w = 76;
    lines = ((int)strlen(msg) + w - 7) / (w - 6); if (lines < 1) lines = 1; if (lines > 6) lines = 6;
    x = (TUI_W - w) / 2 + 1;
    memcpy(save, tui_scr, sizeof save);
    tui_zoom(x, 9, w, 4 + lines, 0x1F);
    tui_window(x, 9, w, 4 + lines, 0x1F, title);
    tui_wrap(x + 3, 11, w - 6, lines, 0x1F, msg);
    tui_key();
    memcpy(tui_scr, save, sizeof save);
}
/* edit a line of text in a window; returns 0 on Esc. Upper-cases if upper != 0 */
TUI_FN int tui_input(const char* title, const char* prompt, char* buf, int max, int upper)
{
    int n = (int)strlen(buf), c, w = 72, x = 5, fw;
    uint16_t save[TUI_H][TUI_W];
    memcpy(save, tui_scr, sizeof save);
    tui_zoom(x, 9, w, 8, 0x1F);
    fw = w - 6;
    for (;;)
    {
        int start = n > fw - 1 ? n - (fw - 1) : 0;
        tui_window(x, 9, w, 8, 0x1F, title);
        tui_wrap(x + 3, 11, w - 6, 2, 0x1E, prompt);
        tui_fill(x + 3, 14, fw, 1, 0x70, ' ');
        tui_put(x + 3, 14, 0x70, buf + start);
        tui_cell(x + 3 + (n - start), 14, 0x70, '_');
        c = tui_key();
        if (c == K_ENTER) break;
        if (c == K_ESC) { memcpy(tui_scr, save, sizeof save); return 0; }
        if (c == 8) { if (n) buf[--n] = 0; continue; }
        if (c >= 32 && c < 256 && n < max - 1) { buf[n++] = (char)(upper && c >= 'a' && c <= 'z' ? c - 32 : c); buf[n] = 0; }
    }
    memcpy(tui_scr, save, sizeof save);
    return 1;
}

/* F4: pick a text mode. After switching, "keep it?" for 10 seconds - no answer (a screen that cannot
 * show the mode) goes back to the previous one. Saves the choice in <home>\THEME.CFG. */
TUI_FN void tui_pick_mode(const char* home)
{
    static TUI_MODE m[40];
    int n = tui_mode_list(m, 40), sel = 0, top = 0, i, c, rows;
    uint16_t save[TUI_H][TUI_W];
    void (*anim)(unsigned long) = tui_anim;
    char prev[16];
    tui_anim = 0;
    for (i = 0; i < n; i++) if (!strcmp(m[i].id, tui_mode_id)) sel = i;
    memcpy(save, tui_scr, sizeof save);
    rows = n < 14 ? n : 14;
    tui_zoom(14, 4, 52, rows + 6, 0x1F);
    for (;;)
    {
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        tui_window(14, 4, 52, rows + 6, 0x1F, "Display mode");
        for (i = 0; i < rows && top + i < n; i++)
        {
            const TUI_MODE* t = &m[top + i];
            char b[60];
            snprintf(b, sizeof b, " %c %s", !strcmp(t->id, tui_mode_id) ? 0x07 : ' ', t->name);
            tui_putw(16, 6 + i, 48, top + i == sel ? 0x5F : 0x1F, b);
        }
        tui_put(16, 7 + rows, 0x1B, "Enter: switch   Esc: back");
        tui_put(16, 8 + rows, 0x13, "\x07 = now.  Text modes: the menu stays 80x25, centred.");
        c = tui_key();
        if (c == K_MOUSE && tui_hit(16, 6, 48, rows))                     /* click: choose, again: switch */
        { int s2 = top + tui_my - 6; if (s2 < n) { if (s2 == sel) c = K_ENTER; else sel = s2; } }
        if (c == K_ESC || c == K_RCLICK) break;
        if (c == K_UP && sel > 0) sel--;
        if (c == K_DOWN && sel < n - 1) sel++;
        if (c == K_HOME) sel = 0;
        if (c == K_END) sel = n - 1;
        if (c == K_ENTER)
        {
            unsigned long t0, last = 0; int keep = 0;
            snprintf(prev, sizeof prev, "%s", tui_mode_id);
            if (!tui_set_mode(&m[sel])) { tui_invalidate(); tui_message("Display mode", "That mode could not be set; back to 80x25."); break; }
            tui_invalidate();
            memcpy(tui_scr, save, sizeof save);
            t0 = tui_ticks();
            for (;;)
            {
                unsigned long el = tui_ticks() - t0;
                if (el != last || el == 0)
                {
                    char b[80];
                    last = el;
                    tui_window(14, 9, 52, 7, 0x4F, "Keep this mode?");
                    snprintf(b, sizeof b, "%s  (%dx%d)", m[sel].name, tui_cols, tui_rows);
                    tui_putw(17, 11, 46, 0x4F, b);
                    snprintf(b, sizeof b, "Y = keep it.  Going back in %lu s ...", el >= 182 ? 0UL : (182 - el + 17) / 18);
                    tui_putw(17, 13, 46, 0x4E, b);
                    tui_flush();
                }
                if (el >= 182) break;
                if (kbhit()) { int k = getch(); if (k == 0 || k == 0xE0) getch(); keep = (k == 'y' || k == 'Y' || k == K_ENTER); break; }
            }
            if (!keep)
            {
                for (i = 0; i < n; i++) if (!strcmp(m[i].id, prev)) { tui_set_mode(&m[i]); break; }
                if (i == n) { tui_gfx_off(); tui_int10(0x1202, 0x30, 0); tui_int10(0x0003, 0, 0); snprintf(tui_mode_id, sizeof tui_mode_id, "VGA80x25"); tui_geometry(); _setcursortype(_NOCURSOR); tui_apply_theme(); }
            }
            tui_invalidate();
            tui_theme_save(home);
            break;
        }
    }
    memcpy(tui_scr, save, sizeof save);
    tui_anim = anim;
}

/* scrolling file viewer; colour(line) picks each line's colour */
TUI_FN void tui_view_file(const char* path, int (*colour)(const char*))
{
    static char lines[3000][200];
    FILE* f; int n = 0, top = 0, left = 0, maxlen = 0, c, i;
    char tmp[300];
    void (*anim)(unsigned long) = tui_anim;
    tui_anim = 0;                               /* no animation in the viewer */
    f = fopen(path, "r");
    if (f)
    {
        while (n < 3000 && fgets(tmp, sizeof tmp, f))
        {
            int l;
            tmp[strcspn(tmp, "\r\n")] = 0; strncpy(lines[n], tmp, 199); lines[n][199] = 0;
            l = (int)strlen(lines[n]); if (l > maxlen) maxlen = l;
            ++n;
        }
        fclose(f);
    }
    for (;;)
    {
        tui_clear(0x07);
        tui_fill(1, 1, 80, 1, 0x30, ' ');
        tui_printf(2, 1, 0x30, "%.56s", path);
        tui_printf(60, 1, 0x30, "%4d-%-4d of %d", n ? top + 1 : 0, top + 23 < n ? top + 23 : n, n);
        if (!n) tui_put(3, 3, 0x07, "(this file does not exist yet, or is empty)");
        for (i = 0; i < 23 && top + i < n; ++i)
        {
            const char* l = lines[top + i];
            int a = colour ? colour(l) : 0x07;
            if ((int)strlen(l) > left) tui_putw(1, 2 + i, 79, a, l + left);
            if ((int)strlen(l) > left + 79) tui_cell(79, 2 + i, 0x0E, 0x1A);   /* more to the right */
        }
        tui_fill(1, 25, 80, 1, 0x30, ' ');
        tui_put(2, 25, 0x30, maxlen > 79 ? "\x18\x19 PgUp PgDn Home End scroll   \x1B\x1A sideways   Esc back"
                                         : "\x18\x19 PgUp PgDn Home End scroll    Esc back");
        if (left) tui_printf(66, 25, 0x30, "col %d", left + 1);
        if (n > 23) { tui_fill(80, 2, 1, 23, 0x08, 0xB0); tui_cell(80, 2 + (int)((long)top * 22 / (n - 23)), 0x0F, 0xDB); }
        c = tui_key();
        if (c == K_ESC || c == K_ENTER || c == K_RCLICK) break;
        if (c == K_UP && top) --top;
        if (c == K_DOWN && top + 23 < n) ++top;
        if (c == K_PGUP) top = top > 23 ? top - 23 : 0;
        if (c == K_PGDN) { top += 23; if (top > n - 23) top = n > 23 ? n - 23 : 0; }
        if (c == K_LEFT) left = left >= 20 ? left - 20 : 0;
        if (c == K_RIGHT && left + 79 < maxlen) left += 20;
        if (c == K_HOME) { top = 0; left = 0; }
        if (c == K_END) top = n > 23 ? n - 23 : 0;
    }
    tui_anim = anim;
}
#endif
