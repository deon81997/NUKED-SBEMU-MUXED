#ifndef VMPU_MT32_H
#define VMPU_MT32_H
/* Roland MT-32 / CM-32L back end (Munt), see vmpu_mt32.cpp */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int MT32_Init(const char* romdir, const char* want, int gain_percent, int verbose); /* want: auto, mt32, cm32l */
int MT32_IsLoaded(void);
int MT32_Active(void);
const char* MT32_LastError(void);
const char* MT32_ModelName(void);
int MT32_PostByte(uint8_t b);           /* 1 = queued */
int MT32_Busy(void);
void MT32_SetGain(int percent);
void MT32_Render(int16_t* pcm, int frames, uint32_t rate);
int MT32_GetLCD(char* out21);           /* 20 characters + NUL; returns the MIDI LED */
#ifdef __cplusplus
}
#endif
#endif
