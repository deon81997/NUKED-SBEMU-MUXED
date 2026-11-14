/* MPUTEST - plays a short test tune through the MPU-401 at port 330h, i.e. through
 * the running SBEMU + SC-55 emulation. SBEMU must already be loaded (run SC55.BAT).
 *
 *   MPUTEST [/P:330] [/L] [/USB]  /P: MPU base port in hex   /L loop until a key is pressed
 *   beta.121: with SBEMU /MUSB the USB MIDI device is shown (AMIS 05h) and checked after the tune (sent / dropped /
 *   errors); /USB adds a running-status, real-time-inside-a-message and split-SysEx check. Exit 3 = MIDI lost.
 *
 * GPL-2.0-or-later. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pc.h>
#include <conio.h>
#include <time.h>
#include <dos.h>
#include "mpuport.h"
#include "toolui.h"
#include "sbemuchk.h"

static int base = 0x330;
static int g_ui;                    /* full-screen view (on the screen; plain text when redirected) */
static int g_step = -1, g_found, g_stop;
static SBMUSB g_mu0, g_mu; static int g_usb, g_usbx;   /* /MUSB state before / after, SBEMU has /MUSB on, /USB given */
static unsigned char g_on[128];     /* notes sounding (melodic channels) */
static unsigned char g_drum[128];   /* drum hits, fading */
static void draw(void);

static void wait_ms(int ms)
{
    uclock_t end = uclock() + (uclock_t)ms * UCLOCKS_PER_SEC / 1000;
    while (uclock() < end && !g_stop)
        if (g_ui)
        {
            int c;
            draw();
            c = tu_poll();
            if (c == 27) g_stop = 1;
        }
}

static void put(unsigned char b)
{
    /* wait while the interface is busy (DRR, status bit 6), with a timeout */
    uclock_t end = uclock() + UCLOCKS_PER_SEC;
    while ((inportb(base + 1) & 0x40) && uclock() < end) {}
    outportb(base, b);
}

static void msg3(int s, int a, int b)
{
    put((unsigned char)s); put((unsigned char)a); put((unsigned char)b);
    if ((s & 0xF0) == 0x90 && b) { if ((s & 15) == 9) g_drum[a & 127] = 4; else g_on[a & 127] |= 1 << (s & 3); }
    else if ((s & 0xF0) == 0x80 || (s & 0xF0) == 0x90) g_on[a & 127] &= ~(1 << (s & 3));
}
static void msg2(int s, int a) { put((unsigned char)s); put((unsigned char)a); }

static int detect(void)
{
    uclock_t end;
    outportb(base + 1, 0xFF);                       /* reset */
    end = uclock() + UCLOCKS_PER_SEC / 2;
    while (uclock() < end)
        if (!(inportb(base + 1) & 0x80))            /* DSR low: a byte is waiting */
            return inportb(base) == 0xFE;
    return 0;
}

static void tune(void)
{
    static const unsigned char scale[] = {60, 62, 64, 65, 67, 69, 71, 72};
    static const unsigned char gs_reset[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41, 0xF7};
    int i, n;
    g_step = 0;
    for (i = 0; i < (int)sizeof gs_reset; i++) put(gs_reset[i]);
    wait_ms(300);
    msg2(0xC0, 0);   /* piano */
    msg2(0xC1, 48);  /* strings */
    msg2(0xC2, 33);  /* finger bass */
    g_step = 1; if (!g_ui) printf("  piano scale\n");
    for (n = 0; n < 8; n++)
    {
        msg3(0x90, scale[n], 100);
        wait_ms(260);
        msg3(0x80, scale[n], 0);
        wait_ms(60);
    }
    g_step = 2; if (!g_ui) printf("  chord with strings and bass\n");
    msg3(0x90, 60, 96); msg3(0x90, 64, 96); msg3(0x90, 67, 96); msg3(0x90, 72, 96);
    msg3(0x91, 48, 80); msg3(0x91, 55, 80);
    msg3(0x92, 36, 110);
    wait_ms(1800);
    for (n = 0; n < 4; n++) { static const unsigned char c[] = {60, 64, 67, 72}; msg3(0x80, c[n], 0); }
    msg3(0x81, 48, 0); msg3(0x81, 55, 0); msg3(0x82, 36, 0);
    g_step = 3; if (!g_ui) printf("  drums\n");
    for (n = 0; n < 8; n++)
    {
        msg3(0x99, 42, 90);
        if (n % 4 == 0) msg3(0x99, 36, 120);
        if (n % 4 == 2) msg3(0x99, 38, 110);
        wait_ms(300);
    }
    wait_ms(800);
    msg3(0xB0, 123, 0); msg3(0xB1, 123, 0); msg3(0xB2, 123, 0); msg3(0xB9, 123, 0); /* all notes off */
    memset(g_on, 0, sizeof g_on);
    if (g_usbx)
    {   /* /USB: the byte forms games use that a USB MIDI cable must repack: running status, a clock byte inside a
         * message and inside a SysEx, a SysEx written in pieces (GS "display" DT1 to the SC-55's screen) */
        static const unsigned char rs[] = { 0x90, 60, 90, 64, 90, 0xF8, 67, 90, 60, 0, 64, 0, 67, 0 };
        static const unsigned char sx[] = { 0xF0, 0x41, 0x10, 0x45, 0x12, 0x10, 0x00, 0x00, 'U', 'S', 0xF8, 'B', ' ', 'M', 'I', 'D', 'I', 0x43, 0xF7 };
        for (i = 0; i < (int)sizeof rs; i++) { put(rs[i]); if (i == 4) wait_ms(400); }
        for (i = 0; i < (int)sizeof sx; i++) { put(sx[i]); if (i == 5) wait_ms(20); }
        wait_ms(300);
    }
    g_step = 4;
}
static int usb_check(void)
{   /* after the tune: did SBEMU send it all? 0 = yes, 3 = MIDI lost */
    char l[200];
    if (!g_usb) return 0;
    { uclock_t end = uclock() + UCLOCKS_PER_SEC * 2; do sbemu_musb(&g_mu); while (g_mu.q && uclock() < end); }
    if (!g_ui) printf("%s\n  this test: %lu packets sent, %lu dropped, %lu errors, waited %lu times for the cable\n", musb_line(&g_mu, l, sizeof l),
        g_mu.sent - g_mu0.sent, g_mu.drop - g_mu0.drop, g_mu.err - g_mu0.err, g_mu.busy - g_mu0.busy);
    return strcmp(g_mu.state, "active") || g_mu.drop != g_mu0.drop || g_mu.err != g_mu0.err || g_mu.sent == g_mu0.sent ? 3 : 0;
}

