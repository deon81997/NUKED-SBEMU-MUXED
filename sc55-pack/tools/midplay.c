/* MIDPLAY - plays Standard MIDI Files (.MID .MIDI .RMI .KAR, format 0 and 1) through the MPU-401 at port 330h,
 * i.e. through the running SBEMU + SC-55 (or MT-32 / SoundFont) emulation. SBEMU must be loaded (SC55.BAT).
 *
 *   MIDPLAY [options] [file.mid ...]     (wildcards work: MIDPLAY *.MID; no files = the last playlist, or every
 *                                         MIDI file in MIDPLAY's folder - see Options)
 *     /D:dir  play that folder   /B file explorer first   /L repeat all   /R1 repeat one   /H shuffle
 *     /GM GM On instead of the GS reset   /N no reset   /P:330 MPU port   /S plain one-line display
 *     test rig: /X no MPU (nothing sent)  /T:file timing statistics  /Q:sec quit after sec  /V:n view
 *
 * Pages (TAB / Shift+TAB, F1 help, F2 options): Player (channel table, SC-55 front panel or 3D), Playlist,
 * Files. The music keeps playing on every page; the next song starts by itself.
 *
 * Timing: the MIDI events are sent by mp_pump(), which runs from the main loop AND from inside every drawing
 * loop (tui_hook / m3_hook: between screen rows, VESA scan lines and 3D scan lines), so a slow frame never makes
 * the music late. The clock is the CPU's time stamp counter, re-synchronised to the BIOS timer every tick (SBEMU
 * reprograms the PIT, so uclock() is unreliable under it, and reading the PIT is slow).
 *
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>

#ifdef HOSTTEST
/* host build: virtual clock, MIDI bytes are printed with their time instead of sent */
static uint64_t g_now_us;
static void out_byte(unsigned char b) { printf("%llu %02X\n", (unsigned long long)g_now_us, b); }
static uint64_t now_us(void) { return g_now_us; }
static unsigned long g_busy_waits;
static int mpu_busy(void) { return 0; }
static __attribute__((unused)) void sleep_ms(int ms) { g_now_us += (uint64_t)ms * 1000; }
#else
#include <pc.h>
#include <conio.h>
#include <time.h>
#include <dos.h>
#include <dir.h>
#include "tui.h"
#include "mpuport.h"

static int base = 0x330;
static int g_dry;               /* /X: no MPU (timing tests): nothing is sent */

/* ---- clock: the CPU's time stamp counter. Its rate is measured against the BIOS tick (54.9254 ms) over
 * windows of ~1 s; a different rate is taken only when two windows in a row agree (SpeedStep), so a timer
 * interrupt held off for a while (video BIOS calls) or a torn read of the BIOS counter cannot fool it. The
 * TSC keeps counting while interrupts are off, so the music never stalls or jumps. Without a TSC: uclock(). */
static int g_tsc;
static int g_win;               /* running in a Windows (3.x enhanced / 9x) DOS box: INT 2Fh AX=1600h */
static struct { uint64_t tsc0, last, wtsc; double base, per_us, cand; uint32_t wtick; } CK;
static uint64_t rdtsc64(void) { uint64_t t; asm volatile("rdtsc" : "=A"(t)); return t; }
static int cpu_has_tsc(void)
{
    uint32_t f1, f2, a, b, c, d;
    asm volatile("pushfl; pushfl; popl %0; movl %0, %1; xorl $0x200000, %0; pushl %0; popfl; pushfl; popl %0; popfl"
                 : "=&r"(f1), "=&r"(f2));
    if (!((f1 ^ f2) & 0x200000)) return 0;                      /* no CPUID (486 and older) */
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    return (d >> 4) & 1;
}
static uint32_t bios_tick(void) { return _farpeekl(_dos_ds, 0x46C); }
static void clock_init(void)
{
    uint32_t t; uint64_t a;
    int win, n, tries;
    __dpmi_regs r;
    g_tsc = cpu_has_tsc();
    if (!g_tsc) return;
    /* in a Windows DOS box the BIOS tick is virtual: Windows delivers it late and in bursts, so 3 ticks
     * (165 ms) could come almost at once and the CPU looked like a few MHz - songs then ran to their end
     * in seconds. There: a whole second (18 ticks), and again while the result is not a plausible CPU. */
    memset(&r, 0, sizeof r); r.x.ax = 0x1600; __dpmi_int(0x2F, &r);
    win = r.h.al != 0 && r.h.al != 0x80;
    g_win = win;
    n = win ? 18 : 3;
    for (tries = 0; tries < 4; tries++)
    {
        t = bios_tick(); while (bios_tick() == t) {}
        t = bios_tick(); a = rdtsc64();
        while (bios_tick() - t < (uint32_t)n && bios_tick() >= t) {}
        CK.per_us = (double)(rdtsc64() - a) / (n * 54925.4);
        if (CK.per_us >= 20 && CK.per_us <= 10000) break;      /* 20 MHz .. 10 GHz */
        n = 18;
    }
    if (CK.per_us < 1) CK.per_us = 1;
    CK.tsc0 = CK.wtsc = rdtsc64(); CK.base = 0; CK.last = 0; CK.wtick = bios_tick(); CK.cand = 0;
}
static uint64_t now_us(void)
{
    uint64_t c, u; uint32_t t;
    if (!g_tsc) return (uint64_t)uclock() * 1000000ULL / UCLOCKS_PER_SEC;
    c = rdtsc64(); t = bios_tick();
    if (t > CK.wtick && t - CK.wtick >= 18)
    {
        if (t - CK.wtick < 200)
        {
            double r = (double)(c - CK.wtsc) / ((t - CK.wtick) * 54925.4), dev = r / CK.per_us - 1;
            if (dev < 0) dev = -dev;
            if (dev > 0.002 && r > 1)
            {   /* a new rate: right away if small, after a second window agreeing if large */
                double cd = CK.cand ? r / CK.cand - 1 : 1; if (cd < 0) cd = -cd;
                if (dev < 0.05 || cd < 0.05)
                {
                    CK.base += (double)(c - CK.tsc0) / CK.per_us; CK.tsc0 = c;   /* continuous at the change */
                    CK.per_us = r; CK.cand = 0;
                }
                else CK.cand = r;
            }
            else CK.cand = 0;
        }
        CK.wtick = t; CK.wtsc = c;
    }
    else if (t < CK.wtick && CK.wtick - t > 1000) { CK.wtick = t; CK.wtsc = c; }   /* midnight: a new window */
    u = (uint64_t)(CK.base + (double)(c - CK.tsc0) / CK.per_us);
    if (u < CK.last) u = CK.last;
    CK.last = u;
    return u;
}
static __attribute__((unused)) void sleep_ms(int ms) { uint64_t t = now_us() + (uint64_t)ms * 1000; while (now_us() < t) {} }

/* ---- keys: the BIOS buffer is looked at directly (no mode switch per loop); INT 16h AH=10h reads them, so
 * Ctrl+arrows, F11/F12 and the grey keys arrive too. Normal keys as is, extended keys as 0x100 | scan code. */
#define K_DEL    0x153
#define K_INS    0x152
#define K_CUP    0x18D
#define K_CDOWN  0x191
#define K_CLEFT  0x173
#define K_CRIGHT 0x174
#define K_F4     0x13E
#define K_F5     0x13F
#define K_F6     0x140
#define K_F7     0x141
#define K_F8     0x142
#define K_F9     0x143
#define K_F10    0x144
#define K_BKSP   8
#define K_CS     0x13      /* Ctrl+S */
#define K_CO     0x0F      /* Ctrl+O */
#define K_CA     0x01      /* Ctrl+A */
#define K_CP     0x10      /* Ctrl+P */
static int key_pressed(void) { return _farpeekw(_dos_ds, 0x41A) != _farpeekw(_dos_ds, 0x41C); }
static int key_read(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r); r.h.ah = 0x10; __dpmi_int(0x16, &r);
    if (r.h.al == 0 || (r.h.al == 0xE0 && r.h.ah)) return 0x100 | r.h.ah;
    return r.h.al;
}

static unsigned long g_bytes_out, g_notes_out;   /* /T statistics: MIDI bytes and note-ons sent */
static unsigned long g_busy_waits;                /* /T: times the synth's queue was full (events sent later) */
static int mpu_busy(void) { return !g_dry && (inportb(base + 1) & 0x40); }
static void out_byte(unsigned char b)
{
    uint64_t end;
    if (g_dry) return;
    g_bytes_out++;
    end = now_us() + 1000000;
    while ((inportb(base + 1) & 0x40) && now_us() < end) {} /* DRR busy */
    outportb(base, b);
}
static int mpu_detect(void)
{
    uint64_t end;
    outportb(base + 1, 0xFF);
    end = now_us() + 500000;
    while (now_us() < end)
        if (!(inportb(base + 1) & 0x80)) return inportb(base) == 0xFE;
    return 0;
}
static void mpu_uart(void) { outportb(base + 1, 0x3F); }
#endif

enum { EV_SHORT = 0, EV_SYSEX = 1, EV_TEMPO = 2 };

typedef struct
{
    uint32_t tick;
    uint32_t seq;
    uint64_t us;        /* filled in by schedule() */
    uint32_t arg;       /* short: status | d1<<8 | d2<<16 ; sysex: offset in file ; tempo: us per quarter */
    uint16_t len;       /* short: 1..3 bytes ; sysex: length incl. leading F0/F7 handled by flag */
    uint8_t kind;
    uint8_t lead;       /* sysex: first byte to send (0xF0), or 0 for raw F7 packets */
} Event;

typedef struct
{
    unsigned char* data;
    long size;
    Event* ev;
    long nev, cap;
    int format, ntrks, division;
    uint32_t last_tick;
    char title[48];
} Song;

static int add_event(Song* s, uint32_t tick, int kind, uint32_t arg, int len, int lead)
{
    Event* e;
    if (s->nev == s->cap)
    {
        long nc = s->cap ? s->cap * 2 : 4096;
        Event* n = (Event*)realloc(s->ev, (size_t)nc * sizeof(Event));
        if (!n) return 0;
        s->ev = n; s->cap = nc;
    }
    e = &s->ev[s->nev];
    e->tick = tick; e->seq = (uint32_t)s->nev; e->us = 0; e->arg = arg; e->len = (uint16_t)len;
    e->kind = (uint8_t)kind; e->lead = (uint8_t)lead;
    s->nev++;
    if (tick > s->last_tick) s->last_tick = tick;
    return 1;
}

