/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
#ifndef SBEMU_USBPAD_H
#define SBEMU_USBPAD_H
/* USB for SBEMU: up to two Xbox 360 pads, USB keyboards and USB mice, all at once (polled UHCI / OHCI /
 * EHCI / xHCI host with hub support, no USB interrupt used). See usbpad.c for how it works. */
#include <stdint.h>

/* button bits, in the order of the pad's own report (bytes 2 and 3) */
#define XPAD_DU    0x0001
#define XPAD_DD    0x0002
#define XPAD_DL    0x0004
#define XPAD_DR    0x0008
#define XPAD_START 0x0010
#define XPAD_BACK  0x0020
#define XPAD_LS    0x0040
#define XPAD_RS    0x0080
#define XPAD_LB    0x0100
#define XPAD_RB    0x0200
#define XPAD_GUIDE 0x0400
#define XPAD_A     0x1000
#define XPAD_B     0x2000
#define XPAD_X     0x4000
#define XPAD_Y     0x8000

typedef struct
{
    volatile int      connected;   /* a pad is enumerated and sending reports */
    volatile uint16_t buttons;     /* XPAD_* bits, 1 = held */
    volatile uint8_t  lt, rt;      /* triggers 0..255 */
    volatile int16_t  lx, ly, rx, ry; /* sticks, Y up = positive (as the pad sends it) */
    volatile uint32_t reports;     /* input reports received */
} USBPAD_State;

#define USBPAD_PADS 2

/* USBPAD_Init flags: which kinds of device to use (pads always) */
#define USBPAD_F_KBD   1
#define USBPAD_F_MOUSE 2
#define USBPAD_F_DISK  4        /* USB disks (mass storage, bulk-only): see docs/USB-STORAGE.md */
#define USBPAD_F_MIDI  8        /* a USB MIDI device (class compliant: audio class, MIDIStreaming), OUT and IN */

/* USBPAD_Have() bits */
#define USBPAD_HAVE_PAD1  1
#define USBPAD_HAVE_PAD2  2
#define USBPAD_HAVE_KBD   4
#define USBPAD_HAVE_MOUSE 8
#define USBPAD_HAVE_DISK  16
#define USBPAD_HAVE_MIDI  32

/* USB disks: every LUN (card reader slot) that is a disk is one drive, numbered 0.. in the order found */
typedef struct
{
    int      ready;                /* medium there and its size known */
    uint32_t blocks, bsize;        /* number of blocks, bytes per block (512..4096) */
    uint16_t vid, pid;
    int      lun;
    uint32_t gen;                  /* differs after an unplug / re-plug */
    char     vendor[9], product[17];
    int      wp;                   /* write protected (MODE SENSE) */
} USBPAD_Disk;
#define USBPAD_E_NODISK   -1
#define USBPAD_E_NOTREADY -2       /* no medium */
#define USBPAD_E_IO       -3
#define USBPAD_E_BUSY     -4       /* called from inside USBPAD_Poll (an interrupt): try again later */
#define USBPAD_E_WP       -5       /* write protected */
int  USBPAD_Disks(void);                                     /* drives now */
const USBPAD_Disk* USBPAD_DiskInfo(int n);                   /* NULL = no drive n; checks the medium again if not ready */
int  USBPAD_DiskRead(int n, uint32_t lba, int count, void* buf);   /* count blocks; 0 or USBPAD_E_* */
int  USBPAD_DiskWrite(int n, uint32_t lba, int count, const void* buf);   /* the same, write */

/* called from inside USBPAD_Poll / PollLight for every new report:
 * keyboard: the 8-byte boot report (modifiers, 0, six key usages); all zero = every key released
 * mouse: buttons (bit 0 left, 1 right, 2 middle), movement (y down = positive), wheel (up = positive) */
typedef void (*USBPAD_KbdFn)(int id, const uint8_t* report);   /* id: which keyboard (0..5) */
typedef void (*USBPAD_MouseFn)(int buttons, int dx, int dy, int dw);

