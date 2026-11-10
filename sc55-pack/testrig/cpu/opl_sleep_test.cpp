// opl_sleep_test.cpp - host test of the Nuked OPL3 sleep in sbemu/opl3emu.cpp (beta.81): notes wake it, a keyed-on
// silent voice (output flickering -1/0) goes to sleep. Build from sc55-pack:
//   g++ -O1 -DSBEMU -Isbemu -Ithird_party/nuked-opl3-fast -o /tmp/oplt testrig/cpu/opl_sleep_test.cpp sbemu/opl3emu.cpp sbemu/dbopl.cpp -x c third_party/nuked-opl3-fast/opl3.c
#include "opl3emu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int16_t buf[4096];
static void w(int r, int v) { OPL3EMU_PrimaryWriteIndex(r); OPL3EMU_PrimaryWriteData(v); }
// mimic SBEMU's ISR: only render while active, 512-frame blocks
static long run(int frames, int* act_blocks) {
    long e = 0; *act_blocks = 0;
    for (int f = 0; f < frames; f += 512) {
        if (!OPL3EMU_IsActive()) continue;
        (*act_blocks)++;
        OPL3EMU_GenSamples(buf, 512);
        for (int i = 0; i < 1024; i++) e += abs(buf[i]);
    }
    return e;
}
int main() {
    OPL3EMU_SetCore(1); OPL3EMU_Init(44100);
    int a; long e;
    int regs[][2] = {{0x20,1},{0x40,0x10},{0x60,0xF0},{0x80,0x77},{0x23,1},{0x43,0},{0x63,0xF0},{0x83,0x77},{0xC0,0x30},{0xA0,0x98}};
    for (auto& r : regs) w(r[0], r[1]);
    for (int k = 0; k < 3; k++) {
        w(0xB0, 0x31); e = run(44100, &a); printf("note %d: energy %ld, active blocks %d/87\n", k, e, a);
        w(0xB0, 0x11); e = run(44100 * 5, &a); printf("  after key-off 5 s: energy %ld, active blocks %d, asleep %d\n", e, a, !OPL3EMU_IsActive());
    }
    // a voice left keyed on at zero volume (TL 3F on both operators): must go to sleep too
    w(0x60, 0x00); w(0x63, 0x00); w(0xB0, 0x31);
    e = run(44100 * 6, &a); printf("silent keyed-on voice 6 s: energy %ld, active blocks %d (of 517), asleep %d\n", e, a, !OPL3EMU_IsActive());
    w(0x63, 0xF0); w(0xB0, 0x11); w(0xB0, 0x31);  // new note with an attack on the still keyed-on voice: wakes, sounds
    e = run(44100, &a); printf("volume up: energy %ld, active %d\n", e, a);
    return 0;
}
