/* vmpu_sc55.c - virtual MPU-401 (UART mode) whose synthesizer is a Roland
 * SC-55 / SC-55mkII emulation (Nuked-SC55 core, see sc55/).
 *
 * This file replaces sbemu/vmpu.c (the TinySoundFont back end) when the project
 * is built with SBEMU_VSC55=1 (the default). It keeps the same VMPU_* API so the
 * rest of SBEMU - port traps for real and protected mode, the audio interrupt
 * mixer, the TSR machinery - is untouched.
 *
 * Behaviour, compared to the soundfont back end:
 *  - MIDI bytes written to the data port are forwarded unmodified to the
 *    emulated module's UART, exactly like a real MPU-401 in UART mode. The
 *    SC-55 firmware does its own message framing, so running status,
 *    interleaved real-time bytes and truncated messages behave as on hardware.
 *  - The status port reports "interface busy" (DRR) when the 64 KB queue in
 *    front of the module is nearly full, giving games that poll DRR natural
 *    back-pressure while the emulation catches up.
 *  - Every command written to the command port is acknowledged (0xFE), so
 *    detection code that sends 0xFF / 0x3F works. The intelligent-mode
 *    sequencer of the original card is not emulated (UART mode only).
 *  - No interrupts are disabled while rendering: the module needs milliseconds
 *    of CPU per audio block and the MIDI queue is lock-free.
 *
 * GPL-2.0-or-later (links the GPL Nuked-SC55 backend).
 */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "vdiag.h"
#include <dpmi.h>
#include <time.h>
#include <stdlib.h>

#include <dpmi/dbgutil.h>
#include "vmpu.h"

#if SBEMU_VMPU && SBEMU_VSC55

#include "sc55synth.h"
#include "vmpu_mt32.h"

/* SoundFont (TinySoundFont) back end, vmpu_sf.c, used instead of the SC-55 when /VSF is given */
BOOL VMPUSF_Init(int baseaddr, int* voices, int freq, const char* sf2);
BOOL VMPUSF_Reset(int baseaddr, int* voices, int freq, const char* sf2, uint32_t sf2_linear_mem, int bytes);
BOOL VMPUSF_IsActive();
void VMPUSF_GenSamples(int16_t* pcm16, int samples, int freq, BOOL domix);
void VMPUSF_SBMidi_RawWrite(uint8_t value);
uint32_t VMPUSF_MPU(uint32_t port, uint32_t val, uint32_t out);
void VMPUSF_SetSFEffects(int rev, int room, int damp, int cho, int rate10, int depth, int sends);
void VMPU_SetSFEffects(int rev, int room, int damp, int cho, int rate10, int depth, int sends) { VMPUSF_SetSFEffects(rev, room, damp, cho, rate10, depth, sends); }
static volatile int VMPU_UseSF;
static int VMPU_SFLoaded;
void VMPU_SelectSoundFont(int on) { VMPU_UseSF = on; }
/* /VMT32: Roland MT-32 / CM-32L (Munt) instead of the SC-55 */
static volatile int VMPU_UseMT32;
static char VMPU_MT32Want[16] = "auto";
void VMPU_SelectMT32(int on, const char* model) { VMPU_UseMT32 = on; if (model && *model) snprintf(VMPU_MT32Want, sizeof VMPU_MT32Want, "%s", model); }

static unsigned char fpu_buffer[512] __attribute__((aligned(16)));

static int VMPU_base;
static volatile uint8_t VMPU_AckPending; /* a command was written; 0xFE waits at the data port */
static char VMPU_Romset[64] = "auto";
static int VMPU_Gain = 100;
static int VMPU_MidiPct = 100;
static int VMPU_EffGain(void) { long g = (long)VMPU_Gain * VMPU_MidiPct / 100; return g > 1600 ? 1600 : (int)g; }
static int VMPU_LiteFx = 1, VMPU_LiteVoices = 0;

/* ---------------------------------------------------------------- note limit (/VSCVOICES)
 * A safe polyphony limit: SBEMU keeps a list of the notes the game is holding (drums on channel 10 are
 * not counted - they end by themselves). When a new note would go over the limit, the oldest held note
 * gets a real Note Off first, so the SC-55 firmware releases that voice the normal way - like voice
 * stealing on a real synth. Nothing inside the synth is skipped, so no voice can get stuck.
 * The MIDI stream is passed on message by message (SysEx and real-time bytes unchanged). */
#define NL_MAX 32
static uint8_t NL_Ch[NL_MAX], NL_Note[NL_MAX]; static uint32_t NL_Age[NL_MAX]; static int NL_N; static uint32_t NL_Clock;
static uint8_t NL_Status, NL_OutStatus, NL_D[2]; static int NL_Have, NL_Need, NL_InSysex;
static int NL_Limit;                       /* 0 = off */
static uint32_t NL_Stolen;

