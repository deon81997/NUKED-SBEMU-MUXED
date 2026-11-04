#ifndef _VMPU_H_
#define _VMPU_H_

#include <sbemucfg.h>

#ifdef __cplusplus
extern "C"
{
#endif

#if SBEMU_VMPU

#define VMPU_DEF_SF2 "sbemusf.sf2"
#define VMPU_DEF_ROMS "roms" //SC-55 back end: default ROM folder (relative to the program folder)

//intialization route, called if option enabled.
BOOL VMPU_Init(int baseaddr, int* voices, int freq, const char* sf2);

//for TSR with pre-loaded memory
//don't change VMPU_Init to pre-loaded memory because streaming take less memory,
//but a full pre-load may fail if the SF is too large.
BOOL VMPU_Reset(int baseaddr, int* voices, int freq, const char* sf2, uint32_t sf2_linear_mem, int bytes);

BOOL VMPU_IsActive();

#if SBEMU_VSC55
//SC-55 back end extras: select the ROM set ("auto" or e.g. "mk1-v1.21") and output gain in percent.
//Call before VMPU_Init (gain may also be changed later).
void VMPU_SetMixVolume(int q8); /* SB mixer master*MIDI volume, 0..256 */
void VMPU_SetSC55Options(const char* romset, int gain_percent);
void VMPU_SetSC55Lite(int fx_on, int max_voices); /* speed-ups for slow CPUs: reverb/chorus off, voice cap */
int VMPU_FrameCap(int requested, int card_spi);
void VMPU_SetMidiVolume(int percent); /* /VMIDIVOL: General MIDI music volume, 0-400% */ /* most frames one interrupt may render (keeps the game timer ticking) */
void VMPU_SetAhead(int ms); /* render-ahead FIFO length, 0 = off */
void VMPU_SetSC55Auto(int on); /* governor: keeps the SC-55 render time below the audio period by shedding voices/effects */
void VMPU_SelectMT32(int on, const char* model); /* /VMT32: Roland MT-32 / CM-32L (Munt); model auto, mt32 or cm32l; call before VMPU_Init */
void VMPU_SelectSoundFont(int on);
void VMPU_SetSFEffects(int rev, int room, int damp, int cho, int rate10, int depth, int sends); /* SoundFont reverb/chorus (percent; rate in tenths of Hz) */ /* 1: VMPU_Init loads a .sf2 (TinySoundFont) instead of the SC-55 ROMs; call before VMPU_Init */
const char* VMPU_LastError(void); //reason of the last VMPU_Init failure
const char* VMPU_ModelName(void); //name of the loaded ROM set
#endif

void VMPU_GenSamples(int16_t* pcm16, int samples, int freq, BOOL domix);

uint32_t VMPU_MPU(uint32_t port, uint32_t val, uint32_t out);
void VMPU_SBMidi_RawWrite( uint8_t value );

#else //SBEMU_VMPU

#define VMPU_IsActive() FALSE

#endif //SBEMU_VMPU

#ifdef __cplusplus
}
#endif

#endif//_VMPU_H_
