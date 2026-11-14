/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* MPUPLAY.EXE - test rig only (not in the pack): plays an MPU-401 script the way a game driver writes it, for the
 * USB MIDI byte-exact checks (testrig/midi/gamecap.py, scripts from mkscript.py).
 *   MPUPLAY script.txt [/P:330]
 * Script: one token per item, whitespace separated:
 *   Dxx data byte (waits for DRR = status bit 6 clear, 1 s at most, like a game)   Cxx command byte (same wait)
 *   Wn wait n ms   A read the data port once DSR says a byte is there (an ACK / answer, 0.5 s at most)
 *   N  write the following D/C bytes WITHOUT waiting for DRR (a careless driver)   Y  wait for DRR again
 * Build: i586-pc-msdosdjgpp-gcc -O2 -o MPUPLAY.EXE mpuplay.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pc.h>
#include <time.h>
static int base = 0x330, drr = 1;
static unsigned long nb, nwait, ntmo;
static void ready(void)
{
    uclock_t end;
    if (!drr || !(inportb(base + 1) & 0x40)) return;
    ++nwait;
    end = uclock() + UCLOCKS_PER_SEC;
    while ((inportb(base + 1) & 0x40) && uclock() < end) {}
    if (inportb(base + 1) & 0x40) ++ntmo;
}
int main(int argc, char** argv)
{
    FILE* f;
    char t[32];
    int i, acks = 0, bad = 0;
    for (i = 2; i < argc; i++) if (!strncmp(argv[i], "/P:", 3)) base = (int)strtol(argv[i] + 3, NULL, 16);
    if (argc < 2 || !(f = fopen(argv[1], "r"))) { puts("MPUPLAY script.txt [/P:330]"); return 2; }
    while (fscanf(f, "%31s", t) == 1)
    {
        switch (t[0])
        {
        case 'D': ready(); outportb(base, (unsigned char)strtol(t + 1, NULL, 16)); ++nb; break;
        case 'C': ready(); outportb(base + 1, (unsigned char)strtol(t + 1, NULL, 16)); break;
        case 'W': { uclock_t e = uclock() + (uclock_t)atoi(t + 1) * UCLOCKS_PER_SEC / 1000; while (uclock() < e) {} } break;
        case 'A':
        {
            uclock_t e = uclock() + UCLOCKS_PER_SEC / 2;
            while ((inportb(base + 1) & 0x80) && uclock() < e) {}
            if (inportb(base + 1) & 0x80) bad++; else { inportb(base); acks++; }
            break;
        }
        case 'N': drr = 0; break;
        case 'Y': drr = 1; break;
        }
    }
    fclose(f);
    printf("MPUPLAY %s: %lu data bytes, %d answers read, %d missing, waited for DRR %lu times (%lu timeouts)\n",
           argv[1], nb, acks, bad, nwait, ntmo);
    return bad ? 1 : 0;
}
