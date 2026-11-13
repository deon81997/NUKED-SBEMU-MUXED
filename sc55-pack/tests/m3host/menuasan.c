/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* menuasan.c - the whole of tools/sc55menu.c (text menu, menu3d.h, tui.h) on a fake DJGPP under ASan + UBSan.
 * A fake DOS disk (C:\ = /tmp/menufs/C), a fake MPU-401 (SBEMU loaded or not), scripted keys and mouse.
 * Every entry of every page is launched in text and in the three 3D styles, with and without SBEMU (MENUGO.BAT is
 * checked); then random key / mouse runs over every style, font, colour style and display mode, the game editor and
 * its file browser.  sh tests/m3host/menurun.sh [quick]  - any sanitizer report or failed check stops it. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#define HS_HOOKS
#include "dosstub.h"

/* ---- fake disk: C:\X\Y -> /tmp/menufs/C/X/Y (upper case) */
#define FSROOT "/tmp/menufs"
static const char* hs_path(const char* d, char* out)
{
    char* o; int i;
    if (strlen(d) < 2 || d[1] != ':') { snprintf(out, 600, FSROOT "/C/SC55/%s", d); }
    else snprintf(out, 600, FSROOT "/%c/%s", toupper((unsigned char)d[0]), d + 2 + (d[2] == '\\'));
    for (o = out + strlen(FSROOT); *o; o++) { if (*o == '\\') *o = '/'; *o = (char)toupper((unsigned char)*o); }
    i = (int)strlen(out); while (i > (int)strlen(FSROOT) + 3 && out[i - 1] == '/') out[--i] = 0;
    return out;
}
static FILE* hs_fopen(const char* p, const char* m) { char h[600]; return fopen(hs_path(p, h), m); }
static int hs_access(const char* p, int m)
{
    char h[600]; struct stat st;
    if (stat(hs_path(p, h), &st)) return -1;
    if (m == D_OK) return S_ISDIR(st.st_mode) ? 0 : -1;
    return 0;
}
static int hs_remove(const char* p) { char h[600]; return remove(hs_path(p, h)); }
static int hs_rename(const char* a, const char* b) { char h[600], g[600]; hs_path(a, h); hs_path(b, g); return rename(h, g); }
static char* hs_getcwd(char* b, size_t n) { snprintf(b, n, "C:\\SC55"); return b; }
static int hs_putenv(const char* s) { return putenv(strdup(s)); }

static DIR* hs_dir; static char hs_dirpath[600];
int findnext(struct ffblk* f)
{
    struct dirent* e;
    while (hs_dir && (e = readdir(hs_dir)) != NULL)
    {
        char p[1200]; struct stat st;
        if (e->d_name[0] == '.') continue;
        snprintf(p, sizeof p, "%s/%s", hs_dirpath, e->d_name);
        if (stat(p, &st)) continue;
        memset(f, 0, sizeof *f);
        snprintf(f->ff_name, sizeof f->ff_name, "%s", e->d_name);
        f->ff_attrib = S_ISDIR(st.st_mode) ? FA_DIREC : FA_ARCH;
        return 0;
    }
    if (hs_dir) { closedir(hs_dir); hs_dir = NULL; }
    return -1;
}
int findfirst(const char* pat, struct ffblk* f, int a)
{
    char d[600]; char* s; (void)a;
    snprintf(d, sizeof d, "%s", pat); s = strrchr(d, '\\'); if (s) s[1] = 0;
    hs_path(d, hs_dirpath);
    if (hs_dir) closedir(hs_dir);
    hs_dir = opendir(hs_dirpath);
    return findnext(f);
}

#define fopen hs_fopen
#define access hs_access
#define remove hs_remove
#define rename hs_rename
#define getcwd hs_getcwd
#define putenv hs_putenv
#define main menu_main
#include "../../tools/sc55menu.c"
#undef main
#undef fopen
#undef access
#undef remove
#undef rename
#undef getcwd
#undef putenv

/* ---- fake PC */
unsigned char hs_dos[0x110000];
HS_MODE hs_modes[64]; int hs_nmodes, hs_fail_set;
unsigned char* hs_lfb; unsigned long hs_lfb_size; int hs_cur_mode = 3;
static int hs_bank, hs_pitch, hs_sbemu, hs_mouse_on = 1, hs_mx, hs_my, hs_mb, hs_mrel;
static unsigned char hs_mq[512]; static int hs_mqh, hs_mqt;
static void mq(unsigned char b) { hs_mq[hs_mqt++ & 511] = b; }

