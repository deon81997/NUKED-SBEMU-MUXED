// opts.h - SBEMU-style options shared with the DOS pack (C:\SC55\SBOPTS.TXT).
// The Windows helper reads and writes the same file, so settings made in
// DOS (SETTINGS.BAT / SBSET) and in Windows stay the same.
#pragma once
#include <string>

struct Options {
    // Sound Blaster
    int sb_port   = 0x220;   // /A
    int irq       = 7;       // /I
    int dma8      = 1;       // /D
    int dma16     = 5;       // /H
    int sbtype    = 5;       // /T 1-6
    int opl       = 1;       // /OPL
    int fmcore    = 0;       // /FMCORE 0 = DOSBox, 1 = Nuked OPL3
    int fixtc     = 0;       // /FIXTC
    int vol       = 80;      // /VOL master 0-100
    int sbvol     = 100;     // /VSBVOL 0-100
    int oplvol    = 100;     // /VOPLVOL 0-100
    int midivol   = 100;     // /VMIDIVOL 0-400
    int rate      = 22050;   // /K internal rate
    // MPU-401 / MIDI
    int mpu_port  = 0x330;   // /P
    int vmpu      = 1;       // /VMPU
    std::string scrom = "ROMS";   // /VSCROM
    std::string scset = "auto";   // /VSCSET
    int scgain    = 100;     // /VSCGAIN 0-1600
    int scvoices  = 0;       // /VSCVOICES 0 or 4-24
    int scfx      = 1;       // /VSCFX
    int scauto    = 1;       // /VSCAUTO
    int scahead   = 50;      // /VSCAHEAD ms
    int vtimer    = 1;       // /VTIMER (DOS only, kept for round-trip)
    std::string sf;          // /VSF SoundFont file
    int sfvoices  = 64;      // /VSFVOICES
    std::string mt32;        // /VMT32 AUTO|MT32|CM32L
    int sfrev     = 0;       // /VSFREV
    int sfroom    = 60;      // /VSFROOM
    int sfdamp    = 50;      // /VSFDAMP
    int sfcho     = 0;       // /VSFCHO
    int sfcrate   = 8;       // /VSFCRATE tenths of Hz
    int sfcdepth  = 50;      // /VSFCDEPTH
    int sfsend    = 1;       // /VSFSEND
    // DOS-only switches we keep so the file round-trips unchanged
    std::string passthru;

    enum Synth { SYN_SC55, SYN_SF2, SYN_MT32 };
    Synth synth() const {
        if (!sf.empty()) return SYN_SF2;
        if (!mt32.empty()) return SYN_MT32;
        return SYN_SC55;
    }

    // Like SBEMU: port, IRQ, DMA, type and MPU port not given in SBOPTS.TXT
    // come from the BLASTER variable. Call before parse().
    void apply_blaster(const char* blaster);
    // parse a whole SBOPTS.TXT / command line; unknown switches go to passthru
    void parse(const std::string& text);
    // the shortest option line that reproduces these settings
    std::string format() const;
};

bool load_text(const std::string& path, std::string& out);
bool save_text(const std::string& path, const std::string& text);
