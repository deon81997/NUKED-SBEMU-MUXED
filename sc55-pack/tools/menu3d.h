/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* menu3d.h - the optional 3D menu of SC55MENU (included by sc55menu.c, uses its items and actions).
 *
 * The text menu stays the main one. F8 (or Settings > 3D menu) switches to this one and back; the choice
 * is kept in MENU3D.CFG ("STYLE=n"; no file = text menu). Holding Shift at start (safe mode) always gives
 * the text menu, and so does a PC without a 640x480 VESA mode with 15/16/24/32-bit colour.
 *
 * Everything is drawn by the CPU (no 3D card needed): 640x480, 32-bit back buffer, z-buffer, perspective-
 * correct textures, converted to the VESA mode's pixel format on the way out. Styles (F3 cycles):
 *   0 Panels    - the menu as raised / pressed 3D buttons and panels (no real 3D, lightest)
 *   1 Carousel  - each item a card on a turning ring above a reflective floor
 *   2 Module    - a rack sound module: its LCD shows the item, the knob picks it, the buttons the category
 *   3 Voodoo    - (Glide, 3dfx cards: not built yet; falls back to Carousel)
 */
#include "m3core.h"
/* ------------------------------------------------------------------------------------------- scene data */
#define m3_catcol m3_accent                    /* the category colours: the colour style's bright accents */
static const char* m3_stylename[] = { "Panels", "Carousel", "Module", "Voodoo" };
#define M3_STYLES 3
/* mouse: what is under the pointer, collected while drawing (last added wins). kind: 1 menu, 2 item, 3 = the
 * chosen item (run it), 4 previous item, 5 next item */
typedef struct { short x, y, w, h, kind, val; } M3HIT;
static M3HIT m3_hits[64]; static int m3_nhits;
static void m3_hit(int x, int y, int w, int h, int kind, int val)
{ if (m3_nhits < 64) { M3HIT* t = &m3_hits[m3_nhits++]; t->x = x; t->y = y; t->w = w; t->h = h; t->kind = kind; t->val = val; } }
static int m3_mouse = -1, m3_mx = 320, m3_my = 240, m3_mb, m3_mseen;
static M3SURF m3_cards[80];          /* card textures of the current category (by item number) */
static int    m3_cards_cat = -1;
static M3SURF m3_panel;
static float  m3_ring, m3_catanim, m3_knob, m3_btnpress[NCAT];


