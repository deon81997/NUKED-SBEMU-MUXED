/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* CDQ.EXE [LOG] - shows the SBCDA1 mailbox of the uCDD driver (state, flags, pos) */
#include <stdio.h>
#include <string.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
int main(int argc,char**argv){ __dpmi_regs r; unsigned char l[5*4], seg[0xF000]; unsigned s,o,i; FILE*f=argc>1?fopen(argv[1],"a"):stdout;
  memset(&r,0,sizeof r); r.x.ax=0x1501; r.x.es=__tb>>4; r.x.bx=0; __dpmi_int(0x2f,&r); dosmemget(__tb,20,l);
  for(i=0;i<4;i++){ unsigned char nm[9]; o=l[i*5+1]|(l[i*5+2]<<8); s=l[i*5+3]|(l[i*5+4]<<8); dosmemget(s*16+o+10,8,nm); nm[8]=0;
    if(!strcmp((char*)nm,"UCDD0001")) break; }
  if(i==4){ fprintf(f,"no UCDD0001 in the 1501h list\n"); return 1; }
  dosmemget(s*16+o,0xF000,seg);
  for(i=0;i+32<0xF000;i++) if(!memcmp(seg+i,"SBCDA1\0\0",8)&&seg[i+30]==1){ unsigned char*b=seg+i;
      fprintf(f,"box at %04X:%04X state %d flags %02X cmd %d start %lu end %lu pos %lu gen %u vol %d/%d\n",s,o+i,b[8],b[9],b[10],
        *(unsigned long*)(b+12),*(unsigned long*)(b+16),*(unsigned long*)(b+20),*(unsigned short*)(b+24),b[26],b[27]); return 0; }
  fprintf(f,"driver %04X:%04X has no SBCDA1 box\n",s,o); return 1; }
