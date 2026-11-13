// Host-side unit tests for sc55/sc55engine.hpp using a fake emulator core.
// Build & run on any machine:  make -f tests/Makefile.host   (see tests/Makefile.host)
// GPL-2.0-or-later.
#include "../sc55/sc55engine.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        if (!(cond)) {                                                    \
            g_fail++;                                                     \
            printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);    \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

// A fake core: emits one native frame every `steps_per_frame` steps.
// Frame value n is (raw = (n*slope + bias) << 15) so the engine sees a ramp.
struct FakeCore
{
    using Sink = void (*)(void*, int32_t, int32_t);
    Sink sink = nullptr;
    void* ud = nullptr;
    unsigned steps_per_frame = 3;
    unsigned step_count = 0;
    int64_t n = 0;                  // native frame index
    int slope = 10;                 // PCM16 units per native frame
    int bias = 0;                   // PCM16 units added to every frame (MK1 DC)
    bool silent = false;            // emit zeros
    double sine_hz = 0, sine_amp = 0, native = 64000; // sine_hz > 0: emit a sine at that frequency instead of the ramp
    bool dead = false;              // never emit
    // UART model
    unsigned uart_cap = 16;         // usable bytes in the ring
    unsigned uart_used = 0;
    unsigned drain_every = 5;       // firmware reads one byte every N steps
    std::vector<uint8_t> received;
    unsigned max_used = 0;
    // optional reentrancy hook: called inside step() to simulate a trap handler
    void (*hook)(FakeCore&) = nullptr;
    void* hook_ud = nullptr;

    void set_sample_sink(Sink s, void* u) { sink = s; ud = u; }
    unsigned uart_free() { return uart_cap - uart_used; }
    void uart_put(uint8_t b)
    {
        received.push_back(b);
        uart_used++;
        if (uart_used > max_used) max_used = uart_used;
    }
    void step()
    {
        step_count++;
        if (hook) hook(*this);
        if (drain_every && (step_count % drain_every) == 0 && uart_used) uart_used--;
        if (dead) return;
        if (step_count % steps_per_frame == 0)
        {
            int32_t raw = silent ? 0 : (int32_t)((int64_t)(n * slope + bias) * 32768);
            if (sine_hz > 0) raw = (int32_t)(std::lround(sine_amp * std::sin(2 * 3.14159265358979 * sine_hz * n / native)) * 32768);
            if (silent && bias) raw = (int32_t)((int64_t)bias * 32768);
            n++;
            sink(ud, raw, raw);
        }
    }
};

using Eng = sc55::Engine<FakeCore>;

static void test_resample_linear_and_chunk_invariance()
{
    printf("resampler: linearity + chunk invariance\n");
    for (uint32_t out_rate : {48000u, 44100u, 22050u, 96000u})
    {
        FakeCore c1, c2;
        c1.slope = c2.slope = 1; // keep the ramp inside int16 for the whole run
        Eng e1(c1, 64000, 0), e2(c2, 64000, 0);
        e1.set_lowpass(false); e2.set_lowpass(false);   // this test checks the interpolator's arithmetic exactly
        CHECK(e1.prime(10), "prime");
        CHECK(e2.prime(10), "prime");
        const int total = 5000;
        std::vector<int16_t> big(total * 2), chunked(total * 2);
        e1.render(big.data(), total, out_rate, false);
        int pos = 0;
        const int sizes[] = {1, 7, 333, 64, 1000, 17};
        int si = 0;
        while (pos < total)
        {
            int n = sizes[si++ % 6];
            if (pos + n > total) n = total - pos;
            e2.render(chunked.data() + pos * 2, n, out_rate, false);
            pos += n;
        }
        CHECK(big == chunked, "chunked render differs at rate %u", out_rate);

        // The source is a ramp of 1 unit per native frame; after linear
        // interpolation the output must be a straight line in time.
        const double step = 1.0 * 64000.0 / out_rate; // output units per output frame
        double max_err = 0;
        for (int i = 1; i < total; i++)
        {
            double expect_delta = step;
            double d = (double)big[2 * i] - big[2 * (i - 1)];
            double err = std::fabs(d - expect_delta);
            if (err > max_err) max_err = err;
        }
        CHECK(max_err <= 1.01, "rate %u: max slope error %.3f", out_rate, max_err);
        CHECK(big[0] == big[1] && big[2 * 100] == big[2 * 100 + 1], "L/R must match for mono source");
    }
}

