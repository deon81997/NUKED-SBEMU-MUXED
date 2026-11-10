/* MSCDX.EXE - checks the MSCDEX interface like DOS games use it (INT 2Fh AX=15xxh).
   MSCDX DRIVE IMAGE [LOG]   IMAGE = the same image on C: (.ISO = 2048-byte sectors, .BIN = 2352-byte raw) to
   compare sectors with. Writes every answer; "BAD" lines are mismatches with what a real drive returns. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/movedata.h>
static FILE* lg; static int bad;
#define P(...) do{ printf(__VA_ARGS__); if(lg) fprintf(lg, __VA_ARGS__); }while(0)
#define CHECK(c, ...) do{ if(!(c)){ bad++; P("BAD "); P(__VA_ARGS__); } else { P("ok  "); P(__VA_ARGS__); } }while(0)
static unsigned tb, tbseg; static int drv;
static __dpmi_regs r;
static void mx(int ax){ memset(&r,0,sizeof r); r.x.ax=ax; }
/* device request through AX=1510h; request header at tb, data at tb+256 */
static int devreq(unsigned char* hdr, int len, unsigned char* data, int dlen){
  dosmemput(hdr,len,tb); if(data&&dlen) dosmemput(data,dlen,tb+256);
  mx(0x1510); r.x.cx=drv; r.x.es=tbseg; r.x.bx=0; __dpmi_int(0x2f,&r);
  dosmemget(tb,len,hdr); if(data&&dlen) dosmemget(tb+256,dlen,data);
  return hdr[3]|(hdr[4]<<8); }
static int ioctl_in(unsigned char* cb, int n){ unsigned char h[26]; memset(h,0,26); h[0]=26; h[2]=3;
  *(unsigned short*)(h+14)=0; *(unsigned short*)(h+16)=tbseg; *(unsigned short*)(h+18)=n;
  /* transfer address = tb+256 */ *(unsigned short*)(h+14)=256; return devreq(h,26,cb,n); }
static int ioctl_out(unsigned char* cb, int n){ unsigned char h[26]; memset(h,0,26); h[0]=26; h[2]=12;
  *(unsigned short*)(h+14)=256; *(unsigned short*)(h+16)=tbseg; *(unsigned short*)(h+18)=n; return devreq(h,26,cb,n); }
