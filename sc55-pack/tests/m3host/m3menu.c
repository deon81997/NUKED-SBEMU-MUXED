/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* m3menu.c - SC55MENU's 3D menu (menu3d.h + m3core.h + m3looks.h) built for 32-bit Linux with ASan + UBSan, like
 * m3asan.c does for MIDPLAY. The whole of tools/sc55menu.c is compiled against the fake DJGPP in inc/. Covers every
 * style x look x colour theme in every mode the fake card offers (320x200 .. 1600x1200, 8/15/16/24/32 bit, odd sizes),
 * full and half resolution, the help and screen-list overlays, the keep-mode question (yes, no, time-out, Windows),
 * mouse clicks on everything clickable, long texts, refresh rates, a BIOS that refuses modes, and out of memory.
 * Build and run: sh tests/m3host/run.sh menu [quick|crash]. Any sanitizer report or failed check stops it. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

static size_t hs_oom_over;
static void* hs_realloc(void* p, size_t n) { if (hs_oom_over && n >= hs_oom_over) return NULL; return realloc(p, n); }
static void* hs_malloc(size_t n) { if (hs_oom_over && n >= hs_oom_over) return NULL; return malloc(n); }
static void* hs_calloc(size_t a, size_t b) { if (hs_oom_over && a * b >= hs_oom_over) return NULL; return calloc(a, b); }
#define main menu_main
#define realloc hs_realloc
#define malloc hs_malloc
#define calloc hs_calloc
#include "../../tools/sc55menu.c"
#undef main
#undef realloc
#undef malloc
#undef calloc
#include "inc/fakebios.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static void frame(void) { m3_frame(); hs_frames++; }
/* a key the menu may get in a test (no key that leaves the menu or opens a text screen) */
static int safe_key(int c)
{
    if (c == K_ESC || c == K_ENTER) return m3_over || m3_ask_until;
    return c != 0x142 && c != 0x13F && c != K_F2 && c != 0;
}
static void key(int c) { if (safe_key(c)) { int r = m3_handle(c); CHECK(r < 0, "key %X left the menu (%d)", c, r); } frame(); }
/* click every clickable thing of the last frame that keeps us in the menu, one after another */
static void click_all(void)
{
    M3HIT h[96]; int n = m3_nhits, i;
    memcpy(h, m3_hits, sizeof h);
    for (i = 0; i < n; i++)
    {
        int k, r;
        hs_mx = h[i].x + h[i].w / 2; hs_my = h[i].y + h[i].h / 2; hs_mb = 1;
        k = m3_mouse_key(); hs_mb = 0; m3_mouse_key();
        if (!k) continue;
        if (k == K_ENTER && (g_it[g_sel].kind != IT_M3D)) continue;      /* would run it */
        if (!safe_key(k)) continue;
        r = m3_handle(k);
        CHECK(r < 0, "click kind %d val %d left the menu (%d)", h[i].kind, h[i].val, r);
        frame();
        if (m3_ask_until) { m3_handle('n'); frame(); }
    }
    hs_mwheel = 1; key(m3_mouse_key()); hs_mwheel = -1; key(m3_mouse_key());
    hs_mb = 2; m3_mouse_key(); hs_mb = 0; m3_mouse_key();
}
/* every style x look in the mode set now (themes rotate unless all), every category, half, overlays */
static void sweep(int all_themes)
{
    int st, lk, th, c;
    for (st = 0; st < 3; st++) for (lk = 0; lk < 3; lk++)
    {
        m3_style = st; m3_look = lk;
        if (all_themes) for (th = 0; th < 11; th++) { m3_ctheme = th; tui_theme = th % THEME_COUNT; frame(); }
        else { m3_ctheme = (int)(hs_frames % 11); frame(); }
        for (c = 0; c < NCAT; c++) { key(K_RIGHT); key(K_DOWN); key(K_DOWN); }
        key(K_END); key(K_HOME);
        m3_set_quality(2); frame(); m3_set_quality(1); frame(); m3_set_quality(0);
        m3_over = 1; frame(); m3_over = 0;
        m3_over = 2; m3_psel = 2 + (int)(hs_frames % (unsigned)m3_nmodes); frame(); m3_psel = m3_prows() - 1; frame(); m3_over = 0;
        m3_ask_until = m3_now() + 5; frame(); m3_ask_until = 0;
        snprintf(m3_msg, sizeof m3_msg, "A note across the screen that is quite a bit longer than a small screen is wide, really");
        m3_msg_until = m3_now() + 1; frame(); m3_msg_until = 0;
    }
}
static int enter_mode(int w, int h, int b, int hz)
{
    m3_vbe_close(); m3_want_w = w; m3_want_h = h; m3_want_b = b; m3_hz = hz;
    return m3_enter(0);
}
static void long_texts(void)
{   /* the longest label and description the menu holds, plus characters from the top of the font */
    int i;
    for (i = 0; i < g_n; i += 3)
    {
        memset(g_it[i].label, i % 2 ? 'W' : 0xDB, sizeof g_it[i].label - 1); g_it[i].label[sizeof g_it[i].label - 1] = 0;
        memset(g_it[i].desc, i % 2 ? 'M' : 0xB2, sizeof g_it[i].desc - 1); g_it[i].desc[sizeof g_it[i].desc - 1] = 0;
    }
    m3_cards_cat = -1;
}
int main(int argc, char** argv)
{
    int i, quick = argc > 1 && !strcmp(argv[1], "quick");
    srand(1);
    hs_dos[0x449] = 3;
    set_modes();
    mkdir("/tmp/m3menu", 0755);
    strcpy(g_home, "/tmp/m3menu");
    hs_mouse_on = 1;
    read_sbopts(); build_menu(); g_sel = 0; g_cat = g_it[0].cat;
    CHECK(g_n > 10, "menu items: %d", g_n);
    m3_vbe_probe(); m3_time_init();
    CHECK(m3_nmodes > 40, "modes found: %d", m3_nmodes);
    if (argc > 1 && !strcmp(argv[1], "crash"))
    {   /* the crash catcher: a few keys in 3D, then SIGSEGV (a real NULL write is caught by UBSan first) -> CRASH.TXT */
        crash_install(); g_in3d = 1;
        enter_mode(800, 600, 16, 0); m3_mouse_setup(); key(K_RIGHT); key(0x141); key(0x140); key(K_F3); frame(); key(0x13E); key(K_DOWN);
        raise(SIGSEGV);
        return 1;
    }
    /* 1: every theme at 640x480 32 bit and 320x200 VGA, all clickables */
    CHECK(enter_mode(640, 480, 32, 0) && M3W == 640, "640x480x32"); m3_mouse_setup(); sweep(1);
    for (i = 0; i < 9; i++) { m3_style = i % 3; m3_look = i / 3; frame(); click_all(); }
    CHECK(enter_mode(320, 200, 8, 0) && M3W == 320 && m3_bpp == 8, "320x200"); m3_mouse_setup(); sweep(1);
    for (i = 0; i < 9; i++) { m3_style = i % 3; m3_look = i / 3; frame(); click_all(); }
    /* 2: every mode the fake card offers, with a refresh rate now and then */
    for (i = 0; i < m3_nmodes; i++)
    {
        int ok = enter_mode(m3_modes[i].w, m3_modes[i].h, m3_modes[i].bpp, i % 4 == 1 ? 75 : 0);
        CHECK(ok && m3_cur == i, "mode %dx%dx%d", m3_modes[i].w, m3_modes[i].h, m3_modes[i].bpp);
        if (!ok) continue;
        printf("%4dx%-4d %2d bit  frames %lu\n", M3W, M3H, m3_bpp, hs_frames); fflush(stdout);
        m3_mouse_setup();
        if (!quick || i % 7 == 0) { sweep(0); m3_style = i % 3; m3_look = (i / 3) % 3; frame(); click_all(); }
        else { m3_style = i % 3; m3_look = (i / 3) % 3; frame(); }
    }
    /* 3: the screen list as a user drives it: a new mode, keep it (Y), a new one, refuse (N), one that times out */
    CHECK(enter_mode(640, 480, 32, 0), "back to 640x480");
    m3_handle(0x13E); for (i = 0; i < 12; i++) m3_handle(K_DOWN); m3_handle(K_RIGHT); m3_handle(K_RIGHT); m3_handle(K_ENTER); frame();
    CHECK(m3_ask_until > 0, "a new mode asks");
    { int w = M3W, h = M3H; m3_handle('y'); frame(); CHECK(m3_ok_w == w && m3_ok_h == h, "Y keeps %dx%d (ok %dx%d)", w, h, m3_ok_w, m3_ok_h); }
    { int w = m3_ok_w, h = m3_ok_h;
      m3_handle(0x13E); m3_handle(K_END); m3_handle(K_ENTER); frame(); CHECK(m3_ask_until > 0 && M3W == 1600, "1600x1200 asks");
      m3_handle(K_ESC); frame(); CHECK(M3W == w && M3H == h, "N went back to %dx%d (now %dx%d)", w, h, M3W, M3H);
      m3_handle(0x13E); m3_handle(K_HOME); m3_handle(K_DOWN); m3_handle(K_DOWN); m3_handle(K_ENTER); frame();
      m3_ask_until = m3_now() - 1; frame(); frame(); CHECK(M3W == w && M3H == h, "time-out went back to %dx%d (now %dx%d)", w, h, M3W, M3H); }
    /* 4: a BIOS that refuses every VESA mode: VGA 320x200 */
    hs_fail_set = 1; CHECK(enter_mode(1024, 768, 16, 0) && m3_modes[m3_cur].mode == 0x13, "refused -> 13h"); sweep(0); hs_fail_set = 0;
    /* 5: a Windows DOS box: auto = 320x200; a VESA mode asks every run until kept */
    m3_win = 1; m3_ok_w = 0; CHECK(enter_mode(0, 0, 0, 0) && M3W == 320, "Windows auto 320x200");
    CHECK(enter_mode(800, 600, 32, 0) && m3_ask_until > 0, "Windows VESA asks"); frame(); m3_handle('x'); frame(); CHECK(M3W == 320, "Windows refused -> 320x200");
    m3_win = 0;
    /* 6: long texts, random everything */
    long_texts();
    for (i = 0; i < (quick ? 200 : 2000); i++)
    {
        int k = rand() % m3_nmodes;
        if (i % 25 == 0) { enter_mode(m3_modes[k].w, m3_modes[k].h, m3_modes[k].bpp, rand() % 5 == 0 ? 85 : 0); m3_mouse_setup(); }
        m3_style = rand() % 3; m3_look = rand() % 3; m3_ctheme = rand() % 11; tui_theme = rand() % THEME_COUNT;
        if (rand() % 9 == 0) m3_set_quality(rand() % 3);
        if (rand() % 7 == 0) { static const int ks[] = { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_TAB, K_STAB, K_HOME, K_END, K_PGUP, K_PGDN, '1', '5', '9', 'a', 's', 'z', K_F1, 0x13E, 0x140, 0x141, K_F3 };
                               key(ks[rand() % (int)(sizeof ks / sizeof ks[0])]); if (m3_over && rand() % 2) key(K_ESC); }
        if (rand() % 23 == 0) { hs_mx = rand() % (M3W + 40); hs_my = rand() % (M3H + 40); hs_mb = 1; key(m3_mouse_key()); hs_mb = 0; m3_mouse_key(); if (m3_ask_until) m3_handle('n'); }
        if (rand() % 17 == 0) m3_t0 -= (unsigned long long)(m3_tsc_us * 1e6 * (rand() % 3 ? 1000 : 1e5));   /* a long clock */
        frame();
    }
    m3_over = 0;
    /* 7: out of memory: the menu falls back to a smaller mode, card textures stay plain */
    {
        static const size_t lim[] = { 16u << 20, 8u << 20, 4u << 20, 2u << 20, 600u << 10, 100u << 10 };
        int l;
        for (l = 0; l < 6; l++)
        {
            m3_vbe_close(); m3_free_all();
            hs_oom_over = lim[l];
            {
                int ok = enter_mode(1600, 1200, 32, 0);
                printf("OOM limit %zu KB: %s %dx%d\n", lim[l] >> 10, ok ? "fell back to" : "text menu,", ok ? M3W : 0, ok ? M3H : 0);
                if (ok) { CHECK((size_t)M3W * M3H * 4 < lim[l], "fb %dx%d over the limit", M3W, M3H); CHECK(l < 2 || M3W < 1600, "no fallback"); sweep(0); }
                else CHECK(lim[l] < 256000, "nothing fits at %zu", lim[l]);
            }
        }
        hs_oom_over = 0;
        m3_free_all();
        CHECK(enter_mode(1600, 1200, 32, 0) && M3W == 1600, "1600x1200 after OOM");
        frame();
    }
    /* 8: the config round trip */
    m3_style = 2; m3_look = 1; m3_ctheme = 9; m3_quality = 2; m3_want_w = 1024; m3_want_h = 768; m3_want_b = 16; m3_hz = 85; m3_on = 1; m3_cfg_save();
    m3_style = m3_look = m3_ctheme = m3_quality = m3_want_w = m3_hz = 0;
    CHECK(m3_cfg_load() == 1 && m3_style == 2 && m3_look == 1 && m3_ctheme == 9 && m3_quality == 2 && m3_want_w == 1024 && m3_want_b == 16 && m3_hz == 85, "MENU3D.CFG round trip");
    m3_on = 0; m3_cfg_save(); CHECK(m3_cfg_load() == 0 && m3_style == 2, "ON=0 keeps the style");
    { char cp[300]; FILE* f; home_path(cp, "MENU3D.CFG"); f = fopen(cp, "w"); fputs("STYLE=1\nQUALITY=0\n", f); fclose(f); CHECK(m3_cfg_load() == 1 && m3_style == 1, "old MENU3D.CFG = on"); }
    { char cp[300]; FILE* f; home_path(cp, "MENU3D.CFG"); f = fopen(cp, "w"); fputs("STYLE=-7\nLOOK=99999\nTHEME=-3\nMODE=99999 -5 x\nHZ=-4\n", f); fclose(f);
      m3_cfg_load(); CHECK(m3_style >= 0 && m3_style < 3 && m3_look < 3 && m3_ctheme >= 0 && m3_want_w == 0 && m3_hz == 0, "bad MENU3D.CFG"); }
    m3_vbe_close();
    printf("%lu frames, %d failures\n", hs_frames, fails);
    return fails != 0;
}
