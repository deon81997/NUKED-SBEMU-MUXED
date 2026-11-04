/* sc55synth.h - C interface to the Nuked-SC55 emulator for the DOS port.
 * Used by sbemu/vmpu_sc55.c (MPU-401 front end) and tools/sc55wav.cpp.
 * GPL-2.0-or-later. */
#ifndef SC55SYNTH_H
#define SC55SYNTH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SC55_Config
{
    const char* romdir;      /* folder holding the ROM dumps (matched by SHA-256, names are irrelevant) */
    const char* romset;      /* "mk1-v1.21", ... ; NULL, "" or "auto" = first complete SC-55 set found */
    int gain_percent;        /* 0..1600, 100 = unity */
    int boot_seconds;        /* emulated seconds to let the firmware boot, 0 = default (4) */
    int verbose;             /* print progress to stdout */
} SC55_Config;

/* Load ROMs, start the emulated module and let its firmware boot.
 * Returns 1 on success. On failure returns 0 and SC55_LastError() explains why. */
int SC55_Init(const SC55_Config* cfg);
void SC55_Shutdown(void);

int SC55_IsLoaded(void);
const char* SC55_LastError(void);
const char* SC55_LastDiagnostic(void); /* last warning/error text from the core, "" if none */
const char* SC55_ModelName(void);     /* e.g. "mk1-v1.21" */
uint32_t SC55_NativeRate(void);       /* 64000 (SC-55) or 66207 (SC-55mkII) */

/* Calls cb for every romset name that can be selected. */
void SC55_ListModels(void (*cb)(const char* name, void* user), void* user);

/* Calls cb for every romset that is complete in romdir. Returns how many, or -1 if the folder is unreadable. */
int SC55_ListInstalledModels(const char* romdir, void (*cb)(const char* name, void* user), void* user);

/* MIDI input. Safe to call from a trap/interrupt that preempts SC55_Render. */
int SC55_PostByte(uint8_t b);         /* 1 = queued, 0 = queue full (byte dropped) */
int SC55_Queued(void);                /* MIDI bytes waiting for the emulated SC-55 (diagnostics) */
int SC55_Busy(void);                  /* 1 while the queue is nearly full (MPU-401 "DRR") */
void SC55_SetGain(int percent);
void SC55_SetLite(int fx_on, int max_voices); /* fx_on 0 = no reverb/chorus; max_voices 0 = all */
void SC55_SetMixVolume(int q8);       /* host mixer volume 0..256, applied on top of the gain */

/* Audio. SC55_Active() says whether the emulation needs CPU time right now. */
int SC55_GetLCD(uint8_t* data80, uint8_t* cg64); /* LCD display RAM + custom characters; 1 = display on */
int SC55_Active(void);
void SC55_Render(int16_t* pcm, int frames, uint32_t out_rate, int domix);

#ifdef __cplusplus
}
#endif
#endif
