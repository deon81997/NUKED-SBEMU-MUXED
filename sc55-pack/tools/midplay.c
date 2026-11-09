/* MIDPLAY - plays Standard MIDI Files (.MID, format 0 and 1) through the MPU-401 at
 * port 330h, i.e. through the running SBEMU + SC-55 emulation.
 * SBEMU must already be loaded (run SC55.BAT); this is a protected-mode DJGPP program.
 *
 *   MIDPLAY [options] file.mid [file2.mid ...]      (wildcards work: MIDPLAY *.MID)
 *     /P:330   MPU base port in hex          /L   loop the playlist until ESC
 *     /GM      send GM System On first       /N   send no reset at all
 *              (default is a GS reset, which the SC-55 and SC-55mkII both understand)
 *     /S       plain one-line display (no full-screen player)
 *   Keys: SPACE pause, N next song, P previous song, S SC-55 panel / channel table, - + part, ESC or Q quit.
 *   The full-screen player shows the song, a progress bar, all 16 MIDI channels (instrument,
 *   volume, pan, activity, notes) and a 88-key note visualizer.
 *
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HOSTTEST
/* host build: virtual clock, MIDI bytes are printed with their time instead of sent */
static uint64_t g_now_us;
static void out_byte(unsigned char b) { printf("%llu %02X\n", (unsigned long long)g_now_us, b); }
static uint64_t now_us(void) { return g_now_us; }
static void idle(void) { g_now_us += 500; }
static void sleep_ms(int ms) { g_now_us += (uint64_t)ms * 1000; }
static int key_pressed(void) { return 0; }
static int key_get(void) { return 0; }
#else
#include <pc.h>
#include <conio.h>
#include <time.h>
#include <dos.h>
#include <dir.h>
#include "mpuport.h"
#include "theme.h"
static int base = 0x330;
static uint64_t now_us(void) { return (uint64_t)uclock() * 1000000ULL / UCLOCKS_PER_SEC; }
static void idle(void) {}
static void sleep_ms(int ms) { uint64_t t = now_us() + (uint64_t)ms * 1000; while (now_us() < t) {} }
static void out_byte(unsigned char b)
{
    uint64_t end = now_us() + 1000000;
    while ((inportb(base + 1) & 0x40) && now_us() < end) {} /* DRR busy */
    outportb(base, b);
}
static int key_pressed(void) { return kbhit(); }
static int key_get(void) { return getch(); }
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
        else { st = running; if (!st) return 1; } /* data byte without running status: broken */
        if (st >= 0xF0)
        {
            running = 0;
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

static void send_event(const Song* s, const Event* e)
{
    int i;
    if (e->kind == EV_SHORT)
    {
        out_byte((unsigned char)(e->arg & 0xFF));
        if (e->len > 1) out_byte((unsigned char)((e->arg >> 8) & 0x7F));
        if (e->len > 2) out_byte((unsigned char)((e->arg >> 16) & 0x7F));
    }
    else if (e->kind == EV_SYSEX)
    {
        if (e->lead) out_byte(e->lead);
        for (i = 0; i < e->len; i++) out_byte(s->data[e->arg + (uint32_t)i]);
    }
}

static void send_reset(int mode)
{
    static const unsigned char gs[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
    static const unsigned char gm[] = {0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7};
    int i;
    if (mode == 0) { for (i = 0; i < (int)sizeof gs; i++) out_byte(gs[i]); sleep_ms(250); }
    else if (mode == 1) { for (i = 0; i < (int)sizeof gm; i++) out_byte(gm[i]); sleep_ms(250); }
}

/* ------------------------------------------------------------------ full-screen text UI (DOS only) */
static int g_ui = 0;          /* 1 = full-screen player */
static char** g_pl; static int g_npl, g_cap, g_cur, g_jump;
static int g_rep = 0, g_shuf = 0;     /* repeat: 0 off, 1 all, 2 one */
static const char* base_of(const char* name);

#ifndef HOSTTEST
#define ROWS 25
#define COLS 80
static uint16_t scr[ROWS][COLS], shown[ROWS][COLS];

static void cell(int r, int c, int ch, int at)
{
    if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
    scr[r][c] = (uint16_t)((at << 8) | (ch & 255));
}
static void fillrow(int r, int ch, int at) { int c; for (c = 0; c < COLS; c++) cell(r, c, ch, at); }
static void text(int r, int c, const char* s, int at, int maxw)
{
    int n = 0;
    while (*s && n < maxw) { cell(r, c + n, (unsigned char)*s, at); s++; n++; }
}
static void ui_flush(int force)
{
    int r;
    for (r = 0; r < ROWS; r++)
        if (force || memcmp(scr[r], shown[r], sizeof scr[r]))
        {
            puttext(1, r + 1, COLS, r + 1, scr[r]);
            memcpy(shown[r], scr[r], sizeof scr[r]);
        }
}
static char g_cfg[260];
static void ui_begin(void)
{
    static int loaded;
    if (!loaded)
    {   /* the style, letters and safe mode chosen in MENU (THEME.CFG next to MIDPLAY.EXE) */
        char home[300]; char* e;
        snprintf(home, sizeof home, "%s", g_cfg);
        e = strrchr(home, '\\'); if (!e) e = strrchr(home, '/');
        if (e) *e = 0; else strcpy(home, ".");
        tui_theme_load(home);
        loaded = 1;
    }
    textmode(C80);
    _setcursortype(_NOCURSOR);
    tui_apply_theme();
    tui_font_loaded = 0; tui_font_apply();
    textattr(0x07); clrscr();
    memset(scr, 0, sizeof scr);
    memset(shown, 0xFF, sizeof shown); /* the screen was cleared: repaint every row */
}
static void ui_end(void)
{
    if (!g_ui) return;
    tui_font_restore();
    tui_plain();
    textattr(0x07); clrscr(); _setcursortype(_NORMALCURSOR);
}

static const char* const note_nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const unsigned char chan_col[16] = {9, 10, 11, 12, 13, 14, 2, 3, 4, 15, 5, 6, 1, 7, 10, 12};

static const char* drum_kit(int p)
{
    switch (p)
    {
    case 8: return "Room Kit";        case 16: return "Power Kit";   case 24: return "Electronic Kit";
    case 25: return "TR-808 Kit";     case 32: return "Jazz Kit";    case 40: return "Brush Kit";
    case 48: return "Orchestra Kit";  case 56: return "SFX Kit";     default: return "Standard Kit";
    }
}

static void fmt_time(char* out, unsigned long sec) { sprintf(out, "%lu:%02lu", sec / 60, sec % 60); }

/* ---- SC-55 front panel view (S key): an amber LCD like the real module - the highlighted part's
 * instrument and settings, and the 16 part level bars with peak hold */
static int g_view = 1;          /* 0 channel table, 1 SC-55 panel; kept in MIDPLAY.CFG */
static int g_part;              /* highlighted part 0..15 (- and + keys) */
static char g_cfg[260];
static void view_load(const char* exe)
{
    const char* cut = NULL; const char* p; FILE* f; char line[64];
    for (p = exe; *p; p++) if (*p == '/' || *p == '\\') cut = p;
    snprintf(g_cfg, sizeof g_cfg, "%.*sMIDPLAY.CFG", cut ? (int)(cut - exe + 1) : 0, exe);
    if (!(f = fopen(g_cfg, "r"))) return;
    while (fgets(line, sizeof line, f)) if (!strncmp(line, "VIEW=", 5)) g_view = atoi(line + 5) ? 1 : 0;
    fclose(f);
}
static void view_save(void)
{
    FILE* f = fopen(g_cfg, "w");
    if (f) { fprintf(f, "VIEW=%d\n", g_view); fclose(f); }
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
    if (t < g_lcd_next) return;
    g_lcd_next = t + 100000;             /* 10 times a second: 150 trapped port reads each */
    outportb(base + 1, 0xB0);
    for (i = 0; i < 150; i++) g_lcd[i] = (unsigned char)inportb(base);
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
    int r, c, p = g_part, x;
    lcd_query();
    for (r = 4; r <= 20; r++) fillrow(r, ' ', 0x07);
    /* case */
    for (c = 1; c < COLS - 1; c++) { cell(4, c, 0xDC, 0x08); cell(20, c, 0xDF, 0x08); }
    for (r = 5; r <= 19; r++) { cell(r, 1, 0xDB, 0x08); cell(r, COLS - 2, 0xDB, 0x08); }
    text(5, 4, "Roland", 0x0F, 6);
    text(5, 12, "SOUND Canvas", 0x07, 12);
    text(5, 26, "SC-55", 0x0F, 5);
    text(5, COLS - 26, "GS", 0x0E, 2);
    text(5, COLS - 22, "\xFE POWER", 0x0A, 7);
    text(5, COLS - 13, "MIDI \xFE", g_viz.count[p] ? 0x0C : 0x08, 6);
    /* LCD */
    for (r = 7; r <= 17; r++) for (c = 4; c <= COLS - 5; c++) cell(r, c, ' ', LCD);
    for (c = 4; c <= COLS - 5; c++) { cell(6, c, 0xDC, 0x06); cell(18, c, 0xDF, 0x06); }
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
    text(19, 52, g_lcd_ok && g_lcd[2] == 0 ? "S table view" : "- + part   S table view", 0x08, 26);
}

static void ui_draw(const Song* s, const char* name, uint64_t el_us, uint64_t total_us, int paused, int done)
{
    char b[200], t1[16], t2[16];
    int r, c, n, w;
    const char* base_name = base_of(name);

    /* title bar */
    fillrow(0, ' ', 0x1F);
    text(0, 2, "MIDPLAY", 0x1F, 7);
    text(0, 10, "\xC4 SC-55 MIDI player", 0x1B, 40);
    sprintf(b, "MPU-401 %Xh", base);
    text(0, COLS - 2 - (int)strlen(b), b, 0x1E, 20);

    /* song info */
    fillrow(1, ' ', 0x07);
    text(1, 1, "File", 0x03, 4);
    text(1, 6, base_name, 0x0F, 20);
    if (s->title[0]) { text(1, 28, "Title", 0x03, 5); text(1, 34, s->title, 0x0F, 30); }
    if (paused) text(1, COLS - 12, " PAUSED ", 0x4F, 8);
    else if (done) text(1, COLS - 12, "  DONE  ", 0x2F, 8);

    fillrow(2, ' ', 0x07);
    fmt_time(t1, (unsigned long)(total_us / 1000000ULL));
    sprintf(b, "Format %d \xFA %d trk \xFA %s \xFA %ld BPM", s->format, s->ntrks, t1, g_viz.bpm10 / 10);
    text(2, 1, b, 0x07, 40);
    sprintf(b, "Song %d/%d \xFA Repeat %s \xFA Shuffle %s", g_cur + 1, g_npl, g_rep == 0 ? "off" : g_rep == 1 ? "all" : "one", g_shuf ? "on" : "off");
    text(2, COLS - 1 - (int)strlen(b), b, 0x07, 40);

    /* progress bar */
    fillrow(3, ' ', 0x07);
    {
        unsigned long el = (unsigned long)(el_us / 1000000ULL);
        if (el_us > total_us) el = (unsigned long)(total_us / 1000000ULL);
        fmt_time(t2, el);
        text(3, 1, t2, 0x0F, 6);
        w = 60;
        n = total_us ? (int)((el_us > total_us ? total_us : el_us) * (uint64_t)w / total_us) : 0;
        cell(3, 8, '[', 0x08);
        for (c = 0; c < w; c++) cell(3, 9 + c, c < n ? 0xDB : 0xB0, c < n ? 0x0A : 0x08);
        cell(3, 9 + w, ']', 0x08);
        text(3, 11 + w, t1, 0x0F, 6);
    }

    if (g_view) sc55_panel();
    else {
    /* channel table */
    fillrow(4, ' ', 0x1B);
    text(4, 1, "Ch", 0x1B, 2); text(4, 4, "Instrument", 0x1B, 10); text(4, 27, "Vol", 0x1B, 3);
    text(4, 31, "Pan", 0x1B, 3); text(4, 36, "Activity", 0x1B, 8); text(4, 65, "Note", 0x1B, 4); text(4, 71, "Held", 0x1B, 4);
    for (r = 0; r < 16; r++)
    {
        int row = 5 + r, used = g_viz.last_note[r] >= 0, act = g_viz.count[r] > 0;
        int col = chan_col[r], lv = g_viz.level[r], pan = g_viz.pan[r] - 64, full;
        int namecol = used ? col : 0x08;
        fillrow(row, ' ', 0x07);
        sprintf(b, "%2d", r + 1);
        text(row, 1, b, used ? 0x0F : 0x08, 2);
        text(row, 4, r == 9 ? drum_kit(g_viz.prog[r]) : gm_names[g_viz.prog[r] & 127], namecol, 22);
        sprintf(b, "%3d", g_viz.vol[r]);
        text(row, 27, b, used ? 0x07 : 0x08, 3);
        if (pan >= -2 && pan <= 2) strcpy(b, "  C"); else sprintf(b, "%c%2d", pan < 0 ? 'L' : 'R', (pan < 0 ? -pan : pan) > 63 ? 63 : (pan < 0 ? -pan : pan));
        text(row, 31, b, used ? 0x07 : 0x08, 3);
        full = lv * 28 / 127; if (lv > 0 && full == 0) full = 1;
        for (c = 0; c < 28; c++)
        {
            if (c < full) cell(row, 36 + c, 0xDB, c < 17 ? 0x0A : c < 24 ? 0x0E : 0x0C);
            else cell(row, 36 + c, 0xFA, 0x08);
        }
        if (used)
        {
            int nt = g_viz.last_note[r];
            sprintf(b, "%s%d", note_nm[nt % 12], nt / 12 - 1);
            text(row, 65, b, act ? 0x0F : 0x07, 4);
        }
        if (act) { sprintf(b, "%2d", g_viz.count[r]); text(row, 72, b, 0x0E, 2); }
    }
    }

    /* note visualizer: 80 columns = notes 24..103 (C1..G7), 2 rows tall, half-block resolution */
    fillrow(21, ' ', 0x1B);
    text(21, 1, "Notes", 0x1B, 5);
    for (c = 0; c < COLS; c += 12)
    {
        int nt = 24 + c;
        sprintf(b, "%s%d", note_nm[nt % 12], nt / 12 - 1);
        if (c) text(21, c, b, 0x1B, 3);
    }
    for (c = 0; c < COLS; c++)
    {
        int nt = 24 + c, lv = g_viz.nl[nt], h = lv > 0 ? 1 + lv * 3 / 127 : 0, col = chan_col[g_viz.ncol[nt] & 15];
        int blk = ((nt % 12) == 1 || (nt % 12) == 3 || (nt % 12) == 6 || (nt % 12) == 8 || (nt % 12) == 10);
        int top = h >= 4 ? 0xDB : h == 3 ? 0xDC : ' ';
        int bot = h >= 2 ? 0xDB : h == 1 ? 0xDC : (blk ? ' ' : 0xFA);
        cell(22, c, top, top == ' ' ? 0x07 : col);
        cell(23, c, bot, h ? col : 0x08);
    }

    /* key help */
    fillrow(24, ' ', 0x70);
    text(24, 1, "SPACE pause  N/P song  \x1B\x1A seek  TAB list  B files  R repeat  H shuffle  S view  ESC quit", 0x70, COLS - 2);
    ui_flush(0);
}

static uint64_t g_last_draw;
static void ui_tick(const Song* s, const char* name, uint64_t el_us, uint64_t total, int paused, int done)
{
    uint64_t t;
    if (!g_ui) return;
    t = now_us();
    if (t - g_last_draw < 40000 && t >= g_last_draw) return;
    g_last_draw = t;
    viz_decay();
    ui_draw(s, name, el_us, total, paused, done);
}

/* ---- keys: normal keys as is, extended keys as 256 + scan code */
enum { K_HOME = 256 + 71, K_UP = 256 + 72, K_PGUP = 256 + 73, K_LEFT = 256 + 75, K_RIGHT = 256 + 77,
       K_END = 256 + 79, K_DOWN = 256 + 80, K_PGDN = 256 + 81, K_DEL = 256 + 83 };
static int get_key(void)
{
    int k = key_get();
    if (k == 0 || k == 224) return 256 + key_get();
    return k;
}

/* ---- playlist */
static void pl_add(const char* p)
{
    if (g_npl == g_cap)
    {
        char** n = (char**)realloc(g_pl, (size_t)(g_cap ? g_cap * 2 : 64) * sizeof(char*));
        if (!n) return;
        g_pl = n; g_cap = g_cap ? g_cap * 2 : 64;
    }
    g_pl[g_npl] = (char*)malloc(strlen(p) + 1);
    if (g_pl[g_npl]) { strcpy(g_pl[g_npl], p); g_npl++; }
}
static void pl_remove(int i)
{
    if (i < 0 || i >= g_npl || i == g_cur) return;
    free(g_pl[i]);
    memmove(&g_pl[i], &g_pl[i + 1], (size_t)(g_npl - i - 1) * sizeof(char*));
    g_npl--;
    if (i < g_cur) g_cur--;
}
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
static int cmp_str(const void* a, const void* b) { return stricmp(*(char* const*)a, *(char* const*)b); }

/* adds every MIDI file in dir to the playlist (sorted); returns how many */
static int scan_dir(const char* dir)
{
    DIR* d = opendir(dir);
    struct dirent* de;
    char** names = NULL; int n = 0, cap = 0, i;
    char path[300];
    size_t dl = strlen(dir);
    if (!d) return 0;
    while ((de = readdir(d)) != NULL)
    {
        struct stat st;
        if (!is_midi_name(de->d_name)) continue;
        sprintf(path, "%s%s%s", dir, (dl && dir[dl - 1] != '/' && dir[dl - 1] != '\\' && dir[dl - 1] != ':') ? "/" : "", de->d_name);
        if (stat(path, &st) || S_ISDIR(st.st_mode)) continue;
        if (n == cap) { char** t = (char**)realloc(names, (size_t)(cap ? cap * 2 : 64) * sizeof(char*)); if (!t) break; names = t; cap = cap ? cap * 2 : 64; }
        names[n] = (char*)malloc(strlen(path) + 1);
        if (names[n]) strcpy(names[n++], path);
    }
    closedir(d);
    if (n > 1) qsort(names, (size_t)n, sizeof(char*), cmp_str);
    for (i = 0; i < n; i++) { pl_add(names[i]); free(names[i]); }
    free(names);
    return n;
}

/* ---- list screens (file browser and playlist) share a small helper */
#define LIST_TOP 3
#define LIST_ROWS 20
static int list_nav(int k, int* sel, int* top, int n)
{
    int s = *sel;
    if (k == K_UP) s--; else if (k == K_DOWN) s++;
    else if (k == K_PGUP) s -= LIST_ROWS - 1; else if (k == K_PGDN) s += LIST_ROWS - 1;
    else if (k == K_HOME) s = 0; else if (k == K_END) s = n - 1;
    else return 0;
    if (s >= n) s = n - 1;
    if (s < 0) s = 0;
    *sel = s;
    if (s < *top) *top = s;
    if (s >= *top + LIST_ROWS) *top = s - LIST_ROWS + 1;
    return 1;
}
static void list_frame(const char* title, const char* sub, const char* help)
{
    int r;
    for (r = 0; r < ROWS; r++) fillrow(r, ' ', 0x07);
    fillrow(0, ' ', 0x1F);
    text(0, 2, "MIDPLAY", 0x1F, 7);
    text(0, 10, title, 0x1B, 60);
    text(1, 1, sub, 0x0F, COLS - 2);
    fillrow(2, ' ', 0x1B);
    fillrow(24, ' ', 0x70);
    text(24, 1, help, 0x70, COLS - 2);
}

/* playlist screen: returns the index to play, or -1 */
static int playlist_view(void)
{
    int sel = g_cur, top = 0, r, k;
    char b[200];
    list_nav(K_END, &sel, &top, g_npl); sel = g_cur; if (sel < top) top = sel; if (sel >= top + LIST_ROWS) top = sel - LIST_ROWS + 1;
    for (;;)
    {
        sprintf(b, "Playlist \xFA %d song%s \xFA repeat %s \xFA shuffle %s", g_npl, g_npl == 1 ? "" : "s",
                g_rep == 0 ? "off" : g_rep == 1 ? "all" : "one", g_shuf ? "on" : "off");
        list_frame("\xC4 Playlist", b, "ENTER play   DEL remove   R repeat   H shuffle   TAB/ESC back");
        text(2, 1, " No  File", 0x1B, 20);
        for (r = 0; r < LIST_ROWS && top + r < g_npl; r++)
        {
            int i = top + r, at = i == sel ? 0x70 : (i == g_cur ? 0x0E : 0x07);
            int c;
            if (i == sel) for (c = 0; c < COLS; c++) cell(LIST_TOP + r, c, ' ', 0x70);
            sprintf(b, "%3d", i + 1);
            text(LIST_TOP + r, 1, b, at, 3);
            if (i == g_cur) cell(LIST_TOP + r, 5, 0x10, at);
            text(LIST_TOP + r, 7, base_of(g_pl[i]), at, 40);
            text(LIST_TOP + r, 49, g_pl[i], i == sel ? 0x70 : 0x08, 30);
        }
        ui_flush(0);
        k = get_key();
        if (list_nav(k, &sel, &top, g_npl)) continue;
        if (k == 27 || k == 9) return -1;
        if (k == 13) return sel;
        if (k == K_DEL) { pl_remove(sel); if (sel >= g_npl) sel = g_npl - 1; }
        if (k == 'r' || k == 'R') g_rep = (g_rep + 1) % 3;
        if (k == 'h' || k == 'H') g_shuf = !g_shuf;
    }
}

/* ---- file browser */
typedef struct { char name[80]; int dir; long size; int mark; } Ent;
static int ent_cmp(const void* a, const void* b)
{
    const Ent* x = (const Ent*)a, *y = (const Ent*)b;
    if (x->dir != y->dir) return y->dir - x->dir;
    return stricmp(x->name, y->name);
}
static Ent* load_dir(int* n)
{
    DIR* d = opendir(".");
    struct dirent* de;
    Ent* e = NULL; int cnt = 0, cap = 0;
    *n = 0;
    if (!d) return NULL;
    while ((de = readdir(d)) != NULL)
    {
        struct stat st;
        int isd;
        if (!strcmp(de->d_name, ".")) continue;
        if (stat(de->d_name, &st)) continue;
        isd = S_ISDIR(st.st_mode) ? 1 : 0;
        if (!isd && !is_midi_name(de->d_name)) continue;
        if (cnt == cap) { Ent* t = (Ent*)realloc(e, (size_t)(cap ? cap * 2 : 64) * sizeof(Ent)); if (!t) break; e = t; cap = cap ? cap * 2 : 64; }
        strncpy(e[cnt].name, de->d_name, sizeof e[cnt].name - 1); e[cnt].name[sizeof e[cnt].name - 1] = 0;
        e[cnt].dir = isd; e[cnt].size = isd ? 0 : (long)st.st_size; e[cnt].mark = 0;
        cnt++;
    }
    closedir(d);
    if (cnt > 1) qsort(e, (size_t)cnt, sizeof(Ent), ent_cmp);
    *n = cnt;
    return e;
}
static void add_cwd_file(const char* name)
{
    char cwd[260], path[340];
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    sprintf(path, "%s%s%s", cwd, (cwd[0] && cwd[strlen(cwd) - 1] != '/' && cwd[strlen(cwd) - 1] != '\\') ? "/" : "", name);
    pl_add(path);
}

/* file explorer: folders, drives (D), multi-select (SPACE), add folder (A). Adds files to the playlist;
 * returns how many were added (the first new one is at index g_npl - added). */
static int browse_midi(void)
{
    int n = 0, sel = 0, top = 0, added = 0, i, r, k, nd;
    Ent* e = load_dir(&n);
    char cwd[260], b[200], keep[260];
    int keep_disk = getdisk();
    if (!getcwd(keep, sizeof keep)) keep[0] = 0;
    for (;;)
    {
        int marked = 0;
        for (i = 0; i < n; i++) marked += e[i].mark;
        if (!getcwd(cwd, sizeof cwd)) strcpy(cwd, "?");
        list_frame("\xC4 Select MIDI files", cwd, "ENTER open/add  SPACE mark  A add folder  BKSP up  D drive  ESC done");
        text(2, 1, "Name", 0x1B, 4); text(2, 40, "Size", 0x1B, 4);
        for (r = 0; r < LIST_ROWS && top + r < n; r++)
        {
            Ent* x = &e[top + r];
            int ii = top + r, at = ii == sel ? 0x70 : (x->dir ? 0x0B : 0x0F), c;
            if (ii == sel) for (c = 0; c < COLS; c++) cell(LIST_TOP + r, c, ' ', 0x70);
            if (x->mark) cell(LIST_TOP + r, 1, 0xFB, ii == sel ? 0x74 : 0x0E);
            if (x->dir) { sprintf(b, "[%s]", x->name); text(LIST_TOP + r, 3, b, at, 36); }
            else
            {
                text(LIST_TOP + r, 3, x->name, at, 36);
                sprintf(b, "%ld KB", (x->size + 1023) / 1024);
                text(LIST_TOP + r, 40, b, ii == sel ? 0x70 : 0x07, 12);
            }
        }
        if (!n) text(LIST_TOP + 1, 3, "(no folders or MIDI files here)", 0x08, 40);
        sprintf(b, "%d marked   %d added to playlist", marked, added);
        text(23, 1, b, 0x0E, 60);
        ui_flush(0);
        k = get_key();
        if (list_nav(k, &sel, &top, n)) continue;
        if (k == 27) break;
        if (k == ' ' && n && !e[sel].dir) { e[sel].mark = !e[sel].mark; if (sel < n - 1) list_nav(K_DOWN, &sel, &top, n); }
        else if (k == 'a' || k == 'A')
        {
            for (i = 0; i < n; i++) if (!e[i].dir) { add_cwd_file(e[i].name); added++; }
        }
        else if (k == 8 || (k == 13 && n && e[sel].dir && !strcmp(e[sel].name, "..")))
        {
            if (!chdir("..")) { free(e); e = load_dir(&n); sel = top = 0; }
        }
        else if (k == 13 && n)
        {
            if (e[sel].dir)
            {
                if (!chdir(e[sel].name)) { free(e); e = load_dir(&n); sel = top = 0; }
            }
            else
            {
                if (marked) { for (i = 0; i < n; i++) if (e[i].mark) { add_cwd_file(e[i].name); added++; e[i].mark = 0; } }
                else { add_cwd_file(e[sel].name); added++; }
                break;
            }
        }
        else if (k == 'd' || k == 'D')
        {
            nd = setdisk(getdisk());
            for (i = 1; i <= 26; i++)
            {
                int dsk = (getdisk() + i) % 26;
                DIR* t;
                if (dsk < 2 || dsk >= nd) continue;           /* skip A: and B: (floppy prompts) */
                setdisk(dsk);
                t = opendir(".");
                if (t) { closedir(t); break; }
            }
            free(e); e = load_dir(&n); sel = top = 0;
        }
    }
    free(e);
    setdisk(keep_disk);
    if (keep[0]) chdir(keep);
    return added;
}

/* ---- seek: silence, reset controllers, replay everything except notes up to time t */
static struct
{
    const Song* s; long i; uint64_t start, total, p0; int paused, skip;
} P;

static void seek_to(uint64_t t)
{
    long k;
    int ch;
    const Song* s = P.s;
    if (t >= P.total) t = P.total > 1000 ? P.total - 1000 : 0;
    silence();
    for (ch = 0; ch < 16; ch++) { out_byte((unsigned char)(0xB0 | ch)); out_byte(121); out_byte(0); }
    viz_reset();
    for (k = 0; k < s->nev && s->ev[k].us < t; k++)
    {
        const Event* e = &s->ev[k];
        if (e->kind == EV_SHORT && ((e->arg & 0xF0) == 0x80 || (e->arg & 0xF0) == 0x90)) continue;
        send_event(s, e);
        viz_event(e);
    }
    P.i = k;
    P.skip = 1;
    if (P.paused) { P.p0 = now_us(); P.start = P.p0 - t; }
    else P.start = now_us() - t;
}
static uint64_t elapsed_us(void) { return P.paused ? P.p0 - P.start : now_us() - P.start; }
static void seek_rel(long delta_s)
{
    long long t = (long long)elapsed_us() + (long long)delta_s * 1000000LL;
    if (t < 0) t = 0;
    seek_to((uint64_t)t);
}

/* runs a modal screen without losing the song position */
static void modal_begin(void) { silence(); P.p0 = now_us(); }
static void modal_end(void)
{
    if (!P.paused) P.start += now_us() - P.p0;
    g_last_draw = 0;
#ifndef HOSTTEST
    memset(shown, 0xFF, sizeof shown); /* the list screen drew over everything: repaint every row */
#endif
}

/* handles one key; returns 0 to carry on or a play() result (1 next, 2 quit, 3 prev, 4 jump) */
static int do_key(int k, int in_tail)
{
    switch (k)
    {
    case 27: case 'q': case 'Q': return 2;
    case 'n': case 'N': case K_END: return 1;
    case 'p': case 'P': return 3;
    case ' ':
        if (in_tail) break;
        if (!P.paused) { P.paused = 1; P.p0 = now_us(); silence(); }
        else { P.paused = 0; P.start += now_us() - P.p0; }
        break;
    case K_LEFT: seek_rel(-10); break;
    case K_RIGHT: seek_rel(10); break;
    case K_PGUP: seek_rel(-60); break;
    case K_PGDN: seek_rel(60); break;
    case K_HOME: seek_to(0); break;
    case 'r': case 'R': g_rep = (g_rep + 1) % 3; break;
    case 'l': case 'L': g_rep = g_rep ? 0 : 1; break;
    case 'h': case 'H': g_shuf = !g_shuf; break;
#ifndef HOSTTEST
    case 's': case 'S': if (g_ui) { g_view = !g_view; view_save(); g_last_draw = 0; } break;
    case '-': case '_': case ',': g_part = (g_part + 15) % 16; g_last_draw = 0; break;
    case '+': case '=': case '.': g_part = (g_part + 1) % 16; g_last_draw = 0; break;
#endif
    case 9:
        if (!g_ui) break;
        {
            int j;
            modal_begin(); j = playlist_view(); modal_end();
            if (j >= 0) { g_jump = j; return 4; }
        }
        break;
    case 'b': case 'B':
        if (!g_ui) break;
        {
            int a;
            modal_begin(); a = browse_midi(); modal_end();
            if (a > 0) { g_jump = g_npl - a; return 4; }
        }
        break;
    default: break;
    }
    return 0;
}

/* returns: 0 finished, 1 next song requested, 2 quit, 3 previous song, 4 jump to g_jump */
static int play(const Song* s, const char* name, int reset_mode)
{
    uint64_t last_show = 0, t_end;
    int ret = 0, act;
    viz_reset();
    send_reset(reset_mode);
    memset(&P, 0, sizeof P);
    P.s = s; P.total = s->ev[s->nev - 1].us;
    if (g_ui) { ui_begin(); g_last_draw = 0; ui_tick(s, name, 0, P.total, 0, 0); }
    else
        printf("%s  (format %d, %d track%s, %lu:%02lu)\n", name, s->format, s->ntrks, s->ntrks == 1 ? "" : "s",
               (unsigned long)(P.total / 60000000ULL), (unsigned long)((P.total / 1000000ULL) % 60));
    P.start = now_us();
again:
    while (P.i < s->nev)
    {
        const Event* e = &s->ev[P.i];
        P.skip = 0;
        while (!P.skip && (P.paused || now_us() < P.start + e->us))
        {
            idle();
            if (key_pressed())
            {
                act = do_key(get_key(), 0);
                if (act) { ret = act; goto out; }
                if (P.skip) break;
            }
            if (g_ui) ui_tick(s, name, elapsed_us(), P.total, P.paused, 0);
            else if (!P.paused)
            {
                uint64_t t = now_us();
                if (t - last_show > 250000 || t < last_show)
                {
                    uint64_t el = (t - P.start) / 1000000ULL;
                    printf("\r  %lu:%02lu  ", (unsigned long)(el / 60), (unsigned long)(el % 60));
                    fflush(stdout);
                    last_show = t;
                }
            }
        }
        if (P.skip) continue;
        send_event(s, e);
        viz_event(e);
        P.i++;
    }
    /* let the last notes ring out while the display settles */
    t_end = now_us() + 1200000;
    while (now_us() < t_end)
    {
        idle();
        ui_tick(s, name, P.total, P.total, 0, 1);
        if (key_pressed())
        {
            act = do_key(get_key(), 1);
            if (act) { ret = act; goto out; }
            if (P.skip && P.i < s->nev) goto again;
        }
    }
out:
    silence();
    if (!g_ui) printf(ret == 0 ? "\r  done        \n" : "\n");
    return ret;
}

#else /* HOSTTEST */
static void ui_end(void) {}
static void ui_begin(void) {}
static int play(const Song* s, const char* name, int reset_mode)
{
    long i;
    (void)name;
    viz_reset();
    send_reset(reset_mode);
    {
        uint64_t start = now_us();
        for (i = 0; i < s->nev; i++)
        {
            while (now_us() < start + s->ev[i].us) idle();
            send_event(s, &s->ev[i]);
            viz_event(&s->ev[i]);
        }
    }
    silence();
    return 0;
}
#endif

#ifdef HOSTTEST
int main(int argc, char** argv)
{
    Song s; char err[64];
    if (argc < 2 || !song_load(&s, argv[1], err, sizeof err)) { fprintf(stderr, "load failed: %s\n", argc < 2 ? "usage" : err); return 1; }
    play(&s, argv[1], 2);
    song_free(&s);
    return 0;
}
#else
static int next_idx(int idx, int finished)
{
    if (g_rep == 2 && finished) return idx;
    if (g_shuf && g_npl > 1) { int j; do j = rand() % g_npl; while (j == idx); return j; }
    return idx + 1;
}

int main(int argc, char** argv)
{
    int i, reset_mode = 0, quit = 0, r, plain = 0, want_browse = 0, plays = 0;
    char exedir[260], scandir_arg[260];
    scandir_arg[0] = 0;
    view_load(argv[0]);
    for (i = 1; i < argc; i++)
    {
        if (!strncmp(argv[i], "/P:", 3) || !strncmp(argv[i], "/p:", 3)) base = (int)strtol(argv[i] + 3, NULL, 16);
        else if (!strncmp(argv[i], "/D:", 3) || !strncmp(argv[i], "/d:", 3)) strncpy(scandir_arg, argv[i] + 3, sizeof scandir_arg - 1);
        else if (!strcmp(argv[i], "/L") || !strcmp(argv[i], "/l")) g_rep = 1;
        else if (!strcmp(argv[i], "/R1") || !strcmp(argv[i], "/r1")) g_rep = 2;
        else if (!strcmp(argv[i], "/H") || !strcmp(argv[i], "/h")) g_shuf = 1;
        else if (!strcmp(argv[i], "/B") || !strcmp(argv[i], "/b")) want_browse = 1;
        else if (!strcmp(argv[i], "/S") || !strcmp(argv[i], "/s")) plain = 1;
        else if (!strcmp(argv[i], "/GM") || !strcmp(argv[i], "/gm")) reset_mode = 1;
        else if (!strcmp(argv[i], "/N") || !strcmp(argv[i], "/n")) reset_mode = 2;
        else if (argv[i][0] == '/') goto usage;
        else pl_add(argv[i]);
    }
    if (0)
    {
usage:
        puts("Plays MIDI files through the MPU-401 at port 330h (SBEMU + SC-55 must be loaded: run SC55.BAT).\n"
             "  no files   plays every MIDI file in the folder MIDPLAY.EXE is in\n"
             "  /D:dir     play every MIDI file in that folder instead\n"
             "  /B         open the file explorer to pick files (also: B key while playing)\n"
             "  /L repeat all   /R1 repeat one   /H shuffle   /S plain one-line display\n"
             "  /GM send GM On instead of a GS reset   /N no reset   /P:330 MPU port (hex)\n"
             "Keys: SPACE pause, N/P next/previous, LEFT/RIGHT seek 10 s, PGUP/PGDN seek 60 s,\n"
             "      HOME restart song, R repeat off/all/one, H shuffle, TAB playlist, B file explorer,\n"
             "      S SC-55 front panel / channel table, - + choose the part shown, ESC quit.");
        return 2;
    }
    { int p = mpu_find(base); if (p) base = p; }   /* SBEMU's MPU-401 on whatever port it uses */
    printf("MPU-401 at port %Xh: ", base);
    if (!mpu_detect())
    {
        puts("no answer.\nIs SBEMU loaded (SC55.BAT) with the SC-55 enabled? Run SC55.BAT first, in the same DOS session.");
        return 1;
    }
    puts("found.");
    mpu_uart();
    g_ui = !plain;
    if (scandir_arg[0]) scan_dir(scandir_arg);
    if (!g_npl && !want_browse)
    {
        char* a0 = argv[0];
        const char* cut = NULL; const char* p;
        for (p = a0; *p; p++) if (*p == '/' || *p == '\\') cut = p;
        if (cut) { size_t l = (size_t)(cut - a0); if (l >= sizeof exedir) l = sizeof exedir - 1; memcpy(exedir, a0, l); exedir[l] = 0; if (!l) strcpy(exedir, "/"); }
        else strcpy(exedir, ".");
        if (!scan_dir(exedir)) scan_dir(".");
        if (!g_npl) want_browse = 1;
    }
    if (want_browse)
    {
        if (!g_ui) { puts("No MIDI files found. Give file names, or run without /S to use the file explorer."); return 1; }
        ui_begin();
        browse_midi();
        ui_end();
        if (!g_npl) { puts("No files selected."); return 0; }
    }
    g_cur = 0;
    while (!quit)
    {
        Song sg; char err[64];
        if (g_cur >= g_npl) { if (g_rep == 1 && g_npl > 0) g_cur = 0; else break; }
        if (!song_load(&sg, g_pl[g_cur], err, sizeof err)) { printf("%s: %s\n", g_pl[g_cur], err); g_cur++; continue; }
        r = play(&sg, g_pl[g_cur], reset_mode);
        song_free(&sg);
        if (r == 2) quit = 1;
        else if (r == 3) g_cur = g_cur > 0 ? g_cur - 1 : (g_rep == 1 ? g_npl - 1 : 0);
        else if (r == 4) g_cur = g_jump;
        else
        {
            if (g_shuf && g_rep == 0 && ++plays >= g_npl && r == 0) break;
            g_cur = next_idx(g_cur, r == 0);
        }
    }
    ui_end();
    silence();
    return 0;
}
#endif
