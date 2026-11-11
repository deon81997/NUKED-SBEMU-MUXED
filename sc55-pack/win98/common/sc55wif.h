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
#define SC55W_IF_VERSION 8

/* DeviceIoControl codes */
#define IOCTL_SC55W_VERSION  0x10   /* out: u32 interface version            */
#define IOCTL_SC55W_ATTACH   0x11   /* in: u32 address of SC55W_SHARED; out: u32 status bits */
#define IOCTL_SC55W_DETACH   0x12   /* removes traps, unlocks the block      */
#define IOCTL_SC55W_TIME     0x13   /* out: u32 VxD clock (microseconds)     */
#define IOCTL_SC55W_MIDI     0x14   /* in: bytes; queued as MIDI events      */
#define IOCTL_SC55W_RESET    0x15   /* DSP/mixer/OPL timer reset             */
#define IOCTL_SC55W_KEYS     0x16   /* in: set-1 scan codes for the DOS box with the keyboard;
                                       out: u32 1 = sent, 0 = Windows has the keyboard, 2 = queue full */
#define IOCTL_SC55W_FOCUS    0x17   /* out: SC55W_FOCUS, the programs running in that DOS box */

/* the program list of the DOS box that has the keyboard (IOCTL_SC55W_FOCUS) */
#define SC55W_NPROG 4
typedef struct {
    u32  size;          /* file size in bytes                          */
    char path[128];     /* full path (DOS TRUENAME), upper case 8.3     */
} SC55W_PROG;
typedef struct {
    u32 is_dos;         /* 1 = a DOS box has the keyboard              */
    u32 vm;             /* its VM handle                               */
    u32 serial;         /* changes whenever its program list changes   */
    u32 depth;          /* programs in prog[], prog[depth-1] = newest  */
    SC55W_PROG prog[SC55W_NPROG];
} SC55W_FOCUS;

/* status bits returned by ATTACH (also in shared->attach_status) */
#define ST_OK_SB        0x0001   /* SB ports trapped                        */
#define ST_OK_OPL       0x0002   /* 388h ports trapped                      */
#define ST_OK_MPU       0x0004   /* MPU ports trapped                       */
#define ST_OK_IRQ       0x0008   /* IRQ virtualized                         */
#define ST_OK_DMA8      0x0010   /* 8-bit DMA channel virtualized           */
#define ST_OK_DMA16     0x0020   /* 16-bit DMA channel virtualized          */
#define ST_OK_JOY       0x0040   /* game port 201h trapped (DOS boxes)      */
#define ST_OK_JBIOS     0x0080   /* INT 15h AH=84h answered (DOS boxes)     */
#define ST_ERR_BLOCK    0x0100   /* bad block (magic/size/version)          */
#define ST_ERR_LOCK     0x0200   /* could not lock/map the block            */
#define ST_ERR_SBPORT   0x0400   /* an SB port is owned by another driver   */
#define ST_ERR_OPLPORT  0x0800
#define ST_ERR_MPUPORT  0x1000
#define ST_ERR_IRQ      0x2000
#define ST_ERR_DMA8     0x4000
#define ST_ERR_DMA16    0x8000
#define ST_ERR_BUSY     0x10000  /* already attached                       */
#define ST_ERR_JOYPORT  0x20000  /* 201h is owned by another driver        */
#define ST_ERR_JBIOS    0x40000  /* could not hook INT 15h                 */
#define ST_OK_PROG      0x80000  /* INT 21h watched: programs in DOS boxes  */
#define ST_ERR_PROG     0x100000 /* could not hook INT 21h                 */

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

/* one NUKED joystick as Windows sees it (Game Controllers type "NUKED SB-EMU Joystick") */
#define SC55W_NWJOY 2
typedef struct {
    volatile u16 axis[6];     /* X Y Z R U V, 0..65535, 32768 = centre          */
    volatile u16 pov;         /* hundredths of a degree, 0xFFFF = centred        */
    volatile u8  present;     /* 1 = the helper feeds it                         */
    volatile u8  nbuttons;
    volatile u32 buttons;     /* bit n = button n+1                              */
    volatile u32 polls;       /* VJoyD polls (NUKEDJOY.VXD)                      */
    volatile u32 joyid;       /* Windows joystick id VJoyD gave it + 1, 0 = none */
} SC55W_WJOY;

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
    u8  joy;           /* bit0 = game port, bit1 = INT 15h AH=84h,
                          bit2 = watch INT 21h EXEC (pad keys per game),
                          bit3 = the game port for Windows programs too (no real one) */
    u8  joy_type;      /* stick type as SBEMU /JOYMODE (helper only)  */
    u16 joy_port;      /* game port address 200h..20Fh (0 = 201h)   */
    u8  reserved[2];
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

    /* joystick for DOS boxes (helper -> VxD), from the Windows joystick API */
    volatile u16 joy_axis[4];   /* 0..65535: stick A x, y, stick B x, y  */
    volatile u8  joy_buttons;   /* bit0..3 = A1 A2 B1 B2, 1 = pressed     */
    volatile u8  joy_present;   /* bit0 = stick A, bit1 = stick B        */
    volatile u16 joy_pad;
    volatile u32 joy_reads;     /* 201h reads + INT 15h calls (VxD)      */

    /* Windows MIDI volume (SC55MIDI.DRV -> VxD -> helper): low word left,
       high word right, 0xFFFF = full; set_by = 1 once the driver set it */
    volatile u32 midi_volume;
    volatile u32 midi_volume_set;

    /* NUKED joysticks for Windows programs (helper -> NUKEDJOY.VXD, the VJoyD minidriver,
       which finds this block through SC55W.VXD's DDB: Reserved1 = address, Reserved2 =
       SC55W_DDB_MAGIC while attached) */
    SC55W_WJOY wjoy[SC55W_NWJOY];

    /* game port stick type, live (helper -> VxD): 0 standard, 1 CH Flightstick Pro, 2 Thrustmaster
       FCS (the helper puts both in joy_buttons / joy_axis), 3 Gravis GrIP: the VxD clocks
       joy_grip[pad] (24-bit frame, as SBEMU's vjoy.c) out on the button lines, one bit per 4 reads */
    volatile u32 joy_mode;
    volatile u32 joy_grip[2];
    volatile u32 joy_grip_n;

    /* front panel for DOS programs (helper -> VxD, ~10 times a second): the packet SBEMU gives for MPU
       command B0h - 'L', display on, synth (0 SC-55, 1 SoundFont, 2 MT-32), load permille lo/hi, governor,
       80 bytes LCD display RAM, 64 bytes custom characters. MIDPLAY draws the real SC-55 panel from it. */
    volatile u32 lcd_seq;
    u8           lcd[152];
} SC55W_SHARED;

#define SC55W_DDB_MAGIC 0x374A4E53UL    /* "SNJ7" */

/* the same layout in Open Watcom (VxDs) and MinGW (helper): checked at compile time */
#define SC55W_CHECK(n, c) typedef char n[(c) ? 1 : -1]
SC55W_CHECK(sc55w_chk_size, sizeof(SC55W_SHARED) == 529560);
SC55W_CHECK(sc55w_chk_cfg, sizeof(SC55W_CONFIG) == 16);
SC55W_CHECK(sc55w_chk_wjoy, sizeof(SC55W_WJOY) == 28);

#endif
