#ifndef _OPL3EMU_H_
#define _OPL3EMU_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

//select the FM core before OPL3EMU_Init: 0 = DOSBox DBOPL (default, fast), 1 = Nuked OPL3 (accurate)
void OPL3EMU_SetCore(int core);
void OPL3EMU_Init(int samplerate);
int OPL3EMU_IsActive();
//get mode set by client. 0: OPL2, other:OPL3
int OPL3EMU_GetMode();
int OPL3EMU_GenSamples(int16_t* pcm16, int count);

uint32_t OPL3EMU_PrimaryRead(uint32_t val);
uint32_t OPL3EMU_PrimaryWriteIndex(uint32_t val);
uint32_t OPL3EMU_PrimaryWriteData(uint32_t val);

uint32_t OPL3EMU_SecondaryRead(uint32_t val);
uint32_t OPL3EMU_SecondaryWriteIndex(uint32_t val);
uint32_t OPL3EMU_SecondaryWriteData(uint32_t val);

#ifdef __cplusplus
}
#endif


#endif//_OPL3EMU_H_