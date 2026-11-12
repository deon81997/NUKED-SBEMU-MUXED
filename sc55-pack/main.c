#include <unistd.h>
#include <sys/farptr.h>
#include <go32.h>
#include "sbemu/vdiag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <conio.h>
#include <stdbool.h>
#include <dos.h>
#include <stdarg.h>
#include <time.h>
#include <pc.h>
#include <dpmi.h>
#include <dpmi/dbgutil.h>
#include <sbemucfg.h>
#include <pic.h>
#include <opl3emu.h>
#include <vdma.h>
#include <virq.h>
#include <sbemu.h>
#include <untrapio.h>
#include <vmpu.h>
#include <dirent.h>
#include <dir.h>
#include <sys/stat.h>
#if SBEMU_VSC55
#include <sc55synth.h>
#endif
#include <vpcspeaker.h>
#include <cdlink.h>
#include "sbemu/usbpad.h"
#include "sbemu/vjoy.h"
#include "sbemu/vkbc.h"
#include "sbemu/padmap.h"
#include "qemm.h"
#include "hdpmipt.h"
#include "serial.h"
#include "irqguard.h"
#include "utility.h"

#include <au_cards/au_cards.h>
#include <au_cards/pcibios.h>

static const char *
PROGNAME = "SBEMU";

static BOOL MAIN_TSRed;

#ifndef MAIN_SBEMU_VER
#define MAIN_SBEMU_VER "1.0 beta3"
#endif

#define MAIN_TRAP_PMPIC_ONDEMAND 0 //now we need a Virtual PIC to hide some IRQ for protected mode games (doom especially)
#define MAIN_TRAP_RMPIC_ONDEMAND 1 //don't hide IRQ for rm, as some driver(i.e.usbuhci) will use it
#define MAIN_INSTALL_RM_ISR 1 //not needed. but to workaround some rm games' problem. need RAW_HOOk in dpmi_dj2.c - disble for more tests.
#define MAIN_ISR_CHAINED 0 //auto calls next handler AFTER current handler exits - cause more mode switches, disable for more tests.
#define MAIN_VMPU_HDPMI_MEMFIX 1 //workaround hdpmi bug that a TSR allocated memory being treated as primary client's, also better by avoiding DPMI memory fragments
                            //details on '_freeclientmemory' at I31MEM.ASM of HDPMI.
#define MAIN_PCM_RESAMPLE_INTERPOLATION 0

#define MAIN_SETIF 1 //merge form vdpmi branch, not suree if this works well enough

//note: use __dpmi_get_and_disable_virtual_interrupt_state()/__dpmi_get_and_enable_virtual_interrupt_state() causes crash.
//needs to find why HDPMI traps #GP well but int31h not not working.
#if MAIN_SETIF
#define SETIF()  __asm__ __volatile__("sti") //STI() //call __dpmi_get_and_disable_virtual_interrupt_state() directly
#define RESETIF() __asm__ __volatile__("cli") //CLI() //call __dpmi_get_and_enable_virtual_interrupt_state() directly
#else
#define SETIF() 
#define RESETIF() 
#endif

#define MAIN_TSR_INT 0x2D   //AMIS multiplex. TODO: 0x2F?
#define MAIN_TSR_INTSTART_ID 0x01 //start id

#if SBEMU_VMPU 

#define VMPU_ENABLED (MAIN_Options[OPT_VMPU_VOICES].value)

#if MAIN_VMPU_HDPMI_MEMFIX

#if SBEMU_VSC55
static size_t VMPU_MemSize = 32*1024*1024; //SC-55 emulation: ~21 MB core state + ~6 MB of ROM images while loading; with /VSF: 2x the .sf2 size + 8 MB (TinySoundFont keeps samples as floats)
#define MAIN_VMPU_MEMSIZE VMPU_MemSize
#define MAIN_USE_MT32 (MAIN_Options[OPT_VMT32].value && *(const char*)MAIN_Options[OPT_VMT32].value \
    && strcmp((const char*)MAIN_Options[OPT_VMT32].value, "0") && strcasecmp((const char*)MAIN_Options[OPT_VMT32].value, "OFF"))
#define MAIN_USE_SF (!MAIN_USE_MT32 && MAIN_Options[OPT_VSF].value && *(const char*)MAIN_Options[OPT_VSF].value)
#else
#define MAIN_VMPU_MEMSIZE (7*1024*1024) //OK for a 4M SF and a 6M (Yamaha XG) SF with TSF_FLOAT_LUT
#endif

static void* VMPU_Mem; //pre-alloc mem. this works because free() does not return dpmi memory immediately to dpmi host.

#define MAIN_VMPU_ALLOCMEM() while(!VMPU_Mem) { \
    VMPU_Mem = malloc(MAIN_VMPU_MEMSIZE); \
    if(!VMPU_Mem) { MAIN_CPrintf(RED, "Failed to allocate memory.\n"); exit(-1); } \
}

#define MAIN_VMPU_FREEMEM() do { \
    if(VMPU_Mem) {free(VMPU_Mem); VMPU_Mem = NULL; } \
} while(0)
#endif //MAIN_VMPU_HDPMI_MEMFIX

#else //SBEMU_VMPU

#define VMPU_ENABLED FALSE

#endif

static mpxplay_audioout_info_s aui = {0};
static mpxplay_audioout_info_s fm_aui = {0};
static mpxplay_audioout_info_s mpu401_aui = {0};

#define MAIN_PCM_SAMPLESIZE 16384

static int16_t MAIN_OPLPCM[MAIN_PCM_SAMPLESIZE];
static int16_t MAIN_PCM[MAIN_PCM_SAMPLESIZE];
static int16_t MAIN_PCMResample[MAIN_PCM_SAMPLESIZE];
static int MAIN_LastSBRate = 0;
static int16_t MAIN_LastResample[SBEMU_CHANNELS];

static DPMI_ISR_HANDLE MAIN_IntHandlePM;
static DPMI_ISR_HANDLE MAIN_IntHandleRM;
static DPMI_REG MAIN_RMIntREG;
static INTCONTEXT MAIN_IntContext;
static uint32_t MAIN_DMA_Addr = 0;
static uint32_t MAIN_DMA_Size = 0;
static uint32_t MAIN_DMA_MappedAddr = 0;
static uint8_t MAIN_QEMM_Present = 0;
static uint8_t MAIN_HDPMI_Present = 0;
static uint8_t MAIN_InINT;
#define MAIN_ININT_PM 0x01
#define MAIN_ININT_RM 0x02

SBEMU_EXTFUNS MAIN_SbemuExtFun;

static void MAIN_Interrupt();
static volatile uint32_t MAIN_CardIrqs;
extern uint16_t PCI_ReadWord(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
extern uint32_t PCI_ReadDWord(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
static int MAIN_DiagFrames; //frames produced by the last sound card interrupt (SBDIAG)
static void MAIN_InterruptPM();
static void MAIN_InterruptRM();

static DPMI_ISR_HANDLE MAIN_TSRIntHandle;
static DPMI_REG MAIN_TSRREG;
static DPMI_ISR_HANDLE MAIN_Int28Handle;   /* DOS idle (prompt, keyboard waits): CD-Audio file reads */
static DPMI_REG MAIN_Int28REG;
static BOOL MAIN_Int28On;
static void MAIN_Int28();
static void MAIN_CDService();
/* BIOS joystick: INT 15h AH=84h answered from the USB pad. A small real-mode stub sends only AH=84h to SBEMU
 * (INT 15h also carries the keyboard intercept AH=4Fh on every key, block moves etc.: those go on directly) */
static DPMI_REG MAIN_I15REG;
static void MAIN_Int15(void)
{
    DPMI_REG* r = &MAIN_I15REG;
    if(r->w.dx == 0) { r->h.al = VJOY_BiosButtons(); r->w.flags &= ~1; }
    else if(r->w.dx == 1)
    {
        uint16_t a[4];
        VJOY_BiosAxes(a);
        r->w.ax = a[0]; r->w.bx = a[1]; r->w.cx = a[2]; r->w.dx = a[3]; r->w.flags &= ~1;
    }
    else r->w.flags |= 1;
}
/* INT 15h stub (source: sbemu/int15.asm, nasm -f bin). Data at 0..1Fh, INT 15h entry at 20h, deliver at 24h:
 *  AH=84h DX=0/1 with joy on -> SBEMU callback (BIOS joystick from the USB pad); other AH=84h -> the BIOS.
 *  AH=C2h (PS/2 mouse BIOS services) -> the BIOS, and when it succeeds the stub notes what the mouse driver did:
 *  byte 9 = mouse enabled (C200h BH=1), 10 = packet size (C205h), 11 = resolution (C203h). Since beta.76 a
 *  USB mouse is sent through the BIOS this way when real-mode port traps are not there (no JEMM/QEMM): the
 *  mouse drivers (CTMOUSE, MOUSE.COM) run in real mode, so their port 60h/64h use cannot be trapped by HDPMI
 *  alone, and the USB mouse never got used - the D810's touchpad answered the driver instead.
 *  beta.78: C207h (the driver's packet handler) is passed to the BIOS as the stub's own (bioscb), which calls the
 *  driver's; the USB mouse's packets go to the driver's handler whole (deliver, far call from the sound
 *  interrupt) instead of byte by byte through the keyboard controller (D3h): BIOSes whose IRQ 12 handler reads
 *  all three bytes in one go (Award; the D810's) timed out waiting for the 2nd byte - nothing moved, or packets
 *  slipped into random moves and clicks. busy (byte 15) keeps the two callers apart. */
static uint32_t MAIN_I15Lin;   /* linear address of the stub (its data bytes), 0 = not hooked */
static const uint8_t MAIN_I15Stub[361] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE9, 0x04, 0x00, 0x90, 0xE9, 0xF7, 0x00, 0x80,
        0xFC, 0xC2, 0x74, 0x24, 0x80, 0xFC, 0x84, 0x75, 0x1A, 0x2E, 0xF6, 0x06, 0x08, 0x00, 0x01, 0x74, 0x12, 0x83, 0xFA, 0x01,
        0x77, 0x0D, 0x55, 0x89, 0xE5, 0x80, 0x66, 0x06, 0xFE, 0x5D, 0x2E, 0xFF, 0x2E, 0x04, 0x00, 0x2E, 0xFF, 0x2E, 0x00, 0x00,
        0x2E, 0xA2, 0x0C, 0x00, 0x2E, 0x88, 0x3E, 0x0D, 0x00, 0x2E, 0xFE, 0x06, 0x0E, 0x00, 0x3C, 0x07, 0x75, 0x1C, 0x2E, 0x89,
        0x1E, 0x1C, 0x00, 0x2E, 0x8C, 0x06, 0x1E, 0x00, 0x83, 0xFB, 0x00, 0x75, 0x08, 0x2E, 0x83, 0x3E, 0x1E, 0x00, 0x00, 0x74,
        0x05, 0x0E, 0x07, 0xBB, 0xFB, 0x00, 0x9C, 0x2E, 0xFF, 0x1E, 0x00, 0x00, 0x72, 0x60, 0x50, 0x2E, 0xA0, 0x0C, 0x00, 0x3C,
        0x07, 0x75, 0x16, 0x2E, 0xA1, 0x1C, 0x00, 0x89, 0xC3, 0x2E, 0xA3, 0x10, 0x00, 0x2E, 0xA1, 0x1E, 0x00, 0x8E, 0xC0, 0x2E,
        0xA3, 0x12, 0x00, 0xEB, 0x3C, 0x3C, 0x00, 0x75, 0x0C, 0x2E, 0xA0, 0x0D, 0x00, 0x24, 0x01, 0x2E, 0xA2, 0x09, 0x00, 0xEB,
        0x2C, 0x3C, 0x01, 0x74, 0x0C, 0x3C, 0x05, 0x75, 0x16, 0x2E, 0xA0, 0x0D, 0x00, 0x2E, 0xA2, 0x0A, 0x00, 0x2E, 0xC6, 0x06,
        0x09, 0x00, 0x00, 0x2E, 0xC6, 0x06, 0x0B, 0x00, 0x02, 0xEB, 0x0E, 0x3C, 0x03, 0x75, 0x0A, 0x2E, 0xA0, 0x0D, 0x00, 0x24,
        0x03, 0x2E, 0xA2, 0x0B, 0x00, 0x58, 0xF8, 0xCA, 0x02, 0x00, 0x2E, 0x80, 0x3E, 0x0C, 0x00, 0x07, 0x75, 0x0A, 0x2E, 0x8B,
        0x1E, 0x1C, 0x00, 0x2E, 0x8E, 0x06, 0x1E, 0x00, 0xCA, 0x02, 0x00, 0x55, 0x89, 0xE5, 0x2E, 0xFE, 0x06, 0x0F, 0x00, 0xFF,
        0x76, 0x0C, 0xFF, 0x76, 0x0A, 0xFF, 0x76, 0x08, 0xFF, 0x76, 0x06, 0x2E, 0xFF, 0x1E, 0x10, 0x00, 0x83, 0xC4, 0x08, 0x2E,
        0xFE, 0x0E, 0x0F, 0x00, 0x5D, 0xCB, 0x31, 0xC0, 0x2E, 0x80, 0x3E, 0x0F, 0x00, 0x00, 0x75, 0x3B, 0x2E, 0xF6, 0x06, 0x09,
        0x00, 0x01, 0x74, 0x33, 0x2E, 0x83, 0x3E, 0x12, 0x00, 0x00, 0x74, 0x2B, 0x2E, 0xFE, 0x06, 0x0F, 0x00, 0x2E, 0xFF, 0x36,
        0x14, 0x00, 0x2E, 0xFF, 0x36, 0x16, 0x00, 0x2E, 0xFF, 0x36, 0x18, 0x00, 0x50, 0x2E, 0xFF, 0x1E, 0x10, 0x00, 0x83, 0xC4,
        0x08, 0x2E, 0xFE, 0x0E, 0x0F, 0x00, 0x2E, 0xFE, 0x06, 0x1A, 0x00, 0xB8, 0x01, 0x00, 0xCB, 0x2E, 0xFE, 0x06, 0x1B, 0x00,
        0xCB,
};
static BOOL MAIN_HookInt15(int joy)
{
    uint32_t cb = joy ? DPMI_AllocateRMCB_IRET(&MAIN_Int15, &MAIN_I15REG) : 0, mem;
    uint8_t code[sizeof(MAIN_I15Stub)];
    uint16_t seg, old_off, old_seg;
    if(MAIN_I15Lin) return TRUE;
    if((joy && !cb) || (mem = DPMI_HighMalloc((sizeof(code) + 15) / 16, TRUE)) == 0) return FALSE;
    seg = (uint16_t)mem;
    memcpy(code, MAIN_I15Stub, sizeof code);
    old_off = DPMI_LoadW(0x15 * 4); old_seg = DPMI_LoadW(0x15 * 4 + 2);
    memcpy(code + 0, &old_off, 2); memcpy(code + 2, &old_seg, 2);
    memcpy(code + 4, &cb, 4);
    code[8] = joy ? 1 : 0;
    DPMI_LMemcpy(DPMI_SEGOFF2L(seg, 0), DPMI_PTR2L(code), sizeof(code));
    MAIN_I15Lin = DPMI_SEGOFF2L(seg, 0);
    CLIS();
    DPMI_StoreW(0x15 * 4, 0x20); DPMI_StoreW(0x15 * 4 + 2, seg);
    STIL();
    return TRUE;
}
/* Pad layout per game (padmap.c): which program is running. A real-mode INT 21h stub (sbemu/int21.asm, bytes in
 * int21bin.h) sees every EXEC (AX=4B00h) before DOS does, finds the program's full name and size and calls back here;
 * it runs the EXEC itself, so it also calls back when the program has ended, however it ended. The callback only
 * looks ids up in the table loaded at start (no file reads, never from the sound interrupt) and keeps a stack of
 * running programs; the layout used is the newest program found by its own id, else the newest found by a file in
 * its folder (data files identify games too), else Default - as SC55W does it in Windows 98. */
#include "sbemu/int21bin.h"
#include "sbemu/usbdisk.h"
#define MAIN_PADSTK 8
static DPMI_REG MAIN_I21REG;
static uint32_t MAIN_I21Lin;           /* the stub (its data bytes), 0 = not hooked */
static uint32_t MAIN_I21Cb;
static struct { int lay, how; char name[13], where[13]; } MAIN_PadStk[MAIN_PADSTK];   /* how: 2 own id, 1 a file in its folder */
static int MAIN_PadDepth;
static char MAIN_PadLog[8][100];       /* last layout picks, oldest first */
static int MAIN_PadLogN;
static uint32_t MAIN_PadExecs;
static BOOL MAIN_PadOn;                /* layouts loaded and the stub hooked */

static void MAIN_PadName(char* d, uint32_t lin)   /* 8.3 name after the last backslash of a real-mode string */
{
    char b[128];
    const char* n;
    char* e;
    DPMI_LMemcpy(DPMI_PTR2L(b), lin, sizeof b);
    b[sizeof b - 1] = 0;
    n = strrchr(b, '\\'); n = n ? n + 1 : b;
    strncpy(d, n, 12); d[12] = 0;
    for(e = d; *e; ++e) *e = (char)toupper((unsigned char)*e);
}

static void MAIN_PadPick(const char* prog, int always)   /* always: log it even when the layout stays */
{
    int i, lay = 0, at = -1, n = MAIN_PadDepth < MAIN_PADSTK ? MAIN_PadDepth : MAIN_PADSTK;
    char* l;
    for(i = n - 1; i >= 0 && at < 0; --i) if(MAIN_PadStk[i].how == 2) at = i;
    for(i = n - 1; i >= 0 && at < 0; --i) if(MAIN_PadStk[i].how == 1) at = i;
    if(at >= 0) lay = MAIN_PadStk[at].lay;
    if(lay == PADMAP_Current() && !always && MAIN_PadLogN) return;
    PADMAP_Select(lay);
    if(MAIN_PadLogN == 8) { memmove(MAIN_PadLog[0], MAIN_PadLog[1], sizeof(MAIN_PadLog[0]) * 7); MAIN_PadLogN = 7; }
    l = MAIN_PadLog[MAIN_PadLogN++];
    if(at < 0) snprintf(l, sizeof MAIN_PadLog[0], "%s: Default (%s)", prog, n ? "no layout found" : "back at the prompt");
    else if(MAIN_PadStk[at].how == 2)
        snprintf(l, sizeof MAIN_PadLog[0], "%s: %.60s (%s found by its id)", prog, PADMAP_Title(lay), MAIN_PadStk[at].where);
    else snprintf(l, sizeof MAIN_PadLog[0], "%s: %.60s (%s in the folder of %s)", prog, PADMAP_Title(lay), MAIN_PadStk[at].where, MAIN_PadStk[at].name);
}

static void MAIN_Int21(void)
{
    DPMI_REG* r = &MAIN_I21REG;
    const uint32_t id = r->d.ebx;
    const int top = MAIN_PadDepth - 1;
    int l;
    switch(r->h.al)
    {
    case 1:                                         /* a program starts: its own id (0 = file not found) */
        ++MAIN_PadExecs;
        r->h.al = 1;
        if(MAIN_PadDepth++ >= MAIN_PADSTK) return;
        MAIN_PadStk[top + 1].lay = 0; MAIN_PadStk[top + 1].how = 0; MAIN_PadStk[top + 1].where[0] = 0;
        MAIN_PadName(MAIN_PadStk[top + 1].name, MAIN_I21Lin + MAIN_I21_PATH);
        if(!id) return;
        if((l = PADMAP_Find(id)) >= 0)
        {
            MAIN_PadStk[top + 1].lay = l; MAIN_PadStk[top + 1].how = 2;
            strcpy(MAIN_PadStk[top + 1].where, MAIN_PadStk[top + 1].name);
            return;
        }
        r->h.al = 0;                                /* try the files in its folder */
        return;
    case 2:                                         /* a file in its folder */
        r->h.al = 0;
        if(top < 0 || top >= MAIN_PADSTK || (l = PADMAP_Find(id)) < 0) return;
        MAIN_PadStk[top].lay = l; MAIN_PadStk[top].how = 1;
        MAIN_PadName(MAIN_PadStk[top].where, MAIN_I21Lin + MAIN_I21_NAME);
        r->h.al = 1;
        return;
    case 3:                                         /* look-up done */
        if(top >= 0 && top < MAIN_PADSTK) MAIN_PadPick(MAIN_PadStk[top].name, 1);
        return;
    case 4:                                         /* it has ended */
        if(MAIN_PadDepth > 0)
        {
            char n[13];
            strcpy(n, top < MAIN_PADSTK ? MAIN_PadStk[top].name : "?");
            --MAIN_PadDepth;
            if(MAIN_PadDepth > 0 && MAIN_PadDepth <= MAIN_PADSTK)
            {   /* name the program now running again */
                char m[40];
                snprintf(m, sizeof m, "%s ended, %s", n, MAIN_PadStk[MAIN_PadDepth - 1].name);
                MAIN_PadPick(m, 0);
            }
            else { strcat(n, " ended"); MAIN_PadPick(n, 0); }
        }
        return;
    }
}

static BOOL MAIN_HookInt21(void)
{
    uint8_t code[sizeof(MAIN_I21Stub)];
    uint32_t mem;
    uint16_t seg, old_off, old_seg;
    if(MAIN_I21Lin) return TRUE;
    if(!MAIN_I21Cb) MAIN_I21Cb = DPMI_AllocateRMCB_IRET(&MAIN_Int21, &MAIN_I21REG);
    if(!MAIN_I21Cb || (mem = DPMI_HighMalloc((sizeof(code) + 15) / 16, TRUE)) == 0) return FALSE;
    seg = (uint16_t)mem;
    memcpy(code, MAIN_I21Stub, sizeof code);
    old_off = DPMI_LoadW(0x21 * 4); old_seg = DPMI_LoadW(0x21 * 4 + 2);
    memcpy(code + 0, &old_off, 2); memcpy(code + 2, &old_seg, 2);
    memcpy(code + 4, &MAIN_I21Cb, 4);
    code[8] = 1;
    DPMI_LMemcpy(DPMI_SEGOFF2L(seg, 0), DPMI_PTR2L(code), sizeof(code));
    MAIN_I21Lin = DPMI_SEGOFF2L(seg, 0);
    CLIS();
    DPMI_StoreW(0x21 * 4, MAIN_I21_ENTRY); DPMI_StoreW(0x21 * 4 + 2, seg);
    STIL();
    return TRUE;
}

static void MAIN_UnhookInt21(void)    /* start-up failed: give INT 21h back if nothing came after us */
{
    if(!MAIN_I21Lin) return;
    CLIS();
    if(DPMI_LoadW(0x21 * 4) == MAIN_I21_ENTRY && DPMI_LoadW(0x21 * 4 + 2) == (uint16_t)(MAIN_I21Lin >> 4))
    {
        DPMI_StoreW(0x21 * 4, DPMI_LoadW(MAIN_I21Lin)); DPMI_StoreW(0x21 * 4 + 2, DPMI_LoadW(MAIN_I21Lin + 2));
    }
    else DPMI_StoreB(MAIN_I21Lin + 8, 0);  /* someone hooked after us: just switch the stub off */
    STIL();
}

/* PADMAP.TXT (made from keyb2joypad.cpp in PADMAP\\ when that is new), PADUSER.TXT -> PADMAP.BIN (rebuilt when any
 * of them changed), loaded whole: padmap.c PADMAP_Prepare. 1 = layouts there. */
static void MAIN_Step(const char* what);
static void MAIN_D(const char* fmt, ...);
static void MAIN_CPrintf(int color, const char* format, ...);
static void MAIN_PadNote(int kind, const char* text)
{
    if(kind == 0)   /* MAIN_Step keeps the pointer: only fixed strings */
    {
        MAIN_Step(strncmp(text, "making PADMAP.TXT", 17) == 0 ? "pad layouts: PADMAP.TXT from keyb2joypad.cpp" : "pad layouts: PADMAP.BIN");
        printf("Pad layouts: %s ...\n", text);
    }
    else if(kind == 1) { MAIN_CPrintf(YELLOW, "Pad layouts: %s - /JOYKEY only.\n", text); MAIN_D("Pad layouts: %s\n", text); }
    else MAIN_D("Pad layouts: %s\n", text);
}
static int MAIN_PadLoad(void)
{
    char dir[_MAX_PATH], d[400];
    int ids = 0, n;
    get_program_path(dir, sizeof dir);
    if(!PADMAP_Prepare(dir, MAIN_PadNote)) return 0;
    n = PADMAP_Count(&ids);
    PADMAP_Describe(0, d, sizeof d);
    MAIN_D("Pad layout Default: %s\n", d);
    printf("Pad buttons as keys, layout per game (%d games): ", n - 1);
    return 1;
}

static uint32_t MAIN_TSR_INT_FNO = MAIN_TSR_INTSTART_ID;
static uint32_t MAIN_ISR_DOSID;
static const char MAIN_ISR_DOSID_String[] = "Crazii  SBEMU   Sound Blaster emulation on AC97"; //8:8:asciiz
static void MAIN_TSR_InstallationCheck();
static void MAIN_TSR_Interrupt();

#define HW_FM_HANDLER(n) do {\
  if (fm_aui.fm) {                                                      \
    if (out) {                                                          \
      if (fm_aui.card_handler->card_fm_write) {                         \
        fm_aui.card_handler->card_fm_write(&fm_aui, n, (uint8_t)(val & 0xff)); \
        return val;                                                     \
      }                                                                 \
    } else {                                                            \
      if (fm_aui.card_handler->card_fm_read)                            \
        return fm_aui.card_handler->card_fm_read(&fm_aui, n);           \
    }                                                                   \
  }                                                                     \
  return 0;                                                             \
  } while (0)

