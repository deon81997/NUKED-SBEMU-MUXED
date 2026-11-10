// engine.h - turns the VxD's event and PCM rings into audio
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "../common/sc55wif.h"
#include "opts.h"
#include "synths.h"

struct EngineStats {
    int midi_volume = 100;          // Windows MIDI volume, percent (average of L and R)
    unsigned midi_bytes = 0, opl_writes = 0, pcm_frames = 0, late_events = 0, resyncs = 0;
    double load = 0;     // render time / audio time, smoothed
    bool fx_shed = false;
    bool midi_idle = false, opl_idle = false;   // synth paused while silent
    bool out_asleep = false;                    // sound output stopped while everything is silent
};

class Engine {
public:
    Options opt;
    int rate = 44100;            // output rate
    int pcm_slack_ms = 0;        // a bigger output buffer: DMA sound may pile up this much before it is dropped
    int latency_ms = 50;         // events are rendered this long after they happen
    std::unique_ptr<MidiSynth> midi;
    std::unique_ptr<Opl> opl;
    std::string midi_error;
    EngineStats st;

    // build synths from options; romdir for SC-55/MT-32
    void setup(const Options& o, const std::string& romdir, int out_rate, int latency);

    // Render `frames` stereo frames into out (int16 interleaved).
    // now_us = current VxD clock. sh may be null (silence + Windows MIDI only).
    void render(SC55W_SHARED* sh, uint32_t now_us, int16_t* out, int frames);

    // Apply options that need no rebuild (volumes, gain, reverb on/off,
    // render-ahead, SB type/ports, OPL on/off). Returns false if the synths
    // must be rebuilt with setup() for the new options to take effect.
    bool apply_live(const Options& o);

    // MIDI from another source (e.g. a Windows MIDI-in port): applied at the next render
    void midi_now(uint8_t b);

    // CPU: the sound output can be stopped while nothing makes a sound (synths asleep, no DMA,
    // no direct DAC, the last second rendered was exact silence and no input is waiting).
    // has_input() = something is waiting in the VxD's rings: start the output again.
    bool has_input(const SC55W_SHARED* sh) const;
    bool can_sleep(const SC55W_SHARED* sh) const;
    // after the output was stopped: start a fresh time window (no resync / late-event counts)
    void wake_output();

private:
    bool started = false;
    uint32_t win_start = 0;      // VxD time of the next output frame
    double frac_us = 0;
    // SB PCM stream
    Resampler pcm_rs;
    double pcm_rate = 22050;
    bool pcm_primed = false;
    uint8_t dac_value = 128;
    bool dac_active = false;
    uint32_t dac_last = 0;
    std::vector<float> bm, bo, bp;    // per-render scratch: midi, opl, pcm
    std::vector<uint8_t> pending_midi;
    float dc_x[2] = {0, 0}, dc_y[2] = {0, 0};   // DC blocker on the MIDI bus
    uint64_t frames_total = 0;                  // frames rendered since setup (idle rule)
    int silent_frames = 0;                      // output frames of exact silence in a row
    bool quiet_late = false;                    // first render after wake_output: old events are not "late"
    // idle gating: a synth that has had no input for a while and has gone
    // silent is not run (saves CPU, the SC-55 costs a lot); input wakes it
    int midi_quiet = 0, opl_quiet = 0;
    float midi_hold[2] = {0, 0};            // frames of silence since the last input
    void wake_midi() { midi_quiet = 0; st.midi_idle = false; }
    void wake_opl() { opl_quiet = 0; st.opl_idle = false; }
    void idle_check(const float* buf, int from, int to, int& quiet, bool& idle);

    void render_sources(int from, int to);
    void pcm_render(SC55W_SHARED* sh, float* out, int frames);
};
