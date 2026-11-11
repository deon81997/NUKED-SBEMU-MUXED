// synths.cpp - SC-55 (Nuked-SC55, J.C. Moyer's fork), MT-32 (Munt),
// SoundFont (TinySoundFont) and OPL (DOSBox DBOPL / Nuked OPL3) wrappers.
#include "synths.h"
#include "romscan.h"
#include "emu.h"
#include "lcd.h"
#include "pcm.h"
#include "rom_io.h"
#include "mt32emu.h"
#define TSF_IMPLEMENTATION
#include "tsf.h"
#include "dbopl.h"
extern "C" {
#include "opl3.h"
}
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ======================================================================
// MIDI helpers
// ======================================================================
static int data_len(uint8_t st)
{
    if (st < 0xF0) {
        uint8_t t = st & 0xF0;
        return (t == 0xC0 || t == 0xD0) ? 1 : 2;
    }
    switch (st) { case 0xF1: case 0xF3: return 1; case 0xF2: return 2; default: return 0; }
}

bool MidiParser::feed(uint8_t b, int& kind, uint8_t msg[3], int& len)
{
    if (b >= 0xF8) { kind = 0; msg[0] = b; len = 1; return true; }   // real-time
    if (in_sysex) {
        if (b == 0xF7) { sysex.push_back(b); in_sysex = false; kind = 1; return true; }
        if (b < 0x80) { if (sysex.size() < 65536) sysex.push_back(b); return false; }
        in_sysex = false;                                               // aborted by a status byte
    }
    if (b == 0xF0) { in_sysex = true; sysex.clear(); sysex.push_back(b); return false; }
    if (b >= 0x80) {
        status = b; have = 0; need = data_len(b);
        if (b >= 0xF0 && need == 0) { kind = 0; msg[0] = b; len = 1; status = 0; return true; }
        return false;
    }
    if (!status) return false;
    buf[have++] = b;
    if (have < need) return false;
    msg[0] = status; msg[1] = buf[0]; msg[2] = need > 1 ? buf[1] : 0;
    len = need + 1; kind = 0; have = 0;
    if (status >= 0xF0) status = 0;          // no running status for system common
    return true;
}

void NoteLimiter::on_message(const uint8_t* m, int len, std::vector<uint8_t>& pre)
{
    if (!limit || len < 3) return;
    uint8_t t = m[0] & 0xF0, ch = m[0] & 0x0F;
    if (ch == 9) return;
    if (t == 0x90 && m[2]) {
        int n = 0;
        for (auto& h : held) if (h.ch != 9) n++;
        while (n >= limit && !held.empty()) {
            Note o = held.front();
            held.erase(held.begin());
            pre.push_back((uint8_t)(0x80 | o.ch)); pre.push_back(o.key); pre.push_back(64);
            n--;
        }
        held.push_back({ch, m[1]});
    } else if (t == 0x80 || (t == 0x90 && !m[2])) {
        for (size_t i = 0; i < held.size(); i++)
            if (held[i].ch == ch && held[i].key == m[1]) { held.erase(held.begin() + i); break; }
    } else if (t == 0xB0 && (m[1] == 123 || m[1] == 120 || m[1] == 121)) {
        held.erase(std::remove_if(held.begin(), held.end(), [&](const Note& n) { return n.ch == ch; }), held.end());
    }
}

// GS: master reverb level / chorus level (dry when 0)
static void gs_fx_sysex(std::vector<uint8_t>& out, uint8_t addr_lo, uint8_t val)
{
    uint8_t s = (uint8_t)(0x40 + 0x01 + addr_lo + val);
    uint8_t chk = (uint8_t)((128 - (s & 0x7F)) & 0x7F);
    uint8_t m[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x01, addr_lo, val, chk, 0xF7};
    out.insert(out.end(), m, m + sizeof m);
}

