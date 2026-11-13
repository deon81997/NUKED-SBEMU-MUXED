/* SBDIAG - explains why MIDI music (SC-55 or SoundFont) does not play at full speed in a game.
 *
 *   SBDIAG START    start measuring (SBEMU must be loaded: run SC55 first)
 *   ...play the game for a minute, quit it...
 *   SBDIAG          print the report (also saved to SBDIAG.TXT)
 *   SBDIAG STOP     stop measuring
 *
 * SBEMU (beta.15 or newer) does the measuring itself while the game runs; this program only switches
 * it on and reads the result through the MPU-401 port. It does not touch the timer or the sound card.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pc.h>
#include <dos.h>
#include <unistd.h>
#include "mpuport.h"
#include "toolui.h"
#include "doctor.h"

static int g_port = 0x330;
static FILE* g_log;
static const char* g_file = "SBDIAG.TXT";
static int g_ui;                    /* on the screen: the report as a full screen (plain text when redirected) */
static char g_rep[12000];           /* the report text, for the screen */

static void out(const char* fmt, ...)
{
    va_list ap;
    if (g_ui)
    {
        size_t n = strlen(g_rep);
        va_start(ap, fmt); vsnprintf(g_rep + n, sizeof g_rep - n, fmt, ap); va_end(ap);
    }
    else { va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
    if (!isatty(fileno(stdout))) { fflush(stdout); fsync(fileno(stdout)); } /* redirected to a file (GTFULL.TXT): commit each line */
    if (g_log) { va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap); fflush(g_log); fsync(fileno(g_log)); } /* keep what is written if the PC hangs */
}

