// engine.cpp - see engine.h
#include "engine.h"
#include <cmath>
#include <cstring>

void Engine::setup(const Options& o, const std::string& romdir, int out_rate, int latency)
{
    opt = o;
    rate = out_rate;
    latency_ms = latency;
    midi.reset();
    midi_error.clear();
    if (o.vmpu) {
        std::string err;
        MidiSynth* m = nullptr;
        switch (o.synth()) {
        case Options::SYN_SF2:  m = create_sf2(o, out_rate, err); break;
        case Options::SYN_MT32: m = create_mt32(o, romdir, out_rate, err); break;
        default:                m = create_sc55(o, romdir, out_rate, err); break;
        }
        midi.reset(m);
        midi_error = err;
    }
    opl.reset(create_opl(o.fmcore, out_rate));
    started = false;
    pcm_primed = false;
    pcm_rate = 22050;
    st = EngineStats();
    midi_quiet = opl_quiet = 0;
    midi_hold[0] = midi_hold[1] = 0;
}

static Options live_blank(Options x)
{
    x.vol = x.sbvol = x.oplvol = x.midivol = x.scgain = 0;
    x.scfx = x.scauto = x.scahead = x.vtimer = 0;
    x.sb_port = x.irq = x.dma8 = x.dma16 = x.sbtype = x.mpu_port = x.fixtc = x.opl = x.vmpu = 0;
    x.passthru.clear();
    return x;
}

bool Engine::apply_live(const Options& o)
{
    if (live_blank(o).format() != live_blank(opt).format()) return false;
    bool fx_changed = o.scfx != opt.scfx;
    int vmpu = opt.vmpu;
    opt = o;
    opt.vmpu = vmpu;
    latency_ms = o.scahead < 20 ? 20 : o.scahead;
    if (midi) {
        midi->gain = midi_gain(o);
        if (fx_changed && !st.fx_shed) midi->set_fx(o.scfx != 0);
    }
    return true;
}

void Engine::midi_now(uint8_t b) { pending_midi.push_back(b); }

bool Engine::has_input(const SC55W_SHARED* sh) const
{
    if (!pending_midi.empty()) return true;
    return sh && (sh->ev_tail != sh->ev_head || sh->pcm_head != sh->pcm_tail);
}

bool Engine::can_sleep(const SC55W_SHARED* sh) const
{
    return silent_frames >= rate && (!midi || st.midi_idle) && (!opl || !opt.opl || st.opl_idle) && !dac_active &&
           !pcm_primed && !has_input(sh);
}

void Engine::wake_output()
{
    started = false;          // render() takes the window from the VxD clock again
    silent_frames = 0;
    quiet_late = true;
}

void Engine::idle_check(const float* buf, int from, int to, int& quiet, bool& idle)
{
    if (idle) return;
    // silent = no signal apart from a constant (DC) level, which the SC-55 has
    float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
    for (int i = from; i < to; i++)
        for (int c = 0; c < 2; c++) {
            float v = buf[i * 2 + c];
            if (v < lo[c]) lo[c] = v;
            if (v > hi[c]) hi[c] = v;
        }
    if (hi[0] - lo[0] > 4e-4f || hi[1] - lo[1] > 4e-4f) { quiet = 0; return; }
    quiet += to - from;
    // silence with no input: 4 s while the synth is starting (the SC-55 boots its firmware in the
    // first seconds), then 2 s (CPU: a sleeping synth costs nothing, a running one ~a third of a D810)
    if (quiet > rate * (frames_total > (uint64_t)rate * 8 ? 2 : 4)) idle = true;
}

void Engine::render_sources(int from, int to)
{
    int n = to - from;
    if (n <= 0) return;
    frames_total += (uint64_t)n;
    if (midi && !st.midi_idle) {
        midi->render(&bm[(size_t)from * 2], n);
        idle_check(bm.data(), from, to, midi_quiet, st.midi_idle);
        midi_hold[0] = bm[(size_t)to * 2 - 2];
        midi_hold[1] = bm[(size_t)to * 2 - 1];
    } else {
        // paused: hold the synth's last (DC) level so waking it does not click
        for (int i = from; i < to; i++) { bm[(size_t)i * 2] = midi_hold[0]; bm[(size_t)i * 2 + 1] = midi_hold[1]; }
    }
    if (opl && opt.opl && !st.opl_idle) {
        opl->render(&bo[(size_t)from * 2], n);
        idle_check(bo.data(), from, to, opl_quiet, st.opl_idle);
    } else memset(&bo[(size_t)from * 2], 0, (size_t)n * 2 * sizeof(float));
    // direct DAC (DSP command 10h): sample-and-hold, added to the PCM bus
    float d = dac_active ? (dac_value - 128) / 128.0f : 0.0f;
    for (int i = from; i < to; i++) { bp[(size_t)i * 2] = d; bp[(size_t)i * 2 + 1] = d; }
}