static uint32_t be32(const unsigned char* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static int be16(const unsigned char* p) { return (p[0] << 8) | p[1]; }

/* read a variable-length quantity; returns 0 on overrun */
static int varlen(const unsigned char** p, const unsigned char* end, uint32_t* out)
{
    uint32_t v = 0; int i;
    for (i = 0; i < 4; i++)
    {
        if (*p >= end) return 0;
        v = (v << 7) | (**p & 0x7F);
        if (!(*(*p)++ & 0x80)) { *out = v; return 1; }
    }
    return 0;
}

static int parse_track(Song* s, const unsigned char* p, const unsigned char* end)
{
    uint32_t tick = 0, d, l;
    int running = 0;
    while (p < end)
    {
        int st;
        if (!varlen(&p, end, &d)) return 1;     /* truncated: keep what we have */
        tick += d;
        if (p >= end) return 1;
        st = *p;
        if (st >= 0x80) { p++; if (st < 0xF0) running = st; }
        else if (running) st = running;
        else { p++; continue; }                  /* stray data byte without any status: skip it, keep the track */
        if (st >= 0xF0)
        {
            /* running status is NOT cancelled by meta or SysEx events here: many files (lyrics in .KAR files,
               markers) go on with running status after them, and the strict rule dropped the rest of the track */
            if (st == 0xFF)
            {
                int type;
                if (p >= end) return 1;
                type = *p++;
                if (!varlen(&p, end, &l) || p + l > end) return 1;
                if (type == 0x51 && l == 3)
                {
                    if (!add_event(s, tick, EV_TEMPO, ((uint32_t)p[0] << 16) | (p[1] << 8) | p[2], 0, 0)) return 0;
                }
                else if (type == 0x03 && !s->title[0] && l > 0)
                {
                    uint32_t k, n = 0;
                    for (k = 0; k < l && n < sizeof(s->title) - 1; k++)
                        s->title[n++] = (p[k] >= 32 && p[k] < 127) ? (char)p[k] : ' ';
                    s->title[n] = 0;
                }
                else if (type == 0x2F) return 1;
                p += l;
            }
            else if (st == 0xF0 || st == 0xF7)
            {
                if (!varlen(&p, end, &l) || p + l > end) return 1;
                /* F0: the stored data already ends with F7. F7: raw continuation / escape bytes. */
                if (!add_event(s, tick, EV_SYSEX, (uint32_t)(p - s->data), (int)(l > 65535 ? 65535 : l), st == 0xF0 ? 0xF0 : 0)) return 0;
                p += l;
            }
            else return 1;                       /* other system messages do not occur in files */
        }
        else
        {
            int n = ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2;
            uint32_t a = (uint32_t)st;
            if (p + n > end) return 1;
            a |= (uint32_t)(p[0] & 0x7F) << 8;
            if (n == 2) a |= (uint32_t)(p[1] & 0x7F) << 16;
            p += n;
            if (!add_event(s, tick, EV_SHORT, a, n + 1, 0)) return 0;
        }
    }
    return 1;
}

static int rank(const Event* e)
{
    if (e->kind == EV_TEMPO) return 0;
    if (e->kind == EV_SHORT)
    {
        int hi = e->arg & 0xF0;
        if (hi == 0x80 || (hi == 0x90 && ((e->arg >> 16) & 0x7F) == 0)) return 1; /* note off first */
        if (hi == 0x90) return 3;                                                   /* note on last */
    }
    return 2;
}

static int ev_cmp(const void* a, const void* b)
{
    const Event* x = (const Event*)a; const Event* y = (const Event*)b;
    int rx, ry;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    rx = rank(x); ry = rank(y);
    if (rx != ry) return rx < ry ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* turn ticks into microseconds, honouring tempo changes (or SMPTE timing) */
static void schedule(Song* s)
{
    uint64_t us = 0; uint32_t last = 0, tempo = 500000;
    long i;
    int smpte = (s->division & 0x8000) != 0;
    uint64_t tpf_fps = 0;
    if (smpte)
    {
        int fps = 256 - ((s->division >> 8) & 0xFF), tpf = s->division & 0xFF;
        if (fps == 29) tpf_fps = (uint64_t)tpf * 29970 / 1000; else tpf_fps = (uint64_t)tpf * (uint64_t)fps;
        if (!tpf_fps) tpf_fps = 1;
    }
    for (i = 0; i < s->nev; i++)
    {
        Event* e = &s->ev[i];
        uint64_t dt = e->tick - last;
        if (smpte) us += dt * 1000000ULL / tpf_fps;
        else us += dt * (uint64_t)tempo / (uint64_t)(s->division ? s->division : 96);
        last = e->tick;
        e->us = us;
        if (e->kind == EV_TEMPO && e->arg) tempo = e->arg;
    }
}

static void song_free(Song* s) { free(s->data); free(s->ev); memset(s, 0, sizeof *s); }

static int song_load(Song* s, const char* name, char* err, size_t errsz)
{
    FILE* f = fopen(name, "rb");
    const unsigned char* p; const unsigned char* end;
    long got;
    memset(s, 0, sizeof *s);
    if (!f) { snprintf(err, errsz, "cannot open"); return 0; }
    fseek(f, 0, SEEK_END); s->size = ftell(f); fseek(f, 0, SEEK_SET);
    if (s->size < 14 || s->size > 8L * 1024 * 1024) { fclose(f); snprintf(err, errsz, "bad size"); return 0; }
    s->data = (unsigned char*)malloc((size_t)s->size);
    if (!s->data) { fclose(f); snprintf(err, errsz, "out of memory"); return 0; }
    got = (long)fread(s->data, 1, (size_t)s->size, f);
    fclose(f);
    if (got != s->size) { song_free(s); snprintf(err, errsz, "read error"); return 0; }
    p = s->data; end = s->data + s->size;
    if (!memcmp(p, "RIFF", 4) && s->size > 20 && !memcmp(p + 8, "RMID", 4) && !memcmp(p + 12, "data", 4))
        p += 20;                                /* RIFF MIDI wrapper */
    if (end - p < 14 || memcmp(p, "MThd", 4)) { song_free(s); snprintf(err, errsz, "not a MIDI file"); return 0; }
    {
        uint32_t hl = be32(p + 4);
        if (hl < 6 || p + 8 + hl > end) { song_free(s); snprintf(err, errsz, "bad header"); return 0; }
        s->format = be16(p + 8); s->ntrks = be16(p + 10); s->division = be16(p + 12);
        p += 8 + hl;
    }
    if (s->format > 1) { song_free(s); snprintf(err, errsz, "format %d (sequences) is not supported", s->format); return 0; }
    while (end - p >= 8)
    {
        uint32_t len = be32(p + 4);
        const unsigned char* body = p + 8;
        if ((uint64_t)len > (uint64_t)(end - body)) len = (uint32_t)(end - body);
        if (!memcmp(p, "MTrk", 4))
            if (!parse_track(s, body, body + len)) { song_free(s); snprintf(err, errsz, "out of memory"); return 0; }
        p = body + len;
    }
    if (!s->nev) { song_free(s); snprintf(err, errsz, "no events"); return 0; }
    qsort(s->ev, (size_t)s->nev, sizeof(Event), ev_cmp);
    schedule(s);
    return 1;
}


/* ------------------------------------------------------------------ channel / note activity (the visualizer's data) */
static const char* const gm_names[128] = {
 "Acoustic Grand Piano","Bright Acoustic Piano","Electric Grand Piano","Honky-tonk Piano","Electric Piano 1","Electric Piano 2","Harpsichord","Clavinet",
 "Celesta","Glockenspiel","Music Box","Vibraphone","Marimba","Xylophone","Tubular Bells","Dulcimer",
 "Drawbar Organ","Percussive Organ","Rock Organ","Church Organ","Reed Organ","Accordion","Harmonica","Tango Accordion",
 "Nylon Guitar","Steel Guitar","Jazz Guitar","Clean Guitar","Muted Guitar","Overdrive Guitar","Distortion Guitar","Guitar Harmonics",
 "Acoustic Bass","Fingered Bass","Picked Bass","Fretless Bass","Slap Bass 1","Slap Bass 2","Synth Bass 1","Synth Bass 2",
 "Violin","Viola","Cello","Contrabass","Tremolo Strings","Pizzicato Strings","Orchestral Harp","Timpani",
 "String Ensemble 1","String Ensemble 2","Synth Strings 1","Synth Strings 2","Choir Aahs","Voice Oohs","Synth Voice","Orchestra Hit",
 "Trumpet","Trombone","Tuba","Muted Trumpet","French Horn","Brass Section","Synth Brass 1","Synth Brass 2",
 "Soprano Sax","Alto Sax","Tenor Sax","Baritone Sax","Oboe","English Horn","Bassoon","Clarinet",
 "Piccolo","Flute","Recorder","Pan Flute","Blown Bottle","Shakuhachi","Whistle","Ocarina",
 "Square Lead","Sawtooth Lead","Calliope Lead","Chiff Lead","Charang Lead","Voice Lead","Fifths Lead","Bass + Lead",
 "New Age Pad","Warm Pad","Polysynth Pad","Choir Pad","Bowed Pad","Metallic Pad","Halo Pad","Sweep Pad",
 "Rain FX","Soundtrack FX","Crystal FX","Atmosphere FX","Brightness FX","Goblins FX","Echoes FX","Sci-Fi FX",
 "Sitar","Banjo","Shamisen","Koto","Kalimba","Bagpipe","Fiddle","Shanai",
 "Tinkle Bell","Agogo","Steel Drums","Woodblock","Taiko Drum","Melodic Tom","Synth Drum","Reverse Cymbal",
 "Guitar Fret Noise","Breath Noise","Seashore","Bird Tweet","Telephone Ring","Helicopter","Applause","Gunshot"
};

typedef struct
{
    int prog[16], vol[16], pan[16], expr[16], rev[16], cho[16], bend[16];
    unsigned char on[16][128];       /* 1 = note currently held on this channel */
    int count[16];                   /* held notes per channel */
    int level[16];                   /* activity meter 0..127 (decays) */
    int last_note[16];               /* most recent note, -1 none */
    int nl[128];                     /* per-note brightness 0..127 for the key visualizer (decays) */
    unsigned char ncol[128];         /* channel that played the note last */
    long bpm10;                      /* tempo in tenths of BPM */
    unsigned long events;
} Viz;
static Viz g_viz;

static void viz_reset(void)
{
    int c;
    memset(&g_viz, 0, sizeof g_viz);
    for (c = 0; c < 16; c++) { g_viz.vol[c] = 100; g_viz.pan[c] = 64; g_viz.expr[c] = 127; g_viz.last_note[c] = -1; g_viz.rev[c] = 40; }
    g_viz.bpm10 = 1200;
}

static void viz_note_off(int ch, int n)
{
    if (g_viz.on[ch][n]) { g_viz.on[ch][n] = 0; if (g_viz.count[ch] > 0) g_viz.count[ch]--; }
}

static void viz_event(const Event* e)
{
    if (e->kind == EV_TEMPO) { if (e->arg) g_viz.bpm10 = (long)(600000000UL / e->arg); return; }
    if (e->kind != EV_SHORT) return;
    {
        int st = (int)(e->arg & 0xFF), ch = st & 15, d1 = (int)((e->arg >> 8) & 0x7F), d2 = (int)((e->arg >> 16) & 0x7F);
        g_viz.events++;
        switch (st & 0xF0)
        {
        case 0x90:
            if (d2 == 0) { viz_note_off(ch, d1); break; }
            if (!g_viz.on[ch][d1]) { g_viz.on[ch][d1] = 1; g_viz.count[ch]++; }
            if (d2 > g_viz.level[ch]) g_viz.level[ch] = d2;
            g_viz.nl[d1] = d2; g_viz.ncol[d1] = (unsigned char)ch; g_viz.last_note[ch] = d1;
            break;
        case 0x80: viz_note_off(ch, d1); break;
        case 0xC0: g_viz.prog[ch] = d1; break;
        case 0xE0: g_viz.bend[ch] = (d1 | (d2 << 7)) - 8192; break;
        case 0xB0:
            if (d1 == 7) g_viz.vol[ch] = d2;
            else if (d1 == 10) g_viz.pan[ch] = d2;
            else if (d1 == 11) g_viz.expr[ch] = d2;
            else if (d1 == 91) g_viz.rev[ch] = d2;
            else if (d1 == 93) g_viz.cho[ch] = d2;
            else if (d1 == 120 || d1 == 123) { int n; for (n = 0; n < 128; n++) g_viz.on[ch][n] = 0; g_viz.count[ch] = 0; }
            else if (d1 == 121) { g_viz.vol[ch] = 100; g_viz.pan[ch] = 64; g_viz.expr[ch] = 127; }
            break;
        default: break;
        }
    }
}

static void viz_decay(void)
{
    int c, n;
    for (c = 0; c < 16; c++)
    {
        if (g_viz.count[c] > 0) { g_viz.level[c] = g_viz.level[c] * 97 / 100; if (g_viz.level[c] < 30) g_viz.level[c] = 30; }
        else g_viz.level[c] = g_viz.level[c] * 82 / 100;
    }
    for (n = 0; n < 128; n++)
    {
        int held = 0;
        for (c = 0; c < 16; c++) if (g_viz.on[c][n]) { held = 1; break; }
        if (held) { if (g_viz.nl[n] > 45) g_viz.nl[n] = g_viz.nl[n] * 96 / 100; }
        else g_viz.nl[n] = g_viz.nl[n] * 78 / 100;
    }
}


/* ---- song info without loading the song: title, length, format, tracks (for the lists; ~1 ms per file) ---- */
typedef struct { int ok; long ms; char title[48]; int fmt, trk; long size; } SongInfo;
typedef struct { uint32_t tick, tempo; } TempoPt;
static int song_info(const char* path, SongInfo* si)
{
    FILE* f; unsigned char* d; long n; const unsigned char *p, *end;
    int division, ntp = 0, ctp = 0, i;
    TempoPt* tp = NULL; uint32_t maxtick = 0;
    uint64_t us = 0;
    memset(si, 0, sizeof *si); si->ok = -1;
    if (!(f = fopen(path, "rb"))) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    si->size = n;
    if (n < 14 || n > 8L * 1024 * 1024 || !(d = (unsigned char*)malloc((size_t)n))) { fclose(f); return 0; }
    if ((long)fread(d, 1, (size_t)n, f) != n) { fclose(f); free(d); return 0; }
    fclose(f);
    p = d; end = d + n;
    if (!memcmp(p, "RIFF", 4) && n > 20 && !memcmp(p + 8, "RMID", 4) && !memcmp(p + 12, "data", 4)) p += 20;
    if (end - p < 14 || memcmp(p, "MThd", 4)) { free(d); return 0; }
    {
        uint32_t hl = be32(p + 4);
        if (hl < 6 || p + 8 + hl > end) { free(d); return 0; }
        si->fmt = be16(p + 8); si->trk = be16(p + 10); division = be16(p + 12);
        p += 8 + hl;
    }
    while (end - p >= 8)
    {
        uint32_t len = be32(p + 4), tick = 0, dt, l;
        const unsigned char* q = p + 8; const unsigned char* te;
        int running = 0;
        if ((uint64_t)len > (uint64_t)(end - q)) len = (uint32_t)(end - q);
        te = q + len;
        if (!memcmp(p, "MTrk", 4))
            while (q < te)
            {
                int st;
                if (!varlen(&q, te, &dt) || q >= te) break;
                tick += dt;
                st = *q;
                if (st >= 0x80) { q++; if (st < 0xF0) running = st; }
                else if (running) st = running;
                else { q++; continue; }
                if (st == 0xFF)
                {
                    int type;
                    if (q >= te) break;
                    type = *q++;
                    if (!varlen(&q, te, &l) || q + l > te) break;
                    if (type == 0x2F) break;
                    if (type == 0x51 && l == 3)
                    {
                        if (ntp == ctp) { TempoPt* t2 = (TempoPt*)realloc(tp, (size_t)(ctp = ctp ? ctp * 2 : 16) * sizeof *tp); if (!t2) break; tp = t2; }
                        tp[ntp].tick = tick; tp[ntp].tempo = ((uint32_t)q[0] << 16) | (q[1] << 8) | q[2]; ntp++;
                    }
                    else if (type == 0x03 && !si->title[0] && l > 0)
                    {
                        uint32_t k, m = 0;
                        for (k = 0; k < l && m < sizeof si->title - 1; k++) si->title[m++] = (q[k] >= 32 && q[k] < 127) ? (char)q[k] : ' ';
                        si->title[m] = 0;
                    }
                    q += l;
                    if (tick > maxtick) maxtick = tick;
                }
                else if (st == 0xF0 || st == 0xF7)
                {
                    if (!varlen(&q, te, &l) || q + l > te) break;
                    q += l;
                    if (tick > maxtick) maxtick = tick;
                }
                else if (st >= 0xF0) break;
                else
                {
                    q += ((st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0) ? 1 : 2;
                    if (tick > maxtick) maxtick = tick;
                }
            }
        p += 8 + len;
    }
    if (division & 0x8000)
    {
        int fps = 256 - ((division >> 8) & 0xFF), tpf = division & 0xFF;
        uint64_t tf = fps == 29 ? (uint64_t)tpf * 29970 / 1000 : (uint64_t)tpf * fps;
        us = tf ? (uint64_t)maxtick * 1000000ULL / tf : 0;
    }
    else
    {
        uint32_t last = 0, tempo = 500000; int dv = division ? division : 96;
        /* tempo points by tick (insertion sort: there are few, usually already in order) */
        for (i = 1; i < ntp; i++) { TempoPt x = tp[i]; int j = i - 1; while (j >= 0 && tp[j].tick > x.tick) { tp[j + 1] = tp[j]; j--; } tp[j + 1] = x; }
        for (i = 0; i < ntp && tp[i].tick <= maxtick; i++)
        {
            us += (uint64_t)(tp[i].tick - last) * tempo / dv;
            last = tp[i].tick;
            if (tp[i].tempo) tempo = tp[i].tempo;
        }
        us += (uint64_t)(maxtick - last) * tempo / dv;
    }
    free(tp); free(d);
    si->ms = (long)(us / 1000);
    si->ok = 1;
    return 1;
}

/* ------------------------------------------------------------------ sending */
static uint16_t g_mute, g_solo;          /* channel bits: muted / solo (solo wins) */
static int audible(int ch) { return g_solo ? (g_solo >> ch) & 1 : !((g_mute >> ch) & 1); }

static void silence(void)
{
    int ch;
    { int c, n; for (c = 0; c < 16; c++) { g_viz.count[c] = 0; for (n = 0; n < 128; n++) g_viz.on[c][n] = 0; } }
    for (ch = 0; ch < 16; ch++)
    {
        out_byte((unsigned char)(0xB0 | ch)); out_byte(64); out_byte(0);   /* sustain off */
        out_byte((unsigned char)(0xB0 | ch)); out_byte(123); out_byte(0);  /* all notes off */
        out_byte((unsigned char)(0xB0 | ch)); out_byte(120); out_byte(0);  /* all sound off */
    }
}
static void channel_quiet(int ch)
{
    out_byte((unsigned char)(0xB0 | ch)); out_byte(64); out_byte(0);
    out_byte((unsigned char)(0xB0 | ch)); out_byte(123); out_byte(0);
}

static void send_event(const Song* s, const Event* e)
{
    int i;
    if (e->kind == EV_SHORT)
    {
        int st = (int)(e->arg & 0xFF);
        if ((st & 0xF0) == 0x90 && ((e->arg >> 16) & 0x7F) && !audible(st & 15)) return;   /* muted: no note-on */
#ifndef HOSTTEST
        if ((st & 0xF0) == 0x90 && ((e->arg >> 16) & 0x7F)) g_notes_out++;
#endif
        out_byte((unsigned char)st);
        if (e->len > 1) out_byte((unsigned char)((e->arg >> 8) & 0x7F));
        if (e->len > 2) out_byte((unsigned char)((e->arg >> 16) & 0x7F));
    }
    else if (e->kind == EV_SYSEX)
    {
        if (e->lead) out_byte(e->lead);
        for (i = 0; i < e->len; i++) out_byte(s->data[e->arg + (uint32_t)i]);
    }
}

static void send_reset_bytes(int mode)
{
    static const unsigned char gs[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
    static const unsigned char gm[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    int i;
    if (mode == 0) for (i = 0; i < (int)sizeof gs; i++) out_byte(gs[i]);
    else if (mode == 1) for (i = 0; i < (int)sizeof gm; i++) out_byte(gm[i]);
}

/* ------------------------------------------------------------------ timing statistics (/T:file) */
static struct { unsigned long n, late1, late2, late5, late10, late20; uint64_t sum, max; unsigned long frames; uint64_t dsum, dmax; } g_st;
static char g_stfile[260]; static long g_quit_s;
static void st_late(uint64_t late)
{
    g_st.n++; g_st.sum += late; if (late > g_st.max) g_st.max = late;
    if (late >= 1000) g_st.late1++;
    if (late >= 2000) g_st.late2++;
    if (late >= 5000) g_st.late5++;
    if (late >= 10000) g_st.late10++;
    if (late >= 20000) g_st.late20++;
}
static void st_frame(uint64_t d) { g_st.frames++; g_st.dsum += d; if (d > g_st.dmax) g_st.dmax = d; }
static void st_write(const char* label)
{
    FILE* f;
    if (!g_stfile[0] || !(f = fopen(g_stfile, "a"))) return;
#ifndef HOSTTEST
    fprintf(f, "sent: %lu MIDI bytes, %lu note-ons, synth busy %lu times\n", g_bytes_out, g_notes_out, g_busy_waits);
#endif
    fprintf(f, "%s: events %lu late avg %.2f ms max %.1f ms  >=1ms %lu >=2 %lu >=5 %lu >=10 %lu >=20 %lu | frames %lu draw avg %.2f max %.1f ms\n",
            label, g_st.n, g_st.n ? g_st.sum / 1000.0 / g_st.n : 0, g_st.max / 1000.0, g_st.late1, g_st.late2, g_st.late5, g_st.late10, g_st.late20,
            g_st.frames, g_st.frames ? g_st.dsum / 1000.0 / g_st.frames : 0, g_st.dmax / 1000.0);
    fclose(f);
}

/* ------------------------------------------------------------------ the player
 * Song time runs at speed/100 of real time from (base_real, base_song). Events whose time has come are sent by
 * mp_pump(), from the main loop and from inside the drawing loops. */
static struct
{
    Song s; int loaded;                 /* the current song */
    long i;                             /* next event */
    uint64_t base_real, base_song, total, tail_until, paused_song;
    int paused, ending, speed, busy;
} P = { .speed = 100 };

static uint64_t song_time(void)
{
    uint64_t t;
    if (!P.loaded) return 0;
    if (P.paused) return P.paused_song;
    t = now_us();
    if (t < P.base_real) return P.base_song;                    /* the reset is still settling */
    return P.base_song + (t - P.base_real) * (uint64_t)P.speed / 100;
}
static void set_speed(int sp)
{
    uint64_t t = song_time();
    if (sp < 25) sp = 25;
    if (sp > 400) sp = 400;
    P.speed = sp; P.base_song = t; P.base_real = now_us();
}

/* sends every event that is due */
static void mp_pump(void)
{
    uint64_t t;
    if (!P.loaded || P.paused || P.busy || P.i >= P.s.nev) return;
    P.busy = 1;
    t = song_time();
    while (P.i < P.s.nev && P.s.ev[P.i].us <= t)
    {
        const Event* e = &P.s.ev[P.i];
        /* the synth's queue is full (MPU "DRR busy": a slow PC cannot render as fast as the song plays): send the
           rest later instead of waiting here. Waiting per byte (up to 1 s each) froze the whole player - no screen,
           no keys - on slow PCs; dropping bytes would lose notes. The song lags; nothing is lost. */
        if (mpu_busy()) { g_busy_waits++; break; }
        st_late((t - e->us) * 100 / P.speed);
        send_event(&P.s, e);
        viz_event(e);
        P.i++;
    }
    if (P.i >= P.s.nev && !P.ending) { P.ending = 1; P.tail_until = now_us() + 1200000; }   /* let the last notes ring */
    P.busy = 0;
}
/* microseconds until the next event (real time), 0 if due, large if none */
static uint64_t mp_next_in(void)
{
    uint64_t t, e;
    if (!P.loaded || P.paused || P.i >= P.s.nev) return 1000000;
    t = song_time(); e = P.s.ev[P.i].us;
    return e > t ? (e - t) * 100 / P.speed : 0;
}

static void mp_pause(int on)
{
    if (!P.loaded || P.ending) return;
    if (on && !P.paused) { P.paused_song = song_time(); P.paused = 1; silence(); }
    else if (!on && P.paused) { P.paused = 0; P.base_song = P.paused_song; P.base_real = now_us(); }
}
/* seek: silence, reset controllers, replay everything but the notes up to t */
static void mp_seek(uint64_t t)
{
    long k; int ch;
    if (!P.loaded) return;
    if (t >= P.total) t = P.total > 1000 ? P.total - 1000 : 0;
    P.busy = 1;
    silence();
    for (ch = 0; ch < 16; ch++) { out_byte((unsigned char)(0xB0 | ch)); out_byte(121); out_byte(0); }
    viz_reset();
    for (k = 0; k < P.s.nev && P.s.ev[k].us < t; k++)
    {
        const Event* e = &P.s.ev[k];
        if (e->kind == EV_SHORT && ((e->arg & 0xF0) == 0x80 || (e->arg & 0xF0) == 0x90)) continue;
        send_event(&P.s, e);
        viz_event(e);
    }
    P.i = k; P.ending = 0;
    if (P.paused) P.paused_song = t;
    else { P.base_song = t; P.base_real = now_us(); }
    P.busy = 0;
}
static void mp_seek_rel(long sec)
{
    long long t = (long long)song_time() + (long long)sec * 1000000LL;
    mp_seek(t < 0 ? 0 : (uint64_t)t);
}
static void mp_unload(void)
{
    if (!P.loaded) return;
    P.busy = 1; silence(); P.busy = 0;
    song_free(&P.s); P.loaded = 0; P.paused = 0; P.ending = 0;
}
/* load and start a song; the reset SysEx gets 250 ms before the first event */
static int mp_start(const char* path, int reset_mode, char* err, size_t errsz)
{
    mp_unload();
    if (!song_load(&P.s, path, err, errsz)) return 0;
    P.loaded = 1; P.i = 0; P.paused = 0; P.ending = 0;
    P.total = P.s.ev[P.s.nev - 1].us;
    viz_reset();
    send_reset_bytes(reset_mode);
    P.base_song = 0; P.base_real = now_us() + (reset_mode == 2 ? 0 : 250000);
    return 1;
}
/* 1 when the song has finished (and its last notes have rung out) */
static int mp_finished(void) { return P.loaded && P.ending && now_us() >= P.tail_until; }


#ifdef HOSTTEST
#include <strings.h>
#define stricmp strcasecmp
static void _fixpath(const char* in, char* out) { strcpy(out, in); }
#endif
static int g_rep = 0, g_shuf = 0;      /* repeat: 0 off, 1 all, 2 one; shuffle: 0 off, 1 all songs, 2 this folder */
/* ---- the playlist: paths plus what song_info() found (filled in the background) */
typedef struct { char* path; SongInfo si; int have; } PE;
static PE* g_pl; static int g_npl, g_cap, g_cur = -1;
/* path -> index hash (open addressing, case-insensitive like stricmp), so "already in the list?" stays O(1) with 10,000+
   songs; rebuilt lazily after anything that moves or removes entries (pl_dirty) */
static int* g_ht; static int g_htsz, g_htok;
static void pl_dirty(void) { g_htok = 0; }
static unsigned pl_hash(const char* s) { unsigned h = 2166136261u; for (; *s; s++) h = (h ^ (unsigned char)tolower((unsigned char)*s)) * 16777619u; return h; }
static void ht_put(int i) { unsigned m = (unsigned)g_htsz - 1, k = pl_hash(g_pl[i].path) & m; while (g_ht[k] >= 0) k = (k + 1) & m; g_ht[k] = i; }
static void ht_build(void)
{
    int i, sz = 256;
    while (sz < g_npl * 2 + 2) sz *= 2;
    if (sz != g_htsz) { free(g_ht); g_ht = (int*)malloc((size_t)sz * sizeof(int)); g_htsz = g_ht ? sz : 0; }
    if (!g_ht) { g_htok = 0; return; }
    memset(g_ht, 0xFF, (size_t)sz * sizeof(int));
    for (i = 0; i < g_npl; i++) ht_put(i);
    g_htok = 1;
}
/* DOS style for the screen: upper case, backslashes (DJGPP gives c:/sc55) */
static void dosify(char* s) { for (; *s; s++) { if (*s == '/') *s = '\\'; else *s = (char)toupper((unsigned char)*s); } }
static const char* base_of(const char* name)
{
    const char* b = name, *p;
    for (p = name; *p; p++) if (*p == '\\' || *p == '/' || *p == ':') b = p + 1;
    return b;
}
static int is_midi_name(const char* n)
{
    const char* e = strrchr(n, '.');
    if (!e) return 0;
    return !stricmp(e, ".mid") || !stricmp(e, ".midi") || !stricmp(e, ".rmi") || !stricmp(e, ".kar");
}
static int is_m3u_name(const char* n) { const char* e = strrchr(n, '.'); return e && (!stricmp(e, ".m3u") || !stricmp(e, ".lst")); }
static void q_added(int i); static void q_removed(int i); static void q_reset(void);
static void pl_add(const char* p)
{
    char full[300];
    if (g_npl == g_cap)
    {
        PE* n = (PE*)realloc(g_pl, (size_t)(g_cap ? g_cap * 2 : 64) * sizeof(PE));
        if (!n) return;
        g_pl = n; g_cap = g_cap ? g_cap * 2 : 64;
    }
    full[0] = 0; _fixpath(p, full);
    memset(&g_pl[g_npl], 0, sizeof(PE));
    g_pl[g_npl].path = strdup(full[0] ? full : p);
    if (g_pl[g_npl].path)
    {
        g_npl++;
        if (g_htok) { if (g_npl * 2 + 2 > g_htsz) g_htok = 0; else ht_put(g_npl - 1); }
        q_added(g_npl - 1);
    }
}
static void pl_remove(int i)
{
    if (i < 0 || i >= g_npl) return;
    q_removed(i);
    free(g_pl[i].path);
    memmove(&g_pl[i], &g_pl[i + 1], (size_t)(g_npl - i - 1) * sizeof(PE));
    g_npl--; pl_dirty();
    if (i < g_cur) g_cur--;
    else if (i == g_cur) g_cur = -2 - i;           /* the playing song left the list: keep playing, next is i */
}
static void pl_clear(void) { int i; for (i = 0; i < g_npl; i++) free(g_pl[i].path); g_npl = 0; g_cur = -1; pl_dirty(); q_reset(); }
static int pl_find(const char* path)
{
    int i; unsigned m, k;
    if (!g_htok) ht_build();
    if (!g_htok) { for (i = 0; i < g_npl; i++) if (!stricmp(g_pl[i].path, path)) return i; return -1; }   /* no memory for the hash */
    m = (unsigned)g_htsz - 1; k = pl_hash(path) & m;
    for (; g_ht[k] >= 0; k = (k + 1) & m) if (!stricmp(g_pl[g_ht[k]].path, path)) return g_ht[k];
    return -1;
}
static const char* pl_name(int i) { return g_pl[i].have && g_pl[i].si.title[0] ? g_pl[i].si.title : base_of(g_pl[i].path); }
static int cmp_str(const void* a, const void* b) { return stricmp(*(char* const*)a, *(char* const*)b); }

/* ---- the play order (shuffle): a Fisher-Yates permutation of the songs (all, or the current song's folder). N walks
 * forward through it, P back, so no song comes twice before every song has played once; after the last one a new
 * order is made (its first song is never the one that just played). Songs added later go in at random places still
 * ahead; removed songs leave the order; moving songs in the list does not change it. Kept in MIDPLAY.SHF on exit. */
static int* g_ord; static int g_nord, g_ordcap, g_opos = -1, g_ordok, g_ordscope;
static uint32_t g_rng = 2463534242u;
static uint32_t q_rand(uint32_t n) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return n ? (uint32_t)(((uint64_t)g_rng * n) >> 32) : 0; }
static void q_reset(void) { g_ordok = 0; g_nord = 0; g_opos = -1; }
static int q_grow(int n)
{
    if (n <= g_ordcap) return 1;
    { int c = g_ordcap ? g_ordcap : 64; int* t; while (c < n) c *= 2; t = (int*)realloc(g_ord, (size_t)c * sizeof(int)); if (!t) return 0; g_ord = t; g_ordcap = c; }
    return 1;
}
static int same_dir(const char* a, const char* b)
{
    const char* ea = base_of(a); const char* eb = base_of(b);
    return ea - a == eb - b && !strncasecmp(a, b, (size_t)(ea - a));
}
static int q_in_scope(int i, int ref) { return g_shuf != 2 || ref < 0 || ref >= g_npl || same_dir(g_pl[i].path, g_pl[ref].path); }
/* a new order: 'first' (>= 0) at the front, the rest of the scope shuffled; avoid >= 0: not first (just played) */
static void q_build(int first, int avoid)
{
    int i, n = 0, ref = first >= 0 ? first : avoid;
    g_ordok = 0; g_nord = 0; g_opos = -1;
    if (!q_grow(g_npl + 1)) return;
    if (first >= 0) g_ord[n++] = first;
    for (i = 0; i < g_npl; i++) if (i != first && q_in_scope(i, ref)) g_ord[n++] = i;
    for (i = n - 1; i > (first >= 0 ? 1 : 0); i--)                        /* Fisher-Yates over the part after 'first' */
    {
        int lo = first >= 0 ? 1 : 0, j = lo + (int)q_rand((uint32_t)(i - lo + 1)), t = g_ord[i]; g_ord[i] = g_ord[j]; g_ord[j] = t;
    }
    if (first < 0 && avoid >= 0 && n > 1 && g_ord[0] == avoid) { int j = 1 + (int)q_rand((uint32_t)(n - 1)), t = g_ord[0]; g_ord[0] = g_ord[j]; g_ord[j] = t; }
    g_nord = n; g_ordok = 1; g_ordscope = g_shuf;
    if (first >= 0) g_opos = 0;
}
static void q_added(int i)
{
    int k, at;
    if (!g_ordok || !q_in_scope(i, g_cur >= 0 ? g_cur : -1) || !q_grow(g_nord + 1)) return;
    at = g_opos + 1 + (int)q_rand((uint32_t)(g_nord - g_opos));           /* somewhere still ahead */
    for (k = g_nord; k > at; k--) g_ord[k] = g_ord[k - 1];
    g_ord[at] = i; g_nord++;
}
static void q_removed(int i)
{
    int k, o = 0;
    if (!g_ordok) return;
    for (k = 0; k < g_nord; k++)
    {
        if (g_ord[k] == i) { if (k <= g_opos) g_opos--; continue; }        /* the current song gone: P/N still work */
        g_ord[o++] = g_ord[k] > i ? g_ord[k] - 1 : g_ord[k];
    }
    g_nord = o;
}
static void q_swapped(int a, int b) { int k; if (!g_ordok) return; for (k = 0; k < g_nord; k++) { if (g_ord[k] == a) g_ord[k] = b; else if (g_ord[k] == b) g_ord[k] = a; } }
/* the song 'i' is starting: put the order's position on it (an order is made if there is none, or 'i' is not in it) */
static void q_playing(int i)
{
    int k;
    if (!g_shuf || i < 0 || i >= g_npl) return;
    if (g_ordok && g_ordscope == g_shuf)
    {
        if (g_opos + 1 < g_nord && g_ord[g_opos + 1] == i) { g_opos++; return; }
        if (g_opos >= 0 && g_opos < g_nord && g_ord[g_opos] == i) return;
        if (g_opos > 0 && g_ord[g_opos - 1] == i) { g_opos--; return; }
        for (k = 0; k < g_nord; k++) if (g_ord[k] == i)
        {   /* picked by hand (playlist ENTER): it becomes the next one in the order, the rest stays ahead */
            int t = g_ord[k], j;
            if (k > g_opos) { for (j = k; j > g_opos + 1; j--) g_ord[j] = g_ord[j - 1]; g_ord[g_opos + 1] = t; g_opos++; }
            else { for (j = k; j < g_opos; j++) g_ord[j] = g_ord[j + 1]; g_ord[g_opos] = t; }
            return;
        }
    }
    q_build(i, -1);
}
/* the song after the current one: 1 when it finished by itself, 0 when N was pressed; -1 = stop (end of the list) */
static int next_index(int finished)
{
    int cur = g_cur;
    if (finished && g_rep == 2 && cur >= 0) return cur;
    if (!g_npl) return -1;
    if (g_shuf)
    {
        if (!g_ordok || g_ordscope != g_shuf) q_build(cur >= 0 ? cur : -1, -1);
        if (g_opos + 1 < g_nord) return g_ord[g_opos + 1];
        if (finished && g_rep == 0) return -1;                             /* every song played once */
        { int last = g_opos >= 0 && g_opos < g_nord ? g_ord[g_opos] : cur; q_build(-1, last); }
        g_opos = -1;
        return g_nord ? g_ord[0] : -1;
    }
    if (cur <= -2) { int n = -2 - cur; return n < g_npl ? n : (g_rep || !finished ? 0 : -1); }   /* the playing song was removed */
    if (cur + 1 >= g_npl) return g_rep || !finished ? 0 : -1;
    return cur + 1;
}
/* the song before: shuffle goes back through the order; at its start it stays on the first song */
static int prev_index(void)
{
    int cur = g_cur;
    if (!g_npl) return -1;
    if (g_shuf)
    {
        if (!g_ordok || g_ordscope != g_shuf) q_build(cur >= 0 ? cur : -1, -1);
        if (g_opos > 0) return g_ord[g_opos - 1];
        return g_opos == 0 ? g_ord[0] : (cur >= 0 ? cur : -1);
    }
    if (cur <= -2) cur = -2 - cur;                                         /* removed: the one before its place */
    return cur > 0 ? cur - 1 : g_npl - 1;
}
/* MIDPLAY.SHF: the order and the position, so a restarted MIDPLAY carries on with the same round */
static void q_save(const char* file)
{
    FILE* f; int k;
    if (!g_shuf || !g_ordok) { remove(file); return; }
    if (!(f = fopen(file, "w"))) return;
    fprintf(f, "SHF %d %d %d %d\n", g_npl, g_nord, g_opos, g_ordscope);
    for (k = 0; k < g_nord; k++) fprintf(f, "%d\n", g_ord[k]);
    fclose(f);
}
static void q_load(const char* file)
{
    FILE* f = fopen(file, "r"); int n, no, op, sc, k, ok = 1;
    if (!f) return;
    if (fscanf(f, "SHF %d %d %d %d", &n, &no, &op, &sc) != 4 || n != g_npl || no < 0 || no > n || op < -1 || op >= no || sc != g_shuf || !q_grow(no)) ok = 0;
    for (k = 0; ok && k < no; k++) if (fscanf(f, "%d", &g_ord[k]) != 1 || g_ord[k] < 0 || g_ord[k] >= n) ok = 0;
    fclose(f);
    if (ok) { g_nord = no; g_opos = op; g_ordok = 1; g_ordscope = sc; } else q_reset();
}
#ifndef HOSTTEST
/* ================================================================== the screens (tui.h: every display mode) */
enum { PG_PLAYER, PG_LIST, PG_FILES, PG_OPTIONS, PG_HELP };
static int g_page = PG_PLAYER, g_back = PG_PLAYER;
static int g_ui = 1;                   /* 0 = /S plain one-line display */
static char g_home[260], g_cfg[300], g_m3u[300], g_shf[300];
/* options, kept in MIDPLAY.CFG */
static int g_view = 0;                 /* player: 0 channel table, 1 SC-55 front panel, 2 3D */
static int g_scene, g_ctheme, g_model; /* 3D: V look (solid, vector, voxel), C colours, J player (midplay3d.h) */
static int g_reset = 0;                /* before each song: 0 GS reset, 1 GM On, 2 nothing */
static int g_startlast = 1;            /* no files given: 1 the last playlist (and song), 0 MIDPLAY's folder */
static int g_gap = 0;                  /* seconds of silence between songs */
static char g_lastdir[260];
static long g_last_index = -1, g_last_pos;   /* resume point (seconds) */
static int g_chsel;                    /* selected channel / SC-55 part */
static char g_msg[90]; static uint64_t g_msg_until;
static int g_act, g_jump;              /* main loop request: 1 next, 2 quit, 3 previous, 4 jump to g_jump */

static void msg(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(g_msg, sizeof g_msg, fmt, ap); va_end(ap);
    g_msg_until = now_us() + 3000000;
}

/* every MIDI file in dir, sorted; returns how many were added */
static int scan_dir(const char* dir)
{
    /* findfirst/findnext gives name, attributes and size in one DOS call per entry; readdir()+stat() cost several
       DOS calls (and a directory search) per file, which made a 10,000 file folder take minutes on a slow PC */
    struct ffblk ff; char** names = NULL; int n = 0, cap = 0, i, r; char path[300], pat[300]; size_t dl = strlen(dir);
    const char* sep = (dl && dir[dl - 1] != '/' && dir[dl - 1] != '\\' && dir[dl - 1] != ':') ? "\\" : "";
    snprintf(pat, sizeof pat, "%s%s*.*", dir, sep);
    for (r = findfirst(pat, &ff, FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_ARCH); !r; r = findnext(&ff))
    {
        if ((ff.ff_attrib & FA_DIREC) || !is_midi_name(ff.ff_name)) continue;
        if ((size_t)snprintf(path, sizeof path, "%s%s%s", dir, sep, ff.ff_name) >= sizeof path) continue;   /* longer than DOS allows */
        if (n == cap) { char** t = (char**)realloc(names, (size_t)(cap ? cap * 2 : 64) * sizeof(char*)); if (!t) break; names = t; cap = cap ? cap * 2 : 64; }
        if ((names[n] = strdup(path))) n++;
    }
    if (n > 1) qsort(names, (size_t)n, sizeof(char*), cmp_str);
    for (i = 0; i < n; i++) { pl_add(names[i]); free(names[i]); }
    free(names);
    return n;
}
/* M3U: one path per line, '#' comments; relative paths are relative to the list */
static int pl_load_m3u(const char* file)
{
    FILE* f = fopen(file, "r"); char line[1024], dir[300], p[1400]; int n = 0; char* e;
    if (!f) return -1;
    snprintf(dir, sizeof dir, "%s", file); e = strrchr(dir, '\\'); if (!e) e = strrchr(dir, '/'); if (e) e[1] = 0; else dir[0] = 0;
    while (fgets(line, sizeof line, f))
    {
        size_t l = strlen(line);
        if (l == sizeof line - 1 && line[l - 1] != '\n') { int c; while ((c = fgetc(f)) != EOF && c != '\n') ; }   /* rest of a too long line */
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0] || line[0] == '#') continue;
        if (line[1] == ':' || line[0] == '\\' || line[0] == '/') snprintf(p, sizeof p, "%s", line);
        else snprintf(p, sizeof p, "%s%s", dir, line);
        pl_add(p); n++;
    }
    fclose(f);
    return n;
}
static int pl_save_m3u(const char* file)
{
    FILE* f = fopen(file, "w"); int i;
    if (!f) return 0;
    fprintf(f, "#EXTM3U\n");
    for (i = 0; i < g_npl; i++)
    {
        char p[600];
        snprintf(p, sizeof p, "%s", g_pl[i].path);
        dosify(p);
        if (g_pl[i].have && g_pl[i].si.ok > 0) fprintf(f, "#EXTINF:%ld,%s\n", (g_pl[i].si.ms + 500) / 1000, pl_name(i));
        fprintf(f, "%s\n", p);
    }
    fclose(f);
    return 1;
}
/* the info of one playlist entry, in the background: only when the next MIDI event is far enough away */
static void pl_info_step(int prefer)
{
    static int scan = 0; int i, k;
    if (mp_next_in() < 20000) return;
    if (prefer >= 0 && prefer < g_npl && !g_pl[prefer].have) k = prefer;
    else
    {
        for (i = 0, k = -1; i < g_npl; i++) { int j = (scan + i) % g_npl; if (!g_pl[j].have) { k = j; break; } }
        if (k < 0) return;
        scan = k + 1;
    }
    song_info(g_pl[k].path, &g_pl[k].si); g_pl[k].have = 1;
}

/* ---- options file */
static int mp3_want_w, mp3_want_h, mp3_want_b, mp3_hz;   /* declared again in midplay3d.h (the 3D screen mode wanted) */
static int g_m3cfg[2][4];            /* 3D screen mode for DOS [0] and Windows [1]: w h bits refresh */
static void cfg_load(void)
{
    FILE* f = fopen(g_cfg, "r"); char line[300];
    if (!f) return;
    while (fgets(line, sizeof line, f))
    {
        char* v = strchr(line, '='); int n;
        if (!v) continue;
        *v++ = 0; v[strcspn(v, "\r\n")] = 0; n = atoi(v);
        if (!strcmp(line, "VIEW")) g_view = n >= 0 && n <= 2 ? n : 0;
        else if (!strcmp(line, "SCENE3D")) g_scene = n >= 0 && n <= 2 ? n : 0;
        else if (!strcmp(line, "THEME3D")) g_ctheme = n >= 0 && n <= 6 ? n : 0;
        else if (!strcmp(line, "PLAYER3D")) g_model = n >= 0 && n <= 3 ? n : 0;
        else if (!strcmp(line, "REPEAT")) g_rep = n >= 0 && n <= 2 ? n : 0;
        else if (!strcmp(line, "SHUFFLE")) g_shuf = n >= 0 && n <= 2 ? n : 0;
        else if (!strcmp(line, "RESET")) g_reset = n >= 0 && n <= 2 ? n : 0;
        else if (!strcmp(line, "START")) g_startlast = n != 0;
        else if (!strcmp(line, "GAP")) g_gap = n >= 0 && n <= 10 ? n : 0;
        else if (!strcmp(line, "SPEED")) P.speed = n >= 25 && n <= 400 ? n : 100;
        else if (!strcmp(line, "DIR")) snprintf(g_lastdir, sizeof g_lastdir, "%s", v);
        else if (!strcmp(line, "LAST")) g_last_index = atol(v);
        else if (!strcmp(line, "POS")) g_last_pos = atol(v);
        else if (!strcmp(line, "MODE3D") || !strcmp(line, "MODE3DW"))
        {   /* "w h bits" (0 = auto) for DOS / for a Windows DOS box */
            int* m = g_m3cfg[line[6] == 'W']; m[0] = m[1] = m[2] = 0; sscanf(v, "%d %d %d", &m[0], &m[1], &m[2]);
        }
        else if (!strcmp(line, "HZ3D")) g_m3cfg[0][3] = n;
        else if (!strcmp(line, "HZ3DW")) g_m3cfg[1][3] = n;
    }
    fclose(f);
}
static void cfg_save(void)
{
    FILE* f = fopen(g_cfg, "w");
    if (!f) return;
    {   /* the 3D screen mode for where MIDPLAY runs now; the other one is kept as it was */
        int* m = g_m3cfg[g_win != 0];
        m[0] = mp3_want_w; m[1] = mp3_want_h; m[2] = mp3_want_b; m[3] = mp3_hz;
        fprintf(f, "MODE3D=%d %d %d\nHZ3D=%d\nMODE3DW=%d %d %d\nHZ3DW=%d\n", g_m3cfg[0][0], g_m3cfg[0][1], g_m3cfg[0][2], g_m3cfg[0][3],
                g_m3cfg[1][0], g_m3cfg[1][1], g_m3cfg[1][2], g_m3cfg[1][3]);
    }
    fprintf(f, "SCENE3D=%d\nTHEME3D=%d\nPLAYER3D=%d\n", g_scene, g_ctheme, g_model);
    fprintf(f, "VIEW=%d\nREPEAT=%d\nSHUFFLE=%d\nRESET=%d\nSTART=%d\nGAP=%d\nSPEED=%d\nDIR=%s\nLAST=%d\nPOS=%lu\n",
            g_view, g_rep, g_shuf, g_reset, g_startlast, g_gap, P.speed, g_lastdir, g_cur,
            P.loaded ? (unsigned long)(song_time() / 1000000ULL) : 0UL);
    fclose(f);
}

/* ---- drawing helpers (0-based row/column wrappers for the panel code, plus the common bands) */
static void cell(int r, int c, int ch, int at) { tui_cell(c + 1, r + 1, at, ch); }
static void fillrow(int r, int ch, int at) { tui_fill(1, r + 1, 80, 1, at, ch); }
static void text(int r, int c, const char* s, int at, int maxw)
{
    int n = 0;
    while (*s && n < maxw) { cell(r, c + n, (unsigned char)*s, at); s++; n++; }
}
static void fmt_time(char* out, unsigned long sec) { sprintf(out, "%lu:%02lu", sec / 60, sec % 60); }
static void fmt_ms(char* out, const PE* e)
{
    if (!e->have) strcpy(out, "  ...");
    else if (e->si.ok <= 0) strcpy(out, "    ?");
    else { unsigned long s = (unsigned long)((e->si.ms + 500) / 1000); sprintf(out, "%2lu:%02lu", s / 60, s % 60); }
}
/* key bar: "~F1~ Help  ~TAB~ Lists ..." - the parts between ~ are keys */
static void keybar(const char* s)
{
    int x = 2, key = 0;
    tui_fill(1, 25, 80, 1, 0x30, ' ');
    for (; *s && x <= 79; s++)
    {
        if (*s == '~') { key = !key; continue; }
        tui_cell(x++, 25, key ? 0x3E : 0x30, (unsigned char)*s);
    }
}
static void hintline(const char* s)
{
    tui_fill(1, 24, 80, 1, 0x10, ' ');
    if (g_msg[0] && now_us() < g_msg_until) tui_putw(2, 24, 78, 0x1E, g_msg);
    else if (s) tui_putw(2, 24, 78, 0x17, s);
}
static void header(const char* what)
{
    char b[100], t1[16], t2[16]; int x;
    tui_fill(1, 1, 80, 1, 0x30, ' ');
    x = 2 + tui_put(2, 1, 0x3F, "MIDPLAY");
    tui_cell(x + 1, 1, 0x30, 0xB3);
    tui_putw(x + 3, 1, 46, 0x30, what);
    if (P.loaded)
    {
        fmt_time(t1, (unsigned long)(song_time() > P.total ? P.total : song_time()) / 1000000UL);
        fmt_time(t2, (unsigned long)(P.total / 1000000ULL));
        snprintf(b, sizeof b, "%c %s / %s", P.paused ? 0xBA : P.ending ? 0xFE : 0x10, t1, t2);
    }
    else snprintf(b, sizeof b, "\xFE stopped");
    tui_put(80 - (int)strlen(b), 1, P.paused ? 0x3E : 0x3F, b);
}

/* ---- the player page */
static const char* const note_nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const unsigned char chan_col[16] = {11, 10, 14, 12, 13, 9, 15, 11, 10, 14, 12, 13, 9, 15, 11, 10};
static const char* drum_kit(int p)
{
    switch (p)
    {
    case 8: return "Room Kit";        case 16: return "Power Kit";   case 24: return "Electronic Kit";
    case 25: return "TR-808 Kit";     case 32: return "Jazz Kit";    case 40: return "Brush Kit";
    case 48: return "Orchestra Kit";  case 56: return "SFX Kit";     default: return "Standard Kit";
    }
}

#define LCD 0x60               /* black "segments" on the amber LCD */
#define LCDL 0x6E              /* labels */
/* the real front panel from SBEMU (MPU command 0xB0, SBEMU beta.34 and later): 'L', on, synth, load lo/hi,
 * governor, 80 bytes LCD display RAM, 64 bytes custom characters */
static unsigned char g_lcd[150];
static int g_lcd_ok;                     /* 1 = the last query gave an SC-55 panel */
static int g_lcd_load = -1;              /* SC-55 render load, permille */
static uint64_t g_lcd_next;
static void lcd_query(void)
{
    int i;
    uint64_t t = now_us();
    if (g_dry) { g_lcd_ok = 0; g_lcd_load = -1; return; }
    if (t < g_lcd_next) return;
    g_lcd_next = t + 100000;             /* 10 times a second: 150 trapped port reads each */
    outportb(base + 1, 0xB0);
    for (i = 0; i < 150; i++) g_lcd[i] = (unsigned char)inportb(base);   /* not interrupted: one MPU conversation */
    g_lcd_ok = g_lcd[0] == 'L' && (g_lcd[2] == 0 || g_lcd[2] == 2);
    g_lcd_load = g_lcd[0] == 'L' ? g_lcd[3] | (g_lcd[4] << 8) : -1;
}
static int lcd_dot(int r, int c)          /* level display dot, row 0 top..15, column 0..15 */
{
    const unsigned char *d = g_lcd + 6, *cg = g_lcd + 86;
    int j = c / 5, k = c % 5; unsigned char ch = d[20 + j + (r >= 8 ? 40 : 0)];
    if (j == 3 && k > 0) return 0;
    if (ch >= 16) return 0;              /* the level display is made of custom characters */
    return (cg[(ch & 7) * 8 + (r & 7)] >> (4 - k)) & 1;
}
static void lcd_field(int r, int c, int at, int off, int n)
{
    int i;
    for (i = 0; i < n; i++) { unsigned char ch = g_lcd[6 + off + i]; cell(r, c + i, ch >= 32 && ch < 127 ? ch : ' ', at); }
}

static void sc55_panel(void)
{
    static int peak[16], hold[16];
    char b[64];
    int r, c, p = g_chsel, x;
    lcd_query();
    for (r = 4; r <= 20; r++) fillrow(r, ' ', 0x07);
    /* case */
    for (c = 1; c < 80 - 1; c++) { cell(4, c, 0xDC, 0x08); cell(20, c, 0xDF, 0x08); }
    for (r = 5; r <= 19; r++) { cell(r, 1, 0xDB, 0x08); cell(r, 80 - 2, 0xDB, 0x08); }
    text(5, 4, "Roland", 0x0F, 6);
    text(5, 12, "SOUND Canvas", 0x07, 12);
    text(5, 26, "SC-55", 0x0F, 5);
    text(5, 80 - 26, "GS", 0x0E, 2);
    text(5, 80 - 22, "\xFE POWER", 0x0A, 7);
    text(5, 80 - 13, "MIDI \xFE", g_viz.count[p] ? 0x0C : 0x08, 6);
    /* LCD */
    for (r = 7; r <= 17; r++) for (c = 4; c <= 80 - 5; c++) cell(r, c, ' ', LCD);
    for (c = 4; c <= 80 - 5; c++) { cell(6, c, 0xDC, 0x06); cell(18, c, 0xDF, 0x06); }
    text(7, 6, "PART", LCDL, 4);  sprintf(b, "A%02d", p + 1); text(7, 13, b, LCD, 3);
    text(8, 6, "INSTRUMENT", LCDL, 10);
    sprintf(b, "%03d %-12.12s", (g_viz.prog[p] & 127) + 1, p == 9 ? drum_kit(g_viz.prog[p]) : gm_names[g_viz.prog[p] & 127]);
    text(9, 6, b, LCD, 16);
    text(11, 6, "LEVEL", LCDL, 5);  sprintf(b, "%3d", g_viz.vol[p]); text(11, 12, b, LCD, 3);
    text(11, 17, "PAN", LCDL, 3);
    { int pn = g_viz.pan[p] - 64;
      if (pn >= -1 && pn <= 1) strcpy(b, "  0"); else sprintf(b, "%c%2d", pn < 0 ? 'L' : 'R', pn < 0 ? -pn : pn);
      text(11, 22, b, LCD, 3); }
    text(12, 6, "EXPR", LCDL, 4);   sprintf(b, "%3d", g_viz.expr[p]); text(12, 12, b, LCD, 3);
    text(12, 17, "KEYS", LCDL, 4);  sprintf(b, "%3d", g_viz.count[p]); text(12, 22, b, LCD, 3);
    text(13, 6, "REV", LCDL, 3);    sprintf(b, "%3d", g_viz.rev[p]); text(13, 12, b, LCD, 3);
    text(13, 17, "CHO", LCDL, 3);   sprintf(b, "%3d", g_viz.cho[p]); text(13, 22, b, LCD, 3);
    text(14, 6, "BEND", LCDL, 4);   sprintf(b, "%+5d", g_viz.bend[p]); text(14, 12, b, LCD, 5);
    text(15, 6, "NOTE", LCDL, 4);
    if (g_viz.last_note[p] >= 0) { int nt = g_viz.last_note[p]; sprintf(b, "%s%d", note_nm[nt % 12], nt / 12 - 1); text(15, 12, b, LCD, 4); }
    text(16, 6, "MIDI CH", LCDL, 7); sprintf(b, "%2d", p + 1); text(16, 14, b, LCD, 2);
    /* 16 part bars, 2 columns wide and 3 apart; 8 rows of half blocks = 16 steps like the SC-55's dot matrix */
    x = 28;
    for (c = 0; c < 16; c++)
    {
        long lv = (long)g_viz.level[c] * g_viz.vol[c] / 127 * g_viz.expr[c] / 127;
        int h = (int)(lv * 16 / 127), k, bx = x + c * 3;
        if (lv > 0 && h == 0) h = 1;
        if (h >= peak[c]) { peak[c] = h; hold[c] = 18; }
        else if (hold[c] > 0) hold[c]--;
        else if (peak[c] > 0) peak[c]--;
        for (r = 0; r < 8; r++)
        {
            int lo = (7 - r) * 2, ch;                    /* steps lo+1 (bottom half) and lo+2 (top half) of this row */
            int bot = h >= lo + 1 || peak[c] == lo + 1, top = h >= lo + 2 || peak[c] == lo + 2;
            ch = bot && top ? 0xDB : bot ? 0xDC : top ? 0xDF : 0xFA;
            for (k = 0; k < 2; k++) cell(7 + r, bx + k, ch, (ch == 0xFA) ? 0x67 : LCD);
        }
        sprintf(b, "%2d", c + 1);
        text(15, bx, b, c == p ? 0x0E : LCDL, 2);
        if (c == p) { cell(16, bx, 0x1E, LCD); cell(16, bx + 1, 0x1E, LCD); }
    }
    text(17, 28, "PART LEVEL", LCDL, 10);
    if (g_lcd_ok && g_lcd[2] == 2)
    {   /* MT-32: its 20-character LCD; the bars stay the MIDI-stream ones */
        for (r = 7; r <= 17; r++) for (c = 4; c <= 26; c++) cell(r, c, ' ', LCD);
        text(5, 12, "Multi Timbre Sound Module", 0x07, 25); text(5, 26 + 12, "MT-32", 0x0F, 5);
        text(8, 6, "MT-32 DISPLAY", LCDL, 13);
        for (c = 0; c < 20; c++) { unsigned char ch = g_lcd[6 + c]; cell(10, 5 + c, ch >= 32 && ch < 127 ? ch : ' ', 0x2F); }
        if (g_lcd_load >= 0) { sprintf(b, "LOAD %3d%%", (g_lcd_load + 5) / 10); text(13, 6, b, LCDL, 9); }
        text(16, 6, "live from SBEMU", 0x68, 15);
    }
    else if (g_lcd_ok)
    {   /* the module's own display: its fields and the 16 x 16 dot level display */
        for (r = 7; r <= 17; r++) for (c = 4; c <= 26; c++) cell(r, c, ' ', LCD);
        text(7, 6, "PART", LCDL, 4);       lcd_field(7, 13, LCD, 0, 3);
        text(8, 6, "INSTRUMENT", LCDL, 10);
        lcd_field(9, 6, LCD, 3, 16);
        text(11, 6, "LEVEL", LCDL, 5);  lcd_field(11, 12, LCD, 40, 3);
        text(11, 17, "PAN", LCDL, 3);   lcd_field(11, 22, LCD, 43, 3);
        text(12, 6, "REV", LCDL, 3);    lcd_field(12, 12, LCD, 49, 3);
        text(12, 17, "CHO", LCDL, 3);   lcd_field(12, 22, LCD, 46, 3);
        text(13, 6, "KEY", LCDL, 3);    lcd_field(13, 12, LCD, 52, 3);
        text(13, 17, "MIDI", LCDL, 4);  lcd_field(13, 22, LCD, 55, 3);
        if (g_lcd_load >= 0) { sprintf(b, "LOAD %3d%%", (g_lcd_load + 5) / 10); text(15, 6, b, LCDL, 9); }
        text(16, 6, "live from SBEMU", 0x68, 15);
        for (c = 0; c < 48; c++) cell(16, x + c, ' ', LCD);           /* no part marker: the module shows its own */
        for (c = 0; c < 16; c++) { sprintf(b, "%2d", c + 1); text(15, x + c * 3, b, LCDL, 2); }
        for (c = 0; c < 16; c++)
            for (r = 0; r < 8; r++)
            {
                int up = lcd_dot(r * 2, c), lo = lcd_dot(r * 2 + 1, c), ch = up && lo ? 0xDB : up ? 0xDF : lo ? 0xDC : 0xFA, k;
                for (k = 0; k < 2; k++) cell(7 + r, x + c * 3 + k, ch, ch == 0xFA ? 0x67 : LCD);
            }
    }
    /* buttons */
    text(19, 4, " \x11 PART \x10 ", 0x70, 10);
    text(19, 16, " \x11 INSTRUMENT \x10 ", 0x70, 16);
    text(19, 34, "  ALL  ", 0x70, 7);
    text(19, 43, " MUTE ", 0x70, 6);
    text(19, 52, g_lcd_ok && g_lcd[2] == 0 ? "S next view" : "\x18\x19 part   S next view", 0x08, 26);
}

/* rows 2-4: what is playing */
static void now_playing(void)
{
    char b[160], t1[16], t2[16], t3[16]; int w, x, i, half;
    const char* title = P.loaded ? (P.s.title[0] ? P.s.title : base_of(g_cur >= 0 && g_cur < g_npl ? g_pl[g_cur].path : "")) : "Nothing playing";
    tui_fill(1, 2, 80, 3, 0x10, ' ');
    tui_cell(2, 2, P.paused ? 0x1E : 0x1A, P.paused ? 0xBA : P.loaded ? 0x10 : 0xFE);
    tui_putw(4, 2, 56, 0x1F, title);
    if (g_npl) { snprintf(b, sizeof b, "Song %d of %d", g_cur >= 0 ? g_cur + 1 : 0, g_npl); tui_put(80 - (int)strlen(b), 2, 0x17, b); }
    if (P.loaded)
    {
        char fn[16]; snprintf(fn, sizeof fn, "%.12s", base_of(g_pl[g_cur >= 0 ? g_cur : 0].path)); dosify(fn);
        snprintf(b, sizeof b, "%s \xFA Format %d \xFA %d track%s \xFA %ld BPM%s", fn, P.s.format,
                 P.s.ntrks, P.s.ntrks == 1 ? "" : "s", g_viz.bpm10 / 10, "");
        tui_putw(4, 3, 50, 0x13, g_cur >= 0 ? b : "");
    }
    x = 55;
    snprintf(b, sizeof b, "%s", g_rep == 1 ? "\x1D all" : g_rep == 2 ? "\x1D one" : "");
    if (b[0]) { tui_put(x, 3, 0x1B, b); }
    if (g_shuf) tui_put(62, 3, 0x1B, g_shuf == 2 ? "\x1A shuf dir" : "\x1A shuf");
    if (P.speed != 100) { snprintf(b, sizeof b, "%d%%", P.speed); tui_put(69, 3, 0x1E, b); }
    if (g_mute || g_solo) tui_put(75, 3, 0x1C, g_solo ? "SOLO" : "MUTE");
    /* progress: time, a bar with half-cell steps, length, time left */
    {
        uint64_t el = P.loaded ? song_time() : 0, tot = P.loaded ? P.total : 0;
        if (el > tot) el = tot;
        fmt_time(t1, (unsigned long)(el / 1000000ULL)); fmt_time(t2, (unsigned long)(tot / 1000000ULL));
        snprintf(t3, sizeof t3, "-%s", t1); fmt_time(t3 + 1, (unsigned long)((tot - el) / 1000000ULL));
        tui_putw(2, 4, 6, 0x1F, t1);
        w = 58; half = tot ? (int)(el * (uint64_t)(w * 2) / tot) : 0;
        for (i = 0; i < w; i++)
        {
            int fill = half - i * 2;
            if (fill >= 2) tui_cell(9 + i, 4, 0x1A, 0xDB);
            else if (fill == 1) tui_cell(9 + i, 4, 0x8A, 0xDD);   /* left half lit on a slate cell */
            else tui_cell(9 + i, 4, 0x18, 0xC4);
        }
        tui_put(68, 4, 0x17, t2);
        tui_put(74, 4, 0x18, t3);
    }
}

/* rows 5-21: the 16 channels */
static void channel_table(void)
{
    char b[64]; int r, c;
    tui_fill(1, 5, 80, 1, 0x80, ' ');
    tui_put(2, 5, 0x8F, "Ch"); tui_put(7, 5, 0x8F, "Instrument"); tui_put(30, 5, 0x8F, "Level");
    tui_put(57, 5, 0x8F, "Vol"); tui_put(61, 5, 0x8F, "Pan"); tui_put(67, 5, 0x8F, "Note"); tui_put(72, 5, 0x8F, "Keys");
    tui_put(77, 5, 0x8F, "Rv");
    for (r = 0; r < 16; r++)
    {
        int y = 6 + r, used = g_viz.last_note[r] >= 0 || g_viz.prog[r], act = g_viz.count[r] > 0, sel = r == g_chsel;
        int bg = sel ? 0x50 : 0x10, lv = g_viz.level[r] * g_viz.vol[r] / 127 * g_viz.expr[r] / 127, halfs, pos;
        int dim = !audible(r);
        tui_fill(1, y, 80, 1, bg, ' ');
        if (sel) tui_cell(1, y, bg | 0x0E, 0x10);
        snprintf(b, sizeof b, "%2d", r + 1); tui_put(2, y, bg | (used ? 0x0F : 0x08), b);
        if (g_solo >> r & 1) tui_cell(5, y, 0xA0, 'S');
        else if (g_mute >> r & 1 || (g_solo && dim)) tui_cell(5, y, 0xC0 | 0x0F, 'M');
        tui_putw(7, y, 22, bg | (dim ? 0x08 : used ? chan_col[r] : 0x08), r == 9 ? drum_kit(g_viz.prog[r]) : gm_names[g_viz.prog[r] & 127]);
        /* level: 26 LED segments, green -> yellow -> pink */
        halfs = lv * 26 / 127; if (lv > 0 && halfs == 0) halfs = 1;
        for (c = 0; c < 26; c++)
        {
            int col = dim ? 0x08 : c < 16 ? 0x0A : c < 22 ? 0x0E : 0x0C;
            if (c < halfs) tui_cell(30 + c, y, bg | col, 0xFE);
            else tui_cell(30 + c, y, bg | 0x08, 0xFA);
        }
        snprintf(b, sizeof b, "%3d", g_viz.vol[r]); tui_put(57, y, bg | (used ? 0x07 : 0x08), b);
        /* pan: a small slider */
        pos = g_viz.pan[r] * 5 / 128;
        for (c = 0; c < 5; c++) tui_cell(61 + c, y, bg | (c == pos ? 0x0F : 0x08), c == pos ? 0x07 : 0xC4);
        if (used && g_viz.last_note[r] >= 0)
        {
            int nt = g_viz.last_note[r];
            snprintf(b, sizeof b, "%s%d", note_nm[nt % 12], nt / 12 - 1); tui_putw(67, y, 4, bg | (act ? 0x0F : 0x07), b);
        }
        if (act) { snprintf(b, sizeof b, "%3d", g_viz.count[r]); tui_put(72, y, bg | 0x0E, b); }
        if (g_viz.bend[r]) tui_cell(76, y, bg | 0x0D, g_viz.bend[r] > 0 ? 0x18 : 0x19);
        if (used) { snprintf(b, sizeof b, "%2d", g_viz.rev[r] * 99 / 127); tui_put(77, y, bg | 0x03, b); }
    }
}

/* rows 22-23: a keyboard, notes 24..103 (C1..G7), lit in the colour of the channel that plays them */
static void keyboard(void)
{
    int c;
    for (c = 0; c < 80; c++)
    {
        int nt = 24 + c, lv = g_viz.nl[nt], k = nt % 12, black = k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
        int col = chan_col[g_viz.ncol[nt] & 15];
        if (black)
        {
            tui_cell(c + 1, 22, lv > 20 ? (col << 4) | col : 0x00, lv > 20 ? 0xDB : ' ');
            tui_cell(c + 1, 23, 0x70, k == 1 || k == 6 ? 0xB3 : ' ');
        }
        else
        {
            int lit = lv > 20, hi = lv > 70;
            tui_cell(c + 1, 22, lit ? (col << 4) | 0x0F : 0x70, k == 0 && !lit ? 0xFA : ' ');
            tui_cell(c + 1, 23, lit ? (col << 4) | 0x00 : 0x70, hi ? 0xDF : (k == 4 || k == 11) ? 0xB3 : ' ');
        }
    }
}

/* ================================================================== list pages */
#define LROWS 19                         /* rows 3..21 */
static char g_find[32]; static uint64_t g_find_t;
static int find_active(void) { return g_find[0] && now_us() - g_find_t < 2000000; }
static void list_keep(int* sel, int* top, int n)
{
    if (*sel >= n) *sel = n - 1;
    if (*sel < 0) *sel = 0;
    if (*sel < *top) *top = *sel;
    if (*sel >= *top + LROWS) *top = *sel - LROWS + 1;
    if (*top < 0) *top = 0;
}
static int list_nav(int k, int* sel, int* top, int n)
{
    int s = *sel;
    if (k == K_UP) s--; else if (k == K_DOWN) s++;
    else if (k == K_PGUP) s -= LROWS - 1; else if (k == K_PGDN) s += LROWS - 1;
    else if (k == K_HOME) s = 0; else if (k == K_END) s = n - 1;
    else return 0;
    *sel = s; list_keep(sel, top, n);
    return 1;
}
static int ci_prefix(const char* s, const char* p) { while (*p) { if (tolower((unsigned char)*s) != *p) return 0; s++; p++; } return 1; }
static int ci_contains(const char* s, const char* p) { for (; *s; s++) if (ci_prefix(s, p)) return 1; return 0; }
/* type to find: letters jump to the first entry whose name (or title) starts with what was typed, else contains it */
static int find_key(int k, int n, const char* (*nm1)(int), const char* (*nm2)(int), int* sel, int* top)
{
    size_t l; int i;
    if (k == K_BKSP && find_active()) { g_find[strlen(g_find) - 1] = 0; g_find_t = now_us(); if (!g_find[0]) return 1; }
    else if (k > 32 && k < 127)
    {
        if (!find_active()) g_find[0] = 0;
        l = strlen(g_find);
        if (l < sizeof g_find - 1) { g_find[l] = (char)tolower(k); g_find[l + 1] = 0; }
        g_find_t = now_us();
    }
    else return 0;
    for (i = 0; i < n; i++) if (ci_prefix(nm1(i), g_find) || (nm2 && ci_prefix(nm2(i), g_find))) break;
    if (i == n) for (i = 0; i < n; i++) if (ci_contains(nm1(i), g_find) || (nm2 && ci_contains(nm2(i), g_find))) break;
    if (i < n) { *sel = i; list_keep(sel, top, n); }
    return 1;
}
static void find_line(void)
{
    tui_fill(1, 23, 80, 1, 0x10, ' ');
    if (find_active()) { tui_put(2, 23, 0x1B, "Find:"); tui_put(8, 23, 0x1E, g_find); tui_cell(8 + (int)strlen(g_find), 23, 0x1E, '_'); }
}

/* ---- Playlist */
static int pl_sel, pl_top, pl_sort;
static const char* pl_nm_file(int i) { return base_of(g_pl[i].path); }
static const char* pl_nm_title(int i) { return g_pl[i].have ? g_pl[i].si.title : ""; }
static int pl_idx_cmp_mode;
static int pe_cmp(const void* a, const void* b)
{
    const PE* x = (const PE*)a; const PE* y = (const PE*)b;
    if (pl_idx_cmp_mode == 1) { int c = stricmp(x->si.title[0] ? x->si.title : base_of(x->path), y->si.title[0] ? y->si.title : base_of(y->path)); if (c) return c; }
    if (pl_idx_cmp_mode == 2 && x->si.ms != y->si.ms) return x->si.ms < y->si.ms ? -1 : 1;
    return stricmp(base_of(x->path), base_of(y->path));
}
static void pl_sort_now(int mode)
{
    char cur[300] = "", selp[300] = ""; int i;
    if (g_npl < 2) return;
    if (g_cur >= 0) snprintf(cur, sizeof cur, "%s", g_pl[g_cur].path);
    snprintf(selp, sizeof selp, "%s", g_pl[pl_sel].path);
    if (mode >= 1) for (i = 0; i < g_npl; i++) if (!g_pl[i].have) { mp_pump(); song_info(g_pl[i].path, &g_pl[i].si); g_pl[i].have = 1; }
    if (mode == 3) { for (i = g_npl - 1; i > 0; i--) { int j = (int)q_rand((uint32_t)(i + 1)); PE t = g_pl[i]; g_pl[i] = g_pl[j]; g_pl[j] = t; } }
    else { pl_idx_cmp_mode = mode; qsort(g_pl, (size_t)g_npl, sizeof(PE), pe_cmp); }
    pl_dirty(); q_reset();
    if (cur[0]) g_cur = pl_find(cur);
    pl_sel = pl_find(selp); if (pl_sel < 0) pl_sel = 0;
    list_keep(&pl_sel, &pl_top, g_npl);
}
static void page_playlist(void)
{
    char b[120], t[16]; int r, i, unknown = 0; long total = 0;
    for (i = 0; i < g_npl; i++) { if (!g_pl[i].have) unknown++; else if (g_pl[i].si.ok > 0) total += g_pl[i].si.ms; }
    fmt_time(t, (unsigned long)(total / 1000));
    snprintf(b, sizeof b, "Playlist \xFA %d song%s \xFA %s%s", g_npl, g_npl == 1 ? "" : "s", t, unknown ? " so far" : "");
    tui_fill(1, 2, 80, 22, 0x10, ' ');
    header(b);
    tui_fill(1, 2, 80, 1, 0x80, ' ');
    tui_put(2, 2, 0x8F, " No"); tui_put(8, 2, 0x8F, "File"); tui_put(22, 2, 0x8F, "Title"); tui_put(62, 2, 0x8F, " Time"); tui_put(69, 2, 0x8F, "Fmt Trk");
    list_keep(&pl_sel, &pl_top, g_npl);
    for (r = 0; r < LROWS && pl_top + r < g_npl; r++)
    {
        int i2 = pl_top + r, y = 3 + r, sel = i2 == pl_sel, cur = i2 == g_cur;
        int bg = sel ? 0x70 : 0x10, fg = sel ? 0x00 : cur ? 0x0E : 0x07;
        PE* e = &g_pl[i2];
        tui_fill(1, y, 80, 1, bg, ' ');
        snprintf(b, sizeof b, "%5d", i2 + 1); tui_put(1, y, bg | 0x08, b);   /* cols 1-5: 99,999 songs before col 6 (the play mark) */
        if (cur) tui_cell(6, y, bg | (P.paused ? 0x0E : 0x0A), P.paused ? 0xBA : 0x10);
        { char fn[80]; snprintf(fn, sizeof fn, "%s", base_of(e->path)); dosify(fn); tui_putw(8, y, 13, bg | fg, fn); }
        if (e->have && e->si.title[0]) tui_putw(22, y, 39, bg | (sel ? 0x01 : 0x03), e->si.title);   /* file name first, then the title */
        fmt_ms(t, e); tui_put(62, y, bg | (e->have && e->si.ok <= 0 ? 0x0C : fg), t);
        if (e->have && e->si.ok > 0) { snprintf(b, sizeof b, " %d  %3d", e->si.fmt, e->si.trk); tui_put(69, y, bg | (sel ? 0x08 : 0x08), b); }
    }
    if (!g_npl) tui_put(8, 5, 0x18, "The playlist is empty: TAB (or Ins) opens the files to add songs.");
    if (g_npl && pl_sel < g_npl) { char pp[300]; snprintf(pp, sizeof pp, "%s", g_pl[pl_sel].path); dosify(pp); tui_putw(2, 22, 78, 0x13, pp); }
    find_line();
    hintline(pl_sort ? "Sorted - F5 sorts again: name, title, length, shuffled" : "Type a name to jump to it. The music keeps playing while you look around.");
    keybar("~ENTER~ Play ~DEL~ Remove ~^\x18\x19~ Move ~F5~ Sort ~^S~ Save ~^O~ Open ~INS~ Add ~TAB~ Page");
}
static void pl_open_dialog(void)
{
    char f[260];
    snprintf(f, sizeof f, "%s\\PLAYLIST.M3U", g_home);
    if (!tui_input("Open a playlist", "File name of the playlist (.M3U):", f, sizeof f, 1)) return;
    {
        int keep = g_npl && !tui_yesno("Replace the playlist with it? (No: add its songs at the end)");
        int was = g_npl, n;
        if (!keep) { mp_unload(); pl_clear(); }
        n = pl_load_m3u(f);
        if (n < 0) { msg("Cannot open %s", f); return; }
        msg("%d song%s from %s", n, n == 1 ? "" : "s", base_of(f));
        if (!keep && g_npl) { g_jump = -1; g_act = 4; }
        else pl_sel = was;
    }
}
static void pl_save_dialog(void)
{
    char f[260];
    snprintf(f, sizeof f, "%s\\PLAYLIST.M3U", g_home);
    if (!tui_input("Save the playlist", "File name (.M3U) - MIDPLAY /D or Ctrl+O on the Playlist page opens it again:", f, sizeof f, 1)) return;
    if (pl_save_m3u(f)) msg("Saved %d songs to %s", g_npl, f); else msg("Cannot write %s", f);
}
static void key_playlist(int k)
{
    if (find_key(k, g_npl, pl_nm_file, pl_nm_title, &pl_sel, &pl_top)) return;
    if (list_nav(k, &pl_sel, &pl_top, g_npl)) return;
    if (k == K_ENTER && g_npl) { g_jump = pl_sel; g_act = 4; }
    else if (k == K_DEL && g_npl) { pl_remove(pl_sel); list_keep(&pl_sel, &pl_top, g_npl); }
    else if ((k == K_CUP || k == K_CDOWN) && g_npl > 1)
    {
        int j = pl_sel + (k == K_CUP ? -1 : 1);
        if (j >= 0 && j < g_npl)
        {
            PE t = g_pl[pl_sel]; g_pl[pl_sel] = g_pl[j]; g_pl[j] = t; pl_dirty(); q_swapped(pl_sel, j);
            if (g_cur == pl_sel) g_cur = j; else if (g_cur == j) g_cur = pl_sel;
            pl_sel = j; list_keep(&pl_sel, &pl_top, g_npl);
        }
    }
    else if (k == K_F5) { pl_sort = (pl_sort % 4) + 1; pl_sort_now(pl_sort - 1); msg("Sorted by %s", (const char*[]){"file name", "title", "length", "chance (shuffled)"}[pl_sort - 1]); }
    else if (k == K_CS) pl_save_dialog();
    else if (k == K_CO) pl_open_dialog();
    else if (k == K_INS) g_page = PG_FILES;
    else if (k == K_MOUSE && tui_my >= 3 && tui_my < 3 + LROWS && pl_top + tui_my - 3 < g_npl)
    {
        int i = pl_top + tui_my - 3;
        if (i == pl_sel) { g_jump = i; g_act = 4; } else pl_sel = i;
    }
}

/* ---- Files */
typedef struct { char* name; int kind; long size; int mark; } Ent;   /* name: full long name (was char[80]: longer names were cut and could not be opened) */    /* kind: 0 file, 1 folder, 2 drive, 3 playlist */
static Ent* fl_e; static int fl_n, fl_sel, fl_top, fl_loaded;
static char fl_cwd[300];                  /* the folder shown, read once per fl_load (getcwd is a DOS call) */
static SongInfo fl_si; static char fl_si_name[300]; static int fl_si_have;
static int ent_cmp(const void* a, const void* b)
{
    const Ent* x = (const Ent*)a, *y = (const Ent*)b;
    int kx = x->kind == 1 ? 0 : x->kind == 2 ? 3 : 1, ky = y->kind == 1 ? 0 : y->kind == 2 ? 3 : 1;
    if (kx != ky) return kx - ky;
    if (x->kind == 1 && !strcmp(x->name, "..")) return -1;
    if (y->kind == 1 && !strcmp(y->name, "..")) return 1;
    return stricmp(x->name, y->name);
}
static int drive_exists(int d)                    /* 0 = A: ; INT 21h 4408h: no disk access, no "not ready" */
{
    __dpmi_regs r; memset(&r, 0, sizeof r); r.x.ax = 0x4408; r.h.bl = (unsigned char)(d + 1); __dpmi_int(0x21, &r);
    if (!(r.x.flags & 1)) return 1;
    return r.x.ax != 0x0F;                        /* "invalid function" (CD-ROM, network) still means it exists */
}
static void fl_add(const char* name, int kind, long size)
{
    static int cap;
    if (fl_n == cap) { Ent* t = (Ent*)realloc(fl_e, (size_t)(cap ? cap * 2 : 128) * sizeof(Ent)); if (!t) return; fl_e = t; cap = cap ? cap * 2 : 128; }
    if (!(fl_e[fl_n].name = strdup(name))) return;
    dosify(fl_e[fl_n].name); fl_e[fl_n].kind = kind; fl_e[fl_n].size = size; fl_e[fl_n].mark = 0; fl_n++;
}
static void fl_load(void)
{
    struct ffblk ff; int i, r;
    for (i = 0; i < fl_n; i++) free(fl_e[i].name);
    fl_n = 0; fl_sel = fl_top = 0; fl_si_have = 0; fl_si_name[0] = 0;
    if (!getcwd(fl_cwd, sizeof fl_cwd)) fl_cwd[0] = 0;
    /* one findfirst/findnext per entry (no stat): fast with thousands of files */
    for (r = findfirst("*.*", &ff, FA_RDONLY | FA_HIDDEN | FA_SYSTEM | FA_ARCH | FA_DIREC); !r; r = findnext(&ff))
    {
        if (!strcmp(ff.ff_name, ".")) continue;
        if (ff.ff_attrib & FA_DIREC) fl_add(ff.ff_name, 1, 0);
        else if (is_midi_name(ff.ff_name)) fl_add(ff.ff_name, 0, (long)ff.ff_fsize);
        else if (is_m3u_name(ff.ff_name)) fl_add(ff.ff_name, 3, (long)ff.ff_fsize);
    }
    for (i = 2; i < 26; i++) if (drive_exists(i)) { char n[4] = { (char)('A' + i), ':', 0 }; fl_add(n, 2, 0); }
    if (fl_n > 1) qsort(fl_e, (size_t)fl_n, sizeof(Ent), ent_cmp);
    fl_loaded = 1;
    if (fl_cwd[0]) snprintf(g_lastdir, sizeof g_lastdir, "%s", fl_cwd);
}
static const char* fl_nm(int i) { return fl_e[i].name; }
static void fl_full(int i, char* out, size_t sz)
{
    const char* cwd = fl_cwd; size_t l = strlen(cwd);
    snprintf(out, sz, "%s%s%s", cwd, (l && cwd[l - 1] != '/' && cwd[l - 1] != '\\') ? "/" : "", fl_e[i].name);
}
static void page_files(void)
{
    char cwd[300], b[120], t[16]; int r, i, marked = 0, x = 52;
    if (!fl_loaded) fl_load();
    for (i = 0; i < fl_n; i++) marked += fl_e[i].mark;
    snprintf(cwd, sizeof cwd, "%s", fl_cwd[0] ? fl_cwd : "?");
    dosify(cwd);
    snprintf(b, sizeof b, "Files \xFA %s", cwd);
    tui_fill(1, 2, 80, 22, 0x10, ' ');
    header(b);
    tui_fill(1, 2, 50, 1, 0x80, ' ');
    tui_put(4, 2, 0x8F, "Name"); tui_put(40, 2, 0x8F, "Size");
    list_keep(&fl_sel, &fl_top, fl_n);
    for (r = 0; r < LROWS && fl_top + r < fl_n; r++)
    {
        Ent* e = &fl_e[fl_top + r]; int y = 3 + r, sel = fl_top + r == fl_sel, bg = sel ? 0x70 : 0x10;
        char path[300]; int inpl;
        tui_fill(1, y, 50, 1, bg, ' ');
        if (e->mark) tui_cell(2, y, bg | 0x0E, 0xFB);
        if (e->kind == 1) { snprintf(b, sizeof b, "\x10%s", e->name); tui_putw(4, y, 34, bg | (sel ? 0x01 : 0x0B), b); tui_put(40, y, bg | 0x08, "folder"); }
        else if (e->kind == 2) { snprintf(b, sizeof b, "%s  drive", e->name); tui_putw(4, y, 34, bg | (sel ? 0x05 : 0x0D), b); }
        else
        {
            fl_full(fl_top + r, path, sizeof path); inpl = pl_find(path) >= 0;
            tui_putw(4, y, 34, bg | (e->kind == 3 ? (sel ? 0x02 : 0x0A) : sel ? 0x00 : inpl ? 0x0E : 0x0F), e->name);
            snprintf(b, sizeof b, "%5ld KB", (e->size + 1023) / 1024); tui_put(40, y, bg | 0x08, b);
            if (inpl) tui_cell(49, y, bg | 0x0E, 0x0D);
        }
    }
    if (!fl_n) tui_put(4, 4, 0x18, "(nothing here)");
    /* the side panel: what the selected song is */
    tui_panel(x, 3, 29, 19, 1, 0x1F, "Selected");
    if (fl_n && (fl_e[fl_sel].kind == 0))
    {
        char path[300]; fl_full(fl_sel, path, sizeof path);
        if (strcmp(path, fl_si_name)) { snprintf(fl_si_name, sizeof fl_si_name, "%s", path); fl_si_have = 0; }
        if (!fl_si_have && mp_next_in() >= 20000) { song_info(path, &fl_si); fl_si_have = 1; }
        tui_wrap(x + 2, 5, 25, 3, 0x1F, fl_si_have && fl_si.title[0] ? fl_si.title : fl_e[fl_sel].name);
        if (fl_si_have && fl_si.ok > 0)
        {
            fmt_time(t, (unsigned long)((fl_si.ms + 500) / 1000));
            tui_printf(x + 2, 9, 0x17, "Length   %s", t);
            tui_printf(x + 2, 10, 0x17, "Format   %d, %d track%s", fl_si.fmt, fl_si.trk, fl_si.trk == 1 ? "" : "s");
            tui_printf(x + 2, 11, 0x17, "Size     %ld KB", (fl_si.size + 1023) / 1024);
        }
        else if (fl_si_have) tui_put(x + 2, 9, 0x1C, "Not a MIDI file it can play");
        else tui_put(x + 2, 9, 0x18, "reading...");
        if (pl_find(path) >= 0) tui_put(x + 2, 13, 0x1E, "\x0D In the playlist");
        tui_put(x + 2, 15, 0x1B, "ENTER  play it now");
        tui_put(x + 2, 16, 0x1B, "INS    mark / unmark");
        tui_put(x + 2, 17, 0x1B, "^A     add this folder");
        tui_put(x + 2, 18, 0x1B, "^P     play this folder");
    }
    else if (fl_n && fl_e[fl_sel].kind == 3) { tui_wrap(x + 2, 5, 25, 2, 0x1F, fl_e[fl_sel].name); tui_wrap(x + 2, 8, 25, 4, 0x17, "A playlist (M3U). ENTER opens it."); }
    else if (fl_n && fl_e[fl_sel].kind == 2) tui_wrap(x + 2, 5, 25, 4, 0x17, "ENTER switches to this drive.");
    else if (fl_n) tui_wrap(x + 2, 5, 25, 4, 0x17, "ENTER opens the folder, BACKSPACE goes up one.");
    snprintf(b, sizeof b, "%d marked \xFA %d in the list", marked, g_npl);
    tui_putw(52, 22, 29, 0x13, b);
    find_line();
    hintline(marked ? "ENTER adds the marked songs (and plays the first of them)." : "Type a name to jump to it. \x0D = already in the playlist.");
    keybar("~ENTER~ Play/Open ~INS~ Mark ~^A~ Add folder ~^P~ Play folder ~BKSP~ Up ~TAB~ Page");
}
static void fl_enter_dir(const char* d) { if (!chdir(d)) fl_load(); }
static void key_files(int k)
{
    int i, first = -1;
    if (!fl_loaded) fl_load();
    if (!(k == K_BKSP && !find_active()) && find_key(k, fl_n, fl_nm, NULL, &fl_sel, &fl_top)) return;
    if (list_nav(k, &fl_sel, &fl_top, fl_n)) return;
    if (k == K_BKSP) { fl_enter_dir(".."); return; }
    if (k == K_MOUSE && tui_my >= 3 && tui_my < 3 + LROWS && tui_mx <= 50 && fl_top + tui_my - 3 < fl_n)
    {
        int j = fl_top + tui_my - 3;
        if (j != fl_sel) { fl_sel = j; return; }
        k = K_ENTER;
    }
    if (!fl_n) return;
    if (k == K_INS) { if (fl_e[fl_sel].kind == 0) fl_e[fl_sel].mark = !fl_e[fl_sel].mark; list_nav(K_DOWN, &fl_sel, &fl_top, fl_n); }
    else if (k == K_CA || k == K_CP)
    {
        if (k == K_CP) { mp_unload(); pl_clear(); }
        for (i = 0; i < fl_n; i++) if (fl_e[i].kind == 0)
        {
            char p[300]; fl_full(i, p, sizeof p);
            if (pl_find(p) < 0) { pl_add(p); if (first < 0) first = g_npl - 1; }
        }
        msg(first < 0 ? "No new songs here" : "Added the songs of this folder");
        if (k == K_CP && g_npl) { g_jump = -1; g_act = 4; g_page = PG_PLAYER; }
    }
    else if (k == K_ENTER)
    {
        Ent* e = &fl_e[fl_sel];
        if (e->kind == 1) fl_enter_dir(e->name);
        else if (e->kind == 2) { setdisk(e->name[0] - 'A'); fl_load(); }
        else if (e->kind == 3)
        {
            char p[300]; int was = g_npl, keep, n;
            fl_full(fl_sel, p, sizeof p);
            keep = g_npl && !tui_yesno("Replace the playlist with this one? (No: add its songs at the end)");
            if (!keep) { mp_unload(); pl_clear(); was = 0; }
            n = pl_load_m3u(p);
            msg("%d song%s from %s", n, n == 1 ? "" : "s", e->name);
            if (n > 0) { g_jump = was ? was : -1; g_act = 4; g_page = PG_PLAYER; }
        }
        else
        {
            int marked = 0;
            for (i = 0; i < fl_n; i++) if (fl_e[i].mark)
            {
                char p[300]; int at; fl_full(i, p, sizeof p);
                at = pl_find(p); if (at < 0) { pl_add(p); at = g_npl - 1; }
                if (first < 0) first = at;
                fl_e[i].mark = 0; marked++;
            }
            if (!marked)
            {
                char p[300]; fl_full(fl_sel, p, sizeof p);
                first = pl_find(p); if (first < 0) { pl_add(p); first = g_npl - 1; }
            }
            if (first >= 0) { g_jump = first; g_act = 4; g_page = PG_PLAYER; }
        }
    }
}

#define M3_DYNAMIC_RES               /* the 3D view in any VESA mode (m3core.h), not only 640x480 */
#include "midplay3d.h"

/* ---- Options */
static const char* const opt_name[] = { "Player view", "Repeat", "Shuffle", "Reset before each song", "Speed",
                                        "Pause between songs", "Started without files", "Display mode", "Style", "Letters", "3D screen mode", "3D refresh rate" };
static const char* const opt_help[] = {
    "S on the Player page switches it too. 3D: the screen mode is chosen below.",
    "R on the Player page. One = the same song again and again.",
    "H on the Player page. A random order: every song once before any comes again; P goes back. This folder: only the songs in the playing song's folder.",
    "GS reset suits the SC-55 and SC-55mkII. GM On for other General MIDI synths. None: what the last song left.",
    "[ and ] on the Player page, BACKSPACE back to 100%. Slower or faster, same pitch.",
    "Seconds of silence after a song before the next one starts.",
    "MIDPLAY with no file names: the last playlist (at the song and time you left), or every MIDI file next to MIDPLAY.EXE.",
    "The screen mode chosen in MENU (F4 anywhere). Bigger text modes show the screen in the middle.",
    "The colours (F3 anywhere); shared with MENU and the other programs.",
    "The font of the text (F7 anywhere); shared with MENU and the other programs.",
    "The 3D view's resolution and colours (the modes this graphics card offers). Bigger = sharper but slower. A new mode asks: Y keeps it, any other key or 10 s goes back. In a Windows DOS box only 320x200 VGA shows in a window.",
    "Needs a VBE 3.0 graphics card. Only rates your monitor can show! A new rate asks: Y keeps it, any other key or 10 s goes back." };
static const int hz_list[] = { 0, 60, 70, 72, 75, 85, 100 };
static int op_sel;
#define OP_N 12
static void opt_value(int i, char* b, size_t n)
{
    switch (i)
    {
    case 0: snprintf(b, n, "%s", (const char*[]){"Channel table", "SC-55 front panel", "3D"}[g_view]); break;
    case 1: snprintf(b, n, "%s", (const char*[]){"Off", "All songs", "One song"}[g_rep]); break;
    case 2: snprintf(b, n, "%s", (const char*[]){"Off", "All songs", "This folder"}[g_shuf]); break;
    case 3: snprintf(b, n, "%s", (const char*[]){"GS reset (SC-55)", "GM System On", "None"}[g_reset]); break;
    case 4: snprintf(b, n, "%d%%", P.speed); break;
    case 5: snprintf(b, n, g_gap ? "%d s" : "None", g_gap); break;
    case 6: snprintf(b, n, "%s", g_startlast ? "Last playlist" : "MIDI files in MIDPLAY's folder"); break;
    case 7: snprintf(b, n, "%s", tui_mode_id); break;
    case 8: snprintf(b, n, "%s", tui_theme_name[tui_theme]); break;
    case 9: snprintf(b, n, "%s", tui_font_name[tui_font]); break;
    case 10:
        if (!mp3_want_w) { char m[48]; snprintf(b, n, "Auto (%s)", m3_nmodes ? mp3_mode_name(mp3_auto(), m, sizeof m) : "no VESA"); }
        else { int k = mp3_find(mp3_want_w, mp3_want_h, mp3_want_b); if (k >= 0) mp3_mode_name(k, b, n); else snprintf(b, n, "%dx%d (not offered here)", mp3_want_w, mp3_want_h); }
        break;
    case 11:
        if (m3_vbever < 3) snprintf(b, n, "BIOS default (card: VBE %d.%d)", m3_vbever >> 4, m3_vbever & 15);
        else if (!mp3_hz) snprintf(b, n, "BIOS default");
        else snprintf(b, n, "%d Hz", mp3_hz);
        break;
    }
}
static void page_options(void)
{
    char b[80]; int i;
    tui_fill(1, 2, 80, 22, 0x10, ' ');
    header("Options \xFA kept in MIDPLAY.CFG");
    tui_panel(8, 3, 66, OP_N + 4, 1, 0x1F, "Options");
    for (i = 0; i < OP_N; i++)
    {
        int y = 5 + i, sel = i == op_sel, bg = sel ? 0x70 : 0x10;
        tui_fill(10, y, 62, 1, bg, ' ');
        tui_put(11, y, bg | (sel ? 0x00 : 0x07), opt_name[i]);
        opt_value(i, b, sizeof b);
        tui_cell(36, y, bg | 0x08, 0x11); tui_putw(38, y, 31, bg | (sel ? 0x01 : 0x0E), b); tui_cell(70, y, bg | 0x08, 0x10);
    }
    tui_wrap(10, OP_N + 8, 62, 3, 0x1B, opt_help[op_sel]);
    hintline("LEFT / RIGHT or ENTER change the setting. ESC goes back.");
    keybar("~\x18\x19~ Choose  ~\x1B\x1A~ Change  ~ENTER~ Next value  ~ESC~ Back  ~F1~ Help");
}
static void opt_change(int i, int d)
{
    switch (i)
    {
    case 0: g_view = (g_view + 3 + d) % 3; break;
    case 1: g_rep = (g_rep + 3 + d) % 3; break;
    case 2: g_shuf = (g_shuf + 3 + d) % 3; break;
    case 3: g_reset = (g_reset + 3 + d) % 3; break;
    case 4: set_speed(P.speed + d * 5); break;
    case 5: g_gap = (g_gap + 11 + d) % 11; break;
    case 6: g_startlast = !g_startlast; break;
    case 7: tui_pick_mode(g_home); tui_invalidate(); break;
    case 8: tui_theme_next(g_home); break;
    case 9: tui_font = (tui_font + 1) % FONT_COUNT; tui_theme_save(g_home); tui_font_loaded = 0; tui_font_apply(); tui_invalidate(); break;
    case 10:
        if (m3_nmodes)
        {   /* auto, then every mode the card offers */
            int k = mp3_want_w ? mp3_find(mp3_want_w, mp3_want_h, mp3_want_b) : -1;
            k = (k + 1 + d + m3_nmodes + 1) % (m3_nmodes + 1) - 1;
            if (k < 0) mp3_want_w = mp3_want_h = mp3_want_b = 0;
            else { mp3_want_w = m3_modes[k].w; mp3_want_h = m3_modes[k].h; mp3_want_b = m3_modes[k].bpp; }
            mp3_ask = 1;
        }
        break;
    case 11:
        if (m3_vbever >= 3)
        {
            int k, nh = (int)(sizeof hz_list / sizeof hz_list[0]);
            for (k = 0; k < nh && hz_list[k] != mp3_hz; k++) ;
            mp3_hz = hz_list[((k < nh ? k : 0) + d + nh) % nh]; mp3_ask = 1;
        }
        else msg("Refresh rates need a VBE 3.0 graphics card (this one: VBE %d.%d).", m3_vbever >> 4, m3_vbever & 15);
        break;
    }
    cfg_save();
}
static void key_options(int k)
{
    if (k == K_UP) op_sel = (op_sel + OP_N - 1) % OP_N;
    else if (k == K_DOWN) op_sel = (op_sel + 1) % OP_N;
    else if (k == K_LEFT) opt_change(op_sel, -1);
    else if (k == K_RIGHT || k == K_ENTER) opt_change(op_sel, 1);
    else if (k == K_MOUSE && tui_my >= 5 && tui_my < 5 + OP_N) { if (op_sel == tui_my - 5) opt_change(op_sel, 1); else op_sel = tui_my - 5; }
}

/* ---- Help */
static void page_help(void)
{
    static const char* const h[] = {
        "~ON EVERY PAGE~",
        "  SPACE pause / play      < >  previous / next song    ^\x1B ^\x1A  seek 10 s",
        "  TAB   next page         F1 help   F2 options   F3 style   F4 display mode",
        "  ESC   back (on the Player page: quit)",
        "~PLAYER~",
        "  \x1B \x1A   seek 10 s      PGUP PGDN  seek 60 s      HOME  start of the song",
        "  N P   next / previous   R  repeat off/all/one    H  shuffle off/all/folder",
        "  \x18 \x19   choose a channel (or the SC-55 part)  M mute it  O solo it  U all on",
        "  [ ]   slower / faster, BACKSPACE = 100%     B  the files     Q  quit",
        "  S     channels \x1A SC-55 front panel \x1A 3D   (F4 in 3D: full / half resolution)",
        "  3D:   V look (solid / vector / voxel)   C colours   J player (module, boombox, jukebox, turntable)",
        "~PLAYLIST~   (type a name to jump to it)",
        "  ENTER play   DEL remove   ^\x18 ^\x19 move   F5 sort: name, title, length, shuffle",
        "  ^S save it as .M3U   ^O open a .M3U   INS add songs",
        "~FILES~      (type a name to jump to it)",
        "  ENTER play the song now, or open the folder, drive or .M3U   BACKSPACE up",
        "  INS mark songs (ENTER adds them)   ^A add this folder   ^P play this folder",
        "~MOUSE~  click: choose, click again: play or open. Wheel scrolls. Click the time",
        "  bar to seek. Right click: back. The playlist, song and time are remembered.",
        NULL };
    int i, y = 3;
    tui_fill(1, 2, 80, 22, 0x10, ' ');
    header("Help");
    for (i = 0; h[i]; i++, y++)
    {
        const char* s = h[i]; int x = 3, hl = 0;
        for (; *s; s++) { if (*s == '~') { hl = !hl; continue; } tui_cell(x++, y, hl ? 0x1E : 0x17, (unsigned char)*s); }
    }
    hintline("The music goes to SBEMU's MPU-401: SC-55, MT-32 or SoundFont, as set in SETTINGS.");
    keybar("~ESC~ Back  ~F2~ Options  ~TAB~ Next page");
}

/* ================================================================== the 3D view, frames, keys, main */

static uint64_t g_next_frame, g_decay_t, g_draw_us;
static int want_3d(void) { return g_ui && g_page == PG_PLAYER && g_view == 2; }
static void to_text(void)
{
    if (!mp3_on) return;
    mp3_leave();
    tui_set_saved_mode();
    _setcursortype(_NOCURSOR);
    tui_font_loaded = 0; tui_font_apply(); tui_apply_theme();
    tui_invalidate();
}
static void screen_sync(void)
{
    if (want_3d()) { if (!mp3_on && !mp3_enter()) { g_view = 0; msg("No 3D screen mode worked here (not even VGA 320x200): the text views stay."); } }
    else to_text();
}
/* the text screen to video memory, with the mouse pointer (an inverted cell) */
static void mp_flush(void)
{
    int mx = tui_mx - 1, my = tui_my - 1, ptr = tui_mshow && mx >= 0 && mx < TUI_W && my >= 0 && my < TUI_H;
    uint16_t keep = 0;
    if (ptr)
    {
        uint16_t c = keep = tui_scr[my][mx];
        tui_scr[my][mx] = (uint16_t)((c & 0x00FF) | ((c & 0x0F00) << 4) | ((c & 0xF000) >> 4) | (((c >> 12) == ((c >> 8) & 15)) ? 0x7000 : 0));
    }
    tui_flush_now();
    if (ptr) tui_scr[my][mx] = keep;
}
static void draw_player(void)
{
    static const char* const vname[3] = { "Channels", "SC-55 front panel", "3D" };
    char b[120];
    snprintf(b, sizeof b, "%s \xFA MPU %Xh%s", vname[g_view], base, g_dry ? " \xFA test, no MIDI" : "");
    header(b);
    now_playing();
    if (g_view == 1) sc55_panel(); else channel_table();
    keyboard();
    hintline(g_view == 1 ? "\x18\x19 part shown. SC-55: the module's real display, live from SBEMU."
                         : "\x18\x19 channel: M mute, O solo, U all on   [ ] speed   R repeat   H shuffle");
    keybar("~F1~ Help ~F2~ Options ~SPACE~ Pause ~\x1B\x1A~ Seek ~N P~ Song ~S~ View ~TAB~ Lists ~ESC~ Quit");
}
/* one frame, if it is time: paced so drawing takes at most ~25% of the time (text) */
static void ui_frame(void)
{
    uint64_t t = now_us(), t1;
    int n;
    if (!g_ui || t < g_next_frame) return;
    n = (int)((t - g_decay_t) / 40000); if (n > 6) n = 6;
    if (n > 0) { g_decay_t = t; while (n--) viz_decay(); }
    screen_sync();
    if (mp3_on)
    {
        mp3_draw(&P.s, g_cur >= 0 && g_cur < g_npl ? g_pl[g_cur].path : "", song_time(), P.total, P.paused, P.ending);
        g_next_frame = t + 10000;                       /* mp3_draw paces itself */
        return;
    }
    if (g_page == PG_PLAYER) draw_player();
    else if (g_page == PG_LIST) page_playlist();
    else if (g_page == PG_FILES) page_files();
    else if (g_page == PG_OPTIONS) page_options();
    else page_help();
    mp_flush();
    if (tui_fade_pending) { tui_fade_pending = 0; tui_fade(1); }   /* Groovy styles fade in after the first frame */
    t1 = now_us();
    g_draw_us = t1 - t;
    st_frame(g_draw_us);
    g_next_frame = t + (g_draw_us * 4 > 40000 ? g_draw_us * 4 : 40000);
}

static void go_page(int p)
{
    if (p == g_page) return;
    if (p == PG_FILES)
    {   /* the folder of the song that plays (else the last one looked at) */
        char d[300] = ""; char* e;
        if (g_cur >= 0 && g_cur < g_npl) { snprintf(d, sizeof d, "%s", g_pl[g_cur].path); e = strrchr(d, '/'); if (!e) e = strrchr(d, '\\'); if (e) { if (e == d + 2) e[1] = 0; else *e = 0; } else d[0] = 0; }
        if (!d[0]) snprintf(d, sizeof d, "%s", g_lastdir);
        if (d[0] && !chdir(d)) { if (d[1] == ':') setdisk(toupper((unsigned char)d[0]) - 'A'); fl_loaded = 0; }
    }
    if (p == PG_LIST && g_cur >= 0) { pl_sel = g_cur; list_keep(&pl_sel, &pl_top, g_npl); }
    if ((p == PG_OPTIONS || p == PG_HELP) && g_page != PG_OPTIONS && g_page != PG_HELP) g_back = g_page;
    g_page = p; g_find[0] = 0; g_next_frame = 0;
}
static void key_player(int k)
{
    switch (k)
    {
    case 'n': case 'N': case K_END: g_act = 1; break;
    case 'p': case 'P': g_act = 3; break;
    case 'q': case 'Q': g_act = 2; break;
    case K_LEFT: mp_seek_rel(-10); break;
    case K_RIGHT: mp_seek_rel(10); break;
    case K_PGUP: mp_seek_rel(-60); break;
    case K_PGDN: mp_seek_rel(60); break;
    case K_HOME: mp_seek(0); break;
    case K_UP: case '-': case '_': case ',': g_chsel = (g_chsel + 15) % 16; break;
    case K_DOWN: case '+': case '=': case '.': g_chsel = (g_chsel + 1) % 16; break;
    case 'm': case 'M': g_mute ^= (uint16_t)(1 << g_chsel); if (!audible(g_chsel)) channel_quiet(g_chsel);
        msg("Channel %d %s", g_chsel + 1, g_mute >> g_chsel & 1 ? "muted" : "on"); break;
    case 'o': case 'O':
        g_solo ^= (uint16_t)(1 << g_chsel);
        { int c; for (c = 0; c < 16; c++) if (!audible(c)) channel_quiet(c); }
        msg(g_solo ? "Solo: only the channels marked S play" : "Solo off"); break;
    case 'u': case 'U': g_mute = g_solo = 0; msg("All channels on"); break;
    case '[': set_speed(P.speed - 5); msg("Speed %d%%", P.speed); break;
    case ']': set_speed(P.speed + 5); msg("Speed %d%%", P.speed); break;
    case K_BKSP: set_speed(100); msg("Speed 100%%"); break;
    case 'r': case 'R': g_rep = (g_rep + 1) % 3; msg("Repeat %s", (const char*[]){"off", "all songs", "one song"}[g_rep]); break;
    case 'l': case 'L': g_rep = g_rep ? 0 : 1; msg("Repeat %s", g_rep ? "all songs" : "off"); break;
    case 'h': case 'H': g_shuf = (g_shuf + 1) % 3; msg("Shuffle %s", (const char*[]){"off", "on: all songs", "on: this folder only"}[g_shuf]); break;
    case 's': case 'S':
        g_view = (g_view + 1) % 3; screen_sync(); cfg_save(); break;
    case 'b': case 'B': go_page(PG_FILES); break;
    case 'v': case 'V': g_scene = (g_scene + 1) % 3; mp3_force = 1; cfg_save(); msg("3D look: %s", mp3_scene_name[g_scene]); break;
    case 'c': case 'C': g_ctheme = (g_ctheme + 1) % 7; mp3_force = 1; cfg_save(); msg("3D colours: %s", mp3_theme_name[g_ctheme]); break;
    case 'j': case 'J': g_model = (g_model + 1) % 4; mp3_force = 1; cfg_save(); msg("3D player: %s", mp3_model_name[g_model]); break;
    case K_F4: if (mp3_on) { m3_half = !m3_half; m3_rw = m3_half ? M3W / 2 : M3W; m3_rh = m3_half ? M3H / 2 : M3H; mp3_slow = 0; return; } break;
    case K_MOUSE:
        if (tui_my == 4 && tui_mx >= 9 && tui_mx < 67 && P.loaded) mp_seek(P.total * (uint64_t)(tui_mx - 9) / 58);
        else if (g_view == 0 && tui_my >= 6 && tui_my < 22) { if (tui_mx == 5 && g_chsel == tui_my - 6) key_player('m'); g_chsel = tui_my - 6; }
        break;
    default: break;
    }
}
static void handle_key(int k)
{
    if (mp3_on && mp3_ask_until) { mp3_answer(k == 'y' || k == 'Y'); mp3_force = 1; cfg_save(); return; }   /* "keep this mode?" */
    /* everywhere */
    if (k == K_F1) { if (g_page == PG_HELP) go_page(g_back); else go_page(PG_HELP); return; }
    if (k == K_F2) { if (g_page == PG_OPTIONS) go_page(g_back); else go_page(PG_OPTIONS); return; }
    if (k == K_F3) { to_text(); tui_theme_next(g_home); return; }
    if (k == K_F4 && !mp3_on) { tui_pick_mode(g_home); tui_invalidate(); return; }
    if (k == K_F7 && !mp3_on) { tui_font = (tui_font + 1) % FONT_COUNT; tui_theme_save(g_home); tui_font_loaded = 0; tui_font_apply(); tui_invalidate(); return; }
    if (k == ' ') { mp_pause(!P.paused); return; }
    if (k == '<') { g_act = 3; return; }
    if (k == '>') { g_act = 1; return; }
    if (k == K_CLEFT) { mp_seek_rel(-10); return; }
    if (k == K_CRIGHT) { mp_seek_rel(10); return; }
    if (k == K_TAB) { go_page(g_page == PG_PLAYER ? PG_LIST : g_page == PG_LIST ? PG_FILES : PG_PLAYER); return; }
    if (k == K_STAB) { go_page(g_page == PG_PLAYER ? PG_FILES : g_page == PG_FILES ? PG_LIST : PG_PLAYER); return; }
    if (k == K_ESC || k == K_RCLICK)
    {
        if (g_page == PG_OPTIONS || g_page == PG_HELP) go_page(g_back);
        else if (g_page != PG_PLAYER) go_page(PG_PLAYER);
        else if (k == K_ESC) g_act = 2;
        return;
    }
    switch (g_page)
    {
    case PG_PLAYER: key_player(k); break;
    case PG_LIST: key_playlist(k); break;
    case PG_FILES: key_files(k); break;
    case PG_OPTIONS: key_options(k); break;
    default: break;
    }
}
static int g_mouse_key;
static int get_input(void)
{
    static uint64_t next_mouse;
    if (key_pressed()) return key_read();
    if (g_ui && !mp3_on && now_us() >= next_mouse)
    {   /* INT 33h is a mode switch: ~30 times a second is plenty */
        int ox = tui_mx, oy = tui_my;
        next_mouse = now_us() + 33000;
        g_mouse_key = tui_mouse_poll();
        if (ox != tui_mx || oy != tui_my) g_next_frame = 0;
        if (g_mouse_key) { int m = g_mouse_key; g_mouse_key = 0; return m; }
    }
    return 0;
}

/* ---- songs */
static int g_plays;
static int start_index(int i)
{
    int tries = 0;
    while (g_npl && i >= 0 && tries < g_npl)
    {
        char err[64];
        if (i >= g_npl) i = 0;
        g_cur = i;
        q_playing(i);
        if (mp_start(g_pl[i].path, g_reset, err, sizeof err))
        {
            g_plays++;
            if (g_stfile[0]) { FILE* f = fopen(g_stfile, "a"); if (f) { fprintf(f, "start %d %s (act %d, t %lu ms)\n", i, g_pl[i].path, g_act, (unsigned long)(now_us() / 1000)); fclose(f); } }
            return 1;
        }
        msg("%s: %s", base_of(g_pl[i].path), err);
        g_pl[i].have = 1; g_pl[i].si.ok = -1;
        i = g_shuf ? next_index(0) : i + 1;        /* a song that cannot be played: the next one in the same order */
        tries++;
    }
    mp_unload(); if (g_cur >= g_npl) g_cur = -1;
    return 0;
}

static void plain_status(void)
{
    static uint64_t last;
    uint64_t t = now_us(), el;
    if (t - last < 250000) return;
    last = t;
    el = song_time() / 1000000ULL;
    printf("\r  %lu:%02lu %s ", (unsigned long)(el / 60), (unsigned long)(el % 60), P.paused ? "(paused)" : "        ");
    fflush(stdout);
}

int main(int argc, char** argv)
{
    int i, want_browse = 0, plain = 0, files = 0, finished, seek_resume = 0;
    uint64_t gap_until = 0; int gap_next = -1;
    char dir_arg[260] = "";
    /* home: the folder of MIDPLAY.EXE (MIDPLAY.CFG, the auto playlist, THEME.CFG) */
    {
        char* e; _fixpath(argv[0], g_home);
        e = strrchr(g_home, '/'); if (!e) e = strrchr(g_home, '\\');
        if (e) *e = 0; else strcpy(g_home, ".");
        dosify(g_home);
        snprintf(g_cfg, sizeof g_cfg, "%s\\MIDPLAY.CFG", g_home);
        snprintf(g_m3u, sizeof g_m3u, "%s\\MIDPLAY.M3U", g_home);
        snprintf(g_shf, sizeof g_shf, "%s\\MIDPLAY.SHF", g_home);
    }
    cfg_load();
    srand((unsigned)time(NULL)); g_rng ^= (uint32_t)time(NULL) * 2654435761u; if (!g_rng) g_rng = 1;
    for (i = 1; i < argc; i++)
    {
        const char* a = argv[i];
        if (!strncasecmp(a, "/P:", 3)) base = (int)strtol(a + 3, NULL, 16);
        else if (!strncasecmp(a, "/D:", 3)) snprintf(dir_arg, sizeof dir_arg, "%s", a + 3);
        else if (!strcasecmp(a, "/L")) g_rep = 1;
        else if (!strcasecmp(a, "/R1")) g_rep = 2;
        else if (!strcasecmp(a, "/H")) g_shuf = 1;
        else if (!strcasecmp(a, "/B")) want_browse = 1;
        else if (!strcasecmp(a, "/S")) plain = 1;
        else if (!strcasecmp(a, "/GM")) g_reset = 1;
        else if (!strcasecmp(a, "/N")) g_reset = 2;
        else if (!strncasecmp(a, "/T:", 3)) snprintf(g_stfile, sizeof g_stfile, "%s", a + 3);
        else if (!strncasecmp(a, "/Q:", 3)) g_quit_s = atol(a + 3);
        else if (!strncasecmp(a, "/V:", 3)) g_view = atoi(a + 3) % 3;
        else if (!strcasecmp(a, "/X")) g_dry = 1;
        else if (a[0] == '/')
        {
            puts("MIDPLAY [options] [file.mid ...]  plays MIDI files through SBEMU's MPU-401 (run SC55.BAT first)\n"
                 "  no files   the last playlist (or every MIDI file next to MIDPLAY.EXE - Options)\n"
                 "  /D:dir     every MIDI file in that folder      /B   start in the file explorer\n"
                 "  /L repeat all   /R1 repeat one   /H shuffle    /S   plain one-line display\n"
                 "  /GM GM System On instead of the GS reset   /N no reset   /P:330 MPU port (hex)\n"
                 "In the player: F1 shows every key.");
            return 2;
        }
        else { pl_add(a); files++; }
    }
    clock_init();
    /* the 3D view uses MIDPLAY's clock (m3core's own 3-tick calibration is wrong in a Windows DOS box, where the BIOS
       tick comes late and in bursts); its mode choice is the one for here (DOS or Windows) */
    if (g_tsc) { m3_tsc_us = CK.per_us; m3_t0 = rdtsc64(); }
    mp3_win = g_win;
    { int* m = g_m3cfg[g_win != 0]; mp3_want_w = m[0]; mp3_want_h = m[1]; mp3_want_b = m[2]; mp3_hz = m[3]; }
    { int p = g_dry ? 0 : mpu_find(base); if (p) base = p; }
    if (!g_dry)
    {
        printf("MPU-401 at port %Xh: ", base);
        if (!mpu_detect()) { puts("no answer.\nIs SBEMU loaded (SC55.BAT) with its MPU-401 on? Run SC55.BAT first, in the same DOS session."); return 1; }
        puts("found.");
        mpu_uart();
    }
    /* the songs: file names, /D, else the last playlist (and song), else MIDPLAY's folder */
    if (dir_arg[0]) files += scan_dir(dir_arg);
    if (!files && !want_browse)
    {
        if (g_startlast && pl_load_m3u(g_m3u) > 0)
        {
            if (g_last_index >= 0 && g_last_index < g_npl) { g_cur = (int)g_last_index; seek_resume = 1; }
            if (g_shuf) q_load(g_shf);                 /* the same shuffle round, where it was */
        }
        else { pl_clear(); if (!scan_dir(g_home)) scan_dir("."); }
    }
    if (!g_npl && plain) { puts("No MIDI files. Give file names, or run without /S to use the file explorer."); return 1; }
    g_ui = !plain;
    if (g_ui)
    {
        tui_theme_load(g_home);
        if (g_win && strncmp(tui_mode_id, "VGA", 3) && strncmp(tui_mode_id, "EGA", 3))
        {   /* a Windows DOS box: tweaked (TWK) and VESA text/graphics modes cannot be shown in a window - plain VGA text
               for this run (THEME.CFG is not changed, so MENU in plain DOS keeps its mode) */
            snprintf(tui_mode_id, sizeof tui_mode_id, "VGA80x25");
            msg("Windows: plain 80x25 text here (the display mode set in MENU needs full-screen DOS).");
        }
        tui_init();
        tui_hook = mp_pump;
        m3_hook = mp_pump;
        tui_mouse_init();
        if (!g_npl || want_browse) go_page(PG_FILES);
    }
    if (g_ui) { m3_vbe_probe(); mp3_prepare(); if (m3_vbest >= 0) mp3_make_panel("", 0, 0); screen_sync(); ui_frame(); }   /* the slow BIOS work (3D mode, first frame) before the music */
    if (g_npl)
    {
        if (start_index(g_cur >= 0 ? g_cur : g_shuf ? next_index(0) : 0) && seek_resume && g_last_pos > 2)   /* shuffle: a random first song */
        {
            mp_seek((uint64_t)g_last_pos * 1000000ULL);
            msg("Back where you left: %s at %ld:%02ld (HOME starts the song from the beginning)", pl_name(g_cur), g_last_pos / 60, g_last_pos % 60);
        }
        if (!g_ui) printf("%s\n", g_pl[g_cur >= 0 ? g_cur : 0].path);
    }
    for (;;)
    {
        int k;
        mp_pump();
        if (g_quit_s && P.loaded && song_time() > (uint64_t)g_quit_s * 1000000ULL) break;
        /* keys first: an N pressed just as a song ends must skip the ending song, not the one that already started
           in its place (that skipped two) */
        k = get_input();
        if (k)
        {
            if (g_ui) { handle_key(k); g_next_frame = 0; mp3_force = 1; }
            else if (k == 27 || k == 'q' || k == 'Q') g_act = 2;
            else if (k == ' ') mp_pause(!P.paused);
            else if (k == 'n' || k == 'N') g_act = 1;
            else if (k == 'p' || k == 'P') g_act = 3;
        }
        if (g_act)
        {
            int a = g_act; g_act = 0; gap_next = -1;
            if (a == 2) break;
            if (a == 1 && g_npl) { int n = next_index(0); if (n >= 0) start_index(n); }
            else if (a == 3 && g_npl)
            {
                if (P.loaded && song_time() > 3000000ULL) mp_seek(0);       /* like a CD player: first back to the start */
                else { int n = prev_index(); if (n >= 0) start_index(n); }
            }
            else if (a == 4 && g_jump == -1 && g_npl) { q_reset(); g_cur = -1; start_index(g_shuf ? next_index(0) : 0); }   /* a new list */
            else if (a == 4 && g_jump >= 0 && g_jump < g_npl) start_index(g_jump);
            if (!g_ui && P.loaded) printf("\n%s\n", g_pl[g_cur].path);
        }
        finished = 0;
        if (P.loaded && mp_finished())
        {
            int n = next_index(1);
            if (n < 0) { mp_unload(); msg("End of the playlist"); if (!g_ui) break; }
            else if (g_gap) { mp_unload(); gap_until = now_us() + (uint64_t)g_gap * 1000000ULL; gap_next = n; }
            else { start_index(n); if (!g_ui && P.loaded) printf("\n%s\n", g_pl[g_cur].path); }
            finished = 1;
        }
        if (gap_next >= 0 && now_us() >= gap_until) { start_index(gap_next); gap_next = -1; }
        if (!g_ui && !P.loaded && gap_next < 0) break;                      /* /S: nothing (left) that can be played */
        (void)finished;
        if (g_win && mp_next_in() > 20000 && (!g_ui || now_us() + 20000 < g_next_frame) && !mp3_on) __dpmi_yield();
        if (g_ui) { ui_frame(); pl_info_step(g_page == PG_LIST ? pl_sel : g_cur + 1 < g_npl ? g_cur + 1 : -1); }
        else plain_status();
    }
    cfg_save();
    if (g_npl) pl_save_m3u(g_m3u);
    q_save(g_shf);
    mp_unload();
    tui_hook = NULL; m3_hook = NULL;
    if (g_ui) { mp3_leave(); tui_done(); }
    else printf("\n");
    { char lb[80]; snprintf(lb, sizeof lb, "view %d mode %s%s", g_view, tui_mode_id, g_dry ? " dry" : ""); st_write(lb); }
    return 0;
}
#else /* HOSTTEST: plays the file with the virtual clock, every byte printed with its time */
/* --queue: the play order (shuffle, next, previous) checked without DOS */
static int qt_fail;
#define QT(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); qt_fail++; } } while (0)
static void qt_play(int i) { g_cur = i; q_playing(i); }
static void qt_list(int n, int dirs)
{
    char b[64]; int i;
    pl_clear();
    for (i = 0; i < n; i++) { snprintf(b, sizeof b, "c:/m/d%d/s%04d.mid", dirs ? i % dirs : 0, i); pl_add(b); }
}
static int queue_test(void)
{
    int n, i, k, r, seen[200], seq[200], first, last;
    for (r = 0; r < 50; r++)
    {
        g_rng = 12345u + (uint32_t)r * 7919u;
        n = 1 + r % 37 + (r > 40 ? 120 : 0); g_shuf = 1; g_rep = 0;
        qt_list(n, 0); q_reset();
        memset(seen, 0, sizeof seen);
        qt_play(r % n); seen[g_cur] = 1; seq[0] = g_cur;
        for (k = 1; k < n; k++) { i = next_index(1); QT(i >= 0 && i < n && !seen[i], "round %d: song %d twice or bad (k %d n %d)", r, i, k, n); if (i < 0) break; seen[i] = 1; seq[k] = i; qt_play(i); }
        QT(next_index(1) == -1, "round %d: repeat off must stop after every song", r);
        for (k = n - 2; k >= 0; k--) { i = prev_index(); QT(i == seq[k], "round %d: P gave %d, expected %d", r, i, seq[k]); qt_play(i); }
        QT(prev_index() == seq[0], "round %d: P at the start stays", r);
        for (k = 1; k < n; k++) { i = next_index(0); QT(i == seq[k], "round %d: N after P gave %d, expected %d", r, i, seq[k]); qt_play(i); }
        g_rep = 1; last = g_cur; first = next_index(1);
        QT(n == 1 || first != last, "round %d: new round starts with the song that just played", r);
        qt_play(first); memset(seen, 0, sizeof seen); seen[first] = 1;
        for (k = 1; k < n; k++) { i = next_index(1); QT(!seen[i], "round %d: 2nd round repeats %d", r, i); seen[i] = 1; qt_play(i); }
    }
    /* remove the playing song and others mid-round, add songs mid-round */
    g_rng = 99; g_shuf = 1; g_rep = 0; qt_list(30, 0); q_reset(); qt_play(5);
    memset(seen, 0, sizeof seen); seen[5] = 1;
    for (k = 0; k < 10; k++) { i = next_index(0); seen[i] = 1; qt_play(i); }
    { int gone = g_cur; char keep[40]; snprintf(keep, sizeof keep, "%s", g_pl[gone].path);
      pl_remove(gone); pl_remove(0);                           /* the playing song and the first one leave */
      pl_add("c:/m/new1.mid"); pl_add("c:/m/new2.mid");
      { int cnt = 0, nn = 0, got1 = 0, got2 = 0, ok = 1; char* pseen[64] = {0};
        while ((i = next_index(1)) >= 0 && cnt < 100)
        {
            if (!strcmp(g_pl[i].path, keep)) ok = 0;
            for (k = 0; k < nn; k++) if (pseen[k] == g_pl[i].path) ok = 0;
            pseen[nn++] = g_pl[i].path; got1 |= !strcmp(g_pl[i].path, "c:/m/new1.mid"); got2 |= !strcmp(g_pl[i].path, "c:/m/new2.mid");
            qt_play(i); cnt++;
        }
        QT(ok, "after removing: a song came twice or the removed one came back");
        QT(got1 && got2, "songs added mid-round were not played in it");
        QT(cnt == 30 - 11 - 1 + 2, "rest of the round: %d songs, expected %d", cnt, 30 - 11 - 1 + 2);
      } }
    /* this folder only */
    g_shuf = 2; g_rep = 0; qt_list(40, 4); q_reset(); qt_play(6);
    { int cnt = 1; while ((i = next_index(1)) >= 0) { QT(same_dir(g_pl[i].path, g_pl[6].path), "folder shuffle left the folder: %s", g_pl[i].path); qt_play(i); cnt++; if (cnt > 50) break; }
      QT(cnt == 10, "folder shuffle played %d songs, expected 10", cnt); }
    /* in order: wrap, end, removed current */
    g_shuf = 0; g_rep = 0; qt_list(5, 0); qt_play(4);
    QT(next_index(1) == -1, "in order: end of list should stop"); QT(next_index(0) == 0, "in order: N at the end wraps");
    QT(prev_index() == 3, "in order: P"); qt_play(0); QT(prev_index() == 4, "in order: P at the start wraps");
    qt_play(2); pl_remove(2); QT(next_index(0) == 2 && !strcmp(g_pl[2].path, "c:/m/d0/s0003.mid"), "in order: N after removing the playing song");
    QT(prev_index() == 1, "in order: P after removing the playing song gave %d", prev_index());
    /* MIDPLAY.SHF round trip */
    g_shuf = 1; g_rep = 0; qt_list(25, 0); q_reset(); qt_play(3); for (k = 0; k < 7; k++) qt_play(next_index(0));
    { int want = next_index(0), wantp = prev_index(); q_save("/tmp/qt.shf"); q_reset(); q_load("/tmp/qt.shf");
      QT(next_index(0) == want && prev_index() == wantp, "MIDPLAY.SHF: order not restored"); }
    printf(qt_fail ? "queue test: %d FAILED\n" : "queue test: all passed\n", qt_fail);
    return qt_fail != 0;
}
int main(int argc, char** argv)
{
    char err[64];
    if (argc > 1 && !strcmp(argv[1], "--queue")) return queue_test();
    if (argc < 2 || !mp_start(argv[1], 2, err, sizeof err)) { fprintf(stderr, "load failed: %s\n", argc < 2 ? "usage" : err); return 1; }
    if (argc > 2) set_speed(atoi(argv[2]));
    while (!mp_finished()) { mp_pump(); g_now_us += 500; }
    mp_unload();
    return 0;
}
#endif
