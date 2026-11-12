/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
// enginetest.cpp - native test of the helper engine with a simulated VxD.
// Feeds timestamped MIDI / OPL events and an SB PCM stream, writes a WAV.
// usage: enginetest <romdir> <synth: sc55|mt32|none> <out.wav> [extra options]
#include "../helper/engine.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <chrono>

static void ev(SC55W_SHARED* s, uint32_t t, uint8_t type, uint8_t a, uint8_t b = 0, uint8_t c = 0)
{
    SC55W_EVENT& e = s->ev[s->ev_head & (EVRING_SIZE - 1)];
    e.time = t; e.type = type; e.a = a; e.b = b; e.c = c;
    s->ev_head++;
}
static void midi(SC55W_SHARED* s, uint32_t t, std::initializer_list<int> bytes)
{
    for (int b : bytes) ev(s, t, EV_MIDI, (uint8_t)b);
}

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage\n"); return 1; }
    std::string romdir = argv[1], syn = argv[2], outp = argv[3];
    Options o;
    std::string extra;
    for (int i = 4; i < argc; i++) extra += std::string(argv[i]) + " ";
    o.parse(extra);
    o.sbtype = 6;
    if (syn == "mt32") o.mt32 = "MT32";
    if (syn == "none") o.vmpu = 0;

    auto sh = std::make_unique<SC55W_SHARED>();
    memset(sh.get(), 0, sizeof *sh);
    // SB16 mixer at its reset levels
    for (int i = 0x30; i <= 0x37; i++) sh->mixer[i] = 0xF8;

    Engine eng;
    auto t0 = std::chrono::steady_clock::now();
    eng.setup(o, romdir, 44100, 50);
    double setup_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("synth: %s (setup %.2fs)%s%s\n", eng.midi ? eng.midi->describe().c_str() : "none", setup_s,
           eng.midi_error.empty() ? "" : " ERROR: ", eng.midi_error.c_str());

    const uint32_t base = 1000000;
    // MIDI: GS reset at 0.1 s, piano C4-E4-G4-C5 one per 0.5 s from 1.0 s (needs ~0.5 s to boot)
    midi(sh.get(), base + 100000, {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7});
    int notes[4] = {60, 64, 67, 72};
    for (int i = 0; i < 4; i++) {
        uint32_t t = base + 1000000 + i * 500000;
        midi(sh.get(), t, {0x90, notes[i], 100});
        midi(sh.get(), t + 400000, {0x80, notes[i], 0});
    }
    // OPL: one 440 Hz-ish tone on channel 0 from 3.5 s to 4.5 s
    uint32_t to = base + 3500000;
    auto opl = [&](uint32_t t, int r, int v) { ev(sh.get(), t, EV_OPL, (uint8_t)r, (uint8_t)v, 0); };
    opl(to, 0x20, 0x01); opl(to, 0x40, 0x10); opl(to, 0x60, 0xF0); opl(to, 0x80, 0x77);
    opl(to, 0x23, 0x01); opl(to, 0x43, 0x00); opl(to, 0x63, 0xF0); opl(to, 0x83, 0x77);
    opl(to, 0xA0, 0x41); opl(to, 0xC0, 0x30); opl(to, 0xB0, 0x32);
    opl(base + 4500000, 0xB0, 0x12);
    // SB PCM: 22050 Hz, 1 kHz sine from 5.0 s to 6.0 s, produced in real-time-sized chunks below
    sh->fmt[0].frame = 0; sh->fmt[0].rate = 22050; sh->fmt_head = 1;

    const int rate = 44100, chunk = 441;      // 10 ms buffers
    const double secs = argc > 0 && getenv("SECS") ? atof(getenv("SECS")) : 7.0;
    FILE* f = fopen(outp.c_str(), "wb");
    fwrite("RIFF", 1, 44, f);
    std::vector<int16_t> buf(chunk * 2);
    unsigned total = 0, pcm_written = 0;
    double render_s = 0;
    for (int n = 0; n * chunk < secs * rate; n++) {
        uint32_t now = base + (uint32_t)((uint64_t)n * chunk * 1000000 / rate);
        // VxD side: emit PCM frames due by now (sine 5-6 s)
        while (true) {
            double t = (double)pcm_written / 22050.0;
            if (base + t * 1e6 > now) break;
            double tt = t;
            int16_t v = (tt >= 5.0 && tt < 6.0) ? (int16_t)(12000 * sin(2 * M_PI * 1000 * tt)) : 0;
            uint32_t i = (sh->pcm_head & (PCMRING_SIZE - 1)) * 2;
            sh->pcm[i] = v; sh->pcm[i + 1] = v; sh->pcm_head++;
            pcm_written++;
        }
        auto a = std::chrono::steady_clock::now();
        eng.render(sh.get(), now, buf.data(), chunk);
        render_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        fwrite(buf.data(), 4, chunk, f);
        total += chunk;
    }
    unsigned hdr[11];
    memcpy(&hdr[0], "RIFF", 4); hdr[1] = 36 + total * 4; memcpy(&hdr[2], "WAVE", 4);
    memcpy(&hdr[3], "fmt ", 4); hdr[4] = 16; hdr[5] = 1 | (2 << 16); hdr[6] = rate;
    hdr[7] = rate * 4; hdr[8] = 4 | (16 << 16); memcpy(&hdr[9], "data", 4); hdr[10] = total * 4;
    fseek(f, 0, SEEK_SET); fwrite(hdr, 4, 11, f); fclose(f);
    printf("rendered %.1fs of audio in %.2fs (%.0f%% of real time)\n", secs, render_s, 100 * render_s / secs);
    printf("midi bytes %u, opl writes %u, pcm frames %u, late %u, resyncs %u\n", eng.st.midi_bytes,
           eng.st.opl_writes, eng.st.pcm_frames, eng.st.late_events, eng.st.resyncs);
    printf("idle: midi %d opl %d\n", (int)eng.st.midi_idle, (int)eng.st.opl_idle);
    return 0;
}
