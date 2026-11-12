/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
// host bench for our Nuked-SC55 fork: render a MIDI file through sc55synth's C API, time it, FNV-hash every sample
#include "sc55synth.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <algorithm>
struct Ev { double t; std::vector<uint8_t> b; };
static uint32_t rd32(const uint8_t* p){return p[0]<<24|p[1]<<16|p[2]<<8|p[3];}
static std::vector<Ev> load(const char* fn){
  FILE* f=fopen(fn,"rb"); std::vector<uint8_t> d; int c; while((c=fgetc(f))!=EOF) d.push_back(c); fclose(f);
  int ntr=d[10]<<8|d[11], div=d[12]<<8|d[13]; size_t p=14; struct Raw{uint64_t tick; std::vector<uint8_t> b; int tempo;}; std::vector<Raw> raw;
  for(int t=0;t<ntr && p+8<=d.size();t++){ uint32_t len=rd32(&d[p+4]); size_t q=p+8,e=q+len; uint64_t tick=0; uint8_t rs=0;
    while(q<e){ uint32_t dt=0; uint8_t x; do{x=d[q++]; dt=dt<<7|(x&127);}while(x&128); tick+=dt; uint8_t s=d[q];
      if(s==0xFF){ uint8_t ty=d[q+1]; q+=2; uint32_t l=0; do{x=d[q++]; l=l<<7|(x&127);}while(x&128);
        if(ty==0x51) raw.push_back({tick,{},(int)(d[q]<<16|d[q+1]<<8|d[q+2])}); q+=l; if(ty==0x2F) break; continue; }
      if(s==0xF0||s==0xF7){ q++; uint32_t l=0; do{x=d[q++]; l=l<<7|(x&127);}while(x&128); std::vector<uint8_t> b; if(s==0xF0) b.push_back(0xF0); b.insert(b.end(),&d[q],&d[q+l]); raw.push_back({tick,b,0}); q+=l; continue; }
      if(s&0x80){rs=s;q++;} int n=((rs&0xF0)==0xC0||(rs&0xF0)==0xD0)?1:2; std::vector<uint8_t> b{rs}; for(int i=0;i<n;i++) b.push_back(d[q++]); raw.push_back({tick,b,0}); }
    p=e; }
  std::stable_sort(raw.begin(),raw.end(),[](const Raw&a,const Raw&b){return a.tick<b.tick;});
  std::vector<Ev> ev; double us=0; uint64_t last=0; int tempo=500000;
  for(auto& r:raw){ us+=(double)(r.tick-last)*tempo/div; last=r.tick; if(r.tempo){tempo=r.tempo; continue;} ev.push_back({us/1e6,r.b}); }
  return ev; }
int main(int argc,char**argv){
  if(argc<3){fprintf(stderr,"bench ROMDIR file.mid [seconds] [romset]\n");return 1;}
  double secs=argc>3?atof(argv[3]):30; SC55_Config cfg={argv[1],argc>4?argv[4]:"auto",100,0,0};
  if(!SC55_Init(&cfg)){fprintf(stderr,"init: %s\n",SC55_LastError());return 1;}
  auto ev=load(argv[2]); uint32_t rate=SC55_NativeRate(); const int B=64; int16_t pcm[B*2]; size_t ei=0; uint32_t h=2166136261u; double t=0; long frames=0;
  auto t0=std::chrono::steady_clock::now();
  while(t<secs){ while(ei<ev.size()&&ev[ei].t<=t){ for(uint8_t x:ev[ei].b) while(!SC55_PostByte(x)) { SC55_Render(pcm,B,rate,0); } ei++; }
    SC55_Render(pcm,B,rate,0); for(int i=0;i<B*2;i++){h^=(uint16_t)pcm[i]; h*=16777619u;} frames+=B; t=(double)frames/rate; }
  double el=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
  printf("%s %s: %.1f s of music in %.2f s = %.2fx real time, hash %08x\n",SC55_ModelName(),argv[2],secs,el,secs/el,h);
}
