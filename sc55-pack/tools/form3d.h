/* form3d.h - 3D list screens for SETTINGS, CD images and GAMETEST (since beta.77), in the 3D menu's styles.
 *
 * A program fills an F3VIEW (title, status, optional tabs, rows of label + value, a description) every
 * frame and calls f3_frame(); f3_key() gives keys and mouse clicks back as the program's own key codes, so
 * each program keeps its own key handling. Pop-ups that stay in 3D: f3_pick (choose from a list),
 * f3_choice / f3_yesno (buttons), f3_message, f3_input (a line of text). Screens that are text by nature
 * (file browser, help, viewers, running games) switch to text mode and come back.
 *
 * On or off for all programs together: on while MENU3D.CFG exists in the pack's folder (MENU's F8 / 3D menu
 * writes it); F8 in any of them switches. The style (F3: Panels, Carousel, Module) and the speed (F4: auto,
 * full, half resolution) are kept in the same file. Safe mode (Shift held at start) or a PC without a
 * 640x480 VESA mode with 15/16/24/32-bit colour: the text screens.
 *
 * Drawn by the CPU (m3core.h); no 3D card needed. */
#ifndef FORM3D_INCLUDED
#define FORM3D_INCLUDED
#include <stdio.h>
#include <conio.h>
#include <ctype.h>
#include "m3core.h"

#define F3K_UP    0x148
#define F3K_DOWN  0x150
#define F3K_LEFT  0x14B
#define F3K_RIGHT 0x14D
#define F3K_PGUP  0x149
#define F3K_PGDN  0x151
#define F3K_HOME  0x147
#define F3K_END   0x14F
#define F3K_F3    0x13D
#define F3K_F4    0x13E
#define F3K_F8    0x142
#define F3K_TAB   9
#define F3K_STAB  0x10F
#define F3K_ESC   27
#define F3K_ENTER 13
#define F3K_BS    8
#define F3K_TABSEL 0x1F2   /* mouse: tab f3_mval clicked */
#define F3K_REDRAW 0x1F4   /* mouse: the selection moved (v->sel), nothing else */

enum { F3_HEAD, F3_ITEM, F3_VALUE };
/* marks drawn before a row's label */
enum { F3M_NONE = 0, F3M_ON, F3M_OFF, F3M_WARN, F3M_FOLDER, F3M_UP, F3M_MUSIC, F3M_DISC, F3M_GO };
#define F3_MAXROWS 320
typedef struct { char label[40]; char value[72]; int kind, mark, dim; uint32_t col; } F3ROW;
typedef struct {
    const char* app;               /* SETTINGS, CD IMAGES, GAMETEST */
    char title[64];                /* the page */
    char status[64]; uint32_t statcol;
    const char* tabs[8]; int ntab, tab;
    F3ROW rows[F3_MAXROWS]; int nrows, sel;
    char info[480]; uint32_t infocol;
    char foot[160];
    const char* keys;
    uint32_t accent;
} F3VIEW;

static const char* f3_stylename[] = { "Panels", "Carousel", "Module" };
#define F3_STYLES 3
static int  f3_on, f3_mouse = -1, f3_mx = 320, f3_my = 240, f3_mb, f3_mseen, f3_mval;
static char f3_home[200];
static double f3_last;
static float f3_selanim, f3_ring, f3_knob, f3_tabanim;
static int f3_lastsel = -1, f3_lasttab = -1;
static double f3_fps_t; static int f3_frames;

/* ------------------------------------------------------------------ the view */
static void f3_clear(F3VIEW* v) { v->nrows = 0; v->info[0] = 0; v->foot[0] = 0; v->status[0] = 0; v->infocol = 0xD8E0F0; }
static F3ROW* f3_add(F3VIEW* v, int kind, const char* label, const char* value)
{
    F3ROW* r;
    if (v->nrows >= F3_MAXROWS) return &v->rows[F3_MAXROWS - 1];
    r = &v->rows[v->nrows++];
    snprintf(r->label, sizeof r->label, "%s", label ? label : "");
    snprintf(r->value, sizeof r->value, "%s", value ? value : "");
    r->kind = kind; r->mark = 0; r->dim = 0; r->col = 0;
    return r;
}
static int f3_selectable(const F3VIEW* v, int i) { return i >= 0 && i < v->nrows && v->rows[i].kind != F3_HEAD; }
/* the next selectable row in direction d (wraps), from v->sel */
static int f3_step(const F3VIEW* v, int d)
{
    int i = v->sel, n;
    for (n = 0; n < v->nrows; n++)
    {
        i = (i + d + v->nrows) % v->nrows;
        if (f3_selectable(v, i)) return i;
    }
    return v->sel;
}
static int f3_first(const F3VIEW* v) { int i; for (i = 0; i < v->nrows; i++) if (f3_selectable(v, i)) return i; return 0; }
static int f3_last_row(const F3VIEW* v) { int i; for (i = v->nrows - 1; i >= 0; i--) if (f3_selectable(v, i)) return i; return 0; }
/* position among the selectable rows (carousel, knob) */
static int f3_index(const F3VIEW* v, int row, int* total)
{
    int i, k = 0, at = 0;
    for (i = 0; i < v->nrows; i++) if (v->rows[i].kind != F3_HEAD) { if (i == row) at = k; k++; }
    if (total) *total = k;
    return at;
}

