#include <pc.h>
#include "mpuport.h"
/* USBTEST - finds USB pads, keyboards and mice the same way SBEMU /JOY does, and shows every step.
 *   USBTEST            USB controllers found, what was done with each, what is on their ports, then live:
 *                      both pads (sticks, triggers, buttons), USB keys pressed, mouse movement, every plug /
 *                      unplug. Esc (PS/2 or USB keyboard) quits.
 *                      USB disks: model, size, and the first bytes of block 0 (S: again).
 *   USBTEST FILE       the same, also written to FILE (for a bug report)
 *   USBTEST FILE /T:n  ... and stop by itself after n seconds
 *   USBTEST /PAD:GAME.EXE  also: the pad layout SBEMU would use for that program (PADMAP.TXT / PADUSER.TXT next to
 *                      USBTEST.EXE, see padmap.c) and why, and the key codes each pad button would type there
 *                      (shown, not typed). /PAD alone: the Default layout.
 * Run it without SBEMU loaded (it takes the USB controllers over, like SBEMU does), after
 * C:\HDPMI\HDPMI32I.EXE -r -x. The BIOS's USB keyboard / mouse support stops until the next restart. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <conio.h>
#include <dos.h>
#include <go32.h>
#include <sys/farptr.h>
#include "usbpad.h"
#include "dpmi/dpmi.h"
#include "toolui.h"
#include "padmap.h"

static FILE* Log;
static void out(const char* f, ...);
static unsigned long long Cps;
static inline unsigned long long tsc(void);
static void disks(void)                 /* USB disks: what they are, and block 0 */
{
    static uint8_t b[4096];
    int n, i, r;
    for (n = 0; n < USBPAD_Disks(); ++n)
    {
        const USBPAD_Disk* u = USBPAD_DiskInfo(n);
        if (!u) break;
        out("\nUSB disk %d: %04X:%04X LUN %d \"%s %s\" ", n, u->vid, u->pid, u->lun, u->vendor, u->product);
        if (!u->ready) { out("no medium\n"); continue; }
        out("%lu blocks of %lu bytes (%lu MB)\n", (unsigned long)u->blocks, (unsigned long)u->bsize,
            (unsigned long)((unsigned long long)u->blocks * u->bsize >> 20));
        r = USBPAD_DiskRead(n, 0, 1, b);
        if (r) { out("  block 0: read error %d\n", r); continue; }
        out("  block 0:");
        for (i = 0; i < 32; ++i) out("%s%02X", i % 16 ? " " : "\n   ", b[i]);
        out("\n   ... %02X %02X at 1FEh (%s)\n", b[510], b[511], b[510] == 0x55 && b[511] == 0xAA ? "boot record" : "no boot record");
        {   /* read test: the first MB (or less), CRC-32 to compare with the image on another PC */
            uint32_t crc = 0xFFFFFFFFUL, blk = 0, want = 1048576UL / u->bsize, j;
            uint64_t t0 = tsc();
            int per = (int)(sizeof b / u->bsize), e;
            if (want > u->blocks) want = u->blocks;
            while (blk < want)
            {
                int c = want - blk > (uint32_t)per ? per : (int)(want - blk);
                if ((r = USBPAD_DiskRead(n, blk, c, b)) != 0) break;
                for (j = 0; j < (uint32_t)c * u->bsize; ++j)
                    for (crc ^= b[j], e = 0; e < 8; ++e) crc = (crc >> 1) ^ (0xEDB88320UL & (0UL - (crc & 1)));
                blk += (uint32_t)c;
            }
            t0 = tsc() - t0;
            if (r) out("  read test: error %d at block %lu\n", r, (unsigned long)blk);
            else out("  read test: %lu KB in %lu ms, CRC-32 %08lX\n", (unsigned long)(want * u->bsize >> 10),
                     (unsigned long)(t0 * 1000 / (Cps ? Cps : 1)), (unsigned long)~crc);
        }
    }
}
static int Ui;                      /* full-screen view (on the screen; plain text when redirected) */
static char Text[16384];            /* what out() wrote, for the Steps panel */

