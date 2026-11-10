#include "opl3emu.h"
#include "dbopl.h"
extern "C"
{
#include "opl3.h" //Nuked-OPL3-fast (from the VOPL3 project, LGPL-2.1+)
}
#include <string.h>
#include <new>

#define OPL3EMU_PRIMARY 0
#define OPL3EMU_SECONDARY 1
//primary index read
#define OPL3EMU_TIMER_REG_INDEX 4
#define OPL3EMU_TIMER1_MASK 0xC0
#define OPL3EMU_TIMER2_MASK 0xA0
#define OPL3EMU_TIMER1_START 0x01
#define OPL3EMU_TIMER2_START 0x02
#define OPL3EMU_TIMER1_TIMEOUT OPL3EMU_TIMER1_MASK
#define OPL3EMU_TIMER2_TIMEOUT OPL3EMU_TIMER2_MASK
static uint32_t OPL3EMU_TimerCtrlReg[2]; //if start 1 and 2 seperately we will miss one, so use 2 cache
static uint32_t OPL3EMU_IndexReg[2];

//secondary index read (Adlib Gold). reference: AIL2.0 source code, dosbox
#define OPL3EMU_ADLG_IOBUSY 0x40UL
#define OPL3EMU_ADLG_VOLL_REG_INDEX 9 //left volume
#define OPL3EMU_ADLG_VOLR_REG_INDEX 10 //right volume
//data
#define KEY_ON 0x10 //channel on bit
static uint32_t OPL3EMU_ADLG_CtrlEnable = 0;    //seems not working for Miles Sound, don't use it
static uint32_t OPL3EMU_ADLG_Volume[2] = {0x08,0x08};

static DBOPL::Chip* OPL3EMU_Chip;

//Alternative FM core: Nuked OPL3 (cycle accurate YMF262), in the bit-exact "fast" build used by VOPL3.
//Static storage on purpose: it can be (re)started while resident without allocating memory.
#define OPL3EMU_CORE_DBOPL 0
#define OPL3EMU_CORE_NUKED 1
#define OPL3EMU_NUKED_TAIL_SECONDS 3 //keep rendering this long after the last note event (release tails)
static opl3_chip OPL3EMU_Nuked;
static int OPL3EMU_Core = OPL3EMU_CORE_DBOPL;
static int OPL3EMU_Rate = 44100;
static uint32_t OPL3EMU_NukedKeyOn;  //bit per melodic channel (0-8 first bank, 9-17 second bank), bit 18: rhythm drums
static int32_t OPL3EMU_NukedTail;    //output frames left to render after the last key event
//CPU: a game can leave voices keyed on at zero volume (silent) for as long as it runs; the core then ran
//without end. After 2 s of output that is exactly silent the core sleeps until the next register write.
static int32_t OPL3EMU_NukedSilent;  //frames of exact silence in a row
static int OPL3EMU_NukedSleep;
#define OPL3EMU_NUKED_SLEEP_SECONDS 2

void OPL3EMU_SetCore(int core)
{
    OPL3EMU_Core = core == OPL3EMU_CORE_NUKED ? OPL3EMU_CORE_NUKED : OPL3EMU_CORE_DBOPL;
}

void OPL3EMU_Init(int samplerate)
{
    OPL3EMU_Rate = samplerate;
    //Allocate once and rebuild in place afterwards: Init is also called while resident (rate / core changes),
    //and allocating after going TSR is unsafe under HDPMI.
    if(OPL3EMU_Chip)
    {
        OPL3EMU_Chip->~Chip();
        new(OPL3EMU_Chip) DBOPL::Chip(true);
    }
    else
        OPL3EMU_Chip = new DBOPL::Chip(true);
    OPL3EMU_Chip->Setup(samplerate);

    memset(&OPL3EMU_Nuked, 0, sizeof(OPL3EMU_Nuked));
    OPL3_Reset(&OPL3EMU_Nuked, (uint32_t)samplerate);
    OPL3EMU_NukedKeyOn = 0;
    OPL3EMU_NukedTail = 0;
    OPL3EMU_NukedSilent = 0;
    OPL3EMU_NukedSleep = 0;
}

int OPL3EMU_IsActive()
{
    if(OPL3EMU_Core == OPL3EMU_CORE_NUKED)
        return !OPL3EMU_NukedSleep && (OPL3EMU_NukedKeyOn != 0 || OPL3EMU_NukedTail > 0);
    return (OPL3EMU_Chip->oplActive&(OPL3EMU_Chip->opl3Active?0xFFFFFFFF:0x8000FFFF));
}

int OPL3EMU_GetMode()
{
    if(OPL3EMU_Core == OPL3EMU_CORE_NUKED)
        return 1; //Nuked always renders stereo frames, no OPL2 mono expansion needed
    return OPL3EMU_Chip->opl3Active;
}

int OPL3EMU_GenSamples(int16_t* pcm16, int count)
{
    if(OPL3EMU_Core == OPL3EMU_CORE_NUKED)
    {
        OPL3_GenerateStream(&OPL3EMU_Nuked, pcm16, (uint32_t)count);
        OPL3EMU_NukedTail = OPL3EMU_NukedTail > count ? OPL3EMU_NukedTail - count : 0;
        {
            int i, quiet = 1;
            for(i = 0; i < count * 2; ++i) if(pcm16[i]) { quiet = 0; break; }
            OPL3EMU_NukedSilent = quiet ? OPL3EMU_NukedSilent + count : 0;
            if(OPL3EMU_NukedSilent > OPL3EMU_Rate * OPL3EMU_NUKED_SLEEP_SECONDS) OPL3EMU_NukedSleep = 1;
        }
        return count;
    }
    return OPL3EMU_Chip->Generate(pcm16, count);
}

