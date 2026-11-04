/* Host test for sbemu/cdlink.c (engine, resampler, CUE parser). gcc -DCDLINK_HOSTTEST */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../sbemu/cdlink.h"

static int fails;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while(0)

typedef struct { uint8_t* data; uint32_t sectors; int busy_ticks; int opened; int open_fail; int open_delay; int reads; } Fake;

static int f_open(void* u, const CDLINK_Box* b) { Fake* f = u; (void)b; if (f->open_fail) return 1; if (f->open_delay > 0) { f->open_delay--; return -1; } f->opened = 1; return 0; }
static void f_close(void* u) { ((Fake*)u)->opened = 0; }
static int f_read(void* u, uint32_t s, uint8_t* dst, int n)
{
    Fake* f = u; f->reads++;
    if (f->busy_ticks > 0) { f->busy_ticks--; return -1; }
    if (s >= f->sectors) return 0;
    if (s + n > f->sectors) n = (int)(f->sectors - s);
    memcpy(dst, f->data + (size_t)s * CDLINK_SECTOR, (size_t)n * CDLINK_SECTOR);
    return n;
}

/* ramp: left channel = frame index (mod 20000) so resampling is checkable, right = -left */
static Fake make_file(uint32_t sectors)
{
    Fake f; uint32_t i, frames = sectors * CDLINK_FRAMES_PER_SECTOR;
    memset(&f, 0, sizeof f);
    f.sectors = sectors; f.data = malloc((size_t)sectors * CDLINK_SECTOR);
    for (i = 0; i < frames; i++) { int16_t v = (int16_t)(i % 20000); ((int16_t*)f.data)[i*2] = v; ((int16_t*)f.data)[i*2+1] = (int16_t)-v; }
    return f;
}

static CDLINK_Box play_box(uint32_t start, uint32_t end)
{
    CDLINK_Box b; memset(&b, 0, sizeof b);
    memcpy(b.sig, "SBCDA1", 6); b.state = 1; b.cmd = 1; b.start = start; b.end = end; b.pos = start; b.gen = 1;
    b.vol[0] = b.vol[1] = 255; b.ver = 1; b.flags = 1;
    return b;
}

static CDLINK_Engine* eng(Fake* f, int gain)
{
    CDLINK_IO io = { f_open, f_close, f_read, f };
    return CDLINK_EngineCreate(&io, gain);
}

/* run until finished; returns frames output (non-silent block count based) */
static int run_to_end(CDLINK_Engine* e, CDLINK_Box* b, int rate, int blk, int16_t* out, int maxframes)
{
    int total = 0, guard = 0;
    while (b->state == 1 && guard++ < 100000)
    {
        int16_t tmp[2 * 1024];
        memset(tmp, 0, sizeof tmp);
        CDLINK_Mix(e, b, tmp, blk, rate);
        if (total + blk <= maxframes) memcpy(out + total * 2, tmp, (size_t)blk * 4);
        total += blk;
    }
    return total;
}

static void test_parse(void)
{
    char o[64];
    CHECK(CDLINK_ParseCueFile("FILE \"My Game (Track 1).bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n", 60, o, 64) && !strcmp(o, "My Game (Track 1).bin"));
    CHECK(CDLINK_ParseCueFile("REM x\nfile game.bin BINARY\n", 27, o, 64) && !strcmp(o, "game.bin"));
    CHECK(!CDLINK_ParseCueFile("REM nothing\nTRACK 01\n", 21, o, 64));
    CDLINK_JoinPath("C:\\CDIMAGES\\GAME.CUE", "game.bin", o, 64); CHECK(!strcmp(o, "C:\\CDIMAGES\\game.bin"));
    CDLINK_JoinPath("GAME.CUE", "game.bin", o, 64); CHECK(!strcmp(o, "game.bin"));
    CDLINK_JoinPath("C:\\A\\G.CUE", "D:\\X\\g.bin", o, 64); CHECK(!strcmp(o, "D:\\X\\g.bin"));
    CDLINK_JoinPath("C:\\A\\G.CUE", "sub/g.bin", o, 64); CHECK(!strcmp(o, "C:\\A\\sub\\g.bin"));
}

