/* USBTEST - finds a USB Xbox 360 pad the same way SBEMU /JOY does, and shows every step.
 *   USBTEST            USB controllers found, what was done with each, what is on their ports, then the
 *                      pad live (sticks, triggers, buttons) and every plug / unplug. Esc quits.
 *   USBTEST FILE       the same, also written to FILE (for a bug report)
 *   USBTEST FILE /T:n  ... and stop by itself after n seconds
 * Run it without SBEMU loaded (it takes the USB controllers over, like SBEMU does), after
 * C:\HDPMI\HDPMI32I.EXE -r -x. The BIOS's USB keyboard / mouse support stops until the next restart. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <conio.h>
#include <dos.h>
#include <go32.h>
#include <sys/farptr.h>
#include "usbpad.h"
#include "dpmi/dpmi.h"

static FILE* Log;

static void out(const char* f, ...)
{
    va_list a;
    va_start(a, f); vprintf(f, a); va_end(a);
    if (Log) { va_start(a, f); vfprintf(Log, f, a); va_end(a); fflush(Log); }
}

static inline unsigned long long tsc(void) { unsigned lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return ((unsigned long long)hi << 32) | lo; }
static unsigned long ticks(void) { return _farpeekl(_dos_ds, 0x46C); }

int main(int argc, char** argv)
{
    unsigned long long cps, a, t;
    unsigned long tk;
    const char* msg = "";
    const USBPAD_State* p;
    int shown = 0, was = -1, n, i, secs = 0;
    unsigned long long start;
    unsigned lastb = 0xFFFFFFFF;
    for (i = 1; i < argc; ++i)
    {
        if (!strncasecmp(argv[i], "/T:", 3)) secs = atoi(argv[i] + 3);
        else if (argv[i][0] != '/') Log = fopen(argv[i], "w");
        else { printf("USBTEST [logfile] [/T:seconds]\n"); return 1; }
    }
    DPMI_Init();
    tk = ticks(); while (ticks() == tk) {}
    a = tsc(); tk = ticks(); while (ticks() - tk < 4) {}
    cps = (tsc() - a) * 18206 / 4000;
    out("USBTEST - USB Xbox 360 pad finder (same code as SBEMU /JOY)\nCPU %lu MHz\n", (unsigned long)(cps / 1000000));
    out("Looking for USB controllers and a pad (3 s)...\n");
    n = USBPAD_Init(cps, 3000, &msg);
    out("%s\n", USBPAD_Trace());
    shown = (int)strlen(USBPAD_Trace());
    out("\nResult: %s\n", msg);
    if (!n) { out("Nothing usable.\n"); return 1; }
    out("\nLive view - plug / unplug the pad, move the sticks. Esc quits.\n");
    t = start = tsc();
    for (;;)
    {
        const char* tr;
        USBPAD_Poll();
        tr = USBPAD_Trace();
        if ((int)strlen(tr) > shown) { out("\n%s", tr + shown); shown = (int)strlen(tr); }
        p = USBPAD_Get();
        if (p->connected != was)
        {
            out("\n%s: %s\n", p->connected ? "PAD CONNECTED" : "no pad", USBPAD_Describe());
            was = p->connected;
        }
        if (tsc() - t > cps / 10)
        {
            t = tsc();
            if (p->connected)
            {
                printf("\rLX %6d LY %6d  RX %6d RY %6d  LT %3d RT %3d  buttons %04X  reports %lu   ",
                    p->lx, p->ly, p->rx, p->ry, p->lt, p->rt, p->buttons, (unsigned long)p->reports);
                if (p->buttons != lastb && Log)
                {
                    fprintf(Log, "buttons %04X  LX %d LY %d RX %d RY %d LT %d RT %d (reports %lu)\n", p->buttons,
                        p->lx, p->ly, p->rx, p->ry, p->lt, p->rt, (unsigned long)p->reports);
                    fflush(Log);
                    lastb = p->buttons;
                }
            }
        }
        if (kbhit() && getch() == 27) break;
        if (secs && tsc() - start > cps * (unsigned long long)secs) break;
    }
    out("\n");
    USBPAD_Shutdown();
    if (Log) fclose(Log);
    return 0;
}
