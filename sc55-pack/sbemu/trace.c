/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
/* trace.c - SBEMU flight recorder: see trace.h. Two banks: interrupts fill one, the DOS prompt (INT 28h) swaps them
 * and writes the full one to SBTRACE.TXT. */
#include "trace.h"
#include "dpmi/dpmi.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TR_NEV    64
#define TR_NPROG  6
#define TR_TXT    48
#define TR_FILEMAX (64L * 1024)              /* SBTRACE.TXT starts again when it is bigger */
#define DOSBUF    4096

typedef struct { uint32_t t0, t1, n; uint8_t kind; char txt[TR_TXT]; } TrEv;
typedef struct
{
    uint16_t psp; char name[9];
    uint32_t t0, t1;
    uint32_t dsp[256], mpucmd[256];
    uint32_t midibytes, midi[8], sysex, common, realtime, dropped;
    uint32_t irq_raised, irq_acked, replayed, late;
    int irq; uint8_t dma_warned, irq_warned;
} TrProg;
typedef struct { TrEv ev[TR_NEV]; int nev, lost; TrProg prog[TR_NPROG]; int nprog; uint32_t since; int dirty; } TrBank;

static TrBank g_bank[2];
static volatile int g_cur;
static int g_on, g_busy;
static char g_path[128];
static uint32_t g_indos_lin, g_sda_lin, g_dosbuf_lin;
static uint16_t g_dosbuf_seg;
static uint32_t g_last_under, g_under_init;

