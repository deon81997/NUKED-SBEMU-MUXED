/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* KT - asks SBEMU to type a key script (same MPU side channel as GAMETEST): KT 80 0A 20 17 13 1C ... (hex) */
#include <stdio.h>
#include <stdlib.h>
#include <pc.h>
static void cmd(int v){ long i; for(i=0;i<100000 && (inportb(0x331)&0x40);i++){} outportb(0x331,v); for(i=0;i<1000;i++) if(!(inportb(0x331)&0x80)) { inportb(0x330); break; } }
int main(int argc,char**argv){ int i; cmd(0xAB); for(i=1;i<argc;i++) cmd((int)strtol(argv[i],0,16)); cmd(0x00); printf("sent %d bytes\n",argc-1); return 0; }
