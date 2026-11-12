/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* OPLT.EXE: three FM notes (1 s each) 5 s apart on the AdLib port 388h - Nuked OPL3 sleep/wake test */
#include <pc.h>
#include <dos.h>
#include <stdio.h>
static void w(int r, int v) { int i; outportb(0x388, r); for (i = 0; i < 6; i++) inportb(0x388); outportb(0x389, v); for (i = 0; i < 35; i++) inportb(0x388); }
int main(void)
{
    static const unsigned char regs[][2] = {{0x20,0x01},{0x40,0x10},{0x60,0xF0},{0x80,0x77},{0x23,0x01},{0x43,0x00},{0x63,0xF0},{0x83,0x77},{0xC0,0x30},{0xA0,0x98}};
    int i, k;
    for (i = 0; i < (int)(sizeof regs / 2); i++) w(regs[i][0], regs[i][1]);
    for (k = 0; k < 3; k++) {
        delay(5000);
        w(0xB0, 0x31); delay(1000); w(0xB0, 0x11);
        printf("note %d\n", k);
    }
    delay(3000);
    return 0;
}