/* ------------------------------------------------------------------ settings shared with MENU */
static void f3_cfg_path(char* p, size_t n, const char* home) { snprintf(p, n, "%s\\MENU3D.CFG", home && *home ? home : "C:\\SC55"); }
static int f3_cfg_load(const char* home)
{
    char p[260], l[40]; FILE* f;
    snprintf(f3_home, sizeof f3_home, "%s", home ? home : "");
    f3_cfg_path(p, sizeof p, home);
    if ((f = fopen(p, "r")) == NULL) return 0;
    while (fgets(l, sizeof l, f))
    {
        if (!strncmp(l, "STYLE=", 6)) m3_style = atoi(l + 6) % F3_STYLES;
        else if (!strncmp(l, "QUALITY=", 8)) m3_quality = atoi(l + 8) % 3;
    }
    fclose(f);
    return 1;
}
static void f3_cfg_save(void)
{
    char p[260]; FILE* f; f3_cfg_path(p, sizeof p, f3_home);
    if ((f = fopen(p, "w")) != NULL) { fprintf(f, "STYLE=%d\nQUALITY=%d\n", m3_style, m3_quality); fclose(f); }
}
static void f3_cfg_off(void) { char p[260]; f3_cfg_path(p, sizeof p, f3_home); remove(p); }
/* 3D wanted at start: MENU3D.CFG there, no Shift held, no SC55SAFE */
static int f3_wanted(const char* home)
{
    const char* e = getenv("SC55SAFE");
    if ((_farpeekb(_dos_ds, 0x417) & 3) || (e && *e == '1')) return 0;
    return f3_cfg_load(home);
}

/* ------------------------------------------------------------------ mode and mouse */
static void f3_mouse_setup(void)
{
    __dpmi_regs r;
    if (f3_mouse < 0) { memset(&r, 0, sizeof r); r.x.ax = 0; __dpmi_int(0x33, &r); f3_mouse = r.x.ax == 0xFFFF; }
    if (!f3_mouse) return;
    memset(&r, 0, sizeof r); r.x.ax = 7; r.x.cx = 0; r.x.dx = M3W - 1; __dpmi_int(0x33, &r);
    memset(&r, 0, sizeof r); r.x.ax = 8; r.x.cx = 0; r.x.dx = M3H - 1; __dpmi_int(0x33, &r);
    memset(&r, 0, sizeof r); r.x.ax = 4; r.x.cx = f3_mx; r.x.dx = f3_my; __dpmi_int(0x33, &r);
#ifdef K_MOUSE
    tui_mouse = -1;                     /* tui.h: the text screens set their own mouse range again */
#endif
}
static int f3_enter(void)
{
    if (f3_on) return 1;
    if (!m3_fb) m3_fb = (uint32_t*)malloc(M3W * M3H * 4);
    if (!m3_zb) m3_zb = (float*)malloc(M3W * M3H * sizeof(float));
    if (!m3_fb || !m3_zb) return 0;
    m3_scr.w = M3W; m3_scr.h = M3H; m3_scr.p = m3_fb;
    if (!m3_floor.p) m3_make_floor();
    if (!m3_vbe_open()) return 0;
    if (m3_tsc_us == 0) m3_time_init();
    m3_half = m3_quality == 2; m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H;
    f3_on = 1; f3_last = f3_fps_t = m3_now(); f3_frames = 0; f3_lastsel = -1; f3_lasttab = -1; f3_tabanim = 1;
    f3_mouse_setup();
    while (kbhit()) getch();
    return 1;
}
static void f3_leave(void)
{
    if (!f3_on) return;
    m3_vbe_close();
    f3_on = 0;
}

/* mouse targets, collected while drawing; kind: 1 tab, 2 row, 3 value left arrow, 4 value right arrow,
 * 5 previous, 6 next, 7 button (pop-ups) */
typedef struct { short x, y, w, h, kind, val; } F3HIT;
static F3HIT f3_hits[96]; static int f3_nhits;
static void f3_hit(int x, int y, int w, int h, int kind, int val)
{ if (f3_nhits < 96) { F3HIT* t = &f3_hits[f3_nhits++]; t->x = x; t->y = y; t->w = w; t->h = h; t->kind = kind; t->val = val; } }

static void f3_pointer(void)
{
    static const char* const a[] = { "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X",
        "X........X", "X.....XXXX", "X..X..X", "X.X X..X", "XX  X..X", "X    X..X", "     X..X", "      XX" };
    int y, x;
    if (f3_mouse <= 0 || !f3_mseen) return;
    for (y = 0; y < 17; y++)
        for (x = 0; a[y][x]; x++)
        {
            int px = f3_mx + x, py = f3_my + y;
            if (px >= M3W || py >= M3H || a[y][x] == ' ') continue;
            m3_fb[py * M3W + px] = a[y][x] == 'X' ? 0x000000 : 0xFFFFFF;
        }
}

