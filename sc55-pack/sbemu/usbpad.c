/* USB Xbox 360 controller support for SBEMU.
 *
 * A small USB host for one device, written for SBEMU's resident setting:
 *   - no USB interrupt at all. The controllers' interrupt enables (and UHCI's PIRQ routing) are
 *     switched off; USBPAD_Poll() looks at the transfer descriptors in memory instead. It is called
 *     from the sound card interrupt and from every read of the emulated gameport (port 201h), so it
 *     never shares an IRQ line with the sound chip (on ICH boards the UHCI and AC'97 often share one).
 *   - UHCI (Intel ICH / VIA chipsets: the Dell D810's ICH6 has four) and OHCI (NEC uPD720100/720101
 *     PCI cards, SiS, ALi, nVidia, AMD, Apple ...). EHCI controllers are taken from the BIOS
 *     (USBLEGSUP handoff), stopped, and their CONFIGFLAG cleared, so every port is routed to its
 *     UHCI / OHCI companion: the pad is a full-speed device and works there. OHCI controllers that
 *     the BIOS's SMM code owns are taken over with the ownership-change request (OCR).
 *   - one controller carries the pad at a time. UHCI: frame list -> interrupt QH (the pad's IN
 *     endpoint, tried every frame) -> control QH. OHCI: HCCA interrupt table -> interrupt ED, plus a
 *     control ED; the other OHCI controllers stay operational (so their root hubs keep reporting
 *     plug-ins) with an empty HCCA and no lists.
 *   - enumeration is a small state machine (debounce, reset, recovery are waited by time stamp, so
 *     hot-plugging does not stall the sound interrupt for long); the control transfers themselves are
 *     synchronous and take a few milliseconds.
 *
 * Supported pads: anything with the Xbox 360 interface (vendor class FFh, subclass 5Dh, protocol 01h:
 * Microsoft wired pad and the third-party wired pads in x360c.inf), and the Microsoft wireless
 * receiver (protocol 81h, first pad slot only; untested). Not supported: Xbox One / Series pads,
 * pads behind a USB hub, xHCI controllers, CardBus cards (the BIOS leaves the slot unconfigured).
 *
 * Report layout (wired, 20 bytes): 00 14, buttons lo (dpad U D L R, start, back, LS, RS),
 * buttons hi (LB, RB, guide, -, A, B, X, Y), LT, RT, LX, LY, RX, RY (int16 little endian, Y up = +).
 * The wireless receiver wraps the same layout 4 bytes further on. Layout from Linux drivers/input/
 * joystick/xpad.c; UHCI / OHCI programming after the UHCI 1.1 and OHCI 1.0a specs, Linux
 * pci-quirks.c (BIOS handoff) and USBDDOS (crazii, GPL-2.0). */
#include <stdio.h>
#include <string.h>
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

/* ------------------------------------------------------------------ UHCI registers */
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

/* UHCI TD control/status */
#define TD_ACTIVE (1UL << 23)
#define TD_STALL  (1UL << 22)
#define TD_ERRMASK 0x00760000UL         /* stall, buffer, babble, crc/timeout, bitstuff (not NAK) */
#define TD_LS     (1UL << 26)
#define TD_CERR3  (3UL << 27)
#define PID_SETUP 0x2D
#define PID_IN    0x69
#define PID_OUT   0xE1
#define LP_T      1UL                   /* link: terminate */
#define LP_Q      2UL                   /* link: points to a QH */
#define LP_VF     4UL                   /* depth first */

/* ------------------------------------------------------------------ OHCI registers */
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
#define OP_CHG    0x001F0000UL          /* CSC PESC PSSC OCIC PRSC, write 1 to clear */
#define OP_PRSC   (1UL << 20)
/* OHCI ED dword 0 / TD dword 0 */
#define ED_SKIP   (1UL << 14)
#define ED_LS     (1UL << 13)
#define ED_HALT   1UL
#define OTD_CC_NA (0xFUL << 28)         /* "not accessed" */
#define OTD_DI7   (7UL << 21)
#define OTD_R     (1UL << 18)
#define OTD_SETUP (0UL << 19)
#define OTD_OUT   (1UL << 19)
#define OTD_IN    (2UL << 19)
#define OTD_T0    (2UL << 24)           /* DATA0 from the TD */
#define OTD_T1    (3UL << 24)           /* DATA1 from the TD */
#define OTD_CC(x) ((x) >> 28)

#define MAX_HC    8
#define MAX_PORT  15
#define NTD       40

enum { T_UHCI, T_OHCI };
typedef struct { int type, idx, nports; uint16_t io; uint32_t mmio; uint8_t bus, dev, func; } HCI;
static HCI      HC[MAX_HC];
static int      HCn, UhciN, OhciN;

