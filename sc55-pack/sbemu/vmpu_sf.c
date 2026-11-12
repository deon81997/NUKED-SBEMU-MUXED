/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED (code from SBEMU's vmpu.c, crazii et al.) */
/* vmpu_sf.c - the original TinySoundFont back end (sbemu/vmpu.c), compiled next to the SC-55 back end
 * under other names. vmpu_sc55.c forwards to these when SBEMU is started with /VSF:file.sf2. */
#include <sbemucfg.h>
#if SBEMU_VSC55
#define VMPU_Init            VMPUSF_Init
#define VMPU_Reset           VMPUSF_Reset
#define VMPU_IsActive        VMPUSF_IsActive
#define VMPU_GenSamples      VMPUSF_GenSamples
#define VMPU_SBMidi_RawWrite VMPUSF_SBMidi_RawWrite
#define VMPU_MPU             VMPUSF_MPU
#define VMPU_SetSFEffects    VMPUSF_SetSFEffects
#define VMPU_SetSFGain       VMPUSF_SetSFGain
#include "vmpu.c"
#endif