// the anti-alias low-pass: a tone above the card's Nyquist must not fold back; a tone in the band must pass
static double tone_rms(uint32_t native, uint32_t out_rate, double hz, bool lpf, std::vector<int16_t>* keep = nullptr, int chunk = 0)
{
    FakeCore c; c.sine_hz = hz; c.sine_amp = 16000; c.native = native;
    Eng e(c, native, 0); e.set_lowpass(lpf);
    e.prime(10);
    const int total = (int)out_rate / 2; std::vector<int16_t> pcm(total * 2);
    if (chunk) for (int p = 0; p < total; p += chunk) e.render(pcm.data() + p * 2, p + chunk > total ? total - p : chunk, out_rate, false);
    else e.render(pcm.data(), total, out_rate, false);
    if (keep) *keep = pcm;
    double sum = 0; int n = 0;
    for (int i = total / 4; i < total; i++) { sum += (double)pcm[2 * i] * pcm[2 * i]; n++; }   // after the filter settles
    return std::sqrt(sum / n);
}
static void test_antialias_lowpass()
{
    printf("resampler: anti-alias low-pass\n");
    const double full = 16000 / std::sqrt(2.0);
    for (uint32_t native : {64000u, 66207u})
        for (uint32_t out : {11025u, 22050u, 32000u, 44100u, 48000u})
        {
            double stop = 20 * std::log10(tone_rms(native, out, 0.75 * out > 0.45 * native ? 0.45 * native : 0.75 * out, true) / full);
            double far = 20 * std::log10(tone_rms(native, out, 1.0 * out > 0.45 * native ? 0.45 * native : 1.0 * out, true) / full);
            double pass = 20 * std::log10(tone_rms(native, out, 0.1 * out, true) / full);
            double nolpf = 20 * std::log10(tone_rms(native, out, 0.75 * out > 0.45 * native ? 0.45 * native : 0.75 * out, false) / full);
            printf("  native %u -> %u: 1.5x Nyquist %.1f dB, 2x %.1f dB (without the filter %.1f dB), in band %.2f dB\n", native, out, stop, far, nolpf, pass);
            CHECK(stop < -30, "native %u out %u: tone at 1.5x Nyquist only %.1f dB down", native, out, stop);
            CHECK(far < -45, "native %u out %u: tone at 2x Nyquist only %.1f dB down", native, out, far);
            CHECK(nolpf > -20, "native %u out %u: test not sensitive (no filter: %.1f dB)", native, out, nolpf);
            CHECK(pass > -0.6 && pass < 0.3, "native %u out %u: in-band tone changed by %.2f dB", native, out, pass);
        }
    std::vector<int16_t> a, b;
    tone_rms(64000, 22050, 3000, true, &a, 0); tone_rms(64000, 22050, 3000, true, &b, 37);
    CHECK(a == b, "low-pass: chunked render differs");
    // the ST's tones (17 and 32 kHz at 66207 Hz) at 22050: must be far down
    double st = 20 * std::log10(tone_rms(66207, 22050, 32061, true) / full), st2 = 20 * std::log10(tone_rms(66207, 22050, 17077, true) / full);
    printf("  SC-55ST tones at 22050: 32 kHz %.1f dB, 17 kHz %.1f dB\n", st, st2);
    CHECK(st < -50 && st2 < -40, "ST tones not removed: %.1f / %.1f dB", st, st2);
}

