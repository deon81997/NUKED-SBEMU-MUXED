/* Virtual gameport for SBEMU: port 201h answers like an analog PC joystick, with the position and
 * buttons of the USB Xbox 360 pad(s). One pad: /JOYAX and /JOYBT give all four axes and buttons (joystick
 * B = its right stick by default). Two pads: pad 1 is joystick A and pad 2 joystick B, each with the first
 * two /JOYAX axes and the first two /JOYBT buttons. /JOYKEY works on either pad.
 *
 * Extended sticks (/JOYMODE, pad 1 only), the two that flight sims and Descent-style games know:
 *   1 CH Flightstick Pro: four buttons and a 4-way hat (the D-pad) on the four button lines. A button alone
 *     pulls its own line; the hat pulls several at once (up 1+2+3+4, right 1+2+4, down 1+2+3, left 1+2), so
 *     only one button or hat direction shows at a time - as on the real stick (the hat wins).
 *   2 Thrustmaster FCS: four buttons, hat on axis 4 (joystick B Y): up 0, right 1/4, down 1/2, left 3/4,
 *     centred = full scale.
 *   3 Gravis GamePad Pro in digital (GrIP) mode: all buttons. The pad clocks a 24-bit frame out without
 *     end on two button lines (pad 1: line 1 = clock, line 2 = data; pad 2: lines 3 / 4), a bit read at
 *     each falling clock edge. Frame bits: 0 Start, 1 Select, 2 R2, 3 Y, 5 L2, 6 A, 7 B, 8 X, 10 L1, 11 R1,
 *     12 up, 13 down, 15 right, 16 left, 18-22 always 1 (sync), 4 9 14 17 23 always 0. Here the clock
 *     steps on every read of port 201h (two reads per level), so it works however slow the trap is.
 *     No analog axes (they read as not connected).
 *   4 none: port 201h is not taken (a real gameport stays usable); /JOYKEY still works.
 * Encodings as Linux drivers/input/joystick/analog.c and grip.c decode them.
 *
 * How a game reads a gameport: it writes any value to 201h, which fires four one-shot timers (one per
 * axis); each axis bit (bits 0-3) reads 1 until its timer runs out, after a time that grows with the
 * joystick's position (about 24 us + 11 us per kOhm, 0-100 kOhm pots: 24 us .. 1.1 ms). Bits 4-7 are
 * the buttons, 0 = pressed. An axis with nothing connected never times out (its bit stays 1), which
 * is how games tell joystick B is absent.
 *
 * Here the write takes a TSC time stamp and works out each axis' end time from the pad's current
 * position; reads compare the TSC with them. Every access is a trap (a few microseconds), so games
 * that time the loop themselves see a little less resolution than with a real stick, which their own
 * calibration takes care of. */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "vjoy.h"
#include "usbpad.h"
#include "vkbc.h"

enum { A_NONE, A_LX, A_LY, A_RX, A_RY, A_LT, A_RT, A_TR };
static const char* AxName[] = { "-", "LX", "LY", "RX", "RY", "LT", "RT", "TR" };

typedef struct { const char* name; uint16_t bit; int trig; } BName; /* trig: 1 LT, 2 RT */
static const BName BNames[] =
{
    { "A", XPAD_A }, { "B", XPAD_B }, { "X", XPAD_X }, { "Y", XPAD_Y },
    { "LB", XPAD_LB }, { "RB", XPAD_RB }, { "LT", 0, 1 }, { "RT", 0, 2 },
    { "ST", XPAD_START }, { "BK", XPAD_BACK }, { "LS", XPAD_LS }, { "RS", XPAD_RS }, { "GD", XPAD_GUIDE },
    { "DU", XPAD_DU }, { "DD", XPAD_DD }, { "DL", XPAD_DL }, { "DR", XPAD_DR },
    { "-", 0, 0 },
};
#define NB (int)(sizeof(BNames) / sizeof(BNames[0]))

static int      Ax[4], AxInv[4];
static int      Bt[4];                 /* index into BNames */
typedef struct { int b; uint16_t code; } KMap;
static KMap     Km[16]; static int Kn;
static uint32_t KDown;                 /* bit per Km entry */
static int      Dz;
static int      Mode;                  /* 0 standard, 1 CH Flightstick Pro, 2 Thrustmaster FCS, 3 GrIP, 4 none */
static uint32_t GripN, GripF[2];      /* read counter, frames being sent */
static uint64_t Cps, T0, End[4], LastPoll;
static int      Inited;


