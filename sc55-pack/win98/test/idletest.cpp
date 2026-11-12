/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
// idletest.cpp - native test: does the helper engine let its synths sleep when nothing plays,
// and what does each second of audio cost? usage: idletest <romdir> <sc55|mt32|sf2> [options]
#include "../helper/engine.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc < 3) return 1;
    Options o;
    std::string extra;
    for (int i = 3; i < argc; i++) extra += std::string(argv[i]) + " ";
    o.parse(extra);
    o.sbtype = 6;
    if (std::string(argv[2]) == "mt32") o.mt32 = "MT32";
    auto sh = std::make_unique<SC55W_SHARED>();
    memset(sh.get(), 0, sizeof *sh);
    for (int i = 0x30; i <= 0x37; i++) sh->mixer[i] = 0xF8;
    Engine eng;
    const int rate = 22050, blk = rate / 100;
    eng.setup(o, argv[1], rate, 50);
    printf("synth %s %s\n", eng.midi ? eng.midi->describe().c_str() : "none", eng.midi_error.c_str());
    std::vector<int16_t> buf(blk * 2);
    uint32_t now = 1000000;
    for (int sec = 0; sec < 30; sec++) {
        if (sec == 12) {                     // one note at 12 s, off at 13 s
            auto ev = [&](uint32_t t, int b) { SC55W_EVENT& e = sh->ev[sh->ev_head & (EVRING_SIZE - 1)];
                e.time = t; e.type = EV_MIDI; e.a = (uint8_t)b; e.b = e.c = 0; sh->ev_head++; };
            ev(now, 0x90); ev(now, 60); ev(now, 100);
            ev(now + 1000000, 0x80); ev(now + 1000000, 60); ev(now + 1000000, 0);
        }
        auto a = std::chrono::steady_clock::now();
        for (int b = 0; b < 100; b++) { now += 10000; eng.render(sh.get(), now, buf.data(), blk); }
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        printf("%2d s: %6.1f ms cpu, midi %s, opl %s\n", sec, s * 1000, eng.st.midi_idle ? "idle" : "RUN",
               eng.st.opl_idle ? "idle" : "RUN");
    }
}
