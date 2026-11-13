/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* m3asan.c - MIDPLAY's 3D view (midplay3d.h + m3core.h) built for 32-bit Linux with -fsanitize=address,undefined.
 * The whole of tools/midplay.c is compiled against the fake DJGPP in inc/ (dosstub.h); this file is the fake BIOS and
 * the test: every look x theme x player, full and half resolution, paused, in every screen size and colour depth from
 * 320x200 to 1600x1200 (plus odd sizes), mode switches between them, and out-of-memory at the big modes.
 * Build and run: sh tests/m3host/run.sh  (needs gcc-multilib). Any sanitizer report or failed check stops it. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* out of memory on demand: allocations of hs_oom_over bytes or more fail */
static size_t hs_oom_over;
static void* hs_realloc(void* p, size_t n) { if (hs_oom_over && n >= hs_oom_over) return NULL; return realloc(p, n); }
static void* hs_malloc(size_t n) { if (hs_oom_over && n >= hs_oom_over) return NULL; return malloc(n); }
static void* hs_calloc(size_t a, size_t b) { if (hs_oom_over && a * b >= hs_oom_over) return NULL; return calloc(a, b); }
#define main midplay_main
#define realloc hs_realloc
#define malloc hs_malloc
#define calloc hs_calloc
#include "../../tools/midplay.c"
#undef main
#undef realloc
#undef malloc
#undef calloc

#include "inc/fakebios.h"

