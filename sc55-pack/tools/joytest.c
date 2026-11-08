/* JOYTEST - shows what a DOS game sees on the gameport (port 201h).
 *   JOYTEST              live view: four axes in microseconds, as bars and as a dot per stick, four buttons;
 *                        G switches to the GrIP view; Esc or Q quits
 *   JOYTEST /LOG:n FILE  n readings, 0.2 s apart, written to FILE (and keys typed meanwhile), then exits
 * Works with a real gameport as well as with SBEMU /JOY (USB Xbox 360 pad). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pc.h>
#include <conio.h>
#include <dos.h>
#include <go32.h>
#include <sys/farptr.h>
#include "toolui.h"

static unsigned long long cps;

static inline unsigned long long tsc(void) { unsigned lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return ((unsigned long long)hi << 32) | lo; }
static unsigned long ticks(void) { return _farpeekl(_dos_ds, 0x46C); }

static void calibrate(void)
{
    unsigned long t = ticks();
    unsigned long long a, b;
    while (ticks() == t) {}
    a = tsc(); t = ticks();
    while (ticks() - t < 4) {}
    b = tsc();
    cps = (b - a) * 18206 / 4000;   /* 4 ticks = 4/18.2065 s */
    if (!cps) cps = 100000000ULL;
}

/* us[i] = axis time in microseconds, -1 = timed out (nothing connected); returns the raw button bits (1 = pressed) */
static int read_port(long us[4])
{
    unsigned long long t0, t, lim = cps * 4 / 1000;  /* 4 ms */
    int i, left = 0x0F, b;
    for (i = 0; i < 4; ++i) us[i] = -1;
    disable();
    outportb(0x201, 0xFF);
    t0 = tsc();
    do
    {
        int v = inportb(0x201);
        t = tsc() - t0;
        for (i = 0; i < 4; ++i)
            if ((left & (1 << i)) && !(v & (1 << i))) { us[i] = (long)(t * 1000000ULL / cps); left &= ~(1 << i); }
    } while (left && t < lim);
    b = (~inportb(0x201) >> 4) & 0x0F;
    enable();
    return b;
}

/* Gravis GamePad Pro digital (GrIP) packet on button lines shift / shift+1, as Linux grip.c reads it:
 * a bit at each falling clock edge, 24 bits, rotated until the sync pattern fits. -1 = none */
static long grip_read(int shift)
{
    unsigned long d = 0;
    int i = 0, t = 2000, u, v;
    disable();
    v = inportb(0x201) >> shift;
    do
    {
        --t;
        u = v; v = (inportb(0x201) >> shift) & 3;
        if (~v & u & 1) { d |= (unsigned long)(v >> 1) << i++; t = 2000; }
    } while (i < 24 && t > 0);
    enable();
    if (i < 24) return -1;
    for (i = 0; i < 24 && ((d & 0xFE4210UL) ^ 0x7C0000UL); i++) d = (d >> 1) | (d & 1) << 23;
    return i == 24 ? -1 : (long)d;
}

static void grip_text(char* s, long d)
{
    static const char* nm[] = { "Start", "Select", "R2", "Y", "", "L2", "A", "B", "X", "", "L1", "R1", "Up", "Down", "", "Right", "Left" };
    int j, n = 0;
    if (d < 0) { strcpy(s, "no GrIP pad"); return; }
    for (j = 0; j < 17; ++j) if (nm[j][0] && (d >> j) & 1) n += sprintf(s + n, "%s ", nm[j]);
    if (!n) strcpy(s, "-");
}

