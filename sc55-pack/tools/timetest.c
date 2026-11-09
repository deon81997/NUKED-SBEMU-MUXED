/* TIMETEST - checks whether a game's fast timer interrupt loses ticks while SBEMU renders music.
 *
 * Games like Duke Nukem 3D speed the PC timer (IRQ 0) up to several hundred Hz and time their MIDI
 * music from it. If timer ticks get lost while SBEMU is busy in the sound card interrupt, the music
 * (and, less noticeably, the game) runs slow. MIDPLAY is not affected because it reads the clock
 * directly. This tool does what such a game does - fast timer + MIDI to the MPU-401 - and counts the
 * ticks against the CMOS real-time clock.
 *
 *   TIMETEST [/R:hz] [/S:seconds] [/P:port]     default 140 Hz, 5 s per phase, port 330
 *
 * Phase 1: no MIDI.  Phase 2: 24 notes held and re-struck (synth busy).  Writes TIMETEST.TXT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pc.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <crt0.h>
#include "mpuport.h"
#include "toolui.h"

int _crt0_startup_flags = _CRT0_FLAG_LOCK_MEMORY;

static volatile unsigned long g_ticks;
static _go32_dpmi_seginfo g_old, g_new;
static unsigned g_div;
static int g_port = 0x330;
static FILE* g_log;

static volatile unsigned long g_last, g_maxgap, g_big, g_huge, g_thr, g_thr2;
static unsigned long rdtsc_lo(void) { unsigned long lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return lo; }

static void tick_isr(void)
{
    unsigned long t = rdtsc_lo(), d = t - g_last;
    if (g_last)
    {
        if (d > g_maxgap) g_maxgap = d;
        if (g_thr && d > g_thr) ++g_big;
        if (g_thr2 && d > g_thr2) ++g_huge;
    }
    g_last = t;
    ++g_ticks;
    outportb(0x20, 0x20);
}

/* the screen: everything out() writes, in a panel (plain text when redirected) */
static int g_ui, g_phase, g_nphase = 2, g_done, g_hz = 140, g_secs = 5;
static char g_text[8192];
static void draw(void)
{
    static const char* ph[3] = { "Calibrate", "Phase 1: no music", "Phase 2: 24 notes" };
    const char* res = strstr(g_text, "RESULT:");
    int i;
    char b[80];
    tu_head("TIMETEST", "does the game timer lose ticks while SBEMU plays music?", NULL);
    tui_panel(2, 3, 77, 5, 1, 0x1E, "Test");
    snprintf(b, sizeof b, "Timer at %u Hz, %d s per phase", g_hz, g_secs);
    tui_put(4, 4, 0x1F, b);
    for (i = 0; i < 3; ++i)
    {
        int st = g_phase > i || g_done ? 1 : 0;
        if (i == 2 && g_nphase < 2) { tu_state(4 + i * 25, 6, -1, "Phase 2: no MPU-401"); continue; }
        if (g_phase == i && !g_done) tui_printf(4 + i * 25, 6, 0x5F, " %s ", ph[i]);   /* running now */
        else tu_state(4 + i * 25, 6, st, ph[i]);
    }
    tui_panel(2, 8, 77, res ? 11 : 15, 1, 0x1E, "Measurements");
    tu_log(4, 9, 73, res ? 9 : 13, 0x17, g_text);
    if (res)
    {
        tui_panel(2, 19, 77, 4, 1, 0x1E, "Result");
        char r[300]; int k = 0; const char* q = res + 8;
        while (*q == ' ') ++q;
        for (; *q && k < (int)sizeof r - 1 && strncmp(q, "Saved", 5); ++q) if (*q != '\n' || r[k - 1] != ' ') r[k++] = *q == '\n' ? ' ' : *q;
        while (k && r[k - 1] == ' ') --k;
        r[k] = 0;
        { char* d; while ((d = strstr(r, "  ")) != NULL) memmove(d, d + 1, strlen(d)); }
        tui_wrap(4, 20, 73, 2, strstr(res, "no timer ticks lost") ? 0x1A : 0x1C, r);
    }
    tu_foot(g_done ? "Saved to TIMETEST.TXT." : "Measuring - the PC is busy, keys wait until the test ends.",
            g_done ? "Esc quit   F4 screen" : "");
}
static void out(const char* fmt, ...)
{
    va_list ap;
    if (!g_ui) { va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); }
    else
    {
        size_t n = strlen(g_text);
        if (n > sizeof g_text - 1024) { memmove(g_text, g_text + 2048, n - 2048 + 1); n -= 2048; }
        va_start(ap, fmt); vsnprintf(g_text + n, sizeof g_text - n, fmt, ap); va_end(ap);
        draw(); tu_poll();
    }
    if (g_log) { va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap); fflush(g_log); }
}

static int cmos(int reg) { outportb(0x70, reg); return inportb(0x71); }
static int bcd(int v) { return (cmos(0x0B) & 4) ? v : ((v >> 4) * 10 + (v & 15)); }
static int rtc_sec(void)
{
    while (cmos(0x0A) & 0x80) {} /* update in progress */
    return bcd(cmos(0x00));
}
static void wait_sec_edge(void)
{
    int s = rtc_sec();
    while (rtc_sec() == s) {}
}