static void test_dc_offset_and_gain_and_mix()
{
    printf("DC offset, gain, saturating mix\n");
    {
        FakeCore c;
        c.silent = true;
        c.bias = 512; // MK1 DC bias
        Eng e(c, 64000, 512);
        e.set_lowpass(false);   // exact gain arithmetic
        CHECK(e.prime(10), "prime");
        std::vector<int16_t> out(200 * 2, 99);
        e.render(out.data(), 200, 48000, false);
        bool all0 = true;
        for (auto v : out) all0 &= (v == 0);
        CHECK(all0, "MK1 bias of 512 must be removed (got %d)", out[100]);
    }
    {
        FakeCore c;
        c.slope = 0;
        c.bias = 20000;
        Eng e(c, 64000, 0);
        e.set_lowpass(false);   // exact gain arithmetic
        e.prime(10);
        e.set_gain_percent(200);
        std::vector<int16_t> out(10 * 2);
        e.render(out.data(), 10, 48000, false);
        CHECK(out[0] == 32767, "200%% gain must clamp, got %d", out[0]);
        e.set_gain_percent(50);
        e.render(out.data(), 10, 48000, false);
        CHECK(out[0] == 10000, "50%% gain, got %d", out[0]);
        e.set_gain_percent(100);
        e.set_mix_volume_q8(128); // host (SB16) mixer at 50% on top of 100% gain
        e.render(out.data(), 10, 48000, false);
        CHECK(out[0] == 10000, "50%% mixer volume, got %d", out[0]);
        e.set_mix_volume_q8(0);
        e.render(out.data(), 10, 48000, false);
        CHECK(out[0] == 0, "mixer muted, got %d", out[0]);
        e.set_mix_volume_q8(1000); // clamps to 100%
        e.render(out.data(), 10, 48000, false);
        CHECK(out[0] == 20000, "mixer volume clamps to unity, got %d", out[0]);
        std::vector<int16_t> mix(10 * 2, 20000);
        e.render(mix.data(), 10, 48000, true);
        CHECK(mix[0] == 32767 && mix[1] == 32767, "mix must saturate, got %d", mix[0]);
        std::vector<int16_t> mix2(10 * 2, -30000);
        e.render(mix2.data(), 10, 48000, true);
        CHECK(mix2[0] == -10000, "mix of -30000 + 20000, got %d", mix2[0]);
    }
}

static void test_midi_throttling_and_order()
{
    printf("MIDI queue: throttling, ordering, wrap\n");
    FakeCore c;
    Eng e(c, 64000, 0);
    e.prime(10);
    std::vector<uint8_t> sent;
    std::vector<int16_t> out(256 * 2);
    uint32_t rng = 1;
    // push more than the UART ring (16) and more than one queue lap (65536)
    int posted = 0;
    for (int round = 0; round < 40; round++)
    {
        for (int i = 0; i < 4000 && posted < 150000; i++)
        {
            rng = rng * 1664525u + 1013904223u;
            uint8_t b = (uint8_t)(rng >> 24);
            if (!e.post_byte(b)) break;
            sent.push_back(b);
            posted++;
        }
        // drain with enough rendering
        for (int k = 0; k < 60; k++) e.render(out.data(), 256, 48000, false);
    }
    for (int k = 0; k < 400 && e.queued(); k++) e.render(out.data(), 256, 48000, false);
    CHECK(e.queued() == 0, "queue not drained, %u left", e.queued());
    CHECK(c.received == sent, "bytes lost/reordered: sent %zu recv %zu", sent.size(), c.received.size());
    CHECK(c.max_used <= c.uart_cap, "UART overfilled: %u > %u", c.max_used, c.uart_cap);
    CHECK(sent.size() > 65536, "test should wrap the ring (%zu bytes)", sent.size());

    // busy flag
    FakeCore c2;
    Eng e2(c2, 64000, 0);
    e2.prime(10);
    CHECK(!e2.busy(), "idle queue must not be busy");
    for (uint32_t i = 0; i < Eng::kBusyMark; i++) e2.post_byte(0x90);
    CHECK(e2.busy(), "queue above watermark must report busy");
    bool full_rejects = false;
    for (uint32_t i = 0; i < Eng::kRing; i++) full_rejects |= !e2.post_byte(0x80);
    CHECK(full_rejects, "a full queue must reject bytes");
}