/* ------------------------------------------------------------------ the screen */
/* a piano keyboard: white keys 2 columns (the second one a dividing line), black keys 1 column on the line */
static void draw_keys(int x, int y, int lo, int octaves)
{
    static const int white[7] = { 0, 2, 4, 5, 7, 9, 11 };
    int o, k;
    for (o = 0; o <= octaves; ++o)
        for (k = 0; k < 7 && (o < octaves || k == 0); ++k)
        {
            int n = lo + o * 12 + white[k], xx = x + (o * 7 + k) * 2, a = g_on[n] ? (g_on[n] & 1 ? 0xE0 : g_on[n] & 2 ? 0xB0 : 0xA0) : 0x70;
            int j;
            for (j = 0; j < 3; ++j) { tui_cell(xx, y + j, a, ' '); tui_cell(xx + 1, y + j, a & 0xF0, 0xB3); }
            if (k == 0) tui_printf(xx, y + 3, 0x18, "C%d", (lo + o * 12) / 12 - 1);
            if (k != 2 && k != 6 && (o < octaves))   /* a black key after C D F G A */
            {
                int nb = n + 1, ab = g_on[nb] ? (g_on[nb] & 1 ? 0xE0 : g_on[nb] & 2 ? 0xB0 : 0xA0) : 0x00;
                tui_cell(xx + 1, y, ab, ' '); tui_cell(xx + 1, y + 1, ab, ' ');
            }
        }
}
static void draw(void)
{
    static const char* steps[4] = { "GS reset", "Piano scale", "Chord: strings + bass", "Drums" };
    static const struct { int n; const char* name; } dr[3] = { { 36, "Kick" }, { 38, "Snare" }, { 42, "Hi-hat" } };
    int i;
    char b[80];
    tu_head("MPUTEST", "a test tune through the MPU-401", NULL);
    tui_panel(2, 3, 30, 9, 1, 0x1E, "MPU-401");
    snprintf(b, sizeof b, "port %Xh", base);
    tui_put(4, 5, 0x1F, b);
    tu_state(4, 7, g_found ? 1 : -1, g_found ? "answers" : "no answer");
    if (g_usb)
    {
        int act = !strcmp(g_mu.state, "active");
        tui_printf(4, 9, act ? 0x1A : 0x1C, "USB %.22s", act ? (g_mu.name[0] ? g_mu.name : g_mu.id) : g_mu.state);
        tui_printf(4, 10, 0x17, "%.26s", g_mu.port);
        if (g_step >= 4) tui_printf(4, 11, g_mu.drop != g_mu0.drop || g_mu.err != g_mu0.err ? 0x1C : 0x1A, "%lu sent %lu lost", g_mu.sent - g_mu0.sent,
            (g_mu.drop - g_mu0.drop) + (g_mu.err - g_mu0.err));
    }
    else tui_put(4, 9, 0x17, g_found ? "UART mode, GS reset sent" : "is SBEMU loaded?");
    tui_panel(33, 3, 46, 9, 1, 0x1E, "Test tune");
    for (i = 0; i < 4; ++i)
    {
        int done = g_step > i, now = g_step == i;
        tui_printf(35, 5 + i, now ? 0x1E : done ? 0x1A : 0x18, "%c %s", done ? 0xFB : now ? 0x10 : 0xFA, steps[i]);
    }
    for (i = 0; i < 3; ++i) { tu_lamp(66, 5 + i, g_drum[dr[i].n], dr[i].name); if (g_drum[dr[i].n]) --g_drum[dr[i].n]; }
    tui_panel(2, 12, 77, 7, 1, 0x1E, "Notes");
    draw_keys(4, 13, 36, 4);
    tui_put(66, 13, 0x1E, "\xDB piano"); tui_put(66, 14, 0x1B, "\xDB strings"); tui_put(66, 15, 0x1A, "\xDB bass");
    tui_panel(2, 19, 77, 4, 1, 0x1E, NULL);
    if (!g_found)
        tui_wrap(4, 20, 73, 2, 0x1C, "No answer on the MPU-401: load SBEMU (SC55.BAT) with the SC-55 on. Real hardware ports without SBEMU do not answer either.");
    else if (g_step >= 4)
        tui_wrap(4, 20, 73, 2, 0x17, "Done. No sound? Check the volume, the SC55LOG.TXT result (ROM + speed test) and that the card is the one SBEMU uses.");
    else
        tui_wrap(4, 20, 73, 2, 0x17, "Playing on the SC-55 (or MT-32 / SoundFont, whichever SBEMU runs). You should hear a scale, a chord and a drum loop.");
    tu_foot(NULL, g_step >= 4 || !g_found ? "Enter play again   Esc quit   F4 screen" : "Esc stop   F4 screen");
}

