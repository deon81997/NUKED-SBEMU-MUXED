/* theme.h - the looks of the pack's menus: Classic (standard DOS colours) and the Groovy family (own
 * palettes in the VGA DAC). Kept in <home>\THEME.CFG; switched with F3 in MENU. Both turn the attribute blink bit
 * into bright backgrounds; theme_plain() puts the normal DOS text colours back. */
#ifndef THEME_INCLUDED
#define THEME_INCLUDED
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pc.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include "fontdata.h"
#ifndef TUI_FN
#define TUI_FN static __attribute__((unused))
#endif

/* styles (F3 in MENU cycles through them). Classic = the standard DOS colours, flat frames; every other
 * style is a "Groovy" one: its own palette, raised 3D panels, optional animation (F6). Colour roles are
 * the same in all of them: 1 desk, 3 side panel, 5 highlight, 7 item panel, 8 dim text / shadow,
 * 10 good, 12 bad, 14 titles and warnings, 15 white. */
enum { THEME_GROOVY = 0, THEME_CLASSIC = 1, THEME_SUNSET, THEME_OCEAN, THEME_NEON, THEME_CANDY, THEME_MIDNIGHT, THEME_COUNT };
static int tui_theme = THEME_GROOVY;
static int tui_animate = 1;                     /* logo colours and window transitions (F6) */
static const char* tui_theme_name[THEME_COUNT] __attribute__((unused)) =
    { "Groovy", "Classic", "Sunset", "Ocean", "Neon", "Candy", "Midnight" };
#define TUI_FANCY (tui_theme != THEME_CLASSIC)
static char tui_mode_id[16] = "VGA80x25";      /* display mode chosen with F4 (see tui.h) */
enum { FONT_VGA = 0, FONT_MODERN, FONT_THIN, FONT_COUNT };
static int tui_font = FONT_MODERN;              /* letters of the screens (F7): the VGA ROM font or a smoother one */
static const char* tui_font_name[FONT_COUNT] __attribute__((unused)) = { "VGA", "Terminus", "Bold" }; /* FONT_THIN = Terminus bold */

/* ------------------------------------------------------------------ theme */
/* safe mode: a Shift key held while the program starts (or SET SC55SAFE=1) ignores THEME.CFG - plain
 * Classic colours, 80x25, VGA font, no animation - for when a saved display mode or style is unusable */
static int tui_safe;
TUI_FN void tui_theme_load(const char* home)
{
    char p[300], line[64]; FILE* f;
    const char* e = getenv("SC55SAFE");
    if ((_farpeekb(_dos_ds, 0x417) & 3) || (e && *e == '1'))
    {
        tui_safe = 1; tui_theme = THEME_CLASSIC; tui_animate = 0; tui_font = FONT_VGA;
        snprintf(tui_mode_id, sizeof tui_mode_id, "VGA80x25");
        return;
    }
    sprintf(p, "%s\\THEME.CFG", home);
    if ((f = fopen(p, "r")) == NULL) return;
    while (fgets(line, sizeof line, f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "THEME=", 6))
        {
            int t;
            for (t = 0; t < THEME_COUNT; t++) if (!strcasecmp(line + 6, tui_theme_name[t])) tui_theme = t;
        }
        else if (!strncmp(line, "ANIM=", 5)) tui_animate = line[5] != '0';
        else if (!strncmp(line, "FONT=", 5))
        {
            int t;
            for (t = 0; t < FONT_COUNT; t++) if (!strcasecmp(line + 5, tui_font_name[t])) tui_font = t;
            if (!strcasecmp(line + 5, "Modern") || !strcasecmp(line + 5, "Thin")) tui_font = FONT_MODERN; /* beta.33's fonts */
        }
        else if (!strncmp(line, "MODE=", 5) && strlen(line) < 5 + sizeof tui_mode_id) strcpy(tui_mode_id, line + 5);
    }
    fclose(f);
}
TUI_FN void tui_theme_save(const char* home)
{
    char p[300]; FILE* f;
    sprintf(p, "%s\\THEME.CFG", home);
    if ((f = fopen(p, "w")) == NULL) return;
    fprintf(f, "THEME=%s\nMODE=%s\nANIM=%d\nFONT=%s\n", tui_theme_name[tui_theme], tui_mode_id, tui_animate, tui_font_name[tui_font]);
    fclose(f);
}