static bool is_reset_sysex(const std::vector<uint8_t>& s)
{
    static const uint8_t gm[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    static const uint8_t gs[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
    return (s.size() == sizeof gm && !memcmp(s.data(), gm, sizeof gm)) ||
           (s.size() == sizeof gs && !memcmp(s.data(), gs, sizeof gs));
}

// ======================================================================
// SC-55
// ======================================================================
// the core only keeps its LCD contents while a backend is attached; this one draws nothing
// (the DOS port does the same, sc55synth.cpp), the contents go to DOS programs as the B0h panel
namespace { struct NullLcd final : LCD_Backend {
    bool Start(const lcd_t&) override { return true; }
    void Stop() override {}
    void Render() override {}
}; NullLcd g_null_lcd; }
class Sc55 : public MidiSynth {
public:
    Emulator emu;
    std::string set_name;
    std::vector<float> fifo;      // stereo frames from the emulator
    size_t rd = 0, wr = 0;        // frame indices (mod size)
    Resampler rs;
    double step = 1.0;
    MidiParser parser;
    NoteLimiter lim;
    bool fx = true;
    std::vector<uint8_t> tmp;

    static void on_sample(void* ud, const AudioFrame<int32_t>& f)
    {
        Sc55* s = (Sc55*)ud;
        size_t n = s->fifo.size() / 2;
        if (s->wr - s->rd >= n) return;
        size_t i = (s->wr % n) * 2;
        const float k = 1.0f / (32768.0f * 32768.0f);
        s->fifo[i] = (float)f.left * k;
        s->fifo[i + 1] = (float)f.right * k;
        s->wr++;
    }

    void post(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) emu.PostMIDI(p[i]); }
    int lcd(uint8_t* data80, uint8_t* cg64) override
    {
        lcd_t& l = emu.GetLCD();
        memcpy(data80, l.LCD_Data, 80);
        memcpy(cg64, l.LCD_CG, 64);
        return 1;
    }

    void midi_byte(uint8_t b) override
    {
        int kind, len;
        uint8_t m[3];
        if (!parser.feed(b, kind, m, len)) return;
        tmp.clear();
        if (kind == 1) {
            post(parser.sysex.data(), parser.sysex.size());
            if (!fx && is_reset_sysex(parser.sysex)) { gs_fx_sysex(tmp, 0x33, 0); gs_fx_sysex(tmp, 0x3A, 0); }
            if (is_reset_sysex(parser.sysex)) lim.held.clear();
            post(tmp.data(), tmp.size());
            return;
        }
        lim.on_message(m, len, tmp);
        if (!fx && len == 3 && (m[0] & 0xF0) == 0xB0 && (m[1] == 91 || m[1] == 93)) m[2] = 0;
        post(tmp.data(), tmp.size());
        post(m, (size_t)len);
    }

    void set_fx(bool on) override
    {
        if (on == fx) return;
        fx = on;
        tmp.clear();
        gs_fx_sysex(tmp, 0x33, on ? 0x40 : 0);
        gs_fx_sysex(tmp, 0x3A, on ? 0x40 : 0);
        post(tmp.data(), tmp.size());
    }

    void all_notes_off() override
    {
        for (int c = 0; c < 16; c++) { uint8_t m[3] = {(uint8_t)(0xB0 | c), 123, 0}; post(m, 3); }
        lim.held.clear();
    }

    void render(float* lr, int frames) override
    {
        rs.run(lr, frames, step, [this](float* f) {
            size_t n = fifo.size() / 2;
            while (wr == rd) emu.Step();
            size_t i = (rd % n) * 2;
            f[0] = fifo[i] * gain; f[1] = fifo[i + 1] * gain;
            rd++;
        });
    }

    std::string describe() const override { return "SC-55 (" + set_name + ")"; }
};

MidiSynth* create_sc55(const Options& o, const std::string& romdir, int out_rate, std::string& err)
{
    auto files = scan_roms(romdir);
    Sc55Choice ch;
    std::vector<std::string> complete;
    if (!find_sc55(romdir, files, o.scset, ch, complete, err)) return nullptr;

    Sc55* s = new Sc55;
    s->set_name = ch.set_name;
    RomsetInfo info;
    for (size_t i = 0; i < ROMLOCATION_COUNT; i++) {
        if (ch.paths[i].empty()) continue;
        FILE* f = fopen(ch.paths[i].c_str(), "rb");
        if (!f) { err = "cannot read " + ch.paths[i]; delete s; return nullptr; }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        info.rom_data[i].resize((size_t)n);
        if (fread(info.rom_data[i].data(), 1, (size_t)n, f) != (size_t)n) { fclose(f); err = "read error " + ch.paths[i]; delete s; return nullptr; }
        fclose(f);
    }
    LoadRomset(info, nullptr);                       // unscrambles the wave ROMs in memory
    EMU_Options eo{}; eo.lcd_backend = &g_null_lcd;
    if (!s->emu.Init(eo)) { err = "SC-55 emulator init failed"; delete s; return nullptr; }
    if (!s->emu.LoadRoms((Romset)ch.romset, info)) { err = "SC-55 ROM load failed"; delete s; return nullptr; }
    s->emu.Reset();
    s->emu.GetPCM().enable_oversampling = false;     // 32 kHz: half the CPU of 64 kHz
    // boot the firmware before anything is sent to it (as Nuked-SC55's renderer does)
    s->emu.PostSystemReset(EMU_SystemReset::GS_RESET);
    for (size_t i = 0; i < 24000000; ++i) s->emu.Step();
    s->fifo.assign(16384 * 2, 0.0f);
    s->emu.SetSampleCallback(Sc55::on_sample, s);
    s->step = (double)PCM_GetOutputFrequency(s->emu.GetPCM()) / out_rate;
    s->gain = midi_gain(o);
    s->lim.limit = o.scvoices;
    if (!o.scfx) s->set_fx(false);
    return s;
}

// ======================================================================
// MT-32 / CM-32L (Munt)
// ======================================================================
class Mt32 : public MidiSynth {
public:
    MT32Emu::Synth synth;
    int kind() const override { return 2; }
    int lcd(uint8_t* data80, uint8_t* cg64) override
    {   // the MT-32's 20-character display, as SBEMU's MT32_GetLCD
        char b[64]; memset(b, 0, sizeof b);
        bool led = synth.getDisplayState(b, false);
        memset(data80, ' ', 80); memset(cg64, 0, 64);
        for (int i = 0; i < 20 && b[i]; i++) data80[i] = (uint8_t)b[i];
        (void)led; return 1;      // SBEMU reports the panel as on whenever the MT-32 is loaded
    }
    MT32Emu::FileStream ctl_file, pcm_file;
    const MT32Emu::ROMImage *ctl = nullptr, *pcm = nullptr;
    std::string desc;
    Resampler rs;
    double step = 1.0;
    MidiParser parser;
    float block[2 * 256];
    int bpos = 256, blen = 256;

    ~Mt32() override
    {
        synth.close();
        if (ctl) MT32Emu::ROMImage::freeROMImage(ctl);
        if (pcm) MT32Emu::ROMImage::freeROMImage(pcm);
    }

    void midi_byte(uint8_t b) override
    {
        int kind, len;
        uint8_t m[3];
        if (!parser.feed(b, kind, m, len)) return;
        if (kind == 1) synth.playSysexNow(parser.sysex.data(), (MT32Emu::Bit32u)parser.sysex.size());
        else if (len >= 1 && m[0] < 0xF0) synth.playMsgNow((MT32Emu::Bit32u)(m[0] | (m[1] << 8) | (m[2] << 16)));
    }

    void all_notes_off() override
    {
        for (int c = 0; c < 16; c++) synth.playMsgNow((MT32Emu::Bit32u)(0xB0 | c | (123 << 8)));
    }

    void render(float* lr, int frames) override
    {
        rs.run(lr, frames, step, [this](float* f) {
            if (bpos >= blen) { synth.render(block, (MT32Emu::Bit32u)blen); bpos = 0; }
            f[0] = block[bpos * 2] * gain; f[1] = block[bpos * 2 + 1] * gain;
            bpos++;
        });
    }

    std::string describe() const override { return desc; }
};

MidiSynth* create_mt32(const Options& o, const std::string& romdir, int out_rate, std::string& err)
{
    auto files = scan_roms(romdir);
    Mt32Choice ch;
    if (!find_mt32(romdir, files, o.mt32, ch, err)) return nullptr;
    Mt32* s = new Mt32;
    if (!s->ctl_file.open(ch.control.c_str()) || !s->pcm_file.open(ch.pcm.c_str())) { err = "cannot read MT-32 ROMs"; delete s; return nullptr; }
    s->ctl = MT32Emu::ROMImage::makeROMImage(&s->ctl_file);
    s->pcm = MT32Emu::ROMImage::makeROMImage(&s->pcm_file);
    if (!s->synth.open(*s->ctl, *s->pcm)) { err = "MT-32 emulation failed to start"; delete s; return nullptr; }
    s->desc = (ch.cm32l ? "CM-32L (" : "MT-32 (") + ch.control_desc + ")";
    s->step = (double)s->synth.getStereoOutputSampleRate() / out_rate;
    s->gain = midi_gain(o);
    return s;
}

// ======================================================================
// SoundFont (TinySoundFont) + reverb/chorus
// ======================================================================
struct Reverb {   // Freeverb-style: 8 combs + 4 allpasses per side
    struct Comb { std::vector<float> b; size_t i = 0; float fs = 0; };
    struct Allpass { std::vector<float> b; size_t i = 0; };
    Comb c[2][8];
    Allpass a[2][4];
    float room = 0.84f, damp = 0.2f;
    void init(int rate)
    {
        static const int cl[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int al[4] = {556, 441, 341, 225};
        for (int s = 0; s < 2; s++) {
            for (int k = 0; k < 8; k++) c[s][k].b.assign((size_t)((cl[k] + s * 23) * rate / 44100), 0.0f);
            for (int k = 0; k < 4; k++) a[s][k].b.assign((size_t)((al[k] + s * 23) * rate / 44100), 0.0f);
        }
    }
    void process(float in, float& outl, float& outr)
    {
        float o[2] = {0, 0};
        for (int s = 0; s < 2; s++) {
            for (auto& cb : c[s]) {
                float y = cb.b[cb.i];
                cb.fs = y * (1 - damp) + cb.fs * damp;
                cb.b[cb.i] = in + cb.fs * room;
                if (++cb.i >= cb.b.size()) cb.i = 0;
                o[s] += y;
            }
            for (auto& ap : a[s]) {
                float bo = ap.b[ap.i];
                float y = -o[s] + bo;
                ap.b[ap.i] = o[s] + bo * 0.5f;
                if (++ap.i >= ap.b.size()) ap.i = 0;
                o[s] = y;
            }
        }
        outl = o[0]; outr = o[1];
    }
};

struct Chorus {
    std::vector<float> b[2];
    size_t i = 0;
    double ph = 0, inc = 0;
    float depth = 0.5f;
    int rate = 44100;
    void init(int r, float hz, float d) { rate = r; for (auto& x : b) x.assign((size_t)(r / 20), 0.0f); inc = hz / r; depth = d; }
    void process(float l, float r, float& ol, float& orr)
    {
        size_t n = b[0].size();
        b[0][i] = l; b[1][i] = r;
        float mid = 0.012f * rate, sw = 0.006f * rate * depth;
        for (int s = 0; s < 2; s++) {
            double d = mid + sw * sin(2 * M_PI * (ph + s * 0.25));
            double rp = (double)i - d;
            while (rp < 0) rp += n;
            size_t i0 = (size_t)rp % n, i1 = (i0 + 1) % n;
            float fr = (float)(rp - floor(rp));
            float v = b[s][i0] * (1 - fr) + b[s][i1] * fr;
            if (s == 0) ol = v; else orr = v;
        }
        ph += inc; if (ph >= 1) ph -= 1;
        if (++i >= n) i = 0;
    }
};

class Sf2 : public MidiSynth {
public:
    int kind() const override { return 1; }
    tsf* t = nullptr;
    std::string file;
    MidiParser parser;
    float rev_level = 0, cho_level = 0;
    bool follow = true;
    uint8_t cc91[16], cc93[16];
    Reverb rev;
    Chorus cho;

    ~Sf2() override { if (t) tsf_close(t); }

    void reset_channels()
    {
        tsf_reset(t);
        for (int c = 0; c < 16; c++) { cc91[c] = 40; cc93[c] = 0; tsf_channel_set_bank_preset(t, c, c == 9 ? 128 : 0, 0); }
    }

    void midi_byte(uint8_t b) override
    {
        int kind, len;
        uint8_t m[3];
        if (!parser.feed(b, kind, m, len)) return;
        if (kind == 1) { if (is_reset_sysex(parser.sysex)) reset_channels(); return; }
        uint8_t ch = m[0] & 0x0F;
        switch (m[0] & 0xF0) {
        case 0x80: tsf_channel_note_off(t, ch, m[1]); break;
        case 0x90: if (m[2]) tsf_channel_note_on(t, ch, m[1], m[2] / 127.0f); else tsf_channel_note_off(t, ch, m[1]); break;
        case 0xB0:
            if (m[1] == 91) cc91[ch] = m[2];
            if (m[1] == 93) cc93[ch] = m[2];
            tsf_channel_midi_control(t, ch, m[1], m[2]);
            break;
        case 0xC0: tsf_channel_set_presetnumber(t, ch, m[1], ch == 9); break;
        case 0xD0: tsf_channel_set_pressure(t, ch, m[1] / 127.0f); break;
        case 0xE0: tsf_channel_set_pitchwheel(t, ch, m[1] | (m[2] << 7)); break;
        case 0xF0: if (m[0] == 0xFF) reset_channels(); break;
        }
    }

    void all_notes_off() override { tsf_note_off_all(t); }

    void render(float* lr, int frames) override
    {
        tsf_render_float(t, lr, frames, 0);
        float rs = rev_level, cs = cho_level;
        if (follow) {
            float a = 0, c = 0;
            for (int i = 0; i < 16; i++) { a += cc91[i]; c += cc93[i]; }
            rs *= a / (16 * 127.0f) * 3.0f;      // GM default send 40 -> about 1x
            cs *= c / (16 * 127.0f) * 3.0f;
        }
        for (int i = 0; i < frames; i++) {
            float l = lr[i * 2] * gain, r = lr[i * 2 + 1] * gain;
            float wl = 0, wr = 0, cl = 0, cr = 0;
            if (rs > 0) rev.process((l + r) * 0.015f, wl, wr);
            if (cs > 0) cho.process(l, r, cl, cr);
            lr[i * 2] = l + wl * rs + cl * cs;
            lr[i * 2 + 1] = r + wr * rs + cr * cs;
        }
    }

    std::string describe() const override { return "SoundFont " + file; }
};

MidiSynth* create_sf2(const Options& o, int out_rate, std::string& err)
{
    Sf2* s = new Sf2;
    s->file = o.sf;
    s->t = tsf_load_filename(o.sf.c_str());
    if (!s->t) { err = "SoundFont file not found or could not be loaded: " + o.sf; delete s; return nullptr; }
    tsf_set_output(s->t, TSF_STEREO_INTERLEAVED, out_rate, 0.0f);
    tsf_set_max_voices(s->t, o.sfvoices);
    s->reset_channels();
    s->gain = midi_gain(o);
    s->rev_level = o.sfrev / 100.0f;
    s->cho_level = o.sfcho / 100.0f;
    s->follow = o.sfsend != 0;
    s->rev.init(out_rate);
    s->rev.room = 0.7f + 0.28f * o.sfroom / 100.0f;
    s->rev.damp = 0.4f * o.sfdamp / 100.0f;
    s->cho.init(out_rate, o.sfcrate / 10.0f, o.sfcdepth / 100.0f);
    return s;
}

// ======================================================================
// OPL
// ======================================================================
class OplDosbox : public Opl {
public:
    DBOPL::Chip* chip = nullptr;
    int rate;
    std::vector<int16_t> tmp;
    explicit OplDosbox(int r) : rate(r) { reset(); }
    ~OplDosbox() override { delete chip; }
    void reset() override { delete chip; chip = new DBOPL::Chip(true); chip->Setup((uint32_t)rate); }
    void write(int bank, uint8_t reg, uint8_t val) override { chip->WriteReg((uint32_t)((bank << 8) | reg), val); }
    void render(float* lr, int frames) override
    {
        tmp.resize((size_t)frames * 2);
        int done = 0;
        while (done < frames) {
            int n = std::min(frames - done, 512);
            bool st = chip->opl3Active != 0;
            chip->Generate(tmp.data(), (uint32_t)n);
            for (int i = 0; i < n; i++) {
                float l = st ? tmp[i * 2] : tmp[i], r = st ? tmp[i * 2 + 1] : tmp[i];
                lr[(done + i) * 2] = l / 32768.0f;
                lr[(done + i) * 2 + 1] = r / 32768.0f;
            }
            done += n;
        }
    }
};

class OplNuked : public Opl {
public:
    opl3_chip chip;
    int rate;
    std::vector<int16_t> tmp;
    explicit OplNuked(int r) : rate(r) { reset(); }
    void reset() override { OPL3_Reset(&chip, (uint32_t)rate); }
    void write(int bank, uint8_t reg, uint8_t val) override { OPL3_WriteRegBuffered(&chip, (uint16_t)((bank << 8) | reg), val); }
    void render(float* lr, int frames) override
    {
        tmp.resize((size_t)frames * 2);
        OPL3_GenerateStream(&chip, tmp.data(), (uint32_t)frames);
        for (int i = 0; i < frames * 2; i++) lr[i] = tmp[i] / 32768.0f;
    }
};

Opl* create_opl(int core, int out_rate)
{
    if (core == 1) return new OplNuked(out_rate);
    return new OplDosbox(out_rate);
}

float midi_gain(const Options& o)
{
    if (o.synth() == Options::SYN_SF2) return (float)(o.midivol / 100.0);
    return (float)(o.scgain / 100.0 * o.midivol / 100.0);
}
