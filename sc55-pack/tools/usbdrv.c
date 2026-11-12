/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* USBDRV - the USB sticks / card readers SBEMU (/USBDISK:1) gave drive letters: letter, stick, file system, size and
 * free space. R refreshes (each drive is asked again, so a stick plugged in since gets one of the spare letters),
 * Esc quits. USBDRV /T prints the list as text (for logs and tests).
 * Finds SBEMU's drives through DOS's drive table (CDS -> DPB -> device header named "USBDSK"); the stick names
 * and states come from the info area SBEMU keeps next to its device stub (usbdisk.c, UD_INFO). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include "toolui.h"

#define UD_INFO   (0x80 + 8 * (0x40 + 0x40))
#define UD_INFOSZ 48
#define UD_RINFO  (UD_INFO + 8 * UD_INFOSZ)
#define UD_LOG    (UD_RINFO + 8 * UD_INFOSZ)
#define UD_LOGN   64
#define UD_DIAG   (UD_LOG + 4 + UD_LOGN * 8)
#define UD_DIAGSZ 160   /* exFAT / NTFS drives (read-only, SBEMU's redirector) */

typedef struct { int letter, unit, state, fat, ok, redir; unsigned seg; char name[44]; double size, freeb; } Drv;
static Drv D[26];
static int ND, g_text;
static char g_msg[80];

static unsigned long lin(unsigned seg, unsigned off) { return (unsigned long)seg * 16 + off; }

/* a real-mode INT 24h that answers "fail": an empty drive must not stop us with Abort, Retry, Fail? */
static int g_i24seg = -1, g_i24sel;
static __dpmi_raddr g_old24;
static void i24_on(void)
{
    static const unsigned char code[] = { 0xB0, 0x03, 0xCF };     /* mov al,3 ; iret */
    __dpmi_raddr a;
    if (g_i24seg < 0) { g_i24seg = __dpmi_allocate_dos_memory(1, &g_i24sel); if (g_i24seg < 0) return; dosmemput(code, sizeof code, g_i24seg * 16); }
    __dpmi_get_real_mode_interrupt_vector(0x24, &g_old24);
    a.segment = (unsigned short)g_i24seg; a.offset16 = 0;
    __dpmi_set_real_mode_interrupt_vector(0x24, &a);
}
static void i24_off(void) { if (g_i24seg >= 0) __dpmi_set_real_mode_interrupt_vector(0x24, &g_old24); }

static void space(Drv* d)
{
    __dpmi_regs r;
    char path[4] = { (char)('A' + d->letter), ':', '\\', 0 };
    d->ok = 0;
    /* FAT32-aware free space first (DOS 7.10 / FreeDOS), else the classic call (wraps at 2 GB) */
    dosmemput(path, 4, __tb);
    memset(&r, 0, sizeof r);
    r.x.ax = 0x7303; r.x.ds = __tb >> 4; r.x.dx = __tb & 15; r.x.es = __tb >> 4; r.x.di = (__tb & 15) + 16; r.x.cx = 0x2C;
    _farpokew(_dos_ds, __tb + 16, 0x2C); _farpokew(_dos_ds, __tb + 18, 0);
    __dpmi_int(0x21, &r);
    if (!(r.x.flags & 1) && r.x.ax != 0x7300)
    {
        unsigned long b = __tb + 16, spc = _farpeekl(_dos_ds, b + 4), bps = _farpeekl(_dos_ds, b + 8);
        d->freeb = (double)_farpeekl(_dos_ds, b + 12) * spc * bps;
        d->size = (double)_farpeekl(_dos_ds, b + 16) * spc * bps;
        d->ok = d->size > 0;
        if (d->ok) return;
    }
    memset(&r, 0, sizeof r);
    r.h.ah = 0x36; r.h.dl = (unsigned char)(d->letter + 1);
    __dpmi_int(0x21, &r);
    if (r.x.ax == 0xFFFF) return;
    d->freeb = (double)r.x.bx * r.x.ax * r.x.cx;
    d->size = (double)r.x.dx * r.x.ax * r.x.cx;
    d->ok = 1;
}

