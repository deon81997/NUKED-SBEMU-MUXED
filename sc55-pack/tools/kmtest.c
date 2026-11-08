/* KMTEST - keyboard and mouse test: what DOS programs see from the keyboard (BIOS INT 16h, with the
 * scan code) and from the mouse driver (INT 33h: position, buttons, wheel). With SBEMU's USB keyboard /
 * mouse support (/JOY:1 /USBKB:1 /USBMS:1) the USB keyboard and mouse show here like PS/2 ones.
 *   KMTEST              live view; Esc twice quits
 *   KMTEST /LOG:n FILE  writes every key and mouse change to FILE, quits after n seconds */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include "toolui.h"

static unsigned long ticks(void) { return _farpeekl(_dos_ds, 0x46C); }

/* ------------------------------------------------------------------ the screen */
#define NHIST 12
static struct { unsigned sc, ch, sh; } g_hist[NHIST];
static int g_nh, g_have, g_btns, g_mx = -1, g_my, g_mb, g_wheel, g_esc;

static const char* key_name(unsigned sc)
{
    static const char* n[0x59] = { "", "Esc", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=", "Backspace", "Tab",
        "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "[", "]", "Enter", "Ctrl", "A", "S", "D", "F", "G", "H", "J", "K", "L",
        ";", "'", "`", "L-Shift", "\\", "Z", "X", "C", "V", "B", "N", "M", ",", ".", "/", "R-Shift", "Keypad *", "Alt", "Space",
        "Caps Lock", "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "Num Lock", "Scroll Lock", "Home", "Up",
        "PgUp", "Keypad -", "Left", "Keypad 5", "Right", "Keypad +", "End", "Down", "PgDn", "Ins", "Del", "", "", "", "F11", "F12" };
    if (sc < 0x59 && n[sc][0]) return n[sc];
    switch (sc)
    {
    case 0x85: return "F11"; case 0x86: return "F12"; case 0x8D: return "Ctrl+Up"; case 0x91: return "Ctrl+Down";
    case 0xE0: return "Keypad Enter"; case 0x47: return "Home";
    }
    return "(extended)";
}
static void draw(void)
{
    static const char* sh[8] = { "R-Shift", "L-Shift", "Ctrl", "Alt", "Scroll", "Num", "Caps", "Ins" };
    unsigned f = _farpeekb(_dos_ds, 0x417);
    int i, x;
    tu_head("KMTEST", "keyboard (INT 16h) and mouse (INT 33h), as programs see them", NULL);
    tui_panel(2, 3, 44, 20, 1, 0x1E, "Keyboard");
    if (g_nh)
    {
        tui_put(4, 5, 0x17, "Last key");
        tui_printf(4, 6, 0x1E, "%-20s", key_name(g_hist[0].sc));
        tui_printf(4, 7, 0x1B, "scan %02X   char %02X  '%c'", g_hist[0].sc, g_hist[0].ch, g_hist[0].ch >= 32 && g_hist[0].ch < 127 ? g_hist[0].ch : ' ');
    }
    else tui_put(4, 5, 0x18, "Press any key ...");
    tui_put(4, 9, 0x17, "Before that");
    for (i = 1; i < NHIST - 2 && i < g_nh; ++i)
        tui_printf(4, 9 + i, 0x13, "%02X %02X  %-16s", g_hist[i].sc, g_hist[i].ch, key_name(g_hist[i].sc));
    tui_put(4, 18, 0x17, "Shift state (0040:0017)");
    for (i = 0, x = 4; i < 8; ++i) { if (i == 4) x = 4; x += 1 + tu_lamp(x, i < 4 ? 19 : 20, (f >> i) & 1, sh[i]); }
    tui_panel(47, 3, 32, 20, 1, 0x1E, "Mouse");
    if (!g_have)
    {
        tu_state(49, 5, -1, "no mouse driver");
        tui_wrap(49, 7, 28, 8, 0x17, "Load CTMOUSE or MOUSE.COM (after SBEMU for a USB mouse), then run KMTEST again.");
    }
    else
    {
        tu_state(49, 5, 1, "driver found");
        tui_printf(64, 5, 0x17, "%d buttons", g_btns);
        tu_plot(49, 7, 28, 8, g_mx < 0 ? 0.5f : g_mx / 639.0f, g_my / 199.0f, 0x1E);
        tui_printf(49, 16, 0x1F, "x %3d  y %3d   wheel %d", g_mx < 0 ? 320 : g_mx, g_my, g_wheel);
        tu_lamp(49, 18, g_mb & 1, "Left");
        tu_lamp(56, 18, g_mb & 4, "Middle");
        tu_lamp(65, 18, g_mb & 2, "Right");
    }
    tu_foot(g_esc ? "Esc once more quits." : "Every key goes to the test (Esc too): Esc twice quits.", "Esc Esc quit");
}

