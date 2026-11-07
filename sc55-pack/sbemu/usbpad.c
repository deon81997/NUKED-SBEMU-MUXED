/* USB Xbox 360 controller support for SBEMU.
 *
 * A small USB host for one device (plus the hubs in front of it), written for SBEMU's resident
 * setting:
 *   - no USB interrupt at all. Every controller's interrupt enables (and UHCI's PIRQ routing) are
 *     off; USBPAD_Poll() looks at the transfer descriptors / event ring in memory instead. It is
 *     called from the sound card interrupt and from every read of the emulated gameport (port 201h),
 *     so it never shares an IRQ line with the sound chip.
 *   - every PC USB controller kind:
 *       UHCI  Intel ICH / VIA (the Dell D810's ICH6)
 *       OHCI  NEC uPD720100/720101 PCI cards, SiS, ALi, nVidia, AMD, Apple ...
 *       EHCI  with companion controllers: taken from the BIOS, CONFIGFLAG cleared, so every port goes
 *             to its UHCI / OHCI companion (the pad is a full-speed device);
 *             without companions (Intel 5-series and later: a rate-matching hub on every port):
 *             driven itself, with split transactions to reach the pad behind the hub's TT
 *       xHCI  USB 3 controllers (Intel 7-series and later, NEC/Renesas uPD720200/201/202, ASMedia,
 *             VIA, Fresco, AMD): only their USB 2 ports are powered and used (as xhci98 does);
 *             on Intel 7/8/9-series the shared ports are switched over from EHCI (XUSB2PR)
 *   - USB hubs (any depth up to 4): the pad may be behind one, as it always is on hub-only EHCIs.
 *   - one controller carries the pad at a time. UHCI: frame list -> interrupt QH -> control QH.
 *     OHCI: its own HCCA (set once at reset: real chips such as NEC's read HcHCCA only when they start,
 *     so it is never changed while running), whose interrupt table points at the interrupt ED only
 *     while that controller has the pad, plus a control ED. EHCI: periodic frame list ->
 *     interrupt QH, async list = one control QH. xHCI: command ring, one event ring, a transfer ring
 *     per endpoint. Controllers without the pad stay running with empty schedules, so their root
 *     hubs keep reporting plug-ins.
 *   - enumeration is a small state machine (debounce, reset, recovery are waited by time stamp, so
 *     hot-plugging does not stall the sound interrupt for long); control transfers and hub port
 *     resets are synchronous and take a few milliseconds.
 *
 * Supported pads: anything with the Xbox 360 interface (vendor class FFh, subclass 5Dh, protocol 01h:
 * Microsoft wired pad and the third-party wired pads in x360c.inf), and the Microsoft wireless
 * receiver (protocol 81h, first pad slot only; untested). Not supported: Xbox One / Series pads,
 * CardBus cards (the BIOS leaves the slot unconfigured), Renesas uPD720201/202 cards without their
 * firmware ROM (they need a firmware upload).
 *
 * Report layout (wired, 20 bytes): 00 14, buttons lo (dpad U D L R, start, back, LS, RS),
 * buttons hi (LB, RB, guide, -, A, B, X, Y), LT, RT, LX, LY, RX, RY (int16 little endian, Y up = +).
 * The wireless receiver wraps the same layout 4 bytes further on. Layout from Linux drivers/input/
 * joystick/xpad.c. Controllers after the UHCI 1.1, OHCI 1.0a, EHCI 1.0 and xHCI 1.2 specs, Linux
 * pci-quirks.c (BIOS handoff, Intel port switch), USBDDOS (crazii, GPL-2.0) and xhci98's DOS
 * qualification tool (Yeo Kheng Meng, GPL-2.0: PORTSC safe-write mask, handoff, reset order). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pc.h>
#include <dpmi.h>
#include "usbpad.h"
#include "dpmi/dpmi.h"
#include "au_cards/au_base.h"

extern uint8_t  PCI_ReadByte(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
extern uint16_t PCI_ReadWord(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
extern uint32_t PCI_ReadDWord(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
extern void PCI_WriteByte(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint8_t value);
extern void PCI_WriteWord(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint16_t value);
extern void PCI_WriteDWord(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t value);

/* ------------------------------------------------------------------ UHCI */
#define U_CMD     0x00
#define U_STS     0x02
#define U_INTR    0x04
#define U_FRNUM   0x06
#define U_FLBASE  0x08
#define U_SOF     0x0C
#define U_PORT    0x10
#define CMD_RS    0x0001
#define CMD_HCRST 0x0002
#define CMD_GRST  0x0004
#define CMD_CF    0x0040
#define CMD_MAXP  0x0080
#define STS_HALT  0x0020
#define P_CCS     0x0001
#define P_CSC     0x0002
#define P_PE      0x0004
#define P_PEC     0x0008
#define P_LSDA    0x0100
#define P_PR      0x0200
#define UHCI_LEGSUP 0xC0
#define TD_ACTIVE (1UL << 23)
#define TD_STALL  (1UL << 22)
#define TD_ERRMASK 0x00760000UL         /* stall, buffer, babble, crc/timeout, bitstuff (not NAK) */
#define TD_LS     (1UL << 26)
#define TD_CERR3  (3UL << 27)
#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1
#define LP_T      1UL                   /* link: terminate */
#define LP_Q      2UL                   /* link: points to a QH (UHCI and EHCI alike) */
#define LP_VF     4UL                   /* UHCI depth first */

/* ------------------------------------------------------------------ OHCI */
#define O_CONTROL 0x04
#define O_CMDSTS  0x08
#define O_INTSTS  0x0C
#define O_INTDIS  0x14
#define O_HCCA    0x18
#define O_CTLHEAD 0x20
#define O_CTLCUR  0x24
#define O_BLKHEAD 0x28
#define O_BLKCUR  0x2C
#define O_FMINT   0x34
#define O_PERSTART 0x40
#define O_LSTHRESH 0x44
#define O_RHDA    0x48
#define O_RHSTS   0x50
#define O_PORT    0x54
#define OC_PLE    (1UL << 2)
#define OC_CLE    (1UL << 4)
#define OC_BLE    (1UL << 5)
#define OC_OPER   (2UL << 6)
#define OC_IR     (1UL << 8)
#define OS_HCR    (1UL << 0)
#define OS_CLF    (1UL << 1)
#define OS_OCR    (1UL << 3)
#define OP_CCS    (1UL << 0)
#define OP_PES    (1UL << 1)
#define OP_PRS    (1UL << 4)
#define OP_PPS    (1UL << 8)
#define OP_LSDA   (1UL << 9)
#define OP_CHG    0x001F0000UL
#define OP_PRSC   (1UL << 20)
#define ED_SKIP   (1UL << 14)
#define ED_LS     (1UL << 13)
#define ED_HALT   1UL
#define OTD_CC_NA (0xFUL << 28)
#define OTD_DI7   (7UL << 21)
#define OTD_R     (1UL << 18)
#define OTD_SETUP (0UL << 19)
#define OTD_OUT   (1UL << 19)
#define OTD_IN    (2UL << 19)
#define OTD_T0    (2UL << 24)
#define OTD_T1    (3UL << 24)
#define OTD_CC(x) ((x) >> 28)

/* ------------------------------------------------------------------ EHCI (operational registers) */
#define E_CMD     0x00
#define E_STS     0x04
#define E_INTR    0x08
#define E_FRINDEX 0x0C
#define E_SEG     0x10
#define E_PERIOD  0x14
#define E_ASYNC   0x18
#define E_CONFIG  0x40
#define E_PORT    0x44
#define EC_RS     (1UL << 0)
#define EC_HCRST  (1UL << 1)
#define EC_PSE    (1UL << 4)
#define EC_ASE    (1UL << 5)
#define ES_HALT   (1UL << 12)
#define ES_PSS    (1UL << 14)
#define ES_ASS    (1UL << 15)
#define EP_CCS    (1UL << 0)
#define EP_PE     (1UL << 2)
#define EP_W1C    0x2AUL                /* CSC, PEC, OCC */
#define EP_PR     (1UL << 8)
#define EP_LS_K   (1UL << 10)           /* line status K: low speed device */
#define EP_PP     (1UL << 12)
#define EP_PO     (1UL << 13)
#define QT_ACTIVE (1UL << 7)
#define QT_HALT   (1UL << 6)
#define QT_ERRS   0x3CUL                /* buffer error, babble, transaction error, missed uframe */

/* ------------------------------------------------------------------ xHCI */
#define X_USBCMD  0x00
#define X_USBSTS  0x04
#define X_DNCTRL  0x14
#define X_CRCR    0x18
#define X_DCBAAP  0x30
#define X_CONFIG  0x38
#define X_PORTSC  0x400
#define XC_RUN    (1UL << 0)
#define XC_HCRST  (1UL << 1)
#define XS_HCH    (1UL << 0)
#define XS_CNR    (1UL << 11)
#define PSC_CCS   (1UL << 0)
#define PSC_PED   (1UL << 1)
#define PSC_PR    (1UL << 4)
#define PSC_PP    (1UL << 9)
#define PSC_SPEED(v) (((v) >> 10) & 0xF)
#define PSC_LWS   (1UL << 16)
#define PSC_CSC   (1UL << 17)
#define PSC_PEC   (1UL << 18)
#define PSC_PRC   (1UL << 21)
#define PSC_CHG   0x00FE0000UL          /* CSC PEC WRC OCC PRC PLC CEC */
#define PSC_WPR   (1UL << 31)
#define PSC_RSVDZ 0x30000004UL
#define TRB_C     1UL
#define TRB_TYPE(t) ((uint32_t)(t) << 10)
#define TRB_GTYPE(c) (((c) >> 10) & 0x3F)
enum { TT_NORMAL = 1, TT_SETUP = 2, TT_DATA = 3, TT_STATUS = 4, TT_LINK = 6, TT_ENSLOT = 9, TT_DISSLOT = 10,
       TT_ADDR = 11, TT_CONFEP = 12, TT_EVAL = 13, TT_RESETEP = 14, TT_SETDEQ = 16,
       TT_EV_XFER = 32, TT_EV_CMD = 33 };
#define XCMD_TRBS 32
#define XEVT_TRBS 128
#define XRING_TRBS 32

#define MAX_HC    16
#define MAX_PORT  32
#define MAX_DEV   8
#define MAX_DEPTH 4
#define NTD       40

enum { T_UHCI, T_OHCI, T_EHCI, T_XHCI };
enum { SP_FS, SP_LS, SP_HS };

typedef struct { volatile uint32_t* t; uint32_t ph; int n, enq, cyc; } Ring;
typedef struct
{
    xmsmem_t mem, sp;                   /* rings / contexts; scratchpad pages */
    uint32_t base, op, rt, db;          /* mapped registers (linear) */
    int ctxsz, maxslots, nsp;
    uint32_t usb2;                      /* bit per port: USB 2 protocol port */
    volatile uint32_t *dcbaa, *erst, *inctx, *spa;
    uint32_t pbase;
    Ring cmd, ev;
    int evcyc;
} XHC;

typedef struct { int type, idx, nports, ncc, given; uint16_t io; uint32_t mmio, op; uint8_t bus, dev, func; XHC* x; } HCI;
static HCI      HC[MAX_HC];
static int      HCn, UhciN, OhciN, EhciN, XhciN, EhciGiven;

typedef struct
{
    int used, addr, speed, mps0, parent, pport, rootport, depth, hub, nports, slot, tt, ttport;
    uint32_t route, ignore;             /* hub: bit per port: device there is not a pad */
    uint32_t child;                     /* hub: bit per port with an enumerated device */
} Dev;
static Dev       D[MAX_DEV];
static int       NextAddr, PadD = -1;

static xmsmem_t Mem;
static volatile uint32_t* FL;           /* UHCI / EHCI frame list, 1024 entries */
static volatile uint32_t* HCCAS;        /* OHCI communication areas, 256 bytes per controller (by HC index) */
#define HCCA_OF(i) (HCCAS + (i) * 64)
static volatile uint32_t* QHI;          /* UHCI interrupt QH */
static volatile uint32_t* QHC;          /* UHCI control QH */
static volatile uint32_t* EDI;          /* OHCI interrupt ED */
static volatile uint32_t* EDC;          /* OHCI control ED */
static volatile uint32_t* TDI;          /* the pad's IN TD(s) */
static volatile uint32_t* QHA;          /* EHCI async (control) QH */
static volatile uint32_t* QHP;          /* EHCI periodic (interrupt) QH */
static volatile uint32_t* TD;           /* NTD control TDs, 8 dwords each */
static uint8_t*  CBuf;                  /* control data, 512 bytes */
static uint8_t*  IBuf;                  /* IN report, 64 */
static uint8_t*  OBuf;                  /* OUT report, 64 */
static uint32_t  PBase;
#define PHYS(p) (PBase + (uint32_t)((volatile uint8_t*)(p) - (volatile uint8_t*)Mem.linearptr))

static uint64_t  Cps;
static USBPAD_State St;
static char      Desc[112];