static uint32_t MAIN_OPL3_388(uint32_t port, uint32_t val, uint32_t out)
{
  return out ? OPL3EMU_PrimaryWriteIndex(val) : OPL3EMU_PrimaryRead(val);
}
static uint32_t MAIN_OPL3_389(uint32_t port, uint32_t val, uint32_t out)
{
  return out ? OPL3EMU_PrimaryWriteData(val) : OPL3EMU_PrimaryRead(val);
}
static uint32_t MAIN_OPL3_38A(uint32_t port, uint32_t val, uint32_t out)
{
  return out ? OPL3EMU_SecondaryWriteIndex(val) : OPL3EMU_SecondaryRead(val);
}
static uint32_t MAIN_OPL3_38B(uint32_t port, uint32_t val, uint32_t out)
{
  return out ? OPL3EMU_SecondaryWriteData(val) : OPL3EMU_SecondaryRead(val);
}

static uint32_t MAIN_HW_OPL3_388(uint32_t port, uint32_t val, uint32_t out)
{
  HW_FM_HANDLER(0);
}
static uint32_t MAIN_HW_OPL3_389(uint32_t port, uint32_t val, uint32_t out)
{
  HW_FM_HANDLER(1);
}
static uint32_t MAIN_HW_OPL3_38A(uint32_t port, uint32_t val, uint32_t out)
{
  HW_FM_HANDLER(2);
}
static uint32_t MAIN_HW_OPL3_38B(uint32_t port, uint32_t val, uint32_t out)
{
  HW_FM_HANDLER(3);
}

static uint32_t MAIN_DMA(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? (VDMA_Write(port, val), val) : (val &=~0xFF, val |= VDMA_Read(port));
}

static uint32_t MAIN_IRQ(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? (VIRQ_Write(port, val), val) : (val &=~0xFF, val |= VIRQ_Read(port));
}

static uint32_t MAIN_SB_MixerAddr(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? (SBEMU_Mixer_WriteAddr(port, val), val) : val;
}
static uint32_t MAIN_SB_MixerData(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? (SBEMU_Mixer_Write(port, val), val) : (val &=~0xFF, val |= SBEMU_Mixer_Read(port));
}
static uint32_t MAIN_SB_DSP_Reset(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? (SBEMU_DSP_Reset(port, val), val) : val;
}
static uint32_t MAIN_SB_DSP_Read(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? val : (val &=~0xFF, val |= SBEMU_DSP_Read(port));
}
static uint32_t MAIN_SB_DSP_Write(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? (SBEMU_DSP_Write(port, val), val) : SBEMU_DSP_WriteStatus(port);
}
static uint32_t MAIN_SB_DSP_ReadStatus(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? val : (val &=~0xFF, val |= SBEMU_DSP_ReadStatus(port));
}
static uint32_t MAIN_SB_DSP_ReadINT16BitACK(uint32_t port, uint32_t val, uint32_t out)
{
    return out ? val : (val &=~0xFF, val |= SBEMU_DSP_INT16ACK(port));
}

int mpu_state = 0;
#define MPU_DEBUG 1
#if MPU_DEBUG
static int mpu_debug = 0;
static int mpu_dbg_ctr = 0;
#endif

static uint32_t MAIN_MPU_330(uint32_t port, uint32_t val, uint32_t out)
{
#if MPU_DEBUG
  if (mpu_debug) {
    if (out) {
      if (mpu_debug >= 2) {
        mpu_dbg_ctr++;
        char c = ' ';
        if (mpu_dbg_ctr == 26) {
          c = '\n';
          mpu_dbg_ctr = 0;
        }
        DBG_Logi("%02x%c", val, c);
      }
    } else {
      DBG_Logi("r%x\n", mpu_state);
    }
  }
#endif
  if (out) {
    if (mpu401_aui.mpu401 && mpu401_aui.card_handler->card_mpu401_write)
      mpu401_aui.card_handler->card_mpu401_write(&mpu401_aui, 0, (uint8_t)(val & 0xff));
    ser_putbyte((int)(val & 0xff));
    return 0;
  } else {
    uint8_t val, hwval, hwval_valid = 0;
    if (mpu401_aui.mpu401 && mpu401_aui.card_handler->card_mpu401_read) {
      hwval = mpu401_aui.card_handler->card_mpu401_read(&mpu401_aui, 0);
      if (!mpu401_aui.mpu401_softread)
        hwval_valid = 1;
    }
    if (mpu_state == 1) {
      mpu_state = 0;
      val = 0xfe;
    } else if (mpu_state == 2) {
      mpu_state = 4;
      val = 0xfe;
    } else {
      val = 0;
    }
    if (hwval_valid)
      return hwval;
    else
      return val;
  }
}
static uint32_t MAIN_MPU_331(uint32_t port, uint32_t val, uint32_t out)
{
#if MPU_DEBUG
  if (mpu_debug) {
    if (out) {
      DBG_Logi("s%x\n", val);
    } else {
      if (mpu_dbg_ctr < 10 && mpu_state <= 2) {
        DBG_Logi("sr%x\n", mpu_state);
        mpu_dbg_ctr++;
      }
    }
  }
#endif
  if (out) {
    if (mpu401_aui.mpu401 && mpu401_aui.card_handler->card_mpu401_write)
      mpu401_aui.card_handler->card_mpu401_write(&mpu401_aui, 1, (uint8_t)(val & 0xff));
    if (val == 0xff) { // Reset
#if MPU_DEBUG
      mpu_dbg_ctr = 0;
#endif
      mpu_state = 1;
    } else if (val == 0x3f) { // UART mode
#if MPU_DEBUG
      mpu_dbg_ctr = 0;
#endif
      mpu_state = 2;
    }
    return 0;
  }
  if (mpu401_aui.mpu401 && mpu401_aui.card_handler->card_mpu401_read) {
    uint8_t hwval = mpu401_aui.card_handler->card_mpu401_read(&mpu401_aui, 1);
    if (!mpu401_aui.mpu401_softread)
      return hwval;
  }
  if ((mpu_state & 3) == 0) {
    return 0x80;
  } else {
    return 0;
  }
}

static QEMM_IODT MAIN_MPUIODT[2] =
{
    0x330, &MAIN_MPU_330,
    0x331, &MAIN_MPU_331
};

static QEMM_IODT MAIN_OPL3IODT[4] =
{
    0x388, &MAIN_OPL3_388,
    0x389, &MAIN_OPL3_389,
    0x38A, &MAIN_OPL3_38A,
    0x38B, &MAIN_OPL3_38B
};

static QEMM_IODT MAIN_HW_OPL3IODT[4] =
{
    0x388, &MAIN_HW_OPL3_388,
    0x389, &MAIN_HW_OPL3_389,
    0x38A, &MAIN_HW_OPL3_38A,
    0x38B, &MAIN_HW_OPL3_38B
};

static QEMM_IODT MAIN_VDMA_IODT[40] =
{
    0x00, &MAIN_DMA,
    0x01, &MAIN_DMA,
    0x02, &MAIN_DMA,
    0x03, &MAIN_DMA,
    0x04, &MAIN_DMA,
    0x05, &MAIN_DMA,
    0x06, &MAIN_DMA,
    0x07, &MAIN_DMA,
    0x08, &MAIN_DMA,
    0x09, &MAIN_DMA,
    0x0A, &MAIN_DMA,
    0x0B, &MAIN_DMA,
    0x0C, &MAIN_DMA,
    0x0D, &MAIN_DMA,
    0x0E, &MAIN_DMA,
    0x0F, &MAIN_DMA,
    0x81, &MAIN_DMA,
    0x82, &MAIN_DMA,
    0x83, &MAIN_DMA,
    0x87, &MAIN_DMA,

    0xC0, &MAIN_DMA,
    0xC2, &MAIN_DMA,
    0xC4, &MAIN_DMA,
    0xC6, &MAIN_DMA,
    0xC8, &MAIN_DMA,
    0xCA, &MAIN_DMA,
    0xCC, &MAIN_DMA,
    0xCE, &MAIN_DMA,
    0xD0, &MAIN_DMA,
    0xD2, &MAIN_DMA,
    0xD4, &MAIN_DMA,
    0xD6, &MAIN_DMA,
    0xD8, &MAIN_DMA,
    0xDA, &MAIN_DMA,
    0xDC, &MAIN_DMA,
    0xDE, &MAIN_DMA,
    0x89, &MAIN_DMA,
    0x8A, &MAIN_DMA,
    0x8B, &MAIN_DMA,
    0x8F, &MAIN_DMA,
};

static QEMM_IODT MAIN_VIRQ_IODT[4] =
{
    0x20, &MAIN_IRQ,
    0x21, &MAIN_IRQ,
    0xA0, &MAIN_IRQ,
    0xA1, &MAIN_IRQ,
};

static QEMM_IODT MAIN_SB_IODT[13] =
{ //MAIN_Options[OPT_ADDR].value will be added at runtime
    0x00, &MAIN_OPL3_388,
    0x01, &MAIN_OPL3_389,
    0x02, &MAIN_OPL3_38A,
    0x03, &MAIN_OPL3_38B,
    0x04, &MAIN_SB_MixerAddr,
    0x05, &MAIN_SB_MixerData,
    0x06, &MAIN_SB_DSP_Reset,
    0x08, &MAIN_OPL3_388,
    0x09, &MAIN_OPL3_389,
    0x0A, &MAIN_SB_DSP_Read,
    0x0C, &MAIN_SB_DSP_Write,
    0x0E, &MAIN_SB_DSP_ReadStatus,
    0x0F, &MAIN_SB_DSP_ReadINT16BitACK,
};

QEMM_IOPT OPL3IOPT;
QEMM_IOPT OPL3IOPT_PM;
QEMM_IOPT MPUIOPT;
QEMM_IOPT MPUIOPT_PM;
QEMM_IOPT MAIN_VDMA_IOPT;
QEMM_IOPT MAIN_VIRQ_IOPT;
QEMM_IOPT MAIN_SB_IOPT;
QEMM_IOPT MAIN_VDMA_IOPT_PM1;
QEMM_IOPT MAIN_VDMA_IOPT_PM2;
QEMM_IOPT MAIN_VDMA_IOPT_PM3;
QEMM_IOPT MAIN_VHDMA_IOPT_PM1;
QEMM_IOPT MAIN_VHDMA_IOPT_PM2;
QEMM_IOPT MAIN_VHDMA_IOPT_PM3;
QEMM_IOPT MAIN_VIRQ_IOPT_PM1;
QEMM_IOPT MAIN_VIRQ_IOPT_PM2;
QEMM_IOPT MAIN_SB_IOPT_PM;

// PC Speaker emulation IO trap handles.
// Note: QEMM accepts the whole 3-port table in a single call, but HDPMI requires
// contiguous ranges, so 0x42-0x43 and 0x61 need separate HDPMI trap handles.
#if SBEMU_PCSPEAKER
static QEMM_IODT MAIN_PCSPEAKER_IODT[3];
static QEMM_IOPT MAIN_PCSPEAKER_IOPT;
static QEMM_IOPT MAIN_PCSPEAKER_IOPT_61;
static BOOL     MAIN_PCSPEAKER_Installed = FALSE;
#endif

#define MAIN_SETCMD_INTRL 0x80000000L //hack: internal. used by TSR communication
#define MAIN_SETCMD_CHGD 0x40000000L //value changed (extra states)
#define OPT_CHANGED(i) (MAIN_Options[i].setcmd&MAIN_SETCMD_CHGD)

#define MAIN_SETCMD_SET 0x01 //set in command line
#define MAIN_SETCMD_HIDDEN 0x02 //hidden flag on report
#define MAIN_SETCMD_BASE10 0x04 //use decimal value (default hex)
#define MAIN_SETCMD_STR 0x08 //string
#define MAIN_SETCMD_STRCS 0x10 //case sensitive
#define MAIN_SETCMD_STRCPY 0x20 //a copy of string, needs free

struct MAIN_OPT
{
    const char* option;
    const char* desc;
    int value;
    int setcmd; //set by command line
}MAIN_Options[] =
{
    "/?", "Show this help screen", FALSE, 0,
    "/DBG", "Debug output (0=console, 1=COM1, 2=COM2, 3=COM3, 4=COM4, otherwise base address)", 0, 0,

    "/A", "IO address (220 or 240) [*]", 0x220, 0,
    "/I", "IRQ number (5 or 7 or 9) [*]", 7, 0,
    "/D", "8-bit DMA channel (0, 1 or 3) [*]", 1, 0,
    "/T", "SB Type (1, 2 or 3=SB; 4 or 5=SBPro; 6=SB16) [*]", 5, 0,
    "/H", "16-bit (\"high\") DMA channel (5, 6 or 7) [*]", 5, 0,

    "/OPL", "Enable OPL3 emulation", TRUE, 0,
    "/PM", "Enable protected mode support (requires HDPMI32I)", TRUE, 0,
    "/RM", "Enable real mode support (requires QEMM or JEMM+QPIEMU)", TRUE, 0,
    "/FMCORE", "OPL3 FM core: 0=DOSBox DBOPL (fast), 1=Nuked OPL3 from VOPL3 (cycle accurate, heavier)", 0, MAIN_SETCMD_BASE10,

    "/O", "Select output. 0: headphone, 1: speaker (Intel HDA) or S/PDIF (Xonar DG)", 1, 0,
    "/VOL", "Set master volume (0-" STR(SBEMU_VOLUME_MAX) ")", 80, MAIN_SETCMD_BASE10,

    "/K", "Internal sample rate", 22050, MAIN_SETCMD_BASE10,
    "/FIXTC", "Fix time constant to match 11/22/44 kHz sample rate", TRUE, 0,
    "/SCL", "List installed sound cards", 0, MAIN_SETCMD_HIDDEN,
    "/SCFM", "Select FM(OPL) sound card index in list (/SCL)", 0, MAIN_SETCMD_HIDDEN|MAIN_SETCMD_BASE10,
    "/SCMPU", "Select MPU-401 sound card index in list (/SCL)", 0, MAIN_SETCMD_HIDDEN|MAIN_SETCMD_BASE10,
    "/SC", "Select sound card index in list (/SCL)", 0, MAIN_SETCMD_HIDDEN|MAIN_SETCMD_BASE10,
    "/R", "Reset sound card driver", 0, MAIN_SETCMD_HIDDEN,
    "/P", "UART mode MPU-401 IO address [*]", 0x330, 0,
    "/MCOM", "UART mode MPU-401 COM port (1=COM1, 2=COM2, 3=COM3, 4=COM4, 9:HW MPU only, otherwise base address)", 9, 0,
    "/COML", "List installed COM ports", 0, MAIN_SETCMD_HIDDEN,
#if MPU_DEBUG
    "/MDBG", "Enable MPU-401 debugging (0 to disable, 1 or 2 to enable)", 0, 0,
#endif
#if SBEMU_VMPU
#if SBEMU_VSC55
    "/VMPU","Virtual MPU-401 played by a Roland SC-55 emulation (needs your own ROM dumps, 0=disabled)", 0, MAIN_SETCMD_BASE10, //also switched on by /VSCROM or /VSCSET
    "/VSCROM", "SC-55 ROM folder (ROMs are recognised by content; file names do not matter)", (uintptr_t)VMPU_DEF_ROMS, MAIN_SETCMD_STR,
    "/VMSFBF", "", 0, MAIN_SETCMD_INTRL|MAIN_SETCMD_HIDDEN, //unused with the SC-55 back end, kept so the table layout matches
    "/VSCSET", "SC-55 ROM set: auto or a name from the list below", (uintptr_t)"auto", MAIN_SETCMD_STR,
    "/VSCGAIN", "SC-55 output gain in percent (100=unity, 0-1600)", 100, MAIN_SETCMD_BASE10,
    "/VSCVOICES", "SC-55 note limit (4-24, 0=off): most notes held at once; the oldest is released first, like a real synth. Lighter on the CPU", 0, MAIN_SETCMD_BASE10,
    "/VSCFX", "SC-55 reverb and chorus (1=on, 0=off: lighter on the CPU, sound is dry)", 1, MAIN_SETCMD_BASE10,
    "/VSCAUTO", "SC-55 automatic speed control (1=on: sheds voices/effects when the CPU cannot keep up, so games keep their timing; 0=off)", 1, MAIN_SETCMD_BASE10,
    "/VSCAHEAD", "SC-55/SoundFont render-ahead in ms (0=off: render inside each interrupt like beta.16; default 50)", 50, MAIN_SETCMD_BASE10,
    "/VTIMER", "Give the game back timer ticks lost while SBEMU renders music (1=on, 0=off). Keeps MIDI tempo right in games", 1, MAIN_SETCMD_BASE10,
    "/VSF", "Play the MPU-401 with this SoundFont (.sf2 file) instead of the SC-55 emulation (much lighter on the CPU)", (uintptr_t)"", MAIN_SETCMD_STR,
    "/VSFREV", "SoundFont reverb level in percent (0=off, 1-100)", 0, MAIN_SETCMD_BASE10,
    "/VSFROOM", "SoundFont reverb room size in percent (0-100, default 60)", 60, MAIN_SETCMD_BASE10,
    "/VSFDAMP", "SoundFont reverb damping in percent (0-100, default 50)", 50, MAIN_SETCMD_BASE10,
    "/VSFCHO", "SoundFont chorus level in percent (0=off, 1-100)", 0, MAIN_SETCMD_BASE10,
    "/VSFCRATE", "SoundFont chorus speed in tenths of Hz (1-30, default 8 = 0.8 Hz)", 8, MAIN_SETCMD_BASE10,
    "/VSFCDEPTH", "SoundFont chorus depth in percent (0-100, default 50)", 50, MAIN_SETCMD_BASE10,
    "/VSFSEND", "SoundFont effects follow the song's reverb/chorus sends, MIDI CC91/CC93 (1=yes, 0=same level for every channel)", 1, MAIN_SETCMD_BASE10,
    "/VSFVOICES", "SoundFont synth: most voices sounding at once (8-128, default 64). Fewer = lighter on the CPU", 64, MAIN_SETCMD_BASE10,
    "/VMT32", "Play the MPU-401 with the Roland MT-32 emulation (Munt) instead of the SC-55: AUTO, MT32 or CM32L (your MT-32 ROMs in the ROM folder)", (uintptr_t)"", MAIN_SETCMD_STR,
#else
    "/VMPU","Virtual MPU max voices ([32-256]. 1=64, 0=disabled)", 0, MAIN_SETCMD_BASE10 | (MAIN_VMPU_HDPMI_MEMFIX ? 0 : MAIN_SETCMD_HIDDEN), //voice num also act as a switch. 0:turn off VMPU
    "/VMSF", "VMPU Soundfont2 file", (uintptr_t)VMPU_DEF_SF2, MAIN_SETCMD_STR | (MAIN_VMPU_HDPMI_MEMFIX ? 0 : MAIN_SETCMD_HIDDEN), //NOTE: string ptr to int conversion always safe for 32bit code
    "/VMSFBF", "", 0, MAIN_SETCMD_INTRL|MAIN_SETCMD_HIDDEN,
#endif
#endif
    "/VCDA", "CD-Audio from uCDD (--sbemu-link) mounted CUE/BIN images: output gain in percent (0=off, 100=unity, 1-400)", 0, MAIN_SETCMD_BASE10,
    "/VSBVOL", "Sound effects (digital Sound Blaster) volume in percent (0-100), on top of the game's own mixer", 100, MAIN_SETCMD_BASE10,
    "/VOPLVOL", "FM (OPL / AdLib) music volume in percent (0-100), on top of the game's own mixer", 100, MAIN_SETCMD_BASE10,
    "/VMIDIVOL", "General MIDI music volume in percent (0-400; SC-55: multiplies /VSCGAIN; SoundFont: its master volume)", 100, MAIN_SETCMD_BASE10,
    "/JOY", "USB in DOS: up to two USB pads (Xbox 360, Xbox One, HID gamepads) as the PC gameport joystick at port 201h, plus USB keyboards / mice (1=on, 0=off; any UHCI / OHCI / EHCI / xHCI USB port, hubs allowed)", 1, MAIN_SETCMD_BASE10,
    "/JOYAX", "Pad -> gameport axes 1-4 (joystick A X,Y, joystick B X,Y): LX LY RX RY LT RT TR(RT minus LT) or - ; ~ in front inverts", (uintptr_t)"LX,LY,RX,RY", MAIN_SETCMD_STR,
    "/JOYBT", "Pad -> gameport buttons 1-4: A B X Y LB RB LT RT ST BK LS RS GD DU DD DL DR or -", (uintptr_t)"A,B,X,Y", MAIN_SETCMD_STR,
    "/JOYKEY", "Pad buttons typed as keys, button:scan code (hex, E0xx extended), e.g. ST:1C,BK:01 (Start=Enter, Back=Esc)", (uintptr_t)"ST:1C,BK:01", MAIN_SETCMD_STR,
    "/JOYDZ", "Stick dead zone in percent (0-90)", 10, MAIN_SETCMD_BASE10,
    "/JOYMODE", "Gameport stick type: 0=standard, 1=CH Flightstick Pro (D-pad = hat), 2=Thrustmaster FCS (D-pad = hat on axis 4), 3=Gravis GamePad Pro digital (GrIP, all buttons, 2 pads), 4=none (real gameport stays usable)", 0, MAIN_SETCMD_BASE10,
    "/USBKB", "USB keyboard typed through the PC's keyboard controller (1=on, 0=off; needs /JOY:1)", 1, MAIN_SETCMD_BASE10,
    "/USBMS", "USB mouse for a DOS mouse driver loaded after SBEMU (1=auto, 2=always, 0=off; needs /JOY:1). Without JEMM/QEMM it goes through the BIOS, next to the touchpad", 1, MAIN_SETCMD_BASE10,
    "/USBDISK", "USB sticks / card readers as DOS drive letters, FAT12/16/32, read and write (1=on, 0=off; needs /JOY:1 and LASTDRIVE high enough). Experimental", 0, MAIN_SETCMD_BASE10,
    "/JOYBIOS", "BIOS joystick calls (INT 15h AH=84h) answered from the USB pad too (1=on, 0=off; needs /JOY:1)", 1, MAIN_SETCMD_BASE10,
    "/PADKEYS", "Pad buttons -> keyboard keys with a layout per game, from PADMAP.TXT / PADUSER.TXT in SBEMU's folder (1=on, 0=off: only /JOYKEY; needs /JOY:1)", 1, MAIN_SETCMD_BASE10,
    "/PADLOG", "Show which pad layout the loaded SBEMU picked for the last programs started (SBEMU /PADLOG)", 0, MAIN_SETCMD_BASE10,
    NULL, NULL, 0,
};
enum EOption
{
    OPT_Help,
    OPT_DEBUG_OUTPUT,

    OPT_ADDR,
    OPT_IRQ,
    OPT_DMA,
    OPT_TYPE,
    OPT_HDMA,
    OPT_OPL,
    OPT_PM,
    OPT_RM,
    OPT_FMCORE,
    OPT_OUTPUT,
    OPT_VOL,
    OPT_RATE,
    OPT_FIX_TC,
    OPT_SCLIST,
    OPT_SCFM,
    OPT_SCMPU,
    OPT_SC,
    OPT_RESET,
    OPT_MPUADDR,
    OPT_MPUCOMPORT,
    OPT_COMPORTLIST,
#if MPU_DEBUG
    OPT_MDBG,
#endif
#if SBEMU_VMPU
    OPT_VMPU_VOICES,
    OPT_VMPU_SF,
    OPT_VMPU_SFBUF,
#if SBEMU_VSC55
    OPT_VSC_SET,
    OPT_VSC_GAIN,
    OPT_VSC_VOICES,
    OPT_VSC_FX,
    OPT_VSC_AUTO,
    OPT_VSC_AHEAD,
    OPT_VTIMER,
    OPT_VSF,
    OPT_VSFREV,
    OPT_VSFROOM,
    OPT_VSFDAMP,
    OPT_VSFCHO,
    OPT_VSFCRATE,
    OPT_VSFCDEPTH,
    OPT_VSFSEND,
    OPT_VSFVOICES,
    OPT_VMT32,
#endif
#endif
    OPT_VCDA,
    OPT_VSBVOL,
    OPT_VOPLVOL,
    OPT_VMIDIVOL,
    OPT_JOY,
    OPT_JOYAX,
    OPT_JOYBT,
    OPT_JOYKEY,
    OPT_JOYDZ,
    OPT_JOYMODE,
    OPT_USBKB,
    OPT_USBMS,
    OPT_USBDISK,
    OPT_JOYBIOS,
    OPT_PADKEYS,
    OPT_PADLOG,
    OPT_COUNT,
};

//T1~T6 maps
static const char* MAIN_SBTypeString[] =
{
    "0",
    "1.0/1.5",
    "Pro (old)",
    "2.0",
    "Pro",
    "Pro",
    "16",
};
static int MAIN_SB_DSPVersion[] =
{
    0,
    0x0100,
    0x0300,
    0x0202,
    0x0302,
    0x0302,
    0x0400,
};

