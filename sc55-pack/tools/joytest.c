/* JOYTEST - shows what a DOS game sees on the gameport (port 201h).
 *   JOYTEST              live view: four axes in microseconds and as bars, four buttons; Esc or Q quits
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

static void bar(char* s, long us)
{
    int i, n = us < 0 ? -1 : (int)(us * 40 / 1150);
    for (i = 0; i < 40; ++i) s[i] = n < 0 ? ' ' : i < n ? '\xDB' : '\xFA';
    s[40] = 0;
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
    clrscr();
    _setcursortype(_NOCURSOR);
    cprintf("JOYTEST - what games see on the gameport (port 201h)       Esc or Q quits\r\n\r\n");
    cprintf("Axis times in microseconds: 24 = left/up, 575 = centre, 1124 = right/down,\r\n");
    cprintf("---- = nothing connected. Calibrate in each game's setup.\r\n");
    if (grip)
    {
        gotoxy(1, 5); cprintf("GrIP view (Gravis GamePad Pro, digital mode; SBEMU /JOYMODE:3)");
        for (;;)
        {
            char a[120];
            for (i = 0; i < 2; ++i)
            {
                grip_text(a, grip_read(4 + i * 2));
                gotoxy(1, 7 + i); clreol(); cprintf("Pad %d: %s", i + 1, a);
            }
            if (kbhit()) { int c = getch(); if (c == 27 || c == 'q' || c == 'Q') break; }
            delay(30);
        }
        gotoxy(1, 14);
        _setcursortype(_NORMALCURSOR);
        return 0;
    }
    for (;;)
    {
        char s[48];
        static const char* nm[4] = { "Joystick A  X", "Joystick A  Y", "Joystick B  X", "Joystick B  Y" };
        b = read_port(us);
        for (i = 0; i < 4; ++i)
        {
            bar(s, us[i]);
            gotoxy(1, 7 + i);
            if (us[i] < 0) cprintf("%s   ----  %s", nm[i], s);
            else cprintf("%s  %5ld  %s", nm[i], us[i], s);
        }
        gotoxy(1, 12);
        cprintf("Buttons     1:%s  2:%s  3:%s  4:%s", b & 1 ? "DOWN" : " up ", b & 2 ? "DOWN" : " up ", b & 4 ? "DOWN" : " up ", b & 8 ? "DOWN" : " up ");
        if (kbhit()) { int c = getch(); if (c == 27 || c == 'q' || c == 'Q') break; }
        delay(30);
    }
    gotoxy(1, 14);
    _setcursortype(_NORMALCURSOR);
    return 0;
}
