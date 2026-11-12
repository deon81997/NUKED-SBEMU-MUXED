/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* jport.c - JPORT.EXE: log a game port the way DOS games read it (test rig).
 * JPORT [port hex] [seconds] [file] [g]: every 250 ms one line: axis times in port reads (loop counts),
 * '-' = never timed out (no stick), button bits (1 = pressed), INT 15h AH=84h answer.
 * 'g': instead decode GrIP frames from the button lines (pad 1: clock line 1, data line 2).
 * Open Watcom 16-bit DOS: wcl -bt=dos -ms -ox jport.c */
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>

static unsigned long ticks(void) { return *(volatile unsigned long __far *)MK_FP(0x40, 0x6C); }
static unsigned pit(void) { unsigned lo, hi; _disable(); outp(0x43, 0); lo = inp(0x40); hi = inp(0x40); _enable(); return (hi << 8) | lo; }

int main(int argc, char **argv)
{
    unsigned port = argc > 1 ? (unsigned)strtoul(argv[1], 0, 16) : 0x201;
    int secs = argc > 2 ? atoi(argv[2]) : 10;
    FILE *f = fopen(argc > 3 ? argv[3] : "JPORT.LOG", "w");
    int grip = argc > 4 && (argv[4][0] == 'g' || argv[4][0] == 'G');
    unsigned long end = ticks() + (unsigned long)secs * 182 / 10, next = 0;
    if (!f) return 1;
    fprintf(f, "JPORT port %X\n", port);
    while (ticks() < end) {
        if (ticks() < next) continue;
        next = ticks() + 4;
        if (grip) {
            /* read until 24 bits with sync: sample data at each falling clock edge */
            unsigned long fr = 0; int n = 0, prev = 1, k;
            unsigned char v;
            unsigned long bits[60]; int nb = 0;
            for (k = 0; k < 3000 && nb < 60; k++) {
                int clk;
                v = (unsigned char)inp(port);
                clk = (v >> 4) & 1;
                if (prev && !clk) bits[nb++] = (v >> 5) & 1;
                prev = clk;
            }
            fprintf(f, "grip %d bits:", nb);
            for (k = 0; k < nb; k++) fputc('0' + (int)bits[k], f);
            fputc('\n', f);
            (void)fr; (void)n;
        } else {
            unsigned n[4], i, k;
            unsigned char v, b;
            union REGS r;
            for (i = 0; i < 4; i++) n[i] = 0xFFFF;
            outp(port, 0xFF);
            for (k = 0; k < 20000; k++) {      /* loop counts, as games time it */
                v = (unsigned char)inp(port);
                for (i = 0; i < 4; i++) if (n[i] == 0xFFFF && !(v & (1 << i))) n[i] = k;
                if ((v & 15) == 0) break;
            }
            {
                unsigned us[4];
                for (i = 0; i < 4; i++) us[i] = n[i];
            b = (unsigned char)(~inp(port) >> 4 & 15);
            fprintf(f, "ax");
            for (i = 0; i < 4; i++) if (us[i] == 0xFFFF) fprintf(f, " -"); else fprintf(f, " %u", us[i]);
            }
            fprintf(f, " bt %X", b);
            r.x.ax = 0x8400; r.x.dx = 1; r.x.cflag = 1;
            int86(0x15, &r, &r);
            if (r.x.cflag) fprintf(f, " int15 -");
            else {
                unsigned ax = r.x.ax, bx = r.x.bx, cx = r.x.cx, dx = r.x.dx;
                r.x.ax = 0x8400; r.x.dx = 0; int86(0x15, &r, &r);
                fprintf(f, " int15 %u %u %u %u b%X", ax, bx, cx, dx, (~r.h.al >> 4) & 15);
            }
            fputc('\n', f);
        }
        fflush(f);
    }
    fclose(f);
    return 0;
}