static int nl_post(uint8_t b) { return SC55_PostByte(b); }
static void nl_emit(uint8_t st, const uint8_t* d, int n)
{
    int i;
    if (st != NL_OutStatus || st >= 0xF0) { nl_post(st); NL_OutStatus = st < 0xF0 ? st : 0; }
    for (i = 0; i < n; ++i) nl_post(d[i]);
}
static void nl_remove(int i) { --NL_N; NL_Ch[i] = NL_Ch[NL_N]; NL_Note[i] = NL_Note[NL_N]; NL_Age[i] = NL_Age[NL_N]; }
static void nl_off(int ch, int note) { int i; for (i = 0; i < NL_N; ++i) if (NL_Ch[i] == ch && NL_Note[i] == note) { nl_remove(i); return; } }
static void nl_clear(int ch) { int i; for (i = NL_N - 1; i >= 0; --i) if (ch < 0 || NL_Ch[i] == ch) nl_remove(i); }

static void nl_message(uint8_t st, const uint8_t* d, int n)
{
    int ch = st & 15, type = st & 0xF0;
    if (NL_Limit > 0 && ch != 9)
    {
        if (type == 0x90 && d[1])
        {
            int i, held = -1;
            for (i = 0; i < NL_N; ++i) if (NL_Ch[i] == ch && NL_Note[i] == d[0]) { held = i; break; }
            if (held >= 0) NL_Age[held] = ++NL_Clock;           /* struck again: same voice slot */
            else
            {
                while (NL_N >= NL_Limit || NL_N >= NL_MAX)      /* release the oldest held note */
                {
                    int o = 0; uint8_t off[2];
                    for (i = 1; i < NL_N; ++i) if (NL_Age[i] < NL_Age[o]) o = i;
                    off[0] = NL_Note[o]; off[1] = 0;
                    nl_emit((uint8_t)(0x90 | NL_Ch[o]), off, 2); /* note on, velocity 0 = note off */
                    nl_remove(o); ++NL_Stolen;
                }
                NL_Ch[NL_N] = (uint8_t)ch; NL_Note[NL_N] = d[0]; NL_Age[NL_N] = ++NL_Clock; ++NL_N;
            }
        }
        else if (type == 0x80 || type == 0x90) nl_off(ch, d[0]);
        else if (type == 0xB0 && (d[0] == 120 || d[0] == 123 || d[0] == 121)) nl_clear(ch);
    }
    nl_emit(st, d, n);
}

/* every MIDI byte for the SC-55 goes through here; returns 0 if the synth's queue was full */
static int NL_Byte(uint8_t b)
{
    if (NL_Limit <= 0 && NL_N == 0) { NL_OutStatus = 0; NL_Status = 0; NL_Have = 0; return SC55_PostByte(b); } /* off: untouched */
    if (b >= 0xF8) return nl_post(b);                          /* real-time: anywhere */
    if (b == 0xF0) { NL_InSysex = 1; NL_OutStatus = 0; nl_clear(-1); return nl_post(b); } /* (a reset may follow) */
    if (NL_InSysex) { if (b == 0xF7 || b >= 0x80) { NL_InSysex = 0; if (b != 0xF7) return NL_Byte(b); } return nl_post(b); }
    if (b >= 0x80)
    {
        NL_Have = 0;
        if (b >= 0xF0)                                         /* system common */
        {
            NL_OutStatus = 0; NL_Need = (b == 0xF2) ? 2 : (b == 0xF1 || b == 0xF3) ? 1 : 0;
            NL_Status = NL_Need ? b : 0;
            return NL_Need ? 1 : nl_post(b);
        }
        NL_Status = b; NL_Need = ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) ? 1 : 2;
        return 1;
    }
    if (!NL_Status) return nl_post(b);                         /* data without a status: pass on */
    NL_D[NL_Have++] = b;
    if (NL_Have < NL_Need) return 1;
    NL_Have = 0;
    if (NL_Status >= 0xF0) { nl_post(NL_Status); nl_post(NL_D[0]); if (NL_Need > 1) nl_post(NL_D[1]); NL_Status = 0; return 1; }
    nl_message(NL_Status, NL_D, NL_Need);
    return 1;
}

static int VMPU_Lpf = 1;
void VMPU_SetSC55Lowpass(int on)
{
    VMPU_Lpf = on;
    if (SC55_IsLoaded()) SC55_SetLowpass(on);
}
static void Gov_Apply(void);
void VMPU_SetSC55Lite(int fx_on, int max_voices)
{
    VMPU_LiteFx = fx_on;
    VMPU_LiteVoices = max_voices;
    NL_Limit = max_voices; if (max_voices <= 0) NL_N = 0;
    if (SC55_IsLoaded())
        Gov_Apply();
}