/* ------------------------------------------------------------------ pieces */
static void f3_mark(M3SURF* s, int x, int y, int mark, uint32_t c)
{
    int i;
    switch (mark)
    {
    case F3M_ON: case F3M_OFF:
        m3_rect(s, x, y + 2, 14, 14, 0x000000, 140); m3_bevel(s, x, y + 2, 14, 14, 1, 1, 200);
        if (mark == F3M_ON) for (i = 0; i < 4; i++) { m3_rect(s, x + 3 + i, y + 8 + i, 2, 2, 0x60FF80, 256); m3_rect(s, x + 7 + i, y + 10 - 2 * i, 2, 2, 0x60FF80, 256); }
        break;
    case F3M_WARN: m3_rect(s, x + 1, y + 1, 14, 16, 0xFF5040, 256); m3_text(s, x + 4, y + 1, 0, "!", 0xFFFFFF, 256); break;
    case F3M_FOLDER: m3_rect(s, x, y + 5, 15, 11, 0xE0B040, 256); m3_rect(s, x, y + 3, 7, 3, 0xE0B040, 256); m3_bevel(s, x, y + 5, 15, 11, 1, 0, 150); break;
    case F3M_UP: for (i = 0; i < 6; i++) m3_rect(s, x + 7 - i, y + 4 + i, 1 + 2 * i, 1, c, 256); m3_rect(s, x + 5, y + 10, 5, 6, c, 256); break;
    case F3M_MUSIC: m3_rect(s, x + 9, y + 2, 2, 11, c, 256); m3_rect(s, x + 4, y + 11, 7, 5, c, 256); m3_rect(s, x + 9, y + 2, 5, 3, c, 256); break;
    case F3M_DISC:
        for (i = -7; i <= 7; i++) { int w = (int)(2 * sqrtf((float)(49 - i * i))); m3_rect(s, x + 7 - w / 2, y + 9 + i, w, 1, i == 0 ? 0x101010 : m3_lerpc(0xA0A8C0, 0xE0E8FF, (i + 7) * 18), 256); }
        m3_rect(s, x + 6, y + 8, 3, 3, 0x101010, 256); break;
    case F3M_GO: for (i = 0; i < 7; i++) m3_rect(s, x + 3 + i, y + 3 + i, 1, 14 - 2 * i, c, 256); break;
    default: break;
    }
}
static void f3_tri(M3SURF* s, int x, int y, int dir, uint32_t c)   /* small arrow, 5 px, dir -1 left / 1 right */
{
    int i;
    for (i = 0; i < 5; i++) m3_rect(s, dir < 0 ? x + i : x + 4 - i, y + 4 - i, 1, 1 + 2 * i, c, 256);
}
static void f3_background(void)
{
    int x, y, t = (int)(m3_bgphase * 40);
    static float sx[M3W], cx[M3W]; static int init;
    if (!init) { for (x = 0; x < M3W; x++) { sx[x] = 14 * sinf(x * 0.01f); cx[x] = 14 * cosf(x * 0.01f); } init = 1; }
    for (y = 0; y < M3H; y++)
    {
        uint32_t* p = m3_fb + y * M3W;
        float b = y * 0.013f + m3_bgphase * 0.6f, sb = sinf(b), cb = cosf(b);
        for (x = 0; x < M3W; x++)
        {
            int v = ((x + y + t) >> 5 & 1) ? 6 : 0, w = (int)(24 + sx[x] * cb + cx[x] * sb);
            p[x] = m3_rgb(8 + v, 14 + w / 2 + v, 40 + w + v);
        }
    }
}
static void f3_fit(char* out, size_t n, const char* s, int cols)
{
    int len = (int)strlen(s);
    if (cols < 4) cols = 4;
    if ((size_t)cols >= n) cols = (int)n - 1;
    if (len <= cols) { snprintf(out, n, "%s", s); return; }
    snprintf(out, n, "%.*s...", cols - 3, s);
}
/* the description box and the keys line (all styles) */
static void f3_infobox(const F3VIEW* v, int y, int h, int label)
{
    m3_shadow(&m3_scr, 12, y, M3W - 24, h, 6);
    m3_rect(&m3_scr, 12, y, M3W - 24, h, 0x0A0F1C, 235);
    m3_bevel(&m3_scr, 12, y, M3W - 24, h, 2, 1, 200);
    {
        int yy = y + 8, lines = (h - 44) / 18;
        if (label && f3_selectable(v, v->sel))
        {
            const F3ROW* r = &v->rows[v->sel]; char b[120];
            snprintf(b, sizeof b, "%s%s%s", r->label, r->value[0] ? ":  " : "", r->value);
            { char c[80]; f3_fit(c, sizeof c, b, 60); m3_text_sh(&m3_scr, 24, yy, 1, c, m3_mul(v->accent, 380)); }
            yy += 24; lines = (h - 68) / 18;
        }
        if (lines < 1) lines = 1;
        if (v->info[0]) yy += 18 * m3_wrap(&m3_scr, 24, yy, M3W - 48, lines, 0, v->info, v->infocol, 0);
        if (v->foot[0]) { char c[80]; f3_fit(c, sizeof c, v->foot, 74); m3_text(&m3_scr, 24, y + h - 40, 0, c, 0x80C8E0, 256); }
    }
    {
        char k[100]; f3_fit(k, sizeof k, v->keys ? v->keys : "", 74);
        m3_text(&m3_scr, 24, y + h - 20, 0, k, 0x8090B0, 256);
    }
}
static void f3_topline(const F3VIEW* v)
{
    char b[80];
    m3_vgrad(&m3_scr, 0, 0, M3W, 40, 0x000000, 0x000000, 150);
    m3_text_sh(&m3_scr, 12, 6, 2, v->app, 0xFFFFFF);
    m3_text(&m3_scr, 24 + 12 * (int)strlen(v->app), 12, 0, v->title, 0xC0D0F0, 256);
    if (m3_now() < m3_note_until)
        snprintf(b, sizeof b, "%s  %s %.0f fps", f3_stylename[m3_style],
                 m3_quality == 1 ? "full" : m3_quality == 2 ? "half" : m3_half ? "auto: half" : "auto: full", m3_fps);
    else snprintf(b, sizeof b, "%s", v->status);
    m3_text(&m3_scr, M3W - 8 - 8 * (int)strlen(b), 12, 0, b, m3_now() < m3_note_until ? 0x8090B0 : v->statcol, 256);
}
static void f3_tabs_top(const F3VIEW* v, int y)
{
    int i, x = 12;
    for (i = 0; i < v->ntab; i++)
    {
        int w = (int)strlen(v->tabs[i]) * 8 + 16, on = i == v->tab;
        if (x + w > M3W - 12) break;
        m3_rect(&m3_scr, x, y, w, 22, on ? v->accent : 0x101830, on ? 230 : 170);
        m3_bevel(&m3_scr, x, y, w, 22, 2, on, 180);
        m3_text(&m3_scr, x + 8, y + 3, 0, v->tabs[i], on ? 0xFFFFFF : 0x90A0C0, 256);
        f3_hit(x, y, w, 22, 1, i);
        x += w + 4;
    }
}