static void mpu_byte(int b)
{
    unsigned long end = g_ticks + 200; /* bounded wait on "output busy" */
    while ((inportb(g_port + 1) & 0x40) && g_ticks < end) {}
    outportb(g_port, b);
}
static int mpu_init(void)
{
    unsigned long end;
    outportb(g_port + 1, 0xFF);
    end = g_ticks + 500;
    while (g_ticks < end)
        if (!(inportb(g_port + 1) & 0x80)) { inportb(g_port); outportb(g_port + 1, 0x3F); return 1; }
    return 0;
}

static const int chord[6] = { 48, 55, 60, 64, 67, 72 };
static void notes(int on)
{
    int ch, i;
    for (ch = 0; ch < 4; ++ch)
        for (i = 0; i < 6; ++i)
        {
            mpu_byte((on ? 0x90 : 0x80) | ch);
            mpu_byte(chord[i] + ch * 2);
            mpu_byte(on ? 100 : 0);
        }
}

/* returns the ticks counted in 'secs' RTC seconds */
static unsigned long measure(int secs, int midi, unsigned hz)
{
    unsigned long t0, t1, next = 0;
    int s, left = secs;
    wait_sec_edge();
    t0 = g_ticks;
    s = rtc_sec();
    while (left > 0)
    {
        if (midi && g_ticks >= next)
        {
            notes(0); notes(1);
            next = g_ticks + hz / 2; /* re-strike twice a second */
        }
        if (rtc_sec() != s) { s = rtc_sec(); --left; }
    }
    t1 = g_ticks;
    if (midi) notes(0);
    return t1 - t0;
}

static double g_cps; /* CPU cycles per second, measured against the RTC */
static void gap_reset(unsigned hz)
{
    g_maxgap = g_big = g_huge = 0; g_last = 0;
    g_thr = (unsigned long)(g_cps * 3.0 / hz);   /* gaps longer than 3 periods */
    g_thr2 = (unsigned long)(g_cps * 0.02);      /* gaps longer than 20 ms */
}
static void gap_report(void)
{
    out("  longest gap between ticks %.1f ms, gaps >3 periods: %lu, gaps >20 ms: %lu\n",
        g_cps > 0 ? g_maxgap * 1000.0 / g_cps : 0.0, g_big, g_huge);
}

/* ask SBEMU's SC-55 module for its statistics (MPU command 0xA5, new in SBEMU beta.8) */
static void sbemu_stats(void)
{
    unsigned long d[8]; int i, j; unsigned char b[32];
    outportb(g_port + 1, 0xA5); /* the snapshot is taken at once, no waiting needed */
    for (i = 0; i < 32; ++i) b[i] = inportb(g_port);
    for (i = 0; i < 8; ++i) { d[i] = 0; for (j = 3; j >= 0; --j) d[i] = (d[i] << 8) | b[4*i+j]; }
    if (d[0] != 0x35355342UL) { out("  SBEMU statistics: none (this is an older SBEMU than beta.8)\n"); return; }
    out("  SBEMU: speed level %lu, load %lu.%lu%%, render avg %lu / max %lu us/block\n"
        "         block %lu frames at %lu Hz (%lu us), %lu blocks\n",
        d[1], d[2] / 10, d[2] % 10, d[4], d[3], d[5], d[6], d[6] ? d[5] * 1000000UL / d[6] : 0UL, d[7]);
}

static void set_pit(unsigned div)
{
    outportb(0x43, 0x36);
    outportb(0x40, div & 0xFF);
    outportb(0x40, (div >> 8) & 0xFF);
}

static void fix_bios_clock(void)
{
    /* the BIOS clock did not run during the test: set it again from the CMOS clock */
    int h, m, s;
    unsigned long t;
    while (cmos(0x0A) & 0x80) {}
    s = bcd(cmos(0x00)); m = bcd(cmos(0x02)); h = bcd(cmos(0x04) & 0x7F);
    t = (unsigned long)(((h * 60UL + m) * 60UL + s) * 1193182.0 / 65536.0);
    _farpokel(_dos_ds, 0x46C, t);
}