static inline uint32_t irq_off(void) { uint32_t f; __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory"); return f; }
static inline void irq_on(uint32_t f) { if (f & 0x200) __asm__ volatile("sti" ::: "memory"); }
static uint32_t ticks(void) { return DPMI_LoadD(0x46C); }
static TrBank* bank(void) { return &g_bank[g_cur]; }

/* ---- events */
static void ev_add(int kind, uint32_t n, const char* txt)
{
    TrBank* b = bank(); uint32_t t = ticks(); TrEv* e;
    if (!g_on) return;
    b->dirty = 1;
    if (b->nev)
    {
        e = &b->ev[(b->nev - 1) % TR_NEV];
        if (e->kind == kind && t - e->t1 < 37 && !strcmp(e->txt, txt)) { e->n += n; e->t1 = t; return; }   /* same within 2 s */
    }
    if (b->nev >= TR_NEV) b->lost++;
    e = &b->ev[b->nev % TR_NEV]; b->nev++;
    e->t0 = e->t1 = t; e->n = n; e->kind = (uint8_t)kind;
    strncpy(e->txt, txt, TR_TXT - 1); e->txt[TR_TXT - 1] = 0;
}
void TRACE_Event(int kind, const char* fmt, ...)
{
    char b[TR_TXT]; va_list ap;
    if (!g_on) return;
    va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ev_add(kind, 1, b);
}

/* ---- the program now running: DOS's current PSP (swappable data area + 10h) and its memory block's name */
static uint16_t cur_psp(void) { return g_sda_lin ? DPMI_LoadW(g_sda_lin + 0x10) : 0; }
static void psp_name(uint16_t psp, char* out)
{
    uint32_t m = ((uint32_t)psp - 1) * 16; int i;
    uint8_t sig = DPMI_LoadB(m);
    if ((sig == 'M' || sig == 'Z') && DPMI_LoadW(m + 1) == psp)
    {
        for (i = 0; i < 8; i++) { char c = (char)DPMI_LoadB(m + 8 + i); if ((unsigned char)c < 33 || (unsigned char)c > 126) break; out[i] = c; }
        out[i] = 0;
        if (i) return;
    }
    snprintf(out, 9, "PSP%04X", psp);
}
static TrProg* prog(void)
{
    TrBank* b = bank(); uint16_t psp = cur_psp(); int i; TrProg* p; uint32_t t = ticks(); char nm[9];
    psp_name(psp, nm);                         /* DOS reuses the same PSP for the next program: the name tells them apart */
    for (i = b->nprog - 1; i >= 0; i--) if (b->prog[i].psp == psp && !strcmp(b->prog[i].name, nm)) { p = &b->prog[i]; p->t1 = t; return p; }
    if (b->nprog >= TR_NPROG) { p = &b->prog[TR_NPROG - 1]; p->t1 = t; return p; }   /* full: the last one collects */
    p = &b->prog[b->nprog++];
    memset(p, 0, sizeof *p);
    p->psp = psp; strcpy(p->name, nm); p->t0 = p->t1 = t; p->irq = -1;
    b->dirty = 1;
    return p;
}

/* ---- per-program counters */
void TRACE_Dsp(uint8_t cmd) { if (g_on) prog()->dsp[cmd]++; }
void TRACE_Mpu(int cmdport, uint8_t v)
{
    TrProg* p;
    if (!g_on) return;
    p = prog();
    if (cmdport) { p->mpucmd[v]++; return; }
    p->midibytes++;
    if (v >= 0xF8) p->realtime++;
    else if (v == 0xF0) p->sysex++;
    else if (v >= 0xF1) p->common++;
    else if (v >= 0x80) p->midi[(v >> 4) & 7]++;
}
void TRACE_MidiDropped(void) { if (g_on) { prog()->dropped++; ev_add(TR_MIDI, 1, "MIDI bytes dropped (synth queue full)"); } }
void TRACE_Underruns(uint32_t total)
{
    uint32_t d;
    if (!g_on) return;
    if (!g_under_init) { g_last_under = total; g_under_init = 1; return; }
    d = total - g_last_under; g_last_under = total;
    if (d && d < 1000000) { prog()->late += d; ev_add(TR_LATE, d, "late render: sound ran dry (buffer underrun)"); }
}
void TRACE_Replayed(uint32_t n) { if (g_on && n) { prog()->replayed += n; ev_add(TR_TIMER, n, "game timer ticks given back (VTIMER)"); } }
void TRACE_Irq(int irq)
{
    TrProg* p; char b[TR_TXT];
    if (!g_on) return;
    p = prog(); p->irq = irq; p->irq_raised++;
    if (!p->irq_warned && p->irq_raised >= 16 && !p->irq_acked)
    {
        p->irq_warned = 1;
        snprintf(b, sizeof b, "%.8s: SB IRQ %d never acknowledged", p->name, irq);
        ev_add(TR_IRQ, 1, b);
    }
}
void TRACE_IrqAck(void) { if (g_on) prog()->irq_acked++; }
void TRACE_DmaCheck(int dma, uint32_t addr, int32_t count)
{
    TrProg* p; char b[TR_TXT];
    if (!g_on || addr) return;
    p = prog();
    if (p->dma_warned) return;
    p->dma_warned = 1;
    snprintf(b, sizeof b, "%.8s: DMA %d not set up (count %ld)", p->name, dma, (long)count);
    ev_add(TR_DMA, 1, b);
}

/* ---- writing SBTRACE.TXT (INT 28h, DOS waiting at the prompt) */
static int dos21(DPMI_REG* r) { return DPMI_CallRealModeINT(0x21, r) == 0 && !(r->w.flags & 1); }
static char g_out[24 * 1024];
static int g_olen;
static void out(const char* fmt, ...)
{
    va_list ap; int n;
    if (g_olen >= (int)sizeof g_out - 1) return;
    va_start(ap, fmt); n = vsnprintf(g_out + g_olen, sizeof g_out - (size_t)g_olen, fmt, ap); va_end(ap);
    if (n > 0) g_olen += n; if (g_olen > (int)sizeof g_out - 1) g_olen = (int)sizeof g_out - 1;
}
static const char* hms(uint32_t t, char* b)
{
    uint32_t s = (uint32_t)((uint64_t)t * 65536ULL / 1193180ULL);
    sprintf(b, "%02lu:%02lu:%02lu", (unsigned long)(s / 3600 % 24), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
    return b;
}
int SBEMU_DSP_Known(uint8_t cmd);                 /* sbemu.c */
static int dsp_supported(int c) { return SBEMU_DSP_Known((uint8_t)c); }
static const char* dsp_name(int c)
{
    if (c == 0x80) return "silence";
    if (c == 0x20 || c == 0x24 || c == 0x2C || c == 0x98 || c == 0x99 || (c >= 0xB8 && c <= 0xBF) || (c >= 0xC8 && c <= 0xCF)) return "record";
    if ((c >= 0xB0 && c <= 0xB7) || (c >= 0xC0 && c <= 0xC7)) return "transfer variant";
    if (c == 0x30 || c == 0x31 || (c >= 0x34 && c <= 0x38)) return "MIDI via DSP";
    if (c == 0xF0 || c == 0xF1 || c == 0xF8 || c == 0xFB || c == 0xFC || c == 0xFD) return "test/status";
    if (c == 0xE7 || c == 0xE2) return "id";
    return "?";
}
static void fmt_prog(const TrProg* p)
{
    char a[12], b2[12]; int c, n, any;
    static const char* const mn[8] = { "note off", "note on", "aftertouch", "controller", "program", "chan.pressure", "pitch bend", "-" };
    out("Program %s (%s - %s):\r\n", p->name, hms(p->t0, a), hms(p->t1, b2));
    n = 0; for (c = 0; c < 256; c++) n += p->dsp[c] != 0;
    if (n)
    {
        out("  SB DSP commands:"); any = 0;
        for (c = 0; c < 256; c++) if (p->dsp[c] && dsp_supported(c)) { out(" %02X x%lu", c, (unsigned long)p->dsp[c]); any = 1; }
        if (!any) out(" (none supported)");
        out("\r\n"); any = 0;
        for (c = 0; c < 256; c++) if (p->dsp[c] && !dsp_supported(c))
        { if (!any) out("  NOT SUPPORTED by SBEMU:"); out(" %02X (%s) x%lu", c, dsp_name(c), (unsigned long)p->dsp[c]); any = 1; }
        if (any) out("\r\n");
        if (p->irq >= 0) out("  SB IRQ %d: %lu raised, %lu acknowledged\r\n", p->irq, (unsigned long)p->irq_raised, (unsigned long)p->irq_acked);
    }
    n = 0; for (c = 0; c < 256; c++) n += p->mpucmd[c] != 0;
    if (n)
    {
        out("  MPU-401 commands:"); any = 0;
        for (c = 0; c < 256; c++) if (p->mpucmd[c])
        {
            const char* w = c == 0xFF ? "reset" : c == 0x3F ? "UART" : (c >= 0xA5 && c <= 0xA9) || c == 0xB0 ? "SBEMU tools" : "NOT SUPPORTED (intelligent mode)";
            out(" %02X %s x%lu", c, w, (unsigned long)p->mpucmd[c]);
        }
        out("\r\n");
    }
    if (p->midibytes)
    {
        out("  MIDI: %lu bytes;", (unsigned long)p->midibytes);
        for (c = 0; c < 7; c++) if (p->midi[c]) out(" %s %lu,", mn[c], (unsigned long)p->midi[c]);
        out(" SysEx %lu, other %lu, real-time %lu; dropped %lu\r\n", (unsigned long)p->sysex, (unsigned long)p->common,
            (unsigned long)p->realtime, (unsigned long)p->dropped);
    }
    if (p->replayed || p->late) out("  Timer ticks given back %lu, late renders %lu\r\n", (unsigned long)p->replayed, (unsigned long)p->late);
}
static int write_file(void)
{
    DPMI_REG r; int h, off; uint32_t size;
    int n = (int)strlen(g_path);
    DPMI_LMemcpy(g_dosbuf_lin, DPMI_PTR2L(g_path), (uint32_t)n + 1);
    memset(&r, 0, sizeof r); r.w.ax = 0x3D01; r.w.ds = g_dosbuf_seg; r.w.dx = 0;
    if (dos21(&r))
    {
        h = r.w.ax;
        memset(&r, 0, sizeof r); r.w.ax = 0x4202; r.w.bx = (uint16_t)h;
        size = dos21(&r) ? ((uint32_t)r.w.dx << 16 | r.w.ax) : 0;
        if (size > (uint32_t)TR_FILEMAX) { memset(&r, 0, sizeof r); r.h.ah = 0x3E; r.w.bx = (uint16_t)h; dos21(&r); h = -1; }
    }
    else h = -1;
    if (h < 0)
    {
        DPMI_LMemcpy(g_dosbuf_lin, DPMI_PTR2L(g_path), (uint32_t)n + 1);
        memset(&r, 0, sizeof r); r.h.ah = 0x3C; r.w.cx = 0; r.w.ds = g_dosbuf_seg; r.w.dx = 0;
        if (!dos21(&r)) return 0;
        h = r.w.ax;
    }
    for (off = 0; off < g_olen; )
    {
        int k = g_olen - off > DOSBUF ? DOSBUF : g_olen - off;
        DPMI_LMemcpy(g_dosbuf_lin, DPMI_PTR2L(g_out + off), (uint32_t)k);
        memset(&r, 0, sizeof r); r.h.ah = 0x40; r.w.bx = (uint16_t)h; r.w.cx = (uint16_t)k; r.w.ds = g_dosbuf_seg; r.w.dx = 0;
        if (!dos21(&r) || r.w.ax != k) break;
        off += k;
    }
    memset(&r, 0, sizeof r); r.h.ah = 0x3E; r.w.bx = (uint16_t)h; dos21(&r);
    return 1;
}
static int at_prompt(void)
{
    uint16_t psp = cur_psp(); char nm[9];
    if (!psp) return 0;
    if (DPMI_LoadW((uint32_t)psp * 16 + 0x16) == psp) return 1;      /* the primary shell is its own parent */
    psp_name(psp, nm);
    return !strcmp(nm, "COMMAND") || !strcmp(nm, "4DOS") || !strcmp(nm, "NDOS");
}
static void flush(const char* where)
{
    TrBank* b; uint32_t f; int i, first; char a[12], b2[12];
    g_busy = 1;
    f = irq_off();                                       /* swap banks: interrupts go on into the empty one */
    b = bank(); g_cur ^= 1;
    memset(bank(), 0, sizeof(TrBank)); bank()->since = ticks();
    irq_on(f);
    g_olen = 0;
    out("==== SBEMU flight recorder: %s - %s (%s)\r\n", hms(b->since, a), hms(ticks(), b2), where);
    first = b->nev > TR_NEV ? b->nev - TR_NEV : 0;
    if (b->nev) out("Events (oldest first; xN = N times in a row):\r\n");
    if (b->lost) out("  (%d older events not kept)\r\n", b->lost);
    for (i = first; i < b->nev; i++)
    {
        static const char* const kn[] = { "USB ", "MIDI", "LATE", "TIMR", "DMA ", "IRQ ", "CD  ", "INFO" };
        const TrEv* e = &b->ev[i % TR_NEV];
        out("  %s %s  %s", hms(e->t0, a), kn[e->kind & 7], e->txt);
        if (e->n > 1) out(" x%lu (until %s)", (unsigned long)e->n, hms(e->t1, b2));
        out("\r\n");
    }
    for (i = 0; i < b->nprog; i++) fmt_prog(&b->prog[i]);
    out("\r\n");
    write_file();
    g_busy = 0;
}
void TRACE_Idle(void)
{
    if (!g_on || g_busy || !bank()->dirty) return;
    if (DPMI_LoadB(g_indos_lin) != 1 || DPMI_LoadB(g_indos_lin - 1) != 0 || !at_prompt()) return;
    flush("at the DOS prompt");
}
void TRACE_Flush(void)
{   /* a program asked (MPU command A9h: BUGREP, MENU): it is running, not inside DOS, so DOS file calls are allowed */
    if (!g_on || g_busy || !bank()->dirty) return;
    if (DPMI_LoadB(g_indos_lin) != 0 || DPMI_LoadB(g_indos_lin - 1) != 0) return;
    flush("asked for by a program");
}

int TRACE_Init(const char* path)
{
    DPMI_REG r; uint32_t seg;
    if (g_on) return 1;
    if (!path || strlen(path) >= sizeof g_path) return 0;
    strcpy(g_path, path);
    memset(&r, 0, sizeof r); r.h.ah = 0x34;
    if (DPMI_CallRealModeINT(0x21, &r) != 0) return 0;
    g_indos_lin = ((uint32_t)r.w.es << 4) + r.w.bx;
    memset(&r, 0, sizeof r); r.w.ax = 0x5D06;                       /* swappable data area: current PSP at +10h */
    if (DPMI_CallRealModeINT(0x21, &r) == 0 && !(r.w.flags & 1)) g_sda_lin = ((uint32_t)r.w.ds << 4) + r.w.si;
    seg = DPMI_DOSMalloc((DOSBUF + 15) / 16);
    if (!seg) return 0;
    g_dosbuf_seg = (uint16_t)(seg & 0xFFFF); g_dosbuf_lin = (uint32_t)g_dosbuf_seg << 4;
    memset(g_bank, 0, sizeof g_bank);
    g_bank[0].since = ticks();
    g_on = 1;
    return 1;
}
