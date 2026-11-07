/* Virtual gameport for SBEMU: port 201h answers like an analog PC joystick, with the position and
 * buttons of the USB Xbox 360 pad(s). One pad: /JOYAX and /JOYBT give all four axes and buttons (joystick
 * B = its right stick by default). Two pads: pad 1 is joystick A and pad 2 joystick B, each with the first
 * two /JOYAX axes and the first two /JOYBT buttons. /JOYKEY works on either pad.
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
    case A_LX: v = dz(p->lx); if (p->buttons & XPAD_DL) v = -32768; if (p->buttons & XPAD_DR) v = 32767; break;
    case A_LY: v = -dz(p->ly); if (p->buttons & XPAD_DU) v = -32768; if (p->buttons & XPAD_DD) v = 32767; break;
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
static const USBPAD_State* pad_for(int i, int* m)
{
    const USBPAD_State* p2 = USBPAD_GetPad(1);
    if (p2->connected) { *m = i & 1; return i < 2 ? USBPAD_GetPad(0) : p2; }
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
            if (Ax[m] == A_NONE || !p->connected) { End[i] = ~0ULL; continue; } /* nothing connected: never times out */
            End[i] = T0 + Cps * (24 + (uint64_t)axis(m, p) * 1100 / 65535) / 1000000ULL;
        }
        return val;
    }
    else
    {
        uint64_t t = tsc();
        uint32_t r = 0xF0;
        for (i = 0; i < 4; ++i) if (t < End[i]) r |= 1u << i;
        for (i = 0; i < 4; ++i)
        {
            const USBPAD_State* p = pad_for(i, &m);
            if (p->connected && Bt[m] >= 0 && held(Bt[m], p)) r &= ~(0x10u << i);
        }
        return r;
    }
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

int VJOY_Init(uint64_t cps, const char* axes, const char* btns, const char* keys, int dzp, const char** bad)
{
    static char badbuf[24];
    const char* s;
    int i, n;
    *bad = NULL;
    Cps = cps ? cps : 1000000000ULL;
    Dz = dzp < 0 ? 0 : dzp > 90 ? 90 : dzp;
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