void Engine::pcm_render(SC55W_SHARED* sh, float* out, int frames)
{
    // out already holds the direct-DAC signal; DMA audio is added to it
    if (!sh) return;
    uint32_t avail = sh->pcm_head - sh->pcm_tail;
    const uint32_t target = (uint32_t)(pcm_rate * latency_ms / 2000.0) + 64;
    if (!pcm_primed) {
        if (avail < target) return;
        pcm_primed = true;
    }
    if (!avail) { pcm_primed = false; return; }

    double err = ((double)avail - target) / target;
    if (err > 1) err = 1;
    if (err < -1) err = -1;
    double step = pcm_rate / rate * (1.0 + 0.01 * err);
    if (avail > target * 4) {                   // far behind (e.g. after a stall): drop the excess
        sh->pcm_tail += avail - target;
        st.resyncs++;
    }
    static thread_local std::vector<float> tmp;
    tmp.resize((size_t)frames * 2);
    pcm_rs.run(tmp.data(), frames, step, [&](float* f) {
        // rate changes take effect at the frame where the VxD recorded them
        while (sh->fmt_tail != sh->fmt_head &&
               (int32_t)(sh->fmt[sh->fmt_tail & (PCMFMT_SIZE - 1)].frame - sh->pcm_tail) <= 0) {
            pcm_rate = sh->fmt[sh->fmt_tail & (PCMFMT_SIZE - 1)].rate;
            sh->fmt_tail++;
        }
        if (sh->pcm_tail != sh->pcm_head) {
            uint32_t i = (sh->pcm_tail & (PCMRING_SIZE - 1)) * 2;
            f[0] = sh->pcm[i] / 32768.0f;
            f[1] = sh->pcm[i + 1] / 32768.0f;
            sh->pcm_tail++;
            st.pcm_frames++;
        } else {
            f[0] *= 0.99f; f[1] *= 0.99f;       // underrun: fade the last frame
            if (f[0] > -1e-15f && f[0] < 1e-15f) f[0] = 0.0f;   // never into denormals (slow)
            if (f[1] > -1e-15f && f[1] < 1e-15f) f[1] = 0.0f;
        }
    });
    for (int i = 0; i < frames * 2; i++) out[i] += tmp[(size_t)i];
}

// SB mixer level -> linear gain
static float sb16_level(uint8_t reg)          // 5-bit in bits 7-3, 2 dB steps
{
    int l = reg >> 3;
    return l ? powf(10.0f, -(31 - l) * 2.0f / 20.0f) : 0.0f;
}
static float sbpro_level(uint8_t nib)         // 4-bit, roughly 3 dB per step
{
    int n = nib & 0x0F;
    return n ? powf(10.0f, -(15 - n) * 3.0f / 20.0f) : 0.0f;
}