static __attribute__((unused)) void tui_dac(int idx, int r, int g, int b) { outportb(0x3C8, idx); outportb(0x3C9, r >> 2); outportb(0x3C9, g >> 2); outportb(0x3C9, b >> 2); }

/* the 16 text colours: index -> DAC entry the attribute controller uses in mode 3 */
static const unsigned char tui_dacidx[16] = { 0, 1, 2, 3, 4, 5, 20, 7, 56, 57, 58, 59, 60, 61, 62, 63 };
static const unsigned char tui_groovy[16][3] = {
    {  0,   0,   0 }, { 16,  24,  92 }, {  0, 150,  96 }, {  0, 132, 168 },   /* black, deep blue, green, teal */
    {176,  20,  72 }, {104,  36, 156 }, {200, 120,  24 }, {196, 200, 216 },   /* crimson, purple, amber, light grey */
    { 64,  66,  96 }, { 72, 120, 255 }, { 88, 255, 136 }, { 72, 236, 255 },   /* slate, bright blue, lime, cyan */
    {255,  72, 120 }, {240,  88, 240 }, {255, 228,  72 }, {255, 255, 255 } }; /* hot pink, magenta, yellow, white */
static const unsigned char tui_more[5][16][3] = {
    /* Sunset */   { {0,0,0}, {64,18,56}, {0,150,96}, {168,72,40}, {200,30,40}, {226,96,32}, {214,128,40}, {244,220,196},
                     {104,60,80}, {255,120,96}, {136,236,96}, {255,196,96}, {255,64,80}, {255,96,180}, {255,232,96}, {255,255,255} },
    /* Ocean */    { {0,0,0}, {8,32,72}, {0,160,110}, {0,112,140}, {190,40,60}, {24,96,200}, {210,150,40}, {204,232,240},
                     {56,84,112}, {80,160,255}, {96,250,170}, {80,240,240}, {255,96,110}, {150,140,255}, {255,236,120}, {255,255,255} },
    /* Neon */     { {0,0,0}, {24,4,40}, {0,200,90}, {96,8,120}, {220,0,60}, {230,0,150}, {255,120,0}, {222,212,244},
                     {72,40,96}, {60,140,255}, {60,255,120}, {0,255,240}, {255,40,80}, {255,60,255}, {255,255,40}, {255,255,255} },
    /* Candy */    { {0,0,0}, {72,44,112}, {60,170,110}, {200,96,164}, {200,60,100}, {40,168,140}, {230,140,90}, {252,236,246},
                     {120,96,150}, {140,180,255}, {150,255,180}, {150,240,255}, {255,110,140}, {255,150,230}, {255,240,150}, {255,255,255} },
    /* Midnight */ { {0,0,0}, {6,10,30}, {0,130,90}, {40,62,104}, {170,30,50}, {72,64,176}, {170,120,50}, {192,198,214},
                     {50,56,80}, {100,140,255}, {90,230,150}, {120,210,255}, {255,90,110}, {200,120,255}, {240,220,110}, {255,255,255} },
};
static const unsigned char tui_classic[16][3] = {
    {  0,   0,   0 }, {  0,   0, 168 }, {  0, 168,   0 }, {  0, 168, 168 },
    {168,   0,   0 }, {168,   0, 168 }, {168,  84,   0 }, {168, 168, 168 },
    { 84,  84,  84 }, { 84,  84, 252 }, { 84, 252,  84 }, { 84, 252, 252 },
    {252,  84,  84 }, {252,  84, 252 }, {252, 252,  84 }, {252, 252, 252 } };