static void MAIN_InvokeIRQ(uint8_t irq) //generate virtual IRQ
{
    #if MAIN_TRAP_RMPIC_ONDEMAND
    if(MAIN_Options[OPT_RM].value) QEMM_Install_IOPortTrap(MAIN_VIRQ_IODT, countof(MAIN_VIRQ_IODT), &MAIN_VIRQ_IOPT);
    #endif
    #if MAIN_TRAP_PMPIC_ONDEMAND
    if(MAIN_Options[OPT_PM].value)
    {
        HDPMIPT_Install_IOPortTrap(0x20, 0x21, MAIN_VIRQ_IODT, 2, &MAIN_VIRQ_IOPT_PM1);
        HDPMIPT_Install_IOPortTrap(0xA0, 0xA1, MAIN_VIRQ_IODT+2, 2, &MAIN_VIRQ_IOPT_PM2);
    }
    #endif

    HDPMIPT_DisableIRQRouting(irq); //disable routing
    IRQGUARD_Enable();
    DIAG_VirqBegin();
    VIRQ_Invoke(irq, &MAIN_IntContext.regs, MAIN_IntContext.EFLAGS&CPU_VMFLAG);
    DIAG_VirqEnd();
    IRQGUARD_Disable();
    HDPMIPT_EnableIRQRouting(irq); //restore routing

    #if MAIN_TRAP_RMPIC_ONDEMAND
    if(MAIN_Options[OPT_RM].value) QEMM_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT);
    #endif
    #if MAIN_TRAP_PMPIC_ONDEMAND
    if(MAIN_Options[OPT_PM].value)
    {
        HDPMIPT_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT_PM1);
        HDPMIPT_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT_PM2);
    }
    #endif
}

/* ---- game timer tick replay ----
 * While SBEMU's sound interrupt runs, the game's timer interrupt (IRQ 0) cannot run (interrupts do not
 * nest under HDPMI). The PIC remembers only one pending tick, so when the interrupt lasts longer than one
 * timer period the extra ticks are lost - and games that time MIDI music with a fast timer (Doom ~1 kHz,
 * Duke 3D ~220 Hz) play their music slower. SBEMU measures how long each of its interrupts took, works
 * out how many ticks went missing (the game's timer rate is read from the PIT) and calls the game's
 * timer handler that many times before it returns, the same way it delivers the virtual SB interrupt. */
static uint64_t MAIN_TCps;              /* CPU cycles per second (integer, no FPU in interrupts) */
static uint32_t MAIN_TDiv, MAIN_TDivWin, MAIN_TSamples;
static uint64_t MAIN_TAccMilli;         /* missing ticks x1000 not yet given back */
static uint32_t MAIN_TReplayed;
static int MAIN_TInReplay;
static uint32_t MAIN_TIsrN;
static int MAIN_TDivOK;                 /* the rate comes from a full 32-sample window (8 samples underestimate it) */
static int MAIN_TSuspend;               /* the PIT looked slower than the learned rate: no ticks until re-learned */

static void MAIN_TimerSample(void)
{
    uint8_t lo, hi; uint32_t v;
    UntrappedIO_OUT(0x43, 0x00);        /* latch counter 0 */
    lo = UntrappedIO_IN(0x40); hi = UntrappedIO_IN(0x40);
    v = lo | ((uint32_t)hi << 8); if(!v) v = 65536;
    if(v > MAIN_TDivWin) MAIN_TDivWin = v;
    if((++MAIN_TSamples & 31) == 0) { MAIN_TDiv = MAIN_TDivWin; MAIN_TDivWin = 0; MAIN_TDivOK = 1; MAIN_TSuspend = 0; }
    else if(!MAIN_TDiv && MAIN_TSamples >= 8) MAIN_TDiv = MAIN_TDivWin;
}

static void MAIN_InvokeTimer(void)
{
    #if MAIN_TRAP_RMPIC_ONDEMAND
    if(MAIN_Options[OPT_RM].value) QEMM_Install_IOPortTrap(MAIN_VIRQ_IODT, countof(MAIN_VIRQ_IODT), &MAIN_VIRQ_IOPT);
    #endif
    #if MAIN_TRAP_PMPIC_ONDEMAND
    if(MAIN_Options[OPT_PM].value)
    {
        HDPMIPT_Install_IOPortTrap(0x20, 0x21, MAIN_VIRQ_IODT, 2, &MAIN_VIRQ_IOPT_PM1);
        HDPMIPT_Install_IOPortTrap(0xA0, 0xA1, MAIN_VIRQ_IODT+2, 2, &MAIN_VIRQ_IOPT_PM2);
    }
    #endif
    VIRQ_Invoke(0, &MAIN_IntContext.regs, MAIN_IntContext.EFLAGS&CPU_VMFLAG);
    #if MAIN_TRAP_RMPIC_ONDEMAND
    if(MAIN_Options[OPT_RM].value) QEMM_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT);
    #endif
    #if MAIN_TRAP_PMPIC_ONDEMAND
    if(MAIN_Options[OPT_PM].value)
    {
        HDPMIPT_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT_PM1);
        HDPMIPT_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT_PM2);
    }
    #endif
}

/* Ground truth for lost ticks: games pass every ~65536 PIT counts of their fast timer on to the BIOS
 * timer handler, which advances the DOS clock (0040:006C). If the game misses ticks, the DOS clock falls
 * behind real time (CPU time-stamp counter) by the same fraction. SBEMU compares the two a few times per
 * second while the game has the timer sped up and gives the game the missing ticks. */
static uint64_t MT_LastTsc, MT_ExpTot, MT_GotTot;
static uint32_t MT_LastBios;
static int64_t  MT_DefMilli;            /* DOS clock behind real time, BIOS ticks x1000 */

static void MAIN_TimerControl(uint64_t now)
{
    uint32_t bios = _farpeekl(_dos_ds, 0x46C);
    uint64_t dt, expm, gotm, db;
    if(!MT_LastTsc) { MT_LastTsc = now; MT_LastBios = bios; return; }
    dt = now - MT_LastTsc;
    db = bios >= MT_LastBios ? bios - MT_LastBios : bios + 0x1800B0u - MT_LastBios; /* midnight */
    MT_LastTsc = now; MT_LastBios = bios;
    if(dt > MAIN_TCps * 5u) { MT_DefMilli = 0; return; }             /* something stalled: start over */
    expm = dt * 1193182u / MAIN_TCps * 1000u / 65536u;                 /* BIOS ticks that should have passed x1000 */
    gotm = db * 1000u;
    MT_ExpTot += expm; MT_GotTot += gotm;
    DIAG_BiosClock(MT_ExpTot, MT_GotTot);
    if(gotm * 4 < expm) { MT_DefMilli = 0; return; }   /* the game does not feed the BIOS clock: cannot judge */
    MT_DefMilli += (int64_t)expm - (int64_t)gotm;
    if(MT_DefMilli < -3000) MT_DefMilli = -3000;
    if(MT_DefMilli > 1000)                              /* more than one DOS tick behind: give ticks back */
    {
        uint64_t bt = (uint64_t)MT_DefMilli / 1000u;               /* whole BIOS ticks to make up */
        uint64_t gt = bt * 65536u / MAIN_TDiv;                     /* = game timer ticks */
        MAIN_TAccMilli += gt * 1000u;
        if(MAIN_TAccMilli > 64000u) MAIN_TAccMilli = 64000u;     /* never a big backlog (beta.26 allowed 4000) */
        MT_DefMilli -= (int64_t)(bt * 1000u);
    }
}

static void MAIN_TimerReplay(uint64_t len)
{
    uint32_t n, i;
    uint64_t now;
    (void)len;
    if(!MAIN_Options[OPT_VTIMER].value || !MAIN_TCps || MAIN_TInReplay)
        return;
    if((MAIN_TIsrN & 7) == 0) MAIN_TimerSample(); /* read the game's timer rate now and then */
    if(!MAIN_TDiv || MAIN_TDiv >= 65000) { MAIN_TAccMilli = 0; MT_LastTsc = 0; MT_DefMilli = 0; ++MAIN_TIsrN; return; } /* plain 18.2 Hz */
    if(!MAIN_TDivOK || MAIN_TSuspend)                   /* rate not known well enough: give nothing back */
    { MAIN_TAccMilli = 0; MT_LastTsc = 0; MT_DefMilli = 0; ++MAIN_TIsrN; return; }
    now = DIAG_Tsc();
    if((MAIN_TIsrN++ & 63) == 0) MAIN_TimerControl(now);   /* ~5 times per second */
    n = (uint32_t)(MAIN_TAccMilli / 1000);
    if(!n) return;
    /* Is the game's fast timer still running? MAIN_TDiv is only re-learned every ~0.75 s, so check the PIT
     * right now: its counter can never be above the reload value, so a count above the rate we learned
     * means the game has slowed the timer down - it is quitting. Ticks given back now would land in a
     * game that is shutting down, or (once it has restored the vector) in the BIOS timer handler, whose
     * own EOI to the PIC would end our sound interrupt early: that locked up the PC when Duke 3D quit. */
    {
        uint8_t lo, hi; uint32_t v;
        UntrappedIO_OUT(0x43, 0x00);
        lo = UntrappedIO_IN(0x40); hi = UntrappedIO_IN(0x40);
        v = lo | ((uint32_t)hi << 8); if(!v) v = 65536;
        /* Only a count no fast game timer can reach means the game has put the timer back to 18.2 Hz.
         * beta.27/28 compared against 1.5x the learned rate instead: Doom's sound library changes the timer
         * rate all the time, so that check fired constantly and blocked every give-back (Doom 5.8% slow). */
        if(v > 40000)
        {
            /* pause until the next full window says what the rate is now. beta.27 threw the learned rate away
             * here and re-learned it from 8 samples, which underestimates it: that gave games far too many
             * ticks (Duke 3D skipping, clock over 100%) and the extra work garbled the sound */
            MAIN_TAccMilli = 0; MT_DefMilli = 0; MT_LastTsc = 0;
            MAIN_TSuspend = 1;
            return;
        }
    }
    /* never re-enter the game's timer handler: if it is in service (we interrupted it), give the ticks later */
    UntrappedIO_OUT(0x20, 0x0B);
    if(UntrappedIO_IN(0x20) & 0x01) { UntrappedIO_OUT(0x20, 0x0A); return; }
    UntrappedIO_OUT(0x20, 0x0A);
    /* hand ticks back evenly: one per sound interrupt (~340 a second, far more than games lose), two while a
     * backlog is being worked off. Up to beta.32 this gave up to 8 at once - a game's music sequencer then
     * jumped 8 steps in one go, which sounded choppy. */
    if(n > 2) n = 2;
    if(n > 1 && MAIN_TAccMilli < 32000u) n = 1;
    MAIN_TInReplay = 1;
    for(i = 0; i < n; ++i)
        MAIN_InvokeTimer();
    MAIN_TInReplay = 0;
    MAIN_TAccMilli -= (uint64_t)n * 1000u;
    MAIN_TReplayed += n;
    DIAG_TimerReplayed(n);
}

static void MAIN_CPrintf(int color, const char* fmt, ...);
static void MAIN_SetBlasterEnv(struct MAIN_OPT* opt) //alter BLASTER env.
{
    char buf[256];
    if(opt[OPT_TYPE].value != 6) //T5 not working for some games, should use T4 for SBPro (t=4/5)
        sprintf(buf, "A%x I%x D%x P%x T%x", opt[OPT_ADDR].value, opt[OPT_IRQ].value, opt[OPT_DMA].value, opt[OPT_MPUADDR].value, opt[OPT_TYPE].value == 5 ? 4 : opt[OPT_TYPE].value);
    else
        sprintf(buf, "A%x I%x D%x T%x H%x P%x", opt[OPT_ADDR].value, opt[OPT_IRQ].value, opt[OPT_DMA].value, opt[OPT_TYPE].value, opt[OPT_HDMA].value, opt[OPT_MPUADDR].value);
    #ifdef DJGPP //makes vscode happy
    if(setenv("BLASTER", buf, TRUE) != 0)
    {   /* utility.c's setenv writes COMMAND.COM's environment; it fails when that is full */
        MAIN_CPrintf(YELLOW, "Warning: ");
        printf("BLASTER could not be set to %s (DOS environment full: SHELL=...COMMAND.COM /E:1024 /P in CONFIG.SYS).\n"
               "Games will read the old BLASTER: %s\n", buf, getenv("BLASTER") ? getenv("BLASTER") : "(none)");
    }
    #endif
}

static int MAIN_LogOn = 0; //set when the SBEMULOG environment variable names a log file: all text goes there
static void MAIN_LogCommit(void) { if(MAIN_LogOn) { fflush(stdout); fsync(fileno(stdout)); } } //DOS only updates the file size on close/commit; the resident program never closes it
static void MAIN_PutStr(const char* t) { if(MAIN_LogOn) { for(; *t; ++t) if(*t != '\r') fputc(*t, stdout); MAIN_LogCommit(); } else cputs(t); }

/* ---- detail log (SBDETAIL.TXT) ------------------------------------------------------------------------
 * SC55.BAT sets SBEMUDETAIL=<file>. SBEMU then writes everything it found and decided while starting:
 * the command line, every option and where it came from, DOS / Windows / memory managers, the CPU, all
 * PCI sound devices with their IRQs, the card it chose, each start-up step and every port trap. Each line
 * is committed to disk at once, so after a hang or a crash the last "[step]" line shows where it stopped.
 * Only written while starting (never from an interrupt); closed before SBEMU goes resident.
 * beta.73: a disk commit costs about 25 ms, and the option list alone is 70 lines (1.9 s of start-up).
 * Plain lines are now only handed to DOS; the commit happens at every "[step]" line (before the step's
 * risky work runs) and for every USB trace line, which is where hangs were seen. */
static FILE* MAIN_Det;
static const char* MAIN_DetStage = "start";
static void MAIN_D(const char* fmt, ...)
{
    va_list ap;
    if(!MAIN_Det) return;
    va_start(ap, fmt); vfprintf(MAIN_Det, fmt, ap); va_end(ap);
    fflush(MAIN_Det);
}
static void MAIN_DCommit(void) { if(MAIN_Det) { fflush(MAIN_Det); fsync(fileno(MAIN_Det)); } }
static void MAIN_UsbTrace(const char* s) { MAIN_D("%s", s); MAIN_DCommit(); }
/* beta.73: each step's start time (TSC), so the detail log shows where start-up time goes */
static const char* MAIN_StepName[40];
static uint64_t MAIN_StepTsc[40];
static int MAIN_StepCount;
static void MAIN_Step(const char* s)
{
    MAIN_DetStage = s;
    if(MAIN_StepCount < 40) { MAIN_StepName[MAIN_StepCount] = s; MAIN_StepTsc[MAIN_StepCount++] = DIAG_Tsc(); }
    MAIN_D("[step] %s\n", s);
    MAIN_DCommit();
}
static void MAIN_DetClose(const char* why)
{
    if(!MAIN_Det) return;
    if(MAIN_StepCount > 1 && MAIN_TCps > 1000)
    {
        uint64_t end = DIAG_Tsc();
        MAIN_D("Start-up time per step:\n");
        for(int i = 0; i < MAIN_StepCount; ++i)
        {
            uint64_t d = (i + 1 < MAIN_StepCount ? MAIN_StepTsc[i+1] : end) - MAIN_StepTsc[i];
            MAIN_D("  %6lu ms  %s\n", (unsigned long)(d / (MAIN_TCps / 1000)), MAIN_StepName[i]);
        }
        MAIN_D("  %6lu ms  total\n", (unsigned long)((end - MAIN_StepTsc[0]) / (MAIN_TCps / 1000)));
    }
    MAIN_D("---- %s (last step: %s)\n", why, MAIN_DetStage);
    fclose(MAIN_Det); MAIN_Det = NULL;
}
static void MAIN_DetAtExit(void) { MAIN_DetClose("SBEMU quit without going resident"); }
static uint32_t MAIN_PCIRead(int bus, int dev, int fn, int reg)
{
    outportl(0xCF8, 0x80000000UL | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)fn << 8) | (reg & 0xFC));
    return inportl(0xCFC);
}
static void MAIN_DetSystem(int argc, char* argv[])
{
    union REGS r; int i;
    time_t t = time(NULL);
    MAIN_D("==== SBEMU %s detail log, %s", MAIN_SBEMU_VER, ctime(&t));
    MAIN_D("Command line:");
    for(i = 1; i < argc; ++i) MAIN_D(" %s", argv[i]);
    MAIN_D("\n");
    { const char* e[] = {"BLASTER", "SBOPTS", "SBEMULOG", "COMSPEC", "PATH", "DOS16M", "DOS4G", "HDPMI"};
      for(i = 0; i < (int)(sizeof(e)/sizeof(e[0])); ++i) { const char* v = getenv(e[i]); MAIN_D("  %%%s%% = %s\n", e[i], v ? v : "(not set)"); } }
    r.x.ax = 0x3000; int86(0x21, &r, &r);
    MAIN_D("DOS version %d.%02d", r.h.al, r.h.ah);
    r.x.ax = 0x3306; r.x.bx = 0; int86(0x21, &r, &r);
    if(r.h.al != 0xFF) MAIN_D(" (true %d.%02d)", r.h.bl, r.h.bh);
    r.x.ax = 0x1600; int86(0x2F, &r, &r);
    if(r.h.al != 0 && r.h.al != 0x80) MAIN_D(", running inside Windows %d.%02d", r.h.al, r.h.ah); else MAIN_D(", no Windows");
    MAIN_D("\n");
    { unsigned int hi, lo; uint32_t m = 0, f = 0;
      __dpmi_free_mem_info fi;
      if(__dpmi_get_free_memory_information(&fi) == 0) f = fi.largest_available_free_block_in_bytes;
      MAIN_D("DPMI largest free block: %lu KB\n", (unsigned long)(f >> 10)); (void)hi; (void)lo; (void)m; }
    {   /* CPU name (CPUID 0x80000002-4), or the vendor string */
        uint32_t a, b, c, d; char name[49]; int ok = 0;
        asm volatile("pushfl; popl %%eax; movl %%eax, %%ecx; xorl $0x200000, %%eax; pushl %%eax; popfl; pushfl; popl %%eax; xorl %%ecx, %%eax" : "=a"(a) : : "ecx");
        if(a & 0x200000)
        {
            asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000));
            if(a >= 0x80000004)
            {
                uint32_t* n = (uint32_t*)name;
                for(i = 0; i < 3; ++i) asm volatile("cpuid" : "=a"(n[i*4]), "=b"(n[i*4+1]), "=c"(n[i*4+2]), "=d"(n[i*4+3]) : "a"(0x80000002 + i));
                name[48] = 0; ok = 1;
            }
            else
            {
                asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
                memcpy(name, &b, 4); memcpy(name + 4, &d, 4); memcpy(name + 8, &c, 4); name[12] = 0; ok = 1;
            }
        }
        MAIN_D("CPU: %s\n", ok ? name : "(no CPUID)");
    }
    {   /* every PCI multimedia device (class 04): the sound chip(s) SBEMU can choose from */
        int bus, dev, fn, n = 0, any = 0;
        for(bus = 0; bus < 8; ++bus)
        for(dev = 0; dev < 32; ++dev)
        for(fn = 0; fn < 8; ++fn)
        {
            uint32_t id = MAIN_PCIRead(bus, dev, fn, 0);
            if(id == 0xFFFFFFFFUL || id == 0) { if(fn == 0) break; continue; }
            ++any;
            uint32_t cls = MAIN_PCIRead(bus, dev, fn, 8);
            uint32_t hdr = MAIN_PCIRead(bus, dev, fn, 0x0C);
            if((cls >> 24) == 0x04 || (cls >> 16) == 0x0C03)   /* sound chips, and USB controllers */
            {
                uint32_t irq = MAIN_PCIRead(bus, dev, fn, 0x3C), cmd = MAIN_PCIRead(bus, dev, fn, 4);
                MAIN_D("PCI %02x:%02x.%d  %04lx:%04lx  class %02lx.%02lx  IRQ line %lu pin %c  cmd %04lx  BAR0 %08lx BAR1 %08lx\n",
                    bus, dev, fn, id & 0xFFFF, id >> 16, cls >> 24, (cls >> 16) & 0xFF,
                    irq & 0xFF, ((irq >> 8) & 0xFF) ? 'A' - 1 + (char)((irq >> 8) & 0xFF) : '-', cmd & 0xFFFF,
                    MAIN_PCIRead(bus, dev, fn, 0x10), MAIN_PCIRead(bus, dev, fn, 0x14));
                ++n;
            }
            if(fn == 0 && !(hdr & 0x00800000UL)) break;   /* not multi-function */
        }
        MAIN_D("PCI: %d devices, %d sound/USB device(s)\n", any, n);
    }
    {   /* which IRQs the PICs have unmasked now */
        unsigned m = inportb(0x21) | (inportb(0xA1) << 8);
        MAIN_D("PIC mask %04x (IRQs in use:", m);
        for(i = 0; i < 16; ++i) if(!(m & (1 << i))) MAIN_D(" %d", i);
        MAIN_D(")\n");
    }
}
static void MAIN_DetOptions(const char* title)
{
    int i;
    MAIN_D("%s (* = given on the command line or in SBOPTS):\n", title);
    for(i = 0; i < OPT_COUNT; ++i)
    {
        const struct MAIN_OPT* o = &MAIN_Options[i];
        if(o->setcmd & MAIN_SETCMD_STR) MAIN_D("  %c%-12s %s\n", (o->setcmd & MAIN_SETCMD_SET) ? '*' : ' ', o->option, o->value ? (const char*)(uintptr_t)o->value : "(none)");
        else MAIN_D("  %c%-12s %x (%d)\n", (o->setcmd & MAIN_SETCMD_SET) ? '*' : ' ', o->option, o->value, o->value);
    }
}
static void MAIN_CPrintf(int color, const char* fmt, ...)
{
    /* cprintf-style output does not turn '\n' into CR+LF. Until beta.72 only a '\n' at the very end got its
     * CR, so a message with a line break in the middle (the USB pad hint) started its second line where the
     * first one ended; and a message under 2 characters read buf[-1]. Every bare LF now becomes CR+LF. */
    char buf[2048]; //limit for cprintf (DJGPP): 2048
    char out[2048 + 64];
    va_list aptr;
    va_start(aptr, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, aptr);
    va_end(aptr);
    if(n < 0) return;
    if(n >= (int)sizeof(buf)) n = sizeof(buf) - 1;
    int o = 0, tail = 0; /* tail: the message ends in a line break, printed after the colour is reset */
    while(n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) { tail |= buf[n-1] == '\n'; --n; }
    for(int i = 0; i < n && o < (int)sizeof(out) - 3; ++i)
    {
        if(buf[i] == '\n' && (i == 0 || buf[i-1] != '\r') && (i + 1 >= n || buf[i+1] != '\r')) out[o++] = '\r';
        out[o++] = buf[i];
    }
    out[o] = '\0';
    textcolor(color);
    MAIN_PutStr(out); //more safe with a fmt string, incase buf contains any fmt.
    textcolor(LIGHTGRAY);
    if(tail) MAIN_PutStr("\r\n");
}

static void
MAIN_Print_Enabled_Newline(bool enabled)
{
    MAIN_CPrintf(enabled ? LIGHTGREEN : LIGHTRED, "%s", enabled ? "enabled" : "disabled");
    MAIN_PutStr(".\r\n");
}

static int
update_serial_debug_output()
{
    bool enabled = (MAIN_Options[OPT_DEBUG_OUTPUT].value != 0);
    if (!enabled) {
        _LOG("Serial port debugging disabled.\n");
    }
    #if MPU_DEBUG
    int err = ser_setup(MAIN_Options[OPT_MDBG].value ? SBEMU_SERIAL_TYPE_FASTDBG : SBEMU_SERIAL_TYPE_DBG, MAIN_Options[OPT_DEBUG_OUTPUT].value);
    #else
    int err = ser_setup(SBEMU_SERIAL_TYPE_DBG, MAIN_Options[OPT_DEBUG_OUTPUT].value);
    #endif
    if (enabled) {
        _LOG("Serial port debugging enabled.\n");
    }
    return err;
}

static int
update_serial_mpu_output()
{
    bool enabled = (MAIN_Options[OPT_MPUCOMPORT].value != 0);
    if (!enabled) {
        _LOG("MPU-401 serial output disabled.\n");
    }
    int err = ser_setup(SBEMU_SERIAL_TYPE_MIDI, MAIN_Options[OPT_MPUCOMPORT].value);
    if (enabled) {
        _LOG("MPU-401 serial output enabled.\n");
    }
    return err;
}