static inline uint64_t tsc(void) { uint32_t lo, hi; __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi)); return ((uint64_t)hi << 32) | lo; }

const char* VJOY_Names(void)
{
    return "axes LX LY RX RY LT RT TR(RT minus LT) - (none), ~ in front inverts; buttons A B X Y LB RB LT RT ST(start) BK(back) LS RS GD(guide) DU DD DL DR(d-pad) -";
}

static int bname(const char* s, int n)
{
    int i;
    for (i = 0; i < NB; ++i) if ((int)strlen(BNames[i].name) == n && !strncasecmp(BNames[i].name, s, n)) return i;
    return -1;
}

static int held(int bi, const USBPAD_State* p)
{
    const BName* b = &BNames[bi];
    if (b->trig == 1) return p->lt > 128;
    if (b->trig == 2) return p->rt > 128;
    return b->bit && (p->buttons & b->bit);
}

static int32_t dz(int32_t v)            /* -32768..32767 -> dead zone applied, same range */
{
    int32_t d = Dz * 32767 / 100, a = v < 0 ? -v : v;
    if (a <= d) return 0;
    a = (a - d) * 32767 / (32767 - d);
    if (a > 32767) a = 32767;
    return v < 0 ? -a : a;
}

/* 0 = left/up .. 65535 = right/down */
static uint32_t axis(int i, const USBPAD_State* p)
{
    int32_t v = 0;
    switch (Ax[i])
    {
    case A_LX: v = dz(p->lx); if (!Mode && (p->buttons & XPAD_DL)) v = -32768; if (!Mode && (p->buttons & XPAD_DR)) v = 32767; break;
    case A_LY: v = -dz(p->ly); if (!Mode && (p->buttons & XPAD_DU)) v = -32768; if (!Mode && (p->buttons & XPAD_DD)) v = 32767; break;
    case A_RX: v = dz(p->rx); break;
    case A_RY: v = -dz(p->ry); break;
    case A_LT: v = p->lt * 257 - 32768; break;
    case A_RT: v = p->rt * 257 - 32768; break;
    case A_TR: v = ((int32_t)p->rt - (int32_t)p->lt) * 128; break;
    }
    if (AxInv[i]) v = -v;
    v += 32768;
    if (v < 0) v = 0;
    if (v > 65535) v = 65535;
    return (uint32_t)v;
}

static void poll(int force)
{
    uint64_t t = tsc();
    if (!force && t - LastPoll < Cps / 2000) return;   /* every 0.5 ms at most from port reads */
    LastPoll = t;
    if (force) USBPAD_Poll(); else USBPAD_PollLight();
}

/* gameport axis / button i: which pad, and which /JOYAX / /JOYBT item */
/* GrIP frame of a pad (24 bits, bit 0 first) */
static uint32_t grip_frame(const USBPAD_State* p)
{
    uint32_t f = 0x7C0000UL;
    uint16_t b;
    int x, y;
    if (!p->connected) return 0;
    b = p->buttons;
    x = (b & XPAD_DL) ? -1 : (b & XPAD_DR) ? 1 : p->lx < -16384 ? -1 : p->lx > 16384 ? 1 : 0;
    y = (b & XPAD_DU) ? -1 : (b & XPAD_DD) ? 1 : p->ly > 16384 ? -1 : p->ly < -16384 ? 1 : 0;
    if (b & XPAD_START) f |= 1UL << 0;
    if (b & XPAD_BACK)  f |= 1UL << 1;
    if (p->rt > 128)    f |= 1UL << 2;
    if (b & XPAD_Y)     f |= 1UL << 3;
    if (p->lt > 128)    f |= 1UL << 5;
    if (b & XPAD_A)     f |= 1UL << 6;
    if (b & XPAD_B)     f |= 1UL << 7;
    if (b & XPAD_X)     f |= 1UL << 8;
    if (b & XPAD_LB)    f |= 1UL << 10;
    if (b & XPAD_RB)    f |= 1UL << 11;
    if (y < 0) f |= 1UL << 12;
    if (y > 0) f |= 1UL << 13;
    if (x > 0) f |= 1UL << 15;
    if (x < 0) f |= 1UL << 16;
    return f;
}