/* ------------------------------------------------------------------ style 0: panels */
static void f3_draw_panels(const F3VIEW* v)
{
    int lx, lw, i, rows, top = 0, rh = 26, y0 = 72, ly = 66, lh = 292;
    f3_background();
    m3_shadow(&m3_scr, 12, 10, M3W - 24, 46, 6);
    m3_vgrad(&m3_scr, 12, 10, M3W - 24, 46, m3_mul(v->accent, 170), m3_mul(v->accent, 60), 256);
    m3_bevel(&m3_scr, 12, 10, M3W - 24, 46, 3, 0, 200);
    m3_text_sh(&m3_scr, 28, 18, 3, v->app, 0xFFFFFF);
    { int tx = 40 + 16 * (int)strlen(v->app); char t[60]; f3_fit(t, sizeof t, v->title, (M3W - 40 - tx - 8 * (int)strlen(v->status)) / 8 - 2); m3_text(&m3_scr, tx, 27, 0, t, 0xD0E0FF, 256); }
    m3_text(&m3_scr, M3W - 28 - 8 * (int)strlen(v->status), 27, 0, v->status, v->statcol, 256);
    if (v->ntab > 1)
    {   /* tabs down the left */
        int th = v->ntab > 6 ? 36 : 42;
        for (i = 0; i < v->ntab; i++)
        {
            int on = i == v->tab, yy = 66 + i * (th + 4);
            m3_shadow(&m3_scr, 14 + on * 2, yy + on * 2, 150, th, on ? 2 : 5);
            m3_vgrad(&m3_scr, 14 + on * 2, yy + on * 2, 150, th, on ? m3_mul(v->accent, 300) : 0x485878, on ? m3_mul(v->accent, 120) : 0x283048, 256);
            m3_bevel(&m3_scr, 14 + on * 2, yy + on * 2, 150, th, 3, on, 220);
            m3_text_sh(&m3_scr, 26 + on * 2, yy + th / 2 - 10 + on * 2, 1, v->tabs[i], 0xFFFFFF);
            f3_hit(14, yy, 152, th + 2, 1, i);
        }
        lx = 178; lw = M3W - 192;
    }
    else { lx = 12; lw = M3W - 24; }
    m3_shadow(&m3_scr, lx, ly, lw, lh, 6);
    m3_rect(&m3_scr, lx, ly, lw, lh, 0x101828, 220);
    m3_bevel(&m3_scr, lx, ly, lw, lh, 2, 1, 200);
    rows = (lh - 12) / rh;
    if (v->sel >= rows) top = v->sel - rows + 1;
    if (top > 0 && v->rows[top].kind != F3_HEAD && top > 0 && v->rows[top - 1].kind == F3_HEAD && v->sel - top < rows - 1) top--;
    {
        float hy = y0 + (f3_selanim - top) * rh;
        if (hy >= ly + 2 && hy < ly + lh - rh) m3_rect(&m3_scr, lx + 6, (int)hy - 1, lw - 12, rh, v->accent, 70);
    }
    {
        int vw = lw * 46 / 100, vx = lx + lw - vw - 14, labw = vx - lx - 44;
        for (i = top; i < v->nrows && i < top + rows; i++)
        {
            const F3ROW* r = &v->rows[i]; int y = y0 + (i - top) * rh, on = i == v->sel; char t[80];
            uint32_t lc = r->dim ? 0x7880A0 : (r->col ? r->col : 0xFFFFFF);
            if (r->kind == F3_HEAD)
            {
                m3_text(&m3_scr, lx + 12, y + 6, 0, r->label, m3_mul(v->accent, 420), 256);
                m3_rect(&m3_scr, lx + 20 + 8 * (int)strlen(r->label), y + 13, lw - 40 - 8 * (int)strlen(r->label), 1, m3_mul(v->accent, 200), 160);
                continue;
            }
            if (on) { m3_vgrad(&m3_scr, lx + 8, y, lw - 16, rh - 2, m3_mul(v->accent, 300), m3_mul(v->accent, 140), 200); m3_bevel(&m3_scr, lx + 8, y, lw - 16, rh - 2, 2, 0, 200); }
            if (r->mark) f3_mark(&m3_scr, lx + 14, y + 3, r->mark, r->col ? r->col : 0xC0D0F0);
            f3_fit(t, sizeof t, r->label, (r->value[0] ? labw : lw - 60) / 10);
            m3_text_sh(&m3_scr, lx + 36, y + 2, 1, t, lc);
            if (r->value[0])
            {
                m3_rect(&m3_scr, vx, y + 2, vw, rh - 6, 0x000000, on ? 120 : 90);
                m3_bevel(&m3_scr, vx, y + 2, vw, rh - 6, 1, 1, 160);
                f3_fit(t, sizeof t, r->value, (vw - 32) / 8);
                m3_text(&m3_scr, vx + 16, y + 4, 0, t, r->dim ? 0x707890 : (on ? 0xFFFFA0 : 0xD0E0F0), 256);
                if (r->kind == F3_VALUE)
                {
                    f3_tri(&m3_scr, vx + 4, y + 7, -1, on ? 0xFFFFFF : 0x8090B0);
                    f3_tri(&m3_scr, vx + vw - 10, y + 7, 1, on ? 0xFFFFFF : 0x8090B0);
                    f3_hit(vx, y, 16, rh, 3, i); f3_hit(vx + vw - 16, y, 16, rh, 4, i);
                }
                else f3_mark(&m3_scr, vx + vw - 14, y + 2, F3M_GO, on ? 0xFFFFFF : 0x6070A0);
            }
            f3_hit(lx + 8, y, lw - 16, rh, 2, i);
        }
        if (top > 0) { int q; for (q = 0; q < 4; q++) m3_rect(&m3_scr, lx + lw - 10 - q, ly + 4 + q, 1 + 2 * q, 1, 0xC0D0F0, 256); }
        if (top + rows < v->nrows) { int q; for (q = 0; q < 4; q++) m3_rect(&m3_scr, lx + lw - 10 - q, ly + lh - 6 - q, 1 + 2 * q, 1, 0xC0D0F0, 256); }
    }
    f3_infobox(v, 366, 104, 0);
}