/* state machine */
enum { S_SCAN, S_DEBOUNCE, S_RESET, S_RECOVER, S_HUB, S_RUN };
static int       S, Cur = -1, CurPort, LastHubKey = -1;
static uint64_t  Until, NextScan, HubSince;
static uint32_t  Ignore[MAX_HC];        /* root port bits: not a pad, skip until unplugged */
static uint32_t  HubRoot[MAX_HC];       /* root port bits: a hub is there */
static uint8_t   Fails[MAX_HC][MAX_PORT];
static int       InEp, OutEp, InMps = 32, InIntv = 4, InTog, OutTog, Wireless, WlPresent;
static int       OCur;                  /* OHCI / EHCI: which of the two IN TDs is the empty tail */
static Ring      XIn, XOut;             /* xHCI rings of the pad's endpoints */
static Ring      XEp0[MAX_DEV];
static volatile int XCmdDone, XCtlDone, XInDone, XOutDone;
static uint32_t  XCmdPh; static int XCmdCC, XCmdSlot, XCtlCC, XInCC, XInRes, XOutCC;
static volatile int Busy;
static int       Inited, Light;
static uint64_t  RunSince; static int Watched;

/* short trace of what happened (start-up report / SBDETAIL), oldest first, stops when full */
static char      Trace[4096]; static int TraceN, TraceOff;
void USBPAD_TraceStop(void) { TraceOff = 1; }   /* SBEMU: no libc formatting inside its interrupt */
static void trace(const char* f, ...)
{
    va_list a;
    if (TraceOff || TraceN >= (int)sizeof(Trace) - 2) return;
    va_start(a, f);
    TraceN += vsnprintf(Trace + TraceN, sizeof(Trace) - TraceN, f, a);
    va_end(a);
    if (TraceN > (int)sizeof(Trace) - 1) TraceN = sizeof(Trace) - 1;
}
const char* USBPAD_Trace(void) { return Trace; }

