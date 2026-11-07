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

static void bar(char* s, long us)
{
    int i, n = us < 0 ? -1 : (int)(us * 40 / 1150);
    for (i = 0; i < 40; ++i) s[i] = n < 0 ? ' ' : i < n ? '\xDB' : '\xFA';
    s[40] = 0;
}

int main(int argc, char** argv)
{
    long us[4];
    int b, i, logn = 0;
    const char* logf = NULL;
    for (i = 1; i < argc; ++i)
    {
        if (!strncasecmp(argv[i], "/LOG:", 5)) logn = atoi(argv[i] + 5);
        else if (argv[i][0] != '/') logf = argv[i];
        else { printf("JOYTEST [/LOG:n FILE]\n"); return 1; }
    }
    calibrate();
    if (logn > 0)
    {
        FILE* f = logf ? fopen(logf, "w") : stdout;
        if (!f) { printf("Cannot write %s\n", logf); return 1; }
        fprintf(f, "JOYTEST log, CPU %lu MHz\n", (unsigned long)(cps / 1000000));
        for (i = 0; i < logn; ++i)
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