void hs_check(unsigned long a, unsigned long n, const char* what)
{
    if (a + n > sizeof hs_dos) { fprintf(stderr, "FAIL: %s outside DOS memory: %lx+%lu\n", what, a, n); abort(); }
    if (a >= 0xA0000 && a < 0xB0000 && hs_cur_mode >= 0x100)
    {
        unsigned long lin = (unsigned long)hs_bank * 65536 + (a - 0xA0000);
        if (a + n > 0xB0000 || lin + n > hs_lfb_size) { fprintf(stderr, "FAIL: %s bank %d past the screen\n", what, hs_bank); abort(); }
    }
    if (a >= 0xB8000 && a < 0xC0000 && hs_cur_mode < 0x100)
    {   /* text memory: inside the rows x cols of the mode */
        unsigned long lim = 0xB8000 + (unsigned long)(hs_dos[0x484] + 1) * (hs_dos[0x44A] | hs_dos[0x44B] << 8) * 2;
        if (a + n > lim) { fprintf(stderr, "FAIL: %s past the text screen (%lx+%lu > %lx)\n", what, a, n, lim); abort(); }
    }
}
unsigned long hs_ticks(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (unsigned long)((t.tv_sec * 1000000000LL + t.tv_nsec) / 54925400LL); }
uclock_t uclock(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uclock_t)((t.tv_sec * 1000000000LL + t.tv_nsec) / 838LL); }
void movedata(unsigned ss, unsigned so, unsigned ds, unsigned dofs, size_t n)
{
    if (ss != 2 || ds != 3) { fprintf(stderr, "FAIL: movedata %u -> %u\n", ss, ds); abort(); }
    if (!hs_lfb || dofs + n > hs_lfb_size) { fprintf(stderr, "FAIL: LFB write %u+%zu past the screen (%lu)\n", dofs, n, hs_lfb_size); abort(); }
    memcpy(hs_lfb + dofs, (const void*)(uintptr_t)so, n);
}
unsigned char inportb(unsigned short p)
{
    static unsigned char t;
    if (p == 0x3DA) return (unsigned char)(t ^= 8);
    if (hs_sbemu && p == 0x331) return hs_mqh != hs_mqt ? 0x00 : 0x80;
    if (hs_sbemu && p == 0x330) return hs_mqh != hs_mqt ? hs_mq[hs_mqh++ & 511] : 0xFE;
    return 0xFF;
}
void outportb(unsigned short p, unsigned char v)
{
    if (!hs_sbemu || p != 0x331) return;
    if (v == 0xFF) mq(0xFE);
    if (v == 0xB0) { int i; mq('L'); mq(0); mq(0); mq(0x40); mq(0x02); for (i = 5; i < 150; i++) mq(0); }   /* load 57.6% */
}
static HS_MODE* hs_find(int mode) { int i; for (i = 0; i < hs_nmodes; i++) if (hs_modes[i].mode == (mode & 0x1FF)) return &hs_modes[i]; return NULL; }
static int hs_pitch_of(const HS_MODE* m) { return m->w * ((m->bpp + 7) / 8); }