static const unsigned char (*tui_palette(void))[3]
{
    return tui_theme == THEME_CLASSIC ? tui_classic : tui_theme == THEME_GROOVY ? tui_groovy : tui_more[tui_theme - THEME_SUNSET];
}
/* the current style's palette at level 0 (black) .. 16 (full) */
TUI_FN void tui_palette_level(int level)
{
    const unsigned char (*p)[3] = tui_palette();
    int i;
    for (i = 0; i < 16; i++) tui_dac(tui_dacidx[i], p[i][0] * level / 16, p[i][1] * level / 16, p[i][2] * level / 16);
}
TUI_FN void tui_apply_theme(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r); r.x.ax = 0x1003; r.h.bl = 0; __dpmi_int(0x10, &r); /* bright backgrounds, no blink */
    tui_palette_level(16);
}
static void tui_vsync(void)
{
    long i;
    for (i = 0; i < 100000L && (inportb(0x3DA) & 8); ++i) {}
    for (i = 0; i < 100000L && !(inportb(0x3DA) & 8); ++i) {}
}
/* fades (Groovy styles with animation on): about 1/8 s */
TUI_FN void tui_fade(int in)
{
    int k;
    if (tui_theme == THEME_CLASSIC || !tui_animate) { tui_palette_level(in ? 16 : 0); return; }
    for (k = 0; k <= 8; k++) { tui_vsync(); tui_palette_level(in ? k * 2 : 16 - k * 2); }
}

/* the normal DOS text colours (default palette, blink on) - before starting another program */
TUI_FN void tui_plain(void)
{
    __dpmi_regs r;
    int i;
    for (i = 0; i < 16; i++) tui_dac(tui_dacidx[i], tui_classic[i][0], tui_classic[i][1], tui_classic[i][2]);
    memset(&r, 0, sizeof r); r.x.ax = 0x1003; r.h.bl = 1; __dpmi_int(0x10, &r);
}
/* ------------------------------------------------------------------ font */
TUI_FN void tui_font_int10(int ax) { __dpmi_regs r; memset(&r, 0, sizeof r); r.x.ax = ax; __dpmi_int(0x10, &r); }
/* the chosen font (F7) for letters 32-126 of the current character height (16 or 14 lines; 8-line modes keep
 * the ROM font, too small to improve). INT 10h AX=1100h loads into the font without resetting the mode. */
static int tui_font_loaded;                 /* height loaded (0 = ROM font) */
static int tui_in_gfx;                      /* a VESA graphics mode draws the letters itself (tui.h) */
static void (*tui_font_changed)(void);
TUI_FN void tui_font_apply(void)
{
    if (tui_in_gfx) { if (tui_font_changed) tui_font_changed(); return; }
    int h = _farpeekw(_dos_ds, 0x485);
    const unsigned char* t = NULL;
    __dpmi_regs r;
    if (tui_font_loaded && tui_font_loaded == h && tui_font == FONT_VGA)
    {   /* back to the ROM letters */
        tui_font_int10(h == 14 ? 0x1101 : 0x1104);
        tui_font_loaded = 0;
        return;
    }
    if (tui_font == FONT_MODERN) t = h == 16 ? tui_ter16n : h == 14 ? tui_ter14n : NULL;
    else if (tui_font == FONT_THIN) t = h == 16 ? tui_ter16b : h == 14 ? tui_ter14b : NULL;
    if (!t) return;
    dosmemput(t, 95 * h, __tb);
    memset(&r, 0, sizeof r);
    r.x.ax = 0x1100; r.h.bh = (unsigned char)h; r.h.bl = 0; r.x.cx = 95; r.x.dx = 32;
    r.x.es = (unsigned short)(__tb >> 4); r.x.bp = (unsigned short)(__tb & 15);
    __dpmi_int(0x10, &r);
    tui_font_loaded = h;
}
TUI_FN void tui_font_restore(void)
{
    if (tui_in_gfx) return;
    if (tui_font_loaded) tui_font_int10(tui_font_loaded == 14 ? 0x1101 : 0x1104);
    tui_font_loaded = 0;
}


#endif