static void out(const char* f, ...)
{
    va_list a;
    if (!Ui) { va_start(a, f); vprintf(f, a); va_end(a); }
    else
    {
        size_t n = strlen(Text);
        if (n > sizeof Text - 2048) { memmove(Text, Text + 4096, n - 4096 + 1); n -= 4096; }
        va_start(a, f); vsnprintf(Text + n, sizeof Text - n, f, a); va_end(a);
    }
    if (Log) { va_start(a, f); vfprintf(Log, f, a); va_end(a); fflush(Log); }
}

static inline unsigned long long tsc(void) { unsigned lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return ((unsigned long long)hi << 32) | lo; }
static unsigned long ticks(void) { return _farpeekl(_dos_ds, 0x46C); }

/* USB keyboard / mouse events: collected in the poll, printed by the main loop */
static volatile int KEsc, KNew, MNew;
static uint8_t KRep[8];
static long MX, MY, MW; static int MBt;
static void kb(int id, const uint8_t* r)
{
    int i;
    (void)id;
    memcpy(KRep, r, 8); KNew = 1;
    for (i = 2; i < 8; ++i) if (r[i] == 0x29) KEsc = 1;
}
static void ms(int b, int dx, int dy, int dw) { MX += dx; MY += dy; MW += dw; MBt = b; MNew = 1; }

/* /PAD: the layout and the key codes the pads would type */
static int PadOn;
static char PadTitle[64], PadKeys[64], PadNew[256];
static int PadNewN;
static void pad_emit(const uint8_t* b, int n)
{
    int i;
    for (i = 0; i < n; ++i)
    {
        size_t l = strlen(PadKeys);
        if (l > sizeof PadKeys - 4) { memmove(PadKeys, PadKeys + 3, l - 3 + 1); l -= 3; }
        snprintf(PadKeys + l, sizeof PadKeys - l, "%02X ", b[i]);
        if (PadNewN < (int)sizeof PadNew - 4) PadNewN += snprintf(PadNew + PadNewN, sizeof PadNew - PadNewN, "%02X ", b[i]);
    }
}
static void out(const char* f, ...);
static void pad_note(int kind, const char* t) { out("Pad layouts: %s%s\n", kind == 1 ? "problem: " : "", t); }
static void pad_tick(void)
{
    PADMAP_In in[2];
    PADMAP_Out o;
    int i, k;
    if (!PadOn) return;
    for (i = 0; i < 2; ++i)
    {
        static const uint16_t x[PM_BUTTONS] = { XPAD_A, XPAD_B, XPAD_X, XPAD_Y, XPAD_LB, XPAD_RB, 0, 0, XPAD_LS, XPAD_RS,
                                                XPAD_BACK, XPAD_START, XPAD_DU, XPAD_DD, XPAD_DL, XPAD_DR };
        const USBPAD_State* p = USBPAD_GetPad(i);
        in[i].present = p->connected;
        in[i].buttons = 0;
        for (k = 0; k < PM_BUTTONS; ++k) if (x[k] && (p->buttons & x[k])) in[i].buttons |= (uint16_t)(1u << k);
        if (p->lt > 128) in[i].buttons |= 1u << PM_LT;
        if (p->rt > 128) in[i].buttons |= 1u << PM_RT;
        in[i].axis[PM_LX] = p->lx; in[i].axis[PM_LY] = p->ly == -32768 ? 32767 : -p->ly;
        in[i].axis[PM_RX] = p->rx; in[i].axis[PM_RY] = p->ry == -32768 ? 32767 : -p->ry;
    }
    PadNewN = 0; PadNew[0] = 0;
    PADMAP_Step(in, 2, &o, pad_emit);
    if (PadNewN) out("\nPad keys: %s%s", PadNew, o.game && o.buttons ? "(game port buttons too)" : "");
}