//Register-bank selection exactly like DBOPL::Chip::WriteAddr: the second bank is only
//reachable once OPL3 mode ("NEW", register 0x105) is on - except for register 0x05 itself.
static uint32_t OPL3EMU_WriteAddr(uint32_t port, uint32_t val)
{
    if(OPL3EMU_Core == OPL3EMU_CORE_DBOPL)
        return OPL3EMU_Chip->WriteAddr(port, val);
    switch(port & 3)
    {
    case 0:
        return val;
    case 2:
        return (OPL3EMU_Nuked.newm || val == 0x05u) ? (0x100u | val) : val;
    }
    return 0u;
}

static void OPL3EMU_WriteReg(uint32_t index, uint32_t val)
{
    if(OPL3EMU_Core == OPL3EMU_CORE_DBOPL)
    {
        OPL3EMU_Chip->WriteReg(index, val);
        return;
    }
    //remember which voices are keyed on so the (heavier) core only runs while there is sound
    const uint32_t reg = index & 0xFF;
    const bool second = (index & 0x100) != 0;
    if(reg >= 0xB0 && reg <= 0xB8)
    {
        const uint32_t bit = 1u << ((reg - 0xB0) + (second ? 9 : 0));
        OPL3EMU_NukedKeyOn = (val & 0x20) ? (OPL3EMU_NukedKeyOn | bit) : (OPL3EMU_NukedKeyOn & ~bit);
        OPL3EMU_NukedTail = OPL3EMU_Rate * OPL3EMU_NUKED_TAIL_SECONDS;
    }
    else if(!second && reg == 0xBD)
    {
        const uint32_t bit = 1u << 18;
        const bool drums = (val & 0x20) && (val & 0x1F);
        OPL3EMU_NukedKeyOn = drums ? (OPL3EMU_NukedKeyOn | bit) : (OPL3EMU_NukedKeyOn & ~bit);
        OPL3EMU_NukedTail = OPL3EMU_Rate * OPL3EMU_NUKED_TAIL_SECONDS;
    }
    //buffered write: keeps the relative timing of writes made between two audio blocks
    OPL3_WriteRegBuffered(&OPL3EMU_Nuked, (uint16_t)index, (uint8_t)val);
    OPL3EMU_NukedSleep = 0; OPL3EMU_NukedSilent = 0;   //anything written can make sound again
}

uint32_t OPL3EMU_PrimaryRead(uint32_t val)
{
    val &= ~0xFF;
    if ((OPL3EMU_TimerCtrlReg[0] & (OPL3EMU_TIMER1_MASK|OPL3EMU_TIMER1_START)) == OPL3EMU_TIMER1_START)
        val |= OPL3EMU_TIMER1_TIMEOUT;
    if ((OPL3EMU_TimerCtrlReg[1] & (OPL3EMU_TIMER2_MASK|OPL3EMU_TIMER2_START)) == OPL3EMU_TIMER2_START)
        val |= OPL3EMU_TIMER2_TIMEOUT;
    return val;
}

uint32_t OPL3EMU_PrimaryWriteIndex(uint32_t val)
{
    OPL3EMU_IndexReg[OPL3EMU_PRIMARY] = OPL3EMU_WriteAddr(0x388, val);
    return val;
}

uint32_t OPL3EMU_PrimaryWriteData(uint32_t val)
{
    if(OPL3EMU_IndexReg[OPL3EMU_PRIMARY] == OPL3EMU_TIMER_REG_INDEX)
    {
        if(val&(OPL3EMU_TIMER1_START|OPL3EMU_TIMER1_MASK))
            OPL3EMU_TimerCtrlReg[0] = val;
        if(val&(OPL3EMU_TIMER2_START|OPL3EMU_TIMER2_MASK))
            OPL3EMU_TimerCtrlReg[1] = val;
    }
    OPL3EMU_WriteReg(OPL3EMU_IndexReg[OPL3EMU_PRIMARY], val);
    return val;
}

uint32_t OPL3EMU_SecondaryRead(uint32_t val)
{
    if(OPL3EMU_IndexReg[OPL3EMU_SECONDARY] == 0x100+OPL3EMU_ADLG_VOLL_REG_INDEX || OPL3EMU_IndexReg[OPL3EMU_SECONDARY] == 0x100+OPL3EMU_ADLG_VOLR_REG_INDEX)
            return OPL3EMU_ADLG_Volume[OPL3EMU_IndexReg[OPL3EMU_SECONDARY]-OPL3EMU_ADLG_VOLL_REG_INDEX];
    return OPL3EMU_PrimaryRead(val);
}

uint32_t OPL3EMU_SecondaryWriteIndex(uint32_t val)
{
    if(val == 0xFF)
        OPL3EMU_ADLG_CtrlEnable = 1;
    else if(val == 0xFE)
        OPL3EMU_ADLG_CtrlEnable = 0;

    OPL3EMU_IndexReg[OPL3EMU_SECONDARY] = OPL3EMU_WriteAddr(0x38A, val);
    return val;
}

uint32_t OPL3EMU_SecondaryWriteData(uint32_t val)
{
    if(/*OPL3EMU_ADLG_CtrlEnable && */(OPL3EMU_IndexReg[OPL3EMU_SECONDARY] == 0x100+OPL3EMU_ADLG_VOLL_REG_INDEX || OPL3EMU_IndexReg[OPL3EMU_SECONDARY] == 0x100+OPL3EMU_ADLG_VOLR_REG_INDEX))
        OPL3EMU_ADLG_Volume[OPL3EMU_IndexReg[OPL3EMU_SECONDARY]-OPL3EMU_ADLG_VOLL_REG_INDEX] = val;
    OPL3EMU_WriteReg(OPL3EMU_IndexReg[OPL3EMU_SECONDARY], val);
    return val;
}