/* ---- speed governor ----
 * The SC-55 emulation needs more CPU than a slow machine has. When rendering one audio block takes
 * longer than the block lasts, the sound-card interrupt hogs the CPU: the game's own timer interrupt
 * loses ticks and the game's music (timed by that tick) plays slow. The governor measures the render
 * time of every block (CPU time-stamp counter) and sheds work - first reverb/chorus, then voices -
 * until rendering takes at most ~70% of the block, and restores it when there is room again. */
static int Gov_On = 1;
static double Gov_CyclesPerSec;
static int Gov_EmaPm = 0;           /* smoothed load, permille of the block time */
static int Gov_Level = 0;           /* 0 = user settings; 1 = fx off; 2.. = voices cap lowered */

/* statistics for TIMETEST: MPU command 0xA5 makes the data port return 32 bytes (little-endian dwords) */
static unsigned long Stat_Calls, Stat_MaxUs, Stat_SumUs, Stat_Samples, Stat_Freq, Stat_MaxLoadPm;
static unsigned char Stat_Buf[32];

/* ---- per-interrupt work cap ----
 * Interrupts stay blocked for the whole sound-card interrupt, so the game's timer (which times its
 * music) cannot tick during it. If one interrupt renders a long stretch of audio (the whole free card
 * buffer after a hiccup), the timer misses ticks and the game's music slows. SBEMU therefore renders at
 * most what fits in ~3 ms of CPU per interrupt; the rest follows at the next interrupt. */
static unsigned long Cap_CyclesPerFrameQ8; /* smoothed render cost per frame, x256 */
static void Cap_Note(unsigned long long cycles, int frames)
{
    unsigned long c;
    if (frames <= 0) return;
    c = (unsigned long)((cycles << 8) / (unsigned)frames);
    Cap_CyclesPerFrameQ8 = Cap_CyclesPerFrameQ8 ? (Cap_CyclesPerFrameQ8 * 7 + c) / 8 : c;
}
static unsigned long Cap_Calls;
static int Cap_Steady; /* typical frames per interrupt (what the card plays between two interrupts) */
int VMPU_FrameCap(int requested, int card_spi)
{
    double budget, per;
    int cap, floor_frames;
    if (requested <= 0) return requested;
    /* learn the steady per-interrupt amount: a slowly rising minimum of the requests */
    ++Cap_Calls;
    if (!Cap_Steady || requested < Cap_Steady) Cap_Steady = requested;
    else if ((Cap_Calls & 63) == 0) Cap_Steady += (requested - Cap_Steady + 7) / 8;
    if (!Gov_On || !Cap_CyclesPerFrameQ8 || Gov_CyclesPerSec <= 0 || Cap_Calls < 256) return requested;
    budget = Gov_CyclesPerSec * 0.003;           /* 3 ms of CPU per interrupt */
    per = Cap_CyclesPerFrameQ8 / 256.0;
    cap = (int)(budget / per);
    /* never below 2x what the card plays per interrupt: catching up after a hiccup must stay possible,
     * or the card runs dry */
    floor_frames = Cap_Steady * 2;
    if (floor_frames < card_spi * 2) floor_frames = card_spi * 2;
    if (floor_frames < 32) floor_frames = 32;
    if (cap < floor_frames) cap = floor_frames;
    return requested > cap ? cap : requested;
}
static int Stat_Pos = 32;

/* ---- front panel / load report (MPU command 0xB0) ----
 * The data port then returns LCD_PKT bytes: 'L', display on, synth (0 SC-55, 1 SoundFont, 2 MT-32),
 * load in permille (2 bytes, smoothed render time / sound time), governor level, 80 bytes of LCD
 * display RAM, 64 bytes of LCD custom characters. MIDPLAY draws the real panel from it, MENU shows the
 * load, and the Ctrl+Alt+F12 overlay (vdiag.c) uses the same data inside games. */
#define LCD_PKT (6 + 80 + 64)
static unsigned char Lcd_Buf[LCD_PKT];
static int Lcd_Pos = LCD_PKT;
static volatile int Ld_EmaPm;                  /* render load, permille, always measured */
int VMPU_LoadPermille(void) { return Ld_EmaPm; }
static int VMPU_SynthKind(void) { return VMPU_UseMT32 ? 2 : VMPU_UseSF ? 1 : 0; }
void VMPU_PanelSnapshot(unsigned char* out)   /* fills LCD_PKT bytes */
{
    int on = 0, k = VMPU_SynthKind();
    memset(out, 0, LCD_PKT);
    out[0] = 'L';
    if (k == 0 && SC55_IsLoaded()) { SC55_GetLCD(out + 6, out + 86); on = 1; }
    if (k == 2 && MT32_IsLoaded()) { char l[21]; MT32_GetLCD(l); memcpy(out + 6, l, 20); on = 1; } /* the MT-32's 20-character LCD */
    out[1] = (unsigned char)on; out[2] = (unsigned char)k;
    out[3] = (unsigned char)(Ld_EmaPm & 255); out[4] = (unsigned char)(Ld_EmaPm >> 8);
    out[5] = (unsigned char)Gov_Level;
}

