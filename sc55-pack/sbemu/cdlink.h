/* CDLINK - CD-Audio from uCDD-mounted CUE/BIN images, mixed by SBEMU.
 *
 * The uCDD lite driver (built with --sbemu-link) answers MSCDEX CD-audio requests (play, stop,
 * resume, seek, Q-channel, volume) from a mailbox in its own segment. This module finds that
 * mailbox, reads the raw 2352-byte audio sectors from the image file and mixes them into the
 * PCM block SBEMU is about to send to the sound card (AC'97 etc.).
 *
 * Experimental. GPL-2.0-or-later. */
#ifndef CDLINK_H
#define CDLINK_H
#include <stdint.h>

#define CDLINK_SECTOR 2352
#define CDLINK_RING_SECTORS 128
#define CDLINK_FRAMES_PER_SECTOR 588
#define CDLINK_PREBUFFER_SECTORS 24
#define CDLINK_REFILL_SECTORS 16

#pragma pack(push, 1)
typedef struct
{
    char sig[8];        /* "SBCDA1\0\0" */
    uint8_t state;      /* 0 idle, 1 playing, 2 paused (SBEMU writes 0 at the end of the range) */
    uint8_t flags;      /* bit0: SBEMU attached (set by SBEMU) */
    uint8_t cmd;        /* 1 play, 2 pause, 3 resume, 4 stop */
    uint8_t unitno;
    uint32_t start;     /* file sector index (raw 2352-byte sectors) */
    uint32_t end;       /* one past the last */
    uint32_t pos;       /* SBEMU advances while playing */
    uint16_t gen;       /* driver increments on every command */
    uint8_t vol[2];     /* 0-255 */
    uint16_t unitp;     /* unit struct offset in the driver segment */
    uint16_t ver;
} CDLINK_Box;
#pragma pack(pop)

#define CDLINK_BOX_STATE 8
#define CDLINK_BOX_FLAGS 9
#define CDLINK_BOX_POS 20

/* How the engine reaches the image file. All return < 0 for "not now / failed". */
typedef struct
{
    /* Open the image of the disc described by the box. 0 ok, <0 try again later, >0 hard failure. */
    int (*open)(void* user, const CDLINK_Box* box);
    void (*close)(void* user);
    /* Read up to n raw sectors starting at file sector `sector`. Returns sectors read,
       0 at end of file, <0 if DOS was busy (try again on the next tick). */
    int (*read)(void* user, uint32_t sector, uint8_t* dst, int n);
    void* user;
} CDLINK_IO;

typedef struct CDLINK_Engine CDLINK_Engine;

CDLINK_Engine* CDLINK_EngineCreate(const CDLINK_IO* io, int gain_percent);
void CDLINK_EngineDestroy(CDLINK_Engine* e);
/* Handle new commands, refill from the file, resample to `rate` and add into pcm (interleaved s16
   stereo, `frames` frames, saturating). Updates box->state / box->pos in the passed box copy; the
   caller writes those back. Returns 1 if anything was mixed. */
int CDLINK_Mix(CDLINK_Engine* e, CDLINK_Box* box, int16_t* pcm, int frames, int rate);

/* Pure helpers (host-tested) */
/* Find the FILE "name" of a CUE sheet. Returns 1 and copies the name (no quotes) into out. */
/* the DOS side of the engine (open / close / read), outside the sound interrupt */
void CDLINK_EngineSetInlineIO(CDLINK_Engine* e, int on);   /* 1 (default): Mix does it itself (host tests) */
int CDLINK_EngineNeedsService(const CDLINK_Engine* e);
int CDLINK_EngineState(const CDLINK_Engine* e);   /* 1 open, 2 open failed, 4 read some */
void CDLINK_EngineService(CDLINK_Engine* e, int max_sectors);

int CDLINK_ParseCueFile(const char* text, int len, char* out, int outsz);
/* Directory part of a DOS path including the trailing backslash, then the name appended. */
void CDLINK_JoinPath(const char* cuepath, const char* name, char* out, int outsz);

#ifndef CDLINK_HOSTTEST
/* DOS glue */
int CDLINK_Init(int gain_percent);   /* 1 if the driver mailbox was found and the engine is ready */
int CDLINK_IsActive(void);
/* Mix into the block that is about to be output. Safe to call every interrupt. */
void CDLINK_MixInto(int16_t* pcm, int frames, int rate);
int CDLINK_NeedsService(void);
void CDLINK_Service(int idle);   /* 0: after the card's EOI (InDOS must be 0); 1: from INT 28h (InDOS 1) */
void CDLINK_Shutdown(void);
#endif

#endif