int main(int argc, char** argv)
{
    int loop = 0, i;
    for (i = 1; i < argc; i++)
    {
        if (!strncmp(argv[i], "/P:", 3) || !strncmp(argv[i], "/p:", 3)) base = (int)strtol(argv[i] + 3, NULL, 16);
        else if (!strcmp(argv[i], "/L") || !strcmp(argv[i], "/l")) loop = 1;
        else if (!strcasecmp(argv[i], "/USB")) g_usbx = 1;
        else { puts("MPUTEST [/P:330] [/L] [/USB]\nPlays a test tune through the MPU-401 (SBEMU + SC-55 must be loaded).\n"
                    "/USB: also checks the USB MIDI device of SBEMU /MUSB (running status, real time, SysEx in pieces)."); return 2; }
    }
    g_usb = sbemu_musb(&g_mu0) && g_mu0.on; g_mu = g_mu0;
    if (g_usbx && !g_usb) { char l[200]; printf("MPUTEST /USB: %s\n", g_mu0.on || sbemu_musb(&g_mu0) ? musb_line(&g_mu0, l, sizeof l) : "SBEMU (beta.121+) is not loaded"); }
    { int p = mpu_find(base); if (p) base = p; }   /* SBEMU's MPU-401 on whatever port it uses */
    g_ui = isatty(1);
    g_found = detect();
    if (!g_ui)
    {
        printf("MPU-401 at port %Xh: ", base);
        if (!g_found)
        {
            puts("no answer.\nIs SBEMU loaded (SC55.BAT) with the SC-55 enabled, and is this a protected-mode program\n"
                 "run after HDPMI32I -r? Real hardware ports without SBEMU will not answer either.");
            return 1;
        }
        puts("found. Playing test tune...");
        if (g_usb) { char l[200]; printf("%s\n", musb_line(&g_mu0, l, sizeof l)); }
        outportb(base + 1, 0x3F);                       /* UART mode */
        do tune(); while (loop && !kbhit());
        if (loop && kbhit()) getch();
        if (g_usb) { int r = usb_check(); puts(r ? "USB MIDI: FAIL (MIDI lost, see above)" : "USB MIDI: PASS (everything went out)"); if (r) return r; }
        puts("Done. No sound? Check volume, the SC55LOG.TXT result and that the card is the one SBEMU uses.");
        return 0;
    }
    tu_start(argv[0]);
    for (;;)
    {
        int c;
        if (g_found)
        {
            outportb(base + 1, 0x3F);                   /* UART mode */
            g_stop = 0;
            do tune(); while (loop && !g_stop);
            usb_check();
            if (g_stop) { msg3(0xB0, 123, 0); msg3(0xB1, 123, 0); msg3(0xB2, 123, 0); msg3(0xB9, 123, 0); memset(g_on, 0, sizeof g_on); g_step = 4; }
        }
        draw();
        c = tu_wait_key();
        if (c == 27 || c == 'q' || c == 'Q') break;
        if (c == 13 && !g_found) g_found = detect();
    }
    tu_end();
    return g_found ? 0 : 1;
}