/* Finds the USB controllers, takes them over from the BIOS (EHCI ports handed to companions where there are some)
 * and waits up to wait_ms for what is plugged in to be set up. cps = TSC cycles per second. Returns the number of
 * usable controllers (0 = nothing usable). Messages go to *msg (a static string). */
int  USBPAD_Init(uint64_t cps, int wait_ms, int flags, const char** msg);
/* beta.110: controllers USBPAD_Init leaves alone: "vvvv:dddd" and / or "bb:dd.f" (hex), separated by + , ; or blanks.
 * NULL / "" = the USBSKIP environment variable (the tools). Set before USBPAD_Init; the string must stay. */
void USBPAD_SetSkip(const char* list);
void USBPAD_SetHid(USBPAD_KbdFn kb, USBPAD_MouseFn ms);
void USBPAD_SetLeds(int leds);     /* keyboard LEDs: bit 0 num, 1 caps, 2 scroll (sent on the next full poll) */
void USBPAD_Poll(void);
void USBPAD_PollLight(void);       /* only collects reports: safe inside a game's port read */
void USBPAD_Shutdown(void);
const USBPAD_State* USBPAD_Get(void);       /* pad 1 */
const USBPAD_State* USBPAD_GetPad(int n);   /* 0 = pad 1, 1 = pad 2 */
int  USBPAD_Have(void);            /* USBPAD_HAVE_* of what is connected now */
void USBPAD_Dump(char* out, int size);   /* USBTEST: IN pipes as the controller sees them */
void USBPAD_Rearm(void);                 /* USBTEST: restart every IN pipe */
void USBPAD_SetTraceHook(void (*fn)(const char* s));   /* every trace piece as it is added */
const char* USBPAD_Trace(void);    /* short log of the USB steps, for reports */
void USBPAD_TraceStop(void);       /* stop adding to it (SBEMU, once resident) */
const char* USBPAD_Describe(void); /* last plug-in / unplug, e.g. "Xbox 360 pad 045E:028E on OHCI 2 port 1" */
const char* USBPAD_Devices(void);  /* what is in use now, one line each */
uint32_t USBPAD_Changes(void);     /* counts plug-ins and unplugs */


/* USB MIDI (USBPAD_F_MIDI). MidiByte takes the MPU-401 / MIDI byte stream (running status, SysEx, real time in between)
 * and queues USB-MIDI event packets; nothing touches the USB there (safe inside a port trap). Bytes are dropped while no
 * USB MIDI device is in use. The queue goes out from USBPAD_Poll / PollLight, up to 16 packets per USB frame. */
void USBPAD_MidiByte(uint8_t b);
void USBPAD_MidiReset(void);               /* parser state cleared, queue emptied */
int  USBPAD_MidiPending(void);             /* packets queued, not sent yet */
typedef void (*USBPAD_MidiInFn)(const uint8_t* msg, int len);   /* MIDI IN: 1-3 bytes (one piece of a message), from Poll */
void USBPAD_SetMidiIn(USBPAD_MidiInFn fn);
void USBPAD_MidiStats(uint32_t* sent, uint32_t* dropped, uint32_t* errors, uint32_t* in);   /* packets; NULL = not wanted */
int  USBPAD_MidiDescriptors(const uint8_t** p);
/* beta.121 */
int  USBPAD_MidiBusy(void);                /* MPU-401 status bit 6 for /MUSB: 1 = queue nearly full, the game should wait (bounded) */
int  USBPAD_MidiRoom(void);                /* packets that still fit in the queue */
void USBPAD_SetMidiTap(void (*fn)(uint32_t pk));   /* every event packet as it is put into an OUT transfer (capture) */
typedef struct
{
    int state;                             /* 0 none seen, 1 in use, 2 unplugged, 3 dropped after errors */
    uint16_t vid, pid;
    char name[40], where[48], last[80];    /* product name, "UHCI 1 port 2", last problem ("12s: ...", "" = none) */
    uint32_t sent, dropped, errors, in, queued, busy, busytmo, paced;
} USBPAD_MidiInfoT;
void USBPAD_MidiInfo(USBPAD_MidiInfoT* m);   /* raw device + config descriptors of the last USB MIDI / M-Audio device seen; bytes (0 = none) */
#endif
