// sc55engine.hpp - core-independent glue between a Nuked-SC55 style emulator
// and a DOS Sound Blaster emulator (SBEMU).
//
//   * a lock-free single-producer/single-consumer MIDI byte queue, fed by the
//     MPU-401 port trap (producer) and drained into the core's UART by the
//     audio interrupt (consumer);
//   * a sample FIFO and linear resampler from the core's native output rate
//     (64000 Hz SC-55, 66207 Hz SC-55mkII) to the sound card's rate;
//   * MK1 DC-bias removal, output gain and saturating mix into SBEMU's buffer;
//   * idle gating, so the (CPU hungry) emulation only runs while there is MIDI
//     traffic or an audible tail.
//
// The engine is a template over a small "Core" interface so the exact same
// code is compiled against the real emulator on DOS and against a fake core in
// the host-side unit tests (tests/engine_test.cpp):
//
//     void     step();                                  // one MCU step
//     unsigned uart_free();                             // bytes the core UART ring can take
//     void     uart_put(uint8_t b);                     // enqueue one MIDI byte
//     void     set_sample_sink(void (*fn)(void*, int32_t, int32_t), void* ud);
//
// The resampling, DC removal and FIFO behaviour follow VSC-55 (Win98 host):
// frames are never dropped by the emulator side, only interpolated.
//
// GPL-2.0-or-later.
#pragma once

#include <atomic>
#include <stdint.h>
#include <stddef.h>

namespace sc55
{

struct Frame
{
    int16_t l, r;
};

template <class Core>
class Engine
{
public:
    static constexpr uint32_t kRing = 65536;            // MIDI queue bytes (power of two)
    static constexpr uint32_t kRingMask = kRing - 1;
    static constexpr uint32_t kBusyMark = kRing - 4096; // report "interface busy" above this
    static constexpr unsigned kFifo = 64;               // native frame FIFO
    static constexpr unsigned kMaxStepsPerFrame = 1u << 20;
    static constexpr int kQuietThreshold = 8;           // PCM16 units considered silence
    // SBEMU skips OPL rendering while the MPU is "active", so keep these short:
    // a game that only sends an MPU init and then plays through OPL must not
    // lose its FM music.
    static constexpr int kWakeMs = 750;                 // run at least this long after MIDI
    static constexpr int kTailMs = 750;                 // keep running this long after the last audible sample (CPU: was 1500)

    Engine(Core& core, uint32_t native_rate, int dc_offset)
        : core_(core), native_rate_(native_rate), dc_offset_(dc_offset)
    {
        core_.set_sample_sink(&Engine::on_sample, this);
    }

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---- configuration -------------------------------------------------
    void set_gain_percent(int pct)
    {
        if (pct < 0) pct = 0;
        if (pct > 1600) pct = 1600;
        gain_ = pct;
        base_q8_ = (pct * 256 + 50) / 100; // Q8 multiplier: no per-sample division in render()
        update_gain();
    }
    // Host mixer volume (SB16 master * MIDI volume), 0..256 = 0..100%. Applied on top of the gain setting.
    void set_mix_volume_q8(int v)
    {
        mix_q8_ = v < 0 ? 0 : (v > 256 ? 256 : v);
        update_gain();
    }
    int gain_percent() const { return gain_; }
    uint32_t native_rate() const { return native_rate_; }
    bool stalled() const { return stalled_; }

    // Run the core for `native_frames` output frames, discarding them. Used to
    // let the firmware boot before the first MIDI byte arrives. Returns false
    // if the core stopped producing audio.
    bool prime(uint32_t native_frames)
    {
        feeding_ = false;
        for (uint32_t i = 0; i < native_frames; i++)
        {
            next_native();
            if (stalled_) return false;
        }
        a_ = next_native();
        b_ = next_native();
        phase_ = 0;
        feeding_ = true;
        started_ = !stalled_;
        return started_;
    }