static void scan(int ask)
{
    __dpmi_regs r;
    unsigned long lol, cds;
    int last, i;
    ND = 0;
    memset(&r, 0, sizeof r); r.h.ah = 0x52; __dpmi_int(0x21, &r);
    lol = lin(r.x.es, r.x.bx);
    cds = lin(_farpeekw(_dos_ds, lol + 0x18), _farpeekw(_dos_ds, lol + 0x16));
    last = _farpeekb(_dos_ds, lol + 0x21);
    if (ask) i24_on();
    for (i = 2; i < last && i < 26; ++i)
    {
        unsigned long c = cds + i * 0x58, dpb;
        unsigned seg;
        char sig[7];
        Drv* d;
        if (!(_farpeekw(_dos_ds, c + 0x43) & 0x4000) || (_farpeekw(_dos_ds, c + 0x43) & 0x8000)) continue;
        dpb = lin(_farpeekw(_dos_ds, c + 0x47), _farpeekw(_dos_ds, c + 0x45));
        seg = _farpeekw(_dos_ds, dpb + 0x15);
        if (_farpeekw(_dos_ds, dpb + 0x13) != 0 || !seg) continue;
        dosmemget(lin(seg, 0x0B), 6, sig); sig[6] = 0;
        if (strcmp(sig, "USBDSK")) continue;
        d = &D[ND++];
        memset(d, 0, sizeof *d);
        d->letter = i; d->seg = seg; d->unit = _farpeekb(_dos_ds, dpb + 1);
        if (ask) space(d);                                       /* DOS asks the drive: a new stick is taken now */
        d->state = _farpeekb(_dos_ds, lin(seg, UD_INFO + d->unit * UD_INFOSZ));
        d->fat = _farpeekb(_dos_ds, lin(seg, UD_INFO + d->unit * UD_INFOSZ + 1));
        dosmemget(lin(seg, UD_INFO + d->unit * UD_INFOSZ + 4), sizeof d->name - 1, d->name);
        d->name[sizeof d->name - 1] = 0;
        if (d->state != 1) d->ok = 0;
    }
    if (ND)                                                      /* + the exFAT / NTFS drives SBEMU noted there */
    {
        unsigned seg = D[0].seg;
        int k, n = ND;
        for (k = 0; k < 8 && ND < 26; ++k)
        {
            unsigned long b = lin(seg, UD_RINFO + k * UD_INFOSZ);
            Drv* d;
            int lt = _farpeekb(_dos_ds, b);
            if (!lt) continue;
            d = &D[ND++];
            memset(d, 0, sizeof *d);
            d->letter = lt - 1; d->redir = _farpeekb(_dos_ds, b + 1); d->state = 1; d->seg = seg;
            dosmemget(b + 4, sizeof d->name - 1, d->name);
            if (ask) { i24_on(); space(d); i24_off(); }
        }
        (void)n;
    }
    if (ask) i24_off();
}