static BOOL OPLRMInstalled, OPLPMInstalled, MPURMInstalled, MPUPMInstalled;
/* USB Xbox 360 pad -> gameport 201h (/JOY) */
static QEMM_IODT MAIN_JOY_IODT[2] = { { 0x201, (void*)&VJOY_IOHandler }, { 0x206, (void*)&VJOY_InfoHandler } };   /* 206h: real mode only (SBEMU16) */
static QEMM_IOPT MAIN_JOY_IOPT, MAIN_JOY_IOPT_PM;
static BOOL MAIN_JOY_RM, MAIN_JOY_PM, MAIN_JOY_On;
/* USB mouse -> PS/2 mouse commands answered by vkbc.c (/USBMS) */
static QEMM_IODT MAIN_KBC_IODT[2] = { { 0x60, (void*)&VKBC_IOHandler }, { 0x64, (void*)&VKBC_IOHandler } };
static QEMM_IOPT MAIN_KBC_IOPT, MAIN_KBC_IOPT_PM60, MAIN_KBC_IOPT_PM64;
static BOOL MAIN_KBC_RM, MAIN_KBC_PM60, MAIN_KBC_PM64;
static HDPMIPT_IRQRoutedHandle OldRoutedHandle = HDPMIPT_IRQRoutedHandle_Default;
static HDPMIPT_IRQRoutedHandle OldRoutedHandle5 = HDPMIPT_IRQRoutedHandle_Default;
static HDPMIPT_IRQRoutedHandle OldRoutedHandle7 = HDPMIPT_IRQRoutedHandle_Default;
static HDPMIPT_IRQRoutedHandle OldRoutedHandle9 = HDPMIPT_IRQRoutedHandle_Default;
static void MAIN_Cleanup()
{
    if(MAIN_JOY_RM) { QEMM_Uninstall_IOPortTrap(&MAIN_JOY_IOPT); MAIN_JOY_RM = FALSE; }
    if(MAIN_JOY_PM) { HDPMIPT_Uninstall_IOPortTrap(&MAIN_JOY_IOPT_PM); MAIN_JOY_PM = FALSE; }
    if(MAIN_KBC_RM) { QEMM_Uninstall_IOPortTrap(&MAIN_KBC_IOPT); MAIN_KBC_RM = FALSE; }
    if(MAIN_KBC_PM60) { HDPMIPT_Uninstall_IOPortTrap(&MAIN_KBC_IOPT_PM60); MAIN_KBC_PM60 = FALSE; }
    if(MAIN_KBC_PM64) { HDPMIPT_Uninstall_IOPortTrap(&MAIN_KBC_IOPT_PM64); MAIN_KBC_PM64 = FALSE; }
    USBDISK_Uninstall();
    if(MAIN_JOY_On) { MAIN_JOY_On = FALSE; USBPAD_Shutdown(); }
    if(OldRoutedHandle.valid)
        HDPMIPT_InstallIRQRoutedHandlerH(aui.card_irq, &OldRoutedHandle);
    //must be after OldRoutedHandle in case aui.card_irq is 5/7/9
    //because OldRoutedHandle is inited after those three
    //need to restore in reversed order.
    if(OldRoutedHandle5.valid)
        HDPMIPT_InstallIRQRoutedHandlerH(5, &OldRoutedHandle5);
    if(OldRoutedHandle7.valid)
        HDPMIPT_InstallIRQRoutedHandlerH(7, &OldRoutedHandle7);
    if(OldRoutedHandle9.valid)
        HDPMIPT_InstallIRQRoutedHandlerH(9, &OldRoutedHandle9);

    if(MAIN_Int28On) { DPMI_UninstallISR(&MAIN_Int28Handle); MAIN_Int28On = FALSE; }
    CDLINK_Shutdown();
    AU_stop(&aui);
    AU_close(&aui, &fm_aui, &mpu401_aui);
    if(OPLRMInstalled)
        QEMM_Uninstall_IOPortTrap(&OPL3IOPT);
    if(OPLPMInstalled)
        HDPMIPT_Uninstall_IOPortTrap(&OPL3IOPT_PM);
    if(MPURMInstalled)
        QEMM_Uninstall_IOPortTrap(&MPUIOPT);
    if(MPUPMInstalled)
        HDPMIPT_Uninstall_IOPortTrap(&MPUIOPT_PM);

#if SBEMU_PCSPEAKER
    // Uninstall PC Speaker IO traps (mirror the install order in main()).
    if(MAIN_PCSPEAKER_Installed)
    {
        if(MAIN_QEMM_Present)
            QEMM_Uninstall_IOPortTrap(&MAIN_PCSPEAKER_IOPT);
        if(MAIN_HDPMI_Present)
        {
            HDPMIPT_Uninstall_IOPortTrap(&MAIN_PCSPEAKER_IOPT);
            HDPMIPT_Uninstall_IOPortTrap(&MAIN_PCSPEAKER_IOPT_61);
        }
    }
#endif

    IRQGUARD_Uninstall();
}

#if SBEMU_VSC55
static void MAIN_PrintRomsetName(const char* name, void* user)
{
    int* column = (int*)user;
    int len = (int)strlen(name) + 2;
    if(*column + len > 78)
    {
        printf("\n");
        *column = 0;
    }
    if(*column == 0)
        printf("   ");
    printf("%s  ", name);
    *column += len;
}
#endif