/* ------------------------------------------------------------------ style 1: carousel */
#define F3_CARDS 24
typedef struct { M3SURF s; char key[160]; unsigned used; } F3CARD;
static F3CARD f3_cards[F3_CARDS]; static unsigned f3_cardclock;
static const M3SURF* f3_card(const F3VIEW* v, const F3ROW* r)
{
    char key[160]; int i, best = 0;
    snprintf(key, sizeof key, "%s|%s|%d%d%06X%06X", r->label, r->value, r->mark, r->dim, (unsigned)r->col, (unsigned)v->accent);
    for (i = 0; i < F3_CARDS; i++) if (f3_cards[i].s.p && !strcmp(f3_cards[i].key, key)) { f3_cards[i].used = ++f3_cardclock; return &f3_cards[i].s; }
    for (i = 0; i < F3_CARDS; i++) { if (!f3_cards[i].s.p) { best = i; break; } if (f3_cards[i].used < f3_cards[best].used) best = i; }
    {
        F3CARD* c = &f3_cards[best]; M3SURF* s = &c->s; uint32_t top = m3_mul(v->accent, 150), bot = m3_mul(v->accent, 50); int y;
        if (!s->p) *s = m3_surf(256, 128);
        m3_vgrad(s, 0, 0, 256, 128, top, bot, 256);
        for (y = 0; y < 128; y += 4) m3_rect(s, 0, y, 256, 1, 0xFFFFFF, 10);
        if (r->dim) m3_rect(s, 0, 0, 256, 128, 0, 120);
        m3_bevel(s, 0, 0, 256, 128, 4, 0, 160);
        if (r->mark) { m3_rect(s, 8, 8, 26, 26, 0, 90); f3_mark(s, 13, 12, r->mark, r->col ? r->col : 0xFFFFFF); }
        m3_wrap(s, 40, 12, 208, 2, 1, r->label, r->dim ? 0x9090A0 : 0xFFFFFF, 1);
        if (r->value[0])
        {
            m3_rect(s, 10, 64, 236, 54, 0x000000, 110); m3_bevel(s, 10, 64, 236, 54, 1, 1, 150);
            m3_wrap(s, 16, 70, 224, 2, 1, r->value, r->kind == F3_VALUE ? 0xFFFFA0 : 0xD0E0F0, 0);
            if (r->kind == F3_VALUE) { f3_tri(s, 12, 86, -1, 0xFFFFFF); f3_tri(s, 239, 86, 1, 0xFFFFFF); }
        }
        snprintf(c->key, sizeof c->key, "%s", key); c->used = ++f3_cardclock;
        return s;
    }
}
static void f3_draw_carousel(const F3VIEW* v, double t)
{
    int k[F3_MAXROWS], n = 0, i, pass, at = 0, vis, span;
    float step, R;
    for (i = 0; i < v->nrows; i++) if (v->rows[i].kind != F3_HEAD) { if (i == v->sel) at = n; k[n++] = i; }
    vis = n < 7 ? 7 : n > 14 ? 14 : n;          /* long lists: a turning arc of the nearest cards */
    span = n > 14 ? 6 : n;
    step = 6.2831853f / vis;
    R = vis * 0.42f; if (R < 2.8f) R = 2.8f;
    m3_fog = 0x0A0820; m3_fog0 = R + 4.5f; m3_fog1 = R * 2 + 10;
    m3_camera(0, 0.35f + 0.05f * sinf((float)t * 0.7f), -(R + 3.9f), 0.04f * sinf((float)t * 0.3f), -0.13f);
    m3_sky();
    for (pass = 0; pass < 2; pass++)
    {
        if (pass == 1) m3_draw_floor(-1.0f, 160);
        for (i = 0; i < n; i++)
        {
            float a, x, z, y, sc, on = i == at;
            if (fabsf(i - f3_ring) > span + 0.5f) continue;
            a = (i - f3_ring) * step; x = R * sinf(a); z = -R * cosf(a);
            sc = on ? 1.12f : 1.0f;
            y = (on ? 0.08f * sinf((float)t * 2.2f) : 0) - f3_tabanim * (1.0f + 0.25f * (i - at < 0 ? at - i : i - at));
            if (pass == 0) y = -2.0f - y;
            m3_boxback = m3_mul(v->accent, 110);
            m3_box(m3_v(x, y, z), 1.0f * sc, 0.5f * sc, 0.05f, -a, f3_card(v, &v->rows[k[i]]), 0x303848, on ? 0.35f + 0.15f * sinf((float)t * 3) : 0, 256);
        }
        if (pass == 0)
        {
            int yy, q;
            for (yy = 0; yy < m3_rh; yy++)
                for (q = yy * M3W; q < yy * M3W + m3_rw; q++) if (m3_zb[q] != 0) { m3_fb[q] = m3_mul(m3_fb[q], 90); m3_zb[q] = 0; }
        }
    }
    m3_boxback = 0;
    m3_upscale();
    f3_hit(0, 70, 170, 295, 5, 0); f3_hit(470, 70, 170, 295, 6, 0);
    if (f3_selectable(v, v->sel)) f3_hit(170, 70, 300, 295, 2, v->sel);
    f3_topline(v);
    if (v->ntab > 1) f3_tabs_top(v, 44);
    { char b[24]; snprintf(b, sizeof b, "%d / %d", at + 1, n); m3_text(&m3_scr, M3W - 16 - 8 * (int)strlen(b), 48, 0, b, 0xA0B0D0, 256); }
    f3_infobox(v, 352, 118, 1);
}