static const char* mb(double b, char* o)
{
    if (b >= 1024.0 * 1024 * 1024 * 10) sprintf(o, "%.0f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024.0 * 1024 * 1024) sprintf(o, "%.1f GB", b / (1024.0 * 1024 * 1024));
    else sprintf(o, "%.0f MB", b / (1024.0 * 1024));
    return o;
}

static void row(int i, char* o, int n)
{
    const Drv* d = &D[i];
    char a[16], b[16], fs[12];
    if (d->redir) strcpy(fs, d->redir == 2 ? "NTFS" : "exFAT");
    else if (d->state == 1) sprintf(fs, "FAT%d", d->fat); else strcpy(fs, "-");
    if (d->state == 1 && d->ok)
        snprintf(o, n, "%c:  %-28.28s %-6s %9s %9s%s", 'A' + d->letter, d->name[0] ? d->name : "(USB disk)", fs, mb(d->size, a), mb(d->freeb, b),
                 d->redir ? "  read-only" : "");
    else if (d->state == 1)
        snprintf(o, n, "%c:  %-28.28s %-6s  DOS gives an error for it (send USBDRV /L)", 'A' + d->letter, d->name[0] ? d->name : "(USB disk)", fs);
    else if (d->redir)
        snprintf(o, n, "%c:  %-28.28s %-6s  not ready (stick out?)", 'A' + d->letter, d->name[0] ? d->name : "(USB disk)", fs);
    else if (d->state == 2)
        snprintf(o, n, "%c:  %-28.28s %-6s  no FAT partition (exFAT / NTFS: later)", 'A' + d->letter, d->name[0] ? d->name : "(USB disk)", fs);
    else
        snprintf(o, n, "%c:  %-28.28s %-6s  empty - plug a stick in, then R", 'A' + d->letter, "(no stick)", fs);
}

static void draw(void)
{
    int i;
    char b[90];
    tu_head("USBDRV", "USB sticks and card readers with a drive letter", NULL);
    tui_panel(2, 3, 77, 18, 1, 0x1E, "USB drives");
    if (!ND)
    {
        tui_wrap(4, 5, 72, 6, 0x17, "No USB drive letters. Turn them on in MENU > Settings > Joystick > USB sticks (SBEMU's /USBDISK:1),"
            " put LASTDRIVE=Z in CONFIG.SYS and restart the PC. Sticks plugged in when SBEMU starts get letters, and two spare"
            " letters wait for sticks plugged in later.");
    }
    else
    {
        snprintf(b, sizeof b, "%-4s%-29s%-6s %9s %9s", "", "Stick", "Format", "Size", "Free");
        tui_put(4, 5, 0x17, b);
        for (i = 0; i < ND && i < 12; ++i) { row(i, b, sizeof b); tui_put(4, 7 + i, D[i].state == 1 ? 0x1F : 0x18, b); }
    }
    if (g_msg[0]) tui_put(4, 19, 0x1B, g_msg);
    tu_foot("R: ask every drive again (a stick plugged in since gets a spare letter).", "R refresh  Esc quit");
}

int main(int argc, char** argv)
{
    __dpmi_regs r;
    int i;
    if (argc > 1 && !strcasecmp(argv[1], "/L"))
    {   /* SBEMU's last 64 USB drive requests from DOS (for bug reports) */
        static const char* cn[] = { "init", "media check", "build bpb", "ioctl in", "read", "nd read", "in status", "in flush",
            "write", "write+verify", "out status", "out flush", "ioctl out", "open", "close", "removable", "out busy", "?",
            "?", "generic ioctl", "?", "?", "?", "get logical", "set logical", "ioctl query" };
        unsigned nx, k;
        unsigned long lg;
        scan(0);
        if (!ND) { printf("No USB drive letters.\n"); return 0; }
        lg = lin(D[0].seg, UD_LOG);
        nx = _farpeekw(_dos_ds, lg);
        printf("USBDRV /L: last requests (oldest first), %u in all. Drives:", nx);
        for (k = 0; k < (unsigned)ND; ++k) if (!D[k].redir) printf(" %c:=unit %d", 'A' + D[k].letter, D[k].unit);
        printf("\n");
        for (k = nx > UD_LOGN ? nx - UD_LOGN : 0; k < nx; ++k)
        {
            unsigned long e = lg + 4 + (k % UD_LOGN) * 8;
            unsigned c = _farpeekb(_dos_ds, e), st = _farpeekw(_dos_ds, e + 2);
            printf("%5u  unit %u  %-13s cmd %02Xh  status %04Xh%s  %s %lu\n", k, _farpeekb(_dos_ds, e + 1), c < 26 ? cn[c] : "?", c, st,
                   st & 0x8000 ? " ERROR" : "      ", (c == 4 || c == 8 || c == 9) ? "sector" : "len", _farpeekl(_dos_ds, e + 4));
        }
        for (k = 0; k < (unsigned)ND; ++k)
        {
            unsigned long g = lin(D[0].seg, UD_DIAG + D[k].unit * UD_DIAGSZ);
            int j;
            if (D[k].redir) continue;
            printf("%c: unit %d  start %lu size %lu  FAT%d fat32ok %d  53h flags %04X ax %04X\n   BPB:", 'A' + D[k].letter, D[k].unit,
                   _farpeekl(_dos_ds, g), _farpeekl(_dos_ds, g + 4), _farpeekb(_dos_ds, g + 12), _farpeekb(_dos_ds, g + 13),
                   _farpeekw(_dos_ds, g + 8), _farpeekw(_dos_ds, g + 10));
            for (j = 0; j < 64; ++j) printf("%s%02X", j && j % 32 == 0 ? "\n       " : " ", _farpeekb(_dos_ds, g + 16 + j));
            printf("\n   MBR 1BEh:");
            for (j = 0; j < 66; ++j) printf("%s%02X", j && j % 16 == 0 ? "\n            " : " ", _farpeekb(_dos_ds, g + 80 + j));
            printf("\n");
        }
        return 0;
    }
    if (argc > 1 && !strcasecmp(argv[1], "/T")) g_text = 1;
    else if (argc > 1) { printf("USBDRV [/T | /L]\n"); return 1; }
    scan(1);
    if (g_text)
    {
        char b[100];
        if (!ND) printf("No USB drive letters (SBEMU /USBDISK:1 not loaded).\n");
        for (i = 0; i < ND; ++i) { row(i, b, sizeof b); printf("%s\n", b); }
        return 0;
    }
    tu_start(argv[0]);
    for (;;)
    {
        draw(); tui_flush();
        if (tui_fade_pending) { tui_fade_pending = 0; tui_fade(1); }
        memset(&r, 0, sizeof r); r.h.ah = 0x11; __dpmi_int(0x16, &r);
        if (!(r.x.flags & 0x40))
        {
            memset(&r, 0, sizeof r); r.h.ah = 0x10; __dpmi_int(0x16, &r);
            if (r.h.ah == 1) break;
            if (r.h.al == 'r' || r.h.al == 'R') { strcpy(g_msg, "Asking the drives ..."); draw(); tui_flush(); scan(1); sprintf(g_msg, "Refreshed: %d drive(s).", ND); }
        }
        __dpmi_yield();
    }
    tu_end();
    return 0;
}
