/* idlemtr.c - IDLEMTR.EXE: how much CPU is left for the program in front (test rig, DOS).
 * IDLEMTR seconds [file] [label]: counts a busy loop between BIOS timer ticks for that long and
 * appends "label loops-per-tick" to the file. Compare a run with SBEMU loaded against one without:
 * the drop is what SBEMU's interrupts take. Open Watcom 16-bit: wcl -bt=dos -ms -ox idlemtr.c */
#include <stdio.h>
#include <stdlib.h>
#include <dos.h>
#include <i86.h>

static volatile unsigned long __far *tick = (volatile unsigned long __far *)MK_FP(0x40, 0x6C);

int main(int argc, char **argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 10;
    const char *fn = argc > 2 ? argv[2] : "IDLEMTR.LOG";
    const char *label = argc > 3 ? argv[3] : "run";
    unsigned long t, end, n = 0, ticks = 0;
    FILE *f;
    t = *tick;
    while (*tick == t) ;                       /* start on a tick edge */
    t = *tick;
    end = t + (unsigned long)secs * 182 / 10;
    while (t < end) {
        if (*tick != t) { t = *tick; ticks++; continue; }
        n++;
    }
    f = fopen(fn, "a");
    if (f) { fprintf(f, "%s %lu\n", label, ticks ? n / ticks : 0); fclose(f); }
    printf("%s: %lu loops per tick\n", label, ticks ? n / ticks : 0);
    return 0;
}