int main(int argc, char* argv[])
{
    /* SBEMU /PADLOG only asks the loaded SBEMU: it must not overwrite that one's start-up log and SBDETAIL.TXT */
    int padlog_only = 0;
    for(int i = 1; i < argc; ++i) if(strnicmp(argv[i], "/PADLOG", 7) == 0) padlog_only = 1;
    if(!padlog_only)
    {   //SC55.BAT sets SBEMULOG=<file>: everything SBEMU prints is written there (and shown afterwards by the batch file)
        const char* logname = getenv("SBEMULOG");
        FILE* probe = (logname && *logname) ? fopen(logname, "w") : NULL;
        if(probe) fclose(probe);
        if(probe && freopen(logname, "w", stdout))
        {
            setvbuf(stdout, NULL, _IONBF, 0); //unbuffered: nothing is lost when SBEMU goes resident
            MAIN_LogOn = 1;
        }
    }
    if(!padlog_only)
    {
        /* SBEMUDETAIL names the file; otherwise SBDETAIL.TXT next to the SBEMULOG file (SC55.BAT sets that) */
        static char dbuf[260];
        const char* dn = getenv("SBEMUDETAIL");
        const char* ln = getenv("SBEMULOG");
        if((!dn || !*dn) && ln && *ln && strlen(ln) < sizeof(dbuf) - 16)
        {
            char* e;
            strcpy(dbuf, ln);
            e = strrchr(dbuf, '\\'); if(!e) e = strrchr(dbuf, '/'); if(!e) e = strrchr(dbuf, ':');
            strcpy(e ? e + 1 : dbuf, "SBDETAIL.TXT");
            dn = dbuf;
        }
        if(dn && *dn && stricmp(dn, "OFF") && (MAIN_Det = fopen(dn, "w")) != NULL)
        {
            atexit(&MAIN_DetAtExit);
            MAIN_DetSystem(argc, argv);
            MAIN_Step("reading options");
        }
    }
    if(!padlog_only)     /* SBEMU /PADLOG prints only the answer (it is often redirected to a file) */
    {
    MAIN_CPrintf(CYAN, "\r\n%s ", PROGNAME);
    MAIN_CPrintf(LIGHTCYAN, "%s ", MAIN_SBEMU_VER);
    MAIN_CPrintf(LIGHTGRAY,"(");
    MAIN_CPrintf(WHITE, "https://github.com/crazii/SBEMU");
    MAIN_CPrintf(LIGHTGRAY, ")\r\n");
    MAIN_CPrintf(WHITE, "Sound Blaster emulation on PCI sound cards for DOS.\r\n");
    MAIN_CPrintf(LIGHTGRAY, "Based on MPXPlay (drivers) and DOSBox (OPL-3 emulation).\r\n");
    printf("\n");
    }

    if((argc == 2 && stricmp(argv[1],"/?") == 0))
    {
        printf("Usage:\n");

        int i = 0;
        while(MAIN_Options[i].option)
        {
            if(MAIN_Options[i].setcmd&MAIN_SETCMD_INTRL) {++i; continue;}

            printf(" %-7s: %s", MAIN_Options[i].option, MAIN_Options[i].desc);
            if (i != 0) {
                if(MAIN_Options[i].setcmd&MAIN_SETCMD_STR)
                    printf(", default: %s.\n", (const char*)MAIN_Options[i].value);
                else if(MAIN_Options[i].setcmd&MAIN_SETCMD_BASE10)
                    printf(", default: %d.\n", MAIN_Options[i].value);
                else
                    printf(", default: %x.\n", MAIN_Options[i].value);
            } else {
                printf(".\n");
            }
            ++i;
        }

        printf("\n");
        printf("  [*] Values will default to the BLASTER variable if not specified.\n");
        #if SBEMU_VSC55
        {
            int column = 0;
            printf("\nSC-55 ROM sets for /VSCSET (default: auto = first complete set found):\n");
            SC55_ListModels(&MAIN_PrintRomsetName, &column);
            printf("\n\nExample: SBEMU /VMPU /VSCROM:C:\\SC55\\MK1\\1.21 /VSCSET:mk1-v1.21\n");
        }
        #endif

        return 0;
    }

    //parse BLASTER env first.
    {
        char* blaster = getenv("BLASTER");
        if(blaster != NULL)
        {
            char c;
            while((c=toupper(*(blaster++))))
            {
                if(c == 'I')
                    MAIN_Options[OPT_IRQ].value = *(blaster++) - '0';
                else if(c == 'D')
                    MAIN_Options[OPT_DMA].value = *(blaster++) - '0';
                else if(c == 'A')
                    MAIN_Options[OPT_ADDR].value = strtol(blaster, &blaster, 16);
                else if(c == 'P')
                    MAIN_Options[OPT_MPUADDR].value = strtol(blaster, &blaster, 16);
                else if(c =='T')
                    MAIN_Options[OPT_TYPE].value = *(blaster++) - '0';
                else if(c =='H')
                    MAIN_Options[OPT_HDMA].value = *(blaster++) - '0';
            }
        }
    }

    for(int i = 1; i < argc; ++i)
    {
        /* options are matched by their start (/A220 has no separator), so take the LONGEST name that
         * matches: otherwise /VSF (SoundFont file) would also catch /VSFCDEPTH:100, /VSFVOICES:32 ... */
        int best = -1, bestlen = 0;
        /* @SBOPTS.BAT as a response file: "SET SBOPTS=/A:220" -> "/A:220" ("@ECHO", "OFF", "SET" match nothing) */
        if(memicmp(argv[i], "SBOPTS=", 7) == 0) argv[i] += 7;
        for(int j = 0; j < OPT_COUNT; ++j)
        {
            int l = strlen(MAIN_Options[j].option);
            if(l > bestlen && memicmp(argv[i], MAIN_Options[j].option, l) == 0) { best = j; bestlen = l; }
        }
        for(int j = (best < 0 ? OPT_COUNT : best); j < OPT_COUNT; ++j)
        {
            int len = bestlen;
            {
                if(argv[i][len] == '=' || argv[i][len] == ':') ++len; //optional separator
                int arglen = strlen(argv[i]);
                if(MAIN_Options[j].setcmd&MAIN_SETCMD_STR)
                    MAIN_Options[j].value = arglen == len ? MAIN_Options[j].value : (uintptr_t)&argv[i][len]; //default set to NULL.
                else
                {
                    int base = (MAIN_Options[j].setcmd&MAIN_SETCMD_BASE10) ? 10 : 16;
                    MAIN_Options[j].value = arglen == len ? 1 : strtol(&argv[i][len], NULL, base);
                }
                MAIN_Options[j].setcmd |= MAIN_SETCMD_SET;
                break;
            }
        }
    }

    MAIN_DetOptions("Options after BLASTER and the command line");
    MAIN_Step("checking options");
#if MPU_DEBUG
    mpu_debug = MAIN_Options[OPT_MDBG].value;
#endif
    if (MAIN_Options[OPT_DEBUG_OUTPUT].value) {
        if (update_serial_debug_output()) {
            return 1;
        }
    }

    if (MAIN_Options[OPT_COMPORTLIST].value) {
      ser_print_com_ports();
      return 0;
    }

    if (MAIN_Options[OPT_MPUCOMPORT].value) {
        if (update_serial_mpu_output()) {
            return 1;
        }
    }

    if(MAIN_Options[OPT_ADDR].value != 0x220 && MAIN_Options[OPT_ADDR].value != 0x240)
    {
        MAIN_CPrintf(RED, "Error: Invalid IO port address: %x.\n", MAIN_Options[OPT_ADDR].value);
        return 1;
    }
    if(MAIN_Options[OPT_IRQ].value != 0x5 && MAIN_Options[OPT_IRQ].value != 0x7 && MAIN_Options[OPT_IRQ].value != 0x9)
    {
        MAIN_CPrintf(RED, "Error: invalid IRQ: %d.\n", MAIN_Options[OPT_IRQ].value);
        return 1;
    }
    if(MAIN_Options[OPT_DMA].value != 0x0 && MAIN_Options[OPT_DMA].value != 0x1 && MAIN_Options[OPT_DMA].value != 0x3)
    {
        MAIN_CPrintf(RED, "Error: invalid DMA channel.\n");
        return 1;
    }
    if(MAIN_Options[OPT_TYPE].value <= 0 || MAIN_Options[OPT_TYPE].value > 6)
    {
        MAIN_CPrintf(RED, "Error: invalid SB Type.\n");
        return 1;
    }
    if(MAIN_Options[OPT_OUTPUT].value != 0 && MAIN_Options[OPT_OUTPUT].value != 1)
    {
        MAIN_CPrintf(RED, "Error: Invalid Output.\n");
        return 1;
    }
    if(MAIN_Options[OPT_VOL].value < 0 || MAIN_Options[OPT_VOL].value > SBEMU_VOLUME_MAX)
    {
        MAIN_CPrintf(RED, "Error: Invalid Volume.\n");
        return 1;
    }
    if(MAIN_Options[OPT_FMCORE].value != 0 && MAIN_Options[OPT_FMCORE].value != 1)
    {
        MAIN_CPrintf(RED, "Error: Invalid FM core (0 or 1).\n");
        return 1;
    }
    if(MAIN_Options[OPT_RATE].value < 4000 || MAIN_Options[OPT_RATE].value > 192000)
    {
        MAIN_CPrintf(RED, "Error: Invalid Sample rate.\n");
        return 1;
    }
    if(MAIN_Options[OPT_HDMA].value < 5 && MAIN_Options[OPT_HDMA].value != MAIN_Options[OPT_DMA].value) //16 bit transfer through 8 bit dma
    {
        printf("Warning: HDMA using 8 bit channel: H=%d, "
        "using 5/6/7 is recommended.\n"
        "set both DMA & HDMA to %d to resolve conflicts\n", MAIN_Options[OPT_HDMA].value, MAIN_Options[OPT_DMA].value);
        MAIN_Options[OPT_HDMA].value = MAIN_Options[OPT_DMA].value; //only one low DMA channel allowed, use same channel for hdma & low dma.
    }

    /* Inside a Windows 98 DOS box (window or full screen) Windows owns the sound card: SBEMU would either fail with
     * "both modes disabled" or, with a DPMI host that lets it through, program the PCI card under the Windows driver
     * (hang). SC55W.EXE already gives DOS boxes the SB / OPL / MPU there. "Restart in MS-DOS mode" unloads Windows
     * (AL = 0 here), so SBEMU loads as usual after the switch. */
    {
        __dpmi_regs wr; memset(&wr, 0, sizeof wr);
        wr.x.ax = 0x1600; __dpmi_int(0x2F, &wr);
        if(wr.h.al != 0 && wr.h.al != 0x80 && !getenv("SBEMU_IN_WINDOWS"))
        {
            MAIN_D("running inside Windows %d.%02d: not loading\n", wr.h.al, wr.h.ah);
            MAIN_CPrintf(YELLOW, "Windows is running: SBEMU is not loaded in a DOS box.\n");
            printf("In Windows 98, NUKED SB-EMU for Windows (C:\\SC55\\WIN98\\SC55W.EXE, keyboard icon\n"
                   "next to the clock) gives DOS boxes the Sound Blaster, OPL3 and SC-55.\n"
                   "For SBEMU: Start > Shut Down > \"Restart in MS-DOS mode\", then run SC55.\n");
            return 1;
        }
    }
    MAIN_Step("DPMI init");
    DPMI_Init();
    MAIN_Step("looking for QEMM / QPIEMU and HDPMI");

    MAIN_QEMM_Present = TRUE;
    if(MAIN_Options[OPT_RM].value)
    {
        MAIN_Options[OPT_RM].value = TRUE; //set to known value for compare
        int qemm_version = QEMM_GetVersion();
        int qemm_major = qemm_version >> 8;
        int qemm_minor = qemm_version & 0xFF;

        _LOG("QEMM version: %x.%02x\n", qemm_major, qemm_minor);

        if (qemm_version < 0x0703) {
            if (qemm_major == 0) {
                if(!padlog_only) printf("QEMM or QPIEMU not installed, disabling real mode support.\n");
            } else {
                printf("QEMM or QPIEMU version below 7.03: %d.%02d, disabling real mode support.\n", qemm_major, qemm_minor);
            }

            MAIN_Options[OPT_RM].value = FALSE;
            MAIN_QEMM_Present = FALSE;
        }
    }
    MAIN_HDPMI_Present = FALSE;
    if(MAIN_Options[OPT_PM].value)
    {
        MAIN_Options[OPT_PM].value = TRUE; //set to known value for compare
        BOOL hasHDPMI = HDPMIPT_Detect(); //another DPMI host used other than HDPMI
        if(!hasHDPMI)
            { if(!padlog_only) printf("HDPMI not installed, disabling protected mode support.\n"); }
        MAIN_Options[OPT_PM].value = hasHDPMI;
        MAIN_HDPMI_Present = hasHDPMI;
    }
    MAIN_Options[OPT_OPL].value = !!MAIN_Options[OPT_OPL].value;
    if(MAIN_Options[OPT_VSBVOL].value < 0 || MAIN_Options[OPT_VSBVOL].value > 100
    || MAIN_Options[OPT_VOPLVOL].value < 0 || MAIN_Options[OPT_VOPLVOL].value > 100
    || MAIN_Options[OPT_VMIDIVOL].value < 0 || MAIN_Options[OPT_VMIDIVOL].value > 400)
    {
        MAIN_CPrintf(RED, "Error: Invalid volume (/VSBVOL and /VOPLVOL 0-100, /VMIDIVOL 0-400 percent).\n");
        return 1;
    }
    if(MAIN_Options[OPT_VCDA].value < 0 || MAIN_Options[OPT_VCDA].value > 400)
    {
        MAIN_CPrintf(RED, "Error: Invalid CD-Audio gain (/VCDA 0-400 percent).\n");
        return 1;
    }

#if SBEMU_VMPU

#if SBEMU_VSC55
    //naming a ROM folder or ROM set implies /VMPU, unless /VMPU was given explicitly (e.g. /VMPU=0)
    if(!(MAIN_Options[OPT_VMPU_VOICES].setcmd&MAIN_SETCMD_SET)
        && ((MAIN_Options[OPT_VMPU_SF].setcmd&MAIN_SETCMD_SET) || (MAIN_Options[OPT_VSC_SET].setcmd&MAIN_SETCMD_SET) || MAIN_USE_SF))
        MAIN_Options[OPT_VMPU_VOICES].value = 1;
    if(MAIN_Options[OPT_VSC_VOICES].value < 0 || MAIN_Options[OPT_VSC_VOICES].value > 28 || (MAIN_Options[OPT_VSC_VOICES].value > 0 && MAIN_Options[OPT_VSC_VOICES].value < 4)
        || MAIN_Options[OPT_VSC_FX].value < 0 || MAIN_Options[OPT_VSC_FX].value > 1
        || MAIN_Options[OPT_VSC_AUTO].value < 0 || MAIN_Options[OPT_VSC_AUTO].value > 1
        || MAIN_Options[OPT_VSC_AHEAD].value < 0 || MAIN_Options[OPT_VSC_AHEAD].value > 150
        || MAIN_Options[OPT_VTIMER].value < 0 || MAIN_Options[OPT_VTIMER].value > 1)
    {
        MAIN_CPrintf(RED, "Error: Invalid /VSCVOICES (0 or 4-24) or /VSCFX (0 or 1).\n");
        return 1;
    }
    if(MAIN_Options[OPT_VSC_GAIN].value < 0 || MAIN_Options[OPT_VSC_GAIN].value > 1600)
    {
        MAIN_CPrintf(RED, "Error: Invalid SC-55 gain (0-1600 percent).\n");
        return 1;
    }
    if(MAIN_Options[OPT_VSFREV].value < 0 || MAIN_Options[OPT_VSFREV].value > 100 || MAIN_Options[OPT_VSFROOM].value < 0 || MAIN_Options[OPT_VSFROOM].value > 100
        || MAIN_Options[OPT_VSFDAMP].value < 0 || MAIN_Options[OPT_VSFDAMP].value > 100 || MAIN_Options[OPT_VSFCHO].value < 0 || MAIN_Options[OPT_VSFCHO].value > 100
        || MAIN_Options[OPT_VSFCRATE].value < 1 || MAIN_Options[OPT_VSFCRATE].value > 30 || MAIN_Options[OPT_VSFCDEPTH].value < 0 || MAIN_Options[OPT_VSFCDEPTH].value > 100)
    {
        MAIN_CPrintf(RED, "Error: Invalid SoundFont effect setting (/VSFREV /VSFROOM /VSFDAMP /VSFCHO /VSFCDEPTH 0-100, /VSFCRATE 1-30).\n");
        return 1;
    }
#endif

    //HW MPU is the default, if VMPU being set, override HW MPU. othewise error.
    //should be checked before MAIN_TSR_InstallationCheck
    if((MAIN_Options[OPT_SCMPU].value || (MAIN_Options[OPT_MPUCOMPORT].value != 9 && MAIN_Options[OPT_MPUCOMPORT].value != 0)) && VMPU_ENABLED)
    {
        MAIN_CPrintf(RED, "Conflict settings: %s with %s or %s.\n", 
            MAIN_Options[OPT_VMPU_VOICES].option, MAIN_Options[OPT_SCMPU].option, MAIN_Options[OPT_MPUCOMPORT].option);
        return -1;
    }
    if(VMPU_ENABLED) //default: /VMPU without params
        MAIN_Options[OPT_VMPU_VOICES].value = 64;
#if SBEMU_VSC55
    if(VMPU_ENABLED && MAIN_USE_SF)
    {
        if(MAIN_Options[OPT_VSFVOICES].value < 8 || MAIN_Options[OPT_VSFVOICES].value > 128)
        {
            MAIN_CPrintf(RED, "Error: Invalid /VSFVOICES (8-128).\n");
            return 1;
        }
        MAIN_Options[OPT_VMPU_VOICES].value = MAIN_Options[OPT_VSFVOICES].value;
    }
#endif
    if(!MAIN_Options[OPT_VMPU_SF].value) //NULL="/VMSF"
        MAIN_Options[OPT_VMPU_SF].value = (uintptr_t)VMPU_DEF_SF2;
#endif

    //TSR installation check: update parameter & exit if already installed
    MAIN_Step("checking for a resident SBEMU");
    MAIN_TSR_InstallationCheck();
    if(MAIN_Options[OPT_PADLOG].setcmd & MAIN_SETCMD_SET) { printf("SBEMU is not loaded.\n"); exit(1); }

#if SBEMU_VMPU && SBEMU_VSC55
    //Auto-load: on a fresh install (a second run already exited above), switch the SC-55 on by itself when
    //a ROM folder with files in it is found, so plain "SBEMU" is enough. /VMPU=0 turns this off; real MPU
    //options (/SCMPU, /MPUCOM) or an explicit /VMPU leave the choice to the user.
    if(!(MAIN_Options[OPT_VMPU_VOICES].setcmd&MAIN_SETCMD_SET) && !VMPU_ENABLED
        && !MAIN_Options[OPT_SCMPU].value && (MAIN_Options[OPT_MPUCOMPORT].value == 9 || MAIN_Options[OPT_MPUCOMPORT].value == 0)
        && MAIN_Options[OPT_MPUADDR].value)
    {
        char rompath[_MAX_PATH];
        const char* rd = (const char*)MAIN_Options[OPT_VMPU_SF].value;
        get_abs_path(rompath, sizeof(rompath), rd ? rd : VMPU_DEF_ROMS);
        DIR* dir = opendir(rompath);
        if(dir)
        {
            int files = 0;
            struct dirent* de;
            while(!files && (de = readdir(dir)) != NULL)
                files = de->d_name[0] != '.';
            closedir(dir);
            if(files)
            {
                MAIN_Options[OPT_VMPU_VOICES].value = 64;
                MAIN_CPrintf(LIGHTGRAY, "SC-55 ROM folder found (%s): enabling the SC-55 MPU-401 automatically (/VMPU=0 disables).\n", rompath);
            }
        }
    }
#endif

#if SBEMU_VMPU && MAIN_VMPU_HDPMI_MEMFIX
#if SBEMU_VSC55
    if(VMPU_ENABLED && MAIN_USE_SF) //SoundFont instead of the SC-55: size the resident memory from the file
    {
        char sfpath[_MAX_PATH];
        struct stat sfst;
        if(stat(get_abs_path(sfpath, sizeof(sfpath), (const char*)MAIN_Options[OPT_VSF].value), &sfst) != 0)
        {
            MAIN_CPrintf(RED, "Error: SoundFont file not found: %s\n", sfpath);
            return 1;
        }
        VMPU_MemSize = (size_t)sfst.st_size * 2 + 8*1024*1024;
    }
    if(VMPU_ENABLED) //reserve resident memory only when the SC-55 emulation is requested
#endif
    MAIN_VMPU_ALLOCMEM();
#endif

    MAIN_SetBlasterEnv(MAIN_Options);

    BOOL enablePM = MAIN_Options[OPT_PM].value;
    BOOL enableRM = MAIN_Options[OPT_RM].value;
    if(!enablePM && !enableRM)
    {
        MAIN_CPrintf(RED, "Both real mode & protected mode support are disabled, exiting.\r\n");
        return 1;
    }

    if(MAIN_Options[OPT_SCLIST].value)
        aui.card_controlbits |= AUINFOS_CARDCNTRLBIT_TESTCARD; //note: this bit will make aui.card_handler == NULL and quit.
    if(MAIN_Options[OPT_SCFM].value)
        aui.card_select_index_fm = MAIN_Options[OPT_SCFM].value;
    if(MAIN_Options[OPT_SCMPU].value)
        aui.card_select_index_mpu401 = MAIN_Options[OPT_SCMPU].value;
    if(MAIN_Options[OPT_SC].value)
        aui.card_select_index = MAIN_Options[OPT_SC].value;
    aui.card_select_config = MAIN_Options[OPT_OUTPUT].value;
    MAIN_D("Real mode support %s, protected mode support %s, QEMM/QPIEMU %s, HDPMI %s\n", enableRM ? "on" : "off", enablePM ? "on" : "off",
        MAIN_QEMM_Present ? "found" : "not found", MAIN_HDPMI_Present ? "found" : "not found");
    MAIN_Step("sound card init (AU_init)");
    AU_init(&aui, &fm_aui, &mpu401_aui);
    if(!aui.card_handler)
    {
        MAIN_D("No usable sound card found (AU_init gave no card handler)\n");
        return 1;
    }
    MAIN_D("Card: %s (test index %d), IRQ %d, PCI %s %04x:%04x at %02x:%02x.%d\n",
        aui.card_handler->shortname ? aui.card_handler->shortname : "?", aui.card_test_index, aui.card_irq,
        aui.card_pci_dev && aui.card_pci_dev->device_name ? aui.card_pci_dev->device_name : "-",
        aui.card_pci_dev ? aui.card_pci_dev->vendor_id : 0, aui.card_pci_dev ? aui.card_pci_dev->device_id : 0,
        aui.card_pci_dev ? aui.card_pci_dev->bBus : 0, aui.card_pci_dev ? aui.card_pci_dev->bDev : 0, aui.card_pci_dev ? aui.card_pci_dev->bFunc : 0);
    MAIN_D("FM card: %s, MPU-401 card: %s\n", fm_aui.card_handler ? fm_aui.card_handler->shortname : "none (emulated)",
        mpu401_aui.card_handler ? mpu401_aui.card_handler->shortname : "none");
    atexit(&MAIN_Cleanup);

    { extern char ICH_Diag[]; if(ICH_Diag[0]) MAIN_D("ICH AC'97 set-up:%s\n", ICH_Diag); }
    MAIN_Step("checking the sound card IRQ");
    if(aui.card_pci_dev)
        MAIN_D("Card PCI after init: cmd %04x, BAR0 %08lx BAR1 %08lx, IRQ line %d\n",
            pcibios_ReadConfig_Word(aui.card_pci_dev, 4),
            (unsigned long)pcibios_ReadConfig_Dword(aui.card_pci_dev, 0x10), (unsigned long)pcibios_ReadConfig_Dword(aui.card_pci_dev, 0x14),
            aui.card_irq);
    /* UEFI with CSM may have APIC enabled (16-31) - but we need read APIC, not implemented for now.
     * 0, 1, 2, 8 and 13 are never a PCI card's IRQ (timer, keyboard, cascade, clock, FPU): Windows 98 leaves the
     * line register at 0 when it switches the sound chip off on its way to MS-DOS mode, and a handler on IRQ 0
     * froze the PC. Ask the PCI BIOS to route a real IRQ to the chip instead. */
    if(aui.card_irq > 15 || aui.card_irq == 0 || aui.card_irq == 1 || aui.card_irq == 2 || aui.card_irq == 8 || aui.card_irq == 13)
    {
        printf("Invalid Sound card IRQ: ");
        MAIN_CPrintf(RED, "%d", aui.card_irq);
        printf(", Trying to assign a valid IRQ...\n");
        aui.card_irq = pcibios_AssignIRQ(aui.card_pci_dev);
        if(aui.card_irq == 0xFF || aui.card_irq < 3)
        {
            MAIN_D("PCI BIOS could not route an IRQ to the sound card\n");
            MAIN_CPrintf(RED, "Failed to assign a valid IRQ for sound card, abort.\n");
            printf("If you came from Windows 98, restart the PC and boot straight into DOS instead.\n");
            return 1;
        }
        printf("Sound card IRQ assigned: ");
        MAIN_CPrintf(LIGHTGREEN, "%d", aui.card_irq);
        printf(".\n");
        MAIN_D("PCI BIOS routed IRQ %d to the sound card\n", aui.card_irq);
    }
    if(aui.card_irq == MAIN_Options[OPT_IRQ].value)
    {
        /* the real card already uses this IRQ: take another of the SB IRQs instead of refusing to load
         * (a BLASTER line or saved setting written for another PC often asks for the card's own IRQ) */
        static const int sbirqs[3] = {7, 5, 9};
        int old = MAIN_Options[OPT_IRQ].value, k;
        for(k = 0; k < 3 && sbirqs[k] == aui.card_irq; ++k);
        MAIN_Options[OPT_IRQ].value = sbirqs[k];
        MAIN_CPrintf(YELLOW, "Warning: ");
        printf("the sound card itself uses IRQ %d, so the emulated SB uses IRQ %d instead of /I:%d.\n", aui.card_irq, MAIN_Options[OPT_IRQ].value, old);
        printf("Save /I:%d in the settings to stop this message.\n", MAIN_Options[OPT_IRQ].value);
        MAIN_SetBlasterEnv(MAIN_Options);
    }
    pcibios_enable_interrupt(aui.card_pci_dev);
    if(aui.card_pci_dev && aui.card_irq >= 3 && aui.card_irq <= 15)
    {
        /* a PCI interrupt is level-triggered: the PIC must have the IRQ in level mode (ELCR, ports 4D0h/4D1h).
         * The BIOS does that for the IRQs it hands out at boot; an IRQ routed after Windows 98 (above) may still be
         * edge mode, and then the card's interrupts never arrive: no sound, and no USB pad (polled from them). */
        uint16_t port = aui.card_irq < 8 ? 0x4D0 : 0x4D1;
        uint8_t bit = (uint8_t)(1 << (aui.card_irq & 7)), e0 = inportb(0x4D0), e1 = inportb(0x4D1);
        if(!(inportb(port) & bit)) outportb(port, inportb(port) | bit);
        MAIN_D("ELCR (level IRQs) %02x%02x -> %02x%02x\n", e1, e0, inportb(0x4D1), inportb(0x4D0));
        if(PCI_ReadWord(0, 0x1F, 0, 0) == 0x8086)   /* Intel ICH: PIRQ A-H routing registers in the LPC bridge */
            MAIN_D("ICH PIRQ routing A-D %08lx E-H %08lx\n", (unsigned long)PCI_ReadDWord(0, 0x1F, 0, 0x60), (unsigned long)PCI_ReadDWord(0, 0x1F, 0, 0x68));
    }
    MAIN_Step("OPL3 / MPU-401 setup");

    printf("Real mode support: ");
    MAIN_Print_Enabled_Newline(enableRM);

    printf("Protected mode support: ");
    MAIN_Print_Enabled_Newline(enablePM);

    if(enablePM) //prefer PM IO since there's no mode switch and thus more faster. previously QEMM IO was used to avoid bugs/crashes.
    {
        UntrappedIO_OUT_Handler = &HDPMIPT_UntrappedIO_Write;
        UntrappedIO_IN_Handler = &HDPMIPT_UntrappedIO_Read;
    }
    else
    {
        UntrappedIO_OUT_Handler = &QEMM_UntrappedIO_Write;
        UntrappedIO_IN_Handler = &QEMM_UntrappedIO_Read;
    }

    if(MAIN_Options[OPT_OPL].value)
    {
        if (!(fm_aui.fm && fm_aui.fm_port == 0x388)) {
            QEMM_IODT *iodt = fm_aui.fm ? MAIN_HW_OPL3IODT : MAIN_OPL3IODT;
            if(enableRM && !(OPLRMInstalled=QEMM_Install_IOPortTrap(iodt, 4, &OPL3IOPT)))
            {
                MAIN_CPrintf(RED, "Error: Failed installing IO port trap for QEMM.\n");
                return 1;
            }
            if(enablePM && !(OPLPMInstalled=HDPMIPT_Install_IOPortTrap(0x388, 0x38B, iodt, 4, &OPL3IOPT_PM)))
            {
                MAIN_CPrintf(RED, "Error: Failed installing IO port trap for HDPMI.\n");
                return 1;
            }
        } else {
            printf("Not installing IO port trap. Using hardware OPL3 at port 388.\n");
        }

        const char *emutype = fm_aui.fm ? "hardware" : "emulation";
        char hwdesc[64];
        hwdesc[0] = '\0';
        if (fm_aui.fm)
          sprintf(hwdesc, "(%d:%s)", fm_aui.card_test_index, fm_aui.card_handler->shortname);
        printf("OPL3 %s%s at port 388: ", emutype, hwdesc);
        MAIN_Print_Enabled_Newline(true);
    }

    if(MAIN_Options[OPT_MPUADDR].value && 
        (MAIN_Options[OPT_MPUCOMPORT].value || VMPU_ENABLED)
    )
    {
#if SBEMU_VMPU
        if(VMPU_ENABLED)
        {
            #if MAIN_VMPU_HDPMI_MEMFIX
            MAIN_VMPU_FREEMEM(); //not needed because there is an init, will re-alloc if init failed.
            #endif

            const char* sf = (const char*)MAIN_Options[OPT_VMPU_SF].value;
            char fullpath[_MAX_PATH];
            #if SBEMU_VSC55
            VMPU_SetMidiVolume(MAIN_Options[OPT_VMIDIVOL].value);
            VMPU_SetSC55Options((const char*)MAIN_Options[OPT_VSC_SET].value, MAIN_Options[OPT_VSC_GAIN].value);
            VMPU_SetSC55Lite(MAIN_Options[OPT_VSC_FX].value, MAIN_Options[OPT_VSC_VOICES].value);
            VMPU_SetSC55Auto(MAIN_Options[OPT_VSC_AUTO].value);
            VMPU_SetAhead(MAIN_Options[OPT_VSC_AHEAD].value);
            if(MAIN_USE_MT32) //Roland MT-32 / CM-32L (Munt) instead of the SC-55: ROMs from the same folder
                VMPU_SelectMT32(1, (const char*)MAIN_Options[OPT_VMT32].value);
            else if(MAIN_USE_SF) //SoundFont synth instead of the SC-55 emulation
            {
                sf = (const char*)MAIN_Options[OPT_VSF].value;
                VMPU_SelectSoundFont(1);
                VMPU_SetSFEffects(MAIN_Options[OPT_VSFREV].value, MAIN_Options[OPT_VSFROOM].value, MAIN_Options[OPT_VSFDAMP].value,
                    MAIN_Options[OPT_VSFCHO].value, MAIN_Options[OPT_VSFCRATE].value, MAIN_Options[OPT_VSFCDEPTH].value, MAIN_Options[OPT_VSFSEND].value);
            }
            #endif
            MAIN_Step("MPU-401 synth init (ROM scan, firmware boot)");
            if( VMPU_Init(MAIN_Options[OPT_MPUADDR].value, &MAIN_Options[OPT_VMPU_VOICES].value, aui.freq_card, get_abs_path(fullpath, sizeof(fullpath), sf) ) )
            {
                #if SBEMU_VSC55
                if(MAIN_USE_MT32)
                {
                    MAIN_CPrintf(LIGHTGRAY, "MPU: Roland MT-32 emulation (Munt), ");
                    MAIN_CPrintf(LIGHTGREEN, "%s", VMPU_ModelName());
                    MAIN_CPrintf(LIGHTGRAY, ", ROMs: ");
                    MAIN_CPrintf(LIGHTBLUE, "%s\n", fullpath);
                }
                else if(MAIN_USE_SF)
                {
                    MAIN_CPrintf(LIGHTGRAY, "MPU: SoundFont synth (%d voices), file: ", MAIN_Options[OPT_VMPU_VOICES].value);
                    MAIN_CPrintf(LIGHTBLUE, "%s\n", fullpath);
                    if(MAIN_Options[OPT_VSFREV].value || MAIN_Options[OPT_VSFCHO].value)
                        MAIN_CPrintf(LIGHTGRAY, "MPU: SoundFont effects: reverb %d%%, chorus %d%%\n", MAIN_Options[OPT_VSFREV].value, MAIN_Options[OPT_VSFCHO].value);
                }
                else {
                MAIN_CPrintf(LIGHTGRAY, "MPU: Roland SC-55 emulation, ROM set ");
                MAIN_CPrintf(LIGHTGREEN, "%s", VMPU_ModelName());
                MAIN_CPrintf(LIGHTGRAY, ", ROMs: ");
                MAIN_CPrintf(LIGHTBLUE, "%s\n", fullpath);
                }
                #else
                MAIN_CPrintf(LIGHTGRAY, "MPU voices: %d, SoundFont: ", MAIN_Options[OPT_VMPU_VOICES].value);
                MAIN_CPrintf(LIGHTBLUE, "%s\n", fullpath);
                #endif
            }
            else
            {
                #if SBEMU_VSC55
                MAIN_CPrintf(YELLOW, "VMPU warning: %s emulation not available: %s\r\nThe MPU-401 will accept MIDI but stay silent.\r\n", MAIN_USE_MT32 ? "MT-32" : "SC-55", VMPU_LastError());
                #else
                MAIN_CPrintf(YELLOW, "VMPU warning: sound font file not found: %s, functions disabled.\n", fullpath);
                #if MAIN_VMPU_HDPMI_MEMFIX
                MAIN_VMPU_ALLOCMEM();
                #endif
                #endif
            }
            //always set
            MAIN_MPUIODT[0].handler = &VMPU_MPU; //330
            MAIN_MPUIODT[1].handler = &VMPU_MPU; //331
        }
#endif

        for(int i = 0; i < countof(MAIN_MPUIODT); ++i) MAIN_MPUIODT[i].port = MAIN_Options[OPT_MPUADDR].value+i;
        if(enableRM && !(MPURMInstalled=QEMM_Install_IOPortTrap(MAIN_MPUIODT, 2, &MPUIOPT)))
        {
            MAIN_CPrintf(RED, "Error: Failed installing MPU-401 IO port trap for QEMM.\n");
            return 1;
        }
        if(enablePM && !(MPUPMInstalled=HDPMIPT_Install_IOPortTrap(MAIN_Options[OPT_MPUADDR].value, MAIN_Options[OPT_MPUADDR].value+1, MAIN_MPUIODT, 2, &MPUIOPT_PM)))
        {
            MAIN_CPrintf(RED, "Error: Failed installing MPU-401 IO port trap for HDPMI.\n");
            return 1;
        }

        const char *emutype = mpu401_aui.mpu401 ? "hardware" : "emulation";
        char hwdesc[64];
        hwdesc[0] = '\0';
        if (mpu401_aui.mpu401)
          sprintf(hwdesc, "(%d:%s)", mpu401_aui.card_test_index, mpu401_aui.card_handler->shortname);
        printf("MPU-401 UART %s%s at address %x: ",
               emutype, hwdesc, MAIN_Options[OPT_MPUADDR].value);
        MAIN_Print_Enabled_Newline(true);
    }

    MAIN_Step("Sound Blaster init");
    VIRQ_Init();

    MAIN_SbemuExtFun.StartPlayback = NULL; //not used
    MAIN_SbemuExtFun.RaiseIRQ = NULL;
    MAIN_SbemuExtFun.DMA_Size = &VDMA_GetCounter;
    MAIN_SbemuExtFun.DMA_Write = &VDMA_WriteData;
    SBEMU_Init(
        MAIN_Options[OPT_IRQ].value,
        MAIN_Options[OPT_DMA].value,
        MAIN_Options[OPT_HDMA].value,
        MAIN_SB_DSPVersion[MAIN_Options[OPT_TYPE].value],
        MAIN_Options[OPT_FIX_TC].value,
        &MAIN_SbemuExtFun);
    VDMA_Virtualize(MAIN_Options[OPT_DMA].value, TRUE);
    VDMA_Virtualize(MAIN_Options[OPT_HDMA].value, TRUE);
    for(int i = 0; i < countof(MAIN_SB_IODT); ++i)
        MAIN_SB_IODT[i].port += MAIN_Options[OPT_ADDR].value;
    QEMM_IODT* SB_Iodt = MAIN_Options[OPT_OPL].value ? MAIN_SB_IODT : MAIN_SB_IODT+4;
    int SB_IodtCount = MAIN_Options[OPT_OPL].value ? countof(MAIN_SB_IODT) : countof(MAIN_SB_IODT)-4;

    {
      char hwdesc[64];
      hwdesc[0] = '\0';
      if (aui.pcm)
        sprintf(hwdesc, "(%d:%s)", aui.card_test_index, aui.card_handler->shortname);
      printf("SB %s%s emulation at address %x, IRQ %x, DMA %x: ",
             MAIN_SBTypeString[MAIN_Options[OPT_TYPE].value],
             hwdesc,
             MAIN_Options[OPT_ADDR].value,
             MAIN_Options[OPT_IRQ].value,
             MAIN_Options[OPT_DMA].value);
    }
    MAIN_Print_Enabled_Newline(true);

    BOOL QEMMInstalledVDMA = !enableRM || QEMM_Install_IOPortTrap(MAIN_VDMA_IODT, countof(MAIN_VDMA_IODT), &MAIN_VDMA_IOPT);
    #if MAIN_TRAP_RMPIC_ONDEMAND//will crash with VIRQ installed, do it temporarily. TODO: figure out why
    BOOL QEMMInstalledVIRQ = TRUE;
    #else
    BOOL QEMMInstalledVIRQ = !enableRM || QEMM_Install_IOPortTrap(MAIN_VIRQ_IODT, countof(MAIN_VIRQ_IODT), &MAIN_VIRQ_IOPT);
    #endif
    BOOL QEMMInstalledSB = !enableRM || QEMM_Install_IOPortTrap(SB_Iodt, SB_IodtCount, &MAIN_SB_IOPT);

    BOOL HDPMIInstalledVDMA1 = !enablePM || HDPMIPT_Install_IOPortTrap(0x0, 0xF, MAIN_VDMA_IODT, 16, &MAIN_VDMA_IOPT_PM1);
    BOOL HDPMIInstalledVDMA2 = !enablePM || HDPMIPT_Install_IOPortTrap(0x81, 0x83, MAIN_VDMA_IODT+16, 3, &MAIN_VDMA_IOPT_PM2);
    BOOL HDPMIInstalledVDMA3 = !enablePM || HDPMIPT_Install_IOPortTrap(0x87, 0x87, MAIN_VDMA_IODT+19, 1, &MAIN_VDMA_IOPT_PM3);
    BOOL HDPMIInstalledVHDMA1 = !enablePM || HDPMIPT_Install_IOPortTrap(0xC0, 0xDE, MAIN_VDMA_IODT+20, 16, &MAIN_VHDMA_IOPT_PM1);
    BOOL HDPMIInstalledVHDMA2 = !enablePM || HDPMIPT_Install_IOPortTrap(0x89, 0x8B, MAIN_VDMA_IODT+36, 3, &MAIN_VHDMA_IOPT_PM2);
    BOOL HDPMIInstalledVHDMA3 = !enablePM || HDPMIPT_Install_IOPortTrap(0x8F, 0x8F, MAIN_VDMA_IODT+39, 1, &MAIN_VHDMA_IOPT_PM3);
    #if MAIN_TRAP_PMPIC_ONDEMAND
    BOOL HDPMIInstalledVIRQ1 = TRUE;
    BOOL HDPMIInstalledVIRQ2 = TRUE;
    #else
    BOOL HDPMIInstalledVIRQ1 = !enablePM || HDPMIPT_Install_IOPortTrap(0x20, 0x21, MAIN_VIRQ_IODT, 2, &MAIN_VIRQ_IOPT_PM1);
    BOOL HDPMIInstalledVIRQ2 = !enablePM || HDPMIPT_Install_IOPortTrap(0xA0, 0xA1, MAIN_VIRQ_IODT+2, 2, &MAIN_VIRQ_IOPT_PM2);
    #endif
    BOOL HDPMIInstalledSB = !enablePM || HDPMIPT_Install_IOPortTrap(MAIN_Options[OPT_ADDR].value, MAIN_Options[OPT_ADDR].value+0x0F, SB_Iodt, SB_IodtCount, &MAIN_SB_IOPT_PM);

#if SBEMU_PCSPEAKER
    //* --- PC Speaker init (andersrodrig) ---
    // Populate PC Speaker IO trap table.
    // 0x42/0x43 = PIT Channel 2 data/command, 0x61 = System Control Port B (speaker gate).
    // The traps are actually installed later, after AU_start() so aui.freq_card is valid
    // for VPCSPEAKER_Init().
    {
        MAIN_PCSPEAKER_IODT[0].port = 0x42; // PIT Channel 2 Data
        MAIN_PCSPEAKER_IODT[1].port = 0x43; // PIT Command
        MAIN_PCSPEAKER_IODT[2].port = 0x61; // System Control Port B
        for(int i = 0; i < 3; i++) {
            MAIN_PCSPEAKER_IODT[i].handler = (void*)VPCSPEAKER_IOHandler;
        }
    }
#endif // SBEMU_PCSPEAKER

    BOOL TSR_ISR = FALSE;
    for(int i = MAIN_TSR_INTSTART_ID; i <= 0xFF; ++i)
    {
        DPMI_REG r = {0};
        r.h.ah = i;
        //DBG_DumpREG(&r);
        DPMI_CallRealModeINT(MAIN_TSR_INT, &r);
        if(r.h.al != 0) //find free multiplex number
            continue;
        MAIN_TSR_INT_FNO = i;
        TSR_ISR = DPMI_InstallRealModeISR(MAIN_TSR_INT, MAIN_TSR_Interrupt, &MAIN_TSRREG, &MAIN_TSRIntHandle, FALSE) == 0;
        if(!TSR_ISR)
            break;
        MAIN_ISR_DOSID = DPMI_HighMalloc((sizeof(MAIN_ISR_DOSID_String)+15)>>4, TRUE);
        //printf("DOSID:%x\n", DPMI_SEGOFF2L(MAIN_ISR_DOSID,0));
        DPMI_LMemcpy(DPMI_SEGOFF2L(MAIN_ISR_DOSID,0), DPMI_PTR2L((char*)MAIN_ISR_DOSID_String), sizeof(MAIN_ISR_DOSID_String));
        break;
    }

    _LOG("sound card IRQ: %d\n", aui.card_irq);
    PIC_MaskIRQ(aui.card_irq);
    AU_ini_interrupts(&aui);
    int samplerate = MAIN_Options[OPT_RATE].value;
    mpxplay_audio_decoder_info_s adi = {NULL, 0, 1, samplerate, SBEMU_CHANNELS, SBEMU_CHANNELS, NULL, SBEMU_BITS, SBEMU_BITS/8, 0};
    AU_setrate(&aui, &adi);
    AU_setmixer_init(&aui);
    AU_setmixer_outs(&aui, MIXER_SETMODE_ABSOLUTE, 100);
    //set volume
    AU_setmixer_one(&aui, AU_MIXCHAN_MASTER, MIXER_SETMODE_ABSOLUTE, MAIN_Options[OPT_VOL].value);
    if(MAIN_Options[OPT_OPL].value)
        OPL3EMU_SetCore(MAIN_Options[OPT_FMCORE].value);
        OPL3EMU_Init(aui.freq_card); //aui.freq_card available after AU_setrate

    MAIN_Step("installing the sound card interrupt handlers");
    BOOL PM_ISR = DPMI_InstallISR(PIC_IRQ2VEC(aui.card_irq), MAIN_InterruptPM, &MAIN_IntHandlePM, MAIN_ISR_CHAINED) == 0;

    #if MAIN_INSTALL_RM_ISR
    BOOL RM_ISR = DPMI_InstallRealModeISR(PIC_IRQ2VEC(aui.card_irq), MAIN_InterruptRM, &MAIN_RMIntREG, &MAIN_IntHandleRM, MAIN_ISR_CHAINED) == 0;
    #else
    BOOL RM_ISR = TRUE;
    MAIN_IntHandleRM.wrapper_cs = MAIN_IntHandleRM.wrapper_offset = -1; //skip for HDPMIPT_InstallIRQRouteHandler
    #endif
    
    IRQGUARD_Install(MAIN_Options[OPT_IRQ].value);
    struct
    {
        int irq;
        HDPMIPT_IRQRoutedHandle* handle;
    }SBIRQRouting[] =
    {
        5, &OldRoutedHandle5,
        7, &OldRoutedHandle7,
        9, &OldRoutedHandle9,
    };
    for(int i = 0; i < countof(SBIRQRouting); ++i)
    {
        HDPMIPT_GetIRQRoutedHandlerH(SBIRQRouting[i].irq, SBIRQRouting[i].handle);
        DPMI_ISR_HANDLE handle;
        DPMI_GetISR(SBIRQRouting[i].irq, &handle);
        //force irq routing to default, skip games. only route to game if the virtual IRQ happens
        HDPMIPT_InstallIRQRoutedHandler(SBIRQRouting[i].irq, handle.old_cs, handle.old_offset, handle.old_rm_cs, handle.old_rm_offset);
    }

    HDPMIPT_GetIRQRoutedHandlerH(aui.card_irq, &OldRoutedHandle);
    #if !MAIN_INSTALL_RM_ISR
    {
        DPMI_ISR_HANDLE handle;
        DPMI_GetISR(aui.card_irq, &handle);
        //need preset irq routing for RM since MAIN_IntHandleRM.wrapper_cs/wrapper_offset is not valid.
        HDPMIPT_InstallIRQRoutedHandler(aui.card_irq, handle.old_cs, handle.old_offset, handle.old_rm_cs, handle.old_rm_offset);
    }
    #endif
    HDPMIPT_InstallIRQRoutedHandler(aui.card_irq, MAIN_IntHandlePM.wrapper_cs, MAIN_IntHandlePM.wrapper_offset,
        MAIN_IntHandleRM.wrapper_cs, (uint16_t)MAIN_IntHandleRM.wrapper_offset);

    HDPMIPT_LockIRQRouting(TRUE);
    PIC_UnmaskIRQ(aui.card_irq);

    DIAG_Calibrate();
    MAIN_TCps = DIAG_CyclesPerSec();
    AU_prestart(&aui);
    AU_start(&aui);

    if(MAIN_Options[OPT_VCDA].value)
    {
        if(CDLINK_Init(MAIN_Options[OPT_VCDA].value))
        {
            MAIN_CPrintf(LIGHTGRAY, "CD-Audio: uCDD link found, audio tracks of mounted CUE/BIN images play through this sound card.\n");
            /* the image is read with DOS file calls: after the card's interrupt (when DOS is free) and from
             * INT 28h, which DOS calls while it waits for a key (the prompt, PAUSE) */
            MAIN_Int28On = DPMI_InstallRealModeISR(0x28, MAIN_Int28, &MAIN_Int28REG, &MAIN_Int28Handle, FALSE) == 0;
            MAIN_D("CD-Audio: INT 28h idle hook %s\n", MAIN_Int28On ? "installed" : "FAILED");
        }
        else
            MAIN_CPrintf(YELLOW, "CD-Audio: the uCDD driver (built with --sbemu-link) is not loaded, /VCDA ignored.\n");
    }

#if SBEMU_PCSPEAKER
    // Now that the audio device is running, aui.freq_card is valid for PC Speaker setup.
    // QEMM accepts the whole 3-port table in one call.
    // HDPMI needs two calls because 0x42-0x43 and 0x61 are not contiguous.
    VPCSPEAKER_Init(aui.freq_card);

    if(enableRM)
    {
        MAIN_PCSPEAKER_Installed = QEMM_Install_IOPortTrap(MAIN_PCSPEAKER_IODT, 3, &MAIN_PCSPEAKER_IOPT);
    }

    if(enablePM)
    {
        HDPMIPT_Install_IOPortTrap(0x42, 0x43, MAIN_PCSPEAKER_IODT,     2, &MAIN_PCSPEAKER_IOPT);
        HDPMIPT_Install_IOPortTrap(0x61, 0x61, MAIN_PCSPEAKER_IODT + 2, 1, &MAIN_PCSPEAKER_IOPT_61);
        MAIN_PCSPEAKER_Installed = TRUE;
    }

    printf("PC Speaker emulation (Ports 42,43,61): ");
    MAIN_Print_Enabled_Newline(MAIN_PCSPEAKER_Installed);
#endif // SBEMU_PCSPEAKER

    if(MAIN_Options[OPT_JOY].value)
    {
        const char* bad = NULL;
        const char* msg = "";
        MAIN_Step("USB joystick / keyboard / mouse (/JOY)");
        if(!VJOY_Init(MAIN_TCps, (const char*)MAIN_Options[OPT_JOYAX].value, (const char*)MAIN_Options[OPT_JOYBT].value,
            (const char*)MAIN_Options[OPT_JOYKEY].value, MAIN_Options[OPT_JOYDZ].value, MAIN_Options[OPT_JOYMODE].value, &bad))
            MAIN_CPrintf(YELLOW, "Joystick: \"%s\" not understood in /JOYAX, /JOYBT or /JOYKEY, USB off.\n", bad ? bad : "?");
        else
        {
            int flags = (MAIN_Options[OPT_USBKB].value ? USBPAD_F_KBD : 0) | (MAIN_Options[OPT_USBMS].value ? USBPAD_F_MOUSE : 0)
                | (MAIN_Options[OPT_USBDISK].value ? USBPAD_F_DISK : 0);
            VKBC_Init(MAIN_TCps, 0);
            USBPAD_SetHid(&VKBC_UsbKeys, &VKBC_UsbMouse);
            int usb_ok;
            if(MAIN_Det) { MAIN_D("USB live: "); USBPAD_SetTraceHook(MAIN_UsbTrace); }
            usb_ok = USBPAD_Init(MAIN_TCps, 3000, flags, &msg);
            USBPAD_SetTraceHook(NULL);
            MAIN_D("\n");
            if(!usb_ok)
                MAIN_CPrintf(YELLOW, "Joystick: %s, USB off.\n", msg);
            else
            {
                /* MAIN_JOY_On (USB polled from the sound interrupt) only once everything below is in place:
                 * beta.64 on the P5PE-VM hung right after USBPAD_Init, somewhere in here */
                if(MAIN_Options[OPT_JOYMODE].value == 4)
                    printf("Joystick (Port 201): left to the real gameport (/JOYMODE:4)\n");
                else
                {
                    static const char* modes[] = { "", " as CH Flightstick Pro", " as Thrustmaster FCS", " as Gravis GamePad Pro (GrIP)" };
                    int md = MAIN_Options[OPT_JOYMODE].value;
                    MAIN_Step("USB: port 201h trap");
                    if(enableRM) MAIN_JOY_RM = QEMM_Install_IOPortTrap(MAIN_JOY_IODT, 2, &MAIN_JOY_IOPT);
                    if(enablePM) MAIN_JOY_PM = HDPMIPT_Install_IOPortTrap(0x201, 0x201, MAIN_JOY_IODT, 1, &MAIN_JOY_IOPT_PM);
                    printf("Joystick (Port 201)%s: ", md >= 0 && md <= 3 ? modes[md] : "");
                    MAIN_Print_Enabled_Newline(MAIN_JOY_RM || MAIN_JOY_PM);
                    MAIN_Step("USB: INT 15h joystick hook");
                    if(MAIN_Options[OPT_JOYBIOS].value && !MAIN_HookInt15(1))
                        MAIN_CPrintf(YELLOW, "  BIOS joystick (INT 15h AH=84h) not hooked: no memory.\n");
                }
                MAIN_CPrintf(LIGHTGRAY, "  USB: %s.\n", msg);
                if(MAIN_Options[OPT_USBMS].value && !enableRM)
                {
                    /* no real-mode traps: the mouse driver's own port I/O can't be seen, so the USB mouse goes
                     * through the BIOS mouse services it uses (see MAIN_I15Stub); the touchpad / PS/2 mouse
                     * keeps working beside it, and a USB mouse plugged in later works too */
                    MAIN_Step("USB: mouse through the BIOS (INT 15h C2h)");
                    if(MAIN_HookInt15(0))
                    {
                        VKBC_SetBios(MAIN_I15Lin);
                        printf("USB mouse (through the BIOS mouse services): ");
                        MAIN_Print_Enabled_Newline(TRUE);
                        MAIN_CPrintf(LIGHTGRAY, "  Load the mouse driver (CTMOUSE, MOUSE.COM) after SBEMU. The touchpad works too.\n");
                    }
                    else MAIN_CPrintf(YELLOW, "  USB mouse: INT 15h not hooked (no memory).\n");
                }
                else if(MAIN_Options[OPT_USBMS].value == 2 || (MAIN_Options[OPT_USBMS].value == 1 && (USBPAD_Have() & USBPAD_HAVE_MOUSE)))
                {
                    MAIN_Step("USB: keyboard controller traps (60h/64h)");
                    VKBC_Init(MAIN_TCps, 1);
                    if(enableRM) MAIN_KBC_RM = QEMM_Install_IOPortTrap(MAIN_KBC_IODT, 2, &MAIN_KBC_IOPT);
                    if(enablePM) MAIN_KBC_PM60 = HDPMIPT_Install_IOPortTrap(0x60, 0x60, MAIN_KBC_IODT, 1, &MAIN_KBC_IOPT_PM60);
                    if(enablePM) MAIN_KBC_PM64 = HDPMIPT_Install_IOPortTrap(0x64, 0x64, MAIN_KBC_IODT + 1, 1, &MAIN_KBC_IOPT_PM64);
                    printf("USB mouse as PS/2 mouse (Ports 60,64): ");
                    MAIN_Print_Enabled_Newline(MAIN_KBC_RM || MAIN_KBC_PM60);
                    MAIN_CPrintf(LIGHTGRAY, "  Load the mouse driver (CTMOUSE, MOUSE.COM) after SBEMU.\n");
                }
                if(!(USBPAD_Have() & (USBPAD_HAVE_PAD1 | USBPAD_HAVE_PAD2)))
                    MAIN_CPrintf(LIGHTGRAY, "  Plug a pad in any time; it is picked up while games run.\n"
                        "  Not found? Run USBTEST.EXE (without SBEMU loaded) to see every USB step.\n");
                if(MAIN_Options[OPT_PADKEYS].value)
                {
                    /* /JOYKEY given and not the default: those buttons stay as it says, in every layout */
                    const char* jk = (const char*)MAIN_Options[OPT_JOYKEY].value;
                    int over = (MAIN_Options[OPT_JOYKEY].setcmd & MAIN_SETCMD_SET) && strcasecmp(jk, "ST:1C,BK:01") != 0;
                    MAIN_Step("pad layouts (/PADKEYS)");
                    if(MAIN_PadLoad())
                    {
                        MAIN_Step("pad layouts: INT 21h EXEC hook");
                        if(MAIN_HookInt21())
                        {
                            MAIN_PadOn = TRUE;
                            VJOY_PadKeys(1, over);
                            MAIN_Print_Enabled_Newline(TRUE);
                            if(over) MAIN_CPrintf(LIGHTGRAY, "  /JOYKEY:%s keeps its buttons in every layout.\n", jk);
                        }
                        else { PADMAP_Reset(); MAIN_CPrintf(YELLOW, "not on (INT 21h not hooked: no memory)\n"); }
                    }
                    MAIN_D("Pad keys: %s (INT 21h stub at %x, /JOYKEY %s)\n", MAIN_PadOn ? "on" : "off - /JOYKEY only",
                        MAIN_I21Lin, over ? "overrides its buttons" : "used only without layouts");
                }
                else MAIN_D("Pad keys: off (/PADKEYS:0) - /JOYKEY only\n");
                MAIN_Step("USB: polling from the sound interrupt on");
                MAIN_JOY_On = TRUE;
                {   /* a second of it before going on, so a hang there shows up in this log */
                    uint64_t t0 = DIAG_Tsc();
                    while(DIAG_Tsc() - t0 < MAIN_TCps) {}
                }
                MAIN_Step("USB: 1 s of polling went fine");
                if(MAIN_Options[OPT_USBDISK].value)
                {
                    MAIN_Step("USB: disk drive letters (/USBDISK)");
                    if(USBDISK_Install()) MAIN_CPrintf(LIGHTGREEN, "USB disk: %s\n", USBDISK_Msg());
                    else MAIN_CPrintf(YELLOW, "USB disk: %s.\n", USBDISK_Msg());
                    MAIN_D("USB disk: %s\n", USBDISK_Msg());
                }
            }
        }
        MAIN_D("Joystick mode %d\n", MAIN_Options[OPT_JOYMODE].value);
        MAIN_D("Joystick: %s | axes %s buttons %s keys %s dz %d | kb %d ms %d | traps rm %d pm %d kbc %d %d%d\n", msg,
            (const char*)MAIN_Options[OPT_JOYAX].value, (const char*)MAIN_Options[OPT_JOYBT].value,
            (const char*)MAIN_Options[OPT_JOYKEY].value, MAIN_Options[OPT_JOYDZ].value, MAIN_Options[OPT_USBKB].value,
            MAIN_Options[OPT_USBMS].value, MAIN_JOY_RM, MAIN_JOY_PM, MAIN_KBC_RM, MAIN_KBC_PM60, MAIN_KBC_PM64);
        MAIN_D("USB devices: %s\n", USBPAD_Devices());
        MAIN_D("USB steps: %s\n", USBPAD_Trace());
        USBPAD_TraceStop();
    }

    MAIN_LogCommit();
    /* Close the log before going resident. Until beta.28 the resident SBEMU kept SBLOG.TXT open as its
     * stdout: anything it printed while a game ran became a DOS disk write (possibly from inside the sound
     * interrupt), and a second SBEMU run (GAMETEST, MENU's Play) opened the same file again. That only
     * happened once beta.26 fixed "Out of environment space" - before, SBEMULOG was never set. */
    if(MAIN_LogOn)
    {
        fflush(stdout);
        if(!freopen("CON", "w", stdout)) freopen("NUL", "w", stdout);
        MAIN_LogOn = 0;
    }
    MAIN_D("Resolved: SB port %x IRQ %d DMA %d HDMA %d type %d, MPU %x, sample rate %u, card IRQ %d\n",
        MAIN_Options[OPT_ADDR].value, MAIN_Options[OPT_IRQ].value, MAIN_Options[OPT_DMA].value, MAIN_Options[OPT_HDMA].value,
        MAIN_Options[OPT_TYPE].value, MAIN_Options[OPT_MPUADDR].value, aui.freq_card, aui.card_irq);
    MAIN_D("Traps: QEMM vdma %d virq %d sb %d | HDPMI vdma %d%d%d vhdma %d%d%d virq %d%d sb %d | ISR pm %d rm %d tsr %d\n",
        QEMMInstalledVDMA, QEMMInstalledVIRQ, QEMMInstalledSB, HDPMIInstalledVDMA1, HDPMIInstalledVDMA2, HDPMIInstalledVDMA3,
        HDPMIInstalledVHDMA1, HDPMIInstalledVHDMA2, HDPMIInstalledVHDMA3, HDPMIInstalledVIRQ1, HDPMIInstalledVIRQ2, HDPMIInstalledSB,
        PM_ISR, RM_ISR, TSR_ISR);
    {   /* the card has been playing since AU_start (USB init alone waits up to 3 s): no interrupt yet means the
         * IRQ does not reach the PIC - SBEMU would load but stay silent */
        uint32_t t0 = _farpeekl(_dos_ds, 0x46C);
        while(!MAIN_CardIrqs && _farpeekl(_dos_ds, 0x46C) - t0 < 10) {}
        MAIN_D("Sound card interrupts so far: %lu (IRQ %d)\n", (unsigned long)MAIN_CardIrqs, aui.card_irq);
        if(!MAIN_CardIrqs)
        {
            MAIN_CPrintf(RED, "Warning: no interrupts from the sound card on IRQ %d - no sound, no USB pad.\n", aui.card_irq);
            printf("Please send C:\\SC55\\SBDETAIL.TXT. Booting straight into DOS avoids it.\n");
        }
    }
    MAIN_Step("going resident");
    MAIN_DetClose(PM_ISR && RM_ISR && TSR_ISR && QEMMInstalledVDMA && QEMMInstalledVIRQ && QEMMInstalledSB && HDPMIInstalledSB
        ? "start-up finished, going resident" : "a trap or handler failed (see Traps above)");
    MAIN_TSRed = TRUE;
    if(!PM_ISR || !RM_ISR || !TSR_ISR
    || !QEMMInstalledVDMA || !QEMMInstalledVIRQ || !QEMMInstalledSB
    || !HDPMIInstalledVDMA1 || !HDPMIInstalledVDMA2 || !HDPMIInstalledVDMA3 || !HDPMIInstalledVHDMA1 || !HDPMIInstalledVHDMA2 || !HDPMIInstalledVHDMA3
    || !HDPMIInstalledVIRQ1 || !HDPMIInstalledVIRQ2 || !HDPMIInstalledSB
    || !(MAIN_TSRed=DPMI_TSR()))
    {
        if(!QEMMInstalledVDMA || !QEMMInstalledVIRQ || !QEMMInstalledSB)
            MAIN_CPrintf(RED, "Error: Failed installing IO port trap for QEMM.\n");
        if(enableRM && QEMMInstalledVDMA) QEMM_Uninstall_IOPortTrap(&MAIN_VDMA_IOPT);
        #if !MAIN_TRAP_RMPIC_ONDEMAND
        if(enableRM && QEMMInstalledVIRQ) QEMM_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT);
        #endif
        if(enableRM && QEMMInstalledSB) QEMM_Uninstall_IOPortTrap(&MAIN_SB_IOPT);

        if(!HDPMIInstalledVDMA1 || !HDPMIInstalledVDMA2 || !HDPMIInstalledVDMA3 || !HDPMIInstalledVHDMA1 || !HDPMIInstalledVHDMA2 || !HDPMIInstalledVHDMA3 || !HDPMIInstalledVIRQ1 || !HDPMIInstalledVIRQ2 || !HDPMIInstalledSB)
            MAIN_CPrintf(RED, "Error: Failed installing IO port trap for HDPMI.\n");
        if(enablePM && HDPMIInstalledVDMA1) HDPMIPT_Uninstall_IOPortTrap(&MAIN_VDMA_IOPT_PM1);
        if(enablePM && HDPMIInstalledVDMA2) HDPMIPT_Uninstall_IOPortTrap(&MAIN_VDMA_IOPT_PM2);
        if(enablePM && HDPMIInstalledVDMA3) HDPMIPT_Uninstall_IOPortTrap(&MAIN_VDMA_IOPT_PM3);
        if(enablePM && HDPMIInstalledVHDMA1) HDPMIPT_Uninstall_IOPortTrap(&MAIN_VHDMA_IOPT_PM1);
        if(enablePM && HDPMIInstalledVHDMA2) HDPMIPT_Uninstall_IOPortTrap(&MAIN_VHDMA_IOPT_PM2);
        if(enablePM && HDPMIInstalledVHDMA3) HDPMIPT_Uninstall_IOPortTrap(&MAIN_VHDMA_IOPT_PM3);
        #if !MAIN_TRAP_PMPIC_ONDEMAND
        if(enablePM && HDPMIInstalledVIRQ1) HDPMIPT_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT_PM1);
        if(enablePM && HDPMIInstalledVIRQ2) HDPMIPT_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT_PM2);
        #endif
        if(enablePM && HDPMIInstalledSB) HDPMIPT_Uninstall_IOPortTrap(&MAIN_SB_IOPT_PM);

        if(!PM_ISR)
            MAIN_CPrintf(RED, "Error: Failed installing sound card ISR.\n");
        if(!RM_ISR)
            MAIN_CPrintf(RED, "Error: Failed installing sound card ISR.\n");
        #if MAIN_INSTALL_RM_ISR
        if(RM_ISR) DPMI_UninstallISR(&MAIN_IntHandleRM); //note: orders are important: reverse order of installation
        #endif
        if(PM_ISR) DPMI_UninstallISR(&MAIN_IntHandlePM);
        if(!TSR_ISR)
            MAIN_CPrintf(RED, "Error: Failed installing TSR interrupt.\n");
        if(TSR_ISR) DPMI_UninstallISR(&MAIN_TSRIntHandle);

        if(!MAIN_TSRed)
            MAIN_CPrintf(RED, "Error: Failed installing TSR.\n");
        MAIN_UnhookInt21();
    }
    return 1;
}

