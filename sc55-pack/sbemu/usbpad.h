#ifndef SBEMU_USBPAD_H
#define SBEMU_USBPAD_H
/* USB Xbox 360 controller for SBEMU (polled UHCI host, no USB interrupt used).
 * See usbpad.c for how it works. */
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

/* Finds the USB controllers, takes them over from the BIOS (EHCI ports handed to the UHCI companions)
 * and waits up to wait_ms for a pad. cps = TSC cycles per second. Returns the number of UHCI
 * controllers found (0 = nothing usable). Messages go to *msg (a static string). */
int  USBPAD_Init(uint64_t cps, int wait_ms, const char** msg);
void USBPAD_Poll(void);            /* cheap; call often (sound interrupt, port 201h reads) */
void USBPAD_Shutdown(void);
const USBPAD_State* USBPAD_Get(void);
const char* USBPAD_Describe(void); /* "VID:PID wired on UHCI 2 port 1" etc. */

#endif