/* ---- scripted input: keys (>0) and mouse events */
typedef struct { int key, x, y, b; } EV;
static EV ev[20000]; static int nev, pev; static long polls, idle;
#define MOUSE(X, Y) (ev[nev].key = -1, ev[nev].x = (X), ev[nev].y = (Y), ev[nev].b = 1, nev++)
#define MOVE(X, Y) (ev[nev].key = -1, ev[nev].x = (X), ev[nev].y = (Y), ev[nev].b = 0, nev++)
#define KEY(K) (ev[nev].key = (K), nev++)
#define DUMP() KEY(0x7FFF)
static int hs_pend = -1, hs_shot;
static void hang_check(void) { if (++idle > 4000000L) { fprintf(stderr, "FAIL: hang (event %d of %d)\n", pev, nev); abort(); } }
int kbhit(void)
{
    hang_check();
    if (hs_pend >= 0) return 1;
    while (pev < nev && ev[pev].key == 0x7FFE)
    {   /* SHOT: the 3D back buffer as /tmp/m3shot<n>.ppm */
        char fn[64]; FILE* f; int i; pev++;
        snprintf(fn, sizeof fn, "/tmp/m3shot%d.ppm", hs_shot++); f = fopen(fn, "wb");
        if (f && m3_fb) { fprintf(f, "P6 640 480 255\n"); for (i = 0; i < 640 * 480; i++) { putc(m3_fb[i] >> 16 & 255, f); putc(m3_fb[i] >> 8 & 255, f); putc(m3_fb[i] & 255, f); } }
        if (f) fclose(f);
    }
    while (pev < nev && ev[pev].key == 0x7FFF)
    {   /* DUMP: the text screen as it is now */
        int y, x; pev++;
        for (y = 0; y < TUI_H; y++) { for (x = 0; x < TUI_W; x++) { int c = tui_scr[y][x] & 255; putchar(c >= 32 && c < 127 ? c : c ? '#' : ' '); } putchar('\n'); }
    }
    if (pev >= nev) return (++polls % 3) == 0;                         /* script done: Esc until the menu ends */
    if (ev[pev].key < 0) { static long w; if (++w > 2000) { w = 0; pev++; } return 0; }   /* a screen that reads no mouse */
    return (++polls % 3) == 0;
}
int getch(void)
{
    int k;
    if (hs_pend >= 0) { k = hs_pend; hs_pend = -1; return k; }
    k = pev < nev && ev[pev].key > 0 ? ev[pev++].key : 27;
    idle = 0;
    if (k >= 0x100) { hs_pend = k & 0xFF; return 0; }
    return k;
}
int __dpmi_int(int v, __dpmi_regs* r)
{
    unsigned long es = (unsigned long)r->x.es * 16 + r->x.di;
    if (v == 0x33)
    {
        if (!hs_mouse_on) return 0;
        if (r->x.ax == 0) r->x.ax = 0xFFFF;
        else if (r->x.ax == 3)
        {
            if (hs_mrel) { hs_mb = 0; hs_mrel = 0; }
            else if (pev < nev && ev[pev].key < 0) { hs_mx = ev[pev].x; hs_my = ev[pev].y; hs_mb = ev[pev].b; hs_mrel = hs_mb; pev++; idle = 0; }
            r->x.bx = (unsigned short)hs_mb; r->x.cx = (unsigned short)hs_mx; r->x.dx = (unsigned short)hs_my;
        }
        else if (r->x.ax == 4) { hs_mx = r->x.cx; hs_my = r->x.dx; }
        return 0;
    }
    if (v != 0x10) return 0;
    if (r->h.ah == 0x00)
    {
        hs_cur_mode = r->h.al & 0x7F; hs_dos[0x449] = (unsigned char)hs_cur_mode; hs_dos[0x484] = 24; hs_dos[0x44A] = 80;
        free(hs_lfb); hs_lfb = NULL; hs_lfb_size = 0;
        return 0;
    }
    if (r->x.ax == 0x1111) { hs_dos[0x484] = 27; return 0; }
    if (r->x.ax == 0x1112) { hs_dos[0x484] = 49; return 0; }
    switch (r->x.ax)
    {
    case 0x4F00:
    {
        unsigned char* vi = hs_dos + es; int i;
        hs_check(es, 512, "4F00");
        memset(vi, 0, 512); memcpy(vi, "VESA", 4); vi[4] = 0; vi[5] = 3;
        vi[0x0E] = 0; vi[0x0F] = 0; vi[0x10] = 0x00; vi[0x11] = 0x80;
        for (i = 0; i < hs_nmodes; i++) { hs_dos[0x80000 + i * 2] = hs_modes[i].mode & 255; hs_dos[0x80001 + i * 2] = (unsigned char)(hs_modes[i].mode >> 8); }
        hs_dos[0x80000 + i * 2] = 0xFF; hs_dos[0x80001 + i * 2] = 0xFF;
        r->x.ax = 0x004F; return 0;
    }
    case 0x4F01:
    {
        HS_MODE* m = hs_find(r->x.cx); unsigned char* mi = hs_dos + es; int p;
        if (!m) { r->x.ax = 0x014F; return 0; }
        hs_check(es, 256, "4F01");
        memset(mi, 0, 256); p = hs_pitch_of(m);
        mi[0] = m->bpp ? 0x9B : 0x0B; mi[2] = 7; mi[4] = 64; mi[6] = 64; mi[8] = 0x00; mi[9] = 0xA0;
        mi[0x10] = p & 255; mi[0x11] = (unsigned char)(p >> 8);
        mi[0x12] = m->w & 255; mi[0x13] = (unsigned char)(m->w >> 8); mi[0x14] = m->h & 255; mi[0x15] = (unsigned char)(m->h >> 8);
        mi[0x19] = (unsigned char)m->bpp; mi[0x1B] = m->bpp == 8 ? 4 : 6;
        if (m->bpp == 15) { mi[0x1F] = 5; mi[0x20] = 10; mi[0x21] = 5; mi[0x22] = 5; mi[0x23] = 5; }
        else if (m->bpp == 16) { mi[0x1F] = 5; mi[0x20] = 11; mi[0x21] = 6; mi[0x22] = 5; mi[0x23] = 5; }
        else if (m->bpp >= 24) { mi[0x1F] = 8; mi[0x20] = 16; mi[0x21] = 8; mi[0x22] = 8; mi[0x23] = 8; }
        mi[0x2B] = 0xE0; mi[0x32] = p & 255; mi[0x33] = (unsigned char)(p >> 8);
        r->x.ax = 0x004F; return 0;
    }
    case 0x4F02:
    {
        HS_MODE* m = hs_find(r->x.bx);
        if (!m || hs_fail_set) { r->x.ax = 0x014F; return 0; }
        hs_cur_mode = m->mode; hs_bank = 0;
        free(hs_lfb); hs_lfb = NULL; hs_lfb_size = 0;
        if (!m->bpp) { hs_cur_mode = 3; hs_dos[0x44A] = (unsigned char)m->w; hs_dos[0x484] = (unsigned char)(m->h - 1); r->x.ax = 0x004F; return 0; }
        hs_pitch = hs_pitch_of(m); hs_lfb_size = (unsigned long)hs_pitch * m->h; hs_lfb = (unsigned char*)malloc(hs_lfb_size);
        r->x.ax = 0x004F; return 0;
    }
    case 0x4F05: hs_bank = r->x.dx; r->x.ax = 0x004F; return 0;
    case 0x4F09: case 0x4F0B: r->x.ax = 0x004F; return 0;
    default: return 0;
    }
}