//with the modified fork of HDPMI, HDPMI will route PM interrupts to IVT.
//IRQ routing path:
//PM: IDT -> PM handlers after SBEMU -> SBEMU MAIN_InterruptPM(*) -> PM handlers befoe SBEMU -> IVT -> SBEMU MAIN_InterruptRM(*) -> DPMI entrance -> IVT handlers before DPMI installed
//RM: IVT -> RM handlers before SBEMU -> SBEMU MAIN_InterruptRM(*) -> RM handlers after SBEMU -> DPMI entrance -> PM handlers after SBEMU -> SBEMU MAIN_InterruptPM(*) -> PM handlers befoe SBEMU -> IVT handlers before DPMI installed
//(*) means SBEMU might early terminate the calling chain if sound irq is handled (when MAIN_ISR_CHAINED==0).
//early terminating is OK because PCI irq are level triggered, IRQ signal will keep high (raised) unless the hardware IRQ is ACKed.

/* The sound interrupt can stop the program at any instruction, also in the middle of its floating point
 * math. Parts of what runs here use the x87 FPU themselves (the PC speaker, the CD volume, printf on errors);
 * the synths save it on their own. With 8 registers only, a program that has most of them in use (3D drawing:
 * MIDPLAY's 3D view, Quake-style games) lost values to stack overflow: black lines across MIDPLAY's 3D picture.
 * Now the whole interrupt keeps the program's FPU state on its own stack (nesting-safe): inside, the FPU is empty
 * (fnsave resets it) but keeps the program's control word, so a game's own IRQ handler called from here rounds
 * as the game set it, like on real hardware. */
#ifdef DJGPP
#define MAIN_FPU_ENTER() unsigned char main_fpu_state[108]; asm volatile("fnsave %0\n\tfldcw %0" : "+m"(main_fpu_state))
#define MAIN_FPU_LEAVE() asm volatile("frstor %0" :: "m"(main_fpu_state))
#else
#define MAIN_FPU_ENTER() do {} while(0)
#define MAIN_FPU_LEAVE() do {} while(0)
#endif
static void MAIN_InterruptPM()
{
    const uint8_t irq = PIC_GetIRQ();
    if(irq != aui.card_irq) //shared IRQ handled by other handlers(EOI sent) or new irq arrived after EOI but not for us
        return;

    //if(MAIN_InINT&MAIN_ININT_PM) return; //skip reentrance. go32 will do this so actually we don't need it
    //DBG_Log("INTPM %d\n", MAIN_InINT);
    MAIN_InINT |= MAIN_ININT_PM;

    //note: we have full control of the calling chain, if the irq belongs to the sound card,
    //we send EOI and skip calling the chain - it will be a little faster. if other devices raises irq at the same time,
    //the interrupt handler will entered again (not nested) so won't be a problem.
    //also we send EOI on our own and terminate, this doesn't rely on the default implementation in IVT - some platform (i.e. VirtualBox)
    //don't send EOI on default handler in IVT.
    //
    //it has one problem that if other drivers (shared IRQ) enables interrupts (because it needs wait or is time consuming)
    //then because we're still in MAIN_InterruptPM, so MAIN_InterruptPM is never entered again (guarded by go32 or MAIN_ININT_PM),
    //so the newly coming irq will never be processed and the IRQ will flood the system (freeze)
    //an alternative chained methods will EXIT MAIN_InterruptPM FIRST and calls next handler, which will avoid this case, see @MAIN_ISR_CHAINED
    //but we need a hack if the default handler in IVT doesn't send EOI or masks the irq - this is done in the RM final wrapper, see @DPMI_RMISR_ChainedWrapper

    //MAIN_IntContext.EFLAGS |= (MAIN_InINT&MAIN_ININT_RM) ? (MAIN_IntContext.EFLAGS&CPU_VMFLAG) : 0;
    HDPMIPT_GetInterrupContext(&MAIN_IntContext);
    if(/*!(MAIN_InINT&MAIN_ININT_RM) && */aui.card_handler->irq_routine && aui.card_handler->irq_routine(&aui)) //check if the irq belong the sound card
    {
        MAIN_FPU_ENTER();   /* the program we interrupted owns the FPU: keep its registers (see MAIN_FPU_ENTER) */
        uint64_t isr_t0 = DIAG_Tsc();
        DIAG_IsrBegin();
        SETIF();
        MAIN_Interrupt();
        RESETIF();
        PIC_SendEOIWithIRQ(aui.card_irq); //some BIOS driver doesn't works well if not sending EOI, there's extra check for EOI in DPMI_RMISR_ChainedWrapper
        DIAG_IsrEnd(MAIN_DiagFrames);
        MAIN_TimerReplay(DIAG_Tsc() - isr_t0);
        MAIN_CDService();
        MAIN_FPU_LEAVE();
    }
    #if !MAIN_ISR_CHAINED
    else
    {
        if(/*(MAIN_InINT&MAIN_ININT_RM) || */(MAIN_IntContext.EFLAGS&CPU_VMFLAG))
            DPMI_CallOldISR(&MAIN_IntHandlePM);
        else
            DPMI_CallOldISRWithContext(&MAIN_IntHandlePM, &MAIN_IntContext.regs);
        PIC_UnmaskIRQ(aui.card_irq);
    }
    #endif
    //DBG_Log("INTPME %d\n", MAIN_InINT);
    MAIN_InINT &= ~MAIN_ININT_PM;
}

static void MAIN_InterruptRM()
{
    const uint8_t irq = PIC_GetIRQ();
    if(irq != aui.card_irq) //shared IRQ handled by other handlers(EOI sent) or new irq arrived after EOI but not for us
        return;

    //if(MAIN_InINT&MAIN_ININT_RM) return; //skip reentrance. go32 will do this so actually we don't need it
    //DBG_Log("INTRM %d\n", MAIN_InINT);
    MAIN_InINT |= MAIN_ININT_RM;

    if(/*!(MAIN_InINT&MAIN_ININT_PM) && */aui.card_handler->irq_routine && aui.card_handler->irq_routine(&aui)) //check if the irq belong the sound card
    {
        MAIN_IntContext.regs = MAIN_RMIntREG;
        MAIN_IntContext.EFLAGS = MAIN_RMIntREG.w.flags | CPU_VMFLAG;
        MAIN_FPU_ENTER();   /* the program we interrupted owns the FPU: keep its registers (see MAIN_FPU_ENTER) */
        uint64_t isr_t0 = DIAG_Tsc();
        DIAG_IsrBegin();
        SETIF();
        MAIN_Interrupt();
        RESETIF();
        PIC_SendEOIWithIRQ(aui.card_irq); //some BIOS driver doesn't works well if not sending EOI, there's extra check for EOI in DPMI_RMISR_ChainedWrapper
        DIAG_IsrEnd(MAIN_DiagFrames);
        MAIN_TimerReplay(DIAG_Tsc() - isr_t0);
        MAIN_CDService();
        MAIN_FPU_LEAVE();
    }
    #if !MAIN_ISR_CHAINED
    else
    {
        DPMI_REG r = MAIN_RMIntREG; //don't modify MAIN_RMIntREG on hardware interrupt
        DPMI_CallRealModeOldISR(&MAIN_IntHandleRM, &r);
        PIC_UnmaskIRQ(aui.card_irq);
    }
    #endif
    //DBG_Log("INTRME %d\n", MAIN_InINT);
    MAIN_InINT &= ~MAIN_ININT_RM;
}