static unsigned long long Gov_Tsc(void)
{
    unsigned long lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((unsigned long long)hi << 32) | lo;
}

static void Gov_Calibrate(void)
{
    uclock_t u0 = uclock(), u1;
    unsigned long long c0 = Gov_Tsc(), c1;
    do { u1 = uclock(); } while ((u1 - u0) < UCLOCKS_PER_SEC / 20); /* 50 ms */
    c1 = Gov_Tsc();
    Gov_CyclesPerSec = (double)(c1 - c0) * UCLOCKS_PER_SEC / (double)(u1 - u0);
    if (Gov_CyclesPerSec < 1e7 || Gov_CyclesPerSec > 2e10) Gov_CyclesPerSec = 0; /* implausible: no governor */
}

static void Gov_Apply(void)
{
    int fx = VMPU_LiteFx;
    if (Gov_Level >= 1) fx = 0;
    SC55_SetLite(fx, 0); /* the old voice cap inside the synth is never used: it froze notes. /VSCVOICES = NL_Limit */
}

void VMPU_SetSC55Auto(int on)
{
    Gov_On = on;
    if (!on && Gov_Level) { Gov_Level = 0; Gov_Apply(); }
}

static void Gov_Update(unsigned long long cycles, int samples, int freq)
{
    double budget = Gov_CyclesPerSec * samples / freq;
    int pm;
    if (budget <= 0) return;
    pm = (int)(cycles * 1000.0 / budget);
    {
        unsigned long us = (unsigned long)(cycles * 1000000.0 / Gov_CyclesPerSec);
        ++Stat_Calls; Stat_SumUs += us; if (us > Stat_MaxUs) Stat_MaxUs = us;
        if ((unsigned long)pm > Stat_MaxLoadPm) Stat_MaxLoadPm = (unsigned long)pm;
        Stat_Samples = (unsigned long)samples; Stat_Freq = (unsigned long)freq;
    }
    Gov_EmaPm = (3 * Gov_EmaPm + pm) / 4;
    /* Level changes are decided from the render-ahead FIFO (Gov_Fifo), not from single busy blocks:
     * the FIFO absorbs those. The governor only ever switches reverb/chorus off: capping voices freezes
     * the skipped voices' envelopes, the firmware never sees them finish, and the music can die. */
}

static int Gov_Low, Gov_Full;
static void Gov_Fifo(int fill, int target)
{
    if (!Gov_On) return;
    if (fill < target / 4) { ++Gov_Low; Gov_Full = 0; }
    else if (fill >= target * 9 / 10) { ++Gov_Full; Gov_Low = 0; }
    else { Gov_Low = 0; Gov_Full = 0; }
    if (Gov_Level == 0 && Gov_Low >= 30)                       /* reserve keeps running low: lighten */
    {
        Gov_Level = 1; Gov_Apply(); Gov_Low = 0;
    }
    else if (Gov_Level == 1 && Gov_Full >= 3000 && Gov_EmaPm < 450) /* ~10 s of full reserve and room */
    {
        Gov_Level = 0; Gov_Apply(); Gov_Full = 0;
    }
}

void VMPU_SetSC55Options(const char* romset, int gain_percent)
{
    if (romset && *romset)
    {
        strncpy(VMPU_Romset, romset, sizeof(VMPU_Romset) - 1);
        VMPU_Romset[sizeof(VMPU_Romset) - 1] = '\0';
    }
    VMPU_Gain = gain_percent;
    if (SC55_IsLoaded())
        SC55_SetGain(VMPU_EffGain());
    if (MT32_IsLoaded())
        MT32_SetGain(VMPU_EffGain());
}

/* /VMIDIVOL: General MIDI music volume in percent. SC-55: multiplies its gain (/VSCGAIN);
 * SoundFont: scales its master volume (set when it next renders, with the FPU saved). */
void VMPUSF_SetSFGain(int percent);
void VMPU_SetMidiVolume(int percent)
{
    VMPU_MidiPct = percent < 0 ? 0 : percent > 400 ? 400 : percent;
    VMPUSF_SetSFGain(VMPU_MidiPct);
    if (SC55_IsLoaded()) SC55_SetGain(VMPU_EffGain());
    if (MT32_IsLoaded()) MT32_SetGain(VMPU_EffGain());
}

void VMPU_SetMixVolume(int q8) { if (!VMPU_UseSF && !VMPU_UseMT32) SC55_SetMixVolume(q8); }