/* ------------------------------------------------------------------ the screen */
static const char* Result = "";
static void draw_pad(int i, int x, int y)
{
    static const struct { const char* n; unsigned bit; int x, y; } b[] = {
        { "LB", XPAD_LB, 2, 1 }, { "RB", XPAD_RB, 42, 1 }, { "Back", XPAD_BACK, 15, 2 }, { "Guide", XPAD_GUIDE, 21, 2 },
        { "Start", XPAD_START, 28, 2 }, { "Up", XPAD_DU, 16, 3 }, { "Left", XPAD_DL, 13, 4 }, { "Right", XPAD_DR, 19, 4 },
        { "Down", XPAD_DD, 15, 5 }, { "Y", XPAD_Y, 29, 3 }, { "X", XPAD_X, 26, 4 }, { "B", XPAD_B, 32, 4 }, { "A", XPAD_A, 29, 5 },
        { "LS", XPAD_LS, 14, 6 }, { "RS", XPAD_RS, 30, 6 } };
    const USBPAD_State* p = USBPAD_GetPad(i);
    char t[40]; int k;
    snprintf(t, sizeof t, "Pad %d", i + 1);
    tui_panel(x, y, 48, 10, 1, 0x1E, t);
    if (!p->connected) { tui_put(x + 2, y + 1, 0x18, "not connected - plug a pad in"); return; }
    tu_plot(x + 2, y + 2, 11, 5, (p->lx + 32768) / 65535.0f, (32767 - p->ly) / 65535.0f, 0x1E);
    tu_plot(x + 35, y + 2, 11, 5, (p->rx + 32768) / 65535.0f, (32767 - p->ry) / 65535.0f, 0x1E);
    tui_bar(x + 2, y + 8, 11, p->lt, 255, 0x1B, 0x18);
    tui_bar(x + 35, y + 8, 11, p->rt, 255, 0x1B, 0x18);
    tui_put(x + 14, y + 8, 0x17, "LT"); tui_put(x + 32, y + 8, 0x17, "RT");
    for (k = 0; k < (int)(sizeof b / sizeof b[0]); ++k)
    {
        int xx = b[k].x, yy = b[k].y;
        tu_lamp(x + xx, y + yy, p->buttons & b[k].bit, b[k].n);
    }
    tui_printf(x + 14, y + 7, 0x13, "reports %lu", (unsigned long)p->reports);
}
static void draw(int busy)
{
    int i;
    tu_head("USBTEST", "USB pads, keyboards and mice, found like SBEMU /JOY", NULL);
    draw_pad(0, 2, 3);
    draw_pad(1, 2, 13);
    tui_panel(51, 3, 28, 7, 1, 0x1E, "USB keyboard");
    tui_printf(53, 5, 0x17, "modifiers %02X", KRep[0]);
    tui_put(53, 6, 0x17, "keys");
    for (i = 2; i < 8; ++i) tui_printf(58 + (i - 2) * 3, 6, KRep[i] ? 0x1E : 0x18, "%02X", KRep[i]);
    tui_put(53, 7, 0x18, "HID usage codes");
    if (PadOn) { tui_printf(53, 4, 0x1B, "Pad: %.20s", PadTitle); tui_printf(53, 8, 0x1E, "%-24.24s", PadKeys[0] ? PadKeys : "pad keys: none yet"); }
    tui_panel(51, 10, 28, 6, 1, 0x1E, "USB mouse");
    tui_printf(53, 11, 0x1F, "x %5ld  y %5ld", MX, MY);
    tui_printf(53, 12, 0x17, "wheel %ld", MW);
    tu_lamp(53, 14, MBt & 1, "Left"); tu_lamp(60, 14, MBt & 4, "Middle"); tu_lamp(69, 14, MBt & 2, "Right");
    tui_panel(51, 16, 28, 7, 1, 0x1E, "Result");
    tu_text(53, 17, 24, 5, busy ? 0x1E : 0x1F, busy ? "Looking for USB controllers and devices (up to 3 s) ..." : Result);
    tui_fill(1, 23, 80, 1, 0x10, ' ');
    tu_log(2, 23, 78, 1, 0x13, Text);
    tu_foot(NULL, Log ? "Esc quit (PS/2 or USB keyboard)   every step is also in the log file" : "Esc quit (PS/2 or USB keyboard)");
}

