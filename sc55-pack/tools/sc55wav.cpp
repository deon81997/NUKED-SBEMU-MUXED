// sc55wav - renders a short demo through the SC-55 emulation to a WAV file.
//
// A stand-alone diagnostic for the DOS port: no TSR, no HDPMI, no sound card.
// It answers the two questions that matter before anything else:
//   1. do my ROMs load and does the emulated firmware boot?
//   2. is this PC fast enough? (the report shows "x real time"; below ~1.5x the
//      live Sound Blaster emulation will stutter)
//
// Usage:  SC55WAV <rom-folder> [out.wav] [/S:romset] [/R:rate] [/G:gain%] [/B:boot-seconds] [/V] [/N]
//   /S:  ROM set name (default auto)       /R:  output rate (default 48000)
//   /G:  gain in percent (default 100)     /B:  seconds of firmware boot (default 4)
//   /V   show the speed graph in VGA 320x200 graphics (DOS only; any key returns)
//   /N   no text graph
//
// The graph plots, for every audio block, how many times faster than real time the
// emulation ran (worst case per column). The line at 1.0x is real time: any dip
// below it means live playback would have glitched at that moment.
//
// GPL-2.0-or-later.
#include "../sc55/sc55synth.h"
#include "../sbemu/dbopl.h"
extern "C" {
#include "opl3.h" // Nuked-OPL3-fast
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__DJGPP__)
#include <time.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <conio.h>
#include <sys/movedata.h>
static double now_seconds() { return (double)uclock() / (double)UCLOCKS_PER_SEC; }
#else
#include <chrono>
static double now_seconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
#endif

#include <stdarg.h>
#include <time.h>

// All console output goes through say(). In the SC55LOG build it is also appended to a
// text file next to the executable and flushed line by line, so the log survives a crash
// or a hang (the last line shows how far it got).
static FILE* g_log = nullptr;
static void say(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_log)
    {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fflush(g_log);
    }
    fflush(stdout);
}

