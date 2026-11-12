/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* cpuload.c - CPULOAD.EXE: how much CPU Windows programs leave over (test rig, Windows 9x).
 * CPULOAD seconds [file] [label]: an idle-priority thread counts a loop for that long and appends
 * "label loops-per-second" to the file. The drop between two runs is what the other programs took.
 * MinGW: i686-w64-mingw32-gcc -O1 -o CPULOAD.EXE cpuload.c */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 10;
    const char *fn = argc > 2 ? argv[2] : "CPULOAD.LOG";
    const char *label = argc > 3 ? argv[3] : "run";
    volatile unsigned long n = 0;
    DWORD t0, end;
    SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_IDLE);
    t0 = GetTickCount();
    while (GetTickCount() == t0) ;
    t0 = GetTickCount();
    end = t0 + secs * 1000;
    for (;;) {
        unsigned i;
        for (i = 0; i < 1000; i++) n++;
        if (GetTickCount() >= end) break;
    }
    {
        FILE *f = fopen(fn, "a");
        unsigned long per = (unsigned long)((double)n / ((GetTickCount() - t0) / 1000.0));
        if (f) { fprintf(f, "%s %lu\n", label, per); fclose(f); }
    }
    return 0;
}