static xmsmem_t Mem;
static volatile uint32_t* FL;           /* UHCI frame list, 1024 entries */
static volatile uint32_t* HCCA;         /* OHCI communication area of the controller with the pad */
static volatile uint32_t* HCCA0;        /* empty one for the other OHCI controllers */
static volatile uint32_t* QHI;          /* UHCI interrupt QH */
static volatile uint32_t* QHC;          /* UHCI control QH */
static volatile uint32_t* EDI;          /* OHCI interrupt ED */
static volatile uint32_t* EDC;          /* OHCI control ED */
static volatile uint32_t* TDI;          /* the pad's IN TD (OHCI: two, used in turn) */
static volatile uint32_t* TD;           /* NTD control TDs, 8 dwords each */
static uint8_t*  CBuf;                  /* control data, 512 bytes */
static uint8_t*  IBuf;                  /* IN report, 64 */
static uint8_t*  OBuf;                  /* OUT report, 32 */
static uint32_t  PBase;                 /* physical address of Mem */
#define PHYS(p) (PBase + (uint32_t)((volatile uint8_t*)(p) - (volatile uint8_t*)Mem.linearptr))

static uint64_t  Cps;
static USBPAD_State St;
static char      Desc[96];

/* state machine */
enum { S_SCAN, S_DEBOUNCE, S_RESET, S_RECOVER, S_RUN };
static int       S, Cur = -1, CurPort;
static uint64_t  Until, NextScan;
static uint32_t  Ignore[MAX_HC];        /* bit per port: not a pad, skip until unplugged */
static uint8_t   Fails[MAX_HC][MAX_PORT]; /* failed enumerations in a row (3 = give up until unplugged) */
static int       Ls;                    /* low speed device */
static int       Mps0 = 8, InEp, OutEp, InMps = 32, InTog, OutTog, Wireless, WlPresent;
static int       OCur;                  /* OHCI: which of the two IN TDs is the empty tail */
static volatile int Busy;
static int       Inited;

static inline uint64_t tsc(void) { uint32_t lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }
static uint64_t us2c(uint32_t us) { return Cps * us / 1000000ULL; }
static void wait_us(uint32_t us) { uint64_t e = tsc() + us2c(us); while (tsc() < e) {} }

static uint32_t ord(int h, int r) { return DPMI_LoadD(HC[h].mmio + r); }
static void owr(int h, int r, uint32_t v) { DPMI_StoreD(HC[h].mmio + r, v); }
static const char* tname(int h) { return HC[h].type == T_OHCI ? "OHCI" : "UHCI"; }

/* ------------------------------------------------------------------ root hub ports */
static uint16_t prt(int hc, int p) { return (uint16_t)(HC[hc].io + U_PORT + p * 2); }
/* UHCI: write a port register without clearing the write-1-to-clear change bits by accident */
static void port_set(int hc, int p, uint16_t set, uint16_t clr)
{
    uint16_t v = inportw(prt(hc, p));
    v &= (uint16_t)~(P_CSC | P_PEC | clr);
    v &= 0x1FF5 & (uint16_t)~0x0080;    /* keep defined writable bits only */
    outportw(prt(hc, p), (uint16_t)(v | set));
}

enum { PS_CCS = 1, PS_PE = 2, PS_LS = 4, PS_GONE = 8 };
static int pstat(int h, int p)
{
    if (HC[h].type == T_UHCI)
    {
        uint16_t v = inportw(prt(h, p));
        if (v == 0xFFFF) return PS_GONE;
        return ((v & P_CCS) ? PS_CCS : 0) | ((v & P_PE) ? PS_PE : 0) | ((v & P_LSDA) ? PS_LS : 0);
    }
    else
    {
        uint32_t v = ord(h, O_PORT + p * 4);
        if (v == 0xFFFFFFFFUL) return PS_GONE;
        return ((v & OP_CCS) ? PS_CCS : 0) | ((v & OP_PES) ? PS_PE : 0) | ((v & OP_LSDA) ? PS_LS : 0);
    }
}
static void p_clear_change(int h, int p)
{
    if (HC[h].type == T_UHCI) port_set(h, p, P_CSC | P_PEC, 0);
    else owr(h, O_PORT + p * 4, OP_CHG);
}
static void p_reset_start(int h, int p)
{
    if (HC[h].type == T_UHCI) port_set(h, p, P_PR, P_PE);
    else owr(h, O_PORT + p * 4, OP_PRS);              /* the OHCI root hub times the 10 ms reset itself */
}
static int p_reset_end(int h, int p)                  /* 1 = port enabled */
{
    int n;
    if (HC[h].type == T_UHCI)
    {
        port_set(h, p, 0, P_PR);
        wait_us(100);
        for (n = 0; n < 20 && !(inportw(prt(h, p)) & P_PE); ++n) { port_set(h, p, P_PE, 0); wait_us(500); }
        port_set(h, p, P_CSC | P_PEC, 0);
    }
    else
    {
        for (n = 0; n < 50 && (ord(h, O_PORT + p * 4) & OP_PRS); ++n) wait_us(1000);
        owr(h, O_PORT + p * 4, OP_CHG);
        for (n = 0; n < 20 && !(ord(h, O_PORT + p * 4) & OP_PES); ++n) { owr(h, O_PORT + p * 4, OP_PES); wait_us(500); }
    }
    return (pstat(h, p) & PS_PE) != 0;
}
static void p_disable(int h, int p)
{
    if (HC[h].type == T_UHCI) port_set(h, p, 0, P_PE);
    else owr(h, O_PORT + p * 4, OP_CCS);               /* bit 0 written = ClearPortEnable */
}

