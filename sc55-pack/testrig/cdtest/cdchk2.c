/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* CDCHK2.EXE - virtual CD test: CDCHK2 DRIVE: LIST [SEEKLIST] [LOG]
   reads every file in LIST (path size crc32) from DRIVE with varying chunk sizes, checks size + CRC,
   counts files via findfirst, random-seek reads from GAME\DATA\BIG.DAT (SEEKLIST: offset len crc).
   Prints and appends results to LOG. Uses plain DOS handle calls (open/read/lseek) like a game. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <io.h>
#include <dir.h>
#include <time.h>
#include <errno.h>
static unsigned long tab[256];
static void mk(void){ unsigned long c; int i,k; for(i=0;i<256;i++){c=i;for(k=0;k<8;k++)c=c&1?0xEDB88320UL^(c>>1):c>>1;tab[i]=c;} }
static unsigned long crc(unsigned long c, const unsigned char* b, long n){ c=~c; while(n--) c=tab[(c^*b++)&0xFF]^(c>>8); return ~c; }
static unsigned char buf[65536];
static FILE* lg;
#define P(...) do{ printf(__VA_ARGS__); if(lg) fprintf(lg, __VA_ARGS__); }while(0)
static int count_files(const char* dir){ struct ffblk f; char p[160]; int n=0, d;
  sprintf(p,"%s\\*.*",dir); d=findfirst(p,&f,FA_DIREC|FA_RDONLY|FA_HIDDEN|FA_SYSTEM);
  while(!d){ if(f.ff_name[0]!='.'){ if(f.ff_attrib&FA_DIREC){ char s[160]; sprintf(s,"%s\\%s",dir,f.ff_name); n+=count_files(s);} else n++; } d=findnext(&f);} return n; }
int main(int argc, char** argv){
  char drv[8], line[300], path[200]; long size; unsigned long want; FILE* l; int ok=0, bad=0, n=0, chunk[]={512,2048,4000,16384,32768,65535};
  clock_t t0; double kb=0;
  if(argc<3){ puts("CDCHK2 M: LIST [SEEKLIST] [LOG]"); return 2; }
  mk(); strcpy(drv,argv[1]); lg = argc>4 ? fopen(argv[4],"a") : NULL;
  l=fopen(argv[2],"r"); if(!l){ P("no list %s\n",argv[2]); return 2; }
  t0=clock();
  while(fgets(line,sizeof line,l)){
    char full[260]; int fd; long got=0; unsigned long c=0; int ch=chunk[n%6];
    if(sscanf(line,"%199s %ld %lx",path,&size,&want)!=3) continue;
    n++; sprintf(full,"%s\\%s",drv,path);
    fd=open(full,O_RDONLY|O_BINARY);
    if(fd<0){ P("OPEN FAIL %s errno %d\n",full,errno); bad++; continue; }
    for(;;){ int r=read(fd,buf,ch); if(r<0){ P("READ FAIL %s at %ld errno %d\n",full,got,errno); break; } if(!r) break; c=crc(c,buf,r); got+=r; }
    close(fd); kb+=got/1024.0;
    if(got!=size||c!=want){ P("BAD %s size %ld/%ld crc %08lX/%08lX\n",full,got,size,c,want); bad++; } else ok++;
  }
  fclose(l);
  P("files: %d ok, %d bad, %.0f KB in %.2f s\n",ok,bad,kb,(double)(clock()-t0)/CLOCKS_PER_SEC);
  { char root[16]; sprintf(root,"%s",drv); P("findfirst count: %d\n",count_files(root)); }
  if(argc>3){ FILE* s=fopen(argv[3],"r"); int sok=0,sbad=0; char full[260]; long o; int fd; t0=clock();
    sprintf(full,"%s\\GAME\\DATA\\BIG.DAT",drv); fd=open(full,O_RDONLY|O_BINARY);
    while(s&&fd>=0&&fgets(line,sizeof line,s)){ long len; if(sscanf(line,"%ld %ld %lx",&o,&len,&want)!=3) continue;
      if(lseek(fd,o,SEEK_SET)!=o||read(fd,buf,len)!=len||crc(0,buf,len)!=want){ sbad++; if(sbad<5) P("SEEK BAD at %ld len %ld\n",o,len);} else sok++; }
    if(fd>=0) close(fd); if(s) fclose(s);
    P("seeks: %d ok, %d bad in %.2f s\n",sok,sbad,(double)(clock()-t0)/CLOCKS_PER_SEC); bad+=sbad; }
  P("RESULT %s\n", bad?"FAIL":"PASS"); if(lg) fclose(lg);
  return bad?1:0;
}