/* The SC-55 and MT-32 cores are built for i686 (they use CMOV): on a CPU without it (Pentium, Pentium MMX,
   AMD K5/K6, Cyrix 6x86/MII) they would stop with an invalid opcode, so they are not started there. Those CPUs
   are far too slow for them anyway. */
static int VMPU_NoCmov;
static int VMPU_HasCmov(void)
{
    unsigned int a, b, c, d, f0, f1;
    /* CPUID is there when EFLAGS.ID (bit 21) can be changed */
    asm volatile("pushfl; popl %0; movl %0,%1; xorl $0x200000,%0; pushl %0; popfl; pushfl; popl %0; pushl %1; popfl"
                 : "=&r"(f0), "=&r"(f1));
    if (!((f0 ^ f1) & 0x200000)) return 0;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
    if (a < 1) return 0;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(1));
    return (d >> 15) & 1;
}

const char* VMPU_LastError(void) { if (VMPU_NoCmov) return "it needs a Pentium Pro / Pentium II or newer CPU (this CPU has no CMOV)";
    return VMPU_UseMT32 ? MT32_LastError() : VMPU_UseSF ? "the SoundFont file could not be loaded (not a valid .sf2, or out of memory)" : SC55_LastError(); }
const char* VMPU_ModelName(void) { return VMPU_UseMT32 ? MT32_ModelName() : SC55_ModelName(); }

static void Stat_Snapshot(void)
{
    unsigned long d[8]; int i;
    d[0] = 0x35355342UL; /* "SB55" */
    d[1] = (unsigned long)Gov_Level;
    d[2] = (unsigned long)Gov_EmaPm;
    d[3] = Stat_MaxUs;
    d[4] = Stat_Calls ? Stat_SumUs / Stat_Calls : 0;
    d[5] = Stat_Samples;
    d[6] = Stat_Freq;
    d[7] = Stat_Calls;
    for (i = 0; i < 8; ++i) { Stat_Buf[4*i] = (unsigned char)d[i]; Stat_Buf[4*i+1] = (unsigned char)(d[i] >> 8); Stat_Buf[4*i+2] = (unsigned char)(d[i] >> 16); Stat_Buf[4*i+3] = (unsigned char)(d[i] >> 24); }
    Stat_Pos = 0;
    Stat_Calls = Stat_MaxUs = Stat_SumUs = Stat_MaxLoadPm = 0;
}

static void VMPU_Write(uint16_t port, uint8_t value)
{
    _LOG("VMPU_Write(%X)=%X\n", port, value);
    if (port == VMPU_base + 1)
    {
        /* Command port. Real hardware answers every command with an ACK. 0xFF (reset)
         * and 0x3F (enter UART mode) are the only ones games need; resetting the
         * MPU-401 does not reset the connected synthesizer. */
        if (value == 0xA5) Stat_Snapshot(); /* not a real MPU command: statistics for TIMETEST */
        VMPU_AckPending = 1;
    }
    else if (VMPU_UseMT32) DIAG_Midi(MT32_PostByte(value) ? 0 : 1);
    else
    {
        DIAG_Midi(NL_Byte(value) ? 0 : 1); /* a full queue drops the byte, as a real UART would overrun */
    }
}

static uint8_t VMPU_Read(uint16_t port)
{
    _LOG("VMPU_Read(%X)\n", port);
    if (port == VMPU_base)
    {
        VMPU_AckPending = 0;
        if (Stat_Pos < 32) return Stat_Buf[Stat_Pos++];
        return 0xFE; /* ACK; also what an idle UART-mode card returns (active sensing) */
    }
    /* Status port: bit 7 DSR (1 = no data to read), bit 6 DRR (1 = not ready to receive),
     * bits 5..0 read as 1 on a real card. */
    uint8_t status = 0x3F;
    if (!VMPU_AckPending)
        status |= 0x80;
    if (VMPU_UseMT32 ? MT32_Busy() : SC55_Busy())
        status |= 0x40;
    return status;
}