/* CD-Audio image reads, after the card's EOI: inside the interrupt (before EOI) a BIOS disk read waiting for
 * IRQ 14 never got it when the card's IRQ was 9-11 (86Box ES1371 IRQ 10: hung). The card's IRQ is masked
 * meanwhile, so it cannot come back in here; a few sectors at a time keep it short. */
static void MAIN_CDService()
{
    static volatile int busy;
    if(busy || !CDLINK_NeedsService()) return;
    busy = 1;
    PIC_MaskIRQ(aui.card_irq);
    SETIF();
    CDLINK_Service(0);
    RESETIF();
    PIC_UnmaskIRQ(aui.card_irq);
    busy = 0;
}

static void MAIN_Int28()
{
    CDLINK_Service(1);
    DPMI_CallRealModeOldISR(&MAIN_Int28Handle, &MAIN_Int28REG);
}

static void MAIN_Interrupt()
{
    ++MAIN_CardIrqs;
    if(MAIN_JOY_On) VJOY_Tick();
    if(!(aui.card_infobits&AUINFOS_CARDINFOBIT_PLAYING))
        return;

    if(SBEMU_IRQTriggered())
    {
        MAIN_InvokeIRQ(SBEMU_GetIRQ());
        SBEMU_SetIRQTriggered(FALSE);
    }
    int32_t vol;
    int32_t voicevol;
    int32_t midivol;
    int32_t cdvol; // 0-100 (percentage)
    static int32_t last_cdvol = -1;
    if(MAIN_Options[OPT_TYPE].value == 1 || MAIN_Options[OPT_TYPE].value == 3) //SB2.0 and before
    {
        vol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_MASTERVOL) >> 1)*256/7;
        voicevol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_VOICEVOL) >> 1)*256/3;
        midivol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_MIDIVOL) >> 1)*256/7;
        cdvol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_CDVOL) >> 1)*100/7;
    }
    else if(MAIN_Options[OPT_TYPE].value == 6) //SB16
    {
        // TODO: This only uses the left channel volume as volume for both left and right
        vol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_MASTERSTEREO)>>4)*256/15; //4:4
        voicevol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_VOICESTEREO)>>4)*256/15; //4:4
        midivol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_MIDISTEREO)>>4)*256/15; //4:4
        cdvol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_CDSTEREO)>>4)*100/15; //4:4
        //_LOG("vol: %d, voicevol: %d, midivol: %d\n", vol, voicevol, midivol);
    }
    else //SBPro
    {
        // TODO: This only uses the left channel volume as volume for both left and right
        vol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_MASTERSTEREO)>>5)*256/7; //3:1:3:1 stereo usually the same for both channel for games?;
        voicevol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_VOICESTEREO)>>5)*256/7; //3:1:3:1
        midivol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_MIDISTEREO)>>5)*256/7;
        cdvol = (SBEMU_GetMixerReg(SBEMU_MIXERREG_CDSTEREO)>>5)*100/7;
        //_LOG("vol: %d, voicevol: %d, midivol: %d\n", vol, voicevol, midivol);
    }

    /* the user's own volumes (SETTINGS): sound effects and FM, 0-100% on top of the game's mixer */
    voicevol = voicevol * MAIN_Options[OPT_VSBVOL].value / 100;
    int32_t oplvol = midivol * MAIN_Options[OPT_VOPLVOL].value / 100;

    if (cdvol != last_cdvol) {
        // Apply CD-Audio volume to device mixer (CDDA mixing is handled purely on soundcard,
        // so plug in your 4-pin audio cable from the CD-ROM drive to the soundcard CD-IN)
        AU_setmixer_one(&aui, AU_MIXCHAN_CDIN, MIXER_SETMODE_ABSOLUTE, cdvol);
        last_cdvol = cdvol;
    }

    aui.card_outbytes = aui.card_dmasize;
    int samples = AU_cardbuf_space(&aui) / sizeof(int16_t) / SBEMU_CHANNELS; //16 bit, 2 channels
    //_LOG("samples:%d\n",samples);
    if(samples == 0)
        return;

    RESETIF();
    BOOL vmpu_active = VMPU_IsActive();
    SETIF();
    
    BOOL opl_active = MAIN_Options[OPT_OPL].value && OPL3EMU_IsActive();
    BOOL digital = SBEMU_HasStarted();
    int dma = (SBEMU_GetBits() <= 8 /*|| MAIN_Options[OPT_TYPE].value < 6*/) ? SBEMU_GetDMA() : SBEMU_GetHDMA();
    int32_t DMA_Count = VDMA_GetCounter(dma); //count in bytes
    if(!digital) MAIN_LastSBRate = 0;
    if(digital)//&& DMA_Count != 0x10000) //-1(0xFFFF)+1=0
    {
        uint32_t DMA_Addr = VDMA_GetAddress(dma);
        int32_t DMA_Index = VDMA_GetIndex(dma);
        uint32_t SB_Bytes = SBEMU_GetSampleBytes();
        uint32_t SB_Pos = SBEMU_GetPos();
        uint32_t SB_Rate = SBEMU_GetSampleRate();
        #if MAIN_PCM_RESAMPLE_INTERPOLATION
        if(MAIN_LastSBRate != SB_Rate)
        {
            for(int i = 0; i < SBEMU_CHANNELS; ++i) MAIN_LastResample[i] = 0;
            MAIN_LastSBRate = SB_Rate;
        }
        #endif
        int samplesize = max(1, SBEMU_GetBits()/8); //sample size in bytes 1 for 8bit. 2 for 16bit
        int channels = SBEMU_GetChannels();
        _LOG("sample rate: %d %d\n", SB_Rate, aui.freq_card);
        _LOG("channels: %d, size:%d\n", channels, samplesize);
        //_LOG("DMA index: %x\n", DMA_Index);
        //_LOG("digital start\n");
        int pos = 0;
        do
        {
            if(MAIN_DMA_MappedAddr != 0
             && !(DMA_Addr >= MAIN_DMA_Addr && DMA_Addr+DMA_Index+DMA_Count <= MAIN_DMA_Addr+MAIN_DMA_Size))
            {
                if(MAIN_DMA_MappedAddr > 1024*1024)
                    DPMI_UnmappMemory(MAIN_DMA_MappedAddr);
                MAIN_DMA_MappedAddr = 0;
            }
            if(MAIN_DMA_MappedAddr == 0)
            {
                MAIN_DMA_Addr = DMA_Addr&~0xFFF;
                MAIN_DMA_Size = align(max(DMA_Addr-MAIN_DMA_Addr+DMA_Index+DMA_Count, 64*1024*2), 4096);
                MAIN_DMA_MappedAddr = (DMA_Addr+DMA_Index+DMA_Count <= 1024*1024) ? (DMA_Addr&~0xFFF) : DPMI_MapMemory(MAIN_DMA_Addr, MAIN_DMA_Size);
            }
            //_LOG("DMA_ADDR:%x, %x, %x\n",DMA_Addr, MAIN_DMA_Addr, MAIN_DMA_MappedAddr);

            int count = samples-pos;
            BOOL resample = TRUE; //don't resample if sample rates are close
            if(SB_Rate < aui.freq_card-50)
                count = max(min(2,count), count*SB_Rate/aui.freq_card); //need at least 2 for interpolation
            else if(SB_Rate > aui.freq_card+50)
                count = count*SB_Rate/aui.freq_card;
            else
                resample = FALSE;
            count = min(count, max(1,(DMA_Count)/samplesize/channels)); //max for stereo initial 1 byte
            count = min(count, max(1,(SB_Bytes-SB_Pos)/samplesize/channels)); //max for stereo initial 1 byte. 1/2channel = 0, make it 1
            if(SBEMU_GetBits()<8) //ADPCM 8bit
                count = max(1, count / (9 / SBEMU_GetBits()));
            _LOG("samples:%d %d %d, %d %d, %d %d\n", samples, pos+count, count, DMA_Count, DMA_Index, SB_Bytes, SB_Pos);
            int bytes = count * samplesize * channels;

            {
                int16_t* pcm = resample ? MAIN_PCMResample+channels*MAIN_PCM_RESAMPLE_INTERPOLATION  : MAIN_PCM + pos*2;
                if(MAIN_DMA_MappedAddr == 0) //map failed?
                    memset(pcm, 0, bytes);
                else
                    DPMI_LMemcpy(DPMI_PTR2L(pcm), MAIN_DMA_MappedAddr+(DMA_Addr-MAIN_DMA_Addr)+DMA_Index, bytes);
                if(SBEMU_GetBits()<8) //ADPCM  8bit
                    count = SBEMU_DecodeADPCM((uint8_t*)(pcm), bytes);
                if(samplesize != 2)
                    cv_bits_n_to_m(pcm, count*channels, samplesize, 2);
                if(resample/*SB_Rate != aui.freq_card*/)
                {
                    #if MAIN_PCM_RESAMPLE_INTERPOLATION
                    for(int i = 0; i < channels; ++i)
                    {
                        MAIN_PCMResample[i] = MAIN_LastResample[i]; //put last sample at beginning for interpolation
                        MAIN_LastResample[i] = *(pcm + (count-1)*channels + i); //record last sample
                    }
                    count += channels;
                    #endif
                    count = mixer_speed_lq(MAIN_PCM+pos*2, MAIN_PCM_SAMPLESIZE-pos*2, MAIN_PCMResample, count*channels, channels, SB_Rate, aui.freq_card)/channels;
                }
            }
            if(channels == 1) //should be the last step
                cv_channels_1_to_n(MAIN_PCM+pos*2, count, 2, 2);
            pos += count;
            //_LOG("samples:%d %d %d\n", count, pos, samples);
            RESETIF();
            DMA_Index = VDMA_SetIndexCounter(dma, DMA_Index+bytes, DMA_Count-bytes);
            SETIF();
            DMA_Count = VDMA_GetCounter(dma);
            SB_Pos = SBEMU_SetPos(SB_Pos+bytes);
            //_LOG("SB bytes: %d %d\n", SB_Pos, SB_Bytes);
            if(SB_Pos >= SB_Bytes)
            {
                //_LOG("INT:%d,%d,%d,%d\n",MAIN_SBBytes,SBEMU_GetSampleBytes(),MAIN_DMAIndex,DMA_Count);
                //_LOG("SBEMU: Auto: %d\n",SBEMU_GetAuto());
                if(!SBEMU_GetAuto())
                    SBEMU_Stop();
                SB_Pos = SBEMU_SetPos(0);

                MAIN_InvokeIRQ(SBEMU_GetIRQ());
                if(SB_Bytes <= 32) //detection routine?
                {
                    int c = SBEMU_GetDetectionCounter();
                    if(++c >= 256) //Miles Sound will "freeze" or crash when we continually send virtual interrupt to it, it seems it processes slow and virtual interrupt keeps happening until crash.
                        SBEMU_Stop(); //fix problem when Miles Sound using SB driver on SBPro emulation
                    SBEMU_SetDetectionCounter(c);
                    break; //fix crash in virtualbox.
                }

                SB_Bytes = SBEMU_GetSampleBytes();
                SB_Pos = SBEMU_GetPos();
                SB_Rate = SBEMU_GetSampleRate();
                //incase IRQ handler re-programs DMA
                DMA_Index = VDMA_GetIndex(dma);
                DMA_Count = VDMA_GetCounter(dma);
                DMA_Addr = VDMA_GetAddress(dma);
                //_LOG("DMACount: %d, DMAIndex:%d, DMA_Addr:%x\n",DMA_Count, DMA_Index, DMA_Addr);
            }
        } while(VDMA_GetAuto(dma) && (pos < samples) && SBEMU_HasStarted());
        //_LOG("digital end %d %d\n", samples, pos);
        //for(int i = pos; i < samples; ++i)
        //    MAIN_PCM[i*2+1] = MAIN_PCM[i*2] = 0;
        if(pos < samples && (vmpu_active || opl_active))
        {
            //The synthesizers must advance in real time. If the game's digital stream delivered fewer frames than the card
            //consumes (short or restarted DMA blocks), truncating here made the music run only as long as the SFX buffer:
            //MIDI/OPL slowed down whenever a game played digital sound. Pad the PCM part with silence instead.
            memset(MAIN_PCM + pos*2, 0, (size_t)(samples - pos) * 2 * sizeof(int16_t));
        }
        else
            samples = min(samples, pos);
    }
    else if(SBEMU_GetDirectCount()>=3)
    {
        samples = SBEMU_GetDirectCount();
        _LOG("direct out:%d %d\n",samples,aui.card_samples_per_int);
        memcpy(MAIN_PCMResample, SBEMU_GetDirectPCM8(), samples);
        SBEMU_ResetDirect();
#if 0   //fix noise for some games - SBEMU-X NOTE: unlikely to be needed
        //NOTE: it's a noise during game startup that can be ignored.
        int zeros = TRUE;
        for(int i = 0; i < samples && zeros; ++i)
        {
            if(((uint8_t*)MAIN_PCM)[i] != 0)
                zeros = FALSE;
        }
        if(zeros)
        {
            for(int i = 0; i < samples; ++i)
                ((uint8_t*)MAIN_PCM)[i] = 128;
        }
#endif
        //for(int i = 0; i < samples; ++i) _LOG("%d ",((uint8_t*)MAIN_PCM)[i]); _LOG("\n");
        cv_bits_n_to_m(MAIN_PCMResample, samples, 1, 2);
        //for(int i = 0; i < samples; ++i) _LOG("%d ",MAIN_PCM[i]); _LOG("\n");
        // the actual sample rate is derived from current count of samples in direct output buffer
        samples = mixer_speed_lq(MAIN_PCM, MAIN_PCM_SAMPLESIZE, MAIN_PCMResample, samples, 1, (samples * aui.freq_card) / aui.card_samples_per_int, aui.freq_card);
        //for(int i = 0; i < samples; ++i) _LOG("%d ",MAIN_PCM[i]); _LOG("\n");
        cv_channels_1_to_n(MAIN_PCM, samples, 2, 2);
        digital = TRUE;
    }
    else if(!opl_active && !vmpu_active)
    {
        _LOG("muted\n");
        memset(MAIN_PCM, 0, samples*sizeof(int16_t)*2); //output muted samples.
    }

    if(opl_active && !fm_aui.fm
        && !vmpu_active //NOTE: skip OPL if MPU is active
    )
    {
        int16_t* pcm = digital ? MAIN_OPLPCM : MAIN_PCM;
        OPL3EMU_GenSamples(pcm, samples); //will generate samples*2 if stereo
        if(!OPL3EMU_GetMode()) //always use 2 channels, convert opl2 samples to 2 channel
            cv_channels_1_to_n(pcm, samples, 2, SBEMU_BITS/8);

        if(digital)
        {
            for(int i = 0; i < samples*2; i+=2)
            {
                #if !SBEMU_LINEAR_MIX
                // https://stackoverflow.com/questions/12089662/mixing-16-bit-linear-pcm-streams-and-avoiding-clipping-overflow
                int la = (int)(MAIN_PCM[i] * voicevol/256) + 32768;
                int ra = (int)(MAIN_PCM[i+1] * voicevol/256) + 32768;
                #if SBEMU_SWAP_STEREO
                {int x = la; la = ra; ra = x;}
                #endif
                int lb = (int)((MAIN_OPLPCM[i]+MAIN_OPLPCM[i]*SBEMU_OPL_VOLUME_AMPLICATION/2) * oplvol/256) + 32768;
                int rb = (int)((MAIN_OPLPCM[i+1]+MAIN_OPLPCM[i+1]*SBEMU_OPL_VOLUME_AMPLICATION/2) * oplvol/256) + 32768;
                int l = (la < 32768 || lb < 32768) ? (la*lb/32768) : ((la+lb)*2 - la*lb/32768 - 65536);
                if(l == 65536) l = 65535;
                int r = (ra < 32768 || rb < 32768) ? (ra*rb/32768) : ((ra+rb)*2 - ra*rb/32768 - 65536);
                if(r == 65536) r = 65535;
                MAIN_PCM[i] = (l - 32768) * vol/256;
                MAIN_PCM[i+1] = (r - 32768) * vol/256;
                
                #else //simple average: sounds the same as DOSBox

                int la = (int)(MAIN_PCM[i] * voicevol/256); //SFX PCM
                int ra = (int)(MAIN_PCM[i+1] * voicevol/256);
                #if SBEMU_SWAP_STEREO
                /*if(channels == 2)*/ {int x = la; la = ra; ra = x;}
                #endif

                // Fix (85fc657 regression): use MAIN_OPLPCM for both terms.
                // Previously the linear mix used MAIN_PCM in the second term, which double-counted
                // the SFX signal (once as SFX via la/ra, again inside the OPL term via lb/rb),
                // causing audible artifacts (choppy SFX at medium volumes).
                // The non-linear path above already uses MAIN_OPLPCM in both terms; this aligns them.
                int lb = (int)((MAIN_OPLPCM[i]+MAIN_OPLPCM[i]*SBEMU_OPL_VOLUME_AMPLICATION/2) * oplvol/256); //OPL PCM
                int rb = (int)((MAIN_OPLPCM[i+1]+MAIN_OPLPCM[i+1]*SBEMU_OPL_VOLUME_AMPLICATION/2) * oplvol/256);
                int l = (la*SBEMU_SFX_RATIO + lb*SBEMU_OPL_RATIO) * vol/256;
                int r = (ra*SBEMU_SFX_RATIO + rb*SBEMU_OPL_RATIO) * vol/256;

                //FIX (hard clipping / crackle):
                //  The linear mix path is the only one without any kind of limiter. Whenever
                //  SFX + OPL + master volume pushed the result outside [-32768, 32767], the
                //  implicit int16_t truncation at assignment produced a hard wrap-around
                //  (audible as periodic clicks/crackle, worst on tracks with strong FM + PCM).
                //  The non-linear path above avoids this via its soft-limiter formula; this
                //  path now mirrors that safety with an explicit clamp after the master volume
                //  stage (so user-requested volume reduction still recovers headroom, but no
                //  sample can ever wrap).
                if (l >  32767) l =  32767;
                if (l < -32768) l = -32768;
                if (r >  32767) r =  32767;
                if (r < -32768) r = -32768;

                MAIN_PCM[i] = l;
                MAIN_PCM[i+1] = r;
                #endif
            }
        }
        else for(int i = 0; i < samples*2; ++i) //MAIN_PCM is opl here
            MAIN_PCM[i] = (MAIN_PCM[i] + MAIN_PCM[i]*SBEMU_OPL_VOLUME_AMPLICATION/2) * SBEMU_OPL_RATIO * oplvol/256 * vol/256;
    }
    else if(digital)
        for(int i = 0; i < samples*2; i+=2)
        {
            int16_t l = MAIN_PCM[i] * voicevol/256 * vol/256;
            int16_t r = MAIN_PCM[i+1] * voicevol/256 * vol/256;
            #if SBEMU_SWAP_STEREO
            {int x = l; l = r; r = x;}
            #endif
            MAIN_PCM[i] = l*SBEMU_SFX_RATIO;
            MAIN_PCM[i+1] = r*SBEMU_SFX_RATIO;
        }

#if SBEMU_VMPU
    if(vmpu_active)
    {
        #if SBEMU_VSC55
        //The SC-55 emulation needs milliseconds per block: interrupts must stay enabled while it runs,
        //otherwise the timer tick (and everything else) stalls. Its MIDI queue is lock-free and the
        //sound card IRQ is still in service (masked) so this code cannot be re-entered.
        VMPU_SetMixVolume(midivol * vol / 256); //honour the game's SB mixer (MIDI + master), like the OPL path does
        VMPU_GenSamples(MAIN_PCM, samples, aui.freq_card, digital);
        #else
        RESETIF();
        VMPU_GenSamples(MAIN_PCM, samples, aui.freq_card, digital);
        SETIF();
        #endif
    }
#endif

    if(CDLINK_IsActive())
        CDLINK_MixInto(MAIN_PCM, samples, aui.freq_card);

#if SBEMU_PCSPEAKER
    // Mix PC Speaker output on top of whatever is already in MAIN_PCM.
    // VPCSPEAKER_GenSamples() uses additive saturating mix (domix=1), so it is
    // safe even when the SB/OPL stream is currently silent.
    if(VPCSPEAKER_IsActive())
    {
        VPCSPEAKER_GenSamples(MAIN_PCM, samples, aui.freq_card, /*domix=*/1);
    }
#endif

    MAIN_DiagFrames = samples;
    samples *= 2; //to stereo

    aui.samplenum = samples;
    aui.pcm_sample = MAIN_PCM;
    AU_writedata(&aui);

    //_LOG("MAIN INT END\n");
}

void MAIN_TSR_InstallationCheck()
{
    for(int i = MAIN_TSR_INTSTART_ID; i <= 0xFF; ++i)
    { //detect TSR existence
        DPMI_REG r = {0};
        r.h.ah = i;
        DPMI_CallRealModeINT(MAIN_TSR_INT, &r);
        if(r.h.al == 0)
            continue;
        //printf("DOSID:%x\n", DPMI_SEGOFF2L(r.w.dx, r.w.di));
        if(DPMI_LMemcmp(DPMI_SEGOFF2L(r.w.dx, r.w.di), DPMI_PTR2L((char*)MAIN_ISR_DOSID_String), 16) == 0)
        {
            if(MAIN_Options[OPT_PADLOG].setcmd & MAIN_SETCMD_SET)
            {   /* SBEMU /PADLOG: what the loaded SBEMU picked, then nothing else */
                char* t = (char*)alloca(1024);
                r.h.ah = i;
                r.h.al = 0x03;
                r.d.ebx = DPMI_PTR2L(t);
                r.d.ecx = 1024;
                DPMI_CallRealModeINT(MAIN_TSR_INT, &r);
                if((r.w.flags & CPU_CFLAG) || r.h.al == 0x03) printf("The loaded SBEMU is older: it has no pad layouts.\n");
                else fputs(t, stdout);
                exit(0);
            }
            printf("%s is active.\n", PROGNAME);

            //note: copy array won't copy string pointers in the array, don't reference them, use string in MAIN_Options.
            //except for MAIN_SETCMD_STR, they are converted to linear addresses.
            struct MAIN_OPT* opt = (struct MAIN_OPT*)alloca(sizeof(MAIN_Options));
            r.h.ah = i;
            r.h.al = 0x01; //get current settings
            r.d.ebx = DPMI_PTR2L(opt); //dest
            r.d.ecx = sizeof(MAIN_Options); //size: in case that a different version of SBEMU is installed
            DPMI_CallRealModeINT(MAIN_TSR_INT, &r);
            if((r.w.flags&CPU_CFLAG))
            {
                MAIN_CPrintf(RED, "Failed to get SBEMU TSR information.\n");
                exit(-1);
            }

            for(int j = 0; j < OPT_COUNT; ++j)
            {
                if(!(MAIN_Options[j].setcmd&MAIN_SETCMD_SET) || (MAIN_Options[j].setcmd&MAIN_SETCMD_HIDDEN))
                    continue;
                if((MAIN_Options[j].setcmd&MAIN_SETCMD_STR) && 
                    ( (MAIN_Options[j].setcmd&MAIN_SETCMD_STRCS) && DPMI_LStrcmp(opt[j].value, DPMI_PTR2L((char*)MAIN_Options[j].value)) != 0
                        || DPMI_LStricmp(opt[j].value, DPMI_PTR2L((char*)MAIN_Options[j].value)) != 0 ) )
                {
                    uint32_t len = DPMI_LStrlen(opt[j].value);
                    char* buf = alloca(len+1);
                    DPMI_LMemcpy(DPMI_PTR2L(buf), opt[j].value, len+1);
                    MAIN_CPrintf(WHITE, "%8s", MAIN_Options[j].option); MAIN_CPrintf(LIGHTGRAY, " changed from ");
                    MAIN_CPrintf(RED, "%s", buf); MAIN_CPrintf(LIGHTGRAY, " to "); MAIN_CPrintf(LIGHTGREEN, "%s\n", (const char*)MAIN_Options[j].value);
                    opt[j].value = DPMI_PTR2L((char*)MAIN_Options[j].value);
                }
                else if(!(MAIN_Options[j].setcmd&MAIN_SETCMD_STR) && MAIN_Options[j].value != opt[j].value)
                {
                    int radix10 = MAIN_Options[j].setcmd&MAIN_SETCMD_BASE10;
                    MAIN_CPrintf(WHITE, "%8s", MAIN_Options[j].option); MAIN_CPrintf(LIGHTGRAY, " changed from ");
                    MAIN_CPrintf(RED, radix10 ? "%d" : "%x", opt[j].value); MAIN_CPrintf(LIGHTGRAY, " to ");
                    MAIN_CPrintf(LIGHTGREEN, radix10 ? "%d\n" : "%x\n", MAIN_Options[j].value);
                    opt[j].value = MAIN_Options[j].value;
                }
            }

#if SBEMU_VMPU //special handling for VMPU. the TSR has freed its RM translation buffer so it cannot do IO in PM.
            opt[OPT_VMPU_SFBUF].value = 0;
            #if SBEMU_VSC55
            if((MAIN_Options[OPT_VMPU_SF].setcmd&MAIN_SETCMD_SET) || (MAIN_Options[OPT_VSC_SET].setcmd&MAIN_SETCMD_SET) || MAIN_USE_SF)
                MAIN_CPrintf(YELLOW, "Note: the SC-55 ROM set or SoundFont is loaded when SBEMU first starts; restart the PC (SBEMU cannot be unloaded) to change it.\n");
            #else
            if(MAIN_Options[OPT_VMPU_VOICES].value && MAIN_Options[OPT_MPUADDR].value)
            {
                char path[_MAX_PATH];
                const char* sf = (const char*)MAIN_Options[OPT_VMPU_SF].value;
                uint32_t len = 0;
                void* sf_data = load_file(get_abs_path(path, sizeof(path), sf), sizeof(uint32_t), &len); //freed on exit
                if(!sf_data)
                {
                    MAIN_CPrintf(RED, "Failed to load Soundfont file to memory: %s\n", path); //OOM? should be streaming but unable to do that.
                    exit(-1);
                }
                *(uint32_t*)sf_data = len;
                opt[OPT_VMPU_SFBUF].value = DPMI_PTR2L(sf_data);
            }
            #endif //SBEMU_VSC55
#endif

            if(MAIN_Options[OPT_RESET].value)
                printf("Resetting sound card driver...\n");
            printf("\n");

            if((MAIN_Options[OPT_ADDR].setcmd&MAIN_SETCMD_SET) || (MAIN_Options[OPT_IRQ].setcmd&MAIN_SETCMD_SET) || (MAIN_Options[OPT_DMA].setcmd&MAIN_SETCMD_SET) || (MAIN_Options[OPT_TYPE].setcmd&MAIN_SETCMD_SET) || (MAIN_Options[OPT_HDMA].setcmd&MAIN_SETCMD_SET))
                MAIN_SetBlasterEnv(opt);

            r.h.ah = i;
            r.h.al = 0x02; //set new settings
            r.d.ebx = DPMI_PTR2L(opt);
            r.d.ecx = sizeof(MAIN_Options);
            DPMI_CallRealModeINT(MAIN_TSR_INT, &r);
            if((r.w.flags&CPU_CFLAG))
            {
                MAIN_CPrintf(RED, "Failed to update settings to SBEMU TSR.\n");
                exit(-1);
            }

            r.h.ah = i;
            r.h.al = 0x01; //read back to confirm
            r.d.ebx = DPMI_PTR2L(opt);
            r.d.ecx = sizeof(MAIN_Options);
            DPMI_CallRealModeINT(MAIN_TSR_INT, &r);
            if((r.w.flags&CPU_CFLAG))
            {
                MAIN_CPrintf(RED, "Failed to get SBEMU TSR information.\n");
                exit(-1);
            }

            printf("Current settings:\n");
            for(int i = OPT_Help+1; i < OPT_COUNT; ++i)
            {
                if(!(MAIN_Options[i].setcmd&MAIN_SETCMD_HIDDEN))
                {
                    if((MAIN_Options[i].setcmd&MAIN_SETCMD_STR))
                    {
                        uint32_t len = DPMI_LStrlen(opt[i].value);
                        char* buf = alloca(len+1);
                        DPMI_LMemcpy(DPMI_PTR2L(buf), opt[i].value, len+1);
                        printf("%-8s: %s\n", MAIN_Options[i].option, buf);
                    }
                    else
                        printf( (MAIN_Options[i].setcmd&MAIN_SETCMD_BASE10) ? "%-8s: %d\n":"%-8s: %x\n", MAIN_Options[i].option, opt[i].value);
                }
            }
            exit(0);
        }
    }
}