static const USBPAD_State* pad_for(int i, int* m)
{
    const USBPAD_State* p2 = USBPAD_GetPad(1);
    if (p2->connected && !Mode) { *m = i & 1; return i < 2 ? USBPAD_GetPad(0) : p2; }
    *m = i;
    return USBPAD_GetPad(0);
}

uint32_t VJOY_IOHandler(uint32_t port, uint32_t val, uint32_t out)
{
    int i, m;
    (void)port;
    if (!Inited) return out ? val : 0xFF;
    poll(0);
    if (out)
    {
        T0 = tsc();
        for (i = 0; i < 4; ++i)
        {
            const USBPAD_State* p = pad_for(i, &m);
            uint32_t a;
            if (Mode == 2 && i == 3 && p->connected)                       /* FCS hat */
            {
                uint16_t b = p->buttons;
                a = (b & XPAD_DU) ? 0 : (b & XPAD_DR) ? 16384 : (b & XPAD_DD) ? 32768 : (b & XPAD_DL) ? 49152 : 65535;
            }
            else if (Ax[m] == A_NONE || !p->connected) { End[i] = ~0ULL; continue; } /* nothing connected: never times out */
            else a = axis(m, p);
            End[i] = T0 + Cps * (24 + (uint64_t)a * 1100 / 65535) / 1000000ULL;
        }
        return val;
    }
    else
    {
        uint64_t t = tsc();
        uint32_t r = 0xF0;
        for (i = 0; i < 4; ++i) if (t < End[i]) r |= 1u << i;
        if (Mode == 3)                                                      /* GrIP */
        {
            uint32_t n = GripN++, bit = (n >> 2) % 24, clk = !((n >> 1) & 1), k;
            if (bit == 0 && (n & 3) == 0) { GripF[0] = grip_frame(USBPAD_GetPad(0)); GripF[1] = grip_frame(USBPAD_GetPad(1)); }
            r = 0x0F;                                                       /* no analog axes */
            for (k = 0; k < 2; ++k)
            {
                if (!GripF[k]) { r |= 0x30u << (k * 2); continue; }       /* no pad: lines stay high */
                if (clk) r |= 0x10u << (k * 2);
                if ((GripF[k] >> bit) & 1) r |= 0x20u << (k * 2);
            }
            return r;
        }
        if (Mode == 1)                                                      /* CH Flightstick Pro */
        {
            const USBPAD_State* p = USBPAD_GetPad(0);
            uint16_t b = p->connected ? p->buttons : 0;
            uint32_t lines = (b & XPAD_DU) ? 0xF : (b & XPAD_DR) ? 0xB : (b & XPAD_DD) ? 0x7 : (b & XPAD_DL) ? 0x3 : 0;
            for (i = 0; i < 4 && !lines && p->connected; ++i)
                if (Bt[i] >= 0 && held(Bt[i], p)) lines = 1u << i;
            return r & ~(lines << 4);
        }
        for (i = 0; i < 4; ++i)
        {
            const USBPAD_State* p = pad_for(i, &m);
            if (p->connected && Bt[m] >= 0 && held(Bt[m], p)) r &= ~(0x10u << i);
        }
        return r;
    }
}

/* Port 206h (real mode only), for SBEMU16 (16-bit protected-mode games): there every access costs a trip
 * down to real mode, longer than a whole stick time-out, so after the game's write to 201h SBEMU16 asks here
 * each axis' time-out (from that write) and times the reads itself, from when the answer gets back to it.
 * Write: snapshot; then 10 reads: 4 x time-out in us (16-bit, FFFFh = never: nothing connected), flags (bit 0:
 * every read must come here - GrIP), A5h. */
static uint8_t  Info[10];
static int      InfoN = 10;
uint32_t VJOY_InfoHandler(uint32_t port, uint32_t val, uint32_t out)
{
    (void)port;
    if (out)
    {
        int i;
        for (i = 0; i < 4; ++i)
        {
            uint32_t us = End[i] == ~0ULL ? 0xFFFF : End[i] <= T0 ? 0 : (uint32_t)((End[i] - T0) * 1000000ULL / Cps);
            if (us > 0xFFFE && End[i] != ~0ULL) us = 0xFFFE;
            Info[i * 2] = (uint8_t)us; Info[i * 2 + 1] = (uint8_t)(us >> 8);
        }
        Info[8] = (uint8_t)(Mode == 3 ? 1 : 0);
        Info[9] = 0xA5;
        InfoN = 0;
        return val;
    }
    return InfoN < 10 ? Info[InfoN++] : 0xFF;
}