/* ---- the test */
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)
static void set_modes(int vesa)
{
    static const int g[][3] = { {640,480,32}, {640,480,16}, {640,480,8}, {800,600,8}, {1024,768,8}, {1280,1024,8}, {640,400,8},
                                {800,600,16}, {132,43,0}, {132,60,0}, {80,60,0} };
    int i; hs_nmodes = 0;
    if (!vesa) return;
    for (i = 0; i < (int)(sizeof g / sizeof g[0]); i++)
    { hs_modes[hs_nmodes].w = g[i][0]; hs_modes[hs_nmodes].h = g[i][1]; hs_modes[hs_nmodes].bpp = g[i][2]; hs_modes[hs_nmodes].mode = 0x101 + i; hs_nmodes++; }
}
static void put(const char* dos, const char* text)
{
    char h[600], d[600]; FILE* f; char* s;
    hs_path(dos, h); snprintf(d, sizeof d, "%s", h); s = strrchr(d, '/'); *s = 0;
    { char cmd[700]; snprintf(cmd, sizeof cmd, "mkdir -p '%s'", d); if (system(cmd)) abort(); }
    f = fopen(h, "wb"); if (!f) abort(); fputs(text, f); fclose(f);
}
static char* get(const char* dos)
{
    static char b[8192]; char h[600]; FILE* f; size_t n;
    b[0] = 0; f = fopen(hs_path(dos, h), "rb"); if (!f) return NULL;
    n = fread(b, 1, sizeof b - 1, f); b[n] = 0; fclose(f); return b;
}
static void make_disk(int games_cfg)
{
    char big[6000]; int i;
    if (system("rm -rf " FSROOT " && mkdir -p " FSROOT "/C/SC55 " FSROOT "/D " FSROOT "/E/ISO/SUB")) abort();
    put("C:\\SC55\\SBOPTS.TXT", "/A:220 /I:7 /D:1 /P:330 /VSCSET:mk1-v2.00 /VSF:C:\\SF2\\GENERALUSER.SF2\r\n");
    put("C:\\SC55\\SC55.BAT", "@ECHO OFF\r\n"); put("C:\\SC55\\SC55JEM.BAT", "x"); put("C:\\SC55\\SC55QMM.BAT", "x");
    put("C:\\SC55\\CDROM.BAT", "x"); put("C:\\CDSET\\GUI.BAT", "x"); put("C:\\CDSET\\CDGUI.CFG", "HOME=E:\\ISO\r\n");
    put("C:\\SC55\\SBLOG.TXT", "SBEMU\r\nError: no soundcard found\r\n");
    put("C:\\SC55\\GAMETEST.TXT", "Doom /K:22050 clock 99.6% ok\r\nDuke clock 96.0%\r\nDoom2 LOCKED\r\nx clock 80%\r\n");
    put("C:\\SC55\\GTBEST.CFG", "BEST0=1003|/VSCAHEAD:80 /K:22050\r\n");
    for (i = 0, big[0] = 0; i < 40; i++) strcat(big, "A very long README line that goes on and on past the eightieth column of the viewer, really.\r\n");
    put("C:\\SC55\\README.TXT", big);
    put("C:\\DUKE3D\\DUKE3D.EXE", "MZ"); put("C:\\DOOM\\DOOM.EXE", "MZ"); put("C:\\GAMES\\X\\RUN.BAT", "x");
    put("C:\\CDIMAGES\\GAMES\\DUKE3D.CUE", "x"); put("E:\\ISO\\SUB\\QUAKE.ISO", "x"); put("E:\\ISO\\TOMB.CUE", "x");
    for (i = 0; i < 30; i++) { char n[60]; snprintf(n, sizeof n, "C:\\MANY\\GAME%02d.EXE", i); put(n, "MZ"); }
    if (games_cfg)
        put("C:\\SC55\\GAMES.CFG", "; list\r\nDuke Nukem 3D|C:\\DUKE3D\\DUKE3D.EXE|-nosetup|/VSCVOICES:16|GAMES\\DUKE3D.CUE\r\n"
            "Doom|C:\\DOOM\\DOOM.EXE|||\r\nGone|C:\\NOPE\\NOPE.EXE|||\r\nBatch|C:\\GAMES\\X\\RUN.BAT||/K:22050|E:\\ISO\\TOMB.CUE\r\n"
            "Root|D:\\GO.EXE|||\r\n");
}
static int run(const char* what)
{   /* the menu in a child process: a crash (sanitizer abort) is a failure of this run */
    pid_t p; int st;
    fflush(stdout);
    p = fork();
    if (!p)
    {
        static char a0[] = "C:\\SC55\\SC55MENU.EXE"; char* av[] = { a0, NULL };
        int r = menu_main(1, av);
        fflush(stdout); _exit(r == 0 ? 0 : 3);
    }
    waitpid(p, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st)) { printf("FAIL: %s: menu crashed (status %x)\n", what, st); fails++; return 0; }
    return 1;
}
static void style_cfg(int s) { char b[40]; if (s < 0) hs_remove("C:\\SC55\\MENU3D.CFG"); else { snprintf(b, sizeof b, "STYLE=%d\nQUALITY=%d\n", s, s == 2 ? 2 : 0); put("C:\\SC55\\MENU3D.CFG", b); } }