/* ------------------------------------------------------------------ style 2: module */
static M3SURF f3_panel;
static void f3_make_panel(const F3VIEW* v, double t)
{
    M3SURF* s = &f3_panel; int i, x, y, at, n; char line[48];
    const F3ROW* r = f3_selectable(v, v->sel) ? &v->rows[v->sel] : NULL;
    if (!s->p) *s = m3_surf(512, 128);
    at = f3_index(v, v->sel, &n);
    for (y = 0; y < 128; y++) for (x = 0; x < 512; x++) { int g = 46 + ((x * 7 + y * 13) % 5) + ((y & 1) ? 2 : 0); s->p[y * 512 + x] = m3_rgb(g, g, g + 4); }
    m3_bevel(s, 0, 0, 512, 128, 3, 0, 120);
    m3_text(s, 12, 8, 2, "NUKED", 0xE8E8F0, 256);
    m3_text(s, 12, 36, 0, "SB-EMU", 0xB8B8C8, 256);
    { char a[20]; snprintf(a, sizeof a, "%.16s", v->app); m3_text(s, 12, 54, 0, a, 0x8A8A98, 256); }
    m3_rect(s, 150, 10, 250, 64, 0x101810, 256);
    m3_bevel(s, 150, 10, 250, 64, 2, 1, 200);
    m3_vgrad(s, 154, 14, 242, 56, m3_mul(0x8CC838, 300), m3_mul(0x8CC838, 220), 256);
    snprintf(line, sizeof line, "%-.20s %d/%d", v->ntab > 1 ? v->tabs[v->tab] : v->title, at + 1, n);
    m3_text(s, 160, 17, 0, line, 0x1C2C10, 256);
    if (r)
    {
        int len = (int)strlen(r->value), off = len > 29 ? (int)(t * 5) % (len + 8) : 0; char sc[32];
        snprintf(line, sizeof line, "%.29s", r->label); m3_text(s, 160, 34, 0, line, 0x10200A, 256);
        for (i = 0; i < 29; i++) { int p = off + i; sc[i] = p < len ? r->value[p] : ' '; } sc[29] = 0;
        m3_text(s, 160, 51, 0, sc, 0x2A3C18, 256);
    }
    for (i = 0; i < v->ntab && i < 6; i++)
    {
        int bx = 150 + i * 42, on = i == v->tab; char sh[6];
        snprintf(sh, sizeof sh, "%.4s", v->tabs[i]);
        for (x = 0; sh[x]; x++) sh[x] = (char)toupper((unsigned char)sh[x]);
        m3_rect(s, bx + 12, 82, 10, 5, on ? 0xFF3020 : 0x401010, 256);
        m3_text(s, bx + 17 - (int)strlen(sh) * 4, 110, 0, sh, on ? 0xFFFFFF : 0xA0A0B0, 256);
    }
    m3_text(s, 432, 104, 0, "SELECT", 0xB0B0C0, 200);
    m3_rect(s, 492, 12, 8, 8, ((int)(t * 2) & 1) ? 0x30FF40 : 0x186020, 256);
    m3_text(s, 470, 24, 0, "PWR", 0x9090A0, 200);
}
static void f3_draw_module(const F3VIEW* v, double t)
{
    float W = 2.9f, H = 0.72f, D = 1.6f; int c;
    m3_fog = 0x0A0820; m3_fog0 = 8; m3_fog1 = 22;
    m3_camera(0.9f * sinf((float)t * 0.25f), 0.15f + 0.1f * sinf((float)t * 0.17f), -6.6f, -0.14f * sinf((float)t * 0.25f), -0.12f);
    m3_sky();
    m3_draw_floor(-H - 0.02f, 256);
    f3_make_panel(v, t);
    m3_box(m3_v(0, 0, 0), W, H, D, 0, &f3_panel, 0x26262C, 0, 256);
    m3_box(m3_v(-W + 0.3f, -H - 0.01f, -D + 0.3f), 0.12f, 0.02f, 0.12f, 0, NULL, 0x101010, 0, 256);
    m3_box(m3_v(W - 0.3f, -H - 0.01f, -D + 0.3f), 0.12f, 0.02f, 0.12f, 0, NULL, 0x101010, 0, 256);
    m3_box(m3_v(-W - 0.12f, 0, -D + 0.03f), 0.12f, H, 0.03f, 0, NULL, 0x303036, 0, 256);
    m3_box(m3_v(W + 0.12f, 0, -D + 0.03f), 0.12f, H, 0.03f, 0, NULL, 0x303036, 0, 256);
    for (c = 0; c < v->ntab && c < 6; c++)
    {
        float u = (150 + c * 42 + 17) / 512.0f, vv = 96 / 128.0f, x = -W + u * 2 * W, y = H - vv * 2 * H, p = c == v->tab ? f3_tabanim : 0;
        m3_box(m3_v(x, y, -D - 0.06f + p * 0.05f), 0.11f, 0.06f, 0.05f, 0, NULL, c == v->tab ? m3_mul(v->accent, 230) : 0x505058, c == v->tab ? 0.2f : 0, 256);
    }
    m3_knob3d(m3_v(-W + (440 / 512.0f) * 2 * W, H - (56 / 128.0f) * 2 * H, -D - 0.16f), 0.34f, 0.16f, f3_knob, 0);
    m3_upscale();
    f3_hit(0, 70, 170, 280, 5, 0); f3_hit(470, 70, 170, 280, 6, 0);
    if (f3_selectable(v, v->sel)) f3_hit(170, 70, 300, 280, 2, v->sel);
    f3_topline(v);
    if (v->ntab > 1) f3_tabs_top(v, 44);
    f3_infobox(v, 352, 118, 1);
}