/* ------------------------------------------------------------------ PCI scan */
static void pci_scan(void (*fn)(uint8_t, uint8_t, uint8_t, uint32_t))
{
    int bus, dev, func;
    for (bus = 0; bus < 8; ++bus)
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

static int EhciN;
static void ehci_release(uint8_t b, uint8_t d, uint8_t f, uint32_t cls)
{
    uint32_t bar, lin, caplen, hcc, op;
    int eecp, guard = 0, i;
    if (cls != 0x0C0320) return;
    ++EhciN;
    PCI_WriteWord(b, d, f, 0x04, (uint16_t)(PCI_ReadWord(b, d, f, 0x04) | 0x02));   /* memory decoding */
    bar = PCI_ReadDWord(b, d, f, 0x10) & ~0xFUL;
    eecp = 0;
    lin = bar ? DPMI_MapMemory(bar, 0x1000) : 0;
    if (lin)
    {
        hcc = DPMI_LoadD(lin + 8);
        eecp = (hcc >> 8) & 0xFF;
    }
    while (eecp >= 0x40 && guard++ < 16)  /* BIOS handoff */
    {
        uint32_t cap = PCI_ReadDWord(b, d, f, (uint8_t)eecp);
        if ((cap & 0xFF) == 1)
        {
            if (cap & (1UL << 16))
            {
                PCI_WriteByte(b, d, f, (uint8_t)(eecp + 3), 1);
                for (i = 0; i < 1000 && (PCI_ReadByte(b, d, f, (uint8_t)(eecp + 2)) & 1); ++i) wait_us(1000);
                if (PCI_ReadByte(b, d, f, (uint8_t)(eecp + 2)) & 1) PCI_WriteByte(b, d, f, (uint8_t)(eecp + 2), 0); /* BIOS did not let go */
            }
            else PCI_WriteByte(b, d, f, (uint8_t)(eecp + 3), 1);
            PCI_WriteDWord(b, d, f, (uint8_t)(eecp + 4), 0xE0000000UL); /* SMIs off, clear their status */
        }
        eecp = (cap >> 8) & 0xFF;
    }
    if (lin)
    {
        caplen = DPMI_LoadD(lin) & 0xFF;
        op = lin + caplen;
        DPMI_StoreD(op + 0, DPMI_LoadD(op + 0) & ~1UL);           /* stop */
        for (i = 0; i < 50 && !(DPMI_LoadD(op + 4) & (1UL << 12)); ++i) wait_us(1000);
        DPMI_StoreD(op + 8, 0);                                   /* no interrupts */
        DPMI_StoreD(op + 0x40, 0);                                /* CONFIGFLAG=0: all ports to the companions */
        DPMI_UnmappMemory(lin);
    }
}

static void hc_found(uint8_t b, uint8_t d, uint8_t f, uint32_t cls)
{
    HCI* h;
    if (HCn >= MAX_HC) return;
    h = &HC[HCn];
    memset(h, 0, sizeof(*h));
    h->bus = b; h->dev = d; h->func = f;
    if (cls == 0x0C0300)
    {
        uint32_t bar = PCI_ReadDWord(b, d, f, 0x20);
        if (!(bar & 1) || !(bar & 0xFFE0)) return;
        h->type = T_UHCI; h->io = (uint16_t)(bar & 0xFFE0); h->nports = 2; h->idx = ++UhciN;
    }
    else if (cls == 0x0C0310)
    {
        uint32_t bar = PCI_ReadDWord(b, d, f, 0x10);
        if ((bar & 1) || !(bar & ~0xFFFUL)) return;
        PCI_WriteWord(b, d, f, 0x04, (uint16_t)(PCI_ReadWord(b, d, f, 0x04) | 0x06)); /* memory + bus master */
        h->mmio = DPMI_MapMemory(bar & ~0xFFFUL, 0x1000);
        if (!h->mmio) return;
        h->type = T_OHCI; h->idx = ++OhciN;
    }
    else return;
    ++HCn;
}

/* ------------------------------------------------------------------ controller reset / run */
static void uhci_reset(int i)
{
    HCI* h = &HC[i];
    int n;
    PCI_WriteWord(h->bus, h->dev, h->func, UHCI_LEGSUP, 0x8F00);  /* legacy off, SMIs off, PIRQ off */
    PCI_WriteWord(h->bus, h->dev, h->func, 0x04, (uint16_t)(PCI_ReadWord(h->bus, h->dev, h->func, 0x04) | 0x05)); /* IO + bus master */
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
    uint32_t fi, rha, wait;
    int n, p;
    if (ord(i, O_CONTROL) & OC_IR)                            /* the BIOS's SMM code owns it: ask for it */
    {
        owr(i, O_CMDSTS, OS_OCR);
        for (n = 0; n < 1000 && (ord(i, O_CONTROL) & OC_IR); ++n) wait_us(1000);
    }
    owr(i, O_INTDIS, 0xC000007FUL);                           /* no interrupts (MIE and all sources) */
    fi = ord(i, O_FMINT) & 0x3FFF;
    if (fi < 11000 || fi > 13000) fi = 11999;
    owr(i, O_CONTROL, 0);                                     /* USB reset state: resets the ports too */
    wait_us(50000);
    owr(i, O_CMDSTS, OS_HCR);
    for (n = 0; n < 100 && (ord(i, O_CMDSTS) & OS_HCR); ++n) wait_us(10);
    /* now in suspend: set up and go operational within 2 ms */
    owr(i, O_FMINT, ((ord(i, O_FMINT) & 0x80000000UL) ^ 0x80000000UL) | (((fi - 210) * 6 / 7) << 16) | fi);
    owr(i, O_PERSTART, fi * 9 / 10);
    owr(i, O_LSTHRESH, 0x628);
    owr(i, O_HCCA, PHYS(HCCA0));
    owr(i, O_CTLHEAD, 0); owr(i, O_CTLCUR, 0);
    owr(i, O_BLKHEAD, 0); owr(i, O_BLKCUR, 0);
    owr(i, O_INTSTS, 0xFFFFFFFFUL);
    owr(i, O_CONTROL, OC_OPER | 3);                           /* operational, no lists yet */
    rha = ord(i, O_RHDA);
    HC[i].nports = (int)(rha & 0xFF);
    if (HC[i].nports > MAX_PORT) HC[i].nports = MAX_PORT;
    owr(i, O_RHSTS, 1UL << 16);                               /* global port power on (LPSC) */
    for (p = 0; p < HC[i].nports; ++p) owr(i, O_PORT + p * 4, OP_PPS); /* and per port */
    wait = (rha >> 24) * 2;                                   /* power-on to power-good time */
    HC[i].type = T_OHCI;
    if (wait < 20) wait = 20;
    if (wait > 100) wait = 100;
    wait_us(wait * 1000);
}

static void hc_run(int i, int on)
{
    if (HC[i].type == T_UHCI)
    {
        uint16_t io = HC[i].io;
        if (on)
        {
            outportw(io + U_INTR, 0);
            outportw(io + U_FRNUM, 0);
            outportl(io + U_FLBASE, PHYS(FL));
            outportb(io + U_SOF, 0x40);
            outportw(io + U_STS, 0x3F);
            outportw(io + U_CMD, CMD_RS | CMD_CF | CMD_MAXP);
        }
        else
        {
            int n;
            outportw(io + U_CMD, 0);
            for (n = 0; n < 20 && !(inportw(io + U_STS) & STS_HALT); ++n) wait_us(1000);
        }
    }
    else
    {
        /* OHCI stays operational (its root hub keeps working); only our lists come and go */
        if (on)
        {
            EDI[0] |= ED_SKIP; EDC[0] |= ED_SKIP;
            owr(i, O_HCCA, PHYS(HCCA));
            owr(i, O_CTLHEAD, PHYS(EDC)); owr(i, O_CTLCUR, 0);
            owr(i, O_CONTROL, (ord(i, O_CONTROL) & ~(OC_PLE | OC_CLE | OC_BLE)) | OC_PLE | OC_CLE);
        }
        else
        {
            EDI[0] |= ED_SKIP; EDC[0] |= ED_SKIP;
            owr(i, O_CONTROL, ord(i, O_CONTROL) & ~(OC_PLE | OC_CLE | OC_BLE));
            wait_us(2000);                                    /* the lists are left at a frame boundary */
            owr(i, O_CTLHEAD, 0); owr(i, O_CTLCUR, 0);
            owr(i, O_HCCA, PHYS(HCCA0));
        }
    }
}

/* ------------------------------------------------------------------ UHCI transfers */
static uint32_t td_cs(void) { return TD_CERR3 | TD_ACTIVE | (Ls ? TD_LS : 0); }
static uint32_t td_tok(int pid, int addr, int ep, int tog, int len)
{
    return ((uint32_t)(len ? len - 1 : 0x7FF) << 21) | ((uint32_t)(tog & 1) << 19) | ((uint32_t)ep << 15) | ((uint32_t)addr << 8) | (uint32_t)pid;
}
static volatile uint32_t* tdn(int i) { return TD + i * 8; }

/* runs TDs 0..n-1 on the control QH; returns 0 ok, -1 error/timeout */
static int u_run_tds(int n, uint32_t timeout_us)
{
    uint64_t end = tsc() + us2c(timeout_us);
    int i;
    QHC[1] = PHYS(tdn(0));
    for (;;)
    {
        int done = 1;
        for (i = 0; i < n; ++i)
        {
            uint32_t cs = tdn(i)[1];
            if (cs & TD_ACTIVE) { done = 0; break; }
            if (cs & TD_ERRMASK) { QHC[1] = LP_T; return -1; }
        }
        if (done) { QHC[1] = LP_T; return 0; }
        if (tsc() > end) { QHC[1] = LP_T; wait_us(1000); return -1; }
    }
}

static int u_control(int addr, int len, int in)
{
    int n = 0, tog = 1, left = len;
    uint8_t* setup = CBuf;
    uint8_t* data = CBuf + 16;
    tdn(n)[1] = td_cs(); tdn(n)[2] = td_tok(PID_SETUP, addr, 0, 0, 8); tdn(n)[3] = PHYS(setup); ++n;
    while (left > 0 && n < NTD - 1)
    {
        int c = left > Mps0 ? Mps0 : left;
        tdn(n)[1] = td_cs(); tdn(n)[2] = td_tok(in ? PID_IN : PID_OUT, addr, 0, tog, c); tdn(n)[3] = PHYS(data + (len - left)); ++n;
        tog ^= 1; left -= c;
    }
    tdn(n)[1] = td_cs(); tdn(n)[2] = td_tok(in && len ? PID_OUT : PID_IN, addr, 0, 1, 0); tdn(n)[3] = 0; ++n;
    {
        int i;
        for (i = 0; i < n; ++i) tdn(i)[0] = (i + 1 < n) ? (PHYS(tdn(i + 1)) | LP_VF) : LP_T;
    }
    return u_run_tds(n, 300000);
}

/* ------------------------------------------------------------------ OHCI transfers */
static void otd(volatile uint32_t* t, uint32_t flags, uint32_t buf, int len, volatile uint32_t* next)
{
    t[0] = OTD_CC_NA | OTD_DI7 | flags;
    t[1] = len ? buf : 0;
    t[2] = next ? PHYS(next) : 0;
    t[3] = len ? buf + len - 1 : 0;
}

/* runs control-ED TDs 0..n-1 (tdn(n) is the empty tail) for endpoint ep; 0 ok, -1 error/timeout */
static int o_run_tds(int addr, int ep, int mps, int n, uint32_t timeout_us)
{
    uint64_t end;
    int i, r = -1;
    volatile uint32_t* tail = tdn(n);
    tail[0] = tail[1] = tail[2] = tail[3] = 0;
    EDC[0] = ED_SKIP | (uint32_t)addr | ((uint32_t)ep << 7) | (Ls ? ED_LS : 0) | ((uint32_t)mps << 16);
    EDC[1] = PHYS(tail);
    EDC[2] = PHYS(tdn(0));
    EDC[3] = 0;
    EDC[0] &= ~ED_SKIP;
    owr(Cur, O_CMDSTS, OS_CLF);
    end = tsc() + us2c(timeout_us);
    for (;;)
    {
        int done = 1, err = 0;
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
    EDC[0] |= ED_SKIP;
    wait_us(1100);                                            /* let the controller leave the ED */
    EDC[2] = PHYS(tail);                                      /* empty again, halt and toggle cleared */
    return r;
}

static int o_control(int addr, int len, int in)
{
    int n = 0;
    uint8_t* setup = CBuf;
    uint8_t* data = CBuf + 16;
    otd(tdn(n), OTD_SETUP | OTD_T0, PHYS(setup), 8, tdn(n + 1)); ++n;
    if (len) { otd(tdn(n), (in ? OTD_IN : OTD_OUT) | OTD_T1 | OTD_R, PHYS(data), len, tdn(n + 1)); ++n; }
    otd(tdn(n), (in && len ? OTD_OUT : OTD_IN) | OTD_T1, 0, 0, tdn(n + 1)); ++n;
    return o_run_tds(addr, 0, Mps0, n, 300000);
}

/* ------------------------------------------------------------------ transfers, either controller */
static int control(int addr, uint8_t rt, uint8_t req, uint16_t val, uint16_t idx, uint16_t len)
{
    uint8_t* setup = CBuf;              /* 8 bytes set-up packet, data from CBuf+16 */
    setup[0] = rt; setup[1] = req; setup[2] = (uint8_t)val; setup[3] = (uint8_t)(val >> 8);
    setup[4] = (uint8_t)idx; setup[5] = (uint8_t)(idx >> 8); setup[6] = (uint8_t)len; setup[7] = (uint8_t)(len >> 8);
    return HC[Cur].type == T_UHCI ? u_control(addr, len, (rt & 0x80) != 0) : o_control(addr, len, (rt & 0x80) != 0);
}

static int send_out(const uint8_t* d, int len)
{
    int r;
    if (!OutEp) return -1;
    memcpy(OBuf, d, len);
    if (HC[Cur].type == T_UHCI)
    {
        tdn(0)[0] = LP_T; tdn(0)[1] = td_cs(); tdn(0)[2] = td_tok(PID_OUT, 1, OutEp, OutTog, len); tdn(0)[3] = PHYS(OBuf);
        r = u_run_tds(1, 100000);
    }
    else
    {
        otd(tdn(0), OTD_OUT | (OutTog ? OTD_T1 : OTD_T0), PHYS(OBuf), len, tdn(1));
        r = o_run_tds(1, OutEp, 64, 1, 100000);
    }
    if (r != 0) return -1;
    OutTog ^= 1;
    return 0;
}

/* OHCI interrupt ED: two TDs used in turn. The empty tail TD is filled and a new empty tail
 * appended (the only change the spec allows while the controller runs the ED). */
static volatile uint32_t* o_tdi(int k) { return TDI + k * 4; }

static void arm_in(void)
{
    if (HC[Cur].type == T_UHCI)
    {
        TDI[0] = LP_T;
        TDI[1] = td_cs();
        TDI[2] = td_tok(PID_IN, 1, InEp, InTog, InMps);
        TDI[3] = PHYS(IBuf);
        QHI[1] = PHYS(TDI);
    }
    else
    {
        volatile uint32_t* t = o_tdi(OCur);
        volatile uint32_t* nt = o_tdi(OCur ^ 1);
        nt[0] = nt[1] = nt[2] = nt[3] = 0;
        otd(t, OTD_IN | OTD_R, PHYS(IBuf), InMps, nt);        /* toggle from the ED */
        EDI[1] = PHYS(nt);
        OCur ^= 1;
    }
}

/* first arming after enumeration */
static void start_in(void)
{
    if (HC[Cur].type == T_OHCI)
    {
        OCur = 0;
        o_tdi(0)[0] = o_tdi(0)[1] = o_tdi(0)[2] = o_tdi(0)[3] = 0;
        EDI[0] = ED_SKIP | 1 | ((uint32_t)InEp << 7) | (2UL << 11) | (Ls ? ED_LS : 0) | ((uint32_t)InMps << 16);
        EDI[1] = PHYS(o_tdi(0));
        EDI[2] = PHYS(o_tdi(0));
        EDI[3] = 0;
        EDI[0] &= ~ED_SKIP;
    }
    arm_in();
}

/* the pad's IN transfer: 0 = still waiting, >0 = bytes received (+1), -1 = error, -2 = stall */
static int poll_in(void)
{
    if (HC[Cur].type == T_UHCI)
    {
        uint32_t cs = TDI[1];
        int len;
        if (cs & TD_ACTIVE) return 0;
        if (cs & TD_ERRMASK) return (cs & TD_STALL) ? -2 : -1;
        len = (int)((cs + 1) & 0x7FF);
        if (len == 0x800) len = 0;
        InTog ^= 1;
        return len + 1;
    }
    else
    {
        volatile uint32_t* t = o_tdi(OCur ^ 1);              /* the one in flight */
        uint32_t cc = OTD_CC(t[0]);
        int len;
        if (cc >= 0xE) return 0;
        if (cc)
        {
            EDI[2] = PHYS(o_tdi(OCur));                       /* clear the halt (and the toggle) */
            return cc == 4 ? -2 : -1;
        }
        len = t[1] ? (int)(t[1] - PHYS(IBuf)) : InMps;        /* CBP left behind = first byte not filled */
        return len + 1;
    }
}

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

/* enumerate the device on HC[Cur] port CurPort (port already reset and enabled):
 * 1 = pad running, 0 = failed (retried), -1 = some other device (left alone until unplugged) */
static int enumerate(void)
{
    uint8_t* d = CBuf + 16;
    int total, pos, cfgval, found = 0;
    uint16_t vid, pid;
    Mps0 = 8;
    if (control(0, 0x80, 6, 0x0100, 0, 8) != 0) return 0;        /* device descriptor, first 8 bytes */
    Mps0 = d[7] >= 8 && d[7] <= 64 ? d[7] : 8;
    if (control(0, 0x00, 5, 1, 0, 0) != 0) return 0;              /* SET_ADDRESS 1 */
    wait_us(5000);
    if (control(1, 0x80, 6, 0x0100, 0, 18) != 0) return 0;
    vid = (uint16_t)(d[8] | d[9] << 8); pid = (uint16_t)(d[10] | d[11] << 8);
    if (control(1, 0x80, 6, 0x0200, 0, 9) != 0) return 0;         /* configuration descriptor */
    total = d[2] | d[3] << 8;
    if (total > 400) total = 400;
    if (control(1, 0x80, 6, 0x0200, 0, (uint16_t)total) != 0) return 0;
    cfgval = d[5];
    InEp = OutEp = 0; Wireless = 0;
    for (pos = 0; pos + 2 <= total && d[pos] >= 2; pos += d[pos])
    {
        uint8_t type = d[pos + 1];
        if (type == 4)                                           /* interface */
        {
            if (found) break;                                    /* endpoints of the pad interface are done */
            if (d[pos + 5] == 0xFF && d[pos + 6] == 0x5D && (d[pos + 7] == 0x01 || d[pos + 7] == 0x81))
            { found = 1; Wireless = d[pos + 7] == 0x81; }
        }
        else if (type == 5 && found && (d[pos + 3] & 3) == 3)    /* interrupt endpoint */
        {
            if (d[pos + 2] & 0x80) { if (!InEp) { InEp = d[pos + 2] & 0x0F; InMps = d[pos + 4]; } }
            else if (!OutEp) OutEp = d[pos + 2] & 0x0F;
        }
    }
    if (!found || !InEp)
    {
        snprintf(Desc, sizeof(Desc), "USB device %04X:%04X on %s %d port %d is not an Xbox 360 pad", vid, pid,
            tname(Cur), HC[Cur].idx, CurPort + 1);
        return -1;
    }
    if (InMps < 1 || InMps > 64) InMps = 32;
    if (control(1, 0x00, 9, (uint16_t)cfgval, 0, 0) != 0) return 0; /* SET_CONFIGURATION */
    InTog = OutTog = 0;
    if (Wireless)
    {
        static const uint8_t inq[12] = { 0x08, 0x00, 0x0F, 0xC0 }; /* ask the receiver whether a pad is on */
        send_out(inq, 12);
        WlPresent = 0;
    }
    else pad_leds();
    snprintf(Desc, sizeof(Desc), "Xbox 360 %s %04X:%04X on %s %d port %d", Wireless ? "wireless receiver" : "pad",
        vid, pid, tname(Cur), HC[Cur].idx, CurPort + 1);
    start_in();
    return 1;
}

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

static void drop_device(int ignore)
{
    QHI[1] = LP_T; QHC[1] = LP_T;
    EDI[0] |= ED_SKIP; EDC[0] |= ED_SKIP;
    if (Cur >= 0)
    {
        if (ignore == 2 || (ignore && ++Fails[Cur][CurPort] >= 3)) Ignore[Cur] |= 1u << CurPort;
        p_disable(Cur, CurPort);
        hc_run(Cur, 0);
    }
    clear_pad();
    Cur = -1; S = S_SCAN;
    NextScan = tsc() + us2c(200000);
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
        for (h = 0; h < HCn; ++h)
            for (p = 0; p < HC[h].nports; ++p)
            {
                int v = pstat(h, p);
                if (v & PS_GONE) continue;
                if (!(v & PS_CCS)) { Ignore[h] &= ~(1u << p); Fails[h][p] = 0; continue; }
                if (Ignore[h] & (1u << p)) continue;
                Cur = h; CurPort = p;
                p_clear_change(h, p);
                hc_run(h, 1);
                S = S_DEBOUNCE; Until = t + us2c(100000);
                return;
            }
        return;
    }
    case S_DEBOUNCE:
        if (t < Until) return;
        if (!(pstat(Cur, CurPort) & PS_CCS)) { drop_device(0); return; }
        Ls = (pstat(Cur, CurPort) & PS_LS) != 0;
        p_reset_start(Cur, CurPort);
        S = S_RESET; Until = t + us2c(55000);
        return;
    case S_RESET:
        if (t < Until) return;
        if (!p_reset_end(Cur, CurPort)) { drop_device(1); return; }
        S = S_RECOVER; Until = t + us2c(20000);
        return;
    case S_RECOVER:
        if (t < Until) return;
        {
            int r = enumerate();
            if (r <= 0) { drop_device(r < 0 ? 2 : 1); return; }
        }
        Fails[Cur][CurPort] = 0;
        clear_pad();
        St.connected = !Wireless;
        S = S_RUN;
        return;
    case S_RUN:
    {
        int v = pstat(Cur, CurPort), r;
        if ((v & PS_GONE) || !(v & PS_CCS) || !(v & PS_PE)) { drop_device(0); return; }
        r = poll_in();
        if (r == 0) return;
        if (r < 0)
        {
            if (r == -2) control(1, 0x02, 1, 0, (uint16_t)(0x80 | InEp), 0); /* CLEAR_FEATURE halt */
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
                if (IBuf[0] == 0x08)                              /* pad came or went */
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
    step();
    Busy = 0;
}

const USBPAD_State* USBPAD_Get(void) { return &St; }
const char* USBPAD_Describe(void) { return Desc; }

int USBPAD_Init(uint64_t cps, int wait_ms, const char** msg)
{
    int i;
    uint8_t* base;
    static char m[160];
    *msg = m;
    Cps = cps ? cps : 1000000000ULL;
    HCn = UhciN = OhciN = EhciN = 0;
    pci_scan(hc_found);
    if (!HCn)
    {
        snprintf(m, sizeof(m), "no UHCI or OHCI USB controller found (xHCI is not supported)");
        return 0;
    }
    if (!pds_dpmi_xms_allocmem(&Mem, 8192))
    {
        snprintf(m, sizeof(m), "out of XMS memory");
        return 0;
    }
    base = (uint8_t*)Mem.linearptr;
    PBase = (uint32_t)(uintptr_t)Mem.physicalptr;
    memset(base, 0, 8192);
    FL    = (volatile uint32_t*)base;                    /* 4K-aligned */
    HCCA  = (volatile uint32_t*)(base + 4096);           /* 256-aligned */
    HCCA0 = (volatile uint32_t*)(base + 4352);
    QHI   = (volatile uint32_t*)(base + 4608);
    QHC   = (volatile uint32_t*)(base + 4624);
    EDI   = (volatile uint32_t*)(base + 4640);
    EDC   = (volatile uint32_t*)(base + 4656);
    TDI   = (volatile uint32_t*)(base + 4672);           /* 2 x 16 bytes */
    TD    = (volatile uint32_t*)(base + 4704);           /* NTD x 32 bytes, to 5984 */
    CBuf  = base + 6144;
    IBuf  = base + 6656;
    OBuf  = base + 6720;
    QHI[0] = PHYS(QHC) | LP_Q; QHI[1] = LP_T;
    QHC[0] = LP_T;             QHC[1] = LP_T;
    for (i = 0; i < 1024; ++i) FL[i] = PHYS(QHI) | LP_Q;
    EDI[0] = EDC[0] = ED_SKIP;
    for (i = 0; i < 32; ++i) HCCA[i] = PHYS(EDI);        /* every interrupt slot: the pad's ED */

    pci_scan(ehci_release);
    for (i = 0; i < HCn; ++i)
        if (HC[i].type == T_UHCI) uhci_reset(i); else ohci_reset(i);
    wait_us(20000);                                      /* let the ports see their devices again */
    clear_pad();
    strcpy(Desc, "no pad connected");
    S = S_SCAN; Cur = -1; NextScan = 0;
    memset(Ignore, 0, sizeof(Ignore)); memset(Fails, 0, sizeof(Fails));
    Inited = 1;
    {
        uint64_t end = tsc() + us2c((uint32_t)wait_ms * 1000);
        while (tsc() < end && !(S == S_RUN && (St.connected || Wireless))) USBPAD_Poll();
        /* first report (the pad sends one as soon as it is polled) */
        end = tsc() + us2c(50000);
        while (S == S_RUN && !St.reports && tsc() < end) USBPAD_Poll();
    }
    snprintf(m, sizeof(m), "%d UHCI + %d OHCI controller(s)%s, %s", UhciN, OhciN,
        EhciN ? " (EHCI ports handed over)" : "", Desc);
    return HCn;
}

void USBPAD_Shutdown(void)
{
    int i;
    if (!Inited) return;
    Inited = 0;
    QHI[1] = LP_T; QHC[1] = LP_T;
    EDI[0] |= ED_SKIP; EDC[0] |= ED_SKIP;
    for (i = 0; i < HCn; ++i)
    {
        if (HC[i].type == T_UHCI) hc_run(i, 0);
        else { owr(i, O_CONTROL, 0); owr(i, O_HCCA, 0); }   /* USB reset state, off the bus */
    }
    pds_dpmi_xms_freemem(&Mem);
}