/* ---- 1: every entry, launched with Enter (and F2 on games) in text and the 3D styles */
static void every_entry(int sbemu, int style)
{
    int i;
    hs_sbemu = sbemu; g_running = sbemu;
    snprintf(g_home, sizeof g_home, "C:\\SC55");
    read_sbopts(); build_menu();
    for (i = 0; i < g_n; i++)
    {
        ITEM it = g_it[i]; int c, k[80], n, pos = 0, j, f2;
        n = cat_items(it.cat, k); for (j = 0; j < n; j++) if (k[j] == i) pos = j;
        for (f2 = 0; f2 <= (it.kind == IT_GAME); f2++)
        {
            char* go; char what[120];
            snprintf(what, sizeof what, "%s / %s, SBEMU %d, style %d%s", g_catname[it.cat], it.label, sbemu, style, f2 ? " F2" : "");
            hs_remove("C:\\SC55\\MENUGO.BAT"); hs_remove("C:\\SC55\\MENU.CFG"); style_cfg(style);
            put("C:\\SC55\\TUNED.FLG", "x");
            nev = pev = 0; polls = idle = 0;
            KEY(0x147);                                       /* Home: first item of Sound */
            for (c = 0; c < NCAT; c++) if (c == it.cat) break; else { int kk[80]; if (cat_items(c, kk)) KEY(0x14D); }
            for (j = 0; j < pos; j++) KEY(0x150);
            KEY(f2 ? 0x13C : 13);
            if (it.kind == IT_VIEW) { KEY(0x150); KEY(0x151); KEY(0x14D); KEY(0x14F); KEY(27); }
            if (it.kind == IT_EDIT) { KEY(27); }
            KEY(27);
            if (!run(what)) continue;
            go = get("C:\\SC55\\MENUGO.BAT");
            if (it.kind == IT_VIEW || it.kind == IT_EDIT || it.kind == IT_M3D || !usable(&it))
            {
                if (it.kind == IT_M3D && style < 0) continue;      /* text: Enter on "3D menu" opens it, Esc there leaves */
                CHECK(!go, "%s: wrote MENUGO.BAT (should not)", what);
                continue;
            }
            CHECK(go != NULL, "%s: no MENUGO.BAT", what);
            if (!go) continue;
            CHECK(strstr(go, "@ECHO OFF\r\nSET MENUBACK=1\r\n") == go, "%s: header", what);
            CHECK(strstr(go, "C:\r\nCD \\SC55\r\n") != NULL, "%s: no way back home:\n%s", what, go);
            if (it.kind == IT_GAME)
            {
                const GAMEP* g = &g_gp[it.gp];
                if (g->cd[0]) CHECK(strstr(go, "UCDD.EXE -mount") && strstr(go, ":GOEND"), "%s: CD mount", what);
                if ((g->opts[0] || (it.best[0] && !f2)) && sbemu) CHECK(strstr(go, "/VSCAHEAD:50") != NULL, "%s: no restore", what);
                if (f2 && it.best[0]) CHECK(!strstr(go, it.best), "%s: F2 used the tested best", what);
            }
            if (it.pause) CHECK(strstr(go, "PAUSE") != NULL, "%s: no PAUSE", what);
            for (j = 0; go[j]; j++) CHECK(go[j] != '|', "%s: '|' left in MENUGO.BAT", what);
        }
    }
}