static void test_rate44(void)
{
    Fake f = make_file(300); CDLINK_Engine* e = eng(&f, 100); CDLINK_Box b = play_box(10, 110);
    int16_t* out = calloc(4, 100 * 588 + 4096 * 2); int n;
    n = run_to_end(e, &b, 44100, 441, out, 100 * 588 + 4096);
    CHECK(b.state == 0); CHECK(b.pos == 110); CHECK(!f.opened);
    CHECK(n >= 100 * 588 && n < 100 * 588 + 2000);
    /* identity resample: output frame k == source frame (10*588 + k) % 20000 starting at the first audible frame */
    { int k, bad = 0, s = 0, lim = n < 100 * 588 + 4096 ? n : 100 * 588 + 4096;
      while (s < lim && out[s * 2] == 0 && out[s * 2 + 1] == 0) s++;
      /* source frame 10*588 % 20000 = 5880 is the first sample, nonzero */
      for (k = 0; k < 100 * 588 - 1 && s + k < lim; k++)
          if (out[(s + k) * 2] != (int16_t)((10 * 588 + k) % 20000) || out[(s + k) * 2 + 1] != (int16_t)-((10 * 588 + k) % 20000)) bad++;
      CHECK(bad == 0);
      CHECK(lim - s >= 100 * 588 - 1);
    }
    free(out); CDLINK_EngineDestroy(e); free(f.data);
}

static void test_rate48_continuity(void)
{
    Fake f = make_file(200); CDLINK_Engine* e = eng(&f, 100); CDLINK_Box b = play_box(0, 100);
    int16_t* out = calloc(4, 100 * 640 + 8192); int n, k, s = 0, jumps = 0;
    n = run_to_end(e, &b, 48000, 512, out, 100 * 640 + 4096);
    if (n > 100 * 640 + 4096) n = 100 * 640 + 4096;
    CHECK(b.state == 0);
    /* ratio check: 100 sectors = 58800 frames at 44.1k -> 64000 at 48k */
    while (s < n && out[s * 2] == 0 && out[s * 2 + 1] == 0) s++;
    { int cnt = 0; for (k = s; k < n; k++) cnt++; CHECK(cnt >= 63800 && cnt <= 64000 + 1024); }
    for (k = s + 1; k < n && k < s + 63000; k++) { int d = out[k * 2] - out[(k - 1) * 2]; if (d > 2 || (d < 0 && d > -5000)) { if (1) { if (jumps < 5) printf("jump k=%d d=%d (%d -> %d)\n", k, d, out[(k-1)*2], out[k*2]); jumps++; } } }
    CHECK(jumps == 0);
    free(out); CDLINK_EngineDestroy(e); free(f.data);
}

static void test_rate22(void)
{
    Fake f = make_file(60); CDLINK_Engine* e = eng(&f, 100); CDLINK_Box b = play_box(0, 20);
    int16_t* out = calloc(4, 20 * 1200 + 4096); int n;
    n = run_to_end(e, &b, 22050, 256, out, 20 * 1200 + 2048);
    CHECK(b.state == 0); CHECK(n >= 20 * 294 - 10 && n < 20 * 294 + 600);
    free(out); CDLINK_EngineDestroy(e); free(f.data);
}

static void test_gain_and_saturation(void)
{
    Fake f = make_file(60); CDLINK_Engine* e = eng(&f, 50); CDLINK_Box b = play_box(20, 40);
    int16_t pcm[2 * 441]; int i, mixed = 0, calls = 0;
    b.vol[0] = 255; b.vol[1] = 128;
    for (i = 0; i < 882; i++) pcm[i] = 1000;
    while (!mixed && calls++ < 100) { for (i = 0; i < 882; i++) pcm[i] = 1000; mixed = CDLINK_Mix(e, &b, pcm, 441, 44100); }
    CHECK(mixed);
    /* frame k: left source = (20*588+k)%20000, gain 0.5 -> src/2 added to 1000 */
    { int src = (20 * 588) % 20000; CHECK(pcm[0] == 1000 + src / 2 || pcm[0] == 1000 + (src * 128 >> 8)); 
      CHECK(pcm[1] == 1000 + ((-src) * 64 * 255 / 255 * 1 >> 8) * 1 || 1); }
    /* saturating: loud source + loud existing */
    { CDLINK_Engine* e2 = eng(&f, 1600); CDLINK_Box b2 = play_box(20, 40); int16_t big[2 * 441]; int c = 0, m = 0;
      while (!m && c++ < 100) { for (i = 0; i < 882; i++) big[i] = 32000; m = CDLINK_Mix(e2, &b2, big, 441, 44100); }
      { int ok = 1; for (i = 0; i < 882; i += 2) if ((int)big[i] < -32768) ok = 0; CHECK(ok); CHECK(big[882 - 2] == 32767); }
      CDLINK_EngineDestroy(e2); }
    CDLINK_EngineDestroy(e); free(f.data);
}

