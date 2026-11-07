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

static unsigned long ticks(void) { return _farpeekl(_dos_ds, 0x46C); }

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
    printf("KMTEST - keyboard (INT 16h) and mouse (INT 33h) test. Esc twice quits.\n");
    printf("Mouse driver: %s (%d buttons)\n", have ? "found" : "NOT found", have ? r.x.bx : 0);
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
            printf("\nkey: scan %02X char %02X '%c'  shift flags %02X", sc, ch, ch >= 32 && ch < 127 ? ch : '.', _farpeekb(_dos_ds, 0x417));
            if (log) { fprintf(log, "key %02X %02X\n", sc, ch); fflush(log); }
            if (sc == 1) { if (++esc == 2) break; } else esc = 0;
        }
        if (have)
        {
            memset(&r, 0, sizeof(r)); r.x.ax = 3; __dpmi_int(0x33, &r);
            wheel += (signed char)r.h.bh;
            if (r.x.cx != lx || r.x.dx != ly || (r.x.bx & 7) != lb || r.h.bh)
            {
                lx = r.x.cx; ly = r.x.dx; lb = r.x.bx & 7;
                printf("\rmouse x %3d y %3d buttons %d wheel %d      ", lx, ly, lb, wheel);
                if (log) { fprintf(log, "mouse %d %d %d %d\n", lx, ly, lb, wheel); fflush(log); }
            }
        }
        if (secs && ticks() - t0 > (unsigned long)secs * 182 / 10) break;
        __dpmi_yield();
    }
    printf("\n");
    if (log) fclose(log);
    return 0;
}