namespace
{
struct Event
{
    double t; // seconds
    std::vector<uint8_t> bytes;
};

std::vector<Event> make_demo()
{
    std::vector<Event> ev;
    auto add = [&](double t, std::initializer_list<uint8_t> b) { ev.push_back({t, std::vector<uint8_t>(b)}); };

    // GS reset, then voices: ch1 piano, ch2 strings, ch3 finger bass, ch10 drums
    add(0.00, {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7});
    add(0.40, {0xC0, 0});
    add(0.40, {0xC1, 48});
    add(0.40, {0xC2, 33});

    // piano: C major scale up, then a held chord
    const uint8_t scale[] = {60, 62, 64, 65, 67, 69, 71, 72};
    double t = 0.60;
    for (uint8_t n : scale)
    {
        add(t, {0x90, n, 100});
        add(t + 0.30, {0x80, n, 0});
        t += 0.35;
    }
    const uint8_t chord[] = {60, 64, 67, 72};
    for (uint8_t n : chord) add(t, {0x90, n, 96});
    for (uint8_t n : chord) add(t + 2.0, {0x80, n, 0});
    // strings pad under the chord
    add(t, {0x91, 48, 80});
    add(t, {0x91, 55, 80});
    add(t + 2.0, {0x81, 48, 0});
    add(t + 2.0, {0x81, 55, 0});
    // bass line
    const uint8_t bass[] = {36, 36, 43, 41};
    for (int i = 0; i < 4; i++)
    {
        add(0.60 + i * 0.7, {0x92, bass[i], 110});
        add(0.60 + i * 0.7 + 0.55, {0x82, bass[i], 0});
    }
    // drums on channel 10: kick, snare, closed hi-hat pattern for 4 beats
    for (int beat = 0; beat < 8; beat++)
    {
        double bt = 0.60 + beat * 0.35;
        add(bt, {0x99, 42, 90});
        if (beat % 4 == 0) add(bt, {0x99, 36, 120});
        if (beat % 4 == 2) add(bt, {0x99, 38, 110});
    }
    std::sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
    return ev;
}

// ---- speed graph -----------------------------------------------------------
// speeds[i] = x real time of block i. Reduced to `cols` columns, keeping the slowest block of each.
std::vector<float> reduce(const std::vector<float>& v, int cols)
{
    std::vector<float> out(cols, 0.0f);
    if (v.empty()) return out;
    for (int c = 0; c < cols; c++)
    {
        size_t a = v.size() * c / cols, b = v.size() * (c + 1) / cols;
        if (b <= a) b = a + 1;
        if (b > v.size()) b = v.size();
        float m = v[a];
        for (size_t i = a; i < b; i++) m = std::min(m, v[i]);
        out[c] = m;
    }
    return out;
}

float graph_max(const std::vector<float>& cols)
{
    float top = 2.0f;
    for (float f : cols) top = std::max(top, f);
    return std::min(top * 1.1f, 64.0f); // keep one slow spike from flattening the rest
}

void text_graph(const std::vector<float>& speeds)
{
    const int W = 72, H = 14;
    std::vector<float> c = reduce(speeds, W);
    const float top = graph_max(c);
    say("\nEmulation speed per audio block (x real time, worst block per column, time ->)\n");
    for (int row = H; row >= 1; row--)
    {
        const float lo = top * (row - 1) / H, hi = top * row / H;
        char line[W + 1];
        for (int x = 0; x < W; x++)
        {
            if (c[x] >= hi) line[x] = '#';
            else if (c[x] > lo) line[x] = '=';
            else if (1.0f > lo && 1.0f <= hi) line[x] = '-'; // the real-time line
            else line[x] = ' ';
        }
        line[W] = 0;
        if (row == H || (1.0f > lo && 1.0f <= hi) || row == 1) say("%5.1fx |%s\n", hi, line);
        else say("       |%s\n", line);
    }
    say("       +%.*s\n", W, "------------------------------------------------------------------------------------");
    say("        '-' = real time (1.0x). Bars under it mean live playback would stutter there.\n");
}

#if defined(__DJGPP__)
void gotoxy_bios(int col, int row)
{
    __dpmi_regs r;
    r.h.ah = 2; r.h.bh = 0; r.h.dh = (unsigned char)row; r.h.dl = (unsigned char)col;
    __dpmi_int(0x10, &r);
}
void print_bios(int col, int row, int color, const char* s)
{
    gotoxy_bios(col, row);
    for (; *s; s++)
    {
        __dpmi_regs r;
        r.h.ah = 0x0E; r.h.al = (unsigned char)*s; r.h.bh = 0; r.h.bl = (unsigned char)color;
        __dpmi_int(0x10, &r);
    }
}
void vga_graph(const std::vector<float>& speeds, double avg, double minspeed)
{
    const int W = 320, H = 200, L = 40, R = 312, T = 24, B = 176; // plot area
    std::vector<float> c = reduce(speeds, R - L);
    const float top = graph_max(c);
    std::vector<unsigned char> fb(W * H, 0);
    auto Y = [&](float v) { int y = B - (int)((B - T) * std::min(v, top) / top); return std::max(T, std::min(B, y)); };
    for (int x = L; x < R; x++) // frame
        fb[B * W + x] = 8;
    for (int y = T; y <= B; y++)
        fb[y * W + L - 1] = 8;
    for (int x = 0; x < R - L; x++)
    {
        const int y = Y(c[x]);
        const unsigned char col = c[x] < 1.0f ? 4 : (c[x] < 1.5f ? 14 : 2); // red / yellow / green
        for (int yy = y; yy < B; yy++) fb[yy * W + L + x] = col;
    }
    const int y1 = Y(1.0f);
    for (int x = L; x < R; x++) fb[y1 * W + x] = 15;
    __dpmi_regs r;
    r.x.ax = 0x0013;
    __dpmi_int(0x10, &r);
    dosmemput(fb.data(), W * H, 0xA0000);
    char t[64];
    print_bios(1, 0, 15, "SC-55 emulation speed (x real time)");
    snprintf(t, sizeof t, "avg %.1fx  slowest %.2fx", avg, minspeed);
    print_bios(1, 1, minspeed < 1.0 ? 12 : 10, t);
    print_bios(0, 3, 15, "white = 1.0x real time");
    print_bios(0, 4, 7, "red below: would glitch");
    snprintf(t, sizeof t, "top %.0fx", top);
    print_bios(0, 22, 7, t);
    print_bios(26, 24, 7, "any key = exit");
    while (!kbhit()) {}
    getch();
    r.x.ax = 0x0003;
    __dpmi_int(0x10, &r);
}
#endif

// ---- FM core benchmark -------------------------------------------------------------------------
// Worst case for an OPL3: all 18 two-operator channels sounding. SBEMU only renders the OPL while
// the SC-55 is idle, so this load does not add to the SC-55 load above - it tells you which
// /FMCORE is affordable for games that use FM music.
struct FmCore
{
    virtual ~FmCore() {}
    virtual void write(uint32_t reg, uint32_t val) = 0;
    virtual void gen(int16_t* pcm, int frames) = 0;
};
struct DboplCore : FmCore
{
    DBOPL::Chip chip;
    DboplCore(uint32_t rate) : chip(true) { chip.Setup(rate); }
    void write(uint32_t reg, uint32_t val) override { chip.WriteReg(reg, (uint8_t)val); }
    void gen(int16_t* pcm, int frames) override { chip.Generate(pcm, (uint32_t)frames); }
};
static opl3_chip g_nuked;
struct NukedCore : FmCore
{
    NukedCore(uint32_t rate) { memset(&g_nuked, 0, sizeof g_nuked); OPL3_Reset(&g_nuked, rate); }
    void write(uint32_t reg, uint32_t val) override { OPL3_WriteRegBuffered(&g_nuked, (uint16_t)reg, (uint8_t)val); }
    void gen(int16_t* pcm, int frames) override { OPL3_GenerateStream(&g_nuked, pcm, (uint32_t)frames); }
};

double fm_bench(FmCore& fm, uint32_t rate, int* peak)
{
    static const int op[9] = {0, 1, 2, 8, 9, 10, 16, 17, 18};
    fm.write(0x105, 1);    // OPL3 mode
    fm.write(0x01, 0x20);
    for (int bank = 0; bank < 2; bank++)
        for (int c = 0; c < 9; c++)
        {
            const uint32_t b = bank ? 0x100 : 0, m = op[c], k = m + 3;
            fm.write(b + 0x20 + m, 0x21); fm.write(b + 0x20 + k, 0x21);
            fm.write(b + 0x40 + m, 0x18); fm.write(b + 0x40 + k, 0x08);
            fm.write(b + 0x60 + m, 0xF2); fm.write(b + 0x60 + k, 0xF2);
            fm.write(b + 0x80 + m, 0x77); fm.write(b + 0x80 + k, 0x77);
            fm.write(b + 0xC0 + c, 0x30 | 0x04 | 0x00); // L+R, some feedback, FM
            fm.write(b + 0xA0 + c, 0x40 + 9 * c + 5 * bank);
            fm.write(b + 0xB0 + c, 0x20 | (4 << 2) | 1); // key on
        }
    std::vector<int16_t> buf(1024 * 2);
    const int total = (int)rate * 3;
    double t = 0;
    *peak = 0;
    for (int done = 0; done < total; done += 1024)
    {
        double a = now_seconds();
        fm.gen(buf.data(), 1024);
        t += now_seconds() - a;
        for (int i = 0; i < 2048; i++) *peak = std::max(*peak, std::abs((int)buf[i]));
    }
    return t > 0 ? 3.0 / t : 999.0;
}

void put_u32(FILE* f, uint32_t v)
{
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}
void put_u16(FILE* f, uint16_t v)
{
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}
void wav_header(FILE* f, uint32_t rate, uint32_t frames)
{
    fwrite("RIFF", 1, 4, f);
    put_u32(f, 36 + frames * 4);
    fwrite("WAVEfmt ", 1, 8, f);
    put_u32(f, 16);
    put_u16(f, 1);
    put_u16(f, 2);
    put_u32(f, rate);
    put_u32(f, rate * 4);
    put_u16(f, 4);
    put_u16(f, 16);
    fwrite("data", 1, 4, f);
    put_u32(f, frames * 4);
}
bool opt(const char* arg, const char* name, const char** value)
{
    size_t n = strlen(name);
    if (strncmp(arg, name, n) != 0) return false;
    *value = arg + n;
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    const char* romdir = nullptr;
    const char* out = "sc55demo.wav";
    const char* romset = "auto";
    uint32_t rate = 48000;
    int gain = 100, boot = 0, lite_voices = 0;
    bool lite_nofx = false;
    bool want_vga = false, want_text = true;

    int positional = 0;
    for (int i = 1; i < argc; i++)
    {
        const char* v = nullptr;
        if (opt(argv[i], "/S:", &v) || opt(argv[i], "/s:", &v)) romset = v;
        else if (opt(argv[i], "/R:", &v) || opt(argv[i], "/r:", &v)) rate = (uint32_t)atoi(v);
        else if (opt(argv[i], "/G:", &v) || opt(argv[i], "/g:", &v)) gain = atoi(v);
        else if (opt(argv[i], "/B:", &v) || opt(argv[i], "/b:", &v)) boot = atoi(v);
        else if (opt(argv[i], "/LV:", &v) || opt(argv[i], "/lv:", &v)) lite_voices = atoi(v);
        else if (!strcmp(argv[i], "/NOFX") || !strcmp(argv[i], "/nofx")) lite_nofx = true;
        else if (!strcmp(argv[i], "/V") || !strcmp(argv[i], "/v")) want_vga = true;
        else if (!strcmp(argv[i], "/N") || !strcmp(argv[i], "/n")) want_text = false;
        else if (argv[i][0] == '/' || argv[i][0] == '-')
        {
            say("Unknown option %s\n", argv[i]);
            return 2;
        }
        else if (positional == 0) { romdir = argv[i]; positional++; }
        else if (positional == 1) { out = argv[i]; positional++; }
    }
    if (!romdir || rate < 8000 || rate > 192000)
    {
        say("%s\n", "SC55WAV <rom-folder> [out.wav] [/S:romset] [/R:rate] [/G:gain%] [/B:boot-seconds] [/V] [/N]\n"
             "  /V = VGA speed graph, /N = no text graph\n"
             "Renders a short demo through the Roland SC-55 emulation (your own ROMs) to a WAV file\n"
             "and reports how fast this PC runs it.");
        return 2;
    }

#ifdef SC55LOG
    {
        std::string path = argv[0] ? argv[0] : "";
        size_t cut = path.find_last_of("/\\");
        path = (cut == std::string::npos ? std::string() : path.substr(0, cut + 1)) + "SC55LOG.TXT";
        g_log = fopen(path.c_str(), "w");
        if (!g_log) g_log = fopen("SC55LOG.TXT", "w"); // exe folder read-only (CD, network): fall back to the current folder
        time_t now = time(nullptr);
        say("SC55LOG - SC-55 emulation benchmark log%s", g_log ? "" : " (COULD NOT CREATE LOG FILE)");
        if (g_log) say("\nLog file: %s\n", path.c_str());
        say("Date: %s", ctime(&now));
        say("Command line:");
        for (int i = 1; i < argc; i++) say(" %s", argv[i]);
        say("\nrate %lu Hz, gain %d%%\n\n", (unsigned long)rate, gain);
    }
#endif
    SC55_Config cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.romdir = romdir;
    cfg.romset = romset;
    cfg.gain_percent = gain;
    cfg.boot_seconds = boot;
#ifdef SC55LOG
    cfg.verbose = 0; // SC55_Init prints straight to the console; the log gets its own progress lines instead
    say("Loading ROMs from %s, set '%s', boot %d s ... (if the log ends here the load or boot hung)\n", romdir, romset,
        boot > 0 ? boot : 4);
#else
    cfg.verbose = 1;
#endif

    double t0 = now_seconds();
    if (!SC55_Init(&cfg))
    {
        say("\nFAILED: %s\n", SC55_LastError());
        return 1;
    }
    SC55_SetLite(lite_nofx ? 0 : 1, lite_voices);
    if (lite_nofx || lite_voices) say("Lite mode: voices %d, reverb/chorus %s\n", lite_voices ? lite_voices : 24, lite_nofx ? "off" : "on");
    double t1 = now_seconds();
    const int boot_s = boot > 0 ? boot : 4;
    say("Model %s, native rate %lu Hz. Start-up took %.2f s (%.1fx real time for the %d s boot).\n",
           SC55_ModelName(), (unsigned long)SC55_NativeRate(), t1 - t0, boot_s / (t1 - t0), boot_s);

    FILE* f = fopen(out, "wb");
    if (!f)
    {
        say("Cannot create %s\n", out);
        return 1;
    }
    wav_header(f, rate, 0);

    std::vector<Event> events = make_demo();
    const double length = events.back().t + 3.0; // let the release tails ring out
    const uint32_t total = (uint32_t)(length * rate);
    std::vector<int16_t> block(1024 * 2);
    size_t next_event = 0;
    uint32_t done = 0;
    int peak = 0;
    double render_time = 0;
    std::vector<float> speeds;

    while (done < total)
    {
        const uint32_t n = std::min<uint32_t>(1024, total - done);
        const double block_end = (double)(done + n) / rate;
        while (next_event < events.size() && events[next_event].t < block_end)
        {
            for (uint8_t b : events[next_event].bytes) SC55_PostByte(b);
            next_event++;
        }
        double a = now_seconds();
        SC55_Render(block.data(), (int)n, rate, 0);
        const double dt = now_seconds() - a;
        render_time += dt;
        if (dt > 0) speeds.push_back((float)(((double)n / rate) / dt));
        else speeds.push_back(64.0f); // below timer resolution: effectively instant
        for (uint32_t i = 0; i < n * 2; i++) peak = std::max(peak, std::abs((int)block[i]));
        fwrite(block.data(), 4, n, f);
        done += n;
    }
    fseek(f, 0, SEEK_SET);
    wav_header(f, rate, done);
    fclose(f);

    const double speed = render_time > 0 ? length / render_time : 0;
    say("Wrote %s: %.1f s of audio at %lu Hz, peak %d (%.1f dBFS).\n", out, length, (unsigned long)rate, peak,
           peak ? 20.0 * __builtin_log10((double)peak / 32768.0) : -99.0);
    say("Rendering took %.2f s = %.2fx real time.\n", render_time, speed);
    if (peak < 100)
        say("%s\n", "WARNING: the output is silent. The ROM set may not match the firmware, or the boot time is too short (try /B:6).");
    else if (speed < 1.5)
        say("%s\n", "WARNING: this PC is too slow for live playback (needs about 1.5x or better).");
    else
        say("%s\n", "This PC is fast enough for live playback.");
    if (!speeds.empty())
    {
        std::vector<float> sorted(speeds);
        std::sort(sorted.begin(), sorted.end());
        size_t under = 0;
        for (float f : sorted) if (f < 1.0f) under++;
        say("Benchmark: %.0f audio frames/s sustained (needs %lu), blocks: slowest %.2fx, 1%% low %.2fx, "
               "median %.2fx, %lu of %lu below real time.\n",
               speed * rate, (unsigned long)rate, sorted.front(), sorted[sorted.size() / 100],
               sorted[sorted.size() / 2], (unsigned long)under, (unsigned long)sorted.size());
    }
    if (want_text) text_graph(speeds);

    {
        say("\nFM (OPL3) core speed, 18 channels sounding at %lu Hz:\n", (unsigned long)rate);
        int pk1 = 0, pk2 = 0;
        DboplCore d(rate);
        const double s1 = fm_bench(d, rate, &pk1);
        NukedCore n(rate);
        const double s2 = fm_bench(n, rate, &pk2);
        say("  DOSBox DBOPL (/FMCORE:0): %.1fx real time%s\n", s1, pk1 < 100 ? "  (silent?)" : "");
        say("  Nuked OPL3  (/FMCORE:1): %.1fx real time%s\n", s2, pk2 < 100 ? "  (silent?)" : "");
        say("  SBEMU skips the OPL while the SC-55 is playing, so this does not add to the SC-55 load.\n");
        say("  %s\n", s2 >= 3.0 ? "Nuked OPL3 is affordable on this PC." : "Stay with DBOPL (/FMCORE:0) on this PC.");
    }
#if defined(__DJGPP__)
    if (want_vga && !speeds.empty())
    {
        float mn = *std::min_element(speeds.begin(), speeds.end());
        vga_graph(speeds, speed, mn);
    }
#else
    if (want_vga) say("%s\n", "(/V needs the DOS build)");
#endif
#ifdef SC55LOG
    if (g_log)
    {
        say("\nEnd of log.\n");
        fclose(g_log);
        g_log = nullptr;
        puts("Report saved to SC55LOG.TXT next to this program.");
    }
#endif
    SC55_Shutdown();
    return 0;
}