static unsigned long long tsc(void)
{
    unsigned long lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

enum { F_MAGIC, F_VER, F_KHZ, F_MS, F_ISRN, F_ISRMS, F_ISRMAX, F_H1, F_H2, F_H5, F_H10, F_STRMAX, F_STRN,
       F_PITDIV, F_PITMAX, F_PITN, F_LOSTM, F_RENN, F_RENMS, F_RENMAX, F_FRAMES, F_FREQ, F_SYNTH,
       F_VIRQN, F_VIRQMS, F_VIRQMAX, F_TMPU, F_TSB, F_TOPL, F_TPIT, F_TPIC, F_TOTH, F_MIDI, F_MIDIDROP,
       F_GOV, F_FX, F_VOICES, F_FRMAX, F_PITFAST, F_EXPM, F_FIFOMIN, F_UNDER, F_REPLAY, F_BEXP, F_BGOT, F_COUNT };

static int read_report(unsigned long* d)
{
    unsigned char b[F_COUNT * 4];
    int i, j;
    outportb(g_port + 1, 0xA6);
    for (i = 0; i < F_COUNT * 4; ++i) b[i] = inportb(g_port);
    for (i = 0; i < F_COUNT; ++i) { d[i] = 0; for (j = 3; j >= 0; --j) d[i] = (d[i] << 8) | b[4 * i + j]; }
    return d[F_MAGIC] == 0x47444253UL;
}

/* cost of one trapped port access, measured on the MPU status port (trapped) vs port 0x80 (not) */
static double trap_cost_us(double cps)
{
    unsigned long long t0, t1, t2;
    int i;
    t0 = tsc();
    for (i = 0; i < 2000; ++i) (void)inportb(g_port + 1);
    t1 = tsc();
    for (i = 0; i < 2000; ++i) (void)inportb(0x80);
    t2 = tsc();
    if (cps <= 0) return 0;
    {
        double a = (double)(t1 - t0) / 2000, b = (double)(t2 - t1) / 2000;
        return (a > b ? a - b : 0) * 1e6 / cps;
    }
}

static int has_sbemu(void)
{
    /* an MPU-401 answers 0xFF (reset) with 0xFE */
    int i;
    outportb(g_port + 1, 0xFF);
    for (i = 0; i < 100000; ++i)
        if (!(inportb(g_port + 1) & 0x80)) return inportb(g_port) == 0xFE;
    return 0;
}

/* a short message: a window on the full screen when shown on the screen, plain text otherwise */
static int say(const char* argv0, const char* title, const char* msg, int rc)
{
    if (!g_ui) { printf("%s\n", msg); return rc; }
    tu_start(argv0);
    tu_head("SBDIAG", "why game music plays too slow", NULL);
    tu_foot(NULL, "any key: back");
    tui_message(title, msg);
    tu_end();
    return rc;
}

/* the report screen: the numbers that matter as bars, the reasons, V for the whole text */
typedef struct { double secs, mhz, hz, lostpct, clock, isr_share, isr_avg, isr_max, run_max, period, load, silence; int synth, reasons; } SUM;
static const char* section(const char* name, char* buf, int size)
{
    const char* a = strstr(g_rep, name), *b;
    int n;
    if (!a) { buf[0] = 0; return buf; }
    a += strlen(name);
    while (*a == '\n') ++a;
    b = strstr(a, "\n\nWHAT WOULD");
    n = b ? (int)(b - a) : (int)strlen(a);
    if (n > size - 1) n = size - 1;
    {   /* the report's lines are broken for 80 columns: join each reason's lines, a new reason on a new line */
        int i, k = 0;
        for (i = 0; i < n && k < size - 1; ++i)
        {
            if (a[i] == '\n')
            {
                int j = i + 1;
                while (j < n && a[j] == ' ') ++j;
                if (j < n && a[j] >= '0' && a[j] <= '9') { buf[k++] = '\n'; i = j - 1; }
                else { buf[k++] = ' '; i = j - 1; }
            }
            else buf[k++] = a[i];
        }
        buf[k] = 0;
    }
    return buf;
}
static void bar_line(int x, int y, const char* label, double v, double full, int bad, const char* text)
{
    int a = bad ? 0x1C : 0x1A;
    tui_put(x, y, 0x17, label);
    tui_bar(x + 22, y, 30, (long)(v * 100), (long)(full * 100), bad ? 0x1C : 0x1A, 0x18);
    tui_put(x + 54, y, a, text);
}
static void show(const char* argv0, const SUM* m)
{
    char t[80], r[4000];
    int c;
    tu_start(argv0);
    for (;;)
    {
        tu_head("SBDIAG", "why game music plays too slow", NULL);
        tui_panel(2, 3, 77, 4, 1, 0x1E, "Measurement");
        snprintf(t, sizeof t, "%.0f s on a %.0f MHz CPU, synth %s", m->secs, m->mhz, m->synth == 2 ? "MT-32" : m->synth ? "SoundFont" : "SC-55");
        tui_put(4, 4, 0x1F, t);
        snprintf(t, sizeof t, m->reasons == 1 ? "1 reason found" : "%d reasons found", m->reasons);
        tu_state(4, 5, m->reasons ? -1 : 1, m->reasons ? t : "no problem found");
        tui_panel(2, 7, 77, 8, 1, 0x1E, "The numbers");
        if (m->hz > 0 && m->hz < 1000)
        {
            snprintf(t, sizeof t, "%.1f%%  (timer %.0f Hz)", m->lostpct, m->hz);
            bar_line(4, 8, "Timer ticks lost", m->lostpct, 10, m->lostpct >= 1, t);
        }
        else tui_put(4, 8, 0x17, "Timer ticks lost      the game left the timer at 18.2 Hz");
        if (m->clock > 0) { snprintf(t, sizeof t, "%.1f%% of real time", m->clock); bar_line(4, 9, "Game clock speed", m->clock, 100, m->clock < 99, t); }
        snprintf(t, sizeof t, "%.0f%% of the CPU", m->isr_share);
        bar_line(4, 10, "Sound interrupt", m->isr_share, 100, m->isr_share >= 60, t);
        snprintf(t, sizeof t, "%.2f ms (tick %.2f)", m->run_max, m->period);
        bar_line(4, 11, "Longest block", m->run_max, m->period > 0 ? m->period * 2 : 10, m->period > 0 && m->run_max > m->period, t);
        snprintf(t, sizeof t, "%.0f%% of real time", m->load);
        bar_line(4, 12, "Synth load", m->load, 100, m->load >= 50, t);
        snprintf(t, sizeof t, "%.2f s", m->silence);
        bar_line(4, 13, "Dropouts (silence)", m->silence, 1, m->silence > 0.05, t);
        tui_panel(2, 15, 77, 8, 1, 0x1E, "Reasons, most important first");
        tu_text(4, 16, 73, 6, 0x17, section("REASONS, MOST IMPORTANT FIRST", r, sizeof r));
        tu_foot(!strcmp(g_file, "SBDIAG.TXT") ? "Saved to SBDIAG.TXT. V shows the whole report with what would fix it." : "A made-up example (SBDIAG DEMO). V shows its whole report.", "V whole report   Esc quit   F4 screen");
        c = tu_wait_key();
        if (c == 27 || c == 'q' || c == 'Q' || c == K_RCLICK) break;
        if (c == 'v' || c == 'V' || c == 13 || c == K_MOUSE) { tui_view_file(g_file, NULL); tui_invalidate(); }
    }
    tu_end();
}

int main(int argc, char** argv)
{
    unsigned long d[F_COUNT];
    const char* cmd = argc > 1 ? argv[1] : "";
    double cps, secs, isr_ms, ren_ms, virq_ms, audio_ms, load, hz = 0, period_ms = 0, lost = 0, expect = 0, lostpct = 0, rawpct = 0, replayed = 0;
    double trapcost, traps, trap_pct, isr_share;
    int reasons = 0;

    if (cmd[0] == '/' || cmd[0] == '-') ++cmd;
    if (!stricmp(cmd, "DOCTOR") || !stricmp(cmd, "FLUSH"))
    {   /* SBEMU writes its flight recorder (SBTRACE.TXT) now (MPU command A9h), then BUGREP's summary */
        int p = mpu_from_blaster(getenv("BLASTER"));                /* SBEMU writes P<port> to BLASTER */
        if (!p) p = 0x330;
        outportb(p + 1, 0xA9); delay(300); mpu_drain(p);             /* (nothing happens without SBEMU) */
        if (!stricmp(cmd, "FLUSH")) { printf("Asked SBEMU (MPU-401 %Xh) to write SBTRACE.TXT.\n", p); return 0; }
        return doctor_main(argc, argv);                              /* the summary at the top of BUGREP.TXT */
    }
    if (!stricmp(cmd, "?") || !stricmp(cmd, "H"))
    {
        printf("SBDIAG START   start measuring (run SC55 first)\n"
               "  ...play the game for a minute, quit it...\n"
               "SBDIAG         show the report (saved to SBDIAG.TXT)\n"
               "SBDIAG STOP    stop measuring\n");
        return 0;
    }
    g_ui = isatty(fileno(stdout));
    if (!stricmp(cmd, "DEMO"))
    {   /* made-up numbers (a 140 Hz game losing ticks on a slow PC), to see the report without a game */
        static const unsigned long demo[F_COUNT] = { 0x47444253UL, 5, 400000, 60000, 6000, 21000, 9200, 900, 420, 60, 0, 11800, 0,
            8522, 8522, 0, 1260000, 6000, 15600, 4100, 1323000, 22050, 0, 3000, 2100, 3100, 2000, 9000, 1000, 400, 300, 200, 90000, 0,
            2, 1, 0, 512, 8522, 8400000, 300, 5000, 0, 1092, 1015 };
        memcpy(d, demo, sizeof d);
        goto report;
    }
    { int p = mpu_find(g_port); if (p) g_port = p; }
    if (!has_sbemu())
    {
        char m[120]; snprintf(m, sizeof m, "No MPU-401 at %X: run SC55 first (SBEMU must be loaded).", g_port);
        return say(argv[0], "SBDIAG", m, 1);
    }
    outportb(g_port + 1, 0x3F); /* back to UART mode */

    if (!stricmp(cmd, "START"))
    {
        outportb(g_port + 1, 0xA7);
        if (!read_report(d))
            return say(argv[0], "SBDIAG", "This SBEMU cannot measure itself: SBDIAG needs SBEMU 1.0.0-beta.16-sc55 or newer (beta.17 for everything).", 1);
        return say(argv[0], "Measuring", "Measuring. Now start the game and play for a minute or two with music on. "
                   "Quit the game, then pick SBDIAG: report (or type SBDIAG) for the result.", 0);
    }
    if (!stricmp(cmd, "STOP")) { outportb(g_port + 1, 0xA8); return say(argv[0], "SBDIAG", "Measuring stopped.", 0); }

    if (!read_report(d))
        return say(argv[0], "SBDIAG", "This SBEMU cannot measure itself: SBDIAG needs SBEMU 1.0.0-beta.16-sc55 or newer (beta.17 for everything).", 1);
report:
    cps = d[F_KHZ] * 1000.0;
    trapcost = 0; /* the in-program measurement was unreliable (always ~0) and touched port 0x80: dropped */
    g_file = stricmp(cmd, "DEMO") ? "SBDIAG.TXT" : "SBDEMO.TXT";
    g_log = fopen(g_file, "w");

    secs = d[F_MS] / 1000.0;
    out("SBDIAG - why the MIDI music is not at full speed\n");
    out("================================================\n");
    if (d[F_ISRN] == 0 || secs < 2)
    {
        out("Nothing measured yet. Run SBDIAG START, play the game with music, quit, then run SBDIAG.\n");
        if (g_log) fclose(g_log);
        if (g_ui) return say(argv[0], "SBDIAG", "Nothing measured yet. Pick SBDIAG: start measuring (SBDIAG START), play the game "
                             "with music for a minute, quit it, then look here again.", 0);
        return 0;
    }
    isr_ms = d[F_ISRMS]; ren_ms = d[F_RENMS]; virq_ms = d[F_VIRQMS];
    audio_ms = d[F_FREQ] ? d[F_FRAMES] * 1000.0 / d[F_FREQ] : 0;
    load = audio_ms > 0 ? 100.0 * ren_ms / audio_ms : 0;
    isr_share = 100.0 * isr_ms / d[F_MS];
    /* the game's fastest timer rate seen while measuring (after quitting, DOS is back at 18.2 Hz) */
    if (d[F_VER] >= 2 && d[F_PITFAST]) d[F_PITDIV] = d[F_PITFAST];
    if (d[F_PITDIV]) { hz = 1193182.0 / d[F_PITDIV]; period_ms = 1000.0 / hz; }
    lost = d[F_LOSTM] / 1000.0;
    expect = d[F_VER] >= 2 ? d[F_EXPM] / 1000.0 : hz * secs; /* ticks due while the game had the timer sped up */
    {
        double given = d[F_VER] >= 4 ? (double)d[F_REPLAY] : 0;
        rawpct = expect > 0 ? 100.0 * lost / expect : 0;
        replayed = given;
        lost = lost > given ? lost - given : 0;          /* what the game really missed */
    }
    lostpct = expect > 0 ? 100.0 * lost / expect : 0;
    if (d[F_VER] >= 5 && d[F_BEXP] > 20) /* real measurement beats the estimate */
        lostpct = d[F_BGOT] >= d[F_BEXP] ? 0 : 100.0 * (d[F_BEXP] - d[F_BGOT]) / d[F_BEXP];
    traps = (double)d[F_TMPU] + d[F_TSB] + d[F_TOPL] + d[F_TPIT] + d[F_TPIC] + d[F_TOTH];
    trap_pct = 100.0 * traps * trapcost / 1e6 / secs;

    out("Measured %.0f s on a %.0f MHz CPU. Synth: %s", secs, cps / 1e6, d[F_SYNTH] == 2 ? "MT-32" : d[F_SYNTH] ? "SoundFont" : "SC-55");
    if (!d[F_SYNTH]) out(" (reverb/chorus %s, voices %s, speed-control level %lu)",
                         d[F_FX] ? "on" : "off", d[F_VOICES] ? "limited" : "all", d[F_GOV]);
    out("\n\n");

    out("THE GAME'S TIMER\n");
    if (!d[F_PITDIV]) out("  not measured (no sound interrupts while armed?)\n");
    else if (d[F_PITDIV] >= 65000) out("  The game left the PC timer at the normal 18.2 Hz (or no game ran while measuring).\n");
    else out("  The game runs the PC timer at about %.0f Hz (a tick every %.2f ms). DOS games time their\n"
             "  MIDI music with it: every lost tick makes the music slower.\n", hz, period_ms);
    out("\nSBEMU'S SOUND INTERRUPT (the timer cannot tick while it runs)\n");
    out("  %lu interrupts (%.0f per second), average %.2f ms, longest %.2f ms\n",
        d[F_ISRN], d[F_ISRN] / secs, isr_ms / d[F_ISRN], d[F_ISRMAX] / 1000.0);
    out("  longer than 1 ms: %lu, 2 ms: %lu, 5 ms: %lu, 10 ms: %lu\n", d[F_H1], d[F_H2], d[F_H5], d[F_H10]);
    out("  longest run of interrupts back to back: %.2f ms\n", d[F_STRMAX] / 1000.0);
    out("  uses %.0f%% of the CPU. Inside it: synth %.0f%%, the game's own sound handler %.0f%%, rest %.0f%%\n",
        isr_share, isr_ms > 0 ? 100.0 * ren_ms / isr_ms : 0, isr_ms > 0 ? 100.0 * virq_ms / isr_ms : 0,
        (isr_ms > 0 && isr_ms > ren_ms + virq_ms) ? 100.0 * (isr_ms - ren_ms - virq_ms) / isr_ms : 0);
    if (d[F_PITDIV] && d[F_PITDIV] < 65000)
    {
        if (replayed > 0)
            out("  -> ticks blocked by SBEMU: %.1f%%; SBEMU gave %.0f of them back to the game (/VTIMER)\n", rawpct, replayed);
        out("  -> timer ticks the game really lost: %.1f%%  (music about %.1f%% slow)\n", lostpct, lostpct);
    }
    if (d[F_VER] >= 5 && d[F_BEXP] > 20)
        out("  -> the DOS clock, which the game's timer drives, ran at %.1f%% of real time while the game ran\n"
            "     (%lu of %lu ticks; this is the real measurement - 100%% means the game got every tick)\n",
            100.0 * d[F_BGOT] / d[F_BEXP], d[F_BGOT], d[F_BEXP]);
    out("\nSYNTH\n");
    out("  %s needs %.0f%% of real time to render (100%% = all of the CPU); longest block %.2f ms\n",
        d[F_SYNTH] == 2 ? "MT-32" : d[F_SYNTH] ? "SoundFont" : "SC-55", load, d[F_RENMAX] / 1000.0);
    out("  output %lu Hz, largest block %lu frames (%.1f ms of sound)\n", d[F_FREQ], d[F_FRMAX],
        d[F_FREQ] ? d[F_FRMAX] * 1000.0 / d[F_FREQ] : 0);
    if (d[F_VER] >= 3 && d[F_FIFOMIN] != 0xFFFFFFFFUL)
        out("  render-ahead buffer: lowest %.1f ms of sound in reserve, %lu frames of silence (%.2f s) because it ran dry\n",
            d[F_FREQ] ? d[F_FIFOMIN] * 1000.0 / d[F_FREQ] : 0, d[F_UNDER], d[F_FREQ] ? (double)d[F_UNDER] / d[F_FREQ] : 0);
    out("\nTHE GAME'S SOUND BLASTER HANDLER (run by SBEMU inside its interrupt)\n");
    out("  %lu calls, average %.2f ms, longest %.2f ms\n", d[F_VIRQN], d[F_VIRQN] ? virq_ms / d[F_VIRQN] : 0, d[F_VIRQMAX] / 1000.0);
    out("\nPORT TRAPS (each game access to an emulated port costs a CPU exception)\n");
    out("  per second: MPU %.0f, SB %.0f, OPL %.0f, timer/speaker %.0f, PIC %.0f, DMA/other %.0f\n",
        d[F_TMPU] / secs, d[F_TSB] / secs, d[F_TOPL] / secs, d[F_TPIT] / secs, d[F_TPIC] / secs, d[F_TOTH] / secs);
    out("\nMIDI\n  %.0f bytes per second from the game, %lu dropped (queue full)\n", d[F_MIDI] / secs, d[F_MIDIDROP]);

    out("\nREASONS, MOST IMPORTANT FIRST\n");
    if (d[F_PITDIV] && d[F_PITDIV] < 65000 && lostpct >= 1.0)
    {
        /* the merged run is the true blocking time; single interrupts are shorter */
        double worst = d[F_STRMAX] / 1000.0;
        ++reasons;
        out("%d. LOST TIMER TICKS (%.1f%%). The game ticks every %.2f ms, but SBEMU's sound interrupt blocks\n"
            "   the timer for up to %.2f ms at a time (interrupts cannot nest under HDPMI). Each block longer\n"
            "   than one tick loses ticks, and the game's music clock falls behind by that much.\n",
            reasons, lostpct, period_ms, worst);
        if (ren_ms >= virq_ms)
            out("   Most of that time is the synth: a lighter synth setting shortens every block.\n");
        else
            out("   Most of that time is the game's own sound-effects handler, which SBEMU has to run\n"
                "   inside its interrupt: fewer sound-effect voices or a lower SB rate in the game's setup helps.\n");
    }
    if (load >= 90)
    {
        ++reasons;
        out("%d. THE SYNTH IS TOO HEAVY FOR THIS CPU (%.0f%% of real time). It cannot keep up, so the sound\n"
            "   interrupt runs almost all the time and everything else waits.\n", reasons, load);
    }
    else if (load >= 50 && lostpct >= 1.0)
    {
        ++reasons;
        out("%d. THE SYNTH IS HEAVY (%.0f%% of real time). Its blocks are long enough to delay the game's timer.\n", reasons, load);
    }
    if (d[F_VER] >= 3 && d[F_UNDER] && d[F_FREQ] && (double)d[F_UNDER] / d[F_FREQ] > 0.05)
    {
        ++reasons;
        out("%d. The synth could not render fast enough within its time budget: %.2f s of music was replaced\n"
            "   by silence (dropouts). A lighter synth setting fixes it.\n", reasons, (double)d[F_UNDER] / d[F_FREQ]);
    }
    if (isr_share >= 60)
    {
        ++reasons;
        out("%d. SBEMU's interrupt takes %.0f%% of the CPU: the game itself is left with %.0f%%.\n", reasons, isr_share, 100 - isr_share);
    }
    if (d[F_VIRQN] && d[F_VIRQMAX] > 2000)
    {
        ++reasons;
        out("%d. The game's sound-effects handler takes up to %.1f ms per call inside SBEMU's interrupt.\n", reasons, d[F_VIRQMAX] / 1000.0);
    }
    if (trap_pct >= 5)
    {
        ++reasons;
        out("%d. Port traps cost %.0f%% of the CPU (the game polls the emulated ports a lot).\n", reasons, trap_pct);
    }
    if (d[F_MIDIDROP])
    {
        ++reasons;
        out("%d. %lu MIDI bytes were dropped because the synth's queue was full: notes went missing.\n", reasons, d[F_MIDIDROP]);
    }
    if (!reasons) out("No problem found in this measurement.\n");

    out("\nWHAT WOULD FIX IT\n");
    if (d[F_PITDIV] && d[F_PITDIV] < 65000)
        out("  Every sound interrupt has to finish in under %.1f ms (one game tick). Now the longest run is %.1f ms.\n",
            period_ms, d[F_STRMAX] / 1000.0);
    out("  Lighter synth: SETTINGS > SC-55 options (reverb/chorus off; note limit off) or a small SoundFont with\n"
        "  fewer voices (/VSFVOICES:24). Lower the SoundFont output rate (SETTINGS) if it is above 22050 Hz.\n");
    out("\nSaved to SBDIAG.TXT\n");
    if (g_log) fclose(g_log);
    if (g_ui)
    {
        SUM m;
        memset(&m, 0, sizeof m);
        m.secs = secs; m.mhz = cps / 1e6; m.synth = (int)d[F_SYNTH]; m.reasons = reasons;
        m.hz = d[F_PITDIV] && d[F_PITDIV] < 65000 ? hz : 0; m.lostpct = lostpct; m.period = period_ms;
        m.clock = d[F_VER] >= 5 && d[F_BEXP] > 20 ? 100.0 * d[F_BGOT] / d[F_BEXP] : 0;
        m.isr_share = isr_share; m.run_max = d[F_STRMAX] / 1000.0; m.load = load;
        m.silence = d[F_VER] >= 3 && d[F_FREQ] ? (double)d[F_UNDER] / d[F_FREQ] : 0;
        show(argv[0], &m);
    }
    return 0;
}
