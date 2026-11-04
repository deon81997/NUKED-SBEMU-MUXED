/*
 * sc55wif.h - interface shared by SC55W.VXD (ring 0, Open Watcom C) and
 * SC55W.EXE (ring 3, MinGW). Plain C, fixed-size types, no padding surprises.
 *
 * The helper allocates one SC55W_SHARED block (page aligned), fills in the
 * configuration and passes its address with IOCTL_SC55W_ATTACH. The VxD
 * locks it and maps a global alias, then writes the rings; the helper reads
 * them. Ring indices are free-running 32-bit counters; sizes are powers of
 * two. Producer writes the data, then advances head; consumer advances tail.
 */
#ifndef SC55WIF_H
#define SC55WIF_H

#ifdef __WATCOMC__
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;
typedef signed short   s16;
typedef signed long    s32;
#else
#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t  s16;
typedef int32_t  s32;
#endif

#define SC55W_MAGIC      0x57353543UL   /* "C55W" */
#define SC55W_IF_VERSION 4

/* DeviceIoControl codes */
#define IOCTL_SC55W_VERSION  0x10   /* out: u32 interface version            */
#define IOCTL_SC55W_ATTACH   0x11   /* in: u32 address of SC55W_SHARED; out: u32 status bits */
#define IOCTL_SC55W_DETACH   0x12   /* removes traps, unlocks the block      */
#define IOCTL_SC55W_TIME     0x13   /* out: u32 VxD clock (microseconds)     */
#define IOCTL_SC55W_MIDI     0x14   /* in: bytes; queued as MIDI events      */
#define IOCTL_SC55W_RESET    0x15   /* DSP/mixer/OPL timer reset             */

/* status bits returned by ATTACH (also in shared->attach_status) */
#define ST_OK_SB        0x0001   /* SB ports trapped                        */
#define ST_OK_OPL       0x0002   /* 388h ports trapped                      */
#define ST_OK_MPU       0x0004   /* MPU ports trapped                       */
#define ST_OK_IRQ       0x0008   /* IRQ virtualized                         */
#define ST_OK_DMA8      0x0010   /* 8-bit DMA channel virtualized           */
#define ST_OK_DMA16     0x0020   /* 16-bit DMA channel virtualized          */
#define ST_ERR_BLOCK    0x0100   /* bad block (magic/size/version)          */
#define ST_ERR_LOCK     0x0200   /* could not lock/map the block            */
#define ST_ERR_SBPORT   0x0400   /* an SB port is owned by another driver   */
#define ST_ERR_OPLPORT  0x0800
#define ST_ERR_MPUPORT  0x1000
#define ST_ERR_IRQ      0x2000
#define ST_ERR_DMA8     0x4000
#define ST_ERR_DMA16    0x8000
#define ST_ERR_BUSY     0x10000  /* already attached                       */

/* event types in the event ring */
#define EV_OPL      1   /* a = register (low 8 bits), b = value, c = bank (0/1) */
#define EV_MIDI     2   /* a = MIDI byte                                        */
#define EV_DAC      3   /* a = 8-bit unsigned sample (DSP command 10h)          */
#define EV_SPEAKER  4   /* a = 1 on, 0 off                                      */
#define EV_PCMFMT   5   /* PCM stream rate change; rate in pcmfmt ring          */
#define EV_RESET    6   /* DSP reset / stop                                     */

typedef struct {
    u32 time;   /* VxD clock, microseconds */
    u8  type;
    u8  a, b, c;
} SC55W_EVENT;

#define EVRING_SIZE   32768u          /* events (8 bytes each)  */
#define PCMRING_SIZE  65536u          /* stereo s16 frames       */
#define PCMFMT_SIZE   64u             /* rate-change records     */
#define TRACE_SIZE    256u

typedef struct {
    u32 frame;  /* PCM ring frame index where this rate starts */
    u32 rate;   /* Hz */
} SC55W_PCMFMT;

/* configuration (helper -> VxD, read at ATTACH) */
typedef struct {
    u16 sb_port;       /* 0x220 / 0x240, 0 = off                    */
    u16 mpu_port;      /* 0x330, 0 = off                            */
    u8  irq;           /* 5 / 7 / 9 (10)                            */
    u8  dma8;          /* 0 / 1 / 3                                 */
    u8  dma16;         /* 5 / 6 / 7, 0 = none                       */
    u8  sbtype;        /* 1..6 as SBEMU /T                          */
    u8  opl;           /* 1 = trap OPL ports (SB base + 388h)       */
    u8  fixtc;         /* 1 = snap time constants to 11/22/44 kHz   */
    u8  reserved[6];
} SC55W_CONFIG;

typedef struct {
    /* header */
    u32 magic;
    u32 if_version;
    u32 size;                 /* sizeof(SC55W_SHARED)              */
    SC55W_CONFIG cfg;

    /* VxD status (VxD -> helper) */
    volatile u32 attach_status;
    volatile u32 now_us;      /* VxD clock at last update          */
    volatile u32 irq_count;
    volatile u32 dma_bytes;
    volatile u32 dsp_cmds;
    volatile u32 ev_overflow;
    volatile u32 pcm_overflow;
    volatile u32 dma_active;  /* 1 while a DMA transfer is running */
    volatile u32 last_vm;
    volatile u32 debug[16];   /* free-form diagnostics             */
    volatile u8  mixer[256];  /* SB mixer register mirror          */

    /* event ring */
    volatile u32 ev_head, ev_tail;
    SC55W_EVENT  ev[EVRING_SIZE];

    /* PCM ring: interleaved stereo s16 at the SB's rate */
    volatile u32 pcm_head, pcm_tail;     /* in frames */
    s16          pcm[PCMRING_SIZE * 2];

    volatile u32 fmt_head, fmt_tail;
    SC55W_PCMFMT fmt[PCMFMT_SIZE];

    /* diagnostics trace (VxD -> helper), overwritten when full */
    volatile u32 tr_head;
    u32 tr[TRACE_SIZE][4];      /* time, code, a, b */
} SC55W_SHARED;

#endif