/* ---- 0: every description fits where it is shown (text: 5 lines of 74, 3D Panels: 5 of 74, Carousel/Module: 4 of 74) */
static int lines_tui(const char* s, int w)
{   /* tui_wrap's breaking */
    int line = 0;
    while (*s) { int n = (int)strlen(s), cut = n; if (n > w) { cut = w; while (cut > w / 2 && s[cut] != ' ') --cut; if (s[cut] != ' ') cut = w; }
                 s += cut; while (*s == ' ') ++s; ++line; }
    return line;
}
static int lines_m3(const char* t, int cols)
{   /* m3_wrap's breaking */
    int n = 0;
    while (*t) { int len, cut; while (*t == ' ') t++; len = (int)strlen(t); if (!len) break;
                 if (len > cols) { cut = cols; while (cut > 0 && t[cut] != ' ') cut--; if (cut == 0) cut = cols; } else cut = len;
                 t += cut; n++; }
    return n;
}
static void fits(void)
{
    int sb, i;
    for (sb = 0; sb <= 1; sb++)
    {
        hs_sbemu = sb; g_running = sb; snprintf(g_home, sizeof g_home, "C:\\SC55"); read_sbopts(); build_menu();
        for (i = 0; i < g_n; i++)
        {
            const ITEM* it = &g_it[i]; int t = lines_tui(it->desc, 74), p = lines_m3(it->desc, (M3W - 48) / 8), o = lines_m3(it->desc, (M3W - 44) / 8);
            CHECK(t <= 5, "text: '%s' needs %d lines (5)", it->label, t);
            CHECK(p <= 5, "3D Panels: '%s' needs %d lines (5)", it->label, p);
            CHECK(o <= 4, "3D Carousel/Module: '%s' needs %d lines (4)", it->label, o);
            CHECK((int)strlen(it->label) <= 38, "label '%s' too long", it->label);
        }
    }
}

/* ---- 2: random keys and clicks */
static const int keyset[] = { 0x148, 0x150, 0x14B, 0x14D, 9, 0x10F, 0x149, 0x151, 0x147, 0x14F, 0x13B, 0x13D, 0x13E, 0x140, 0x141,
    0x142, 0x13F, 0x13C, '1', '3', '9', 'd', 'S', 'p', 'z', 'G', 13, 13, 8, 'y', 'n', 'Y', 0x153 };
