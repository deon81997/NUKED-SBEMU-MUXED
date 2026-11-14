/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* menu3d.h - the optional 3D menu of SC55MENU (included by sc55menu.c, uses its items and actions).
 *
 * The text menu stays the main one. F8 (or Settings > 3D menu) switches to this one and back; the choice
 * is kept in MENU3D.CFG (no file = text menu). Holding Shift at start (safe mode) always gives the text menu.
 *
 * Everything is drawn by the CPU (no 3D card needed) into a 32-bit back buffer with a z-buffer, then converted to
 * the screen mode's pixel format. Since beta.106 the menu works like MIDPLAY's 3D view:
 *   - any VESA mode 320x200 .. 1600x1200 (8 bit dithered, 15/16/24/32 bit) or VGA 320x200, at any refresh rate the
 *     BIOS can set (VBE 3.0); F4 opens the screen list. A new mode asks "Keep this mode? Y" for 10 s, anything else
 *     (or a black screen and no key) brings the last good one back. A mode that cannot be set falls back to the last
 *     good one, then the best 640x480, then VGA 320x200, then the text menu.
 *   - low memory: the frame and depth buffers are allocated BEFORE the mode is set; too little memory for the mode
 *     picks the biggest smaller one that fits (a note says so). Card textures that do not fit are left plain.
 *   - the same 11 colour themes as MIDPLAY (F6, 0 = the text style's colours) and the same three looks (F7):
 *     solid, vector (glowing lines with an afterglow) and voxel (built from blocks).
 *   - mouse everywhere (menus, items, the key bar, the help, the screen list, the keep-mode question), F1 help,
 *     the same key bar as the text menu, and a crash catcher in sc55menu.c (CRASH.TXT with the last 50 actions).
 * Styles (F3): 0 Panels (flat 3D buttons, lightest), 1 Carousel (cards on a turning ring), 2 Module (a rack module).
 */
#define M3_DYNAMIC_RES               /* any VESA mode (m3core.h), not only 640x480 */
#include "m3core.h"
#include "m3looks.h"
/* ------------------------------------------------------------------------------------------- scene data */
#define m3_catcol m3_accent                    /* the category colours: the colour theme's bright accents */
static const char* m3_stylename[] = { "Panels", "Carousel", "Module" };
static const char* const m3_lookname[3] = { "solid", "vector", "voxel" };
#define M3_STYLES 3
static int m3_look, m3_ctheme;               /* F7 look, F6 colour theme (0 = the text style's colours) */
static int m3_on = 1;                        /* MENU3D.CFG ON=: start in the 3D menu */
static int g_m3other[4];                     /* the other place's screen mode (DOS / Windows DOS box), kept as it is */
/* mouse: what is under the pointer, collected while drawing (last added wins). kind: 1 menu, 2 item, 3 = the
 * chosen item (run it), 4 previous item, 5 next item, 6 a key (val = key code), 7 a row of the screen list */
typedef struct { short x, y, w, h, kind, val; } M3HIT;
static M3HIT m3_hits[96]; static int m3_nhits;
static void m3_hit(int x, int y, int w, int h, int kind, int val)
{ if (m3_nhits < 96) { M3HIT* t = &m3_hits[m3_nhits++]; t->x = (short)x; t->y = (short)y; t->w = (short)w; t->h = (short)h; t->kind = (short)kind; t->val = (short)val; } }
static int m3_mouse = -1, m3_mx = 320, m3_my = 240, m3_mb, m3_mseen;
#define M3_MAXCARDS 24
static M3SURF m3_cards[M3_MAXCARDS]; /* card textures of the current category (by position in it); NULL = plain */
static int    m3_cards_cat = -1;
static M3SURF m3_panel;
static float  m3_ring, m3_catanim, m3_knob, m3_btnpress[NCAT], m3_selanim;
/* the module's colours: a theme other than 0 sets them */
static uint32_t m3_body = 0x26262C, m3_lcd = 0x8CC838, m3_ink = 0x10200A;

/* ------------------------------------------------------------------------------------------- layout */
/* everything 2D is placed from the screen size: small screens (under 640x400) get one font size, short texts and
 * a category switcher instead of tabs; big ones (1024x768 and up) bigger fonts and rows */
typedef struct { int sm, big, hdr, f, rowh, catw, catf, top, bot, dy, dh, df, dl; } M3LAY;
static M3LAY L;
static void m3_layout(void)
{
    int W = M3W, H = M3H;
    L.sm = W < 640 || H < 400; L.big = !L.sm && W >= 1024 && H >= 768;
    if (L.sm) { L.hdr = 26; L.f = 0; L.rowh = 18; L.catw = W >= 480 ? 120 : 78; L.catf = 0; L.dh = H >= 240 ? 66 : 50; L.df = 0; }
    else if (L.big) { L.hdr = 66; L.f = 2; L.rowh = 52; L.catw = 210; L.catf = 2; L.dh = 150; L.df = 1; }
    else { L.hdr = 66; L.f = 1; L.rowh = 40; L.catw = 150; L.catf = 1; L.dh = 124; L.df = 0; }   /* 5 lines of description */
    L.dy = H - L.dh - (L.sm ? 2 : 10);
    L.top = L.hdr; L.bot = L.dy - (L.sm ? 3 : 10);
    L.dl = (L.dh - (L.sm ? 22 : 34)) / (m3_fh(L.df) + 2); if (L.dl < 1) L.dl = 1;
}
/* the key bar: the same keys as the text menu's, as many as fit (the most useful first), each one clickable */
static void m3_keybar(int x, int y, int w, uint32_t c)
{
    static const struct { int key, pri; const char* t; const char* ts; } kb[] = {
        { K_ENTER, 0, "Enter go", "Enter" }, { K_F1, 0, "F1 help", "F1 help" }, { K_F2, 7, "F2 saved", "F2" },
        { K_F3, 3, "F3 style", "F3" }, { 0x13E, 2, "F4 screen", "F4" }, { 0x13F, 6, "F5 MIDI", "F5" },
        { 0x140, 4, "F6 colours", "F6" }, { 0x141, 5, "F7 look", "F7" }, { 0x142, 1, "F8 text", "F8" }, { K_ESC, 0, "Esc quit", "Esc" } };
    int n = (int)(sizeof kb / sizeof kb[0]), pri, i, shrt, x0 = x;
    for (shrt = 0; shrt < 2; shrt++)
        for (pri = 7; pri >= 0; pri--)
        {
            int tw = 0;
            for (i = 0; i < n; i++) if (kb[i].pri <= pri) tw += 8 * (int)strlen(shrt ? kb[i].ts : kb[i].t) + 12;
            if (tw - 12 > w && !(shrt && pri == 0)) continue;
            for (i = 0; i < n; i++) if (kb[i].pri <= pri)
            {
                const char* t = shrt ? kb[i].ts : kb[i].t; int tx = x, tl = 8 * (int)strlen(t);
                if (x + tl > x0 + w) break;
                x = m3_text(&m3_scr, x, y, 0, t, c, 256);
                m3_hit(tx - 3, y - 2, tl + 6, 20, 6, kb[i].key);
                x += 12;
            }
            return;
        }
}

/* ------------------------------------------------------------------------------------------- textures */
static void m3_make_card(M3SURF* s, const ITEM* it)
{
    uint32_t c = m3_catcol[it->cat], top = m3_mul(c, 150), bot = m3_mul(c, 50); int ok = usable(it), y;
    if (!s->p) return;
    m3_vgrad(s, 0, 0, 256, 128, top, bot, 256);
    for (y = 0; y < 128; y += 4) m3_rect(s, 0, y, 256, 1, 0xFFFFFF, 10);
    m3_rect(s, 0, 0, 256, 128, 0, ok ? 0 : 140);
    m3_bevel(s, 0, 0, 256, 128, 4, 0, 160);
    m3_rect(s, 8, 8, 34, 34, 0x000000, 90);
    { const char* sym = it->kind == IT_GAME ? "P" : it->kind == IT_VIEW ? "=" : it->kind == IT_EDIT ? "+" : ">";
      m3_text_c(s, 25, 13, 2, sym, 0xFFFFFF, 1); }
    m3_text(s, 50, 16, 0, g_catname[it->cat], m3_mul(c, 400), 256);
    m3_wrap(s, 12, 50, 236, 3, 1, it->label, ok ? 0xFFFFFF : 0x9090A0, 1);
    if (!ok) m3_text(s, 12, 108, 0, it->needs_sbemu > 0 ? "start SBEMU first" : "restart PC first", 0xFFC060, 256);
}
/* card textures, one per place in the category, made when it is shown; out of memory: the rest stay plain boxes */
static void m3_cards_build(int cat)
{
    int k[80], n = cat_items(cat, k), i;
    static int warned;
    for (i = 0; i < n && i < M3_MAXCARDS; i++)
    {
        if (!m3_cards[i].p)
        {
            m3_cards[i] = m3_surf(256, 128);
            if (!m3_cards[i].p) { if (!warned) { ui_log("cards: low memory at %d", i); warned = 1; } break; }
        }
        m3_make_card(&m3_cards[i], &g_it[k[i]]);
    }
    m3_cards_cat = cat;
}
static void m3_cards_free(void)
{ int i; for (i = 0; i < M3_MAXCARDS; i++) { free(m3_cards[i].p); m3_cards[i].p = NULL; } m3_cards_cat = -1; }
static const M3SURF* m3_card(int pos) { return pos >= 0 && pos < M3_MAXCARDS && m3_cards[pos].p ? &m3_cards[pos] : NULL; }

/* the colour theme over the text style's colours (theme 0 = the style's own, F3 in the text menu) */
static void m3_theme_apply(void)
{
    static int last = -1, lastgen = -1; int i;
    m3_theme_sync();
    if (m3_ctheme == last && m3_theme_gen == lastgen) return;
    if (m3_ctheme > 0 && m3_ctheme < 11)
    {
        const M3THEME* T = &m3_themes[m3_ctheme];
        m3_sky0 = T->s0; m3_sky1 = T->s1; m3_gridc = m3_glowc = T->grid;
        m3_floorbg = m3_mul(T->s0, 300); m3_fogc = m3_mul(T->s1, 160);
        m3_tile0 = m3_mul(T->s1, 70); m3_tile1 = m3_mul(T->s1, 95);
        for (i = 0; i < 6; i++) m3_accent[i] = m3_hsv(T->h0 + T->hs * i / 5.0f, T->sat, 0.92f);
        m3_body = T->body; m3_lcd = T->l1; m3_ink = T->ink;
        m3_make_floor(); m3_theme_gen++;
    }
    else
    {
        if (last > 0) m3_theme_from_pal(tui_palette());
        m3_body = 0x26262C; m3_lcd = 0x8CC838; m3_ink = 0x10200A;
    }
    last = m3_ctheme; lastgen = m3_theme_gen;
}

/* why a grey item cannot run now (the text menu says it under the description) */
static const char* m3_reason_text(const ITEM* it)
{
    return it->needs_sbemu == 2 ? "program not found" : it->needs_sbemu > 0 ? "start the SC-55 first" : "restart the PC first";
}
static void m3_reason(const ITEM* it, int x, int y)
{
    const char* r = m3_reason_text(it); int w = 8 * (int)strlen(r);
    if (usable(it)) return;
    if (x + w > M3W - 8) x = M3W - 8 - w;
    if (x < 4) x = 4;
    m3_rect(&m3_scr, x - 4, y - 2, w + 8, 20, 0x401000, 200);
    m3_text(&m3_scr, x, y, 0, r, 0xFFB040, 256);
}

/* ------------------------------------------------------------------------------------------- 2D boxes by look */
/* solid: gradient + bevel + shadow; vector: see-through with a glowing outline; voxel: built from small blocks */
static void m3_box2d(int x, int y, int w, int h, uint32_t top, uint32_t bot, int in, int sh)
{
    if (w <= 0 || h <= 0) return;
    if (m3_look == 1)
    {
        uint32_t g = m3_lerpc(top, 0xFFFFFF, 60);
        m3_rect(&m3_scr, x, y, w, h, m3_mul(bot, 60), 150);
        m3_rect(&m3_scr, x, y, w, 1, g, 256); m3_rect(&m3_scr, x, y + h - 1, w, 1, g, 256);
        m3_rect(&m3_scr, x, y, 1, h, g, 256); m3_rect(&m3_scr, x + w - 1, y, 1, h, g, 256);
        m3_rect(&m3_scr, x - 1, y - 1, w + 2, 1, g, 70); m3_rect(&m3_scr, x - 1, y + h, w + 2, 1, g, 70);
        m3_rect(&m3_scr, x - 1, y, 1, h, g, 70); m3_rect(&m3_scr, x + w, y, 1, h, g, 70);
        return;
    }
    if (sh && !in) m3_shadow(&m3_scr, x, y, w, h, L.sm ? 2 : sh);
    if (m3_look == 2)
    {   /* blocks of 8 (6 on small screens), each with its own light edge */
        int q = L.sm ? 6 : 8, bx, by;
        for (by = 0; by < h; by += q)
        {
            uint32_t c = m3_lerpc(top, bot, h > 1 ? by * 256 / h : 0);
            int bh = by + q > h ? h - by : q;
            for (bx = 0; bx < w; bx += q)
            {
                int bw = bx + q > w ? w - bx : q; uint32_t cc = ((bx / q + by / q) & 1) ? m3_mul(c, 236) : c;
                m3_rect(&m3_scr, x + bx, y + by, bw, bh, cc, 256);
                m3_rect(&m3_scr, x + bx, y + by, bw, 1, 0xFFFFFF, in ? 20 : 60);
                m3_rect(&m3_scr, x + bx, y + by + bh - 1, bw, 1, 0, 70);
            }
        }
        return;
    }
    m3_vgrad(&m3_scr, x, y, w, h, top, bot, 256);
    m3_bevel(&m3_scr, x, y, w, h, L.sm ? 1 : 2, in, 200);
}
static void m3_fit(char* b, size_t n, const char* t, int px, int f)
{ int c = px / m3_fw(f); if (c < 0) c = 0; if (c > (int)n - 1) c = (int)n - 1; snprintf(b, n, "%.*s", c, t); }

/* ------------------------------------------------------------------------------------------- backgrounds */
static void m3_vector_fade(void)
{   /* the last frame fades to 5/8: an afterglow like a vector monitor */
    uint32_t* p = m3_fb; int i, n = M3W * M3H;
    m3_rw = M3W; m3_rh = M3H;
    for (i = 0; i < n; i++) { uint32_t v = p[i]; p[i] = (v >> 1 & 0x7F7F7F) + (v >> 3 & 0x1F1F1F); }
    memset(m3_zb, 0, (size_t)n * sizeof(float));
}
static void m3_vector_floor(float fy, float t)
{
    uint32_t dim = m3_mul(m3_gridc, 150); float off = fmodf(t * 1.2f, 1.0f); int i;
    for (i = -10; i <= 10; i++) m3_line3(m3_v(i * 1.0f, fy, -6), m3_v(i * 1.0f, fy, 22), dim);
    for (i = 0; i < 28; i++) { float z = -6 + i - off; m3_line3(m3_v(-10, fy, z), m3_v(10, fy, z), dim); }
}
static void m3_bg_panels(void)
{
    int x, y, t = (int)(m3_bgphase * 40);
    static int sx[M3WMAX], cx[M3WMAX], init, lgen = -1; static uint32_t lut[64];
    m3_rw = M3W; m3_rh = M3H;
    if (m3_look == 1)
    {   /* vector: a grid floor sliding towards you under the panels */
        m3_vector_fade();
        m3_camera(0, 1.2f, -6, 0, -0.12f);
        m3_vector_floor(-0.2f, m3_bgphase);
        return;
    }
    if (lgen != m3_theme_gen)
    {   /* the waves in the theme's desk colour: dark .. lighter, with a touch of the highlight */
        int i2; uint32_t d = m3_lerpc(m3_sky1, m3_accent[0], 40);
        for (i2 = 0; i2 < 64; i2++) lut[i2] = m3_lerpc(m3_mul(d, 70), m3_mul(d, 300), i2 * 4);
        lgen = m3_theme_gen;
    }
    if (!init) { for (x = 0; x < M3WMAX; x++) { sx[x] = (int)(14 * 256 * sinf(x * 0.01f)); cx[x] = (int)(14 * 256 * cosf(x * 0.01f)); } init = 1; }
    if (m3_look == 2)
    {   /* voxel: the waves in 16-pixel blocks (8 on small screens) */
        int q = L.sm ? 8 : 16;
        for (y = 0; y < M3H; y += q)
        {
            float b = (y + q / 2) * 0.013f + m3_bgphase * 0.6f; int sb = (int)(256 * sinf(b)), cb = (int)(256 * cosf(b));
            for (x = 0; x < M3W; x += q)
            {
                int xm = x + q / 2 < M3W ? x + q / 2 : M3W - 1, w = 24 + ((sx[xm] * cb + cx[xm] * sb) >> 16);
                uint32_t c = lut[(w + ((((x + y) / q + t / 32) & 1) ? 4 : 0)) & 63];
                m3_rect(&m3_scr, x, y, q, q, c, 256);
                m3_rect(&m3_scr, x, y, q, 1, 0xFFFFFF, 26); m3_rect(&m3_scr, x, y, 1, q, 0xFFFFFF, 18);
            }
        }
        return;
    }
    /* sin(x*0.01 + b) = sin(x*0.01) cos b + cos(x*0.01) sin b: two tables instead of a sinf per pixel */
    for (y = 0; y < M3H; y++)
    {
        uint32_t* p = m3_fb + y * M3W;
        float b = y * 0.013f + m3_bgphase * 0.6f; int sb = (int)(256 * sinf(b)), cb = (int)(256 * cosf(b));
        if (!(y & 15)) M3_HOOK();
        for (x = 0; x < M3W; x++)
        {
            int v = ((x + y + t) >> 5 & 1) ? 6 : 0, w = 24 + ((sx[x] * cb + cx[x] * sb) >> 16);
            p[x] = lut[(w + v) & 63];
        }
    }
}

/* ------------------------------------------------------------------------------------------- styles */
static void m3_draw_panels(int cat, int sel, float selanim)
{
    int k[80], n, i, j, nc = 0, W = M3W, mg = L.sm ? 4 : 12;
    const ITEM* it = &g_it[sel];
    m3_bg_panels();
    /* title bar */
    if (L.sm)
    {
        const char* st = g_running ? "SBEMU on" : "SBEMU off";
        m3_box2d(mg, 2, W - 2 * mg, 22, m3_mul(m3_lerpc(m3_sky1, m3_accent[0], 90), 330), m3_mul(m3_sky1, 160), 0, 3);
        m3_text_glow(&m3_scr, mg + 6, 5, 0, "NUKED SB-EMU", 0xFFFFFF, m3_glowc);
        m3_text(&m3_scr, W - mg - 6 - 8 * (int)strlen(st), 5, 0, st, g_running ? 0x80FF90 : 0xFFC060, 256);
    }
    else
    {
        char b[48];
        m3_box2d(12, 10, W - 24, 46, m3_mul(m3_lerpc(m3_sky1, m3_accent[0], 90), 330), m3_mul(m3_sky1, 160), 0, 6);
        m3_text_glow(&m3_scr, 28, 18, 3, "NUKED SB-EMU", 0xFFFFFF, m3_glowc);
        m3_text(&m3_scr, 250, 26, 0, g_running ? "SBEMU is loaded" : "SBEMU is not loaded", g_running ? 0x80FF90 : 0xFFC060, 256);
        snprintf(b, sizeof b, "Panels, %s", m3_lookname[m3_look % 3]);
        if (W >= 760) m3_text(&m3_scr, W - 24 - 8 * (int)strlen(b), 26, 0, b, 0xA0B8E0, 256);
    }
    /* categories */
    for (i = 0; i < NCAT; i++) if (cat_items(i, k)) nc++;
    {
        int y0 = L.top + (L.sm ? 2 : 6), cs = nc ? (L.bot - y0) / nc : 20, cw = L.catw, bh, x0 = mg + 2;
        int cmax = L.sm ? 24 : L.big ? 62 : 46;
        if (cs > cmax) cs = cmax;
        bh = cs - (L.sm ? 3 : 8); if (bh < 8) bh = 8;
        for (i = 0, j = 0; i < NCAT; i++)
        {
            int on = i == cat, yy, ty; char nm[16];
            if (!cat_items(i, k)) continue;
            yy = y0 + j * cs; j++;
            m3_box2d(x0 + on * 2, yy + on * 2, cw, bh, on ? m3_mul(m3_catcol[i], 300) : 0x485878, on ? m3_mul(m3_catcol[i], 120) : 0x283048, on, 5);
            ty = yy + on * 2 + (bh - m3_fh(L.catf)) / 2;
            m3_fit(nm, sizeof nm, g_catname[i], cw - (L.sm ? 8 : 20), L.catf);
            m3_text_sh(&m3_scr, x0 + (L.sm ? 4 : 14) + on * 2, ty, L.catf, nm, 0xFFFFFF);
            m3_hit(x0, yy, cw + 2, bh + 2, 1, i);
        }
    }
    n = cat_items(cat, k);
    /* items */
    {
        int ix = mg + 2 + L.catw + (L.sm ? 4 : 14), iy = L.top, iw = W - ix - mg, ih = L.bot - L.top;
        int rows = (ih - (L.sm ? 4 : 10)) / L.rowh, top = 0, si = 0, rx = ix + (L.sm ? 3 : 8), rw = iw - (L.sm ? 6 : 16), rh = L.rowh - (L.sm ? 2 : 6);
        int fh = m3_fh(L.f), fw = m3_fw(L.f), numw = L.sm ? 12 : 3 * fw;
        if (rows < 1) rows = 1;
        if (m3_look == 0) m3_shadow(&m3_scr, ix, iy, iw, ih, L.sm ? 2 : 6);
        m3_rect(&m3_scr, ix, iy, iw, ih, 0x101828, m3_look == 1 ? 120 : 220);
        if (m3_look != 2) m3_bevel(&m3_scr, ix, iy, iw, ih, L.sm ? 1 : 2, 1, 200);
        for (i = 0; i < n; i++) if (k[i] == sel) si = i;
        if (si >= rows) top = si - rows + 1;
        {   /* the highlight slides to the chosen row */
            float hy = iy + (L.sm ? 2 : 8) + (selanim - top) * L.rowh;
            if (hy >= iy && hy + L.rowh <= iy + ih) m3_rect(&m3_scr, ix + 4, (int)hy - 2, iw - 8, L.rowh, m3_catcol[cat], 70);
        }
        for (i = top; i < n && i < top + rows; i++)
        {
            const ITEM* t2 = &g_it[k[i]]; int on = k[i] == sel, yy = iy + (L.sm ? 2 : 8) + (i - top) * L.rowh, ok = usable(t2), ty = yy + (rh - fh) / 2 + on;
            uint32_t a = on ? m3_mul(m3_catcol[cat], 330) : 0x5A6888, b = on ? m3_mul(m3_catcol[cat], 140) : 0x303850;
            char lb[64];
            if (!ok) { a = 0x3A3F4A; b = 0x22252C; }
            m3_box2d(rx + on, yy + on, rw, rh, a, b, on, 3);
            if (i < 9) { char num[4]; snprintf(num, sizeof num, "%d", i + 1); m3_text(&m3_scr, rx + (L.sm ? 2 : 10) + on, ty, L.sm ? 0 : L.f, num, 0xC0D0F0, 256); }
            {   /* a grey chosen row shows its reason at the right: the label stops before that box */
                int rsn = on && !ok && rw > 300 ? 8 * (int)strlen(m3_reason_text(t2)) + 20 : 0;
                m3_fit(lb, sizeof lb, t2->label, rw - numw - (L.sm ? 6 : 16) - rsn, L.f);
            }
            if (on && ok) m3_text_glow(&m3_scr, rx + numw + (L.sm ? 4 : 10) + on, ty, L.f, lb, 0xFFFFFF, m3_glowc);
            else m3_text_sh(&m3_scr, rx + numw + (L.sm ? 4 : 10) + on, ty, L.f, lb, ok ? 0xFFFFFF : 0x8890A0);
            if (t2->kind == IT_GAME && t2->best[0] && rw > 200) m3_text(&m3_scr, rx + rw - 14, ty, 0, "\x04", 0xFFE060, 256);
            m3_hit(rx, yy, rw, rh + 2, on ? 3 : 2, k[i]);
            if (on && !ok && rw > 300) m3_reason(t2, rx + rw - 12 - 8 * (int)strlen(m3_reason_text(t2)), ty);
        }
        {   /* small arrows in the right margin when items are hidden above or below; clickable */
            int r2, ax = ix + iw - 6;
            if (top > 0) { for (r2 = 0; r2 < 4; r2++) m3_rect(&m3_scr, ax - r2, iy + 4 + r2, 1 + 2 * r2, 1, 0xC0D0F0, 256); m3_hit(ax - 6, iy, 12, 10, 4, 0); }
            if (top + rows < n) { for (r2 = 0; r2 < 4; r2++) m3_rect(&m3_scr, ax - r2, iy + ih - 5 - r2, 1 + 2 * r2, 1, 0xC0D0F0, 256); m3_hit(ax - 6, iy + ih - 10, 12, 10, 5, 0); }
        }
    }
    /* description and keys */
    m3_box2d(mg, L.dy, W - 2 * mg, L.dh, 0x0A0F1C, 0x0A0F1C, 1, 6);
    if (m3_look == 0) m3_rect(&m3_scr, mg, L.dy, W - 2 * mg, L.dh, 0x0A0F1C, 140);
    m3_wrap(&m3_scr, mg + (L.sm ? 6 : 12), L.dy + (L.sm ? 4 : 8), W - 2 * mg - (L.sm ? 12 : 24), L.dl, L.df, it->desc, 0xD8E0F0, 0);
    m3_keybar(mg + (L.sm ? 6 : 12), L.dy + L.dh - (L.sm ? 18 : 22), W - 2 * mg - (L.sm ? 12 : 24), 0x8090B0);
}

/* the 2D layer shared by the 3D styles: title, category tabs (small screens: a switcher), description, keys */
static void m3_overlay(int cat, int sel, int sb)
{
    int i, W = M3W, H = M3H; const ITEM* it = &g_it[sel]; char b[96];
    int dh = L.sm ? L.dh : L.big ? 164 : 136,   /* 4 lines of description */ dy = H - dh - (L.sm ? 2 : 8);
    if (L.sm)
    {
        const char* st = sb ? "SBEMU on" : "SBEMU off";
        int k[80];
        m3_vgrad(&m3_scr, 0, 0, W, 40, 0x000000, 0x000000, 150);
        m3_text_glow(&m3_scr, 6, 2, 0, "NUKED SB-EMU", 0xFFFFFF, m3_glowc);
        m3_text(&m3_scr, W - 6 - 8 * (int)strlen(st), 2, 0, st, sb ? 0x60FF80 : 0xFFB040, 256);
        snprintf(b, sizeof b, "\x11 %s \x10", g_catname[cat]);
        m3_text_glow(&m3_scr, W / 2 - 4 * (int)strlen(b), 20, 0, b, 0xFFFFFF, m3_catcol[cat]);
        m3_hit(0, 18, W / 2, 20, 6, K_LEFT); m3_hit(W / 2, 18, W - W / 2, 20, 6, K_RIGHT);
        (void)k;
    }
    else
    {
        int x, total = 0, k[80], tf = L.big ? 1 : 0, th = L.big ? 28 : 22;
        m3_vgrad(&m3_scr, 0, 0, W, 40, 0x000000, 0x000000, 150);
        m3_text_glow(&m3_scr, 12, 6, 2, "NUKED SB-EMU", 0xFFFFFF, m3_glowc);
        m3_text(&m3_scr, 172, 12, 0, sb ? "SBEMU loaded" : "SBEMU not loaded", sb ? 0x60FF80 : 0xFFB040, 256);
        if (m3_now() < m3_note_until)
            snprintf(b, sizeof b, "%s %s %s  %dx%d %s %.0f fps", m3_stylename[m3_style], m3_lookname[m3_look % 3], m3_theme_names[m3_ctheme % 11], W, H,
                     m3_quality == 1 ? "full" : m3_quality == 2 ? "half" : m3_half ? "auto: half" : "auto: full", m3_fps);
        else snprintf(b, sizeof b, "%s", m3_stylename[m3_style]);
        if (W - 8 - 8 * (int)strlen(b) > 320) m3_text(&m3_scr, W - 8 - 8 * (int)strlen(b), 12, 0, b, 0x8090B0, 256);
        for (i = 0; i < NCAT; i++) if (cat_items(i, k)) total += (int)strlen(g_catname[i]) * m3_fw(tf) + 16 + 4;
        x = W - 12 - total; if (x < 12) x = 12;
        for (i = 0; i < NCAT; i++)
        {
            int w = (int)strlen(g_catname[i]) * m3_fw(tf) + 16, on = i == cat;
            if (!cat_items(i, k)) continue;
            m3_rect(&m3_scr, x, 44, w, th, on ? m3_catcol[i] : 0x101830, on ? 230 : 170);
            m3_bevel(&m3_scr, x, 44, w, th, 2, on, 180);
            m3_text(&m3_scr, x + 8, 44 + (th - m3_fh(tf)) / 2, tf, g_catname[i], on ? 0xFFFFFF : 0x90A0C0, 256);
            m3_hit(x, 44, w, th, 1, i);
            x += w + 4;
        }
    }
    /* description box: title, lines, then the keys on their own line */
    m3_rect(&m3_scr, L.sm ? 2 : 10, dy, W - (L.sm ? 4 : 20), dh, 0x000010, 170);
    m3_bevel(&m3_scr, L.sm ? 2 : 10, dy, W - (L.sm ? 4 : 20), dh, L.sm ? 1 : 2, 1, 120);
    {
        int lf = L.sm ? 0 : 1, df = L.big ? 1 : 0, ly = dy + (L.sm ? 2 : 6), wy = ly + m3_fh(lf) + (L.sm ? 0 : 4);
        int lines = (dy + dh - (L.sm ? 20 : 34) - wy) / (m3_fh(df) + 2);
        m3_fit(b, sizeof b, it->label, W - (L.sm ? 12 : 44), lf);
        m3_text_glow(&m3_scr, L.sm ? 6 : 22, ly, lf, b, m3_mul(m3_catcol[it->cat], 400), m3_catcol[it->cat]);
        if (L.sm) { if (!usable(it)) m3_reason(it, W - 8 - 8 * (int)strlen(m3_reason_text(it)), wy); }
        else m3_reason(it, 22 + (int)strlen(b) * m3_fw(lf) + 16, ly + 4);
        if (lines > 0) m3_wrap(&m3_scr, L.sm ? 6 : 22, wy, W - (L.sm ? 12 : 44), lines, df, it->desc, 0xD0D8E8, 0);
        if (!L.sm) m3_rect(&m3_scr, 18, H - 36, W - 36, 1, 0x7080A0, 90);
        m3_keybar(L.sm ? 6 : 22, H - (L.sm ? 19 : 30), W - (L.sm ? 12 : 44), 0x7080A0);
    }
}
/* the scene's clickable thirds: left = previous item, middle = run the chosen one, right = next */
static void m3_scene_hits(void)
{
    int W = M3W, top = L.sm ? 40 : 70, bot = M3H - (L.sm ? L.dh : L.big ? 140 : 118) - 10;
    m3_hit(0, top, W * 170 / 640, bot - top, 4, 0); m3_hit(W * 470 / 640, top, W - W * 470 / 640, bot - top, 5, 0);
    m3_hit(W * 170 / 640, top, W * 300 / 640, bot - top, 3, 0);
}

/* voxel: a textured box face as blocks (the texture sampled, lit pixels stand out a little) */
static int m3_lod;                    /* voxel on a slow PC: half as many blocks */
static void m3_voxel_face(M3V c, float hx, float hy, float yaw, const M3SURF* tex, uint32_t col, int gx, int gy, float glow)
{
    int x, y; float cy = cosf(yaw), sy = sinf(yaw), q = hx / gx;
    if (m3_lod) { gx = (gx + 1) / 2; gy = (gy + 1) / 2; q = hx / gx; }
    for (y = 0; y < gy; y++)
    {
        M3_HOOK();
        for (x = 0; x < gx; x++)
        {
            uint32_t cc = tex ? tex->p[(int)((y + 0.5f) * tex->h / gy) * tex->w + (int)((x + 0.5f) * tex->w / gx)] : m3_mul(col, 200 + ((x + y) & 1) * 40);
            int lum = (cc >> 16 & 255) + (cc >> 8 & 255) + (cc & 255);
            float px = -hx + (x + 0.5f) * hx * 2 / gx, py = hy - (y + 0.5f) * hy * 2 / gy, pz = lum > 400 ? -q * 0.5f : 0;
            m3_box(m3_v(c.x + px * cy + pz * sy, c.y + py, c.z - px * sy + pz * cy), q * 0.92f, hy / gy * 0.92f, q * 0.6f, yaw, NULL, cc, lum > 400 ? glow : 0, 256);
        }
    }
}

static void m3_draw_carousel(int cat, int sel, double t)
{
    int k[80], n = cat_items(cat, k), i, si = 0, pass;
    float step, R, rise = m3_catanim;
    for (i = 0; i < n; i++) if (k[i] == sel) si = i;
    if (m3_cards_cat != cat) m3_cards_build(cat);
    step = 6.2831853f / (n < 7 ? 7 : n);
    R = (n < 7 ? 7 : n) * 0.42f; if (R < 2.8f) R = 2.8f;
    m3_fog = m3_fogc; m3_fog0 = R + 4.5f; m3_fog1 = R * 2 + 10;
    m3_camera(0, 0.35f + 0.05f * sinf((float)t * 0.7f), -(R + 3.9f), 0.04f * sinf((float)t * 0.3f), -0.13f);
    if (m3_look == 1)
    {   /* vector: card outlines and their names, a grid floor, the slots as rings */
        uint32_t acc = m3_lerpc(m3_gridc, 0xFFFFFF, 60);
        int slots = n < 7 ? 7 : n;
        m3_vector_fade();
        m3_vector_floor(-1.0f, (float)t);
        for (i = 0; i < slots; i++)
        {
            float a = (i - m3_ring) * step, x = R * sinf(a), z = -R * cosf(a);
            m3_wcircle(x, -0.99f, z, 0.32f, 0, i < n && k[i] == sel ? m3_catcol[cat] : m3_mul(acc, 90));
        }
        for (i = 0; i < n; i++)
        {
            float a = (i - m3_ring) * step, x = R * sinf(a), z = -R * cosf(a), on = (k[i] == sel), y = (on ? 0.08f * sinf((float)t * 2.2f) : 0) - rise * (1.0f + 0.25f * i);
            uint32_t c = on ? m3_lerpc(m3_catcol[cat], 0xFFFFFF, 80) : m3_mul(m3_catcol[cat], 200);
            M3P p;
            m3_wbox(x, y, z, on ? 1.12f : 1.0f, on ? 0.56f : 0.5f, 0.05f, -a, c);
            p = m3_xf(m3_v(x, y, z), 0, 0, 1);
            if (p.z > 1.0f)
            {   /* the item's name at the card's centre, if the card faces us */
                float f = m3_f * m3_rw / M3_FBASE; int sx = (int)(m3_rw / 2 + p.x * f / p.z), sy = (int)(m3_rh / 2 - p.y * f * m3_fyk / p.z);
                if (cosf(a) > 0.2f && sx > -200 && sx < M3W + 200)
                {
                    char lb[24]; int fnt = (!L.sm && on) ? 1 : 0;
                    snprintf(lb, sizeof lb, "%.18s", g_it[k[i]].label);
                    m3_text(&m3_scr, sx - (int)strlen(lb) * m3_fw(fnt) / 2, sy - m3_fh(fnt) / 2, fnt, lb, c, on ? 256 : 160);
                }
            }
        }
    }
    else
    {
        m3_sky();
        for (pass = 0; pass < 2; pass++)
        {   /* pass 0: the mirror images under the floor, pass 1: the cards */
            if (pass == 1) m3_draw_floor(-1.0f, 160);
            if (pass == 0 && m3_look == 2) continue;                   /* voxel: no mirror (too many blocks) */
            for (i = 0; i < n; i++)
            {
                float a = (i - m3_ring) * step, x = R * sinf(a), z = -R * cosf(a), on = (k[i] == sel), bob, y, sc;
                bob = on ? 0.08f * sinf((float)t * 2.2f) : 0;
                sc = on ? 1.12f : 1.0f;
                y = bob - rise * (1.0f + 0.25f * i);
                if (pass == 0) y = -2.0f - y;
                if (m3_look == 2 && cosf(a) > -0.3f)
                    m3_voxel_face(m3_v(x, y, z), 1.0f * sc, 0.5f * sc, -a, m3_card(i), m3_catcol[cat], 16, 8, on ? 0.3f : 0.1f);
                else
                {
                    m3_boxback = m3_mul(m3_catcol[cat], 110);
                    m3_box(m3_v(x, y, z), 1.0f * sc, 0.5f * sc, 0.05f, -a, m3_card(i), m3_card(i) ? 0x303848 : m3_mul(m3_catcol[cat], 160),
                           on ? 0.35f + 0.15f * sinf((float)t * 3) : 0, 256);
                }
            }
            if (pass == 0)
            {   /* darken the mirror images */
                int yy, q;
                for (yy = 0; yy < m3_rh; yy++)
                    for (q = yy * M3W; q < yy * M3W + m3_rw; q++) if (m3_zb[q] != 0) { m3_fb[q] = m3_mul(m3_fb[q], 90); m3_zb[q] = 0; }
            }
        }
        m3_boxback = 0;
        {   /* a glowing marker on the floor for every place on the ring, so a short list still looks like a carousel */
            int slots = n < 7 ? 7 : n;
            for (i = 0; i < slots; i++)
            {
                float a = (i - m3_ring) * step, x = R * sinf(a), z = -R * cosf(a);
                int used = i < n, on = used && k[i] == sel;
                m3_box(m3_v(x, -0.99f, z), 0.32f, 0.01f, 0.32f, -a, NULL, on ? m3_catcol[cat] : used ? m3_mul(m3_catcol[cat], 120) : m3_mul(m3_gridc, 80),
                       on ? 0.6f : 0.2f, 256);
            }
        }
        m3_upscale();
    }
    {   /* where you are, and that there is more left and right */
        char b[24]; int f = L.sm ? 0 : 1, fa = L.sm ? 1 : 3;
        snprintf(b, sizeof b, "%d / %d", si + 1, n);
        m3_text_glow(&m3_scr, M3W / 2 - (int)strlen(b) * m3_fw(f) / 2, M3H * 318 / 480, f, b, 0xFFFFFF, m3_catcol[cat]);
        if (n > 1)
        {
            m3_text_glow(&m3_scr, L.sm ? 6 : 24, M3H * 200 / 480, fa, "<", 0xFFFFFF, m3_catcol[cat]);
            m3_text_glow(&m3_scr, M3W - (L.sm ? 16 : 40), M3H * 200 / 480, fa, ">", 0xFFFFFF, m3_catcol[cat]);
        }
    }
    m3_scene_hits();
    m3_overlay(cat, sel, g_running);
}

static void m3_make_panel(int cat, int sel, double t)
{
    M3SURF* s = &m3_panel; const ITEM* it = &g_it[sel]; int i, k[80], n = cat_items(cat, k), si = 0;
    char line[48];
    static uint32_t px[512 * 128];                       /* static: never out of memory */
    static int kcat = -1, ksel = -1, koff = -1, krun = -1, kgen = -1;
    if (!s->p) { s->w = 512; s->h = 128; s->p = px; }
    for (i = 0; i < n; i++) if (k[i] == sel) si = i;
    {   /* drawn again only when something on it changed (it was 1/5 of a frame on a Pentium) */
        int len = (int)strlen(it->desc), off = len > 28 ? (int)(t * 6) % (len + 8) : 0;
        if (cat == kcat && sel == ksel && off == koff && g_running == krun && kgen == m3_theme_gen + 7919 * g_menugen) return;
        kcat = cat; ksel = sel; koff = off; krun = g_running; kgen = m3_theme_gen + 7919 * g_menugen;
    }
    {   /* brushed metal: horizontal streaks (a brightness per row plus fine grain), lighter towards the top, in the body colour */
        int y, x, br = (int)(m3_body >> 16 & 255) - 0x26, bg = (int)(m3_body >> 8 & 255) - 0x26, bb = (int)(m3_body & 255) - 0x2C;
        for (y = 0; y < 128; y++)
        {
            unsigned h = (unsigned)y * 2654435761u; int row = (int)(h >> 28) - 8, top = (128 - y) / 10;
            for (x = 0; x < 512; x++) { int v = 46 + top + row / 2 + (int)(((unsigned)(x * 7 + y * 131) * 2246822519u) >> 30); s->p[y * 512 + x] = m3_rgb(v + br, v + bg, v + 5 + bb); }
        }
    }
    m3_bevel(s, 0, 0, 512, 128, 3, 0, 120);
    m3_text(s, 12, 8, 2, "NUKED", 0xE8E8F0, 256);
    m3_text(s, 12, 36, 0, "SB-EMU", 0xB8B8C8, 256);
    m3_text(s, 12, 54, 0, "SOUND MODULE", 0x8A8A98, 256);
    m3_text(s, 12, 74, 0, "GS MT-32 OPL3", 0x6A6A78, 256);
    /* LCD */
    {   /* the LCD's backlight glowing through around its window */
        int g; for (g = 6; g >= 1; g--) m3_rect(s, 150 - g, 10 - g, 250 + 2 * g, 64 + 2 * g, m3_lcd, 14);
    }
    m3_rect(s, 150, 10, 250, 64, 0x101810, 256);
    m3_bevel(s, 150, 10, 250, 64, 2, 1, 200);
    m3_vgrad(s, 154, 14, 242, 56, m3_mul(m3_lcd, 300), m3_mul(m3_lcd, 220), 256);
    snprintf(line, sizeof line, "%-8.8s %2d/%-2d", g_catname[cat], si + 1, n);
    m3_text(s, 160, 17, 0, line, m3_lerpc(m3_ink, m3_lcd, 50), 256);
    snprintf(line, sizeof line, "%.24s", it->label); m3_text(s, 160, 34, 0, line, m3_ink, 256);
    {   /* the description scrolls along the last line */
        int len = (int)strlen(it->desc), off = len > 28 ? (int)(t * 6) % (len + 8) : 0; char sc[30];
        for (i = 0; i < 29; i++) { int p = off + i; sc[i] = p < len ? it->desc[p] : ' '; } sc[29] = 0;
        m3_text(s, 160, 51, 0, sc, m3_lerpc(m3_ink, m3_lcd, 80), 256);
    }
    /* category buttons: labels and LEDs (the buttons themselves are 3D boxes) */
    for (i = 0; i < NCAT; i++)
    {
        static const char* const sh[NCAT] = { "SND", "SET", "TEST", "GAME", "CD", "FILE" };
        int bx = 150 + i * 42, on = i == cat;
        m3_rect(s, bx + 12, 82, 10, 5, on ? 0xFF3020 : 0x401010, 256);
        m3_text(s, bx + 17 - (int)strlen(sh[i]) * 4, 110, 0, sh[i], on ? 0xFFFFFF : 0xA0A0B0, 256);
    }
    m3_text(s, 432, 104, 0, "SELECT", 0xB0B0C0, 200);
    m3_rect(s, 492, 12, 8, 8, g_running ? 0x30FF40 : 0x203020, 256);
    m3_text(s, 470, 24, 0, "PWR", 0x9090A0, 200);
}

static void m3_draw_module(int cat, int sel, double t)
{
    float W = 2.9f, H = 0.72f, D = 1.6f; int c;
    float kx = -W + (440 / 512.0f) * 2 * W, ky = H - (56 / 128.0f) * 2 * H;
    m3_fog = m3_fogc; m3_fog0 = 8; m3_fog1 = 22;
    m3_camera(0.9f * sinf((float)t * 0.25f), 0.15f + 0.1f * sinf((float)t * 0.17f), -6.6f, -0.14f * sinf((float)t * 0.25f), -0.12f);
    m3_make_panel(cat, sel, t);
    if (m3_look == 1)
    {   /* vector: the module's outline, its buttons, the knob as rings; the LCD's lines as text where it is */
        uint32_t acc = m3_lerpc(m3_gridc, 0xFFFFFF, 60), dim = m3_mul(acc, 110);
        M3P p; int k[80], n = cat_items(cat, k), si = 0, i;
        for (i = 0; i < n; i++) if (k[i] == sel) si = i;
        m3_vector_fade();
        m3_vector_floor(-H - 0.02f, (float)t);
        m3_wbox(0, 0, 0, W, H, D, 0, acc);
        m3_wbox(-W - 0.12f, 0, -D + 0.03f, 0.12f, H, 0.03f, 0, dim); m3_wbox(W + 0.12f, 0, -D + 0.03f, 0.12f, H, 0.03f, 0, dim);
        m3_wbox(-W + 150 / 512.0f * 2 * W + 125 / 512.0f * 2 * W, H - 42 / 128.0f * 2 * H, -D - 0.01f, 125 / 512.0f * 2 * W, 32 / 128.0f * 2 * H, 0.005f, 0, m3_lcd);
        for (c = 0; c < NCAT; c++)
        {
            float u = (150 + c * 42 + 17) / 512.0f, x = -W + u * 2 * W, y = H - 96 / 128.0f * 2 * H;
            m3_wbox(x, y, -D - 0.06f + m3_btnpress[c] * 0.05f, 0.11f, 0.06f, 0.05f, 0, c == cat ? m3_catcol[c] : dim);
        }
        m3_wcircle(kx, ky, -D - 0.16f, 0.34f, m3_knob + 1.5708f, acc); m3_wcircle(kx, ky, -D - 0.16f, 0.2f, 0, dim);
        p = m3_xf(m3_v(-W + 160 / 512.0f * 2 * W, H - 17 / 128.0f * 2 * H, -D - 0.01f), 0, 0, 1);
        if (p.z > 0.5f)
        {
            float f = m3_f * m3_rw / M3_FBASE; int sx = (int)(m3_rw / 2 + p.x * f / p.z), sy = (int)(m3_rh / 2 - p.y * f * m3_fyk / p.z);
            char l1[48], l2[32]; int fh = m3_fh(0) + 2;
            snprintf(l1, sizeof l1, "%-8.8s %2d/%-2d", g_catname[cat], si + 1, n); snprintf(l2, sizeof l2, "%.24s", g_it[sel].label);
            if (M3W < 640) { sy -= 4; fh = 14; }
            m3_text(&m3_scr, sx, sy, 0, l1, m3_mul(m3_lcd, 300), 256);
            m3_text(&m3_scr, sx, sy + fh, 0, l2, m3_lerpc(m3_lcd, 0xFFFFFF, 90), 256);
        }
    }
    else
    {
        m3_sky();
        m3_draw_floor(-H - 0.02f, 256);
        if (m3_look == 2) m3_voxel_face(m3_v(0, 0, -D), W, H, 0, &m3_panel, m3_body, 40, 12, 0.15f);
        m3_box(m3_v(0, 0, m3_look == 2 ? 0.08f : 0), W, H, m3_look == 2 ? D - 0.08f : D, 0, m3_look == 2 ? NULL : &m3_panel, m3_body, 0, 256);
        m3_box(m3_v(-W + 0.3f, -H - 0.01f, -D + 0.3f), 0.12f, 0.02f, 0.12f, 0, NULL, 0x101010, 0, 256);   /* feet */
        m3_box(m3_v(W - 0.3f, -H - 0.01f, -D + 0.3f), 0.12f, 0.02f, 0.12f, 0, NULL, 0x101010, 0, 256);
        /* rack ears */
        m3_box(m3_v(-W - 0.12f, 0, -D + 0.03f), 0.12f, H, 0.03f, 0, NULL, 0x303036, 0, 256);
        m3_box(m3_v(W + 0.12f, 0, -D + 0.03f), 0.12f, H, 0.03f, 0, NULL, 0x303036, 0, 256);
        /* category buttons (panel texture x 150 + i*42 .. of 512, y 88..104 of 128) */
        for (c = 0; c < NCAT; c++)
        {
            float u = (150 + c * 42 + 17) / 512.0f, v = 96 / 128.0f, x = -W + u * 2 * W, y = H - v * 2 * H, p = m3_btnpress[c];
            m3_box(m3_v(x, y, -D - 0.06f - (m3_look == 2 ? 0.12f : 0) + p * 0.05f), 0.11f, 0.06f, 0.05f, 0, NULL, c == cat ? m3_mul(m3_catcol[c], 230) : 0x505058, c == cat ? 0.2f : 0, 256);
        }
        /* the knob: one notch per item (voxel: a disc of blocks that turns) */
        if (m3_look == 2)
        {
            int r = m3_lod ? 3 : 5, x, y; float q = 0.34f / r;
            for (y = -r; y <= r; y++) for (x = -r; x <= r; x++) if (x * x + y * y <= r * r)
            {
                float a = atan2f((float)y, (float)x) - m3_knob; int notch = fabsf(fmodf(a + 31.4159265f, 6.2831853f) - 3.14159265f) > 2.9f && x * x + y * y > 2;
                m3_box(m3_v(kx + x * q, ky + y * q, -D - 0.28f), q * 0.48f, q * 0.48f, 0.08f, 0, NULL, notch ? 0xFF8020 : m3_mul(0x606068, 150 + (x * x + y * y) * 60 / (r * r + 1)), notch ? 0.4f : 0, 256);
            }
        }
        else m3_knob3d(m3_v(kx, ky, -D - 0.16f), 0.34f, 0.16f, m3_knob, 0);
        m3_upscale();
    }
    m3_scene_hits();
    m3_overlay(cat, sel, g_running);
}

/* ------------------------------------------------------------------------------------------- screen modes */
/* the choice (w 0 = auto), the one set now and the last confirmed one; as MIDPLAY (midplay3d.h mp3_enter) */
static int    m3_want_w, m3_want_h, m3_want_b, m3_hz;
static int    m3_cur = -1, m3_ok_w, m3_ok_h, m3_ok_b, m3_ok_hz, m3_have, m3_oom, m3_win, m3_win_ok;
static double m3_ask_until;
static char   m3_msg[100]; static double m3_msg_until;
static int m3_find(int w, int h, int b)
{
    int i;
    for (i = 0; i < m3_nmodes; i++) if (m3_modes[i].w == w && m3_modes[i].h == h && (!b || m3_modes[i].bpp == b)) return i;
    return -1;
}
static int m3_auto(void)
{   /* the best 640x480 high-colour mode (as before beta.106); a Windows DOS box: VGA 320x200 (a window can show it) */
    int i;
    if (m3_win) return 0;
    for (i = 0; i < m3_nmodes; i++) if (m3_modes[i].mode == m3_vbest) return i;
    for (i = m3_nmodes - 1; i >= 0; i--) if (m3_modes[i].w == 640 && m3_modes[i].h == 480 && m3_modes[i].bpp > 8) return i;
    return 0;
}
static const char* m3_mode_name(int i, char* b, size_t n)
{
    if (i < 0 || i >= m3_nmodes) { snprintf(b, n, "?"); return b; }
    if (m3_modes[i].mode == 0x13) snprintf(b, n, "320x200 VGA, 256 colours");
    else snprintf(b, n, "%dx%d %s%s", m3_modes[i].w, m3_modes[i].h,
                  m3_modes[i].bpp == 8 ? "256 colours" : m3_modes[i].bpp == 15 ? "15 bit" : m3_modes[i].bpp == 16 ? "16 bit" : m3_modes[i].bpp == 24 ? "24 bit" : "32 bit",
                  m3_modes[i].lfb ? "" : " (banked)");
    return b;
}
/* the frame and depth buffers BEFORE the mode is set (out of memory: the screen stays as it is) */
static int m3_buffers(int w, int h)
{
    int need = w * h;
    if (need > m3_have || !m3_fb || !m3_zb)
    {
        free(m3_fb); free(m3_zb); m3_fb = NULL; m3_zb = NULL; m3_have = 0;
        m3_fb = (uint32_t*)malloc((size_t)need * 4); m3_zb = (float*)malloc((size_t)need * sizeof(float));
        if (!m3_fb || !m3_zb) { free(m3_fb); free(m3_zb); m3_fb = NULL; m3_zb = NULL; return 0; }
        memset(m3_fb, 0, (size_t)need * 4); memset(m3_zb, 0, (size_t)need * sizeof(float));
        m3_have = need;
    }
    return 1;
}
/* everything the 3D menu holds, given back when it is left for the text menu */
static void m3_free_all(void)
{ free(m3_fb); free(m3_zb); m3_fb = NULL; m3_zb = NULL; m3_have = 0; m3_cards_free(); }
static int m3_try(int i, int hz)
{
    m3_oom = 0;
    if (i < 0 || i >= m3_nmodes) return 0;
    if (!m3_buffers(m3_modes[i].w, m3_modes[i].h)) { m3_oom = 1; return 0; }
    if (!m3_open_mode(&m3_modes[i], hz)) return 0;
    m3_scr.w = M3W; m3_scr.h = M3H; m3_scr.p = m3_fb;
    m3_cur = i;
    m3_half = m3_quality == 2; m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H;
    m3_layout();
    return 1;
}
static int m3_try_smaller(int i)
{
    long below = (long)m3_modes[i].w * m3_modes[i].h; int pass;
    for (pass = 0; pass < 2; pass++)
    {
        long lim = below;
        for (;;)
        {
            int k, best = -1; long bp = 0;
            for (k = 0; k < m3_nmodes; k++)
            {
                long px = (long)m3_modes[k].w * m3_modes[k].h;
                if (px >= lim || px <= bp || (!pass && m3_modes[k].bpp != m3_modes[i].bpp)) continue;
                if (m3_win && m3_modes[k].mode != 0x13) continue;
                best = k; bp = px;
            }
            if (best < 0) break;
            if (m3_try(best, 0)) return 1;
            lim = bp;
        }
    }
    return 0;
}
/* set the chosen mode; ask = a mode the user just picked (keep it?). Falls back: smaller (low memory), last good,
 * auto, VGA 320x200. 0 = nothing works (text menu). */
static int m3_enter(int ask)
{
    int want, ok, oom = 0;
    m3_vbe_probe();
    want = m3_want_w ? m3_find(m3_want_w, m3_want_h, m3_want_b) : -1;
    if (want < 0) want = m3_auto();
    ok = m3_try(want, m3_hz);
    if (!ok && m3_oom) { oom = 1; ok = m3_try_smaller(want); if (ok) ask = 0; }
    if (!ok && m3_ok_w) { ok = m3_try(m3_find(m3_ok_w, m3_ok_h, m3_ok_b), m3_ok_hz); if (ok) ask = 0; }
    if (!ok) { ok = m3_try(m3_auto(), 0); if (ok) ask = 0; }
    if (!ok) { ok = m3_try(0, 0); if (ok) ask = 0; }
    if (!ok) { ui_log("3D: no mode works (low memory %d)", m3_oom); return 0; }
    ui_log("3D mode %dx%d %d%s%s", M3W, M3H, m3_bpp, oom ? " (low memory)" : "", ask ? " ask" : "");
    if (oom && m3_cur != want)
    {
        snprintf(m3_msg, sizeof m3_msg, "Not enough memory for %dx%d: %dx%d instead.", m3_modes[want].w, m3_modes[want].h, M3W, M3H);
        m3_msg_until = m3_now() + 6;
    }
    if (!m3_win && !m3_refresh) m3_refresh_seen = m3_measure_refresh();
    if (m3_win && m3_modes[m3_cur].mode != 0x13 && !m3_win_ok) ask = 1;     /* may be black in a window: ask */
    m3_ask_until = ask ? m3_now() + 10 : 0;
    if (!ask && !oom) { m3_ok_w = m3_modes[m3_cur].w; m3_ok_h = m3_modes[m3_cur].h; m3_ok_b = m3_modes[m3_cur].bpp; m3_ok_hz = m3_refresh; }
    return 1;
}
static void m3_cfg_save(void);
/* the answer to "keep this mode?": 1 = Y, 0 = anything else or the time ran out (back to the last good one) */
static void m3_answer(int keep)
{
    if (!m3_ask_until) return;
    m3_ask_until = 0;
    ui_log("keep mode: %s", keep ? "yes" : "no");
    if (keep)
    {
        m3_ok_w = m3_modes[m3_cur].w; m3_ok_h = m3_modes[m3_cur].h; m3_ok_b = m3_modes[m3_cur].bpp; m3_ok_hz = m3_refresh;
        if (m3_win) m3_win_ok = 1;
        m3_want_w = m3_ok_w; m3_want_h = m3_ok_h; m3_want_b = m3_ok_b; m3_hz = m3_ok_hz;
        m3_cfg_save();
        return;
    }
    m3_vbe_close();
    m3_want_w = m3_ok_w; m3_want_h = m3_ok_h; m3_want_b = m3_ok_b; m3_hz = m3_ok_hz;
    if (m3_win && !m3_ok_w) { m3_want_w = 320; m3_want_h = 200; m3_want_b = 8; }
    m3_enter(0);
    m3_cfg_save();
}

/* ------------------------------------------------------------------------------------------- overlays */
static int m3_over;                              /* 0 none, 1 help, 2 the screen list */
static int m3_psel, m3_ptop;                     /* the screen list: chosen row, first row shown */
static const int m3_rates[] = { 0, 60, 70, 72, 75, 85, 100 };
static int m3_prate;                             /* index into m3_rates for the next mode set */
static void m3_panelbox(int x, int y, int w, int h, const char* title)
{
    m3_shadow(&m3_scr, x, y, w, h, L.sm ? 3 : 8);
    m3_rect(&m3_scr, x, y, w, h, 0x081020, 235);
    m3_bevel(&m3_scr, x, y, w, h, 2, 0, 200);
    m3_rect(&m3_scr, x + 2, y + 2, w - 4, 20, m3_mul(m3_accent[0], 120), 220);
    m3_text_sh(&m3_scr, x + 8, y + 4, 0, title, 0xFFFFFF);
}
static void m3_draw_help(void)
{
    static const char* const t[] = {
        "Arrows, Tab  choose menu and item",
        "Enter        run it (or click it twice)",
        "1-9, A-Z     jump to an item",
        "F2 on a game play with saved settings",
        "F3  style: Panels, Carousel, Module",
        "F4  screen mode, refresh rate, speed",
        "F5  MIDI player (MIDPLAY)",
        "F6  colours: 11 themes, as MIDPLAY",
        "F7  look: solid, vector, voxel",
        "F8  the text menu",
        "Esc back to DOS (type MENU again)",
        "Mouse: click, wheel = up / down,",
        "right click = back",
        NULL };
    int n = 0, i, lh = 18, w = 42 * 8 + 16, h, x, y, fit;
    while (t[n]) n++;
    if (w > M3W - 8) w = M3W - 8;
    fit = (M3H - 8 - 44) / lh; if (fit > n) fit = n; if (fit < 1) fit = 1;
    h = 44 + fit * lh; x = (M3W - w) / 2; y = (M3H - h) / 2;
    m3_panelbox(x, y, w, h, "Help - 3D menu");
    for (i = 0; i < fit; i++) m3_text(&m3_scr, x + 8, y + 26 + i * lh, 0, t[i], 0xD8E0F0, 256);
    m3_text(&m3_scr, x + 8, y + h - 18, 0, "any key or click: back", 0xFFE060, 256);
    m3_hit(0, 0, M3W, M3H, 6, K_ESC);
}
static int m3_prows(void) { return 2 + m3_nmodes; }
static void m3_prow_text(int r, char* b, size_t n)
{
    if (r == 0) snprintf(b, n, "Speed: %s", m3_quality == 1 ? "full resolution" : m3_quality == 2 ? "half resolution (faster)" : "auto (half when slow)");
    else if (r == 1) { if (m3_rates[m3_prate]) snprintf(b, n, "Refresh: %d Hz (VBE 3.0 cards)", m3_rates[m3_prate]); else snprintf(b, n, "Refresh: the BIOS's own rate"); }
    else
    {
        char mn[48]; int i = r - 2;
        m3_mode_name(i, mn, sizeof mn);
        snprintf(b, n, "%s%s%s", mn, i == m3_cur ? "  (now)" : "", (m3_modes[i].w == m3_ok_w && m3_modes[i].h == m3_ok_h && m3_modes[i].bpp == m3_ok_b && i != m3_cur) ? "  (last good)" : "");
    }
}
static void m3_draw_picker(void)
{
    int lh = 18, w = 46 * 8 + 16, h, x, y, vis, i, n = m3_prows();
    char b[80];
    if (w > M3W - 8) w = M3W - 8;
    h = M3H - (L.sm ? 8 : 80); if (h > 44 + n * lh + 18) h = 44 + n * lh + 18;
    vis = (h - 44 - 18) / lh; if (vis < 1) vis = 1;
    x = (M3W - w) / 2; y = (M3H - h) / 2;
    if (m3_psel < m3_ptop) m3_ptop = m3_psel;
    if (m3_psel >= m3_ptop + vis) m3_ptop = m3_psel - vis + 1;
    m3_panelbox(x, y, w, h, "Screen (F4)");
    for (i = 0; i < vis && m3_ptop + i < n; i++)
    {
        int r = m3_ptop + i, on = r == m3_psel, yy = y + 26 + i * lh;
        if (on) m3_rect(&m3_scr, x + 4, yy - 1, w - 8, lh, m3_accent[0], 150);
        { char t[80]; m3_prow_text(r, t, sizeof t); m3_fit(b, sizeof b, t, w - 16, 0); }
        m3_text(&m3_scr, x + 8, yy, 0, b, r < 2 ? 0xFFE060 : on ? 0xFFFFFF : 0xC0C8D8, 256);
        m3_hit(x + 4, yy - 1, w - 8, lh, 7, r);
    }
    if (m3_ptop > 0) { m3_text(&m3_scr, x + w - 16, y + 26, 0, "\x18", 0xFFFFFF, 256); m3_hit(x + w - 18, y + 24, 14, 16, 6, K_PGUP); }
    if (m3_ptop + vis < n) { m3_text(&m3_scr, x + w - 16, y + h - 36, 0, "\x19", 0xFFFFFF, 256); m3_hit(x + w - 18, y + h - 38, 14, 16, 6, K_PGDN); }
    m3_text(&m3_scr, x + 8, y + h - 18, 0, M3W >= 400 ? "Enter set  \x1B\x1A change  Esc back" : "Enter set  Esc back", 0xFFE060, 256);
}
static void m3_draw_ask(void)
{   /* a new screen mode: keep it? (if the screen is black, any key or 10 s brings the last one back) */
    char q[80]; int left = (int)(m3_ask_until - m3_now()) + 1, w = 300, x0, y0;
    if (left < 0) left = 0;
    if (w > M3W - 8) w = M3W - 8;
    x0 = (M3W - w) / 2; y0 = M3H / 2 - 40;
    m3_panelbox(x0, y0, w, 80, "Screen mode");
    snprintf(q, sizeof q, "%dx%d", M3W, M3H); if (m3_refresh) snprintf(q + strlen(q), sizeof q - strlen(q), " at %d Hz", m3_refresh);
    m3_text(&m3_scr, x0 + 8, y0 + 26, 0, q, 0xFFFFFF, 256);
    snprintf(q, sizeof q, "Keep this mode? (%d s)", left);
    m3_text(&m3_scr, x0 + 8, y0 + 44, 0, q, 0xFFC040, 256);
    m3_rect(&m3_scr, x0 + w - 116, y0 + 58, 48, 18, 0x206020, 256); m3_text(&m3_scr, x0 + w - 108, y0 + 59, 0, "Yes", 0xFFFFFF, 256);
    m3_rect(&m3_scr, x0 + w - 60, y0 + 58, 48, 18, 0x602020, 256); m3_text(&m3_scr, x0 + w - 48, y0 + 59, 0, "No", 0xFFFFFF, 256);
    m3_hit(0, 0, M3W, M3H, 6, 'n');
    m3_hit(x0 + w - 116, y0 + 58, 48, 18, 6, 'y');
}

/* ------------------------------------------------------------------------------------------- config, input */
static void m3_cfg_save(void)
{
    char p[300]; FILE* f; home_path(p, "MENU3D.CFG");
    if ((f = fopen(p, "w")) != NULL)
    {
        fprintf(f, "ON=%d\nSTYLE=%d\nQUALITY=%d\nLOOK=%d\nTHEME=%d\n", m3_on, m3_style, m3_quality, m3_look, m3_ctheme);
        /* the screen mode: a Windows DOS box keeps its own (a window can show only VGA 320x200) */
        fprintf(f, "%s=%d %d %d\n%s=%d\n", m3_win ? "MODEW" : "MODE", m3_want_w, m3_want_h, m3_want_b, m3_win ? "HZW" : "HZ", m3_hz);
        if (g_m3other[0] || g_m3other[3])
            fprintf(f, "%s=%d %d %d\n%s=%d\n", m3_win ? "MODE" : "MODEW", g_m3other[0], g_m3other[1], g_m3other[2], m3_win ? "HZ" : "HZW", g_m3other[3]);
        fclose(f);
    }
}
static int m3_cfg_load(void)
{
    char p[300], l[60]; FILE* f; home_path(p, "MENU3D.CFG");
    m3_on = 1;
    if ((f = fopen(p, "r")) == NULL) return 0;
    while (fgets(l, sizeof l, f))
    {
        int mine = !strncmp(l, m3_win ? "MODEW=" : "MODE=", m3_win ? 6 : 5), other = !mine && !strncmp(l, m3_win ? "MODE=" : "MODEW=", m3_win ? 5 : 6);
        int hzm = !strncmp(l, m3_win ? "HZW=" : "HZ=", m3_win ? 4 : 3), hzo = !hzm && !strncmp(l, m3_win ? "HZ=" : "HZW=", m3_win ? 3 : 4);
        if (!strncmp(l, "ON=", 3)) m3_on = atoi(l + 3) != 0;
        else if (!strncmp(l, "STYLE=", 6)) m3_style = abs(atoi(l + 6)) % M3_STYLES;
        else if (!strncmp(l, "QUALITY=", 8)) m3_quality = abs(atoi(l + 8)) % 3;
        else if (!strncmp(l, "LOOK=", 5)) m3_look = abs(atoi(l + 5)) % 3;
        else if (!strncmp(l, "THEME=", 6)) m3_ctheme = abs(atoi(l + 6)) % 11;
        else if (mine) { if (sscanf(strchr(l, '=') + 1, "%d %d %d", &m3_want_w, &m3_want_h, &m3_want_b) != 3) m3_want_w = 0; }
        else if (other) { if (sscanf(strchr(l, '=') + 1, "%d %d %d", &g_m3other[0], &g_m3other[1], &g_m3other[2]) != 3) g_m3other[0] = 0; }
        else if (hzm) m3_hz = atoi(strchr(l, '=') + 1);
        else if (hzo) g_m3other[3] = atoi(strchr(l, '=') + 1);
    }
    fclose(f);
    if (m3_want_w < 0 || m3_want_w > M3WMAX || m3_want_h < 0 || m3_want_h > M3HMAX) m3_want_w = 0;
    if (m3_hz < 0 || m3_hz > 200) m3_hz = 0;
    m3_ok_w = m3_want_w; m3_ok_h = m3_want_h; m3_ok_b = m3_want_b; m3_ok_hz = m3_hz;
    { int i; for (i = 0; i < (int)(sizeof m3_rates / sizeof m3_rates[0]); i++) if (m3_rates[i] == m3_hz) m3_prate = i; }
    return m3_on;                                    /* an old MENU3D.CFG without ON= : its being there meant "on" */
}
static int m3_key(void)
{
    int c;
    if (!kbhit()) return 0;
    c = getch(); if (c == 0 || c == 0xE0) c = 0x100 | getch();
    return c;
}
/* the mouse in the graphics screen: INT 33h with its range set to the screen, the pointer drawn here */
static void m3_mouse_setup(void)
{
    __dpmi_regs r;
    if (m3_mouse < 0) { memset(&r, 0, sizeof r); r.x.ax = 0; __dpmi_int(0x33, &r); m3_mouse = r.x.ax == 0xFFFF; }
    if (!m3_mouse) return;
    if (m3_mx >= M3W) m3_mx = M3W / 2;
    if (m3_my >= M3H) m3_my = M3H / 2;
    memset(&r, 0, sizeof r); r.x.ax = 7; r.x.cx = 0; r.x.dx = M3W - 1; __dpmi_int(0x33, &r);
    memset(&r, 0, sizeof r); r.x.ax = 8; r.x.cx = 0; r.x.dx = M3H - 1; __dpmi_int(0x33, &r);
    memset(&r, 0, sizeof r); r.x.ax = 4; r.x.cx = m3_mx; r.x.dx = m3_my; __dpmi_int(0x33, &r);
    tui_mouse = -1;                                   /* the text screens set their own range again */
}
static int m3_mval;
/* a key from the mouse: 0x1F2 = open menu m3_mval, 0x1F3 = choose item m3_mval, 0x1F4 = screen list row m3_mval,
 * or a key (Enter, Up, Down, a key bar key); right click = Esc in the overlays */
static int m3_mouse_key(void)
{
    __dpmi_regs r;
    int b, w, i, k = 0;
    if (m3_mouse <= 0) return 0;
    memset(&r, 0, sizeof r); r.x.ax = 3; __dpmi_int(0x33, &r);
    b = r.x.bx & 7; w = (signed char)r.h.bh;
    if (r.x.cx != m3_mx || r.x.dx != m3_my) { m3_mx = r.x.cx < M3W ? r.x.cx : M3W - 1; m3_my = r.x.dx < M3H ? r.x.dx : M3H - 1; m3_mseen = 1; }
    if ((b & 1) && !(m3_mb & 1))
    {
        m3_mseen = 1;
        for (i = m3_nhits - 1; i >= 0; i--)
        {
            const M3HIT* h = &m3_hits[i];
            if (m3_mx < h->x || m3_mx >= h->x + h->w || m3_my < h->y || m3_my >= h->y + h->h) continue;
            m3_mval = h->val;
            k = h->kind == 1 ? 0x1F2 : h->kind == 2 ? 0x1F3 : h->kind == 3 ? K_ENTER : h->kind == 4 ? K_UP : h->kind == 5 ? K_DOWN : h->kind == 6 ? h->val : 0x1F4;
            break;
        }
    }
    else if ((b & 2) && !(m3_mb & 2)) k = (m3_over || m3_ask_until) ? K_ESC : 0;
    else if (w) k = w > 0 ? K_DOWN : K_UP;
    m3_mb = b;
    return k;
}
static void m3_pointer(void)
{
    static const char* const a[] = { "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X",
        "X........X", "X.....XXXX", "X..X..X", "X.X X..X", "XX  X..X", "X    X..X", "     X..X", "      XX" };
    int y, x;
    if (m3_mouse <= 0 || !m3_mseen) return;
    for (y = 0; y < 17; y++)
        for (x = 0; a[y][x]; x++)
        {
            int px = m3_mx + x, py = m3_my + y;
            if (px < 0 || py < 0 || px >= M3W || py >= M3H || a[y][x] == ' ') continue;
            m3_fb[py * M3W + px] = a[y][x] == 'X' ? 0x000000 : 0xFFFFFF;
        }
}

/* ------------------------------------------------------------------------------------------- frame and keys */
static double m3_last, m3_fps_t; static int m3_frames;
static double m3_frame_ms;
static void m3_frame(void)
{
    double t = m3_now(), t0 = t, hz; int k[80], n, si = 0, i; float dt;
    dt = (float)(t - m3_last); m3_last = t; if (dt > 0.2f) dt = 0.2f; if (dt < 0) dt = 0;
    if (m3_ask_until && t > m3_ask_until) { m3_answer(0); return; }
    { int g0 = m3_theme_gen; m3_theme_apply(); if (g0 != m3_theme_gen) m3_cards_cat = -1; }
    n = cat_items(g_cat, k);
    for (i = 0; i < n; i++) if (k[i] == g_sel) si = i;
    /* animation: ease towards the targets */
    m3_ring += (si - m3_ring) * (1 - expf(-dt * 9));
    m3_selanim += (si - m3_selanim) * (1 - expf(-dt * 14));
    m3_catanim += (0 - m3_catanim) * (1 - expf(-dt * 6));
    m3_knob += (-si * 0.5f - m3_knob) * (1 - expf(-dt * 10));
    for (i = 0; i < NCAT; i++) m3_btnpress[i] *= expf(-dt * 8);
    m3_bgphase = (float)t;
    m3_nhits = 0;
    if (m3_look != 1) { m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H; }
    if (m3_style == 0) m3_draw_panels(g_cat, g_sel, m3_selanim);
    else if (m3_style == 1) m3_draw_carousel(g_cat, g_sel, t);
    else m3_draw_module(g_cat, g_sel, t);
    if (t < m3_msg_until) { m3_rect(&m3_scr, 0, L.sm ? 38 : 70, M3W, 20, 0, 170); m3_text(&m3_scr, 8, L.sm ? 40 : 72, 0, m3_msg, 0xFFC040, 256); }
    if (m3_over == 1) m3_draw_help();
    else if (m3_over == 2) m3_draw_picker();
    if (m3_ask_until) m3_draw_ask();
    m3_pointer();
    /* at most one frame per screen refresh; drawn quickly: shown at the start of the retrace (no tearing) */
    hz = m3_refresh ? m3_refresh : (m3_refresh_seen >= 40 && m3_refresh_seen <= 200 ? m3_refresh_seen : 70);
    if (!m3_win && (m3_now() - t0) < 0.75 / hz)
    {
        double lim = m3_now() + 1.0 / hz;
        while ((inportb(0x3DA) & 8) && m3_now() < lim) ;
        while (!(inportb(0x3DA) & 8) && m3_now() < lim) ;
    }
    else if (m3_win) { while (m3_now() - t0 < 1.0 / hz) __dpmi_yield(); }   /* a DOS box: give Windows the CPU */
    m3_present();
    m3_frame_ms = (m3_now() - t0) * 1000;
    if (m3_look == 2) { if (m3_frame_ms > 90) m3_lod = 1; else if (m3_frame_ms < 35) m3_lod = 0; }
    if (++m3_frames, t - m3_fps_t >= 1.0)
    {   /* frames a second; auto quality drops to half resolution once if the PC is too slow */
        m3_fps = (float)(m3_frames / (t - m3_fps_t));
        if (getenv("M3PROF")) { FILE* pf = fopen("C:\\M3PROF.TXT", "a"); if (pf) { fprintf(pf, "style %d look %d %dx%d half %d fps %.1f\n", m3_style, m3_look, M3W, M3H, m3_half, m3_fps); fclose(pf); } }
        m3_frames = 0; m3_fps_t = t;
        if (m3_quality == 0 && !m3_half && m3_look != 1 && m3_style != 0 && t > 2.5 && m3_fps < 15)
        { m3_half = 1; m3_rw = M3W / 2; m3_rh = M3H / 2; m3_note_until = t + 5; ui_log("auto: half resolution"); }
    }
}
static void m3_set_quality(int q)
{
    m3_quality = q; m3_half = m3_quality == 2;
    m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H;
    m3_cfg_save(); m3_note_until = m3_now() + 5;
}
/* one key (or mouse key). Returns -1 to go on, 0 = leave the menu (MENUGO.BAT written, or Esc), 1 = text menu,
 * 2 = no graphics mode works any more (text menu) */
static int m3_handle(int c)
{
    int oc = g_cat;
    ui_log("3D key %03X cat %d sel %d%s", c, g_cat, g_sel, m3_over ? (m3_over == 1 ? " help" : " list") : "");
#ifdef CRASHKEY
    if (c == '!') *(volatile int*)0xFFFFFFF0 = 1;                         /* test build only: a GPF for the crash catcher */
#endif
    if (m3_ask_until) { m3_answer(c == 'y' || c == 'Y'); return -1; }
    if (m3_over == 1) { m3_over = 0; return -1; }                            /* help: any key back */
    if (m3_over == 2)
    {
        int n = m3_prows();
        if (c == K_ESC || c == 0x13E) m3_over = 0;
        else if (c == K_UP && m3_psel > 0) m3_psel--;
        else if (c == K_DOWN && m3_psel + 1 < n) m3_psel++;
        else if (c == K_PGUP) { m3_psel -= 10; if (m3_psel < 0) m3_psel = 0; }
        else if (c == K_PGDN) { m3_psel += 10; if (m3_psel >= n) m3_psel = n - 1; }
        else if (c == K_HOME) m3_psel = 0;
        else if (c == K_END) m3_psel = n - 1;
        else if (c == 0x1F4 || c == K_ENTER || c == K_LEFT || c == K_RIGHT)
        {
            int d = c == K_LEFT ? -1 : 1, nr = (int)(sizeof m3_rates / sizeof m3_rates[0]);
            if (c == 0x1F4) { if (m3_mval != m3_psel) { m3_psel = m3_mval; return -1; } }
            if (m3_psel == 0 && c == 0x1F4) m3_set_quality((m3_quality + 1) % 3);
            else if (m3_psel == 0) m3_set_quality((m3_quality + 3 + d) % 3);
            else if (m3_psel == 1 && c == 0x1F4) m3_prate = (m3_prate + 1) % nr;
            else if (m3_psel == 1) m3_prate = (m3_prate + nr + d) % nr;
            else if (c != K_LEFT && c != K_RIGHT)
            {
                int i = m3_psel - 2;
                m3_vbe_close();
                m3_want_w = m3_modes[i].w; m3_want_h = m3_modes[i].h; m3_want_b = m3_modes[i].bpp; m3_hz = m3_rates[m3_prate];
                if (!m3_enter(1)) return 2;
                m3_mouse_setup(); m3_cards_cat = -1; m3_over = 0;
                if (!m3_ask_until) m3_cfg_save();
            }
        }
        return -1;
    }
    if (c == K_ESC) { save_sel(&g_it[g_sel]); m3_vbe_close(); return 0; }
    if (c == 0x142) { save_sel(&g_it[g_sel]); m3_on = 0; m3_cfg_save(); m3_vbe_close(); m3_free_all(); return 1; } /* F8: text menu */
    if (c == K_F1) { m3_over = 1; return -1; }
    if (c == 0x13E) { m3_over = 2; m3_psel = m3_cur + 2; return -1; }       /* F4: the screen list */
    if (c == K_F3 || c == 0x140 || c == 0x141)                               /* F3 style, F6 colours, F7 look */
    {
        if (c == K_F3) { m3_style = (m3_style + 1) % M3_STYLES; m3_catanim = 1; }
        else if (c == 0x140) m3_ctheme = (m3_ctheme + 1) % 11;
        else m3_look = (m3_look + 1) % 3;
        m3_cfg_save();
        snprintf(m3_msg, sizeof m3_msg, "%s, %s, colours: %s", m3_stylename[m3_style], m3_lookname[m3_look], m3_theme_names[m3_ctheme]);
        m3_msg_until = m3_now() + 3;
        return -1;
    }
    if (c == 0x13F)                                                           /* F5: MIDI player */
    {
        int q;
        for (q = 0; q < g_n; q++) if (!strncmp(g_it[q].cmd, "MIDPLAY", 7)) break;
        if (q < g_n && usable(&g_it[q])) { save_sel(&g_it[g_sel]); m3_vbe_close(); if (write_go(&g_it[q], 0)) return 0; g_goerr = 1; return 1; }
        snprintf(m3_msg, sizeof m3_msg, "MIDI player: start the SC-55 (or SoundFont) first."); m3_msg_until = m3_now() + 4;
        return -1;
    }
    if (c == 0x1F2) { if (m3_mval != g_cat) { g_cat = m3_mval; g_sel = first_in(g_cat); } }       /* mouse: a menu */
    else if (c == 0x1F3) g_sel = m3_mval;                                                   /* mouse: an item */
    else if (c == K_UP) g_sel = move_in(g_cat, g_sel, -1);
    else if (c == K_DOWN) g_sel = move_in(g_cat, g_sel, 1);
    else if (c == K_LEFT || c == K_STAB || c == K_PGUP) { g_cat = next_cat(g_cat, -1); g_sel = first_in(g_cat); }
    else if (c == K_RIGHT || c == K_TAB || c == K_PGDN) { g_cat = next_cat(g_cat, 1); g_sel = first_in(g_cat); }
    else if (c == K_HOME) g_sel = first_in(g_cat);
    else if (c == K_END) { int kk[80], nn = cat_items(g_cat, kk); if (nn) g_sel = kk[nn - 1]; }
    else if (c >= '1' && c <= '9') { int kk[80], nn = cat_items(g_cat, kk); if (c - '1' < nn) g_sel = kk[c - '1']; }
    else if (c < 256 && isalpha(c)) { g_sel = jump_letter(g_sel, c); g_cat = g_it[g_sel].cat; }
    else if (c == K_ENTER || c == K_F2)
    {
        ITEM* it = &g_it[g_sel];
        if (c == K_F2 && it->kind != IT_GAME) return -1;
        if (!usable(it)) return -1;
        save_sel(it);
        if (it->kind == IT_VIEW || it->kind == IT_EDIT)
        {   /* these screens are text ones: show them, then come back */
            m3_vbe_close(); tui_init();
            if (it->kind == IT_VIEW) view(it->cmd);
            else
            {
                char keep[40]; int q;
                game_editor(); snprintf(keep, sizeof keep, "%s", it->label); build_menu();
                for (q = 0; q < g_n; q++) if (g_it[q].cat == C_GAMES) { g_sel = q; break; }
                for (q = 0; q < g_n; q++) if (!strcmp(g_it[q].label, keep)) g_sel = q;
                g_cat = g_it[g_sel].cat;
            }
            m3_cards_cat = -1;
            tui_done();
            if (!(m3_cur >= 0 && m3_try(m3_cur, m3_refresh)) && !m3_enter(0)) return 2;
            m3_mouse_setup();
            m3_last = m3_now();
            return -1;
        }
        if (it->kind == IT_M3D) return -1;
        m3_vbe_close();
        if (write_go(it, c == K_F2)) return 0;
        g_goerr = 1; return 1;
    }
    if (g_cat != oc) { m3_catanim = 1; m3_btnpress[g_cat] = 1; m3_cards_cat = -1; m3_ring = 0; m3_selanim = 0; }
    return -1;
}
/* returns 0: leave the menu (MENUGO.BAT written, or Esc), 1: go to the text menu, 2: no graphics mode (text menu) */
static int m3_main(void)
{
    int c, r;
    m3_vbe_probe();
    if (m3_tsc_us == 0) m3_time_init();
    if (!m3_enter(0)) { m3_free_all(); return 2; }
    g_in3d = 1;
    m3_last = m3_fps_t = m3_now(); m3_frames = 0;
    m3_cards_cat = -1; m3_catanim = 1;
    m3_mouse_setup();
    for (;;)
    {
        m3_frame();
        while ((c = m3_key()) != 0 || (c = m3_mouse_key()) != 0)
        {
            r = m3_handle(c);
            if (r >= 0) { g_in3d = 0; if (r != 0) m3_free_all(); return r; }
        }
    }
}