BOOL VMPU_Init(int baseaddr, int* voices, int freq, const char* romdir)
{
    SC55_Config cfg;
    VMPU_NoCmov = 0;
    if (!VMPU_UseSF && !VMPU_HasCmov())
    {
        VMPU_NoCmov = 1;
        VMPU_base = baseaddr;
        return FALSE;
    }
    if (VMPU_UseMT32)
    {
        int ok = MT32_Init(romdir, VMPU_MT32Want, VMPU_EffGain(), 0);
        Gov_Calibrate();
        VMPU_AckPending = 0;
        VMPU_base = baseaddr;
        if (voices) *voices = 1;
        return ok ? TRUE : FALSE;
    }
    if (VMPU_UseSF)
    {
        VMPU_base = baseaddr;  /* the side channel (SBDIAG, front panel/LCD report, key player) checks VMPU_base: it stayed 0 with /VSF */
        VMPU_SFLoaded = VMPUSF_Init(baseaddr, voices, freq, romdir) ? 1 : 0;
        Gov_Calibrate();
        return VMPU_SFLoaded ? TRUE : FALSE;
    }
    (void)freq; /* the output rate is passed with every render call */
    VMPU_base = 0x330;
    if (voices)
        *voices = 1;

    memset(&cfg, 0, sizeof(cfg));
    cfg.romdir = romdir;
    cfg.romset = VMPU_Romset;
    cfg.gain_percent = VMPU_EffGain();
    cfg.boot_seconds = 0;
    cfg.verbose = 1;
    if (!SC55_Init(&cfg))
        return FALSE;

    SC55_SetLite(VMPU_LiteFx, 0); /* only after the firmware has booted */
    SC55_SetLowpass(VMPU_Lpf);
    Gov_Calibrate();
    VMPU_AckPending = 0;
    VMPU_base = baseaddr;
    return TRUE;
}

/* Called in the resident instance when a second SBEMU run changes settings.
 * The ROM set is loaded once, before going resident (the TSR can no longer
 * read files); only the gain can change on the fly. */
BOOL VMPU_Reset(int baseaddr, int* voices, int freq, const char* romdir, uint32_t unused_mem, int unused_bytes)
{
    (void)freq; (void)romdir; (void)unused_mem; (void)unused_bytes;
    if (VMPU_UseMT32) { VMPU_base = baseaddr; if (voices) *voices = 1; return MT32_IsLoaded() ? TRUE : FALSE; }
    if (VMPU_UseSF)
    {
        VMPU_base = baseaddr; /* the SoundFont stays loaded; nothing to reload */
        return VMPU_SFLoaded ? TRUE : FALSE;
    }
    if (voices)
        *voices = 1;
    VMPU_base = baseaddr;
    if (SC55_IsLoaded())
        SC55_SetGain(VMPU_Gain);
    return SC55_IsLoaded() ? TRUE : FALSE;
}

BOOL VMPU_IsActive()
{
    if (VMPU_UseMT32)
        return MT32_Active() ? TRUE : FALSE;
    if (VMPU_UseSF)
        return VMPUSF_IsActive();
    return SC55_Active() ? TRUE : FALSE;
}

/* ---- render-ahead FIFO ----
 * SBEMU's sound interrupt blocks the game's timer for as long as it runs (interrupts cannot nest under
 * HDPMI). Rendering exactly what the card needs made every interrupt as long as the synth's slowest
 * block - up to 4-5 ms on a Pentium M, several in a row when the SC-55 has a busy moment - and games
 * such as Duke 3D (timer at ~220 Hz = a tick every 4.5 ms) lost 15% of their ticks: their MIDI music
 * played that much slower. Now the synth renders into a FIFO a few dozen ms ahead, in small slices,
 * and each interrupt spends at most ~2 ms on it: busy moments are absorbed by the FIFO instead of
 * stretching the interrupt. Cost: the music is ~50 ms later than the game sends it (constant). */
#define FIFO_FRAMES 4096                 /* power of two */
static int16_t Fifo[FIFO_FRAMES * 2];
static int Fifo_Head, Fifo_Count, Fifo_Freq, Fifo_Synth = -1;
static unsigned long Fifo_Underruns;     /* frames that had to be filled with silence */
static int Fifo_TargetMs = 50, Fifo_BudgetUs = 2000;
static unsigned long long Fifo_LastT0;   /* start of the previous render, for the CPU ceiling */
static unsigned long Fifo_DutyCuts;      /* interrupts that stopped short of the sound that was due */
static long long Fifo_Credit;            /* CPU cycles the synth may still use (CPU ceiling) */

static void Fifo_RenderInto(int16_t* dst, int n, int freq)
{
    if (VMPU_UseMT32) MT32_Render(dst, n, (uint32_t)freq);
    else if (VMPU_UseSF) VMPUSF_GenSamples(dst, n, freq, FALSE);
    else SC55_Render(dst, n, (uint32_t)freq, 0);
}

void VMPU_SetAhead(int ms) { Fifo_TargetMs = ms; Fifo_Head = Fifo_Count = 0; }

