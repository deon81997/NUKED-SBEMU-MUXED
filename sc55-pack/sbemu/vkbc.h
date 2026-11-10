#ifndef SBEMU_VKBC_H
#define SBEMU_VKBC_H
/* USB keyboard / mouse -> the PC's keyboard controller (8042): key bytes and PS/2 mouse packets are put in
 * its output buffer (commands D2h / D3h) as if a PS/2 keyboard / mouse had sent them. See vkbc.c. */
#include <stdint.h>

void VKBC_Init(uint64_t cps, int mouse);    /* mouse: answer PS/2 mouse commands (ports 60h / 64h trapped) */
void VKBC_Key(uint16_t code, int down);     /* set 1 scan code, E0xxh = extended */
void VKBC_Bytes(const uint8_t* b, int n);   /* raw set 1 bytes (prefixes included) */
void VKBC_PadBytes(const uint8_t* b, int n);/* one pad layout key (padmap.c emit): counted with the other sources, repeats */
void VKBC_UsbKeys(int id, const uint8_t* report);       /* USB boot keyboard report (USBPAD_KbdFn) */
void VKBC_UsbMouse(int buttons, int dx, int dy, int dw); /* USB mouse movement (USBPAD_MouseFn) */
void VKBC_Tick(void);                       /* sound interrupt: puts queued bytes into the controller, key repeat */
uint32_t VKBC_IOHandler(uint32_t port, uint32_t val, uint32_t out);    /* ports 60h, 64h */
int  VKBC_Leds(void);                       /* BIOS lock keys as USB LED bits */
void VKBC_SetBios(uint32_t lin);            /* USB mouse through the BIOS mouse services: lin = INT 15h stub data */
int  VKBC_MouseSeen(void);
uint32_t VKBC_Unstuck(void);                /* keyboard bytes put in again after 50 ms unread (lost IRQ 1) */                  /* a program has enabled the emulated PS/2 mouse */

#endif