/* ------------------------------------------------------------------ a frame */
static void f3_scene(const F3VIEW* v)
{
    double t = m3_now(); float dt = (float)(t - f3_last); int at, n;
    f3_last = t; if (dt > 0.2f) dt = 0.2f;
    if (v->tab != f3_lasttab) { f3_tabanim = 1; f3_lasttab = v->tab; f3_selanim = (float)v->sel; at = f3_index(v, v->sel, &n); f3_ring = (float)at; }
    at = f3_index(v, v->sel, &n);
    f3_selanim += (v->sel - f3_selanim) * (1 - expf(-dt * 14));
    f3_ring += (at - f3_ring) * (1 - expf(-dt * 9));
    f3_knob += (-at * 0.5f - f3_knob) * (1 - expf(-dt * 10));
    f3_tabanim *= expf(-dt * 6);
    m3_bgphase = (float)t;
    f3_nhits = 0;
    if (m3_style == 1) f3_draw_carousel(v, t);
    else if (m3_style == 2) f3_draw_module(v, t);
    else f3_draw_panels(v);
}
static void f3_frame(const F3VIEW* v)
{
    double t;
    if (!f3_on) return;
    f3_scene(v);
    f3_pointer();
    m3_present();
    t = m3_now();
    if (++f3_frames, t - f3_fps_t >= 1.0)
    {
        m3_fps = (float)(f3_frames / (t - f3_fps_t)); f3_frames = 0; f3_fps_t = t;
        if (m3_quality == 0 && !m3_half && m3_style != 0 && t > 2.5 && m3_fps < 15)
        { m3_half = 1; m3_rw = M3W / 2; m3_rh = M3H / 2; m3_note_until = t + 5; }
    }
}

/* ------------------------------------------------------------------ input */
static int f3_rawkey(void)
{
    int c;
    if (!kbhit()) return 0;
    c = getch(); if (c == 0 || c == 0xE0) c = 0x100 | getch();
    return c;
}
/* mouse: returns a key, or 0. Button clicks give 0x1F7 with f3_mval = the button */
static int f3_mousekey(F3VIEW* v)
{
    __dpmi_regs r; int b, w, i, k = 0;
    if (f3_mouse <= 0) return 0;
    memset(&r, 0, sizeof r); r.x.ax = 3; __dpmi_int(0x33, &r);
    b = r.x.bx & 7; w = (signed char)r.h.bh;
    if (r.x.cx != f3_mx || r.x.dx != f3_my) { f3_mx = r.x.cx; f3_my = r.x.dx; f3_mseen = 1; }
    if ((b & 1) && !(f3_mb & 1))
    {
        f3_mseen = 1;
        for (i = f3_nhits - 1; i >= 0; i--)
        {
            const F3HIT* h = &f3_hits[i];
            if (f3_mx < h->x || f3_mx >= h->x + h->w || f3_my < h->y || f3_my >= h->y + h->h) continue;
            f3_mval = h->val;
            switch (h->kind)
            {
            case 1: k = F3K_TABSEL; break;
            case 2: if (!v || h->val == v->sel) k = F3K_ENTER; else { v->sel = h->val; k = F3K_REDRAW; } break;
            case 3: if (v) v->sel = h->val; k = F3K_LEFT; break;
            case 4: if (v) v->sel = h->val; k = F3K_RIGHT; break;
            case 5: k = F3K_UP; break;
            case 6: k = F3K_DOWN; break;
            case 7: k = 0x1F7; break;
            }
            break;
        }
    }
    else if ((b & 2) && !(f3_mb & 2)) k = F3K_ESC;
    else if (w) k = w > 0 ? F3K_DOWN : F3K_UP;
    f3_mb = b;
    return k;
}
/* the next key for a list screen; handles F3 (style) and F4 (speed) itself. 0 = nothing yet */
static int f3_key(F3VIEW* v)
{
    int c = f3_rawkey();
    if (!c) c = f3_mousekey(v);
    if (c == F3K_F3) { m3_style = (m3_style + 1) % F3_STYLES; f3_cfg_save(); f3_tabanim = 1; m3_note_until = m3_now() + 3; return F3K_REDRAW; }
    if (c == F3K_F4)
    {
        m3_quality = (m3_quality + 1) % 3; m3_half = m3_quality == 2;
        m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H;
        f3_cfg_save(); m3_note_until = m3_now() + 5;
        return F3K_REDRAW;
    }
    return c;
}

/* ------------------------------------------------------------------ pop-ups (in 3D) */
static void f3_window(int x, int y, int w, int h, const char* title, uint32_t col)
{
    m3_rect(&m3_scr, 0, 0, M3W, M3H, 0x000000, 110);
    m3_shadow(&m3_scr, x, y, w, h, 8);
    m3_vgrad(&m3_scr, x, y, w, h, m3_mul(col, 120), m3_mul(col, 45), 245);
    m3_bevel(&m3_scr, x, y, w, h, 3, 0, 220);
    m3_vgrad(&m3_scr, x + 4, y + 4, w - 8, 26, m3_mul(col, 220), m3_mul(col, 110), 256);
    m3_text_sh(&m3_scr, x + 14, y + 7, 1, title, 0xFFFFFF);
}
static void f3_button(int x, int y, int w, const char* t, int on, int id)
{
    m3_vgrad(&m3_scr, x, y, w, 28, on ? 0x40C060 : 0x485878, on ? 0x206030 : 0x283048, 256);
    m3_bevel(&m3_scr, x, y, w, 28, 2, 0, 220);
    m3_text_c(&m3_scr, x + w / 2, y + 4, 1, t, 0xFFFFFF, 1);
    f3_hit(x, y, w, 28, 7, id);
}
/* buttons (first letters are hot keys); Enter = the highlighted one, Esc = the last. Returns its number */
static int f3_choice(const F3VIEW* v, const char* title, const char* q, const char** b, int nb)
{
    int sel = 0, c, lines;
    for (;;)
    {
        int w = 480, h, x, y, i, bw = 120, bx;
        if (v) f3_scene(v); else { f3_nhits = 0; f3_background(); }
        f3_nhits = 0;
        lines = (int)strlen(q) * 8 / (w - 40) + 1; if (lines > 6) lines = 6;
        h = 92 + lines * 18; x = (M3W - w) / 2; y = (M3H - h) / 2;
        f3_window(x, y, w, h, title, v ? v->accent : 0x4060C0);
        m3_wrap(&m3_scr, x + 20, y + 42, w - 40, lines, 0, q, 0xF0F0FF, 0);
        bx = x + (w - nb * (bw + 12) + 12) / 2;
        for (i = 0; i < nb; i++) f3_button(bx + i * (bw + 12), y + h - 42, bw, b[i], i == sel, i);
        f3_pointer(); m3_present();
        c = f3_rawkey(); if (!c) c = f3_mousekey(NULL);
        if (!c) continue;
        if (c == 0x1F7) return f3_mval;
        if (c == F3K_ENTER) return sel;
        if (c == F3K_ESC) return nb - 1;
        if (c == F3K_LEFT || c == F3K_STAB) sel = (sel + nb - 1) % nb;
        if (c == F3K_RIGHT || c == F3K_TAB) sel = (sel + 1) % nb;
        if (c < 256) for (i = 0; i < nb; i++) if (toupper(c) == toupper((unsigned char)b[i][0])) return i;
    }
}
static int f3_yesno(const F3VIEW* v, const char* q) { static const char* b[2] = { "Yes", "No" }; return f3_choice(v, "Question", q, b, 2) == 0; }
static void f3_message(const F3VIEW* v, const char* title, const char* msg) { static const char* b[1] = { "OK" }; f3_choice(v, title, msg, b, 1); }

