/* wjoy.c - WJOY.EXE: log the Windows joysticks (winmm), test rig.
 * WJOY [seconds] [file]: the joysticks Windows has (name, VxD, axes, buttons, ranges), then every
 * 500 ms each one's position. MinGW (msvcrt), console. */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
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