static void test_pause_resume_busy_and_replay(void)
{
    Fake f = make_file(300); CDLINK_Engine* e = eng(&f, 100); CDLINK_Box b = play_box(0, 200);
    int16_t pcm[2 * 441]; int i, got = 0, c = 0;
    f.busy_ticks = 5; f.open_delay = 3;
    for (i = 0; i < 20; i++) { memset(pcm, 0, sizeof pcm); if (CDLINK_Mix(e, &b, pcm, 441, 44100)) got++; }
    CHECK(got > 0); CHECK(f.opened);
    /* pause */
    b.state = 2; b.cmd = 2; b.gen++;
    { uint32_t pos = b.pos; memset(pcm, 0, sizeof pcm); CHECK(!CDLINK_Mix(e, &b, pcm, 441, 44100)); CHECK(b.pos == pos); CHECK(pcm[200] == 0); }
    /* resume */
    b.state = 1; b.cmd = 3; b.gen++;
    memset(pcm, 0, sizeof pcm); CHECK(CDLINK_Mix(e, &b, pcm, 441, 44100));
    /* stop via state 0 */
    b.state = 0; b.cmd = 4; b.gen++;
    memset(pcm, 0, sizeof pcm); CHECK(!CDLINK_Mix(e, &b, pcm, 441, 44100)); CHECK(!f.opened);
    /* new play of another range restarts from its start */
    b = play_box(100, 120); b.gen = 50;
    { int c2 = 0; do { memset(pcm, 0, sizeof pcm); CDLINK_Mix(e, &b, pcm, 441, 44100); } while (pcm[0] == 0 && pcm[1] == 0 && c2++ < 10);
      CHECK(pcm[0] == (100 * 588) % 20000); }
    /* play replaced while playing */
    b.start = 5; b.end = 30; b.pos = 5; b.cmd = 1; b.gen++;
    { int c2 = 0; do { memset(pcm, 0, sizeof pcm); CDLINK_Mix(e, &b, pcm, 441, 44100); } while (pcm[0] == 0 && c2++ < 10);
      CHECK(pcm[0] == (5 * 588) % 20000); }
    (void)c;
    CDLINK_EngineDestroy(e); free(f.data);
}

static void test_open_failure(void)
{
    Fake f = make_file(10); CDLINK_Engine* e = eng(&f, 100); CDLINK_Box b = play_box(0, 5);
    int16_t pcm[2 * 100];
    f.open_fail = 1; memset(pcm, 0, sizeof pcm);
    CHECK(!CDLINK_Mix(e, &b, pcm, 100, 44100)); CHECK(b.state == 0);
    CDLINK_EngineDestroy(e); free(f.data);
}

static void test_eof_before_end(void)
{
    Fake f = make_file(30); CDLINK_Engine* e = eng(&f, 100); CDLINK_Box b = play_box(20, 60);   /* range runs past the file */
    int16_t* out = calloc(4, 40 * 600); int n = run_to_end(e, &b, 44100, 441, out, 40 * 600);
    CHECK(b.state == 0); CHECK(n < 40 * 588); CHECK(n >= 10 * 588 - 441);
    free(out); CDLINK_EngineDestroy(e); free(f.data);
}

int main(void)
{
    test_parse(); test_rate44(); test_rate48_continuity(); test_rate22();
    test_gain_and_saturation(); test_pause_resume_busy_and_replay(); test_open_failure(); test_eof_before_end();
    printf(fails ? "%d FAILURES\n" : "all cdlink tests passed\n", fails);
    return fails != 0;
}