void VMPU_GenSamples(int16_t* pcm16, int samples, int freq, BOOL domix)
{
    unsigned long long t0, deadline, hardline, spent = 0;
    int rendered = 0, target, i, slice;
    if (samples <= 0 || freq <= 0) return;
    if (VMPU_UseMT32 ? !MT32_IsLoaded() : (!VMPU_UseSF && !SC55_IsLoaded())) return;
    if (Fifo_TargetMs <= 0 && VMPU_UseMT32) /* render-ahead off: straight into a scratch block, then mixed */
    {
        static int16_t tmp[4096 * 2];
        int done = 0;
        fpu_save(fpu_buffer);
        while (done < samples)
        {
            int n = samples - done > 4096 ? 4096 : samples - done, i;
            unsigned long long a = Gov_Tsc();
            MT32_Render(tmp, n, (uint32_t)freq);
            DIAG_Render(Gov_Tsc() - a, n, freq, 2);
            for (i = 0; i < n * 2; i++)
            {
                int32_t v = tmp[i] + (domix ? pcm16[2 * done + i] : 0);
                pcm16[2 * done + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
            }
            done += n;
        }
        fpu_restore(fpu_buffer);
        return;
    }
    if (Fifo_TargetMs <= 0) /* render-ahead off: render the block directly (beta.16 behaviour) */
    {
        if (VMPU_UseSF)
        {
            unsigned long long a = Gov_Tsc();
            VMPUSF_GenSamples(pcm16, samples, freq, domix);
            DIAG_Render(Gov_Tsc() - a, samples, freq, 1);
            return;
        }
        fpu_save(fpu_buffer);
        {
            unsigned long long a = Gov_Tsc(), dt;
            SC55_Render(pcm16, samples, (uint32_t)freq, domix ? 1 : 0);
            dt = Gov_Tsc() - a;
            if (Gov_CyclesPerSec > 0) { int pm = (int)(dt * 1000.0 * freq / (Gov_CyclesPerSec * samples)); if (pm > 9999) pm = 9999; Ld_EmaPm = (Ld_EmaPm * 15 + pm) / 16; }
            DIAG_Render(dt, samples, freq, 0);
            DIAG_SetSynthInfo(Gov_Level, Gov_Level >= 1 ? 0 : VMPU_LiteFx, VMPU_LiteVoices);
            if (Gov_On && Gov_CyclesPerSec > 0) Gov_Update(dt, samples, freq);
        }
        fpu_restore(fpu_buffer);
        return;
    }
    fpu_save(fpu_buffer);

    if (freq != Fifo_Freq || Fifo_Synth != VMPU_SynthKind()) /* output rate or synth changed: start over */
    {
        Fifo_Head = Fifo_Count = 0; Fifo_Freq = freq; Fifo_Synth = VMPU_SynthKind();
    }
    target = (int)((long)freq * Fifo_TargetMs / 1000);
    if (target < samples) target = samples;
    if (target > FIFO_FRAMES - 64) target = FIFO_FRAMES - 64;
    slice = freq / 1000; if (slice < 16) slice = 16;  /* ~1 ms of sound per render call */

    t0 = Gov_Tsc();
    deadline = t0 + (unsigned long long)(Gov_CyclesPerSec > 0 ? Gov_CyclesPerSec * Fifo_BudgetUs / 1e6 : 4e6);
    hardline = deadline + (deadline - t0) / 2;
    /* CPU ceiling: the budget above is a fixed time per interrupt, but interrupts can come every ~4 ms, and on
     * a CPU too slow for the synth one 1 ms slice can cost 4 ms. The sound interrupt then took 98% of the CPU
     * and the game hardly ran (Dark Forces' iMUSE setup, Test Music with the SC-55: it crawled and looked
     * frozen, and a key press then overflowed DOS's interrupt stacks). So rendering runs on a credit: every
     * interrupt earns 60% of the time since the previous one and each slice spends what it cost. When the
     * credit is used up the interrupt renders nothing more - the music stutters and drags on such a PC, but
     * the game keeps at least 40% of the CPU. Fast PCs never come near the limit. /VSCAUTO:0 turns it off. */
    if (Fifo_LastT0 && t0 > Fifo_LastT0 && Gov_CyclesPerSec > 0 && t0 - Fifo_LastT0 < (unsigned long long)(Gov_CyclesPerSec / 10))
    {
        long long cap = (long long)(Gov_CyclesPerSec * 0.006);          /* save up at most 6 ms */
        Fifo_Credit += (long long)((t0 - Fifo_LastT0) * 60 / 100);
        if (Fifo_Credit > cap) Fifo_Credit = cap;
    }
    else Fifo_Credit = (long long)(Gov_CyclesPerSec > 0 ? Gov_CyclesPerSec * 0.004 : 0); /* start, or after a pause */
    Fifo_LastT0 = t0;
    for (;;)
    {
        unsigned long long now = Gov_Tsc();
        int need = Fifo_Count < samples;          /* what this interrupt must output */
        int want = Fifo_Count < target;           /* render-ahead reserve */
        int tail, contig, n;
        if (!want) break;
        if (!need && now >= deadline) break;      /* reserve is optional: keep the interrupt short */
        if (need && now >= hardline) { ++Fifo_DutyCuts; break; } /* hopeless: give up, play silence */
        tail = (Fifo_Head + Fifo_Count) & (FIFO_FRAMES - 1);
        contig = FIFO_FRAMES - tail;
        n = target - Fifo_Count;
        if (n > slice) n = slice;
        if (n > contig) n = contig;
        if (Gov_On && Gov_CyclesPerSec > 0 && Fifo_Credit <= 0) { if (need) ++Fifo_DutyCuts; break; } /* CPU ceiling (off with /VSCAUTO:0) */
        if (Gov_On && Gov_CyclesPerSec > 0 && Cap_CyclesPerFrameQ8)   /* a smaller slice when little credit is left */
        {
            long long fit = (Fifo_Credit << 8) / (long long)Cap_CyclesPerFrameQ8;
            if (fit < n) n = fit < 16 ? 16 : (int)fit;
            if (n > contig) n = contig;
        }
        {
            unsigned long long s0 = Gov_Tsc();
            Fifo_RenderInto(Fifo + 2 * tail, n, freq);
            s0 = Gov_Tsc() - s0;
            spent += s0; Fifo_Credit -= (long long)s0;
        }
        Fifo_Count += n; rendered += n;
    }

    /* output */
    for (i = 0; i < samples; ++i)
    {
        int32_t l = 0, r = 0;
        if (Fifo_Count > 0)
        {
            l = Fifo[2 * Fifo_Head]; r = Fifo[2 * Fifo_Head + 1];
            Fifo_Head = (Fifo_Head + 1) & (FIFO_FRAMES - 1); --Fifo_Count;
        }
        else ++Fifo_Underruns;
        if (domix)
        {
            l += pcm16[2 * i]; r += pcm16[2 * i + 1];
            if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
            if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
        }
        pcm16[2 * i] = (int16_t)l; pcm16[2 * i + 1] = (int16_t)r;
    }

    if (rendered > 0)
    {
        if (Gov_CyclesPerSec > 0)
        {
            int pm = (int)(spent * 1000.0 * freq / (Gov_CyclesPerSec * rendered));
            if (pm > 9999) pm = 9999;
            Ld_EmaPm = (Ld_EmaPm * 15 + pm) / 16;
        }
        Cap_Note(spent, rendered);
        DIAG_Render(spent, rendered, freq, VMPU_SynthKind());
        if (VMPU_SynthKind() == 0) DIAG_SetSynthInfo(Gov_Level, Gov_Level >= 1 ? 0 : VMPU_LiteFx, VMPU_LiteVoices);
        if (VMPU_SynthKind() == 0 && Gov_On && Gov_CyclesPerSec > 0)
            Gov_Update(spent, rendered, freq);
    }
    if (VMPU_SynthKind() == 0) Gov_Fifo(Fifo_Count, target);
    DIAG_Fifo(Fifo_Count, (uint32_t)Fifo_Underruns);
    fpu_restore(fpu_buffer);
}

/* SB-MIDI: raw MIDI output through DSP command 0x38 goes to the same module. */
void VMPU_SBMidi_RawWrite(uint8_t value)
{
    if (VMPU_UseMT32) { MT32_PostByte(value); return; }
    if (VMPU_UseSF)
    {
        VMPUSF_SBMidi_RawWrite(value);
        return;
    }
    NL_Byte(value);
}

uint32_t VMPU_MPU(uint32_t port, uint32_t val, uint32_t out)
{
    /* reset / UART mode: drop report bytes nobody read (see DIAG_ResetFlush) */
    if (out && port == (uint32_t)VMPU_base + 1 && DIAG_ResetFlush((uint8_t)val)) Lcd_Pos = LCD_PKT;
    /* front panel + load report (any synth) */
    if (out && port == (uint32_t)VMPU_base + 1 && (val & 0xff) == 0xB0) { VMPU_PanelSnapshot(Lcd_Buf); Lcd_Pos = 0; return val; }
    if (!out && port == (uint32_t)VMPU_base && Lcd_Pos < LCD_PKT) { val &= ~0xff; return val | Lcd_Buf[Lcd_Pos++]; }
    /* SBDIAG side channel (works for both synths): commands 0xA6-0xA8, then report bytes on the data port */
    if (out && port == (uint32_t)VMPU_base + 1 && DIAG_Command((uint8_t)val))
        return val;
    if (!out && port == (uint32_t)VMPU_base)
    {
        uint8_t b;
        if (DIAG_ReadByte(&b)) { val &= ~0xff; return val | b; }
    }
    if (out && port == (uint32_t)VMPU_base && VMPU_UseSF) DIAG_Midi(0);
    if (VMPU_UseSF)
        return VMPUSF_MPU(port, val, out);
    return out ? (VMPU_Write(port, val), val) : (val &= ~0xff, val |= VMPU_Read(port));
}

#endif