static void fuzz(int seed, int nkeys, int style, int sbemu)
{
    int i; char what[80];
    srand((unsigned)seed);
    hs_sbemu = sbemu; style_cfg(style); put("C:\\SC55\\TUNED.FLG", "x");
    nev = pev = 0; polls = idle = 0;
    for (i = 0; i < nkeys && nev < 19000; i++)
    {
        int r = rand() % 100;
        if (r < 12) { if (style >= 0 && rand() % 2) MOUSE(rand() % 640, rand() % 480); else MOUSE(rand() % 640, rand() % 200); }
        else if (r < 15) MOVE(rand() % 640, rand() % 480);
        else { int k = keyset[rand() % (int)(sizeof keyset / sizeof keyset[0])];
               if (k == 13 && rand() % 4) k = 0x150;          /* Enter leaves the menu (MENUGO): not too often */
               KEY(k); }
    }
    snprintf(what, sizeof what, "fuzz seed %d style %d SBEMU %d", seed, style, sbemu);
    run(what);
}

/* ---- 3: game editor + file browser: add one from C:\MANY, rename it, CD image from E:\ISO\SUB, remove another */
static void editor(void)
{
    char* cfg; int i;
    hs_sbemu = 1; style_cfg(-1); hs_remove("C:\\SC55\\MENU.CFG"); put("C:\\SC55\\TUNED.FLG", "x");
    nev = pev = 0; polls = idle = 0;
    KEY(0x147); KEY(0x14D); KEY(0x14D); KEY(0x14D); KEY(0x14F); KEY(13);          /* Games > Add / edit games */
    for (i = 0; i < 5; i++) KEY(0x150);                                            /* "+ new game" */
    KEY(13);                                                                       /* browser at C:\ */
    KEY(0x14F); KEY(0x148); KEY(0x147);
    { const char* m = "MANY"; (void)m; }
    for (i = 0; i < 5; i++) KEY(0x150);                                            /* walk down to MANY (sorted dirs) */
    KEY(13); KEY(0x151); KEY(0x151); KEY(0x149); KEY(13);                          /* a program in it */
    KEY(8); KEY(8); KEY('N'); KEY('e'); KEY('w'); KEY(13);                         /* name */
    KEY(0x150); KEY(0x150); KEY(0x150); KEY(0x150); KEY(13);                       /* CD image field: browser */
    KEY(9); KEY(9); KEY(9); KEY('g'); KEY(0x150); KEY(13); KEY(0x14F); KEY(13);    /* drives, home E:\ISO, SUB, last */
    KEY(27); KEY(0x148); KEY(0x148); KEY(0x153); KEY('y');                  /* remove the game above */
    KEY(27); KEY(27);
    run("game editor");
    cfg = get("C:\\SC55\\GAMES.CFG");
    CHECK(cfg && strstr(cfg, "C:\\MANY\\"), "editor: new game not saved:\n%s", cfg ? cfg : "(none)");
    if (cfg) printf("    GAMES.CFG after the editor:\n%s", cfg);
}

