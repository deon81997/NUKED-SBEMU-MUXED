/* SPDX-License-Identifier: GPL-2.0-or-later - part of NUKED SB-EMU, https://github.com/deon81997/NUKED-SBEMU-MUXED */
#ifndef _TRACE_H_
#define _TRACE_H_
/* SBEMU flight recorder (SBTRACE.TXT).
 * - Events: a RAM ring of the last 64 runtime events (USB plug/unplug, dropped MIDI, late renders, timer tick
 *   replays, DMA/IRQ problems, CD mounts). The same event again within 2 s only counts up.
 * - Per program: every Sound Blaster DSP command, MPU-401 command and MIDI message type a program sends is
 *   counted, under the program's name (read from DOS: current PSP -> memory block name); unsupported ones are marked.
 * - Written (appended) to SBTRACE.TXT from INT 28h when DOS waits at the command prompt, so never while a game runs.
 * Every TRACE_ function except TRACE_Init / TRACE_Idle may be called from interrupt handlers: no DOS calls, no
 * allocation, a few memory writes. */
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { TR_USB, TR_MIDI, TR_LATE, TR_TIMER, TR_DMA, TR_IRQ, TR_CD, TR_INFO };
int  TRACE_Init(const char* path);             /* full path of SBTRACE.TXT; 0 = recorder off (no DOS memory) */
void TRACE_Event(int kind, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void TRACE_Dsp(uint8_t cmd);                   /* first byte of every DSP command (port 2xC) */
void TRACE_Mpu(int cmdport, uint8_t val);      /* every byte written to the MPU-401 (data or command port) */
void TRACE_MidiDropped(void);                  /* a MIDI byte the synth's queue could not take */
void TRACE_Underruns(uint32_t total);          /* render-ahead FIFO underruns so far (late renders) */
void TRACE_Replayed(uint32_t n);               /* game timer ticks given back */
void TRACE_Irq(int irq);                       /* virtual SB IRQ raised */
void TRACE_IrqAck(void);                       /* the program acknowledged it (read 2xE / 2xF) */
void TRACE_DmaCheck(int dma, uint32_t addr, int32_t count);   /* digital sound running: is the DMA channel set up? */
void TRACE_Flush(void);                        /* MPU command A9h: write SBTRACE.TXT now (BUGREP) */
void TRACE_Idle(void);                         /* INT 28h: writes SBTRACE.TXT when at the prompt and something is new */
#ifdef __cplusplus
}
#endif
#endif
