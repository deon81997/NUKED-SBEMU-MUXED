/* USB pads, keyboards and mice for SBEMU.
 *
 * A small USB host written for SBEMU's resident setting:
 *   - no USB interrupt at all. Every controller's interrupt enables (and UHCI's PIRQ routing) are
 *     off; USBPAD_Poll() looks at the transfer descriptors / event rings in memory instead. It is
 *     called from the sound card interrupt and from every read of the emulated gameport (port 201h),
 *     so it never shares an IRQ line with the sound chip.
 *   - every PC USB controller kind:
 *       UHCI  Intel ICH / VIA (the Dell D810's ICH6)
 *       OHCI  NEC uPD720100/720101 PCI cards, SiS, ALi, nVidia, AMD, Apple ...
 *       EHCI  with companion controllers: taken from the BIOS, CONFIGFLAG cleared, so every port goes
 *             to its UHCI / OHCI companion (pads, keyboards and mice are full / low speed devices);
 *             without companions (Intel 5-series and later: a rate-matching hub on every port):
 *             driven itself, with split transactions to reach devices behind the hub's TT
 *       xHCI  USB 3 controllers (Intel 7-series and later, NEC/Renesas uPD720200/201/202, ASMedia,
 *             VIA, Fresco, AMD): only their USB 2 ports are powered and used (as xhci98 does);
 *             on Intel 7/8/9-series the shared ports are switched over from EHCI (XUSB2PR)
 *   - USB hubs (any depth up to 4), as many as there are ports.
 *   - devices in use: up to two Xbox 360 pads (wired, or the wireless receiver's first slots), USB keyboards
 *     and USB mice (HID boot protocol), at once, on any mix of controllers and hubs. Each one is a "pipe"
 *     (its interrupt IN endpoint); every controller runs all the time with its own schedule:
 *       UHCI  frame list -> one QH per pipe slot (a TD only while that pipe is on this controller) -> control QH
 *       OHCI  HCCA (set once at reset: real chips such as NEC's read HcHCCA only when they start) interrupt
 *             table -> one ED per pipe slot (skipped unless in use); control list = one ED
 *       EHCI  periodic list -> the QHs of its pipes, relinked with the schedule stopped; async = control QH
 *       xHCI  command ring, one event ring, a transfer ring per endpoint
 *   - enumeration is a small state machine for root ports (debounce, reset, recovery waited by time stamp,
 *     so hot-plugging does not stall the sound interrupt for long) and a round-robin check of one hub port
 *     at a time; control transfers and hub port resets are synchronous and take a few milliseconds.
 *
 * Pads: anything with the Xbox 360 interface (vendor class FFh, subclass 5Dh, protocol 01h: Microsoft wired
 * pad and the third-party wired pads in x360c.inf), and the Microsoft wireless receiver (protocol 81h, one pad
 * per slot; untested). Keyboards / mice: HID boot interfaces (every USB keyboard; nearly every mouse); their
 * reports go to the callbacks set with USBPAD_SetHid (SBEMU: vkbc.c). Not supported: Xbox One / Series pads,
 * CardBus cards (the BIOS leaves the slot unconfigured), Renesas uPD720201/202 cards without their
 * firmware ROM (they need a firmware upload).
 *
 * Pad report layout (wired, 20 bytes): 00 14, buttons lo (dpad U D L R, start, back, LS, RS),
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
#define MAX_DEV   16
#define MAX_DEPTH 4
#define MAX_PIPE  6
#define NTD       40

enum { T_UHCI, T_OHCI, T_EHCI, T_XHCI };
enum { SP_FS, SP_LS, SP_HS };
enum { PT_PAD, PT_KBD, PT_MOUSE };
enum { PK_360, PK_GIP, PK_HID };          /* kinds of pad: Xbox 360 (wired / wireless), Xbox One (GIP), HID gamepad */

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
/* xHCI memory (per controller) */
#define XO_EP0  0x2000                  /* + device * 512: EP0 transfer rings */
#define XO_IN   0x4000                  /* input context */
#define XO_DEV  0x5000                  /* + device * 2048: output device contexts */
#define XO_SPA  0xD000                  /* scratchpad buffer array */
#define XO_SIZE 0xE000

typedef struct
{
    int type, idx, nports, ncc, given, nextaddr;
    uint16_t io; uint32_t mmio, op; uint8_t bus, dev, func; XHC* x;
    volatile uint32_t* fl;              /* UHCI / EHCI frame list; OHCI HCCA */
    volatile uint32_t* ctl;             /* UHCI control QH / OHCI control ED / EHCI async QH */
    volatile uint32_t* pq;              /* MAX_PIPE interrupt QHs / EDs, 128 bytes apart (pipe k uses its own) */
    uint32_t own;                       /* root ports with a device in use */
    uint32_t ignore;                    /* root ports: nothing usable there, skip until unplugged */
    uint8_t  fails[MAX_PORT];
    int      run;                       /* UHCI: running (only while it has a device, as in beta.44) */
} HCI;
static HCI      HC[MAX_HC];
static int      HCn, UhciN, OhciN, EhciN, XhciN, EhciGiven;
#define PQ(h, k) (HC[h].pq + (k) * 32)

typedef struct
{
    int used, hc, addr, speed, mps0, parent, pport, rootport, depth, hub, nports, slot, tt, ttport, maxdci;
    uint16_t vid, pid;
    uint32_t route;
    uint32_t ignore, child, pend;       /* hub: bit per port: not usable / enumerated / seen, waiting for debounce */
    uint64_t pendt[16];
} Dev;
static Dev       D[MAX_DEV];

/* one interrupt IN endpoint in use: a pad, a keyboard or a mouse */
typedef struct
{
    int used, type, dev, hc, ifc, ep, mps, intv, tog, ocur, outep, outmps, otog, pad, wireless, wlpresent, watched;
    int kind, gseq;                     /* PK_*; Xbox One: sequence number of the packets sent */
    volatile uint32_t* td;              /* its IN TD(s): UHCI 1, OHCI 2, EHCI 2 (used in turn) */
    uint8_t *ibuf, *obuf;
    Ring xin, xout;
    volatile int xdone, xodone;
    int xcc, xres, xocc;
    uint8_t prev[8];
    uint64_t since;
    uint32_t reports;
} Pipe;
static Pipe      P[MAX_PIPE];

/* HID gamepads: where the axes, the hat and the buttons are in the input report (from its report descriptor) */
typedef struct { int id, off, size; int32_t lmin, lmax; } HidF;    /* id -1 = not there; off in bits after the ID */
typedef struct { int ids; HidF ax[6], hat, btn; int nbtn; } HidMap;  /* ax: X Y Z Rx Ry Rz */
static HidMap    Hm[MAX_PIPE];

static xmsmem_t Mem;
#define M_TD    0x0000                  /* NTD control TDs, 64 bytes apart */
#define M_CBUF  0x0C00                  /* control data, 512 bytes */
#define M_PIPE  0x1000                  /* + pipe * 0x800: TDs 0, IN buffer 0x100, OUT 0x140, xHCI rings 0x200 / 0x400 */
#define M_HC    0x4000                  /* + controller * 0x2000: frame list / HCCA 0, control QH 0x1000, pipe QHs 0x1080 */
static volatile uint32_t* TD;
static uint8_t*  CBuf;
static uint32_t  PBase;
#define PHYS(p) (PBase + (uint32_t)((volatile uint8_t*)(p) - (volatile uint8_t*)Mem.linearptr))

static uint64_t  Cps;
static USBPAD_State Pad[USBPAD_PADS];
static char      Desc[112], DevList[480];
static uint32_t  Changes;
static int       Flags;
static USBPAD_KbdFn KbFn;
static USBPAD_MouseFn MsFn;
static volatile int LedsWant = 0;
static int       LedsHave = -1;

/* enumeration: one root port at a time (state machine); hub ports one per call (hub_task) */
enum { S_SCAN, S_DEBOUNCE, S_RESET, S_RECOVER };
static int       S, Cur = -1, CurPort;
static uint64_t  Until, NextScan, NextMon, NextHub;
static int       HubCur, HubPort, HubQuiet, HubBusy;
static Ring      XEp0[MAX_DEV];
static volatile int XCmdDone, XCtlDone;
static uint32_t  XCmdPh; static int XCmdCC, XCmdSlot, XCtlCC;
static volatile int Busy;
static int       Inited, Light, Settling;

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
         * can otherwise stay with the halted EHCI, so a device plugged in later is never seen, and on a card
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
    for (n = 0; n < 1024; ++n) h->fl[n] = LP_T;        /* periodic list: filled by ehci_relink() */
    zero(h->ctl, 24);
    h->ctl[0] = PHYS(h->ctl) | LP_Q; h->ctl[1] = 1UL << 15; h->ctl[4] = h->ctl[5] = LP_T;   /* async: the control QH */
    fl(h->fl, 4096); fl(h->ctl, 96);
    ewr(i, E_PERIOD, PHYS(h->fl));
    ewr(i, E_ASYNC, PHYS(h->ctl));
    ewr(i, E_FRINDEX, 0);
    ewr(i, E_CMD, (8UL << 16) | EC_RS);               /* run; schedules on when there is something on them */
    ewr(i, E_CONFIG, 1);                              /* ports to this controller */
    if (hcs & 0x10)                                   /* port power control */
        for (p = 0; p < h->nports; ++p) eport_set(i, p, EP_PP, 0);
    wait_us(20000);
    return 1;
}

static void uhci_reset(int i)
{
    HCI* h = &HC[i];
    int n, k;
    uint16_t io = h->io;
    PCI_WriteWord(h->bus, h->dev, h->func, UHCI_LEGSUP, 0x8F00);  /* legacy off, SMIs off, PIRQ off */
    PCI_WriteWord(h->bus, h->dev, h->func, 0x04, (uint16_t)(PCI_ReadWord(h->bus, h->dev, h->func, 0x04) | 0x05));
    outportw(io + U_INTR, 0);
    outportw(io + U_CMD, 0);
    for (n = 0; n < 20 && !(inportw(io + U_STS) & STS_HALT); ++n) wait_us(1000);
    outportw(io + U_CMD, CMD_GRST); wait_us(20000); outportw(io + U_CMD, 0); wait_us(1000);
    outportw(io + U_CMD, CMD_HCRST);
    for (n = 0; n < 100 && (inportw(io + U_CMD) & CMD_HCRST); ++n) wait_us(100);
    outportw(io + U_INTR, 0);
    outportw(io + U_STS, 0x3F);
    /* schedule, for good: frame list -> pipe QH 0 -> ... -> pipe QH 5 -> control QH. A pipe's QH has
     * a TD only while that pipe is on this controller */
    h->ctl[0] = LP_T; h->ctl[1] = LP_T;
    for (k = 0; k < MAX_PIPE; ++k)
    {
        PQ(i, k)[0] = (k + 1 < MAX_PIPE ? PHYS(PQ(i, k + 1)) : PHYS(h->ctl)) | LP_Q;
        PQ(i, k)[1] = LP_T;
    }
    for (k = 0; k < 1024; ++k) h->fl[k] = PHYS(PQ(i, 0)) | LP_Q;
    fl(h->fl, 4096); fl(h->ctl, 0x400);
    h->run = 0;                                   /* started by uhci_run() when a device needs it */
}

/* UHCI on / off. Only controllers with something on them run (four ICH6 controllers all running from the
 * start crashed the D810 in beta.45; beta.44, which ran only the pad's, did not) */
static void uhci_run(int i, int on)
{
    HCI* h = &HC[i];
    uint16_t io = h->io;
    int n;
    if (h->type != T_UHCI || h->run == on) return;
    h->run = on;
    if (on)
    {
        outportw(io + U_INTR, 0);
        outportw(io + U_FRNUM, 0);
        outportl(io + U_FLBASE, PHYS(h->fl));
        outportb(io + U_SOF, 0x40);
        outportw(io + U_STS, 0x3F);
        outportw(io + U_CMD, CMD_RS | CMD_CF | CMD_MAXP);
    }
    else
    {
        outportw(io + U_CMD, CMD_CF | CMD_MAXP);
        for (n = 0; n < 20 && !(inportw(io + U_STS) & STS_HALT); ++n) wait_us(1000);
    }
}