static FILE* img; static int raw;
#include <time.h>
static void delay_ms(int ms){ uclock_t e=uclock()+(uclock_t)ms*UCLOCKS_PER_SEC/1000; while(uclock()<e); }
static int imgsec(long lba, unsigned char* out){ if(!img) return 0; fseek(img, raw? lba*2352+16 : lba*2048, SEEK_SET); return fread(out,1,2048,img)==2048; }
static long msf2lba(unsigned long m){ return ((m>>16)&0xFF)*4500L + ((m>>8)&0xFF)*75L + (m&0xFF) - 150; }
int main(int argc,char**argv){
  unsigned char cb[512], s1[4096], s2[4096]; int st,i;
  if(argc<3){ puts("MSCDX M: C:\\CDIMAGES\\X.ISO [LOG]"); return 2; }
  drv=(argv[1][0]|0x20)-'a'; lg=argc>3?fopen(argv[3],"a"):NULL;
  img=fopen(argv[2],"rb"); raw = strstr(argv[2],".BIN")||strstr(argv[2],".bin");
  tb=__tb; tbseg=__tb>>4;
  P("drive %c: image %s (%s)\n",'A'+drv,argv[2],raw?"raw 2352":"2048");
  mx(0x1500); r.x.bx=0; __dpmi_int(0x2f,&r); CHECK(r.x.bx>=1,"1500h: %d CD drive(s), first %c:\n",r.x.bx,'A'+r.x.cx);
  { int first=r.x.cx; unsigned char v[2048]; mx(0x1505); r.x.cx=first; r.x.dx=0; r.x.es=tbseg; r.x.bx=0; __dpmi_int(0x2f,&r); dosmemget(tb,2048,v);
    CHECK(first==drv,"a game that takes the FIRST CD drive (%c:) finds %s\n",'A'+first, (r.x.flags&1)?"an error (no disc)":"a disc"); }
  mx(0x150C); __dpmi_int(0x2f,&r); CHECK(r.x.bx>=0x20A,"150Ch: MSCDEX version %d.%02d\n",r.h.bh,r.h.bl);
  mx(0x150B); r.x.cx=drv; __dpmi_int(0x2f,&r); CHECK(r.x.bx==0xADAD&&r.x.ax!=0,"150Bh: drive check BX=%04X AX=%04X\n",r.x.bx,r.x.ax);
  mx(0x150D); r.x.es=tbseg; r.x.bx=0; __dpmi_int(0x2f,&r); dosmemget(tb,4,cb); CHECK(1,"150Dh: drive letters %c %c\n",'A'+cb[0],cb[1]<26?'A'+cb[1]:'-');
  /* volume descriptor */
  mx(0x1505); r.x.cx=drv; r.x.dx=0; r.x.es=tbseg; r.x.bx=0; __dpmi_int(0x2f,&r); dosmemget(tb,2048,s1);
  CHECK(!(r.x.flags&1)&&!memcmp(s1+1,"CD001",5),"1505h: read VTOC CF=%d AX=%04X id %.5s label %.16s\n",r.x.flags&1,r.x.ax,s1+1,s1+40);
  /* absolute reads, 1 and 8 sectors, several places */
  { long lbas[]={16,17,18,20,100,1000,5000,14000}; int k;
    for(k=0;k<8;k++){ int n= k&1?8:1; int j, same=1; mx(0x1508); r.x.cx=drv; r.x.dx=n; r.x.si=lbas[k]>>16; r.x.di=lbas[k]&0xFFFF; r.x.es=tbseg; r.x.bx=0;
      __dpmi_int(0x2f,&r); for(j=0;j<n&&same;j++){ dosmemget(tb+j*2048,2048,s1); if(imgsec(lbas[k]+j,s2)&&memcmp(s1,s2,2048)) same=0; }
      CHECK(!(r.x.flags&1)&&same,"1508h: read %d sector(s) at %ld CF=%d AX=%04X %s\n",n,lbas[k],r.x.flags&1,r.x.ax,same?"same as image":"DIFFERENT"); } }
  /* device requests */
  cb[0]=0; st=ioctl_in(cb,5); CHECK(!(st&0x8000),"IOCTL 0 device header: status %04X addr %04X:%04X\n",st,*(unsigned short*)(cb+3),*(unsigned short*)(cb+1));
  cb[0]=1; cb[1]=0; st=ioctl_in(cb,6); CHECK(!(st&0x8000),"IOCTL 1 head location: status %04X lba %lu\n",st,*(unsigned long*)(cb+2));
  cb[0]=6; st=ioctl_in(cb,5); { unsigned long ds=*(unsigned long*)(cb+1);
    CHECK(!(st&0x8000)&&!(ds&0x800),"IOCTL 6 device status: status %04X flags %08lX (door %s, %s, raw %s, audio %s, no disc %s)\n",st,ds,ds&1?"open":"closed",ds&2?"unlocked":"locked",ds&4?"yes":"no",ds&0x10?"yes":"no",ds&0x800?"YES":"no"); }
  cb[0]=7; cb[1]=0; st=ioctl_in(cb,4); CHECK(!(st&0x8000)&&*(unsigned short*)(cb+2)==2048,"IOCTL 7 sector size cooked: status %04X %u\n",st,*(unsigned short*)(cb+2));
  cb[0]=7; cb[1]=1; st=ioctl_in(cb,4); P("    IOCTL 7 sector size raw: status %04X %u\n",st,*(unsigned short*)(cb+2));
  cb[0]=8; st=ioctl_in(cb,5); { unsigned long vs=*(unsigned long*)(cb+1); long want=-1;
    if(img){ fseek(img,0,SEEK_END); want = ftell(img)/(raw?2352:2048); }
    CHECK(!(st&0x8000)&&vs>0,"IOCTL 8 volume size: status %04X %lu sectors (image file %ld)\n",st,vs,want); }
  cb[0]=9; st=ioctl_in(cb,2); CHECK(!(st&0x8000)&&(signed char)cb[1]!=0,"IOCTL 9 media changed: status %04X %d (1 = no, -1 = yes, 0 = don't know)\n",st,(signed char)cb[1]);
  cb[0]=9; st=ioctl_in(cb,2); CHECK(!(st&0x8000)&&(signed char)cb[1]==1,"IOCTL 9 media changed again: status %04X %d\n",st,(signed char)cb[1]);
  cb[0]=10; st=ioctl_in(cb,7); { int lo=cb[1],hi=cb[2]; unsigned long lead=*(unsigned long*)(cb+3);
    CHECK(!(st&0x8000)&&lo==1&&hi>=lo,"IOCTL 10 audio disk info: status %04X tracks %d-%d lead-out %02lu:%02lu.%02lu\n",st,lo,hi,(lead>>16)&0xFF,(lead>>8)&0xFF,lead&0xFF);
    for(i=lo;i<=hi&&i<100;i++){ cb[0]=11; cb[1]=i; st=ioctl_in(cb,7); { unsigned long a=*(unsigned long*)(cb+2);
      CHECK(!(st&0x8000),"IOCTL 11 track %d: status %04X start %02lu:%02lu.%02lu (lba %ld) control %02X %s\n",i,st,(a>>16)&0xFF,(a>>8)&0xFF,a&0xFF,msf2lba(a),cb[6],cb[6]&0x40?"data":"audio"); } } }
  cb[0]=12; st=ioctl_in(cb,11); CHECK(!(st&0x8000),"IOCTL 12 Q-channel: status %04X ctrl %02X track %02X index %02X\n",st,cb[1],cb[2],cb[3]);
  cb[0]=15; st=ioctl_in(cb,11); CHECK(!(st&0x8000),"IOCTL 15 audio status: status %04X paused %d\n",st,cb[1]&1);
  cb[0]=4; st=ioctl_in(cb,9); CHECK(!(st&0x8000),"IOCTL 4 audio channel info: status %04X vol %d %d\n",st,cb[2],cb[4]);
  /* READ LONG (command 128), cooked, 2 sectors at 16 */
  { unsigned char h[27]; memset(h,0,27); h[0]=27; h[2]=128; h[13]=0; *(unsigned short*)(h+14)=256; *(unsigned short*)(h+16)=tbseg;
    *(unsigned short*)(h+18)=2; *(unsigned long*)(h+20)=16; h[24]=0; h[25]=0;
    st=devreq(h,27,NULL,0); dosmemget(tb+256,2048,s1); imgsec(16,s2);
    CHECK(!(st&0x8000)&&!memcmp(s1,s2,2048),"READ LONG cooked 2 at 16: status %04X %s\n",st,memcmp(s1,s2,2048)?"DIFFERENT":"same as image");
    if(raw){ memset(h,0,27); h[0]=27; h[2]=128; *(unsigned short*)(h+14)=256; *(unsigned short*)(h+16)=tbseg; *(unsigned short*)(h+18)=1; *(unsigned long*)(h+20)=16; h[24]=1;
      st=devreq(h,27,NULL,0); P("    READ LONG raw 1 at 16: status %04X\n",st); }
    memset(h,0,27); h[0]=27; h[2]=128; *(unsigned short*)(h+14)=256; *(unsigned short*)(h+16)=tbseg; *(unsigned short*)(h+18)=1; *(unsigned long*)(h+20)=16; h[13]=1; /* Red Book (MSF) address */
    *(unsigned long*)(h+20)=(0L<<16)|(2L<<8)|16; /* 00:02.16 = lba 16 */
    st=devreq(h,27,NULL,0); dosmemget(tb+256,2048,s1); CHECK(!(st&0x8000)&&!memcmp(s1,s2,2048),"READ LONG MSF 00:02.16: status %04X %s\n",st,memcmp(s1,s2,2048)?"DIFFERENT":"same");
  }
  /* SEEK (131) */
  { unsigned char h[24]; memset(h,0,24); h[0]=24; h[2]=131; *(unsigned long*)(h+20)=1000; st=devreq(h,24,NULL,0); CHECK(!(st&0x8000),"SEEK 1000: status %04X\n",st); }
  /* PLAY / timing / pause / resume / end, if the disc has audio */
  { unsigned char h[22]; cb[0]=10; ioctl_in(cb,7); if(cb[2]>cb[1]){ unsigned long t2; int st1,st2; unsigned char q1[11],q2[11];
      cb[0]=11; cb[1]=2; ioctl_in(cb,7); t2=*(unsigned long*)(cb+2);
      memset(h,0,22); h[0]=22; h[2]=132; h[13]=1; memcpy(h+14,&t2,4); *(unsigned long*)(h+18)=75*3; st=devreq(h,22,NULL,0);
      CHECK(st==0x0300,"PLAY track 2 for 3 s: status %04X (want 0300 = done + busy)\n",st);
      cb[0]=12; st1=ioctl_in(cb,11); memcpy(q1,cb,11); delay_ms(1000); cb[0]=12; st2=ioctl_in(cb,11); memcpy(q2,cb,11);
      CHECK(q2[2]==0x02&&(q2[5]>q1[5]||q2[4]>q1[4])&&(st2&0x200),"Q-channel moves while playing: track %02X %02X:%02X.%02X -> %02X:%02X.%02X, busy %s\n",q2[2],q1[4],q1[5],q1[6],q2[4],q2[5],q2[6],st2&0x200?"yes":"NO");
      memset(h,0,13); h[0]=13; h[2]=133; st=devreq(h,13,NULL,0); CHECK(!(st&0x8200)||st==0x0100,"STOP (pause): status %04X\n",st);
      cb[0]=15; st=ioctl_in(cb,11); CHECK(!(st&0x200)&&(cb[1]&1),"audio status after STOP: paused %d, busy %s\n",cb[1]&1,st&0x200?"YES":"no");
      cb[0]=12; ioctl_in(cb,11); memcpy(q1,cb,11); delay_ms(700); cb[0]=12; ioctl_in(cb,11);
      CHECK(cb[5]==q1[5]&&cb[6]==q1[6],"position stays while paused: %02X.%02X / %02X.%02X\n",q1[5],q1[6],cb[5],cb[6]);
      memset(h,0,13); h[0]=13; h[2]=136; st=devreq(h,13,NULL,0); CHECK(st==0x0300,"RESUME: status %04X (want 0300)\n",st);
      delay_ms(2600); cb[0]=15; st=ioctl_in(cb,11); CHECK(!(st&0x200),"track finished on time (3 s + pause): busy %s\n",st&0x200?"STILL":"no");
      memset(h,0,13); h[0]=13; h[2]=136; st=devreq(h,13,NULL,0); CHECK(st&0x8000,"RESUME after the end is refused: status %04X\n",st);
      cb[0]=11; cb[1]=1; ioctl_in(cb,7); t2=*(unsigned long*)(cb+2);
      memset(h,0,22); h[0]=22; h[2]=132; h[13]=1; memcpy(h+14,&t2,4); *(unsigned long*)(h+18)=75; st=devreq(h,22,NULL,0);
      CHECK(st&0x8000,"PLAY on the data track is refused: status %04X\n",st);
      memset(h,0,13); h[0]=13; h[2]=133; st=devreq(h,13,NULL,0); CHECK(st==0x0100,"STOP when stopped: status %04X\n",st); }
    else { memset(h,0,22); h[0]=22; h[2]=132; h[13]=0; *(unsigned long*)(h+14)=0; *(unsigned long*)(h+18)=75; st=devreq(h,22,NULL,0);
      CHECK(st&0x8000,"PLAY on a data-only disc is refused: status %04X\n",st); } }
  /* get directory entry 150Fh */
  { char nm[]="\\README.TXT"; dosmemput(nm,sizeof nm,tb); mx(0x150F); r.h.cl=drv; r.h.ch=1; r.x.es=tbseg; r.x.bx=0; r.x.si=tbseg; r.x.di=512; __dpmi_int(0x2f,&r);
    CHECK(!(r.x.flags&1),"150Fh: directory entry of README.TXT CF=%d AX=%04X\n",r.x.flags&1,r.x.ax); }
  P("RESULT %s (%d bad)\n",bad?"FAIL":"PASS",bad); if(lg) fclose(lg); return bad!=0;
}
