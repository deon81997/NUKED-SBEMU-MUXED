#ifndef SBEMU_PADMAP_H
#define SBEMU_PADMAP_H
/* Game pad buttons -> keyboard keys, with a layout per game (DOS side of the shared PADMAP format; the Windows 98
 * side is win98/helper/pad.cpp - same files, same names, same rules). See padmap.c. */
#include <stdint.h>

/* pad buttons in Xbox names, the order of pad.h's PadButton */
enum { PM_A, PM_B, PM_X, PM_Y, PM_LB, PM_RB, PM_LT, PM_RT, PM_LS, PM_RS, PM_BACK, PM_START,
       PM_DUP, PM_DDOWN, PM_DLEFT, PM_DRIGHT, PM_BUTTONS };
enum { PM_LX, PM_LY, PM_RX, PM_RY, PM_AXES };

typedef struct
{
    int      present;
    uint16_t buttons;              /* 1 << PM_A ... */
    int      axis[PM_AXES];        /* -32768..32767, 0 = centre; negative = left / up */
} PADMAP_In;

typedef struct
{
    int      game;                 /* 1 = a game's layout is in use (the game port follows it), 0 = Default */
    int      axis[4];              /* game port axes 1-4 (A x, A y, B x, B y), -32768..32767, only when game */
    uint8_t  buttons;              /* game port buttons 1-4 (bit 0 = button 1), only when game */
    uint8_t  present;              /* bit 0 joystick A there, bit 1 joystick B, only when game */
    uint16_t taken;                /* buttons bound to keys only (Default: kept off the game port) */
    uint8_t  axis_taken;           /* axes bound to keys (bit per PM_LX..) */
} PADMAP_Out;

/* key bytes out (set 1, E0 / E1 prefixes included), called from PADMAP_Step */
typedef void (*PADMAP_EmitFn)(const uint8_t* bytes, int n);

/* PADMAP.BIN: build from PADMAP.TXT + PADUSER.TXT (PADUSER wins). stamps: what the BIN was made from (the
 * FAT date/time and size of each, 0 = missing), so a changed text file makes it rebuild. 0 = ok. */
int  PADMAP_Build(const char* txt, const char* user, const char* bin, const uint32_t stamps[6], char* msg, int msglen);
/* Load PADMAP.BIN whole into memory (the resident SBEMU looks layouts up from there, no file reads later).
 * stamps (6, may be NULL) receives what it was built from. 0 = ok. */
int  PADMAP_Load(const char* bin, uint32_t stamps[6], char* msg, int msglen);
/* only the built-in Default (no files) */
void PADMAP_Reset(void);
/* FAT date/time << 16 ^ size-ish stamp of a file, 0 = missing (for PADMAP_Build / staleness checks) */
void PADMAP_Stamp(const char* path, uint32_t* stamp, uint32_t* size);

uint32_t    PADMAP_FileId(const char* name83, uint32_t size);   /* FNV-1 of the upper-case name ^ (size << 3) */
int         PADMAP_Find(uint32_t id);                           /* layout index, -1 = no game */
int         PADMAP_Count(int* ids);                             /* layouts (Default included) and ids */
const char* PADMAP_Title(int lay);
int         PADMAP_Describe(int lay, char* out, int size);     /* "A=LCtrl B=Space ..." (with Default's lines) */

/* layout to use from the next step on (0 = Default); held keys are let go first. Safe from any context. */
void PADMAP_Select(int lay);
int  PADMAP_Current(void);
/* one step: pads (npads, in[0] drives the game port) -> key changes through emit, game port values in out */
void PADMAP_Step(const PADMAP_In* in, int npads, PADMAP_Out* out, PADMAP_EmitFn emit);
/* let go of every held key */
void PADMAP_Release(PADMAP_EmitFn emit);

const char* PADMAP_ButtonName(int b);

/* Everything SBEMU / USBTEST do at start, in folder dir (SBEMU's): PADMAP\*.CPP (keyb2joypad.cpp) -> PADMAP.TXT when
 * that is missing or the .cpp changed; PADMAP.TXT + PADUSER.TXT -> PADMAP.BIN when any changed; load it.
 * note(kind, text): 0 = about to do something slow, 1 = a problem, 2 = detail (for SBDETAIL / a log).
 * Returns 1 when layouts are there (PADMAP.TXT or PADUSER.TXT), 0 = Default only (nothing loaded). */
typedef void (*PADMAP_NoteFn)(int kind, const char* text);
int  PADMAP_Prepare(const char* dir, PADMAP_NoteFn note);
/* the layout SBEMU would pick for this program: its own id, else a file in its folder (where = that file's name,
 * how = 2 own id, 1 folder, 0 none -> Default). Uses file look-ups: not for the resident SBEMU. */
int  PADMAP_ForProgram(const char* path, char* where, int wherelen, int* how);

#endif