static void ohci_reset(int i)
{
    HCI* h = &HC[i];
    uint32_t fi, rha, w;
    int n, p, k;
    if (ord(i, O_CONTROL) & OC_IR)                            /* the BIOS's SMM code owns it */
    {
        /* as Linux does: enable the OwnershipChange interrupt first - some BIOSes only get their SMI from it */
        owr(i, 0x10, (1UL << 30));
        owr(i, O_CMDSTS, OS_OCR);
        for (n = 0; n < 1000 && (ord(i, O_CONTROL) & OC_IR); ++n) wait_us(1000);
        trace("OHCI %02x:%02x.%x: BIOS handoff %s ", h->bus, h->dev, h->func, (ord(i, O_CONTROL) & OC_IR) ? "FAILED" : "ok");
    }
    /* legacy keyboard emulation (HceControl, OHCI 1.0a appendix B): with the BIOS gone, an emulation left on
     * keeps catching ports 60h/64h and the PS/2 keyboard goes dead (P5PE-VM + NEC card, beta.57 USBTEST) */
    if (ord(i, 0) & 0x100)
    {
        uint32_t hce = ord(i, 0x100);
        if (hce & 0x1FF) { owr(i, 0x100, 0); trace("OHCI %02x:%02x.%x: keyboard emulation %03lx off ", h->bus, h->dev, h->func, (unsigned long)(hce & 0x1FF)); }
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
    /* schedule, for good (real chips such as NEC's read HcHCCA only when they start, so it is never changed):
     * every interrupt slot -> pipe ED 0 -> ... -> pipe ED 5 (skipped unless in use); control list = one ED */
    zero(h->fl, 64);
    zero(h->ctl, 12); h->ctl[0] = ED_SKIP; h->ctl[8] = ED_SKIP; h->ctl[3] = PHYS(h->ctl + 8);
    for (k = 0; k < MAX_PIPE; ++k)
    {
        zero(PQ(i, k), 4);
        PQ(i, k)[0] = ED_SKIP;
        PQ(i, k)[3] = k + 1 < MAX_PIPE ? PHYS(PQ(i, k + 1)) : 0;
    }
    for (k = 0; k < 32; ++k) h->fl[k] = PHYS(PQ(i, 0));
    fl(h->fl, 256); fl(h->ctl, 0x400);
    owr(i, O_HCCA, PHYS(h->fl));
    owr(i, O_CTLHEAD, PHYS(h->ctl)); owr(i, O_CTLCUR, 0);
    owr(i, O_BLKHEAD, 0); owr(i, O_BLKCUR, 0);
    owr(i, O_INTSTS, 0xFFFFFFFFUL);
    owr(i, O_CONTROL, OC_OPER | OC_PLE | OC_CLE | 3);
    rha = ord(i, O_RHDA);
    h->nports = (int)(rha & 0xFF);
    if (h->nports > MAX_PORT) h->nports = MAX_PORT;
    owr(i, O_RHSTS, 1UL << 16);
    for (p = 0; p < h->nports; ++p) owr(i, O_PORT + p * 4, OP_PPS);
    trace("OHCI %02x:%02x.%x: rev %02lx, %d ports, rhda %08lx\n", h->bus, h->dev, h->func,
        (unsigned long)(ord(i, 0) & 0xFF), h->nports, (unsigned long)rha);
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
    fl(t, n * 16);
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
    int guard = 0, k;
    for (;;)
    {
        volatile uint32_t* e = x->ev.t + x->ev.enq * 4;
        uint32_t ctl;
        int type, cc, slot, ep;
        fl(e, 16);
        ctl = e[3];
        if ((ctl & 1) != (uint32_t)x->evcyc || guard++ > XEVT_TRBS) break;
        type = (int)TRB_GTYPE(ctl); cc = (int)(e[2] >> 24); slot = (int)(ctl >> 24); ep = (int)((ctl >> 16) & 0x1F);
        if (type == TT_EV_CMD && e[0] == XCmdPh) { XCmdCC = cc; XCmdSlot = slot; XCmdDone = 1; }
        else if (type == TT_EV_XFER)
        {
            if (ep == 1) { XCtlCC = cc; XCtlDone = 1; }
            else for (k = 0; k < MAX_PIPE; ++k)
            {
                Pipe* q = &P[k];
                if (!q->used || q->hc != h || D[q->dev].slot != slot) continue;
                if (ep == q->ep * 2 + 1) { q->xcc = cc; q->xres = (int)(e[2] & 0xFFFFFF); q->xdone = 1; break; }
                if (q->outep && ep == q->outep * 2) { q->xocc = cc; q->xodone = 1; break; }
            }
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

static volatile uint32_t* x_ctx(int h, volatile uint32_t* base, int i) { return base + i * (HC[h].x->ctxsz / 4); }
static volatile uint32_t* x_out(int h, int di)                   /* output device context of device di */
{
    return (volatile uint32_t*)((uint8_t*)HC[h].x->mem.linearptr + XO_DEV + di * 2048);
}
static uint32_t x_outph(int h, int di) { return HC[h].x->pbase + XO_DEV + di * 2048; }

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
    if (!pds_dpmi_xms_allocmem(&x->mem, XO_SIZE)) return 0;
    m = (uint8_t*)x->mem.linearptr;
    x->pbase = (uint32_t)(uintptr_t)x->mem.physicalptr;
    memset(m, 0, XO_SIZE);
    x->dcbaa = (volatile uint32_t*)m;
    x->erst  = (volatile uint32_t*)(m + 0x400);
    x->inctx = (volatile uint32_t*)(m + XO_IN);
    x->spa   = (volatile uint32_t*)(m + XO_SPA);
    ring_init(&x->cmd, (volatile uint32_t*)(m + 0x800), x->pbase + 0x800, XCMD_TRBS);
    x->ev.t = (volatile uint32_t*)(m + 0xA00); x->ev.ph = x->pbase + 0xA00; x->ev.n = XEVT_TRBS; x->ev.enq = 0; x->evcyc = 1;
    x->erst[0] = x->ev.ph; x->erst[1] = 0; x->erst[2] = XEVT_TRBS; x->erst[3] = 0;
    if (x->nsp)
    {
        if (x->nsp > 512 || !pds_dpmi_xms_allocmem(&x->sp, x->nsp * 4096)) return 0;
        memset(x->sp.linearptr, 0, x->nsp * 4096);
        for (n = 0; n < x->nsp; ++n) { x->spa[n * 2] = (uint32_t)(uintptr_t)x->sp.physicalptr + n * 4096; x->spa[n * 2 + 1] = 0; }
        x->dcbaa[0] = x->pbase + XO_SPA; x->dcbaa[1] = 0;
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
    fl(m, XO_SIZE);
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

/* EHCI schedules on / off (the controller stays running either way) */
static void ehci_sched(int i, uint32_t bits, int on)
{
    int n;
    uint32_t sts = (bits & EC_PSE ? ES_PSS : 0) | (bits & EC_ASE ? ES_ASS : 0);
    if (on) ewr(i, E_CMD, erd(i, E_CMD) | bits);
    else ewr(i, E_CMD, erd(i, E_CMD) & ~bits);
    for (n = 0; n < 40 && ((erd(i, E_STS) & sts) != (on ? sts : 0)); ++n) wait_us(100);
}

/* EHCI periodic list = the QHs of the pipes on this controller, linked one after the other; rebuilt with the
 * schedule stopped whenever a pipe comes or goes */
static void ehci_relink(int h)
{
    int k, first = -1, last = -1;
    ehci_sched(h, EC_PSE, 0);
    for (k = 0; k < MAX_PIPE; ++k)
    {
        if (!P[k].used || P[k].hc != h || !P[k].ep) continue;
        if (last >= 0) PQ(h, last)[0] = PHYS(PQ(h, k)) | LP_Q;
        else first = k;
        last = k;
    }
    if (last >= 0) PQ(h, last)[0] = LP_T;
    for (k = 0; k < 1024; ++k) HC[h].fl[k] = first >= 0 ? (PHYS(PQ(h, first)) | LP_Q) : LP_T;
    fl(HC[h].fl, 4096); fl(HC[h].pq, MAX_PIPE * 128);
    if (first >= 0) ehci_sched(h, EC_PSE, 1);
}

/* ================================================================== devices */
static int dev_new(int hc, int parent, int port, int rootport, int speed)
{
    int i;
    for (i = 0; i < MAX_DEV; ++i)
        if (!D[i].used)
        {
            Dev* d = &D[i];
            memset(d, 0, sizeof(*d));
            d->used = 1; d->hc = hc; d->parent = parent; d->pport = port; d->rootport = rootport; d->speed = speed;
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

static const char* where(int di)
{
    static char w[48];
    const Dev* d = &D[di];
    int n = snprintf(w, sizeof(w), "%s %d port %d", tname(d->hc), HC[d->hc].idx, d->rootport + 1);
    if (d->depth)
    {
        int k, p[MAX_DEPTH + 1], c = di;
        for (k = d->depth; k > 0; --k) { p[k] = D[c].pport; c = D[c].parent; }
        n += snprintf(w + n, sizeof(w) - n, ", hub port");
        for (k = 1; k <= d->depth && n < (int)sizeof(w) - 4; ++k) n += snprintf(w + n, sizeof(w) - n, "%s%d", k > 1 ? "." : " ", p[k]);
    }
    return w;
}

static const char* pipe_name(const Pipe* q)
{
    static char s[40];
    if (q->type == PT_KBD) return "keyboard";
    if (q->type == PT_MOUSE) return "mouse";
    snprintf(s, sizeof(s), "%s %d", q->kind == PK_GIP ? "Xbox One pad" : q->kind == PK_HID ? "USB gamepad" :
             q->wireless ? "wireless 360 pad" : "360 pad", q->pad + 1);
    return s;
}

/* the list USBPAD_Devices() gives (built when something changes: no formatting in the interrupt otherwise) */
static void devlist(void)
{
    int k, n = 0;
    DevList[0] = 0;
    for (k = 0; k < MAX_PIPE; ++k)
    {
        const Pipe* q = &P[k];
        if (!q->used || n > (int)sizeof(DevList) - 80) continue;
        n += snprintf(DevList + n, sizeof(DevList) - n, "%s %04X:%04X on %s\n", pipe_name(q), D[q->dev].vid, D[q->dev].pid, where(q->dev));
    }
    ++Changes;
}

static void pipe_stop(int k);

static void dev_free(int di)                    /* and everything behind it */
{
    int i, h;
    if (di < 0 || !D[di].used) return;
    h = D[di].hc;
    for (i = 0; i < MAX_DEV; ++i) if (D[i].used && D[i].parent == di) dev_free(i);
    for (i = 0; i < MAX_PIPE; ++i) if (P[i].used && P[i].dev == di) pipe_stop(i);
    if (HC[h].type == T_XHCI && D[di].slot)
    {
        x_cmd(h, 0, TRB_TYPE(TT_DISSLOT) | ((uint32_t)D[di].slot << 24), NULL);
        HC[h].x->dcbaa[D[di].slot * 2] = 0;
        fl(HC[h].x->dcbaa + D[di].slot * 2, 8);
    }
    D[di].used = 0;
}

/* ================================================================== UHCI transfers */
static uint32_t td_cs(const Dev* d) { return TD_CERR3 | TD_ACTIVE | (d->speed == SP_LS ? TD_LS : 0); }
static uint32_t td_tok(int pid, int addr, int ep, int tog, int len)
{
    return ((uint32_t)(len ? len - 1 : 0x7FF) << 21) | ((uint32_t)(tog & 1) << 19) | ((uint32_t)ep << 15) | ((uint32_t)addr << 8) | (uint32_t)pid;
}
static volatile uint32_t* tdn(int i) { return TD + i * 8; }

static int u_run_tds(int h, int n, uint32_t timeout_us)
{
    uint64_t end = tsc() + us2c(timeout_us);
    volatile uint32_t* qh = HC[h].ctl;
    int i;
    fl(TD, n * 32); fl(CBuf, 512);
    qh[1] = PHYS(tdn(0));
    fl(qh, 16);
    for (;;)
    {
        int done = 1;
        fl(TD, n * 32);
        for (i = 0; i < n; ++i)
        {
            uint32_t cs = tdn(i)[1];
            if (cs & TD_ACTIVE) { done = 0; break; }
            if (cs & TD_ERRMASK) { qh[1] = LP_T; fl(qh, 16); return -1; }
        }
        if (done) { qh[1] = LP_T; fl(qh, 16); return 0; }
        if (tsc() > end)
        {
            uint16_t io = HC[h].io;
            trace("[u%08lx t cmd%04x sts%04x fr%03x leg%04x] ", (unsigned long)tdn(0)[1], inportw(io + U_CMD), inportw(io + U_STS),
                inportw(io + U_FRNUM) & 0x7FF, PCI_ReadWord(HC[h].bus, HC[h].dev, HC[h].func, UHCI_LEGSUP));
            qh[1] = LP_T; fl(qh, 16); wait_us(1000);
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
    return u_run_tds(d->hc, n, 300000);
}

/* ================================================================== OHCI transfers */
static void otd(volatile uint32_t* t, uint32_t flags, uint32_t buf, int len, volatile uint32_t* next)
{
    t[0] = OTD_CC_NA | OTD_DI7 | flags;
    t[1] = len ? buf : 0;
    t[2] = next ? PHYS(next) : 0;
    t[3] = len ? buf + len - 1 : 0;
}

/* runs TDs 0..n-1 on the control ED */
/* NEC uPD720101 + wired 360 pad experiments (enumerate() picks them per try and says which in the trace):
 * OAltEd: a second control ED, used only for addresses other than 0 (Linux never rewrites a live ED's address);
 * OSplit: SET_ADDRESS's SETUP and status stage as two separate runs, 2 ms apart */
static int OAltEd, OSplit;
static int o_run_tds(const Dev* d, int ep, int mps, int n, uint32_t timeout_us)
{
    uint64_t end;
    int i, r = -1, h = d->hc;
    volatile uint32_t* ed = (OAltEd && d->addr) ? HC[h].ctl + 8 : HC[h].ctl;
    volatile uint32_t* tail = tdn(n);
    uint32_t info = (uint32_t)d->addr | ((uint32_t)ep << 7) | (d->speed == SP_LS ? ED_LS : 0) | ((uint32_t)mps << 16);
    zero(tail, 4);
    fl(ed, 16);
    if ((ed[0] & ~ED_SKIP) != info)
    {   /* new address / max packet / speed on the shared control ED. An OHCI may keep a copy of an ED it has
         * read (the NEC uPD720101 seems to: after SET_ADDRESS the wired 360 pad was "silent" at its new address
         * and "still at 0", every try - the old address still went out). OHCI 5.2.7: take the list out of use for
         * a frame before changing an ED, and restart it from the head so the HC reads the ED again. */
        uint32_t c = ord(h, O_CONTROL), f0 = ord(h, 0x3C) & 0xFFFF;
        int n;
        owr(h, O_CONTROL, c & ~OC_CLE);
        for (n = 0; n < 40 && ((ord(h, 0x3C) - f0) & 0xFFFF) < 2; ++n) wait_us(250);   /* 2 frame starts */
        owr(h, O_CTLCUR, 0);
        ed[0] = ED_SKIP | info;
        fl(ed, 16);
        owr(h, O_CONTROL, c);
    }
    ed[0] = ED_SKIP | info;
    ed[1] = PHYS(tail);
    ed[2] = PHYS(tdn(0));
    ed[3] = ed == HC[h].ctl ? PHYS(HC[h].ctl + 8) : 0;       /* control list: main ED -> second ED (skipped) */
    fl(TD, (n + 1) * 32); fl(CBuf, 512);
    ed[0] &= ~ED_SKIP;
    fl(ed, 16);
    owr(h, O_CMDSTS, OS_CLF);
    end = tsc() + us2c(timeout_us);
    for (;;)
    {
        int done = 1, err = 0;
        fl(TD, n * 32); fl(ed, 16);
        for (i = 0; i < n; ++i)
        {
            uint32_t cc = OTD_CC(tdn(i)[0]);
            if (cc >= 0xE) { done = 0; break; }
            /* an error code with the ED not halted is a try the HC repeats (ErrorCount < 3: a device that
             * did not answer once, e.g. just after SET_ADDRESS); only a halted ED means the TD failed */
            if (cc) { if (ed[2] & ED_HALT) err = 1; else done = 0; break; }
        }
        if (err || (ed[2] & ED_HALT)) break;
        if (done) { r = 0; break; }
        if (tsc() > end) break;
        owr(h, O_CMDSTS, OS_CLF);
    }
    if (r)
        trace("[o%08lx/%08lx ed%08lx %08lx ctl%08lx is%08lx ps%08lx fn%04lx] ", (unsigned long)tdn(0)[0], (unsigned long)tdn(n - 1)[0],
            (unsigned long)ed[0], (unsigned long)ed[2], (unsigned long)ord(h, O_CONTROL), (unsigned long)ord(h, O_INTSTS),
            (unsigned long)(d->parent < 0 ? ord(h, O_PORT + d->rootport * 4) : 0), (unsigned long)(ord(h, 0x3C) & 0xFFFF));
    ed[0] |= ED_SKIP;
    fl(ed, 16);
    wait_us(1100);
    ed[2] = PHYS(tail);
    fl(ed, 16);
    owr(h, O_INTSTS, 0x06);                                   /* WDH, SF: let the done queue be written back */
    return r;
}

static int o_control(const Dev* d, int len, int in)
{
    int n = 0;
    if (OSplit && !len)
    {
        otd(tdn(0), OTD_SETUP | OTD_T0, PHYS(CBuf), 8, tdn(1));
        if (o_run_tds(d, 0, d->mps0, 1, 300000) != 0) return -1;
        wait_us(2000);
        otd(tdn(0), OTD_IN | OTD_T1, 0, 0, tdn(1));
        return o_run_tds(d, 0, d->mps0, 1, 300000);
    }
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
    int i, r = -1, h = d->hc;
    volatile uint32_t* qh = HC[h].ctl;
    ehci_sched(h, EC_ASE, 0);
    zero(qh, 24);
    qh[0] = PHYS(qh) | LP_Q;
    qh[1] = e_ch(d, ep, mps, 1, 1);
    qh[2] = e_caps(d, 0, 0);
    qh[4] = PHYS(qtdn(0));
    qh[5] = LP_T;
    fl(TD, n * 64); fl(qh, 96); fl(CBuf, 512);
    ehci_sched(h, EC_ASE, 1);
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
    ehci_sched(h, EC_ASE, 0);
    if (r) trace("[q%08lx %08lx s%08lx] ", (unsigned long)qtdn(0)[2], (unsigned long)qh[6], (unsigned long)erd(h, E_STS));
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
static int x_recover(int h, int slot, int dci, Ring* r)
{
    x_cmd(h, 0, TRB_TYPE(TT_RESETEP) | ((uint32_t)slot << 24) | ((uint32_t)dci << 16), NULL);
    return x_cmd(h, ring_deq(r), TRB_TYPE(TT_SETDEQ) | ((uint32_t)slot << 24) | ((uint32_t)dci << 16), NULL);
}

static int x_control(int di, int len, int in)
{
    const Dev* d = &D[di];
    int h = d->hc;
    Ring* r = &XEp0[di];
    uint64_t end;
    uint32_t* s = (uint32_t*)CBuf;
    XCtlDone = 0;
    ring_push(r, s[0], s[1], 8, TRB_TYPE(TT_SETUP) | (1UL << 6) | ((uint32_t)(len ? (in ? 3 : 2) : 0) << 16));
    if (len) ring_push(r, PHYS(CBuf + 16), 0, (uint32_t)len, TRB_TYPE(TT_DATA) | (in ? (1UL << 16) : 0));
    ring_push(r, 0, 0, 0, TRB_TYPE(TT_STATUS) | (1UL << 5) | ((len && in) ? 0 : (1UL << 16)));
    wr(HC[h].x->db + d->slot * 4, 1);
    end = tsc() + us2c(300000);
    while (!XCtlDone && tsc() < end) x_events(h);
    if (XCtlDone && (XCtlCC == 1 || XCtlCC == 13)) return 0;
    trace("[x cc%d] ", XCtlDone ? XCtlCC : -1);
    x_recover(h, d->slot, 1, r);
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
    Dev* d = &D[di];
    int h = d->hc;
    XHC* x = HC[h].x;
    volatile uint32_t* ic = x->inctx;
    volatile uint32_t* ep0;
    int slot = 0;
    if (x_cmd(h, 0, TRB_TYPE(TT_ENSLOT), &slot) != 1 || !slot || slot > x->maxslots) return -1;
    d->slot = slot;
    zero(x_out(h, di), 512);
    x->dcbaa[slot * 2] = x_outph(h, di); x->dcbaa[slot * 2 + 1] = 0;
    fl(x->dcbaa + slot * 2, 8); fl(x_out(h, di), 2048);
    ring_init(&XEp0[di], (volatile uint32_t*)((uint8_t*)x->mem.linearptr + XO_EP0 + di * 512), x->pbase + XO_EP0 + di * 512, XRING_TRBS);
    zero(ic, 33 * 16);
    ic[1] = 3;                                                   /* add slot + EP0 */
    x_slotctx(x_ctx(h, ic, 1), d, 1);
    ep0 = x_ctx(h, ic, 2);
    ep0[1] = (3UL << 1) | (4UL << 3) | ((uint32_t)d->mps0 << 16);
    ep0[2] = XEp0[di].ph | 1; ep0[3] = 0; ep0[4] = 8;
    if (x_cmd(h, x->pbase + XO_IN, TRB_TYPE(TT_ADDR) | ((uint32_t)slot << 24), NULL) != 1) return -1;
    fl(x_out(h, di), 64);
    d->addr = (int)(x_out(h, di)[3] & 0xFF);
    d->maxdci = 1;
    return 0;
}

static int x_set_mps0(int di)
{
    int h = D[di].hc;
    XHC* x = HC[h].x;
    volatile uint32_t* ic = x->inctx;
    volatile uint32_t* ep0;
    zero(ic, 33 * 16);
    ic[1] = 2;
    ep0 = x_ctx(h, ic, 2);
    ep0[1] = (3UL << 1) | (4UL << 3) | ((uint32_t)D[di].mps0 << 16);
    return x_cmd(h, x->pbase + XO_IN, TRB_TYPE(TT_EVAL) | ((uint32_t)D[di].slot << 24), NULL) == 1 ? 0 : -1;
}

static int x_hubinfo(int di)
{
    int h = D[di].hc;
    XHC* x = HC[h].x;
    volatile uint32_t* ic = x->inctx;
    volatile uint32_t* sc;
    volatile uint32_t* out = x_out(h, di);
    int k;
    zero(ic, 33 * 16);
    ic[1] = 1;
    sc = x_ctx(h, ic, 1);
    fl(out, 64);
    for (k = 0; k < 4; ++k) sc[k] = out[k];
    sc[0] = (sc[0] & ~(0x1FUL << 27)) | (1UL << 27) | (1UL << 26);   /* hub */
    sc[1] = (sc[1] & 0x00FFFFFFUL) | ((uint32_t)D[di].nports << 24);
    sc[2] &= ~(3UL << 16);
    sc[3] = 0;
    return x_cmd(h, x->pbase + XO_IN, TRB_TYPE(TT_CONFEP) | ((uint32_t)D[di].slot << 24), NULL) == 1 ? 0 : -1;
}

/* xHCI interval for an interrupt endpoint: 2^iv x 125 us */
static int x_intv(const Dev* d, int bint)
{
    int iv;
    if (d->speed == SP_HS) iv = bint - 1;
    else { iv = 3; while ((1 << (iv - 3)) * 2 <= bint && iv < 10) ++iv; }  /* 2^iv x 125 us <= bInterval ms */
    return iv < 0 ? 0 : iv > 15 ? 15 : iv;
}

/* add the interrupt endpoints of every pipe of device di (one Configure Endpoint) */
static int x_conf_eps(int di)
{
    Dev* d = &D[di];
    int h = d->hc, k, maxd = d->maxdci;
    XHC* x = HC[h].x;
    volatile uint32_t* ic = x->inctx;
    volatile uint32_t* sc;
    volatile uint32_t* e;
    volatile uint32_t* out = x_out(h, di);
    zero(ic, 33 * 16);
    ic[1] = 1;
    for (k = 0; k < MAX_PIPE; ++k)
    {
        Pipe* q = &P[k];
        int idci, odci, iv;
        if (!q->used || q->dev != di) continue;
        idci = q->ep * 2 + 1; odci = q->outep ? q->outep * 2 : 0;
        iv = x_intv(d, q->intv);
        ring_init(&q->xin, (volatile uint32_t*)(q->ibuf + 0x100), PHYS(q->ibuf + 0x100), XRING_TRBS);
        ring_init(&q->xout, (volatile uint32_t*)(q->ibuf + 0x300), PHYS(q->ibuf + 0x300), XRING_TRBS);
        ic[1] |= 1UL << idci;
        if (idci > maxd) maxd = idci;
        e = x_ctx(h, ic, idci + 1);
        e[0] = (uint32_t)iv << 16;
        e[1] = (3UL << 1) | (7UL << 3) | ((uint32_t)q->mps << 16);
        e[2] = q->xin.ph | 1; e[3] = 0; e[4] = (uint32_t)q->mps | ((uint32_t)q->mps << 16);
        if (odci)
        {
            ic[1] |= 1UL << odci;
            if (odci > maxd) maxd = odci;
            e = x_ctx(h, ic, odci + 1);
            e[0] = (uint32_t)iv << 16;
            e[1] = (3UL << 1) | (3UL << 3) | ((uint32_t)q->outmps << 16);
            e[2] = q->xout.ph | 1; e[3] = 0; e[4] = (uint32_t)q->outmps | ((uint32_t)q->outmps << 16);
        }
    }
    sc = x_ctx(h, ic, 1);
    fl(out, 64);
    for (k = 0; k < 4; ++k) sc[k] = out[k];
    sc[0] = (sc[0] & ~(0x1FUL << 27)) | ((uint32_t)maxd << 27);
    sc[3] = 0;
    if (x_cmd(h, x->pbase + XO_IN, TRB_TYPE(TT_CONFEP) | ((uint32_t)d->slot << 24), NULL) != 1) return -1;
    d->maxdci = maxd;
    return 0;
}

/* ================================================================== transfers, any controller */
static int control(int di, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len)
{
    const Dev* d = &D[di];
    uint8_t* s = CBuf;
    int in = (rt & 0x80) != 0, r;
    s[0] = rt; s[1] = req; s[2] = (uint8_t)val; s[3] = (uint8_t)(val >> 8);
    s[4] = (uint8_t)idx; s[5] = (uint8_t)(idx >> 8); s[6] = (uint8_t)len; s[7] = (uint8_t)(len >> 8);
    fl(CBuf, 512);
    switch (HC[d->hc].type)
    {
    case T_UHCI: r = u_control(d, len, in); break;
    case T_OHCI: r = o_control(d, len, in); break;
    case T_EHCI: r = e_control(d, len, in); break;
    default:     r = x_control(di, len, in); break;
    }
    fl(CBuf, 512);                                                /* read what the device sent, not a cached copy */
    return r;
}

/* OUT report on pipe k's interrupt OUT endpoint (pad LEDs), synchronous */
static int send_out(int k, const uint8_t* data, int len)
{
    Pipe* q = &P[k];
    const Dev* d;
    int r, h = q->hc;
    if (!q->outep) return -1;
    d = &D[q->dev];
    memcpy(q->obuf, data, len);
    fl(q->obuf, 64);
    switch (HC[h].type)
    {
    case T_UHCI:
        tdn(0)[0] = LP_T; tdn(0)[1] = td_cs(d); tdn(0)[2] = td_tok(PID_OUT, d->addr, q->outep, q->otog, len); tdn(0)[3] = PHYS(q->obuf);
        r = u_run_tds(h, 1, 100000);
        break;
    case T_OHCI:
        otd(tdn(0), OTD_OUT | (q->otog ? OTD_T1 : OTD_T0), PHYS(q->obuf), len, tdn(1));
        r = o_run_tds(d, q->outep, q->outmps, 1, 100000);
        break;
    case T_EHCI:
        qtd(qtdn(0), 0, q->otog, PHYS(q->obuf), len, NULL);
        r = e_run(d, q->outep, q->outmps, 1, 100000);
        break;
    default:
    {
        uint64_t end = tsc() + us2c(100000);
        q->xodone = 0;
        ring_push(&q->xout, PHYS(q->obuf), 0, (uint32_t)len, TRB_TYPE(TT_NORMAL) | (1UL << 5));
        wr(HC[h].x->db + d->slot * 4, (uint32_t)(q->outep * 2));
        while (!q->xodone && tsc() < end) x_events(h);
        if (q->xodone && q->xocc == 1) return 0;                 /* the controller keeps the toggle */
        if (q->xodone) x_recover(h, d->slot, q->outep * 2, &q->xout);
        return -1;
    }
    }
    if (r != 0) return -1;
    q->otog ^= 1;
    return 0;
}

/* the IN endpoint of pipe k: two TDs used in turn on OHCI / EHCI (the empty tail is filled, a new
 * empty one appended - the only change allowed while the controller runs the queue) */
static volatile uint32_t* o_tdi(const Pipe* q, int i) { return q->td + i * 4; }
static volatile uint32_t* e_tdi(const Pipe* q, int i) { return q->td + i * 16; }

static void arm_in(int k)
{
    Pipe* q = &P[k];
    const Dev* d = &D[q->dev];
    int h = q->hc;
    switch (HC[h].type)
    {
    case T_UHCI:
        q->td[0] = LP_T;
        q->td[1] = td_cs(d);
        q->td[2] = td_tok(PID_IN, d->addr, q->ep, q->tog, q->mps);
        q->td[3] = PHYS(q->ibuf);
        fl(q->td, 16); fl(q->ibuf, 64);
        PQ(h, k)[1] = PHYS(q->td);
        fl(PQ(h, k), 16);
        break;
    case T_OHCI:
    {
        volatile uint32_t* nt = o_tdi(q, q->ocur ^ 1);
        zero(nt, 4);
        otd(o_tdi(q, q->ocur), OTD_IN | OTD_R, PHYS(q->ibuf), q->mps, nt);
        fl(q->td, 32); fl(q->ibuf, 64);
        PQ(h, k)[1] = PHYS(nt);
        fl(PQ(h, k), 16);
        q->ocur ^= 1;
        break;
    }
    case T_EHCI:
    {
        volatile uint32_t* t = e_tdi(q, q->ocur);
        volatile uint32_t* nt = e_tdi(q, q->ocur ^ 1);
        zero(nt, 16); nt[0] = LP_T; nt[1] = LP_T;           /* inactive: the controller waits on it */
        fl(nt, 64);
        qtd(t, 1, 0, PHYS(q->ibuf), q->mps, nt);              /* written active last (token) */
        fl(t, 64); fl(q->ibuf, 64);
        q->ocur ^= 1;
        break;
    }
    default:
        q->xdone = 0;
        fl(q->ibuf, 64);
        ring_push(&q->xin, PHYS(q->ibuf), 0, (uint32_t)q->mps, TRB_TYPE(TT_NORMAL) | (1UL << 5) | (1UL << 2));
        wr(HC[h].x->db + d->slot * 4, (uint32_t)(q->ep * 2 + 1));
        break;
    }
}

static void start_in(int k)
{
    Pipe* q = &P[k];
    const Dev* d = &D[q->dev];
    int h = q->hc;
    volatile uint32_t* e = PQ(h, k);
    q->ocur = 0; q->tog = 0;
    switch (HC[h].type)
    {
    case T_OHCI:
        e[0] |= ED_SKIP;
        fl(e, 16);
        wait_us(1100);
        zero(o_tdi(q, 0), 4);
        e[0] = ED_SKIP | (uint32_t)d->addr | ((uint32_t)q->ep << 7) | (2UL << 11) | (d->speed == SP_LS ? ED_LS : 0) | ((uint32_t)q->mps << 16);
        e[1] = PHYS(o_tdi(q, 0));
        e[2] = PHYS(o_tdi(q, 0));                                 /* e[3]: next ED, set at reset */
        fl(e, 16); fl(q->td, 32);
        arm_in(k);
        e[0] &= ~ED_SKIP;
        fl(e, 16);
        return;
    case T_EHCI:
        zero(e_tdi(q, 0), 32); e_tdi(q, 0)[0] = e_tdi(q, 0)[1] = LP_T;
        zero(e, 24);
        e[0] = LP_T;
        e[1] = e_ch(d, q->ep, q->mps, 0, 0);
        e[2] = e_caps(d, 0x01, 0x1C);                            /* start-split uframe 0, complete 2-4 */
        e[4] = PHYS(e_tdi(q, 0));
        e[5] = LP_T;
        fl(e, 96); fl(q->td, 128);
        arm_in(k);
        ehci_relink(h);
        return;
    default:
        arm_in(k);
        return;
    }
}

static void pipe_stop(int k)
{
    Pipe* q = &P[k];
    int h = q->hc;
    static const uint8_t none[8];
    switch (HC[h].type)
    {
    case T_UHCI: PQ(h, k)[1] = LP_T; fl(PQ(h, k), 16); break;
    case T_OHCI: PQ(h, k)[0] |= ED_SKIP; fl(PQ(h, k), 16); break;
    case T_EHCI: q->used = 0; ehci_relink(h); break;
    default:     break;                                           /* xHCI: the slot is disabled */
    }
    q->used = 0;
    if (q->type == PT_PAD) memset((void*)&Pad[q->pad], 0, sizeof(Pad[0]));
    else if (q->type == PT_KBD) { if (KbFn) KbFn(k, none); }       /* let go of every key it held */
    else if (MsFn) MsFn(0, 0, 0, 0);
    trace("-%s ", pipe_name(q));
    snprintf(Desc, sizeof(Desc), "%s %04X:%04X unplugged", pipe_name(q), D[q->dev].vid, D[q->dev].pid);
    devlist();
}

/* 0 = still waiting, >0 = bytes received (+1), -1 = error, -2 = stall */
static int poll_in(int k)
{
    Pipe* q = &P[k];
    int h = q->hc;
    switch (HC[h].type)
    {
    case T_UHCI:
    {
        uint32_t cs;
        int len;
        fl(q->td, 16);
        cs = q->td[1];
        if (cs & TD_ACTIVE) return 0;
        if (!(cs & TD_ERRMASK))
        {
            /* done - but the controller writes the TD's status first and moves the QH on (element = T) a
             * moment later: re-arming before that lands gets the new TD overwritten with T, and the pipe
             * stops for good (real hardware only: "inputs stop after a few moments" on the D810) */
            fl(PQ(h, k), 16);
            if (!(PQ(h, k)[1] & LP_T)) return 0;
        }
        fl(q->ibuf, 64);
        if (cs & TD_ERRMASK) return (cs & TD_STALL) ? -2 : -1;
        len = (int)((cs + 1) & 0x7FF);
        if (len == 0x800) len = 0;
        q->tog ^= 1;
        return len + 1;
    }
    case T_OHCI:
    {
        volatile uint32_t* t = o_tdi(q, q->ocur ^ 1);
        uint32_t cc;
        fl(q->td, 32);
        cc = OTD_CC(t[0]);
        if (cc >= 0xE) return 0;
        if (cc) { fl(PQ(h, k), 16); if (!(PQ(h, k)[2] & ED_HALT)) return 0; }   /* the HC tries again (ErrorCount < 3) */
        fl(q->ibuf, 64);
        owr(h, O_INTSTS, 0x06);
        if (cc) { PQ(h, k)[2] = PHYS(o_tdi(q, q->ocur)); fl(PQ(h, k), 16); return cc == 4 ? -2 : -1; }
        return (t[1] ? (int)(t[1] - PHYS(q->ibuf)) : q->mps) + 1;
    }
    case T_EHCI:
    {
        volatile uint32_t* t = e_tdi(q, q->ocur ^ 1);
        volatile uint32_t* e = PQ(h, k);
        uint32_t tk;
        fl(t, 64);
        tk = t[2];
        if (tk & QT_ACTIVE) return 0;
        fl(q->ibuf, 64);
        if (tk & QT_HALT)
        {
            if (Light) return 0;                                      /* recovery: interrupt only */
            ehci_sched(h, EC_PSE, 0);
            e[3] = 0; e[4] = PHYS(e_tdi(q, q->ocur)); e[5] = LP_T; e[6] = 0;  /* clear halt and toggle */
            fl(e, 96);
            ehci_sched(h, EC_PSE, 1);
            return (tk & QT_ERRS) ? -1 : -2;
        }
        return q->mps - (int)((tk >> 16) & 0x7FFF) + 1;
    }
    default:
    {
        int cc;
        if (!q->xdone) return 0;
        cc = q->xcc;
        if (cc != 1 && cc != 13 && Light) return 0;                 /* error: leave it for the interrupt */
        q->xdone = 0;
        if (cc == 1 || cc == 13) { fl(q->ibuf, 64); return q->mps - q->xres + 1; }
        x_recover(h, D[q->dev].slot, q->ep * 2 + 1, &q->xin);
        return cc == 6 ? -2 : -1;
    }
    }
}

/* ================================================================== reports */
static void parse_pad(USBPAD_State* s, const uint8_t* r)
{
    s->buttons = (uint16_t)(r[2] | r[3] << 8);
    s->lt = r[4]; s->rt = r[5];
    s->lx = (int16_t)(r[6] | r[7] << 8);
    s->ly = (int16_t)(r[8] | r[9] << 8);
    s->rx = (int16_t)(r[10] | r[11] << 8);
    s->ry = (int16_t)(r[12] | r[13] << 8);
    ++s->reports;
}
static void clear_pad(USBPAD_State* s) { s->buttons = 0; s->lt = s->rt = 0; s->lx = s->ly = s->rx = s->ry = 0; }

/* Xbox One input packet 20h (as Linux xpad reads it): buttons in bytes 4-5, triggers 0..1023, sticks like the 360's */
static void parse_gip(USBPAD_State* s, const uint8_t* r)
{
    static const uint16_t b4[8] = { 0, 0, XPAD_START, XPAD_BACK, XPAD_A, XPAD_B, XPAD_X, XPAD_Y };
    static const uint16_t b5[8] = { XPAD_DU, XPAD_DD, XPAD_DL, XPAD_DR, XPAD_LB, XPAD_RB, XPAD_LS, XPAD_RS };
    uint16_t b = (uint16_t)(s->buttons & XPAD_GUIDE);
    int i, t;
    for (i = 0; i < 8; ++i) { if (r[4] >> i & 1) b |= b4[i]; if (r[5] >> i & 1) b |= b5[i]; }
    s->buttons = b;
    t = r[6] | r[7] << 8; s->lt = (uint8_t)(t > 1023 ? 255 : t >> 2);
    t = r[8] | r[9] << 8; s->rt = (uint8_t)(t > 1023 ? 255 : t >> 2);
    s->lx = (int16_t)(r[10] | r[11] << 8); s->ly = (int16_t)(r[12] | r[13] << 8);
    s->rx = (int16_t)(r[14] | r[15] << 8); s->ry = (int16_t)(r[16] | r[17] << 8);
    ++s->reports;
}

/* ---------------- HID gamepads: the report descriptor tells where X, Y, Z, Rx, Ry, Rz, the hat and the buttons are */
static int32_t hid_sval(const uint8_t* d, int sz) /* item data, sign-extended */
{
    int32_t v = sz == 1 ? (int8_t)d[0] : sz == 2 ? (int16_t)(d[0] | d[1] << 8) : sz == 4 ? (int32_t)(d[0] | d[1] << 8 | d[2] << 16 | (uint32_t)d[3] << 24) : 0;
    return v;
}
static uint32_t hid_uval(const uint8_t* d, int sz)
{
    return sz == 1 ? d[0] : sz == 2 ? (uint32_t)(d[0] | d[1] << 8) : sz == 4 ? (uint32_t)(d[0] | d[1] << 8 | d[2] << 16 | (uint32_t)d[3] << 24) : 0;
}
/* 1 = a joystick / gamepad with at least X and Y */
static int hid_parse(HidMap* m, const uint8_t* d, int len)
{
    int pos = 0, page = 0, rsize = 0, rcount = 0, rid = 0, nu = 0, umin = -1, umax = -1, coll = 0, app = 0, i;
    int32_t lmin = 0, lmax = 0;
    int usages[16];
    static int bits[256];                       /* input bit position per report ID */
    memset(m, 0, sizeof(*m));
    for (i = 0; i < 6; ++i) m->ax[i].id = -1;
    m->hat.id = m->btn.id = -1;
    memset(bits, 0, sizeof bits);
    while (pos < len)
    {
        int h = d[pos], sz = h & 3, type = (h >> 2) & 3, tag = h >> 4;
        const uint8_t* v;
        if (h == 0xFE) { if (pos + 2 >= len) break; pos += 3 + d[pos + 1]; continue; }   /* long item */
        if (sz == 3) sz = 4;
        if (pos + 1 + sz > len) break;
        v = d + pos + 1;
        pos += 1 + sz;
        if (type == 1)                                                  /* global */
        {
            if (tag == 0) page = (int)hid_uval(v, sz);
            else if (tag == 1) lmin = hid_sval(v, sz);
            else if (tag == 2) lmax = lmin >= 0 ? (int32_t)hid_uval(v, sz) : hid_sval(v, sz);
            else if (tag == 7) rsize = (int)hid_uval(v, sz);
            else if (tag == 8) { rid = (int)hid_uval(v, sz) & 255; m->ids = 1; }
            else if (tag == 9) rcount = (int)hid_uval(v, sz);
        }
        else if (type == 2)                                             /* local */
        {
            uint32_t u = hid_uval(v, sz);
            if (sz == 4) u &= 0xFFFF;
            if (tag == 0 && nu < 16) usages[nu++] = (int)u;
            else if (tag == 1) umin = (int)u;
            else if (tag == 2) umax = (int)u;
        }
        else if (type == 0)                                             /* main */
        {
            uint32_t f = hid_uval(v, sz);
            if (tag == 0xA)
            {   /* the application collection must be a joystick (4) or a gamepad (5) */
                if (coll == 0 && page == 1 && nu > 0 && (usages[0] == 4 || usages[0] == 5)) app = 1;
                ++coll;
            }
            else if (tag == 0xC) { if (coll) --coll; }
            else if (tag == 0x8)                                        /* input */
            {
                for (i = 0; i < rcount; ++i)
                {
                    int u = nu ? usages[i < nu ? i : nu - 1] : umin >= 0 ? umin + i : -1;
                    if (umax >= 0 && umin >= 0 && u > umax) u = -1;
                    if (!(f & 1) && u >= 0 && rsize > 0 && rsize <= 32)
                    {
                        HidF x; x.id = m->ids ? rid : 0; x.off = bits[rid]; x.size = rsize; x.lmin = lmin; x.lmax = lmax;
                        if (page == 1 && u >= 0x30 && u <= 0x35 && m->ax[u - 0x30].id < 0 && lmax > lmin) m->ax[u - 0x30] = x;
                        else if (page == 1 && u == 0x39 && m->hat.id < 0) m->hat = x;
                        else if (page == 9 && rsize == 1 && u >= 1 && u <= 16)
                        {
                            if (m->btn.id < 0 && u == 1) { m->btn = x; m->nbtn = 1; }
                            else if (m->btn.id == x.id && x.off == m->btn.off + u - 1 && u > m->nbtn) m->nbtn = u;
                        }
                    }
                    bits[rid] += rsize;
                }
            }
            nu = 0; umin = umax = -1;
        }
    }
    return app && m->ax[0].id >= 0 && m->ax[1].id >= 0;
}
static uint32_t hid_get(const uint8_t* r, int len, const HidF* x)
{
    uint32_t v = 0; int i;
    for (i = 0; i < x->size; ++i)
    {
        int b = x->off + i;
        if ((b >> 3) >= len) break;
        v |= (uint32_t)((r[b >> 3] >> (b & 7)) & 1) << i;
    }
    if (x->lmin < 0 && x->size < 32 && (v >> (x->size - 1) & 1)) v |= ~0U << x->size;    /* sign */
    return v;
}
static int16_t hid_axis(const uint8_t* r, int len, const HidF* x, int flip)
{
    int32_t v = (int32_t)hid_get(r, len, x);
    int32_t n = (int32_t)(((int64_t)(v - x->lmin) * 65535) / (x->lmax - x->lmin)) - 32768;
    if (n < -32768) n = -32768;
    if (n > 32767) n = 32767;
    return (int16_t)(flip ? -1 - n : n);
}
/* buttons 1-4 = A B X Y (the game's fire buttons), 5 6 = LB RB, 7 8 = triggers, 9 10 = Back Start, 11 12 = sticks,
 * 13 = Guide; X Y = left stick; Z Rz (or Rx Ry) = right stick; the hat = d-pad */
static void parse_hid(USBPAD_State* s, const HidMap* m, const uint8_t* r0, int len)
{
    static const uint16_t bm[13] = { XPAD_A, XPAD_B, XPAD_X, XPAD_Y, XPAD_LB, XPAD_RB, 0, 0, XPAD_BACK, XPAD_START, XPAD_LS, XPAD_RS, XPAD_GUIDE };
    int id = m->ids ? r0[0] : 0, i, any = 0;
    const uint8_t* r = m->ids ? r0 + 1 : r0;
    uint16_t b = 0;
    if (m->ids) --len;
    if (m->ax[0].id == id) { s->lx = hid_axis(r, len, &m->ax[0], 0); any = 1; }
    if (m->ax[1].id == id) { s->ly = hid_axis(r, len, &m->ax[1], 1); any = 1; }
    if (m->ax[2].id >= 0 && m->ax[5].id >= 0)
    {
        if (m->ax[2].id == id) s->rx = hid_axis(r, len, &m->ax[2], 0);
        if (m->ax[5].id == id) s->ry = hid_axis(r, len, &m->ax[5], 1);
        if (m->ax[3].id == id) s->lt = (uint8_t)((hid_axis(r, len, &m->ax[3], 0) + 32768) >> 8);
        if (m->ax[4].id == id) s->rt = (uint8_t)((hid_axis(r, len, &m->ax[4], 0) + 32768) >> 8);
    }
    else
    {
        if (m->ax[3].id == id) s->rx = hid_axis(r, len, &m->ax[3], 0);
        if (m->ax[4].id == id) s->ry = hid_axis(r, len, &m->ax[4], 1);
    }
    if (m->btn.id == id)
    {
        HidF x = m->btn;
        for (i = 0; i < m->nbtn && i < 13; ++i)
        {
            x.off = m->btn.off + i; x.size = 1; x.lmin = 0;
            if (hid_get(r, len, &x))
            {
                b |= bm[i];
                if (i == 6) s->lt = 255;
                if (i == 7) s->rt = 255;
            }
            else if ((i == 6 && m->ax[3].id < 0) || (i == 7 && m->ax[4].id < 0)) { if (i == 6) s->lt = 0; else s->rt = 0; }
        }
        any = 1;
    }
    else b = (uint16_t)(s->buttons & ~(XPAD_DU | XPAD_DD | XPAD_DL | XPAD_DR));
    if (m->hat.id == id)
    {
        static const uint16_t hd[8] = { XPAD_DU, XPAD_DU | XPAD_DR, XPAD_DR, XPAD_DR | XPAD_DD, XPAD_DD, XPAD_DD | XPAD_DL, XPAD_DL, XPAD_DL | XPAD_DU };
        int32_t v = (int32_t)hid_get(r, len, &m->hat) - m->hat.lmin;
        if (v >= 0 && v < 8 && m->hat.lmax - m->hat.lmin >= 7) b |= hd[v];
        any = 1;
    }
    else b |= (uint16_t)(s->buttons & (XPAD_DU | XPAD_DD | XPAD_DL | XPAD_DR));
    s->buttons = b;
    if (any) ++s->reports;
}

static void pad_leds(int k)
{
    Pipe* q = &P[k];
    if (q->wireless)
    {
        uint8_t led[12] = { 0x00, 0x00, 0x08, 0x00 };
        led[3] = (uint8_t)(0x40 + 6 + q->pad);
        send_out(k, led, 12);
    }
    else
    {
        uint8_t led[3] = { 0x01, 0x03, 0x06 };                /* ring: player n on */
        led[2] = (uint8_t)(0x06 + q->pad);
        send_out(k, led, 3);
    }
}

static void pipe_poll(int k)
{
    Pipe* q = &P[k];
    int r = poll_in(k), len;
    if (r == 0)
    {
        if (!Light && q->type == PT_PAD && !q->watched && !Pad[q->pad].reports && !q->wireless && tsc() - q->since > us2c(2000000))
        {
            int h = q->hc;
            q->watched = 1;                                             /* running, but nothing comes */
            if (HC[h].type == T_OHCI)
                trace("[no reports: ed %08lx %08lx %08lx td %08lx ctl%08lx sts%08lx fn%04lx] ",
                    (unsigned long)PQ(h, k)[0], (unsigned long)PQ(h, k)[1], (unsigned long)PQ(h, k)[2],
                    (unsigned long)o_tdi(q, q->ocur ^ 1)[0], (unsigned long)ord(h, O_CONTROL), (unsigned long)ord(h, O_INTSTS),
                    (unsigned long)(ord(h, 0x3C) & 0xFFFF));
            else if (HC[h].type == T_UHCI)
                trace("[no reports: td %08lx %08lx qh %08lx cmd%04x sts%04x] ", (unsigned long)q->td[1], (unsigned long)q->td[2],
                    (unsigned long)PQ(h, k)[1], inportw(HC[h].io + U_CMD), inportw(HC[h].io + U_STS));
            else trace("[no reports] ");
        }
        return;
    }
    if (r < 0)
    {
        if (Light) return;                                              /* recovery from the interrupt */
        if (r == -2) control(q->dev, 0x02, 1, 0, (uint16_t)(0x80 | q->ep), 0); /* CLEAR_FEATURE halt */
        q->tog = 0;
        arm_in(k);
        return;
    }
    len = r - 1;
    ++q->reports;
    switch (q->type)
    {
    case PT_PAD:
    {
        USBPAD_State* s = &Pad[q->pad];
        if (q->kind == PK_GIP)
        {
            const uint8_t* r = q->ibuf;
            if (r[0] == 0x20 && len >= 18) parse_gip(s, r);
            else if (r[0] == 0x07 && len >= 5)                          /* the Xbox button has its own packet */
            {
                s->buttons = (uint16_t)((s->buttons & ~XPAD_GUIDE) | ((r[4] & 1) ? XPAD_GUIDE : 0));
                if ((r[1] & 0x10) && !Light)
                {   /* the pad wants this one acknowledged (else it sends it again) */
                    uint8_t ack[13] = { 0x01, 0x20, 0, 0x09, 0x00, 0x07, 0x20, 0x02, 0, 0, 0, 0, 0 };
                    ack[2] = r[2]; ack[5] = r[0];
                    arm_in(k); send_out(k, ack, 13); return;
                }
            }
        }
        else if (q->kind == PK_HID) { if (len > 0) parse_hid(s, &Hm[k], q->ibuf, len); }
        else if (!q->wireless)
        {
            if (len >= 14 && q->ibuf[0] == 0x00) parse_pad(s, q->ibuf);
        }
        else if (len >= 2)
        {
            if (q->ibuf[0] == 0x08)
            {
                int on = (q->ibuf[1] & 0x80) != 0;
                if (on && !q->wlpresent) { q->wlpresent = 1; s->connected = 1; arm_in(k); if (!Light) pad_leds(k); return; }
                if (!on) { q->wlpresent = 0; clear_pad(s); }
                s->connected = q->wlpresent;
            }
            else if (len >= 18 && q->ibuf[0] == 0x00 && (q->ibuf[1] & 1))
            {
                if (!q->wlpresent) { q->wlpresent = 1; s->connected = 1; }
                parse_pad(s, q->ibuf + 4);
            }
        }
        break;
    }
    case PT_KBD:
        /* boot report: modifiers, reserved, 6 keys; 01 in the key slots = too many keys (ignored) */
        if (len >= 3 && q->ibuf[2] != 0x01)
        {
            uint8_t rep[8];
            int i;
            for (i = 0; i < 8; ++i) rep[i] = i < len ? q->ibuf[i] : 0;
            if (memcmp(rep, q->prev, 8) && KbFn) KbFn(k, rep);
            memcpy(q->prev, rep, 8);
        }
        break;
    default:
        if (len >= 3 && MsFn)
            MsFn(q->ibuf[0] & 7, (int8_t)q->ibuf[1], (int8_t)q->ibuf[2], len >= 4 ? (int8_t)q->ibuf[3] : 0);
        break;
    }
    arm_in(k);
}

/* ================================================================== enumeration */
typedef struct { int type, ifc, ep, mps, intv, outep, outmps, wl, kind, hidlen; HidMap hm; } Fn;

/* make device di usable (address, descriptors, configuration, its pads / keyboards / mice started):
 * 1 = in use, 2 = hub ready, 0 = failed (retried), -1 = nothing usable there */
static int enumerate_(int di);
static int enumerate(int di)
{
    int r;
    OAltEd = OSplit = 0;
    r = enumerate_(di);
    OAltEd = OSplit = 0;
    return r;
}
static int enumerate_(int di)
{
    Dev* d = &D[di];
    int h = d->hc;
    uint8_t* b = CBuf + 16;
    int total, pos, cfgval, ishub, p, nf = 0, cur = -1, k, i, used = 0, pads = 0, kbds = 0, mice = 0;
    Fn f[MAX_PIPE];
    uint16_t vid, pid;
    int att = d->parent < 0 ? HC[h].fails[d->rootport] : 0;   /* earlier failed tries of this root port */
#ifdef OFORCE
    att = OFORCE;
#endif
    if (HC[h].type == T_XHCI)
    {
        if (x_address(di) != 0) { trace("addr! "); return 0; }
        if (control(di, 0x80, 6, 0x0100, 0, 8) != 0) return 0;
        if (b[7] >= 8 && b[7] <= 64 && b[7] != d->mps0) { d->mps0 = b[7]; if (x_set_mps0(di) != 0) return 0; }
    }
    else
    {
        /* OHCI root port: the Windows / Linux "new scheme" - GET_DESCRIPTOR asking 64 bytes with a 64-byte ep0
         * (the device's first short packet ends it), a second port reset, then SET_ADDRESS. The wired 360 pad on
         * the NEC uPD720101 (P5PE-VM, beta.54 USBTEST) answered an 8-byte request and SET_ADDRESS but stayed at
         * address 0 every time; devices are tested against the Windows order. */
        int winseq = HC[h].type == T_OHCI && d->parent < 0 && att == 0;
        int ohci = HC[h].type == T_OHCI;
        OAltEd = ohci && att >= 1; OSplit = ohci && att >= 2;
        if (ohci && att) trace("[try%d:%s%s] ", att + 1, OAltEd ? " 2nd ED" : "", OSplit ? " split" : "");
        d->addr = 0;
        if (winseq && d->speed == SP_FS) d->mps0 = 64;
        for (k = 0; k < 4; ++k)
        {
            if (control(di, 0x80, 6, 0x0100, 0, winseq ? 64 : 8) == 0) break;
            wait_us(50000);
        }
        if (k == 4) { trace("desc8! "); return 0; }
        if (k) trace("desc8 try%d ", k + 1);
        d->mps0 = b[7] >= 8 && b[7] <= 64 ? b[7] : 8;
        /* later tries of a root port (USBTEST on a NEC OHCI card: the wired 360 pad took SET_ADDRESS, then
         * never answered at the new address): reset the port again before SET_ADDRESS, as Windows does */
        if ((att >= 1 || winseq) && d->parent < 0)
        {
            p_reset_start(h, d->rootport);
            wait_us(55000);
            if (!p_reset_end(h, d->rootport)) { trace("re-reset! "); return 0; }
            wait_us(20000);
            trace("re-reset ");
        }
        if (control(di, 0x00, 5, (uint16_t)HC[h].nextaddr, 0, 0) != 0) { trace("setaddr! "); return 0; }
        if (ohci) trace("sa[%08lx %08lx] ", (unsigned long)tdn(0)[0], (unsigned long)tdn(1)[0]);
        d->addr = HC[h].nextaddr++;
        if (ohci)
        {   /* where does it answer 2 ms after SET_ADDRESS? (GET_STATUS, 2 bytes) */
            int a = d->addr, rn, r0;
            wait_us(2000);
            rn = control(di, 0x80, 0, 0, 0, 2);
            d->addr = 0; r0 = rn ? control(di, 0x80, 0, 0, 0, 2) : -1; d->addr = a;
            trace("@2ms new:%s old:%s ", rn ? "no" : "yes", rn ? (r0 ? "no" : "yes") : "-");
        }
        if (HC[h].nextaddr > 127) HC[h].nextaddr = 1;
        /* Linux gives SET_ADDRESS 10 ms to settle; the wired 360 pad on a NEC OHCI did not answer at its new
         * address after 5 ms (CC 5, USBTEST from the P5PE-VM): wait 20 ms (100 ms on the last try) */
        wait_us(att >= 2 ? 100000 : 20000);
    }
    for (k = 0; k < 4; ++k)
    {
        if (control(di, 0x80, 6, 0x0100, 0, 18) == 0) break;
        if (HC[h].type == T_XHCI) return 0;
        wait_us(30000);
    }
    if (k == 4 && HC[h].type != T_XHCI)
    {   /* does it still answer at address 0 (SET_ADDRESS not taken)? then send it once more */
        int a = d->addr;
        d->addr = 0;
        if (control(di, 0x80, 6, 0x0100, 0, 8) == 0)
        {
            trace("still at 0 ");
            if (control(di, 0x00, 5, (uint16_t)a, 0, 0) != 0) trace("setaddr2! ");
            else
            {
                d->addr = a;
                wait_us(50000);
                if (control(di, 0x80, 6, 0x0100, 0, 18) == 0) k = 5;
            }
        }
        else trace("silent at 0 too ");
        d->addr = a;
    }
    OSplit = 0;
    if (k == 4) { trace("desc18! "); return 0; }
    if (k) trace("desc18 try%d ", k + 1);
    vid = (uint16_t)(b[8] | b[9] << 8); pid = (uint16_t)(b[10] | b[11] << 8);
    d->vid = vid; d->pid = pid;
    ishub = b[4] == 9;
    trace("%04x:%04x cls %02x mps %d ", vid, pid, b[4], d->mps0);
    if (control(di, 0x80, 6, 0x0200, 0, 9) != 0) return 0;
    total = b[2] | b[3] << 8;
    if (total > 400) total = 400;
    if (control(di, 0x80, 6, 0x0200, 0, (uint16_t)total) != 0) return 0;
    cfgval = b[5];
    /* what is in it: Xbox 360 pad interfaces (vendor FFh / 5Dh / 01h wired, 81h wireless receiver slot),
     * HID boot keyboards (03h / 01h / 01h) and boot mice (03h / 01h / 02h), alternate setting 0 only */
    for (pos = 0; pos + 2 <= total && b[pos] >= 2; pos += b[pos])
    {
        uint8_t type = b[pos + 1];
        if (type == 4 && pos + 8 < total)
        {
            int c = b[pos + 5], s = b[pos + 6], pr = b[pos + 7], t = -1;
            cur = -1;
            if (c == 9) ishub = 1;
            if (b[pos + 3] != 0 || nf >= MAX_PIPE) continue;
            int kind = PK_360;
            if (c == 0xFF && s == 0x5D && (pr == 0x01 || pr == 0x81)) t = PT_PAD;
            else if (c == 0xFF && s == 0x47 && pr == 0xD0) { t = PT_PAD; kind = PK_GIP; }   /* Xbox One / Series pad */
            else if (c == 3 && s == 1 && pr == 1) t = PT_KBD;
            else if (c == 3 && s == 1 && pr == 2) t = PT_MOUSE;
            else if (c == 3 && s <= 1 && pr == 0) { t = PT_PAD; kind = PK_HID; }   /* maybe a gamepad: its report descriptor says */
            if (t < 0) continue;
            cur = nf++;
            memset(&f[cur], 0, sizeof(f[cur]));
            f[cur].type = t; f[cur].ifc = b[pos + 2]; f[cur].wl = pr == 0x81; f[cur].kind = kind;
        }
        else if (type == 0x21 && cur >= 0 && pos + 8 < total)                      /* HID descriptor: report length */
            f[cur].hidlen = b[pos + 7] | b[pos + 8] << 8;
        else if (type == 5 && cur >= 0 && pos + 6 < total && (b[pos + 3] & 3) == 3)
        {
            Fn* x = &f[cur];
            int mps = b[pos + 4] | (b[pos + 5] & 7) << 8;
            if (mps < 1 || mps > 64) mps = mps > 64 ? 64 : 8;
            if (b[pos + 2] & 0x80) { if (!x->ep) { x->ep = b[pos + 2] & 0x0F; x->mps = mps; x->intv = b[pos + 6] ? b[pos + 6] : 4; } }
            else if (!x->outep) { x->outep = b[pos + 2] & 0x0F; x->outmps = mps; }
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
        if (HC[h].type == T_XHCI && x_hubinfo(di) != 0) return 0;
        for (p = 1; p <= d->nports; ++p) control(di, 0x23, 3, 8, (uint16_t)p, 0);  /* SET_FEATURE PORT_POWER */
        wait_us((uint32_t)(pwr < 20 ? 20 : pwr > 200 ? 200 : pwr) * 1000 + 20000);
        snprintf(Desc, sizeof(Desc), "USB hub %04X:%04X on %s", vid, pid, where(di));
        return 2;
    }
    /* keep the ones there is room for */
    for (k = 0; k < MAX_PIPE; ++k)
        if (P[k].used) { if (P[k].type == PT_PAD) ++pads; else if (P[k].type == PT_KBD) ++kbds; else ++mice; }
    for (i = 0; i < nf; ++i)
    {
        int ok = f[i].ep != 0;
        if (f[i].type == PT_PAD && f[i].kind == PK_HID && ok && pads < USBPAD_PADS)
        {   /* a HID interface is used as a pad only if it says it is a joystick / gamepad with X and Y */
            int n = f[i].hidlen > 480 ? 480 : f[i].hidlen;
            ok = n > 0 && control(di, 0x81, 6, 0x2200, (uint16_t)f[i].ifc, (uint16_t)n) == 0 && hid_parse(&f[i].hm, b, n);
            if (!ok) trace("hid-not-pad ");
        }
        if (f[i].type == PT_PAD) ok = ok && pads < USBPAD_PADS;
        else if (f[i].type == PT_KBD) ok = ok && (Flags & USBPAD_F_KBD) && kbds < 2;
        else ok = ok && (Flags & USBPAD_F_MOUSE) && mice < 2;
        if (!ok) { f[i].type = -1; continue; }
        if (f[i].type == PT_PAD) ++pads; else if (f[i].type == PT_KBD) ++kbds; else ++mice;
        ++used;
    }
    if (!used)
    {
        snprintf(Desc, sizeof(Desc), "USB device %04X:%04X on %s: not used (no pad, keyboard or mouse)", vid, pid, where(di));
        return -1;
    }
    if (control(di, 0x00, 9, (uint16_t)cfgval, 0, 0) != 0) return 0;
    for (i = 0; i < nf; ++i)
    {
        Pipe* q;
        if (f[i].type < 0) continue;
        for (k = 0; k < MAX_PIPE && P[k].used; ++k) {}
        if (k == MAX_PIPE) break;
        q = &P[k];
        memset(q, 0, sizeof(*q));
        q->used = 1; q->type = f[i].type; q->dev = di; q->hc = h; q->ifc = f[i].ifc;
        q->ep = f[i].ep; q->mps = f[i].mps; q->intv = f[i].intv; q->outep = f[i].outep; q->outmps = f[i].outmps ? f[i].outmps : 32;
        q->wireless = f[i].wl; q->kind = f[i].kind;
        if (q->kind == PK_HID) Hm[k] = f[i].hm;
        q->td = (volatile uint32_t*)((uint8_t*)Mem.linearptr + M_PIPE + k * 0x800);
        q->ibuf = (uint8_t*)Mem.linearptr + M_PIPE + k * 0x800 + 0x100;
        q->obuf = q->ibuf + 0x40;
        if (q->type == PT_PAD)
        {
            int n;
            for (n = 0; n < USBPAD_PADS; ++n)
            {
                int j, taken = 0;
                for (j = 0; j < MAX_PIPE; ++j) if (j != k && P[j].used && P[j].type == PT_PAD && P[j].pad == n) taken = 1;
                if (!taken) break;
            }
            q->pad = n;
            if (q->kind == PK_360 && q->mps < 20) q->mps = 32;
            if (q->kind == PK_HID) control(di, 0x21, 0x0A, 0, (uint16_t)q->ifc, 0);   /* SET_IDLE: only changes */
            if (q->kind != PK_360 && q->mps > 64) q->mps = 64;
        }
        else
        {
            /* boot protocol (fixed report layout) and no repeats of unchanged reports */
            if (control(di, 0x21, 0x0B, 0, (uint16_t)q->ifc, 0) != 0) trace("setproto! ");
            control(di, 0x21, 0x0A, 0, (uint16_t)q->ifc, 0);
            if (q->type == PT_KBD) LedsHave = -1;
            q->outep = 0;
        }
    }
    if (HC[h].type == T_XHCI && x_conf_eps(di) != 0)
    {
        for (k = 0; k < MAX_PIPE; ++k) if (P[k].used && P[k].dev == di) P[k].used = 0;
        return 0;
    }
    Desc[0] = 0;
    for (k = 0; k < MAX_PIPE; ++k)
    {
        Pipe* q = &P[k];
        if (!q->used || q->dev != di) continue;
        if (q->type == PT_PAD)
        {
            USBPAD_State* s = &Pad[q->pad];
            memset((void*)s, 0, sizeof(*s));
            if (q->kind == PK_GIP)
            {   /* Xbox One: nothing comes until the pad is told to start (Linux xpad: power on, then the
                 * "S" init that newer Microsoft pads want; others ignore it) */
                uint8_t on[5] = { 0x05, 0x20, 0x00, 0x01, 0x00 }, sinit[5] = { 0x05, 0x20, 0x00, 0x0F, 0x06 };
                on[2] = (uint8_t)q->gseq++; send_out(k, on, 5);
                if (vid == 0x045E) { sinit[2] = (uint8_t)q->gseq++; send_out(k, sinit, 5); }
                s->connected = 1;
            }
            else if (q->kind == PK_HID) s->connected = 1;
            else if (q->wireless)
            {
                static const uint8_t inq[12] = { 0x08, 0x00, 0x0F, 0xC0 };
                send_out(k, inq, 12);
            }
            else { pad_leds(k); s->connected = 1; }
        }
        q->since = tsc();
        start_in(k);
        trace("+%s ", pipe_name(q));
        if (!Desc[0]) snprintf(Desc, sizeof(Desc), "%s %04X:%04X on %s", pipe_name(q), vid, pid, where(di));
    }
    devlist();
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
    di = dev_new(D[hd].hc, hd, port, D[hd].rootport, sp);
    if (di < 0) return -1;
    r = enumerate(di);
    if (r == 1 || r == 2) D[hd].child |= 1UL << port;
    else
    {
        dev_free(di);
        control(hd, 0x23, 1, 1, (uint16_t)port, 0);                 /* CLEAR_FEATURE PORT_ENABLE: off address 0 */
    }
    return r;
}

/* one hub port: plugged / unplugged? */
static void hub_check(int hd, int k)
{
    Dev* d = &D[hd];
    uint32_t bit = 1UL << k;
    uint16_t st, chg;
    int r, c;
    if (hub_status(hd, k, &st, &chg) != 0) return;
    if (chg & 1) control(hd, 0x23, 1, 16, (uint16_t)k, 0);         /* CLEAR_FEATURE C_PORT_CONNECTION */
    if (!(st & 1) || ((chg & 1) && (d->child & bit)))
    {
        if (d->child & bit)
        {
            for (c = 0; c < MAX_DEV; ++c) if (D[c].used && D[c].parent == hd && D[c].pport == k) dev_free(c);
            HubBusy = 1;
        }
        d->child &= ~bit; d->ignore &= ~bit;
        if (!(st & 1)) { d->pend &= ~bit; return; }
    }
    if ((d->child | d->ignore) & bit) return;
    HubBusy = 1;
    if (!(d->pend & bit)) { d->pend |= bit; d->pendt[k] = tsc(); return; }    /* debounce: look again later */
    if (tsc() - d->pendt[k] < us2c(100000)) return;
    d->pend &= ~bit;
    r = hub_port_enum(hd, k);
    trace("h%d.%d:e%d ", hd, k, r);
    if (r != 1 && r != 2 && r != -2) d->ignore |= bit;              /* nothing usable, or failed: until unplugged */
}

/* the next hub port, round-robin over every hub */
static void hub_task(void)
{
    int n;
    for (n = 0; n <= MAX_DEV; ++n)
    {
        Dev* d = &D[HubCur];
        if (d->used && d->hub && HubPort < d->nports) { ++HubPort; hub_check(HubCur, HubPort); return; }
        HubPort = 0;
        if (++HubCur >= MAX_DEV) { HubCur = 0; if (!HubBusy) ++HubQuiet; else HubQuiet = 0; HubBusy = 0; }
    }
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
            /* Port Owner straight away first: CONFIGFLAG 0 -> 1 takes every port from the companions (QEMU),
             * which unplugs whatever else is in use there */
            eport_set(h, p, EP_PO, EP_PE);
            wait_us(1000);
            if (!(erd(h, E_PORT + p * 4) & EP_PO))
            {
                ewr(h, E_CONFIG, 1);
                eport_set(h, p, EP_PO, EP_PE);
                ewr(h, E_CONFIG, 0);
            }
            trace("EHCI port %d back to companion ", p + 1);
        }
    }
}

/* a root port with something new on it */
static int pick_root(int* ph, int* pp)
{
    int h, p;
    if (EhciGiven) ehci_reclaim();
    for (h = 0; h < HCn; ++h)
        for (p = 0; p < HC[h].nports; ++p)
        {
            uint32_t bit = 1UL << p;
            int v;
            if (HC[h].own & bit) continue;
            v = pstat(h, p);
            if (v & PS_GONE) continue;
            if (!(v & PS_CCS)) { HC[h].ignore &= ~bit; HC[h].fails[p] = 0; continue; }
            if (HC[h].ignore & bit) continue;
            *ph = h; *pp = p;
            return 1;
        }
    return 0;
}

/* everything on root port p of controller h is gone */
static void root_gone(int h, int p)
{
    int i;
    if (HC[h].type == T_OHCI)    /* why: PESC (bit 17) = disabled by an error, OCIC (19) = over-current; UE = HC stopped */
        trace("%s%d.%d: gone [ps%08lx is%08lx ctl%08lx rh%08lx] ", tname(h), HC[h].idx, p + 1, (unsigned long)ord(h, O_PORT + p * 4),
            (unsigned long)ord(h, O_INTSTS), (unsigned long)ord(h, O_CONTROL), (unsigned long)ord(h, O_RHSTS));
    else trace("%s%d.%d: gone ", tname(h), HC[h].idx, p + 1);
    for (i = 0; i < MAX_DEV; ++i) if (D[i].used && D[i].hc == h && D[i].parent < 0 && D[i].rootport == p) dev_free(i);
    HC[h].own &= ~(1UL << p);
    p_clear_change(h, p);
    if (!HC[h].own && Cur != h) uhci_run(h, 0);
}

/* root port enumeration did not give anything usable */
static void root_fail(int ignore)
{
    int i;
    for (i = 0; i < MAX_DEV; ++i) if (D[i].used && D[i].hc == Cur && D[i].parent < 0 && D[i].rootport == CurPort) dev_free(i);
    p_disable(Cur, CurPort);
    if (ignore == 2 || (ignore && ++HC[Cur].fails[CurPort] >= 3)) HC[Cur].ignore |= 1UL << CurPort;
    if (!HC[Cur].own) uhci_run(Cur, 0);
    Cur = -1; S = S_SCAN;
    NextScan = tsc() + us2c(200000);
}

static void set_leds(void)
{
    int k, l = LedsWant;
    for (k = 0; k < MAX_PIPE; ++k)
        if (P[k].used && P[k].type == PT_KBD)
        {
            CBuf[16] = (uint8_t)l;
            control(P[k].dev, 0x21, 0x09, 0x0200, (uint16_t)P[k].ifc, 1);   /* SET_REPORT output: LEDs */
        }
    LedsHave = l;
}

static void step(void)
{
    uint64_t t;
    int k, h, p;
    for (h = 0; h < HCn; ++h)                                           /* xHCI: transfer events to the pipes */
        if (HC[h].type == T_XHCI && HC[h].nports && (HC[h].own || !Light)) x_events(h);
    for (k = 0; k < MAX_PIPE; ++k) if (P[k].used) pipe_poll(k);
    if (Light) return;
    t = tsc();
    if (LedsWant != LedsHave) set_leds();
    if (t >= NextMon)                                                   /* root ports in use: still there? */
    {
        NextMon = t + us2c(20000);
        for (h = 0; h < HCn; ++h)                                       /* UHCI stopped by an error: say so, restart */
            if (HC[h].type == T_UHCI && HC[h].run)
            {
                uint16_t st = inportw(HC[h].io + U_STS);
                if (st & 0x38)
                {
                    trace("[UHCI%d halted sts%04x cmd%04x fr%03x] ", HC[h].idx, st, inportw(HC[h].io + U_CMD), inportw(HC[h].io + U_FRNUM) & 0x7FF);
                    outportw(HC[h].io + U_STS, 0x3F);
                    outportw(HC[h].io + U_CMD, CMD_RS | CMD_CF | CMD_MAXP);
                }
            }
        for (h = 0; h < HCn; ++h)                                       /* OHCI stopped (UnrecoverableError, as */
            if (HC[h].type == T_OHCI && HC[h].own)                      /* NEC chips can do): reset it, find again */
            {
                uint32_t is = ord(h, O_INTSTS), c = ord(h, O_CONTROL);
                if (is != 0xFFFFFFFFUL && ((is & 0x10) || (c & 0xC0) != OC_OPER))
                {
                    trace("[OHCI%d stopped is%08lx ctl%08lx] ", HC[h].idx, (unsigned long)is, (unsigned long)c);
                    for (p = 0; p < HC[h].nports; ++p) if (HC[h].own & (1UL << p)) root_gone(h, p);
                    ohci_reset(h);
                    HubQuiet = 0;
                }
            }
        for (h = 0; h < HCn; ++h)
            for (p = 0; p < HC[h].nports; ++p)
                if (HC[h].own & (1UL << p))
                {
                    int v = pstat(h, p);
                    if ((v & PS_GONE) || !(v & PS_CCS) || !(v & PS_PE)) { root_gone(h, p); HubQuiet = 0; }
                }
    }
    switch (S)
    {
    case S_SCAN:
        if (t >= NextHub) { NextHub = t + us2c(Settling ? 2000 : 30000); hub_task(); }
        if (t < NextScan) return;
        NextScan = t + us2c(250000);
        if (!pick_root(&h, &p)) return;
        Cur = h; CurPort = p;
        uhci_run(h, 1);
        trace("%s%d.%d:", tname(h), HC[h].idx, p + 1);
        p_clear_change(h, p);
        S = S_DEBOUNCE; Until = t + us2c(100000);
        HubQuiet = 0;
        return;
    case S_DEBOUNCE:
    {
        int v;
        if (t < Until) return;
        v = pstat(Cur, CurPort);
        if (!(v & PS_CCS)) { root_fail(0); return; }
        if (HC[Cur].type == T_EHCI && (v & PS_LS)) { root_fail(2); return; }  /* low speed on a hub-only EHCI */
        p_reset_start(Cur, CurPort);
        S = S_RESET; Until = t + us2c(55000);
        return;
    }
    case S_RESET:
    {
        int v;
        if (t < Until) return;
        if (!p_reset_end(Cur, CurPort)) { trace("noenable "); root_fail(HC[Cur].type == T_EHCI ? 2 : 1); return; }
        v = pstat(Cur, CurPort);
        trace("%s ", (v & PS_HS) ? "hs" : (v & PS_LS) ? "ls" : "fs");
        if (dev_new(Cur, -1, 0, CurPort, (v & PS_HS) ? SP_HS : (v & PS_LS) ? SP_LS : SP_FS) < 0) { root_fail(1); return; }
        S = S_RECOVER; Until = t + us2c(20000);
        return;
    }
    case S_RECOVER:
    {
        int r, di;
        if (t < Until) return;
        for (di = 0; di < MAX_DEV; ++di) if (D[di].used && D[di].hc == Cur && D[di].parent < 0 && D[di].rootport == CurPort) break;
        if (di == MAX_DEV) { root_fail(1); return; }
        r = enumerate(di);
        trace("e%d ", r);
        if (r <= 0) { root_fail(r < 0 ? 2 : 1); return; }
        HC[Cur].own |= 1UL << CurPort;
        HC[Cur].fails[CurPort] = 0;
        Cur = -1; S = S_SCAN; NextScan = tsc();                         /* look for more right away */
        HubQuiet = 0;
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

/* from a game's port 201h read: only pick up finished reports, never anything that can take time
 * (enumeration, hub checks, recovery), so the game's stick timing is not disturbed */
void USBPAD_PollLight(void)
{
    if (!Inited || Busy) return;
    Busy = 1;
    Light = 1;
    step();
    Busy = 0;
}

const USBPAD_State* USBPAD_Get(void) { return &Pad[0]; }
const USBPAD_State* USBPAD_GetPad(int n) { return &Pad[n > 0 ? 1 : 0]; }
const char* USBPAD_Describe(void) { return Desc; }
const char* USBPAD_Devices(void) { return DevList; }
uint32_t USBPAD_Changes(void) { return Changes; }
void USBPAD_SetHid(USBPAD_KbdFn kb, USBPAD_MouseFn ms) { KbFn = kb; MsFn = ms; }
void USBPAD_SetLeds(int leds) { LedsWant = leds & 7; }

int USBPAD_Have(void)
{
    int k, r = 0;
    for (k = 0; k < MAX_PIPE; ++k)
        if (P[k].used)
            r |= P[k].type == PT_KBD ? USBPAD_HAVE_KBD : P[k].type == PT_MOUSE ? USBPAD_HAVE_MOUSE
                : P[k].pad ? USBPAD_HAVE_PAD2 : USBPAD_HAVE_PAD1;
    return r;
}

int USBPAD_Init(uint64_t cps, int wait_ms, int flags, const char** msg)
{
    int i, j, usable = 0;
    uint8_t* base;
    uint32_t size;
    static char m[240];
    *msg = m;
    Cps = cps ? cps : 1000000000ULL;
    Flags = flags;
    HCn = UhciN = OhciN = EhciN = XhciN = EhciGiven = 0;
    pci_scan(hc_found);
    if (!HCn)
    {
        snprintf(m, sizeof(m), "no USB controller found");
        return 0;
    }
    size = M_HC + (uint32_t)HCn * 0x2000;
    if (!pds_dpmi_xms_allocmem(&Mem, size))
    {
        snprintf(m, sizeof(m), "out of XMS memory");
        return 0;
    }
    base = (uint8_t*)Mem.linearptr;
    PBase = (uint32_t)(uintptr_t)Mem.physicalptr;
    memset(base, 0, size);
    TD    = (volatile uint32_t*)(base + M_TD);
    CBuf  = base + M_CBUF;
    for (i = 0; i < HCn; ++i)
    {
        uint8_t* blk = base + M_HC + i * 0x2000;          /* 4K-aligned */
        HC[i].fl  = (volatile uint32_t*)blk;
        HC[i].ctl = (volatile uint32_t*)(blk + 0x1000);
        HC[i].pq  = (volatile uint32_t*)(blk + 0x1080);
        HC[i].nextaddr = 1;
    }
    memset(P, 0, sizeof(P)); memset(D, 0, sizeof(D)); memset((void*)Pad, 0, sizeof(Pad));
    cache_init();
    fl(base, (int)size);
    trace("mem %08lx, clflush %d\n", (unsigned long)PBase, ClLine);

    /* xHCI first (it may take ports away from EHCI), then EHCI (hands ports to companions or keeps
     * them), then the UHCI / OHCI companions. Controllers that fail to come up are dropped. Every one
     * runs from here on, with its own schedule. */
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
    strcpy(Desc, "nothing connected");
    S = S_SCAN; Cur = -1; NextScan = NextMon = NextHub = 0;
    HubCur = HubPort = HubQuiet = HubBusy = 0;
    LedsHave = -1;
    Inited = 1;
    /* set up what is plugged in now: until no root port is left to look at and every hub has had two quiet
     * rounds (or nothing at all is plugged in after 0.3 s), at most wait_ms */
    {
        uint64_t t0 = tsc(), end = t0 + us2c((uint32_t)wait_ms * 1000);
        int hh, pp;
        Settling = 1;
        while (tsc() < end)
        {
            USBPAD_Poll();
            if (S == S_SCAN && tsc() - t0 > us2c(300000) && HubQuiet >= 2 && !pick_root(&hh, &pp)) break;
        }
        Settling = 0;
        end = tsc() + us2c(50000);
        while (tsc() < end) USBPAD_Poll();                 /* first reports */
    }
    {
        int n = 0, have = USBPAD_Have();
        if (UhciN) n += snprintf(m + n, sizeof(m) - n, "%s%d UHCI", n ? " + " : "", UhciN);
        if (OhciN) n += snprintf(m + n, sizeof(m) - n, "%s%d OHCI", n ? " + " : "", OhciN);
        if (EhciN) n += snprintf(m + n, sizeof(m) - n, "%s%d EHCI", n ? " + " : "", EhciN);
        if (XhciN) n += snprintf(m + n, sizeof(m) - n, "%s%d xHCI", n ? " + " : "", XhciN);
        n += snprintf(m + n, sizeof(m) - n, " controller(s)%s, ", EhciGiven ? " (EHCI ports handed to companions)" : "");
        if (!have) snprintf(m + n, sizeof(m) - n, "nothing connected yet");
        else snprintf(m + n, sizeof(m) - n, "found:%s%s%s%s", (have & USBPAD_HAVE_PAD1) ? " pad 1" : "",
            (have & USBPAD_HAVE_PAD2) ? " pad 2" : "", (have & USBPAD_HAVE_KBD) ? " keyboard" : "", (have & USBPAD_HAVE_MOUSE) ? " mouse" : "");
    }
    return usable;
}

void USBPAD_Shutdown(void)
{
    int i;
    if (!Inited) return;
    Inited = 0;
    for (i = 0; i < HCn; ++i)
    {
        if (!HC[i].nports) continue;
        switch (HC[i].type)
        {
        case T_UHCI:
        {
            int k;
            outportw(HC[i].io + U_CMD, 0);
            for (k = 0; k < 20 && !(inportw(HC[i].io + U_STS) & STS_HALT); ++k) wait_us(1000);
            break;
        }
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