int main(int argc, char** argv)
{
    __dpmi_regs r;
    FILE* log = NULL;
    int secs = 0, i, have, esc = 0, lx = -1, ly = -1, lb = -1, wheel = 0;
    unsigned long t0;
    for (i = 1; i < argc; ++i)
    {
        if (!strncasecmp(argv[i], "/LOG:", 5)) { secs = atoi(argv[i] + 5); if (i + 1 < argc) log = fopen(argv[++i], "w"); }
        else { printf("KMTEST [/LOG:seconds FILE]\n"); return 1; }
    }
    memset(&r, 0, sizeof(r));
    r.x.ax = 0;                                                     /* mouse reset */
    __dpmi_int(0x33, &r);
    have = r.x.ax == 0xFFFF;
    g_have = have; g_btns = have ? r.x.bx : 0;
    if (!log) tu_start(argv[0]);
    if (log) { fprintf(log, "mouse driver %s buttons %d\n", have ? "found" : "none", have ? r.x.bx : 0); fflush(log); }
    if (have)
    {
        memset(&r, 0, sizeof(r)); r.x.ax = 7; r.x.cx = 0; r.x.dx = 639; __dpmi_int(0x33, &r);
        memset(&r, 0, sizeof(r)); r.x.ax = 8; r.x.cx = 0; r.x.dx = 199; __dpmi_int(0x33, &r);
        memset(&r, 0, sizeof(r)); r.x.ax = 4; r.x.cx = 320; r.x.dx = 100; __dpmi_int(0x33, &r);
    }
    t0 = ticks();
    for (;;)
    {
        memset(&r, 0, sizeof(r));
        r.h.ah = 0x11;                                              /* key waiting? */
        __dpmi_int(0x16, &r);
        if (!(r.x.flags & 0x40))
        {
            unsigned sc, ch;
            memset(&r, 0, sizeof(r)); r.h.ah = 0x10; __dpmi_int(0x16, &r);
            sc = r.h.ah; ch = r.h.al;
            memmove(g_hist + 1, g_hist, sizeof g_hist[0] * (NHIST - 1));
            g_hist[0].sc = sc; g_hist[0].ch = ch; if (g_nh < NHIST) ++g_nh;
            if (log) { fprintf(log, "key %02X %02X\n", sc, ch); fflush(log); }
            if (sc == 1) { if (++esc == 2) break; } else esc = 0;
            g_esc = esc;
        }
        if (have)
        {
            memset(&r, 0, sizeof(r)); r.x.ax = 3; __dpmi_int(0x33, &r);
            wheel += (signed char)r.h.bh;
            if (r.x.cx != lx || r.x.dx != ly || (r.x.bx & 7) != lb || r.h.bh)
            {
                lx = r.x.cx; ly = r.x.dx; lb = r.x.bx & 7;
                g_mx = lx; g_my = ly; g_mb = lb; g_wheel = wheel;
                if (log) { fprintf(log, "mouse %d %d %d %d\n", lx, ly, lb, wheel); fflush(log); }
            }
        }
        if (secs && ticks() - t0 > (unsigned long)secs * 182 / 10) break;
        if (!log) { draw(); tui_flush(); if (tui_fade_pending) { tui_fade_pending = 0; tui_fade(1); } }
        __dpmi_yield();
    }
    tu_end();
    if (log) fclose(log);
    return 0;
}