void Engine::render(SC55W_SHARED* sh, uint32_t now_us, int16_t* out, int frames)
{
    bm.resize((size_t)frames * 2);
    bo.resize((size_t)frames * 2);
    bp.resize((size_t)frames * 2);
    const double us_per_frame = 1e6 / rate;
    const uint32_t lat_us = (uint32_t)latency_ms * 1000u;
    const uint32_t target = now_us - lat_us;

    if (!started) { win_start = target; frac_us = 0; started = true; }
    int32_t drift = (int32_t)(target - win_start);
    if (drift > (int32_t)(4 * lat_us + 100000) || drift < -(int32_t)(4 * lat_us + 100000)) {
        win_start = target; st.resyncs++;
    } else {
        win_start += drift / 32;                // follow the VxD clock smoothly
    }

    for (uint8_t b : pending_midi) if (midi) { wake_midi(); midi->midi_byte(b); }
    pending_midi.clear();

    int pos = 0;
    if (sh) {
        while (sh->ev_tail != sh->ev_head) {
            const SC55W_EVENT& e = sh->ev[sh->ev_tail & (EVRING_SIZE - 1)];
            int32_t dt = (int32_t)(e.time - win_start);
            int epos = dt <= 0 ? 0 : (int)(dt / us_per_frame);
            if (epos >= frames) break;                  // belongs to a later buffer
            if (dt < -(int32_t)lat_us && !quiet_late) st.late_events++;
            if (epos > pos) { render_sources(pos, epos); pos = epos; }
            switch (e.type) {
            case EV_OPL:  if (opl) { wake_opl(); opl->write(e.c, e.a, e.b); } st.opl_writes++; break;
            case EV_MIDI: if (midi) { wake_midi(); midi->midi_byte(e.a); } st.midi_bytes++; break;
            case EV_DAC:  dac_value = e.a; dac_active = true; dac_last = e.time; break;
            case EV_RESET: dac_active = false; break;
            default: break;
            }
            sh->ev_tail++;
        }
        if (dac_active && (int32_t)(win_start - dac_last) > 200000) dac_active = false;
    }
    render_sources(pos, frames);
    pcm_render(sh, bp.data(), frames);

    // levels: SB mixer registers (as the DOS program set them) x pack volumes
    float vl = 1, vr = 1, fl = 1, fr = 1, ml = 1, mr = 1;
    if (sh && opt.sbtype >= 6) {
        const volatile uint8_t* m = sh->mixer;
        ml = sb16_level(m[0x30]); mr = sb16_level(m[0x31]);
        vl = sb16_level(m[0x32]); vr = sb16_level(m[0x33]);
        fl = sb16_level(m[0x34]); fr = sb16_level(m[0x35]);
    } else if (sh && opt.sbtype >= 4) {
        const volatile uint8_t* m = sh->mixer;
        ml = sbpro_level(m[0x22] >> 4); mr = sbpro_level(m[0x22]);
        vl = sbpro_level(m[0x04] >> 4); vr = sbpro_level(m[0x04]);
        fl = sbpro_level(m[0x26] >> 4); fr = sbpro_level(m[0x26]);
    }
    const float master = opt.vol / 100.0f;
    const float sbv = opt.sbvol / 100.0f, oplv = opt.oplvol / 100.0f;
    const float pl = master * ml * vl * sbv, pr = master * mr * vr * sbv;
    const float ol = master * ml * fl * oplv, orr = master * mr * fr * oplv;
    // remove the SC-55's DC offset. CPU: while the synth sleeps its output is a constant, the filter's
    // output decays towards 0 and would end up as denormal floats (and stay there: 0.9995 x the smallest
    // one rounds back to itself), which the Pentium M / 4 handle ~100x slower - a sleeping synth then
    // cost more than a playing one. Flush the tail to 0, and skip the loop once it has settled.
    if (st.midi_idle && dc_y[0] == 0.0f && dc_y[1] == 0.0f && bm[0] == dc_x[0] && bm[1] == dc_x[1]) {
        for (int i = 0; i < frames * 2; i++) bm[i] = 0.0f;
    } else {
        for (int i = 0; i < frames; i++) {
            for (int c = 0; c < 2; c++) {
                float x = bm[i * 2 + c];
                float y = x - dc_x[c] + 0.9995f * dc_y[c];
                if (y > -1e-15f && y < 1e-15f) y = 0.0f;
                dc_x[c] = x; dc_y[c] = y;
                bm[i * 2 + c] = y;
            }
        }
    }
    // Windows MIDI volume (Volume Control, midiOutSetVolume): the synth's output
    float wl = 1, wr = 1;
    if (sh && sh->midi_volume_set) {
        uint32_t v = sh->midi_volume;
        wl = (v & 0xFFFF) / 65535.0f;
        wr = (v >> 16) / 65535.0f;
    }
    const float mvl = master * wl, mvr = master * wr;
    st.midi_volume = (int)((wl + wr) * 50 + 0.5f);
    for (int i = 0; i < frames; i++) {
        float l = bp[i * 2] * pl + bo[i * 2] * ol + bm[i * 2] * mvl;
        float r = bp[i * 2 + 1] * pr + bo[i * 2 + 1] * orr + bm[i * 2 + 1] * mvr;
        int a = (int)lrintf(l * 32767.0f), b = (int)lrintf(r * 32767.0f);
        out[i * 2] = (int16_t)(a > 32767 ? 32767 : a < -32768 ? -32768 : a);
        out[i * 2 + 1] = (int16_t)(b > 32767 ? 32767 : b < -32768 ? -32768 : b);
    }

    quiet_late = false;
    {
        bool zero = true;
        for (int i = 0; i < frames * 2 && zero; i++) zero = out[i] == 0;
        silent_frames = zero ? (silent_frames > (1 << 30) ? silent_frames : silent_frames + frames) : 0;
    }

    frac_us += frames * us_per_frame;
    uint32_t whole = (uint32_t)frac_us;
    win_start += whole;
    frac_us -= whole;
}
