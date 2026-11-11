// synths.h - MIDI synths (SC-55, MT-32, SoundFont) and OPL cores for the
// NUKED SB-EMU helper. All render float stereo interleaved at the output rate.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "opts.h"

// ---- MIDI message assembly (running status, sysex) --------------------
struct MidiParser {
    uint8_t status = 0;
    uint8_t buf[3];
    int need = 0, have = 0;
    std::vector<uint8_t> sysex;
    bool in_sysex = false;
    // returns true when a complete message is available in msg/len or sysex
    // kind: 0 = short (msg, len), 1 = sysex (sysex vector)
    bool feed(uint8_t b, int& kind, uint8_t msg[3], int& len);
};

// ---- note limiter (/VSCVOICES): releases the oldest note with a real
// Note Off when more than `limit` notes would sound (drums not counted) ----
struct NoteLimiter {
    int limit = 0;
    struct Note { uint8_t ch, key; };
    std::vector<Note> held;
    // returns extra messages (3 bytes each) to send *before* msg
    void on_message(const uint8_t* m, int len, std::vector<uint8_t>& pre);
};

class MidiSynth {
public:
    virtual ~MidiSynth() {}
    virtual void midi_byte(uint8_t b) = 0;
    virtual void render(float* lr, int frames) = 0;   // overwrites lr (2*frames)
    virtual std::string describe() const = 0;
    virtual void all_notes_off() {}
    virtual void set_fx(bool on) { (void)on; }       // SC-55 only (/VSCFX, /VSCAUTO)
    virtual int lcd(uint8_t* data80, uint8_t* cg64) { (void)data80; (void)cg64; return 0; }  // 1 = display on (MPU B0h panel)
    virtual int kind() const { return 0; }          // panel synth byte: 0 SC-55, 1 SoundFont, 2 MT-32
    float gain = 1.0f;                                // output gain (/VSCGAIN x /VMIDIVOL)
};

// out_rate: output sample rate. Returns nullptr and sets err on failure.
MidiSynth* create_sc55(const Options& o, const std::string& romdir, int out_rate, std::string& err);
MidiSynth* create_mt32(const Options& o, const std::string& romdir, int out_rate, std::string& err);
MidiSynth* create_sf2(const Options& o, int out_rate, std::string& err);
// the gain create_* gives a synth for these options
float midi_gain(const Options& o);

// ---- OPL ----------------------------------------------------------------
class Opl {
public:
    virtual ~Opl() {}
    virtual void write(int bank, uint8_t reg, uint8_t val) = 0;
    virtual void render(float* lr, int frames) = 0;   // overwrites lr
    virtual void reset() = 0;
};
Opl* create_opl(int core, int out_rate);   // 0 = DOSBox (fast), 1 = Nuked OPL3

// ---- simple stereo resampler (cubic) ----------------------------------
struct Resampler {
    double pos = 0;          // fractional read position into hist
    float hist[4][2] = {};   // last 4 input frames
    // pull: called to fetch the next input frame
    template <class Pull>
    void run(float* out, int frames, double step, Pull pull)
    {
        for (int i = 0; i < frames; i++) {
            while (pos >= 1.0) {
                for (int k = 0; k < 3; k++) { hist[k][0] = hist[k + 1][0]; hist[k][1] = hist[k + 1][1]; }
                pull(hist[3]);
                pos -= 1.0;
            }
            double t = pos;
            for (int c = 0; c < 2; c++) {
                float y0 = hist[0][c], y1 = hist[1][c], y2 = hist[2][c], y3 = hist[3][c];
                float a = (float)t;
                // Catmull-Rom between y1 and y2
                out[i * 2 + c] = y1 + 0.5f * a * (y2 - y0 + a * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + a * (3.0f * (y1 - y2) + y3 - y0)));
            }
            pos += step;
        }
    }
};