/* sound interrupt: full USB poll, pad buttons -> keys, then the keyboard controller (vkbc.c) */
void VJOY_Tick(void)
{
    const USBPAD_State* p1;
    const USBPAD_State* p2;
    int i;
    if (!Inited) return;
    poll(1);
    p1 = USBPAD_GetPad(0); p2 = USBPAD_GetPad(1);
    for (i = 0; i < Kn; ++i)
    {
        int h = (p1->connected && held(Km[i].b, p1)) || (p2->connected && held(Km[i].b, p2)), was = (KDown >> i) & 1;
        if (h == was) continue;
        KDown ^= 1u << i;
        VKBC_Key(Km[i].code, h);
    }
    VKBC_Tick();
}

/* next comma-separated item */
static const char* item(const char* s, int* n)
{
    const char* e = s;
    while (*e && *e != ',' && *e != ' ') ++e;
    *n = (int)(e - s);
    return e;
}

/* A second SBEMU run changed /JOYMODE or /JOYDZ: the resident copy takes them (not to or from 4, which
   changes the port traps; main.c checks that). */
void VJOY_SetLive(int dzp, int mode)
{
    Dz = dzp < 0 ? 0 : dzp > 90 ? 90 : dzp;
    Mode = mode >= 1 && mode <= 4 ? mode : 0;
    GripN = 0; GripF[0] = GripF[1] = 0;
}

int VJOY_Init(uint64_t cps, const char* axes, const char* btns, const char* keys, int dzp, int mode, const char** bad)
{
    static char badbuf[24];
    const char* s;
    int i, n;
    *bad = NULL;
    Cps = cps ? cps : 1000000000ULL;
    Dz = dzp < 0 ? 0 : dzp > 90 ? 90 : dzp;
    Mode = mode >= 1 && mode <= 4 ? mode : 0;
    GripN = 0; GripF[0] = GripF[1] = 0;
    s = axes;
    for (i = 0; i < 4; ++i)
    {
        int inv = 0, a, found = -1;
        Ax[i] = A_NONE; AxInv[i] = 0;
        while (*s == ',' || *s == ' ') ++s;
        if (!*s) continue;
        if (*s == '~') { inv = 1; ++s; }
        s = item(s, &n);
        for (a = 0; a < (int)(sizeof(AxName) / sizeof(AxName[0])); ++a)
            if ((int)strlen(AxName[a]) == n && !strncasecmp(AxName[a], s - n, n)) found = a;
        if (found < 0) { snprintf(badbuf, sizeof(badbuf), "%.*s", n > 20 ? 20 : n, s - n); *bad = badbuf; return 0; }
        Ax[i] = found; AxInv[i] = inv;
    }
    s = btns;
    for (i = 0; i < 4; ++i)
    {
        Bt[i] = -1;
        while (*s == ',' || *s == ' ') ++s;
        if (!*s) continue;
        s = item(s, &n);
        Bt[i] = bname(s - n, n);
        if (Bt[i] < 0) { snprintf(badbuf, sizeof(badbuf), "%.*s", n > 20 ? 20 : n, s - n); *bad = badbuf; return 0; }
    }
    Kn = 0; KDown = 0;
    s = keys;
    while (*s && Kn < 16)
    {
        const char* c;
        int b;
        char* end;
        long code;
        while (*s == ',' || *s == ' ') ++s;
        if (!*s) break;
        s = item(s, &n);
        if (n == 1 && s[-1] == '-') continue;          /* "-" = no keys */
        c = memchr(s - n, ':', n);
        if (!c) { snprintf(badbuf, sizeof(badbuf), "%.*s", n > 20 ? 20 : n, s - n); *bad = badbuf; return 0; }
        b = bname(s - n, (int)(c - (s - n)));
        code = strtol(c + 1, &end, 16);
        if (b < 0 || end == c + 1 || code <= 0 || code > 0xE07F || (code > 0x7F && (code >> 8) != 0xE0))
        { snprintf(badbuf, sizeof(badbuf), "%.*s", n > 20 ? 20 : n, s - n); *bad = badbuf; return 0; }
        Km[Kn].b = b; Km[Kn].code = (uint16_t)code; ++Kn;
    }
    for (i = 0; i < 4; ++i) End[i] = 0;
    Inited = 1;
    return 1;
}