int main(int argc, char** argv)
{
    unsigned hz = 140;
    int secs = 5, i, mpu;
    unsigned long base, busy;
    double expect, lb, lm;

    for (i = 1; i < argc; ++i)
    {
        const char* a = argv[i];
        if ((a[0] == '/' || a[0] == '-') && a[1] && a[2] == ':')
        {
            switch (a[1] | 0x20)
            {
            case 'r': hz = (unsigned)atoi(a + 3); break;
            case 's': secs = atoi(a + 3); break;
            case 'p': g_port = (int)strtol(a + 3, NULL, 16); break;
            }
        }
        else if (a[1] == '?' || (a[1] | 0x20) == 'h')
        {
            printf("TIMETEST [/R:hz] [/S:seconds] [/P:port]   (default /R:140 /S:5 /P:330)\n");
            return 0;
        }
    }
    if (hz < 100) hz = 100;
    if (hz > 4000) hz = 4000;
    if (secs < 3) secs = 3;
    if (secs > 60) secs = 60;
    g_div = (unsigned)(1193182UL / hz);
    expect = 1193182.0 / g_div * secs;

    { int p = mpu_find(g_port); if (p) g_port = p; }   /* SBEMU's MPU-401 on whatever port it uses */
    g_hz = hz; g_secs = secs;
    g_ui = isatty(1);
    if (g_ui) { tu_start(argv[0]); draw(); tu_poll(); }
    g_log = fopen("TIMETEST.TXT", "w");
    out("TIMETEST - timer interrupt at %u Hz, %d s per phase, MPU-401 at %X\n", hz, secs, g_port);
    out("(the game-style test: does IRQ 0 lose ticks while SBEMU plays music?)\n\n");

    _go32_dpmi_get_protected_mode_interrupt_vector(8, &g_old);
    g_new.pm_offset = (unsigned long)tick_isr;
    g_new.pm_selector = _go32_my_cs();
    if (_go32_dpmi_allocate_iret_wrapper(&g_new) != 0) { out("Cannot install the timer handler.\n"); tu_end(); return 1; }
    disable();
    _go32_dpmi_set_protected_mode_interrupt_vector(8, &g_new);
    set_pit(g_div);
    enable();

    mpu = mpu_init();
    if (!mpu) g_nphase = 1;
    if (!mpu) out("No MPU-401 answered at %X: is SBEMU loaded (run SC55 first)? Phase 2 is skipped.\n\n", g_port);
    else
    {   /* GM reset + a piano on 4 channels, then let the synth settle */
        static const unsigned char gm[6] = { 0xF0, 0x7E, 0x7F, 0x09, 0x01, 0xF7 };
        unsigned long w;
        for (i = 0; i < 6; ++i) mpu_byte(gm[i]);
        w = g_ticks + hz; while (g_ticks < w) {}
    }

    {   /* calibrate the TSC against the RTC (1 s) */
        unsigned long long c0, c1;
        wait_sec_edge();
        __asm__ __volatile__("rdtsc" : "=A"(c0));
        wait_sec_edge();
        __asm__ __volatile__("rdtsc" : "=A"(c1));
        g_cps = (double)(c1 - c0);
        out("CPU %.0f MHz (from the RTC)\n\n", g_cps / 1e6);
    }

    lm = lb = 0;
    {
        unsigned rates[2]; int r, nr = 1;
        rates[0] = hz;
        /* default 140 Hz is roughly what Build-engine games use; /R:1000 is a stress test that may freeze the PC */
        for (r = 0; r < nr; ++r)
        {
            unsigned rh = rates[r], dv = (unsigned)(1193182UL / rh);
            double ex = 1193182.0 / dv * secs, l1, l2 = 0;
            disable(); set_pit(dv); enable();
            { unsigned long w = g_ticks + rh / 2; while (g_ticks < w) {} }
            g_phase = 1;
            out("== timer at %u Hz ==\nPhase 1: no music ...\n", rh);
            gap_reset(rh);
            base = measure(secs, 0, rh);
            l1 = 100.0 * (1.0 - base / ex);
            out("  ticks %lu of %.0f expected  -> lost %.1f%%\n", base, ex, l1 < 0 ? 0.0 : l1);
            gap_report();
            if (mpu)
            {
                g_phase = 2;
                out("Phase 2: 24 notes playing ...\n");
                gap_reset(rh);
                busy = measure(secs, 1, rh);
                l2 = 100.0 * (1.0 - busy / ex);
                out("  ticks %lu of %.0f expected  -> lost %.1f%%\n", busy, ex, l2 < 0 ? 0.0 : l2);
                gap_report();
                sbemu_stats();
            }
            if (r == 0) { lb = l1; lm = l2; }
            out("\n");
        }
    }

    disable();
    set_pit(0);
    _go32_dpmi_set_protected_mode_interrupt_vector(8, &g_old);
    enable();
    _go32_dpmi_free_iret_wrapper(&g_new);
    fix_bios_clock();

    out("\n");
    if (mpu && lm > 2.0)
        out("RESULT: timer ticks are LOST while music plays (%.1f%%). Games that time their music from\n"
            "        the timer (Duke 3D, Build games, ...) play the music that much slower.\n", lm);
    else if (lb > 2.0)
        out("RESULT: timer ticks are lost even without music (%.1f%%).\n", lb);
    else
        out("RESULT: no timer ticks lost - the timer is not the cause.\n");
    g_done = 1;
    out("Saved to TIMETEST.TXT\n");
    if (g_log) fclose(g_log);
    if (g_ui) { while (kbhit()) getch(); for (;;) { int c = tu_wait_key(); if (c == 27 || c == 13 || c == 'q' || c == 'Q') break; } tu_end(); }
    return 0;
}