static void test_idle_gating()
{
    printf("idle gating\n");
    FakeCore c;
    c.silent = true;
    Eng e(c, 64000, 0);
    e.prime(10);
    CHECK(!e.active(), "freshly primed silent engine must be inactive");
    e.post_byte(0x90);
    CHECK(e.active(), "MIDI byte must wake the engine");
    std::vector<int16_t> out(24000 * 2);
    // wake window is 750 ms: still active after 0.5 s, asleep after 1 s
    e.render(out.data(), 24000, 48000, false);
    CHECK(e.active(), "must still be awake 0.5 s after MIDI");
    e.render(out.data(), 24000, 48000, false);
    CHECK(!e.active(), "silent engine must go back to sleep after the wake window");
    e.post_byte(0x80);
    CHECK(e.active(), "second byte wakes it again");

    // audible output keeps it awake
    FakeCore c2;
    c2.slope = 0;
    c2.bias = 5000; // constant loud signal
    Eng e2(c2, 64000, 0);
    e2.prime(10);
    e2.post_byte(0x90);
    for (int s = 0; s < 10; s++)
        for (int k = 0; k < 10; k++) e2.render(out.data(), 4800, 48000, false);
    CHECK(e2.active(), "audible output must keep the engine active");
}

static void test_stall_detection()
{
    printf("stall detection\n");
    FakeCore c;
    c.dead = true;
    Eng e(c, 64000, 0);
    CHECK(!e.prime(10), "prime must fail for a core that never produces audio");
    CHECK(e.stalled(), "stalled flag");
    std::vector<int16_t> out(8 * 2, 7);
    e.render(out.data(), 8, 48000, false);
    CHECK(out[0] == 0 && out[15] == 0, "overwrite mode must output silence, not stale data, for a dead core");
    CHECK(!e.active(), "a stalled core must not ask for more CPU time");
    std::vector<int16_t> mix(8 * 2, 7);
    e.render(mix.data(), 8, 48000, true);
    CHECK(mix[0] == 7 && mix[15] == 7, "mix mode must leave the existing audio untouched");
}

static void test_preempting_producer()
{
    printf("producer preempting the consumer\n");
    FakeCore c;
    c.uart_cap = 4096;
    Eng* ep = nullptr;
    static std::vector<uint8_t> sent;
    sent.clear();
    c.hook = [](FakeCore& core)
    {
        // Simulates a timer-ISR / trap handler writing MIDI in the middle of
        // the consumer's feed()/step() loop.
        static uint32_t rng = 7;
        if ((core.step_count % 11) == 0)
        {
            Eng* e = static_cast<Eng*>(core.hook_ud);
            rng = rng * 1664525u + 1013904223u;
            uint8_t b = (uint8_t)(rng >> 24);
            if (e->post_byte(b)) sent.push_back(b);
        }
    };
    Eng e(c, 64000, 0);
    ep = &e;
    c.hook_ud = ep;
    e.prime(10);
    std::vector<int16_t> out(512 * 2);
    for (int k = 0; k < 2000; k++) e.render(out.data(), 512, 44100, false);
    c.hook = nullptr;
    for (int k = 0; k < 100 && e.queued(); k++) e.render(out.data(), 512, 44100, false);
    CHECK(e.queued() == 0, "left over %u", e.queued());
    CHECK(c.received == sent, "bytes lost or reordered under preemption (sent %zu, got %zu)", sent.size(), c.received.size());
    CHECK(sent.size() > 1000, "hook should have produced traffic, got %zu", sent.size());
}

int main()
{
    test_resample_linear_and_chunk_invariance();
    test_antialias_lowpass();
    test_dc_offset_and_gain_and_mix();
    test_midi_throttling_and_order();
    test_idle_gating();
    test_stall_detection();
    test_preempting_producer();
    printf(g_fail ? "\n%d FAILURE(S)\n" : "\nall engine tests passed\n", g_fail);
    return g_fail ? 1 : 0;
}