static int KeySeen, dumped5, dumped20;
static void out(const char* fmt, ...);
static void dump(const char* when)
{
    static char b[2048];
    USBPAD_Dump(b, sizeof b);
    out("\n-- pipes (%s):\n%s", when, b);
}
int main(int argc, char** argv)
{
    unsigned long long cps, a, t, start;
    unsigned long tk;
    const char* msg = "";
    int n, i, secs = 0;
    uint32_t ch = 0;
    unsigned lastb[2] = { 0xFFFFFFFF, 0xFFFFFFFF };
    int force = 0;
    const char* padprog = NULL;
    for (i = 1; i < argc; ++i)
    {
        if (!strncasecmp(argv[i], "/T:", 3)) secs = atoi(argv[i] + 3);
        else if (!strcasecmp(argv[i], "/F")) force = 1;
        else if (!strncasecmp(argv[i], "/PAD", 4) && (!argv[i][4] || argv[i][4] == ':'))
        { PadOn = 1; padprog = argv[i][4] == ':' && argv[i][5] ? argv[i] + 5 : NULL; }
        else if (argv[i][0] != '/') Log = fopen(argv[i], "w");
        else { printf("USBTEST [logfile] [/T:seconds] [/F] [/PAD[:GAME.EXE]]\n"); return 1; }
    }
    /* SBEMU resident drives the same USB controllers from its sound interrupt: both at once crashed the
     * P5PE-VM (EMM386 error 06 after SC55, then USBTEST). Its MPU-401 answering = SBEMU (or a real MPU). */
    if (!force && mpu_find(0))
    {
        printf("SBEMU (or an MPU-401) is answering: SBEMU is loaded and is already using the USB controllers.\n"
               "Running USBTEST now can crash the PC. Restart, then run USBTEST BEFORE SC55.BAT.\n"
               "(USBTEST /F runs it anyway, e.g. with a real MPU-401 card and no SBEMU.)\n");
        return 2;
    }
    DPMI_Init();
    Ui = isatty(1);
    if (Ui) { tu_start(argv[0]); draw(1); tu_poll(); }
    tk = ticks(); while (ticks() == tk) {}
    a = tsc(); tk = ticks(); while (ticks() - tk < 4) {}
    cps = (tsc() - a) * 18206 / 4000; Cps = cps;
    out("USBTEST - USB pad / keyboard / mouse finder (same code as SBEMU /JOY)\nCPU %lu MHz\n", (unsigned long)(cps / 1000000));
    if (PadOn)
    {   /* the same files and look-up as SBEMU (its folder = this program's) */
        char dir[260], where[64], d[600];
        char* e;
        int how = 0, lay = 0;
        snprintf(dir, sizeof dir, "%s", argv[0]);
        e = strrchr(dir, '\\'); if (!e) e = strrchr(dir, '/');
        if (e) *e = 0; else strcpy(dir, ".");
        if (!PADMAP_Prepare(dir, pad_note)) out("Pad layouts: none (no PADMAP.TXT or PADUSER.TXT in %s): only the Default layout\n", dir);
        if (padprog)
        {
            {   /* its id, for a PADUSER.TXT section of the user's own */
                struct find_t f;
                if (_dos_findfirst(padprog, _A_NORMAL | _A_RDONLY | _A_ARCH | _A_HIDDEN | _A_SYSTEM, &f) == 0)
                    out("%s: %lu bytes, id %08lX (Id= in PADUSER.TXT)\n", f.name, (unsigned long)f.size,
                        (unsigned long)PADMAP_FileId(f.name, f.size));
                else out("%s: not found\n", padprog);
            }
            lay = PADMAP_ForProgram(padprog, where, sizeof where, &how);
            out("Pad layout for %s: %s (%s%s)\n", padprog, PADMAP_Title(lay), how ? where : "no layout for it",
                how == 2 ? " found by its id" : how == 1 ? " in its folder" : "");
        }
        PADMAP_Select(lay);
        snprintf(PadTitle, sizeof PadTitle, "%s", PADMAP_Title(lay));
        PADMAP_Describe(lay, d, sizeof d);
        out("Pad layout %s: %s\n", PADMAP_Title(lay), d);
    }
    out("Looking for USB controllers and devices (up to 3 s)...\n");
    USBPAD_SetHid(kb, ms);
    n = USBPAD_Init(cps, 3000, USBPAD_F_KBD | USBPAD_F_MOUSE | USBPAD_F_DISK, &msg);
    out("%s\n", USBPAD_Trace());
    out("\nResult: %s\n", msg);
    Result = msg;
    if (!n)
    {
        out("Nothing usable.\n");
        if (Ui) { Result = "Nothing usable: no USB controller with a pad, keyboard or mouse. The log has every step."; draw(0); tu_wait_key(); tu_end(); }
        return 1;
    }
    disks();
    out("\nLive view - plug / unplug, move the sticks, type, move the mouse. Esc quits\n(closes by itself after 2 minutes without a key). D: pipe state, R: restart pipes, S: USB disks.\n");
    {
        int shown = (int)strlen(USBPAD_Trace());
        t = start = tsc();
        for (;;)
        {
            const char* tr;
            USBPAD_Poll();
            pad_tick();
            tr = USBPAD_Trace();
            if ((int)strlen(tr) > shown) { out("\n%s", tr + shown); shown = (int)strlen(tr); }
            if (USBPAD_Changes() != ch)
            {
                ch = USBPAD_Changes();
                out("\n-- %s\n-- in use now:\n%s", USBPAD_Describe(), USBPAD_Devices()[0] ? USBPAD_Devices() : "(nothing)\n");
                if (USBPAD_Have() & USBPAD_HAVE_DISK) disks();
            }
            if (KNew)
            {
                KNew = 0; KeySeen = 1;
                out("\nUSB keys: mod %02X keys %02X %02X %02X %02X %02X %02X", KRep[0], KRep[2], KRep[3], KRep[4], KRep[5], KRep[6], KRep[7]);
            }
            if (Ui && tsc() - t > cps / 30)
            {
                t = tsc();
                Result = USBPAD_Devices()[0] ? USBPAD_Devices() : "(nothing in use)";
                draw(0);
                { int k = tu_poll(); if (k) KeySeen = 1; if (k == 27) break;
                  if (k == 'd' || k == 'D') dump("key D");
                  if (k == 's' || k == 'S') disks();
                  if (k == 'r' || k == 'R') { dump("before R"); USBPAD_Rearm(); out("\n-- R: every pipe restarted\n"); } }
                for (i = 0; i < USBPAD_PADS; ++i)
                {
                    const USBPAD_State* p = USBPAD_GetPad(i);
                    if (p->connected && p->buttons != lastb[i] && Log)
                    {
                        fprintf(Log, "pad %d buttons %04X  LX %d LY %d RX %d RY %d LT %d RT %d (reports %lu)\n", i + 1, p->buttons,
                            p->lx, p->ly, p->rx, p->ry, p->lt, p->rt, (unsigned long)p->reports);
                        fflush(Log);
                        lastb[i] = p->buttons;
                    }
                }
                if (MNew) { MNew = 0; if (Log) { fprintf(Log, "mouse x %ld y %ld wheel %ld buttons %d\n", MX, MY, MW, MBt); fflush(Log); } }
            }
            else if (!Ui && tsc() - t > cps / 10)
            {
                t = tsc();
                printf("\r");
                for (i = 0; i < USBPAD_PADS; ++i)
                {
                    const USBPAD_State* p = USBPAD_GetPad(i);
                    if (p->connected)
                    {
                        printf("%d:%6d%7d%7d%7d %04X ", i + 1, p->lx, p->ly, p->rx, p->ry, p->buttons);   /* 37 chars */
                        if (p->buttons != lastb[i] && Log)
                        {
                            fprintf(Log, "pad %d buttons %04X  LX %d LY %d RX %d RY %d LT %d RT %d (reports %lu)\n", i + 1, p->buttons,
                                p->lx, p->ly, p->rx, p->ry, p->lt, p->rt, (unsigned long)p->reports);
                            fflush(Log);
                            lastb[i] = p->buttons;
                        }
                    }
                }
                if (MNew)
                {
                    MNew = 0;
                    out("\nmouse x %ld y %ld wheel %ld buttons %d\n", MX, MY, MW, MBt);
                }
            }
            if (!dumped5 && tsc() - start > cps * 5ULL) { dumped5 = 1; dump("5 s"); }
            if (!dumped20 && tsc() - start > cps * 20ULL) { dumped20 = 1; dump("20 s"); }
            if (KEsc) break;
            /* way out when the PS/2 keyboard's interrupts do not reach the BIOS: Esc straight from the
             * keyboard controller, and closing by itself after 2 minutes without a key */
            if ((inportb(0x64) & 0x21) == 0x01 && inportb(0x60) == 0x01) break;
            if (!secs && tsc() - start > cps * 120ULL && !KeySeen) { out("\nNo key for 2 minutes: closing by itself.\n"); break; }
            if (!Ui && kbhit() && getch() == 27) break;
            if (secs && tsc() - start > cps * (unsigned long long)secs) break;
        }
    }
    out("\n");
    USBPAD_Shutdown();
    tu_end();
    if (Log) fclose(Log);
    return 0;
}