static void MAIN_TSR_Interrupt()
{
    if(MAIN_TSRREG.h.ah != MAIN_TSR_INT_FNO)
    {
        //DBG_Log("chain %x ",MAIN_TSRREG.d.eax);
        //DBG_DumpREG(&MAIN_TSRREG);
        DPMI_CallRealModeOldISR(&MAIN_TSRIntHandle, &MAIN_TSRREG);
        return;
    }
    //DBG_Log("call tsr\n");
    switch(MAIN_TSRREG.h.al)
    {
        case 0x00:  //AMIS installation check
            MAIN_TSRREG.h.al = 0xFF;
            MAIN_TSRREG.w.cx = 0x0100; //bcd version
            MAIN_TSRREG.w.dx = (MAIN_ISR_DOSID&0xFFFF); //a real mode string buffer of identification: segment
            MAIN_TSRREG.w.di = 0; //offset
        return;
        case 0x03: //pad layout log (SBEMU /PADLOG): text into EBX, at most ECX bytes
        {
            char t[8 * 104 + 120];
            int i, n;
            if(!MAIN_PadOn) n = snprintf(t, sizeof t, "Pad layouts per game are off (no PADMAP.TXT / PADUSER.TXT, /PADKEYS:0 or /JOY:0).\r\n");
            else
            {
                n = snprintf(t, sizeof t, "Pad layout now: %s. Programs started: %lu, running: %d.\r\n", PADMAP_Title(PADMAP_Current()),
                    (unsigned long)MAIN_PadExecs, MAIN_PadDepth);
                for(i = 0; i < MAIN_PadLogN && n < (int)sizeof t - 1; ++i)
                    n += snprintf(t + n, sizeof t - n, "  %s\r\n", MAIN_PadLog[i]);
            }
            if(n > (int)sizeof t - 1) n = sizeof t - 1;
            if(MAIN_TSRREG.d.ecx < (uint32_t)n + 1) n = MAIN_TSRREG.d.ecx ? (int)MAIN_TSRREG.d.ecx - 1 : 0;
            t[n] = 0;
            if(MAIN_TSRREG.d.ecx) DPMI_LMemcpy(MAIN_TSRREG.d.ebx, DPMI_PTR2L(t), n + 1);
            MAIN_TSRREG.d.ecx = n;
            MAIN_TSRREG.h.al = 0;                       /* an older SBEMU leaves AL as it was */
            MAIN_TSRREG.w.flags &= ~CPU_CFLAG;
        }
        return;
        case 0x01: //query
        {
            if(MAIN_TSRREG.d.ecx != sizeof(MAIN_Options)) //different version of SBEMU?
            {
                MAIN_TSRREG.w.flags |= CPU_CFLAG;
                return;
            }
            MAIN_TSRREG.w.flags &= ~CPU_CFLAG;
            //convert string to linear addr, for the SBEMU client to access
            for(int i = 0; i < countof(MAIN_Options); ++i)
            {
                if((MAIN_Options[i].setcmd&MAIN_SETCMD_STR))
                    MAIN_Options[i].value = DPMI_PTR2L((char*)MAIN_Options[i].value);
            }
            DPMI_LMemcpy(MAIN_TSRREG.d.ebx, DPMI_PTR2L(MAIN_Options), sizeof(MAIN_Options));

            for(int i = 0; i < countof(MAIN_Options); ++i) //convert back
            {
                if((MAIN_Options[i].setcmd&MAIN_SETCMD_STR))
                    MAIN_Options[i].value = (uintptr_t)DPMI_L2PTR(MAIN_Options[i].value);
            }
        }
        return;
        case 0x02: //set
        {
            if(MAIN_TSRREG.d.ecx != sizeof(MAIN_Options)) //different version of SBEMU?
            {
                MAIN_TSRREG.w.flags |= CPU_CFLAG;
                return;
            }
            MAIN_TSRREG.w.flags &= ~CPU_CFLAG;

            struct MAIN_OPT* _opt = (struct MAIN_OPT*)alloca(sizeof(MAIN_Options));
            DPMI_LMemcpy(DPMI_PTR2L(_opt), MAIN_TSRREG.d.ebx, sizeof(MAIN_Options));

            for(int i = 0; i < countof(MAIN_Options); ++i)
            {
                if(!(MAIN_Options[i].setcmd&MAIN_SETCMD_STR))
                {
                    MAIN_Options[i].setcmd |= (MAIN_Options[i].value != _opt[i].value) ? MAIN_SETCMD_CHGD : 0;
                    swapi(MAIN_Options[i].value, _opt[i].value);
                    continue;
                }

                if((MAIN_Options[i].setcmd&MAIN_SETCMD_STRCS) && DPMI_LStrcmp(_opt[i].value, DPMI_PTR2L((char*)MAIN_Options[i].value)) == 0)
                    continue;
                if(!(MAIN_Options[i].setcmd&MAIN_SETCMD_STRCS) && DPMI_LStricmp(_opt[i].value, DPMI_PTR2L((char*)MAIN_Options[i].value)) == 0)
                    continue;

                uint32_t len = DPMI_LStrlen(_opt[i].value);
                char* str = malloc(len+1);
                DPMI_LMemcpy(DPMI_PTR2L(str), _opt[i].value, len+1);
                if((MAIN_Options[i].setcmd&MAIN_SETCMD_STRCPY))
                    free((void*)MAIN_Options[i].value);
                MAIN_Options[i].value = (uintptr_t)str;
                MAIN_Options[i].setcmd |= MAIN_SETCMD_STRCPY|MAIN_SETCMD_CHGD;
            }

            char* fpustate = (char*)alloca(108);
            #ifdef DJGPP //make vscode happy
            asm("fsave %0\n\t finit":"=m"(*fpustate));
            #endif
            int irq = aui.card_irq;
            PIC_MaskIRQ(irq);

            struct MAIN_OPT* old_opt = _opt; //readibility. value swapped.
#if MPU_DEBUG
            mpu_debug = MAIN_Options[OPT_MDBG].value;
#endif
            if(OPT_CHANGED(OPT_DEBUG_OUTPUT))
            {
                update_serial_debug_output();
            }
            if(OPT_CHANGED(OPT_MPUCOMPORT))
            {
                unsigned int oldval = old_opt[OPT_MPUCOMPORT].value;
                update_serial_mpu_output();
                MAIN_Options[OPT_MPUCOMPORT].value = oldval; //TODO: don't know why keep the old val.
            }

            if(OPT_CHANGED(OPT_OUTPUT) || OPT_CHANGED(OPT_RATE) || MAIN_Options[OPT_RESET].value)
            {
                if(OPT_CHANGED(OPT_OUTPUT) || MAIN_Options[OPT_RESET].value)
                {
                    _LOG("Reset\n");
                    AU_close(&aui, &fm_aui, &mpu401_aui);
                    memset(&aui, 0, sizeof(aui));
                    memset(&fm_aui, 0, sizeof(fm_aui));
                    memset(&mpu401_aui, 0, sizeof(mpu401_aui));
                    aui.card_select_config = MAIN_Options[OPT_OUTPUT].value;
                    aui.card_select_index =  MAIN_Options[OPT_SC].value;
                    aui.card_controlbits |= AUINFOS_CARDCNTRLBIT_SILENT; //don't print anything in interrupt
                    AU_init(&aui, &fm_aui, &mpu401_aui);
                    AU_ini_interrupts(&aui);
                    AU_setmixer_init(&aui);
                    AU_setmixer_outs(&aui, MIXER_SETMODE_ABSOLUTE, 100);
                    MAIN_Options[OPT_VOL].setcmd |= MAIN_SETCMD_CHGD;   //mark volume dirty
                }
                _LOG("Change sample rate\n");
                _LOG("FLAGS:%x\n",CPU_FLAGS());

                int samplerate = MAIN_Options[OPT_RATE].value;
                mpxplay_audio_decoder_info_s adi = {NULL, 0, 1, samplerate, SBEMU_CHANNELS, SBEMU_CHANNELS, NULL, SBEMU_BITS, SBEMU_BITS/8, 0};
                AU_setrate(&aui, &adi);
                if(OPT_CHANGED(OPT_RATE))
                    OPL3EMU_Init(aui.freq_card);
                AU_prestart(&aui); //setsamplerate/reset will do stop
                AU_start(&aui);
            }
            if(OPT_CHANGED(OPT_VOL))
            {
                _LOG("Reset volume\n");
                AU_setmixer_one(&aui, AU_MIXCHAN_MASTER, MIXER_SETMODE_ABSOLUTE, MAIN_Options[OPT_VOL].value);
            }
            #ifdef DJGPP //make vscode happy
            asm("frstor %0" ::"m"(*fpustate));
            #endif
            PIC_UnmaskIRQ(irq);

            if(OPT_CHANGED(OPT_DMA))
            {
                _LOG("Change DMA\n");
                VDMA_Virtualize(old_opt[OPT_DMA].value, FALSE);
                VDMA_Virtualize(MAIN_Options[OPT_DMA].value, TRUE);
            }
            if((OPT_CHANGED(OPT_JOYMODE) || OPT_CHANGED(OPT_JOYDZ)) && MAIN_Options[OPT_JOY].value
                && old_opt[OPT_JOYMODE].value != 4 && MAIN_Options[OPT_JOYMODE].value != 4)
                VJOY_SetLive(MAIN_Options[OPT_JOYDZ].value, MAIN_Options[OPT_JOYMODE].value);
            if(OPT_CHANGED(OPT_PADKEYS) && MAIN_PadOn)   /* layouts were loaded at start: just on / off */
            {
                DPMI_StoreB(MAIN_I21Lin + 8, MAIN_Options[OPT_PADKEYS].value ? 1 : 0);
                VJOY_PadKeys(MAIN_Options[OPT_PADKEYS].value ? 1 : 0, -1);
            }
            if(OPT_CHANGED(OPT_FMCORE))
            {
                OPL3EMU_SetCore(MAIN_Options[OPT_FMCORE].value);
                OPL3EMU_Init(aui.freq_card); //static Nuked chip: no allocation needed while resident
            }
            #if SBEMU_VSC55
            if(OPT_CHANGED(OPT_VMIDIVOL))
                VMPU_SetMidiVolume(MAIN_Options[OPT_VMIDIVOL].value);
            if(OPT_CHANGED(OPT_VSC_GAIN) || OPT_CHANGED(OPT_VMIDIVOL))
                VMPU_SetSC55Options(NULL, MAIN_Options[OPT_VSC_GAIN].value);
            if(OPT_CHANGED(OPT_VSC_AHEAD))
                VMPU_SetAhead(MAIN_Options[OPT_VSC_AHEAD].value);
            if(OPT_CHANGED(OPT_VSC_AUTO))
                VMPU_SetSC55Auto(MAIN_Options[OPT_VSC_AUTO].value);
            if(OPT_CHANGED(OPT_VSC_VOICES) || OPT_CHANGED(OPT_VSC_FX))
                VMPU_SetSC55Lite(MAIN_Options[OPT_VSC_FX].value, MAIN_Options[OPT_VSC_VOICES].value);
            #endif
            if(OPT_CHANGED(OPT_HDMA))
            {
                _LOG("Change HDMA\n");
                VDMA_Virtualize(old_opt[OPT_HDMA].value, FALSE);
                VDMA_Virtualize(MAIN_Options[OPT_HDMA].value, TRUE);
            }
            if(OPT_CHANGED(OPT_DMA) || OPT_CHANGED(OPT_HDMA) || OPT_CHANGED(OPT_IRQ) || OPT_CHANGED(OPT_TYPE) || OPT_CHANGED(OPT_FIX_TC))
            {
                _LOG("Reinit SBEMU\n");
                HDPMIPT_LockIRQRouting(FALSE);
                IRQGUARD_Install(MAIN_Options[OPT_IRQ].value);
                HDPMIPT_LockIRQRouting(TRUE);
                SBEMU_Init(
                    MAIN_Options[OPT_IRQ].value,
                    MAIN_Options[OPT_DMA].value,
                    MAIN_Options[OPT_HDMA].value,
                    MAIN_SB_DSPVersion[MAIN_Options[OPT_TYPE].value],
                    MAIN_Options[OPT_FIX_TC].value,
                    &MAIN_SbemuExtFun);
            }

            do
            {
                if(!OPT_CHANGED(OPT_OPL) && OPT_CHANGED(OPT_ADDR) && !OPT_CHANGED(OPT_PM) && !OPT_CHANGED(OPT_RM) && !OPT_CHANGED(OPT_FIX_TC) &&
                !OPT_CHANGED(OPT_MPUADDR) && OPT_CHANGED(OPT_MPUCOMPORT)
    #if SBEMU_VMPU
                    && !OPT_CHANGED(OPT_VMPU_VOICES) && (!VMPU_ENABLED || !OPT_CHANGED(OPT_VMPU_SF))
    #endif
                )
                {
                    break;
                }

                //re-install all
                if(MAIN_Options[OPT_RM].value)
                {
                    _LOG("uninstall qemm\n");
                    if(MAIN_Options[OPT_OPL].value && OPLRMInstalled) QEMM_Uninstall_IOPortTrap(&OPL3IOPT);
                    if(MAIN_Options[OPT_MPUADDR].value && MAIN_Options[OPT_MPUCOMPORT].value) QEMM_Uninstall_IOPortTrap(&MPUIOPT);
                    QEMM_Uninstall_IOPortTrap(&MAIN_VDMA_IOPT);
                    #if !MAIN_TRAP_RMPIC_ONDEMAND
                    QEMM_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT);
                    #endif
                    QEMM_Uninstall_IOPortTrap(&MAIN_SB_IOPT);
                }
                if(MAIN_Options[OPT_PM].value)
                {
                    _LOG("uninstall hdpmi\n");
                    if(MAIN_Options[OPT_OPL].value && OPLPMInstalled) HDPMIPT_Uninstall_IOPortTrap(&OPL3IOPT_PM);
                    if(MAIN_Options[OPT_MPUADDR].value && MAIN_Options[OPT_MPUCOMPORT].value) HDPMIPT_Uninstall_IOPortTrap(&MPUIOPT_PM);
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_VDMA_IOPT_PM1);
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_VDMA_IOPT_PM2);
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_VDMA_IOPT_PM3);
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_VHDMA_IOPT_PM1);
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_VHDMA_IOPT_PM2);
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_VHDMA_IOPT_PM3);
                    #if !MAIN_TRAP_PMPIC_ONDEMAND
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT_PM1);
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_VIRQ_IOPT_PM2);
                    #endif
                    HDPMIPT_Uninstall_IOPortTrap(&MAIN_SB_IOPT_PM);
                }
                MAIN_Options[OPT_PM].value = MAIN_Options[OPT_PM].value && MAIN_HDPMI_Present;
                MAIN_Options[OPT_RM].value = MAIN_Options[OPT_RM].value && MAIN_QEMM_Present;

                if(MAIN_Options[OPT_OPL].value && !(fm_aui.fm && fm_aui.fm_port == 0x388))
                {
                    _LOG("install opl\n");
                    QEMM_IODT *iodt = fm_aui.fm ? MAIN_HW_OPL3IODT : MAIN_OPL3IODT;
                    if(MAIN_Options[OPT_RM].value) QEMM_Install_IOPortTrap(iodt, 4, &OPL3IOPT);
                    if(MAIN_Options[OPT_PM].value) HDPMIPT_Install_IOPortTrap(0x388, 0x38B, iodt, 4, &OPL3IOPT_PM);
                }

                if(MAIN_Options[OPT_MPUADDR].value && (MAIN_Options[OPT_MPUCOMPORT].value || VMPU_ENABLED) )
                {
                    _LOG("install mpu\n");
#if SBEMU_VMPU
                    if(VMPU_ENABLED) //NOTE: OPT_VMPU_SF maybe not changed when changed previously but not enabled
                    {
                        #if MAIN_VMPU_HDPMI_MEMFIX
                        MAIN_VMPU_FREEMEM();
                        #endif

                        #if SBEMU_VSC55
                        if(!VMPU_Reset(MAIN_Options[OPT_MPUADDR].value, &MAIN_Options[OPT_VMPU_VOICES].value, aui.freq_card, NULL, 0, 0) )
                        {
                            //the ROMs can only be loaded by the first instance, before it goes resident
                            MAIN_CPrintf(YELLOW, "VMPU warning: the SC-55 emulation was not loaded when SBEMU started. Restart the PC and start SBEMU again with /VMPU.\n");
                        }
                        #else
                        uint32_t size = DPMI_LoadD(MAIN_Options[OPT_VMPU_SFBUF].value);
                        if(!VMPU_Reset(MAIN_Options[OPT_MPUADDR].value, &MAIN_Options[OPT_VMPU_VOICES].value, aui.freq_card, 
                            (const char*)MAIN_Options[OPT_VMPU_SF].value, MAIN_Options[OPT_VMPU_SFBUF].value+sizeof(size), size) )
                        {
                            MAIN_CPrintf(YELLOW, "VMPU warning: setup failed with SF: %s, functions disabled.\n", (const char*)MAIN_Options[OPT_VMPU_SF].value);
                            #if MAIN_VMPU_HDPMI_MEMFIX
                            MAIN_VMPU_ALLOCMEM();
                            #endif
                        }
                        #endif
                        MAIN_Options[OPT_VMPU_SFBUF].value = 0;
                        //always set
                        MAIN_MPUIODT[0].handler = &VMPU_MPU; //330
                        MAIN_MPUIODT[1].handler = &VMPU_MPU; //331
                    }
                    else
                    {
                        MAIN_MPUIODT[0].handler = &MAIN_MPU_330; //330
                        MAIN_MPUIODT[1].handler = &MAIN_MPU_331; //331
                    }
#endif
                    for(int i = 0; i < countof(MAIN_MPUIODT); ++i) MAIN_MPUIODT[i].port = MAIN_Options[OPT_MPUADDR].value+i;
                    if(MAIN_Options[OPT_RM].value) QEMM_Install_IOPortTrap(MAIN_MPUIODT, 2, &MPUIOPT);
                    if(MAIN_Options[OPT_PM].value) HDPMIPT_Install_IOPortTrap(MAIN_Options[OPT_MPUADDR].value, MAIN_Options[OPT_MPUADDR].value+1, MAIN_MPUIODT, 2, &MPUIOPT_PM);
                }

                QEMM_IODT* SB_Iodt = MAIN_Options[OPT_OPL].value ? MAIN_SB_IODT : MAIN_SB_IODT+4;
                int SB_IodtCount = MAIN_Options[OPT_OPL].value ? countof(MAIN_SB_IODT) : countof(MAIN_SB_IODT)-4;
                if(OPT_CHANGED(OPT_ADDR))
                {
                    for(int i = 0; i < countof(MAIN_SB_IODT); ++i)
                        MAIN_SB_IODT[i].port = MAIN_SB_IODT[i].port - old_opt[OPT_ADDR].value + MAIN_Options[OPT_ADDR].value;
                }

                if(MAIN_Options[OPT_RM].value)
                {
                    _LOG("install qemm\n");
                    QEMM_Install_IOPortTrap(MAIN_VDMA_IODT, countof(MAIN_VDMA_IODT), &MAIN_VDMA_IOPT);
                    QEMM_Install_IOPortTrap(SB_Iodt, SB_IodtCount, &MAIN_SB_IOPT);
                    #if !MAIN_TRAP_RMPIC_ONDEMAND
                    QEMM_Install_IOPortTrap(MAIN_VIRQ_IODT, countof(MAIN_VIRQ_IODT), &MAIN_VIRQ_IOPT);
                    #endif
                }

                if(MAIN_Options[OPT_PM].value)
                {
                    _LOG("install hdpmi\n");
                    HDPMIPT_Install_IOPortTrap(0x0, 0xF, MAIN_VDMA_IODT, 16, &MAIN_VDMA_IOPT_PM1);
                    HDPMIPT_Install_IOPortTrap(0x81, 0x83, MAIN_VDMA_IODT+16, 3, &MAIN_VDMA_IOPT_PM2);
                    HDPMIPT_Install_IOPortTrap(0x87, 0x87, MAIN_VDMA_IODT+19, 1, &MAIN_VDMA_IOPT_PM3);
                    HDPMIPT_Install_IOPortTrap(0xC0, 0xDE, MAIN_VDMA_IODT+20, 16, &MAIN_VHDMA_IOPT_PM1);
                    HDPMIPT_Install_IOPortTrap(0x89, 0x8B, MAIN_VDMA_IODT+36, 3, &MAIN_VHDMA_IOPT_PM2);
                    HDPMIPT_Install_IOPortTrap(0x8F, 0x8F, MAIN_VDMA_IODT+39, 1, &MAIN_VHDMA_IOPT_PM3);
                    #if !MAIN_TRAP_PMPIC_ONDEMAND
                    HDPMIPT_Install_IOPortTrap(0x20, 0x21, MAIN_VIRQ_IODT, 2, &MAIN_VIRQ_IOPT_PM1);
                    HDPMIPT_Install_IOPortTrap(0xA0, 0xA1, MAIN_VIRQ_IODT+2, 2, &MAIN_VIRQ_IOPT_PM2);
                    #endif
                    HDPMIPT_Install_IOPortTrap(MAIN_Options[OPT_ADDR].value, MAIN_Options[OPT_ADDR].value+0x0F, SB_Iodt, SB_IodtCount, &MAIN_SB_IOPT_PM);
                }

                if(MAIN_Options[OPT_RM].value)
                {
                    UntrappedIO_OUT_Handler = &QEMM_UntrappedIO_Write;
                    UntrappedIO_IN_Handler = &QEMM_UntrappedIO_Read;
                }
                else
                {
                    UntrappedIO_OUT_Handler = &HDPMIPT_UntrappedIO_Write;
                    UntrappedIO_IN_Handler = &HDPMIPT_UntrappedIO_Read;
                }
            } while(0);

            //cleanup changed states for all
            for(int i = 0; i < countof(MAIN_Options); ++i)
                MAIN_Options[i].setcmd &= ~MAIN_SETCMD_CHGD;
        }
        return;
    }
}