/* choose one of n items; returns its number or -1 */
static int f3_pick(const F3VIEW* v, const char* title, const char** items, int n, int cur)
{
    int sel = cur < 0 ? 0 : cur, top = 0, c;
    if (n <= 0) return -1;
    for (;;)
    {
        int rows = n < 10 ? n : 10, w = 360, h = 52 + rows * 26, x = (M3W - w) / 2, y = (M3H - h) / 2, i, wid = 0;
        for (i = 0; i < n; i++) if ((int)strlen(items[i]) > wid) wid = (int)strlen(items[i]);
        if (wid * 10 + 60 > w) w = wid * 10 + 60 > M3W - 40 ? M3W - 40 : wid * 10 + 60;
        x = (M3W - w) / 2;
        if (sel < top) top = sel;
        if (sel >= top + rows) top = sel - rows + 1;
        if (v) f3_scene(v); else f3_background();
        f3_nhits = 0;
        f3_window(x, y, w, h, title, v ? v->accent : 0x4060C0);
        for (i = top; i < n && i < top + rows; i++)
        {
            int yy = y + 38 + (i - top) * 26; char t[80];
            if (i == sel) { m3_vgrad(&m3_scr, x + 8, yy, w - 16, 24, 0x60A0FF, 0x2050A0, 256); m3_bevel(&m3_scr, x + 8, yy, w - 16, 24, 2, 0, 200); }
            f3_fit(t, sizeof t, items[i], (w - 50) / 10);
            m3_text_sh(&m3_scr, x + 32, yy + 2, 1, t, i == cur ? 0xFFFFA0 : 0xFFFFFF);
            if (i == cur) f3_mark(&m3_scr, x + 12, yy + 3, F3M_GO, 0xFFFFA0);
            f3_hit(x + 8, yy, w - 16, 24, 7, i);
        }
        if (top > 0) m3_text(&m3_scr, x + w - 24, y + 36, 0, "^", 0xFFFFFF, 256);
        if (top + rows < n) m3_text(&m3_scr, x + w - 24, y + h - 20, 0, "v", 0xFFFFFF, 256);
        f3_pointer(); m3_present();
        c = f3_rawkey(); if (!c) c = f3_mousekey(NULL);
        if (!c) continue;
        if (c == 0x1F7) { if (f3_mval == sel) return sel; sel = f3_mval; continue; }
        if (c == F3K_ENTER || c == ' ') return sel;
        if (c == F3K_ESC) return -1;
        if (c == F3K_UP) sel = (sel + n - 1) % n;
        else if (c == F3K_DOWN) sel = (sel + 1) % n;
        else if (c == F3K_HOME) sel = 0;
        else if (c == F3K_END) sel = n - 1;
        else if (c == F3K_PGUP) { sel -= rows; if (sel < 0) sel = 0; }
        else if (c == F3K_PGDN) { sel += rows; if (sel >= n) sel = n - 1; }
    }
}
/* a line of text; returns 0 on Esc */
static int f3_input(const F3VIEW* v, const char* title, const char* prompt, char* buf, int max, int upper)
{
    char t[256]; int n, c;
    snprintf(t, sizeof t, "%s", buf); n = (int)strlen(t);
    if (max > (int)sizeof t - 1) max = (int)sizeof t - 1;
    for (;;)
    {
        int w = 600, h = 140, x = (M3W - w) / 2, y = (M3H - h) / 2, cols = (w - 48) / 8, off = n > cols - 1 ? n - cols + 1 : 0;
        if (v) f3_scene(v); else f3_background();
        f3_nhits = 0;
        f3_window(x, y, w, h, title, v ? v->accent : 0x4060C0);
        m3_wrap(&m3_scr, x + 20, y + 40, w - 40, 2, 0, prompt, 0xE0E8FF, 0);
        m3_rect(&m3_scr, x + 20, y + 84, w - 40, 26, 0x000000, 200); m3_bevel(&m3_scr, x + 20, y + 84, w - 40, 26, 2, 1, 200);
        m3_text(&m3_scr, x + 26, y + 89, 0, t + off, 0xFFFFFF, 256);
        if (((int)(m3_now() * 3)) & 1) m3_rect(&m3_scr, x + 26 + (n - off) * 8, y + 104, 8, 2, 0xFFFFA0, 256);
        m3_text(&m3_scr, x + 20, y + h - 20, 0, "Enter OK   Esc cancel   Backspace delete", 0x8090B0, 256);
        f3_pointer(); m3_present();
        c = f3_rawkey(); if (!c) c = f3_mousekey(NULL);
        if (!c) continue;
        if (c == F3K_ENTER) { snprintf(buf, (size_t)max + 1, "%s", t); return 1; }
        if (c == F3K_ESC) return 0;
        if (c == F3K_BS) { if (n) t[--n] = 0; continue; }
        if (c >= 32 && c < 127 && n < max) { t[n++] = (char)(upper ? toupper(c) : c); t[n] = 0; }
    }
}
#endif