    // ---- producer side (MPU-401 port trap) -----------------------------
    // Safe to call from an interrupt/trap that preempts render().
    bool post_byte(uint8_t b)
    {
        uint32_t t = tail_.load(std::memory_order_relaxed);
        uint32_t h = head_.load(std::memory_order_acquire);
        if (t - h >= kRing) return false; // full: drop, a real UART would overrun too
        ring_[t & kRingMask] = b;
        tail_.store(t + 1, std::memory_order_release);
        midi_seq_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    uint32_t queued() const
    {
        return tail_.load(std::memory_order_acquire) - head_.load(std::memory_order_acquire);
    }

    // Value for the MPU-401 status "DRR" bit: true while the queue is nearly full.
    bool busy() const { return queued() >= kBusyMark; }

    // ---- consumer side (audio interrupt) -------------------------------
    // True while the emulator should keep running.
    bool active() const
    {
        if (stalled_) return false; // a dead core must not keep burning CPU in the audio interrupt
        return awake_ > 0 || queued() != 0 ||
               midi_seq_.load(std::memory_order_relaxed) != seen_seq_;
    }

    // Produce `frames` stereo frames at `out_rate` into `pcm` (interleaved).
    // domix: add (saturating) to what is already in the buffer, else overwrite.
    void render(int16_t* pcm, int frames, uint32_t out_rate, bool domix)
    {
        if (!started_ || stalled_ || out_rate == 0)
        {
            if (!domix && pcm && frames > 0) // overwrite mode must still define the buffer
                for (int i = 0; i < frames * 2; i++) pcm[i] = 0;
            return;
        }
        if (out_rate != out_rate_)
        {
            out_rate_ = out_rate;
            step_ = ((uint64_t)native_rate_ << 32) / out_rate_; // native frames per output frame, 32.32
            phase_ = 0;
        }
        wake_if_midi();
        feed();

        const int tail_frames = (int)((uint64_t)out_rate_ * kTailMs / 1000);
        for (int i = 0; i < frames; i++)
        {
            const int32_t frac = (int32_t)((uint32_t)phase_ >> 17); // 0..32767, fraction of the 32.32 phase
            int32_t l = a_.l + ((((int32_t)b_.l - a_.l) * frac) >> 15);
            int32_t r = a_.r + ((((int32_t)b_.r - a_.r) * frac) >> 15);

            if (gain_q8_ != 256)
            {
                l = clamp16((l * gain_q8_) >> 8);
                r = clamp16((r * gain_q8_) >> 8);
            }

            if (l > kQuietThreshold || l < -kQuietThreshold || r > kQuietThreshold || r < -kQuietThreshold)
                awake_ = tail_frames > awake_ ? tail_frames : awake_;
            else if (awake_ > 0)
                awake_--;

            if (domix)
            {
                pcm[2 * i] = (int16_t)clamp16((int32_t)pcm[2 * i] + l);
                pcm[2 * i + 1] = (int16_t)clamp16((int32_t)pcm[2 * i + 1] + r);
            }
            else
            {
                pcm[2 * i] = (int16_t)l;
                pcm[2 * i + 1] = (int16_t)r;
            }

            phase_ += step_;
            for (uint32_t adv = (uint32_t)(phase_ >> 32); adv; adv--)
            {
                a_ = b_;
                b_ = next_native();
            }
            phase_ &= 0xFFFFFFFFull;
            if (stalled_) // core died mid-block: finish the block as silence
            {
                for (i++; i < frames; i++)
                    if (!domix) { pcm[2 * i] = 0; pcm[2 * i + 1] = 0; }
                return;
            }
        }
    }

private:
    void update_gain() { gain_q8_ = (base_q8_ * mix_q8_ + 128) >> 8; }
    static int32_t clamp16(int32_t v) { return v > 32767 ? 32767 : (v < -32768 ? -32768 : v); }

    static void on_sample(void* self, int32_t left, int32_t right)
    {
        static_cast<Engine*>(self)->push(left, right);
    }

    // Same arithmetic as VSC-55's SampleFIFO: widen, subtract the MK1 bias
    // (512 PCM16 units == 512 * 32768 in the core's accumulator scale), shift.
    void push(int32_t left, int32_t right)
    {
        const int64_t bias = (int64_t)dc_offset_ * 32768;
        int64_t l = ((int64_t)left - bias) >> 15;
        int64_t r = ((int64_t)right - bias) >> 15;
        Frame f;
        f.l = (int16_t)(l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
        f.r = (int16_t)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
        if (fcount_ == kFifo) // overflow: discard the oldest, keep running
        {
            fhead_ = (fhead_ + 1) % kFifo;
            fcount_--;
        }
        fifo_[(fhead_ + fcount_) % kFifo] = f;
        fcount_++;
    }

    Frame next_native()
    {
        unsigned guard = 0;
        while (!fcount_)
        {
            core_.step();
            if (++guard > kMaxStepsPerFrame)
            {
                stalled_ = true;
                return Frame{0, 0};
            }
        }
        if (feeding_ && ((++native_count_) & 63) == 0) feed();
        Frame f = fifo_[fhead_];
        fhead_ = (fhead_ + 1) % kFifo;
        fcount_--;
        return f;
    }

    // Move queued MIDI into the core UART without ever filling it completely
    // (equal read/write pointers mean "empty" in the core).
    void feed()
    {
        uint32_t h = head_.load(std::memory_order_relaxed);
        const uint32_t t = tail_.load(std::memory_order_acquire);
        while (h != t)
        {
            unsigned space = core_.uart_free();
            if (!space) break;
            uint32_t n = t - h;
            if (n > space) n = space;
            for (uint32_t i = 0; i < n; i++)
                core_.uart_put(ring_[(h + i) & kRingMask]);
            h += n;
            head_.store(h, std::memory_order_release);
        }
    }

    void wake_if_midi()
    {
        const uint32_t s = midi_seq_.load(std::memory_order_relaxed);
        if (s != seen_seq_)
        {
            seen_seq_ = s;
            const int wake = (int)((uint64_t)out_rate_ * kWakeMs / 1000);
            if (awake_ < wake) awake_ = wake;
        }
    }

    Core& core_;
    uint32_t native_rate_;
    int dc_offset_;
    int gain_ = 100;

    // native sample FIFO (consumer context only)
    Frame fifo_[kFifo] = {};
    unsigned fhead_ = 0, fcount_ = 0;
    Frame a_ = {0, 0}, b_ = {0, 0};
    uint64_t phase_ = 0;            // position between a_ and b_, 32.32 fixed point
    uint64_t step_ = 0;             // native frames consumed per output frame, 32.32
    int base_q8_ = 256, mix_q8_ = 256, gain_q8_ = 256;
    uint32_t out_rate_ = 0;
    uint32_t native_count_ = 0;
    bool feeding_ = false, started_ = false, stalled_ = false;

    // MIDI queue: producer owns tail_, consumer owns head_
    uint8_t ring_[kRing];
    std::atomic<uint32_t> head_{0}, tail_{0};
    std::atomic<uint32_t> midi_seq_{0};
    uint32_t seen_seq_ = 0;
    int awake_ = 0;
};

} // namespace sc55
