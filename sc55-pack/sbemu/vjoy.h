#ifndef SBEMU_VJOY_H
#define SBEMU_VJOY_H
/* Virtual gameport (port 201h) fed by the USB Xbox 360 pad (usbpad.c). */
#include <stdint.h>

/* axes:  4 names for gameport axes 1-4 (joystick A X/Y, joystick B X/Y), e.g. "LX,LY,RX,RY"
 * btns:  4 names for gameport buttons 1-4, e.g. "A,B,X,Y"
 * keys:  pad buttons typed as keyboard keys, e.g. "ST:1C,BK:01" (set-1 scan codes, E0xx = extended)
 * dz:    stick dead zone in percent
 * Returns 0 if a name was not understood (*bad points at it). */
int  VJOY_Init(uint64_t cps, const char* axes, const char* btns, const char* keys, int dz, int mode, const char** bad); /* mode: 0 standard, 1 CH Flightstick Pro, 2 Thrustmaster FCS */
void VJOY_SetLive(int dz, int mode); /* resident copy: new /JOYDZ, /JOYMODE (0-3) */
uint32_t VJOY_IOHandler(uint32_t port, uint32_t val, uint32_t out);
uint32_t VJOY_InfoHandler(uint32_t port, uint32_t val, uint32_t out);   /* port 206h, real mode: for SBEMU16 */
void VJOY_Tick(void);
uint8_t VJOY_BiosButtons(void);   /* INT 15h AH=84h DX=0: AL bits 4-7 */
void VJOY_BiosAxes(uint16_t* a);  /* INT 15h AH=84h DX=1: AX BX CX DX, 0..255 */   /* from the sound interrupt: USB poll + key presses */
const char* VJOY_Names(void); /* list of the names for the help text */

#endif
