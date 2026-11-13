// sc55render - plays a timed MIDI byte list through SBEMU's SC-55 path (sc55synth + sc55engine: the same
// resampler / low-pass / gain as in DOS) into a WAV, as fast as the host can, so the sound can be judged without a
// slow emulated PC (QEMU cannot render most ROM sets in real time and pads the output with silence).
//
//   sc55render ROMDIR ROMSET RATE LPF(0/1) BYTES.TXT OUT.WAV [TAIL_S]
//   BYTES.TXT: "microseconds hexbyte" per line - exactly what MIDPLAY's host build prints (tools/midplay.c -DHOSTTEST).
// Build: see tests/Makefile.host (target out/sc55render). GPL-2.0-or-later.
#include "sc55synth.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 7) { fprintf(stderr, "usage: sc55render ROMDIR ROMSET RATE LPF BYTES.TXT OUT.WAV [TAIL_S]\n"); return 2; }
    const unsigned rate = (unsigned)atoi(argv[3]); const int lpf = atoi(argv[4]); const double tail = argc > 7 ? atof(argv[7]) : 2.0;
    std::vector<std::pair<unsigned long long, int>> ev;
    { FILE* f = fopen(argv[5], "r"); if (!f) { perror(argv[5]); return 1; }
      unsigned long long t; unsigned b; char line[128];
      while (fgets(line, sizeof line, f)) if (line[0] != '#' && sscanf(line, "%llu %x", &t, &b) == 2) ev.push_back({t, (int)b});
      fclose(f); }
    SC55_Config cfg = {}; cfg.romdir = argv[1]; cfg.romset = argv[2]; cfg.gain_percent = 100; cfg.verbose = 0;
    if (!SC55_Init(&cfg)) { fprintf(stderr, "SC55_Init: %s\n", SC55_LastError()); return 1; }
    SC55_SetLowpass(lpf);
    const unsigned long long end_us = (ev.empty() ? 0 : ev.back().first) + (unsigned long long)(tail * 1e6);
    const int block = 128; std::vector<int16_t> pcm; size_t k = 0; unsigned long long frame = 0; int16_t buf[block * 2];
    while (frame * 1000000ULL / rate < end_us)
    {
        const unsigned long long now = frame * 1000000ULL / rate;
        while (k < ev.size() && ev[k].first <= now) { while (!SC55_PostByte((uint8_t)ev[k].second)) SC55_Render(buf, 1, rate, 0); k++; }
        SC55_Render(buf, block, rate, 0);
        pcm.insert(pcm.end(), buf, buf + block * 2); frame += block;
    }
    FILE* o = fopen(argv[6], "wb"); if (!o) { perror(argv[6]); return 1; }
    auto w32 = [&](unsigned v) { fwrite(&v, 4, 1, o); }; auto w16 = [&](unsigned short v) { fwrite(&v, 2, 1, o); };
    const unsigned bytes = (unsigned)(pcm.size() * 2);
    fwrite("RIFF", 1, 4, o); w32(36 + bytes); fwrite("WAVEfmt ", 1, 8, o); w32(16); w16(1); w16(2); w32(rate); w32(rate * 4); w16(4); w16(16);
    fwrite("data", 1, 4, o); w32(bytes); fwrite(pcm.data(), 2, pcm.size(), o); fclose(o);
    printf("%s %s %u Hz lpf %d: %.1f s\n", argv[2], SC55_ModelName(), rate, lpf, (double)frame / rate);
    SC55_Shutdown();
    return 0;
}
