/* wjoy.c - WJOY.EXE: log the Windows joysticks (winmm), test rig.
 * WJOY [seconds] [file]: the joysticks Windows has (name, VxD, axes, buttons, ranges), then every
 * 500 ms each one's position. MinGW (msvcrt), console. */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned char inb(unsigned short p) { unsigned char v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v; }
static void outb(unsigned short p, unsigned char v) { __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(p)); }

/* WJOY P port seconds file: read a game port from a Windows program (loop counts, as JPORT) */
static int port_test(int argc, char **argv)
{
    unsigned port = argc > 2 ? (unsigned)strtoul(argv[2], 0, 16) : 0x201;
    int secs = argc > 3 ? atoi(argv[3]) : 3;
    FILE *f = fopen(argc > 4 ? argv[4] : "WPORT.LOG", "w");
    DWORD end = GetTickCount() + secs * 1000;
    if (!f) return 1;
    fprintf(f, "WJOY port %X (Windows program)\n", port);
    while (GetTickCount() < end) {
        unsigned n[4] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, k, i;
        unsigned char v;
        outb((unsigned short)port, 0xFF);
        for (k = 0; k < 20000; k++) {
            v = inb((unsigned short)port);
            for (i = 0; i < 4; i++) if (n[i] == 0xFFFF && !(v & (1 << i))) n[i] = k;
            if ((v & 15) == 0) break;
        }
        fprintf(f, "ax");
        for (i = 0; i < 4; i++) if (n[i] == 0xFFFF) fprintf(f, " -"); else fprintf(f, " %u", n[i]);
        fprintf(f, " bt %X\n", (~inb((unsigned short)port) >> 4) & 15);
        fflush(f);
        Sleep(250);
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc > 1 && (argv[1][0] == 'P' || argv[1][0] == 'p') && !argv[1][1]) return port_test(argc, argv);
    int secs = argc > 1 ? atoi(argv[1]) : 10;
    FILE *f = fopen(argc > 2 ? argv[2] : "WJOY.LOG", "w");
    UINT n = joyGetNumDevs(), j;
    DWORD end;
    if (!f) return 1;
    fprintf(f, "joyGetNumDevs %u\n", n);
    if (n > 16) n = 16;
    for (j = 0; j < n; j++) {
        JOYCAPSA c;
        MMRESULT r = joyGetDevCapsA(j, &c, sizeof c);
        if (r != JOYERR_NOERROR) { fprintf(f, "%u: caps err %u\n", j + 1, r); continue; }
        fprintf(f, "%u: \"%s\" vxd \"%s\" reg \"%s\" axes %u/%u buttons %u/%u caps %X X %u-%u Y %u-%u Z %u-%u R %u-%u U %u-%u\n",
                j + 1, c.szPname, c.szOEMVxD, c.szRegKey, c.wNumAxes, c.wMaxAxes, c.wNumButtons, c.wMaxButtons,
                c.wCaps, c.wXmin, c.wXmax, c.wYmin, c.wYmax, c.wZmin, c.wZmax, c.wRmin, c.wRmax, c.wUmin, c.wUmax);
    }
    fflush(f);
    end = GetTickCount() + secs * 1000;
    while (GetTickCount() < end) {
        fprintf(f, "t %lu", GetTickCount());
        for (j = 0; j < n; j++) {
            JOYINFOEX x;
            MMRESULT r;
            x.dwSize = sizeof x; x.dwFlags = JOY_RETURNALL;
            r = joyGetPosEx(j, &x);
            if (r != JOYERR_NOERROR) { fprintf(f, " | %u: err %u", j + 1, r); continue; }
            fprintf(f, " | %u: %lu %lu %lu %lu %lu %lu b%lX pov %lu", j + 1, x.dwXpos, x.dwYpos, x.dwZpos, x.dwRpos,
                    x.dwUpos, x.dwVpos, x.dwButtons, x.dwPOV);
        }
        fputc('\n', f);
        fflush(f);
        Sleep(500);
    }
    fclose(f);
    return 0;
}
