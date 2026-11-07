#ifndef SBEMU_VJOY_H
#define SBEMU_VJOY_H
/* Virtual gameport (port 201h) fed by the USB Xbox 360 pad (usbpad.c). */
#include <stdint.h>

/* axes:  4 names for gameport axes 1-4 (joystick A X/Y, joystick B X/Y), e.g. "LX,LY,RX,RY"
 * btns:  4 names for gameport buttons 1-4, e.g. "A,B,X,Y"
 * keys:  pad buttons typed as keyboard keys, e.g. "ST:1C,BK:01" (set-1 scan codes, E0xx = extended)
 * dz:    stick dead zone in percent
 * Returns 0 if a name was not understood (*bad points at it). */
int  VJOY_Init(uint64_t cps, const char* axes, const char* btns, const char* keys, int dz, const char** bad);
uint32_t VJOY_IOHandler(uint32_t port, uint32_t val, uint32_t out);
void VJOY_Tick(void);   /* from the sound interrupt: USB poll + key presses */
const char* VJOY_Names(void); /* list of the names for the help text */

#endif