/* ------------------------------------------------------------------ the screens */
static void draw_stick(int x, int y, const char* title, const long* us, int b, int b1)
{
    int k, there = us[0] >= 0 || us[1] >= 0;
    static const char* ax[2] = { "X", "Y" };
    tui_panel(x, y, 38, 10, 1, 0x1E, title);
    tu_plot(x + 2, y + 1, 15, 7, there ? (us[0] < 0 ? 0.5f : us[0] / 1124.0f) : -1, us[1] < 0 ? 0.5f : us[1] / 1124.0f, 0x1E);
    for (k = 0; k < 2; ++k)
    {
        int yy = y + 2 + k * 2;
        if (us[k] < 0) { tui_printf(x + 19, yy, 0x17, "%s  ----", ax[k]); tui_bar(x + 19, yy + 1, 16, 0, 1, 0x1B, 0x18); }
        else { tui_printf(x + 19, yy, 0x1F, "%s %5ld us", ax[k], us[k]); tui_bar(x + 19, yy + 1, 16, us[k], 1124, 0x1B, 0x18); }
    }
    tu_lamp(x + 19, y + 7, b & (1 << b1), b1 ? "Fire 3" : "Fire 1");
    tu_lamp(x + 28, y + 7, b & (2 << b1), b1 ? "Fire 4" : "Fire 2");
    if (!there) tui_put(x + 19, y + 8, 0x18, "nothing connected");
}
static void draw_axes(void)
{
    long us[4];
    int b = read_port(us);
    tu_head("JOYTEST", "what games see on the gameport (port 201h)", NULL);
    draw_stick(2, 3, "Joystick A", us, b, 0);
    draw_stick(41, 3, "Joystick B", us + 2, b, 2);
    tui_panel(2, 14, 77, 9, 1, 0x1E, "Reading the numbers");
    tui_wrap(4, 15, 73, 7, 0x17,
        "Axis times in microseconds, as a game measures them: about 24 = left / up, 575 = centre, 1124 = right / down."
        " A stick that never moves its dot, or an axis showing ---- , is not connected (or SBEMU's /JOY is off)."
        " Games need the stick calibrated in their own setup. With the USB pad: the left stick is joystick A,"
        " A / B / X / Y are fire 1-4 (Settings > Joystick changes the layout). G shows the Gravis GrIP view.");
    tu_foot("Move the sticks and press the buttons: the dots, bars and lamps follow.", "Esc / Q quit   G GrIP view   F4 screen");
}
static void draw_grip(void)
{
    static const struct { const char* n; int bit, x, y; } btn[] = {
        { "L2", 5, 2, 0 }, { "R2", 2, 27, 0 }, { "L1", 10, 2, 1 }, { "R1", 11, 27, 1 },
        { "Up", 12, 5, 3 }, { "Left", 16, 0, 4 }, { "Right", 15, 7, 4 }, { "Down", 13, 4, 5 },
        { "Select", 1, 16, 4 }, { "Start", 0, 16, 5 },
        { "X", 8, 27, 3 }, { "Y", 3, 30, 4 }, { "A", 6, 24, 4 }, { "B", 7, 27, 5 } };
    int p, j;
    tu_head("JOYTEST", "Gravis GamePad Pro (GrIP, digital mode)", NULL);
    for (p = 0; p < 2; ++p)
    {
        long d = grip_read(4 + p * 2);
        int x = 2 + p * 39, y = 3;
        char t[16]; snprintf(t, sizeof t, "Pad %d", p + 1);
        tui_panel(x, y, 38, 10, 1, 0x1E, t);
        for (j = 0; j < (int)(sizeof btn / sizeof btn[0]); ++j)
            tu_lamp(x + 2 + btn[j].x, y + 2 + btn[j].y, d >= 0 && (d >> btn[j].bit) & 1, btn[j].n);
        tui_put(x + 2, y + 8, d >= 0 ? 0x1A : 0x18, d >= 0 ? "pad answers" : "no GrIP pad on this port");
    }
    tui_panel(2, 14, 77, 9, 1, 0x1E, "GrIP");
    tui_wrap(4, 15, 73, 7, 0x17,
        "Every button of up to two Gravis GamePad Pro pads, read the way games with GrIP support read them."
        " With the USB pad this needs Settings > Joystick > Stick type = Gravis GamePad Pro (SBEMU /JOYMODE:3)."
        " G goes back to the plain gameport view.");
    tu_foot("Press the pad buttons: the lamps light while they are held.", "Esc / Q quit   G gameport view   F4 screen");
}

int main(int argc, char** argv)
{
    long us[4];
    int b, i, logn = 0, grip = 0;
    const char* logf = NULL;
    for (i = 1; i < argc; ++i)
    {
        if (!strncasecmp(argv[i], "/LOG:", 5)) logn = atoi(argv[i] + 5);
        else if (!strcasecmp(argv[i], "/GRIP")) grip = 1;
        else if (argv[i][0] != '/') logf = argv[i];
        else { printf("JOYTEST [/GRIP] [/LOG:n FILE]\n  /GRIP  Gravis GamePad Pro digital (GrIP) view: every button of pads 1 and 2\n"); return 1; }
    }
    calibrate();
    if (logn > 0)
    {
        FILE* f = logf ? fopen(logf, "w") : stdout;
        if (!f) { printf("Cannot write %s\n", logf); return 1; }
        fprintf(f, "JOYTEST log, CPU %lu MHz\n", (unsigned long)(cps / 1000000));
        for (i = 0; i < logn && grip; ++i)
        {
            unsigned long t = ticks();
            char a[120], c[120];
            grip_text(a, grip_read(4)); grip_text(c, grip_read(6));
            fprintf(f, "%3d  pad 1: %s | pad 2: %s\n", i, a, c);
            fflush(f);
            while (ticks() - t < 4) {}
        }
        for (i = 0; i < logn && !grip; ++i)
        {
            unsigned long t = ticks();
            char keys[64]; int kn = 0;
            b = read_port(us);
            while (ticks() - t < 4)
                while (kbhit() && kn < 60) { int c = getch(); kn += sprintf(keys + kn, "%02X ", c); }
            keys[kn] = 0;
            fprintf(f, "%3d  A:%5ld %5ld  B:%5ld %5ld  buttons %c%c%c%c  keys %s\n", i, us[0], us[1], us[2], us[3],
                b & 1 ? '1' : '-', b & 2 ? '2' : '-', b & 4 ? '3' : '-', b & 8 ? '4' : '-', keys);
            fflush(f);
        }
        if (logf) fclose(f);
        return 0;
    }
    if (!isatty(1)) { printf("JOYTEST: run it on the screen (or use /LOG:n FILE)\n"); return 1; }
    tu_start(argv[0]);
    for (;;)
    {
        int c;
        if (grip) draw_grip(); else draw_axes();
        c = tu_poll();
        if (c == 27 || c == 'q' || c == 'Q') break;
        if (c == 'g' || c == 'G') grip = !grip;
        delay(30);
    }
    tu_end();
    return 0;
}