static inline uint64_t tsc(void) { uint32_t lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static uint64_t us2c(uint32_t us) { return Cps * us / 1000000ULL; }
static void wait_us(uint32_t us) { uint64_t e = tsc() + us2c(us); while (tsc() < e) {} }

/* CPU cache vs. the USB controller's DMA. The controller should see memory coherently (it snoops),
 * but on the D810 (ICH6 UHCI) it acted on descriptors the CPU had written and the CPU kept seeing its own
 * old copy. So descriptors are flushed out of the cache (CLFLUSH: Pentium 4 / Pentium M and later) before
 * the controller is pointed at them, and flushed again before the CPU reads what the controller wrote. */
static int ClLine;                      /* CLFLUSH line size, 0 = no CLFLUSH */
static void cache_init(void)
{
    uint32_t a = 1, b, c, d;
    __asm__ __volatile__("pushfl; popl %%eax; movl %%eax, %%ecx; xorl $0x200000, %%eax; pushl %%eax; popfl;"
                         "pushfl; popl %%eax; xorl %%ecx, %%eax" : "=a"(a) : : "ecx");
    if (!(a & 0x200000)) { ClLine = 0; return; }                 /* no CPUID */
    __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    ClLine = (d & (1UL << 19)) ? (int)(((b >> 8) & 0xFF) * 8) : 0;
    if (ClLine && ClLine < 32) ClLine = 32;
}
static void fl(const volatile void* p, int bytes)
{
    const volatile uint8_t* q;
    const volatile uint8_t* e = (const volatile uint8_t*)p + bytes;
    if (!ClLine) return;
    __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory");
    for (q = (const volatile uint8_t*)((uintptr_t)p & ~(uintptr_t)(ClLine - 1)); q < e; q += ClLine)
        __asm__ __volatile__("clflush %0" : : "m"(*(const volatile char*)q) : "memory");
    __asm__ __volatile__("lock; orl $0, (%%esp)" ::: "memory");
}

static uint32_t rd(uint32_t a) { return DPMI_LoadD(a); }
static void wr(uint32_t a, uint32_t v) { DPMI_StoreD(a, v); }
static uint32_t ord(int h, int r) { return rd(HC[h].mmio + r); }
static void owr(int h, int r, uint32_t v) { wr(HC[h].mmio + r, v); }
static uint32_t erd(int h, int r) { return rd(HC[h].op + r); }
static void ewr(int h, int r, uint32_t v) { wr(HC[h].op + r, v); }
static const char* tname(int h) { static const char* n[] = { "UHCI", "OHCI", "EHCI", "xHCI" }; return n[HC[h].type]; }
static void zero(volatile uint32_t* p, int dw) { while (dw--) *p++ = 0; }

/* ================================================================== root hub ports */
static uint16_t prt(int hc, int p) { return (uint16_t)(HC[hc].io + U_PORT + p * 2); }
static void port_set(int hc, int p, uint16_t set, uint16_t clr)  /* UHCI */
{
    uint16_t v = inportw(prt(hc, p));
    v &= (uint16_t)~(P_CSC | P_PEC | clr);
    v &= 0x1FF5 & (uint16_t)~0x0080;
    outportw(prt(hc, p), (uint16_t)(v | set));
}
static uint32_t xpsc(int h, int p) { return rd(HC[h].x->op + X_PORTSC + p * 0x10); }
static void xpsc_set(int h, int p, uint32_t set)                  /* xhci98's safe-write rule */
{
    uint32_t v = xpsc(h, p);
    v &= ~(PSC_PED | PSC_PR | PSC_WPR | PSC_LWS | PSC_CHG | PSC_RSVDZ);
    wr(HC[h].x->op + X_PORTSC + p * 0x10, v | set);
}
static void eport_set(int h, int p, uint32_t set, uint32_t clr)
{
    uint32_t v = erd(h, E_PORT + p * 4);
    v &= ~(EP_W1C | clr);
    ewr(h, E_PORT + p * 4, v | set);
}

enum { PS_CCS = 1, PS_PE = 2, PS_LS = 4, PS_HS = 8, PS_GONE = 16 };
static int pstat(int h, int p)
{
    switch (HC[h].type)
    {
    case T_UHCI:
    {
        uint16_t v = inportw(prt(h, p));
        if (v == 0xFFFF) return PS_GONE;
        return ((v & P_CCS) ? PS_CCS : 0) | ((v & P_PE) ? PS_PE : 0) | ((v & P_LSDA) ? PS_LS : 0);
    }
    case T_OHCI:
    {
        uint32_t v = ord(h, O_PORT + p * 4);
        if (v == 0xFFFFFFFFUL) return PS_GONE;
        return ((v & OP_CCS) ? PS_CCS : 0) | ((v & OP_PES) ? PS_PE : 0) | ((v & OP_LSDA) ? PS_LS : 0);
    }
    case T_EHCI:
    {
        uint32_t v = erd(h, E_PORT + p * 4);
        if (v == 0xFFFFFFFFUL || (v & EP_PO)) return PS_GONE;
        return ((v & EP_CCS) ? PS_CCS : 0) | ((v & EP_PE) ? PS_PE | PS_HS : 0) | (((v >> 10) & 3) == 1 ? PS_LS : 0);
    }
    default:
    {
        uint32_t v;
        if (!(HC[h].x->usb2 & (1UL << p))) return PS_GONE;  /* USB 3 ports are left alone */
        v = xpsc(h, p);
        if (v == 0xFFFFFFFFUL) return PS_GONE;
        return ((v & PSC_CCS) ? PS_CCS : 0) | ((v & PSC_PED) ? PS_PE : 0) |
            (PSC_SPEED(v) == 2 ? PS_LS : 0) | (PSC_SPEED(v) == 3 ? PS_HS : 0);
    }
    }
}
static void p_clear_change(int h, int p)
{
    switch (HC[h].type)
    {
    case T_UHCI: port_set(h, p, P_CSC | P_PEC, 0); break;
    case T_OHCI: owr(h, O_PORT + p * 4, OP_CHG); break;
    case T_EHCI: ewr(h, E_PORT + p * 4, erd(h, E_PORT + p * 4) | EP_W1C); break;   /* PE is read/write: keep it */
    default:     xpsc_set(h, p, PSC_CHG); break;
    }
}
static void p_reset_start(int h, int p)
{
    switch (HC[h].type)
    {
    case T_UHCI: port_set(h, p, P_PR, P_PE); break;
    case T_OHCI: owr(h, O_PORT + p * 4, OP_PRS); break;
    case T_EHCI: eport_set(h, p, EP_PR, EP_PE); break;
    default:     xpsc_set(h, p, PSC_PR); break;
    }
}
static int p_reset_end(int h, int p)                  /* 1 = port enabled */
{
    int n;
    switch (HC[h].type)
    {
    case T_UHCI:
        port_set(h, p, 0, P_PR);
        wait_us(100);
        for (n = 0; n < 20 && !(inportw(prt(h, p)) & P_PE); ++n) { port_set(h, p, P_PE, 0); wait_us(500); }
        port_set(h, p, P_CSC | P_PEC, 0);
        break;
    case T_OHCI:
    {
        /* the root hub's own reset is only ~10 ms; USB 2.0 asks 50 ms for a root port (Linux repeats it
         * too): some devices, the Xbox 360 pad among them it seems, stay silent after a short one */
        int k;
        for (k = 0; k < 5; ++k)
        {
            for (n = 0; n < 30 && (ord(h, O_PORT + p * 4) & OP_PRS); ++n) wait_us(1000);
            owr(h, O_PORT + p * 4, OP_PRSC);
            if (k < 4) owr(h, O_PORT + p * 4, OP_PRS);
        }
        owr(h, O_PORT + p * 4, OP_CHG);
        for (n = 0; n < 20 && !(ord(h, O_PORT + p * 4) & OP_PES); ++n) { owr(h, O_PORT + p * 4, OP_PES); wait_us(500); }
        break;
    }
    case T_EHCI:                                      /* enabled after reset = high speed; else full/low speed, */
        eport_set(h, p, 0, EP_PR | EP_PE);            /* which a companion-less EHCI cannot serve */
        for (n = 0; n < 20 && (erd(h, E_PORT + p * 4) & EP_PR); ++n) wait_us(500);
        wait_us(2000);
        p_clear_change(h, p);
        break;
    default:
        for (n = 0; n < 100 && !(xpsc(h, p) & PSC_PRC); ++n) wait_us(1000);
        xpsc_set(h, p, PSC_PRC | PSC_CSC | PSC_PEC);
        break;
    }
    return (pstat(h, p) & PS_PE) != 0;
}
static void p_disable(int h, int p)
{
    switch (HC[h].type)
    {
    case T_UHCI: port_set(h, p, 0, P_PE); break;
    case T_OHCI: owr(h, O_PORT + p * 4, OP_CCS); break;
    case T_EHCI: eport_set(h, p, 0, EP_PE); break;
    default:     xpsc_set(h, p, PSC_PED); break;   /* PED is write-1-to-disable */
    }
}

/* ================================================================== PCI */
static void pci_scan(void (*fn)(uint8_t, uint8_t, uint8_t, uint32_t))
{
    int bus, dev, func;
    for (bus = 0; bus < 64; ++bus)
        for (dev = 0; dev < 32; ++dev)
        {
            int nf = 1;
            for (func = 0; func < nf; ++func)
            {
                uint32_t id = PCI_ReadDWord((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 0);
                if ((id & 0xFFFF) == 0xFFFF || (id & 0xFFFF) == 0) { if (func == 0) break; continue; }
                if (func == 0 && (PCI_ReadByte((uint8_t)bus, (uint8_t)dev, 0, 0x0E) & 0x80)) nf = 8;
                fn((uint8_t)bus, (uint8_t)dev, (uint8_t)func, PCI_ReadDWord((uint8_t)bus, (uint8_t)dev, (uint8_t)func, 8) >> 8);
            }
        }
}

/* map MMIO at any physical address: DPMI maps whole pages, so map the page(s) around it and
 * return the linear address of the byte asked for (EHCI BARs are only 256- or 1K-aligned) */
static uint32_t map_mmio(uint32_t phys, uint32_t size)
{
    uint32_t off = phys & 0xFFF, lin;
    lin = DPMI_MapMemory(phys - off, (size + off + 0xFFF) & ~0xFFFUL);
    return lin ? lin + off : 0;
}

/* a memory BAR below 4 GB, or 0 */
static uint32_t mem_bar(uint8_t b, uint8_t d, uint8_t f, uint8_t reg)
{
    uint32_t bar = PCI_ReadDWord(b, d, f, reg);
    if (bar & 1) return 0;
    if (((bar >> 1) & 3) == 2 && PCI_ReadDWord(b, d, f, (uint8_t)(reg + 4)) != 0) return 0; /* above 4 GB */
    PCI_WriteWord(b, d, f, 0x04, (uint16_t)(PCI_ReadWord(b, d, f, 0x04) | 0x0406)); /* memory, bus master, INTx off */
    return bar & ~0xFUL;
}

static void hc_found(uint8_t b, uint8_t d, uint8_t f, uint32_t cls)
{
    HCI* h;
    uint32_t bar;
    if (HCn >= MAX_HC) return;
    h = &HC[HCn];
    memset(h, 0, sizeof(*h));
    h->bus = b; h->dev = d; h->func = f;
    if (cls == 0x0C0300)
    {
        bar = PCI_ReadDWord(b, d, f, 0x20);
        if (!(bar & 1) || !(bar & 0xFFE0)) return;
        h->type = T_UHCI; h->io = (uint16_t)(bar & 0xFFE0); h->nports = 2;
        bar &= 0xFFE0;
    }
    else if (cls == 0x0C0310 || cls == 0x0C0320 || cls == 0x0C0330)
    {
        bar = mem_bar(b, d, f, 0x10);
        if (!bar) return;
        h->mmio = map_mmio(bar, cls == 0x0C0330 ? 0x10000 : 0x1000);
        if (!h->mmio) { trace("map %08lx failed ", (unsigned long)bar); return; }
        h->type = cls == 0x0C0310 ? T_OHCI : cls == 0x0C0320 ? T_EHCI : T_XHCI;
        if (h->type == T_XHCI)
        {
            uint32_t need = rd(h->mmio + 0x14) & ~3UL;                /* DBOFF, RTSOFF */
            if ((rd(h->mmio + 0x18) & ~0x1FUL) > need) need = rd(h->mmio + 0x18) & ~0x1FUL;
            if (need + 0x1000 > 0x10000)
            {
                h->mmio = map_mmio(bar, (need + 0x2000 + 0xFFF) & ~0xFFFUL);   /* the small mapping stays: harmless */
                if (!h->mmio) return;
            }
        }
    }
    else return;
    trace("%02x:%02x.%x %04x:%04x %s %s%08lx\n", b, d, f, PCI_ReadWord(b, d, f, 0), PCI_ReadWord(b, d, f, 2),
        cls == 0x0C0300 ? "UHCI" : cls == 0x0C0310 ? "OHCI" : cls == 0x0C0320 ? "EHCI" : "xHCI",
        cls == 0x0C0300 ? "io " : "mem ", (unsigned long)bar);
    ++HCn;
}

/* ================================================================== controller bring-up */
static void ehci_handoff(int i)
{
    HCI* h = &HC[i];
    int eecp = (rd(h->mmio + 8) >> 8) & 0xFF, guard = 0, n;
    while (eecp >= 0x40 && guard++ < 16)
    {
        uint32_t cap = PCI_ReadDWord(h->bus, h->dev, h->func, (uint8_t)eecp);
        if ((cap & 0xFF) == 1)
        {
            PCI_WriteByte(h->bus, h->dev, h->func, (uint8_t)(eecp + 3), 1);
            for (n = 0; n < 1000 && (PCI_ReadByte(h->bus, h->dev, h->func, (uint8_t)(eecp + 2)) & 1); ++n) wait_us(1000);
            if (PCI_ReadByte(h->bus, h->dev, h->func, (uint8_t)(eecp + 2)) & 1)
                PCI_WriteByte(h->bus, h->dev, h->func, (uint8_t)(eecp + 2), 0); /* BIOS did not let go */
            PCI_WriteDWord(h->bus, h->dev, h->func, (uint8_t)(eecp + 4), 0xE0000000UL); /* SMIs off */
        }
        eecp = (cap >> 8) & 0xFF;
    }
}

/* EHCI: stop it; with companions hand every port to them, without drive it ourselves */
static int ehci_setup(int i)
{
    HCI* h = &HC[i];
    uint32_t hcs;
    int n, p;
    ehci_handoff(i);
    h->op = h->mmio + (rd(h->mmio) & 0xFF);
    hcs = rd(h->mmio + 4);
    h->nports = (int)(hcs & 0xF);
    h->ncc = (int)((hcs >> 12) & 0xF);
    ewr(i, E_CMD, erd(i, E_CMD) & ~(EC_RS | EC_PSE | EC_ASE));
    for (n = 0; n < 50 && !(erd(i, E_STS) & ES_HALT); ++n) wait_us(1000);
    ewr(i, E_INTR, 0);
    trace("EHCI %02x:%02x.%x: %d ports, %d companions, cap %02lx -> %s\n", h->bus, h->dev, h->func, h->nports, h->ncc,
        (unsigned long)(rd(h->mmio) & 0xFF), h->ncc ? "ports to companions" : "driven directly");
    if (h->ncc)
    {
        /* all ports to the companions. CONFIGFLAG=0 alone does it on paper, but give every port away
         * explicitly too (Port Owner, which is only writable while CONFIGFLAG=1) and power it: an empty port
         * can otherwise stay with the halted EHCI, so a pad plugged in later is never seen, and on a card
         * no BIOS has set up the port power can still be off */
        ewr(i, E_CONFIG, 1);
        wait_us(1000);
        for (p = 0; p < h->nports; ++p) eport_set(i, p, EP_PO | ((hcs & 0x10) ? EP_PP : 0), EP_PE);
        wait_us(5000);
        ewr(i, E_CONFIG, 0);
        wait_us(20000);
        h->given = h->nports;                         /* watched in pick_root(), see ehci_reclaim() */
        ++EhciGiven;
        return 0;
    }
    ewr(i, E_CMD, EC_HCRST);
    for (n = 0; n < 100 && (erd(i, E_CMD) & EC_HCRST); ++n) wait_us(1000);
    ewr(i, E_INTR, 0);
    if (rd(h->mmio + 8) & 1) ewr(i, E_SEG, 0);         /* 64-bit capable: our structures are below 4 GB */
    ewr(i, E_PERIOD, PHYS(FL));
    ewr(i, E_ASYNC, PHYS(QHA));
    ewr(i, E_FRINDEX, 0);
    ewr(i, E_CMD, (8UL << 16) | EC_RS);               /* run, schedules off until it has the pad */
    ewr(i, E_CONFIG, 1);                              /* ports to this controller */
    if (hcs & 0x10)                                   /* port power control */
        for (p = 0; p < h->nports; ++p) eport_set(i, p, EP_PP, 0);
    wait_us(20000);
    return 1;
}

static void uhci_reset(int i)
{
    HCI* h = &HC[i];
    int n;
    PCI_WriteWord(h->bus, h->dev, h->func, UHCI_LEGSUP, 0x8F00);  /* legacy off, SMIs off, PIRQ off */
    PCI_WriteWord(h->bus, h->dev, h->func, 0x04, (uint16_t)(PCI_ReadWord(h->bus, h->dev, h->func, 0x04) | 0x05));
    outportw(h->io + U_INTR, 0);
    outportw(h->io + U_CMD, 0);
    for (n = 0; n < 20 && !(inportw(h->io + U_STS) & STS_HALT); ++n) wait_us(1000);
    outportw(h->io + U_CMD, CMD_GRST); wait_us(20000); outportw(h->io + U_CMD, 0); wait_us(1000);
    outportw(h->io + U_CMD, CMD_HCRST);
    for (n = 0; n < 100 && (inportw(h->io + U_CMD) & CMD_HCRST); ++n) wait_us(100);
    outportw(h->io + U_INTR, 0);
    outportw(h->io + U_STS, 0x3F);
}

static void ohci_reset(int i)
{
    uint32_t fi, rha, w;
    int n, p;
    if (ord(i, O_CONTROL) & OC_IR)                            /* the BIOS's SMM code owns it */
    {
        owr(i, O_CMDSTS, OS_OCR);
        for (n = 0; n < 1000 && (ord(i, O_CONTROL) & OC_IR); ++n) wait_us(1000);
    }
    owr(i, O_INTDIS, 0xC000007FUL);
    fi = ord(i, O_FMINT) & 0x3FFF;
    if (fi < 11000 || fi > 13000) fi = 11999;
    owr(i, O_CONTROL, 0);
    wait_us(50000);
    owr(i, O_CMDSTS, OS_HCR);
    for (n = 0; n < 100 && (ord(i, O_CMDSTS) & OS_HCR); ++n) wait_us(10);
    owr(i, O_FMINT, ((ord(i, O_FMINT) & 0x80000000UL) ^ 0x80000000UL) | (((fi - 210) * 6 / 7) << 16) | fi);
    owr(i, O_PERSTART, fi * 9 / 10);
    owr(i, O_LSTHRESH, 0x628);
    zero(HCCA_OF(i), 64);
    owr(i, O_HCCA, PHYS(HCCA_OF(i)));                         /* for good: never changed while running */
    owr(i, O_CTLHEAD, 0); owr(i, O_CTLCUR, 0);
    owr(i, O_BLKHEAD, 0); owr(i, O_BLKCUR, 0);
    owr(i, O_INTSTS, 0xFFFFFFFFUL);
    owr(i, O_CONTROL, OC_OPER | 3);
    rha = ord(i, O_RHDA);
    HC[i].nports = (int)(rha & 0xFF);
    if (HC[i].nports > MAX_PORT) HC[i].nports = MAX_PORT;
    owr(i, O_RHSTS, 1UL << 16);
    for (p = 0; p < HC[i].nports; ++p) owr(i, O_PORT + p * 4, OP_PPS);
    trace("OHCI %02x:%02x.%x: rev %02lx, %d ports, rhda %08lx\n", HC[i].bus, HC[i].dev, HC[i].func,
        (unsigned long)(ord(i, 0) & 0xFF), HC[i].nports, (unsigned long)rha);
    w = (rha >> 24) * 2;
    if (w < 20) w = 20;
    if (w > 100) w = 100;
    wait_us(w * 1000);
}

/* ---------------- xHCI rings */
static void ring_init(Ring* r, volatile uint32_t* t, uint32_t ph, int n)
{
    r->t = t; r->ph = ph; r->n = n; r->enq = 0; r->cyc = 1;
    zero(t, n * 4);
    t[(n - 1) * 4 + 0] = ph;
    t[(n - 1) * 4 + 3] = TRB_TYPE(TT_LINK) | 2;               /* toggle cycle */
}
static uint32_t ring_push(Ring* r, uint32_t p0, uint32_t p1, uint32_t st, uint32_t ctl)
{
    volatile uint32_t* t = r->t + r->enq * 4;
    uint32_t ph = r->ph + r->enq * 16;
    t[0] = p0; t[1] = p1; t[2] = st;
    t[3] = ctl | (r->cyc ? TRB_C : 0);
    fl(t, 16);
    if (++r->enq == r->n - 1)
    {
        volatile uint32_t* l = r->t + (r->n - 1) * 4;
        l[3] = TRB_TYPE(TT_LINK) | 2 | (r->cyc ? TRB_C : 0);
        fl(l, 16);
        r->cyc ^= 1;
        r->enq = 0;
    }
    return ph;
}
static uint32_t ring_deq(const Ring* r) { return (r->ph + r->enq * 16) | (r->cyc ? 1 : 0); }

/* drain the event ring of controller h */
static void x_events(int h)
{
    XHC* x = HC[h].x;
    int guard = 0;
    for (;;)
    {
        volatile uint32_t* e = x->ev.t + x->ev.enq * 4;
        uint32_t ctl;
        fl(e, 16);
        ctl = e[3];
        int type, cc, slot, ep;
        if ((ctl & 1) != (uint32_t)x->evcyc || guard++ > XEVT_TRBS) break;
        type = (int)TRB_GTYPE(ctl); cc = (int)(e[2] >> 24); slot = (int)(ctl >> 24); ep = (int)((ctl >> 16) & 0x1F);
        if (type == TT_EV_CMD && e[0] == XCmdPh) { XCmdCC = cc; XCmdSlot = slot; XCmdDone = 1; }
        else if (type == TT_EV_XFER)
        {
            if (ep == 1) { XCtlCC = cc; XCtlDone = 1; }
            else if (PadD >= 0 && slot == D[PadD].slot && ep == InEp * 2 + 1) { XInCC = cc; XInRes = (int)(e[2] & 0xFFFFFF); XInDone = 1; }
            else if (PadD >= 0 && slot == D[PadD].slot && ep == OutEp * 2) { XOutCC = cc; XOutDone = 1; }
        }
        if (++x->ev.enq == XEVT_TRBS) { x->ev.enq = 0; x->evcyc ^= 1; }
        wr(x->rt + 0x38, x->ev.ph + x->ev.enq * 16);              /* ERDP (keeps EHB) */
        wr(x->rt + 0x3C, 0);
    }
    wr(x->rt + 0x38, (x->ev.ph + x->ev.enq * 16) | 8);            /* ring empty: release EHB */
    wr(x->rt + 0x3C, 0);
}

/* run one command; returns its completion code (1 = success), -1 timeout */
static int x_cmd(int h, uint32_t p0, uint32_t ctl, int* slot)
{
    XHC* x = HC[h].x;
    uint64_t end = tsc() + us2c(500000);
    XCmdDone = 0;
    fl(x->inctx, 33 * 64);                                      /* input context, if the command uses one */
    XCmdPh = ring_push(&x->cmd, p0, 0, 0, ctl);
    wr(x->db, 0);
    while (!XCmdDone && tsc() < end) x_events(h);
    if (!XCmdDone) return -1;
    if (slot) *slot = XCmdSlot;
    return XCmdCC;
}

static volatile uint32_t* x_ctx(volatile uint32_t* base, int i) { return base + i * (HC[Cur].x->ctxsz / 4); }
static volatile uint32_t* x_out(int di)                         /* output device context of device di */
{
    XHC* x = HC[Cur].x;
    return (volatile uint32_t*)((uint8_t*)x->mem.linearptr + 0x4000 + di * 2048);
}
static uint32_t x_outph(int di) { return HC[Cur].x->pbase + 0x4000 + di * 2048; }

static int xhci_setup(int i)
{
    HCI* h = &HC[i];
    XHC* x;
    uint32_t hcs1, hcs2, hcc, v;
    int n, p, eo, guard = 0;
    uint8_t* m;
    if (!(h->x = x = (XHC*)calloc(1, sizeof(XHC)))) return 0;
    x->base = h->mmio;
    x->op = x->base + (rd(x->base) & 0xFF);
    x->rt = x->base + (rd(x->base + 0x18) & ~0x1FUL);
    x->db = x->base + (rd(x->base + 0x14) & ~3UL);
    hcs1 = rd(x->base + 4); hcs2 = rd(x->base + 8); hcc = rd(x->base + 0x10);
    h->nports = (int)(hcs1 >> 24);
    if (h->nports > MAX_PORT) h->nports = MAX_PORT;
    x->maxslots = (int)(hcs1 & 0xFF);
    if (x->maxslots > 32) x->maxslots = 32;
    x->ctxsz = (hcc & 4) ? 64 : 32;
    x->nsp = (int)(((hcs2 >> 21) & 0x1F) << 5 | ((hcs2 >> 27) & 0x1F));
    /* extended capabilities: BIOS handoff, which ports are USB 2 */
    eo = (int)((hcc >> 16) & 0xFFFF) * 4;
    while (eo && guard++ < 64)
    {
        uint32_t a = x->base + eo, c = rd(a);
        if ((c & 0xFF) == 1)                                      /* USB legacy support */
        {
            wr(a, c | (1UL << 24));
            for (n = 0; n < 1000 && (rd(a) & (1UL << 16)); ++n) wait_us(1000);
            v = rd(a + 4);
            wr(a + 4, (v & 0x000E1FEEUL) | 0xE0000000UL);         /* SMI enables off, acks */
        }
        else if ((c & 0xFF) == 2 && (c >> 24) == 2)               /* supported protocol: USB 2 */
        {
            uint32_t d2 = rd(a + 8);
            int off = (int)(d2 & 0xFF), cnt = (int)((d2 >> 8) & 0xFF);
            for (p = off; p < off + cnt && p <= MAX_PORT; ++p) if (p >= 1) x->usb2 |= 1UL << (p - 1);
        }
        n = (int)((c >> 8) & 0xFF);
        eo = n ? eo + n * 4 : 0;
    }
    if (!x->usb2) x->usb2 = 0xFFFFFFFFUL;                         /* no protocol caps: try every port */
    /* Intel 7/8/9-series: move the shared ports from EHCI to xHCI */
    if (PCI_ReadWord(h->bus, h->dev, h->func, 0) == 0x8086)
    {
        uint32_t prm = PCI_ReadDWord(h->bus, h->dev, h->func, 0xD4);
        if (prm && prm != 0xFFFFFFFFUL) PCI_WriteDWord(h->bus, h->dev, h->func, 0xD0, prm);
    }
    /* memory */
    if (!pds_dpmi_xms_allocmem(&x->mem, 0x9000)) return 0;
    m = (uint8_t*)x->mem.linearptr;
    x->pbase = (uint32_t)(uintptr_t)x->mem.physicalptr;
    memset(m, 0, 0x9000);
    x->dcbaa = (volatile uint32_t*)m;
    x->erst  = (volatile uint32_t*)(m + 0x400);
    x->inctx = (volatile uint32_t*)(m + 0x3000);
    x->spa   = (volatile uint32_t*)(m + 0x8000);
    ring_init(&x->cmd, (volatile uint32_t*)(m + 0x800), x->pbase + 0x800, XCMD_TRBS);
    x->ev.t = (volatile uint32_t*)(m + 0xA00); x->ev.ph = x->pbase + 0xA00; x->ev.n = XEVT_TRBS; x->ev.enq = 0; x->evcyc = 1;
    x->erst[0] = x->ev.ph; x->erst[1] = 0; x->erst[2] = XEVT_TRBS; x->erst[3] = 0;
    if (x->nsp)
    {
        if (x->nsp > 512 || !pds_dpmi_xms_allocmem(&x->sp, x->nsp * 4096)) return 0;
        memset(x->sp.linearptr, 0, x->nsp * 4096);
        for (n = 0; n < x->nsp; ++n) { x->spa[n * 2] = (uint32_t)(uintptr_t)x->sp.physicalptr + n * 4096; x->spa[n * 2 + 1] = 0; }
        x->dcbaa[0] = x->pbase + 0x8000; x->dcbaa[1] = 0;
    }
    /* halt, reset (xhci98's order: wait HCH, then HCRST and CNR) */
    for (n = 0; n < 1000 && (rd(x->op + X_USBSTS) & XS_CNR); ++n) wait_us(1000);
    wr(x->op + X_USBCMD, rd(x->op + X_USBCMD) & ~XC_RUN);
    for (n = 0; n < 100 && !(rd(x->op + X_USBSTS) & XS_HCH); ++n) wait_us(1000);
    wr(x->op + X_USBCMD, (rd(x->op + X_USBCMD) & 0xFFFE1070UL) | XC_HCRST);
    wait_us(1000);
    for (n = 0; n < 1000 && ((rd(x->op + X_USBCMD) & XC_HCRST) || (rd(x->op + X_USBSTS) & XS_CNR)); ++n) wait_us(1000);
    if (rd(x->op + X_USBCMD) & XC_HCRST) { trace("xHCI reset stuck\n"); return 0; }
    /* program, interrupts off, run */
    wr(x->op + X_CONFIG, (rd(x->op + X_CONFIG) & 0xFFFFFC00UL) | (uint32_t)x->maxslots);
    wr(x->op + X_DNCTRL, rd(x->op + X_DNCTRL) & 0xFFFF0000UL);
    wr(x->op + X_DCBAAP, x->pbase); wr(x->op + X_DCBAAP + 4, 0);
    wr(x->op + X_CRCR, (rd(x->op + X_CRCR) & 0x30UL) | x->cmd.ph | 1); wr(x->op + X_CRCR + 4, 0);
    wr(x->rt + 0x20, 0);                                          /* IMAN: interrupts off */
    wr(x->rt + 0x28, (rd(x->rt + 0x28) & 0xFFFF0000UL) | 1);      /* ERSTSZ */
    wr(x->rt + 0x38, x->ev.ph); wr(x->rt + 0x3C, 0);              /* ERDP */
    wr(x->rt + 0x30, (rd(x->rt + 0x30) & 0x3FUL) | (x->pbase + 0x400)); wr(x->rt + 0x34, 0); /* ERSTBA */
    fl(m, 0x9000);
    if (x->nsp) fl(x->sp.linearptr, x->nsp * 4096);
    wr(x->op + X_USBCMD, (rd(x->op + X_USBCMD) & ~(1UL << 2)) | XC_RUN);
    for (n = 0; n < 100 && (rd(x->op + X_USBSTS) & XS_HCH); ++n) wait_us(1000);
    /* power the USB 2 ports only (USB 3 ones stay off: devices fall back to their USB 2 side) */
    if (hcc & 8)
        for (p = 0; p < h->nports; ++p)
            if (x->usb2 & (1UL << p)) xpsc_set(i, p, PSC_PP);
    wait_us(50000);
    x_events(i);
    trace("xHCI %02x:%02x.%x: %d ports (USB 2: %08lx), %d slots, ctx %d, %d scratchpads, %s\n", h->bus, h->dev, h->func,
        h->nports, (unsigned long)x->usb2, x->maxslots, x->ctxsz, x->nsp, (rd(x->op + X_USBSTS) & XS_HCH) ? "HALTED" : "running");
    return 1;
}

/* schedules on / off for controller i (it stays running either way) */
static void ehci_sched(int i, uint32_t bits, int on)
{
    int n;
    uint32_t sts = (bits & EC_PSE ? ES_PSS : 0) | (bits & EC_ASE ? ES_ASS : 0);
    if (on) ewr(i, E_CMD, erd(i, E_CMD) | bits);
    else ewr(i, E_CMD, erd(i, E_CMD) & ~bits);
    for (n = 0; n < 40 && ((erd(i, E_STS) & sts) != (on ? sts : 0)); ++n) wait_us(100);
}

static void hc_run(int i, int on)
{
    int k;
    switch (HC[i].type)
    {
    case T_UHCI:
    {
        uint16_t io = HC[i].io;
        if (on)
        {
            for (k = 0; k < 1024; ++k) FL[k] = PHYS(QHI) | LP_Q;
            fl(FL, 4096); fl(QHI, 32);
            outportw(io + U_INTR, 0);
            outportw(io + U_FRNUM, 0);
            outportl(io + U_FLBASE, PHYS(FL));
            outportb(io + U_SOF, 0x40);
            outportw(io + U_STS, 0x3F);
            outportw(io + U_CMD, CMD_RS | CMD_CF | CMD_MAXP);
        }
        else
        {
            outportw(io + U_CMD, 0);
            for (k = 0; k < 20 && !(inportw(io + U_STS) & STS_HALT); ++k) wait_us(1000);
        }
        break;
    }
    case T_OHCI:
        EDI[0] |= ED_SKIP; EDC[0] |= ED_SKIP;
        if (on)
        {
            for (k = 0; k < 32; ++k) HCCA_OF(i)[k] = PHYS(EDI);  /* every interrupt slot: the pad's ED */
            fl(HCCA_OF(i), 256); fl(EDI, 32);
            owr(i, O_CTLHEAD, PHYS(EDC)); owr(i, O_CTLCUR, 0);
            owr(i, O_CONTROL, (ord(i, O_CONTROL) & ~(OC_PLE | OC_CLE | OC_BLE)) | OC_PLE | OC_CLE);
            trace("[on ctl%08lx hcca%08lx/%08lx fi%08lx ps%04lx] ", (unsigned long)ord(i, O_CONTROL),
                (unsigned long)ord(i, O_HCCA), (unsigned long)PHYS(HCCA_OF(i)), (unsigned long)ord(i, O_FMINT),
                (unsigned long)ord(i, O_PERSTART));
        }
        else
        {
            owr(i, O_CONTROL, ord(i, O_CONTROL) & ~(OC_PLE | OC_CLE | OC_BLE));
            wait_us(2000);
            owr(i, O_CTLHEAD, 0); owr(i, O_CTLCUR, 0);
            for (k = 0; k < 32; ++k) HCCA_OF(i)[k] = 0;
            fl(HCCA_OF(i), 256);
        }
        break;
    case T_EHCI:
        ehci_sched(i, EC_PSE | EC_ASE, 0);
        if (on)
        {
            zero(QHP, 24);
            QHP[0] = LP_T; QHP[4] = LP_T; QHP[5] = LP_T;              /* idle interrupt QH */
            for (k = 0; k < 1024; ++k) FL[k] = PHYS(QHP) | LP_Q;
            fl(FL, 4096); fl(QHP, 96);
            ewr(i, E_PERIOD, PHYS(FL));
            ewr(i, E_ASYNC, PHYS(QHA));
            ehci_sched(i, EC_PSE, 1);
        }
        break;
    default:
        break;                                                        /* xHCI: rings only move when rung */
    }
}

/* ================================================================== devices */
static int dev_new(int parent, int port, int rootport, int speed)
{
    int i;
    for (i = 0; i < MAX_DEV; ++i)
        if (!D[i].used)
        {
            Dev* d = &D[i];
            memset(d, 0, sizeof(*d));
            d->used = 1; d->parent = parent; d->pport = port; d->rootport = rootport; d->speed = speed;
            d->mps0 = speed == SP_HS ? 64 : 8;
            d->depth = parent < 0 ? 0 : D[parent].depth + 1;
            d->route = parent < 0 ? 0 : D[parent].route | ((uint32_t)(port > 15 ? 15 : port) << (4 * (d->depth - 1)));
            d->tt = -1;
            if (parent >= 0 && speed != SP_HS)
            {
                if (D[parent].speed == SP_HS) { d->tt = parent; d->ttport = port; }
                else { d->tt = D[parent].tt; d->ttport = D[parent].ttport; }
            }
            return i;
        }
    return -1;
}

static void dev_free(int di)                    /* and everything behind it */
{
    int i;
    if (di < 0 || !D[di].used) return;
    for (i = 0; i < MAX_DEV; ++i) if (D[i].used && D[i].parent == di) dev_free(i);
    if (Cur >= 0 && HC[Cur].type == T_XHCI && D[di].slot)
    {
        x_cmd(Cur, 0, TRB_TYPE(TT_DISSLOT) | ((uint32_t)D[di].slot << 24), NULL);
        HC[Cur].x->dcbaa[D[di].slot * 2] = 0;
    }
    if (di == PadD) PadD = -1;
    D[di].used = 0;
}

/* ================================================================== UHCI transfers */
static uint32_t td_cs(const Dev* d) { return TD_CERR3 | TD_ACTIVE | (d->speed == SP_LS ? TD_LS : 0); }
static uint32_t td_tok(int pid, int addr, int ep, int tog, int len)
{
    return ((uint32_t)(len ? len - 1 : 0x7FF) << 21) | ((uint32_t)(tog & 1) << 19) | ((uint32_t)ep << 15) | ((uint32_t)addr << 8) | (uint32_t)pid;
}
static volatile uint32_t* tdn(int i) { return TD + i * 8; }

static int u_run_tds(int n, uint32_t timeout_us)
{
    uint64_t end = tsc() + us2c(timeout_us);
    int i;
    fl(TD, n * 32); fl(CBuf, 512);
    QHC[1] = PHYS(tdn(0));
    fl(QHC, 16);
    for (;;)
    {
        int done = 1;
        fl(TD, n * 32);
        for (i = 0; i < n; ++i)
        {
            uint32_t cs = tdn(i)[1];
            if (cs & TD_ACTIVE) { done = 0; break; }
            if (cs & TD_ERRMASK) { QHC[1] = LP_T; return -1; }
        }
        if (done) { QHC[1] = LP_T; return 0; }
        if (tsc() > end)
        {
            uint16_t io = HC[Cur].io, f1 = inportw(io + U_FRNUM), f2;
            static uint32_t alt;                                 /* same memory through a second mapping */
            uint32_t ph = PHYS(tdn(0)) & ~0xFFFUL;
            wait_us(3000);
            f2 = inportw(io + U_FRNUM);
            if (!alt) alt = DPMI_MapMemory(ph, 0x1000);
            if (alt) trace("[alt td %08lx qh %08lx, cl%d] ", (unsigned long)rd(alt + (PHYS(tdn(0)) & 0xFFF) + 4),
                (unsigned long)rd(alt + (PHYS(QHC) - ph) + 4), ClLine);
            trace("[u%08lx t cmd%04x sts%04x fr%03x>%03x fl%08lx/%08lx pci%04x leg%04x qh%08lx td%08lx %08lx] ",
                (unsigned long)tdn(0)[1], inportw(io + U_CMD), inportw(io + U_STS), f1 & 0x7FF, f2 & 0x7FF,
                (unsigned long)inportl(io + U_FLBASE), (unsigned long)PHYS(FL),
                PCI_ReadWord(HC[Cur].bus, HC[Cur].dev, HC[Cur].func, 4), PCI_ReadWord(HC[Cur].bus, HC[Cur].dev, HC[Cur].func, UHCI_LEGSUP),
                (unsigned long)QHC[1], (unsigned long)tdn(0)[2], (unsigned long)tdn(0)[3]);
            QHC[1] = LP_T; wait_us(1000);
            return -1;
        }
    }
}

static int u_control(const Dev* d, int len, int in)
{
    int n = 0, tog = 1, left = len, i;
    uint8_t* data = CBuf + 16;
    tdn(n)[1] = td_cs(d); tdn(n)[2] = td_tok(PID_SETUP, d->addr, 0, 0, 8); tdn(n)[3] = PHYS(CBuf); ++n;
    while (left > 0 && n < NTD - 1)
    {
        int c = left > d->mps0 ? d->mps0 : left;
        tdn(n)[1] = td_cs(d); tdn(n)[2] = td_tok(in ? PID_IN : PID_OUT, d->addr, 0, tog, c); tdn(n)[3] = PHYS(data + (len - left)); ++n;
        tog ^= 1; left -= c;
    }
    tdn(n)[1] = td_cs(d); tdn(n)[2] = td_tok(in && len ? PID_OUT : PID_IN, d->addr, 0, 1, 0); tdn(n)[3] = 0; ++n;
    for (i = 0; i < n; ++i) tdn(i)[0] = (i + 1 < n) ? (PHYS(tdn(i + 1)) | LP_VF) : LP_T;
    return u_run_tds(n, 300000);
}

/* ================================================================== OHCI transfers */
static void otd(volatile uint32_t* t, uint32_t flags, uint32_t buf, int len, volatile uint32_t* next)
{
    t[0] = OTD_CC_NA | OTD_DI7 | flags;
    t[1] = len ? buf : 0;
    t[2] = next ? PHYS(next) : 0;
    t[3] = len ? buf + len - 1 : 0;
}

static int o_run_tds(const Dev* d, int ep, int mps, int n, uint32_t timeout_us)
{
    uint64_t end;
    int i, r = -1;
    volatile uint32_t* tail = tdn(n);
    zero(tail, 4);
    EDC[0] = ED_SKIP | (uint32_t)d->addr | ((uint32_t)ep << 7) | (d->speed == SP_LS ? ED_LS : 0) | ((uint32_t)mps << 16);
    EDC[1] = PHYS(tail);
    EDC[2] = PHYS(tdn(0));
    EDC[3] = 0;
    fl(TD, (n + 1) * 32); fl(CBuf, 512); fl(OBuf, 64);
    EDC[0] &= ~ED_SKIP;
    fl(EDC, 16);
    owr(Cur, O_CMDSTS, OS_CLF);
    end = tsc() + us2c(timeout_us);
    for (;;)
    {
        int done = 1, err = 0;
        fl(TD, n * 32); fl(EDC, 16);
        for (i = 0; i < n; ++i)
        {
            uint32_t cc = OTD_CC(tdn(i)[0]);
            if (cc >= 0xE) { done = 0; break; }
            if (cc) { err = 1; break; }
        }
        if (err || (EDC[2] & ED_HALT)) break;
        if (done) { r = 0; break; }
        if (tsc() > end) break;
        owr(Cur, O_CMDSTS, OS_CLF);
    }
    if (r)
    {
        uint32_t f1 = ord(Cur, 0x3C) & 0xFFFF;
        wait_us(2000);
        trace("[o%08lx/%08lx ed%08lx %08lx ctl%08lx cs%08lx cur%08lx head%08lx/%08lx fn%04lx>%04lx] ",
            (unsigned long)tdn(0)[0], (unsigned long)tdn(n - 1)[0], (unsigned long)EDC[0], (unsigned long)EDC[2],
            (unsigned long)ord(Cur, O_CONTROL), (unsigned long)ord(Cur, O_CMDSTS), (unsigned long)ord(Cur, O_CTLCUR),
            (unsigned long)ord(Cur, O_CTLHEAD), (unsigned long)PHYS(EDC), (unsigned long)f1, (unsigned long)(ord(Cur, 0x3C) & 0xFFFF));
    }
    EDC[0] |= ED_SKIP;
    fl(EDC, 16);
    wait_us(1100);
    EDC[2] = PHYS(tail);
    fl(EDC, 16);
    owr(Cur, O_INTSTS, 0x06);                                 /* WDH, SF: let the done queue be written back */
    return r;
}

static int o_control(const Dev* d, int len, int in)
{
    int n = 0;
    otd(tdn(n), OTD_SETUP | OTD_T0, PHYS(CBuf), 8, tdn(n + 1)); ++n;
    if (len) { otd(tdn(n), (in ? OTD_IN : OTD_OUT) | OTD_T1 | OTD_R, PHYS(CBuf + 16), len, tdn(n + 1)); ++n; }
    otd(tdn(n), (in && len ? OTD_OUT : OTD_IN) | OTD_T1, 0, 0, tdn(n + 1)); ++n;
    return o_run_tds(d, 0, d->mps0, n, 300000);
}

/* ================================================================== EHCI transfers */
static volatile uint32_t* qtdn(int i) { return TD + i * 16; }        /* 64-byte stride (64-bit layout) */
static void qtd(volatile uint32_t* q, int pid, int dt, uint32_t buf, int len, volatile uint32_t* next)
{
    zero(q, 16);
    q[0] = next ? PHYS(next) : LP_T;
    q[1] = LP_T;
    q[3] = len ? buf : 0;
    q[4] = len ? ((buf & ~0xFFFUL) + 0x1000) : 0;
    q[2] = ((uint32_t)dt << 31) | ((uint32_t)len << 16) | (3UL << 10) | ((uint32_t)pid << 8) | QT_ACTIVE;
}
static uint32_t e_ch(const Dev* d, int ep, int mps, int ctl, int async)
{
    uint32_t eps = d->speed == SP_HS ? 2 : d->speed == SP_LS ? 1 : 0;
    return (uint32_t)d->addr | ((uint32_t)ep << 8) | (eps << 12) | (ctl ? (1UL << 14) : 0) |
           (async ? (1UL << 15) : 0) | ((uint32_t)mps << 16) |
           (ctl && d->speed != SP_HS ? (1UL << 27) : 0) | (async && d->speed == SP_HS ? (4UL << 28) : 0);
}
static uint32_t e_caps(const Dev* d, uint32_t smask, uint32_t cmask)
{
    uint32_t c = (1UL << 30) | smask;
    if (d->speed != SP_HS && d->tt >= 0) c |= (cmask << 8) | ((uint32_t)D[d->tt].addr << 16) | ((uint32_t)d->ttport << 23);
    return c;
}

/* runs qTDs 0..n-1 on the async QH */
static int e_run(const Dev* d, int ep, int mps, int n, uint32_t timeout_us)
{
    uint64_t end;
    int i, r = -1;
    ehci_sched(Cur, EC_ASE, 0);
    zero(QHA, 24);
    QHA[0] = PHYS(QHA) | LP_Q;
    QHA[1] = e_ch(d, ep, mps, 1, 1);
    QHA[2] = e_caps(d, 0, 0);
    QHA[4] = PHYS(qtdn(0));
    QHA[5] = LP_T;
    fl(TD, n * 64); fl(QHA, 96); fl(CBuf, 512); fl(OBuf, 64);
    ehci_sched(Cur, EC_ASE, 1);
    end = tsc() + us2c(timeout_us);
    for (;;)
    {
        int done = 1, err = 0;
        fl(TD, n * 64);
        for (i = 0; i < n; ++i)
        {
            uint32_t t = qtdn(i)[2];
            if (t & QT_HALT) { err = 1; break; }
            if (t & QT_ACTIVE) { done = 0; break; }
        }
        if (err) break;
        if (done) { r = 0; break; }
        if (tsc() > end) break;
    }
    ehci_sched(Cur, EC_ASE, 0);
    if (r) trace("[q%08lx %08lx s%08lx] ", (unsigned long)qtdn(0)[2], (unsigned long)QHA[6], (unsigned long)erd(Cur, E_STS));
    return r;
}

static int e_control(const Dev* d, int len, int in)
{
    int n = 0;
    qtd(qtdn(n), 2, 0, PHYS(CBuf), 8, qtdn(n + 1)); ++n;
    if (len) { qtd(qtdn(n), in ? 1 : 0, 1, PHYS(CBuf + 16), len, qtdn(n + 1)); ++n; }
    qtd(qtdn(n), in && len ? 0 : 1, 1, 0, 0, NULL); ++n;
    return e_run(d, 0, d->mps0, n, 300000);
}

/* ================================================================== xHCI transfers */
static int x_recover(int slot, int dci, Ring* r)
{
    x_cmd(Cur, 0, TRB_TYPE(TT_RESETEP) | ((uint32_t)slot << 24) | ((uint32_t)dci << 16), NULL);
    return x_cmd(Cur, ring_deq(r), TRB_TYPE(TT_SETDEQ) | ((uint32_t)slot << 24) | ((uint32_t)dci << 16), NULL);
}

static int x_control(int di, int len, int in)
{
    const Dev* d = &D[di];
    Ring* r = &XEp0[di];
    uint64_t end;
    uint32_t* s = (uint32_t*)CBuf;
    XCtlDone = 0;
    ring_push(r, s[0], s[1], 8, TRB_TYPE(TT_SETUP) | (1UL << 6) | ((uint32_t)(len ? (in ? 3 : 2) : 0) << 16));
    if (len) ring_push(r, PHYS(CBuf + 16), 0, (uint32_t)len, TRB_TYPE(TT_DATA) | (in ? (1UL << 16) : 0));
    ring_push(r, 0, 0, 0, TRB_TYPE(TT_STATUS) | (1UL << 5) | ((len && in) ? 0 : (1UL << 16)));
    wr(HC[Cur].x->db + d->slot * 4, 1);
    end = tsc() + us2c(300000);
    while (!XCtlDone && tsc() < end) x_events(Cur);
    if (XCtlDone && (XCtlCC == 1 || XCtlCC == 13)) return 0;
    trace("[x cc%d] ", XCtlDone ? XCtlCC : -1);
    x_recover(d->slot, 1, r);
    return -1;
}

static int x_speed(const Dev* d) { return d->speed == SP_HS ? 3 : d->speed == SP_LS ? 2 : 1; }

static void x_slotctx(volatile uint32_t* sc, const Dev* d, int entries)
{
    sc[0] = d->route | ((uint32_t)x_speed(d) << 20) | ((uint32_t)entries << 27);
    sc[1] = (uint32_t)(d->rootport + 1) << 16;
    sc[2] = d->tt >= 0 ? ((uint32_t)D[d->tt].slot | ((uint32_t)d->ttport << 8)) : 0;
    sc[3] = 0;
}

/* enable slot + address device; also sets the device's address (the controller sends SET_ADDRESS) */
static int x_address(int di)
{
    XHC* x = HC[Cur].x;
    Dev* d = &D[di];
    volatile uint32_t* ic = x->inctx;
    volatile uint32_t* ep0;
    int slot = 0;
    if (x_cmd(Cur, 0, TRB_TYPE(TT_ENSLOT), &slot) != 1 || !slot || slot > x->maxslots) return -1;
    d->slot = slot;
    zero(x_out(di), 512);
    x->dcbaa[slot * 2] = x_outph(di); x->dcbaa[slot * 2 + 1] = 0;
    fl(x->dcbaa + slot * 2, 8); fl(x_out(di), 2048);
    ring_init(&XEp0[di], (volatile uint32_t*)((uint8_t*)x->mem.linearptr + 0x1600 + di * 512), x->pbase + 0x1600 + di * 512, XRING_TRBS);
    zero(ic, 33 * 16);
    ic[1] = 3;                                                   /* add slot + EP0 */
    x_slotctx(x_ctx(ic, 1), d, 1);
    ep0 = x_ctx(ic, 2);
    ep0[1] = (3UL << 1) | (4UL << 3) | ((uint32_t)d->mps0 << 16);
    ep0[2] = XEp0[di].ph | 1; ep0[3] = 0; ep0[4] = 8;
    if (x_cmd(Cur, x->pbase + 0x3000, TRB_TYPE(TT_ADDR) | ((uint32_t)slot << 24), NULL) != 1) return -1;
    fl(x_out(di), 64);
    d->addr = (int)(x_out(di)[3] & 0xFF);
    return 0;
}

static int x_set_mps0(int di)
{
    XHC* x = HC[Cur].x;
    volatile uint32_t* ic = x->inctx;
    volatile uint32_t* ep0;
    zero(ic, 33 * 16);
    ic[1] = 2;
    ep0 = x_ctx(ic, 2);
    ep0[1] = (3UL << 1) | (4UL << 3) | ((uint32_t)D[di].mps0 << 16);
    return x_cmd(Cur, x->pbase + 0x3000, TRB_TYPE(TT_EVAL) | ((uint32_t)D[di].slot << 24), NULL) == 1 ? 0 : -1;
}

static int x_hubinfo(int di)
{
    XHC* x = HC[Cur].x;
    volatile uint32_t* ic = x->inctx;
    volatile uint32_t* sc;
    volatile uint32_t* out = x_out(di);
    int k;
    zero(ic, 33 * 16);
    ic[1] = 1;
    sc = x_ctx(ic, 1);
    for (k = 0; k < 4; ++k) sc[k] = out[k];
    sc[0] = (sc[0] & ~(0x1FUL << 27)) | (1UL << 27) | (1UL << 26);   /* hub */
    sc[1] = (sc[1] & 0x00FFFFFFUL) | ((uint32_t)D[di].nports << 24);
    sc[2] &= ~(3UL << 16);
    sc[3] = 0;
    return x_cmd(Cur, x->pbase + 0x3000, TRB_TYPE(TT_CONFEP) | ((uint32_t)D[di].slot << 24), NULL) == 1 ? 0 : -1;
}

static int x_pad_eps(int di)
{
    XHC* x = HC[Cur].x;
    volatile uint32_t* ic = x->inctx;
    volatile uint32_t* sc;
    volatile uint32_t* e;
    volatile uint32_t* out = x_out(di);
    const Dev* d = &D[di];
    int k, idci = InEp * 2 + 1, odci = OutEp ? OutEp * 2 : 0, maxd = idci > odci ? idci : odci, iv;
    uint8_t* m = (uint8_t*)x->mem.linearptr;
    ring_init(&XIn, (volatile uint32_t*)(m + 0x1200), x->pbase + 0x1200, XRING_TRBS);
    ring_init(&XOut, (volatile uint32_t*)(m + 0x1400), x->pbase + 0x1400, XRING_TRBS);
    if (d->speed == SP_HS) iv = InIntv - 1;
    else { iv = 3; while ((1 << (iv - 3)) * 2 <= InIntv && iv < 10) ++iv; }  /* 2^iv x 125 us <= bInterval ms */
    if (iv < 0) iv = 0;
    zero(ic, 33 * 16);
    ic[1] = 1 | (1UL << idci) | (odci ? (1UL << odci) : 0);
    sc = x_ctx(ic, 1);
    for (k = 0; k < 4; ++k) sc[k] = out[k];
    sc[0] = (sc[0] & ~(0x1FUL << 27)) | ((uint32_t)maxd << 27);
    sc[3] = 0;
    e = x_ctx(ic, idci + 1);
    e[0] = (uint32_t)iv << 16;
    e[1] = (3UL << 1) | (7UL << 3) | ((uint32_t)InMps << 16);
    e[2] = XIn.ph | 1; e[3] = 0; e[4] = (uint32_t)InMps | ((uint32_t)InMps << 16);
    if (odci)
    {
        e = x_ctx(ic, odci + 1);
        e[0] = (uint32_t)iv << 16;
        e[1] = (3UL << 1) | (3UL << 3) | (32UL << 16);
        e[2] = XOut.ph | 1; e[3] = 0; e[4] = 32 | (32UL << 16);
    }
    return x_cmd(Cur, x->pbase + 0x3000, TRB_TYPE(TT_CONFEP) | ((uint32_t)d->slot << 24), NULL) == 1 ? 0 : -1;
}

/* ================================================================== transfers, any controller */
static int control(int di, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len)
{
    const Dev* d = &D[di];
    uint8_t* s = CBuf;
    int in = (rt & 0x80) != 0;
    s[0] = rt; s[1] = req; s[2] = (uint8_t)val; s[3] = (uint8_t)(val >> 8);
    s[4] = (uint8_t)idx; s[5] = (uint8_t)(idx >> 8); s[6] = (uint8_t)len; s[7] = (uint8_t)(len >> 8);
    fl(CBuf, 512);
    {
        int r;
        switch (HC[Cur].type)
        {
        case T_UHCI: r = u_control(d, len, in); break;
        case T_OHCI: r = o_control(d, len, in); break;
        case T_EHCI: r = e_control(d, len, in); break;
        default:     r = x_control(di, len, in); break;
        }
        fl(CBuf, 512);                                            /* read what the device sent, not a cached copy */
        return r;
    }
}

static int send_out(const uint8_t* data, int len)
{
    const Dev* d;
    int r;
    if (!OutEp || PadD < 0) return -1;
    d = &D[PadD];
    memcpy(OBuf, data, len);
    fl(OBuf, 64);
    switch (HC[Cur].type)
    {
    case T_UHCI:
        tdn(0)[0] = LP_T; tdn(0)[1] = td_cs(d); tdn(0)[2] = td_tok(PID_OUT, d->addr, OutEp, OutTog, len); tdn(0)[3] = PHYS(OBuf);
        r = u_run_tds(1, 100000);
        break;
    case T_OHCI:
        otd(tdn(0), OTD_OUT | (OutTog ? OTD_T1 : OTD_T0), PHYS(OBuf), len, tdn(1));
        r = o_run_tds(d, OutEp, 64, 1, 100000);
        break;
    case T_EHCI:
        qtd(qtdn(0), 0, OutTog, PHYS(OBuf), len, NULL);
        r = e_run(d, OutEp, 64, 1, 100000);
        break;
    default:
    {
        uint64_t end = tsc() + us2c(100000);
        XOutDone = 0;
        ring_push(&XOut, PHYS(OBuf), 0, (uint32_t)len, TRB_TYPE(TT_NORMAL) | (1UL << 5));
        wr(HC[Cur].x->db + d->slot * 4, (uint32_t)(OutEp * 2));
        while (!XOutDone && tsc() < end) x_events(Cur);
        r = (XOutDone && XOutCC == 1) ? 0 : -1;
        if (!r) return 0;                                       /* the controller keeps the toggle */
        if (XOutDone) x_recover(d->slot, OutEp * 2, &XOut);
        return -1;
    }
    }
    if (r != 0) return -1;
    OutTog ^= 1;
    return 0;
}

/* the pad's IN endpoint: two TDs used in turn on OHCI / EHCI (the empty tail is filled, a new
 * empty one appended - the only change allowed while the controller runs the queue) */
static volatile uint32_t* o_tdi(int k) { return TDI + k * 4; }
static volatile uint32_t* e_tdi(int k) { return TDI + k * 16; }

static void arm_in(void)
{
    const Dev* d = &D[PadD];
    switch (HC[Cur].type)
    {
    case T_UHCI:
        TDI[0] = LP_T;
        TDI[1] = td_cs(d);
        TDI[2] = td_tok(PID_IN, d->addr, InEp, InTog, InMps);
        TDI[3] = PHYS(IBuf);
        fl(TDI, 16); fl(IBuf, 64);
        QHI[1] = PHYS(TDI);
        fl(QHI, 16);
        break;
    case T_OHCI:
    {
        volatile uint32_t* nt = o_tdi(OCur ^ 1);
        zero(nt, 4);
        otd(o_tdi(OCur), OTD_IN | OTD_R, PHYS(IBuf), InMps, nt);
        fl(TDI, 32); fl(IBuf, 64);
        EDI[1] = PHYS(nt);
        fl(EDI, 16);
        OCur ^= 1;
        break;
    }
    case T_EHCI:
    {
        volatile uint32_t* t = e_tdi(OCur);
        volatile uint32_t* nt = e_tdi(OCur ^ 1);
        zero(nt, 16); nt[0] = LP_T; nt[1] = LP_T;           /* inactive: the controller waits on it */
        fl(nt, 64);
        qtd(t, 1, 0, PHYS(IBuf), InMps, nt);                  /* written active last (token) */
        fl(t, 64); fl(IBuf, 64);
        OCur ^= 1;
        break;
    }
    default:
        XInDone = 0;
        fl(IBuf, 64);
        ring_push(&XIn, PHYS(IBuf), 0, (uint32_t)InMps, TRB_TYPE(TT_NORMAL) | (1UL << 5) | (1UL << 2));
        wr(HC[Cur].x->db + d->slot * 4, (uint32_t)(InEp * 2 + 1));
        break;
    }
}

static void start_in(void)
{
    const Dev* d = &D[PadD];
    OCur = 0;
    switch (HC[Cur].type)
    {
    case T_OHCI:
        zero(o_tdi(0), 4);
        EDI[0] = ED_SKIP | (uint32_t)d->addr | ((uint32_t)InEp << 7) | (2UL << 11) | (d->speed == SP_LS ? ED_LS : 0) | ((uint32_t)InMps << 16);
        EDI[1] = PHYS(o_tdi(0));
        EDI[2] = PHYS(o_tdi(0));
        EDI[3] = 0;
        EDI[0] &= ~ED_SKIP;
        fl(EDI, 16); fl(TDI, 32);
        break;
    case T_EHCI:
        ehci_sched(Cur, EC_PSE, 0);
        zero(e_tdi(0), 32); e_tdi(0)[0] = e_tdi(0)[1] = LP_T;
        zero(QHP, 24);
        QHP[0] = LP_T;
        QHP[1] = e_ch(d, InEp, InMps, 0, 0);
        QHP[2] = e_caps(d, 0x01, 0x1C);                      /* start-split uframe 0, complete 2-4 */
        QHP[4] = PHYS(e_tdi(0));
        QHP[5] = LP_T;
        fl(QHP, 96); fl(TDI, 128);
        ehci_sched(Cur, EC_PSE, 1);
        break;
    default:
        break;
    }
    arm_in();
}

/* 0 = still waiting, >0 = bytes received (+1), -1 = error, -2 = stall */
static int poll_in(void)
{
    switch (HC[Cur].type)
    {
    case T_UHCI:
    {
        uint32_t cs;
        int len;
        fl(TDI, 16);
        cs = TDI[1];
        if (cs & TD_ACTIVE) return 0;
        fl(IBuf, 64);
        if (cs & TD_ERRMASK) return (cs & TD_STALL) ? -2 : -1;
        len = (int)((cs + 1) & 0x7FF);
        if (len == 0x800) len = 0;
        InTog ^= 1;
        return len + 1;
    }
    case T_OHCI:
    {
        volatile uint32_t* t = o_tdi(OCur ^ 1);
        uint32_t cc;
        fl(TDI, 32);
        cc = OTD_CC(t[0]);
        if (cc >= 0xE) return 0;
        fl(IBuf, 64);
        owr(Cur, O_INTSTS, 0x06);
        if (cc) { EDI[2] = PHYS(o_tdi(OCur)); fl(EDI, 16); return cc == 4 ? -2 : -1; }
        return (t[1] ? (int)(t[1] - PHYS(IBuf)) : InMps) + 1;
    }
    case T_EHCI:
    {
        volatile uint32_t* t = e_tdi(OCur ^ 1);
        uint32_t tk;
        fl(t, 64);
        tk = t[2];
        if (tk & QT_ACTIVE) return 0;
        fl(IBuf, 64);
        if (tk & QT_HALT)
        {
            if (Light) return 0;                                      /* recovery: interrupt only */
            ehci_sched(Cur, EC_PSE, 0);
            QHP[3] = 0; QHP[4] = PHYS(e_tdi(OCur)); QHP[5] = LP_T; QHP[6] = 0;  /* clear halt and toggle */
            fl(QHP, 96);
            ehci_sched(Cur, EC_PSE, 1);
            return (tk & QT_ERRS) ? -1 : -2;
        }
        return InMps - (int)((tk >> 16) & 0x7FFF) + 1;
    }
    default:
    {
        int cc;
        x_events(Cur);
        if (!XInDone) return 0;
        cc = XInCC;
        if (cc != 1 && cc != 13 && Light) return 0;                 /* error: leave it for the interrupt */
        XInDone = 0;
        if (cc == 1 || cc == 13) { fl(IBuf, 64); return InMps - XInRes + 1; }
        x_recover(D[PadD].slot, InEp * 2 + 1, &XIn);
        return cc == 6 ? -2 : -1;
    }
    }
}

/* ================================================================== enumeration */
static void pad_leds(void)
{
    if (Wireless)
    {
        static const uint8_t led[12] = { 0x00, 0x00, 0x08, 0x40 + 6 };
        send_out(led, 12);
    }
    else
    {
        static const uint8_t led[3] = { 0x01, 0x03, 0x06 };   /* ring: player 1 on */
        send_out(led, 3);
    }
}

static const char* where(int di)
{
    static char w[48];
    const Dev* d = &D[di];
    int n = snprintf(w, sizeof(w), "%s %d port %d", tname(Cur), HC[Cur].idx, d->rootport + 1);
    if (d->depth)
    {
        int k, p[MAX_DEPTH + 1], c = di;
        for (k = d->depth; k > 0; --k) { p[k] = D[c].pport; c = D[c].parent; }
        n += snprintf(w + n, sizeof(w) - n, ", hub port");
        for (k = 1; k <= d->depth && n < (int)sizeof(w) - 4; ++k) n += snprintf(w + n, sizeof(w) - n, "%s%d", k > 1 ? "." : " ", p[k]);
    }
    return w;
}

/* make device di usable (address, descriptors, configuration):
 * 1 = pad running, 2 = hub ready, 0 = failed (retried), -1 = some other device */
static int enumerate(int di)
{
    Dev* d = &D[di];
    uint8_t* b = CBuf + 16;
    int total, pos, cfgval, found = 0, ishub, p;
    uint16_t vid, pid;
    if (HC[Cur].type == T_XHCI)
    {
        if (x_address(di) != 0) { trace("addr! "); return 0; }
        if (control(di, 0x80, 6, 0x0100, 0, 8) != 0) return 0;
        if (b[7] >= 8 && b[7] <= 64 && b[7] != d->mps0) { d->mps0 = b[7]; if (x_set_mps0(di) != 0) return 0; }
    }
    else
    {
        int k;
        d->addr = 0;
        for (k = 0; k < 4; ++k)
        {
            if (control(di, 0x80, 6, 0x0100, 0, 8) == 0) break;
            wait_us(50000);
        }
        if (k == 4) { trace("desc8! "); return 0; }
        if (k) trace("desc8 try%d ", k + 1);
        d->mps0 = b[7] >= 8 && b[7] <= 64 ? b[7] : 8;
        if (control(di, 0x00, 5, (uint16_t)NextAddr, 0, 0) != 0) return 0;
        d->addr = NextAddr++;
        if (NextAddr > 127) NextAddr = 1;
        wait_us(5000);
    }
    if (control(di, 0x80, 6, 0x0100, 0, 18) != 0) return 0;
    vid = (uint16_t)(b[8] | b[9] << 8); pid = (uint16_t)(b[10] | b[11] << 8);
    ishub = b[4] == 9;
    trace("%04x:%04x cls %02x mps %d ", vid, pid, b[4], d->mps0);
    if (control(di, 0x80, 6, 0x0200, 0, 9) != 0) return 0;
    total = b[2] | b[3] << 8;
    if (total > 400) total = 400;
    if (control(di, 0x80, 6, 0x0200, 0, (uint16_t)total) != 0) return 0;
    cfgval = b[5];
    InEp = OutEp = 0; Wireless = 0;
    for (pos = 0; pos + 2 <= total && b[pos] >= 2; pos += b[pos])
    {
        uint8_t type = b[pos + 1];
        if (type == 4)
        {
            if (found) break;
            if (b[pos + 5] == 9) ishub = 1;
            if (b[pos + 5] == 0xFF && b[pos + 6] == 0x5D && (b[pos + 7] == 0x01 || b[pos + 7] == 0x81))
            { found = 1; Wireless = b[pos + 7] == 0x81; }
        }
        else if (type == 5 && found && (b[pos + 3] & 3) == 3)
        {
            if (b[pos + 2] & 0x80) { if (!InEp) { InEp = b[pos + 2] & 0x0F; InMps = b[pos + 4]; InIntv = b[pos + 6] ? b[pos + 6] : 4; } }
            else if (!OutEp) OutEp = b[pos + 2] & 0x0F;
        }
    }
    if (ishub && d->depth < MAX_DEPTH)
    {
        int pwr;
        if (control(di, 0x00, 9, (uint16_t)cfgval, 0, 0) != 0) return 0;
        if (control(di, 0xA0, 6, 0x2900, 0, 9) != 0) return 0;     /* hub descriptor */
        d->hub = 1;
        d->nports = b[2] > 15 ? 15 : b[2];
        pwr = b[5] * 2;
        if (HC[Cur].type == T_XHCI && x_hubinfo(di) != 0) return 0;
        for (p = 1; p <= d->nports; ++p) control(di, 0x23, 3, 8, (uint16_t)p, 0);  /* SET_FEATURE PORT_POWER */
        wait_us((uint32_t)(pwr < 20 ? 20 : pwr > 200 ? 200 : pwr) * 1000 + 20000);
        snprintf(Desc, sizeof(Desc), "USB hub %04X:%04X on %s, no pad behind it", vid, pid, where(di));
        return 2;
    }
    if (!found || !InEp)
    {
        snprintf(Desc, sizeof(Desc), "USB device %04X:%04X on %s is not an Xbox 360 pad", vid, pid, where(di));
        return -1;
    }
    if (InMps < 1 || InMps > 64) InMps = 32;
    if (control(di, 0x00, 9, (uint16_t)cfgval, 0, 0) != 0) return 0;
    PadD = di;
    if (HC[Cur].type == T_XHCI && x_pad_eps(di) != 0) { PadD = -1; return 0; }
    InTog = OutTog = 0;
    if (Wireless)
    {
        static const uint8_t inq[12] = { 0x08, 0x00, 0x0F, 0xC0 };
        send_out(inq, 12);
        WlPresent = 0;
    }
    else pad_leds();
    snprintf(Desc, sizeof(Desc), "Xbox 360 %s %04X:%04X on %s", Wireless ? "wireless receiver" : "pad", vid, pid, where(di));
    start_in();
    return 1;
}

/* hub port helpers (USB 2.0 hub class requests) */
static int hub_status(int hd, int port, uint16_t* st, uint16_t* chg)
{
    uint8_t* b = CBuf + 16;
    if (control(hd, 0xA3, 0, 0, (uint16_t)port, 4) != 0) return -1;
    *st = (uint16_t)(b[0] | b[1] << 8); *chg = (uint16_t)(b[2] | b[3] << 8);
    return 0;
}

/* reset hub port and enumerate what is there: same results as enumerate(), -2 = nothing there */
static int hub_port_enum(int hd, int port)
{
    uint16_t st, chg;
    int n, di, sp, r;
    control(hd, 0x23, 1, 16, (uint16_t)port, 0);                    /* CLEAR_FEATURE C_PORT_CONNECTION */
    wait_us(100000);                                                /* debounce */
    if (hub_status(hd, port, &st, &chg) != 0 || !(st & 1)) return -2;
    if (control(hd, 0x23, 3, 4, (uint16_t)port, 0) != 0) return 0;  /* SET_FEATURE PORT_RESET */
    for (n = 0; n < 20; ++n)
    {
        wait_us(10000);
        if (hub_status(hd, port, &st, &chg) == 0 && (chg & 0x10)) break;
    }
    control(hd, 0x23, 1, 20, (uint16_t)port, 0);                    /* CLEAR_FEATURE C_PORT_RESET */
    if (!(st & 2)) return 0;                                        /* not enabled */
    wait_us(20000);
    sp = (st & 0x200) ? SP_LS : (st & 0x400) ? SP_HS : SP_FS;
    di = dev_new(hd, port, D[hd].rootport, sp);
    if (di < 0) return -1;
    r = enumerate(di);
    if (r == 1 || r == 2) D[hd].child |= 1UL << port;
    else dev_free(di);
    return r;
}

/* ================================================================== state machine */
static void parse(const uint8_t* r)
{
    St.buttons = (uint16_t)(r[2] | r[3] << 8);
    St.lt = r[4]; St.rt = r[5];
    St.lx = (int16_t)(r[6] | r[7] << 8);
    St.ly = (int16_t)(r[8] | r[9] << 8);
    St.rx = (int16_t)(r[10] | r[11] << 8);
    St.ry = (int16_t)(r[12] | r[13] << 8);
    ++St.reports;
}

static void clear_pad(void)
{
    St.connected = 0; St.buttons = 0; St.lt = St.rt = 0; St.lx = St.ly = St.rx = St.ry = 0;
}

static void drop_all(int ignore)
{
    int i;
    QHI[1] = LP_T; QHC[1] = LP_T;
    EDI[0] |= ED_SKIP; EDC[0] |= ED_SKIP;
    if (Cur >= 0)
    {
        if (ignore == 2 || (ignore && ++Fails[Cur][CurPort] >= 3)) Ignore[Cur] |= 1UL << CurPort;
        hc_run(Cur, 0);
        for (i = 0; i < MAX_DEV; ++i) if (D[i].used && D[i].parent < 0) dev_free(i);
        p_disable(Cur, CurPort);
    }
    memset(D, 0, sizeof(D));
    PadD = -1;
    clear_pad();
    Cur = -1; S = S_SCAN;
    NextScan = tsc() + us2c(200000);
}

/* EHCIs whose ports were handed to companions: a port that has come back to the EHCI (some chips, and
 * QEMU, return a port to the EHCI on disconnect even with CONFIGFLAG=0) and has a device on it is given to
 * the companion again, so plugging in after an unplug works */
static void ehci_reclaim(void)
{
    int h, p;
    for (h = 0; h < HCn; ++h)
    {
        if (HC[h].type != T_EHCI || !HC[h].given) continue;
        for (p = 0; p < HC[h].given; ++p)
        {
            uint32_t v = erd(h, E_PORT + p * 4);
            if (v == 0xFFFFFFFFUL || (v & EP_PO) || !(v & EP_CCS)) continue;
            ewr(h, E_CONFIG, 1);
            eport_set(h, p, EP_PO, EP_PE);
            ewr(h, E_CONFIG, 0);
            trace("EHCI port %d back to companion ", p + 1);
        }
    }
}

/* a root port worth looking at; hubs only when nothing else is there, round-robin between them */
static int pick_root(int* ph, int* pp, int nonhub_only)
{
    int h, p, best = -1, bp = 0, bkey = 0x7FFFFFFF;
    if (EhciGiven) ehci_reclaim();
    for (h = 0; h < HCn; ++h)
        for (p = 0; p < HC[h].nports; ++p)
        {
            int v, key;
            if (p == 0 && HC[h].type == T_XHCI && h != Cur) x_events(h);   /* keep idle event rings drained */
            v = pstat(h, p);
            if (v & PS_GONE) continue;
            if (!(v & PS_CCS)) { Ignore[h] &= ~(1UL << p); HubRoot[h] &= ~(1UL << p); Fails[h][p] = 0; continue; }
            if (Ignore[h] & (1UL << p)) continue;
            if (h == Cur && p == CurPort) continue;
            if (HubRoot[h] & (1UL << p))
            {
                if (nonhub_only) continue;
                key = 1000 + ((h * MAX_PORT + p) <= LastHubKey ? 1000 : 0) + h * MAX_PORT + p;
            }
            else key = h * MAX_PORT + p;
            if (key < bkey) { bkey = key; best = h; bp = p; }
        }
    if (best < 0) return 0;
    *ph = best; *pp = bp;
    return 1;
}

static void step(void)
{
    uint64_t t = tsc();
    switch (S)
    {
    case S_SCAN:
    {
        int h, p;
        if (t < NextScan) return;
        NextScan = t + us2c(250000);
        if (!pick_root(&h, &p, 0)) return;
        Cur = h; CurPort = p;
        trace("%s%d.%d:", tname(h), HC[h].idx, p + 1);
        memset(D, 0, sizeof(D)); PadD = -1; NextAddr = 1;
        p_clear_change(h, p);
        hc_run(h, 1);
        S = S_DEBOUNCE; Until = t + us2c(100000);
        return;
    }
    case S_DEBOUNCE:
    {
        int v;
        if (t < Until) return;
        v = pstat(Cur, CurPort);
        if (!(v & PS_CCS)) { drop_all(0); return; }
        if (HC[Cur].type == T_EHCI && (v & PS_LS)) { drop_all(2); return; }  /* low speed on a hub-only EHCI */
        p_reset_start(Cur, CurPort);
        S = S_RESET; Until = t + us2c(55000);
        return;
    }
    case S_RESET:
    {
        int v, di;
        if (t < Until) return;
        if (!p_reset_end(Cur, CurPort)) { trace("noenable "); drop_all(HC[Cur].type == T_EHCI ? 2 : 1); return; }
        v = pstat(Cur, CurPort);
        trace("%s ", (v & PS_HS) ? "hs" : (v & PS_LS) ? "ls" : "fs");
        di = dev_new(-1, 0, CurPort, (v & PS_HS) ? SP_HS : (v & PS_LS) ? SP_LS : SP_FS);
        (void)di;
        S = S_RECOVER; Until = t + us2c(20000);
        return;
    }
    case S_RECOVER:
    {
        int r;
        if (t < Until) return;
        r = enumerate(0);
        trace("e%d ", r);
        if (r == 2)
        {
            HubRoot[Cur] |= 1UL << CurPort;
            LastHubKey = Cur * MAX_PORT + CurPort;
            Fails[Cur][CurPort] = 0;
            S = S_HUB; NextScan = 0; HubSince = t;
            return;
        }
        if (r <= 0) { drop_all(r < 0 ? 2 : 1); return; }
        Fails[Cur][CurPort] = 0;
        clear_pad();
        St.connected = !Wireless;
        S = S_RUN; NextScan = t + us2c(500000); RunSince = t; Watched = 0;
        return;
    }
    case S_HUB:
    {
        int v, h, p, i, k;
        if (t < NextScan) return;
        NextScan = t + us2c(250000);
        v = pstat(Cur, CurPort);
        if ((v & PS_GONE) || !(v & PS_CCS) || !(v & PS_PE)) { drop_all(0); return; }
        if (pick_root(&h, &p, 1)) { drop_all(0); return; }                 /* something new elsewhere */
        for (i = 0; i < MAX_DEV; ++i)
        {
            if (!D[i].used || !D[i].hub) continue;
            for (k = 1; k <= D[i].nports; ++k)
            {
                uint16_t st, chg;
                int r;
                if (hub_status(i, k, &st, &chg) != 0) { drop_all(0); return; }
                if (!(st & 1))
                {
                    if (chg & 1) control(i, 0x23, 1, 16, (uint16_t)k, 0);
                    D[i].ignore &= ~(1UL << k);
                    if (D[i].child & (1UL << k))
                    {
                        int c;
                        for (c = 0; c < MAX_DEV; ++c) if (D[c].used && D[c].parent == i && D[c].pport == k) dev_free(c);
                        D[i].child &= ~(1UL << k);
                    }
                    continue;
                }
                if ((D[i].child | D[i].ignore) & (1UL << k)) continue;
                r = hub_port_enum(i, k);
                trace("h%d.%d:e%d ", i, k, r);
                if (r == 1) { clear_pad(); St.connected = !Wireless; S = S_RUN; NextScan = tsc() + us2c(500000); return; }
                if (r != 2 && r != -2) D[i].ignore |= 1UL << k;   /* not a pad, or failed: until unplugged */
                return;                                                     /* one port per tick */
            }
        }
        /* nothing behind this hub: after a while give another hub a turn */
        if (t - HubSince > us2c(3000000) && pick_root(&h, &p, 0) && (HubRoot[h] & (1UL << p))) drop_all(0);
        return;
    }
    case S_RUN:
    {
        int v, r;
        if (!Light)                                                         /* (port checks: interrupt only) */
        {
            v = pstat(Cur, CurPort);
            if ((v & PS_GONE) || !(v & PS_CCS) || !(v & PS_PE)) { drop_all(0); return; }
        }
        if (PadD < 0) { drop_all(0); return; }
        if (!Light && D[PadD].depth && t >= NextScan)                                 /* behind a hub: still there? */
        {
            uint16_t st, chg;
            NextScan = t + us2c(500000);
            if (hub_status(D[PadD].parent, D[PadD].pport, &st, &chg) != 0 || !(st & 1)) { drop_all(0); return; }
        }
        r = poll_in();
        if (r == 0)
        {
            if (!Light && !Watched && !St.reports && t - RunSince > us2c(2000000))   /* running, but nothing comes */
            {
                Watched = 1;
                if (HC[Cur].type == T_OHCI)
                    trace("[no reports: ed %08lx %08lx %08lx td %08lx %08lx %08lx %08lx ctl%08lx sts%08lx fn%04lx hcca0 %08lx] ",
                        (unsigned long)EDI[0], (unsigned long)EDI[1], (unsigned long)EDI[2],
                        (unsigned long)o_tdi(OCur ^ 1)[0], (unsigned long)o_tdi(OCur ^ 1)[1], (unsigned long)o_tdi(OCur ^ 1)[2],
                        (unsigned long)o_tdi(OCur ^ 1)[3], (unsigned long)ord(Cur, O_CONTROL), (unsigned long)ord(Cur, O_INTSTS),
                        (unsigned long)(ord(Cur, 0x3C) & 0xFFFF), (unsigned long)HCCA_OF(Cur)[0]);
                else if (HC[Cur].type == T_UHCI)
                    trace("[no reports: td %08lx %08lx qh %08lx cmd%04x sts%04x] ", (unsigned long)TDI[1], (unsigned long)TDI[2],
                        (unsigned long)QHI[1], inportw(HC[Cur].io + U_CMD), inportw(HC[Cur].io + U_STS));
                else trace("[no reports] ");
            }
            return;
        }
        if (r < 0 && Light) return;                                         /* recovery from the interrupt */
        if (r < 0)
        {
            if (r == -2) control(PadD, 0x02, 1, 0, (uint16_t)(0x80 | InEp), 0); /* CLEAR_FEATURE halt */
            InTog = 0;
            arm_in();
            return;
        }
        {
            int len = r - 1;
            if (!Wireless)
            {
                if (len >= 14 && IBuf[0] == 0x00) parse(IBuf);
            }
            else if (len >= 2)
            {
                if (IBuf[0] == 0x08)
                {
                    int on = (IBuf[1] & 0x80) != 0;
                    if (on && !WlPresent) { WlPresent = 1; arm_in(); pad_leds(); return; }
                    if (!on) { WlPresent = 0; clear_pad(); }
                    St.connected = WlPresent;
                }
                else if (len >= 18 && IBuf[0] == 0x00 && (IBuf[1] & 1))
                {
                    if (!WlPresent) { WlPresent = 1; St.connected = 1; }
                    parse(IBuf + 4);
                }
            }
            arm_in();
        }
        return;
    }
    }
}

void USBPAD_Poll(void)
{
    if (!Inited || Busy) return;
    Busy = 1;
    Light = 0;
    step();
    Busy = 0;
}

/* from a game's port 201h read: only pick up a finished report, never anything that can take time
 * (enumeration, hub checks, recovery), so the game's stick timing is not disturbed */
void USBPAD_PollLight(void)
{
    if (!Inited || Busy || S != S_RUN) return;
    Busy = 1;
    Light = 1;
    step();
    Busy = 0;
}

const USBPAD_State* USBPAD_Get(void) { return &St; }
const char* USBPAD_Describe(void) { return Desc; }

int USBPAD_Init(uint64_t cps, int wait_ms, const char** msg)
{
    int i, j, usable = 0;
    uint8_t* base;
    static char m[200];
    *msg = m;
    Cps = cps ? cps : 1000000000ULL;
    HCn = UhciN = OhciN = EhciN = XhciN = EhciGiven = 0;
    pci_scan(hc_found);
    if (!HCn)
    {
        snprintf(m, sizeof(m), "no USB controller found");
        return 0;
    }
    if (!pds_dpmi_xms_allocmem(&Mem, 0x3000))
    {
        snprintf(m, sizeof(m), "out of XMS memory");
        return 0;
    }
    base = (uint8_t*)Mem.linearptr;
    PBase = (uint32_t)(uintptr_t)Mem.physicalptr;
    memset(base, 0, 0x3000);
    FL    = (volatile uint32_t*)base;                    /* 4K-aligned */
    HCCAS = (volatile uint32_t*)(base + 0x2000);         /* 256-aligned, MAX_HC x 256 bytes */
    QHI   = (volatile uint32_t*)(base + 0x1200);
    QHC   = (volatile uint32_t*)(base + 0x1210);
    EDI   = (volatile uint32_t*)(base + 0x1220);
    EDC   = (volatile uint32_t*)(base + 0x1230);
    TDI   = (volatile uint32_t*)(base + 0x1240);         /* 128 bytes */
    QHA   = (volatile uint32_t*)(base + 0x1300);         /* 32-aligned, 96 bytes */
    QHP   = (volatile uint32_t*)(base + 0x1380);
    TD    = (volatile uint32_t*)(base + 0x1400);         /* NTD x 32 bytes, to 0x1900 */
    CBuf  = base + 0x1A00;
    IBuf  = base + 0x1C00;
    OBuf  = base + 0x1C40;
    QHI[0] = PHYS(QHC) | LP_Q; QHI[1] = LP_T;
    QHC[0] = LP_T;             QHC[1] = LP_T;
    EDI[0] = EDC[0] = ED_SKIP;
    QHA[0] = PHYS(QHA) | LP_Q; QHA[1] = 1UL << 15; QHA[4] = QHA[5] = LP_T;
    cache_init();
    fl(base, 0x3000);
    trace("mem %08lx, clflush %d\n", (unsigned long)PBase, ClLine);

    /* xHCI first (it may take ports away from EHCI), then EHCI (hands ports to companions or keeps
     * them), then the UHCI / OHCI companions. Controllers that fail to come up are dropped. */
    for (j = 0; j < 4; ++j)
    {
        static const int order[4] = { T_XHCI, T_EHCI, T_UHCI, T_OHCI };
        for (i = 0; i < HCn; ++i)
        {
            int ok = 1;
            if (HC[i].type != order[j]) continue;
            switch (HC[i].type)
            {
            case T_XHCI: ok = xhci_setup(i); if (ok) HC[i].idx = ++XhciN; break;
            case T_EHCI: ok = ehci_setup(i); if (ok) HC[i].idx = ++EhciN; break;
            case T_UHCI: uhci_reset(i); HC[i].idx = ++UhciN; break;
            default:     ohci_reset(i); HC[i].idx = ++OhciN; break;
            }
            if (!ok) HC[i].nports = 0;                   /* not used */
            else ++usable;
        }
    }
    if (!usable)
    {
        snprintf(m, sizeof(m), "no usable USB controller found");
        return 0;
    }
    wait_us(20000);
    clear_pad();
    strcpy(Desc, "no pad connected");
    S = S_SCAN; Cur = -1; NextScan = 0;
    memset(Ignore, 0, sizeof(Ignore)); memset(HubRoot, 0, sizeof(HubRoot)); memset(Fails, 0, sizeof(Fails));
    memset(D, 0, sizeof(D)); PadD = -1;
    Inited = 1;
    {
        uint64_t end = tsc() + us2c((uint32_t)wait_ms * 1000);
        while (tsc() < end && !(S == S_RUN && (St.connected || Wireless))) USBPAD_Poll();
        end = tsc() + us2c(50000);
        while (S == S_RUN && !St.reports && tsc() < end) USBPAD_Poll();
    }
    {
        int n = 0;
        if (UhciN) n += snprintf(m + n, sizeof(m) - n, "%s%d UHCI", n ? " + " : "", UhciN);
        if (OhciN) n += snprintf(m + n, sizeof(m) - n, "%s%d OHCI", n ? " + " : "", OhciN);
        if (EhciN) n += snprintf(m + n, sizeof(m) - n, "%s%d EHCI", n ? " + " : "", EhciN);
        if (XhciN) n += snprintf(m + n, sizeof(m) - n, "%s%d xHCI", n ? " + " : "", XhciN);
        snprintf(m + n, sizeof(m) - n, " controller(s)%s, %s", EhciGiven ? " (EHCI ports handed to companions)" : "", Desc);
    }
    return usable;
}

void USBPAD_Shutdown(void)
{
    int i;
    if (!Inited) return;
    Inited = 0;
    if (Cur >= 0) drop_all(0);
    for (i = 0; i < HCn; ++i)
    {
        if (!HC[i].nports) continue;
        switch (HC[i].type)
        {
        case T_UHCI: hc_run(i, 0); break;
        case T_OHCI: owr(i, O_CONTROL, 0); owr(i, O_HCCA, 0); break;
        case T_EHCI: ehci_sched(i, EC_PSE | EC_ASE, 0); ewr(i, E_CMD, erd(i, E_CMD) & ~EC_RS); break;
        default:
        {
            XHC* x = HC[i].x;
            int n;
            wr(x->op + X_USBCMD, rd(x->op + X_USBCMD) & ~XC_RUN);
            for (n = 0; n < 20 && !(rd(x->op + X_USBSTS) & XS_HCH); ++n) wait_us(1000);
            break;
        }
        }
    }
    pds_dpmi_xms_freemem(&Mem);
}