/* ---- the test */
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static void write_song(const char* path)
{   /* 60 s: 8 channels of notes + drums on channel 10, program changes, 120 bpm */
    unsigned char* t = (unsigned char*)malloc(200000); int n = 0, i, c; FILE* f;
    #define B(x) t[n++] = (unsigned char)(x)
    for (c = 0; c < 9; c++) { B(0); B(0xC0 | c); B(c * 9); }
    for (i = 0; i < 480; i++)
    {
        for (c = 0; c < 8; c++) { B(0); B(0x90 | c); B(36 + (i * 7 + c * 5) % 60); B(60 + c * 8); }
        B(0); B(0x99); B(i % 2 ? 38 : 36); B(110);
        B(60); for (c = 0; c < 8; c++) { B(0x80 | c); B(36 + (i * 7 + c * 5) % 60); B(0); if (c < 7) B(0); }
        B(0); B(0x89); B(i % 2 ? 38 : 36); B(0);
    }
    B(0); B(0xFF); B(0x2F); B(0);
    f = fopen(path, "wb");
    fwrite("MThd\0\0\0\6\0\0\0\1\0\x78", 1, 14, f);
    { unsigned char h[8] = { 'M','T','r','k', (unsigned char)(n >> 24), (unsigned char)(n >> 16), (unsigned char)(n >> 8), (unsigned char)n }; fwrite(h, 1, 8, f); }
    fwrite(t, 1, (size_t)n, f); fclose(f); free(t);
    #undef B
}
static void frame(int paused)
{
    int i;
    for (i = 0; i < 3; i++) { mp_pump(); viz_decay(); }
    mp3_force = 1;
    mp3_draw(&P.s, "C:\\MIDI\\TEST.MID", song_time(), P.total, paused, 0);
    hs_frames++;
}
/* every look x player in the mode set now (themes rotate), full and half resolution, and paused */
static void sweep(int all_themes)
{
    int sc, mo, th;
    for (sc = 0; sc < 3; sc++) for (mo = 0; mo < 4; mo++)
    {
        g_scene = sc; g_model = mo; tui_theme = (int)(hs_frames / 7) % THEME_COUNT;
        if (all_themes) for (th = 0; th < 11; th++) { g_ctheme = th; frame(0); }
        else { g_ctheme = (sc * 4 + mo + (int)hs_frames) % 11; frame(0); frame(0); }
        m3_half = 1; m3_rw = M3W / 2; m3_rh = M3H / 2; frame(0);    /* F4 */
        m3_half = 0; m3_rw = M3W; m3_rh = M3H;
        frame(1);
    }
}
static int enter_mode(int w, int h, int b)
{
    mp3_leave(); mp3_want_w = w; mp3_want_h = h; mp3_want_b = b; mp3_hz = 0;
    return mp3_enter();
}
int main(int argc, char** argv)
{
    char err[200]; int i, quick = argc > 1 && !strcmp(argv[1], "quick");
    srand(1);
    hs_dos[0x449] = 3;
    set_modes();
    write_song("/tmp/m3asan.mid");
    g_dry = 1; g_ui = 1; g_page = PG_PLAYER; g_view = 2;
    clock_init();
    m3_hook = mp_pump;
    m3_vbe_probe(); mp3_prepare();
    CHECK(m3_nmodes > 40, "modes found: %d", m3_nmodes);
    CHECK(mp_start("/tmp/m3asan.mid", 0, err, sizeof err), "load: %s", err);
    if (argc > 1 && !strcmp(argv[1], "crash"))
    {   /* the crash catcher: a few keys in 3D, then a real NULL write */
        strcpy(g_home, "/tmp"); crash_install();
        enter_mode(800, 600, 16); handle_key('v'); handle_key('c'); handle_key('j'); frame(0); handle_key(' ');
        { volatile int* np = (volatile int*)(uintptr_t)(argc > 2 ? 0 : 0); *np = 1; }
        return 1;
    }
    /* 1: every theme at 640x480 32 bit and 320x200 VGA */
    CHECK(enter_mode(640, 480, 32), "640x480x32"); sweep(1);
    CHECK(enter_mode(320, 200, 8) && m3_modes[mp3_cur].w == 320, "320x200"); sweep(1);
    /* 2: every mode the fake card offers */
    for (i = 0; i < m3_nmodes; i++)
    {
        int ok = enter_mode(m3_modes[i].w, m3_modes[i].h, m3_modes[i].bpp);
        CHECK(ok && mp3_cur == i, "mode %dx%dx%d", m3_modes[i].w, m3_modes[i].h, m3_modes[i].bpp);
        if (!ok) continue;
        printf("%4dx%-4d %2d bit  frames %lu\n", M3W, M3H, m3_bpp, hs_frames); fflush(stdout);
        if (!quick || i % 7 == 0) sweep(0);
        mp_seek((uint64_t)(rand() % 55) * 1000000);
    }
    /* 3: mode switches without leaving (as Options does), big <-> small */
    for (i = 0; i < 40; i++)
    {
        int k = rand() % m3_nmodes;
        to_text(); enter_mode(m3_modes[k].w, m3_modes[k].h, m3_modes[k].bpp);
        g_scene = rand() % 3; g_model = rand() % 4; g_ctheme = rand() % 11; m3_half = rand() % 2;
        m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H;
        frame(rand() % 2); frame(0);
    }
    m3_half = 0;
    /* 5: random everything: styles, a long clock, seeks, long song titles, F4, every mode */
    for (i = 0; i < (quick ? 150 : 1500); i++)
    {
        int k = rand() % m3_nmodes;
        if (i % 25 == 0) { to_text(); enter_mode(m3_modes[k].w, m3_modes[k].h, m3_modes[k].bpp); }
        g_scene = rand() % 3; g_model = rand() % 4; g_ctheme = rand() % 11; tui_theme = rand() % THEME_COUNT;
        if (rand() % 9 == 0) { m3_half = !m3_half; m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H; }
        if (rand() % 13 == 0) mp_seek((uint64_t)(rand() % 70) * 1000000);
        if (rand() % 17 == 0) mp3_anim = rand() % 3 ? (double)(rand() % 100000) : 1e7;
        if (rand() % 11 == 0) { memset(P.s.title, rand() % 2 ? 'W' : 0xDB, sizeof P.s.title - 1); P.s.title[sizeof P.s.title - 1] = 0; }
        frame(rand() % 5 == 0);
    }
    P.s.title[0] = 0; m3_half = 0; m3_rw = M3W; m3_rh = M3H;
    /* 4: out of memory. Big modes need more than the limit: the view must fall back to a smaller mode and keep drawing */
    {
        static const size_t lim[] = { 16u << 20, 8u << 20, 4u << 20, 2u << 20, 600u << 10, 64u << 10 };
        int l;
        for (l = 0; l < 6; l++)
        {
            mp3_leave(); free(m3_fb); free(m3_zb); m3_fb = NULL; m3_zb = NULL; mp3_have = 0;
            hs_oom_over = lim[l];
            {   /* the buffers are freed: mp3_buffers must not think it still has them */
                int ok = enter_mode(1600, 1200, 32);
                printf("OOM limit %zu KB: %s %dx%d\n", lim[l] >> 10, ok ? "fell back to" : "text views,", ok ? M3W : 0, ok ? M3H : 0);
                if (ok) { CHECK((size_t)M3W * M3H * 4 < lim[l], "fb %dx%d over the limit", M3W, M3H); CHECK(l < 2 || M3W < 1600, "no fallback"); sweep(0); }
                else CHECK(lim[l] < 256000, "nothing fits at %zu", lim[l]);
            }
        }
        hs_oom_over = 0;
        CHECK(enter_mode(1600, 1200, 32) && M3W == 1600, "1600x1200 after OOM");
        frame(0);
    }
    mp3_leave();
    printf("%lu frames, %d failures\n", hs_frames, fails);
    return fails != 0;
}