static void m3_make_card(M3SURF* s, const ITEM* it)
{
    uint32_t c = m3_catcol[it->cat], top = m3_mul(c, 150), bot = m3_mul(c, 50); int ok = usable(it), y;
    if (!s->p) *s = m3_surf(256, 128);
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
static void m3_cards_build(int cat)
{
    int k[80], n = cat_items(cat, k), i;
    for (i = 0; i < n; i++) m3_make_card(&m3_cards[k[i]], &g_it[k[i]]);
    m3_cards_cat = cat;
}

/* the 2D layer shared by the 3D styles: category tabs, description, keys */
static void m3_overlay(int cat, int sel, int sb)
{
    int i, x = 12; const ITEM* it = &g_it[sel];
    m3_vgrad(&m3_scr, 0, 0, M3W, 40, 0x000000, 0x000000, 150);
    m3_text_glow(&m3_scr, 12, 6, 2, "NUKED SB-EMU", 0xFFFFFF, m3_glowc);
    m3_text(&m3_scr, 172, 12, 0, sb ? "SBEMU loaded" : "SBEMU not loaded", sb ? 0x60FF80 : 0xFFB040, 256);
    {
        char b[80];
        if (m3_now() < m3_note_until)
            snprintf(b, sizeof b, "%s  %s  %s %.0f fps", m3_stylename[m3_style], tui_theme_name[tui_theme],
                     m3_quality == 1 ? "full" : m3_quality == 2 ? "half" : m3_half ? "auto: half" : "auto: full", m3_fps);
        else snprintf(b, sizeof b, "%s", m3_stylename[m3_style]);
        m3_text(&m3_scr, M3W - 8 - 8 * (int)strlen(b), 12, 0, b, 0x8090B0, 256);
    }
    for (i = 0; i < NCAT; i++)
    {
        int w = (int)strlen(g_catname[i]) * 8 + 16, on = i == cat, k[80];
        if (!cat_items(i, k)) continue;
        x += 0;
        m3_rect(&m3_scr, x + 300, 44, w, 22, on ? m3_catcol[i] : 0x101830, on ? 230 : 170);
        m3_bevel(&m3_scr, x + 300, 44, w, 22, 2, on, 180);
        m3_text(&m3_scr, x + 308, 47, 0, g_catname[i], on ? 0xFFFFFF : 0x90A0C0, 256);
        m3_hit(x + 300, 44, w, 22, 1, i);
        x += w + 4;
        if (x + 300 > M3W - 40) break;
    }
    /* description box: title, 3 lines, then the keys on their own line (they used to share the last line) */
    m3_rect(&m3_scr, 10, 354, M3W - 20, 118, 0x000010, 170);
    m3_bevel(&m3_scr, 10, 354, M3W - 20, 118, 2, 1, 120);
    m3_text_glow(&m3_scr, 22, 360, 1, it->label, m3_mul(m3_catcol[it->cat], 400), m3_catcol[it->cat]);
    m3_wrap(&m3_scr, 22, 384, M3W - 44, 3, 0, it->desc, 0xD0D8E8, 0);
    m3_rect(&m3_scr, 18, 449, M3W - 36, 1, 0x7080A0, 90);
    m3_text(&m3_scr, 22, 452, 0, "Arrows choose  Enter go  F3 style  F6 colours  F8 text  Esc quit", 0x7080A0, 256);
}

/* ------------------------------------------------------------------------------------------- styles */
/* half resolution: the scene in the top left 320x240 is doubled to fill the screen */

static void m3_draw_panels(int cat, int sel, float selanim)
{
    int k[80], n = cat_items(cat, k), i, x, y, j, t = (int)(m3_bgphase * 40);
    const ITEM* it = &g_it[sel];
    {   /* sin(x*0.01 + b) = sin(x*0.01) cos b + cos(x*0.01) sin b: two tables instead of 300000 sinf a frame */
        /* integers per pixel: a float-to-int conversion per pixel (307200 a frame) is very slow on i386-built code */
        static int sx[M3W], cx[M3W]; static int init, lgen = -1; static uint32_t lut[64];
        if (lgen != m3_theme_gen)
        {   /* the waves in the colour style's desk colour: dark .. lighter, with a touch of the highlight */
            int i2; uint32_t d = m3_lerpc(m3_sky1, m3_accent[0], 40);
            for (i2 = 0; i2 < 64; i2++) lut[i2] = m3_lerpc(m3_mul(d, 70), m3_mul(d, 300), i2 * 4);
            lgen = m3_theme_gen;
        }
        if (!init) { for (x = 0; x < M3W; x++) { sx[x] = (int)(14 * 256 * sinf(x * 0.01f)); cx[x] = (int)(14 * 256 * cosf(x * 0.01f)); } init = 1; }
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
    /* title bar */
    m3_shadow(&m3_scr, 12, 10, M3W - 24, 46, 6);
    m3_vgrad(&m3_scr, 12, 10, M3W - 24, 46, m3_mul(m3_lerpc(m3_sky1, m3_accent[0], 90), 330), m3_mul(m3_sky1, 160), 256);
    m3_bevel(&m3_scr, 12, 10, M3W - 24, 46, 3, 0, 200);
    m3_text_glow(&m3_scr, 28, 18, 3, "NUKED SB-EMU", 0xFFFFFF, m3_glowc);
    m3_text(&m3_scr, 250, 26, 0, g_running ? "SBEMU is loaded" : "SBEMU is not loaded", g_running ? 0x80FF90 : 0xFFC060, 256);
    m3_text(&m3_scr, M3W - 120, 26, 0, "Panels", 0xA0B8E0, 256);
    /* categories */
    for (i = 0, j = 0; i < NCAT; i++)
    {
        int on = i == cat, yy;
        if (!cat_items(i, k)) continue;
        yy = 72 + j * 46; j++;
        m3_shadow(&m3_scr, 14 + on * 2, yy + on * 2, 150, 38, on ? 2 : 5);
        m3_vgrad(&m3_scr, 14 + on * 2, yy + on * 2, 150, 38, on ? m3_mul(m3_catcol[i], 300) : 0x485878, on ? m3_mul(m3_catcol[i], 120) : 0x283048, 256);
        m3_bevel(&m3_scr, 14 + on * 2, yy + on * 2, 150, 38, 3, on, 220);
        m3_text_sh(&m3_scr, 28 + on * 2, yy + 9 + on * 2, 1, g_catname[i], 0xFFFFFF);
        m3_hit(14, yy, 152, 40, 1, i);
    }
    n = cat_items(cat, k);
    /* items */
    m3_shadow(&m3_scr, 178, 66, M3W - 192, 290, 6);
    m3_rect(&m3_scr, 178, 66, M3W - 192, 290, 0x101828, 220);
    m3_bevel(&m3_scr, 178, 66, M3W - 192, 290, 2, 1, 200);
    {
        int top = 0, rows = 7, si = 0;
        for (i = 0; i < n; i++) if (k[i] == sel) si = i;
        if (si >= rows) top = si - rows + 1;
        {   /* the highlight slides to the chosen row */
            float hy = 74 + (selanim - top) * 40;
            if (hy >= 70 && hy < 74 + rows * 40)
            { m3_rect(&m3_scr, 186, (int)hy - 2, M3W - 208, 40, m3_catcol[cat], 70); }
        }
        for (i = top; i < n && i < top + rows; i++)
        {
            const ITEM* t2 = &g_it[k[i]]; int on = k[i] == sel, yy = 74 + (i - top) * 40, ok = usable(t2);
            uint32_t a = on ? m3_mul(m3_catcol[cat], 330) : 0x5A6888, b = on ? m3_mul(m3_catcol[cat], 140) : 0x303850;
            if (!ok) { a = 0x3A3F4A; b = 0x22252C; }
            if (!on) m3_shadow(&m3_scr, 188, yy, M3W - 212, 34, 3);
            m3_vgrad(&m3_scr, 188 + on, yy + on, M3W - 212, 34, a, b, 256);
            m3_bevel(&m3_scr, 188 + on, yy + on, M3W - 212, 34, 2, on, 220);
            { char num[4]; snprintf(num, sizeof num, "%d", i + 1); if (i < 9) m3_text(&m3_scr, 198 + on, yy + 7 + on, 1, num, 0xC0D0F0, 256); }
            if (on && ok) m3_text_glow(&m3_scr, 224 + on, yy + 7 + on, 1, t2->label, 0xFFFFFF, m3_glowc);
            else m3_text_sh(&m3_scr, 224 + on, yy + 7 + on, 1, t2->label, ok ? 0xFFFFFF : 0x8890A0);
            m3_hit(188, yy, M3W - 212, 36, on ? 3 : 2, k[i]);
        }
        {   /* small arrows in the right margin when items are hidden above or below */
            int r2;
            if (top > 0) for (r2 = 0; r2 < 4; r2++) m3_rect(&m3_scr, 621 - r2, 70 + r2, 1 + 2 * r2, 1, 0xC0D0F0, 256);
            if (top + rows < n) for (r2 = 0; r2 < 4; r2++) m3_rect(&m3_scr, 621 - r2, 351 - r2, 1 + 2 * r2, 1, 0xC0D0F0, 256);
        }
    }
    /* description */
    m3_shadow(&m3_scr, 12, 366, M3W - 24, 104, 6);
    m3_rect(&m3_scr, 12, 366, M3W - 24, 104, 0x0A0F1C, 235);
    m3_bevel(&m3_scr, 12, 366, M3W - 24, 104, 2, 1, 200);
    m3_wrap(&m3_scr, 24, 374, M3W - 48, 4, 0, it->desc, 0xD8E0F0, 0);
    m3_text(&m3_scr, 24, 450, 0, "Arrows choose  Enter go  F3 style  F6 colours  F8 text  Esc quit", 0x8090B0, 256);
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
    m3_sky();
    for (pass = 0; pass < 2; pass++)
    {   /* pass 0: the mirror images under the floor, pass 1: the cards */
        if (pass == 1) m3_draw_floor(-1.0f, 160);
        for (i = 0; i < n; i++)
        {
            float a = (i - m3_ring) * step, x = R * sinf(a), z = -R * cosf(a), on = (k[i] == sel), bob, y, sc;
            bob = on ? 0.08f * sinf((float)t * 2.2f) : 0;
            sc = on ? 1.12f : 1.0f;
            y = bob - rise * (1.0f + 0.25f * i);
            if (pass == 0) y = -2.0f - y;
            m3_boxback = m3_mul(m3_catcol[cat], 110);
            m3_box(m3_v(x, y, z), 1.0f * sc, 0.5f * sc, 0.05f, -a, &m3_cards[k[i]], 0x303848, on ? 0.35f + 0.15f * sinf((float)t * 3) : 0,
                   pass == 0 ? 256 : 256);
            (void)si;
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
    {   /* where you are, and that there is more left and right */
        char b[24]; snprintf(b, sizeof b, "%d / %d", si + 1, n);
        m3_text_glow(&m3_scr, M3W / 2 - (int)strlen(b) * 5, 318, 1, b, 0xFFFFFF, m3_catcol[cat]);
        if (n > 1)
        {
            m3_text_glow(&m3_scr, 24, 200, 3, "<", 0xFFFFFF, m3_catcol[cat]);
            m3_text_glow(&m3_scr, M3W - 40, 200, 3, ">", 0xFFFFFF, m3_catcol[cat]);
        }
    }
    m3_hit(0, 70, 170, 295, 4, 0); m3_hit(470, 70, 170, 295, 5, 0); m3_hit(170, 70, 300, 295, 3, 0);
    m3_overlay(cat, sel, g_running);
}

static void m3_make_panel(int cat, int sel, double t)
{
    M3SURF* s = &m3_panel; const ITEM* it = &g_it[sel]; int i, k[80], n = cat_items(cat, k), si = 0;
    char line[48];
    static int kcat = -1, ksel = -1, koff = -1, krun = -1;
    if (!s->p) *s = m3_surf(512, 128);
    for (i = 0; i < n; i++) if (k[i] == sel) si = i;
    {   /* drawn again only when something on it changed (it was 1/5 of a frame on a Pentium) */
        int len = (int)strlen(it->desc), off = len > 28 ? (int)(t * 6) % (len + 8) : 0;
        if (cat == kcat && sel == ksel && off == koff && g_running == krun) return;
        kcat = cat; ksel = sel; koff = off; krun = g_running;
    }
    {   /* brushed metal: horizontal streaks (a brightness per row plus fine grain), lighter towards the top */
        int y, x;
        for (y = 0; y < 128; y++)
        {
            unsigned h = (unsigned)y * 2654435761u; int row = (int)(h >> 28) - 8, top = (128 - y) / 10;
            for (x = 0; x < 512; x++) { int v = 46 + top + row / 2 + (int)(((unsigned)(x * 7 + y * 131) * 2246822519u) >> 30); s->p[y * 512 + x] = m3_rgb(v, v, v + 5); }
        }
    }
    m3_bevel(s, 0, 0, 512, 128, 3, 0, 120);
    m3_text(s, 12, 8, 2, "NUKED", 0xE8E8F0, 256);
    m3_text(s, 12, 36, 0, "SB-EMU", 0xB8B8C8, 256);
    m3_text(s, 12, 54, 0, "SOUND MODULE", 0x8A8A98, 256);
    m3_text(s, 12, 74, 0, "GS MT-32 OPL3", 0x6A6A78, 256);
    /* LCD */
    {   /* the LCD's backlight glowing through around its window */
        int g; for (g = 6; g >= 1; g--) m3_rect(s, 150 - g, 10 - g, 250 + 2 * g, 64 + 2 * g, 0x8CC838, 14);
    }
    m3_rect(s, 150, 10, 250, 64, 0x101810, 256);
    m3_bevel(s, 150, 10, 250, 64, 2, 1, 200);
    { int lit = 0x8CC838 ; m3_vgrad(s, 154, 14, 242, 56, m3_mul(lit, 300), m3_mul(lit, 220), 256); }
    snprintf(line, sizeof line, "%-8.8s %2d/%-2d", g_catname[cat], si + 1, n);
    m3_text(s, 160, 17, 0, line, 0x1C2C10, 256);
    snprintf(line, sizeof line, "%.24s", it->label); m3_text(s, 160, 34, 0, line, 0x10200A, 256);
    {   /* the description scrolls along the last line */
        int len = (int)strlen(it->desc), off = len > 28 ? (int)(t * 6) % (len + 8) : 0; char sc[30];
        for (i = 0; i < 29; i++) { int p = off + i; sc[i] = p < len ? it->desc[p] : ' '; } sc[29] = 0;
        m3_text(s, 160, 51, 0, sc, 0x2A3C18, 256);
    }
    /* category buttons: labels and LEDs (the buttons themselves are 3D boxes) */
    for (i = 0; i < NCAT; i++)
    {
        int bx = 150 + i * 42, on = i == cat;
        m3_rect(s, bx + 12, 82, 10, 5, on ? 0xFF3020 : 0x401010, 256);
        static const char* const sh[NCAT] = { "SND", "SET", "TEST", "GAME", "CD", "FILE" };
        m3_text(s, bx + 17 - (int)strlen(sh[i]) * 4, 110, 0, sh[i], on ? 0xFFFFFF : 0xA0A0B0, 256);
    }
    m3_text(s, 432, 104, 0, "SELECT", 0xB0B0C0, 200);
    m3_rect(s, 492, 12, 8, 8, g_running ? 0x30FF40 : 0x203020, 256);
    m3_text(s, 470, 24, 0, "PWR", 0x9090A0, 200);
}

static void m3_draw_module(int cat, int sel, double t)
{
    float W = 2.9f, H = 0.72f, D = 1.6f; int c;
    m3_fog = m3_fogc; m3_fog0 = 8; m3_fog1 = 22;
    m3_camera(0.9f * sinf((float)t * 0.25f), 0.15f + 0.1f * sinf((float)t * 0.17f), -6.6f, -0.14f * sinf((float)t * 0.25f), -0.12f);
    m3_sky();
    m3_draw_floor(-H - 0.02f, 256);
    m3_make_panel(cat, sel, t);
    m3_box(m3_v(0, 0, 0), W, H, D, 0, &m3_panel, 0x26262C, 0, 256);
    m3_box(m3_v(-W + 0.3f, -H - 0.01f, -D + 0.3f), 0.12f, 0.02f, 0.12f, 0, NULL, 0x101010, 0, 256);   /* feet */
    m3_box(m3_v(W - 0.3f, -H - 0.01f, -D + 0.3f), 0.12f, 0.02f, 0.12f, 0, NULL, 0x101010, 0, 256);
    /* rack ears */
    m3_box(m3_v(-W - 0.12f, 0, -D + 0.03f), 0.12f, H, 0.03f, 0, NULL, 0x303036, 0, 256);
    m3_box(m3_v(W + 0.12f, 0, -D + 0.03f), 0.12f, H, 0.03f, 0, NULL, 0x303036, 0, 256);
    /* category buttons (panel texture x 150 + i*42 .. of 512, y 88..104 of 128) */
    for (c = 0; c < NCAT; c++)
    {
        float u = (150 + c * 42 + 17) / 512.0f, v = 96 / 128.0f, x = -W + u * 2 * W, y = H - v * 2 * H, p = m3_btnpress[c];
        m3_box(m3_v(x, y, -D - 0.06f + p * 0.05f), 0.11f, 0.06f, 0.05f, 0, NULL, c == cat ? m3_mul(m3_catcol[c], 230) : 0x505058, c == cat ? 0.2f : 0, 256);
    }
   
    /* the knob: one notch per item */
    m3_knob3d(m3_v(-W + (440 / 512.0f) * 2 * W, H - (56 / 128.0f) * 2 * H, -D - 0.16f), 0.34f, 0.16f, m3_knob, 0);
    m3_upscale();
    m3_hit(0, 70, 170, 295, 4, 0); m3_hit(470, 70, 170, 295, 5, 0); m3_hit(170, 70, 300, 295, 3, 0);
    m3_overlay(cat, sel, g_running);
}

/* ------------------------------------------------------------------------------------------- loop */
static void m3_cfg_save(void)
{ char p[300]; FILE* f; home_path(p, "MENU3D.CFG"); if ((f = fopen(p, "w")) != NULL) { fprintf(f, "STYLE=%d\nQUALITY=%d\n", m3_style, m3_quality); fclose(f); } }
static int m3_cfg_load(void)
{
    char p[300], l[40]; FILE* f; home_path(p, "MENU3D.CFG");
    if ((f = fopen(p, "r")) == NULL) return 0;
    while (fgets(l, sizeof l, f))
    {
        if (!strncmp(l, "STYLE=", 6)) m3_style = atoi(l + 6) % M3_STYLES;
        else if (!strncmp(l, "QUALITY=", 8)) m3_quality = atoi(l + 8) % 3;
    }
    fclose(f); return 1;
}
static int m3_key(void)
{
    int c;
    if (!kbhit()) return 0;
    c = getch(); if (c == 0 || c == 0xE0) c = 0x100 | getch();
    return c;
}
static int m3_open(void)
{
    if (!m3_fb) m3_fb = (uint32_t*)malloc(M3W * M3H * 4);
    if (!m3_zb) m3_zb = (float*)malloc(M3W * M3H * sizeof(float));
    if (!m3_fb || !m3_zb) return 0;
    m3_scr.w = M3W; m3_scr.h = M3H; m3_scr.p = m3_fb;
    m3_theme_sync();
    return m3_vbe_open();
}

/* returns 0: leave the menu (MENUGO.BAT written, or Esc), 1: go to the text menu, 2: no VESA mode (text menu) */
/* the mouse in the 640x480 screen: INT 33h with its range set to the screen, the pointer drawn here */
static void m3_mouse_setup(void)
{
    __dpmi_regs r;
    if (m3_mouse < 0) { memset(&r, 0, sizeof r); r.x.ax = 0; __dpmi_int(0x33, &r); m3_mouse = r.x.ax == 0xFFFF; }
    if (!m3_mouse) return;
    memset(&r, 0, sizeof r); r.x.ax = 7; r.x.cx = 0; r.x.dx = M3W - 1; __dpmi_int(0x33, &r);
    memset(&r, 0, sizeof r); r.x.ax = 8; r.x.cx = 0; r.x.dx = M3H - 1; __dpmi_int(0x33, &r);
    memset(&r, 0, sizeof r); r.x.ax = 4; r.x.cx = m3_mx; r.x.dx = m3_my; __dpmi_int(0x33, &r);
    tui_mouse = -1;                                   /* the text screens set their own range again */
}
static int m3_mval;
/* a key from the mouse: 0x1F2 = open menu m3_mval, 0x1F3 = choose item m3_mval, or Enter / Up / Down */
static int m3_mouse_key(void)
{
    __dpmi_regs r;
    int b, w, i, k = 0;
    if (m3_mouse <= 0) return 0;
    memset(&r, 0, sizeof r); r.x.ax = 3; __dpmi_int(0x33, &r);
    b = r.x.bx & 7; w = (signed char)r.h.bh;
    if (r.x.cx != m3_mx || r.x.dx != m3_my) { m3_mx = r.x.cx; m3_my = r.x.dx; m3_mseen = 1; }
    if ((b & 1) && !(m3_mb & 1))
    {
        m3_mseen = 1;
        for (i = m3_nhits - 1; i >= 0; i--)
        {
            const M3HIT* h = &m3_hits[i];
            if (m3_mx < h->x || m3_mx >= h->x + h->w || m3_my < h->y || m3_my >= h->y + h->h) continue;
            m3_mval = h->val;
            k = h->kind == 1 ? 0x1F2 : h->kind == 2 ? 0x1F3 : h->kind == 3 ? K_ENTER : h->kind == 4 ? K_UP : K_DOWN;
            break;
        }
    }
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
            if (px >= M3W || py >= M3H || a[y][x] == ' ') continue;
            m3_fb[py * M3W + px] = a[y][x] == 'X' ? 0x000000 : 0xFFFFFF;
        }
}
static int m3_main(void)
{
    double last, t; int c, k[80], n;
    float selanim = 0;
    double fps_t; int frames = 0;
    if (!m3_open()) return 2;
    m3_time_init();
    last = fps_t = m3_now();
    m3_half = m3_quality == 2; m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H;
    m3_cards_cat = -1; m3_catanim = 1;
    m3_mouse_setup();
    for (;;)
    {
        int si = 0, i; float dt;
        t = m3_now(); dt = (float)(t - last); last = t; if (dt > 0.2f) dt = 0.2f;
        { int g0 = m3_theme_gen; m3_theme_sync(); if (g0 != m3_theme_gen) m3_cards_cat = -1; }
        n = cat_items(g_cat, k);
        for (i = 0; i < n; i++) if (k[i] == g_sel) si = i;
        /* animation: ease towards the targets */
        m3_ring += (si - m3_ring) * (1 - expf(-dt * 9));
        selanim += (si - selanim) * (1 - expf(-dt * 14));
        m3_catanim += (0 - m3_catanim) * (1 - expf(-dt * 6));
        m3_knob += (-si * 0.5f - m3_knob) * (1 - expf(-dt * 10));
        for (i = 0; i < NCAT; i++) m3_btnpress[i] *= expf(-dt * 8);
        m3_bgphase = (float)t;
        m3_nhits = 0;
        if (m3_style == 0) m3_draw_panels(g_cat, g_sel, selanim);
        else if (m3_style == 1) m3_draw_carousel(g_cat, g_sel, t);
        else m3_draw_module(g_cat, g_sel, t);
        m3_pointer();
        m3_present();
        if (++frames, t - fps_t >= 1.0)
        {   /* frames a second; auto quality drops to half resolution once if the PC is too slow */
            m3_fps = (float)(frames / (t - fps_t));
            if (getenv("M3PROF")) { FILE* pf = fopen("C:\\M3PROF.TXT", "a"); if (pf) { fprintf(pf, "style %d half %d fps %.1f\n", m3_style, m3_half, m3_fps); fclose(pf); } }
            frames = 0; fps_t = t;
            if (m3_quality == 0 && !m3_half && m3_style != 0 && t > 2.5 && m3_fps < 15)
            { m3_half = 1; m3_rw = M3W / 2; m3_rh = M3H / 2; m3_note_until = t + 5; }
        }
        while ((c = m3_key()) != 0 || (c = m3_mouse_key()) != 0)
        {
            int oc = g_cat;
            if (c == K_ESC) { save_sel(&g_it[g_sel]); m3_vbe_close(); return 0; }
            if (c == 0x142) { char p[300]; save_sel(&g_it[g_sel]); home_path(p, "MENU3D.CFG"); remove(p); m3_vbe_close(); return 1; } /* F8 */
            if (c == K_F3) { m3_style = (m3_style + 1) % M3_STYLES; m3_cfg_save(); m3_catanim = 1; continue; }
            if (c == 0x140) { tui_theme = (tui_theme + 1) % THEME_COUNT; if (tui_theme == THEME_CLASSIC) tui_theme = (tui_theme + 1) % THEME_COUNT; tui_theme_save(g_home); m3_note_until = m3_now() + 3; continue; }   /* F6: colours */
            if (c == 0x13E)                                             /* F4: auto / full / half resolution */
            {
                m3_quality = (m3_quality + 1) % 3;
                m3_half = m3_quality == 2;
                m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H;
                m3_cfg_save(); m3_note_until = m3_now() + 5;
                continue;
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
                if (c == K_F2 && it->kind != IT_GAME) continue;
                if (!usable(it)) continue;
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
                        g_cat = g_it[g_sel].cat; m3_cards_cat = -1;
                    }
                    tui_done();
                    if (!m3_vbe_open()) return 2;
                    m3_mouse_setup();
                    last = m3_now();
                    continue;
                }
                if (it->kind == IT_M3D) continue;
                m3_vbe_close();
                if (write_go(it, c == K_F2)) return 0;
                return 1;
            }
            if (g_cat != oc) { m3_catanim = 1; m3_btnpress[g_cat] = 1; m3_cards_cat = -1; m3_ring = 0; selanim = 0; }
        }
    }
}