int main(int argc, char** argv)
{
    int quick = argc > 1 && !strcmp(argv[1], "quick"), s, sb, seed;
    hs_dos[0x449] = 3; hs_dos[0x44A] = 80; hs_dos[0x484] = 24;
    setvbuf(stdout, NULL, _IONBF, 0);
    set_modes(1);
    make_disk(1);
    if (argc > 1 && !strcmp(argv[1], "ed")) { editor(); return 0; }
    printf("0: descriptions fit\n"); fits();
    if (argc > 1 && !strcmp(argv[1], "fit")) return fails != 0;
    if (argc > 1 && !strcmp(argv[1], "shots"))
    {   /* each 3D style on Settings > Automatic setup with SBEMU loaded (grey: shows why) and on Games > Play Duke */
        system("rm -f /tmp/m3shot*.ppm");
        make_disk(1); hs_sbemu = 1;
        for (s = 0; s < 3; s++)
        {
            int i; style_cfg(s); put("C:\\SC55\\TUNED.FLG", "x"); hs_remove("C:\\SC55\\MENU.CFG"); nev = pev = 0;
            KEY(0x147); KEY(0x14D); KEY(0x150); for (i = 0; i < 30; i++) KEY(0x7000); KEY(0x7FFE);
            KEY(0x14D); KEY(0x14D); KEY(0x147); for (i = 0; i < 30; i++) KEY(0x7000); KEY(0x7FFE); KEY(27);
            run("shots"); hs_shot += 2;
        }
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "seed")) { seed = atoi(argv[2]); make_disk(1); fuzz(seed, 1500, (seed % 4) - 1, seed % 3 != 0); return fails != 0; }
    printf("1: every entry (text, Panels, Carousel, Module) x SBEMU off/on\n");
    for (sb = 0; sb <= 1; sb++) for (s = -1; s < 3; s++) { every_entry(sb, s); if (quick && s >= 0) break; }
    printf("   done, %d failures\n", fails);
    printf("2: game editor\n"); editor();
    {   /* 20 games in GAMES.CFG: the editor scrolls to the last ones, adds up to 24, keeps every line */
        char big[4000] = "", *cfg; int i, n = 0;
        make_disk(1);
        for (i = 0; i < 20; i++) { char l[80]; snprintf(l, sizeof l, "G%02d|C:\\MANY\\GAME%02d.EXE|||\r\n", i, i); strcat(big, l); }
        put("C:\\SC55\\GAMES.CFG", big); style_cfg(-1); hs_remove("C:\\SC55\\MENU.CFG");
        nev = pev = 0;
        KEY(0x147); KEY(0x14D); KEY(0x14D); KEY(0x14D); KEY(0x14F); KEY(13);
        for (i = 0; i < 19; i++) KEY(0x150);
        KEY(13); KEY(13); KEY('X'); KEY(13); KEY(27);                                  /* G19 renamed G19X */
        for (i = 0; i < 6; i++) { KEY(0x150); KEY(13); KEY('t'); KEY('C'); KEY(':'); KEY('\\'); KEY('A'); KEY('.'); KEY('E'); KEY('X'); KEY('E'); KEY(13); KEY(13); KEY(27); }
        KEY(27); KEY(27);
        run("editor, 20+ games");
        cfg = get("C:\\SC55\\GAMES.CFG");
        for (i = 0; cfg && cfg[i]; i++) n += cfg[i] == '\n';
        CHECK(cfg && strstr(cfg, "G19X|") && n == 25, "editor 20+: %d lines\n%s", n, cfg ? cfg : "");
    }
    printf("3: first start (no SBOPTS.TXT), MENUVIEW, safe mode, no VESA, no mouse\n");
    make_disk(0); hs_remove("C:\\SC55\\SBOPTS.TXT"); hs_remove("C:\\SC55\\TUNED.FLG"); hs_sbemu = 0;
    nev = pev = 0; KEY('n'); KEY(27); run("first start, no");
    CHECK(get("C:\\SC55\\TUNED.FLG") != NULL, "TUNED.FLG not written");
    hs_remove("C:\\SC55\\TUNED.FLG"); nev = pev = 0; KEY('y'); run("first start, yes");
    CHECK(get("C:\\SC55\\MENUGO.BAT") && strstr(get("C:\\SC55\\MENUGO.BAT"), "/TUNE"), "first start: no /TUNE");
    put("C:\\SC55\\TUNED.FLG", "x");
    setenv("MENUVIEW", "SBLOG.TXT", 1); style_cfg(1); nev = pev = 0; KEY(0x150); KEY(27); KEY(27); run("MENUVIEW"); unsetenv("MENUVIEW");
    hs_dos[0x417] = 1; nev = pev = 0; KEY('y'); KEY(27); run("safe mode"); hs_dos[0x417] = 0;
    set_modes(0); style_cfg(1); nev = pev = 0; KEY(0x142); KEY(13); KEY(27); run("no VESA"); set_modes(1);
    hs_mouse_on = 0; style_cfg(0); nev = pev = 0; KEY(0x150); KEY(0x142); KEY(0x13E); KEY(27); run("no mouse"); hs_mouse_on = 1;
    printf("4: display modes (F4) and fonts (F7), every mode kept\n");
    make_disk(1);
    for (s = 0; s < 7 + hs_nmodes; s++)
    {
        int j; char w[40];
        style_cfg(-1); nev = pev = 0;
        KEY(0x13E); KEY(0x147); for (j = 0; j < s; j++) KEY(0x150); KEY(13); KEY('y');
        KEY(0x141); KEY(0x150); KEY(0x141); KEY(0x13D); KEY(0x13B); KEY(27); KEY(0x141); KEY(0x14D); KEY(27);
        snprintf(w, sizeof w, "display mode %d", s); run(w);
        nev = pev = 0; KEY(0x150); KEY(0x142); KEY(0x13D); KEY(0x142); KEY(27); run("saved mode, 3D and back");
    }
    hs_remove("C:\\SC55\\THEME.CFG");
    printf("5: random keys and clicks\n");
    for (seed = 1; seed <= (quick ? 8 : 60); seed++) fuzz(seed, quick ? 300 : 1500, (seed % 4) - 1, seed % 3 != 0);
    printf("%s: %d failures\n", fails ? "FAILED" : "PASS", fails);
    return fails ? 1 : 0;
}
