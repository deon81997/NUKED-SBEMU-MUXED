/* USBTEST - finds USB pads, keyboards and mice the same way SBEMU /JOY does, and shows every step.
 *   USBTEST            USB controllers found, what was done with each, what is on their ports, then live:
 *                      both pads (sticks, triggers, buttons), USB keys pressed, mouse movement, every plug /
 *                      unplug. Esc (PS/2 or USB keyboard) quits.
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

/* USB keyboard / mouse events: collected in the poll, printed by the main loop */
static volatile int KEsc, KNew, MNew;
static uint8_t KRep[8];
static long MX, MY, MW; static int MBt;
static void kb(int id, const uint8_t* r)
{
    int i;
    (void)id;
    memcpy(KRep, r, 8); KNew = 1;
    for (i = 2; i < 8; ++i) if (r[i] == 0x29) KEsc = 1;
}
static void ms(int b, int dx, int dy, int dw) { MX += dx; MY += dy; MW += dw; MBt = b; MNew = 1; }

int main(int argc, char** argv)
{
    unsigned long long cps, a, t, start;
    unsigned long tk;
    const char* msg = "";
    int n, i, secs = 0;
    uint32_t ch = 0;
    unsigned lastb[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
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
    out("USBTEST - USB pad / keyboard / mouse finder (same code as SBEMU /JOY)\nCPU %lu MHz\n", (unsigned long)(cps / 1000000));
    out("Looking for USB controllers and devices (up to 3 s)...\n");
    USBPAD_SetHid(kb, ms);
    n = USBPAD_Init(cps, 3000, USBPAD_F_KBD | USBPAD_F_MOUSE, &msg);
    out("%s\n", USBPAD_Trace());
    out("\nResult: %s\n", msg);
    if (!n) { out("Nothing usable.\n"); return 1; }
    out("\nLive view - plug / unplug, move the sticks, type, move the mouse. Esc quits.\n");
    {
        int shown = (int)strlen(USBPAD_Trace());
        t = start = tsc();
        for (;;)
        {
            const char* tr;
            USBPAD_Poll();
            tr = USBPAD_Trace();
            if ((int)strlen(tr) > shown) { out("\n%s", tr + shown); shown = (int)strlen(tr); }
            if (USBPAD_Changes() != ch)
            {
                ch = USBPAD_Changes();
                out("\n-- %s\n-- in use now:\n%s", USBPAD_Describe(), USBPAD_Devices()[0] ? USBPAD_Devices() : "(nothing)\n");
            }
            if (KNew)
            {
                KNew = 0;
                out("\nUSB keys: mod %02X keys %02X %02X %02X %02X %02X %02X", KRep[0], KRep[2], KRep[3], KRep[4], KRep[5], KRep[6], KRep[7]);
            }
            if (tsc() - t > cps / 10)
            {
                t = tsc();
                printf("\r");
                for (i = 0; i < USBPAD_PADS; ++i)
                {
                    const USBPAD_State* p = USBPAD_GetPad(i);
                    if (p->connected)
                    {
                        printf("%d:%6d%7d%7d%7d %04X ", i + 1, p->lx, p->ly, p->rx, p->ry, p->buttons);   /* 37 chars */
                        if (p->buttons != lastb[i] && Log)
                        {
                            fprintf(Log, "pad %d buttons %04X  LX %d LY %d RX %d RY %d LT %d RT %d (reports %lu)\n", i + 1, p->buttons,
                                p->lx, p->ly, p->rx, p->ry, p->lt, p->rt, (unsigned long)p->reports);
                            fflush(Log);
                            lastb[i] = p->buttons;
                        }
                    }
                }
                if (MNew)
                {
                    MNew = 0;
                    out("\nmouse x %ld y %ld wheel %ld buttons %d\n", MX, MY, MW, MBt);
                }
            }
            if (KEsc) break;
            if (kbhit() && getch() == 27) break;
            if (secs && tsc() - start > cps * (unsigned long long)secs) break;
        }
    }
    out("\n");
    USBPAD_Shutdown();
    if (Log) fclose(Log);
    return 0;
}
